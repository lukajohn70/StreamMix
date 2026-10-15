#pragma once
/**
 * sm::Compositor — vMix 29-Class GPU/CPU Video Compositing Engine
 *
 * Capabilities:
 *   - Preview / Program / T-Bar broadcast switching architecture
 *   - Transitions: Cut, Fade (Dissolve), Zoom, Wipe, Slide, Fly, Cube, Stinger 1-8
 *   - 8 Independent Real-time Overlay Channels (vMix 29 specification)
 *   - Hardware Fade-to-Black (FTB)
 *   - Multi-source frame ingestion with thread-safe double buffering
 *   - PixelFormat::RGBA8 master pipeline with sub-pixel alpha blending
 *   - MultiView composite output generator
 */

#include "streammix/frame.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>

namespace sm {

constexpr int MAX_OVERLAYS = 8;
constexpr int MAX_STINGERS = 8;

enum class TransitionType {
    Cut,
    Fade,
    Zoom,
    WipeLeft,
    WipeRight,
    WipeUp,
    WipeDown,
    SlideLeft,
    SlideRight,
    SlideUp,
    SlideDown,
    Fly,
    Cube,
    Stinger1,
    Stinger2,
    Stinger3,
    Stinger4,
    Stinger5,
    Stinger6,
    Stinger7,
    Stinger8
};

enum class TransitionState {
    Idle,
    Active
};

enum class OverlayState {
    Off,
    In,      // Animating into view
    Active,  // Fully visible on Program
    Out      // Animating out of view
};

enum class OverlayTransition {
    Cut,
    Fade,
    Zoom,
    SlideBottom,
    SlideTop,
    SlideLeft,
    SlideRight
};

struct OverlayChannel {
    int id = 1;                     ///< 1 through 8
    uint64_t sourceId = 0;          ///< Bound input ID (0 = none)
    OverlayState state = OverlayState::Off;
    OverlayTransition transition = OverlayTransition::Fade;
    int durationMs = 300;
    float progress = 0.0f;          ///< 0.0f -> 1.0f animation progress
    float x = 0.0f;                 ///< Normalized left (0.0 .. 1.0)
    float y = 0.0f;                 ///< Normalized top  (0.0 .. 1.0)
    float width = 1.0f;             ///< Normalized width (0.0 .. 1.0)
    float height = 1.0f;            ///< Normalized height (0.0 .. 1.0)
    float opacity = 1.0f;           ///< Layer master opacity (0.0 .. 1.0)
};

struct CompositorConfig {
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
    PixelFormat format = PixelFormat::RGBA8;
};

using OutputFrameCallback = std::function<void(VideoFramePtr)>;

class Compositor {
public:
    explicit Compositor(CompositorConfig config = CompositorConfig{});
    ~Compositor() = default;

    // ─── Configuration ───────────────────────────────────────────
    void setResolution(int width, int height);
    int  width()  const { return m_config.width; }
    int  height() const { return m_config.height; }
    double fps()  const { return m_config.fps; }

    // ─── Input Frame Ingestion (Thread-Safe) ──────────────────────
    void updateInputFrame(uint64_t sourceId, VideoFramePtr frame);
    VideoFramePtr getInputFrame(uint64_t sourceId) const;
    void removeInput(uint64_t sourceId);

    // ─── Preview & Program Bus Assignment ────────────────────────
    void setPreviewSource(uint64_t sourceId);
    void setProgramSource(uint64_t sourceId);
    uint64_t previewSourceId() const;
    uint64_t programSourceId() const;

    // ─── Switching & Transitions ─────────────────────────────────
    void cut();
    void quickPlay(uint64_t sourceId);
    void startTransition(TransitionType type, int durationMs = 500);
    void setTBarPosition(float position); // 0.0f = Program, 1.0f = Preview
    float tbarPosition() const { return m_transitionProgress.load(); }
    bool isTransitioning() const { return m_transitionState == TransitionState::Active; }

    // ─── Fade to Black (FTB) ─────────────────────────────────────
    void toggleFTB(int durationMs = 500);
    bool isFTBActive() const { return m_ftbActive.load(); }
    float ftbProgress() const { return m_ftbProgress.load(); }

    // ─── vMix 29 8-Overlay Stack ─────────────────────────────────
    void setOverlaySource(int channelIndex, uint64_t sourceId);
    void toggleOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Fade, int durationMs = 300);
    void showOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Fade, int durationMs = 300);
    void hideOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Fade, int durationMs = 300);
    void setOverlayGeometry(int channelIndex, float x, float y, float w, float h, float opacity = 1.0f);
    bool isOverlayActive(int channelIndex) const;
    const OverlayChannel& overlay(int channelIndex) const;

    // ─── Engine Clock / Animation Tick ───────────────────────────
    void tick(double deltaSeconds);

    // ─── Rendering Outputs ───────────────────────────────────────
    VideoFramePtr renderProgram(int64_t pts_us = 0);
    VideoFramePtr renderPreview(int64_t pts_us = 0);
    VideoFramePtr renderMultiView(int64_t pts_us = 0);

    // ─── Callbacks for Output Pipeline ───────────────────────────
    void setProgramCallback(OutputFrameCallback cb) { m_programCb = std::move(cb); }
    void setPreviewCallback(OutputFrameCallback cb) { m_previewCb = std::move(cb); }

private:
    void blendFrames(const VideoFrame& srcA, const VideoFrame& srcB, VideoFrame& dst, float t, TransitionType type);
    void compositeOverlay(VideoFrame& canvas, const VideoFrame& overlayFrame, const OverlayChannel& ov);
    void applyFTB(VideoFrame& frame, float progress);
    VideoFramePtr getSourceOrBlack(uint64_t sourceId) const;

    CompositorConfig m_config;
    mutable std::mutex m_mutex;

    // Frame storage per source
    std::unordered_map<uint64_t, VideoFramePtr> m_inputFrames;

    // Switching state
    uint64_t m_previewSourceId = 0;
    uint64_t m_programSourceId = 0;
    uint64_t m_transitionOutgoingSourceId = 0;

    // Active Transition
    TransitionState m_transitionState = TransitionState::Idle;
    TransitionType  m_activeTransition = TransitionType::Cut;
    int    m_transitionDurationMs = 500;
    double m_transitionElapsedSeconds = 0.0;
    std::atomic<float> m_transitionProgress { 0.0f };

    // FTB
    std::atomic<bool>  m_ftbActive { false };
    bool   m_ftbTargetState = false;
    int    m_ftbDurationMs = 500;
    double m_ftbElapsedSeconds = 0.0;
    std::atomic<float> m_ftbProgress { 0.0f }; // 0.0 = full color, 1.0 = pitch black

    // vMix 29 8 Overlays
    std::vector<OverlayChannel> m_overlays; // indices 0..7 correspond to channels 1..8

    // Output callbacks
    OutputFrameCallback m_programCb;
    OutputFrameCallback m_previewCb;

    // Sequence counters
    uint64_t m_programSeq = 0;
    uint64_t m_previewSeq = 0;
};

} // namespace sm
