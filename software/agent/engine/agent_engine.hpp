#pragma once

#include <string>
#include <vector>
#include <ctime>

#include "provider/provider.hpp"
#include "tools/registry.hpp"
#include "schema/message.hpp"
#include "engine/reporter.hpp"
#include "context/composer.hpp"
#include "common/logger.hpp"

namespace engine {

class AgentEngine {
public:
    AgentEngine(provider::LLMProvider* p,
                tools::Registry* r,
                const std::string& work_dir,
                bool enable_thinking,
                Reporter* reporter = nullptr)
        : provider_(p)
        , registry_(r)
        , work_dir_(work_dir)
        , enable_thinking_(enable_thinking)
        , reporter_(reporter)
        , composer_(new context::PromptComposer(work_dir)) {}

    ~AgentEngine() {
        delete composer_;
    }

    void run(const std::string& user_prompt) {
        logger::info("AgentEngine", "started with [path:" + work_dir_ + "] prompt: " + user_prompt);
        logger::info("AgentEngine", std::string("thinking mode: ") + (enable_thinking_ ? "true" : "false"));

        std::vector<schema::Message> context_history;
        context_history.push_back(composer_->build());
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
                if (reporter_) reporter_->onThinking();

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
                if (reporter_) reporter_->onMessage(action_resp.content);
            }

            if (action_resp.tool_calls.empty()) {
                break;
            }

            logger::info("AgentEngine", "Model requested " + std::to_string(action_resp.tool_calls.size()) + " tool call(s)");

            for (const auto& tool_call : action_resp.tool_calls) {
                if (reporter_) reporter_->onToolCall(tool_call.name, tool_call.args);

                logger::info("AgentEngine", "Execute tool: " + tool_call.name + " Args: " + tool_call.args);

                schema::ToolResult result = registry_->execute(tool_call);

                if (reporter_) {
                    std::string display = result.output;
                    if (display.size() > 200) {
                        display = display.substr(0, 200) + "...(truncated)";
                    }
                    reporter_->onToolResult(tool_call.name, display, result.is_error);
                }

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
    Reporter* reporter_;
    context::PromptComposer* composer_;
};

} // namespace engine
