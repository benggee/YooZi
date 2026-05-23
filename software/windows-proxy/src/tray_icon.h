#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QSettings>

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject* parent = nullptr);

    void setConnected(bool connected);

signals:
    void quitRequested();
    void showMainWindowRequested();
    void settingsRequested();

private:
    void createIcon(bool connected);

    QSystemTrayIcon tray_;
    QMenu menu_;
    QAction* status_action_ = nullptr;
    bool connected_ = false;
};
