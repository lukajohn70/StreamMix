#include "streammix/audio_dsp.h"
#include <cmath>
#include <algorithm>

namespace sm {

static constexpr float PI = 3.14159265358979323846f;

static inline float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

static inline float linearToDb(float lin) {
    if (lin <= 1e-5f) return -100.0f;
    return 20.0f * std::log10(lin);
}

// ─────────────────────────────────────────────────────────────────
//  BiquadFilter Implementation (Direct Form II Transposed)
// ─────────────────────────────────────────────────────────────────

BiquadFilter::BiquadFilter() {
    reset();
}

void BiquadFilter::reset() {
    m_z1L = m_z2L = 0.0f;
    m_z1R = m_z2R = 0.0f;
}

void BiquadFilter::configure(EQFilterType type, float freq, float gainDb, float q, float sampleRate) {
    if (freq <= 10.0f) freq = 10.0f;
    if (freq >= sampleRate * 0.49f) freq = sampleRate * 0.49f;
    if (q <= 0.05f) q = 0.05f;

    float omega = 2.0f * PI * freq / sampleRate;
    float sn = std::sin(omega);
    float cs = std::cos(omega);
    float alpha = sn / (2.0f * q);
    float A = std::pow(10.0f, gainDb / 40.0f); // sqrt of linear gain

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (type) {
        case EQFilterType::HighPass: {
            b0 = (1.0f + cs) / 2.0f;
            b1 = -(1.0f + cs);
            b2 = (1.0f + cs) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cs;
            a2 = 1.0f - alpha;
            break;
        }
        case EQFilterType::LowPass: {
            b0 = (1.0f - cs) / 2.0f;
            b1 = 1.0f - cs;
            b2 = (1.0f - cs) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cs;
            a2 = 1.0f - alpha;
            break;
        }
        case EQFilterType::LowShelf: {
            float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + twoSqrtAAlpha);
            b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
            b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - twoSqrtAAlpha);
            a0 = (A + 1.0f) + (A - 1.0f) * cs + twoSqrtAAlpha;
            a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
            a2 = (A + 1.0f) + (A - 1.0f) * cs - twoSqrtAAlpha;
            break;
        }
        case EQFilterType::HighShelf: {
            float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + twoSqrtAAlpha);
            b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
            b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - twoSqrtAAlpha);
            a0 = (A + 1.0f) - (A - 1.0f) * cs + twoSqrtAAlpha;
            a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
            a2 = (A + 1.0f) - (A - 1.0f) * cs - twoSqrtAAlpha;
            break;
        }
        case EQFilterType::Peaking:
        default: {
            b0 = 1.0f + alpha * A;
            b1 = -2.0f * cs;
            b2 = 1.0f - alpha * A;
            a0 = 1.0f + alpha / A;
            a1 = -2.0f * cs;
            a2 = 1.0f - alpha / A;
            break;
        }
    }

    // Normalize by a0
    float invA0 = 1.0f / a0;
    m_b0 = b0 * invA0;
    m_b1 = b1 * invA0;
    m_b2 = b2 * invA0;
    m_a1 = a1 * invA0;
    m_a2 = a2 * invA0;
}

void BiquadFilter::processStereo(float* samples, size_t frameCount) {
    for (size_t i = 0; i < frameCount; ++i) {
        // Left Channel
        float inL = samples[i * 2];
        float outL = inL * m_b0 + m_z1L;
        m_z1L = inL * m_b1 - outL * m_a1 + m_z2L;
        m_z2L = inL * m_b2 - outL * m_a2;
        samples[i * 2] = outL;

        // Right Channel
        float inR = samples[i * 2 + 1];
        float outR = inR * m_b0 + m_z1R;
        m_z1R = inR * m_b1 - outR * m_a1 + m_z2R;
        m_z2R = inR * m_b2 - outR * m_a2;
        samples[i * 2 + 1] = outR;
    }
}

// ─────────────────────────────────────────────────────────────────
//  ParametricEQ Implementation
// ─────────────────────────────────────────────────────────────────

ParametricEQ::ParametricEQ(float sampleRate) : m_sampleRate(sampleRate) {
    // Default 4-band setup:
    // Band 0: Low Shelf (80 Hz, 0 dB, Q=0.7)
    m_configs[0] = {true, EQFilterType::LowShelf, 80.0f, 0.0f, 0.707f};
    // Band 1: Peaking (500 Hz, 0 dB, Q=1.0)
    m_configs[1] = {true, EQFilterType::Peaking, 500.0f, 0.0f, 1.0f};
    // Band 2: Peaking (2500 Hz, 0 dB, Q=1.0)
    m_configs[2] = {true, EQFilterType::Peaking, 2500.0f, 0.0f, 1.0f};
    // Band 3: High Shelf (10000 Hz, 0 dB, Q=0.7)
    m_configs[3] = {true, EQFilterType::HighShelf, 10000.0f, 0.0f, 0.707f};

    for (int i = 0; i < 4; ++i) {
        updateFilter(i);
    }
}

