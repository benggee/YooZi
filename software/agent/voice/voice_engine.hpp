#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <algorithm>
#include <cstdlib>

#include <unistd.h>

#include "provider/provider.hpp"
#include "tools/registry.hpp"
#include "speech/speech_recognizer.hpp"
#include "speech/speech_synthesizer.hpp"
#include "voice/audio_capture.hpp"
#include "schema/message.hpp"
#include "context/composer.hpp"
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
        , last_activity_(0)
        , composer_(new context::PromptComposer(work_dir)) {}

    ~VoiceEngine() {
        delete composer_;
    }

    void start() {
        running_ = true;
        context_.clear();

        schema::Message sys = composer_->build();
        sys.content += "\n\n## 语音模式专属指南\n"
            "当前为语音对话模式，回答要简洁自然，适合语音播报，不要使用 Markdown 格式。\n"
            "始终优先使用工具获取实时信息，不要编造数据。\n";
        context_.push_back(sys);

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

            // 检查退出词
            if (containsExitWord(user_text)) {
                logger::info("VoiceEngine", "用户退出: " + user_text);
                speak("好的，需要的时候再叫我");
                state_ = SLEEPING;
                break;
            }

            logger::info("VoiceEngine", "用户: " + user_text);

            context_.push_back(schema::Message{
                schema::RoleUser, user_text, {}, {}, ""
            });

            std::string response = runAgentTurn();
            if (!response.empty() && !tts_played_) {
                logger::info("VoiceEngine", "Mose: " + response);
                speak(response);
                audio_capture_->flush();
            } else {
                if (tts_played_) {
                    logger::info("VoiceEngine", "TTS已播放，跳过speak");
                }
                audio_capture_->flush();
            }
            tts_played_ = false;
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

                if (tc.name == "text_to_speech" && !result.is_error) {
                    try {
                        auto tool_out = nlohmann::json::parse(result.output);
                        std::string output_file = tool_out.value("output_file", "");
                        if (!output_file.empty()) {
                            playTtsFile(output_file);
                            tts_played_ = true;
                        }
                    } catch (const std::exception& e) {
                        logger::error("VoiceEngine", std::string("播放TTS失败: ") + e.what());
                    }
                }

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

    void playTtsFile(const std::string& wav_path) {
        audio_capture_->setMuted(true);
        audio_capture_->setPlaybackSource(wav_path);
        while (!audio_capture_->isPlaybackComplete()) {
            usleep(100000);
        }
        // 等待回声消散
        usleep(500000);
        audio_capture_->flush();
        audio_capture_->setMuted(false);
    }

    void speak(const std::string& text) {
        if (text.empty()) return;

        audio_capture_->setMuted(true);

        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&now));
        std::string out = std::string("/tmp/mose_tts_") + buf + ".wav";

        speech::SynthesisResult result = synthesizer_->synthesize(
            text, out, "wav", 16000, "longxiaocheng_v2", 1.5f);

        if (!result.success) {
            logger::error("VoiceEngine", "TTS错误: " + result.error_message);
            audio_capture_->setMuted(false);
            return;
        }

        playTtsFile(result.output_file_path);
    }

    bool containsExitWord(const std::string& text) {
        return text.find("退下") != std::string::npos ||
               text.find("滚吧") != std::string::npos ||
               text.find("你下去吧") != std::string::npos ||
               text.find("下去吧") != std::string::npos;
    }

    bool containsWakeWord(const std::string& text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        // 检查中文唤醒词（含常见误识别）
        if (text.find("柚子") != std::string::npos ||
            text.find("你好柚子") != std::string::npos ||
            text.find("燕子") != std::string::npos ||
            text.find("油子") != std::string::npos ||
            text.find("右子") != std::string::npos ||
            text.find("有事") != std::string::npos ||
            text.find("幽子") != std::string::npos) {
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
    bool tts_played_ = false;
    std::time_t last_activity_;
    std::vector<schema::Message> context_;
    context::PromptComposer* composer_;
};

} // namespace voice
