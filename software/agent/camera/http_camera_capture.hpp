#pragma once

#include <curl/curl.h>
#include <string>
#include "camera/camera_capture.hpp"

namespace camera {

class HttpCameraCapture : public CameraCapture {
public:
    explicit HttpCameraCapture(const std::string& base_url)
        : base_url_(base_url) {
        // Strip trailing slash
        if (!base_url_.empty() && base_url_.back() == '/') {
            base_url_.pop_back();
        }
    }

    CaptureResult capture() override {
        CaptureResult result;
        result.format = "jpeg";

        std::string url = base_url_ + "/snapshot";

        CURL* curl = curl_easy_init();
        if (!curl) {
            result.success = false;
            result.error_message = "Failed to initialize curl";
            return result;
        }

        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            result.success = false;
            result.error_message = std::string("curl error: ") + curl_easy_strerror(res);
            curl_easy_cleanup(curl);
            return result;
        }

        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);

        if (status != 200) {
            result.success = false;
            result.error_message = "HTTP " + std::to_string(status) + ": " + body;
            return result;
        }

        result.image_data = std::move(body);
        result.success = true;
        return result;
    }

private:
    std::string base_url_;

    static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
        size_t total = size * nmemb;
        std::string* str = static_cast<std::string*>(userdata);
        str->append(static_cast<char*>(ptr), total);
        return total;
    }
};

} // namespace camera
