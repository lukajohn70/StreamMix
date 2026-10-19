#pragma once
/**
 * sm::RecordOutput — Native Master Broadcast Recording Engine
 *
 * Implements:
 *   - Real MP4 / MOV / MKV recording to local disk
 *   - Multi-threaded encoding worker queue
 *   - VideoToolbox / H.264 video compression
 *   - AAC stereo audio compression
 *   - Recording duration, file size, and frame statistics
 */

#include "streammix/output.h"
#include <memory>
#include <string>

namespace sm {

class RecordOutput final : public Output {
public:
    explicit RecordOutput(const std::string& name = "Master Recorder");
    ~RecordOutput() override;

    bool start(const OutputSettings& settings) override;
    void stop() override;

    void pushVideo(VideoFramePtr frame) override;
    void pushAudio(AudioFramePtr frame) override;

    OutputStats stats() const override;
    std::string currentFilePath() const;
    uint64_t    bytesRecorded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace sm
