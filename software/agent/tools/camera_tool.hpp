#pragma once

#include <string>

#include "tools/base_tool.hpp"
#include "camera/camera_capture.hpp"
#include "vendor/base64.hpp"
#include "vendor/nlohmann/json.hpp"

namespace tools {

class CameraTool : public BaseTool {
public:
    explicit CameraTool(camera::CameraCapture* capture)
        : capture_(capture) {}

    std::string name() const override {
        return "camera_capture";
    }

    schema::ToolDefinition definition() const override {
        return schema::ToolDefinition{
            "camera_capture",
            "Take a photo from the webcam and return the base64-encoded image",
            R"schema({
                "type": "object",
                "properties": {
                    "format": {
                        "type": "string",
                        "description": "Image format: jpeg or png (default: jpeg)",
                        "enum": ["jpeg", "png"]
                    }
                },
                "required": []
            })schema"
        };
    }

    std::string execute(const std::string& args_json) override {
        camera::CaptureResult cap = capture_->capture();
        if (!cap.success) {
            throw std::runtime_error("Camera capture failed: " + cap.error_message);
        }

        std::string b64 = base64::encode(cap.image_data);

        nlohmann::json out;
        out["format"] = cap.format;
        out["size"] = static_cast<int>(cap.image_data.size());
        out["base64"] = b64;
        out["message"] = "Photo captured successfully ("
            + std::to_string(cap.image_data.size()) + " bytes). "
            "Image data included for analysis.";
        return out.dump();
    }

private:
    camera::CameraCapture* capture_;
};

} // namespace tools
