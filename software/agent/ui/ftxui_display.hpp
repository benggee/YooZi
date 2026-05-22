#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <chrono>
#include <ctime>
#include <functional>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/vt.h>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include "ui/ui_event_bus.hpp"

namespace ui {

class FtxuiDisplay {
public:
    FtxuiDisplay() : running_(false), orig_vt_(-1) {}
    ~FtxuiDisplay() { stop(); }

    void start() {
        running_ = true;
        thread_ = std::thread(&FtxuiDisplay::run, this);
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void run() {
        int ioctl_fd = open("/dev/tty0", O_WRONLY);
        int write_fd = open("/dev/tty1", O_WRONLY);
        if (write_fd < 0) write_fd = STDOUT_FILENO;

        // Save current VT and switch to tty1
        if (ioctl_fd >= 0) {
            struct vt_stat vts;
            if (ioctl(ioctl_fd, VT_GETSTATE, &vts) == 0) {
                orig_vt_ = vts.v_active;
            }
            ioctl(ioctl_fd, VT_ACTIVATE, 1);
            ioctl(ioctl_fd, VT_WAITACTIVE, 1);
        }

        while (running_) {
            auto element = buildUI();
            auto screen = ftxui::Screen::Create(
                ftxui::Dimension::Fixed(80),
                ftxui::Dimension::Fixed(30));
            ftxui::Render(screen, element);
            std::string output = "\033[H" + screen.ToString();
            write(write_fd, output.c_str(), output.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Clear screen
        if (write_fd >= 0 && write_fd != STDOUT_FILENO) {
            write(write_fd, "\033[2J\033[H", 7);
            close(write_fd);
        }
        // Restore original VT
        if (ioctl_fd >= 0) {
            if (orig_vt_ > 0) {
                ioctl(ioctl_fd, VT_ACTIVATE, orig_vt_);
            }
            close(ioctl_fd);
        }
    }

    ftxui::Element buildUI() {
        using namespace ftxui;

        auto& bus = UIEventBus::instance();
        int state = bus.engineState();
        float energy = bus.vadEnergy();
        std::string asr = bus.asrText();
        std::string llm = bus.llmText();
        ToolStatus tool = bus.toolStatus();
        auto logs = bus.logLines();

        // --- Status bar ---
        const char* state_label[] = {"休眠", "唤醒", "思考", "播报"};
        Color state_color[] = {Color::Red, Color::Green, Color::Yellow, Color::Cyan};
        std::string time_str = currentTime();

        auto status_bar = hbox({
            text("[") | color(state_color[state]),
            text(state_label[state]) | color(state_color[state]) | bold,
            text("]") | color(state_color[state]),
            text(" Mose v1.0"),
            filler(),
            text(time_str) | dim,
        });

        // --- VAD energy gauge ---
        float normalized = std::min(energy / 3000.0f, 1.0f);
        auto gauge_bar = hbox({
            text("麦: "),
            gauge(normalized) | color(Color::Green),
            text(" "),
            text(asr.empty() ? std::string("柚子") : asr) | flex | bold,
        });

        // --- LLM response ---
        std::string llm_display = llm.empty() ? std::string(60, ' ') : llm;
        if (llm_display.size() > 78) {
            llm_display = llm_display.substr(0, 75) + "...";
        }
        auto llm_bar = hbox({
            text("我: "),
            text(llm_display) | flex,
        });

        // --- Tool status ---
        Elements log_elements;

        if (!tool.name.empty()) {
            std::string tool_display;
            if (!tool.finished) {
                tool_display = "[" + tool.name + "] ...";
            } else {
                tool_display = "[" + tool.name + "] " + (tool.error ? "FAIL" : "OK");
            }
            Color tool_color = tool.error ? Color::Red : Color::Green;
            log_elements.push_back(text(tool_display) | color(tool_color));
        }

        // --- Log area ---
        int max_log_lines = 19;
        int log_start = 0;
        if ((int)logs.size() > max_log_lines) {
            log_start = (int)logs.size() - max_log_lines;
        }

        for (int i = log_start; i < (int)logs.size(); i++) {
            std::string line = logs[i];
            if (line.size() > 78) {
                line = line.substr(0, 75) + "...";
            }
            Color c = Color::White;
            if (line.find("[ERROR]") != std::string::npos) c = Color::Red;
            else if (line.find("[WARN]") != std::string::npos) c = Color::Yellow;
            else if (line.find("[TOOL]") != std::string::npos) c = Color::Cyan;
            log_elements.push_back(text(line) | color(c));
        }

        // Fill remaining space
        int remaining = max_log_lines - (int)log_elements.size();
        for (int i = 0; i < remaining; i++) {
            log_elements.push_back(text(""));
        }

        auto log_area = vbox(log_elements) | flex | border;

        // --- Compose ---
        return vbox({
            status_bar | border,
            gauge_bar | border,
            llm_bar | border,
            log_area,
        });
    }

    static std::string currentTime() {
        std::time_t now = std::time(nullptr);
        char buf[16];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&now));
        return buf;
    }

    std::atomic<bool> running_;
    std::thread thread_;
    int orig_vt_;
};

} // namespace ui
