/**
 * StreamMix macOS Live Preview Server (Port 8088)
 *
 * Runs the complete headless production engine, registers sample cameras,
 * starts the embedded Web Controller on port 8088, and keeps the 60fps render
 * loop active for interactive browser / iPad previewing.
 */

#include "streammix.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

static std::atomic<bool> g_running{true};

void sigHandler(int) {
    g_running = false;
}

class PreviewCameraSource : public sm::Source {
public:
    explicit PreviewCameraSource(const std::string& name, uint8_t r, uint8_t g, uint8_t b)
        : sm::Source(name, sm::SourceType::Color), m_r(r), m_g(g), m_b(b)
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

private:
    uint8_t m_r, m_g, m_b;
};

int main(int argc, char** argv) {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    int port = 8088;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║    StreamMix v1.0.0 — Live Broadcast Production Server     ║\n";
    std::cout << "║    Native C++20 Core Engine & Embedded Web Controller     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n\n";

    // 1. Initialize Master Production Context
    sm::Context ctx;
    ctx.initialize();

    // 2. Register Inputs
    auto cam1 = std::make_shared<PreviewCameraSource>("Stage Camera (Host)", 220, 50, 50);
    auto cam2 = std::make_shared<PreviewCameraSource>("Remote Guest Feed", 50, 120, 220);
    auto cam3 = std::make_shared<PreviewCameraSource>("Wide Angle Stage", 50, 180, 80);
    auto clip = std::make_shared<PreviewCameraSource>("VT 1 - Promo VT", 220, 180, 40);
    auto title = std::make_shared<PreviewCameraSource>("Lower Third Graphic", 240, 240, 240);

    ctx.addSource(cam1);
    ctx.addSource(cam2);
    ctx.addSource(cam3);
    ctx.addSource(clip);
    ctx.addSource(title);

    ctx.setProgramSource(cam1->id());
    ctx.setPreviewSource(cam2->id());

    // Configure Audio Mixer
    ctx.audioMixer().setGain(cam1->id(), 0.90f);
    ctx.audioMixer().setGain(cam2->id(), 0.80f);
    ctx.audioMixer().setGain(cam3->id(), 0.70f);
    ctx.audioMixer().setGain(clip->id(), 1.0f);
    ctx.audioMixer().setMasterVolume(0.95f);

    // 3. Start Embedded Web Controller
    sm::WebServerConfig cfg;
    cfg.port = port;
    cfg.webRoot = ".";
    cfg.enableTelestrator = true;
    cfg.telestratorOverlayChannel = 8;

    sm::WebServer server(ctx, cfg);
    if (!server.start()) {
        std::cerr << "❌ Failed to bind Web Controller on port " << port << "!\n";
        return 1;
    }

    std::cout << "✔ Engine Status: ONLINE (60 FPS tick loop active)\n";
    std::cout << "✔ Active Program: Input 1 (" << cam1->name() << ")\n";
    std::cout << "✔ Active Preview: Input 2 (" << cam2->name() << ")\n";
    std::cout << "✔ Web Controller & Telestrator: http://localhost:" << port << "\n";
    std::cout << "✔ vMix 29 REST API: http://localhost:" << port << "/api?Function=Cut\n";
    std::cout << "✔ vMix 29 XML Status: http://localhost:" << port << "/api\n\n";
    std::cout << "Press Ctrl+C to terminate server.\n\n";

    // 4. Run 60 FPS Render & Animation Loop
    while (g_running.load()) {
        ctx.tick(0.01666);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    std::cout << "\nStopping StreamMix server...\n";
    server.stop();
    ctx.shutdown();
    std::cout << "StreamMix stopped cleanly.\n";

    return 0;
}
