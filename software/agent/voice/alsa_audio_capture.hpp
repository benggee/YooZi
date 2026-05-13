#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iostream>
#include <fstream>
#include <alsa/asoundlib.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

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
        , vad_(sample_rate, 800.0f, 8, 30)
        , barge_in_vad_(sample_rate, 2000.0f, 15, 50)
        , echo_state_(nullptr)
        , preprocess_state_(nullptr)
        , aec_enabled_(true)  // 默认启用AEC
        , frame_size_(sample_rate / 100)  // 10ms frame
        , echo_buffer_(sample_rate * 4)    // 4 seconds echo buffer
        , echo_write_pos_(0)
        , echo_read_pos_(0) {}

    ~AlsaAudioCapture() {
        stop();
        cleanupAEC();
    }

    void start() override {
        running_ = true;
        muted_ = false;
        barge_in_mode_ = false;
        has_utterance_ = false;
        vad_.reset();
        barge_in_vad_.reset();

        // Initialize AEC since it's always enabled now
        if (aec_enabled_ && !echo_state_) {
            initializeAEC();
        }

        capture_thread_ = std::thread(&AlsaAudioCapture::captureLoop, this);
        playback_thread_ = std::thread(&AlsaAudioCapture::playbackLoop, this);
    }

    void stop() override {
        running_ = false;
        cv_.notify_all();
        playback_cv_.notify_all();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (playback_thread_.joinable()) {
            playback_thread_.join();
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

    void setEchoCancellation(bool enabled) override {
        std::lock_guard<std::mutex> lock(mutex_);
        aec_enabled_ = enabled;
        if (enabled && !echo_state_) {
            initializeAEC();
        } else if (!enabled && echo_state_) {
            cleanupAEC();
        }
    }

    void writeEchoReference(const int16_t* data, int frames) override {
        if (!aec_enabled_ || !echo_state_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // Write echo reference to circular buffer
        for (int i = 0; i < frames; i++) {
            echo_buffer_[echo_write_pos_] = data[i];
            echo_write_pos_ = (echo_write_pos_ + 1) % echo_buffer_.size();
        }
    }

    void setPlaybackSource(const std::string& wav_path) {
        playback_source_ = wav_path;
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
    void initializeAEC() {
        // Initialize echo cancellation
        int filter_length = sample_rate_ * 2; // 2 seconds
        echo_state_ = speex_echo_state_init(frame_size_, filter_length);
        speex_echo_ctl(echo_state_, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate_);

        // Initialize preprocessing (optional)
        // preprocess_state_ = speex_preprocess_state_init(frame_size_, sample_rate_);
        // if (preprocess_state_) {
        //     speex_preprocess_ctl(preprocess_state_, SPEEX_PREPROCESS_SET_ECHO_STATE, echo_state_);
        // }
    }

    void cleanupAEC() {
        if (echo_state_) {
            speex_echo_state_destroy(echo_state_);
            echo_state_ = nullptr;
        }
        if (preprocess_state_) {
            speex_preprocess_state_destroy(preprocess_state_);
            preprocess_state_ = nullptr;
        }
    }

    int readEchoBuffer(int16_t* buffer, int frames) {
        for (int i = 0; i < frames; i++) {
            buffer[i] = echo_buffer_[echo_read_pos_];
            echo_read_pos_ = (echo_read_pos_ + 1) % echo_buffer_.size();
        }
        return frames;
    }

    void processWithAEC(const int16_t* input, int frames, int16_t* output) {
        if (!aec_enabled_ || !echo_state_) {
            std::memcpy(output, input, frames * sizeof(int16_t));
            return;
        }

        int16_t echo_ref[frame_size_];
        readEchoBuffer(echo_ref, frames);

        // Perform echo cancellation
        speex_echo_cancellation(echo_state_, const_cast<int16_t*>(input), echo_ref, output);
    }

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

            // Apply echo cancellation if enabled
            int16_t processed_buf[chunk];
            processWithAEC(buf.data(), frames, processed_buf);

            std::lock_guard<std::mutex> lock(mutex_);

            if (barge_in_mode_) {
                // 播放期间：使用高阈值 VAD，只有大声说话才触发
                barge_in_vad_.process(processed_buf, frames);

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
                vad_.process(processed_buf, frames);

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

    // AEC components
    SpeexEchoState* echo_state_;
    SpeexPreprocessState* preprocess_state_;
    bool aec_enabled_;
    int frame_size_;
    std::vector<int16_t> echo_buffer_;
    int echo_write_pos_;
    int echo_read_pos_;
    std::string playback_source_;
    std::condition_variable playback_cv_;
    std::thread playback_thread_;

    std::thread capture_thread_;

    void playbackLoop() {
        while (running_) {
            if (!playback_source_.empty() && aec_enabled_) {
                // 读取并播放WAV文件，提供参考信号
                std::vector<int16_t> audio_data = read_wav_file(playback_source_);
                if (!audio_data.empty()) {
                    provideEchoReference(audio_data);
                }

                // 清空播放源，只播放一次
                std::lock_guard<std::mutex> lock(mutex_);
                playback_source_.clear();
            }

            std::unique_lock<std::mutex> lock(mutex_);
            playback_cv_.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return !running_ || !playback_source_.empty(); });
        }
    }

    std::vector<int16_t> read_wav_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        // 简单的WAV文件解析（仅支持16-bit PCM）
        file.seekg(44); // 跳过WAV头
        std::vector<int16_t> audio_data;
        int16_t sample;

        while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
            audio_data.push_back(sample);
        }

        return audio_data;
    }

    void provideEchoReference(const std::vector<int16_t>& audio_data) {
        // 等待aplay启动并播放一小段时间，确保参考信号与实际播放同步
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int pos = 0;
        while (pos < audio_data.size() && running_ && !playback_source_.empty()) {
            int frames = std::min(frame_size_, (int)audio_data.size() - pos);
            if (frames > 0) {
                writeEchoReference(audio_data.data() + pos, frames);
                pos += frames;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

} // namespace voice
