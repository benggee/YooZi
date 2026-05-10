#pragma once

#include <string>

namespace anthropic {

struct ToolChoiceUnionParam {
    enum Kind { AUTO, ANY, TOOL, NONE };
    Kind kind = AUTO;
    std::string tool_name;

    static ToolChoiceUnionParam auto_choice() {
        ToolChoiceUnionParam tc;
        tc.kind = AUTO;
        return tc;
    }

    static ToolChoiceUnionParam any_choice() {
        ToolChoiceUnionParam tc;
        tc.kind = ANY;
        return tc;
    }

    static ToolChoiceUnionParam tool(const std::string& name) {
        ToolChoiceUnionParam tc;
        tc.kind = TOOL;
        tc.tool_name = name;
        return tc;
    }

    static ToolChoiceUnionParam none_choice() {
        ToolChoiceUnionParam tc;
        tc.kind = NONE;
        return tc;
    }
};

} // namespace anthropic
