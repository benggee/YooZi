#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <cstring>

#include "speech/speech_synthesizer.hpp"
#include "speech/alibaba_config.hpp"
#include "nlsClient.h"
#include "dashCosyVoiceSynthesizerRequest.h"
#include "nlsEvent.h"
#include "common/logger.hpp"

namespace speech {

struct SynthContext {
    std::vector<uint8_t> audio_data;
    bool completed = false;
    bool failed = false;
    std::string error_message;
    std::mutex mtx;
    std::condition_variable cv;
};

struct SynthStreamContext {
    AudioChunkCallback callback;
    bool completed = false;
    bool failed = false;
    bool first_chunk_received = false;
    std::string error_message;
    std::mutex mtx;
    std::condition_variable cv;
};

class AlibabaSpeechSynthesizer : public SpeechSynthesizer {
public:
    AlibabaSpeechSynthesizer(const AlibabaConfig& config,
                              AlibabaNls::NlsClient* client)
        : config_(config), client_(client) {}

    SynthesisResult synthesize(const std::string& text,
                                const std::string& output_path,
                                const std::string& format,
                                int sample_rate,
                                const std::string& voice,
                                float speech_rate = 1.0f) override {
        SynthesisResult result;
        result.success = false;

        if (config_.dashscope_api_key.empty()) {
            result.error_message = "DASHSCOPE_API_KEY not set (needed for CosyVoice)";
            return result;
        }

        SynthContext ctx;

        AlibabaNls::DashCosyVoiceSynthesizerRequest* request =
            client_->createDashCosyVoiceSynthesizerRequest();
        if (!request) {
            result.error_message = "Failed to create CosyVoice TTS request";
            return result;
        }

        // Set callbacks
        request->setOnBinaryDataReceived(onBinaryData, &ctx);
        request->setOnSynthesisCompleted(onCompleted, &ctx);
        request->setOnTaskFailed(onTaskFailed, &ctx);
        request->setOnChannelClosed(onChannelClosed, &ctx);

        // Set parameters
        request->setUrl("wss://dashscope.aliyuncs.com/api-ws/v1/inference");
        request->setAPIKey(config_.dashscope_api_key.c_str());
        request->setModel("cosyvoice-v3-flash");
        request->setVoice(voice.c_str());
        request->setFormat(format.c_str());
        request->setSampleRate(sample_rate);
        request->setVolume(95);
        request->setSpeechRate(speech_rate);
        request->setTimeout(5000);
        request->setSingleRoundText(text.c_str());

        int ret = request->start();
        if (ret < 0) {
            result.error_message = "TTS start failed: " + std::to_string(ret);
            client_->releaseDashCosyVoiceSynthesizerRequest(request);
            return result;
        }

        // Wait for completion (with timeout)
        {
            std::unique_lock<std::mutex> lock(ctx.mtx);
            if (!ctx.cv.wait_for(lock, std::chrono::seconds(30), [&ctx] {
                return ctx.completed || ctx.failed;
            })) {
                result.error_message = "TTS timeout";
                request->cancel();
                client_->releaseDashCosyVoiceSynthesizerRequest(request);
                return result;
            }
        }

        client_->releaseDashCosyVoiceSynthesizerRequest(request);

        if (ctx.failed) {
            result.error_message = ctx.error_message;
            return result;
        }

        // Write audio data to file
        if (ctx.audio_data.empty()) {
            result.error_message = "TTS returned empty audio";
            return result;
        }

        std::ofstream out(output_path, std::ios::binary);
        if (!out.is_open()) {
            result.error_message = "Failed to write audio file: " + output_path;
            return result;
        }
        out.write(reinterpret_cast<const char*>(ctx.audio_data.data()),
                  ctx.audio_data.size());
        out.close();

        result.output_file_path = output_path;
        result.success = true;
        return result;
    }

