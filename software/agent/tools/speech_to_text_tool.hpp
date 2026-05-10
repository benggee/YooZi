#pragma once

#include <string>

#include "tools/base_tool.hpp"
#include "speech/speech_recognizer.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class SpeechToTextTool : public BaseTool {
public:
    explicit SpeechToTextTool(speech::SpeechRecognizer* recognizer)
        : recognizer_(recognizer) {}

    std::string name() const override {
        return "speech_to_text";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "speech_to_text",
            "Transcribe an audio file to text using speech recognition",
            R"({
                "type": "object",
                "properties": {
                    "file_path": {
                        "type": "string",
                        "description": "Path to the audio file to transcribe"
                    },
                    "format": {
                        "type": "string",
                        "description": "Audio format (wav, pcm, opus, mp3). Default: wav"
                    },
                    "sample_rate": {
                        "type": "integer",
                        "description": "Sample rate in Hz. Default: 16000"
                    }
                },
                "required": ["file_path"]
            })"
        };
    }

    std::string execute(const std::string& args_json) override {
        nlohmann::json args = nlohmann::json::parse(args_json);

        std::string file_path = args.value("file_path", std::string());
        std::string format = args.value("format", std::string("wav"));
        int sample_rate = args.value("sample_rate", 16000);

        if (file_path.empty()) {
            throw std::runtime_error("file_path is required");
        }

        speech::RecognitionResult result = recognizer_->recognize(file_path, format, sample_rate);

        if (!result.success) {
            throw std::runtime_error("Speech recognition failed: " + result.error_message);
        }

        return result.text;
    }

private:
    speech::SpeechRecognizer* recognizer_;
};

} // namespace tools
