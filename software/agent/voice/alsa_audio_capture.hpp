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
        , has_utterance_(false)
        , utterance_counter_(0)
        , vad_(sample_rate, 1500.0f, 15, 30)
        , playback_active_(false)
        , playback_complete_(true) {}

    ~AlsaAudioCapture() {
        stop();
    }

    void start() override {
        running_ = true;
        muted_ = false;
        has_utterance_.store(false);
        playback_active_ = false;
        playback_complete_ = true;
        vad_.reset();

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
        has_utterance_.store(false);
        pending_wav_.clear();
    }

    void setMuted(bool muted) override {
        muted_ = muted;
        if (muted) flush();
    }

    void setPlaybackSource(const std::string& wav_path) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            playback_source_ = wav_path;
            playback_complete_.store(false);
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

            std::lock_guard<std::mutex> lock(mutex_);

            vad_.process(buf.data(), frames);

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

        playback_active_.store(true);
        playback_complete_.store(false);

        const int chunk = 160;  // 10ms at 16kHz
        size_t pos = 0;

        while (running_ && playback_active_.load() && pos < audio_data.size()) {
            int frames = std::min(chunk, static_cast<int>(audio_data.size() - pos));

            int written = snd_pcm_writei(pcm, audio_data.data() + pos, frames);
            if (written < 0) {
                written = snd_pcm_recover(pcm, written, 0);
                if (written < 0) break;
            }

            pos += frames;
        }

        snd_pcm_drain(pcm);

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

    std::mutex mutex_;
    std::condition_variable cv_;
    VoiceActivityDetector vad_;
    std::string pending_wav_;
    std::atomic<bool> has_utterance_;
    int utterance_counter_;

    // Playback control
    std::string playback_source_;
    std::condition_variable playback_cv_;
    std::thread playback_thread_;
    std::atomic<bool> playback_active_;
    std::atomic<bool> playback_complete_;

    std::thread capture_thread_;
};

} // namespace voice
