#pragma once

#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <atomic>
#include <vector>

#include <libwebsockets.h>

#include "common/logger.hpp"

namespace ws {

using MessageCallback = std::function<void(const std::string& client_id, const std::string& message)>;
using ConnectCallback = std::function<void(const std::string& client_id)>;
using DisconnectCallback = std::function<void(const std::string& client_id)>;

class WSServer {
public:
    explicit WSServer(int port = 8765)
        : port_(port), running_(false), context_(nullptr) {}

    ~WSServer() {
        stop();
    }

    void start() {
        if (running_) return;
        running_ = true;
        thread_ = std::thread(&WSServer::run, this);
        logger::info("WSServer", "Starting on port " + std::to_string(port_));
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        if (context_) {
            lws_cancel_service(context_);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        logger::info("WSServer", "Stopped");
    }

    bool send(const std::string& client_id, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = clients_.find(client_id);
        if (it == clients_.end()) return false;

        // Prepend LWS preamble
        auto& buf = it->second.tx_queue;
        buf.push_back(std::string((char)LWS_PRE, LWS_PRE) + message);

        if (it->second.wsi) {
            lws_callback_on_writable(it->second.wsi);
        }
        return true;
    }

    bool broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (clients_.empty()) return false;
        for (auto& [id, info] : clients_) {
            info.tx_queue.push_back(std::string((char)LWS_PRE, LWS_PRE) + message);
            if (info.wsi) {
                lws_callback_on_writable(info.wsi);
            }
        }
        return true;
    }

    bool hasConnectedClient() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !clients_.empty();
    }

    void onMessage(MessageCallback cb) { msg_cb_ = std::move(cb); }
    void onConnect(ConnectCallback cb) { connect_cb_ = std::move(cb); }
    void onDisconnect(DisconnectCallback cb) { disconnect_cb_ = std::move(cb); }

private:
    struct ClientInfo {
        lws* wsi = nullptr;
        std::string id;
        std::vector<std::string> tx_queue;
        std::string rx_buffer;
    };

    void run() {
        static const lws_protocols protocols[] = {
            {
                "default",
                [](lws* wsi, enum lws_callback_reasons reason,
                   void* user, void* in, size_t len) -> int {
                    auto* server = static_cast<WSServer*>(lws_context_user(lws_get_context(wsi)));
                    return server->callback(wsi, reason, user, in, len);
                },
                sizeof(ClientInfo*),
                65536,
                0, nullptr, 0
            },
            {nullptr, nullptr, 0, 0, 0, nullptr, 0}
        };

        lws_context_creation_info info = {};
        info.port = port_;
        info.protocols = protocols;
        info.user = this;
        info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

        context_ = lws_create_context(&info);
        if (!context_) {
            logger::error("WSServer", "Failed to create context");
            running_ = false;
            return;
        }

        while (running_) {
            lws_service(context_, 100);
        }

        lws_context_destroy(context_);
        context_ = nullptr;
    }

    int callback(lws* wsi, enum lws_callback_reasons reason,
                 void* user, void* in, size_t len) {
        switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            std::string id = "ws_" + std::to_string((uintptr_t)wsi);
            auto* client = new ClientInfo();
            client->wsi = wsi;
            client->id = id;
            *(ClientInfo**)user = client;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_[id] = *client;
            }
            delete client;
            // Re-read from map
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_[id].wsi = wsi;
                *(ClientInfo**)user = &clients_[id];
            }

            logger::info("WSServer", "Client connected: " + id);
            if (connect_cb_) connect_cb_(id);
            break;
        }
        case LWS_CALLBACK_RECEIVE: {
            auto* client = *(ClientInfo**)user;
            if (!client) break;
            client->rx_buffer.append((char*)in, len);

            if (lws_is_final_fragment(wsi)) {
                std::string msg = std::move(client->rx_buffer);
                client->rx_buffer.clear();
                if (msg_cb_) msg_cb_(client->id, msg);
            }
            break;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            auto* client = *(ClientInfo**)user;
            if (!client || client->tx_queue.empty()) break;

            std::string& buf = client->tx_queue.front();
            lws_write(wsi, (uint8_t*)buf.data() + LWS_PRE,
                      buf.size() - LWS_PRE, LWS_WRITE_TEXT);
            client->tx_queue.erase(client->tx_queue.begin());

            if (!client->tx_queue.empty()) {
                lws_callback_on_writable(wsi);
            }
            break;
        }
        case LWS_CALLBACK_CLOSED: {
            auto* client = *(ClientInfo**)user;
            if (!client) break;
            std::string id = client->id;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_.erase(id);
            }
            logger::info("WSServer", "Client disconnected: " + id);
            if (disconnect_cb_) disconnect_cb_(id);
            break;
        }
        default:
            break;
        }
        return 0;
    }

    int port_;
    std::atomic<bool> running_;
    lws_context* context_;
    std::thread thread_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ClientInfo> clients_;

    MessageCallback msg_cb_;
    ConnectCallback connect_cb_;
    DisconnectCallback disconnect_cb_;
};

} // namespace ws
