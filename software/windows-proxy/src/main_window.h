#pragma once

#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QJsonObject>

class WSClient;
class CommandExecutor;
class TrayIcon;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    void startConnection();

private slots:
    void onConnected();
    void onDisconnected();
    void onMessageReceived(const QString& message);
    void onShowSettings();
    void onClearLog();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUI();
    void setupConnections();
    void appendLog(const QString& text, const QString& level = "info");
    void updateConnectionStatus();
    void handleCommand(const QJsonObject& msg);
    void buildWsUrl();

    WSClient* wsClient_;
    CommandExecutor* executor_;
    TrayIcon* tray_;
    QSettings settings_;
    QString wsUrl_;

    QLabel* statusDot_;
    QLabel* statusLabel_;
    QLabel* urlLabel_;
    QPlainTextEdit* logEdit_;
    QPushButton* settingsBtn_;
    QPushButton* clearLogBtn_;
};
