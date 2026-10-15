/**
 * sm::PTZController — Implementation
 *
 * Cross-platform VISCA-over-IP (UDP/TCP) and Pelco-D PTZ camera control.
 * All network I/O uses POSIX sockets on macOS/Linux and Winsock2 on Windows.
 * The "Simulated" protocol path performs zero network I/O for headless testing.
 */

#include "streammix/ptz_controller.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <sstream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using socket_t = SOCKET;
  #define CLOSE_SOCK(s) closesocket(s)
  #define SOCK_INVALID  INVALID_SOCKET
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  using socket_t = int;
  #define CLOSE_SOCK(s) ::close(s)
  #define SOCK_INVALID  (-1)
#endif

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  PTZCamera
// ═════════════════════════════════════════════════════════════════

PTZCamera::PTZCamera(uint64_t id, const std::string& name,
                     const std::string& host, uint16_t port,
                     PTZProtocol protocol)
    : m_id(id), m_name(name), m_host(host), m_port(port), m_protocol(protocol)
{
    for (size_t i = 0; i < m_presets.size(); ++i) {
        m_presets[i].number = static_cast<uint8_t>(i);
    }
}

PTZCamera::~PTZCamera() {
    disconnect();
}

bool PTZCamera::connect() {
    if (m_protocol == PTZProtocol::Simulated) {
        m_connected.store(true);
        return true;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    bool tcp = (m_protocol == PTZProtocol::VISCA_TCP ||
                m_protocol == PTZProtocol::Pelco_D_TCP);

    m_socket = static_cast<int>(::socket(AF_INET,
                                          tcp ? SOCK_STREAM : SOCK_DGRAM,
                                          0));
    if (m_socket == SOCK_INVALID) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(m_port);
    // Resolve hostname
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = tcp ? SOCK_STREAM : SOCK_DGRAM;
    if (::getaddrinfo(m_host.c_str(), nullptr, &hints, &res) == 0 && res) {
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    } else {
        // Fallback: try parsing as dotted-decimal
        ::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    }

    if (tcp) {
        if (::connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            CLOSE_SOCK(m_socket);
            m_socket = SOCK_INVALID;
            return false;
        }
    } else {
        // UDP: store server address for subsequent sendto calls
        // Set socket timeout
        struct timeval tv { 0, 200000 }; // 200ms
        ::setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&tv), sizeof(tv));
    }

    m_connected.store(true);
    return true;
}

void PTZCamera::disconnect() {
    if (!m_connected.load()) return;
    m_connected.store(false);
    if (m_socket != SOCK_INVALID) {
        CLOSE_SOCK(m_socket);
        m_socket = SOCK_INVALID;
    }
}

// ─── VISCA Command Builders ───────────────────────────────────────

// VISCA Packet header for VISCA-over-IP: [type(2)] [len(2)] [seq(4)] [payload]
static std::vector<uint8_t> viscaIPPacket(const std::vector<uint8_t>& payload,
                                           uint8_t& seqCounter) {
    uint16_t len = static_cast<uint16_t>(payload.size());
    std::vector<uint8_t> pkt;
    pkt.push_back(0x01); pkt.push_back(0x00);         // Type: VISCA Command
    pkt.push_back((len >> 8) & 0xFF);
    pkt.push_back(len & 0xFF);
    pkt.push_back(0x00); pkt.push_back(0x00);
    pkt.push_back(0x00); pkt.push_back(seqCounter++);  // Sequence number
    for (auto b : payload) pkt.push_back(b);
    return pkt;
}

// VISCA serial-style payload (camera address 1 = 0x81)
static std::vector<uint8_t> viscaCmd(std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> v = {0x81}; // Address byte (camera 1)
    for (auto b : bytes) v.push_back(b);
    v.push_back(0xFF); // Terminator
    return v;
}

