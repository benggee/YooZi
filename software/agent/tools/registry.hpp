#pragma once

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include "tools/base_tool.hpp"
#include "schema/message.hpp"

namespace tools {

class Registry {
public:
    void registry(BaseTool* tool) {
        std::string n = tool->name();
        if (tools_.find(n) != tools_.end()) {
            std::cerr << "[Warning] tool: " << n << " is already registered" << std::endl;
        }
        tools_[n] = tool;
        std::cout << "[Registry] tool registered: " << n << std::endl;
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
            std::ostringstream oss;
            oss << "Error: tool not found: " << call.name;
            return schema::ToolResult{call.id, oss.str(), true};
        }

        std::string output;
        try {
            output = it->second->execute(call.args);
        } catch (const std::exception& e) {
            std::ostringstream oss;
            oss << "Error executing " << call.name << ": " << e.what();
            return schema::ToolResult{call.id, oss.str(), true};
        }

        return schema::ToolResult{call.id, output, false};
    }

private:
    std::map<std::string, BaseTool*> tools_;
};

} // namespace tools
