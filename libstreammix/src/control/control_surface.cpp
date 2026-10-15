/**
 * sm::MidiController — Implementation
 *
 * Implements USB MIDI control surface ingestion, preset mappings
 * (Akai APC, Behringer X-Touch, Novation Launchpad), and two-way
 * LED tally feedback to illuminate switcher buttons in real time.
 */

#include "streammix/control_surface.h"
#include <algorithm>

namespace sm {

MidiController::MidiController(Context& context)
    : m_context(context)
{
}

void MidiController::setMidiSendCallback(MidiSendCallback cb) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sendCb = std::move(cb);
}

void MidiController::sendMidi(uint8_t status, uint8_t data1, uint8_t data2) {
    if (m_sendCb) {
        m_sendCb(status, data1, data2);
    }
}

void MidiController::addMapping(const MidiMappingEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mappings.push_back(entry);
}

void MidiController::addFeedback(const MidiFeedbackEntry& entry) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_feedbacks.push_back(entry);
}

void MidiController::clearMappings() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mappings.clear();
    m_feedbacks.clear();
}

void MidiController::processMidiMessage(uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t msgType = status & 0xF0;
    uint8_t channel = (status & 0x0F) + 1; // 1 to 16

    bool isCC = (msgType == 0xB0);
    bool isNoteOn = (msgType == 0x90 && data2 > 0);

    // If Note Off (0x80) or Note On with 0 velocity, ignore for action triggers
    if (!isCC && !isNoteOn) return;

    std::vector<MidiMappingEntry> matched;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& entry : m_mappings) {
            if ((entry.channel == 0 || entry.channel == channel) &&
                entry.isCC == isCC &&
                entry.controlIndex == data1) {
                matched.push_back(entry);
            }
        }
    }

    for (const auto& m : matched) {
        executeAction(m, data2);
    }

    // Refresh LED feedback across hardware buttons
    updateLedFeedback();
}

void MidiController::executeAction(const MidiMappingEntry& mapping, uint8_t velocityOrValue) {
    switch (mapping.command) {
        case MidiCommandType::Cut:
            m_context.cut();
            break;
        case MidiCommandType::Fade:
            m_context.startTransition(TransitionType::Fade, 500);
            break;
        case MidiCommandType::QuickPlay:
            m_context.quickPlay(mapping.targetParam);
            break;
        case MidiCommandType::FTB:
            m_context.toggleFTB();
            break;
        case MidiCommandType::PreviewInput:
            m_context.setPreviewSource(mapping.targetParam);
            break;
        case MidiCommandType::ActiveInput:
            m_context.setProgramSource(mapping.targetParam);
            break;
        case MidiCommandType::ToggleOverlay:
            m_context.toggleOverlay(mapping.targetParam, OverlayTransition::Cut, 0);
            break;
        case MidiCommandType::ShowOverlay:
            m_context.showOverlay(mapping.targetParam);
            break;
        case MidiCommandType::HideOverlay:
            m_context.hideOverlay(mapping.targetParam);
            break;
        case MidiCommandType::SetTBar: {
            float pos = static_cast<float>(velocityOrValue) / 127.0f;
            m_context.setTBarPosition(std::clamp(pos, 0.0f, 1.0f));
            break;
        }
        case MidiCommandType::SetMasterVolume: {
            float vol = static_cast<float>(velocityOrValue) / 127.0f;
            m_context.audioMixer().setMasterVolume(std::clamp(vol, 0.0f, 1.0f));
            break;
        }
        case MidiCommandType::SetInputVolume: {
            float vol = static_cast<float>(velocityOrValue) / 127.0f;
            m_context.audioMixer().setVolume(mapping.targetParam, std::clamp(vol, 0.0f, 1.0f));
            break;
        }
        case MidiCommandType::ToggleAudioMute: {
            bool muted = m_context.audioMixer().isMuted(mapping.targetParam);
            m_context.audioMixer().setMute(mapping.targetParam, !muted);
            break;
        }
        case MidiCommandType::SoloInput: {
            bool solo = m_context.audioMixer().isSolo(mapping.targetParam);
            m_context.audioMixer().setSolo(mapping.targetParam, !solo);
            break;
        }
        case MidiCommandType::StartStreaming:
            m_context.startStreaming();
            break;
        case MidiCommandType::StopStreaming:
            m_context.stopStreaming();
            break;
        case MidiCommandType::StartRecording:
            m_context.startRecording();
            break;
        case MidiCommandType::StopRecording:
            m_context.stopRecording();
            break;
    }
}

void MidiController::updateLedFeedback() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_sendCb) return;

    uint64_t curPgm = m_context.compositor().programSourceId();
    uint64_t curPvw = m_context.compositor().previewSourceId();

    for (const auto& fb : m_feedbacks) {
        uint8_t status = fb.isCC ? (0xB0 | ((fb.channel - 1) & 0x0F))
                                 : (0x90 | ((fb.channel - 1) & 0x0F));
        uint8_t val = static_cast<uint8_t>(MidiLedColor::Off);

        switch (fb.feedbackType) {
            case MidiCommandType::ActiveInput:
                if (curPgm == static_cast<uint64_t>(fb.targetParam)) {
                    val = static_cast<uint8_t>(MidiLedColor::Red);
                }
                break;
            case MidiCommandType::PreviewInput:
                if (curPvw == static_cast<uint64_t>(fb.targetParam)) {
                    val = static_cast<uint8_t>(MidiLedColor::Green);
                }
                break;
            case MidiCommandType::ToggleOverlay: {
                const auto& ov = m_context.compositor().overlay(fb.targetParam);
                if (ov.state == OverlayState::Active || ov.state == OverlayState::In) {
                    val = static_cast<uint8_t>(MidiLedColor::Amber);
                }
                break;
            }
            case MidiCommandType::ToggleAudioMute:
                if (m_context.audioMixer().isMuted(fb.targetParam)) {
                    val = static_cast<uint8_t>(MidiLedColor::Red);
                }
                break;
            case MidiCommandType::StartStreaming:
                if (m_context.isStreaming()) {
                    val = static_cast<uint8_t>(MidiLedColor::Red);
                }
                break;
            case MidiCommandType::StartRecording:
                if (m_context.isRecording()) {
                    val = static_cast<uint8_t>(MidiLedColor::Red);
                }
                break;
            default:
                break;
        }

        sendMidi(status, fb.controlIndex, val);
    }
}

