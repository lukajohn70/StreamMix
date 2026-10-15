/**
 * sm::Compositor — Implementation
 *
 * Implements broadcast Preview/Program switching, T-Bar blending,
 * the vMix 29 8-overlay stack, Fade to Black (FTB), and MultiView rendering.
 */

#include "streammix/compositor.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace sm {

static inline uint8_t clamp8(int v) {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

// ─── Constructor ─────────────────────────────────────────────────
Compositor::Compositor(CompositorConfig config)
    : m_config(config)
{
    // Initialize 8 independent overlay channels (vMix 29 standard)
    m_overlays.resize(MAX_OVERLAYS);
    for (int i = 0; i < MAX_OVERLAYS; ++i) {
        m_overlays[i].id = i + 1;
        m_overlays[i].state = OverlayState::Off;
        m_overlays[i].transition = OverlayTransition::Fade;
        m_overlays[i].durationMs = 300;
        m_overlays[i].progress = 0.0f;
        m_overlays[i].x = 0.0f;
        m_overlays[i].y = 0.0f;
        m_overlays[i].width = 1.0f;
        m_overlays[i].height = 1.0f;
        m_overlays[i].opacity = 1.0f;
    }
}

// ─── Configuration ───────────────────────────────────────────────
void Compositor::setResolution(int width, int height) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config.width = std::max(16, width);
    m_config.height = std::max(16, height);
}

// ─── Input Frame Ingestion ───────────────────────────────────────
void Compositor::updateInputFrame(uint64_t sourceId, VideoFramePtr frame) {
    if (!frame || frame->empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputFrames[sourceId] = std::move(frame);
}

VideoFramePtr Compositor::getInputFrame(uint64_t sourceId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_inputFrames.find(sourceId);
    if (it != m_inputFrames.end()) {
        return it->second;
    }
    return nullptr;
}

void Compositor::removeInput(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_inputFrames.erase(sourceId);
    if (m_previewSourceId == sourceId) m_previewSourceId = 0;
    if (m_programSourceId == sourceId) m_programSourceId = 0;
    for (auto& ov : m_overlays) {
        if (ov.sourceId == sourceId) {
            ov.sourceId = 0;
            ov.state = OverlayState::Off;
            ov.progress = 0.0f;
        }
    }
}

// ─── Preview & Program Bus Assignment ────────────────────────────
void Compositor::setPreviewSource(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_previewSourceId = sourceId;
}

void Compositor::setProgramSource(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_programSourceId = sourceId;
    m_transitionState = TransitionState::Idle;
    m_transitionProgress.store(0.0f);
}

uint64_t Compositor::previewSourceId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_previewSourceId;
}

uint64_t Compositor::programSourceId() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_programSourceId;
}

// ─── Switching & Transitions ─────────────────────────────────────
void Compositor::cut() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::swap(m_programSourceId, m_previewSourceId);
    m_transitionState = TransitionState::Idle;
    m_transitionProgress.store(0.0f);
}

void Compositor::quickPlay(uint64_t sourceId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_programSourceId = sourceId;
    m_transitionState = TransitionState::Idle;
    m_transitionProgress.store(0.0f);
}

