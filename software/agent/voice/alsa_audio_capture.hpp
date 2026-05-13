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
#include <cstring>
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
                     const std::string& playback_device = "default",
                     int sample_rate = 16000)
        : device_(device)
        , playback_device_(playback_device)
        , sample_rate_(sample_rate)
        , running_(false)
        , muted_(false)
        , barge_in_mode_(false)
        , has_utterance_(false)
        , utterance_counter_(0)
        , vad_(sample_rate, 1500.0f, 15, 30)
        , barge_in_vad_(sample_rate, 2000.0f, 8, 10)
        , echo_state_(nullptr)
        , preprocess_state_(nullptr)
        , aec_enabled_(true)
        , frame_size_(sample_rate / 100)
        , echo_buffer_(sample_rate * 4)
        , echo_write_pos_(0)
        , echo_read_pos_(0)
        , playback_active_(false)
        , playback_complete_(true) {}

    ~AlsaAudioCapture() {
        stop();
        cleanupAEC();
    }

    void start() override {
        running_ = true;
        muted_ = false;
        barge_in_mode_ = false;
        has_utterance_.store(false);
        playback_active_ = false;
        playback_complete_ = true;
        vad_.reset();
        barge_in_vad_.reset();

        if (aec_enabled_ && !echo_state_) {
            initializeAEC();
        }

        capture_thread_ = std::thread(&AlsaAudioCapture::captureLoop, this);
        playback_thread_ = std::thread(&AlsaAudioCapture::playbackLoop, this);
    }

    void stop() override {
        running_ = false;
        playback_active_ = false;
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
        has_utterance_.store(false);
        pending_wav_.clear();
    }

    void setMuted(bool muted) override {
        muted_ = muted;
        if (muted) flush();
    }

    void setBargeInMode(bool enabled) override {
        barge_in_mode_ = enabled;
        barge_in_vad_.reset();
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

        // Direct write to ring buffer — playbackLoop is the sole writer
        int wp = echo_write_pos_.load();
        for (int i = 0; i < frames; i++) {
            echo_buffer_[wp] = data[i];
            wp = (wp + 1) % static_cast<int>(echo_buffer_.size());
        }
        echo_write_pos_.store(wp);
    }

    void setPlaybackSource(const std::string& wav_path) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            playback_source_ = wav_path;
        }
        playback_cv_.notify_one();
    }

    bool isPlaybackComplete() const override {
        return playback_complete_.load();
    }

    void stopPlayback() override {
        playback_active_.store(false);
    }

    void setPlaybackDevice(const std::string& device) override {
        playback_device_ = device;
    }

    std::string waitForUtterance(int timeout_seconds) override {
        std::unique_lock<std::mutex> lock(mutex_);
        if (timeout_seconds <= 0) {
            cv_.wait(lock, [this] { return has_utterance_.load() || !running_; });
        } else {
            cv_.wait_for(lock, std::chrono::seconds(timeout_seconds),
                         [this] { return has_utterance_.load() || !running_; });
        }
        if (has_utterance_.load()) {
            has_utterance_.store(false);
            return pending_wav_;
        }
        return "";
    }

    bool hasPendingUtterance() const override {
        return has_utterance_.load();
    }

    std::string getPendingWav() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (has_utterance_.load()) {
            has_utterance_.store(false);
            return pending_wav_;
        }
        return "";
    }

