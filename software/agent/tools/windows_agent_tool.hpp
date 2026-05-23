#pragma once

#include <string>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <chrono>

#include "tools/base_tool.hpp"
#include "ws/ws_server.hpp"
#include "vendor/nlohmann/json.hpp"
#include "common/logger.hpp"

namespace tools {

class WindowsAgentTool : public BaseTool {
public:
    explicit WindowsAgentTool(ws::WSServer* server) : server_(server) {}

    std::string name() const override {
        return "windows_agent";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "windows_agent",
            "向 Windows 电脑发送控制指令，可以打开程序、执行命令。当用户要求操作电脑时使用此工具。",
            R"({
                "type": "object",
                "properties": {
                    "type": {
                        "type": "string",
                        "enum": ["cmd", "natural_language"],
                        "description": "指令类型：cmd=固定指令, natural_language=自然语言"
                    },
                    "action": {
                        "type": "string",
                        "description": "cmd模式的操作动词，如 open/run/close"
                    },
                    "param": {
                        "type": "string",
                        "description": "cmd模式的目标程序或命令参数"
                    },
                    "text": {
                        "type": "string",
                        "description": "natural_language模式的用户原始文本"
                    }
                },
                "required": ["type"]
            })"
        };
    }

    std::string execute(const std::string& args_json) override {
        if (!server_->hasConnectedClient()) {
            return "Error: 没有 Windows 客户端连接，无法执行指令";
        }

        nlohmann::json args;
        try {
            args = nlohmann::json::parse(args_json);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("参数解析失败: ") + e.what());
        }

        std::string type = args.value("type", std::string());
        if (type.empty()) {
            throw std::runtime_error("type is required");
        }

        // Build request JSON
        nlohmann::json request;
        std::string request_id = generateRequestId();
        request["type"] = type;
        request["request_id"] = request_id;

        if (type == "cmd") {
            request["action"] = args.value("action", std::string());
            request["param"] = args.value("param", std::string());
        } else if (type == "natural_language") {
            request["text"] = args.value("text", std::string());
        } else {
            throw std::runtime_error("Unknown type: " + type);
        }

        // Register pending response
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_.try_emplace(request_id);
        }

        // Send request
        std::string msg = request.dump();
        if (!server_->broadcast(msg)) {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_.erase(request_id);
            return "Error: 发送指令失败";
        }

        logger::info("WindowsAgentTool", "Sent request: " + request_id);

        // Wait for response with 30s timeout
        {
            std::unique_lock<std::mutex> lock(pending_mutex_);
            auto it = pending_.find(request_id);
            if (it != pending_.end()) {
                if (it->second.cv.wait_for(lock, std::chrono::seconds(30),
                        [&]() { return it->second.received; })) {
                    std::string output = it->second.output;
                    bool success = it->second.success;
                    std::string error = it->second.error;
                    pending_.erase(request_id);

                    if (success) {
                        return output.empty() ? "执行成功" : output;
                    } else {
                        return "执行失败: " + (error.empty() ? "未知错误" : error);
                    }
                } else {
                    pending_.erase(request_id);
                    return "Error: 等待 Windows 响应超时 (30s)";
                }
            }
        }

        return "Error: 内部错误";
    }

    // Called from WSServer message callback to deliver response
    void handleResponse(const std::string& client_id, const std::string& message) {
        try {
            nlohmann::json msg = nlohmann::json::parse(message);
            if (msg.value("type", std::string()) != "result") return;

            std::string request_id = msg.value("request_id", std::string());
            if (request_id.empty()) return;

            std::lock_guard<std::mutex> lock(pending_mutex_);
            auto it = pending_.find(request_id);
            if (it != pending_.end()) {
                it->second.success = msg.value("success", false);
                it->second.output = msg.value("output", std::string());
                it->second.error = msg.value("error", std::string());
                it->second.received = true;
                it->second.cv.notify_one();
            }
        } catch (const std::exception& e) {
            logger::warn("WindowsAgentTool", std::string("Failed to parse response: ") + e.what());
        }
    }

private:
    struct PendingResponse {
        bool received = false;
        bool success = false;
        std::string output;
        std::string error;
        std::condition_variable cv;
    };

    static std::string generateRequestId() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return "req_" + std::to_string(now) + "_" + std::to_string(counter++);
    }

    ws::WSServer* server_;
    std::mutex pending_mutex_;
    std::unordered_map<std::string, PendingResponse> pending_;
};

} // namespace tools
