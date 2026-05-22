#pragma once

#include <string>
#include <cstdint>
#include <stddef.h>

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
    virtual void setPlaybackSource(const std::string& wav_path) = 0;
    virtual std::string getPendingWav() = 0;
    virtual bool isPlaybackComplete() const = 0;
    virtual void stopPlayback() = 0;
    virtual void setPlaybackDevice(const std::string& device) = 0;

    // Streaming playback: PCM data written directly to ALSA
    virtual bool beginStreamPlayback() = 0;
    virtual void writeStreamPCM(const int16_t* data, size_t num_samples) = 0;
    virtual void endStreamPlayback() = 0;
};

} // namespace voice
