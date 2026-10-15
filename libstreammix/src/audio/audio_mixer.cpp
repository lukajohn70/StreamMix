/**
 * sm::AudioMixer — Implementation
 *
 * Implements 32-bit floating point matrix audio mixing,
 * Master, Headphones, Sub-Buses A-G, AFV smooth fading,
 * and automated Mix-Minus for Zoom/remote callers.
 */

#include "streammix/audio_mixer.h"
#include "streammix/audio_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm {

AudioMixer::AudioMixer(int sampleRate, int channels)
    : m_sampleRate(sampleRate), m_channels(channels)
{
    // Default bus master faders to unity gain (1.0f)
    m_busMasterGains[Bus_Master]     = 1.0f;
    m_busMasterGains[Bus_Headphones] = 1.0f;
    m_busMasterGains[Bus_A]          = 1.0f;
    m_busMasterGains[Bus_B]          = 1.0f;
    m_busMasterGains[Bus_C]          = 1.0f;
    m_busMasterGains[Bus_D]          = 1.0f;
    m_busMasterGains[Bus_E]          = 1.0f;
    m_busMasterGains[Bus_F]          = 1.0f;
    m_busMasterGains[Bus_G]          = 1.0f;
}

// ─── Channel Management ──────────────────────────────────────────
void AudioMixer::addChannel(uint64_t sourceId, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_channelsMap.find(sourceId) == m_channelsMap.end()) {
        InternalChannel ch;
        ch.config.sourceId = sourceId;
        ch.config.name = name.empty() ? ("Input " + std::to_string(sourceId)) : name;
        ch.config.gain = 1.0f;
        ch.config.pan = 0.0f;
        ch.config.mute = false;
        ch.config.solo = false;
        ch.config.afv = false;
        ch.config.busRouting = Bus_Master;
        ch.afvMultiplier = 1.0f;
        m_channelsMap[sourceId] = std::move(ch);
    }
}

void AudioMixer::removeChannel(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channelsMap.erase(sourceId);

    // Recheck solo status
    m_anySoloActive = false;
    for (const auto& pair : m_channelsMap) {
        if (pair.second.config.solo) {
            m_anySoloActive = true;
            break;
        }
    }
}

bool AudioMixer::hasChannel(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_channelsMap.find(sourceId) != m_channelsMap.end();
}

std::shared_ptr<AudioDSPRack> AudioMixer::getChannelDSP(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it == m_channelsMap.end()) {
        InternalChannel ch;
        ch.config.sourceId = sourceId;
        ch.config.name = "Input " + std::to_string(sourceId);
        ch.dsp = std::make_shared<AudioDSPRack>(static_cast<float>(m_sampleRate));
        auto inserted = m_channelsMap.emplace(sourceId, std::move(ch));
        return inserted.first->second.dsp;
    }
    if (!it->second.dsp) {
        it->second.dsp = std::make_shared<AudioDSPRack>(static_cast<float>(m_sampleRate));
    }
    return it->second.dsp;
}

// ─── Input Audio Ingestion ───────────────────────────────────────
void AudioMixer::pushAudio(uint64_t sourceId, AudioFramePtr frame) {
    if (!frame || frame->samples.empty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it == m_channelsMap.end()) {
        // Auto-register channel if not already present
        InternalChannel ch;
        ch.config.sourceId = sourceId;
        ch.config.name = "Input " + std::to_string(sourceId);
        it = m_channelsMap.emplace(sourceId, std::move(ch)).first;
    }

    // Process channel through Pro Audio DSP rack if enabled
    if (it->second.dsp) {
        it->second.dsp->process(frame);
    }

    auto& queue = it->second.sampleQueue;
    // Cap buffer at ~1.5 seconds to prevent unbounded queue growth
    size_t maxSamples = static_cast<size_t>(m_sampleRate * m_channels * 1.5);
    if (queue.size() > maxSamples) {
        queue.erase(queue.begin(), queue.begin() + static_cast<long>(queue.size() - maxSamples));
    }

    queue.insert(queue.end(), frame->samples.begin(), frame->samples.end());
}

// ─── Channel Strip Controls ──────────────────────────────────────
void AudioMixer::setGain(uint64_t sourceId, float gain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.gain = std::clamp(gain, 0.0f, 2.0f);
    }
}

void AudioMixer::setPan(uint64_t sourceId, float pan) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

void AudioMixer::setMute(uint64_t sourceId, bool mute) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.mute = mute;
    }
}

