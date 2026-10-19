#pragma once
/**
 * sm::InstantReplay — vMix 29 Instant Replay 2.0 Engine
 *
 * Implements:
 *   - Continuous circular ring buffer recording up to 8 cameras simultaneously
 *   - Configurable buffer depth (30 seconds → 8 hours of footage)
 *   - Variable speed slow-motion playback (10% → 200%) with nearest-frame interpolation
 *   - vMix 29 Quad-Cam Replay View layout (4 simultaneous angle playheads)
 *   - Mark In / Mark Out event tagging and named clip extraction
 *   - Replay Bus — isolated output stream (sourceId 9998) separate from Program
 *   - Thread-safe multi-camera ingestion with zero-copy frame reference counting
 *   - Live metadata: camera name, timestamp, frame count, buffer utilization
 */

#include "streammix/source.h"
#include "streammix/frame.h"
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  ReplayFrame — A single captured frame with full metadata
// ═════════════════════════════════════════════════════════════════

struct ReplayFrame {
    VideoFramePtr video;            ///< RGBA8 1920x1080 or native resolution
    int64_t  pts_us    = 0;         ///< Capture timestamp (microseconds)
    double   clockSec  = 0.0;       ///< Wall-clock capture time (seconds from epoch)
    uint64_t cameraId  = 0;         ///< Source camera ID
    uint32_t frameIndex = 0;        ///< Sequential capture frame counter
    bool     isMarked  = false;     ///< User-tagged event frame
    std::string markerLabel;        ///< Optional marker description
};

using ReplayFramePtr = std::shared_ptr<ReplayFrame>;

// ═════════════════════════════════════════════════════════════════
//  ReplayClip — Named clip extracted from the ring buffer
// ═════════════════════════════════════════════════════════════════

struct ReplayClip {
    std::string name;
    uint64_t    cameraId    = 0;
    int64_t     markInPts   = 0;    ///< Start PTS (microseconds)
    int64_t     markOutPts  = 0;    ///< End PTS (microseconds)
    std::vector<ReplayFramePtr> frames;

    double durationSeconds() const {
        return (frames.empty()) ? 0.0
               : static_cast<double>(markOutPts - markInPts) / 1'000'000.0;
    }
    size_t frameCount() const { return frames.size(); }
};

using ReplayClipPtr = std::shared_ptr<ReplayClip>;

// ═════════════════════════════════════════════════════════════════
//  ReplayCameraBuffer — Per-camera ring buffer
// ═════════════════════════════════════════════════════════════════

class ReplayCameraBuffer {
public:
    explicit ReplayCameraBuffer(uint64_t cameraId, const std::string& name,
                                 size_t maxFrames = 3600);

    uint64_t    id()   const { return m_cameraId; }
    const std::string& name() const { return m_name; }

    void pushFrame(ReplayFramePtr frame);
    size_t frameCount() const;
    float  bufferUtilization() const; ///< 0.0 → 1.0

    /// Retrieve all frames between two PTS timestamps
    std::vector<ReplayFramePtr> getFrameRange(int64_t fromPts, int64_t toPts) const;

    /// Most-recent N seconds of footage
    std::vector<ReplayFramePtr> getLastSeconds(double seconds, double fps = 60.0) const;

    /// Raw buffer access for advanced clip editing
    const std::deque<ReplayFramePtr>& buffer() const { return m_buffer; }

    void markCurrentFrame(const std::string& label = "");

    int64_t newestPts() const;
    int64_t oldestPts() const;

private:
    uint64_t     m_cameraId;
    std::string  m_name;
    size_t       m_maxFrames;

    mutable std::mutex          m_mutex;
    std::deque<ReplayFramePtr>  m_buffer;
    std::atomic<uint32_t>       m_frameCounter { 0 };
};

// ═════════════════════════════════════════════════════════════════
//  ReplayPlayhead — Single camera's playback cursor
// ═════════════════════════════════════════════════════════════════

struct ReplayPlayhead {
    uint64_t cameraId      = 0;
    size_t   frameIndex    = 0;     ///< Current frame offset within clip
    float    speed         = 1.0f;  ///< 0.10 = 10% slow-mo, 2.0 = 2× fast forward
    bool     playing       = false;
    bool     loop          = false;
    bool     reversed      = false;

    double   accumulator   = 0.0;   ///< Sub-frame accumulator for speed control

    std::vector<ReplayFramePtr> frames; ///< Clip frames being played back
};

// ═════════════════════════════════════════════════════════════════
//  InstantReplay — Main Engine
// ═════════════════════════════════════════════════════════════════

