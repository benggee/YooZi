#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace schema {

enum Role {
    RoleSystem,
    RoleUser,
    RoleAssistant
};

inline const char* role_to_string(Role r) {
    switch (r) {
    case RoleSystem:    return "system";
    case RoleUser:      return "user";
    case RoleAssistant: return "assistant";
    }
    return "";
}

struct ToolCall {
    std::string id;
    std::string name;
    std::string args;   // raw JSON string
};

struct ContentPart {
    enum Type { TEXT, IMAGE_URL };
    Type type;
    std::string text;
    std::string image_url;
};

struct Message {
    Role role;
    std::string content;
    std::vector<ContentPart> content_parts;
    std::vector<ToolCall> tool_calls;
    std::string tool_call_id;
};

struct ToolResult {
    std::string tool_call_id;
    std::string output;
    bool is_error;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    std::string input_schema;   // JSON string
};

} // namespace schema
