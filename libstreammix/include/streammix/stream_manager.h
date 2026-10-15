#pragma once
/**
 * sm::MultiStreamManager — Multi-Destination Broadcast Simulcast Engine (vMix 29)
 *
 * Implements:
 *   - Up to 5 concurrent streaming destinations (vMix 29 standard)
 *   - Destination presets for YouTube Live, Twitch, Facebook Live, and Custom RTMP/SRT
 *   - Per-stream independent video/audio encoding parameters and bitrates
 *   - Per-stream audio bus routing (e.g., Master on Stream 1, Aux Audio on Stream 2)
 *   - Real-time aggregate telemetry (combined Mbps, dropped frames, live health)
 */

#include "streammix/output.h"
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <array>

namespace sm {

struct StreamDestination {
    int id = 1;                         ///< Stream 1 to 5
    std::string name = "Stream 1";
    std::string platform = "Custom";    ///< "YouTube", "Twitch", "Facebook", "Custom RTMP", "SRT"
    OutputSettings settings;
    bool enabled = true;
    std::shared_ptr<Output> outputInstance;
};

class MultiStreamManager {
public:
    static constexpr int MAX_STREAMS = 5;

    MultiStreamManager();
    ~MultiStreamManager();

    // ─── Destination Configuration ───────────────────────────────
    void configureStream(int streamIndex, const std::string& platform,
                         const std::string& url, const std::string& streamKey,
                         int bitrateKbps = 6000, uint32_t audioBus = 1);

    StreamDestination getStreamConfig(int streamIndex) const;
    void setStreamEnabled(int streamIndex, bool enabled);
    bool isStreamEnabled(int streamIndex) const;

    // ─── Start / Stop Controls ───────────────────────────────────
    bool startStream(int streamIndex);
    void stopStream(int streamIndex);

    bool startAll();
    void stopAll();

    bool isStreaming(int streamIndex) const;
    bool isAnyStreaming() const;
    int activeStreamCount() const;

    // ─── Frame Distribution (call from Render / Audio engine) ────
    void pushVideo(VideoFramePtr frame);
    void pushAudio(AudioFramePtr frame, uint32_t audioBus = 1);

    // ─── Aggregated Telemetry ────────────────────────────────────
    double totalBitrateMbps() const;
    uint64_t totalFramesSent() const;
    uint64_t totalDroppedFrames() const;

private:
    mutable std::mutex m_mutex;
    std::array<StreamDestination, MAX_STREAMS> m_streams;
};

} // namespace sm
