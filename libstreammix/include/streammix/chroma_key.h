#pragma once
/**
 * sm::ChromaKeyFilter — Broadcast-Grade Chroma Keyer & Despill Engine
 *
 * Implements:
 *   - Real-time Green / Blue / Custom RGB keying
 *   - Euclidean / Color-Difference chroma distance metric
 *   - Smooth edge anti-aliasing and feathering (soft alpha transition)
 *   - Green / Blue spill suppression (despill) algorithm to neutralize edge halos
 *   - Thread-safe in-place RGBA processing
 */

#include "streammix/frame.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace sm {

struct ChromaKeyConfig {
    bool enabled = true;
    uint8_t keyR = 0;       ///< Target key Color: Red (default pure green screen)
    uint8_t keyG = 255;     ///< Target key Color: Green
    uint8_t keyB = 0;       ///< Target key Color: Blue

    float similarity = 0.40f;   ///< Tolerance / threshold (0.0 to 1.0)
    float smoothness = 0.10f;   ///< Edge feathering falloff (0.0 to 1.0)
    float spill = 0.60f;        ///< Spill suppression strength (0.0 to 1.0)
};

class ChromaKeyFilter {
public:
    explicit ChromaKeyFilter(const ChromaKeyConfig& config = ChromaKeyConfig{});
    ~ChromaKeyFilter() = default;

    void setConfig(const ChromaKeyConfig& config);
    const ChromaKeyConfig& config() const { return m_config; }

    /// Process an RGBA video frame in-place, updating alpha channel and despill
    void processFrame(VideoFramePtr frame) const;

    /// Process input frame and return a new key-filtered RGBA frame
    VideoFramePtr applyKey(const VideoFramePtr& input) const;

private:
    ChromaKeyConfig m_config;
};

} // namespace sm
