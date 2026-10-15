#pragma once
/**
 * sm::MultiCorder — ISO Multi-Camera Broadcast Recording Engine (vMix 29)
 *
 * Implements:
 *   - Concurrent ISO recording of up to 8 camera feeds into isolated video files
 *   - Independent of master Program recording
 *   - Frame-accurate synchronized timecode across all ISO files (SMPTE/PTS alignment)
 *   - Circular thread-safe disk write queue with overflow protection
 *   - Recording telemetry: elapsed time, total frames written, disk write rate (MB/s), dropped frames
 */

#include "streammix/frame.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <deque>
#include <thread>
#include <unordered_map>

namespace sm {

enum class MultiCorderFormat {
    MP4,
    MKV,
    AVI,
    ProRes,
    RAW_RGBA
};

struct MultiCorderChannelConfig {
    uint64_t sourceId = 0;
    std::string sourceName;
    std::string outputPath;         ///< Destination directory or file path
    MultiCorderFormat format = MultiCorderFormat::MP4;
    bool enabled = true;
    int bitrateKbps = 15000;        ///< High-bitrate ISO recording (default 15 Mbps)
    uint32_t audioBus = 1;          ///< Audio bus to capture with this camera
};

struct MultiCorderChannelStats {
    uint64_t sourceId = 0;
    std::string sourceName;
    bool isRecording = false;
    uint64_t framesWritten = 0;
    uint64_t framesDropped = 0;
    uint64_t bytesWritten = 0;
    double writeRateMBps = 0.0;
    int64_t elapsedSeconds = 0;
};

class MultiCorder {
public:
    MultiCorder();
    ~MultiCorder();

    // ─── Channel Configuration ───────────────────────────────────
    void addChannel(const MultiCorderChannelConfig& config);
    void removeChannel(uint64_t sourceId);
    void setChannelEnabled(uint64_t sourceId, bool enabled);
    bool isChannelEnabled(uint64_t sourceId) const;
    std::vector<uint64_t> channelIds() const;

    // ─── Recording Controls ──────────────────────────────────────
    bool startRecording(const std::string& outputDirectory = "./recordings");
    void stopRecording();
    bool isRecording() const { return m_recording.load(); }

    // ─── Frame Ingestion (call from Source thread / Compositor) ───
    void ingestVideo(uint64_t sourceId, VideoFramePtr frame);
    void ingestAudio(uint64_t sourceId, AudioFramePtr frame);

    // ─── Telemetry ───────────────────────────────────────────────
    MultiCorderChannelStats channelStats(uint64_t sourceId) const;
    std::vector<MultiCorderChannelStats> allChannelStats() const;
    uint64_t totalBytesWritten() const;

private:
    struct ChannelRecorder {
        MultiCorderChannelConfig config;
        std::atomic<bool> active{false};
        std::mutex queueMutex;
        std::mutex statsMutex;
        std::deque<VideoFramePtr> videoQueue;
        std::deque<AudioFramePtr> audioQueue;
        std::thread writerThread;

        MultiCorderChannelStats stats;
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastRateCheck;
        uint64_t bytesSinceCheck = 0;
    };

    mutable std::mutex m_mutex;
    std::atomic<bool> m_recording{false};
    std::string m_outputDir;
    std::unordered_map<uint64_t, std::shared_ptr<ChannelRecorder>> m_channels;

    void writerLoop(std::shared_ptr<ChannelRecorder> ch);
};

} // namespace sm
