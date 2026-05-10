#pragma once

#include <string>

namespace camera {

struct CaptureResult {
    std::string image_data;
    std::string format;
    bool success;
    std::string error_message;
};

class CameraCapture {
public:
    virtual ~CameraCapture() {}
    virtual CaptureResult capture() = 0;
};

} // namespace camera
