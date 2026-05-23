#include "tray_icon.h"
#include <QPixmap>
#include <QPainter>

TrayIcon::TrayIcon(QObject* parent)
    : QObject(parent) {
    createIcon(false);

    status_action_ = menu_.addAction("Disconnected");
    status_action_->setEnabled(false);
    menu_.addSeparator();

    QAction* showAction = menu_.addAction("Show Window");
    connect(showAction, &QAction::triggered, this, &TrayIcon::showMainWindowRequested);

    QAction* settingsAction = menu_.addAction("Settings");
    connect(settingsAction, &QAction::triggered, this, &TrayIcon::settingsRequested);

    menu_.addSeparator();

    QAction* quitAction = menu_.addAction("Quit");
    connect(quitAction, &QAction::triggered, this, &TrayIcon::quitRequested);
    connect(quitAction, &QAction::triggered, QApplication::instance(), &QApplication::quit);

    tray_.setContextMenu(&menu_);
    tray_.show();
}

void TrayIcon::setConnected(bool connected) {
    connected_ = connected;
    createIcon(connected);
    status_action_->setText(connected ? "Connected" : "Disconnected");
}

void TrayIcon::createIcon(bool connected) {
    // Create a simple colored circle icon
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(connected ? Qt::green : Qt::red);
    p.setPen(Qt::NoPen);
    p.drawEllipse(4, 4, 24, 24);
    tray_.setIcon(pix);
    tray_.setToolTip(connected ? "YooZi Proxy - Connected" : "YooZi Proxy - Disconnected");
}
