#pragma once

#include <curl/curl.h>
#include <string>
#include <vector>
#include <utility>
#include <map>
#include <cstring>

namespace anthropic {
namespace http {

struct HttpResponse {
    long status_code;
    std::string body;
    std::map<std::string, std::string> headers;
};

class HttpClient {
public:
    virtual ~HttpClient() {}
    virtual HttpResponse post(const std::string& url,
                              const std::string& body,
                              const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

class CurlHttpClient : public HttpClient {
public:
    CurlHttpClient() : timeout_seconds_(600), connect_timeout_seconds_(30) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~CurlHttpClient() {
        curl_global_cleanup();
    }

    void set_timeout(long seconds) { timeout_seconds_ = seconds; }
    void set_connect_timeout(long seconds) { connect_timeout_seconds_ = seconds; }

    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::vector<std::pair<std::string, std::string>>& headers) override {
        HttpResponse resp;

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.status_code = 0;
            resp.body = "Failed to initialize curl";
            return resp;
        }

        struct curl_slist* chunk = NULL;
        for (const auto& h : headers) {
            std::string header_str = h.first + ": " + h.second;
            chunk = curl_slist_append(chunk, header_str.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect_timeout_seconds_);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            resp.status_code = 0;
            resp.body = std::string("curl error: ") + curl_easy_strerror(res);
            curl_slist_free_all(chunk);
            curl_easy_cleanup(curl);
            return resp;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
        curl_slist_free_all(chunk);
        curl_easy_cleanup(curl);
        return resp;
    }

private:
    long timeout_seconds_;
    long connect_timeout_seconds_;

    static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
        size_t total = size * nmemb;
        std::string* str = static_cast<std::string*>(userdata);
        str->append(static_cast<char*>(ptr), total);
        return total;
    }
};

} // namespace http
} // namespace anthropic
