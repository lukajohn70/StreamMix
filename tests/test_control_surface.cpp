/**
 * StreamMix Integration Test — Hardware Control Surfaces & vMix API (vMix 29)
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <string>

// Test dummy source
class DummyVideoSource : public sm::Source {
public:
    explicit DummyVideoSource(const std::string& name)
        : sm::Source(name, sm::SourceType::Color)
    {
        m_id = s_nextId.fetch_add(1);
        m_width = 1920;
        m_height = 1080;
        m_fps = 60.0;
    }

    bool start(const sm::SourceSettings&) override {
        m_state = sm::SourceState::Running;
        return true;
    }

    void stop() override {
        m_state = sm::SourceState::Stopped;
    }
};

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Control Surface & Stream Deck API Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Context and Register Sources
    sm::Context ctx;
    bool initOk = ctx.initialize();
    assert(initOk);

    auto cam1 = std::make_shared<DummyVideoSource>("Camera 1 - Host");
    auto cam2 = std::make_shared<DummyVideoSource>("Camera 2 - Guest");
    auto cam3 = std::make_shared<DummyVideoSource>("Camera 3 - Wide");
    auto title1 = std::make_shared<DummyVideoSource>("Lower Third Title");

    uint64_t id1 = cam1->id();
    uint64_t id2 = cam2->id();
    uint64_t id3 = cam3->id();
    uint64_t idTitle = title1->id();

    ctx.addSource(cam1);
    ctx.addSource(cam2);
    ctx.addSource(cam3);
    ctx.addSource(title1);

    ctx.setProgramSource(id1);
    ctx.setPreviewSource(id2);
    assert(ctx.compositor().programSourceId() == id1);
    assert(ctx.compositor().previewSourceId() == id2);
    std::cout << "[PASS] Production context initialized with 4 sources.\n";

    // 2. vMix 29 HTTP / REST API Emulation (Companion & Stream Deck Commands)
    std::cout << "\n--- Testing vMix REST API Execution ---\n";

    // Test PreviewInput & ActiveInput
    ctx.executeVmixFunction("PreviewInput", {{"Input", std::to_string(id3)}});
    assert(ctx.compositor().previewSourceId() == id3);
    std::cout << "[PASS] PreviewInput command set preview to Camera 3.\n";

    ctx.executeVmixFunction("ActiveInput", {{"Input", std::to_string(id2)}});
    assert(ctx.compositor().programSourceId() == id2);
    std::cout << "[PASS] ActiveInput command set program to Camera 2.\n";

    // Test Cut
    ctx.executeVmixFunction("Cut", {});
    assert(ctx.compositor().programSourceId() == id3);
    assert(ctx.compositor().previewSourceId() == id2);
    std::cout << "[PASS] Cut command swapped Program (Camera 3) and Preview (Camera 2).\n";

    // Test SetFader (T-Bar)
    ctx.executeVmixFunction("SetFader", {{"Value", "128"}}); // 128 / 255 ~= 0.502
    assert(ctx.compositor().tbarPosition() > 0.49f && ctx.compositor().tbarPosition() < 0.52f);
    std::cout << "[PASS] SetFader positioned T-Bar to midpoint (" << ctx.compositor().tbarPosition() << ").\n";

    // Test Overlay 1 Trigger
    ctx.executeVmixFunction("OverlayInput1", {{"Input", std::to_string(idTitle)}});
    assert(ctx.compositor().overlay(1).sourceId == idTitle);
    assert(ctx.compositor().isOverlayActive(1));
    std::cout << "[PASS] OverlayInput1 toggled Lower Third on Overlay 1.\n";

    // Test Audio Mute & Volume
    ctx.executeVmixFunction("SetVolume", {{"Input", std::to_string(id1)}, {"Value", "75"}});
    assert(ctx.audioMixer().getVolume(id1) > 0.74f && ctx.audioMixer().getVolume(id1) < 0.76f);
    std::cout << "[PASS] SetVolume adjusted Camera 1 volume to 75%.\n";

    ctx.executeVmixFunction("Audio", {{"Input", std::to_string(id1)}});
    assert(ctx.audioMixer().isMuted(id1));
    ctx.executeVmixFunction("AudioOn", {{"Input", std::to_string(id1)}});
    assert(!ctx.audioMixer().isMuted(id1));
    std::cout << "[PASS] Audio mute and unmute commands verified.\n";

    // Test Streaming & FTB Triggers
    ctx.executeVmixFunction("StartStreaming", {});
    assert(ctx.isStreaming());
    ctx.executeVmixFunction("FadeToBlack", {});
    assert(ctx.isFTBActive());
    std::cout << "[PASS] StartStreaming and FadeToBlack commands executed.\n";

    // 3. vMix XML Status Engine (Bitfocus Companion Polling)
    std::cout << "\n--- Testing vMix XML Polling Document Generation ---\n";
    std::string xml = ctx.generateVmixXml();
    assert(!xml.empty());
    assert(xml.find("<version>29.0.0.1</version>") != std::string::npos);
    assert(xml.find("<edition>Pro</edition>") != std::string::npos);
    assert(xml.find("<streaming>True</streaming>") != std::string::npos);
    assert(xml.find("<fadeToBlack>True</fadeToBlack>") != std::string::npos);
    assert(xml.find("<active>" + std::to_string(id3) + "</active>") != std::string::npos);
    assert(xml.find("<preview>" + std::to_string(id2) + "</preview>") != std::string::npos);
    assert(xml.find("Camera 1 - Host") != std::string::npos);
    assert(xml.find("<master volume=") != std::string::npos);
    std::cout << "[PASS] vMix 29 compliant XML status tree generated successfully (" << xml.size() << " bytes).\n";

    // 4. USB MIDI Control Surface Engine (Akai APC Mini Preset)
    std::cout << "\n--- Testing USB MIDI Controller Engine (Akai APC Mini) ---\n";
    sm::MidiController midi(ctx);
    midi.loadAkaiApcMiniPreset();

    // Struct to capture outgoing MIDI LED feedback packets
    struct MidiPacket {
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
    };
    std::vector<MidiPacket> capturedMidi;
    midi.setMidiSendCallback([&](uint8_t status, uint8_t data1, uint8_t data2) {
        capturedMidi.push_back({status, data1, data2});
    });

    // Reset switcher state: Program = 1, Preview = 2
    ctx.setProgramSource(1);
    ctx.setPreviewSource(2);

    // Simulate incoming MIDI Note 0 (Program Input 1) button press: Channel 1, Note 0, Velocity 127
    capturedMidi.clear();
    midi.processMidiMessage(0x90, 0, 127);
    assert(ctx.compositor().programSourceId() == 1);

    // Verify LED tally feedback was sent
    // Note 0 (Program 1) should be Red (value 3)
    bool foundPgmRed = false;
    for (const auto& p : capturedMidi) {
        if (p.data1 == 0 && p.data2 == static_cast<uint8_t>(sm::MidiLedColor::Red)) {
            foundPgmRed = true;
        }
    }
    assert(foundPgmRed);
    std::cout << "[PASS] APC Mini Program button 1 pressed -> Program Source 1 -> LED tally RED emitted.\n";

    // Simulate incoming MIDI Note 9 (Preview Input 2) button press: Channel 1, Note 9, Velocity 127
    capturedMidi.clear();
    midi.processMidiMessage(0x90, 9, 127);
    assert(ctx.compositor().previewSourceId() == 2);

    // Verify LED tally: Note 9 (Preview 2) should be Green (value 1)
    bool foundPvwGreen = false;
    for (const auto& p : capturedMidi) {
        if (p.data1 == 9 && p.data2 == static_cast<uint8_t>(sm::MidiLedColor::Green)) {
            foundPvwGreen = true;
        }
    }
    assert(foundPvwGreen);
    std::cout << "[PASS] APC Mini Preview button 2 pressed -> Preview Source 2 -> LED tally GREEN emitted.\n";

    // Simulate Cut button: Note 64
    capturedMidi.clear();
    midi.processMidiMessage(0x90, 64, 127);
    assert(ctx.compositor().programSourceId() == 2);
    assert(ctx.compositor().previewSourceId() == 1);
    std::cout << "[PASS] APC Mini Cut button triggered cut transition.\n";

    // Test Master Volume Slider: CC 56, Value 127 (100%)
    midi.processMidiMessage(0xB0, 56, 127);
    assert(ctx.audioMixer().masterVolume() > 0.99f);
    std::cout << "[PASS] APC Mini Master fader CC 56 set master volume to 100%.\n";

    // 5. Behringer X-Touch Mini Preset (Hardware T-Bar Fader)
    std::cout << "\n--- Testing Behringer X-Touch Mini Preset ---\n";
    midi.loadBehringerXTouchMiniPreset();

    // CC 9 is Master Slider mapped to T-Bar
    midi.processMidiMessage(0xB0, 9, 64); // 64 / 127 ~= 0.5039
    assert(ctx.compositor().tbarPosition() > 0.49f && ctx.compositor().tbarPosition() < 0.52f);
    std::cout << "[PASS] X-Touch Mini fader CC 9 drove physical T-Bar position ("
              << ctx.compositor().tbarPosition() << ").\n";

    std::cout << "\n====================================================\n";
    std::cout << "ALL CONTROL SURFACE & STREAM DECK API TESTS PASSED!\n";
    std::cout << "====================================================\n";

    return 0;
}
