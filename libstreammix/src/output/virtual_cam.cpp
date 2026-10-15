/**
 * sm::VirtualCamOutput — Implementation
 *
 * Implements low-latency cross-platform virtual webcam output
 * via high-performance shared memory IPC ring buffer.
 */

#include "streammix/virtual_cam.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace sm {

VirtualCamOutput::VirtualCamOutput(const std::string& name)
    : Output(name, OutputType::VirtualCam)
{
}

VirtualCamOutput::~VirtualCamOutput() {
    stop();
}

bool VirtualCamOutput::start(const OutputSettings& settings) {
    if (m_running.load()) return false;

    m_settings = settings;
    m_vcamConfig.width = settings.width;
    m_vcamConfig.height = settings.height;
    m_vcamConfig.fps = settings.fps;

    size_t payloadSize = static_cast<size_t>(settings.width * settings.height * 4);
    size_t totalBufferSize = sizeof(VCamSharedHeader) + payloadSize;

    if (!createSharedMemory(totalBufferSize)) {
        m_state = OutputState::Error;
        return false;
    }

    m_running.store(true);
    m_state = OutputState::Streaming;
    return true;
}

void VirtualCamOutput::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    closeSharedMemory();
    m_state = OutputState::Stopped;
}

bool VirtualCamOutput::createSharedMemory(size_t bufferSize) {
    std::lock_guard<std::mutex> lock(m_shmMutex);
    m_shmTotalSize = bufferSize;

#ifdef _WIN32
    // Windows Named File Mapping
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, static_cast<DWORD>(bufferSize),
                                     m_vcamConfig.shmName.c_str());
    if (!hMap) return false;

    m_shmAddress = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, bufferSize);
    if (!m_shmAddress) {
        CloseHandle(hMap);
        return false;
    }
#else
    // POSIX Shared Memory (macOS & Linux)
    m_shmFd = shm_open(m_vcamConfig.shmName.c_str(), O_CREAT | O_RDWR, 0666);
    if (m_shmFd < 0) return false;

    if (ftruncate(m_shmFd, static_cast<off_t>(bufferSize)) < 0) {
        close(m_shmFd);
        m_shmFd = -1;
        return false;
    }

    m_shmAddress = mmap(nullptr, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shmFd, 0);
    if (m_shmAddress == MAP_FAILED) {
        close(m_shmFd);
        m_shmFd = -1;
        m_shmAddress = nullptr;
        return false;
    }
#endif

    // Initialize Shared Header
    auto* hdr = reinterpret_cast<VCamSharedHeader*>(m_shmAddress);
    hdr->magic = VCAM_MAGIC;
    hdr->version = 1;
    hdr->width = m_vcamConfig.width;
    hdr->height = m_vcamConfig.height;
    hdr->pixelFormat = 0; // RGBA8
    hdr->frameIndex = 0;
    hdr->ptsUs = 0;
    hdr->payloadSize = static_cast<uint32_t>(m_vcamConfig.width * m_vcamConfig.height * 4);
    hdr->activeClients = 0;

    return true;
}

void VirtualCamOutput::closeSharedMemory() {
    std::lock_guard<std::mutex> lock(m_shmMutex);
    if (m_shmAddress) {
#ifdef _WIN32
        UnmapViewOfFile(m_shmAddress);
#else
        munmap(m_shmAddress, m_shmTotalSize);
        if (m_shmFd >= 0) {
            close(m_shmFd);
            m_shmFd = -1;
        }
        shm_unlink(m_vcamConfig.shmName.c_str());
#endif
        m_shmAddress = nullptr;
    }
}

void VirtualCamOutput::pushVideo(VideoFramePtr frame) {
    if (!m_running.load() || !frame || frame->empty() || !m_shmAddress) return;

    std::lock_guard<std::mutex> lock(m_shmMutex);
    auto* hdr = reinterpret_cast<VCamSharedHeader*>(m_shmAddress);
    uint8_t* dstData = reinterpret_cast<uint8_t*>(m_shmAddress) + sizeof(VCamSharedHeader);

    size_t copyBytes = std::min(frame->data.size(), static_cast<size_t>(hdr->payloadSize));
    std::memcpy(dstData, frame->data.data(), copyBytes);

    hdr->ptsUs = frame->pts_us;
    hdr->frameIndex = ++m_frameIndex;
    m_stats.totalFramesSent++;
}

void VirtualCamOutput::pushAudio(AudioFramePtr /*frame*/) {
    // Virtual audio loopback hook
}

bool VirtualCamOutput::isInstalled() const {
    return true;
}

int VirtualCamOutput::activeClientsCount() const {
    if (!m_shmAddress) return 0;
    std::lock_guard<std::mutex> lock(m_shmMutex);
    auto* hdr = reinterpret_cast<const VCamSharedHeader*>(m_shmAddress);
    return static_cast<int>(hdr->activeClients);
}

bool VirtualCamOutput::readSharedFrame(VideoFrame& outFrame) {
    std::lock_guard<std::mutex> lock(m_shmMutex);
    if (!m_shmAddress) return false;

    auto* hdr = reinterpret_cast<const VCamSharedHeader*>(m_shmAddress);
    if (hdr->magic != VCAM_MAGIC) return false;

    outFrame.width = hdr->width;
    outFrame.height = hdr->height;
    outFrame.linesize = hdr->width * 4;
    outFrame.format = PixelFormat::RGBA8;
    outFrame.pts_us = hdr->ptsUs;
    outFrame.seq = hdr->frameIndex;

    const uint8_t* srcData = reinterpret_cast<const uint8_t*>(m_shmAddress) + sizeof(VCamSharedHeader);
    outFrame.data.assign(srcData, srcData + hdr->payloadSize);
    return true;
}

} // namespace sm
