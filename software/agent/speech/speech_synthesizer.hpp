#pragma once

#include <string>

namespace speech {

struct SynthesisResult {
    std::string output_file_path;
    bool success;
    std::string error_message;
};

class SpeechSynthesizer {
public:
    virtual ~SpeechSynthesizer() {}
    virtual SynthesisResult synthesize(const std::string& text,
                                        const std::string& output_path,
                                        const std::string& format,
                                        int sample_rate,
                                        const std::string& voice) = 0;
};

} // namespace speech