// Pelco-D frame: [sync(FF)] [addr(01)] [cmd1] [cmd2] [data1] [data2] [checksum]
static std::vector<uint8_t> pelcoDFrame(uint8_t addr, uint8_t cmd1, uint8_t cmd2,
                                         uint8_t d1, uint8_t d2) {
    uint8_t ck = static_cast<uint8_t>((addr + cmd1 + cmd2 + d1 + d2) & 0xFF);
    return {0xFF, addr, cmd1, cmd2, d1, d2, ck};
}

bool PTZCamera::sendCommand(const std::vector<uint8_t>& bytes) {
    if (m_protocol == PTZProtocol::Simulated) return true;
    if (!m_connected.load() || m_socket == SOCK_INVALID) return false;

    bool tcp = (m_protocol == PTZProtocol::VISCA_TCP ||
                m_protocol == PTZProtocol::Pelco_D_TCP);

    std::vector<uint8_t> packet;
    if (m_protocol == PTZProtocol::VISCA_UDP ||
        m_protocol == PTZProtocol::VISCA_TCP) {
        packet = viscaIPPacket(bytes, m_viscaSeq);
    } else {
        packet = bytes; // Pelco-D pre-assembled
    }

    if (tcp) {
        ::send(m_socket, reinterpret_cast<const char*>(packet.data()),
               static_cast<int>(packet.size()), 0);
    } else {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(m_port);
        ::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
        ::sendto(m_socket,
                 reinterpret_cast<const char*>(packet.data()),
                 static_cast<int>(packet.size()), 0,
                 reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }
    return true;
}

// ─── Motion ──────────────────────────────────────────────────────

void PTZCamera::panLeft(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_protocol == PTZProtocol::Pelco_D_TCP) {
        sendCommand(pelcoDFrame(0x01, 0x00, 0x04, speed, 0x00));
    } else {
        sendCommand(viscaCmd({0x01, 0x06, 0x01, speed, 0x01, 0x03, 0x03}));
    }
    m_position.pan -= speed * 10;
}

void PTZCamera::panRight(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_protocol == PTZProtocol::Pelco_D_TCP) {
        sendCommand(pelcoDFrame(0x01, 0x00, 0x02, speed, 0x00));
    } else {
        sendCommand(viscaCmd({0x01, 0x06, 0x01, speed, 0x01, 0x02, 0x03}));
    }
    m_position.pan += speed * 10;
}

void PTZCamera::tiltUp(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_protocol == PTZProtocol::Pelco_D_TCP) {
        sendCommand(pelcoDFrame(0x01, 0x00, 0x08, 0x00, speed));
    } else {
        sendCommand(viscaCmd({0x01, 0x06, 0x01, 0x01, speed, 0x03, 0x01}));
    }
    m_position.tilt += speed * 5;
}

void PTZCamera::tiltDown(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_protocol == PTZProtocol::Pelco_D_TCP) {
        sendCommand(pelcoDFrame(0x01, 0x00, 0x10, 0x00, speed));
    } else {
        sendCommand(viscaCmd({0x01, 0x06, 0x01, 0x01, speed, 0x03, 0x02}));
    }
    m_position.tilt -= speed * 5;
}

void PTZCamera::panTilt(int8_t panDir, int8_t tiltDir,
                         uint8_t panSpeed, uint8_t tiltSpeed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Pan-Tilt Drive: 0x01 0x06 0x01 panSpeed tiltSpeed panDir tiltDir FF
    uint8_t pd = (panDir  < 0) ? 0x01 : (panDir  > 0) ? 0x02 : 0x03; // L/R/Stop
    uint8_t td = (tiltDir > 0) ? 0x01 : (tiltDir < 0) ? 0x02 : 0x03; // U/D/Stop
    sendCommand(viscaCmd({0x01, 0x06, 0x01, panSpeed, tiltSpeed, pd, td}));
    m_position.pan  += panDir  * panSpeed  * 10;
    m_position.tilt += tiltDir * tiltSpeed * 5;
}