void Compositor::startTransition(TransitionType type, int durationMs) {
    if (type == TransitionType::Cut || durationMs <= 0) {
        cut();
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_transitionOutgoingSourceId = m_programSourceId;
    m_activeTransition = type;
    m_transitionDurationMs = std::max(50, durationMs);
    m_transitionElapsedSeconds = 0.0;
    m_transitionProgress.store(0.0f);
    m_transitionState = TransitionState::Active;
}

void Compositor::setTBarPosition(float position) {
    std::lock_guard<std::mutex> lock(m_mutex);
    float p = std::clamp(position, 0.0f, 1.0f);

    if (p <= 0.0f) {
        if (m_transitionState == TransitionState::Active) {
            m_transitionState = TransitionState::Idle;
        }
        m_transitionProgress.store(0.0f);
        return;
    }

    if (p >= 1.0f) {
        // Complete the transition
        std::swap(m_programSourceId, m_previewSourceId);
        m_transitionState = TransitionState::Idle;
        m_transitionProgress.store(0.0f);
        return;
    }

    // Manual intermediate position
    if (m_transitionState != TransitionState::Active) {
        m_transitionOutgoingSourceId = m_programSourceId;
        m_activeTransition = TransitionType::Fade;
        m_transitionState = TransitionState::Active;
    }
    m_transitionProgress.store(p);
}

// ─── Fade to Black (FTB) ─────────────────────────────────────────
void Compositor::toggleFTB(int durationMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_ftbTargetState = !m_ftbTargetState;
    m_ftbDurationMs = std::max(50, durationMs);
    m_ftbElapsedSeconds = 0.0;
    m_ftbActive.store(true);
}

// ─── vMix 29 8-Overlay Stack ─────────────────────────────────────
void Compositor::setOverlaySource(int channelIndex, uint64_t sourceId) {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_overlays[channelIndex - 1].sourceId = sourceId;
}

void Compositor::toggleOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ov = m_overlays[channelIndex - 1];
    if (ov.state == OverlayState::Off || ov.state == OverlayState::Out) {
        // Show — unlocked inline
        ov.transition = trans;
        if (trans == OverlayTransition::Cut || durationMs <= 0) {
            ov.durationMs = 0;
            ov.progress = 1.0f;
            ov.state = OverlayState::Active;
        } else {
            ov.durationMs = std::max(20, durationMs);
            ov.state = OverlayState::In;
        }
    } else {
        // Hide — unlocked inline
        ov.transition = trans;
        if (trans == OverlayTransition::Cut || durationMs <= 0) {
            ov.durationMs = 0;
            ov.progress = 0.0f;
            ov.state = OverlayState::Off;
        } else {
            ov.durationMs = std::max(20, durationMs);
            ov.state = OverlayState::Out;
        }
    }
}

void Compositor::showOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ov = m_overlays[channelIndex - 1];
    ov.transition = trans;
    if (trans == OverlayTransition::Cut || durationMs <= 0) {
        ov.durationMs = 0;
        ov.progress = 1.0f;
        ov.state = OverlayState::Active;
    } else {
        ov.durationMs = std::max(20, durationMs);
        ov.state = OverlayState::In;
    }
}

void Compositor::hideOverlay(int channelIndex, OverlayTransition trans, int durationMs) {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ov = m_overlays[channelIndex - 1];
    ov.transition = trans;
    if (trans == OverlayTransition::Cut || durationMs <= 0) {
        ov.durationMs = 0;
        ov.progress = 0.0f;
        ov.state = OverlayState::Off;
    } else {
        ov.durationMs = std::max(20, durationMs);
        ov.state = OverlayState::Out;
    }
}

void Compositor::setOverlayGeometry(int channelIndex, float x, float y, float w, float h, float opacity) {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ov = m_overlays[channelIndex - 1];
    ov.x = std::clamp(x, 0.0f, 1.0f);
    ov.y = std::clamp(y, 0.0f, 1.0f);
    ov.width = std::clamp(w, 0.01f, 1.0f);
    ov.height = std::clamp(h, 0.01f, 1.0f);
    ov.opacity = std::clamp(opacity, 0.0f, 1.0f);
}

bool Compositor::isOverlayActive(int channelIndex) const {
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_overlays[channelIndex - 1].state != OverlayState::Off;
}

const OverlayChannel& Compositor::overlay(int channelIndex) const {
    static const OverlayChannel s_dummy{};
    if (channelIndex < 1 || channelIndex > MAX_OVERLAYS) return s_dummy;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_overlays[channelIndex - 1];
}

