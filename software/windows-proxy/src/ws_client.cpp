#include "ws_client.h"
#include "protocol.h"

WSClient::WSClient(const QString& url, QObject* parent)
    : QObject(parent), url_(url) {
    connect(&ws_, &QWebSocket::connected, this, &WSClient::onConnected);
    connect(&ws_, &QWebSocket::disconnected, this, &WSClient::onDisconnected);
    connect(&ws_, &QWebSocket::textMessageReceived,
            this, &WSClient::onTextMessageReceived);
    connect(&ws_, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &WSClient::onError);

    reconnect_timer_.setInterval(5000);
    reconnect_timer_.setSingleShot(false);
    connect(&reconnect_timer_, &QTimer::timeout, [this]() {
        if (!connected_) connectToServer();
    });

    ping_timer_.setInterval(30000);
    connect(&ping_timer_, &QTimer::timeout, this, &WSClient::onPingTimeout);
}

void WSClient::connectToServer() {
    ws_.open(QUrl(url_));
}

void WSClient::disconnectFromServer() {
    reconnect_timer_.stop();
    ping_timer_.stop();
    ws_.close();
}

void WSClient::sendMessage(const QString& message) {
    if (connected_) {
        ws_.sendTextMessage(message);
    }
}

bool WSClient::isConnected() const {
    return connected_;
}

void WSClient::onConnected() {
    connected_ = true;
    reconnect_timer_.stop();
    ping_timer_.start();
    emit connected();

    // Send initial status
    sendMessage(protocol::toJson(protocol::makeStatus(QSysInfo::machineHostName())));
}

void WSClient::onDisconnected() {
    connected_ = false;
    ping_timer_.stop();
    reconnect_timer_.start();
    emit disconnected();
}

void WSClient::onTextMessageReceived(const QString& message) {
    emit messageReceived(message);
}

void WSClient::onPingTimeout() {
    sendMessage(protocol::toJson(protocol::makePing()));
}

void WSClient::setUrl(const QString& url) {
    url_ = url;
}

void WSClient::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    emit errorOccurred(ws_.errorString());
}
