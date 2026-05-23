#pragma once

#include <string>
#include <memory>

#include "camera/camera_capture.hpp"
#include "camera/pi_camera_capture.hpp"
#include "camera/http_camera_capture.hpp"
#include "common/logger.hpp"

namespace camera {

class FallbackCameraCapture : public CameraCapture {
public:
    explicit FallbackCameraCapture(const std::string& http_url = "http://127.0.0.1:8080")
        : pi_camera_(), http_camera_(http_url) {}

    CaptureResult capture() override {
        CaptureResult result = pi_camera_.capture();
        if (result.success) {
            logger::info("Camera", "Captured via PiCamera");
            return result;
        }

        logger::info("Camera", "PiCamera failed (" + result.error_message + "), falling back to HTTP");
        result = http_camera_.capture();
        if (result.success) {
            logger::info("Camera", "Captured via HTTP camera_stream");
        }
        return result;
    }

private:
    PiCameraCapture pi_camera_;
    HttpCameraCapture http_camera_;
};

} // namespace camera
