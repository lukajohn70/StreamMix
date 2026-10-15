#pragma once
/**
 * sm::AudioDSPRack — vMix 29 Broadcast Pro Audio Dynamics & DSP Pipeline
 *
 * Implements:
 *   - 4-Band Parametric Equalizer (HighPass, LowShelf, Peaking/Bell, HighShelf)
 *   - Audio Dynamics Compressor with soft knee, envelope follower & auto makeup gain
 *   - Studio Noise Gate with threshold, attack, hold, and release
 *   - Brickwall Peak Limiter (-0.1 dBFS ceiling anti-clip)
 *   - Real-time Gain Reduction (GR) and gate status telemetry
 *   - 32-bit floating point stereo DSP architecture (48 kHz default)
 */

#include "streammix/frame.h"
#include <vector>
#include <cmath>
#include <string>
#include <memory>
#include <algorithm>

namespace sm {

// ─── EQ Filter Type ──────────────────────────────────────────────
enum class EQFilterType {
    HighPass,
    LowShelf,
    Peaking,
    HighShelf,
    LowPass
};

struct EQBandConfig {
    bool enabled = true;
    EQFilterType type = EQFilterType::Peaking;
    float frequency = 1000.0f;  ///< 20 Hz to 20000 Hz
    float gainDb = 0.0f;        ///< -24.0 dB to +24.0 dB (for peaking & shelf)
    float q = 1.0f;             ///< 0.1 to 10.0 (Q / resonance)
};

// ─── Biquad Filter (Stereo) ──────────────────────────────────────
class BiquadFilter {
public:
    BiquadFilter();
    void configure(EQFilterType type, float freq, float gainDb, float q, float sampleRate);
    void reset();

    /// Process interleaved stereo samples in place
    void processStereo(float* samples, size_t frameCount);

private:
    float m_b0 = 1.0f, m_b1 = 0.0f, m_b2 = 0.0f;
    float m_a1 = 0.0f, m_a2 = 0.0f;

    // State history for Left and Right channels (Direct Form II Transposed)
    float m_z1L = 0.0f, m_z2L = 0.0f;
    float m_z1R = 0.0f, m_z2R = 0.0f;
};

// ─── 4-Band Parametric Equalizer ─────────────────────────────────
class ParametricEQ {
public:
    explicit ParametricEQ(float sampleRate = 48000.0f);

    void setBand(int bandIndex, const EQBandConfig& config);
    EQBandConfig getBand(int bandIndex) const;
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setSampleRate(float sampleRate);

    void processStereo(float* samples, size_t frameCount);
    void reset();

private:
    bool m_enabled = true;
    float m_sampleRate = 48000.0f;
    EQBandConfig m_configs[4];
    BiquadFilter m_filters[4];

    void updateFilter(int index);
};

// ─── Dynamics Compressor ─────────────────────────────────────────
struct CompressorConfig {
    bool enabled = true;
    float thresholdDb = -20.0f;  ///< -60.0 to 0.0 dBFS
    float ratio = 4.0f;          ///< 1.0 to 30.0 (4.0 = 4:1)
    float attackMs = 10.0f;      ///< 0.1 to 100 ms
    float releaseMs = 100.0f;    ///< 10 to 1000 ms
    float kneeDb = 6.0f;         ///< 0.0 (hard) to 20.0 (soft knee)
    float makeupGainDb = 0.0f;   ///< 0.0 to 24.0 dB
};

class AudioCompressor {
public:
    explicit AudioCompressor(float sampleRate = 48000.0f);

    void setConfig(const CompressorConfig& config);
    CompressorConfig getConfig() const { return m_config; }
    void setEnabled(bool enabled) { m_config.enabled = enabled; }
    bool isEnabled() const { return m_config.enabled; }

    void processStereo(float* samples, size_t frameCount);
    void reset();

    /// Returns current gain reduction in decibels (negative value, e.g. -3.5 dB)
    float getGainReductionDb() const { return m_currentGainReductionDb; }

private:
    CompressorConfig m_config;
    float m_sampleRate = 48000.0f;
    float m_envelopeDb = -100.0f;
    float m_currentGainReductionDb = 0.0f;

