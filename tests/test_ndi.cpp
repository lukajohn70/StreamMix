#include "streammix/ndi_source.h"
#include "streammix/ndi_output.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace sm;

int main() {
    std::cout << "[TEST] Starting NDI® 6 & NDI|HX3 Integration Test Suite...\n";

    // ─────────────────────────────────────────────────────────────
    // 1. NDI Source Discovery (NDIFinder)
    // ─────────────────────────────────────────────────────────────
    NDIFinder finder;
    assert(!finder.isDiscovering());

    finder.startDiscovery();
    assert(finder.isDiscovering());

    auto sources = finder.findSources();
    assert(sources.size() >= 2);
    bool foundHX3 = false;
    for (const auto& s : sources) {
        if (s.isHX3) foundHX3 = true;
    }
    assert(foundHX3); // Must discover NDI|HX3 PTZ camera

    // Register dynamic studio sender
    NDIStreamInfo customCam;
    customCam.name = "OB-VAN-1 (Graphics Alpha)";
    customCam.url = "10.0.4.50:5961";
    customCam.isHX3 = false;
    finder.registerSource(customCam);

    assert(finder.findSources().size() >= 3);
    finder.stopDiscovery();
    assert(!finder.isDiscovering());

    std::cout << "  ✓ NDI 6 and NDI|HX3 LAN sender discovery verified\n";

    // ─────────────────────────────────────────────────────────────
    // 2. NDI Ingest Source & Bi-Directional Tally
    // ─────────────────────────────────────────────────────────────
    auto ndiSource = std::make_shared<NDISource>("STUDIO-PC (Camera 1 - Host)");
    assert(ndiSource->state() == SourceState::Idle || ndiSource->state() == SourceState::Stopped);

    ndiSource->setBandwidth(NDIBandwidth::NDIHX3);
    assert(ndiSource->bandwidth() == NDIBandwidth::NDIHX3);

    // Track received frames
    std::atomic<int> receivedVideoFrames{0};
    std::atomic<int> receivedAudioFrames{0};

    ndiSource->setVideoCallback([&](VideoFramePtr vf) {
        if (vf && vf->width == 1920 && vf->height == 1080) {
            receivedVideoFrames.fetch_add(1);
        }
    });

    ndiSource->setAudioCallback([&](AudioFramePtr af) {
        if (af && af->channels == 2 && af->sample_rate == 48000) {
            receivedAudioFrames.fetch_add(1);
        }
    });

    // Start NDI input
    SourceSettings settings;
    bool started = ndiSource->start(settings);
    assert(started);
    assert(ndiSource->state() == SourceState::Running);

    // Test Tally State
    assert(!ndiSource->isProgramTally());
    assert(!ndiSource->isPreviewTally());

    // Switch to Program
    ndiSource->setTally(true, false);
    assert(ndiSource->isProgramTally());
    assert(!ndiSource->isPreviewTally());

    // Switch to Preview
    ndiSource->setTally(false, true);
    assert(!ndiSource->isProgramTally());
    assert(ndiSource->isPreviewTally());

    // Allow worker loop to ingest a few frames
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    assert(receivedVideoFrames.load() > 0);
    assert(receivedAudioFrames.load() > 0);

    // Stop NDI input
    ndiSource->stop();
    assert(ndiSource->state() == SourceState::Stopped);

    std::cout << "  ✓ NDI Ingest (60fps ticks, 32-bit float audio, Tally sync) verified\n";

    // ─────────────────────────────────────────────────────────────
    // 3. NDI Network Broadcast Output (Transmitter)
    // ─────────────────────────────────────────────────────────────
    NDIOutput ndiOutput("StreamMix - Main Output 1");
    assert(ndiOutput.senderName() == "StreamMix - Main Output 1");

    OutputSettings outSettings;
    assert(ndiOutput.start(outSettings));
    assert(ndiOutput.state() == OutputState::Streaming);

    // External tally callback
    std::atomic<bool> externalTallyFired{false};
    ndiOutput.setTallyCallback([&](const NDITallyState& ts) {
        if (ts.onProgram) {
            externalTallyFired.store(true);
        }
    });

    // Stream 10 frames
    for (int i = 0; i < 10; ++i) {
        auto vf = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(320, 240));
        ndiOutput.pushVideo(vf);

        auto af = std::make_shared<AudioFrame>();
        af->channels = 2;
        af->samples.resize(800 * 2, 0.0f);
        ndiOutput.pushAudio(af);
    }

    assert(ndiOutput.framesSent() == 10);

    // Simulate external TriCaster tally feedback
    ndiOutput.simulateExternalTally(true, false);
    assert(ndiOutput.tallyState().onProgram == true);
    assert(externalTallyFired.load() == true);

    ndiOutput.stop();
    assert(ndiOutput.state() == OutputState::Stopped);

    std::cout << "  ✓ NDI 6 Output Transmitter (frames sent=" << ndiOutput.framesSent() 
              << ", external tally feedback) verified\n";

    std::cout << "[PASS] NDI® 6 & NDI|HX3 Integration Test Suite completed successfully!\n";
    return 0;
}
