#pragma once

#include <string>
#include <sstream>
#include <fstream>

#include "schema/message.hpp"
#include "context/skill.hpp"

namespace context {

class PromptComposer {
public:
    explicit PromptComposer(const std::string& workDir)
        : workDir_(workDir)
        , skillLoader_(workDir) {}

    // Base system message for context_ initialization (identity + guidelines + skills)
    schema::Message build() const {
        std::ostringstream ss;

        ss << "<identity>\n"
              "你是 YooZi（柚子），一个运行在树莓派上的语音助手。\n"
              "你可以控制树莓派本机，也可以通过 hermes_tool 远程操控 Windows 电脑。\n"
              "你拥有摄像头（拍照识别）、麦克风和扬声器。\n"
              "</identity>\n";

        std::string agents = loadAgentsMD();
        if (!agents.empty()) {
            ss << "\n<project_guidelines>\n";
            ss << agents;
            ss << "\n</project_guidelines>\n";
        }

        std::string skills = skillLoader_.loadAll();
        if (!skills.empty()) {
            ss << skills;
        }

        return schema::Message{
            schema::RoleSystem,
            ss.str(),
            {}, {}, ""
        };
    }

    // === Phase 1: Intent Analysis (no tools) ===
    schema::Message buildIntentSystem() const {
        return {schema::RoleSystem, buildIntentPrompt(), {}, {}, ""};
    }

    // === Phase 2: Tool Execution (with all tools) ===
    schema::Message buildExecutionSystem(const std::string& intent) const {
        std::string prompt = buildExecutionPrompt(intent);

        std::string agents = loadAgentsMD();
        if (!agents.empty()) {
            prompt += "\n<project_guidelines>\n";
            prompt += agents;
            prompt += "\n</project_guidelines>\n";
        }

        std::string skills = skillLoader_.loadAll();
        if (!skills.empty()) {
            prompt += skills;
        }

        return {schema::RoleSystem, prompt, {}, {}, ""};
    }

    // === Phase 3: Summary (no tools) ===
    schema::Message buildSummarySystem() const {
        return {schema::RoleSystem, buildSummaryPrompt(), {}, {}, ""};
    }

private:
    std::string buildIntentPrompt() const {
        return std::string(
            "<identity>\n"
            "你是 YooZi（柚子），一个运行在树莓派上的语音助手。\n"
            "你可以控制树莓派本机，也可以通过 hermes_tool 远程操控 Windows 电脑。\n"
            "你拥有摄像头（拍照识别）、麦克风和扬声器。\n"
            "</identity>\n"
            "\n"
            "<intent_analysis>\n"
            "分析用户消息的意图，判断属于以下哪一类，只回复类别名称和简要说明：\n"
            "\n"
            "1. **hermes**：涉及 Windows 电脑的操作\n"
            "   触发词：放歌、播放音乐、打开程序、关闭程序、打开浏览器、搜索文档、\n"
            "   打开 VS Code、写代码、打开微信、调节音量、换壁纸、文件管理等。\n"
            "   用户说\"本地播放\"或\"在树莓派上播放\"时不属于此类。\n"
            "\n"
            "2. **local**：树莓派本机操作\n"
            "   触发词：查看系统信息、安装软件、查看时间、天气、网络状态、本机文件操作。\n"
            "   用户明确说\"本地播放音乐\"时也属于此类。\n"
            "\n"
            "3. **camera**：需要使用摄像头\n"
            "   触发词：看看、这是什么、拍照、我手里拿的什么、前面有什么。\n"
            "\n"
            "4. **conversation**：纯对话问答或闲聊\n"
            "   不涉及任何工具操作的知识问答、闲聊、讲故事等。\n"
            "\n"
            "回复格式：只回复一行，格式为 \"类别:简要说明\"。\n"
            "例如：\"hermes:用户想播放音乐\"\n"
            "例如：\"local:用户想知道树莓派的当前时间\"\n"
            "例如：\"conversation:用户在闲聊\"\n"
            "</intent_analysis>\n"
        );
    }

