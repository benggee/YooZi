#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUuid>

namespace protocol {

// Message types
inline const QString TYPE_CMD = "cmd";
inline const QString TYPE_NATURAL_LANGUAGE = "natural_language";
inline const QString TYPE_PING = "ping";
inline const QString TYPE_PONG = "pong";
inline const QString TYPE_RESULT = "result";
inline const QString TYPE_STATUS = "status";

// Build a command request
inline QJsonObject makeCmdRequest(const QString& action, const QString& param) {
    return {
        {"type", TYPE_CMD},
        {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"action", action},
        {"param", param}
    };
}

// Build a natural language request
inline QJsonObject makeNaturalLanguageRequest(const QString& text) {
    return {
        {"type", TYPE_NATURAL_LANGUAGE},
        {"request_id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"text", text}
    };
}

// Build a ping
inline QJsonObject makePing() {
    return {{"type", TYPE_PING}};
}

// Build a result response
inline QJsonObject makeResult(const QString& requestId, bool success,
                               const QString& output, const QString& error) {
    return {
        {"type", TYPE_RESULT},
        {"request_id", requestId},
        {"success", success},
        {"output", output},
        {"error", error}
    };
}

// Build a status message
inline QJsonObject makeStatus(const QString& hostname) {
    return {
        {"type", TYPE_STATUS},
        {"hostname", hostname},
        {"status", "connected"}
    };
}

// Serialize to text
inline QString toJson(const QJsonObject& obj) {
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Parse from text
inline QJsonObject fromJson(const QString& text) {
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

} // namespace protocol
