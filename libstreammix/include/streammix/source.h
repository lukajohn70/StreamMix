#pragma once
/**
 * sm::Source — Abstract base class for all video/audio input sources.
 *
 * Implementations:
 *   FFmpegSource   — webcams, files, RTSP, RTMP, SRT (Phase 1)
 *   NDISource      — NDI network video (Phase 2)
 *   ScreenSource   — desktop capture (Phase 2)
 *   WebRTCSource   — remote guests (Phase 3)
 */

#include "frame.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <cstdint>

namespace sm {

enum class SourceState {
    Idle,
    Starting,
    Running,
    Error,
    Stopped
};

enum class SourceType {
    Camera,
    File,
    Screen,
    NDI,
    OMT,
    RTSP,
    RTMP,
    SRT,
    WebRTC,
    Zoom,
    Color,
    Image,
    Title,
    Browser,
    Audio,
    Unknown
};

struct SourceSettings {
    std::string url;        ///< Device path, file path, or URL
    int  width      = 0;   ///< 0 = use source native
    int  height     = 0;
    int  fps_num    = 0;   ///< 0 = use source native
    int  fps_den    = 1;
    bool loop       = false;
    bool hw_decode  = true;
    std::string hw_device;  ///< e.g. "cuda", "videotoolbox", "vaapi"
};

/**
 * Callback signatures delivered from source threads to the pipeline.
 */
using VideoCallback = std::function<void(VideoFramePtr)>;
using AudioCallback = std::function<void(AudioFramePtr)>;

/**
 * Abstract source base — all sources must implement this interface.
 */
class Source {
public:
    explicit Source(std::string name, SourceType type)
        : m_name(std::move(name)), m_type(type) {}

    virtual ~Source() = default;

    // ── Lifecycle ────────────────────────────────────────────
    virtual bool  start(const SourceSettings& settings) = 0;
    virtual void  stop()  = 0;
    virtual bool  isRunning() const { return m_state == SourceState::Running; }
    virtual void  tick(double /*deltaSeconds*/) {}

    // ── Callbacks ────────────────────────────────────────────
    void setVideoCallback(VideoCallback cb) { m_videoCb = std::move(cb); }
    void setAudioCallback(AudioCallback cb) { m_audioCb = std::move(cb); }

    // ── Playback & Transport Controls (vMix Video Inputs) ───
    virtual void   pause() {}
    virtual void   resume() {}
    virtual bool   isPaused() const { return false; }
    virtual void   seek(double /*seconds*/) {}
    virtual double durationSeconds() const { return 0.0; }
    virtual double positionSeconds() const { return 0.0; }
    virtual void   setLoop(bool /*loop*/) {}
    virtual bool   isLooping() const { return false; }

    // ── Info ─────────────────────────────────────────────────
    const std::string& name()  const { return m_name; }
    SourceType         type()  const { return m_type; }
    SourceState        state() const { return m_state; }
    int                width() const { return m_width; }
    int                height()const { return m_height; }
    double             fps()   const { return m_fps; }
    uint64_t           id()    const { return m_id; }

protected:
    void pushVideo(VideoFramePtr frame) { if (m_videoCb) m_videoCb(std::move(frame)); }
    void pushAudio(AudioFramePtr frame) { if (m_audioCb) m_audioCb(std::move(frame)); }

    std::string  m_name;
    SourceType   m_type;
    std::atomic<SourceState> m_state { SourceState::Idle };
    int          m_width  = 0;
    int          m_height = 0;
    double       m_fps    = 0.0;
    uint64_t     m_id     = 0;

    static std::atomic<uint64_t> s_nextId;

private:
    VideoCallback m_videoCb;
    AudioCallback m_audioCb;
};

using SourcePtr = std::shared_ptr<Source>;

} // namespace sm
