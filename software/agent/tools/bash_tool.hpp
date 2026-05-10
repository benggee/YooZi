#pragma once

#include <string>
#include <cstring>
#include <ctime>
#include <cstdlib>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <signal.h>

#include "tools/base_tool.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class BashTool : public BaseTool {
public:
    explicit BashTool(const std::string& work_dir) : work_dir_(work_dir) {}

    std::string name() const override {
        return "bash";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "bash",
            "在当前工作区执行任意的bash命令, 支持链式命令（如 &&）,返回标准输出（stdout）和标准错误（stderr）",
            R"({
                "type": "object",
                "properties": {
                    "command": {
                        "type": "string",
                        "description": "要执行的bash命令，例如：ls -al 或 go test ./..."
                    }
                },
                "required": ["command"]
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

        std::string command = args.value("command", std::string());
        if (command.empty()) {
            throw std::runtime_error("command is required");
        }

        int pipefd[2];
        if (pipe(pipefd) == -1) {
            return "执行报错：无法创建管道";
        }

        pid_t pid = fork();
        if (pid == -1) {
            close(pipefd[0]);
            close(pipefd[1]);
            return "执行报错：fork 失败";
        }

        if (pid == 0) {
            // Child process
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            if (chdir(work_dir_.c_str()) != 0) {
                const char* err = "chdir failed\n";
                write(STDERR_FILENO, err, strlen(err));
                _exit(127);
            }

            execl("/bin/bash", "bash", "-c", command.c_str(), (char*)NULL);
            _exit(127);
        }

        // Parent process
        close(pipefd[1]);

        // Set read end to non-blocking
        int flags = fcntl(pipefd[0], F_GETFL, 0);
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

        std::string output;
        char buf[4096];
        time_t start = time(NULL);
        bool timed_out = false;

        while (true) {
            int elapsed = static_cast<int>(time(NULL) - start);
            if (elapsed >= 30) {
                timed_out = true;
                break;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(pipefd[0], &rfds);
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            int sel = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
            if (sel > 0) {
                ssize_t n = read(pipefd[0], buf, sizeof(buf));
                if (n <= 0) break;
                output.append(buf, n);
            } else if (sel < 0 && errno != EINTR) {
                break;
            }
        }

        close(pipefd[0]);

        if (timed_out) {
            kill(pid, SIGKILL);
        }

        int status;
        waitpid(pid, &status, 0);

        if (timed_out) {
            return output + "\n [警告：命令执行超(30s), 已被系统强制终止，如果是启动常驻服务，请尝试将其转入后台。]";
        }

        bool cmd_failed = false;
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            cmd_failed = true;
        } else if (WIFSIGNALED(status)) {
            cmd_failed = true;
        }

        if (cmd_failed) {
            std::string result = "执行报错：exit code ";
            if (WIFEXITED(status)) {
                result += std::to_string(WEXITSTATUS(status));
            } else {
                result += "signal " + std::to_string(WTERMSIG(status));
            }
            result += "\n 输出: \n" + output;
            return result;
        }

        if (output.empty()) {
            return "命令执行成功，无终端输出";
        }

        const size_t max_len = 8000;
        if (output.size() > max_len) {
            return output.substr(0, max_len)
                + "\n\n...[终端输出过长，已截断至前" + std::to_string(max_len) + "字节]...";
        }

        return output;
    }

private:
    std::string work_dir_;
};

} // namespace tools
