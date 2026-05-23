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
#include "voice/led_controller.hpp"
#include "schema/message.hpp"
#include "context/composer.hpp"
#include "vendor/nlohmann/json.hpp"
#include "common/logger.hpp"
#include "ui/ui_event_bus.hpp"

namespace voice {

static const int AWAKE_TIMEOUT_SECONDS = 120;

class VoiceEngine {
public:
    VoiceEngine(provider::LLMProvider* provider,
                tools::Registry* registry,
                speech::SpeechRecognizer* recognizer,
                speech::SpeechSynthesizer* synthesizer,
                AudioCapture* audio_capture,
                const std::string& work_dir,
                LedController* led = nullptr)
        : provider_(provider)
        , registry_(registry)
        , recognizer_(recognizer)
        , synthesizer_(synthesizer)
        , audio_capture_(audio_capture)
        , work_dir_(work_dir)
        , state_(SLEEPING)
        , running_(false)
        , last_activity_(0)
        , led_(led)
        , composer_(new context::PromptComposer(work_dir)) {}

    ~VoiceEngine() {
        delete composer_;
    }

    void start() {
        running_ = true;
        context_.clear();

        schema::Message sys = composer_->build();
        sys.content += "\n\n## 语音模式\n"
            "当前为语音对话模式。\n"
            "始终优先使用工具获取实时信息，不要编造数据。\n";
        context_.push_back(sys);

        logger::info("VoiceEngine", "启动完成。说 \"你好柚子\" 或 \"柚子\" 来唤醒。");
        ui::UIEventBus::instance().setEngineState(ui::SLEEPING);

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

    struct ExecutionResult {
        bool has_tool_calls;
        std::string content;       // LLM final text (when no tool calls)
        std::string tool_results;  // Aggregated tool results text
    };

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
        ui::UIEventBus::instance().setEngineState(ui::SLEEPING);

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
                ui::UIEventBus::instance().setAsrText(asr.text);

            if (containsWakeWord(asr.text)) {
                logger::info("VoiceEngine", "唤醒成功！");
                ui::UIEventBus::instance().setEngineState(ui::AWAKE);

                // Reset conversation for new session
                context_.erase(context_.begin() + 1, context_.end());

                // 先播放确认音，然后再切换状态
                speak("我在，请说");

                // 切换到 AWAKE 状态并刷新 VAD
                audio_capture_->flush();
                state_ = AWAKE;
                ui::UIEventBus::instance().setEngineState(ui::AWAKE);
                last_activity_ = std::time(nullptr);
            } else {
                // 不是唤醒词，关闭 LED
                if (led_) led_->setOff();
            }
        }
    }

