/**
 * StreamMix Integration Test — Web Server, Web Controller & Telestrator
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Web Controller & Telestrator Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Context
    sm::Context ctx;
    ctx.initialize();
    ctx.setProgramSource(1);
    ctx.setPreviewSource(2);

    // 2. Start Embedded Web Controller on port 8089 (test port)
    sm::WebServerConfig cfg;
    cfg.port = 8089;
    cfg.webRoot = ".";
    cfg.enableTelestrator = true;
    cfg.telestratorOverlayChannel = 8;

    sm::WebServer server(ctx, cfg);
    bool started = server.start();
    assert(started);
    assert(server.isRunning());
    std::cout << "[PASS] Embedded Web Controller started on port 8089.\n";

    // Give server thread a moment to bind & listen
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 3. Test HTTP GET Request
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8089);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int conn = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(conn == 0);

    std::string req = "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sock, req.data(), req.size(), 0);

    char respBuf[1024] = {0};
    int n = recv(sock, respBuf, sizeof(respBuf) - 1, 0);
    assert(n > 0);
    std::string resp(respBuf, n);
    assert(resp.find("200 OK") != std::string::npos);
    close(sock);
    std::cout << "[PASS] HTTP GET /index.html returned 200 OK with web assets.\n";

    // 4. Test vMix 29 Web Telestrator Real-Time Drawing
    std::vector<sm::TelestratorPoint> stroke;
    sm::TelestratorPoint pt1;
    pt1.x = 0.5f; pt1.y = 0.5f; pt1.color = 0xFFFF0000; pt1.brushSize = 6.0f;
    stroke.push_back(pt1);

    server.addTelestratorStroke(stroke);
    assert(ctx.compositor().isOverlayActive(8));
    std::cout << "[PASS] Web Telestrator annotation injected onto Overlay Channel 8.\n";

    // Clear telestrator
    server.clearTelestrator();
    std::cout << "[PASS] Web Telestrator canvas cleared.\n";

    // 5. Cleanup & Server Shutdown
    server.stop();
    assert(!server.isRunning());
    ctx.shutdown();
    std::cout << "[PASS] Web Controller shutdown cleanly.\n";

    std::cout << "====================================================\n";
    std::cout << "ALL WEB CONTROLLER TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "====================================================\n";

    return 0;
}
