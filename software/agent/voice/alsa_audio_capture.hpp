#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iostream>
#include <alsa/asoundlib.h>

#include "voice/audio_capture.hpp"
#include "voice/vad.hpp"
#include "voice/wav_writer.hpp"

namespace voice {

class AlsaAudioCapture : public AudioCapture {
public:
    AlsaAudioCapture(const std::string& device = "default",
                     int sample_rate = 16000)
        : device_(device)
        , sample_rate_(sample_rate)
        , running_(false)
        , muted_(false)
        , barge_in_mode_(false)
        , has_utterance_(false)
        , utterance_counter_(0)
        , vad_(sample_rate, 20.0f, 8, 30)
        , barge_in_vad_(sample_rate, 50.0f, 15, 30) {}

    ~AlsaAudioCapture() { stop(); }

    void start() override {
        running_ = true;
        muted_ = false;
        barge_in_mode_ = false;
        has_utterance_ = false;
        vad_.reset();
        barge_in_vad_.reset();
        capture_thread_ = std::thread(&AlsaAudioCapture::captureLoop, this);
    }

    void stop() override {
        running_ = false;
        cv_.notify_all();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        vad_.reset();
        barge_in_vad_.reset();
        has_utterance_ = false;
        pending_wav_.clear();
    }

    void setMuted(bool muted) override {
        muted_ = muted;
        if (muted) flush();
    }

    void setBargeInMode(bool enabled) override {
        barge_in_mode_ = enabled;
        flush();
    }

    std::string waitForUtterance(int timeout_seconds) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (timeout_seconds <= 0) {
            cv_.wait(lock, [this] { return has_utterance_ || !running_; });
        } else {
            cv_.wait_for(lock, std::chrono::seconds(timeout_seconds),
                         [this] { return has_utterance_ || !running_; });
        }
        if (has_utterance_) {
            has_utterance_ = false;
            return pending_wav_;
        }
        return "";
    }

    bool hasPendingUtterance() const override {
        return has_utterance_;
    }

private:
    void captureLoop() {
        snd_pcm_t* handle = nullptr;
        int err = snd_pcm_open(&handle, device_.c_str(),
                               SND_PCM_STREAM_CAPTURE, 0);
        if (err < 0) {
            std::cerr << "[AlsaAudioCapture] Open failed: "
                      << snd_strerror(err) << std::endl;
            return;
        }

        err = snd_pcm_set_params(handle,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 1, sample_rate_, 1, 500000);
        if (err < 0) {
            std::cerr << "[AlsaAudioCapture] Set params failed: "
                      << snd_strerror(err) << std::endl;
            snd_pcm_close(handle);
            return;
        }

        const int chunk = 160; // 10ms at 16kHz
        std::vector<int16_t> buf(chunk);

        while (running_) {
            int frames = snd_pcm_readi(handle, buf.data(), chunk);
            if (frames <= 0) {
                frames = snd_pcm_recover(handle, frames, 0);
                if (frames < 0) break;
                continue;
            }

            if (muted_) continue;

            std::lock_guard<std::mutex> lock(mutex_);

            if (barge_in_mode_) {
                // 播放期间：使用高阈值 VAD，只有大声说话才触发
                barge_in_vad_.process(buf.data(), frames);

                if (barge_in_vad_.speech_ended()) {
                    std::vector<int16_t> audio = barge_in_vad_.take_buffer();
                    if (audio.size() > 8000) { // 至少 500ms 持续语音
                        utterance_counter_++;
                        std::string path = "/tmp/mose_barge_"
                            + std::to_string(utterance_counter_) + ".wav";
                        write_wav(path, audio, sample_rate_);
                        pending_wav_ = path;
                        has_utterance_ = true;
                        cv_.notify_one();
                    }
                }
            } else {
                // 正常模式：标准 VAD
                vad_.process(buf.data(), frames);

                if (vad_.speech_ended()) {
                    std::vector<int16_t> audio = vad_.take_buffer();
                    if (audio.size() > 4800) {
                        utterance_counter_++;
                        std::string path = "/tmp/mose_utt_"
                            + std::to_string(utterance_counter_) + ".wav";
                        write_wav(path, audio, sample_rate_);
                        pending_wav_ = path;
                        has_utterance_ = true;
                        cv_.notify_one();
                    }
                }
            }
        }

        snd_pcm_close(handle);
    }

    std::string device_;
    int sample_rate_;
    std::atomic<bool> running_;
    std::atomic<bool> muted_;
    std::atomic<bool> barge_in_mode_;

    std::mutex mutex_;
    std::condition_variable cv_;
    VoiceActivityDetector vad_;
    VoiceActivityDetector barge_in_vad_;
    std::string pending_wav_;
    bool has_utterance_;
    int utterance_counter_;

    std::thread capture_thread_;
};

} // namespace voice
