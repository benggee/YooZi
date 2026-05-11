#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>

#include "schema/message.hpp"
#include "provider/provider.hpp"
#include "anthropic/http/http_client.hpp"
#include "anthropic/error.hpp"
#include "vendor/nlohmann/json.hpp"
#include "common/logger.hpp"

namespace provider {

class OpenAIProvider : public LLMProvider {
public:
    OpenAIProvider(const std::string& model,
                   const std::string& base_url = "",
                   const std::string& api_key = "")
        : model_(model) {
        const char* envKey = std::getenv("ZHIPU_API_KEY");
        if (!envKey) envKey = std::getenv("OPENAI_API_KEY");
        if (!envKey && api_key.empty()) {
            throw std::runtime_error("ZHIPU_API_KEY or OPENAI_API_KEY not set");
        }
        api_key_ = api_key.empty() ? std::string(envKey) : api_key;
        base_url_ = base_url.empty()
            ? "https://open.bigmodel.cn/api/paas/v4/"
            : base_url;
        http_client_ = new anthropic::http::CurlHttpClient();
    }

    ~OpenAIProvider() {
        delete http_client_;
    }

    schema::Message generate(
        const std::vector<schema::Message>& messages,
        const std::vector<schema::ToolDefinition>& available_tools) override {

        nlohmann::json body;
        body["model"] = model_;
        nlohmann::json msgs_arr = nlohmann::json::array();

        for (const auto& msg : messages) {
            nlohmann::json m;
            switch (msg.role) {
            case schema::RoleSystem:
                m["role"] = "system";
                m["content"] = msg.content;
                break;
            case schema::RoleUser:
                if (!msg.tool_call_id.empty()) {
                    m["role"] = "tool";
                    m["content"] = msg.content;
                    m["tool_call_id"] = msg.tool_call_id;
                } else if (!msg.content_parts.empty()) {
                    m["role"] = "user";
                    nlohmann::json parts = nlohmann::json::array();
                    for (const auto& part : msg.content_parts) {
                        nlohmann::json p;
                        if (part.type == schema::ContentPart::IMAGE_URL) {
                            p["type"] = "image_url";
                            p["image_url"]["url"] = part.image_url;
                        } else {
                            p["type"] = "text";
                            p["text"] = part.text;
                        }
                        parts.push_back(p);
                    }
                    m["content"] = parts;
                } else {
                    m["role"] = "user";
                    m["content"] = msg.content;
                }
                break;
            case schema::RoleAssistant: {
                m["role"] = "assistant";
                if (!msg.content.empty()) {
                    m["content"] = msg.content;
                }
                if (!msg.tool_calls.empty()) {
                    nlohmann::json tc_arr = nlohmann::json::array();
                    for (const auto& tc : msg.tool_calls) {
                        nlohmann::json tc_obj;
                        tc_obj["id"] = tc.id;
                        tc_obj["type"] = "function";
                        tc_obj["function"]["name"] = tc.name;
                        tc_obj["function"]["arguments"] = tc.args;
                        tc_arr.push_back(tc_obj);
                    }
                    m["tool_calls"] = tc_arr;
                }
                break;
            }
            }
            msgs_arr.push_back(m);
        }
        body["messages"] = msgs_arr;

        if (!available_tools.empty()) {
            nlohmann::json tools_arr = nlohmann::json::array();
            for (const auto& td : available_tools) {
                nlohmann::json tool_obj;
                tool_obj["type"] = "function";
                tool_obj["function"]["name"] = td.name;
                tool_obj["function"]["description"] = td.description;
                try {
                    tool_obj["function"]["parameters"] = nlohmann::json::parse(td.input_schema);
                } catch (...) {
                    tool_obj["function"]["parameters"] = nlohmann::json::object();
                }
                tools_arr.push_back(tool_obj);
            }
            body["tools"] = tools_arr;
        }

        std::string url = base_url_;
        if (!url.empty() && url[url.size() - 1] == '/') {
            url += "chat/completions";
        } else {
            url += "/chat/completions";
        }

        std::string req_body = body.dump();
        logger::info("LLM", "Request URL: " + url);
        logger::info("LLM", "Request body: " + req_body);

        std::vector<std::pair<std::string, std::string>> headers;
        headers.push_back({"Content-Type", "application/json"});
        headers.push_back({"Accept", "application/json"});
        headers.push_back({"Authorization", "Bearer " + api_key_});

        anthropic::http::HttpResponse resp = http_client_->post(url, req_body, headers);

        logger::info("LLM", "Response status: " + std::to_string(resp.status_code));
        logger::info("LLM", "Response body: " + resp.body);

        if (resp.status_code == 0) {
            throw std::runtime_error("Network error: " + resp.body);
        }
        if (resp.status_code >= 400) {
            std::string err_msg = "API error";
            try {
                auto ej = nlohmann::json::parse(resp.body);
                if (ej.count("error") && ej["error"].is_object()) {
                    err_msg = ej["error"].value("message", err_msg);
                }
            } catch (...) {}
            throw std::runtime_error("API error [" + std::to_string(resp.status_code) + "]: " + err_msg);
        }

        nlohmann::json resp_json;
        try {
            resp_json = nlohmann::json::parse(resp.body);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to parse response: ") + e.what());
        }

        if (!resp_json.count("choices") || resp_json["choices"].empty()) {
            throw std::runtime_error("API response has no choices");
        }

        auto& choice = resp_json["choices"][0]["message"];

        schema::Message result;
        result.role = schema::RoleAssistant;
        result.content = choice.value("content", std::string());

        if (choice.count("tool_calls") && choice["tool_calls"].is_array()) {
            for (const auto& tc : choice["tool_calls"]) {
                result.tool_calls.push_back(schema::ToolCall{
                    tc.value("id", std::string()),
                    tc["function"].value("name", std::string()),
                    tc["function"].value("arguments", std::string())
                });
            }
        }

        return result;
    }

private:
    std::string model_;
    std::string api_key_;
    std::string base_url_;
    anthropic::http::HttpClient* http_client_;
};

} // namespace provider
