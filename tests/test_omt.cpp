/**
 * StreamMix Integration Test — Open Media Transport (OMT) Pipeline
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Open Media Transport (OMT) Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Master Context
    sm::Context ctx;
    bool initOk = ctx.initialize();
    assert(initOk);
    std::cout << "[PASS] Context initialized.\n";

    // 2. Instantiate OMT Source (Receiver)
    auto omtSource = std::make_shared<sm::OMTSource>("OMT Stage Cam");
    assert(omtSource != nullptr);
    assert(omtSource->type() == sm::SourceType::OMT);

    sm::SourceSettings rxSettings;
    rxSettings.url = "omt://127.0.0.1:9050/StageCam";
    bool startOk = omtSource->start(rxSettings);
    assert(startOk);
    assert(omtSource->isRunning());
    std::cout << "[PASS] OMT Receiver started and listening on 127.0.0.1:9050.\n";

    // Register into Context
    ctx.addSource(omtSource);
    ctx.setProgramSource(omtSource->id());
    assert(ctx.compositor().programSourceId() == omtSource->id());
    std::cout << "[PASS] OMT Source added to switcher and set to Program.\n";

    // 3. Simulate Ingesting OMT Video Packet (1920x1080 RGBA)
    sm::OMTHeader vHdr;
    vHdr.magic = sm::OMT_MAGIC;
    vHdr.packetType = 1; // Video
    vHdr.seqNumber = 1001;
    vHdr.ptsUs = 12345678;

    sm::OMTVideoMeta vMeta;
    vMeta.width = 1920;
    vMeta.height = 1080;
    vMeta.fpsNum = 60;
    vMeta.fpsDen = 1;
    vMeta.format = 0; // RGBA8

    // Build payload: Meta + sample pixel bytes
    size_t pixelSize = 1920 * 1080 * 4;
    std::vector<uint8_t> vPayload(sizeof(sm::OMTVideoMeta) + pixelSize, 0);
    std::memcpy(vPayload.data(), &vMeta, sizeof(sm::OMTVideoMeta));
    // Fill first few pixels with cyan (R=0, G=255, B=255, A=255)
    uint8_t* pix = vPayload.data() + sizeof(sm::OMTVideoMeta);
    for (size_t i = 0; i < 1000 * 4; i += 4) {
        pix[i + 0] = 0;
        pix[i + 1] = 255;
        pix[i + 2] = 255;
        pix[i + 3] = 255;
    }

    omtSource->injectPacket(vHdr, vPayload.data(), vPayload.size());
    std::cout << "[PASS] Injected 1920x1080 60fps OMT video packet into receiver.\n";

    // 4. Verify Compositor Rendered OMT Video Frame
    auto progFrame = ctx.compositor().renderProgram();
    assert(progFrame != nullptr);
    assert(progFrame->width == 1920 && progFrame->height == 1080);
    assert(progFrame->data[1] == 255); // Green channel present
    std::cout << "[PASS] Compositor rendered OMT frame on Program output.\n";

    // 5. Ingest OMT 32-bit Float Audio Packet (1024 frames @ 48kHz)
    sm::OMTHeader aHdr;
    aHdr.magic = sm::OMT_MAGIC;
    aHdr.packetType = 2; // Audio
    aHdr.seqNumber = 2001;
    aHdr.ptsUs = 12345678;

    sm::OMTAudioMeta aMeta;
    aMeta.sampleRate = 48000;
    aMeta.channels = 2;
    aMeta.frameCount = 1024;
    aMeta.format = 0; // Float32

    std::vector<float> samples(1024 * 2, 0.75f);
    std::vector<uint8_t> aPayload(sizeof(sm::OMTAudioMeta) + samples.size() * sizeof(float));
    std::memcpy(aPayload.data(), &aMeta, sizeof(sm::OMTAudioMeta));
    std::memcpy(aPayload.data() + sizeof(sm::OMTAudioMeta), samples.data(), samples.size() * sizeof(float));

    omtSource->injectPacket(aHdr, aPayload.data(), aPayload.size());
    std::cout << "[PASS] Injected 48kHz float OMT audio packet into receiver.\n";

    // 6. Verify Audio Mixer Processed OMT Audio
    auto masterAudio = ctx.audioMixer().renderMaster(1024);
    assert(masterAudio != nullptr);
    assert(!masterAudio->samples.empty());
    assert(std::abs(masterAudio->samples[0] - 0.75f) < 0.05f);
    std::cout << "[PASS] AudioMixer processed OMT 32-bit float audio cleanly at unity gain.\n";

    // 7. Test OMT Output Transmitter
    sm::OMTOutput omtOut("OMT Program TX");
    sm::OutputSettings outSettings;
    outSettings.width = 1920;
    outSettings.height = 1080;
    outSettings.fps = 60.0;
    bool outOk = omtOut.start(outSettings);
    assert(outOk);
    assert(omtOut.isStreaming());

    omtOut.pushVideo(progFrame);
    omtOut.pushAudio(masterAudio);
    assert(omtOut.stats().totalFramesSent == 1);
    std::cout << "[PASS] OMT Output transmitter active and packetizing Program feed.\n";

    // Cleanup
    omtOut.stop();
    omtSource->stop();
    ctx.shutdown();
    std::cout << "[PASS] OMT pipeline stopped cleanly.\n";

    std::cout << "====================================================\n";
    std::cout << "ALL OPEN MEDIA TRANSPORT (OMT) TESTS PASSED!\n";
    std::cout << "====================================================\n";

    return 0;
}