class InstantReplay {
public:
    /// @param bufferDepthSeconds  Circular buffer depth per camera (default 60s @ 60fps = 3,600 frames)
    /// @param fps                 Source frame rate for buffer sizing
    explicit InstantReplay(double bufferDepthSeconds = 60.0, double fps = 60.0);
    ~InstantReplay() = default;

    // ─── Camera Registration ─────────────────────────────────────
    void addCamera(uint64_t cameraId, const std::string& name);
    void removeCamera(uint64_t cameraId);
    std::vector<uint64_t> cameraIds() const;

    // ─── Live Frame Ingestion (call from Source callbacks) ───────
    void ingestFrame(uint64_t cameraId, VideoFramePtr frame, int64_t pts_us);

    // ─── Event Markers ───────────────────────────────────────────
    /// Tags the most recently captured frame on all cameras simultaneously
    void markEvent(const std::string& label = "Goal");

    /// Mark the In point for clip extraction on a specific camera
    void setMarkIn(uint64_t cameraId, int64_t pts_us = -1);
    /// Mark the Out point for clip extraction
    void setMarkOut(uint64_t cameraId, int64_t pts_us = -1);

    // ─── Clip Extraction ─────────────────────────────────────────
    /// Extract a named clip from markIn to markOut for given camera
    ReplayClipPtr extractClip(uint64_t cameraId, const std::string& name = "Replay");

    /// Extract last N seconds from a camera
    ReplayClipPtr extractLastSeconds(uint64_t cameraId, double seconds, const std::string& name = "Replay");

    std::vector<ReplayClipPtr> clips() const;
    void removeClip(const std::string& name);

    // ─── Playback Control ────────────────────────────────────────
    /// Load a clip into a playhead slot (0-3 for Quad View)
    void loadClip(int playheadIndex, ReplayClipPtr clip);
    void play(int playheadIndex, float speed = 1.0f);
    void pause(int playheadIndex);
    void stop(int playheadIndex);
    void setSpeed(int playheadIndex, float speed);
    void setLooping(int playheadIndex, bool loop);
    void stepForward(int playheadIndex, int frames = 1);
    void stepBack(int playheadIndex, int frames = 1);
    void seekToFrame(int playheadIndex, size_t frameIndex);

    float speed(int playheadIndex) const;
    bool  isPlaying(int playheadIndex) const;
    size_t currentFrame(int playheadIndex) const;

    // ─── Engine Tick ─────────────────────────────────────────────
    void tick(double deltaSeconds);

    // ─── Replay Bus Output ───────────────────────────────────────
    /// Returns the current composite Replay Bus frame (Quad-Cam or Single)
    /// Push this frame to the Compositor as source 9998 (Replay Bus)
    VideoFramePtr renderReplayBus();

    /// Set active playhead for single-camera replay output
    void setActivePlayhead(int index);
    int  activePlayhead() const { return m_activePlayhead; }

    // ─── Quad-Camera Replay View ─────────────────────────────────
    bool quadViewEnabled() const { return m_quadView; }
    void setQuadView(bool enabled) { m_quadView = enabled; }

    // ─── Buffer Statistics ───────────────────────────────────────
    struct BufferStats {
        size_t totalFrames    = 0;
        size_t maxFrames      = 0;
        float  utilization    = 0.0f; ///< 0.0 → 1.0
        double oldestSec      = 0.0;
        double newestSec      = 0.0;
        double bufferDepthSec = 0.0;
    };
    BufferStats stats(uint64_t cameraId) const;

    // ─── Replay Event Callback ───────────────────────────────────
    using ReplayEventCallback = std::function<void(const std::string& event,
                                                    int playheadIndex)>;
    void setEventCallback(ReplayEventCallback cb) { m_eventCb = std::move(cb); }

    static constexpr int MAX_PLAYHEADS = 4;
    static constexpr uint64_t REPLAY_BUS_SOURCE_ID = 9998;

private:
    VideoFramePtr renderSinglePlayhead(int index);
    VideoFramePtr renderQuadView();
    void blitSubFrame(VideoFrame& dst, const VideoFrame& src,
                      int dstX, int dstY, int dstW, int dstH);

    double m_fps;
    size_t m_maxFramesPerCamera;
    bool   m_quadView      = false;
    int    m_activePlayhead = 0;

    mutable std::mutex m_mutex;

    std::unordered_map<uint64_t, std::shared_ptr<ReplayCameraBuffer>> m_cameras;
    std::unordered_map<uint64_t, int64_t> m_markIn;
    std::unordered_map<uint64_t, int64_t> m_markOut;

    std::array<ReplayPlayhead, MAX_PLAYHEADS> m_playheads;
    std::vector<ReplayClipPtr> m_clips;

    ReplayEventCallback m_eventCb;
};

} // namespace sm
