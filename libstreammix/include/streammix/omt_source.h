#pragma once
/**
 * sm::OMTSource & sm::OMTOutput — Open Media Transport Engine (vMix 29)
 *
 * Implements:
 *   - Native Open Media Transport (OMT) open-source LAN protocol
 *   - Ultra-low latency (<1 frame) 4K/120fps video transport over Gigabit/10G LAN
 *   - 32-bit Float uncompressed multi-channel audio
 *   - Receiver (`sm::OMTSource`) and Transmitter (`sm::OMTOutput`)
 *   - Automatic LAN stream discovery via UDP Multicast Announce
 */

#include "streammix/source.h"
#include "streammix/output.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace sm {

constexpr uint32_t OMT_MAGIC = 0x4F4D5431; // "OMT1"
constexpr int OMT_DEFAULT_PORT = 9000;
constexpr int OMT_DISCOVERY_PORT = 9001;

#pragma pack(push, 1)
struct OMTHeader {
    uint32_t magic = OMT_MAGIC;
    uint16_t version = 1;
    uint16_t packetType = 0;   // 1 = Video, 2 = Audio, 3 = Announce
    uint64_t seqNumber = 0;
    int64_t  ptsUs = 0;
    uint32_t payloadSize = 0;
};

struct OMTVideoMeta {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint16_t fpsNum = 60;
    uint16_t fpsDen = 1;
    uint8_t  format = 0;       // 0 = RGBA8, 1 = YUV420P, 2 = NV12
    uint8_t  hasAlpha = 1;
    uint16_t reserved = 0;
};

struct OMTAudioMeta {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
    uint16_t frameCount = 1024;
    uint8_t  format = 0;       // 0 = Float32, 1 = Int24, 2 = Int16
    uint8_t  reserved[3] = {0, 0, 0};
};
#pragma pack(pop)

struct OMTStreamInfo {
    std::string name;
    std::string ip;
    int port = OMT_DEFAULT_PORT;
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
};

// ═════════════════════════════════════════════════════════════════
//  OMTSource (Receiver Input)
// ═════════════════════════════════════════════════════════════════

class OMTSource final : public Source {
public:
    explicit OMTSource(const std::string& name = "OMT Input");
    ~OMTSource() override;

    bool start(const SourceSettings& settings) override;
    void stop() override;

    bool connectTo(const std::string& host, int port = OMT_DEFAULT_PORT, const std::string& streamName = "");
    OMTStreamInfo streamInfo() const;

    // Simulation / testing injection
    void injectPacket(const OMTHeader& hdr, const uint8_t* payload, size_t size);

private:
    void receiveLoop();

    std::string m_host;
    int m_port = OMT_DEFAULT_PORT;
    std::string m_streamName;

    std::atomic<bool> m_running { false };
    std::thread m_rxThread;
    int m_socket = -1;

    mutable std::mutex m_infoMutex;
    OMTStreamInfo m_info;
};

// ═════════════════════════════════════════════════════════════════
//  OMTOutput (Transmitter / Network Sender)
// ═════════════════════════════════════════════════════════════════

class OMTOutput final : public Output {
public:
    explicit OMTOutput(const std::string& name = "OMT Output 1");
    ~OMTOutput() override;

    bool start(const OutputSettings& settings) override;
    void stop() override;

    void pushVideo(VideoFramePtr frame) override;
    void pushAudio(AudioFramePtr frame) override;

private:
    void announceLoop();

    std::atomic<bool> m_running { false };
    std::thread m_announceThread;
    int m_socket = -1;
    uint64_t m_packetSeq = 0;
};

// ═════════════════════════════════════════════════════════════════
//  OMTDiscovery (Automatic LAN Stream Discovery)
// ═════════════════════════════════════════════════════════════════

class OMTDiscovery {
public:
    static OMTDiscovery& instance();

    bool start();
    void stop();
    std::vector<OMTStreamInfo> discoveredStreams() const;

private:
    OMTDiscovery() = default;
    ~OMTDiscovery();

    void listenLoop();

    std::atomic<bool> m_running { false };
    std::thread m_listenThread;
    int m_socket = -1;

    mutable std::mutex m_mutex;
    std::vector<OMTStreamInfo> m_streams;
};

} // namespace sm
