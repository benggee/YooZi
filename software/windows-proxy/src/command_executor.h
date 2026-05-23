#pragma once

#include <QObject>
#include <QString>
#include <QPair>

class CommandExecutor : public QObject {
    Q_OBJECT
public:
    explicit CommandExecutor(QObject* parent = nullptr);

    struct Result {
        bool success;
        QString output;
        QString error;
    };

    // Execute a command based on action + param
    Result execute(const QString& action, const QString& param);

private:
    Result execProcess(const QString& program, const QStringList& args,
                       bool detached = false, int timeoutMs = 30000);
};
