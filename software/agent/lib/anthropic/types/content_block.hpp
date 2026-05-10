#pragma once

#include <string>
#include "text_block.hpp"
#include "tool_use_block.hpp"
#include "tool_result_block.hpp"
#include "vendor/nlohmann/json.hpp"

namespace anthropic {

// Response content block — discriminated by `type` field
struct ContentBlockUnion {
    std::string type;

    // text
    std::string text;

    // tool_use
    std::string id;
    std::string name;
    nlohmann::json input;

    bool is_text() const { return type == "text"; }
    bool is_tool_use() const { return type == "tool_use"; }
};

// Request content block — tagged union
struct ContentBlockParamUnion {
    enum Kind { TEXT, TOOL_USE, TOOL_RESULT };
    Kind kind;

    TextBlockParam text_block;
    ToolUseBlockParam tool_use_block;
    ToolResultBlockParam tool_result_block;

    static ContentBlockParamUnion text(const std::string& t) {
        ContentBlockParamUnion u;
        u.kind = TEXT;
        u.text_block.text = t;
        return u;
    }

    static ContentBlockParamUnion tool_use(const std::string& id,
                                           const std::string& name,
                                           const nlohmann::json& input) {
        ContentBlockParamUnion u;
        u.kind = TOOL_USE;
        u.tool_use_block.id = id;
        u.tool_use_block.name = name;
        u.tool_use_block.input = input;
        return u;
    }

    static ContentBlockParamUnion tool_result(const std::string& tool_use_id,
                                              const std::string& content,
                                              bool is_error = false) {
        ContentBlockParamUnion u;
        u.kind = TOOL_RESULT;
        u.tool_result_block.tool_use_id = tool_use_id;
        u.tool_result_block.content = content;
        u.tool_result_block.is_error = is_error;
        return u;
    }
};

} // namespace anthropic