    StreamSynthesisResult synthesizeStream(
        const std::string& text,
        int sample_rate,
        const std::string& voice,
        float speech_rate,
        AudioChunkCallback callback) override {

        StreamSynthesisResult result;

        if (config_.dashscope_api_key.empty()) {
            result.error_message = "DASHSCOPE_API_KEY not set";
            return result;
        }

        SynthStreamContext ctx;
        ctx.callback = callback;

        AlibabaNls::DashCosyVoiceSynthesizerRequest* request =
            client_->createDashCosyVoiceSynthesizerRequest();
        if (!request) {
            result.error_message = "Failed to create CosyVoice TTS request";
            return result;
        }

        request->setOnBinaryDataReceived(onBinaryDataStream, &ctx);
        request->setOnSynthesisCompleted(onCompletedStream, &ctx);
        request->setOnTaskFailed(onTaskFailedStream, &ctx);
        request->setOnChannelClosed(onChannelClosedStream, &ctx);

        request->setUrl("wss://dashscope.aliyuncs.com/api-ws/v1/inference");
        request->setAPIKey(config_.dashscope_api_key.c_str());
        request->setModel("cosyvoice-v3-flash");
        request->setVoice(voice.c_str());
        request->setFormat("pcm");
        request->setSampleRate(sample_rate);
        request->setVolume(95);
        request->setSpeechRate(speech_rate);
        request->setTimeout(5000);
        request->setSingleRoundText(text.c_str());

        int ret = request->start();
        if (ret < 0) {
            result.error_message = "TTS stream start failed: " + std::to_string(ret);
            client_->releaseDashCosyVoiceSynthesizerRequest(request);
            return result;
        }

        {
            std::unique_lock<std::mutex> lock(ctx.mtx);
            if (!ctx.cv.wait_for(lock, std::chrono::seconds(30), [&ctx] {
                return ctx.completed || ctx.failed;
            })) {
                result.error_message = "TTS stream timeout";
                request->cancel();
                client_->releaseDashCosyVoiceSynthesizerRequest(request);
                return result;
            }
        }

        client_->releaseDashCosyVoiceSynthesizerRequest(request);

        if (ctx.failed) {
            result.error_message = ctx.error_message;
            return result;
        }

        result.success = true;
        return result;
    }

private:
    static void onBinaryData(AlibabaNls::NlsEvent* event, void* param) {
        SynthContext* ctx = static_cast<SynthContext*>(param);
        std::vector<unsigned char> data = event->getBinaryData();
        if (!data.empty()) {
            std::lock_guard<std::mutex> lock(ctx->mtx);
            ctx->audio_data.insert(ctx->audio_data.end(),
                                    data.begin(), data.end());
        }
    }

    static void onCompleted(AlibabaNls::NlsEvent* event, void* param) {
        SynthContext* ctx = static_cast<SynthContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->completed = true;
        ctx->cv.notify_one();
    }

    static void onTaskFailed(AlibabaNls::NlsEvent* event, void* param) {
        SynthContext* ctx = static_cast<SynthContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->failed = true;
        ctx->error_message = "TTS error [" + std::to_string(event->getStatusCode())
            + "]: " + (event->getErrorMessage() ? event->getErrorMessage() : "unknown");
        ctx->cv.notify_one();
    }

    static void onChannelClosed(AlibabaNls::NlsEvent* event, void* param) {
        SynthContext* ctx = static_cast<SynthContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        if (!ctx->completed && !ctx->failed) {
            ctx->completed = true;
        }
        ctx->cv.notify_one();
    }

    // --- Streaming callbacks ---

    static void onBinaryDataStream(AlibabaNls::NlsEvent* event, void* param) {
        SynthStreamContext* ctx = static_cast<SynthStreamContext*>(param);
        std::vector<unsigned char> data = event->getBinaryData();
        if (data.empty()) return;

        const uint8_t* raw = data.data();
        size_t len = data.size();

        // Guard: skip WAV header if server returns one despite pcm format
        if (!ctx->first_chunk_received) {
            ctx->first_chunk_received = true;
            if (len >= 44 && raw[0] == 'R' && raw[1] == 'I'
                          && raw[2] == 'F' && raw[3] == 'F') {
                raw += 44;
                len -= 44;
            }
        }

        if (len > 0 && ctx->callback) {
            ctx->callback(raw, len);
        }
    }

    static void onCompletedStream(AlibabaNls::NlsEvent* event, void* param) {
        SynthStreamContext* ctx = static_cast<SynthStreamContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->completed = true;
        ctx->cv.notify_one();
    }

    static void onTaskFailedStream(AlibabaNls::NlsEvent* event, void* param) {
        SynthStreamContext* ctx = static_cast<SynthStreamContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->failed = true;
        ctx->error_message = "TTS stream error [" + std::to_string(event->getStatusCode())
            + "]: " + (event->getErrorMessage() ? event->getErrorMessage() : "unknown");
        ctx->cv.notify_one();
    }

    static void onChannelClosedStream(AlibabaNls::NlsEvent* event, void* param) {
        SynthStreamContext* ctx = static_cast<SynthStreamContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        if (!ctx->completed && !ctx->failed) {
            ctx->completed = true;
        }
        ctx->cv.notify_one();
    }

    AlibabaConfig config_;
    AlibabaNls::NlsClient* client_;
};

} // namespace speech
