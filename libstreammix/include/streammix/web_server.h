#pragma once
/**
 * sm::WebServer — Embedded Web Controller & Web Telestrator Server (vMix 29)
 *
 * Implements:
 *   - Embedded HTTP server serving the Web Controller UI (index.html, style.css, app.js)
 *   - Real-time WebSocket server for low-latency wireless tablet/phone control
 *   - Two-way Tally state synchronization (Preview/Program status to mobile devices)
 *   - vMix 29 Web Telestrator: Receives live stylus/touch drawing coordinates from web
 *     clients and injects them as a real-time overlay layer on the Compositor
 *   - Cross-platform C++ socket architecture (macOS, Linux, Windows)
 */

#include "streammix/context.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace sm {

struct WebServerConfig {
    int port = 8088;                        ///< Standard vMix Web Controller port
    std::string webRoot = ".";              ///< Directory containing index.html, app.js, style.css
    bool enableTelestrator = true;          ///< vMix 29 interactive on-screen drawing
    int telestratorOverlayChannel = 8;      ///< Overlay layer reserved for annotations
};

struct TelestratorPoint {
    float x = 0.0f;     ///< Normalized 0.0 -> 1.0
    float y = 0.0f;     ///< Normalized 0.0 -> 1.0
    uint32_t color = 0xFFFF0000; ///< ARGB
    float brushSize = 4.0f;
    bool isNewStroke = false;
};

class WebServer {
public:
    explicit WebServer(Context& context, WebServerConfig config = WebServerConfig{});
    ~WebServer();

    bool start();
    void stop();
    bool isRunning() const { return m_running.load(); }

    int port() const { return m_config.port; }
    size_t connectedClientsCount() const;

    // ─── Broadcast State to All Connected Web Controllers ────────
    void broadcastState();

    // ─── Telestrator Overlay Ingestion ───────────────────────────
    void clearTelestrator();
    void addTelestratorStroke(const std::vector<TelestratorPoint>& points);

private:
    void serverLoop();
    void handleClient(int clientSocket);
    void handleHttpRequest(int clientSocket, const std::string& request);
    void handleWebSocketMessage(int clientSocket, const std::string& message);
    void sendHttpResponse(int clientSocket, const std::string& contentType, const std::string& body);
    void sendWebSocketFrame(int clientSocket, const std::string& payload);

    std::string getMimeType(const std::string& path);
    std::string readFileContent(const std::string& relativePath);
    void executeWebCommand(const std::string& json);

    Context& m_context;
    WebServerConfig m_config;

    std::atomic<bool> m_running { false };
    std::thread m_serverThread;
    int m_listenSocket = -1;

    mutable std::mutex m_clientsMutex;
    std::vector<int> m_clientSockets;

    // Telestrator Layer Data
    std::mutex m_telestratorMutex;
    std::vector<TelestratorPoint> m_telestratorPoints;
    VideoFramePtr m_telestratorFrame;
};

} // namespace sm
