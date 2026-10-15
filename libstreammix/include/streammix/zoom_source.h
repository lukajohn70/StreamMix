#pragma once
/**
 * sm::ZoomSource & sm::ZoomMeetingManager — Native Zoom C++ Integration
 *
 * Implements:
 *   - Native Zoom Meeting SDK multi-caller ingest (vMix 27–29 standard)
 *   - Individual participant isolation into independent switcher inputs
 *   - Active Speaker and Screen Share dynamic tracking
 *   - Raw YUV420/I420 video frame conversion to sm::VideoFrame (RGBA8)
 *   - Isolated raw PCM audio capture per caller for the AudioMixer
 *   - Bi-directional Return Feed: Sends Program Video & Mix-Minus Audio back to Zoom
 */

#include "streammix/source.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>

namespace sm {

enum class ZoomMeetingState {
    Disconnected,
    Connecting,
    InMeeting,
    Reconnecting,
    Failed
};

enum class ZoomTargetMode {
    SpecificUser,   ///< Locked to a specific participant ID or name
    ActiveSpeaker,  ///< Follows whichever participant is talking
    ScreenShare     ///< Ingests the meeting's active desktop/slide share
};

struct ZoomParticipant {
    uint32_t userId = 0;
    std::string userName;
    bool isHost = false;
    bool isVideoOn = false;
    bool isAudioMuted = true;
    bool isSharing = false;
    bool isTalking = false;
};

struct ZoomMeetingCredentials {
    std::string meetingNumber;
    std::string passcode;
    std::string displayName = "StreamMix Production";
    std::string sdkJwtToken;
    std::string sdkKey;
    std::string sdkSecret;
    bool hdVideo = true; ///< Request 720p/1080p stream from Zoom
};

class ZoomMeetingManager;

class ZoomSource final : public Source {
public:
    explicit ZoomSource(const std::string& name, ZoomTargetMode mode = ZoomTargetMode::ActiveSpeaker);
    ~ZoomSource() override;

    bool start(const SourceSettings& settings) override;
    void stop() override;

    // ─── Target Configuration ────────────────────────────────────
    void setTargetMode(ZoomTargetMode mode);
    ZoomTargetMode targetMode() const { return m_targetMode; }

    void setTargetUserId(uint32_t userId);
    uint32_t targetUserId() const { return m_targetUserId; }

    void setTargetUserName(const std::string& name);
    const std::string& targetUserName() const { return m_targetUserName; }

    ZoomParticipant currentParticipant() const;

    // ─── Raw Data Ingestion from SDK ─────────────────────────────
    void handleRawVideo(const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
                        int width, int height, int strideY, int strideUV, int64_t pts_us = 0);

    void handleRawAudio(const float* pcmSamples, int frameCount, int sampleRate = 48000,
                        int channels = 2, int64_t pts_us = 0);

private:
    ZoomTargetMode m_targetMode;
    uint32_t m_targetUserId = 0;
    std::string m_targetUserName;
    mutable std::mutex m_sourceMutex;
    ZoomParticipant m_participantInfo;
    std::atomic<bool> m_running { false };
};

class ZoomMeetingManager {
public:
    static ZoomMeetingManager& instance();

    bool initializeSDK(const std::string& sdkKey = "", const std::string& sdkSecret = "");
    bool joinMeeting(const ZoomMeetingCredentials& creds);
    void leaveMeeting();

    ZoomMeetingState state() const { return m_state.load(); }
    std::vector<ZoomParticipant> participants() const;
    uint32_t activeSpeakerId() const { return m_activeSpeakerId.load(); }
    uint32_t screenShareUserId() const { return m_screenShareUserId.load(); }

    // ─── Input Creation ──────────────────────────────────────────
    std::shared_ptr<ZoomSource> createZoomSource(const std::string& name,
                                                 ZoomTargetMode mode = ZoomTargetMode::ActiveSpeaker,
                                                 uint32_t targetUserId = 0);

    void unregisterZoomSource(uint64_t sourceId);

    // ─── Bi-Directional Return Feeds to Zoom ──────────────────────
    /// Feeds Program video back to Zoom meeting as local camera
    void sendReturnVideo(VideoFramePtr programFrame);

    /// Feeds Mix-Minus audio back to Zoom meeting as local mic
    void sendReturnAudio(AudioFramePtr mixMinusFrame);

    // ─── Internal Dispatchers from SDK Callbacks ──────────────────
    void dispatchRawVideo(uint32_t userId, const uint8_t* yPlane, const uint8_t* uPlane,
                          const uint8_t* vPlane, int width, int height, int strideY, int strideUV);
    void dispatchRawAudio(uint32_t userId, const float* pcmSamples, int frameCount,
                          int sampleRate, int channels);
    void dispatchParticipantList(const std::vector<ZoomParticipant>& list);
    void dispatchActiveSpeaker(uint32_t userId);
    void dispatchScreenShare(uint32_t userId, bool isSharing);

private:
    ZoomMeetingManager();
    ~ZoomMeetingManager();

    std::atomic<ZoomMeetingState> m_state { ZoomMeetingState::Disconnected };
    ZoomMeetingCredentials m_creds;

    mutable std::mutex m_mutex;
    std::unordered_map<uint32_t, ZoomParticipant> m_participants;
    std::unordered_map<uint64_t, std::shared_ptr<ZoomSource>> m_activeSources;

    std::atomic<uint32_t> m_activeSpeakerId { 0 };
    std::atomic<uint32_t> m_screenShareUserId { 0 };
};

} // namespace sm
