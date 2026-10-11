#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

using Callback = std::function<void(std::string_view, std::string_view)>;

void iterHeaders(std::string_view req, Callback &&callback);

std::pair<std::string, std::string> findHostPort(std::string_view req);

std::optional<size_t> findContentLength(std::string_view rsp);
