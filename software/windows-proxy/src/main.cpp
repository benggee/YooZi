#include <QApplication>
#include <QSettings>
#include <QSysInfo>

#include "ws_client.h"
#include "command_executor.h"
#include "ollama_client.h"
#include "tray_icon.h"
#include "protocol.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QSettings settings("YooZi", "WindowsProxy");
    QString piHost = settings.value("pi_host", "raspberrypi.local").toString();
    QString wsUrl = QString("ws://%1:8765").arg(piHost);

    WSClient wsClient(wsUrl);
    CommandExecutor executor;
    OllamaClient ollama;
    TrayIcon tray;

    // Connect tray to WSClient status
    QObject::connect(&wsClient, &WSClient::connected,
                     &tray, [&]() { tray.setConnected(true); });
    QObject::connect(&wsClient, &WSClient::disconnected,
                     &tray, [&]() { tray.setConnected(false); });

    // Handle incoming messages
    QObject::connect(&wsClient, &WSClient::messageReceived,
                     [&](const QString& message) {
        QJsonObject msg = protocol::fromJson(message);
        QString type = msg["type"].toString();

        if (type == protocol::TYPE_PING) {
            wsClient.sendMessage(protocol::toJson(
                protocol::makeResult("", true, "pong", "")));
            return;
        }

        QString requestId = msg["request_id"].toString();

        if (type == protocol::TYPE_CMD) {
            QString action = msg["action"].toString();
            QString param = msg["param"].toString();
            auto result = executor.execute(action, param);
            wsClient.sendMessage(protocol::toJson(
                protocol::makeResult(requestId, result.success,
                                      result.output, result.error)));
        }
        else if (type == protocol::TYPE_NATURAL_LANGUAGE) {
            QString text = msg["text"].toString();
            auto interp = ollama.interpret(text);
            if (!interp.success) {
                wsClient.sendMessage(protocol::toJson(
                    protocol::makeResult(requestId, false, "", interp.error)));
                return;
            }
            auto result = executor.execute(interp.action, interp.param);
            wsClient.sendMessage(protocol::toJson(
                protocol::makeResult(requestId, result.success,
                                      result.output, result.error)));
        }
    });

    // Start connecting
    wsClient.connectToServer();

    return app.exec();
}