// ─── Engine Clock / Animation Tick ───────────────────────────────
void Compositor::tick(double deltaSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Advance Active Transition
    if (m_transitionState == TransitionState::Active) {
        m_transitionElapsedSeconds += deltaSeconds;
        double totalSec = static_cast<double>(m_transitionDurationMs) / 1000.0;
        float p = static_cast<float>(m_transitionElapsedSeconds / totalSec);
        if (p >= 1.0f) {
            // Swap program and preview
            m_programSourceId = m_previewSourceId;
            m_previewSourceId = m_transitionOutgoingSourceId;
            m_transitionState = TransitionState::Idle;
            m_transitionProgress.store(0.0f);
        } else {
            m_transitionProgress.store(p);
        }
    }

    // 2. Advance FTB
    if (m_ftbActive.load()) {
        m_ftbElapsedSeconds += deltaSeconds;
        double totalSec = static_cast<double>(m_ftbDurationMs) / 1000.0;
        float step = static_cast<float>(deltaSeconds / totalSec);
        float cur = m_ftbProgress.load();
        if (m_ftbTargetState) {
            cur = std::min(1.0f, cur + step);
            if (cur >= 1.0f) m_ftbActive.store(false);
        } else {
            cur = std::max(0.0f, cur - step);
            if (cur <= 0.0f) m_ftbActive.store(false);
        }
        m_ftbProgress.store(cur);
    }

    // 3. Advance Overlays
    for (auto& ov : m_overlays) {
        if (ov.state == OverlayState::In) {
            double totalSec = static_cast<double>(ov.durationMs) / 1000.0;
            ov.progress += static_cast<float>(deltaSeconds / totalSec);
            if (ov.progress >= 1.0f) {
                ov.progress = 1.0f;
                ov.state = OverlayState::Active;
            }
        } else if (ov.state == OverlayState::Out) {
            double totalSec = static_cast<double>(ov.durationMs) / 1000.0;
            ov.progress -= static_cast<float>(deltaSeconds / totalSec);
            if (ov.progress <= 0.0f) {
                ov.progress = 0.0f;
                ov.state = OverlayState::Off;
            }
        }
    }
}

// ─── Helpers: Source Frame or Black ──────────────────────────────
VideoFramePtr Compositor::getSourceOrBlack(uint64_t sourceId) const {
    if (sourceId != 0) {
        auto it = m_inputFrames.find(sourceId);
        if (it != m_inputFrames.end() && it->second && !it->second->empty()) {
            return it->second;
        }
    }
    // Return pristine black frame
    auto black = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_config.width, m_config.height));
    return black;
}

// ─── Output Rendering ────────────────────────────────────────────
VideoFramePtr Compositor::renderProgram(int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto outFrame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_config.width, m_config.height));
    outFrame->pts_us = pts_us;
    outFrame->seq = ++m_programSeq;

    // Base video layer (switching / transition blending)
    if (m_transitionState == TransitionState::Active) {
        auto outgoing = getSourceOrBlack(m_transitionOutgoingSourceId);
        auto incoming = getSourceOrBlack(m_previewSourceId);
        blendFrames(*outgoing, *incoming, *outFrame, m_transitionProgress.load(), m_activeTransition);
    } else {
        auto prog = getSourceOrBlack(m_programSourceId);
        // Direct blit / resize to master resolution
        blendFrames(*prog, *prog, *outFrame, 0.0f, TransitionType::Cut);
    }

    // Composite all 8 Overlay Channels in order 1..8
    for (const auto& ov : m_overlays) {
        if (ov.state != OverlayState::Off && ov.sourceId != 0) {
            auto it = m_inputFrames.find(ov.sourceId);
            if (it != m_inputFrames.end() && it->second && !it->second->empty()) {
                compositeOverlay(*outFrame, *(it->second), ov);
            }
        }
    }

    // Apply Fade to Black if active
    float ftb = m_ftbProgress.load();
    if (ftb > 0.0f) {
        applyFTB(*outFrame, ftb);
    }

    if (m_programCb) {
        m_programCb(outFrame);
    }

    return outFrame;
}

VideoFramePtr Compositor::renderPreview(int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto outFrame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_config.width, m_config.height));
    outFrame->pts_us = pts_us;
    outFrame->seq = ++m_previewSeq;

    auto prev = getSourceOrBlack(m_previewSourceId);
    blendFrames(*prev, *prev, *outFrame, 0.0f, TransitionType::Cut);

    if (m_previewCb) {
        m_previewCb(outFrame);
    }

    return outFrame;
}

