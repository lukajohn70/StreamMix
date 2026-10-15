#pragma once
/**
 * sm::NDISource & sm::NDIFinder — NDI® 6 & NDI|HX3 IP Video Ingest (vMix 29)
 *
 * Capabilities:
 *   - Automatic mDNS discovery of NDI 6 & NDI|HX3 senders on local subnets
 *   - High-performance zero-copy frame ingest (RGBA8, BGRA8, UYVY, NV12)
 *   - 32-bit float 48 kHz uncompressed multi-channel audio
 *   - Bi-directional Tally metadata feedback (Program / Preview)
 *   - Dynamic SDK runtime loader with built-in simulated/fallback engine
 */

#include "streammix/source.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace sm {

enum class NDIBandwidth {
    Highest,    ///< Full NDI (SpeedHQ)
    Lowest,     ///< Low-bandwidth proxy
    AudioOnly,  ///< Audio only
    NDIHX3      ///< NDI|HX3 High Efficiency (H.264/HEVC ultra-low latency)
};

struct NDIStreamInfo {
    std::string name;       ///< e.g. "STUDIO-PC (Camera 1)"
    std::string url;        ///< e.g. "192.168.1.105:5961"
    bool isHX3 = false;
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
};

// ─── NDI Source Discovery ────────────────────────────────────────
class NDIFinder {
public:
    NDIFinder();
    ~NDIFinder();

    bool startDiscovery();
    void stopDiscovery();
    bool isDiscovering() const { return m_discovering.load(); }

    /// Returns list of currently discovered NDI senders on the LAN
    std::vector<NDIStreamInfo> findSources(int timeoutMs = 200);

    /// Injects an NDI source (for simulated / LAN manual entry)
    void registerSource(const NDIStreamInfo& info);

private:
    std::atomic<bool> m_discovering{false};
    mutable std::mutex m_mutex;
    std::vector<NDIStreamInfo> m_sources;
};

// ─── NDI Source Input ────────────────────────────────────────────
class NDISource : public Source {
public:
    explicit NDISource(const std::string& name = "NDI Input");
    ~NDISource() override;

    bool start(const SourceSettings& settings) override;
    void stop() override;

    // ─── NDI Specific Configuration ──────────────────────────────
    void setStreamName(const std::string& name) { m_streamName = name; }
    std::string streamName() const { return m_streamName; }

    void setBandwidth(NDIBandwidth bw) { m_bandwidth = bw; }
    NDIBandwidth bandwidth() const { return m_bandwidth; }

    void setTally(bool onProgram, bool onPreview);
    bool isProgramTally() const { return m_programTally.load(); }
    bool isPreviewTally() const { return m_previewTally.load(); }

    /// Inject frame directly (for simulated NDI transmitter or mock test)
    void injectVideoFrame(VideoFramePtr frame);
    void injectAudioFrame(AudioFramePtr frame);

private:
    std::string m_streamName;
    NDIBandwidth m_bandwidth = NDIBandwidth::Highest;

    std::atomic<bool> m_programTally{false};
    std::atomic<bool> m_previewTally{false};

    std::atomic<bool> m_running{false};
    std::thread m_worker;
    mutable std::mutex m_sourceMutex;

    void workerLoop();
};

} // namespace sm
