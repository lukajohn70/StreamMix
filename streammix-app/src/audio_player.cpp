#include "audio_player.h"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace sm {

AudioPlayer::AudioPlayer(int sampleRate, int channels)
    : m_sampleRate(sampleRate), m_channels(channels)
{
}

AudioPlayer::~AudioPlayer() {
    stop();
}

bool AudioPlayer::start() {
    if (m_running) return true;

#if defined(__APPLE__)
    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = static_cast<Float64>(m_sampleRate);
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = static_cast<UInt32>(m_channels);
    asbd.mBitsPerChannel = 32;
    asbd.mBytesPerFrame = asbd.mChannelsPerFrame * sizeof(float);
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;

    OSStatus status = AudioQueueNewOutput(&asbd, audioQueueCallback, this, nullptr, nullptr, 0, &m_queue);
    if (status != noErr) {
        std::cerr << "[AudioPlayer] Failed to create AudioQueue: " << status << std::endl;
        return false;
    }

    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(m_queue, kBufferByteSize, &m_buffers[i]);
        if (status != noErr) {
            std::cerr << "[AudioPlayer] Failed to allocate AudioQueue buffer: " << status << std::endl;
            AudioQueueDispose(m_queue, true);
            m_queue = nullptr;
            return false;
        }
        m_buffers[i]->mAudioDataByteSize = kBufferByteSize;
        std::memset(m_buffers[i]->mAudioData, 0, kBufferByteSize);
        AudioQueueEnqueueBuffer(m_queue, m_buffers[i], 0, nullptr);
    }

    status = AudioQueueStart(m_queue, nullptr);
    if (status != noErr) {
        std::cerr << "[AudioPlayer] Failed to start AudioQueue: " << status << std::endl;
        AudioQueueDispose(m_queue, true);
        m_queue = nullptr;
        return false;
    }
#endif

    m_running = true;
    return true;
}

void AudioPlayer::stop() {
    if (!m_running) return;

#if defined(__APPLE__)
    if (m_queue) {
        AudioQueueStop(m_queue, true);
        AudioQueueDispose(m_queue, true);
        m_queue = nullptr;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sampleRing.clear();
#endif

    m_running = false;
}

void AudioPlayer::enqueueFrame(AudioFramePtr frame) {
    if (!frame || frame->samples.empty() || !m_running) return;
    enqueueSamples(frame->samples.data(), frame->samples.size());
}

void AudioPlayer::enqueueSamples(const float* data, size_t sampleCount) {
    if (!data || sampleCount == 0 || !m_running) return;

#if defined(__APPLE__)
    std::lock_guard<std::mutex> lock(m_mutex);

    // Limit backlog buffer to ~0.25 seconds to keep latency ultra low and prevent drift
    size_t maxBacklog = static_cast<size_t>(m_sampleRate * m_channels * 0.25f);
    if (m_sampleRing.size() > maxBacklog) {
        m_sampleRing.erase(m_sampleRing.begin(), m_sampleRing.begin() + static_cast<long>(m_sampleRing.size() - maxBacklog));
    }

    if (m_muted || m_volume <= 0.0001f) {
        // If muted, append silence so timing isn't interrupted
        m_sampleRing.insert(m_sampleRing.end(), sampleCount, 0.0f);
    } else if (std::abs(m_volume - 1.0f) < 0.001f) {
        m_sampleRing.insert(m_sampleRing.end(), data, data + sampleCount);
    } else {
        size_t startIdx = m_sampleRing.size();
        m_sampleRing.resize(startIdx + sampleCount);
        for (size_t i = 0; i < sampleCount; ++i) {
            m_sampleRing[startIdx + i] = data[i] * m_volume;
        }
    }
#endif
}

#if defined(__APPLE__)
void AudioPlayer::audioQueueCallback(void* userData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer) {
    auto* self = static_cast<AudioPlayer*>(userData);
    if (!self) return;

    std::lock_guard<std::mutex> lock(self->m_mutex);

    size_t samplesNeeded = inBuffer->mAudioDataBytesCapacity / sizeof(float);
    float* dst = static_cast<float*>(inBuffer->mAudioData);

    size_t available = std::min(samplesNeeded, self->m_sampleRing.size());
    if (available > 0) {
        std::memcpy(dst, self->m_sampleRing.data(), available * sizeof(float));
        self->m_sampleRing.erase(self->m_sampleRing.begin(), self->m_sampleRing.begin() + static_cast<long>(available));
    }

    // Fill any remainder with silence
    if (available < samplesNeeded) {
        std::memset(dst + available, 0, (samplesNeeded - available) * sizeof(float));
    }

    inBuffer->mAudioDataByteSize = inBuffer->mAudioDataBytesCapacity;
    AudioQueueEnqueueBuffer(inAQ, inBuffer, 0, nullptr);
}
#endif

} // namespace sm
