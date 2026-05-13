#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <algorithm>
#include <cstdlib>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

#include "provider/provider.hpp"
#include "tools/registry.hpp"
#include "speech/speech_recognizer.hpp"
#include "speech/speech_synthesizer.hpp"
#include "voice/audio_capture.hpp"
#include "schema/message.hpp"
#include "vendor/nlohmann/json.hpp"
#include "common/logger.hpp"

namespace voice {

static const int AWAKE_TIMEOUT_SECONDS = 120;

class VoiceEngine {
public:
    VoiceEngine(provider::LLMProvider* provider,
                tools::Registry* registry,
                speech::SpeechRecognizer* recognizer,
                speech::SpeechSynthesizer* synthesizer,
                AudioCapture* audio_capture,
                const std::string& work_dir)
        : provider_(provider)
        , registry_(registry)
        , recognizer_(recognizer)
        , synthesizer_(synthesizer)
        , audio_capture_(audio_capture)
        , work_dir_(work_dir)
        , state_(SLEEPING)
        , running_(false)
        , last_activity_(0) {}

    void start() {
        running_ = true;
        context_.clear();
        context_.push_back(schema::Message{
            schema::RoleSystem,
            "你是柚子，一个智能语音助手。你可以使用各种工具帮助用户。"
            "回答要简洁自然，适合语音播报，不要有标点符号以外的格式。\n"
            "当你没有专用工具时，应该主动使用bash工具完成任务，例如：\n"
            "- 查天气：curl \"https://wttr.in/城市名?format=3&lang=zh\"\n"
            "- 查IP、网络诊断、系统信息等都可以用bash执行\n"
            "始终优先使用工具获取实时信息，不要编造数据。",
            {}, {}, ""
        });

        logger::info("VoiceEngine", "启动完成。说 \"你好柚子\" 或 \"柚子\" 来唤醒。");

        audio_capture_->start();

        try {
            runLoop();
        } catch (const std::exception& e) {
            logger::error("VoiceEngine", std::string("异常: ") + e.what());
        }

        audio_capture_->stop();
    }

    void stop() {
        running_ = false;
        audio_capture_->stop();
    }

private:
    enum State { SLEEPING, AWAKE };

    void runLoop() {
        while (running_) {
            if (state_ == SLEEPING) {
                sleepLoop();
            } else {
                awakeLoop();
            }
        }
    }

    void sleepLoop() {
        logger::info("VoiceEngine", "休眠中，等待唤醒词...");

        while (running_ && state_ == SLEEPING) {
            std::string wav = audio_capture_->waitForUtterance(0);
            if (!running_) break;
            if (wav.empty()) continue;

            speech::RecognitionResult asr = recognizer_->recognize(wav, "wav", 16000);
            if (!asr.success) {
                logger::error("VoiceEngine", "ASR错误: " + asr.error_message);
                continue;
            }

            logger::info("VoiceEngine", "听到: " + asr.text);

            if (containsWakeWord(asr.text)) {
                logger::info("VoiceEngine", "唤醒成功！");

                // Reset conversation for new session
                context_.erase(context_.begin() + 1, context_.end());

                // 先播放确认音，然后再切换状态
                speak("我在，请说");

                // 等待回声消散
                usleep(500000); // 500ms

                // 切换到 AWAKE 状态并刷新 VAD
                audio_capture_->flush();
                state_ = AWAKE;
                last_activity_ = std::time(nullptr);
            }
        }
    }

    void awakeLoop() {
        logger::info("VoiceEngine", "已唤醒，正在监听...");

        while (running_ && state_ == AWAKE) {
            int remaining = AWAKE_TIMEOUT_SECONDS
                - static_cast<int>(std::time(nullptr) - last_activity_);
            if (remaining <= 0) {
                logger::info("VoiceEngine", "超时，进入休眠。");
                speak("我先休息一下，需要的时候再叫我");
                state_ = SLEEPING;
                break;
            }

            std::string wav = audio_capture_->waitForUtterance(remaining);
            if (!running_) break;

            if (wav.empty()) {
                logger::info("VoiceEngine", "2分钟无对话，进入休眠。");
                speak("我先休息一下，需要的时候再叫我");
                state_ = SLEEPING;
                break;
            }

            last_activity_ = std::time(nullptr);

            speech::RecognitionResult asr = recognizer_->recognize(wav, "wav", 16000);
            if (!asr.success) {
                logger::error("VoiceEngine", "ASR错误: " + asr.error_message);
                continue;
            }

            std::string user_text = asr.text;
            if (user_text.empty()) continue;

            logger::info("VoiceEngine", "用户: " + user_text);

            context_.push_back(schema::Message{
                schema::RoleUser, user_text, {}, {}, ""
            });

            std::string response = runAgentTurn();
            if (!response.empty()) {
                logger::info("VoiceEngine", "Mose: " + response);
                speak(response);
            }
        }
    }

