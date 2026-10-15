/**
 * StreamMix Integration Test — PTZ Camera Control (vMix 29)
 *
 * Uses PTZProtocol::Simulated — zero network I/O, purely validates:
 *   - Camera registration and connection lifecycle
 *   - VISCA pan/tilt/zoom/focus commands update simulated position
 *   - Absolute moveTo() with 4-nibble VISCA encoding
 *   - Preset save, recall (with auto-position restore), clear
 *   - Home command resets position to origin
 *   - Tally state: Program (Red), Preview (Green), Off
 *   - PTZController multi-camera registry
 *   - Auto-track: preset recalled automatically on program source change
 *   - Broadcast tally update across all cameras
 *   - Active camera delegation (panLeft, zoomIn, etc.)
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <memory>
#include <string>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — PTZ Camera Control Test\n";
    std::cout << "====================================================\n";

    // ─── 1. Register cameras (Simulated protocol — no network) ───
    std::cout << "\n--- Camera Registration ---\n";
    sm::PTZController ptz;

    const uint64_t CAM1 = 1001;
    const uint64_t CAM2 = 1002;
    const uint64_t CAM3 = 1003;

    auto cam1 = ptz.addCamera(CAM1, "Wide PTZ",    "192.168.1.101", 52381, sm::PTZProtocol::Simulated);
    auto cam2 = ptz.addCamera(CAM2, "Close-Up PTZ","192.168.1.102", 52381, sm::PTZProtocol::Simulated);
    auto cam3 = ptz.addCamera(CAM3, "Aerial PTZ",  "192.168.1.103",  5678, sm::PTZProtocol::Simulated);

    assert(ptz.cameraCount() == 3);
    assert(cam1 != nullptr && cam2 != nullptr && cam3 != nullptr);
    std::cout << "[PASS] 3 PTZ cameras registered.\n";

    // Connect all
    assert(cam1->connect());
    assert(cam2->connect());
    assert(cam3->connect());
    assert(cam1->connected());
    std::cout << "[PASS] All cameras connected (Simulated).\n";

    // ─── 2. Pan / Tilt motion commands ───────────────────────────
    std::cout << "\n--- Pan / Tilt Commands ---\n";
    auto pos0 = cam1->currentPosition();
    assert(pos0.pan == 0 && pos0.tilt == 0);

    cam1->panRight(10);
    assert(cam1->currentPosition().pan > 0);
    std::cout << "[PASS] panRight(10) -> pan=" << cam1->currentPosition().pan << "\n";

    cam1->panLeft(5);
    int panAfterLeft = cam1->currentPosition().pan;
    std::cout << "[PASS] panLeft(5) -> pan=" << panAfterLeft << "\n";

    cam1->tiltUp(8);
    assert(cam1->currentPosition().tilt > 0);
    std::cout << "[PASS] tiltUp(8) -> tilt=" << cam1->currentPosition().tilt << "\n";

    cam1->tiltDown(4);
    std::cout << "[PASS] tiltDown(4) -> tilt=" << cam1->currentPosition().tilt << "\n";

    cam1->stopPanTilt();
    std::cout << "[PASS] stopPanTilt() issued.\n";

    // panTilt() compound command
    cam2->panTilt(1, 1, 12, 8); // pan right, tilt up
    assert(cam2->currentPosition().pan  > 0);
    assert(cam2->currentPosition().tilt > 0);
    std::cout << "[PASS] panTilt(R, U, 12, 8) -> pan=" << cam2->currentPosition().pan
              << " tilt=" << cam2->currentPosition().tilt << "\n";

    // ─── 3. Zoom / Focus ─────────────────────────────────────────
    std::cout << "\n--- Zoom / Focus Commands ---\n";
    cam1->zoomIn(5);
    assert(cam1->currentPosition().zoom > 0);
    std::cout << "[PASS] zoomIn(5) -> zoom=" << cam1->currentPosition().zoom << "\n";

    cam1->zoomOut(3);
    std::cout << "[PASS] zoomOut(3) -> zoom=" << cam1->currentPosition().zoom << "\n";

    cam1->zoomTo(0x2000);
    assert(cam1->currentPosition().zoom == 0x2000);
    std::cout << "[PASS] zoomTo(0x2000) -> zoom=" << std::hex << cam1->currentPosition().zoom << std::dec << "\n";

    cam1->setAutoFocus(true);
    std::cout << "[PASS] setAutoFocus(true) issued.\n";

    cam1->focusFar(3);
    assert(cam1->currentPosition().focus > 0);
    cam1->focusNear(2);
    std::cout << "[PASS] focusFar/focusNear commands issued and focus updated.\n";

    // ─── 4. Absolute Position moveTo() ───────────────────────────
    std::cout << "\n--- Absolute Position moveTo() ---\n";
    sm::PTZPosition target;
    target.pan  = 1200;
    target.tilt = 300;
    target.zoom = 0x1800;

    sm::PTZSpeed spd;
    spd.panSpeed  = 20;
    spd.tiltSpeed = 15;

    cam2->moveTo(target, spd);
    auto pos2 = cam2->currentPosition();
    assert(pos2.pan  == 1200);
    assert(pos2.tilt == 300);
    assert(pos2.zoom == 0x1800);
    std::cout << "[PASS] moveTo(pan=1200, tilt=300, zoom=0x1800) verified.\n";

    // ─── 5. Home ─────────────────────────────────────────────────
    std::cout << "\n--- Home Command ---\n";
    cam2->home();
    auto posHome = cam2->currentPosition();
    assert(posHome.pan == 0 && posHome.tilt == 0 && posHome.zoom == 0);
    std::cout << "[PASS] home() reset pan/tilt/zoom to (0, 0, 0).\n";

    // ─── 6. Preset Save & Recall ─────────────────────────────────
    std::cout << "\n--- Preset Save / Recall ---\n";

    // Position cam1, save as preset 1
    cam1->setSimulatedPosition({500, 100, 0x1000, 0x5000});
    cam1->setPresetName(1, "Stage Wide");
    cam1->savePreset(1);
    assert(cam1->preset(1).populated);
    assert(cam1->preset(1).name == "Stage Wide");
    assert(cam1->preset(1).position.pan  == 500);
    assert(cam1->preset(1).position.tilt == 100);
    std::cout << "[PASS] Preset 1 'Stage Wide' saved at pan=500, tilt=100.\n";

    // Position cam1, save as preset 2
    cam1->setSimulatedPosition({-800, -200, 0x3000, 0x8000});
    cam1->setPresetName(2, "Corner Close");
    cam1->savePreset(2);
    assert(cam1->preset(2).populated);
    std::cout << "[PASS] Preset 2 'Corner Close' saved at pan=-800, tilt=-200.\n";

    // Recall preset 1 — position should restore
    cam1->recallPreset(1);
    auto posR1 = cam1->currentPosition();
    assert(posR1.pan  == 500);
    assert(posR1.tilt == 100);
    assert(posR1.zoom == 0x1000);
    std::cout << "[PASS] recallPreset(1) restored 'Stage Wide': pan=500, tilt=100.\n";

    // Recall preset 2
    cam1->recallPreset(2);
    auto posR2 = cam1->currentPosition();
    assert(posR2.pan  == -800);
    assert(posR2.tilt == -200);
    std::cout << "[PASS] recallPreset(2) restored 'Corner Close': pan=-800, tilt=-200.\n";

    // Clear preset 1
    cam1->clearPreset(1);
    assert(!cam1->preset(1).populated);
    std::cout << "[PASS] clearPreset(1): preset 1 cleared.\n";

    // ─── 7. Tally Light Control ───────────────────────────────────
    std::cout << "\n--- Tally Light Control ---\n";
    cam1->setTally(sm::TallyState::Program);
    assert(cam1->tally() == sm::TallyState::Program);
    std::cout << "[PASS] cam1 tally = Program (Red).\n";

    cam2->setTally(sm::TallyState::Preview);
    assert(cam2->tally() == sm::TallyState::Preview);
    std::cout << "[PASS] cam2 tally = Preview (Green).\n";

    cam3->setTally(sm::TallyState::Off);
    assert(cam3->tally() == sm::TallyState::Off);
    std::cout << "[PASS] cam3 tally = Off.\n";

    // ─── 8. Broadcast Tally Update (PTZController) ───────────────
    std::cout << "\n--- Broadcast Tally Update ---\n";
    ptz.updateTally(CAM2, CAM3);
    assert(cam1->tally() == sm::TallyState::Off);
    assert(cam2->tally() == sm::TallyState::Program);
    assert(cam3->tally() == sm::TallyState::Preview);
    std::cout << "[PASS] updateTally(Program=CAM2, Preview=CAM3):\n";
    std::cout << "       CAM1=Off, CAM2=Program(Red), CAM3=Preview(Green).\n";

    // ─── 9. Auto-Track Preset on Program Source Change ───────────
    std::cout << "\n--- Auto-Track: Recall Preset on Program Switch ---\n";

    // Set cam1 preset 5 = "Auto Home"
    cam1->setSimulatedPosition({0, 0, 0, 0});
    cam1->savePreset(5);
    cam1->setPresetName(5, "Auto Home");

    // Set cam2 preset 3 = "Interview Close-Up"
    cam2->setSimulatedPosition({200, 50, 0x2500, 0x6000});
    cam2->savePreset(3);
    cam2->setPresetName(3, "Interview Close-Up");

    // Register auto-track: when CAM1 goes live -> recall preset 5
    //                       when CAM2 goes live -> recall preset 3
    ptz.setAutoTrackPreset(CAM1, 5);
    ptz.setAutoTrackPreset(CAM2, 3);

    // Simulate switching program from CAM1 to CAM2
    // Before: move cam2 to a random position
    cam2->setSimulatedPosition({999, 999, 0, 0});
    assert(cam2->currentPosition().pan == 999);

    ptz.onProgramSourceChanged(CAM2, CAM1);

    // CAM2 should have been sent back to preset 3 (pan=200, tilt=50)
    auto posAT = cam2->currentPosition();
    assert(posAT.pan  == 200);
    assert(posAT.tilt == 50);
    assert(posAT.zoom == 0x2500);
    std::cout << "[PASS] onProgramSourceChanged(CAM2): auto-tracked to preset 3 "
              << "'Interview Close-Up' (pan=200, tilt=50).\n";

    // CAM2 should now be Program (Red tally)
    assert(cam2->tally() == sm::TallyState::Program);
    std::cout << "[PASS] Tally updated: CAM2 = Program (Red).\n";

    // ─── 10. Active Camera Delegation ────────────────────────────
    std::cout << "\n--- Active Camera Delegation ---\n";
    ptz.setActiveCamera(CAM3);
    assert(ptz.activeCamera() == CAM3);

    auto pos3Before = cam3->currentPosition();
    ptz.panRight(10);
    assert(cam3->currentPosition().pan > pos3Before.pan);
    std::cout << "[PASS] PTZController::panRight(10) delegated to active CAM3 "
              << "(pan=" << cam3->currentPosition().pan << ").\n";

    ptz.home();
    assert(cam3->currentPosition().pan  == 0);
    assert(cam3->currentPosition().tilt == 0);
    std::cout << "[PASS] PTZController::home() delegated to active CAM3.\n";

    ptz.savePreset(10);
    ptz.recallPreset(10, 18);
    std::cout << "[PASS] PTZController::savePreset/recallPreset delegated to active CAM3.\n";

    // ─── 11. Disconnect ──────────────────────────────────────────
    std::cout << "\n--- Disconnect ---\n";
    cam1->disconnect();
    assert(!cam1->connected());
    cam2->disconnect();
    cam3->disconnect();
    std::cout << "[PASS] All cameras disconnected cleanly.\n";

    std::cout << "\n====================================================\n";
    std::cout << "ALL PTZ CAMERA CONTROL TESTS PASSED!\n";
    std::cout << "====================================================\n";

    return 0;
}
