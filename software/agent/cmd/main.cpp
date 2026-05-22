#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <libgen.h>

#include "provider/openai_provider.hpp"
#include "tools/registry.hpp"
#include "tools/read_file_tool.hpp"
#include "tools/write_file_tool.hpp"
#include "tools/bash_tool.hpp"
#include "tools/camera_tool.hpp"
#include "tools/speech_to_text_tool.hpp"
#include "tools/text_to_speech_tool.hpp"
#include "speech/alibaba_config.hpp"
#include "speech/alibaba_speech_recognizer.hpp"
#include "speech/alibaba_speech_synthesizer.hpp"
#include "nlsClient.h"
#include "camera/pi_camera_capture.hpp"
#include "voice/alsa_audio_capture.hpp"
#include "voice/voice_engine.hpp"
#include "voice/led_controller.hpp"
#include "common/logger.hpp"
#include "ui/ftxui_display.hpp"

// 获取二进制文件路径，向上查找项目根目录（包含 .yooz/skills 的目录）
static std::string resolveProjectDir(const std::string& cwd) {
    struct stat st;

    // 1. 先检查 cwd 本身
    std::string check = cwd + "/.yooz/skills";
    if (stat(check.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        return cwd;
    }

    // 2. 向上查找（最多 3 层）
    std::string dir = cwd;
    for (int i = 0; i < 3; i++) {
        size_t pos = dir.rfind('/');
        if (pos == std::string::npos) break;
        dir = dir.substr(0, pos);
        if (dir.empty()) dir = "/";
        check = dir + "/.yooz/skills";
        if (stat(check.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return dir;
        }
    }

    // 3. 回退到 cwd
    return cwd;
}

int main() {
    logger::init("/tmp");
    logger::info("Main", "Hello, YooZi!");

    if (!std::getenv("ZHIPU_API_KEY") && !std::getenv("OPENAI_API_KEY")) {
        logger::error("Main", "Please set ZHIPU_API_KEY or OPENAI_API_KEY environment variable");
        return 1;
    }

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        logger::error("Main", "Failed to get working directory");
        return 1;
    }
    std::string workDir(cwd);
    workDir = resolveProjectDir(workDir);
    logger::info("Main", "Project dir: " + workDir);

    provider::OpenAIProvider llm_provider("glm-5v-turbo");

    camera::PiCameraCapture camera_capture;

    speech::AlibabaConfig alibaba_config;
    bool has_speech = true;
    try {
        alibaba_config = speech::AlibabaConfig::from_env();
    } catch (const std::exception& e) {
        logger::warn("Main", std::string(e.what()) + " (speech tools disabled)");
        has_speech = false;
    }

    // Initialize NLS SDK client
    AlibabaNls::NlsClient* nls_client = nullptr;
    if (has_speech) {
        nls_client = AlibabaNls::NlsClient::getInstance();
        nls_client->startWorkThread(1);
    }

    speech::AlibabaSpeechRecognizer asr(alibaba_config, nls_client);
    speech::AlibabaSpeechSynthesizer tts(alibaba_config, nls_client);

    tools::ReadFileTool read_file_tool(workDir);
    tools::WriteFileTool write_file_tool(workDir);
    tools::BashTool bash_tool(workDir);
    tools::CameraTool camera_tool(&camera_capture);

    tools::Registry registry;
    registry.registry(&read_file_tool);
    registry.registry(&write_file_tool);
    registry.registry(&bash_tool);
    registry.registry(&camera_tool);

    tools::SpeechToTextTool stt_tool(&asr);
    tools::TextToSpeechTool tts_tool(&tts);

    if (has_speech) {
        registry.registry(&stt_tool);
        registry.registry(&tts_tool);
    }

    if (!has_speech) {
        logger::error("Main", "Cannot start voice engine without speech services");
        return 1;
    }

    voice::AlsaAudioCapture audio_capture("plughw:3,0", "plughw:3,0");  // ReSpeaker 2-Mics Pi HAT

    // Initialize LED controller on GPIO12
    voice::LedController led(12);
    led.start();

    // Set volume to 95%
    system("amixer -c 3 set Speaker 95% 2>/dev/null");

    audio_capture.setLedController(&led);

    // Connect logger to UI event bus
    logger::Logger::instance().setLogSink([](const std::string& line) {
        ui::UIEventBus::instance().addLog(line);
    });

    // Start FTXUI display thread
    ui::FtxuiDisplay display;
    display.start();

    voice::VoiceEngine voice_engine(
        &llm_provider, &registry, &asr, &tts, &audio_capture, workDir, &led);

    try {
        voice_engine.start();
    } catch (const std::exception& e) {
        logger::error("Main", std::string("VoiceEngine crashed: ") + e.what());
    }

    display.stop();
    led.stop();

    if (nls_client) {
        AlibabaNls::NlsClient::releaseInstance();
    }

    logger::info("Main", "Mose Finished.");
    return 0;
}