private:
    void initializeAEC() {
        int filter_length = sample_rate_ * 2;
        echo_state_ = speex_echo_state_init(frame_size_, filter_length);
        speex_echo_ctl(echo_state_, SPEEX_ECHO_SET_SAMPLING_RATE, &sample_rate_);

        // 残差回声抑制 + 噪声抑制 + AGC
        preprocess_state_ = speex_preprocess_state_init(frame_size_, sample_rate_);
        speex_preprocess_ctl(preprocess_state_, SPEEX_PREPROCESS_SET_ECHO_STATE, echo_state_);

        int enabled = 1;
        speex_preprocess_ctl(preprocess_state_, SPEEX_PREPROCESS_SET_DENOISE, &enabled);

        int noise_suppress = 20;
        speex_preprocess_ctl(preprocess_state_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &noise_suppress);

        speex_preprocess_ctl(preprocess_state_, SPEEX_PREPROCESS_SET_AGC, &enabled);
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
        int rp = echo_read_pos_.load();
        int wp = echo_write_pos_.load();
        int available = (wp - rp + static_cast<int>(echo_buffer_.size()))
                        % static_cast<int>(echo_buffer_.size());
        int to_read = std::min(frames, available);
        for (int i = 0; i < to_read; i++) {
            buffer[i] = echo_buffer_[rp];
            rp = (rp + 1) % static_cast<int>(echo_buffer_.size());
        }
        echo_read_pos_.store(rp);
        // Zero-fill if not enough data
        for (int i = to_read; i < frames; i++) {
            buffer[i] = 0;
        }
        return to_read;
    }

    void processWithAEC(const int16_t* input, int frames, int16_t* output) {
        if (!aec_enabled_ || !echo_state_) {
            std::memcpy(output, input, frames * sizeof(int16_t));
            return;
        }

        // 只在播放进行中时做 AEC + 预处理，避免无回声参考时污染 Speex 内部状态
        if (!playback_active_.load()) {
            std::memcpy(output, input, frames * sizeof(int16_t));
            return;
        }

        int16_t echo_ref[frame_size_];
        readEchoBuffer(echo_ref, frames);

        speex_echo_cancellation(echo_state_, const_cast<int16_t*>(input), echo_ref, output);

        // 残差回声抑制 + 噪声抑制 + AGC
        if (preprocess_state_) {
            speex_preprocess_run(preprocess_state_, output);
        }
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

        const int chunk = 160;
        std::vector<int16_t> buf(chunk);

        while (running_) {
            int frames = snd_pcm_readi(handle, buf.data(), chunk);
            if (frames <= 0) {
                frames = snd_pcm_recover(handle, frames, 0);
                if (frames < 0) break;
                continue;
            }

            if (muted_) continue;

            int16_t processed_buf[chunk];
            processWithAEC(buf.data(), frames, processed_buf);

            std::lock_guard<std::mutex> lock(mutex_);

            if (barge_in_mode_) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - playback_start_time_).count();
                if (elapsed >= 500) {
                    barge_in_vad_.process(processed_buf, frames);

                    if (barge_in_vad_.speech_ended()) {
                        std::vector<int16_t> audio = barge_in_vad_.take_buffer();
                        if (audio.size() > 3200) {
                            utterance_counter_++;
                            std::string path = "/tmp/mose_barge_"
                                + std::to_string(utterance_counter_) + ".wav";
                            write_wav(path, audio, sample_rate_);
                            pending_wav_ = path;
                            has_utterance_.store(true);
                            cv_.notify_one();
                        }
                    } else if (barge_in_vad_.is_speaking() && barge_in_vad_.buffer().size() > 80000) {
                        // 最大时长保护：5秒后强制截断保存
                        std::vector<int16_t> audio = barge_in_vad_.take_buffer();
                        utterance_counter_++;
                        std::string path = "/tmp/mose_barge_"
                            + std::to_string(utterance_counter_) + ".wav";
                        write_wav(path, audio, sample_rate_);
                        pending_wav_ = path;
                        has_utterance_.store(true);
                        cv_.notify_one();
                    }
                }
            } else {
                vad_.process(processed_buf, frames);

                if (vad_.speech_ended()) {
                    std::vector<int16_t> audio = vad_.take_buffer();
                    if (audio.size() > 6400) {
                        utterance_counter_++;
                        std::string path = "/tmp/mose_utt_"
                            + std::to_string(utterance_counter_) + ".wav";
                        write_wav(path, audio, sample_rate_);
                        pending_wav_ = path;
                        has_utterance_.store(true);
                        cv_.notify_one();
                    }
                }
            }
        }

        snd_pcm_close(handle);
    }

    void playbackLoop() {
        while (running_) {
            std::string current_source;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!playback_source_.empty()) {
                    current_source = playback_source_;
                    playback_source_.clear();
                }
            }

            if (!current_source.empty()) {
                playAudioFile(current_source);
            }

            // Wait for next playback request
            {
                std::unique_lock<std::mutex> lock(mutex_);
                playback_cv_.wait_for(lock, std::chrono::milliseconds(50),
                                     [this] { return !running_ || !playback_source_.empty(); });
            }
        }
    }

    void playAudioFile(const std::string& wav_path) {
        std::vector<int16_t> audio_data = read_wav_file(wav_path);
        if (audio_data.empty()) {
            playback_complete_.store(true);
            return;
        }

        // Open ALSA playback device
        snd_pcm_t* pcm = nullptr;
        int err = snd_pcm_open(&pcm, playback_device_.c_str(),
                               SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            std::cerr << "[AlsaAudioCapture] Playback open failed: "
                      << snd_strerror(err) << std::endl;
            playback_complete_.store(true);
            return;
        }

        err = snd_pcm_set_params(pcm,
                                 SND_PCM_FORMAT_S16_LE,
                                 SND_PCM_ACCESS_RW_INTERLEAVED,
                                 1, sample_rate_, 1, 500000);
        if (err < 0) {
            std::cerr << "[AlsaAudioCapture] Playback set params failed: "
                      << snd_strerror(err) << std::endl;
            snd_pcm_close(pcm);
            playback_complete_.store(true);
            return;
        }

        // Reset echo buffer pointers for synchronized AEC
        echo_write_pos_.store(0);
        echo_read_pos_.store(0);

        playback_active_.store(true);
        playback_start_time_ = std::chrono::steady_clock::now();
        playback_complete_.store(false);

        const int chunk = 160;  // 10ms at 16kHz
        size_t pos = 0;

        while (running_ && playback_active_.load() && pos < audio_data.size()) {
            int frames = std::min(chunk, static_cast<int>(audio_data.size() - pos));

            // Write to speaker — snd_pcm_writei blocks, providing natural rate control
            int written = snd_pcm_writei(pcm, audio_data.data() + pos, frames);
            if (written < 0) {
                written = snd_pcm_recover(pcm, written, 0);
                if (written < 0) break;
            }

            // Write same data to echo reference — synchronized with actual playback
            writeEchoReference(audio_data.data() + pos, frames);

            pos += frames;
        }

        if (playback_active_.load()) {
            // Normal completion — drain remaining audio
            snd_pcm_drain(pcm);
        } else {
            // Interrupted — drop immediately
            snd_pcm_drop(pcm);
        }

        playback_active_.store(false);
        snd_pcm_close(pcm);
        playback_complete_.store(true);
    }

    std::vector<int16_t> read_wav_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }

        file.seekg(44);
        std::vector<int16_t> audio_data;
        int16_t sample;

        while (file.read(reinterpret_cast<char*>(&sample), sizeof(sample))) {
            audio_data.push_back(sample);
        }

        return audio_data;
    }

    std::string device_;
    std::string playback_device_;
    int sample_rate_;
    std::atomic<bool> running_;
    std::atomic<bool> muted_;
    std::atomic<bool> barge_in_mode_;

    std::mutex mutex_;
    std::condition_variable cv_;
    VoiceActivityDetector vad_;
    VoiceActivityDetector barge_in_vad_;
    std::string pending_wav_;
    std::atomic<bool> has_utterance_;
    int utterance_counter_;

    // AEC components
    SpeexEchoState* echo_state_;
    SpeexPreprocessState* preprocess_state_;
    bool aec_enabled_;
    int frame_size_;
    std::vector<int16_t> echo_buffer_;
    std::atomic<int> echo_write_pos_;
    std::atomic<int> echo_read_pos_;

    // Playback control
    std::string playback_source_;
    std::condition_variable playback_cv_;
    std::thread playback_thread_;
    std::atomic<bool> playback_active_;
    std::atomic<bool> playback_complete_;
    std::chrono::steady_clock::time_point playback_start_time_;

    std::thread capture_thread_;
};

} // namespace voice
