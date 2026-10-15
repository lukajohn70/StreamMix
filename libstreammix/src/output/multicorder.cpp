#include "streammix/multicorder.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace sm {

MultiCorder::MultiCorder() = default;

MultiCorder::~MultiCorder() {
    stopRecording();
}

void MultiCorder::addChannel(const MultiCorderChannelConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto ch = std::make_shared<ChannelRecorder>();
    ch->config = config;
    ch->stats.sourceId = config.sourceId;
    ch->stats.sourceName = config.sourceName;
    ch->stats.isRecording = false;
    m_channels[config.sourceId] = ch;
}

void MultiCorder::removeChannel(uint64_t sourceId) {
    std::shared_ptr<ChannelRecorder> chToStop;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_channels.find(sourceId);
        if (it != m_channels.end()) {
            chToStop = it->second;
            m_channels.erase(it);
        }
    }
    if (chToStop) {
        chToStop->active.store(false);
        if (chToStop->writerThread.joinable()) {
            chToStop->writerThread.join();
        }
    }
}

void MultiCorder::setChannelEnabled(uint64_t sourceId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channels.find(sourceId);
    if (it != m_channels.end()) {
        it->second->config.enabled = enabled;
    }
}

bool MultiCorder::isChannelEnabled(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channels.find(sourceId);
    if (it != m_channels.end()) {
        return it->second->config.enabled;
    }
    return false;
}

std::vector<uint64_t> MultiCorder::channelIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> ids;
    ids.reserve(m_channels.size());
    for (const auto& [id, _] : m_channels) {
        ids.push_back(id);
    }
    return ids;
}

bool MultiCorder::startRecording(const std::string& outputDirectory) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_recording.load()) {
        return false;
    }

    m_outputDir = outputDirectory;
    std::error_code ec;
    std::filesystem::create_directories(m_outputDir, ec);

    m_recording.store(true);

    auto now = std::chrono::steady_clock::now();
    for (auto& [id, ch] : m_channels) {
        if (!ch->config.enabled) continue;

        ch->active.store(true);
        ch->stats.isRecording = true;
        ch->stats.framesWritten = 0;
        ch->stats.framesDropped = 0;
        ch->stats.bytesWritten = 0;
        ch->stats.writeRateMBps = 0.0;
        ch->stats.elapsedSeconds = 0;
        ch->startTime = now;
        ch->lastRateCheck = now;
        ch->bytesSinceCheck = 0;

        ch->writerThread = std::thread(&MultiCorder::writerLoop, this, ch);
    }

    return true;
}

void MultiCorder::stopRecording() {
    if (!m_recording.load()) return;
    m_recording.store(false);

    std::vector<std::shared_ptr<ChannelRecorder>> chList;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [id, ch] : m_channels) {
            ch->active.store(false);
            chList.push_back(ch);
        }
    }

    for (auto& ch : chList) {
        if (ch->writerThread.joinable()) {
            ch->writerThread.join();
        }
        ch->stats.isRecording = false;
    }
}

void MultiCorder::ingestVideo(uint64_t sourceId, VideoFramePtr frame) {
    if (!m_recording.load() || !frame) return;

    std::shared_ptr<ChannelRecorder> ch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_channels.find(sourceId);
        if (it != m_channels.end()) {
            ch = it->second;
        }
    }

    if (ch && ch->active.load() && ch->config.enabled) {
        std::lock_guard<std::mutex> qLock(ch->queueMutex);
        // Max queue size: 120 frames (approx 2-4 seconds buffer)
        if (ch->videoQueue.size() >= 120) {
            ch->stats.framesDropped++;
        } else {
            ch->videoQueue.push_back(frame);
        }
    }
}

void MultiCorder::ingestAudio(uint64_t sourceId, AudioFramePtr frame) {
    if (!m_recording.load() || !frame) return;

    std::shared_ptr<ChannelRecorder> ch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_channels.find(sourceId);
        if (it != m_channels.end()) {
            ch = it->second;
        }
    }

    if (ch && ch->active.load() && ch->config.enabled) {
        std::lock_guard<std::mutex> qLock(ch->queueMutex);
        if (ch->audioQueue.size() >= 250) {
            ch->audioQueue.pop_front();
        }
        ch->audioQueue.push_back(frame);
    }
}

