/**
 * sm::FFmpegSource — Implementation
 *
 * Decode loop:
 *   av_read_frame() → avcodec_send_packet() → avcodec_receive_frame()
 *   → convert → push VideoFrame / AudioFrame callbacks
 */

#include "streammix/ffmpeg_source.h"
#include "streammix/frame.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavdevice/avdevice.h>
}

#include <stdexcept>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <filesystem>

namespace sm {

// ─── Constructor ─────────────────────────────────────────────────
FFmpegSource::FFmpegSource(const std::string& name)
    : Source(name, SourceType::Unknown)
{
    m_id = s_nextId.fetch_add(1);
    // Register all demuxers, decoders, and device drivers once
    avdevice_register_all();
}

// ─── Destructor ──────────────────────────────────────────────────
FFmpegSource::~FFmpegSource() {
    stop();
}

// ─── start() ─────────────────────────────────────────────────────
bool FFmpegSource::start(const SourceSettings& settings) {
    if (m_running.load()) return false;

    m_loop = settings.loop;

    if (!openInput(settings)) {
        m_state = SourceState::Error;
        return false;
    }
    if (m_video_stream_idx >= 0 && !openVideoDecoder()) {
        m_state = SourceState::Error;
        return false;
    }
    if (m_audio_stream_idx >= 0) {
        openAudioDecoder(); // soft-fail: audio missing is non-fatal
    }

    m_running = true;
    m_state   = SourceState::Running;
    m_decode_thread = std::thread(&FFmpegSource::decodeLoop, this);
    return true;
}

// ─── stop() ──────────────────────────────────────────────────────
void FFmpegSource::stop() {
    m_running = false;
    if (m_decode_thread.joinable()) m_decode_thread.join();

    if (m_sws_ctx) { sws_freeContext(m_sws_ctx); m_sws_ctx = nullptr; }
    if (m_swr_ctx) { swr_free(&m_swr_ctx); }
    if (m_vid_ctx) { avcodec_free_context(&m_vid_ctx); }
    if (m_aud_ctx) { avcodec_free_context(&m_aud_ctx); }
    if (m_fmt_ctx) { avformat_close_input(&m_fmt_ctx); }

    m_state = SourceState::Stopped;
}

// ─── Transport & Playback ────────────────────────────────────────
void FFmpegSource::pause() {
    m_paused.store(true);
}

void FFmpegSource::resume() {
    m_paused.store(false);
}

void FFmpegSource::seek(double seconds) {
    m_seekTargetSec.store(std::max(0.0, seconds));
    m_seekRequested.store(true);
}

// ─── openInput() ─────────────────────────────────────────────────
bool FFmpegSource::openInput(const SourceSettings& settings) {
    AVDictionary* opts = nullptr;

    // ── Platform-specific device input formats ──
#if defined(__APPLE__)
    const char* device_fmt = "avfoundation";
#elif defined(_WIN32)
    const char* device_fmt = "dshow";
#else
    const char* device_fmt = "v4l2";
#endif

    // Detect if this is a local device path vs a URL/file
    bool is_file = false;
    try {
        is_file = std::filesystem::exists(settings.url);
    } catch (...) {}

    bool is_url = (settings.url.find("://") != std::string::npos);

    bool is_device = !is_file && !is_url && (
        settings.url == "default" ||
        settings.url == "0" ||
        settings.url == "1" ||
        settings.url.find("Camera") != std::string::npos ||
        settings.url.find("screen") != std::string::npos ||
        (settings.url.find(':') != std::string::npos && !is_file)
    );

    const AVInputFormat* ifmt = nullptr;
    if (is_device) {
        ifmt = av_find_input_format(device_fmt);
    }

    // Low-latency options for network sources
    if (!is_device) {
        av_dict_set(&opts, "rtsp_transport", "tcp",    0);
        av_dict_set(&opts, "stimeout",       "5000000", 0); // 5s connect timeout
        av_dict_set(&opts, "fflags",         "nobuffer", 0);
        av_dict_set(&opts, "flags",          "low_delay", 0);
    }

    int ret = avformat_open_input(&m_fmt_ctx, settings.url.c_str(), ifmt, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "[FFmpegSource:" << m_name << "] avformat_open_input failed: " << errbuf << "\n";
        return false;
    }

    ret = avformat_find_stream_info(m_fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "[FFmpegSource:" << m_name << "] Could not find stream info\n";
        return false;
    }

    // Find best video and audio streams
    m_video_stream_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_audio_stream_idx = av_find_best_stream(m_fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (m_fmt_ctx && m_fmt_ctx->duration > 0) {
        m_durationSec.store(static_cast<double>(m_fmt_ctx->duration) / AV_TIME_BASE);
    }

    // Determine source type from URL scheme
    const std::string& url = settings.url;
    if (url.starts_with("rtsp://"))  m_type = SourceType::RTSP;
    else if (url.starts_with("rtmp://")) m_type = SourceType::RTMP;
    else if (url.starts_with("srt://"))  m_type = SourceType::SRT;
    else if (is_device)                  m_type = SourceType::Camera;
    else                                 m_type = SourceType::File;

    return m_video_stream_idx >= 0;
}

// ─── openVideoDecoder() ──────────────────────────────────────────
bool FFmpegSource::openVideoDecoder() {
    AVStream* st = m_fmt_ctx->streams[m_video_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        std::cerr << "[FFmpegSource:" << m_name << "] No decoder for video stream\n";
        return false;
    }

    m_vid_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_vid_ctx, st->codecpar);

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "threads", "auto", 0);