void AudioMixer::setSolo(uint64_t sourceId, bool solo) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.solo = solo;
    }

    m_anySoloActive = false;
    for (const auto& pair : m_channelsMap) {
        if (pair.second.config.solo) {
            m_anySoloActive = true;
            break;
        }
    }
}

void AudioMixer::setAFV(uint64_t sourceId, bool afv) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.afv = afv;
    }
}

void AudioMixer::setDelay(uint64_t sourceId, int delayMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.delayMs = std::clamp(delayMs, 0, 1000);
    }
}

void AudioMixer::setBusRouting(uint64_t sourceId, uint32_t busMask) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        it->second.config.busRouting = busMask;
    }
}

float AudioMixer::getGain(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.gain : 1.0f;
}

float AudioMixer::getPan(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.pan : 0.0f;
}

bool AudioMixer::isMuted(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.mute : false;
}

bool AudioMixer::isSolo(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.solo : false;
}

bool AudioMixer::isAFV(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.afv : false;
}

uint32_t AudioMixer::getBusRouting(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    return (it != m_channelsMap.end()) ? it->second.config.busRouting : Bus_Master;
}

// ─── Bus Master Faders ───────────────────────────────────────────
void AudioMixer::setBusMasterGain(AudioBus bus, float gain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_busMasterGains[bus] = std::clamp(gain, 0.0f, 2.0f);
}

float AudioMixer::getBusMasterGain(AudioBus bus) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_busMasterGains.find(bus);
    return (it != m_busMasterGains.end()) ? it->second : 1.0f;
}

// ─── Audio Follow Video (AFV) Update ─────────────────────────────
void AudioMixer::updateAFVState(uint64_t activeProgramSourceId, double deltaSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentProgramSourceId = activeProgramSourceId;

    // Crossfade at 10.0 units per second (~100ms fade rate)
    float step = static_cast<float>(deltaSeconds * 10.0);

    for (auto& pair : m_channelsMap) {
        auto& ch = pair.second;
        if (ch.config.afv) {
            if (ch.config.sourceId == activeProgramSourceId) {
                ch.afvMultiplier = std::min(1.0f, ch.afvMultiplier + step);
            } else {
                ch.afvMultiplier = std::max(0.0f, ch.afvMultiplier - step);
            }
        } else {
            ch.afvMultiplier = 1.0f;
        }
    }
}

// ─── Real-Time Metering ──────────────────────────────────────────
float AudioMixer::linearToDb(float linear) {
    if (linear <= 0.00001f) return -100.0f;
    return std::clamp(20.0f * std::log10(linear), -100.0f, 0.0f);
}

AudioMeterData AudioMixer::getChannelMeters(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_channelsMap.find(sourceId);
    if (it != m_channelsMap.end()) {
        return it->second.meters;
    }
    return AudioMeterData{};
}

AudioMeterData AudioMixer::getMasterMeters() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_masterMeters;
}

// ─── Soft-Knee Limiter / Anti-Clipping ────────────────────────────
void AudioMixer::applySoftLimiter(std::vector<float>& samples) {
    for (float& s : samples) {
        // Soft saturate using fast rational tanh approximation
        if (s > 1.0f) {
            s = 1.0f - (1.0f / (1.0f + (s - 1.0f)));
        } else if (s < -1.0f) {
            s = -1.0f + (1.0f / (1.0f + (-s - 1.0f)));
        }
    }
}

// ─── Mix Output Rendering ────────────────────────────────────────
AudioFramePtr AudioMixer::renderMaster(int frameCount, int64_t pts_us) {
    return renderBus(Bus_Master, frameCount, pts_us);
}

AudioFramePtr AudioMixer::renderHeadphones(int frameCount, int64_t pts_us) {
    return renderBus(Bus_Headphones, frameCount, pts_us);
}

AudioFramePtr AudioMixer::renderMixMinus(uint64_t excludeSourceId, int frameCount, int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto outFrame = std::make_shared<AudioFrame>();
    outFrame->channels = m_channels;
    outFrame->sample_rate = m_sampleRate;
    outFrame->frames = frameCount;
    outFrame->pts_us = pts_us;
    outFrame->samples.assign(static_cast<size_t>(frameCount * m_channels), 0.0f);

    float* outData = outFrame->samples.data();
    size_t samplesNeeded = static_cast<size_t>(frameCount * m_channels);

    for (auto& pair : m_channelsMap) {
        auto& ch = pair.second;

        // The heart of Mix-Minus: Completely exclude the caller's own audio
        if (ch.config.sourceId == excludeSourceId) continue;
        if (ch.config.mute) continue;

        float effectiveGain = ch.config.gain * ch.afvMultiplier;
        if (effectiveGain <= 0.0001f) continue;

        // Equal-power pan calculation
        float pan = ch.config.pan;
        float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
        float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

        size_t available = std::min(samplesNeeded, ch.sampleQueue.size());
        for (size_t i = 0; i < available; i += 2) {
            float inL = ch.sampleQueue[i];
            float inR = (i + 1 < available) ? ch.sampleQueue[i + 1] : inL;

            outData[i + 0] += inL * effectiveGain * panL;
            outData[i + 1] += inR * effectiveGain * panR;
        }
    }

    applySoftLimiter(outFrame->samples);
    return outFrame;
}