    void awakeLoop() {
        logger::info("VoiceEngine", "已唤醒，正在监听...");
        ui::UIEventBus::instance().setEngineState(ui::AWAKE);

        while (running_ && state_ == AWAKE) {
            int remaining = AWAKE_TIMEOUT_SECONDS
                - static_cast<int>(std::time(nullptr) - last_activity_);
            if (remaining <= 0) {
                logger::info("VoiceEngine", "超时，进入休眠。");
                speak("我先休息一下，需要的时候再叫我");
                if (led_) led_->setOff();
                state_ = SLEEPING;
                ui::UIEventBus::instance().setEngineState(ui::SLEEPING);
                break;
            }

            std::string wav = audio_capture_->waitForUtterance(remaining);
            if (!running_) break;

            if (wav.empty()) {
                logger::info("VoiceEngine", "2分钟无对话，进入休眠。");
                speak("我先休息一下，需要的时候再叫我");
                if (led_) led_->setOff();
                state_ = SLEEPING;
                ui::UIEventBus::instance().setEngineState(ui::SLEEPING);
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
            ui::UIEventBus::instance().setAsrText(user_text);

            // 检查退出词
            if (containsExitWord(user_text)) {
                logger::info("VoiceEngine", "用户退出: " + user_text);
                speak("好的，需要的时候再叫我");
                if (led_) led_->setOff();
                state_ = SLEEPING;
                ui::UIEventBus::instance().setEngineState(ui::SLEEPING);
                break;
            }

            logger::info("VoiceEngine", "用户: " + user_text);

            context_.push_back(schema::Message{
                schema::RoleUser, user_text, {}, {}, ""
            });

            if (led_) led_->setBreathing();  // 思考中 → 呼吸灯
            ui::UIEventBus::instance().setEngineState(ui::THINKING);
            std::string response = runAgentTurn();
            if (!response.empty() && !tts_played_) {
                logger::info("VoiceEngine", "Mose: " + response);
                ui::UIEventBus::instance().setLlmText(response);
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

    // === Three-Phase Agent Turn ===

    std::string runAgentTurn() {
        std::string user_text = context_.back().content;

        // === Phase 1: Intent Analysis (no tools) ===
        std::string intent = runIntentPhase(user_text);
        logger::info("VoiceEngine", "意图分析结果: " + intent);

        // If pure conversation, answer directly (no tools)
        if (intent.find("conversation") == 0) {
            return runConversationPhase(user_text);
        }

        // === Phase 2: Tool Execution (with all tools) ===
        ExecutionResult exec = runExecutionPhase(user_text, intent);

        // If no tool calls, or execution produced a final text response, return it directly
        if (!exec.has_tool_calls || !exec.content.empty()) {
            return exec.content;
        }

        // === Phase 3: Summary (no tools) ===
        return runSummaryPhase(user_text, exec.tool_results);
    }

    // Phase 1: Analyze user intent without any tools
    std::string runIntentPhase(const std::string& user_text) {
        std::vector<schema::Message> intent_ctx;
        intent_ctx.push_back(composer_->buildIntentSystem());
        intent_ctx.push_back({schema::RoleUser, user_text, {}, {}, ""});

        schema::Message resp;
        try {
            resp = provider_->generate(intent_ctx, {});
        } catch (const std::exception& e) {
            logger::error("VoiceEngine", "意图分析失败: " + std::string(e.what()));
            return "conversation:意图分析失败";
        }
        return resp.content;
    }

    // Phase 2: Execute tools based on intent
    ExecutionResult runExecutionPhase(const std::string& user_text,
                                       const std::string& intent) {
        std::vector<schema::ToolDefinition> tools = registry_->get_available_tools();
        std::vector<schema::Message> exec_ctx;
        exec_ctx.push_back(composer_->buildExecutionSystem(intent));
        exec_ctx.push_back({schema::RoleUser, user_text, {}, {}, ""});

        ExecutionResult result;
        result.has_tool_calls = false;
        std::ostringstream results_ss;

        for (int turn = 0; turn < 10; turn++) {
            logger::info("VoiceEngine", "执行阶段 turn " + std::to_string(turn + 1));

            schema::Message resp;
            try {
                resp = provider_->generate(exec_ctx, tools);
            } catch (const std::exception& e) {
                logger::error("VoiceEngine", "执行阶段LLM错误: " + std::string(e.what()));
                result.content = "抱歉，执行时遇到问题";
                return result;
            }

            if (resp.tool_calls.empty()) {
                result.content = resp.content;
                break;
            }

            result.has_tool_calls = true;
            exec_ctx.push_back(resp);

            for (const auto& tc : resp.tool_calls) {
                logger::info("VoiceEngine", "工具: " + tc.name + " 参数: " + tc.args);
                ui::UIEventBus::instance().setToolCall(tc.name, tc.args);
                schema::ToolResult tr = registry_->execute(tc);

                if (tr.is_error) {
                    logger::error("VoiceEngine", "工具执行失败: " + tc.name + " " + tr.output);
                    ui::UIEventBus::instance().setToolResult(tc.name, true);
                } else {
                    logger::info("VoiceEngine", "工具执行成功: " + tc.name);
                    ui::UIEventBus::instance().setToolResult(tc.name, false);
                }

                results_ss << "工具 " << tc.name << ": "
                           << (tr.is_error ? "失败 - " : "成功 - ")
                           << tr.output << "\n";

                // Observation message
                schema::Message obs;
                obs.role = schema::RoleUser;
                obs.content = tr.output;
                obs.tool_call_id = tc.id;
                exec_ctx.push_back(obs);

                // TTS playback
                if (tc.name == "text_to_speech" && !tr.is_error) {
                    try {
                        auto tool_out = nlohmann::json::parse(tr.output);
                        std::string output_file = tool_out.value("output_file", "");
                        if (!output_file.empty()) {
                            playTtsFile(output_file);
                            tts_played_ = true;
                        }
                    } catch (const std::exception& e) {
                        logger::error("VoiceEngine", std::string("播放TTS失败: ") + e.what());
                    }
                }

                // Camera vision injection
                if (tc.name == "camera_capture" && !tr.is_error) {
                    try {
                        auto tool_out = nlohmann::json::parse(tr.output);
                        std::string b64 = tool_out.value("base64", "");
                        if (!b64.empty()) {
                            schema::Message vision_msg;
                            vision_msg.role = schema::RoleUser;
                            vision_msg.content_parts.push_back(schema::ContentPart{
                                schema::ContentPart::IMAGE_URL, "", "data:image/jpeg;base64," + b64
                            });
                            vision_msg.content_parts.push_back(schema::ContentPart{
                                schema::ContentPart::TEXT,
                                "这是你面前的摄像头刚刚拍下的照片，照片中的人就是正在和你说话的用户。请用第二人称简短描述用户正在做什么，一两句话即可。", ""
                            });
                            exec_ctx.push_back(vision_msg);
                        }
                    } catch (const std::exception& e) {
                        logger::error("VoiceEngine", std::string("解析相机结果失败: ") + e.what());
                    }
                }
            }
        }

        result.tool_results = results_ss.str();
        return result;
    }

    // Phase 3: Summarize tool results for voice playback
    std::string runSummaryPhase(const std::string& user_text,
                                 const std::string& tool_results) {
        std::vector<schema::Message> summary_ctx;
        summary_ctx.push_back(composer_->buildSummarySystem());
        summary_ctx.push_back({schema::RoleUser,
            "用户说：「" + user_text + "」\n\n工具执行结果：\n" + tool_results,
            {}, {}, ""});

        schema::Message resp;
        try {
            resp = provider_->generate(summary_ctx, {});
        } catch (const std::exception& e) {
            logger::error("VoiceEngine", "总结阶段失败: " + std::string(e.what()));
            return "操作已完成";
        }
        return resp.content;
    }

    // Conversation: direct answer without tools
    std::string runConversationPhase(const std::string& user_text) {
        std::vector<schema::Message> ctx;
        ctx.push_back(composer_->buildSummarySystem());
        ctx.push_back({schema::RoleUser, user_text, {}, {}, ""});

        schema::Message resp;
        try {
            resp = provider_->generate(ctx, {});
        } catch (const std::exception& e) {
            logger::error("VoiceEngine", "对话阶段失败: " + std::string(e.what()));
            return "抱歉，我遇到了一些问题";
        }
        return resp.content;
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
        if (led_) led_->setOff();
    }

    void speak(const std::string& text) {
        if (text.empty()) return;

        ui::UIEventBus::instance().setEngineState(ui::SPEAKING);
        audio_capture_->setMuted(true);

        // Try streaming playback first
        if (audio_capture_->beginStreamPlayback()) {
            speech::StreamSynthesisResult sr = synthesizer_->synthesizeStream(
                text, 16000, "longxiaoxia_v3", 1.0f,
                [this](const uint8_t* data, size_t len) {
                    if (len >= 2) {
                        audio_capture_->writeStreamPCM(
                            reinterpret_cast<const int16_t*>(data),
                            len / 2);
                    }
                });

            audio_capture_->endStreamPlayback();

            if (sr.success) {
                usleep(500000);
                audio_capture_->flush();
                audio_capture_->setMuted(false);
                if (led_) led_->setOff();
                ui::UIEventBus::instance().setEngineState(ui::AWAKE);
                return;
            }

            logger::warn("VoiceEngine", "流式TTS失败, 回退文件模式: " + sr.error_message);
        }

        // Fallback: file-based playback
        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&now));
        std::string out = std::string("/tmp/mose_tts_") + buf + ".wav";

        speech::SynthesisResult result = synthesizer_->synthesize(
            text, out, "wav", 16000, "longxiaoxia_v3", 1.0f);

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
            text.find("幽子") != std::string::npos ||
            text.find("游子") != std::string::npos ||
            text.find("釉子") != std::string::npos ||
            text.find("幼子") != std::string::npos) {
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
    LedController* led_;
    std::vector<schema::Message> context_;
    context::PromptComposer* composer_;
};

} // namespace voice
