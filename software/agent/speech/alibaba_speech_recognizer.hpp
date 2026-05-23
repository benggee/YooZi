#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <condition_variable>

#include "speech/speech_recognizer.hpp"
#include "speech/alibaba_config.hpp"
#include "nlsClient.h"
#include "speechRecognizerRequest.h"
#include "nlsEvent.h"
#include "common/logger.hpp"

namespace speech {

struct AsrContext {
    std::string result_text;
    bool completed = false;
    bool failed = false;
    bool started = false;
    std::string error_message;
    std::mutex mtx;
    std::condition_variable cv;
};

class AlibabaSpeechRecognizer : public SpeechRecognizer {
public:
    AlibabaSpeechRecognizer(const AlibabaConfig& config,
                             AlibabaNls::NlsClient* client)
        : config_(config), client_(client) {}

    RecognitionResult recognize(const std::string& audio_file_path,
                                 const std::string& format,
                                 int sample_rate) override {
        RecognitionResult result;
        result.success = false;

        std::vector<uint8_t> audio_data = read_binary_file(audio_file_path);
        if (audio_data.empty()) {
            result.error_message = "Failed to read audio file: " + audio_file_path;
            return result;
        }

        // Detect WAV header and determine actual format + data offset
        std::string actual_format = format;
        size_t data_offset = 0;
        if (audio_data.size() >= 44 &&
            audio_data[0] == 'R' && audio_data[1] == 'I' &&
            audio_data[2] == 'F' && audio_data[3] == 'F') {
            // WAV file detected: SDK only supports pcm/opus/opu, not "wav"
            actual_format = "pcm";
            data_offset = 44;  // skip 44-byte RIFF header
        }

        AsrContext ctx;

        AlibabaNls::SpeechRecognizerRequest* request =
            client_->createRecognizerRequest();
        if (!request) {
            result.error_message = "Failed to create ASR request";
            return result;
        }

        // Set callbacks
        request->setOnRecognitionStarted(onRecognitionStarted, &ctx);
        request->setOnRecognitionCompleted(onRecognitionCompleted, &ctx);
        request->setOnRecognitionResultChanged(onRecognitionResultChanged, &ctx);
        request->setOnTaskFailed(onTaskFailed, &ctx);
        request->setOnChannelClosed(onChannelClosed, &ctx);

        // Set parameters
        request->setAppKey(config_.appkey.c_str());
        request->setToken(config_.token.c_str());
        request->setFormat(actual_format.c_str());
        request->setSampleRate(sample_rate);
        request->setPunctuationPrediction(true);
        request->setInverseTextNormalization(true);
        request->setTimeout(5000);

        int ret = request->start();
        if (ret < 0) {
            result.error_message = "ASR start failed: " + std::to_string(ret);
            client_->releaseRecognizerRequest(request);
            return result;
        }

        // Wait for recognition started
        {
            std::unique_lock<std::mutex> lock(ctx.mtx);
            if (!ctx.cv.wait_for(lock, std::chrono::seconds(10), [&ctx] {
                return ctx.started || ctx.failed;
            })) {
                result.error_message = "ASR start timeout";
                request->cancel();
                client_->releaseRecognizerRequest(request);
                return result;
            }
        }

        if (ctx.failed) {
            result.error_message = ctx.error_message;
            client_->releaseRecognizerRequest(request);
            return result;
        }

        // Send audio data in chunks (skip WAV header if present)
        const size_t chunk_size = 6400;  // ~200ms of 16kHz 16-bit audio
        size_t offset = data_offset;
        while (offset < audio_data.size()) {
            size_t len = std::min(chunk_size, audio_data.size() - offset);
            ret = request->sendAudio(audio_data.data() + offset, len);
            if (ret < 0) {
                logger::warn("ASR", "sendAudio failed at offset " + std::to_string(offset));
                break;
            }
            offset += len;
            // Small delay between chunks to avoid overwhelming the SDK
            usleep(20000);  // 20ms
        }

        // Stop recognition (triggers RecognitionCompleted callback)
        request->stop();

        // Wait for completion
        {
            std::unique_lock<std::mutex> lock(ctx.mtx);
            if (!ctx.cv.wait_for(lock, std::chrono::seconds(15), [&ctx] {
                return ctx.completed || ctx.failed;
            })) {
                result.error_message = "ASR completion timeout";
                request->cancel();
                client_->releaseRecognizerRequest(request);
                return result;
            }
        }

        client_->releaseRecognizerRequest(request);

        if (ctx.failed) {
            result.error_message = ctx.error_message;
            return result;
        }

        result.text = ctx.result_text;
        result.success = true;
        return result;
    }

private:
    static void onRecognitionStarted(AlibabaNls::NlsEvent* event, void* param) {
        AsrContext* ctx = static_cast<AsrContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->started = true;
        ctx->cv.notify_one();
    }

    static void onRecognitionResultChanged(AlibabaNls::NlsEvent* event, void* param) {
        // Intermediate results - not needed for one-shot recognition
    }

    static void onRecognitionCompleted(AlibabaNls::NlsEvent* event, void* param) {
        AsrContext* ctx = static_cast<AsrContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        const char* result = event->getResult();
        if (result) {
            ctx->result_text = result;
        }
        ctx->completed = true;
        ctx->cv.notify_one();
    }

    static void onTaskFailed(AlibabaNls::NlsEvent* event, void* param) {
        AsrContext* ctx = static_cast<AsrContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ctx->failed = true;
        ctx->error_message = "ASR error [" + std::to_string(event->getStatusCode())
            + "]: " + (event->getErrorMessage() ? event->getErrorMessage() : "unknown");
        ctx->cv.notify_one();
    }

    static void onChannelClosed(AlibabaNls::NlsEvent* event, void* param) {
        AsrContext* ctx = static_cast<AsrContext*>(param);
        std::lock_guard<std::mutex> lock(ctx->mtx);
        if (!ctx->completed && !ctx->failed) {
            ctx->completed = true;
        }
        ctx->cv.notify_one();
    }

    std::vector<uint8_t> read_binary_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};
        std::vector<uint8_t> data((
            std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        return data;
    }

    AlibabaConfig config_;
    AlibabaNls::NlsClient* client_;
};

} // namespace speech
