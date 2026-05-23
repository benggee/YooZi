#pragma once

#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include <QString>

class WSClient : public QObject {
    Q_OBJECT
public:
    explicit WSClient(const QString& url, QObject* parent = nullptr);

    void connectToServer();
    void disconnectFromServer();
    void sendMessage(const QString& message);
    bool isConnected() const;
    void setUrl(const QString& url);

signals:
    void connected();
    void disconnected();
    void messageReceived(const QString& message);
    void errorOccurred(const QString& error);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString& message);
    void onPingTimeout();
    void onError(QAbstractSocket::SocketError error);

private:
    QWebSocket ws_;
    QString url_;
    QTimer reconnect_timer_;
    QTimer ping_timer_;
    bool connected_ = false;
};
