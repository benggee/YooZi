#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <condition_variable>

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
        request->setModel("cosyvoice-v2");
        request->setVoice(voice.c_str());
        request->setFormat(format.c_str());
        request->setSampleRate(sample_rate);
        request->setVolume(50);
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

    AlibabaConfig config_;
    AlibabaNls::NlsClient* client_;
};

} // namespace speech
