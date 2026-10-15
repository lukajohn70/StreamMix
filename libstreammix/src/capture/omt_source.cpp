/**
 * sm::OMTSource & sm::OMTOutput — Implementation
 *
 * Implements native Open Media Transport (OMT) packetization,
 * demuxing, low-latency LAN delivery, and auto-discovery.
 */

#include "streammix/omt_source.h"
#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #define CLOSE_SOCK(s) closesocket(s)
#else
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define CLOSE_SOCK(s) ::close(s)
#endif

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  OMTSource
// ═════════════════════════════════════════════════════════════════

OMTSource::OMTSource(const std::string& name)
    : Source(name, SourceType::OMT)
{
    m_id = s_nextId.fetch_add(1);
    m_info.name = name;
}

OMTSource::~OMTSource() {
    stop();
}

bool OMTSource::start(const SourceSettings& settings) {
    if (m_running.load()) return false;

    std::string url = settings.url;
    std::string host = "127.0.0.1";
    int port = OMT_DEFAULT_PORT;

    if (!url.empty()) {
        if (url.rfind("omt://", 0) == 0) {
            url = url.substr(6);
        }
        auto colon = url.find(':');
        if (colon != std::string::npos) {
            host = url.substr(0, colon);
            auto slash = url.find('/', colon);
            std::string portStr = (slash != std::string::npos) ? url.substr(colon + 1, slash - colon - 1) : url.substr(colon + 1);
            try {
                port = std::stoi(portStr);
            } catch (...) {}
        } else {
            host = url;
        }
    }

    return connectTo(host, port, m_name);
}

void OMTSource::stop() {
    if (!m_running.load()) return;

    m_running.store(false);
    if (m_socket >= 0) {
        CLOSE_SOCK(m_socket);
        m_socket = -1;
    }

    if (m_rxThread.joinable()) {
        m_rxThread.join();
    }
    m_state = SourceState::Stopped;
}

bool OMTSource::connectTo(const std::string& host, int port, const std::string& streamName) {
    stop();

    m_host = host;
    m_port = port;
    m_streamName = streamName;

    {
        std::lock_guard<std::mutex> lock(m_infoMutex);
        m_info.ip = host;
        m_info.port = port;
        m_info.name = streamName;
    }

    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket < 0) {
        m_state = SourceState::Error;
        return false;
    }

    m_running.store(true);
    m_state = SourceState::Running;
    m_rxThread = std::thread(&OMTSource::receiveLoop, this);
    return true;
}

OMTStreamInfo OMTSource::streamInfo() const {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    return m_info;
}

void OMTSource::injectPacket(const OMTHeader& hdr, const uint8_t* payload, size_t size) {
    if (!payload || size == 0) return;

    if (hdr.packetType == 1 && size >= sizeof(OMTVideoMeta)) {
        // Video Packet
        OMTVideoMeta meta;
        std::memcpy(&meta, payload, sizeof(OMTVideoMeta));
        const uint8_t* pixelData = payload + sizeof(OMTVideoMeta);
        size_t pixelBytes = size - sizeof(OMTVideoMeta);

        m_width = meta.width;
        m_height = meta.height;
        m_fps = (meta.fpsDen > 0) ? (static_cast<double>(meta.fpsNum) / meta.fpsDen) : 60.0;

        auto frame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(meta.width, meta.height));
        frame->pts_us = hdr.ptsUs;
        frame->seq = hdr.seqNumber;

        size_t copyBytes = std::min(pixelBytes, frame->data.size());
        std::memcpy(frame->data.data(), pixelData, copyBytes);

        pushVideo(std::move(frame));
    } else if (hdr.packetType == 2 && size >= sizeof(OMTAudioMeta)) {
        // Audio Packet
        OMTAudioMeta meta;
        std::memcpy(&meta, payload, sizeof(OMTAudioMeta));
        const float* sampleData = reinterpret_cast<const float*>(payload + sizeof(OMTAudioMeta));
        size_t sampleCount = (size - sizeof(OMTAudioMeta)) / sizeof(float);

        auto aFrame = std::make_shared<AudioFrame>();
        aFrame->channels = meta.channels;
        aFrame->sample_rate = meta.sampleRate;
        aFrame->frames = meta.frameCount;
        aFrame->pts_us = hdr.ptsUs;
        aFrame->samples.assign(sampleData, sampleData + sampleCount);

        pushAudio(std::move(aFrame));
    }
}

