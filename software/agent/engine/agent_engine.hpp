#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <ctime>

#include "provider/provider.hpp"
#include "tools/registry.hpp"
#include "schema/message.hpp"

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
        std::cout << "Agent Engine started with [path:" << work_dir_
                  << "] and prompt: " << user_prompt << std::endl;
        std::cout << "Agent Engine thinking mode: "
                  << (enable_thinking_ ? "true" : "false") << std::endl;

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
            std::cout << "============[Turn " << turn_count << "] start ============" << std::endl;

            std::vector<schema::ToolDefinition> available_tools = registry_->get_available_tools();

            if (enable_thinking_) {
                std::cout << "Agent Engine thinking phase (tools disabled)..." << std::endl;

                schema::Message think_resp;
                try {
                    think_resp = provider_->generate(context_history, std::vector<schema::ToolDefinition>());
                } catch (const std::exception& e) {
                    std::cerr << "Thinking phase failed: " << e.what() << std::endl;
                    return;
                }

                if (!think_resp.content.empty()) {
                    std::cout << "Thinking trace: " << think_resp.content << std::endl;
                    context_history.push_back(think_resp);
                }
            }

            std::cout << "Agent Engine action phase (tools enabled)..." << std::endl;

            schema::Message action_resp;
            try {
                action_resp = provider_->generate(context_history, available_tools);
            } catch (const std::exception& e) {
                std::cerr << "LLM generation error: " << e.what() << std::endl;
                return;
            }

            context_history.push_back(action_resp);

            if (!action_resp.content.empty()) {
                std::cout << "Provider response: " << action_resp.content << std::endl;
            }

            if (action_resp.tool_calls.empty()) {
                break;
            }

            std::cout << "Model requested " << action_resp.tool_calls.size() << " tool call(s)" << std::endl;

            for (const auto& tool_call : action_resp.tool_calls) {
                std::cout << "Execute tool: " << tool_call.name
                          << ", Args: " << tool_call.args << std::endl;

                schema::ToolResult result = registry_->execute(tool_call);

                if (result.is_error) {
                    std::cerr << "Tool error: " << result.output << std::endl;
                } else {
                    std::cout << "Tool success, output length: " << result.output.size() << std::endl;
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
