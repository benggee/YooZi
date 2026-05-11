#pragma once

#include <string>
#include <vector>
#include <ctime>

#include "provider/provider.hpp"
#include "tools/registry.hpp"
#include "schema/message.hpp"
#include "common/logger.hpp"

namespace engine {

class AgentEngine {
public:
    AgentEngine(provider::LLMProvider* p,
                tools::Registry* r,
                const std::string& work_dir,
                bool enable_thinking)
        : provider_(p)
        , registry_(r)
        , work_dir_(work_dir)
        , enable_thinking_(enable_thinking) {}

    void run(const std::string& user_prompt) {
        logger::info("AgentEngine", "started with [path:" + work_dir_ + "] prompt: " + user_prompt);
        logger::info("AgentEngine", std::string("thinking mode: ") + (enable_thinking_ ? "true" : "false"));

        std::vector<schema::Message> context_history;
        context_history.push_back(schema::Message{
            schema::RoleSystem,
            "You are YooZ, an expert coding assistant. You have full access to tools in the workspace.",
            {}, ""
        });
        context_history.push_back(schema::Message{
            schema::RoleUser,
            user_prompt,
            {}, ""
        });

        int turn_count = 0;

        while (true) {
            turn_count++;
            logger::info("AgentEngine", "============[Turn " + std::to_string(turn_count) + "] start ============");

            std::vector<schema::ToolDefinition> available_tools = registry_->get_available_tools();

            if (enable_thinking_) {
                logger::info("AgentEngine", "thinking phase (tools disabled)...");

                schema::Message think_resp;
                try {
                    think_resp = provider_->generate(context_history, std::vector<schema::ToolDefinition>());
                } catch (const std::exception& e) {
                    logger::error("AgentEngine", std::string("Thinking phase failed: ") + e.what());
                    return;
                }

                if (!think_resp.content.empty()) {
                    logger::info("AgentEngine", "Thinking trace: " + think_resp.content);
                    context_history.push_back(think_resp);
                }
            }

            logger::info("AgentEngine", "action phase (tools enabled)...");

            schema::Message action_resp;
            try {
                action_resp = provider_->generate(context_history, available_tools);
            } catch (const std::exception& e) {
                logger::error("AgentEngine", std::string("LLM generation error: ") + e.what());
                return;
            }

            context_history.push_back(action_resp);

            if (!action_resp.content.empty()) {
                logger::info("AgentEngine", "Provider response: " + action_resp.content);
            }

            if (action_resp.tool_calls.empty()) {
                break;
            }

            logger::info("AgentEngine", "Model requested " + std::to_string(action_resp.tool_calls.size()) + " tool call(s)");

            for (const auto& tool_call : action_resp.tool_calls) {
                logger::info("AgentEngine", "Execute tool: " + tool_call.name + " Args: " + tool_call.args);

                schema::ToolResult result = registry_->execute(tool_call);

                if (result.is_error) {
                    logger::error("AgentEngine", "Tool error: " + result.output);
                } else {
                    logger::info("AgentEngine", "Tool success, output length: " + std::to_string(result.output.size()));
                }

                schema::Message observation;
                observation.role = schema::RoleUser;
                observation.content = result.output;
                observation.tool_call_id = tool_call.id;
                context_history.push_back(observation);
            }
        }
    }

private:
    provider::LLMProvider* provider_;
    tools::Registry* registry_;
    std::string work_dir_;
    bool enable_thinking_;
};

} // namespace engine