void PTZCamera::stopPanTilt() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_protocol == PTZProtocol::Pelco_D_TCP) {
        sendCommand(pelcoDFrame(0x01, 0x00, 0x00, 0x00, 0x00));
    } else {
        sendCommand(viscaCmd({0x01, 0x06, 0x01, 0x01, 0x01, 0x03, 0x03}));
    }
}

void PTZCamera::zoomIn(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint8_t sp = speed & 0x07;
    sendCommand(viscaCmd({0x01, 0x04, 0x07,
                          static_cast<uint8_t>(0x20 | sp)}));
    m_position.zoom = std::min(m_position.zoom + sp * 200, 0x4000);
}

void PTZCamera::zoomOut(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint8_t sp = speed & 0x07;
    sendCommand(viscaCmd({0x01, 0x04, 0x07,
                          static_cast<uint8_t>(0x30 | sp)}));
    m_position.zoom = std::max(m_position.zoom - sp * 200, 0);
}

void PTZCamera::stopZoom() {
    std::lock_guard<std::mutex> lock(m_mutex);
    sendCommand(viscaCmd({0x01, 0x04, 0x07, 0x00}));
}

void PTZCamera::focusNear(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint8_t sp = speed & 0x07;
    sendCommand(viscaCmd({0x01, 0x04, 0x08,
                          static_cast<uint8_t>(0x30 | sp)}));
    m_position.focus = std::max(m_position.focus - sp * 50, 0x1000);
}

void PTZCamera::focusFar(uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    uint8_t sp = speed & 0x07;
    sendCommand(viscaCmd({0x01, 0x04, 0x08,
                          static_cast<uint8_t>(0x20 | sp)}));
    m_position.focus = std::min(m_position.focus + sp * 50, 0xF000);
}

void PTZCamera::stopFocus() {
    std::lock_guard<std::mutex> lock(m_mutex);
    sendCommand(viscaCmd({0x01, 0x04, 0x08, 0x00}));
}

void PTZCamera::setAutoFocus(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // CAM_Focus Auto: 81 01 04 38 02 FF  / Manual: 81 01 04 38 03 FF
    sendCommand(viscaCmd({0x01, 0x04, 0x38,
                          static_cast<uint8_t>(enable ? 0x02 : 0x03)}));
}

// ─── Absolute Position ────────────────────────────────────────────

void PTZCamera::moveTo(PTZPosition pos, PTZSpeed speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA AbsolutePosition: 81 01 06 02 vv ww 0p 0q 0r 0s 0t 0u 0v 0w FF
    // pp = pan position (4 nibbles), tt = tilt position (4 nibbles)
    int32_t p = std::clamp(pos.pan,  -0x8FFF, 0x8FFF);
    int32_t t = std::clamp(pos.tilt, -0x8FFF, 0x8FFF);
    // Encode as unsigned 16-bit signed with nibble split
    uint16_t pu = static_cast<uint16_t>(p);
    uint16_t tu = static_cast<uint16_t>(t);
    auto n = [](uint16_t v, int shift) -> uint8_t {
        return static_cast<uint8_t>((v >> shift) & 0x0F);
    };
    sendCommand(viscaCmd({
        0x01, 0x06, 0x02,
        speed.panSpeed, speed.tiltSpeed,
        n(pu,12), n(pu,8), n(pu,4), n(pu,0),
        n(tu,12), n(tu,8), n(tu,4), n(tu,0)
    }));
    m_position.pan  = pos.pan;
    m_position.tilt = pos.tilt;
    // Also move zoom
    if (pos.zoom != m_position.zoom) {
        uint16_t zu = static_cast<uint16_t>(std::clamp(pos.zoom, 0, 0x4000));
        sendCommand(viscaCmd({0x01, 0x04, 0x47,
                              n(zu,12), n(zu,8), n(zu,4), n(zu,0)}));
        m_position.zoom = pos.zoom;
    }
}

