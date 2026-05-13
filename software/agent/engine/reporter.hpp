#pragma once

#include <string>

namespace engine {

struct Reporter {
    virtual ~Reporter() {}

    virtual void onThinking() = 0;
    virtual void onToolCall(const std::string& toolName, const std::string& args) = 0;
    virtual void onToolResult(const std::string& toolName, const std::string& result, bool isError) = 0;
    virtual void onMessage(const std::string& content) = 0;
};

} // namespace engine