void ParametricEQ::setBand(int bandIndex, const EQBandConfig& config) {
    if (bandIndex >= 0 && bandIndex < 4) {
        m_configs[bandIndex] = config;
        updateFilter(bandIndex);
    }
}

EQBandConfig ParametricEQ::getBand(int bandIndex) const {
    if (bandIndex >= 0 && bandIndex < 4) {
        return m_configs[bandIndex];
    }
    return {};
}

void ParametricEQ::setSampleRate(float sampleRate) {
    m_sampleRate = sampleRate;
    for (int i = 0; i < 4; ++i) {
        updateFilter(i);
    }
}

void ParametricEQ::updateFilter(int index) {
    m_filters[index].configure(
        m_configs[index].type,
        m_configs[index].frequency,
        m_configs[index].gainDb,
        m_configs[index].q,
        m_sampleRate
    );
}

void ParametricEQ::reset() {
    for (int i = 0; i < 4; ++i) {
        m_filters[i].reset();
    }
}

void ParametricEQ::processStereo(float* samples, size_t frameCount) {
    if (!m_enabled) return;

    for (int i = 0; i < 4; ++i) {
        if (m_configs[i].enabled) {
            m_filters[i].processStereo(samples, frameCount);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
//  AudioCompressor Implementation
// ─────────────────────────────────────────────────────────────────

AudioCompressor::AudioCompressor(float sampleRate) : m_sampleRate(sampleRate) {
    updateCoefficients();
}

void AudioCompressor::setConfig(const CompressorConfig& config) {
    m_config = config;
    updateCoefficients();
}

void AudioCompressor::updateCoefficients() {
    float attackSec = std::max(0.0001f, m_config.attackMs / 1000.0f);
    float releaseSec = std::max(0.001f, m_config.releaseMs / 1000.0f);
    m_attackAlpha = std::exp(-1.0f / (attackSec * m_sampleRate));
    m_releaseAlpha = std::exp(-1.0f / (releaseSec * m_sampleRate));
}

void AudioCompressor::reset() {
    m_envelopeDb = -100.0f;
    m_currentGainReductionDb = 0.0f;
}

float AudioCompressor::computeGain(float inputDb) {
    float T = m_config.thresholdDb;
    float R = std::max(1.0f, m_config.ratio);
    float W = std::max(0.0f, m_config.kneeDb);

    // Hard knee
    if (W <= 0.001f) {
        if (inputDb <= T) {
            return 0.0f;
        }
        return (1.0f / R - 1.0f) * (inputDb - T);
    }

    // Soft knee
    if (inputDb <= T - W / 2.0f) {
        return 0.0f;
    } else if (inputDb >= T + W / 2.0f) {
        return (1.0f / R - 1.0f) * (inputDb - T);
    } else {
        float x = inputDb - T + W / 2.0f;
        return (1.0f / R - 1.0f) * (x * x) / (2.0f * W);
    }
}

void AudioCompressor::processStereo(float* samples, size_t frameCount) {
    if (!m_config.enabled) {
        m_currentGainReductionDb = 0.0f;
        return;
    }

    float makeupLin = dbToLinear(m_config.makeupGainDb);

    for (size_t i = 0; i < frameCount; ++i) {
        float inL = samples[i * 2];
        float inR = samples[i * 2 + 1];

        // Peak detector (max of absolute L and R)
        float peak = std::max(std::abs(inL), std::abs(inR));
        float peakDb = linearToDb(peak);

        // Envelope follower in dB domain
        if (peakDb > m_envelopeDb) {
            m_envelopeDb = m_attackAlpha * m_envelopeDb + (1.0f - m_attackAlpha) * peakDb;
        } else {
            m_envelopeDb = m_releaseAlpha * m_envelopeDb + (1.0f - m_releaseAlpha) * peakDb;
        }

        // Compute gain reduction (dB)
        float grDb = computeGain(m_envelopeDb);
        m_currentGainReductionDb = grDb;

        float gainLin = dbToLinear(grDb) * makeupLin;
        samples[i * 2] *= gainLin;
        samples[i * 2 + 1] *= gainLin;
    }
}

// ─────────────────────────────────────────────────────────────────
//  AudioNoiseGate Implementation
// ─────────────────────────────────────────────────────────────────

AudioNoiseGate::AudioNoiseGate(float sampleRate) : m_sampleRate(sampleRate) {
    updateSteps();
}

void AudioNoiseGate::setConfig(const GateConfig& config) {
    m_config = config;
    updateSteps();
}

void AudioNoiseGate::updateSteps() {
    float attackFrames = std::max(1.0f, (m_config.attackMs / 1000.0f) * m_sampleRate);
    float releaseFrames = std::max(1.0f, (m_config.releaseMs / 1000.0f) * m_sampleRate);
    m_attackStep = std::abs(m_config.rangeDb) / attackFrames;
    m_releaseStep = std::abs(m_config.rangeDb) / releaseFrames;
}

void AudioNoiseGate::reset() {
    m_state = GateState::Closed;
    m_currentAttenuationDb = m_config.rangeDb;
    m_holdCounter = 0;
}

void AudioNoiseGate::processStereo(float* samples, size_t frameCount) {
    if (!m_config.enabled) {
        m_currentAttenuationDb = 0.0f;
        return;
    }

    int holdSamples = static_cast<int>((m_config.holdMs / 1000.0f) * m_sampleRate);

    for (size_t i = 0; i < frameCount; ++i) {
        float peak = std::max(std::abs(samples[i * 2]), std::abs(samples[i * 2 + 1]));
        float peakDb = linearToDb(peak);

        // State Machine
        if (peakDb >= m_config.thresholdDb) {
            m_state = GateState::Open;
            m_holdCounter = holdSamples;
        } else if (m_state == GateState::Open) {
            m_state = GateState::Hold;
        } else if (m_state == GateState::Hold) {
            if (--m_holdCounter <= 0) {
                m_state = GateState::Release;
            }
        }

        // Apply smooth attenuation
        if (m_state == GateState::Open || m_state == GateState::Hold) {
            // Ramp up to 0 dB
            m_currentAttenuationDb = std::min(0.0f, m_currentAttenuationDb + m_attackStep);
        } else { // Release or Closed
            // Ramp down to rangeDb (e.g. -60 dB)
            m_currentAttenuationDb = std::max(m_config.rangeDb, m_currentAttenuationDb - m_releaseStep);
            if (m_currentAttenuationDb <= m_config.rangeDb) {
                m_state = GateState::Closed;
            }
        }

        float attLin = dbToLinear(m_currentAttenuationDb);
        samples[i * 2] *= attLin;
        samples[i * 2 + 1] *= attLin;
    }
}

// ─────────────────────────────────────────────────────────────────
//  AudioLimiter Implementation (Brickwall Peak Limiter)
// ─────────────────────────────────────────────────────────────────

AudioLimiter::AudioLimiter(float sampleRate) : m_sampleRate(sampleRate) {
    updateCoefficients();
}

void AudioLimiter::setConfig(const LimiterConfig& config) {
    m_config = config;
    updateCoefficients();
}

void AudioLimiter::updateCoefficients() {
    float releaseSec = std::max(0.001f, m_config.releaseMs / 1000.0f);
    m_releaseAlpha = std::exp(-1.0f / (releaseSec * m_sampleRate));
}

void AudioLimiter::reset() {
    m_currentGain = 1.0f;
    m_currentReductionDb = 0.0f;
}

void AudioLimiter::processStereo(float* samples, size_t frameCount) {
    if (!m_config.enabled) {
        m_currentReductionDb = 0.0f;
        return;
    }

    float ceilingLin = dbToLinear(m_config.ceilingDb);

    for (size_t i = 0; i < frameCount; ++i) {
        float inL = samples[i * 2];
        float inR = samples[i * 2 + 1];
        float peak = std::max(std::abs(inL), std::abs(inR));

        float targetGain = 1.0f;
        if (peak > ceilingLin && peak > 1e-6f) {
            targetGain = ceilingLin / peak;
        }

        // Instant attack, exponential release
        if (targetGain < m_currentGain) {
            m_currentGain = targetGain;
        } else {
            m_currentGain = m_releaseAlpha * m_currentGain + (1.0f - m_releaseAlpha) * 1.0f;
        }

        m_currentReductionDb = linearToDb(m_currentGain);

        samples[i * 2] *= m_currentGain;
        samples[i * 2 + 1] *= m_currentGain;

        // Hard safety clamp to ceiling
        samples[i * 2] = std::clamp(samples[i * 2], -ceilingLin, ceilingLin);
        samples[i * 2 + 1] = std::clamp(samples[i * 2 + 1], -ceilingLin, ceilingLin);
    }
}

// ─────────────────────────────────────────────────────────────────
//  AudioDSPRack Implementation
// ─────────────────────────────────────────────────────────────────

AudioDSPRack::AudioDSPRack(float sampleRate)
    : m_sampleRate(sampleRate)
    , m_eq(sampleRate)
    , m_gate(sampleRate)
    , m_compressor(sampleRate)
    , m_limiter(sampleRate)
{
}

void AudioDSPRack::reset() {
    m_eq.reset();
    m_gate.reset();
    m_compressor.reset();
    m_limiter.reset();
}

void AudioDSPRack::processStereo(float* samples, size_t frameCount) {
    if (m_bypassAll || !samples || frameCount == 0) return;

    // 1. Parametric EQ
    m_eq.processStereo(samples, frameCount);

    // 2. Noise Gate
    m_gate.processStereo(samples, frameCount);

    // 3. Compressor
    m_compressor.processStereo(samples, frameCount);

    // 4. Brickwall Peak Limiter
    m_limiter.processStereo(samples, frameCount);
}

void AudioDSPRack::process(AudioFramePtr frame) {
    if (!frame || frame->samples.empty() || m_bypassAll) return;
    if (frame->channels == 2) {
        processStereo(frame->samples.data(), frame->frames);
    }
}

} // namespace sm
