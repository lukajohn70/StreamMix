/**
 * sm::ChromaKeyFilter — Implementation
 *
 * Real-time SIMD-friendly Chroma Keying with Green/Blue spill suppression.
 */

#include "streammix/chroma_key.h"
#include <cmath>
#include <algorithm>

namespace sm {

ChromaKeyFilter::ChromaKeyFilter(const ChromaKeyConfig& config)
    : m_config(config)
{
}

void ChromaKeyFilter::setConfig(const ChromaKeyConfig& config) {
    m_config = config;
}

void ChromaKeyFilter::processFrame(VideoFramePtr frame) const {
    if (!frame || frame->empty() || !m_config.enabled) return;

    const float kr = m_config.keyR / 255.0f;
    const float kg = m_config.keyG / 255.0f;
    const float kb = m_config.keyB / 255.0f;

    const float sim = std::clamp(m_config.similarity, 0.01f, 1.0f);
    const float smooth = std::clamp(m_config.smoothness, 0.001f, 0.5f);
    const float spillAmt = std::clamp(m_config.spill, 0.0f, 1.0f);

    const bool isGreenKey = (m_config.keyG > m_config.keyR && m_config.keyG > m_config.keyB);
    const bool isBlueKey = (m_config.keyB > m_config.keyR && m_config.keyB > m_config.keyG);

    uint8_t* p = frame->data.data();
    const size_t pixelCount = frame->data.size() / 4;

    for (size_t i = 0; i < pixelCount; ++i) {
        float r = p[0] / 255.0f;
        float g = p[1] / 255.0f;
        float b = p[2] / 255.0f;

        // Euclidean distance in normalized RGB
        float dr = r - kr;
        float dg = g - kg;
        float db = b - kb;
        float dist = std::sqrt(dr * dr + dg * dg + db * db) / 1.7320508f; // normalize by sqrt(3)

        // Alpha calculation with smooth falloff
        float alpha = 1.0f;
        if (dist <= sim) {
            alpha = 0.0f;
        } else if (dist < sim + smooth) {
            alpha = (dist - sim) / smooth;
        }

        // Green / Blue spill suppression
        if (alpha > 0.0f && spillAmt > 0.0f) {
            if (isGreenKey) {
                float maxOther = std::max(r, b);
                float avgOther = (r + b) * 0.5f;
                float targetLimit = maxOther * (1.0f - spillAmt) + avgOther * spillAmt;
                if (g > targetLimit) {
                    g = targetLimit;
                    p[1] = static_cast<uint8_t>(std::clamp(g * 255.0f, 0.0f, 255.0f));
                }
            } else if (isBlueKey) {
                float maxOther = std::max(r, g);
                float avgOther = (r + g) * 0.5f;
                float targetLimit = maxOther * (1.0f - spillAmt) + avgOther * spillAmt;
                if (b > targetLimit) {
                    b = targetLimit;
                    p[2] = static_cast<uint8_t>(std::clamp(b * 255.0f, 0.0f, 255.0f));
                }
            }
        }

        p[3] = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
        p += 4;
    }
}

VideoFramePtr ChromaKeyFilter::applyKey(const VideoFramePtr& input) const {
    if (!input || input->empty()) return nullptr;
    auto copy = std::make_shared<VideoFrame>(*input);
    processFrame(copy);
    return copy;
}

} // namespace sm
