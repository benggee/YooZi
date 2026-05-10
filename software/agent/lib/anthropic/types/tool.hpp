#pragma once

#include <string>
#include <vector>
#include "vendor/nlohmann/json.hpp"

namespace anthropic {

struct ToolInputSchemaParam {
    nlohmann::json properties;
    std::vector<std::string> required;
};

struct ToolParam {
    std::string name;
    std::string description;
    ToolInputSchemaParam input_schema;
};

struct ToolUnionParam {
    ToolParam tool;
};

} // namespace anthropic
