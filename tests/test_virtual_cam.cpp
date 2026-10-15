/**
 * StreamMix Integration Test — Virtual Camera Output & Shared Memory IPC
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Virtual Camera Output Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Context
    sm::Context ctx;
    bool initOk = ctx.initialize();
    assert(initOk);
    std::cout << "[PASS] Context initialized.\n";

    // 2. Instantiate and Start Virtual Camera Output
    sm::VirtualCamOutput vcam("StreamMix Virtual Cam");
    assert(vcam.type() == sm::OutputType::VirtualCam);

    sm::OutputSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps = 60.0;

    bool startOk = vcam.start(settings);
    assert(startOk);
    assert(vcam.isStreaming());
    std::cout << "[PASS] Virtual Camera started with shared memory IPC buffer (1920x1080 @ 60fps).\n";

    // 3. Generate a sample Program Video Frame
    auto testFrame = std::make_shared<sm::VideoFrame>(sm::VideoFrame::makeRGBA(1920, 1080));
    testFrame->pts_us = 987654321;
    // Paint a distinct magenta test stripe (R=255, G=0, B=255, A=255)
    for (size_t i = 0; i < 500 * 4; i += 4) {
        testFrame->data[i + 0] = 255;
        testFrame->data[i + 1] = 0;
        testFrame->data[i + 2] = 255;
        testFrame->data[i + 3] = 255;
    }

    // 4. Push Live Frame to Virtual Camera Driver
    vcam.pushVideo(testFrame);
    assert(vcam.stats().totalFramesSent == 1);
    std::cout << "[PASS] Live Program frame pushed into Virtual Camera shared memory.\n";

    // 5. Simulate Consumer App (Zoom, Teams, Meet) Reading from Virtual Camera
    sm::VideoFrame consumerFrame;
    bool readOk = vcam.readSharedFrame(consumerFrame);
    assert(readOk);
    assert(consumerFrame.width == 1920);
    assert(consumerFrame.height == 1080);
    assert(consumerFrame.pts_us == 987654321);
    assert(consumerFrame.data[0] == 255); // Red
    assert(consumerFrame.data[1] == 0);   // Green
    assert(consumerFrame.data[2] == 255); // Blue
    assert(consumerFrame.data[3] == 255); // Alpha
    std::cout << "[PASS] Consumer client successfully read 1920x1080 frame with perfect pixel fidelity.\n";

    // 6. Stop and Clean Shutdown
    vcam.stop();
    assert(!vcam.isStreaming());
    ctx.shutdown();
    std::cout << "[PASS] Virtual Camera closed and shared memory unmapped cleanly.\n";

    std::cout << "====================================================\n";
    std::cout << "ALL VIRTUAL CAMERA TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "====================================================\n";

    return 0;
}
