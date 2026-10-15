#pragma once
/**
 * sm::AudioMixer — vMix 29-Class Multi-Bus Pro Audio Matrix Mixer
 *
 * Capabilities:
 *   - 32-bit Floating Point PCM Audio Pipeline (48 kHz / stereo)
 *   - Master Bus, Headphones / Director Bus, and Sub-Buses A through G (vMix 29)
 *   - Compound Bus Routing (AB, CD, DE, ABCD, DEFG)
 *   - Automated Audio-Follow-Video (AFV) with smooth volume crossfading
 *   - Automated Mix-Minus generation for Zoom, WebRTC, and remote callers
 *   - Per-channel Gain (0.0 to 2.0 / +6dB), Pan (-1.0 to +1.0), Mute, Solo, and Delay
 *   - Real-time dBFS Peak and RMS metering per channel
 *   - Soft-knee limiter / brickwall anti-clipping protection
 */

#include "streammix/frame.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <deque>

namespace sm {

// ─── vMix 29 Audio Bus Bitmasks ──────────────────────────────────
enum AudioBus : uint32_t {
    Bus_Master     = 1 << 0,  ///< Main broadcast / stream bus
    Bus_Headphones = 1 << 1,  ///< PFL / Solo / Director monitor
    Bus_A          = 1 << 2,  ///< Sub-bus A
    Bus_B          = 1 << 3,  ///< Sub-bus B
    Bus_C          = 1 << 4,  ///< Sub-bus C
    Bus_D          = 1 << 5,  ///< Sub-bus D
    Bus_E          = 1 << 6,  ///< Sub-bus E
    Bus_F          = 1 << 7,  ///< Sub-bus F
    Bus_G          = 1 << 8   ///< Sub-bus G
};

// Common Compound Bus Bitmasks (vMix 29 standard)
constexpr uint32_t BUS_COMBO_AB   = Bus_A | Bus_B;
constexpr uint32_t BUS_COMBO_CD   = Bus_C | Bus_D;
constexpr uint32_t BUS_COMBO_DE   = Bus_D | Bus_E;
constexpr uint32_t BUS_COMBO_ABCD = Bus_A | Bus_B | Bus_C | Bus_D;
constexpr uint32_t BUS_COMBO_DEFG = Bus_D | Bus_E | Bus_F | Bus_G;

struct AudioChannelConfig {
    uint64_t sourceId = 0;
    std::string name;
    float gain = 1.0f;          ///< 0.0 to 2.0 (1.0 = unity, 2.0 = +6dB)
    float pan = 0.0f;           ///< -1.0 (hard left) to +1.0 (hard right)
    bool mute = false;
    bool solo = false;
    bool afv = false;           ///< Audio Follow Video
    int delayMs = 0;            ///< Audio sync delay (0 .. 1000 ms)
    uint32_t busRouting = Bus_Master; ///< Enabled buses
};

struct AudioMeterData {
    float peakL = 0.0f;         ///< 0.0 to 1.0 linear amplitude
    float peakR = 0.0f;
    float peakDbL = -100.0f;    ///< dBFS (-100 to 0)
    float peakDbR = -100.0f;
};

using AudioCallback = std::function<void(AudioFramePtr)>;

class AudioMixer {
public:
    explicit AudioMixer(int sampleRate = 48000, int channels = 2);
    ~AudioMixer() = default;

    // ─── Channel Management ──────────────────────────────────────
    void addChannel(uint64_t sourceId, const std::string& name = "");
    void removeChannel(uint64_t sourceId);
    bool hasChannel(uint64_t sourceId) const;

    // ─── Input Audio Ingestion (Thread-Safe) ──────────────────────
    void pushAudio(uint64_t sourceId, AudioFramePtr frame);

