/**
 * sm::WebServer — Implementation
 *
 * Implements an embedded HTTP/WebSocket server on port 8088,
 * serving the Web Controller UI to iPads/tablets, synchronizing
 * two-way Tally state, and driving vMix 29's Web Telestrator.
 */

#include "streammix/web_server.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define CLOSE_SOCKET(s) closesocket(s)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  using socket_t = int;
  #define CLOSE_SOCKET(s) ::close(s)
#endif

namespace sm {

// ─── SHA-1 & Base64 Helpers for WebSocket Handshake ──────────────
static std::string base64Encode(const unsigned char* data, size_t len) {
    static const char s_b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (data[i] << 16);
        if (i + 1 < len) val |= (data[i + 1] << 8);
        if (i + 2 < len) val |= data[i + 2];

        out.push_back(s_b64[(val >> 18) & 0x3F]);
        out.push_back(s_b64[(val >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? s_b64[(val >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? s_b64[val & 0x3F] : '=');
    }
    return out;
}

// Lightweight standard SHA-1 implementation
struct SHA1Context {
    uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t count = 0;
    uint8_t buffer[64];
};

static inline uint32_t rol(uint32_t val, int bits) {
    return (val << bits) | (val >> (32 - bits));
}

static void sha1Transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) | (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }

        uint32_t temp = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static std::string computeWebSocketAcceptKey(const std::string& clientKey) {
    std::string magic = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA1Context ctx;
    size_t len = magic.length();
    size_t i = 0;

    while (i + 64 <= len) {
        sha1Transform(ctx.state, reinterpret_cast<const uint8_t*>(magic.data() + i));
        i += 64;
    }

    uint8_t finalBlock[64] = {0};
    size_t rem = len - i;
    std::memcpy(finalBlock, magic.data() + i, rem);
    finalBlock[rem] = 0x80;

    if (rem >= 56) {
        sha1Transform(ctx.state, finalBlock);
        std::memset(finalBlock, 0, 64);
    }
    uint64_t bits = len * 8;
    for (int b = 0; b < 8; ++b) {
        finalBlock[63 - b] = static_cast<uint8_t>(bits >> (b * 8));
    }
    sha1Transform(ctx.state, finalBlock);

    unsigned char digest[20];
    for (int j = 0; j < 5; ++j) {
        digest[j * 4 + 0] = static_cast<unsigned char>((ctx.state[j] >> 24) & 0xFF);
        digest[j * 4 + 1] = static_cast<unsigned char>((ctx.state[j] >> 16) & 0xFF);
        digest[j * 4 + 2] = static_cast<unsigned char>((ctx.state[j] >> 8) & 0xFF);
        digest[j * 4 + 3] = static_cast<unsigned char>(ctx.state[j] & 0xFF);
    }
    return base64Encode(digest, 20);
}

// ═════════════════════════════════════════════════════════════════
//  WebServer
// ═════════════════════════════════════════════════════════════════

WebServer::WebServer(Context& context, WebServerConfig config)
    : m_context(context), m_config(config)
{
    // Initialize transparent Telestrator canvas (1920x1080 RGBA)
    m_telestratorFrame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(1920, 1080));
    std::memset(m_telestratorFrame->data.data(), 0, m_telestratorFrame->data.size());

    // Bind Telestrator frame as source 9999 to overlay channel 8
    m_context.compositor().updateInputFrame(9999, m_telestratorFrame);
    m_context.compositor().setOverlaySource(m_config.telestratorOverlayChannel, 9999);
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() {
    if (m_running.load()) return false;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    m_listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenSocket < 0) return false;

    int opt = 1;
#ifndef _WIN32
    setsockopt(m_listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(m_config.port));

    if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSE_SOCKET(m_listenSocket);
        m_listenSocket = -1;
        return false;
    }

    if (listen(m_listenSocket, 10) < 0) {
        CLOSE_SOCKET(m_listenSocket);
        m_listenSocket = -1;
        return false;
    }

    m_running.store(true);
    m_serverThread = std::thread(&WebServer::serverLoop, this);
    return true;
}

void WebServer::stop() {
    if (!m_running.load()) return;

    m_running.store(false);
    if (m_listenSocket >= 0) {
        CLOSE_SOCKET(m_listenSocket);
        m_listenSocket = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        for (int sock : m_clientSockets) {
            CLOSE_SOCKET(sock);
        }
        m_clientSockets.clear();
    }

    if (m_serverThread.joinable()) {
        m_serverThread.join();
    }
}

size_t WebServer::connectedClientsCount() const {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    return m_clientSockets.size();
}

void WebServer::serverLoop() {
    while (m_running.load()) {
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(m_listenSocket, &readFds);

        timeval tv{0, 200000}; // 200ms
        int res = select(m_listenSocket + 1, &readFds, nullptr, nullptr, &tv);

        if (res > 0 && FD_ISSET(m_listenSocket, &readFds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int clientSock = accept(m_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
            if (clientSock >= 0) {
                std::thread(&WebServer::handleClient, this, clientSock).detach();
            }
        }
    }
}

void WebServer::handleClient(int clientSocket) {
    char buffer[4096];
    int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        CLOSE_SOCKET(clientSocket);
        return;
    }
    buffer[bytesRead] = '\0';
    std::string request(buffer, static_cast<size_t>(bytesRead));

    // Check for WebSocket Upgrade
    if (request.find("Upgrade: websocket") != std::string::npos ||
        request.find("upgrade: websocket") != std::string::npos) {
        // Extract Sec-WebSocket-Key
        std::string keyHeader = "Sec-WebSocket-Key: ";
        auto pos = request.find(keyHeader);
        if (pos != std::string::npos) {
            auto endPos = request.find("\r\n", pos);
            std::string key = request.substr(pos + keyHeader.length(), endPos - (pos + keyHeader.length()));
            std::string acceptKey = computeWebSocketAcceptKey(key);

            std::string response =
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + acceptKey + "\r\n\r\n";

            send(clientSocket, response.data(), response.size(), 0);

            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_clientSockets.push_back(clientSocket);
            }

            // Immediately send current switcher state
            broadcastState();

            // WebSocket message loop
            while (m_running.load()) {
                uint8_t wsHdr[2];
                int n = recv(clientSocket, reinterpret_cast<char*>(wsHdr), 2, 0);
                if (n <= 0) break;

                bool masked = (wsHdr[1] & 0x80) != 0;
                uint64_t payloadLen = wsHdr[1] & 0x7F;

                if (payloadLen == 126) {
                    uint8_t ext[2];
                    recv(clientSocket, reinterpret_cast<char*>(ext), 2, 0);
                    payloadLen = (ext[0] << 8) | ext[1];
                } else if (payloadLen == 127) {
                    uint8_t ext[8];
                    recv(clientSocket, reinterpret_cast<char*>(ext), 8, 0);
                    payloadLen = 0;
                    for (int k = 0; k < 8; ++k) payloadLen = (payloadLen << 8) | ext[k];
                }

                uint8_t maskKey[4] = {0};
                if (masked) {
                    recv(clientSocket, reinterpret_cast<char*>(maskKey), 4, 0);
                }

                std::vector<char> payload(payloadLen + 1, 0);
                recv(clientSocket, payload.data(), payloadLen, 0);

                if (masked) {
                    for (size_t k = 0; k < payloadLen; ++k) {
                        payload[k] ^= maskKey[k % 4];
                    }
                }

                std::string msg(payload.data(), payloadLen);
                executeWebCommand(msg);
            }

            // Client disconnected
            {
                std::lock_guard<std::mutex> lock(m_clientsMutex);
                m_clientSockets.erase(std::remove(m_clientSockets.begin(), m_clientSockets.end(), clientSocket), m_clientSockets.end());
            }
            CLOSE_SOCKET(clientSocket);
            return;
        }
    }

    // Standard HTTP Request
    handleHttpRequest(clientSocket, request);
    CLOSE_SOCKET(clientSocket);
}

void WebServer::handleHttpRequest(int clientSocket, const std::string& request) {
    std::string path = "/";
    if (request.rfind("GET ", 0) == 0) {
        auto endPos = request.find(' ', 4);
        if (endPos != std::string::npos) {
            path = request.substr(4, endPos - 4);
        }
    }

    if (path == "/" || path.empty()) {
        path = "/index.html";
    }

    // ─── vMix 29 HTTP / REST API & XML Status Endpoint ───────────
    if (path.rfind("/api", 0) == 0) {
        std::unordered_map<std::string, std::string> params;
        auto qpos = path.find('?');
        if (qpos != std::string::npos) {
            std::string query = path.substr(qpos + 1);
            std::istringstream qss(query);
            std::string pair;
            while (std::getline(qss, pair, '&')) {
                auto eq = pair.find('=');
                if (eq != std::string::npos) {
                    std::string k = pair.substr(0, eq);
                    std::string v = pair.substr(eq + 1);
                    params[k] = v;
                }
            }
        }

        if (params.find("Function") != params.end()) {
            std::string result = m_context.executeVmixFunction(params["Function"], params);
            broadcastState(); // sync web controller & iPad tallies immediately
            sendHttpResponse(clientSocket, "text/plain; charset=utf-8", result);
            return;
        } else {
            // vMix XML status engine (Bitfocus Companion & Stream Deck polling)
            std::string xml = m_context.generateVmixXml();
            sendHttpResponse(clientSocket, "text/xml; charset=utf-8", xml);
            return;
        }
    }

    std::string content = readFileContent(path);
    if (!content.empty()) {
        sendHttpResponse(clientSocket, getMimeType(path), content);
    } else {
        std::string notFound = "<html><body><h1>404 Not Found</h1></body></html>";
        std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: " +
                           std::to_string(notFound.size()) + "\r\n\r\n" + notFound;
        send(clientSocket, resp.data(), resp.size(), 0);
    }
}

std::string WebServer::readFileContent(const std::string& relativePath) {
    std::string clean = (relativePath.front() == '/') ? relativePath.substr(1) : relativePath;
    std::string fullPath = m_config.webRoot + "/" + clean;

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string WebServer::getMimeType(const std::string& path) {
    if (path.ends_with(".html")) return "text/html; charset=utf-8";
    if (path.ends_with(".css"))  return "text/css; charset=utf-8";
    if (path.ends_with(".js"))   return "application/javascript; charset=utf-8";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    return "text/plain";
}

void WebServer::sendHttpResponse(int clientSocket, const std::string& contentType, const std::string& body) {
    std::string header = "HTTP/1.1 200 OK\r\n"
                         "Content-Type: " + contentType + "\r\n"
                         "Content-Length: " + std::to_string(body.size()) + "\r\n"
                         "Access-Control-Allow-Origin: *\r\n"
                         "Connection: close\r\n\r\n";
    send(clientSocket, header.data(), header.size(), 0);
    send(clientSocket, body.data(), body.size(), 0);
}

void WebServer::sendWebSocketFrame(int clientSocket, const std::string& payload) {
    std::vector<uint8_t> frame;
    frame.push_back(0x81); // Text frame, FIN bit set

    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    send(clientSocket, reinterpret_cast<const char*>(frame.data()), frame.size(), 0);
}

void WebServer::broadcastState() {
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    if (m_clientSockets.empty()) return;

    uint64_t progId = m_context.compositor().programSourceId();
    uint64_t prevId = m_context.compositor().previewSourceId();
    float tbar = m_context.compositor().tbarPosition();
    bool ftb = m_context.compositor().isFTBActive();

    std::ostringstream json;
    json << "{\"type\":\"state\","
         << "\"program\":" << progId << ","
         << "\"preview\":" << prevId << ","
         << "\"tbar\":" << tbar << ","
         << "\"ftb\":" << (ftb ? "true" : "false") << "}";

    std::string payload = json.str();
    for (int sock : m_clientSockets) {
        sendWebSocketFrame(sock, payload);
    }
}

void WebServer::executeWebCommand(const std::string& json) {
    if (json.find("\"cut\"") != std::string::npos) {
        m_context.cut();
        broadcastState();
    } else if (json.find("\"fade\"") != std::string::npos) {
        m_context.startTransition(TransitionType::Fade, 500);
        broadcastState();
    } else if (json.find("\"ftb\"") != std::string::npos) {
        m_context.compositor().toggleFTB();
        broadcastState();
    } else if (json.find("\"overlay\"") != std::string::npos) {
        // e.g. {"action":"overlay","channel":1}
        m_context.toggleOverlay(1);
        broadcastState();
    } else if (json.find("\"clear_telestrator\"") != std::string::npos) {
        clearTelestrator();
    }
}

// ─── vMix 29 Web Telestrator Real-time Drawing ───────────────────
void WebServer::clearTelestrator() {
    std::lock_guard<std::mutex> lock(m_telestratorMutex);
    m_telestratorPoints.clear();
    if (m_telestratorFrame) {
        std::memset(m_telestratorFrame->data.data(), 0, m_telestratorFrame->data.size());
    }
}

void WebServer::addTelestratorStroke(const std::vector<TelestratorPoint>& points) {
    std::lock_guard<std::mutex> lock(m_telestratorMutex);
    if (!m_telestratorFrame || m_telestratorFrame->empty()) return;

    int w = m_telestratorFrame->width;
    int h = m_telestratorFrame->height;
    uint8_t* dst = m_telestratorFrame->data.data();

    // Ensure overlay 8 is visible on screen
    m_context.compositor().showOverlay(m_config.telestratorOverlayChannel, OverlayTransition::Cut, 0);

    for (const auto& pt : points) {
        int cx = static_cast<int>(pt.x * w);
        int cy = static_cast<int>(pt.y * h);
        int r = static_cast<int>(pt.brushSize);

        uint8_t red   = (pt.color >> 16) & 0xFF;
        uint8_t green = (pt.color >> 8)  & 0xFF;
        uint8_t blue  = pt.color & 0xFF;

        int minX = std::max(0, cx - r);
        int maxX = std::min(w - 1, cx + r);
        int minY = std::max(0, cy - r);
        int maxY = std::min(h - 1, cy + r);

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                if (dx * dx + dy * dy <= r * r) {
                    size_t idx = static_cast<size_t>((y * w + x) * 4);
                    dst[idx + 0] = red;
                    dst[idx + 1] = green;
                    dst[idx + 2] = blue;
                    dst[idx + 3] = 255;
                }
            }
        }
    }
}

} // namespace sm
