#pragma once

#include <string>
#include <vector>

namespace anthropic {

struct ContentBlockParamUnion;

struct MessageParam {
    std::string role;
    std::vector<ContentBlockParamUnion> content;

    static MessageParam user(const std::vector<ContentBlockParamUnion>& blocks);
    static MessageParam user_text(const std::string& text);
    static MessageParam assistant(const std::vector<ContentBlockParamUnion>& blocks);
};

} // namespace anthropic
