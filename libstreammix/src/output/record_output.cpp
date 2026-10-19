#include "streammix/record_output.h"
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace sm {

struct RecordOutput::Impl {
    std::mutex queueMutex;
    std::deque<VideoFramePtr> videoQueue;
    std::deque<AudioFramePtr> audioQueue;
    std::thread workerThread;
    std::atomic<bool> running { false };

    std::string outputPath;
    std::atomic<uint64_t> bytesWritten { 0 };
    std::chrono::steady_clock::time_point startTime;

    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext*  vidCtx = nullptr;
    AVStream*        vidStream = nullptr;
    SwsContext*      swsCtx = nullptr;
    AVFrame*         yuvFrame = nullptr;
    int64_t          frameIndex = 0;
};

RecordOutput::RecordOutput(const std::string& name)
    : Output(name, OutputType::FileRecord), m_impl(std::make_unique<Impl>())
{
}

RecordOutput::~RecordOutput() {
    stop();
}

bool RecordOutput::start(const OutputSettings& settings) {
    if (m_impl->running.load()) return false;

    m_settings = settings;
    m_impl->outputPath = settings.url;

    std::error_code ec;
    std::filesystem::path p(m_impl->outputPath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // Allocate output format context
    int ret = avformat_alloc_output_context2(&m_impl->fmtCtx, nullptr, nullptr, m_impl->outputPath.c_str());
    if (ret < 0 || !m_impl->fmtCtx) {
        std::cerr << "[RecordOutput] avformat_alloc_output_context2 failed for: " << m_impl->outputPath << "\n";
        return false;
    }

    // Find H.264 video encoder
    const AVCodec* vCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vCodec) {
        vCodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    }

    if (vCodec) {
        m_impl->vidStream = avformat_new_stream(m_impl->fmtCtx, vCodec);
        m_impl->vidCtx = avcodec_alloc_context3(vCodec);

        m_impl->vidCtx->width = m_settings.width > 0 ? m_settings.width : 1920;
        m_impl->vidCtx->height = m_settings.height > 0 ? m_settings.height : 1080;
        m_impl->vidCtx->time_base = AVRational{ 1, static_cast<int>(m_settings.fps > 0 ? m_settings.fps : 60) };
        m_impl->vidCtx->framerate = AVRational{ static_cast<int>(m_settings.fps > 0 ? m_settings.fps : 60), 1 };
        m_impl->vidCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        m_impl->vidCtx->bit_rate = static_cast<int64_t>(m_settings.videoBitrateKbps > 0 ? m_settings.videoBitrateKbps : 15000) * 1000;
        m_impl->vidCtx->gop_size = 30;

        if (m_impl->fmtCtx->oformat->flags & AVFMT_GLOBALHEADER) {
            m_impl->vidCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        av_opt_set(m_impl->vidCtx->priv_data, "preset", "veryfast", 0);
        av_opt_set(m_impl->vidCtx->priv_data, "tune", "zerolatency", 0);

        if (avcodec_open2(m_impl->vidCtx, vCodec, nullptr) >= 0) {
            avcodec_parameters_from_context(m_impl->vidStream->codecpar, m_impl->vidCtx);
            m_impl->vidStream->time_base = m_impl->vidCtx->time_base;

            m_impl->yuvFrame = av_frame_alloc();
            m_impl->yuvFrame->format = m_impl->vidCtx->pix_fmt;
            m_impl->yuvFrame->width = m_impl->vidCtx->width;
            m_impl->yuvFrame->height = m_impl->vidCtx->height;
            av_frame_get_buffer(m_impl->yuvFrame, 0);

            m_impl->swsCtx = sws_getContext(
                m_impl->vidCtx->width, m_impl->vidCtx->height, AV_PIX_FMT_RGBA,
                m_impl->vidCtx->width, m_impl->vidCtx->height, m_impl->vidCtx->pix_fmt,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
        }
    }

    if (!(m_impl->fmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_impl->fmtCtx->pb, m_impl->outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "[RecordOutput] avio_open failed for " << m_impl->outputPath << "\n";
            return false;
        }
    }

    ret = avformat_write_header(m_impl->fmtCtx, nullptr);
    if (ret < 0) {
        std::cerr << "[RecordOutput] avformat_write_header failed\n";
        return false;
    }

    m_impl->frameIndex = 0;
    m_impl->bytesWritten = 0;
    m_impl->startTime = std::chrono::steady_clock::now();
    m_impl->running.store(true);
    m_state = OutputState::Streaming;

    m_impl->workerThread = std::thread([this]() {
        AVPacket* pkt = av_packet_alloc();

        while (m_impl->running.load()) {
            VideoFramePtr vFrame;
            {
                std::lock_guard<std::mutex> lock(m_impl->queueMutex);
                if (!m_impl->videoQueue.empty()) {
                    vFrame = m_impl->videoQueue.front();
                    m_impl->videoQueue.pop_front();
                }
            }

            if (vFrame && m_impl->vidCtx && m_impl->swsCtx && m_impl->yuvFrame) {
                const uint8_t* srcSlice[4] = { vFrame->data.data(), nullptr, nullptr, nullptr };
                int srcStride[4] = { vFrame->linesize, 0, 0, 0 };

                sws_scale(m_impl->swsCtx, srcSlice, srcStride, 0, vFrame->height,
                          m_impl->yuvFrame->data, m_impl->yuvFrame->linesize);

                m_impl->yuvFrame->pts = m_impl->frameIndex++;

                if (avcodec_send_frame(m_impl->vidCtx, m_impl->yuvFrame) >= 0) {
                    while (avcodec_receive_packet(m_impl->vidCtx, pkt) >= 0) {
                        av_packet_rescale_ts(pkt, m_impl->vidCtx->time_base, m_impl->vidStream->time_base);
                        pkt->stream_index = m_impl->vidStream->index;
                        m_impl->bytesWritten += pkt->size;
                        av_interleaved_write_frame(m_impl->fmtCtx, pkt);
                        av_packet_unref(pkt);
                    }
                }
                m_stats.totalFramesSent++;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Flush video encoder
        if (m_impl->vidCtx) {
            avcodec_send_frame(m_impl->vidCtx, nullptr);
            while (avcodec_receive_packet(m_impl->vidCtx, pkt) >= 0) {
                av_packet_rescale_ts(pkt, m_impl->vidCtx->time_base, m_impl->vidStream->time_base);
                pkt->stream_index = m_impl->vidStream->index;
                m_impl->bytesWritten += pkt->size;
                av_interleaved_write_frame(m_impl->fmtCtx, pkt);
                av_packet_unref(pkt);
            }
        }

        av_packet_free(&pkt);

        if (m_impl->fmtCtx) {
            av_write_trailer(m_impl->fmtCtx);
        }
    });

    return true;
}

void RecordOutput::stop() {
    if (!m_impl->running.load()) return;
    m_impl->running.store(false);

    if (m_impl->workerThread.joinable()) {
        m_impl->workerThread.join();
    }

    if (m_impl->swsCtx) { sws_freeContext(m_impl->swsCtx); m_impl->swsCtx = nullptr; }
    if (m_impl->yuvFrame) { av_frame_free(&m_impl->yuvFrame); }
    if (m_impl->vidCtx) { avcodec_free_context(&m_impl->vidCtx); }
    if (m_impl->fmtCtx) {
        if (!(m_impl->fmtCtx->oformat->flags & AVFMT_NOFILE) && m_impl->fmtCtx->pb) {
            avio_closep(&m_impl->fmtCtx->pb);
        }
        avformat_free_context(m_impl->fmtCtx);
        m_impl->fmtCtx = nullptr;
    }

    m_state = OutputState::Stopped;
}

void RecordOutput::pushVideo(VideoFramePtr frame) {
    if (!m_impl->running.load() || !frame) return;
    std::lock_guard<std::mutex> lock(m_impl->queueMutex);
    if (m_impl->videoQueue.size() < 120) {
        m_impl->videoQueue.push_back(frame);
    } else {
        m_stats.droppedFrames++;
    }
}

void RecordOutput::pushAudio(AudioFramePtr /*frame*/) {
    // Audio encoding can be multiplexed into container
}

OutputStats RecordOutput::stats() const {
    OutputStats s = m_stats;
    if (m_impl->running.load()) {
        auto now = std::chrono::steady_clock::now();
        s.uptimeSeconds = std::chrono::duration_cast<std::chrono::seconds>(now - m_impl->startTime).count();
        if (s.uptimeSeconds > 0) {
            s.currentBitrateKbps = (m_impl->bytesWritten.load() * 8.0) / (s.uptimeSeconds * 1000.0);
        }
    }
    return s;
}

std::string RecordOutput::currentFilePath() const {
    return m_impl->outputPath;
}

uint64_t RecordOutput::bytesRecorded() const {
    return m_impl->bytesWritten.load();
}

} // namespace sm
