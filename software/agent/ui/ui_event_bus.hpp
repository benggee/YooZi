#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <ctime>

namespace ui {

enum EngineState { SLEEPING = 0, AWAKE = 1, THINKING = 2, SPEAKING = 3 };

struct ToolStatus {
    std::string name;
    std::string args;
    bool finished = false;
    bool error = false;
};

class UIEventBus {
public:
    static UIEventBus& instance() {
        static UIEventBus bus;
        return bus;
    }

    void setEngineState(int state) {
        engine_state_.store(state);
        cv_.notify_one();
    }

    void setAsrText(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        asr_text_ = text;
        addLogLocked("[ASR] " + text);
        cv_.notify_one();
    }

    void setLlmText(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        llm_text_ = text;
        cv_.notify_one();
    }

    void setToolCall(const std::string& name, const std::string& args) {
        std::lock_guard<std::mutex> lock(mutex_);
        tool_status_ = {name, args, false, false};
        addLogLocked("[TOOL] " + name + " " + args);
        cv_.notify_one();
    }

    void setToolResult(const std::string& name, bool error) {
        std::lock_guard<std::mutex> lock(mutex_);
        tool_status_.finished = true;
        tool_status_.error = error;
        addLogLocked("[TOOL] " + name + (error ? " FAIL" : " OK"));
        cv_.notify_one();
    }

    void setVadEnergy(float energy) {
        vad_energy_.store(energy);
    }

    void addLog(const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex_);
        addLogLocked(line);
    }

    // --- FTXUI thread readers ---

    int engineState() const { return engine_state_.load(); }

    float vadEnergy() const { return vad_energy_.load(); }

    std::string asrText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return asr_text_;
    }

    std::string llmText() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return llm_text_;
    }

    ToolStatus toolStatus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tool_status_;
    }

    std::vector<std::string> logLines() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_lines_;
    }

    int logOffset() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return log_offset_;
    }

    // Notification for FTXUI refresh
    std::mutex& cvMutex() { return cv_mutex_; }
    std::condition_variable& cv() { return cv_; }

private:
    UIEventBus() = default;
    UIEventBus(const UIEventBus&) = delete;
    UIEventBus& operator=(const UIEventBus&) = delete;

    void addLogLocked(const std::string& line) {
        std::time_t now = std::time(nullptr);
        char ts[16];
        std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&now));
        std::string entry = std::string(ts) + " " + line;

        if ((int)log_lines_.size() >= MAX_LOG_LINES) {
            log_lines_.erase(log_lines_.begin());
            log_offset_++;
        }
        log_lines_.push_back(entry);
    }

    static const int MAX_LOG_LINES = 100;

    std::atomic<int> engine_state_{SLEEPING};
    std::atomic<float> vad_energy_{0.0f};

    mutable std::mutex mutex_;
    std::string asr_text_;
    std::string llm_text_;
    ToolStatus tool_status_;
    std::vector<std::string> log_lines_;
    int log_offset_ = 0;

    // Separate mutex for CV to avoid blocking UI reads
    std::mutex cv_mutex_;
    std::condition_variable cv_;
};

} // namespace ui
