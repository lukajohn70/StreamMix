#pragma once
/**
 * sm::VideoFrame
 *
 * The unified internal video frame passed between all pipeline stages.
 * All sources (FFmpeg, NDI, screen-cap, etc.) must convert to this format.
 */

#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <memory>

namespace sm {

enum class PixelFormat {
    RGBA8,      ///< 32-bit RGBA, packed, no padding
    BGRA8,      ///< 32-bit BGRA (common on Windows GDI / DX surfaces)
    YUV420P,    ///< Planar YUV 4:2:0 (standard for H.264 decode)
    NV12,       ///< Semi-planar YUV 4:2:0 (NVDEC / VideoToolbox output)
    Unknown
};

struct VideoFrame {
    std::vector<uint8_t> data;      ///< Raw pixel bytes
    int   width      = 0;
    int   height     = 0;
    int   linesize   = 0;           ///< Bytes per row (may include padding)
    PixelFormat format = PixelFormat::Unknown;
    int64_t pts_us   = 0;           ///< Presentation timestamp (microseconds)
    uint64_t seq     = 0;           ///< Monotonic frame sequence number

    bool empty() const { return data.empty() || width == 0 || height == 0; }

    /// Convenience: allocate RGBA frame of given dimensions
    static VideoFrame makeRGBA(int w, int h) {
        VideoFrame f;
        f.width    = w;
        f.height   = h;
        f.linesize = w * 4;
        f.format   = PixelFormat::RGBA8;
        f.data.resize(static_cast<size_t>(w * h * 4), 0);
        return f;
    }
};

using VideoFramePtr = std::shared_ptr<VideoFrame>;

// ─── Audio Frame ────────────────────────────────────────────────

struct AudioFrame {
    std::vector<float> samples;     ///< Interleaved float PCM (L,R,L,R,...)
    int   channels       = 2;
    int   sample_rate    = 48000;
    int   frames         = 0;       ///< Number of audio frames (samples/channels)
    int64_t pts_us       = 0;
};

using AudioFramePtr = std::shared_ptr<AudioFrame>;

} // namespace sm
