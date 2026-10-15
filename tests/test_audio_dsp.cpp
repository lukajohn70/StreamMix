#include "streammix/audio_dsp.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <cmath>

using namespace sm;

static constexpr float PI = 3.14159265358979323846f;

// Helper to generate sine wave
std::vector<float> generateSineWave(float freqHz, float amp, int sampleRate, int frameCount) {
    std::vector<float> buffer(frameCount * 2);
    for (int i = 0; i < frameCount; ++i) {
        float sample = amp * std::sin(2.0f * PI * freqHz * i / sampleRate);
        buffer[i * 2] = sample;     // Left
        buffer[i * 2 + 1] = sample; // Right
    }
    return buffer;
}

float measureRms(const float* buffer, int frameCount) {
    double sum = 0.0;
    for (int i = 0; i < frameCount * 2; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / (frameCount * 2));
}

float measurePeak(const float* buffer, int frameCount) {
    float peak = 0.0f;
    for (int i = 0; i < frameCount * 2; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

int main() {
    std::cout << "[TEST] Starting Pro Audio DSP & Dynamics Rack Test Suite...\n";

    const int sampleRate = 48000;
    const int frameCount = 4800; // 100ms test buffer

    // ─────────────────────────────────────────────────────────────
    // 1. Parametric EQ Test
    // ─────────────────────────────────────────────────────────────
    {
        ParametricEQ eq(sampleRate);

        // Configure Band 0 as HighPass at 150 Hz
        EQBandConfig hpConfig;
        hpConfig.enabled = true;
        hpConfig.type = EQFilterType::HighPass;
        hpConfig.frequency = 150.0f;
        hpConfig.q = 0.707f;
        eq.setBand(0, hpConfig);

        // Generate 50 Hz sub-bass rumble
        auto rumble = generateSineWave(50.0f, 0.5f, sampleRate, frameCount);
        float rawRumbleRms = measureRms(rumble.data(), frameCount);

        eq.processStereo(rumble.data(), frameCount);
        float filteredRumbleRms = measureRms(rumble.data(), frameCount);

        // HighPass at 150 Hz must significantly attenuate 50 Hz (by at least 6 dB)
        assert(filteredRumbleRms < rawRumbleRms * 0.5f);

        // Generate 1000 Hz vocal range
        auto vocal = generateSineWave(1000.0f, 0.5f, sampleRate, frameCount);
        float rawVocalRms = measureRms(vocal.data(), frameCount);

        eq.reset();
        eq.processStereo(vocal.data(), frameCount);
        float filteredVocalRms = measureRms(vocal.data(), frameCount);

        // 1000 Hz should pass through HighPass 150 Hz with virtually zero loss
        assert(std::abs(filteredVocalRms - rawVocalRms) < 0.05f);

        // Test Peaking EQ Boost (+12 dB at 1 kHz)
        EQBandConfig peakConfig;
        peakConfig.enabled = true;
        peakConfig.type = EQFilterType::Peaking;
        peakConfig.frequency = 1000.0f;
        peakConfig.gainDb = 12.0f;
        peakConfig.q = 1.0f;
        eq.setBand(1, peakConfig);

        auto vocalBoost = generateSineWave(1000.0f, 0.2f, sampleRate, frameCount);
        float rawBoostRms = measureRms(vocalBoost.data(), frameCount);
        eq.processStereo(vocalBoost.data(), frameCount);
        float boostedRms = measureRms(vocalBoost.data(), frameCount);

        // +12 dB boost is ~3.98x amplitude
        assert(boostedRms > rawBoostRms * 2.5f);

        std::cout << "  ✓ 4-Band Parametric EQ (HighPass & Peaking) verified\n";
    }

    // ─────────────────────────────────────────────────────────────
    // 2. Audio Noise Gate Test
    // ─────────────────────────────────────────────────────────────
    {
        AudioNoiseGate gate(sampleRate);
        GateConfig gConfig;
        gConfig.enabled = true;
        gConfig.thresholdDb = -30.0f;
        gConfig.rangeDb = -60.0f;
        gConfig.attackMs = 1.0f;
        gConfig.holdMs = 10.0f;
        gConfig.releaseMs = 10.0f;
        gate.setConfig(gConfig);

        // Low amplitude background room noise: -40 dBFS (~0.01)
        auto noise = generateSineWave(1000.0f, 0.01f, sampleRate, frameCount);
        float rawNoiseRms = measureRms(noise.data(), frameCount);

        gate.processStereo(noise.data(), frameCount);
        float gatedNoiseRms = measureRms(noise.data(), frameCount);

        // Noise gate must attenuate signal below threshold
        assert(gatedNoiseRms < rawNoiseRms * 0.2f);
        assert(!gate.isOpen());

        // Loud speaker voice: -10 dBFS (~0.316)
        auto speech = generateSineWave(1000.0f, 0.316f, sampleRate, frameCount);
        gate.processStereo(speech.data(), frameCount);

        // Gate must open for loud speech
        assert(gate.isOpen());
        assert(gate.getAttenuationDb() >= -3.0f);

        std::cout << "  ✓ Audio Noise Gate (threshold, suppression & state) verified\n";
    }

    // ─────────────────────────────────────────────────────────────
    // 3. Audio Dynamics Compressor Test
    // ─────────────────────────────────────────────────────────────
    {
        AudioCompressor comp(sampleRate);
        CompressorConfig cConfig;
        cConfig.enabled = true;
        cConfig.thresholdDb = -18.0f;
        cConfig.ratio = 4.0f;
        cConfig.attackMs = 1.0f;
        cConfig.releaseMs = 50.0f;
        cConfig.kneeDb = 4.0f;
        cConfig.makeupGainDb = 0.0f;
        comp.setConfig(cConfig);

        // Loud signal: -6 dBFS (amp = 0.5f) -> 12 dB above threshold
        auto loudSignal = generateSineWave(1000.0f, 0.5f, sampleRate, frameCount);
        float rawRms = measureRms(loudSignal.data(), frameCount);

        comp.processStereo(loudSignal.data(), frameCount);
        float compRms = measureRms(loudSignal.data(), frameCount);
        float postAttackPeak = measurePeak(loudSignal.data() + frameCount, frameCount / 2);

        // Compressor must reduce RMS and post-attack peak
        assert(compRms < rawRms);
        assert(postAttackPeak < 0.45f);
        assert(comp.getGainReductionDb() < -2.0f);

        std::cout << "  ✓ Audio Compressor (soft knee & gain reduction: " 
                  << comp.getGainReductionDb() << " dB) verified\n";
    }

    // ─────────────────────────────────────────────────────────────
    // 4. Brickwall Peak Limiter Test
    // ─────────────────────────────────────────────────────────────
    {
        AudioLimiter limiter(sampleRate);
        LimiterConfig lConfig;
        lConfig.enabled = true;
        lConfig.ceilingDb = -1.0f; // ceiling ~ 0.891
        limiter.setConfig(lConfig);

        // Excessive signal that would digitally clip (amp = 2.0f)
        auto clippedSignal = generateSineWave(1000.0f, 2.0f, sampleRate, frameCount);
        assert(measurePeak(clippedSignal.data(), frameCount) > 1.0f);

        limiter.processStereo(clippedSignal.data(), frameCount);
        float limitedPeak = measurePeak(clippedSignal.data(), frameCount);

        // Peak must strictly never exceed ceiling (0.895f with floating point margin)
        assert(limitedPeak <= 0.895f);
        assert(limiter.getGainReductionDb() < -3.0f);

        std::cout << "  ✓ Brickwall Peak Limiter (ceiling clamping: " 
                  << limitedPeak << " <= 0.891) verified\n";
    }

    // ─────────────────────────────────────────────────────────────
    // 5. Full AudioDSPRack Integration
    // ─────────────────────────────────────────────────────────────
    {
        AudioDSPRack rack(sampleRate);

        auto frame = std::make_shared<AudioFrame>();
        frame->channels = 2;
        frame->sample_rate = sampleRate;
        frame->frames = frameCount;
        frame->samples = generateSineWave(1000.0f, 1.5f, sampleRate, frameCount);

        // Process full chain
        rack.process(frame);

        // Verify limiter caught the 1.5f surge
        float rackPeak = measurePeak(frame->samples.data(), frameCount);
        assert(rackPeak < 1.0f);

        // Test Master Bypass
        rack.setBypassAll(true);
        frame->samples = generateSineWave(1000.0f, 1.5f, sampleRate, frameCount);
        rack.process(frame);
        float bypassedPeak = measurePeak(frame->samples.data(), frameCount);
        assert(bypassedPeak > 1.4f); // untouched

        std::cout << "  ✓ Integrated AudioDSPRack (EQ + Gate + Comp + Limiter + Bypass) verified\n";
    }

    std::cout << "[PASS] Pro Audio DSP & Dynamics Rack Test Suite completed successfully!\n";
    return 0;
}
