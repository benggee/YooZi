#pragma once

#include <string>

namespace speech {

struct RecognitionResult {
    std::string text;
    bool success;
    std::string error_message;
};

class SpeechRecognizer {
public:
    virtual ~SpeechRecognizer() {}
    virtual RecognitionResult recognize(const std::string& audio_file_path,
                                         const std::string& format,
                                         int sample_rate) = 0;
};

} // namespace speech
