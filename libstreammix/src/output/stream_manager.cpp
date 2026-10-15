/**
 * sm::MultiStreamManager — Implementation
 */

#include "streammix/stream_manager.h"
#include <iostream>

namespace sm {

MultiStreamManager::MultiStreamManager() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < MAX_STREAMS; ++i) {
        m_streams[i].id = i + 1;
        m_streams[i].name = "Stream " + std::to_string(i + 1);
        m_streams[i].enabled = (i == 0); // default Stream 1 enabled
        m_streams[i].settings.width = 1920;
        m_streams[i].settings.height = 1080;
        m_streams[i].settings.fps = 60.0;
        m_streams[i].settings.videoBitrateKbps = 6000;
        m_streams[i].settings.audioBitrateKbps = 192;
        m_streams[i].settings.audioBus = 1; // Master Bus
        m_streams[i].outputInstance = std::make_shared<RTMPOutput>(m_streams[i].name);
    }

    // Default platform presets
    m_streams[0].platform = "YouTube";
    m_streams[0].settings.url = "rtmp://a.rtmp.youtube.com/live2";
    m_streams[0].settings.videoBitrateKbps = 6500;

    m_streams[1].platform = "Twitch";
    m_streams[1].settings.url = "rtmp://live.twitch.tv/app";
    m_streams[1].settings.videoBitrateKbps = 6000;

    m_streams[2].platform = "Facebook";
    m_streams[2].settings.url = "rtmps://live-api-s.facebook.com:443/rtmp/";
    m_streams[2].settings.videoBitrateKbps = 4000;

    m_streams[3].platform = "Custom RTMP";
    m_streams[4].platform = "SRT";
}

MultiStreamManager::~MultiStreamManager() {
    stopAll();
}

void MultiStreamManager::configureStream(int streamIndex, const std::string& platform,
                                         const std::string& url, const std::string& streamKey,
                                         int bitrateKbps, uint32_t audioBus)
{
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& s = m_streams[streamIndex - 1];
    s.platform = platform;
    s.settings.url = url;
    s.settings.streamKey = streamKey;
    s.settings.videoBitrateKbps = bitrateKbps;
    s.settings.audioBus = audioBus;
}

StreamDestination MultiStreamManager::getStreamConfig(int streamIndex) const {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return StreamDestination{};
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_streams[streamIndex - 1];
}

void MultiStreamManager::setStreamEnabled(int streamIndex, bool enabled) {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_streams[streamIndex - 1].enabled = enabled;
}

bool MultiStreamManager::isStreamEnabled(int streamIndex) const {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_streams[streamIndex - 1].enabled;
}

bool MultiStreamManager::startStream(int streamIndex) {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& s = m_streams[streamIndex - 1];
    if (s.outputInstance) {
        return s.outputInstance->start(s.settings);
    }
    return false;
}

void MultiStreamManager::stopStream(int streamIndex) {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& s = m_streams[streamIndex - 1];
    if (s.outputInstance) {
        s.outputInstance->stop();
    }
}

bool MultiStreamManager::startAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool anyStarted = false;
    for (auto& s : m_streams) {
        if (s.enabled && s.outputInstance) {
            if (s.outputInstance->start(s.settings)) {
                anyStarted = true;
            }
        }
    }
    return anyStarted;
}

void MultiStreamManager::stopAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) {
            s.outputInstance->stop();
        }
    }
}

bool MultiStreamManager::isStreaming(int streamIndex) const {
    if (streamIndex < 1 || streamIndex > MAX_STREAMS) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& s = m_streams[streamIndex - 1];
    return s.outputInstance ? s.outputInstance->isStreaming() : false;
}

bool MultiStreamManager::isAnyStreaming() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) return true;
    }
    return false;
}

int MultiStreamManager::activeStreamCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = 0;
    for (const auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) count++;
    }
    return count;
}

void MultiStreamManager::pushVideo(VideoFramePtr frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) {
            s.outputInstance->pushVideo(frame);
        }
    }
}

void MultiStreamManager::pushAudio(AudioFramePtr frame, uint32_t audioBus) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) {
            // Push audio if routed to stream's designated audio bus
            if ((s.settings.audioBus & audioBus) != 0 || s.settings.audioBus == 0) {
                s.outputInstance->pushAudio(frame);
            }
        }
    }
}

double MultiStreamManager::totalBitrateMbps() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    double total = 0.0;
    for (const auto& s : m_streams) {
        if (s.outputInstance && s.outputInstance->isStreaming()) {
            total += s.outputInstance->stats().currentBitrateKbps / 1000.0;
        }
    }
    return total;
}

uint64_t MultiStreamManager::totalFramesSent() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& s : m_streams) {
        if (s.outputInstance) total += s.outputInstance->stats().totalFramesSent;
    }
    return total;
}

uint64_t MultiStreamManager::totalDroppedFrames() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint64_t total = 0;
    for (const auto& s : m_streams) {
        if (s.outputInstance) total += s.outputInstance->stats().droppedFrames;
    }
    return total;
}

} // namespace sm