VideoFramePtr Compositor::renderMultiView(int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // MultiView Layout:
    // Top half: Left = Preview (50% w, 50% h), Right = Program (50% w, 50% h)
    // Bottom half: Up to 8 input thumbnails
    auto mv = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_config.width, m_config.height));
    mv->pts_us = pts_us;

    // Render Preview into top-left
    auto prev = getSourceOrBlack(m_previewSourceId);
    OverlayChannel ovPrev;
    ovPrev.x = 0.02f; ovPrev.y = 0.04f; ovPrev.width = 0.46f; ovPrev.height = 0.44f;
    compositeOverlay(*mv, *prev, ovPrev);

    // Render Program into top-right
    auto prog = getSourceOrBlack(m_programSourceId);
    OverlayChannel ovProg;
    ovProg.x = 0.52f; ovProg.y = 0.04f; ovProg.width = 0.46f; ovProg.height = 0.44f;
    compositeOverlay(*mv, *prog, ovProg);

    return mv;
}

// ─── Pixel Blending & Transitions ────────────────────────────────
void Compositor::blendFrames(const VideoFrame& srcA, const VideoFrame& srcB, VideoFrame& dst, float t, TransitionType type) {
    int w = dst.width;
    int h = dst.height;
    t = std::clamp(t, 0.0f, 1.0f);

    const uint8_t* aData = srcA.data.data();
    const uint8_t* bData = srcB.data.data();
    uint8_t* dData = dst.data.data();

    bool aSame = (srcA.width == w && srcA.height == h);
    bool bSame = (srcB.width == w && srcB.height == h);

    // 1. Cut (t == 0 or Cut type)
    if (type == TransitionType::Cut || t <= 0.0f) {
        if (aSame && aData) {
            std::memcpy(dData, aData, static_cast<size_t>(w * h * 4));
        }
        return;
    }

    // 2. Full cutover
    if (t >= 1.0f) {
        if (bSame && bData) {
            std::memcpy(dData, bData, static_cast<size_t>(w * h * 4));
        }
        return;
    }

    // 3. Fade (Dissolve)
    if (type == TransitionType::Fade || type == TransitionType::Zoom || type == TransitionType::Fly) {
        int alphaB = static_cast<int>(t * 256.0f);
        int alphaA = 256 - alphaB;
        size_t totalPixels = static_cast<size_t>(w * h);

        if (aSame && bSame && aData && bData) {
            for (size_t i = 0; i < totalPixels * 4; i += 4) {
                dData[i + 0] = static_cast<uint8_t>((aData[i + 0] * alphaA + bData[i + 0] * alphaB) >> 8);
                dData[i + 1] = static_cast<uint8_t>((aData[i + 1] * alphaA + bData[i + 1] * alphaB) >> 8);
                dData[i + 2] = static_cast<uint8_t>((aData[i + 2] * alphaA + bData[i + 2] * alphaB) >> 8);
                dData[i + 3] = 255;
            }
            return;
        }
    }

    // 4. Wipe Transitions (Horizontal / Vertical)
    if (type == TransitionType::WipeRight || type == TransitionType::SlideRight) {
        int splitX = static_cast<int>(t * w);
        for (int y = 0; y < h; ++y) {
            size_t rowOffset = static_cast<size_t>(y * w * 4);
            size_t splitBytes = static_cast<size_t>(splitX * 4);
            if (bSame && bData) {
                std::memcpy(dData + rowOffset, bData + rowOffset, splitBytes);
            }
            if (aSame && aData) {
                std::memcpy(dData + rowOffset + splitBytes, aData + rowOffset + splitBytes, static_cast<size_t>((w - splitX) * 4));
            }
        }
        return;
    }

    // Fallback: Default to Fade
    int alphaB = static_cast<int>(t * 256.0f);
    int alphaA = 256 - alphaB;
    size_t totalPixels = static_cast<size_t>(w * h);
    if (aSame && bSame && aData && bData) {
        for (size_t i = 0; i < totalPixels * 4; i += 4) {
            dData[i + 0] = static_cast<uint8_t>((aData[i + 0] * alphaA + bData[i + 0] * alphaB) >> 8);
            dData[i + 1] = static_cast<uint8_t>((aData[i + 1] * alphaA + bData[i + 1] * alphaB) >> 8);
            dData[i + 2] = static_cast<uint8_t>((aData[i + 2] * alphaA + bData[i + 2] * alphaB) >> 8);
            dData[i + 3] = 255;
        }
    }
}

