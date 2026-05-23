#include "ollama_client.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTimer>

static const char* kSystemPrompt =
    "You are a Windows command interpreter. The user will describe what they want to do on Windows. "
    "You must respond with ONLY a JSON object: {\"action\":\"...\", \"param\":\"...\"}\n"
    "Supported actions:\n"
    "- \"open\": open a program, file, or URL. param is the target (e.g. \"calc.exe\", \"notepad.exe\", \"https://github.com\")\n"
    "- \"run\": run a command and capture output. param is the command string (e.g. \"dir C:\\\\\")\n"
    "- \"close\": close/kill a process. param is the process name (e.g. \"calc.exe\")\n\n"
    "Examples:\n"
    "  \"open calculator\" -> {\"action\":\"open\", \"param\":\"calc.exe\"}\n"
    "  \"open browser and visit github.com\" -> {\"action\":\"open\", \"param\":\"https://github.com\"}\n"
    "  \"list files on C drive\" -> {\"action\":\"run\", \"param\":\"dir C:\\\\\"}\n"
    "  \"close notepad\" -> {\"action\":\"close\", \"param\":\"notepad.exe\"}\n"
    "  \"turn up volume\" -> {\"action\":\"run\", \"param\":\"powershell -c \\\"(New-Object -ComObject WScript.Shell).SendKeys([char]175)\\\"\"}\n"
    "Respond with ONLY the JSON, no other text.";

OllamaClient::OllamaClient(const QString& baseUrl, QObject* parent)
    : QObject(parent), baseUrl_(baseUrl) {}

OllamaClient::InterpretResult OllamaClient::interpret(const QString& text, int timeoutMs) {
    QNetworkAccessManager nam;
    QEventLoop loop;

    QJsonObject body;
    body["model"] = "qwen2.5:3b";
    body["stream"] = false;
    body["messages"] = QJsonArray{
        QJsonObject{{"role", "system"}, {"content", kSystemPrompt}},
        QJsonObject{{"role", "user"}, {"content", text}}
    };

    QNetworkRequest request(QUrl(baseUrl_ + "/api/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = nam.post(request, QJsonDocument(body).toJson());

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(timeoutMs);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();
        reply->deleteLater();
        return {false, "", "", "Ollama error: " + err};
    }

    QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    QString content = resp["message"].toObject()["content"].toString().trimmed();

    // Strip markdown code fences if present
    if (content.startsWith("```")) {
        int start = content.indexOf('\n') + 1;
        int end = content.lastIndexOf("```");
        if (end > start) {
            content = content.mid(start, end - start).trimmed();
        }
    }

    QJsonObject parsed;
    try {
        parsed = QJsonDocument::fromJson(content.toUtf8()).object();
    } catch (...) {
        return {false, "", "", "Failed to parse Ollama response: " + content};
    }

    if (parsed.isEmpty()) {
        return {false, "", "", "Empty response from Ollama: " + content};
    }

    return {true, parsed["action"].toString(), parsed["param"].toString(), ""};
}
