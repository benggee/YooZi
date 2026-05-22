#pragma once

#include <iostream>
#include <fstream>
#include <ctime>
#include <mutex>
#include <string>

namespace logger {

enum Level { DEBUG, INFO, WARN, ERROR };

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    void setLevel(Level level) { level_ = level; }

    void setFile(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.close();
        file_.open(path, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << path << std::endl;
        }
    }

    void log(Level level, const std::string& tag, const std::string& message) {
        if (level < level_) return;

        std::time_t now = std::time(nullptr);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

        const char* lvl[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        std::string line = std::string("[") + ts + "] [" + lvl[level] + "] [" + tag + "] " + message;

        std::lock_guard<std::mutex> lock(mutex_);

        if (level >= ERROR) {
            std::cerr << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }

        if (file_.is_open()) {
            file_ << line << std::endl;
            file_.flush();
        }

        if (log_sink_) {
            log_sink_(line);
        }
    }

    using LogSink = void(*)(const std::string&);
    void setLogSink(LogSink sink) { log_sink_ = sink; }

private:
    Logger() : level_(INFO), log_sink_(nullptr) {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Level level_;
    std::ofstream file_;
    std::mutex mutex_;
    LogSink log_sink_;
};

inline void debug(const std::string& tag, const std::string& msg) {
    Logger::instance().log(DEBUG, tag, msg);
}

inline void info(const std::string& tag, const std::string& msg) {
    Logger::instance().log(INFO, tag, msg);
}

inline void warn(const std::string& tag, const std::string& msg) {
    Logger::instance().log(WARN, tag, msg);
}

inline void error(const std::string& tag, const std::string& msg) {
    Logger::instance().log(ERROR, tag, msg);
}

inline void init(const std::string& logDir = "") {
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d", std::localtime(&now));
    std::string path = logDir.empty()
        ? std::string("/tmp/mose_") + buf + ".log"
        : logDir + "/mose_" + buf + ".log";
    Logger::instance().setFile(path);
}

} // namespace logger
