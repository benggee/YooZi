#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QSettings>
#include <QPushButton>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QSettings& settings, QWidget* parent = nullptr);

private slots:
    void onAccept();
    void onRestoreDefaults();

private:
    void setupUI();
    void loadSettings();

    QSettings& settings_;
    QLineEdit* hostEdit_;
    QSpinBox* portSpin_;
};
