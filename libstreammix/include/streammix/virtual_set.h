#pragma once
/**
 * sm::VirtualSet — Multi-Layer Virtual Studio Set & Camera Zoom Engine (vMix 29)
 *
 * Implements:
 *   - Multi-layer composition:
 *       • Layer 0: Background 3D/Photo studio backdrop or digital video wall
 *       • Layer 1: Presenter / Talent layer with real-time Chroma Keying
 *       • Layer 2: Studio desk / podium foreground occlusion layer
 *       • Layer 3: Mirror floor reflection with alpha falloff
 *   - Virtual camera presets (Wide, Medium, Close-Up, Custom)
 *   - Smooth dynamic camera zoom and pan transitions with easing
 */

#include "streammix/frame.h"
#include "streammix/chroma_key.h"
#include <vector>
#include <string>
#include <memory>
#include <mutex>

namespace sm {

struct VirtualCameraAngle {
    std::string name = "Wide";
    float pan = 0.0f;    ///< -1.0 (left) to +1.0 (right)
    float tilt = 0.0f;   ///< -1.0 (down) to +1.0 (up)
    float zoom = 1.0f;   ///< 1.0x to 4.0x
};

struct TalentLayerSettings {
    float posX = 0.0f;      ///< Center offset (-1.0 to 1.0)
    float posY = 0.0f;      ///< Vertical offset
    float scale = 1.0f;     ///< Talent scaling (0.5 to 2.0)
    bool floorReflection = true;
    float reflectionOpacity = 0.20f;
};

class VirtualSet {
public:
    explicit VirtualSet(int width = 1920, int height = 1080);
    ~VirtualSet() = default;

    // ─── Layer Assignment ─────────────────────────────────────────
    void setBackground(VideoFramePtr bgFrame);
    void setTalent(VideoFramePtr talentFrame);
    void setForeground(VideoFramePtr fgFrame);

    // ─── Talent Chroma Keying ─────────────────────────────────────
    ChromaKeyFilter& chromaKey() { return m_chromaKey; }
    const ChromaKeyFilter& chromaKey() const { return m_chromaKey; }
    void setTalentSettings(const TalentLayerSettings& settings);

    // ─── Virtual Camera Presets ───────────────────────────────────
    void addCameraAngle(const VirtualCameraAngle& angle);
    void selectCameraAngle(size_t index, int transitionMs = 1000);
    size_t cameraAngleCount() const;
    const VirtualCameraAngle& currentCameraAngle() const { return m_currentAngle; }

    // ─── Animation Tick ───────────────────────────────────────────
    void tick(double deltaSeconds);

    // ─── Render Virtual Set ───────────────────────────────────────
    VideoFramePtr render(int64_t pts_us = 0);

private:
    int m_width;
    int m_height;
    mutable std::mutex m_mutex;

    VideoFramePtr m_bgLayer;
    VideoFramePtr m_talentLayer;
    VideoFramePtr m_fgLayer;

    ChromaKeyFilter m_chromaKey;
    TalentLayerSettings m_talentSettings;

    std::vector<VirtualCameraAngle> m_angles;
    VirtualCameraAngle m_currentAngle;
    VirtualCameraAngle m_targetAngle;

    bool m_zooming = false;
    double m_zoomElapsed = 0.0;
    double m_zoomDuration = 1.0;
};

} // namespace sm
