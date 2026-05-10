#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "speech/speech_synthesizer.hpp"
#include "speech/alibaba_config.hpp"
#include "anthropic/http/http_client.hpp"
#include "vendor/nlohmann/json.hpp"

namespace speech {

class AlibabaSpeechSynthesizer : public SpeechSynthesizer {
public:
    AlibabaSpeechSynthesizer(const AlibabaConfig& config,
                              anthropic::http::HttpClient* http_client)
        : config_(config), http_client_(http_client) {}

    SynthesisResult synthesize(const std::string& text,
                                const std::string& output_path,
                                const std::string& format,
                                int sample_rate,
                                const std::string& voice) override {
        SynthesisResult result;
        result.success = false;

        std::string url = "https://nls-gateway-cn-shanghai.aliyuncs.com/stream/v1/tts";

        nlohmann::json body;
        body["appkey"] = config_.appkey;
        body["token"] = config_.token;
        body["text"] = text;
        body["format"] = format;
        body["sample_rate"] = sample_rate;
        body["voice"] = voice;
        body["volume"] = 50;
        body["speech_rate"] = 0;
        body["pitch_rate"] = 0;

        std::vector<std::pair<std::string, std::string>> headers;
        headers.push_back({"Content-Type", "application/json"});
        headers.push_back({"X-NLS-Token", config_.token});

        anthropic::http::HttpResponse resp = http_client_->post(url, body.dump(), headers);

        if (resp.status_code == 0) {
            result.error_message = "Network error: " + resp.body;
            return result;
        }

        // Try to parse as JSON (error response)
        try {
            nlohmann::json err_json = nlohmann::json::parse(resp.body);
            if (err_json.count("status") && err_json["status"].get<int>() != 20000000) {
                result.error_message = "TTS error: " + err_json.value("message", "unknown error");
                return result;
            }
            if (resp.status_code >= 400) {
                result.error_message = "TTS HTTP error [" + std::to_string(resp.status_code) + "]";
                return result;
            }
        } catch (...) {
            // Not JSON — this is binary audio data, which is what we want
        }

        // Write binary audio to output file
        std::ofstream out(output_path, std::ios::binary);
        if (!out.is_open()) {
            result.error_message = "Failed to write audio file: " + output_path;
            return result;
        }
        out.write(resp.body.data(), resp.body.size());
        out.close();

        result.output_file_path = output_path;
        result.success = true;
        return result;
    }

private:
    AlibabaConfig config_;
    anthropic::http::HttpClient* http_client_;
};

} // namespace speech
