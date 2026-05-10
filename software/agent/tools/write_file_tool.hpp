#pragma once

#include <string>
#include <fstream>
#include <cstdlib>

#include "tools/base_tool.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class WriteFileTool : public BaseTool {
public:
    explicit WriteFileTool(const std::string& work_dir) : work_dir_(work_dir) {}

    std::string name() const override {
        return "write_file";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "write_file",
            "创建或覆盖写入一个文件，如果目录不存在会自动创建。请提供相对于工作区的相对路径",
            R"({
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "要写入的文件路径，如src/main.go"
                    },
                    "content": {
                        "type": "string",
                        "description": "要写入的完整文件内容"
                    }
                },
                "required": ["path", "content"]
            })"
        };
    }

    std::string execute(const std::string& args_json) override {
        nlohmann::json args;
        try {
            args = nlohmann::json::parse(args_json);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("参数解析失败: ") + e.what());
        }

        std::string path = args.value("path", std::string());
        std::string content = args.value("content", std::string());

        if (path.empty()) {
            throw std::runtime_error("path is required");
        }

        std::string full_path = work_dir_;
        if (!full_path.empty() && full_path[full_path.size() - 1] != '/') {
            full_path += '/';
        }
        full_path += path;

        // Create parent directories
        std::string parent_dir;
        size_t last_slash = full_path.rfind('/');
        if (last_slash != std::string::npos) {
            parent_dir = full_path.substr(0, last_slash);
        }
        if (!parent_dir.empty()) {
            std::string cmd = "mkdir -p \"" + parent_dir + "\"";
            int ret = std::system(cmd.c_str());
            if (ret != 0) {
                throw std::runtime_error("创建父目录失败: " + parent_dir);
            }
        }

        std::ofstream file(full_path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("写入文件失败：" + full_path);
        }
        file << content;
        file.close();

        if (file.fail()) {
            throw std::runtime_error("写入文件失败：" + full_path);
        }

        return "成功将内容写入到文件：" + path;
    }

private:
    std::string work_dir_;
};

} // namespace tools
