#pragma once

#include <string>
#include <cstdlib>

#include "tools/base_tool.hpp"
#include "anthropic/http/http_client.hpp"
#include "vendor/nlohmann/json.hpp"
#include "common/logger.hpp"

namespace tools {

class HermesTool : public BaseTool {
public:
    HermesTool()
        : webhook_url_("http://127.0.0.1:8644/webhooks/yoozi")
        , webhook_token_("xxxx") {
        const char* url = std::getenv("HERMES_WEBHOOK_URL");
        if (url && url[0] != '\0') {
            webhook_url_ = url;
        }
        const char* token = std::getenv("HERMES_WEBHOOK_TOKEN");
        if (token && token[0] != '\0') {
            webhook_token_ = token;
        }
        http_client_.set_timeout(15);
        http_client_.set_connect_timeout(5);
        http_client_.set_noproxy(true);
    }

    std::string name() const override {
        return "hermes_tool";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "hermes_tool",
            "操控 Windows 电脑的通用工具。当用户请求涉及电脑操作时使用此工具，不要用 bash 代替。"
            "适用场景：播放音乐、打开/关闭程序、搜索网页、打开代码文件、文件管理、系统设置等。"
            "bash 只能操作树莓派本机，不能操作 Windows 电脑。",
            R"({
                "type": "object",
                "properties": {
                    "intent": {
                        "type": "string",
                        "description": "提炼后的操作指令，描述要在电脑上执行的具体操作。例如：'播放周杰伦的歌曲'、'打开Chrome浏览器搜索今天的天气'、'打开VSCode的work.workspace工作区，找到并打开文件main.py'、'搜索桌面上包含报告的文档'、'在浏览器打开GitHub'"
                    },
                    "category": {
                        "type": "string",
                        "enum": ["music", "application", "document", "code", "browser", "file_management", "system", "other"],
                        "description": "操作类别：music=音乐播放, application=打开/关闭程序, document=文档搜索与操作, code=代码相关, browser=浏览器, file_management=文件管理, system=系统操作, other=其他"
                    }
                },
                "required": ["intent"]
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

        std::string intent = args.value("intent", std::string());
        if (intent.empty()) {
            throw std::runtime_error("intent is required");
        }

        std::string category = args.value("category", "other");

        // Build request body
        nlohmann::json body;
        body["message"] = intent;

        std::string body_str = body.dump();

        // Build headers
        std::vector<std::pair<std::string, std::string>> headers;
        headers.push_back({"Content-Type", "application/json"});
        headers.push_back({"X-Gitlab-Token", webhook_token_});
        headers.push_back({"X-Gitlab-Event", "test"});

        logger::info("HermesTool", "POST " + webhook_url_ + " body: " + body_str);

        anthropic::http::HttpResponse resp = http_client_.post(webhook_url_, body_str, headers);

        logger::info("HermesTool", "Response: " + std::to_string(resp.status_code) + " " + resp.body);

        nlohmann::json result;
        result["category"] = category;
        if (resp.status_code == 0) {
            result["status"] = "error";
            result["message"] = "网络错误，无法连接到电脑";
            return result.dump();
        }

        if (resp.status_code >= 400) {
            result["status"] = "error";
            result["message"] = "电脑端返回错误 (" + std::to_string(resp.status_code) + ")";
            return result.dump();
        }

        // Parse Hermes response
        try {
            auto hr = nlohmann::json::parse(resp.body);
            std::string hr_status = hr.value("status", std::string());
            if (hr_status == "ignored" || hr_status == "error") {
                result["status"] = "error";
                result["message"] = "电脑端未响应，请确认电脑已开机且 Hermes 服务正在运行";
            } else {
                result["status"] = "success";
                result["delivery_id"] = hr.value("delivery_id", std::string());
                result["message"] = "指令已发送到电脑";
            }
        } catch (...) {
            result["status"] = "success";
            result["message"] = "指令已发送";
        }

        return result.dump();
    }

private:
    std::string webhook_url_;
    std::string webhook_token_;
    anthropic::http::CurlHttpClient http_client_;
};

} // namespace tools
