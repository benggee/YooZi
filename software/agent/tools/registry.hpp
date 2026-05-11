#pragma once

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include "tools/base_tool.hpp"
#include "schema/message.hpp"
#include "common/logger.hpp"

namespace tools {

class Registry {
public:
    void registry(BaseTool* tool) {
        std::string n = tool->name();
        if (tools_.find(n) != tools_.end()) {
            logger::warn("Registry", "tool already registered: " + n);
        }
        tools_[n] = tool;
        logger::info("Registry", "tool registered: " + n);
    }

    std::vector<schema::ToolDefinition> get_available_tools() const {
        std::vector<schema::ToolDefinition> defs;
        for (auto it = tools_.begin(); it != tools_.end(); ++it) {
            defs.push_back(it->second->definition());
        }
        return defs;
    }

    schema::ToolResult execute(const schema::ToolCall& call) {
        auto it = tools_.find(call.name);
        if (it == tools_.end()) {
            std::string err = "Error: tool not found: " + call.name;
            logger::error("Tool", err);
            return schema::ToolResult{call.id, err, true};
        }

        logger::info("Tool", "executing: " + call.name + " args: " + call.args);

        std::string output;
        try {
            output = it->second->execute(call.args);
        } catch (const std::exception& e) {
            std::string err = std::string("Error executing ") + call.name + ": " + e.what();
            logger::error("Tool", err);
            return schema::ToolResult{call.id, err, true};
        }

        logger::info("Tool", "result: " + call.name + " output_length: " + std::to_string(output.size()));
        logger::debug("Tool", "result: " + call.name + " output: " + output);

        return schema::ToolResult{call.id, output, false};
    }

private:
    std::map<std::string, BaseTool*> tools_;
};

} // namespace tools
