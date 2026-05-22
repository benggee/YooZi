#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <fstream>

#include "common/logger.hpp"

namespace voice {

class LedController {
public:
    LedController(int gpio_pin = 12)
        : gpio_pin_(gpio_pin)
        , sysfs_pin_(-1)
        , value_fd_(-1)
        , mode_(MODE_OFF)
        , running_(false) {}

    ~LedController() {
        stop();
    }

    void start() {
        // Find correct sysfs GPIO number by matching pin to the right gpiochip
        sysfs_pin_ = findSysfsGpio(gpio_pin_);
        if (sysfs_pin_ < 0) {
            logger::error("LedController", "Failed to find sysfs GPIO for pin " + std::to_string(gpio_pin_));
            return;
        }
        logger::info("LedController", "GPIO" + std::to_string(gpio_pin_)
            + " -> sysfs gpio" + std::to_string(sysfs_pin_));

        // Export GPIO
        {
            std::ofstream exp("/sys/class/gpio/export");
            if (exp.is_open()) {
                exp << sysfs_pin_;
            }
        }
        usleep(200000); // Wait for sysfs to create node

        // Set direction to output
        std::string dir_path = "/sys/class/gpio/gpio"
            + std::to_string(sysfs_pin_) + "/direction";
        {
            std::ofstream dir(dir_path);
            if (!dir.is_open()) {
                logger::error("LedController", "Failed to open direction: " + dir_path);
                return;
            }
            dir << "out";
        }

        // Open value file and keep fd for fast toggling
        std::string val_path = "/sys/class/gpio/gpio"
            + std::to_string(sysfs_pin_) + "/value";
        value_fd_ = open(val_path.c_str(), O_WRONLY);
        if (value_fd_ < 0) {
            logger::error("LedController", "Failed to open GPIO value: " + val_path);
            return;
        }

        running_ = true;
        thread_ = std::thread(&LedController::breathingLoop, this);

        logger::info("LedController", "GPIO" + std::to_string(sysfs_pin_) + " initialized");
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (value_fd_ >= 0) {
            // Turn off before closing
            writeGpio(false);
            close(value_fd_);
            value_fd_ = -1;
        }
        // Unexport GPIO
        if (sysfs_pin_ >= 0) {
            std::ofstream unexp("/sys/class/gpio/unexport");
            if (unexp.is_open()) {
                unexp << sysfs_pin_;
            }
        }
    }

    void setOn() {
        mode_.store(MODE_ON);
    }

    void setBreathing() {
        mode_.store(MODE_BREATHING);
    }

    void setOff() {
        mode_.store(MODE_OFF);
    }

private:
    void writeGpio(bool high) {
        if (value_fd_ < 0) return;
        lseek(value_fd_, 0, SEEK_SET);
        write(value_fd_, high ? "1" : "0", 1);
    }

    void breathingLoop() {
        const int STEPS = 100;
        const int PERIOD_US = 20000; // 20ms PWM period (50Hz)
        const int BREATH_CYCLE_MS = 2500; // Full breath cycle duration

        while (running_) {
            int m = mode_.load();

            if (m == MODE_OFF) {
                writeGpio(false);
                usleep(50000); // Check every 50ms
                continue;
            }

            if (m == MODE_ON) {
                writeGpio(true);
                usleep(50000);
                continue;
            }

            // MODE_BREATHING - software PWM with sine wave brightness
            for (int step = 0; step < STEPS && running_ && mode_.load() == MODE_BREATHING; step++) {
                // Sine wave: 0 → 1 → 0 over STEPS steps
                float t = static_cast<float>(step) / STEPS;
                float brightness = 0.5f * (1.0f - std::cos(2.0f * M_PI * t));

                int on_time = static_cast<int>(brightness * PERIOD_US);
                int off_time = PERIOD_US - on_time;

                if (on_time > 0) {
                    writeGpio(true);
                    usleep(on_time);
                }
                if (off_time > 0) {
                    writeGpio(false);
                    usleep(off_time);
                }
            }
        }
    }

    enum Mode { MODE_OFF = 0, MODE_ON = 1, MODE_BREATHING = 2 };

    static int findSysfsGpio(int pin) {
        DIR* dir = opendir("/sys/class/gpio/");
        if (!dir) return -1;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name(entry->d_name);
            if (name.substr(0, 8) != "gpiochip") continue;

            std::string chip_path = "/sys/class/gpio/" + name + "/";

            int base = -1, ngpio = -1;
            std::ifstream base_f(chip_path + "base");
            if (base_f.is_open()) base_f >> base;
            std::ifstream ngpio_f(chip_path + "ngpio");
            if (ngpio_f.is_open()) ngpio_f >> ngpio;

            // pin offset must be within this chip's GPIO range
            if (base >= 0 && ngpio > 0 && pin < ngpio) {
                closedir(dir);
                return base + pin;
            }
        }
        closedir(dir);
        return -1;
    }

    int gpio_pin_;
    int sysfs_pin_;
    int value_fd_;
    std::atomic<int> mode_;
    std::atomic<bool> running_;
    std::thread thread_;
};

} // namespace voice
