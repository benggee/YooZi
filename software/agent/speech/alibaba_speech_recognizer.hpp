#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "speech/speech_recognizer.hpp"
#include "speech/alibaba_config.hpp"
#include "anthropic/http/http_client.hpp"
#include "vendor/nlohmann/json.hpp"

namespace speech {

class AlibabaSpeechRecognizer : public SpeechRecognizer {
public:
    AlibabaSpeechRecognizer(const AlibabaConfig& config,
                             anthropic::http::HttpClient* http_client)
        : config_(config), http_client_(http_client) {}

    RecognitionResult recognize(const std::string& audio_file_path,
                                 const std::string& format,
                                 int sample_rate) override {
        RecognitionResult result;
        result.success = false;

        std::string audio_data = read_binary_file(audio_file_path);
        if (audio_data.empty()) {
            result.error_message = "Failed to read audio file: " + audio_file_path;
            return result;
        }

        std::string url = std::string("https://nls-gateway-cn-shanghai.aliyuncs.com/stream/v1/asr")
            + "?appkey=" + config_.appkey
            + "&format=" + format
            + "&sample_rate=" + std::to_string(sample_rate)
            + "&enable_punctuation_prediction=true"
            + "&enable_inverse_text_normalization=true";

        std::vector<std::pair<std::string, std::string>> headers;
        headers.push_back({"Content-Type", "application/octet-stream"});
        headers.push_back({"X-NLS-Token", config_.token});

        anthropic::http::HttpResponse resp = http_client_->post(url, audio_data, headers);

        if (resp.status_code == 0) {
            result.error_message = "Network error: " + resp.body;
            return result;
        }

        nlohmann::json resp_json;
        try {
            resp_json = nlohmann::json::parse(resp.body);
        } catch (const std::exception& e) {
            result.error_message = std::string("Failed to parse ASR response: ") + e.what();
            return result;
        }

        if (resp_json.count("status") && resp_json["status"].get<int>() != 20000000) {
            result.error_message = "ASR error: " + resp_json.value("message", "unknown error");
            return result;
        }

        if (resp.status_code >= 400) {
            result.error_message = "ASR HTTP error [" + std::to_string(resp.status_code) + "]: " + resp.body;
            return result;
        }

        result.text = resp_json.value("result", std::string());
        result.success = true;
        return result;
    }

private:
    std::string read_binary_file(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return "";
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    AlibabaConfig config_;
    anthropic::http::HttpClient* http_client_;
};

} // namespace speech
