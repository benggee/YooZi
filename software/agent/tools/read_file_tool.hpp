#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

#include "tools/base_tool.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class ReadFileTool : public BaseTool {
public:
    explicit ReadFileTool(const std::string& work_dir) : work_dir_(work_dir) {}

    std::string name() const override {
        return "read_file";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "read_file",
            "Read the contents of the file at the specified path",
            R"({
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "File path to read"
                    }
                },
                "required": ["path"]
            })"
        };
    }

    std::string execute(const std::string& args_json) override {
        nlohmann::json args;
        try {
            args = nlohmann::json::parse(args_json);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to parse args: ") + e.what());
        }

        std::string path = args.value("path", std::string());

        std::string full_path = work_dir_;
        if (!full_path.empty() && full_path[full_path.size() - 1] != '/') {
            full_path += '/';
        }
        full_path += path;

        std::ifstream file(full_path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + full_path);
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        const size_t max_len = 8000;
        if (content.size() > max_len) {
            content = content.substr(0, max_len)
                + "\n\n...[Content truncated to first "
                + std::to_string(max_len) + " bytes]...";
        }

        return content;
    }

private:
    std::string work_dir_;
};

} // namespace tools
