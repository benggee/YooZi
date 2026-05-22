#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace speech {

struct SynthesisResult {
    std::string output_file_path;
    bool success;
    std::string error_message;
};

typedef std::function<void(const uint8_t* data, size_t len)> AudioChunkCallback;

struct StreamSynthesisResult {
    bool success = false;
    std::string error_message;
};

class SpeechSynthesizer {
public:
    virtual ~SpeechSynthesizer() {}
    virtual SynthesisResult synthesize(const std::string& text,
                                        const std::string& output_path,
                                        const std::string& format,
                                        int sample_rate,
                                        const std::string& voice,
                                        float speech_rate = 1.0f) = 0;

    virtual StreamSynthesisResult synthesizeStream(
        const std::string& text,
        int sample_rate,
        const std::string& voice,
        float speech_rate,
        AudioChunkCallback callback) = 0;
};

} // namespace speech
