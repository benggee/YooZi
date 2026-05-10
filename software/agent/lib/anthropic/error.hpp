#pragma once

#include <stdexcept>
#include <string>

namespace anthropic {

struct AnthropicError : public std::runtime_error {
    int status_code;
    std::string body;

    AnthropicError(const std::string& what, int code = 0, std::string b = "")
        : std::runtime_error(what), status_code(code), body(std::move(b)) {}
};

struct AuthenticationError : public AnthropicError {
    AuthenticationError(const std::string& what, const std::string& body = "")
        : AnthropicError(what, 401, body) {}
};

struct BadRequestError : public AnthropicError {
    BadRequestError(const std::string& what, const std::string& body = "")
        : AnthropicError(what, 400, body) {}
};

struct RateLimitError : public AnthropicError {
    RateLimitError(const std::string& what, const std::string& body = "")
        : AnthropicError(what, 429, body) {}
};

struct NotFoundError : public AnthropicError {
    NotFoundError(const std::string& what, const std::string& body = "")
        : AnthropicError(what, 404, body) {}
};

struct OverloadedError : public AnthropicError {
    OverloadedError(const std::string& what, const std::string& body = "")
        : AnthropicError(what, 529, body) {}
};

struct ApiError : public AnthropicError {
    ApiError(const std::string& what, int code, const std::string& body = "")
        : AnthropicError(what, code, body) {}
};

} // namespace anthropic
