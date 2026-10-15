#pragma once
/**
 * sm::VirtualCamOutput — Cross-Platform Virtual Webcam Driver
 *
 * Capabilities:
 *   - Pipes StreamMix live Program feed as a native virtual camera device
 *   - Zero-copy shared memory IPC ring buffer (POSIX shm_open / Windows CreateFileMapping)
 *   - Compatible with Zoom, Microsoft Teams, Google Meet, Discord, and OBS Studio
 *   - Format conversions: RGBA8 -> NV12 / YUV420P / UYVY for webcam consumer compatibility
 *   - macOS CoreMediaIO / Windows DirectShow / Linux v4l2loopback integration
 */

#include "streammix/output.h"
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace sm {

constexpr uint32_t VCAM_MAGIC = 0x5643414D; // "VCAM"
constexpr const char* VCAM_DEFAULT_SHM_NAME = "/streammix_vcam_buffer";

#pragma pack(push, 1)
struct VCamSharedHeader {
    uint32_t magic = VCAM_MAGIC;
    uint32_t version = 1;
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t pixelFormat = 0; // 0 = RGBA8, 1 = NV12, 2 = YUV420P
    uint64_t frameIndex = 0;
    int64_t  ptsUs = 0;
    uint32_t payloadSize = 0;
    uint32_t activeClients = 0;
};
#pragma pack(pop)

struct VirtualCamConfig {
    std::string deviceName = "StreamMix Virtual Camera";
    std::string shmName = VCAM_DEFAULT_SHM_NAME;
    int width = 1920;
    int height = 1080;
    double fps = 60.0;
    PixelFormat targetFormat = PixelFormat::RGBA8;
};

class VirtualCamOutput final : public Output {
public:
    explicit VirtualCamOutput(const std::string& name = "Virtual Camera");
    ~VirtualCamOutput() override;

    bool start(const OutputSettings& settings) override;
    void stop() override;

    void pushVideo(VideoFramePtr frame) override;
    void pushAudio(AudioFramePtr frame) override; // Virtual Audio loopback

    bool isInstalled() const;
    int activeClientsCount() const;

    // Simulation / testing reader helper: Reads current frame from shared memory
    bool readSharedFrame(VideoFrame& outFrame);

private:
    bool createSharedMemory(size_t bufferSize);
    void closeSharedMemory();

    VirtualCamConfig m_vcamConfig;
    std::atomic<bool> m_running { false };
    std::atomic<uint64_t> m_frameIndex { 0 };

    int m_shmFd = -1;
    void* m_shmAddress = nullptr;
    size_t m_shmTotalSize = 0;

    mutable std::mutex m_shmMutex;
};

} // namespace sm
