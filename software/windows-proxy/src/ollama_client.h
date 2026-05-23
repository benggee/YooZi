#pragma once

#include <QObject>
#include <QString>
#include <QPair>

class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(const QString& baseUrl = "http://localhost:11434",
                          QObject* parent = nullptr);

    struct InterpretResult {
        bool success;
        QString action;
        QString param;
        QString error;
    };

    // Interpret natural language text into action + param
    InterpretResult interpret(const QString& text, int timeoutMs = 30000);

private:
    QString baseUrl_;
};
