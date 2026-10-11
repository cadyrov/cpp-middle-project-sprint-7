#include "headers.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace {
using Headers = std::vector<std::pair<std::string, std::string>>;

Headers collectHeaders(std::string_view request) {
    Headers headers;
    iterHeaders(request, [&](std::string_view name, std::string_view value) { headers.emplace_back(name, value); });
    return headers;
}
}  // namespace

TEST(iterHeaders, Empty) { EXPECT_TRUE(collectHeaders("").empty()); }

TEST(iterHeaders, SkipRequestLine) {
    EXPECT_TRUE(collectHeaders("GET http://example.com:8080/ HTTP/1.1\r\n\r\n").empty());
}

TEST(iterHeaders, SingleHeader) {
    const Headers expected{{"Host", "example.com"}};
    EXPECT_EQ(collectHeaders("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"), expected);
}

TEST(iterHeaders, MultipleHeaders) {
    const Headers expected{{"Host", "example.com"}, {"Accept", "text/html"}, {"Connection", "close"}};
    EXPECT_EQ(collectHeaders("GET / HTTP/1.1\r\nHost: example.com\r\nAccept: text/html\r\n"
                             "Connection: close\r\n\r\n"),
              expected);
}

TEST(iterHeaders, MultipleSameHeaders) {
    const Headers expected{{"Set-Cookie", "first=1"}, {"Set-Cookie", "second=2"}};
    EXPECT_EQ(collectHeaders("HTTP/1.1 200 OK\r\nSet-Cookie: first=1\r\nSet-Cookie: second=2\r\n\r\n"), expected);
}

TEST(iterHeaders, TrimWhitespace) {
    const Headers expected{{"Host", "example.com"}, {"X-Empty", ""}, {"X-Url", "http://example.com:8080/"}};
    EXPECT_EQ(collectHeaders("GET / HTTP/1.1\r\nHost:\t example.com \t\r\nX-Empty: \t\r\n"
                             "X-Url: http://example.com:8080/\r\n\r\n"),
              expected);
}

TEST(iterHeaders, IgnoreBody) {
    const Headers expected{{"Content-Length", "19"}};
    EXPECT_EQ(collectHeaders("HTTP/1.1 200 OK\r\nContent-Length: 19\r\n\r\nHost: body.test\r\n\r\n"), expected);
}

TEST(findHostPort, Simple) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    EXPECT_EQ(host, "example.com");
    EXPECT_EQ(port, "80");
}

TEST(findHostPort, NoHost) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nAccept: text/html\r\n\r\n");
    EXPECT_TRUE(host.empty());
    EXPECT_EQ(port, "80");
}

TEST(findHostPort, ExplicitPortAndLowercaseName) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nhost: localhost:8080\r\n\r\n");
    EXPECT_EQ(host, "localhost");
    EXPECT_EQ(port, "8080");
}

TEST(findHostPort, IPv6) {
    const auto [host, port] = findHostPort("GET / HTTP/1.1\r\nHost: [::1]:8080\r\n\r\n");
    EXPECT_EQ(host, "::1");
    EXPECT_EQ(port, "8080");
}

TEST(findContentLength, Simple) {
    const auto length = findContentLength("HTTP/1.1 200 OK\r\nContent-Length: 4096\r\n\r\n");
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(*length, 4096u);
}

TEST(findContentLength, NoContentLength) {
    EXPECT_FALSE(findContentLength("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n").has_value());
}

TEST(findContentLength, ZeroAndMixedCaseName) {
    const auto length = findContentLength("HTTP/1.1 200 OK\r\ncOnTeNt-LeNgTh: 0\r\n\r\n");
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(*length, 0u);
}

TEST(findContentLength, InvalidValue) {
    for (const std::string value : {"", "-1", "abc", "42abc", "184467440737095516160"}) {
        SCOPED_TRACE(value);
        const auto response = "HTTP/1.1 200 OK\r\nContent-Length: " + value + "\r\n\r\n";
        EXPECT_THROW(findContentLength(response), std::invalid_argument);
    }
}

TEST(findContentLength, MatchingDuplicates) {
    const auto length = findContentLength("HTTP/1.1 200 OK\r\nContent-Length: 42\r\nContent-Length: 42\r\n\r\n");
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(*length, 42u);
}

TEST(findContentLength, ConflictingDuplicates) {
    EXPECT_THROW(findContentLength("HTTP/1.1 200 OK\r\nContent-Length: 42\r\nContent-Length: 43\r\n\r\n"),
                 std::invalid_argument);
}
