#include "headers.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ranges>
#include <stdexcept>
#include <string_view>

using namespace std::string_view_literals;

namespace {
bool headerNameEquals(std::string_view name, std::string_view expected) {
    return std::ranges::equal(
        name, expected, [](unsigned char lhs, unsigned char rhs) { return std::tolower(lhs) == std::tolower(rhs); });
}
}  // namespace

void iterHeaders(std::string_view req, Callback &&callback) {
    auto headers_end = req.find("\r\n\r\n"sv);
    if (headers_end == std::string_view::npos) {
        headers_end = req.rfind("\r\n"sv);
    }
    if (headers_end == std::string_view::npos) {
        return;
    }
    req = req.substr(0, headers_end);

    auto lines = req | std::views::split("\r\n"sv) | std::views::drop(1) |
                 std::views::take_while([](const auto &line) { return !std::ranges::empty(line); });
    for (const auto part : lines) {
        const std::string_view line(part.begin(), part.end());

        const auto colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            continue;
        }

        const auto name = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        const auto value_begin = value.find_first_not_of(" \t"sv);
        if (value_begin == std::string_view::npos) {
            value = {};
        } else {
            const auto value_end = value.find_last_not_of(" \t"sv);
            value = value.substr(value_begin, value_end - value_begin + 1);
        }
        callback(name, value);
    }
}

std::pair<std::string, std::string> findHostPort(std::string_view req) {
    std::pair<std::string, std::string> result{"", "80"};
    iterHeaders(req, [&](std::string_view name, std::string_view value) {
        if (!headerNameEquals(name, "Host"sv) || !result.first.empty()) {
            return;
        }
        if (value.starts_with('[')) {
            const auto bracket = value.find(']');
            if (bracket == std::string_view::npos) {
                throw std::invalid_argument("Invalid Host header");
            }
            result.first = value.substr(1, bracket - 1);
            if (bracket + 1 < value.size()) {
                if (value[bracket + 1] != ':' || bracket + 2 == value.size()) {
                    throw std::invalid_argument("Invalid Host port");
                }
                result.second = value.substr(bracket + 2);
            }
        } else {
            const auto colon = value.find(':');
            result.first = value.substr(0, colon);
            if (colon != std::string_view::npos) {
                if (colon + 1 == value.size()) {
                    throw std::invalid_argument("Invalid Host port");
                }
                result.second = value.substr(colon + 1);
            }
        }
    });
    return result;
}

std::optional<size_t> findContentLength(std::string_view rsp) {
    std::optional<size_t> result;
    iterHeaders(rsp, [&](std::string_view name, std::string_view value) {
        if (!headerNameEquals(name, "Content-Length"sv)) {
            return;
        }
        if (value.empty()) {
            throw std::invalid_argument("Invalid Content-Length header");
        }
        size_t length = 0;
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), length);
        if (error != std::errc{} || end != value.data() + value.size() || (result && *result != length)) {
            throw std::invalid_argument("Invalid Content-Length header");
        }
        result = length;
    });
    return result;
}