    // ─── Channel Strip Controls ──────────────────────────────────
    void setGain(uint64_t sourceId, float gain);
    void setPan(uint64_t sourceId, float pan);
    void setMute(uint64_t sourceId, bool mute);
    void setSolo(uint64_t sourceId, bool solo);
    void setAFV(uint64_t sourceId, bool afv);
    void setDelay(uint64_t sourceId, int delayMs);
    void setBusRouting(uint64_t sourceId, uint32_t busMask);

    float getGain(uint64_t sourceId) const;
    float getPan(uint64_t sourceId) const;
    bool  isMuted(uint64_t sourceId) const;
    bool  isSolo(uint64_t sourceId) const;
    bool  isAFV(uint64_t sourceId) const;
    uint32_t getBusRouting(uint64_t sourceId) const;

    // ─── Pro Audio DSP Rack (EQ, Gate, Compressor, Limiter) ──────
    std::shared_ptr<class AudioDSPRack> getChannelDSP(uint64_t sourceId);

    // ─── Bus Master Faders ───────────────────────────────────────
    void setBusMasterGain(AudioBus bus, float gain);
    float getBusMasterGain(AudioBus bus) const;

    // Convenience volume aliases
    void setVolume(uint64_t sourceId, float vol) { setGain(sourceId, vol); }
    float getVolume(uint64_t sourceId) const { return getGain(sourceId); }
    void setMasterVolume(float vol) { setBusMasterGain(AudioBus::Bus_Master, vol); }
    float masterVolume() const { return getBusMasterGain(AudioBus::Bus_Master); }
    float headphonesVolume() const { return getBusMasterGain(AudioBus::Bus_Headphones); }
    bool isMasterMuted() const { return getBusMasterGain(AudioBus::Bus_Master) <= 0.0001f; }

    // ─── Audio Follow Video (AFV) Update ─────────────────────────
    void updateAFVState(uint64_t activeProgramSourceId, double deltaSeconds = 0.033);

    // ─── Real-Time Metering ──────────────────────────────────────
    AudioMeterData getChannelMeters(uint64_t sourceId) const;
    AudioMeterData getMasterMeters() const;

    // ─── Mix Output Rendering ────────────────────────────────────
    /// Mixes and returns the main Master audio feed (for live stream & recording)
    AudioFramePtr renderMaster(int frameCount = 1024, int64_t pts_us = 0);

    /// Mixes and returns the director Headphones feed (handles solo PFL)
    AudioFramePtr renderHeadphones(int frameCount = 1024, int64_t pts_us = 0);

    /// Mixes and returns any arbitrary bus (e.g. Bus A for PA, Bus B for translation)
    AudioFramePtr renderBus(AudioBus bus, int frameCount = 1024, int64_t pts_us = 0);

    /// Automated Mix-Minus: Returns a complete mix of all channels EXCLUDING excludeSourceId
    /// (Mandatory for Zoom, WebRTC, and remote callers to eliminate caller feedback echo)
    AudioFramePtr renderMixMinus(uint64_t excludeSourceId, int frameCount = 1024, int64_t pts_us = 0);

    // ─── Output Callbacks ────────────────────────────────────────
    void setMasterCallback(AudioCallback cb) { m_masterCb = std::move(cb); }

private:
    struct InternalChannel {
        AudioChannelConfig config;
        std::deque<float> sampleQueue; ///< Interleaved float buffer
        float afvMultiplier = 1.0f;     ///< Fades between 0.0 and 1.0
        AudioMeterData meters;
        std::shared_ptr<class AudioDSPRack> dsp;
    };

    void applySoftLimiter(std::vector<float>& samples);
    static float linearToDb(float linear);

    int m_sampleRate = 48000;
    int m_channels = 2;
    mutable std::mutex m_mutex;

    std::unordered_map<uint64_t, InternalChannel> m_channelsMap;
    std::unordered_map<uint32_t, float> m_busMasterGains;

    uint64_t m_currentProgramSourceId = 0;
    bool m_anySoloActive = false;

    AudioMeterData m_masterMeters;
    AudioCallback m_masterCb;
};

} // namespace sm