void PTZCamera::zoomTo(int32_t absoluteZoom, uint8_t speed) {
    (void)speed;
    std::lock_guard<std::mutex> lock(m_mutex);
    uint16_t zu = static_cast<uint16_t>(std::clamp(absoluteZoom, 0, 0x4000));
    auto n = [](uint16_t v, int shift) -> uint8_t {
        return static_cast<uint8_t>((v >> shift) & 0x0F);
    };
    sendCommand(viscaCmd({0x01, 0x04, 0x47,
                          n(zu,12), n(zu,8), n(zu,4), n(zu,0)}));
    m_position.zoom = absoluteZoom;
}

// ─── Presets ──────────────────────────────────────────────────────

void PTZCamera::savePreset(uint8_t presetNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Memory Set: 81 01 04 3F 01 pp FF
    sendCommand(viscaCmd({0x01, 0x04, 0x3F, 0x01, presetNumber}));
    m_presets[presetNumber].position  = m_position;
    m_presets[presetNumber].populated = true;
}

void PTZCamera::recallPreset(uint8_t presetNumber, uint8_t speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Memory Recall: 81 01 04 3F 02 pp FF
    // Some cameras: 81 01 06 24 speed 01 pp FF (PTZOptics extension with speed)
    sendCommand(viscaCmd({0x01, 0x06, 0x24, speed, 0x01, presetNumber}));
    if (m_presets[presetNumber].populated) {
        m_position = m_presets[presetNumber].position;
    }
}

void PTZCamera::clearPreset(uint8_t presetNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    sendCommand(viscaCmd({0x01, 0x04, 0x3F, 0x00, presetNumber}));
    m_presets[presetNumber].populated = false;
    m_presets[presetNumber].name.clear();
}

void PTZCamera::setPresetName(uint8_t presetNumber, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_presets[presetNumber].name = name;
}

const PTZPreset& PTZCamera::preset(uint8_t presetNumber) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_presets[presetNumber];
}

// ─── Home & Power ─────────────────────────────────────────────────

void PTZCamera::home() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Home: 81 01 06 04 FF
    sendCommand(viscaCmd({0x01, 0x06, 0x04}));
    m_position = PTZPosition{};
}

void PTZCamera::powerOn() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Power On: 81 01 04 00 02 FF
    sendCommand(viscaCmd({0x01, 0x04, 0x00, 0x02}));
}

void PTZCamera::powerOff() {
    std::lock_guard<std::mutex> lock(m_mutex);
    // VISCA Power Off: 81 01 04 00 03 FF
    sendCommand(viscaCmd({0x01, 0x04, 0x00, 0x03}));
}

// ─── Tally ────────────────────────────────────────────────────────

void PTZCamera::setTally(TallyState state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tally = state;
    // PTZOptics VISCA Tally extension:
    //   Tally Off:     81 0A 02 02 00 FF
    //   Tally Red On:  81 0A 02 02 01 FF  (Program)
    //   Tally Green On:81 0A 02 02 02 FF  (Preview)
    uint8_t tallyCode = 0x00;
    if (state == TallyState::Program) tallyCode = 0x01;
    if (state == TallyState::Preview) tallyCode = 0x02;
    sendCommand(viscaCmd({0x0A, 0x02, 0x02, tallyCode}));
}

// ─── State ────────────────────────────────────────────────────────

PTZPosition PTZCamera::currentPosition() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_position;
}

void PTZCamera::setSimulatedPosition(PTZPosition pos) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_position = pos;
}

// ═════════════════════════════════════════════════════════════════
//  PTZController
// ═════════════════════════════════════════════════════════════════

std::shared_ptr<PTZCamera> PTZController::addCamera(uint64_t sourceId,
                                                      const std::string& name,
                                                      const std::string& host,
                                                      uint16_t port,
                                                      PTZProtocol protocol) {
    auto cam = std::make_shared<PTZCamera>(sourceId, name, host, port, protocol);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cameras[sourceId] = cam;
    if (m_activeCameraId == 0) m_activeCameraId = sourceId;
    return cam;
}

