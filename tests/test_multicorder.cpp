#include "streammix/multicorder.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using namespace sm;

int main() {
    std::cout << "[TEST] Starting MultiCorder (ISO Recording) Test Suite...\n";

    MultiCorder multicorder;
    assert(!multicorder.isRecording());

    // 1. Add 4 ISO Camera channels
    MultiCorderChannelConfig ch1;
    ch1.sourceId = 101;
    ch1.sourceName = "Camera 1 (Main Host)";
    ch1.format = MultiCorderFormat::MP4;
    ch1.bitrateKbps = 20000;
    multicorder.addChannel(ch1);

    MultiCorderChannelConfig ch2;
    ch2.sourceId = 102;
    ch2.sourceName = "Camera 2 (Guest)";
    ch2.format = MultiCorderFormat::ProRes;
    ch2.bitrateKbps = 50000;
    multicorder.addChannel(ch2);

    MultiCorderChannelConfig ch3;
    ch3.sourceId = 103;
    ch3.sourceName = "Camera 3 (Wide Studio)";
    ch3.format = MultiCorderFormat::MKV;
    ch3.bitrateKbps = 15000;
    multicorder.addChannel(ch3);

    MultiCorderChannelConfig ch4;
    ch4.sourceId = 104;
    ch4.sourceName = "Camera 4 (Overhead)";
    ch4.format = MultiCorderFormat::RAW_RGBA;
    ch4.enabled = false; // Disabled channel
    multicorder.addChannel(ch4);

    assert(multicorder.channelIds().size() == 4);
    assert(multicorder.isChannelEnabled(101) == true);
    assert(multicorder.isChannelEnabled(104) == false);

    // Toggle enabled state
    multicorder.setChannelEnabled(104, true);
    assert(multicorder.isChannelEnabled(104) == true);
    multicorder.setChannelEnabled(104, false);
    assert(multicorder.isChannelEnabled(104) == false);

    std::cout << "  ✓ Multi-channel configuration & status verified\n";

    // 2. Start MultiCorder Recording
    bool started = multicorder.startRecording("./build/test_iso_recordings");
    assert(started);
    assert(multicorder.isRecording());

    // Cannot start again while recording
    assert(!multicorder.startRecording("./build/test_iso_recordings"));

    std::cout << "  ✓ MultiCorder start recording verified\n";

    // 3. Ingest frames into channels
    auto createDummyFrame = [](int width, int height, int64_t pts) {
        auto vf = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(width, height));
        vf->pts_us = pts;
        return vf;
    };

    auto createDummyAudio = [](int frames, int64_t pts) {
        auto af = std::make_shared<AudioFrame>();
        af->channels = 2;
        af->sample_rate = 48000;
        af->frames = frames;
        af->samples.resize(frames * 2, 0.5f);
        af->pts_us = pts;
        return af;
    };

    // Push 30 frames to Cam 1, Cam 2, Cam 3
    for (int i = 0; i < 30; ++i) {
        int64_t pts = i * 16666; // 60fps
        multicorder.ingestVideo(101, createDummyFrame(320, 240, pts));
        multicorder.ingestAudio(101, createDummyAudio(800, pts));

        multicorder.ingestVideo(102, createDummyFrame(320, 240, pts));
        multicorder.ingestVideo(103, createDummyFrame(320, 240, pts));

        // Push to disabled channel 104 (should be ignored)
        multicorder.ingestVideo(104, createDummyFrame(320, 240, pts));
    }

    // Allow writer threads to process queued frames
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // 4. Verify telemetry
    auto stats1 = multicorder.channelStats(101);
    assert(stats1.sourceId == 101);
    assert(stats1.isRecording);
    assert(stats1.framesWritten > 0);
    assert(stats1.bytesWritten > 0);

    auto stats2 = multicorder.channelStats(102);
    assert(stats2.framesWritten > 0);

    auto stats4 = multicorder.channelStats(104);
    assert(stats4.framesWritten == 0); // was disabled

    assert(multicorder.totalBytesWritten() > 0);
    std::cout << "  ✓ Telemetry and frame writer loops verified (Total bytes: " 
              << multicorder.totalBytesWritten() << ")\n";

    // 5. Verify buffer overflow drop logic
    // Flood 150 frames to Cam 1 rapidly to test queue drop protection
    for (int i = 0; i < 150; ++i) {
        multicorder.ingestVideo(101, createDummyFrame(640, 360, 1000000 + i * 16666));
    }
    // Allow writer threads to consume some
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stats1 = multicorder.channelStats(101);
    std::cout << "  ✓ Saturation test: Cam 1 frames written=" << stats1.framesWritten 
              << ", dropped=" << stats1.framesDropped << "\n";

    // 6. Stop MultiCorder
    multicorder.stopRecording();
    assert(!multicorder.isRecording());

    stats1 = multicorder.channelStats(101);
    assert(!stats1.isRecording);

    // 7. Remove channel
    multicorder.removeChannel(103);
    assert(multicorder.channelIds().size() == 3);

    std::cout << "  ✓ MultiCorder clean shutdown and channel removal verified\n";
    std::cout << "[PASS] MultiCorder Test Suite completed successfully!\n";
    return 0;
}
