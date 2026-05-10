#pragma once

#include <vector>
#include <cstdint>
#include <cmath>

namespace voice {

class VoiceActivityDetector {
public:
    VoiceActivityDetector(int sample_rate = 16000,
                          float energy_threshold = 500.0f,
                          int speech_start_frames = 5,
                          int speech_end_frames = 30)
        : sample_rate_(sample_rate)
        , energy_threshold_(energy_threshold)
        , speech_start_frames_(speech_start_frames)
        , speech_end_frames_(speech_end_frames)
        , speaking_(false)
        , speech_ended_(false)
        , consecutive_speech_(0)
        , consecutive_silence_(0)
        , frame_size_(sample_rate / 100) {}

    void process(const int16_t* samples, size_t count) {
        speech_ended_ = false;

        size_t offset = 0;
        while (offset + frame_size_ <= count) {
            float energy = calcEnergy(samples + offset, frame_size_);
            offset += frame_size_;

            if (energy > energy_threshold_) {
                consecutive_speech_++;
                consecutive_silence_ = 0;
            } else {
                consecutive_silence_++;
                consecutive_speech_ = 0;
            }

            if (!speaking_ && consecutive_speech_ >= speech_start_frames_) {
                speaking_ = true;
            }

            if (speaking_ && consecutive_silence_ >= speech_end_frames_) {
                speaking_ = false;
                speech_ended_ = true;
            }

            if (speaking_ || consecutive_speech_ > 0) {
                size_t start = offset - frame_size_;
                buffer_.insert(buffer_.end(), samples + start, samples + offset);
            }
        }
    }

    bool is_speaking() const { return speaking_; }
    bool speech_ended() const { return speech_ended_; }
    const std::vector<int16_t>& buffer() const { return buffer_; }

    std::vector<int16_t> take_buffer() {
        std::vector<int16_t> result;
        result.swap(buffer_);
        speech_ended_ = false;
        consecutive_speech_ = 0;
        consecutive_silence_ = 0;
        return result;
    }

    void reset() {
        buffer_.clear();
        speaking_ = false;
        speech_ended_ = false;
        consecutive_speech_ = 0;
        consecutive_silence_ = 0;
    }

private:
    float calcEnergy(const int16_t* samples, size_t count) {
        float sum = 0;
        for (size_t i = 0; i < count; i++) {
            float s = static_cast<float>(samples[i]) / 32768.0f;
            sum += s * s;
        }
        return sum / count * 10000.0f;
    }

    int sample_rate_;
    float energy_threshold_;
    int speech_start_frames_;
    int speech_end_frames_;
    bool speaking_;
    bool speech_ended_;
    int consecutive_speech_;
    int consecutive_silence_;
    int frame_size_;
    std::vector<int16_t> buffer_;
};

} // namespace voice
