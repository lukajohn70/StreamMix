#include "streammix/ndi_output.h"

namespace sm {

NDIOutput::NDIOutput(const std::string& senderName)
    : Output(senderName, OutputType::NDI)
    , m_senderName(senderName)
{
}

NDIOutput::~NDIOutput() {
    stop();
}

bool NDIOutput::start(const OutputSettings& settings) {
    m_active.store(true);
    m_state = OutputState::Streaming;
    return true;
}

void NDIOutput::stop() {
    m_active.store(false);
    m_state = OutputState::Stopped;
}

void NDIOutput::pushVideo(VideoFramePtr frame) {
    if (!m_active.load() || !frame) return;
    m_framesSent.fetch_add(1);
}

void NDIOutput::pushAudio(AudioFramePtr frame) {
    if (!m_active.load() || !frame) return;
}

NDITallyState NDIOutput::tallyState() const {
    std::lock_guard<std::mutex> lock(m_tallyMutex);
    return m_tally;
}

void NDIOutput::simulateExternalTally(bool program, bool preview) {
    NDITallyCallback cb;
    {
        std::lock_guard<std::mutex> lock(m_tallyMutex);
        m_tally.onProgram = program;
        m_tally.onPreview = preview;
        cb = m_tallyCb;
    }
    if (cb) {
        cb(m_tally);
    }
}

} // namespace sm
