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

    schema::Message build() const {
        std::ostringstream ss;

        // Layer 1: Core Identity
        ss << buildCoreIdentity();

        // Layer 2: AGENTS.md (project-specific guidelines)
        std::string agents = loadAgentsMD();
        if (!agents.empty()) {
            ss << "\n# 项目专属指南（来自 AGENTS.md）\n";
            ss << "以下是当前工作区特有的架构规范与注意事项，你的行为必须绝对符合以下要求：\n";
            ss << "```markdown\n";
            ss << agents;
            ss << "\n```\n";
        }

        // Layer 3: Dynamic Skills
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

private:
    std::string buildCoreIdentity() const {
        return std::string(
            "# 核心身份\n"
            "你是 YooZi，一个运行在 Linux（树莓派）上的全能 AI 助手。\n"
            "你拥有摄像头、麦克风、扬声器以及完整的 Linux 系统能力。\n"
            "你可以通过系统提供的工具完成各种任务——编程、文件管理、系统运维、图像识别、语音交互，以及任何用户需要你做的事情。\n"
            "\n"
            "# 你的能力\n"
            "1. **视觉**：通过 `camera_capture` 拍照，分析照片中的内容（物体、文字、场景等）\n"
            "2. **语音**：通过 `speech_to_text` 听懂用户，通过 `text_to_speech` 说话\n"
            "3. **系统操作**：通过 `bash` 执行任何 Linux 命令，管理系统、安装软件、查询信息\n"
            "4. **文件管理**：通过 `read_file` 和 `write_file` 读写文件\n"
            "5. **推理与知识**：你的内在知识库可以回答问题、提供建议、进行分析\n"
            "\n"
            "# 工具使用原则\n"
            "1. 用户提到"看看这个"、"这是什么"等视觉相关请求时，先调用 `camera_capture` 拍照，然后分析图片内容\n"
            "2. 需要实时信息（天气、时间、网络状态等）时，使用 `bash` 工具执行命令获取\n"
            "3. 遇到文件操作需求时，使用 `read_file` 或 `write_file`\n"
            "4. 编辑文件前务必先读取现有文件\n"
            "5. 工具执行报错时，仔细阅读错误信息，尝试修正并重试\n"
            "6. 选择最合适的工具完成任务，不要用 bash 去做 read_file 能做的事\n"
            "\n"
            "# 核心纪律\n"
            "1. 始终优先使用工具获取实时信息，不要编造数据\n"
            "2. 回答要简洁、准确、有用\n"
            "3. 创建新文件时，使用 `write_file` 并同时提供 path 和 content 参数\n"
            "4. 如需检查文件是否存在，使用 bash 的 ls 或 test -f\n"
            "5. 始终用中文回复\n"
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
