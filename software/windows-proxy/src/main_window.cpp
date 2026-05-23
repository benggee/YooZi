#include "main_window.h"
#include "ws_client.h"
#include "command_executor.h"
#include "tray_icon.h"
#include "settings_dialog.h"
#include "protocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPixmap>
#include <QPainter>
#include <QDateTime>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QCloseEvent>
#include <QScrollBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      settings_("YooZi", "WindowsProxy"),
      wsClient_(nullptr),
      executor_(nullptr),
      tray_(nullptr) {
    setWindowTitle("YooZi Windows Proxy");
    resize(600, 400);

    buildWsUrl();

    wsClient_ = new WSClient(wsUrl_, this);
    executor_ = new CommandExecutor(this);
    tray_ = new TrayIcon(this);

    setupUI();
    setupConnections();
}

void MainWindow::buildWsUrl() {
    QString host = settings_.value("pi_host", "127.0.0.1").toString();
    int port = settings_.value("pi_port", 8765).toInt();
    wsUrl_ = QString("ws://%1:%2").arg(host).arg(port);
}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    // Status bar
    QFrame* topBar = new QFrame;
    topBar->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 4, 8, 4);

    statusDot_ = new QLabel;
    QPixmap dot(16, 16);
    dot.fill(Qt::transparent);
    QPainter p(&dot);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::red);
    p.setPen(Qt::NoPen);
    p.drawEllipse(2, 2, 12, 12);
    statusDot_->setPixmap(dot);

    statusLabel_ = new QLabel("Disconnected");
    urlLabel_ = new QLabel(wsUrl_);

    topLayout->addWidget(statusDot_);
    topLayout->addWidget(statusLabel_);
    topLayout->addSpacing(16);
    topLayout->addWidget(urlLabel_);
    topLayout->addStretch();

    settingsBtn_ = new QPushButton("Config");
    topLayout->addWidget(settingsBtn_);

    mainLayout->addWidget(topBar);

    // Log area
    logEdit_ = new QPlainTextEdit;
    logEdit_->setReadOnly(true);
    logEdit_->setMaxBlockCount(5000);
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(9);
    logEdit_->setFont(monoFont);
    mainLayout->addWidget(logEdit_);

    // Bottom bar
    QHBoxLayout* bottomLayout = new QHBoxLayout;
    bottomLayout->addStretch();

    clearLogBtn_ = new QPushButton("Clear Log");
    bottomLayout->addWidget(clearLogBtn_);

    mainLayout->addLayout(bottomLayout);
}

void MainWindow::setupConnections() {
    connect(wsClient_, &WSClient::connected, this, &MainWindow::onConnected);
    connect(wsClient_, &WSClient::disconnected, this, &MainWindow::onDisconnected);
    connect(wsClient_, &WSClient::messageReceived, this, &MainWindow::onMessageReceived);
    connect(wsClient_, &WSClient::errorOccurred, this, [this](const QString& err) {
        appendLog(err, "error");
    });

    connect(tray_, &TrayIcon::showMainWindowRequested, this, [this]() {
        show();
        raise();
        activateWindow();
    });
    connect(tray_, &TrayIcon::settingsRequested, this, &MainWindow::onShowSettings);
    connect(tray_, &TrayIcon::quitRequested, qApp, &QApplication::quit);

    connect(settingsBtn_, &QPushButton::clicked, this, &MainWindow::onShowSettings);
    connect(clearLogBtn_, &QPushButton::clicked, this, &MainWindow::onClearLog);
}

void MainWindow::startConnection() {
    appendLog("Connecting to " + wsUrl_);
    wsClient_->connectToServer();
}

void MainWindow::onConnected() {
    tray_->setConnected(true);
    updateConnectionStatus();
    appendLog("Connected");
}

void MainWindow::onDisconnected() {
    tray_->setConnected(false);
    updateConnectionStatus();
    appendLog("Disconnected, will retry...", "error");
}

void MainWindow::onMessageReceived(const QString& message) {
    QJsonObject msg = protocol::fromJson(message);
    QString type = msg["type"].toString();

    if (type == protocol::TYPE_PING) {
        wsClient_->sendMessage(protocol::toJson(
            protocol::makeResult("", true, "pong", "")));
        return;
    }

    if (type == protocol::TYPE_CMD) {
        handleCommand(msg);
    }
}

void MainWindow::handleCommand(const QJsonObject& msg) {
    QString requestId = msg["request_id"].toString();
    QString action = msg["action"].toString();
    QString param = msg["param"].toString();

    appendLog(QString("Executing: %1 %2").arg(action, param), "cmd");
    auto result = executor_->execute(action, param);

    if (result.success) {
        appendLog(QString("Result: %1").arg(result.output), "cmd");
    } else {
        appendLog(QString("Error: %1").arg(result.error), "error");
    }

    wsClient_->sendMessage(protocol::toJson(
        protocol::makeResult(requestId, result.success, result.output, result.error)));
}

void MainWindow::onShowSettings() {
    SettingsDialog dlg(settings_, this);
    if (dlg.exec() == QDialog::Accepted) {
        wsClient_->disconnectFromServer();

        buildWsUrl();
        urlLabel_->setText(wsUrl_);

        appendLog("Reconnecting to " + wsUrl_);
        wsClient_->setUrl(wsUrl_);
        wsClient_->connectToServer();
    }
}

void MainWindow::onClearLog() {
    logEdit_->clear();
}

void MainWindow::appendLog(const QString& text, const QString& level) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString prefix;
    if (level == "error") prefix = "ERR ";
    else if (level == "cmd") prefix = "CMD ";
    else prefix = "INFO";

    logEdit_->appendPlainText(QString("[%1] %2  %3").arg(timestamp, prefix, text));
    logEdit_->verticalScrollBar()->setValue(logEdit_->verticalScrollBar()->maximum());
}

void MainWindow::updateConnectionStatus() {
    bool connected = wsClient_->isConnected();

    QPixmap dot(16, 16);
    dot.fill(Qt::transparent);
    QPainter p(&dot);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(connected ? Qt::green : Qt::red);
    p.setPen(Qt::NoPen);
    p.drawEllipse(2, 2, 12, 12);
    statusDot_->setPixmap(dot);

    statusLabel_->setText(connected ? "Connected" : "Disconnected");
}

void MainWindow::closeEvent(QCloseEvent* event) {
    hide();
    event->ignore();
}
