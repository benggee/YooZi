#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>

#include "schema/message.hpp"
#include "provider/provider.hpp"
#include "anthropic/anthropic.hpp"

namespace provider {

class ClaudeProvider : public LLMProvider {
public:
    explicit ClaudeProvider(const std::string& model, const std::string& base_url = "")
        : model_(model) {
        std::string apiKey;
        const char* envKey = std::getenv("ZHIPU_API_KEY");
        if (!envKey) {
            envKey = std::getenv("ANTHROPIC_API_KEY");
        }
        if (!envKey) {
            throw std::runtime_error("ZHIPU_API_KEY or ANTHROPIC_API_KEY environment variable is not set");
        }
        apiKey = envKey;

        std::string url = base_url.empty()
            ? "https://open.bigmodel.cn/api/paas/v4/"
            : base_url;

        anthropic::ClientOptions opts;
        opts.api_key = apiKey;
        opts.base_url = url;
        client_ = new anthropic::Client(opts);
    }

    ~ClaudeProvider() {
        delete client_;
    }

    schema::Message generate(
        const std::vector<schema::Message>& messages,
        const std::vector<schema::ToolDefinition>& available_tools) override {

        std::vector<anthropic::MessageParam> anthropic_msgs;
        std::vector<anthropic::TextBlockParam> system_prompt;

        for (const auto& msg : messages) {
            switch (msg.role) {
            case schema::RoleSystem:
                system_prompt.push_back(anthropic::TextBlockParam{msg.content});
                break;

            case schema::RoleUser:
                if (!msg.tool_call_id.empty()) {
                    anthropic_msgs.push_back(anthropic::MessageParam::user({
                        anthropic::ContentBlockParamUnion::tool_result(
                            msg.tool_call_id, msg.content, false)
                    }));
                } else {
                    anthropic_msgs.push_back(
                        anthropic::MessageParam::user_text(msg.content));
                }
                break;

            case schema::RoleAssistant: {
                std::vector<anthropic::ContentBlockParamUnion> blocks;
                if (!msg.content.empty()) {
                    blocks.push_back(anthropic::ContentBlockParamUnion::text(msg.content));
                }
                for (const auto& tc : msg.tool_calls) {
                    nlohmann::json input;
                    try {
                        input = nlohmann::json::parse(tc.args);
                    } catch (...) {
                        input = nlohmann::json::object();
                    }
                    blocks.push_back(anthropic::ContentBlockParamUnion::tool_use(
                        tc.id, tc.name, input));
                }
                if (!blocks.empty()) {
                    anthropic_msgs.push_back(anthropic::MessageParam::assistant(blocks));
                }
                break;
            }
            }
        }

        // Build tool definitions
        std::vector<anthropic::ToolUnionParam> anthropic_tools;
        for (const auto& td : available_tools) {
            nlohmann::json schema_json;
            try {
                schema_json = nlohmann::json::parse(td.input_schema);
            } catch (...) {
                schema_json = nlohmann::json::object();
            }

            nlohmann::json properties = schema_json.value("properties", nlohmann::json::object());
            std::vector<std::string> required;
            if (schema_json.count("required") && schema_json["required"].is_array()) {
                for (const auto& r : schema_json["required"]) {
                    required.push_back(r.get<std::string>());
                }
            }

            anthropic_tools.push_back(anthropic::ToolUnionParam{
                anthropic::ToolParam{
                    td.name,
                    td.description,
                    anthropic::ToolInputSchemaParam{properties, required}
                }
            });
        }

        // Build request
        anthropic::MessageNewParams params;
        params.model = model_;
        params.max_tokens = 4096;
        params.messages = anthropic_msgs;

        if (!system_prompt.empty()) {
            params.system = system_prompt;
        }
        if (!anthropic_tools.empty()) {
            params.tools = anthropic_tools;
        }

        // Execute
        anthropic::Message resp = client_->messages().New(params);

        // Parse response
        schema::Message result;
        result.role = schema::RoleAssistant;

        for (const auto& block : resp.content) {
            if (block.is_text()) {
                result.content += block.text;
            } else if (block.is_tool_use()) {
                result.tool_calls.push_back(schema::ToolCall{
                    block.id,
                    block.name,
                    block.input.dump()
                });
            }
        }

        return result;
    }

private:
    std::string model_;
    anthropic::Client* client_;
};

} // namespace provider
