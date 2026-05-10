#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include "schema/message.hpp"

namespace provider {

struct LLMProvider {
    virtual ~LLMProvider() {}

    virtual schema::Message generate(
        const std::vector<schema::Message>& messages,
        const std::vector<schema::ToolDefinition>& available_tools) = 0;
};

} // namespace provider
