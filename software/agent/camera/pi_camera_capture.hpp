#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>

#include "camera/camera_capture.hpp"

namespace camera {

class PiCameraCapture : public CameraCapture {
public:
    explicit PiCameraCapture(const std::string& device = "/dev/video0")
        : device_(device) {}

    CaptureResult capture() override {
        CaptureResult result;
        result.format = "jpeg";
        result.success = false;

        std::string tmp = "/tmp/mose_capture.jpg";

        // Try rpicam-still (Pi Camera on Bookworm+)
        int ret = std::system(
            ("rpicam-still --width 320 --height 240 --timeout 1000 -o "
             + tmp + " 2>/dev/null").c_str());

        // Fallback to libcamera-jpeg (older Pi OS)
        if (ret != 0) {
            ret = std::system(
                ("libcamera-jpeg --width 320 --height 240 --timeout 1000 -o "
                 + tmp + " 2>/dev/null").c_str());
        }

        // Fallback to fswebcam (USB webcam)
        if (ret != 0) {
            ret = std::system(
                ("fswebcam -d " + device_
                 + " -r 320x240 --jpeg 60 --no-banner "
                 + tmp + " 2>/dev/null").c_str());
        }

        if (ret != 0) {
            result.error_message = "Camera capture failed";
            return result;
        }

        std::ifstream file(tmp, std::ios::binary);
        if (!file.is_open()) {
            result.error_message = "Failed to read captured image";
            return result;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        result.image_data = ss.str();
        result.success = true;

        std::remove(tmp.c_str());
        return result;
    }

private:
    std::string device_;
};

} // namespace camera
