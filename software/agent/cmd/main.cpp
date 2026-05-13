#include <iostream>
#include <cstdlib>
#include <unistd.h>

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
#include "anthropic/http/http_client.hpp"
#include "camera/pi_camera_capture.hpp"
#include "voice/alsa_audio_capture.hpp"
#include "voice/voice_engine.hpp"
#include "common/logger.hpp"

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

    provider::OpenAIProvider llm_provider("glm-5v-turbo");
    anthropic::http::CurlHttpClient speech_http;

    camera::PiCameraCapture camera_capture;

    speech::AlibabaConfig alibaba_config;
    bool has_speech = true;
    try {
        alibaba_config = speech::AlibabaConfig::from_env();
    } catch (const std::exception& e) {
        logger::warn("Main", std::string(e.what()) + " (speech tools disabled)");
        has_speech = false;
    }

    speech::AlibabaSpeechRecognizer asr(alibaba_config, &speech_http);
    speech::AlibabaSpeechSynthesizer tts(alibaba_config, &speech_http);

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
    voice::VoiceEngine voice_engine(
        &llm_provider, &registry, &asr, &tts, &audio_capture, workDir);

    try {
        voice_engine.start();
    } catch (const std::exception& e) {
        logger::error("Main", std::string("VoiceEngine crashed: ") + e.what());
        return 1;
    }

    logger::info("Main", "Mose Finished.");
    return 0;
}
