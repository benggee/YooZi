#pragma once

#include "types/message.hpp"
#include "types/content_block.hpp"
#include "types/usage.hpp"
#include "../vendor/nlohmann/json.hpp"

namespace anthropic {

// --- Usage ---
inline void from_json(const nlohmann::json& j, Usage& u) {
    u.input_tokens = j.value("input_tokens", int64_t(0));
    u.output_tokens = j.value("output_tokens", int64_t(0));
    u.cache_creation_input_tokens = j.value("cache_creation_input_tokens", int64_t(0));
    u.cache_read_input_tokens = j.value("cache_read_input_tokens", int64_t(0));
}

// --- ContentBlockUnion (response) ---
inline void from_json(const nlohmann::json& j, ContentBlockUnion& v) {
    j.at("type").get_to(v.type);
    if (v.type == "text") {
        j.at("text").get_to(v.text);
    } else if (v.type == "tool_use") {
        j.at("id").get_to(v.id);
        j.at("name").get_to(v.name);
        v.input = j.value("input", nlohmann::json::object());
    }
}

// --- ContentBlockParamUnion (request) ---
inline void to_json(nlohmann::json& j, const ContentBlockParamUnion& v) {
    switch (v.kind) {
    case ContentBlockParamUnion::TEXT:
        j = nlohmann::json{{"type", "text"}, {"text", v.text_block.text}};
        break;
    case ContentBlockParamUnion::TOOL_USE:
        j = nlohmann::json{
            {"type", "tool_use"},
            {"id", v.tool_use_block.id},
            {"name", v.tool_use_block.name},
            {"input", v.tool_use_block.input}
        };
        break;
    case ContentBlockParamUnion::TOOL_RESULT: {
        nlohmann::json content_arr = nlohmann::json::array({
            {{"type", "text"}, {"text", v.tool_result_block.content}}
        });
        j = nlohmann::json{
            {"type", "tool_result"},
            {"tool_use_id", v.tool_result_block.tool_use_id},
            {"content", content_arr}
        };
        if (v.tool_result_block.is_error) {
            j["is_error"] = true;
        }
        break;
    }
    }
}

// --- TextBlockParam ---
inline void to_json(nlohmann::json& j, const TextBlockParam& v) {
    j = nlohmann::json{{"type", "text"}, {"text", v.text}};
}

// --- ToolInputSchemaParam ---
inline void to_json(nlohmann::json& j, const ToolInputSchemaParam& v) {
    j = nlohmann::json{{"type", "object"}};
    if (!v.properties.is_null()) {
        j["properties"] = v.properties;
    }
    if (!v.required.empty()) {
        j["required"] = v.required;
    }
}

// --- ToolParam ---
inline void to_json(nlohmann::json& j, const ToolParam& v) {
    j = nlohmann::json{
        {"name", v.name},
        {"description", v.description},
        {"input_schema", v.input_schema}
    };
}

// --- ToolUnionParam ---
inline void to_json(nlohmann::json& j, const ToolUnionParam& v) {
    to_json(j, v.tool);
}

// --- ToolChoiceUnionParam ---
inline void to_json(nlohmann::json& j, const ToolChoiceUnionParam& v) {
    switch (v.kind) {
    case ToolChoiceUnionParam::AUTO:
        j = nlohmann::json{{"type", "auto"}};
        break;
    case ToolChoiceUnionParam::ANY:
        j = nlohmann::json{{"type", "any"}};
        break;
    case ToolChoiceUnionParam::TOOL:
        j = nlohmann::json{{"type", "tool"}, {"name", v.tool_name}};
        break;
    case ToolChoiceUnionParam::NONE:
        j = nlohmann::json{{"type", "none"}};
        break;
    }
}

// --- MessageParam ---
inline void to_json(nlohmann::json& j, const MessageParam& v) {
    j = nlohmann::json{{"role", v.role}, {"content", v.content}};
}

// --- MessageParam factory implementations (need ContentBlockParamUnion) ---
inline MessageParam MessageParam::user(const std::vector<ContentBlockParamUnion>& blocks) {
    MessageParam m;
    m.role = "user";
    m.content = blocks;
    return m;
}

inline MessageParam MessageParam::user_text(const std::string& text) {
    MessageParam m;
    m.role = "user";
    m.content.push_back(ContentBlockParamUnion::text(text));
    return m;
}

inline MessageParam MessageParam::assistant(const std::vector<ContentBlockParamUnion>& blocks) {
    MessageParam m;
    m.role = "assistant";
    m.content = blocks;
    return m;
}

// --- Message (response) ---
inline void from_json(const nlohmann::json& j, Message& m) {
    j.at("id").get_to(m.id);
    j.value("type", std::string("message")).swap(m.type);
    j.value("role", std::string("assistant")).swap(m.role);
    j.value("model", std::string()).swap(m.model);
    j.value("stop_reason", std::string()).swap(m.stop_reason);
    j.value("stop_sequence", std::string()).swap(m.stop_sequence);
    if (j.count("content") && j["content"].is_array()) {
        m.content = j["content"].get<std::vector<ContentBlockUnion>>();
    }
    if (j.count("usage")) {
        m.usage = j["usage"].get<Usage>();
    }
}

// --- MessageNewParams (request) ---
inline void to_json(nlohmann::json& j, const MessageNewParams& p) {
    j = nlohmann::json{
        {"model", p.model},
        {"max_tokens", p.max_tokens},
        {"messages", p.messages}
    };
    if (!p.system.empty()) {
        j["system"] = p.system;
    }
    if (!p.tools.empty()) {
        j["tools"] = p.tools;
    }
    if (p.tool_choice.kind == ToolChoiceUnionParam::TOOL) {
        j["tool_choice"] = p.tool_choice;
    } else if (p.tool_choice.kind != ToolChoiceUnionParam::AUTO && p.tool_choice.kind != ToolChoiceUnionParam::NONE) {
        j["tool_choice"] = p.tool_choice;
    } else if (p.tool_choice.kind == ToolChoiceUnionParam::NONE) {
        j["tool_choice"] = p.tool_choice;
    }
    if (p.has_temperature) {
        j["temperature"] = p.temperature;
    }
    if (p.has_top_p) {
        j["top_p"] = p.top_p;
    }
    if (!p.stop_sequences.empty()) {
        j["stop_sequences"] = p.stop_sequences;
    }
}

} // namespace anthropic
