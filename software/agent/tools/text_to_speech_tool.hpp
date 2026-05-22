#pragma once

#include <string>
#include <ctime>

#include "tools/base_tool.hpp"
#include "speech/speech_synthesizer.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class TextToSpeechTool : public BaseTool {
public:
    explicit TextToSpeechTool(speech::SpeechSynthesizer* synthesizer)
        : synthesizer_(synthesizer) {}

    std::string name() const override {
        return "text_to_speech";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "text_to_speech",
            "Convert text to speech audio and play it on the local audio device",
            R"({
                "type": "object",
                "properties": {
                    "text": {
                        "type": "string",
                        "description": "The text to convert to speech"
                    },
                    "voice": {
                        "type": "string",
                        "description": "Voice name (e.g., xiaoyun, xiaogang). Default: xiaoyun"
                    },
                    "format": {
                        "type": "string",
                        "description": "Audio format (wav, mp3, pcm). Default: wav"
                    },
                    "sample_rate": {
                        "type": "integer",
                        "description": "Sample rate in Hz. Default: 16000"
                    }
                },
                "required": ["text"]
            })"
        };
    }

    std::string execute(const std::string& args_json) override {
        nlohmann::json args = nlohmann::json::parse(args_json);

        std::string text = args.value("text", std::string());
        std::string voice = args.value("voice", std::string("longxiaoxia_v3"));
        std::string format = args.value("format", std::string("wav"));
        int sample_rate = args.value("sample_rate", 16000);
        float speech_rate = args.value("speech_rate", 1.0f);

        if (text.empty()) {
            throw std::runtime_error("text is required");
        }

        std::string output_path = generate_temp_path(format);

        speech::SynthesisResult result = synthesizer_->synthesize(
            text, output_path, format, sample_rate, voice, speech_rate);

        if (!result.success) {
            throw std::runtime_error("Speech synthesis failed: " + result.error_message);
        }

        nlohmann::json out;
        out["status"] = "success";
        out["output_file"] = result.output_file_path;
        out["text"] = text;
        return out.dump();
    }

private:
    std::string generate_temp_path(const std::string& format) {
        std::time_t now = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&now));
        return std::string("/tmp/tts_") + buf + "." + format;
    }

    speech::SpeechSynthesizer* synthesizer_;
};

} // namespace tools
