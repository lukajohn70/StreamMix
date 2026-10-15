#pragma once
/**
 * sm::Output — Multi-Destination Broadcast Output & Streaming Engine
 *
 * Implements:
 *   - Up to 5 simultaneous streaming endpoints (vMix 29 standard)
 *   - RTMP / RTMPS streaming
 *   - SRT streaming (Caller / Listener)
 *   - Local MP4/MOV ISO MultiCorder recording
 *   - Real-time encoder telemetry (bitrate, dropped frames, encoding latency)
 */

#include "streammix/frame.h"
#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include <functional>

namespace sm {

enum class OutputType {
    RTMP,
    SRT,
    FileRecord,
    VirtualCam,
    NDI
};

enum class OutputState {
    Stopped,
    Starting,
    Streaming,
    Reconnecting,
    Error
};

struct OutputSettings {
    std::string url;            ///< e.g. "rtmp://a.rtmp.youtube.com/live2" or "output.mp4"
    std::string streamKey;
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
    int videoBitrateKbps = 6000;
    int audioBitrateKbps = 192;
    int audioSampleRate = 48000;
    uint32_t audioBus = 1;      ///< Bus bitmask to encode (default: Master)
};

struct OutputStats {
    double currentBitrateKbps = 0.0;
    uint64_t totalFramesSent = 0;
    uint64_t droppedFrames = 0;
    double fps = 0.0;
    int64_t uptimeSeconds = 0;
};

class Output {
public:
    explicit Output(std::string name, OutputType type)
        : m_name(std::move(name)), m_type(type) {}

    virtual ~Output() = default;

    virtual bool start(const OutputSettings& settings) = 0;
    virtual void stop() = 0;

    virtual void pushVideo(VideoFramePtr frame) = 0;
    virtual void pushAudio(AudioFramePtr frame) = 0;

    const std::string& name() const { return m_name; }
    OutputType  type()  const { return m_type; }
    OutputState state() const { return m_state; }
    bool isStreaming() const { return m_state == OutputState::Streaming; }
    virtual OutputStats stats() const { return m_stats; }

protected:
    std::string m_name;
    OutputType  m_type;
    std::atomic<OutputState> m_state { OutputState::Stopped };
    OutputSettings m_settings;
    OutputStats m_stats;
};

class RTMPOutput final : public Output {
public:
    explicit RTMPOutput(const std::string& name = "Stream 1");
    ~RTMPOutput() override;

    bool start(const OutputSettings& settings) override;
    void stop() override;

    void pushVideo(VideoFramePtr frame) override;
    void pushAudio(AudioFramePtr frame) override;

    OutputStats stats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sm
