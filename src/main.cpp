#include "headers.h"

#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

using boost::asio::async_read_until;
using boost::asio::awaitable;
using boost::asio::buffer;
using boost::asio::co_spawn;
using boost::asio::dynamic_buffer;
using boost::asio::io_service;
using boost::asio::transfer_at_least;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;
using boost::system::error_code;

constexpr std::string_view delimiter = "\r\n\r\n";

awaitable<void> session(tcp::socket client_socket, io_service &io_service) {
    try {
        std::string request;
        const auto request_header_size =
            co_await async_read_until(client_socket, dynamic_buffer(request), delimiter, use_awaitable);
        const auto [host, port] = findHostPort(std::string_view(request).substr(0, request_header_size));
        if (host.empty()) {
            throw std::invalid_argument("Missing Host header");
        }

        tcp::resolver resolver(io_service);
        const auto endpoints = co_await resolver.async_resolve(host, port, use_awaitable);
        tcp::socket site_socket(io_service);
        co_await boost::asio::async_connect(site_socket, endpoints, use_awaitable);

        // Convert the absolute HTTP proxy target to the site's request target.
        const auto target_begin = request.find(' ');
        if (target_begin != std::string::npos && request.compare(target_begin + 1, 7, "http://") == 0) {
            const auto target_end = request.find(' ', target_begin + 1);
            if (target_end == std::string::npos) {
                throw std::invalid_argument("Invalid HTTP request line");
            }
            const auto path_begin = request.find_first_of("/?", target_begin + 8);
            std::string target = "/";
            if (path_begin != std::string::npos && path_begin < target_end) {
                target = request.substr(path_begin, target_end - path_begin);
                if (target.front() == '?') {
                    target.insert(0, "/");
                }
            }
            request.replace(target_begin + 1, target_end - target_begin - 1, target);
        }
        co_await boost::asio::async_write(site_socket, buffer(request), use_awaitable);

        std::string response;
        const auto response_header_size =
            co_await async_read_until(site_socket, dynamic_buffer(response), delimiter, use_awaitable);
        const auto content_length = findContentLength(std::string_view(response).substr(0, response_header_size));
        if (!content_length) {
            throw std::invalid_argument("Missing Content-Length header");
        }

        // read_until may also receive part of the body together with the headers.
        const auto buffered_body_size = std::min(response.size() - response_header_size, *content_length);
        co_await boost::asio::async_write(
            client_socket, buffer(response.data(), response_header_size + buffered_body_size), use_awaitable);

        size_t remaining = *content_length - buffered_body_size;
        std::array<char, 8192> body;
        while (remaining > 0) {
            const auto received = co_await site_socket.async_read_some(
                buffer(body.data(), std::min(body.size(), remaining)), use_awaitable);
            co_await boost::asio::async_write(client_socket, buffer(body.data(), received), use_awaitable);
            remaining -= received;
        }
    } catch (const std::exception &error) {
        std::cerr << "Session error: " << error.what() << '\n';
    }
}

class Server {
public:
    Server(io_service &io_service, short port)
        : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)), socket_(io_service) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(socket_, [this](error_code ec) {
            if (!ec) {
                co_spawn(io_service_, session(std::move(socket_), io_service_), boost::asio::detached);
            }
            do_accept();
        });
    }

    io_service &io_service_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
};

int main(int argc, char *argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: proxy_server";
            std::cerr << " <listen_port>\n";
            return 1;
        }
        io_service io_service(1);
        Server server(io_service, std::atoi(argv[1]));
        io_service.run();

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
}
