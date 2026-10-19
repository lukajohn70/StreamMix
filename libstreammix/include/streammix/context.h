#pragma once
/**
 * sm::Context — Master Production Controller
 *
 * Coordinates:
 *   - Video Compositor (Preview/Program switching, T-Bar, 8 Overlays)
 *   - Pro Audio Matrix Mixer (Master, Headphones, Buses A-G, AFV, Mix-Minus)
 *   - Multi-source registry (FFmpeg, Zoom, OMT, NDI, Screen)
 *   - Live Outputs (RTMP/SRT streaming, ISO MultiCorder, Virtual Cam)
 */

#include "streammix/source.h"
#include "streammix/compositor.h"
#include "streammix/audio_mixer.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>

namespace sm {

class RecordOutput;
class VirtualCamOutput;
class MultiCorder;
class MultiStreamManager;

struct ContextConfig {
    int videoWidth = 1920;
    int videoHeight = 1080;
    double videoFps = 60.0;
    int audioSampleRate = 48000;
    int audioChannels = 2;
};

class Context {
public:
    explicit Context(ContextConfig config = ContextConfig{});
    ~Context();

    // ─── Lifecycle ───────────────────────────────────────────────
    bool initialize();
    void shutdown();
    void tick(double deltaSeconds);

    // ─── Source Registry ─────────────────────────────────────────
    bool addSource(std::shared_ptr<Source> source);
    bool removeSource(uint64_t sourceId);
    std::shared_ptr<Source> getSource(uint64_t sourceId) const;
    std::vector<std::shared_ptr<Source>> getAllSources() const;

    // ─── Core Subsystems ─────────────────────────────────────────
    Compositor& compositor() { return *m_compositor; }
    const Compositor& compositor() const { return *m_compositor; }

    AudioMixer& audioMixer() { return *m_audioMixer; }
    const AudioMixer& audioMixer() const { return *m_audioMixer; }

    MultiStreamManager& streamManager() { return *m_streamManager; }
    MultiCorder& multiCorder() { return *m_multiCorder; }
    RecordOutput* recordOutput() const { return m_recordOutput.get(); }
    VirtualCamOutput* virtualCam() const { return m_virtualCamOutput.get(); }
    const ContextConfig& config() const { return m_config; }
    void setMasterResolution(int width, int height);

    // ─── Unified Switching API ───────────────────────────────────
    void cut();
    void quickPlay(uint64_t sourceId);
    void startTransition(TransitionType type, int durationMs = 500);
    void setTBarPosition(float position);
    void setPreviewSource(uint64_t sourceId);
    void setProgramSource(uint64_t sourceId);

    // ─── vMix 29 Overlay Triggers ────────────────────────────────
    void toggleOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Fade, int durationMs = 300);
    void showOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Cut, int durationMs = 0);
    void hideOverlay(int channelIndex, OverlayTransition trans = OverlayTransition::Cut, int durationMs = 0);
    void setOverlaySource(int channelIndex, uint64_t sourceId);

    // ─── Fade To Black (FTB) ─────────────────────────────────────
    void toggleFTB(int durationMs = 500);
    bool isFTBActive() const;

    // ─── Live Broadcasting, Recording & External Outputs ─────────
    bool startRecording(const std::string& path = "", int bitrateKbps = 15000);
    void stopRecording();
    bool isRecording() const;

    bool startExternal();
    void stopExternal();
    bool isExternalActive() const;

    bool startStreaming();
    void stopStreaming();
    bool isStreaming() const;

    bool startMultiCorder(const std::string& dir = "");
    void stopMultiCorder();
    bool isMultiCorderActive() const;

    // ─── vMix 29 HTTP / REST API Execution ───────────────────────
    std::string executeVmixFunction(const std::string& function,
                                    const std::unordered_map<std::string, std::string>& params);

    // ─── vMix XML Status Engine (Companion / Stream Deck Polling) ──
    std::string generateVmixXml() const;

    // ─── Zoom / Remote Caller Mix-Minus ──────────────────────────
    AudioFramePtr getMixMinusForCaller(uint64_t callerSourceId, int frameCount = 1024, int64_t pts_us = 0);

private:
    ContextConfig m_config;
    mutable std::mutex m_mutex;

    std::unique_ptr<Compositor> m_compositor;
    std::unique_ptr<AudioMixer> m_audioMixer;
    std::unique_ptr<RecordOutput> m_recordOutput;
    std::unique_ptr<VirtualCamOutput> m_virtualCamOutput;
    std::unique_ptr<MultiCorder> m_multiCorder;
    std::unique_ptr<MultiStreamManager> m_streamManager;

    std::unordered_map<uint64_t, std::shared_ptr<Source>> m_sources;
    std::atomic<bool> m_streaming { false };
    std::atomic<bool> m_recording { false };
    std::atomic<bool> m_externalActive { false };
};

} // namespace sm
