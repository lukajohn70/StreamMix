#pragma once
/**
 * sm::MidiController — USB MIDI Hardware Control Surface Integration (vMix 29)
 *
 * Implements:
 *   - Universal USB MIDI controller mapping (Akai APC mini/40, Behringer X-Touch, Novation Launchpad)
 *   - Real-time 3-byte MIDI message ingestion:
 *       • Note On (0x90) / Note Off (0x80)
 *       • Control Change (0xB0) for T-Bar, volume faders, and rotary pots
 *       • Pitch Bend (0xE0)
 *   - Broadcast action execution: Cut, Fade, QuickPlay, FTB, Preview/Program inputs, Overlays 1-8, Mute, Solo
 *   - Two-Way Hardware LED Tally Feedback:
 *       • Sends velocity / value to controller to illuminate buttons
 *       • Green = Preview, Red = Program, Amber = Overlay active, Blinking Red = Streaming/Recording
 */

#include "streammix/context.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <mutex>
#include <cstdint>

namespace sm {

enum class MidiCommandType {
    Cut,
    Fade,
    QuickPlay,
    FTB,
    PreviewInput,
    ActiveInput,
    ToggleOverlay,
    ShowOverlay,
    HideOverlay,
    SetTBar,
    SetMasterVolume,
    SetInputVolume,
    ToggleAudioMute,
    SoloInput,
    StartStreaming,
    StopStreaming,
    StartRecording,
    StopRecording
};

enum class MidiLedColor : uint8_t {
    Off = 0,
    Green = 1,
    BlinkGreen = 2,
    Red = 3,
    BlinkRed = 4,
    Amber = 5,
    BlinkAmber = 6
};

struct MidiMappingEntry {
    uint8_t channel = 1;        ///< 1-16 (0 = any channel)
    bool isCC = false;          ///< false = Note, true = Control Change (CC)
    uint8_t controlIndex = 0;   ///< Note number (0-127) or CC number (0-127)
    MidiCommandType command = MidiCommandType::Cut;
    int targetParam = 0;        ///< e.g., input index 1-8 or overlay index 1-8
};

struct MidiFeedbackEntry {
    uint8_t channel = 1;
    bool isCC = false;
    uint8_t controlIndex = 0;
    MidiCommandType feedbackType = MidiCommandType::PreviewInput;
    int targetParam = 0;
};

using MidiSendCallback = std::function<void(uint8_t status, uint8_t data1, uint8_t data2)>;

class MidiController {
public:
    explicit MidiController(Context& context);
    ~MidiController() = default;

    // ─── MIDI Message Ingestion ──────────────────────────────────
    void processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2);

    // ─── Mapping Management ──────────────────────────────────────
    void addMapping(const MidiMappingEntry& entry);
    void addFeedback(const MidiFeedbackEntry& entry);
    void clearMappings();

    // ─── Built-in Factory Presets ────────────────────────────────
    void loadAkaiApcMiniPreset();
    void loadBehringerXTouchMiniPreset();
    void loadNovationLaunchpadPreset();

    bool loadPreset(const std::string& name) {
        if (name == "apc_mini" || name == "akai_apc_mini") {
            loadAkaiApcMiniPreset();
            m_currentPreset = "apc_mini";
            return true;
        } else if (name == "xtouch" || name == "behringer_xtouch") {
            loadBehringerXTouchMiniPreset();
            m_currentPreset = "xtouch";
            return true;
        } else if (name == "launchpad" || name == "novation_launchpad") {
            loadNovationLaunchpadPreset();
            m_currentPreset = "launchpad";
            return true;
        }
        return false;
    }
    std::string currentPreset() const { return m_currentPreset; }

    bool connect() { return true; }
    void disconnect() {}

    // ─── Two-Way LED Feedback ────────────────────────────────────
    void setMidiSendCallback(MidiSendCallback cb);
    void updateLedFeedback();

private:
    void executeAction(const MidiMappingEntry& mapping, uint8_t velocityOrValue);
    void sendMidi(uint8_t status, uint8_t data1, uint8_t data2);

    Context& m_context;
    mutable std::mutex m_mutex;
    std::string m_currentPreset;
    std::vector<MidiMappingEntry> m_mappings;
    std::vector<MidiFeedbackEntry> m_feedbacks;
    MidiSendCallback m_sendCb;
};

} // namespace sm
