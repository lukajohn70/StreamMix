#pragma once

#include <memory>
#include <vector>
#include <mutex>
#include "streammix/frame.h"

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace sm {

class AudioPlayer {
public:
    explicit AudioPlayer(int sampleRate = 48000, int channels = 2);
    ~AudioPlayer();

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    void enqueueFrame(AudioFramePtr frame);
    void enqueueSamples(const float* data, size_t sampleCount);

    void setMuted(bool muted) { m_muted = muted; }
    bool isMuted() const { return m_muted; }

    void setVolume(float volume) { m_volume = std::max(0.0f, std::min(volume, 2.0f)); }
    float volume() const { return m_volume; }

private:
#if defined(__APPLE__)
    static void audioQueueCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer);
    AudioQueueRef m_queue = nullptr;
    static constexpr int kNumBuffers = 4;
    static constexpr int kBufferByteSize = 4096;
    AudioQueueBufferRef m_buffers[kNumBuffers];
    std::mutex m_mutex;
    std::vector<float> m_sampleRing;
#endif

    int m_sampleRate = 48000;
    int m_channels = 2;
    bool m_running = false;
    bool m_muted = false;
    float m_volume = 1.0f;
};

} // namespace sm
