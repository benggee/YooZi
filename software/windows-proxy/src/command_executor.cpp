#include "command_executor.h"
#include <QProcess>
#include <QElapsedTimer>

CommandExecutor::CommandExecutor(QObject* parent)
    : QObject(parent) {}

CommandExecutor::Result CommandExecutor::execute(const QString& action, const QString& param) {
    if (action == "open") {
        // Open program or URL
        // Use cmd.exe /c start for URLs and general programs
        return execProcess("cmd.exe", {"/c", "start", "", param}, true);
    } else if (action == "run") {
        // Run command and capture output
        return execProcess("cmd.exe", {"/c", param}, false);
    } else if (action == "close") {
        // Kill process by name
        return execProcess("taskkill", {"/IM", param, "/F"}, false);
    } else {
        return {false, "", "Unknown action: " + action};
    }
}

CommandExecutor::Result CommandExecutor::execProcess(
        const QString& program, const QStringList& args,
        bool detached, int timeoutMs) {
    if (detached) {
        bool ok = QProcess::startDetached(program, args);
        if (ok) {
            return {true, "Started: " + args.join(" "), ""};
        }
        return {false, "", "Failed to start: " + program};
    }

    QProcess proc;
    QElapsedTimer timer;
    timer.start();

    proc.start(program, args);
    if (!proc.waitForStarted(5000)) {
        return {false, "", "Failed to start process: " + proc.errorString()};
    }

    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(3000);
        return {false, "", "Process timed out"};
    }

    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QString err = QString::fromLocal8Bit(proc.readAllStandardError());

    if (proc.exitCode() != 0) {
        return {false, output, err};
    }

    return {true, output.isEmpty() ? "Done" : output, ""};
}
