#include "streammix/ndi_source.h"
#include <chrono>

namespace sm {

// ─────────────────────────────────────────────────────────────────
//  NDIFinder Implementation
// ─────────────────────────────────────────────────────────────────

NDIFinder::NDIFinder() = default;

NDIFinder::~NDIFinder() {
    stopDiscovery();
}

bool NDIFinder::startDiscovery() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_discovering.store(true);

    // Populate common local broadcast NDI endpoints
    if (m_sources.empty()) {
        NDIStreamInfo s1;
        s1.name = "STUDIO-PC (Camera 1 - Host)";
        s1.url = "192.168.1.105:5961";
        s1.isHX3 = false;
        m_sources.push_back(s1);

        NDIStreamInfo s2;
        s2.name = "REMOTE-CAM-01 (NDI|HX3 PTZ)";
        s2.url = "192.168.1.120:5962";
        s2.isHX3 = true;
        m_sources.push_back(s2);
    }
    return true;
}

void NDIFinder::stopDiscovery() {
    m_discovering.store(false);
}

std::vector<NDIStreamInfo> NDIFinder::findSources(int timeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sources;
}

void NDIFinder::registerSource(const NDIStreamInfo& info) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& s : m_sources) {
        if (s.name == info.name) {
            s = info;
            return;
        }
    }
    m_sources.push_back(info);
}

// ─────────────────────────────────────────────────────────────────
//  NDISource Implementation
// ─────────────────────────────────────────────────────────────────

NDISource::NDISource(const std::string& name)
    : Source(name, SourceType::NDI)
    , m_streamName(name)
{
    m_id = s_nextId.fetch_add(1);
    m_width = 1920;
    m_height = 1080;
    m_fps = 60.0;
}

NDISource::~NDISource() {
    stop();
}

bool NDISource::start(const SourceSettings& settings) {
    std::lock_guard<std::mutex> lock(m_sourceMutex);
    if (m_running.load()) return true;

    m_running.store(true);
    m_state = SourceState::Running;

    m_worker = std::thread(&NDISource::workerLoop, this);
    return true;
}

void NDISource::stop() {
    m_running.store(false);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_state = SourceState::Stopped;
}

void NDISource::setTally(bool onProgram, bool onPreview) {
    m_programTally.store(onProgram);
    m_previewTally.store(onPreview);
}

void NDISource::injectVideoFrame(VideoFramePtr frame) {
    if (!frame) return;
    pushVideo(frame);
}

void NDISource::injectAudioFrame(AudioFramePtr frame) {
    if (!frame) return;
    pushAudio(frame);
}

void NDISource::workerLoop() {
    int64_t frameIdx = 0;
    auto start = std::chrono::steady_clock::now();

    while (m_running.load()) {
        // Sleep for 60fps tick (16.6ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (!m_running.load()) break;

        auto now = std::chrono::steady_clock::now();
        int64_t pts = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();

        // Push test NDI pattern frame
        auto vf = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_width, m_height));
        vf->pts_us = pts;
        vf->seq = ++frameIdx;

        // Visual tally indicator in pixel border
        uint8_t borderR = 0, borderG = 0, borderB = 0;
        if (m_programTally.load()) {
            borderR = 255; // Red border for Program
        } else if (m_previewTally.load()) {
            borderG = 255; // Green border for Preview
        }

        if (borderR || borderG) {
            for (int y = 0; y < 10; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    size_t idx = (y * m_width + x) * 4;
                    vf->data[idx + 0] = borderR;
                    vf->data[idx + 1] = borderG;
                    vf->data[idx + 2] = borderB;
                    vf->data[idx + 3] = 255;
                }
            }
        }

        pushVideo(vf);

        // Audio 48 kHz stereo block (800 samples = ~16.6ms)
        auto af = std::make_shared<AudioFrame>();
        af->channels = 2;
        af->sample_rate = 48000;
        af->frames = 800;
        af->samples.resize(800 * 2, 0.0f);
        af->pts_us = pts;

        pushAudio(af);
    }
}

} // namespace sm
