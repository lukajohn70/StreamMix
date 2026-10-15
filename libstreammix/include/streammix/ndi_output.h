#pragma once
/**
 * sm::NDIOutput — NDI® 6 Network Output Transmitter (vMix 29)
 *
 * Broadcasts StreamMix Program or Preview output as a high-bandwidth NDI 6 stream
 * accessible by any TriCaster, vMix, OBS, or NDI Studio Monitor on the local network.
 */

#include "streammix/output.h"
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

namespace sm {

struct NDITallyState {
    bool onProgram = false;
    bool onPreview = false;
};

using NDITallyCallback = std::function<void(const NDITallyState&)>;

class NDIOutput : public Output {
public:
    explicit NDIOutput(const std::string& senderName = "StreamMix - Output 1");
    ~NDIOutput() override;

    bool start(const OutputSettings& settings) override;
    void stop() override;

    void pushVideo(VideoFramePtr frame) override;
    void pushAudio(AudioFramePtr frame) override;

    // ─── Tally Feedback from External Receivers ──────────────────
    NDITallyState tallyState() const;
    void setTallyCallback(NDITallyCallback cb) { m_tallyCb = std::move(cb); }
    void simulateExternalTally(bool program, bool preview);

    std::string senderName() const { return m_senderName; }
    void setSenderName(const std::string& name) { m_senderName = name; }

    uint64_t framesSent() const { return m_framesSent.load(); }

private:
    std::string m_senderName;
    std::atomic<bool> m_active{false};
    std::atomic<uint64_t> m_framesSent{0};

    mutable std::mutex m_tallyMutex;
    NDITallyState m_tally;
    NDITallyCallback m_tallyCb;
};

} // namespace sm
