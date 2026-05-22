#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>

#include "common/logger.hpp"

namespace context {

struct Skill {
    std::string name;
    std::string description;
    std::string body;
};

class SkillLoader {
public:
    explicit SkillLoader(const std::string& workDir)
        : workDir_(workDir) {}

    std::string loadAll() const {
        std::string skillBaseDir = workDir_;
        if (!skillBaseDir.empty() && skillBaseDir[skillBaseDir.size() - 1] != '/') {
            skillBaseDir += '/';
        }
        skillBaseDir += ".yooz/skills";

        struct stat st;
        if (stat(skillBaseDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            logger::warn("SkillLoader", "Skills dir not found: " + skillBaseDir);
            return "";
        }

        logger::info("SkillLoader", "Scanning: " + skillBaseDir);

        std::vector<std::string> skillFiles;
        scanDir(skillBaseDir, skillFiles);

        logger::info("SkillLoader", "Found " + std::to_string(skillFiles.size()) + " skill files");

        if (skillFiles.empty()) {
            return "";
        }

        std::ostringstream ss;
        ss << "\n### 可用专业技能 (Agent Skills)\n";
        ss << "以下是你拥有的标准化外挂技能，请在符合description描述的场景下严格遵循其正文指令:\n\n";

        for (const auto& filePath : skillFiles) {
            std::ifstream file(filePath);
            if (!file.is_open()) continue;

            std::ostringstream content;
            content << file.rdbuf();
            Skill skill = parseSkillMD(content.str());

            ss << "#### 技能名称:" << skill.name << "\n";
            ss << "**触发条件**:" << skill.description << "\n\n";
            ss << "**执行指南**:\n";
            ss << skill.body << "\n\n---\n";
        }

        std::string result = ss.str();
        if (result.size() < 100) {
            return "";
        }

        return result;
    }

private:
    void scanDir(const std::string& dirPath, std::vector<std::string>& skillFiles) const {
        DIR* dir = opendir(dirPath.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name == "." || name == "..") continue;

            std::string fullPath = dirPath + "/" + name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode)) {
                scanDir(fullPath, skillFiles);
            } else if (name == "SKILL.md") {
                skillFiles.push_back(fullPath);
            }
        }

        closedir(dir);
    }

    static Skill parseSkillMD(const std::string& content) {
        Skill skill;
        skill.name = "Unknown Skill";
        skill.description = "No description provided.";
        skill.body = content;

        bool hasFrontmatter = false;
        if (content.size() >= 4 && content.substr(0, 4) == "---\n") {
            hasFrontmatter = true;
        } else if (content.size() >= 5 && content.substr(0, 5) == "---\r\n") {
            hasFrontmatter = true;
        }

        if (!hasFrontmatter) {
            return skill;
        }

        // Find closing ---
        size_t startSearch = content.find('\n') + 1;
        size_t secondDelim = content.find("\n---", startSearch);
        if (secondDelim == std::string::npos) {
            return skill;
        }

        std::string frontmatter = content.substr(startSearch, secondDelim - startSearch);
        skill.body = content.substr(secondDelim + 4);

        // Trim leading whitespace from body
        size_t bodyStart = skill.body.find_first_not_of(" \t\n\r");
        if (bodyStart != std::string::npos) {
            skill.body = skill.body.substr(bodyStart);
        }

        // Parse key:value lines
        std::istringstream stream(frontmatter);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.find("name:") == 0) {
                skill.name = line.substr(5);
                size_t s = skill.name.find_first_not_of(" \t");
                if (s != std::string::npos) skill.name = skill.name.substr(s);
            } else if (line.find("description:") == 0) {
                skill.description = line.substr(12);
                size_t s = skill.description.find_first_not_of(" \t");
                if (s != std::string::npos) skill.description = skill.description.substr(s);
            }
        }

        return skill;
    }

    std::string workDir_;
};

} // namespace context
