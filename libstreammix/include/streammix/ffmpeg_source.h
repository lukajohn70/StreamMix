#pragma once
/**
 * sm::FFmpegSource — Phase 1 capture module
 *
 * Handles:
 *   - USB webcams    (v4l2 / AVFoundation / DirectShow)
 *   - Video files    (MP4, MOV, AVI, MXF, ...)
 *   - RTSP streams
 *   - RTMP ingest
 *   - SRT streams
 *
 * Uses a dedicated decode thread that delivers VideoFrame + AudioFrame
 * callbacks to the compositor at native source frame-rate.
 */

#include "streammix/source.h"
#include <thread>
#include <atomic>
#include <string>

// Forward-declare FFmpeg types (avoid pulling all FFmpeg headers into public API)
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

namespace sm {

class FFmpegSource final : public Source {
public:
    explicit FFmpegSource(const std::string& name);
    ~FFmpegSource() override;

    bool start(const SourceSettings& settings) override;
    void stop()  override;

private:
    bool openInput(const SourceSettings& settings);
    bool openVideoDecoder();
    bool openAudioDecoder();
    void decodeLoop();

    // Convert AVFrame → sm::VideoFrame (RGBA)
    VideoFramePtr convertVideoFrame(AVFrame* av_frame);
    // Convert AVFrame → sm::AudioFrame (float PCM)
    AudioFramePtr convertAudioFrame(AVFrame* av_frame);

    // FFmpeg contexts
    AVFormatContext* m_fmt_ctx   = nullptr;
    AVCodecContext*  m_vid_ctx   = nullptr;
    AVCodecContext*  m_aud_ctx   = nullptr;
    SwsContext*      m_sws_ctx   = nullptr;
    SwrContext*      m_swr_ctx   = nullptr;

    int m_video_stream_idx = -1;
    int m_audio_stream_idx = -1;
    bool m_loop = false;

    std::thread      m_decode_thread;
    std::atomic<bool> m_running { false };
};

} // namespace sm