    if (avcodec_open2(m_vid_ctx, codec, &opts) < 0) {
        av_dict_free(&opts);
        std::cerr << "[FFmpegSource:" << m_name << "] avcodec_open2 (video) failed\n";
        return false;
    }
    av_dict_free(&opts);

    m_width  = m_vid_ctx->width;
    m_height = m_vid_ctx->height;

    AVRational tb = st->avg_frame_rate;
    m_fps = (tb.den > 0) ? (double)tb.num / tb.den : 30.0;

    // Allocate software scaler (any pix fmt → RGBA)
    m_sws_ctx = sws_getContext(
        m_width, m_height, m_vid_ctx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGBA,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );
    if (!m_sws_ctx) {
        std::cerr << "[FFmpegSource:" << m_name << "] sws_getContext failed\n";
        return false;
    }

    return true;
}

// ─── openAudioDecoder() ──────────────────────────────────────────
bool FFmpegSource::openAudioDecoder() {
    AVStream* st = m_fmt_ctx->streams[m_audio_stream_idx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) return false;

    m_aud_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_aud_ctx, st->codecpar);
    if (avcodec_open2(m_aud_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&m_aud_ctx);
        return false;
    }

    // Software resampler → float interleaved stereo @ 48kHz
    m_swr_ctx = swr_alloc();
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    swr_alloc_set_opts2(&m_swr_ctx,
        &stereo,              AV_SAMPLE_FMT_FLTP, 48000,
        &m_aud_ctx->ch_layout, m_aud_ctx->sample_fmt, m_aud_ctx->sample_rate,
        0, nullptr
    );
    swr_init(m_swr_ctx);
    return true;
}

