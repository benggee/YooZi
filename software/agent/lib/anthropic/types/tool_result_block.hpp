#pragma once

#include <string>

namespace anthropic {

struct ToolResultBlockParam {
    std::string tool_use_id;
    std::string content;
    bool is_error = false;
};

} // namespace anthropic
