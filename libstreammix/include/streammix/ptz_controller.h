#pragma once
/**
 * sm::PTZController — Pan/Tilt/Zoom Camera Control (vMix 29)
 *
 * Implements:
 *   - VISCA-over-IP (UDP port 52381 & TCP port 5678) — Sony, PTZOptics,
 *     BirdDog, Lumens, Marshall, Aver, and all VISCA-compatible PTZ cameras
 *   - Pelco-D over TCP (port 4196) — legacy SDI matrix and encoder cameras
 *   - Pan / Tilt with 25-speed levels (0 = stop, 24 = maximum velocity)
 *   - Zoom / Focus: tele/wide/near/far with speed control + absolute position
 *   - 255 Preset save & recall per camera
 *   - Home position and power on/off
 *   - Tally light control (red/green/off) for cameras supporting VISCA tally
 *   - Auto-Track Hook: fires PTZ preset recall when program source changes
 *   - Multi-camera registry: up to 64 simultaneous PTZ cameras
 *   - All network I/O is non-blocking and cross-platform (macOS/Linux/Windows)
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <array>

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  Protocol Selection
// ═════════════════════════════════════════════════════════════════

enum class PTZProtocol {
    VISCA_UDP,   ///< VISCA-over-IP UDP (port 52381) — most modern cameras
    VISCA_TCP,   ///< VISCA-over-IP TCP (port 5678)  — BirdDog, some Sony
    Pelco_D_TCP, ///< Pelco-D over TCP (port 4196)
    Simulated    ///< Software simulation (no network) — for testing
};

// ═════════════════════════════════════════════════════════════════
//  PTZ Position & Speed
// ═════════════════════════════════════════════════════════════════

struct PTZPosition {
    int32_t pan    = 0;     ///< Absolute pan position  (-2448 to +2448 for most cameras)
    int32_t tilt   = 0;     ///< Absolute tilt position (-432 to +1296 typical)
    int32_t zoom   = 0;     ///< Absolute zoom position (0x0000 = wide, 0x4000 = tele)
    int32_t focus  = 0;     ///< Absolute focus position (0x1000 near to 0xF000 far)
};

struct PTZSpeed {
    uint8_t panSpeed   = 8;  ///< 0 (stop) to 24 (max)
    uint8_t tiltSpeed  = 8;  ///< 0 (stop) to 24 (max)
    uint8_t zoomSpeed  = 4;  ///< 0 (stop) to 7 (max)
    uint8_t focusSpeed = 4;  ///< 0 (stop) to 7 (max)
};

// ═════════════════════════════════════════════════════════════════
//  PTZ Preset
// ═════════════════════════════════════════════════════════════════

struct PTZPreset {
    uint8_t     number = 0;             ///< 0–254
    std::string name;                    ///< Human-readable label
    PTZPosition position;                ///< Cached position snapshot
    bool        populated = false;
};

// ═════════════════════════════════════════════════════════════════
//  Tally State
// ═════════════════════════════════════════════════════════════════

enum class TallyState {
    Off,
    Program,   ///< Red
    Preview    ///< Green
};

// ═════════════════════════════════════════════════════════════════
//  PTZCamera — Single camera connection and state
// ═════════════════════════════════════════════════════════════════

class PTZCamera {
public:
    PTZCamera(uint64_t id, const std::string& name,
              const std::string& host, uint16_t port,
              PTZProtocol protocol = PTZProtocol::VISCA_UDP);
    ~PTZCamera();

    uint64_t            id()       const { return m_id; }
    const std::string&  name()     const { return m_name; }
    const std::string&  host()     const { return m_host; }
    uint16_t            port()     const { return m_port; }
    PTZProtocol         protocol() const { return m_protocol; }
    bool                connected()const { return m_connected.load(); }

    // ─── Connection ──────────────────────────────────────────────
    bool connect();
    void disconnect();

    // ─── Motion Commands ─────────────────────────────────────────
    void panLeft (uint8_t speed = 8);
    void panRight(uint8_t speed = 8);
    void tiltUp  (uint8_t speed = 8);
    void tiltDown(uint8_t speed = 8);
    void panTilt (int8_t panDir, int8_t tiltDir,
                  uint8_t panSpeed = 8, uint8_t tiltSpeed = 8);
    void stopPanTilt();

    void zoomIn   (uint8_t speed = 4);
    void zoomOut  (uint8_t speed = 4);
    void stopZoom ();

    void focusNear(uint8_t speed = 4);
    void focusFar (uint8_t speed = 4);
    void stopFocus();
    void setAutoFocus(bool enable);

    // ─── Absolute Position ───────────────────────────────────────
    void moveTo(PTZPosition pos, PTZSpeed speed = PTZSpeed{});
    void zoomTo(int32_t absoluteZoom, uint8_t speed = 4);

    // ─── Presets ─────────────────────────────────────────────────
    void savePreset  (uint8_t presetNumber);
    void recallPreset(uint8_t presetNumber, uint8_t speed = 24);
    void clearPreset (uint8_t presetNumber);

    void setPresetName(uint8_t presetNumber, const std::string& name);
    const PTZPreset& preset(uint8_t presetNumber) const;

    // ─── Home & Power ────────────────────────────────────────────
    void home();
    void powerOn();
    void powerOff();

    // ─── Tally Feedback ──────────────────────────────────────────
    void setTally(TallyState state);
    TallyState tally() const { return m_tally; }

    // ─── Current State ───────────────────────────────────────────
    PTZPosition currentPosition() const;
    void        setSimulatedPosition(PTZPosition pos); ///< For Simulated protocol

private:
    bool sendViscaUDP(const std::vector<uint8_t>& cmd);
    bool sendViscaTCP(const std::vector<uint8_t>& cmd);
    bool sendPelcoD  (uint8_t cmd1, uint8_t cmd2,
                      uint8_t data1, uint8_t data2);
    bool sendCommand (const std::vector<uint8_t>& bytes);

    uint64_t    m_id;
    std::string m_name;
    std::string m_host;
    uint16_t    m_port;
    PTZProtocol m_protocol;

    std::atomic<bool> m_connected { false };
    int  m_socket = -1;

    mutable std::mutex m_mutex;
    std::array<PTZPreset, 255> m_presets;
    PTZPosition m_position;
    TallyState  m_tally = TallyState::Off;

    uint8_t  m_viscaSeq = 1; ///< VISCA command sequence counter
};

// ═════════════════════════════════════════════════════════════════
//  PTZController — Multi-camera manager + auto-track hook
// ═════════════════════════════════════════════════════════════════

class PTZController {
public:
    PTZController() = default;
    ~PTZController() = default;

    // ─── Camera Registry ─────────────────────────────────────────
    std::shared_ptr<PTZCamera> addCamera(uint64_t sourceId,
                                          const std::string& name,
                                          const std::string& host,
                                          uint16_t port = 52381,
                                          PTZProtocol protocol = PTZProtocol::VISCA_UDP);

    void removeCamera(uint64_t sourceId);
    std::shared_ptr<PTZCamera> camera(uint64_t sourceId) const;
    std::vector<uint64_t> cameraIds() const;
    size_t cameraCount() const;

    // ─── Auto-Track Hook ─────────────────────────────────────────
    /// When programSourceId changes, recall this preset on that camera
    void setAutoTrackPreset(uint64_t sourceId, uint8_t presetNumber);
    void clearAutoTrack(uint64_t sourceId);

    /// Call this when the Compositor program source changes (connect to Context)
    void onProgramSourceChanged(uint64_t newProgramSourceId,
                                 uint64_t prevProgramSourceId);

    // ─── Broadcast Tally to All Cameras ──────────────────────────
    void updateTally(uint64_t programSourceId, uint64_t previewSourceId);

    // ─── Convenience: Fire Command on Active Camera ───────────────
    void setActiveCamera(uint64_t sourceId);
    uint64_t activeCamera() const { return m_activeCameraId; }

    void panLeft  (uint8_t speed = 8);
    void panRight (uint8_t speed = 8);
    void tiltUp   (uint8_t speed = 8);
    void tiltDown (uint8_t speed = 8);
    void stopPanTilt();
    void zoomIn   (uint8_t speed = 4);
    void zoomOut  (uint8_t speed = 4);
    void home();
    void recallPreset(uint8_t preset, uint8_t speed = 24);
    void savePreset  (uint8_t preset);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<PTZCamera>> m_cameras;
    std::unordered_map<uint64_t, uint8_t> m_autoTrackPresets;
    uint64_t m_activeCameraId = 0;
};

} // namespace sm
