#include "settings_dialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QRegularExpressionValidator>
#include <QMessageBox>

SettingsDialog::SettingsDialog(QSettings& settings, QWidget* parent)
    : QDialog(parent), settings_(settings) {
    setWindowTitle("Settings");
    setFixedSize(350, 180);
    setupUI();
    loadSettings();
}

void SettingsDialog::setupUI() {
    QFormLayout* form = new QFormLayout(this);

    hostEdit_ = new QLineEdit;
    hostEdit_->setPlaceholderText("e.g. 127.0.0.1");
    auto* hostValidator = new QRegularExpressionValidator(
        QRegularExpression(R"(^(?:\d{1,3}\.){3}\d{1,3}$|^[a-zA-Z0-9]([a-zA-Z0-9\-\.]*[a-zA-Z0-9])?$)"),
        this);
    hostEdit_->setValidator(hostValidator);
    form->addRow("Host / IP:", hostEdit_);

    portSpin_ = new QSpinBox;
    portSpin_->setRange(1, 65535);
    form->addRow("Port:", portSpin_);

    QHBoxLayout* btnLayout = new QHBoxLayout;

    QPushButton* defaultsBtn = new QPushButton("Restore Defaults");
    connect(defaultsBtn, &QPushButton::clicked, this, &SettingsDialog::onRestoreDefaults);
    btnLayout->addWidget(defaultsBtn);

    btnLayout->addStretch();

    QPushButton* cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    QPushButton* connectBtn = new QPushButton("Connect");
    connectBtn->setDefault(true);
    connect(connectBtn, &QPushButton::clicked, this, &SettingsDialog::onAccept);
    btnLayout->addWidget(connectBtn);

    form->addRow(btnLayout);
}

void SettingsDialog::loadSettings() {
    hostEdit_->setText(settings_.value("pi_host", "127.0.0.1").toString());
    portSpin_->setValue(settings_.value("pi_port", 8765).toInt());
}

void SettingsDialog::onAccept() {
    if (hostEdit_->text().trimmed().isEmpty()) {
        hostEdit_->setText("127.0.0.1");
    }
    settings_.setValue("pi_host", hostEdit_->text().trimmed());
    settings_.setValue("pi_port", portSpin_->value());
    settings_.sync();
    accept();
}

void SettingsDialog::onRestoreDefaults() {
    hostEdit_->setText("127.0.0.1");
    portSpin_->setValue(8765);
}
