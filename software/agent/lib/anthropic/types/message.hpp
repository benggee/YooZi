#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "content_block.hpp"
#include "message_param.hpp"
#include "text_block.hpp"
#include "tool.hpp"
#include "tool_choice.hpp"
#include "usage.hpp"

namespace anthropic {

struct MessageNewParams {
    std::string model;
    int64_t max_tokens = 4096;
    std::vector<MessageParam> messages;

    std::vector<TextBlockParam> system;
    std::vector<ToolUnionParam> tools;
    ToolChoiceUnionParam tool_choice;

    double temperature = 0;
    double top_p = 0;
    bool has_temperature = false;
    bool has_top_p = false;

    std::vector<std::string> stop_sequences;
};

struct Message {
    std::string id;
    std::string type;
    std::string role;
    std::string model;
    std::string stop_reason;
    std::string stop_sequence;
    std::vector<ContentBlockUnion> content;
    Usage usage;
};

} // namespace anthropic
