/**
 * sm::VirtualSet — Implementation
 */

#include "streammix/virtual_set.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace sm {

VirtualSet::VirtualSet(int width, int height)
    : m_width(width), m_height(height)
{
    // Default angles (vMix 29 standard: Wide, Close-Up, Medium)
    m_angles.push_back({"Wide Shot", 0.0f, 0.0f, 1.0f});
    m_angles.push_back({"Close-Up Presenter", -0.10f, 0.05f, 1.6f});
    m_angles.push_back({"Medium Two-Shot", 0.15f, 0.02f, 1.3f});

    m_currentAngle = m_angles[0];
    m_targetAngle = m_angles[0];
}

void VirtualSet::setBackground(VideoFramePtr bgFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_bgLayer = bgFrame;
}

void VirtualSet::setTalent(VideoFramePtr talentFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_talentLayer = talentFrame;
}

void VirtualSet::setForeground(VideoFramePtr fgFrame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fgLayer = fgFrame;
}

void VirtualSet::setTalentSettings(const TalentLayerSettings& settings) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_talentSettings = settings;
}

void VirtualSet::addCameraAngle(const VirtualCameraAngle& angle) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_angles.push_back(angle);
}

void VirtualSet::selectCameraAngle(size_t index, int transitionMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= m_angles.size()) return;

    if (transitionMs <= 0) {
        m_currentAngle = m_angles[index];
        m_targetAngle = m_angles[index];
        m_zooming = false;
    } else {
        m_targetAngle = m_angles[index];
        m_zoomElapsed = 0.0;
        m_zoomDuration = std::max(0.1, static_cast<double>(transitionMs) / 1000.0);
        m_zooming = true;
    }
}

size_t VirtualSet::cameraAngleCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_angles.size();
}

void VirtualSet::tick(double deltaSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_zooming) return;

    m_zoomElapsed += deltaSeconds;
    float t = static_cast<float>(std::clamp(m_zoomElapsed / m_zoomDuration, 0.0, 1.0));

    // Smooth cubic ease-in-out
    float ease = (t < 0.5f) ? (4.0f * t * t * t) : (1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f);

    m_currentAngle.pan = m_currentAngle.pan + (m_targetAngle.pan - m_currentAngle.pan) * ease;
    m_currentAngle.tilt = m_currentAngle.tilt + (m_targetAngle.tilt - m_currentAngle.tilt) * ease;
    m_currentAngle.zoom = m_currentAngle.zoom + (m_targetAngle.zoom - m_currentAngle.zoom) * ease;

    if (t >= 1.0f) {
        m_currentAngle = m_targetAngle;
        m_zooming = false;
    }
}

static void blendRgbaPixel(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a == 0) return;
    if (a == 255) {
        dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 255;
        return;
    }
    float alpha = a / 255.0f;
    float inv = 1.0f - alpha;
    dst[0] = static_cast<uint8_t>(r * alpha + dst[0] * inv);
    dst[1] = static_cast<uint8_t>(g * alpha + dst[1] * inv);
    dst[2] = static_cast<uint8_t>(b * alpha + dst[2] * inv);
    dst[3] = std::max(dst[3], a);
}

VideoFramePtr VirtualSet::render(int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto out = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_width, m_height));
    out->pts_us = pts_us;

    // 1. Draw Background Layer (or synthetic studio gradient)
    if (m_bgLayer && !m_bgLayer->empty()) {
        const size_t copySize = std::min(out->data.size(), m_bgLayer->data.size());
        std::memcpy(out->data.data(), m_bgLayer->data.data(), copySize);
    } else {
        // Dark blue-grey broadcast studio default gradient
        for (int y = 0; y < m_height; ++y) {
            uint8_t bVal = static_cast<uint8_t>(std::clamp(30 + (y * 45) / m_height, 0, 255));
            for (int x = 0; x < m_width; ++x) {
                size_t idx = (y * m_width + x) * 4;
                out->data[idx + 0] = 16;
                out->data[idx + 1] = 20;
                out->data[idx + 2] = bVal;
                out->data[idx + 3] = 255;
            }
        }
    }

    // 2. Process Talent with Chroma Keying & Layer Over Background
    if (m_talentLayer && !m_talentLayer->empty()) {
        auto keyedTalent = m_chromaKey.applyKey(m_talentLayer);

        int tw = keyedTalent->width;
        int th = keyedTalent->height;

        // Position & Scale
        int dstX = (m_width - tw) / 2 + static_cast<int>(m_talentSettings.posX * m_width * 0.5f);
        int dstY = (m_height - th) / 2 + static_cast<int>(m_talentSettings.posY * m_height * 0.5f);

        // Optional floor reflection (rendered below talent)
        if (m_talentSettings.floorReflection) {
            float reflAlphaMult = m_talentSettings.reflectionOpacity;
            int reflYStart = dstY + th;
            for (int y = 0; y < std::min(th / 2, m_height - reflYStart); ++y) {
                int srcY = th - 1 - y; // vertical flip
                float fadeOut = (1.0f - static_cast<float>(y) / (th * 0.5f));
                for (int x = 0; x < tw; ++x) {
                    int targetX = dstX + x;
                    int targetY = reflYStart + y;
                    if (targetX < 0 || targetX >= m_width || targetY < 0 || targetY >= m_height) continue;

                    size_t sIdx = (srcY * tw + x) * 4;
                    uint8_t a = static_cast<uint8_t>(keyedTalent->data[sIdx + 3] * reflAlphaMult * fadeOut);
                    if (a > 0) {
                        size_t dIdx = (targetY * m_width + targetX) * 4;
                        blendRgbaPixel(&out->data[dIdx],
                                       keyedTalent->data[sIdx + 0],
                                       keyedTalent->data[sIdx + 1],
                                       keyedTalent->data[sIdx + 2],
                                       a);
                    }
                }
            }
        }

        // Talent primary blit
        for (int y = 0; y < th; ++y) {
            int targetY = dstY + y;
            if (targetY < 0 || targetY >= m_height) continue;
            for (int x = 0; x < tw; ++x) {
                int targetX = dstX + x;
                if (targetX < 0 || targetX >= m_width) continue;

                size_t sIdx = (y * tw + x) * 4;
                uint8_t a = keyedTalent->data[sIdx + 3];
                if (a > 0) {
                    size_t dIdx = (targetY * m_width + targetX) * 4;
                    blendRgbaPixel(&out->data[dIdx],
                                   keyedTalent->data[sIdx + 0],
                                   keyedTalent->data[sIdx + 1],
                                   keyedTalent->data[sIdx + 2],
                                   a);
                }
            }
        }
    }

    // 3. Foreground Occlusion Layer (Podium / Desk)
    if (m_fgLayer && !m_fgLayer->empty()) {
        int fw = std::min(m_width, m_fgLayer->width);
        int fh = std::min(m_height, m_fgLayer->height);
        for (int y = 0; y < fh; ++y) {
            for (int x = 0; x < fw; ++x) {
                size_t sIdx = (y * m_fgLayer->width + x) * 4;
                uint8_t a = m_fgLayer->data[sIdx + 3];
                if (a > 0) {
                    size_t dIdx = (y * m_width + x) * 4;
                    blendRgbaPixel(&out->data[dIdx],
                                   m_fgLayer->data[sIdx + 0],
                                   m_fgLayer->data[sIdx + 1],
                                   m_fgLayer->data[sIdx + 2],
                                   a);
                }
            }
        }
    }

    return out;
}

} // namespace sm
