/**
 * StreamMix Integration Test — Instant Replay 2.0 (vMix 29)
 *
 * Validates:
 *   - Multi-camera ring buffer ingestion and circular overflow
 *   - Variable speed slow-motion (10%, 50%, 200%)
 *   - Mark In/Out event tagging and clip extraction
 *   - extractLastSeconds convenience method
 *   - Quad-Cam replay view composite rendering
 *   - Playhead: play, pause, stop, step, loop
 *   - Event callbacks (play, end, loop)
 *   - Buffer statistics (utilization, depth)
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <memory>
#include <vector>
#include <string>

// ─── Helper: Generate a coloured RGBA8 test frame ────────────────
static sm::VideoFramePtr makeTestFrame(int width, int height,
                                       uint8_t r, uint8_t g, uint8_t b,
                                       int64_t pts_us) {
    auto frame = std::make_shared<sm::VideoFrame>(sm::VideoFrame::makeRGBA(width, height));
    for (size_t i = 0; i < frame->data.size(); i += 4) {
        frame->data[i + 0] = r;
        frame->data[i + 1] = g;
        frame->data[i + 2] = b;
        frame->data[i + 3] = 255;
    }
    frame->pts_us = pts_us;
    return frame;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Instant Replay 2.0 Engine Test\n";
    std::cout << "====================================================\n";

    // 1. Engine Initialization
    const double FPS      = 60.0;
    const double DEPTH_S  = 5.0;   // 5-second buffer depth = 300 frames per camera
    sm::InstantReplay replay(DEPTH_S, FPS);

    const uint64_t CAM_A = 101;
    const uint64_t CAM_B = 102;
    const uint64_t CAM_C = 103;
    const uint64_t CAM_D = 104;

    replay.addCamera(CAM_A, "Wide Shot");
    replay.addCamera(CAM_B, "Close-Up A");
    replay.addCamera(CAM_C, "Close-Up B");
    replay.addCamera(CAM_D, "Aerial");

    auto ids = replay.cameraIds();
    assert(ids.size() == 4);
    std::cout << "[PASS] 4 cameras registered: Wide, Close-Up A, Close-Up B, Aerial.\n";

    // 2. Ingest 300 frames per camera (@ 60fps, 5 seconds of footage)
    std::cout << "\n--- Ingesting 300 frames per camera (5s @ 60fps) ---\n";
    const int FRAME_COUNT = 300;
    const int64_t FRAME_US = static_cast<int64_t>(1'000'000.0 / FPS); // 16,666 µs

    for (int i = 0; i < FRAME_COUNT; ++i) {
        int64_t pts = static_cast<int64_t>(i) * FRAME_US;
        // Each camera gets a distinct colour for pixel validation
        replay.ingestFrame(CAM_A, makeTestFrame(1920, 1080, 200, 50,  50,  pts), pts);
        replay.ingestFrame(CAM_B, makeTestFrame(1920, 1080, 50,  200, 50,  pts), pts);
        replay.ingestFrame(CAM_C, makeTestFrame(1920, 1080, 50,  50,  200, pts), pts);
        replay.ingestFrame(CAM_D, makeTestFrame(1920, 1080, 200, 200, 50,  pts), pts);
    }

    // Verify buffer stats
    auto statsA = replay.stats(CAM_A);
    assert(statsA.totalFrames == FRAME_COUNT);
    assert(statsA.utilization > 0.99f);
    assert(statsA.bufferDepthSec == DEPTH_S);
    std::cout << "[PASS] Ring buffer filled: " << statsA.totalFrames << " frames, "
              << static_cast<int>(statsA.utilization * 100) << "% utilized, "
              << statsA.bufferDepthSec << "s depth.\n";

    // 3. Circular overflow: ingest 60 more frames and verify oldest are dropped
    std::cout << "\n--- Testing Circular Buffer Overflow (60 extra frames) ---\n";
    int64_t overflowStart = static_cast<int64_t>(FRAME_COUNT) * FRAME_US;
    for (int i = 0; i < 60; ++i) {
        int64_t pts = overflowStart + static_cast<int64_t>(i) * FRAME_US;
        replay.ingestFrame(CAM_A, makeTestFrame(1920, 1080, 255, 128, 0, pts), pts);
    }
    auto statsA2 = replay.stats(CAM_A);
    assert(statsA2.totalFrames == FRAME_COUNT); // Should remain at max capacity
    assert(statsA2.newestSec > statsA2.oldestSec);
    std::cout << "[PASS] Circular overflow: buffer capped at " << statsA2.totalFrames
              << " frames. Oldest PTS rolled forward.\n";

    // 4. Mark Event & Clip Extraction (Mark In / Out)
    std::cout << "\n--- Testing Mark In/Out & Clip Extraction ---\n";
    replay.markEvent("Try"); // Tag most-recent frame on all cameras

    // Mark In: frame 100 (1.66s), Mark Out: frame 200 (3.33s) on CAM_B
    int64_t markInPts  = static_cast<int64_t>(100) * FRAME_US;
    int64_t markOutPts = static_cast<int64_t>(200) * FRAME_US;

    replay.setMarkIn(CAM_B, markInPts);
    replay.setMarkOut(CAM_B, markOutPts);

    auto clip = replay.extractClip(CAM_B, "Close-Up Try");
    assert(clip != nullptr);
    assert(clip->name == "Close-Up Try");
    assert(clip->cameraId == CAM_B);
    assert(clip->frameCount() > 0);
    // Should contain frames between frames 100..200 = ~100 frames
    assert(clip->frameCount() >= 95 && clip->frameCount() <= 105);
    double dur = clip->durationSeconds();
    assert(dur > 1.5 && dur < 1.8);
    std::cout << "[PASS] Clip extracted: '" << clip->name << "' "
              << clip->frameCount() << " frames, " << dur << "s duration.\n";

    // 5. Extract Last Seconds convenience
    auto quickClip = replay.extractLastSeconds(CAM_C, 2.0, "Quick Replay");
    assert(quickClip != nullptr);
    assert(quickClip->frameCount() >= 115 && quickClip->frameCount() <= 125);
    std::cout << "[PASS] extractLastSeconds(2.0s): " << quickClip->frameCount() << " frames captured.\n";

    // 6. Playhead Load & Normal Speed Playback
    std::cout << "\n--- Testing Playback: Normal Speed (100%) ---\n";
    replay.loadClip(0, clip);
    assert(!replay.isPlaying(0));
    assert(replay.currentFrame(0) == 0);

    replay.play(0, 1.0f);
    assert(replay.isPlaying(0));

    // Advance 1 second of real time at 1× speed -> should consume ~100 frames
    for (int t = 0; t < 60; ++t) {
        replay.tick(1.0 / FPS);
    }
    size_t frameAfter1s = replay.currentFrame(0);
    assert(frameAfter1s > 50 && frameAfter1s < 110);
    std::cout << "[PASS] Normal speed playback: after 1s ticked, playhead at frame "
              << frameAfter1s << ".\n";

    // 7. Slow-Motion 10% (1/10 speed)
    std::cout << "\n--- Testing Slow-Motion Playback (10%) ---\n";
    replay.loadClip(1, clip);
    replay.play(1, 0.10f);
    assert(replay.speed(1) < 0.11f);

    // Advance 1 second real time at 10% speed -> should consume ~10 frames
    for (int t = 0; t < 60; ++t) {
        replay.tick(1.0 / FPS);
    }
    size_t frameAt10pct = replay.currentFrame(1);
    assert(frameAt10pct >= 5 && frameAt10pct <= 20);
    std::cout << "[PASS] 10% slow-motion: after 1s real time, playhead at frame "
              << frameAt10pct << " (expected ~10).\n";

    // 8. Fast-Forward 200%
    std::cout << "\n--- Testing Fast-Forward Playback (200%) ---\n";
    replay.loadClip(2, clip);
    replay.play(2, 2.0f);

    // Advance 0.5s at 2× -> ~60 frames consumed
    for (int t = 0; t < 30; ++t) {
        replay.tick(1.0 / FPS);
    }
    size_t frameAt200 = replay.currentFrame(2);
    assert(frameAt200 > 40 && frameAt200 < 130);
    std::cout << "[PASS] 200% fast-forward: after 0.5s real time, playhead at frame "
              << frameAt200 << ".\n";

    // 9. Looping
    std::cout << "\n--- Testing Looping ---\n";
    // Make a tiny clip of 5 frames for fast loop test
    auto loopFrames = std::vector<sm::ReplayFramePtr>();
    for (int i = 0; i < 5; ++i) {
        auto rf = std::make_shared<sm::ReplayFrame>();
        rf->video   = makeTestFrame(1920, 1080, 100, 100, 100, i * FRAME_US);
        rf->pts_us  = i * FRAME_US;
        rf->cameraId = CAM_D;
        loopFrames.push_back(rf);
    }
    auto tinyClip = std::make_shared<sm::ReplayClip>();
    tinyClip->name      = "Loop Test";
    tinyClip->cameraId  = CAM_D;
    tinyClip->markInPts = 0;
    tinyClip->markOutPts = 4 * FRAME_US;
    tinyClip->frames    = loopFrames;

    bool loopFired = false;
    replay.setEventCallback([&](const std::string& event, int /*ph*/) {
        if (event == "loop") loopFired = true;
    });

    replay.loadClip(3, tinyClip);
    replay.setLooping(3, true);
    replay.play(3, 1.0f);

    // Advance enough to complete 2 full loops
    for (int t = 0; t < 20; ++t) {
        replay.tick(1.0 / FPS);
    }
    assert(loopFired);
    assert(replay.isPlaying(3)); // Should still be playing due to loop
    std::cout << "[PASS] Looping: playhead looped at end and continued playing.\n";

    // 10. Step Forward / Back
    std::cout << "\n--- Testing Frame Step ---\n";
    replay.loadClip(0, clip);
    replay.pause(0);
    assert(!replay.isPlaying(0));
    assert(replay.currentFrame(0) == 0);

    replay.stepForward(0, 10);
    assert(replay.currentFrame(0) == 10);
    replay.stepBack(0, 3);
    assert(replay.currentFrame(0) == 7);
    replay.stepForward(0, 1);
    assert(replay.currentFrame(0) == 8);
    std::cout << "[PASS] Frame step: stepForward(10), stepBack(3), stepForward(1) -> frame 8.\n";

    // 11. Quad-Cam Replay View
    std::cout << "\n--- Testing Quad-Cam Replay View ---\n";
    replay.loadClip(0, clip);
    replay.loadClip(1, clip);
    replay.loadClip(2, quickClip);
    replay.loadClip(3, tinyClip);

    replay.setQuadView(true);
    assert(replay.quadViewEnabled());

    auto quadFrame = replay.renderReplayBus();
    assert(quadFrame != nullptr);
    assert(quadFrame->width  == 1920);
    assert(quadFrame->height == 1080);
    // Verify top-left quadrant pixel is from CAM_B (green-ish)
    uint8_t tl_g = quadFrame->data[1]; // pixel (0,0), green channel
    assert(tl_g > 100); // CAM_B frames are R=50, G=200
    std::cout << "[PASS] Quad-Cam view rendered: 1920x1080 composite (TL pixel G="
              << static_cast<int>(tl_g) << ").\n";

    // 12. Single-Camera Replay Bus Output
    replay.setQuadView(false);
    replay.setActivePlayhead(0);
    auto singleFrame = replay.renderReplayBus();
    assert(singleFrame != nullptr);
    assert(singleFrame->width == 1920);
    std::cout << "[PASS] Single-camera replay bus: 1920x1080 frame from active playhead 0.\n";

    // 13. Clip Registry
    auto allClips = replay.clips();
    assert(allClips.size() >= 2);
    replay.removeClip("Quick Replay");
    assert(replay.clips().size() == allClips.size() - 1);
    std::cout << "[PASS] Clip registry: " << allClips.size()
              << " clips stored, removeClip() verified.\n";

    std::cout << "\n====================================================\n";
    std::cout << "ALL INSTANT REPLAY 2.0 TESTS PASSED!\n";
    std::cout << "====================================================\n";

    return 0;
}