    float m_attackAlpha = 0.0f;
    float m_releaseAlpha = 0.0f;

    void updateCoefficients();
    float computeGain(float inputDb);
};

// ─── Noise Gate ──────────────────────────────────────────────────
struct GateConfig {
    bool enabled = true;
    float thresholdDb = -45.0f;  ///< -80.0 to -10.0 dBFS
    float rangeDb = -60.0f;      ///< Depth of attenuation when closed (-80 to 0)
    float attackMs = 2.0f;       ///< Open time (0.1 to 50 ms)
    float holdMs = 50.0f;        ///< Hold time before release (0 to 500 ms)
    float releaseMs = 150.0f;    ///< Decay time (10 to 1000 ms)
};

enum class GateState {
    Closed,
    Open,
    Hold,
    Release
};

class AudioNoiseGate {
public:
    explicit AudioNoiseGate(float sampleRate = 48000.0f);

    void setConfig(const GateConfig& config);
    GateConfig getConfig() const { return m_config; }
    void setEnabled(bool enabled) { m_config.enabled = enabled; }
    bool isEnabled() const { return m_config.enabled; }

    void processStereo(float* samples, size_t frameCount);
    void reset();

    bool isOpen() const { return m_state == GateState::Open || m_state == GateState::Hold; }
    float getAttenuationDb() const { return m_currentAttenuationDb; }

private:
    GateConfig m_config;
    float m_sampleRate = 48000.0f;
    GateState m_state = GateState::Closed;
    float m_currentAttenuationDb = -60.0f;
    int m_holdCounter = 0;

    float m_attackStep = 0.0f;
    float m_releaseStep = 0.0f;

    void updateSteps();
};

// ─── Brickwall Peak Limiter ──────────────────────────────────────
struct LimiterConfig {
    bool enabled = true;
    float ceilingDb = -0.3f;     ///< Maximum peak level (-12.0 to -0.1 dBFS)
    float releaseMs = 50.0f;     ///< Fast recovery (10 to 500 ms)
};

class AudioLimiter {
public:
    explicit AudioLimiter(float sampleRate = 48000.0f);

    void setConfig(const LimiterConfig& config);
    LimiterConfig getConfig() const { return m_config; }
    void setEnabled(bool enabled) { m_config.enabled = enabled; }
    bool isEnabled() const { return m_config.enabled; }

    void processStereo(float* samples, size_t frameCount);
    void reset();

    float getGainReductionDb() const { return m_currentReductionDb; }

private:
    LimiterConfig m_config;
    float m_sampleRate = 48000.0f;
    float m_currentGain = 1.0f;
    float m_currentReductionDb = 0.0f;
    float m_releaseAlpha = 0.0f;

    void updateCoefficients();
};

// ─── Integrated Audio DSP Rack ────────────────────────────────────
class AudioDSPRack {
public:
    explicit AudioDSPRack(float sampleRate = 48000.0f);

    void setBypassAll(bool bypass) { m_bypassAll = bypass; }
    bool isBypassAll() const { return m_bypassAll; }

    ParametricEQ& eq() { return m_eq; }
    const ParametricEQ& eq() const { return m_eq; }

    AudioNoiseGate& gate() { return m_gate; }
    const AudioNoiseGate& gate() const { return m_gate; }

    AudioCompressor& compressor() { return m_compressor; }
    const AudioCompressor& compressor() const { return m_compressor; }

    AudioLimiter& limiter() { return m_limiter; }
    const AudioLimiter& limiter() const { return m_limiter; }

    /// Process an AudioFrame (in place)
    void process(AudioFramePtr frame);

    /// Process raw interleaved stereo samples in place
    void processStereo(float* samples, size_t frameCount);

    void reset();

private:
    bool m_bypassAll = false;
    float m_sampleRate = 48000.0f;

    ParametricEQ m_eq;
    AudioNoiseGate m_gate;
    AudioCompressor m_compressor;
    AudioLimiter m_limiter;
};

} // namespace sm