// ─── decodeLoop() ────────────────────────────────────────────────
void FFmpegSource::decodeLoop() {
    const auto target_interval = std::chrono::microseconds(
        m_fps > 0.0 ? static_cast<int64_t>(1000000.0 / m_fps) : 33333
    );
    auto next_frame_time = std::chrono::steady_clock::now();
    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();

    while (m_running.load()) {
        // Handle Seek Request
        if (m_seekRequested.exchange(false)) {
            double target = m_seekTargetSec.load();
            int64_t targetTs = static_cast<int64_t>(target * AV_TIME_BASE);
            av_seek_frame(m_fmt_ctx, -1, targetTs, AVSEEK_FLAG_BACKWARD);
            if (m_vid_ctx) avcodec_flush_buffers(m_vid_ctx);
            if (m_aud_ctx) avcodec_flush_buffers(m_aud_ctx);
            m_positionSec.store(target);
            next_frame_time = std::chrono::steady_clock::now();
        }

        // Handle Paused state
        if (m_paused.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        int ret = av_read_frame(m_fmt_ctx, pkt);

        if (ret == AVERROR_EOF) {
            if (m_loop) {
                // Seek back to start
                av_seek_frame(m_fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
                if (m_vid_ctx) avcodec_flush_buffers(m_vid_ctx);
                if (m_aud_ctx) avcodec_flush_buffers(m_aud_ctx);
                m_positionSec.store(0.0);
                next_frame_time = std::chrono::steady_clock::now();
                continue;
            } else {
                // Not looping: stay on final frame
                m_paused.store(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
        }

        if (ret < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        // ── Video packet ──────────────────────────────────────
        if (pkt->stream_index == m_video_stream_idx && m_vid_ctx) {
            if (avcodec_send_packet(m_vid_ctx, pkt) == 0) {
                while (avcodec_receive_frame(m_vid_ctx, frame) == 0) {
                    if (m_video_stream_idx >= 0 && m_fmt_ctx->streams[m_video_stream_idx]) {
                        auto tb = m_fmt_ctx->streams[m_video_stream_idx]->time_base;
                        if (frame->pts != AV_NOPTS_VALUE) {
                            m_positionSec.store(static_cast<double>(frame->pts) * av_q2d(tb));
                        }
                    }
                    if (auto vf = convertVideoFrame(frame)) {
                        pushVideo(std::move(vf));
                        if (m_type == SourceType::File) {
                            next_frame_time += target_interval;
                            std::this_thread::sleep_until(next_frame_time);
                        }
                    }
                    av_frame_unref(frame);
                }
            }
        }
        // ── Audio packet ──────────────────────────────────────
        else if (pkt->stream_index == m_audio_stream_idx && m_aud_ctx) {
            if (avcodec_send_packet(m_aud_ctx, pkt) == 0) {
                while (avcodec_receive_frame(m_aud_ctx, frame) == 0) {
                    if (auto af = convertAudioFrame(frame)) {
                        pushAudio(std::move(af));
                    }
                    av_frame_unref(frame);
                }
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    m_running = false;
    m_state   = SourceState::Stopped;
}

// ─── convertVideoFrame() ─────────────────────────────────────────
VideoFramePtr FFmpegSource::convertVideoFrame(AVFrame* av_frame) {
    if (!m_sws_ctx) return nullptr;

    auto vf = std::make_shared<VideoFrame>();
    vf->width    = m_width;
    vf->height   = m_height;
    vf->linesize = m_width * 4;
    vf->format   = PixelFormat::RGBA8;
    vf->pts_us   = av_frame->pts; // will be properly scaled caller-side
    vf->data.resize(static_cast<size_t>(m_width * m_height * 4));

    uint8_t* dst[4]  = { vf->data.data(), nullptr, nullptr, nullptr };
    int dst_ls[4]    = { vf->linesize, 0, 0, 0 };

    sws_scale(m_sws_ctx,
        av_frame->data, av_frame->linesize, 0, m_height,
        dst, dst_ls
    );

    return vf;
}

// ─── convertAudioFrame() ─────────────────────────────────────────
AudioFramePtr FFmpegSource::convertAudioFrame(AVFrame* av_frame) {
    if (!m_swr_ctx) return nullptr;

    auto af = std::make_shared<AudioFrame>();
    af->channels    = 2;
    af->sample_rate = 48000;
    af->frames      = av_frame->nb_samples;
    af->samples.resize(static_cast<size_t>(af->frames * af->channels));

    uint8_t* out_data = reinterpret_cast<uint8_t*>(af->samples.data());
    const uint8_t** in_data = const_cast<const uint8_t**>(av_frame->extended_data);

    swr_convert(m_swr_ctx,
        &out_data, af->frames,
        in_data,   av_frame->nb_samples
    );

    return af;
}

} // namespace sm
