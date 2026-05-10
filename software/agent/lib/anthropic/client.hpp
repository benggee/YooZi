#pragma once

#include <cstdlib>
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <map>
#include "types/message.hpp"
#include "json_fwd.hpp"
#include "http/http_client.hpp"
#include "error.hpp"
#include "vendor/nlohmann/json.hpp"

namespace anthropic {

struct ClientOptions {
    std::string api_key;
    std::string base_url = "https://api.anthropic.com/";
    std::string anthropic_version = "2023-06-01";
    int max_retries = 2;
    long timeout_seconds = 600;
    std::map<std::string, std::string> extra_headers;
};

class Client;

class MessageService {
public:
    Message New(const MessageNewParams& params);

private:
    explicit MessageService(Client* client) : client_(client) {}
    Client* client_;
    friend class Client;
};

class Client {
public:
    Client()
        : http_client_(std::make_shared<http::CurlHttpClient>())
        , messages_(this) {
        init_from_env();
        init_curl_timeout();
    }

    explicit Client(const std::string& api_key)
        : http_client_(std::make_shared<http::CurlHttpClient>())
        , messages_(this) {
        init_from_env();
        options_.api_key = api_key;
        init_curl_timeout();
    }

    explicit Client(const ClientOptions& options)
        : options_(options)
        , http_client_(std::make_shared<http::CurlHttpClient>())
        , messages_(this) {
        if (options_.api_key.empty()) {
            const char* key = std::getenv("ANTHROPIC_API_KEY");
            if (key) options_.api_key = key;
        }
        init_curl_timeout();
    }

    MessageService& messages() { return messages_; }
    const ClientOptions& options() const { return options_; }

    void set_http_client(std::shared_ptr<http::HttpClient> client) {
        http_client_ = client;
    }

    Message do_message_request(const MessageNewParams& params) {
        nlohmann::json body_json = params;
        std::string body_str = body_json.dump();

        std::string url = options_.base_url;
        if (!url.empty() && url[url.size() - 1] == '/') {
            url += "messages";
        } else {
            url += "/messages";
        }

        std::vector<std::pair<std::string, std::string>> headers;
        headers.push_back({"Content-Type", "application/json"});
        headers.push_back({"Accept", "application/json"});
        headers.push_back({"x-api-key", options_.api_key});
        headers.push_back({"anthropic-version", options_.anthropic_version});
        for (const auto& eh : options_.extra_headers) {
            headers.push_back({eh.first, eh.second});
        }

        http::HttpResponse resp = http_client_->post(url, body_str, headers);

        if (resp.status_code == 0) {
            throw AnthropicError("Network error: " + resp.body, 0, resp.body);
        }

        if (resp.status_code >= 400) {
            std::string error_msg = "API error";
            try {
                auto err_json = nlohmann::json::parse(resp.body);
                if (err_json.count("error") && err_json["error"].is_object()) {
                    error_msg = err_json["error"].value("message", error_msg);
                }
            } catch (...) {}

            switch (resp.status_code) {
            case 400: throw BadRequestError(error_msg, resp.body);
            case 401: throw AuthenticationError(error_msg, resp.body);
            case 404: throw NotFoundError(error_msg, resp.body);
            case 429: throw RateLimitError(error_msg, resp.body);
            case 529: throw OverloadedError(error_msg, resp.body);
            default:  throw ApiError(error_msg, resp.status_code, resp.body);
            }
        }

        try {
            return nlohmann::json::parse(resp.body).get<Message>();
        } catch (const std::exception& e) {
            throw AnthropicError(std::string("Failed to parse response: ") + e.what(),
                                 static_cast<int>(resp.status_code), resp.body);
        }
    }

private:
    ClientOptions options_;
    std::shared_ptr<http::HttpClient> http_client_;
    MessageService messages_;

    void init_from_env() {
        const char* key = std::getenv("ANTHROPIC_API_KEY");
        if (key) options_.api_key = key;
        const char* base = std::getenv("ANTHROPIC_BASE_URL");
        if (base) options_.base_url = base;
    }

    void init_curl_timeout() {
        auto* curl = dynamic_cast<http::CurlHttpClient*>(http_client_.get());
        if (curl) {
            curl->set_timeout(options_.timeout_seconds);
        }
    }
};

// Defined after Client is complete
inline Message MessageService::New(const MessageNewParams& params) {
    return client_->do_message_request(params);
}

} // namespace anthropic