// ─── Presets ─────────────────────────────────────────────────────

void MidiController::loadAkaiApcMiniPreset() {
    clearMappings();

    // Akai APC Mini Channel 1
    // 1. Program Row: Notes 0..7 -> Inputs 1..8 (LED Red)
    for (uint8_t i = 0; i < 8; ++i) {
        addMapping({1, false, i, MidiCommandType::ActiveInput, i + 1});
        addFeedback({1, false, i, MidiCommandType::ActiveInput, i + 1});
    }

    // 2. Preview Row: Notes 8..15 -> Inputs 1..8 (LED Green)
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 8 + i;
        addMapping({1, false, note, MidiCommandType::PreviewInput, i + 1});
        addFeedback({1, false, note, MidiCommandType::PreviewInput, i + 1});
    }

    // 3. Overlays 1..8 Row: Notes 16..23 (LED Amber)
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 16 + i;
        addMapping({1, false, note, MidiCommandType::ToggleOverlay, i + 1});
        addFeedback({1, false, note, MidiCommandType::ToggleOverlay, i + 1});
    }

    // 4. Audio Mute Row: Notes 24..31 (LED Red)
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 24 + i;
        addMapping({1, false, note, MidiCommandType::ToggleAudioMute, i + 1});
        addFeedback({1, false, note, MidiCommandType::ToggleAudioMute, i + 1});
    }

    // 5. Function Buttons (Side row)
    addMapping({1, false, 64, MidiCommandType::Cut, 0});
    addMapping({1, false, 65, MidiCommandType::Fade, 0});
    addMapping({1, false, 66, MidiCommandType::FTB, 0});
    addMapping({1, false, 67, MidiCommandType::StartStreaming, 0});
    addFeedback({1, false, 67, MidiCommandType::StartStreaming, 0});

    // 6. Volume Sliders: CC 48..55 -> Inputs 1..8 Volume
    for (uint8_t i = 0; i < 8; ++i) {
        addMapping({1, true, static_cast<uint8_t>(48 + i), MidiCommandType::SetInputVolume, i + 1});
    }
    // Master Slider: CC 56 -> Master Volume
    addMapping({1, true, 56, MidiCommandType::SetMasterVolume, 0});
}

void MidiController::loadBehringerXTouchMiniPreset() {
    clearMappings();

    // Behringer X-Touch Mini (Layer A)
    // Buttons: Notes 0..7 -> Program Inputs 1..8
    for (uint8_t i = 0; i < 8; ++i) {
        addMapping({1, false, i, MidiCommandType::ActiveInput, i + 1});
        addFeedback({1, false, i, MidiCommandType::ActiveInput, i + 1});
    }

    // Buttons: Notes 8..15 -> Preview Inputs 1..8
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 8 + i;
        addMapping({1, false, note, MidiCommandType::PreviewInput, i + 1});
        addFeedback({1, false, note, MidiCommandType::PreviewInput, i + 1});
    }

    // Rotary Encoders: CC 1..8 -> Inputs 1..8 Volume
    for (uint8_t i = 0; i < 8; ++i) {
        addMapping({1, true, static_cast<uint8_t>(1 + i), MidiCommandType::SetInputVolume, i + 1});
    }

    // Master Slider: CC 9 -> Physical T-Bar Fader!
    addMapping({1, true, 9, MidiCommandType::SetTBar, 0});
}

void MidiController::loadNovationLaunchpadPreset() {
    clearMappings();

    // Launchpad Row 0: Program Inputs 1..8
    for (uint8_t i = 0; i < 8; ++i) {
        addMapping({1, false, i, MidiCommandType::ActiveInput, i + 1});
        addFeedback({1, false, i, MidiCommandType::ActiveInput, i + 1});
    }

    // Launchpad Row 1: Preview Inputs 1..8
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 16 + i;
        addMapping({1, false, note, MidiCommandType::PreviewInput, i + 1});
        addFeedback({1, false, note, MidiCommandType::PreviewInput, i + 1});
    }

    // Launchpad Row 2: Overlays 1..8
    for (uint8_t i = 0; i < 8; ++i) {
        uint8_t note = 32 + i;
        addMapping({1, false, note, MidiCommandType::ToggleOverlay, i + 1});
        addFeedback({1, false, note, MidiCommandType::ToggleOverlay, i + 1});
    }

    // Side buttons: Cut, Fade, FTB
    addMapping({1, false, 112, MidiCommandType::Cut, 0});
    addMapping({1, false, 113, MidiCommandType::Fade, 0});
    addMapping({1, false, 114, MidiCommandType::FTB, 0});
}

} // namespace sm