AudioFramePtr AudioMixer::renderBus(AudioBus bus, int frameCount, int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto outFrame = std::make_shared<AudioFrame>();
    outFrame->channels = m_channels;
    outFrame->sample_rate = m_sampleRate;
    outFrame->frames = frameCount;
    outFrame->pts_us = pts_us;
    outFrame->samples.assign(static_cast<size_t>(frameCount * m_channels), 0.0f);

    float* outData = outFrame->samples.data();
    size_t samplesNeeded = static_cast<size_t>(frameCount * m_channels);

    bool isHeadphones = (bus == Bus_Headphones);

    float maxPeakL = 0.0f;
    float maxPeakR = 0.0f;

    for (auto& pair : m_channelsMap) {
        auto& ch = pair.second;

        // Solo PFL logic for headphones
        if (isHeadphones && m_anySoloActive && !ch.config.solo) {
            continue;
        }

        // Mute check (solo bypasses mute in headphones)
        if (ch.config.mute && !(isHeadphones && ch.config.solo)) {
            continue;
        }

        // Bus routing check
        if (!isHeadphones && (ch.config.busRouting & bus) == 0) {
            continue;
        }

        float effectiveGain = ch.config.gain * ch.afvMultiplier;
        if (effectiveGain <= 0.0001f) {
            // Drain queue even if muted/silent to maintain AV sync
            size_t available = std::min(samplesNeeded, ch.sampleQueue.size());
            ch.sampleQueue.erase(ch.sampleQueue.begin(), ch.sampleQueue.begin() + static_cast<long>(available));
            continue;
        }

        // Equal-power pan calculation
        float pan = ch.config.pan;
        float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
        float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

        size_t available = std::min(samplesNeeded, ch.sampleQueue.size());
        float chPeakL = 0.0f;
        float chPeakR = 0.0f;

        for (size_t i = 0; i < available; i += 2) {
            float inL = ch.sampleQueue[i];
            float inR = (i + 1 < available) ? ch.sampleQueue[i + 1] : inL;

            chPeakL = std::max(chPeakL, std::abs(inL));
            chPeakR = std::max(chPeakR, std::abs(inR));

            outData[i + 0] += inL * effectiveGain * panL;
            outData[i + 1] += inR * effectiveGain * panR;
        }

        // Update channel meters
        ch.meters.peakL = chPeakL;
        ch.meters.peakR = chPeakR;
        ch.meters.peakDbL = linearToDb(chPeakL);
        ch.meters.peakDbR = linearToDb(chPeakR);

        // Pop processed samples if this is the Master or Headphones bus render
        if (bus == Bus_Master || (isHeadphones && !hasChannel(1 /* master tracking */))) {
            ch.sampleQueue.erase(ch.sampleQueue.begin(), ch.sampleQueue.begin() + static_cast<long>(available));
        }
    }

    // Apply Bus Master Fader
    float busFader = 1.0f;
    auto itFader = m_busMasterGains.find(bus);
    if (itFader != m_busMasterGains.end()) {
        busFader = itFader->second;
    }

    for (size_t i = 0; i < samplesNeeded; i += 2) {
        outData[i + 0] *= busFader;
        outData[i + 1] *= busFader;

        maxPeakL = std::max(maxPeakL, std::abs(outData[i + 0]));
        maxPeakR = std::max(maxPeakR, std::abs(outData[i + 1]));
    }

    // Anti-clipping limiter
    applySoftLimiter(outFrame->samples);

    // Update Master meters
    if (bus == Bus_Master) {
        m_masterMeters.peakL = maxPeakL;
        m_masterMeters.peakR = maxPeakR;
        m_masterMeters.peakDbL = linearToDb(maxPeakL);
        m_masterMeters.peakDbR = linearToDb(maxPeakR);

        if (m_masterCb) {
            m_masterCb(outFrame);
        }
    }

    return outFrame;
}

} // namespace sm