    std::string runAgentTurn() {
        std::vector<schema::ToolDefinition> tools = registry_->get_available_tools();
        std::string final_text;
        int max_turns = 10;

        for (int turn = 0; turn < max_turns; turn++) {
            logger::info("VoiceEngine", "Agent turn " + std::to_string(turn + 1) + " start");

            pruneVisionContext();

            schema::Message resp;
            try {
                resp = provider_->generate(context_, tools);
            } catch (const std::exception& e) {
                logger::error("VoiceEngine", "LLM错误: " + std::string(e.what()));
                return "抱歉，我遇到了一些问题";
            }

            context_.push_back(resp);

            if (resp.tool_calls.empty()) {
                final_text = resp.content;
                break;
            }

            logger::info("VoiceEngine", "执行 " + std::to_string(resp.tool_calls.size()) + " 个工具...");

            for (const auto& tc : resp.tool_calls) {
                logger::info("VoiceEngine", "工具: " + tc.name + " 参数: " + tc.args);
                schema::ToolResult result = registry_->execute(tc);

                if (result.is_error) {
                    logger::error("VoiceEngine", "工具执行失败: " + tc.name + " " + result.output);
                } else {
                    logger::info("VoiceEngine", "工具执行成功: " + tc.name);
                }

                schema::Message obs;
                obs.role = schema::RoleUser;
                obs.content = result.output;
                obs.tool_call_id = tc.id;
                context_.push_back(obs);

                if (tc.name == "camera_capture" && !result.is_error) {
                    try {
                        auto tool_out = nlohmann::json::parse(result.output);
                        std::string b64 = tool_out.value("base64", "");
                        if (!b64.empty()) {
                            schema::Message vision_msg;
                            vision_msg.role = schema::RoleUser;
                            vision_msg.content_parts.push_back(schema::ContentPart{
                                schema::ContentPart::IMAGE_URL, "", b64
                            });
                            vision_msg.content_parts.push_back(schema::ContentPart{
                                schema::ContentPart::TEXT,
                                "请描述这张照片的内容，用中文简洁回答。", ""
                            });
                            context_.push_back(vision_msg);
                        }
                    } catch (const std::exception& e) {
                        logger::error("VoiceEngine", std::string("解析相机结果失败: ") + e.what());
                    }
                }
            }
        }

        return final_text;
    }

    void pruneVisionContext() {
        int last_vision_idx = -1;
        for (int i = 0; i < static_cast<int>(context_.size()); i++) {
            if (!context_[i].content_parts.empty()) {
                last_vision_idx = i;
            }
        }
        for (int i = 0; i < last_vision_idx; i++) {
            if (!context_[i].content_parts.empty()) {
                std::string summary;
                for (const auto& part : context_[i].content_parts) {
                    if (part.type == schema::ContentPart::TEXT) {
                        summary += part.text;
                    } else {
                        summary += "[照片已分析]";
                    }
                }
                context_[i].content = summary;
                context_[i].content_parts.clear();
            }
        }
    }

    void speak(const std::string& text) {
        if (text.empty()) return;

        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&now));
        std::string out = std::string("/tmp/mose_tts_") + buf + ".wav";

        speech::SynthesisResult result = synthesizer_->synthesize(
            text, out, "wav", 16000, "zhimiao_emo");

        if (!result.success) {
            logger::error("VoiceEngine", "TTS错误: " + result.error_message);
            return;
        }

        // 先启动 aplay，再设置 AEC 参考信号，确保时序同步
        pid_t aplay_pid = fork();
        if (aplay_pid == 0) {
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            execlp("aplay", "aplay", "-D", "plughw:3,0",
                   result.output_file_path.c_str(), (char*)NULL);
            _exit(127);
        }

        if (aplay_pid < 0) {
            logger::error("VoiceEngine", "aplay fork失败");
            audio_capture_->setBargeInMode(false);
            return;
        }

        // 确保 aplay 已经启动
        usleep(50000); // 等待 50ms

        // 切换到高阈值 barge-in 模式，过滤环境音和回声
        audio_capture_->setBargeInMode(true);

        // 在 aplay 启动后才提供参考信号
        audio_capture_->setPlaybackSource(result.output_file_path);

        bool interrupted = false;
        while (true) {
            int status;
            pid_t ret = waitpid(aplay_pid, &status, WNOHANG);
            if (ret > 0) break;

            if (audio_capture_->hasPendingUtterance()) {
                logger::info("VoiceEngine", "用户打断，停止播放");
                kill(aplay_pid, SIGKILL);
                waitpid(aplay_pid, &status, 0);
                interrupted = true;
                break;
            }

            usleep(100000);
        }

        audio_capture_->setBargeInMode(false);

        if (!interrupted) {
            audio_capture_->flush();
        }

        // 等待回声消散，避免误识别
        usleep(500000); // 500ms
    }

    bool containsWakeWord(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // 检查中文唤醒词
        if (text.find("柚子") != std::string::npos ||
            text.find("你好柚子") != std::string::npos) {
            return true;
        }

        // 检查拼音和发音相似的词
        if (lower.find("youzi") != std::string::npos ||
            lower.find("yoz") != std::string::npos ||
            lower.find("yuz") != std::string::npos) {
            return true;
        }

        return false;
    }

    provider::LLMProvider* provider_;
    tools::Registry* registry_;
    speech::SpeechRecognizer* recognizer_;
    speech::SpeechSynthesizer* synthesizer_;
    AudioCapture* audio_capture_;
    std::string work_dir_;

    State state_;
    bool running_;
    std::time_t last_activity_;
    std::vector<schema::Message> context_;
};

} // namespace voice