MultiCorderChannelStats MultiCorder::channelStats(uint64_t sourceId) const {
    std::shared_ptr<ChannelRecorder> ch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_channels.find(sourceId);
        if (it != m_channels.end()) {
            ch = it->second;
        }
    }
    if (ch) {
        std::lock_guard<std::mutex> sLock(ch->statsMutex);
        auto stats = ch->stats;
        if (ch->active.load()) {
            auto now = std::chrono::steady_clock::now();
            stats.elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - ch->startTime).count();
        }
        return stats;
    }
    return {};
}

std::vector<MultiCorderChannelStats> MultiCorder::allChannelStats() const {
    std::vector<std::shared_ptr<ChannelRecorder>> chList;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, ch] : m_channels) {
            chList.push_back(ch);
        }
    }
    std::vector<MultiCorderChannelStats> results;
    results.reserve(chList.size());
    auto now = std::chrono::steady_clock::now();
    for (const auto& ch : chList) {
        std::lock_guard<std::mutex> sLock(ch->statsMutex);
        auto stats = ch->stats;
        if (ch->active.load()) {
            stats.elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - ch->startTime).count();
        }
        results.push_back(stats);
    }
    return results;
}

uint64_t MultiCorder::totalBytesWritten() const {
    std::vector<std::shared_ptr<ChannelRecorder>> chList;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [id, ch] : m_channels) {
            chList.push_back(ch);
        }
    }
    uint64_t total = 0;
    for (const auto& ch : chList) {
        std::lock_guard<std::mutex> sLock(ch->statsMutex);
        total += ch->stats.bytesWritten;
    }
    return total;
}

void MultiCorder::writerLoop(std::shared_ptr<ChannelRecorder> ch) {
    while (true) {
        VideoFramePtr vFrame;
        AudioFramePtr aFrame;
        bool active = ch->active.load();

        {
            std::lock_guard<std::mutex> qLock(ch->queueMutex);
            if (!ch->videoQueue.empty()) {
                vFrame = ch->videoQueue.front();
                ch->videoQueue.pop_front();
            }
            if (!ch->audioQueue.empty()) {
                aFrame = ch->audioQueue.front();
                ch->audioQueue.pop_front();
            }
        }

        if (!active && !vFrame && !aFrame) {
            break;
        }

        if (vFrame) {
            // Calculate approximate compressed/raw bytes
            uint64_t frameBytes = 0;
            if (ch->config.format == MultiCorderFormat::RAW_RGBA) {
                frameBytes = static_cast<uint64_t>(vFrame->width) * vFrame->height * 4;
            } else {
                // Approximate 60fps slice for configured bitrate
                frameBytes = static_cast<uint64_t>((ch->config.bitrateKbps * 1000) / (8 * 60));
                if (frameBytes == 0) frameBytes = 32768; // fallback min
            }

            {
                std::lock_guard<std::mutex> sLock(ch->statsMutex);
                ch->stats.framesWritten++;
                ch->stats.bytesWritten += frameBytes;
                ch->bytesSinceCheck += frameBytes;

                auto now = std::chrono::steady_clock::now();
                auto rateElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - ch->lastRateCheck).count();
                if (rateElapsedMs >= 500) {
                    double sec = rateElapsedMs / 1000.0;
                    ch->stats.writeRateMBps = (ch->bytesSinceCheck / (1024.0 * 1024.0)) / sec;
                    ch->bytesSinceCheck = 0;
                    ch->lastRateCheck = now;
                }

                ch->stats.elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - ch->startTime).count();
            }
        }

        if (aFrame) {
            uint64_t aBytes = aFrame->samples.size() * sizeof(float);
            std::lock_guard<std::mutex> sLock(ch->statsMutex);
            ch->stats.bytesWritten += aBytes;
        }

        if (!vFrame && !aFrame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

} // namespace sm
