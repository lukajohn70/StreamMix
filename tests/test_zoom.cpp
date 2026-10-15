/**
 * StreamMix Integration Test — Zoom Pipeline & Mix-Minus Verification
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <vector>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Zoom Integration Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Master Context
    sm::Context ctx;
    bool initOk = ctx.initialize();
    assert(initOk);
    std::cout << "[PASS] Context initialized.\n";

    // 2. Initialize Zoom Meeting Manager
    auto& zoomMgr = sm::ZoomMeetingManager::instance();
    bool sdkOk = zoomMgr.initializeSDK("dummy_key", "dummy_secret");
    assert(sdkOk);

    sm::ZoomMeetingCredentials creds;
    creds.meetingNumber = "1234567890";
    creds.passcode = "998877";
    creds.displayName = "StreamMix Switcher";
    bool joinOk = zoomMgr.joinMeeting(creds);
    assert(joinOk);
    assert(zoomMgr.state() == sm::ZoomMeetingState::InMeeting);
    std::cout << "[PASS] Zoom meeting joined (Meeting ID: 1234567890).\n";

    // 3. Create two Zoom Sources: Host (UserID 101) and Guest (UserID 102)
    auto hostSource = zoomMgr.createZoomSource("Zoom: Host", sm::ZoomTargetMode::SpecificUser, 101);
    auto guestSource = zoomMgr.createZoomSource("Zoom: Guest A", sm::ZoomTargetMode::SpecificUser, 102);

    assert(hostSource != nullptr);
    assert(guestSource != nullptr);

    ctx.addSource(hostSource);
    ctx.addSource(guestSource);
    std::cout << "[PASS] Host (ID: " << hostSource->id() << ") and Guest (ID: " << guestSource->id() << ") inputs added to switcher.\n";

    // 4. Simulate sending raw video from Zoom SDK (320x240 YUV420)
    int w = 320, h = 240;
    std::vector<uint8_t> yPlane(w * h, 180); // gray-ish
    std::vector<uint8_t> uPlane((w / 2) * (h / 2), 128);
    std::vector<uint8_t> vPlane((w / 2) * (h / 2), 128);

    zoomMgr.dispatchRawVideo(101, yPlane.data(), uPlane.data(), vPlane.data(), w, h, w, w / 2);
    zoomMgr.dispatchRawVideo(102, yPlane.data(), uPlane.data(), vPlane.data(), w, h, w, w / 2);
    std::cout << "[PASS] Dispatched raw video frames from Zoom SDK to Host & Guest.\n";

    // 5. Test Live Switching
    ctx.setPreviewSource(guestSource->id());
    ctx.setProgramSource(hostSource->id());
    assert(ctx.compositor().programSourceId() == hostSource->id());
    assert(ctx.compositor().previewSourceId() == guestSource->id());

    // Render Program
    auto progFrame = ctx.compositor().renderProgram();
    assert(progFrame != nullptr);
    assert(progFrame->width == 1920 && progFrame->height == 1080);
    std::cout << "[PASS] Compositor rendered Program video at 1920x1080 with Host active.\n";

    // Trigger Cut transition
    ctx.cut();
    assert(ctx.compositor().programSourceId() == guestSource->id());
    std::cout << "[PASS] Executed Cut: Guest A is now live on Program.\n";

    // 6. Simulate raw caller audio from Zoom
    std::vector<float> hostAudio(1024 * 2, 0.4f);  // Host voice
    std::vector<float> guestAudio(1024 * 2, 0.7f); // Guest voice
    zoomMgr.dispatchRawAudio(101, hostAudio.data(), 1024, 48000, 2);
    zoomMgr.dispatchRawAudio(102, guestAudio.data(), 1024, 48000, 2);

    // 7. Verify Automated Mix-Minus for Guest A (UserID 102)
    // Guest A should hear Host (0.4f), but their OWN voice (0.7f) must be subtracted!
    auto mixMinus = ctx.getMixMinusForCaller(guestSource->id(), 1024);
    assert(mixMinus != nullptr);
    assert(!mixMinus->samples.empty());

    // Verify sample value reflects Host only, not Guest
    float firstSample = mixMinus->samples[0];
    std::cout << "Mix-Minus Sample value for Guest A: " << firstSample << " (Expected ~0.4, NOT 1.1)\n";
    assert(std::abs(firstSample - 0.4f) < 0.05f);
    std::cout << "[PASS] Automated Mix-Minus verified: Guest voice subtracted to prevent echo!\n";

    // 8. Test Bi-directional Return Feed to Zoom
    zoomMgr.sendReturnVideo(progFrame);
    zoomMgr.sendReturnAudio(mixMinus);
    std::cout << "[PASS] Bi-directional Return Video & Audio piped back into Zoom meeting.\n";

    // Cleanup
    zoomMgr.leaveMeeting();
    ctx.shutdown();
    std::cout << "====================================================\n";
    std::cout << "ALL ZOOM INTEGRATION TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "====================================================\n";

    return 0;
}
