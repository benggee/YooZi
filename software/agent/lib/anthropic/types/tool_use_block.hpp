#pragma once

#include <string>
#include "vendor/nlohmann/json.hpp"

namespace anthropic {

struct ToolUseBlockParam {
    std::string id;
    std::string name;
    nlohmann::json input;
};

} // namespace anthropic
