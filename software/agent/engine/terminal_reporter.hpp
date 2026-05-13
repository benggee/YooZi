#pragma once

#include <string>
#include <iostream>

#include "engine/reporter.hpp"

namespace engine {

class TerminalReporter : public Reporter {
public:
    void onThinking() override {
        std::cout << "\n[思考中] 模型正在推理..." << std::endl;
    }

    void onToolCall(const std::string& toolName, const std::string& args) override {
        std::cout << "[调用工具] " << toolName << std::endl;

        std::string display = args;
        for (size_t i = 0; i < display.size(); i++) {
            if (display[i] == '\n') {
                display.replace(i, 1, "\\n");
                i += 1;
            } else if (display[i] == '\r') {
                display.replace(i, 1, "\\r");
                i += 1;
            }
        }

        if (display.size() > 150) {
            display = display.substr(0, 150) + "...[已截断]";
        }

        std::cout << " 参数：" << display << std::endl;
    }

    void onToolResult(const std::string& toolName, const std::string& result, bool isError) override {
        if (isError) {
            std::cout << "[FAIL] " << toolName << std::endl;
            if (!result.empty()) {
                std::string display = result;
                if (display.size() > 200) {
                    display = display.substr(0, 200) + "...(truncated)";
                }
                std::cout << "   错误: " << display << std::endl;
            }
        } else {
            std::cout << "[OK] " << toolName << std::endl;
        }
    }

    void onMessage(const std::string& content) override {
        if (content.empty()) return;
        std::cout << "\n Agent 回复：" << std::endl;
        std::cout << content << std::endl;
    }
};

} // namespace engine
