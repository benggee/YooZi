#pragma once

#include <string>

namespace voice {

class AudioCapture {
public:
    virtual ~AudioCapture() {}
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void flush() = 0;
    virtual void setMuted(bool muted) = 0;
    virtual std::string waitForUtterance(int timeout_seconds) = 0;
    virtual bool hasPendingUtterance() const = 0;
    virtual void setBargeInMode(bool enabled) = 0;
    virtual void setEchoCancellation(bool enabled) = 0;
    virtual void writeEchoReference(const int16_t* data, int frames) = 0;
    virtual void setPlaybackSource(const std::string& wav_path) = 0;
};

} // namespace voice