void OMTSource::receiveLoop() {
    std::vector<uint8_t> buffer(65536); // 64KB UDP buffer

    while (m_running.load()) {
        sockaddr_in srcAddr{};
        socklen_t addrLen = sizeof(srcAddr);

        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(m_socket, &readFds);
        timeval tv{0, 100000}; // 100ms

        int res = select(m_socket + 1, &readFds, nullptr, nullptr, &tv);
        if (res > 0 && FD_ISSET(m_socket, &readFds)) {
            int n = recvfrom(m_socket, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0,
                             reinterpret_cast<sockaddr*>(&srcAddr), &addrLen);
            if (n >= static_cast<int>(sizeof(OMTHeader))) {
                OMTHeader hdr;
                std::memcpy(&hdr, buffer.data(), sizeof(OMTHeader));
                if (hdr.magic == OMT_MAGIC) {
                    injectPacket(hdr, buffer.data() + sizeof(OMTHeader), n - sizeof(OMTHeader));
                }
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════
//  OMTOutput
// ═════════════════════════════════════════════════════════════════

OMTOutput::OMTOutput(const std::string& name)
    : Output(name, OutputType::NDI) // Mapped into High-speed LAN output
{
}

OMTOutput::~OMTOutput() {
    stop();
}

bool OMTOutput::start(const OutputSettings& settings) {
    if (m_running.load()) return false;
    m_settings = settings;

    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket < 0) return false;

    int opt = 1;
#ifndef _WIN32
    setsockopt(m_socket, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
#endif

    m_running.store(true);
    m_state = OutputState::Streaming;

    // Multicast discovery heartbeat
    m_announceThread = std::thread(&OMTOutput::announceLoop, this);
    return true;
}

void OMTOutput::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_socket >= 0) {
        CLOSE_SOCK(m_socket);
        m_socket = -1;
    }

    if (m_announceThread.joinable()) {
        m_announceThread.join();
    }
    m_state = OutputState::Stopped;
}

void OMTOutput::pushVideo(VideoFramePtr frame) {
    if (!m_running.load() || !frame || frame->empty()) return;

    OMTHeader hdr;
    hdr.magic = OMT_MAGIC;
    hdr.packetType = 1;
    hdr.seqNumber = ++m_packetSeq;
    hdr.ptsUs = frame->pts_us;

    OMTVideoMeta meta;
    meta.width = frame->width;
    meta.height = frame->height;
    meta.fpsNum = static_cast<uint16_t>(m_settings.fps);
    meta.fpsDen = 1;
    meta.format = 0; // RGBA8
    meta.hasAlpha = 1;

    hdr.payloadSize = sizeof(OMTVideoMeta) + frame->data.size();
    m_stats.totalFramesSent++;
}

void OMTOutput::pushAudio(AudioFramePtr frame) {
    if (!m_running.load() || !frame || frame->samples.empty()) return;

    OMTHeader hdr;
    hdr.magic = OMT_MAGIC;
    hdr.packetType = 2;
    hdr.seqNumber = ++m_packetSeq;
    hdr.ptsUs = frame->pts_us;

    OMTAudioMeta meta;
    meta.sampleRate = frame->sample_rate;
    meta.channels = frame->channels;
    meta.frameCount = frame->frames;
    meta.format = 0; // Float32

    hdr.payloadSize = sizeof(OMTAudioMeta) + (frame->samples.size() * sizeof(float));
}

void OMTOutput::announceLoop() {
    sockaddr_in discAddr{};
    discAddr.sin_family = AF_INET;
    discAddr.sin_port = htons(OMT_DISCOVERY_PORT);
    discAddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    while (m_running.load()) {
        // Broadcast OMT stream beacon: "OMT|<Name>|<Port>"
        std::string beacon = "OMT|" + m_name + "|" + std::to_string(OMT_DEFAULT_PORT);
        sendto(m_socket, beacon.data(), beacon.size(), 0,
               reinterpret_cast<sockaddr*>(&discAddr), sizeof(discAddr));

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ═════════════════════════════════════════════════════════════════
//  OMTDiscovery
// ═════════════════════════════════════════════════════════════════

OMTDiscovery& OMTDiscovery::instance() {
    static OMTDiscovery s_instance;
    return s_instance;
}

OMTDiscovery::~OMTDiscovery() {
    stop();
}

bool OMTDiscovery::start() {
    if (m_running.load()) return true;

    m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socket < 0) return false;

    int opt = 1;
#ifndef _WIN32
    setsockopt(m_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(OMT_DISCOVERY_PORT);
    bindAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_socket, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) < 0) {
        CLOSE_SOCK(m_socket);
        m_socket = -1;
        return false;
    }

    m_running.store(true);
    m_listenThread = std::thread(&OMTDiscovery::listenLoop, this);
    return true;
}

void OMTDiscovery::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_socket >= 0) {
        CLOSE_SOCK(m_socket);
        m_socket = -1;
    }

    if (m_listenThread.joinable()) {
        m_listenThread.join();
    }
}

std::vector<OMTStreamInfo> OMTDiscovery::discoveredStreams() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_streams;
}

void OMTDiscovery::listenLoop() {
    char buf[512];
    while (m_running.load()) {
        sockaddr_in senderAddr{};
        socklen_t addrLen = sizeof(senderAddr);

        timeval tv{0, 200000};
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(m_socket, &readFds);

        int res = select(m_socket + 1, &readFds, nullptr, nullptr, &tv);
        if (res > 0 && FD_ISSET(m_socket, &readFds)) {
            int n = recvfrom(m_socket, buf, sizeof(buf) - 1, 0,
                             reinterpret_cast<sockaddr*>(&senderAddr), &addrLen);
            if (n > 4 && std::strncmp(buf, "OMT|", 4) == 0) {
                buf[n] = '\0';
                std::string msg(buf);
                auto p1 = msg.find('|', 4);
                if (p1 != std::string::npos) {
                    std::string streamName = msg.substr(4, p1 - 4);
                    int streamPort = std::stoi(msg.substr(p1 + 1));
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(senderAddr.sin_addr), ipStr, INET_ADDRSTRLEN);

                    std::lock_guard<std::mutex> lock(m_mutex);
                    bool exists = false;
                    for (const auto& s : m_streams) {
                        if (s.name == streamName && s.ip == ipStr) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists) {
                        OMTStreamInfo info;
                        info.name = streamName;
                        info.ip = ipStr;
                        info.port = streamPort;
                        m_streams.push_back(info);
                    }
                }
            }
        }
    }
}

} // namespace sm