    std::string buildExecutionPrompt(const std::string& intent) const {
        std::string prompt =
            "<identity>\n"
            "你是 YooZi（柚子），语音助手。根据意图分析结果选择正确的工具执行。\n"
            "</identity>\n"
            "\n"
            "<intent_context>\n"
            "意图分析结果：" + intent + "\n"
            "</intent_context>\n"
            "\n"
            "<tool_routing>\n"
            "根据意图分析结果选择工具：\n"
            "\n"
            "1. 意图为 **hermes** → 必须调用 hermes_tool\n"
            "   不要使用 bash 代替 hermes_tool。将用户自然语言提炼为 intent 参数。\n"
            "   - \"放首歌\" → hermes_tool {\"intent\":\"打开网易云音乐并播放\",\"category\":\"music\"}\n"
            "   - \"打开 VS Code\" → hermes_tool {\"intent\":\"打开VS Code并打开work.workspace工作区\",\"category\":\"application\"}\n"
            "   - \"搜索桌面的报告\" → hermes_tool {\"intent\":\"搜索桌面包含报告的文档\",\"category\":\"document\"}\n"
            "   - \"打开浏览器访问GitHub\" → hermes_tool {\"intent\":\"在浏览器打开GitHub\",\"category\":\"browser\"}\n"
            "   - \"暂停音乐\" → hermes_tool {\"intent\":\"暂停音乐播放\",\"category\":\"music\"}\n"
            "   注意：hermes_tool 操控 Windows 电脑，bash 只能操作树莓派本机，不能混淆。\n"
            "\n"
            "2. 意图为 **camera** → 调用 camera_capture，然后分析图片\n"
            "\n"
            "3. 意图为 **local** → 使用 bash / read_file / write_file\n"
            "\n"
            "4. 意图为 **conversation** → 直接回答，不调用工具\n"
            "\n"
            "通用原则：\n"
            "- 始终优先使用工具获取实时信息，不要编造数据\n"
            "- 编辑文件前务必先读取现有文件\n"
            "- 工具执行报错时，仔细阅读错误信息，尝试修正并重试\n"
            "- 创建新文件时，使用 write_file 并同时提供 path 和 content 参数\n"
            "- 如需检查文件是否存在，使用 bash 的 ls 或 test -f\n"
            "- 始终用中文回复\n"
            "- 你可以使用 bash 工具通过 sudo apt-get install -y <软件名> 安装所需的系统软件\n"
            "- 对于可能持续运行的命令（如播放音乐、启动服务等），必须在命令末尾追加 & 将其转入后台运行\n"
            "\n"
            "执行工具后，只返回工具的原始执行结果，不需要总结或润色。\n"
            "</tool_routing>\n";

        return prompt;
    }

    std::string buildSummaryPrompt() const {
        return std::string(
            "<identity>\n"
            "你是 YooZi（柚子），语音助手。\n"
            "</identity>\n"
            "\n"
            "<summary_rules>\n"
            "你的任务是将工具执行结果提炼为适合语音播报的简短回复。\n"
            "\n"
            "规则：\n"
            "1. 简洁自然，1-2 句话\n"
            "2. 不使用 Markdown 格式\n"
            "3. 不复述技术细节（delivery_id、HTTP 状态码等）\n"
            "4. hermes_tool 成功时：「好的，已帮你{动作}」或「任务已下发到你的电脑」\n"
            "5. 工具失败时：「抱歉，{简要原因}」\n"
            "6. 纯对话内容直接原样返回\n"
            "7. 始终用中文回复\n"
            "</summary_rules>\n"
        );
    }

    std::string loadAgentsMD() const {
        std::string path = workDir_;
        if (!path.empty() && path[path.size() - 1] != '/') {
            path += '/';
        }
        path += "AGENTS.md";

        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    std::string workDir_;
    SkillLoader skillLoader_;
};

} // namespace context