// ─── Overlay Compositing with Sub-Pixel Positioning ──────────────
void Compositor::compositeOverlay(VideoFrame& canvas, const VideoFrame& ovFrame, const OverlayChannel& ov) {
    if (ovFrame.empty() || canvas.empty()) return;

    int canvasW = canvas.width;
    int canvasH = canvas.height;

    // Calculate effective animated position based on transition
    float effectiveX = ov.x;
    float effectiveY = ov.y;
    float effectiveOpacity = ov.opacity * ov.progress;

    if (ov.transition == OverlayTransition::SlideBottom) {
        // Slides in from bottom edge
        effectiveY = ov.y + (1.0f - ov.progress) * (1.0f - ov.y);
    } else if (ov.transition == OverlayTransition::SlideLeft) {
        // Slides in from left edge
        effectiveX = ov.x - (1.0f - ov.progress) * (ov.x + ov.width);
    } else if (ov.transition == OverlayTransition::SlideRight) {
        // Slides in from right edge
        effectiveX = ov.x + (1.0f - ov.progress) * (1.0f - ov.x);
    }

    int dstX = static_cast<int>(effectiveX * canvasW);
    int dstY = static_cast<int>(effectiveY * canvasH);
    int dstW = static_cast<int>(ov.width * canvasW);
    int dstH = static_cast<int>(ov.height * canvasH);

    if (dstW <= 0 || dstH <= 0 || dstX >= canvasW || dstY >= canvasH) return;

    int srcW = ovFrame.width;
    int srcH = ovFrame.height;
    const uint8_t* srcData = ovFrame.data.data();
    uint8_t* dstData = canvas.data.data();

    int startY = std::max(0, dstY);
    int endY   = std::min(canvasH, dstY + dstH);
    int startX = std::max(0, dstX);
    int endX   = std::min(canvasW, dstX + dstW);

    int masterAlphaScale = static_cast<int>(effectiveOpacity * 256.0f);

    for (int y = startY; y < endY; ++y) {
        int srcY = ((y - dstY) * srcH) / dstH;
        size_t srcRowOffset = static_cast<size_t>(srcY * srcW * 4);
        size_t dstRowOffset = static_cast<size_t>(y * canvasW * 4);

        for (int x = startX; x < endX; ++x) {
            int srcX = ((x - dstX) * srcW) / dstW;
            size_t srcIdx = srcRowOffset + static_cast<size_t>(srcX * 4);
            size_t dstIdx = dstRowOffset + static_cast<size_t>(x * 4);

            uint8_t srcA = srcData[srcIdx + 3];
            int scaledAlpha = (srcA * masterAlphaScale) >> 8;
            if (scaledAlpha <= 0) continue;

            if (scaledAlpha >= 255) {
                dstData[dstIdx + 0] = srcData[srcIdx + 0];
                dstData[dstIdx + 1] = srcData[srcIdx + 1];
                dstData[dstIdx + 2] = srcData[srcIdx + 2];
            } else {
                int invAlpha = 256 - scaledAlpha;
                dstData[dstIdx + 0] = static_cast<uint8_t>((srcData[srcIdx + 0] * scaledAlpha + dstData[dstIdx + 0] * invAlpha) >> 8);
                dstData[dstIdx + 1] = static_cast<uint8_t>((srcData[srcIdx + 1] * scaledAlpha + dstData[dstIdx + 1] * invAlpha) >> 8);
                dstData[dstIdx + 2] = static_cast<uint8_t>((srcData[srcIdx + 2] * scaledAlpha + dstData[dstIdx + 2] * invAlpha) >> 8);
            }
        }
    }
}

// ─── Fade to Black Blit ──────────────────────────────────────────
void Compositor::applyFTB(VideoFrame& frame, float progress) {
    if (progress <= 0.0f) return;
    int scale = static_cast<int>((1.0f - std::clamp(progress, 0.0f, 1.0f)) * 256.0f);
    uint8_t* data = frame.data.data();
    size_t count = static_cast<size_t>(frame.width * frame.height * 4);

    for (size_t i = 0; i < count; i += 4) {
        data[i + 0] = static_cast<uint8_t>((data[i + 0] * scale) >> 8);
        data[i + 1] = static_cast<uint8_t>((data[i + 1] * scale) >> 8);
        data[i + 2] = static_cast<uint8_t>((data[i + 2] * scale) >> 8);
    }
}

} // namespace sm