void PTZController::removeCamera(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cameras.erase(sourceId);
    m_autoTrackPresets.erase(sourceId);
    if (m_activeCameraId == sourceId) {
        m_activeCameraId = m_cameras.empty() ? 0 : m_cameras.begin()->first;
    }
}

std::shared_ptr<PTZCamera> PTZController::camera(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cameras.find(sourceId);
    return (it != m_cameras.end()) ? it->second : nullptr;
}

std::vector<uint64_t> PTZController::cameraIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> ids;
    ids.reserve(m_cameras.size());
    for (const auto& pair : m_cameras) ids.push_back(pair.first);
    return ids;
}

size_t PTZController::cameraCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_cameras.size();
}

void PTZController::setAutoTrackPreset(uint64_t sourceId, uint8_t presetNumber) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_autoTrackPresets[sourceId] = presetNumber;
}

void PTZController::clearAutoTrack(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_autoTrackPresets.erase(sourceId);
}

void PTZController::onProgramSourceChanged(uint64_t newSourceId,
                                            uint64_t prevSourceId) {
    // Send tally updates to new program & previous cameras
    updateTally(newSourceId, 0);

    // Auto-track: recall preset on the newly active camera
    std::shared_ptr<PTZCamera> newCam, prevCam;
    uint8_t newPreset = 0;
    bool hasAutoTrack = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cameras.find(newSourceId);
        if (it != m_cameras.end()) newCam = it->second;
        auto pit = m_cameras.find(prevSourceId);
        if (pit != m_cameras.end()) prevCam = pit->second;
        auto atIt = m_autoTrackPresets.find(newSourceId);
        if (atIt != m_autoTrackPresets.end()) {
            newPreset    = atIt->second;
            hasAutoTrack = true;
        }
    }
    if (newCam  && hasAutoTrack) newCam->recallPreset(newPreset);
}

void PTZController::updateTally(uint64_t programSourceId,
                                 uint64_t previewSourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_cameras) {
        uint64_t id  = pair.first;
        auto& cam    = pair.second;
        TallyState ts = TallyState::Off;
        if (id == programSourceId) ts = TallyState::Program;
        else if (id == previewSourceId) ts = TallyState::Preview;
        cam->setTally(ts);
    }
}

void PTZController::setActiveCamera(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cameras.count(sourceId)) m_activeCameraId = sourceId;
}

// ─── Delegate to active camera ────────────────────────────────────

#define ACTIVE_CAM_DO(expr) \
    do { \
        auto cam = camera(m_activeCameraId); \
        if (cam) { cam->expr; } \
    } while(0)

void PTZController::panLeft  (uint8_t speed) { ACTIVE_CAM_DO(panLeft(speed)); }
void PTZController::panRight (uint8_t speed) { ACTIVE_CAM_DO(panRight(speed)); }
void PTZController::tiltUp   (uint8_t speed) { ACTIVE_CAM_DO(tiltUp(speed)); }
void PTZController::tiltDown (uint8_t speed) { ACTIVE_CAM_DO(tiltDown(speed)); }
void PTZController::stopPanTilt()            { ACTIVE_CAM_DO(stopPanTilt()); }
void PTZController::zoomIn   (uint8_t speed) { ACTIVE_CAM_DO(zoomIn(speed)); }
void PTZController::zoomOut  (uint8_t speed) { ACTIVE_CAM_DO(zoomOut(speed)); }
void PTZController::home     ()              { ACTIVE_CAM_DO(home()); }
void PTZController::recallPreset(uint8_t p, uint8_t speed) { ACTIVE_CAM_DO(recallPreset(p, speed)); }
void PTZController::savePreset  (uint8_t p)                { ACTIVE_CAM_DO(savePreset(p)); }

} // namespace sm
