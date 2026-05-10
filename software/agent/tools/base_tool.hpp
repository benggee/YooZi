#pragma once

#include <string>
#include "schema/message.hpp"

namespace tools {

struct BaseTool {
    virtual ~BaseTool() {}

    virtual std::string name() const = 0;
    virtual schema::ToolDefinition definition() const = 0;
    virtual std::string execute(const std::string& args_json) = 0;
};

} // namespace tools
