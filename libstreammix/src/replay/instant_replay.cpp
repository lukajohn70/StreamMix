/**
 * sm::InstantReplay — Implementation
 *
 * Implements vMix 29's Instant Replay 2.0 engine:
 * continuous circular buffer, variable slow-motion, quad-cam layout,
 * mark-in/out clip extraction, and the Replay Bus composite output.
 */

#include "streammix/instant_replay.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <chrono>
#include <sstream>

namespace sm {

// ─────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────

static double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()
    ).count();
}

// ═════════════════════════════════════════════════════════════════
//  ReplayCameraBuffer
// ═════════════════════════════════════════════════════════════════

ReplayCameraBuffer::ReplayCameraBuffer(uint64_t cameraId, const std::string& name,
                                        size_t maxFrames)
    : m_cameraId(cameraId), m_name(name), m_maxFrames(maxFrames)
{
}

void ReplayCameraBuffer::pushFrame(ReplayFramePtr frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    frame->frameIndex = m_frameCounter.fetch_add(1);
    frame->clockSec = nowSeconds();
    m_buffer.push_back(std::move(frame));
    if (m_buffer.size() > m_maxFrames) {
        m_buffer.pop_front();  // Circular: drop oldest frame
    }
}

size_t ReplayCameraBuffer::frameCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size();
}

float ReplayCameraBuffer::bufferUtilization() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_maxFrames > 0
           ? static_cast<float>(m_buffer.size()) / static_cast<float>(m_maxFrames)
           : 0.0f;
}

int64_t ReplayCameraBuffer::newestPts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.empty() ? 0 : m_buffer.back()->pts_us;
}

int64_t ReplayCameraBuffer::oldestPts() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.empty() ? 0 : m_buffer.front()->pts_us;
}

void ReplayCameraBuffer::markCurrentFrame(const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_buffer.empty()) {
        m_buffer.back()->isMarked = true;
        m_buffer.back()->markerLabel = label;
    }
}

std::vector<ReplayFramePtr> ReplayCameraBuffer::getFrameRange(int64_t fromPts,
                                                               int64_t toPts) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ReplayFramePtr> result;
    for (const auto& f : m_buffer) {
        if (f->pts_us >= fromPts && f->pts_us <= toPts) {
            result.push_back(f);
        }
    }
    return result;
}

std::vector<ReplayFramePtr> ReplayCameraBuffer::getLastSeconds(double seconds,
                                                                double fps) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = static_cast<size_t>(std::ceil(seconds * fps));
    if (count >= m_buffer.size()) {
        return std::vector<ReplayFramePtr>(m_buffer.begin(), m_buffer.end());
    }
    return std::vector<ReplayFramePtr>(m_buffer.end() - count, m_buffer.end());
}

// ═════════════════════════════════════════════════════════════════
//  InstantReplay
// ═════════════════════════════════════════════════════════════════

InstantReplay::InstantReplay(double bufferDepthSeconds, double fps)
    : m_fps(fps)
    , m_maxFramesPerCamera(static_cast<size_t>(std::ceil(bufferDepthSeconds * fps)))
{
    // Zero-initialize all playheads
    for (auto& ph : m_playheads) {
        ph = ReplayPlayhead{};
    }
}

void InstantReplay::addCamera(uint64_t cameraId, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_cameras.find(cameraId) == m_cameras.end()) {
        m_cameras[cameraId] = std::make_shared<ReplayCameraBuffer>(
            cameraId, name, m_maxFramesPerCamera);
    }
}

void InstantReplay::removeCamera(uint64_t cameraId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cameras.erase(cameraId);
}

std::vector<uint64_t> InstantReplay::cameraIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<uint64_t> ids;
    for (const auto& pair : m_cameras) {
        ids.push_back(pair.first);
    }
    return ids;
}

void InstantReplay::ingestFrame(uint64_t cameraId, VideoFramePtr frame, int64_t pts_us) {
    std::shared_ptr<ReplayCameraBuffer> buf;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cameras.find(cameraId);
        if (it == m_cameras.end()) return;
        buf = it->second;
    }

    auto rf = std::make_shared<ReplayFrame>();
    rf->video   = std::move(frame);
    rf->pts_us  = pts_us;
    rf->cameraId = cameraId;
    buf->pushFrame(std::move(rf));
}

void InstantReplay::markEvent(const std::string& label) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& pair : m_cameras) {
        pair.second->markCurrentFrame(label);
    }
}

void InstantReplay::setMarkIn(uint64_t cameraId, int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cameras.find(cameraId);
    if (it == m_cameras.end()) return;

    int64_t pts = (pts_us < 0) ? it->second->newestPts() : pts_us;
    m_markIn[cameraId] = pts;
}

void InstantReplay::setMarkOut(uint64_t cameraId, int64_t pts_us) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cameras.find(cameraId);
    if (it == m_cameras.end()) return;

    int64_t pts = (pts_us < 0) ? it->second->newestPts() : pts_us;
    m_markOut[cameraId] = pts;
}

ReplayClipPtr InstantReplay::extractClip(uint64_t cameraId, const std::string& name) {
    std::shared_ptr<ReplayCameraBuffer> buf;
    int64_t inPts = 0, outPts = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cameras.find(cameraId);
        if (it == m_cameras.end()) return nullptr;
        buf = it->second;

        auto inIt = m_markIn.find(cameraId);
        auto outIt = m_markOut.find(cameraId);
        inPts  = (inIt  != m_markIn.end())  ? inIt->second  : buf->oldestPts();
        outPts = (outIt != m_markOut.end()) ? outIt->second : buf->newestPts();
    }

    if (outPts <= inPts) return nullptr;

    auto clip = std::make_shared<ReplayClip>();
    clip->name        = name;
    clip->cameraId    = cameraId;
    clip->markInPts   = inPts;
    clip->markOutPts  = outPts;
    clip->frames      = buf->getFrameRange(inPts, outPts);

    if (clip->frames.empty()) return nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clips.push_back(clip);
    }
    return clip;
}

ReplayClipPtr InstantReplay::extractLastSeconds(uint64_t cameraId, double seconds,
                                                 const std::string& name) {
    std::shared_ptr<ReplayCameraBuffer> buf;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_cameras.find(cameraId);
        if (it == m_cameras.end()) return nullptr;
        buf = it->second;
    }

    auto frames = buf->getLastSeconds(seconds, m_fps);
    if (frames.empty()) return nullptr;

    auto clip = std::make_shared<ReplayClip>();
    clip->name       = name;
    clip->cameraId   = cameraId;
    clip->markInPts  = frames.front()->pts_us;
    clip->markOutPts = frames.back()->pts_us;
    clip->frames     = std::move(frames);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clips.push_back(clip);
    }
    return clip;
}

std::vector<ReplayClipPtr> InstantReplay::clips() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clips;
}

void InstantReplay::removeClip(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clips.erase(std::remove_if(m_clips.begin(), m_clips.end(),
        [&](const ReplayClipPtr& c) { return c && c->name == name; }),
        m_clips.end());
}

void InstantReplay::loadClip(int playheadIndex, ReplayClipPtr clip) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    if (!clip || clip->frames.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph       = m_playheads[playheadIndex];
    ph.frames      = clip->frames;
    ph.frameIndex  = 0;
    ph.cameraId    = clip->cameraId;
    ph.playing     = false;
    ph.speed       = 1.0f;
    ph.accumulator = 0.0;
    ph.reversed    = false;
}

void InstantReplay::play(int playheadIndex, float speed) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph   = m_playheads[playheadIndex];
    ph.speed   = std::clamp(speed, 0.05f, 4.0f);
    ph.playing = !ph.frames.empty();
    if (m_eventCb) m_eventCb("play", playheadIndex);
}

void InstantReplay::pause(int playheadIndex) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playheads[playheadIndex].playing = false;
    if (m_eventCb) m_eventCb("pause", playheadIndex);
}

void InstantReplay::stop(int playheadIndex) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph   = m_playheads[playheadIndex];
    ph.playing    = false;
    ph.frameIndex = 0;
    ph.accumulator = 0.0;
    if (m_eventCb) m_eventCb("stop", playheadIndex);
}

void InstantReplay::setSpeed(int playheadIndex, float speed) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playheads[playheadIndex].speed = std::clamp(speed, 0.05f, 4.0f);
}

void InstantReplay::setLooping(int playheadIndex, bool loop) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playheads[playheadIndex].loop = loop;
}

void InstantReplay::stepForward(int playheadIndex, int frames) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph = m_playheads[playheadIndex];
    if (ph.frames.empty()) return;
    size_t newIdx = ph.frameIndex + static_cast<size_t>(frames);
    ph.frameIndex = std::min(newIdx, ph.frames.size() - 1);
}

void InstantReplay::stepBack(int playheadIndex, int frames) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph = m_playheads[playheadIndex];
    if (ph.frames.empty()) return;
    size_t step = static_cast<size_t>(frames);
    ph.frameIndex = (ph.frameIndex >= step) ? ph.frameIndex - step : 0;
}

void InstantReplay::seekToFrame(int playheadIndex, size_t frameIndex) {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& ph = m_playheads[playheadIndex];
    if (ph.frames.empty()) return;
    ph.frameIndex = std::min(frameIndex, ph.frames.size() - 1);
}

float InstantReplay::speed(int playheadIndex) const {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return 1.0f;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playheads[playheadIndex].speed;
}

bool InstantReplay::isPlaying(int playheadIndex) const {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return false;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playheads[playheadIndex].playing;
}

size_t InstantReplay::currentFrame(int playheadIndex) const {
    if (playheadIndex < 0 || playheadIndex >= MAX_PLAYHEADS) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playheads[playheadIndex].frameIndex;
}

void InstantReplay::setActivePlayhead(int index) {
    if (index < 0 || index >= MAX_PLAYHEADS) return;
    m_activePlayhead = index;
}

// ─── Engine Tick: Advance playheads using fractional accumulator ──

void InstantReplay::tick(double deltaSeconds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < MAX_PLAYHEADS; ++i) {
        auto& ph = m_playheads[i];
        if (!ph.playing || ph.frames.empty()) continue;

        // Accumulate sub-frame steps (enables fractional frame rates)
        ph.accumulator += deltaSeconds * ph.speed * m_fps;
        size_t steps = static_cast<size_t>(ph.accumulator);
        ph.accumulator -= static_cast<double>(steps);

        if (steps == 0) continue;

        if (!ph.reversed) {
            ph.frameIndex += steps;
            if (ph.frameIndex >= ph.frames.size()) {
                if (ph.loop) {
                    ph.frameIndex = ph.frameIndex % ph.frames.size();
                    if (m_eventCb) m_eventCb("loop", i);
                } else {
                    ph.frameIndex = ph.frames.size() - 1;
                    ph.playing    = false;
                    if (m_eventCb) m_eventCb("end", i);
                }
            }
        } else {
            // Reverse playback
            if (steps > ph.frameIndex) {
                if (ph.loop) {
                    ph.frameIndex = ph.frames.size() - 1;
                    if (m_eventCb) m_eventCb("loop", i);
                } else {
                    ph.frameIndex = 0;
                    ph.playing    = false;
                    if (m_eventCb) m_eventCb("end", i);
                }
            } else {
                ph.frameIndex -= steps;
            }
        }
    }
}

// ─── Rendering ────────────────────────────────────────────────────

VideoFramePtr InstantReplay::renderReplayBus() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_quadView ? renderQuadView() : renderSinglePlayhead(m_activePlayhead);
}

VideoFramePtr InstantReplay::renderSinglePlayhead(int index) {
    // Note: called with m_mutex already held
    if (index < 0 || index >= MAX_PLAYHEADS) return nullptr;
    auto& ph = m_playheads[index];
    if (ph.frames.empty() || ph.frameIndex >= ph.frames.size()) return nullptr;
    return ph.frames[ph.frameIndex]->video;
}

void InstantReplay::blitSubFrame(VideoFrame& dst, const VideoFrame& src,
                                  int dstX, int dstY, int dstW, int dstH) {
    // Simple nearest-neighbour scaled blit of src RGBA8 into a sub-rectangle of dst
    uint8_t* dstPx = dst.data.data();
    const uint8_t* srcPx = src.data.data();

    for (int y = 0; y < dstH; ++y) {
        int srcY = (y * src.height) / dstH;
        for (int x = 0; x < dstW; ++x) {
            int srcX = (x * src.width) / dstW;

            size_t srcIdx = static_cast<size_t>((srcY * src.width + srcX) * 4);
            size_t dstIdx = static_cast<size_t>(((dstY + y) * dst.width + (dstX + x)) * 4);

            if (srcIdx + 3 < src.data.size() && dstIdx + 3 < dst.data.size()) {
                dstPx[dstIdx + 0] = srcPx[srcIdx + 0];
                dstPx[dstIdx + 1] = srcPx[srcIdx + 1];
                dstPx[dstIdx + 2] = srcPx[srcIdx + 2];
                dstPx[dstIdx + 3] = srcPx[srcIdx + 3];
            }
        }
    }
}

VideoFramePtr InstantReplay::renderQuadView() {
    // Note: called with m_mutex already held
    // Produces a 1920x1080 frame with 4 equal 960x540 sub-frames
    constexpr int W = 1920, H = 1080;
    constexpr int SW = W / 2, SH = H / 2;

    auto out = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(W, H));
    std::memset(out->data.data(), 0x1A, out->data.size()); // Dark slate background

    struct QuadPos { int x; int y; };
    constexpr QuadPos positions[4] = {{0,0},{SW,0},{0,SH},{SW,SH}};

    for (int i = 0; i < MAX_PLAYHEADS; ++i) {
        auto& ph = m_playheads[i];
        if (ph.frames.empty() || ph.frameIndex >= ph.frames.size()) continue;
        const auto& srcFrame = ph.frames[ph.frameIndex]->video;
        if (!srcFrame) continue;

        blitSubFrame(*out, *srcFrame, positions[i].x, positions[i].y, SW, SH);
    }

    return out;
}

InstantReplay::BufferStats InstantReplay::stats(uint64_t cameraId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_cameras.find(cameraId);
    if (it == m_cameras.end()) return {};

    const auto& buf = it->second;
    BufferStats s;
    s.totalFrames    = buf->frameCount();
    s.maxFrames      = m_maxFramesPerCamera;
    s.utilization    = buf->bufferUtilization();
    s.bufferDepthSec = static_cast<double>(m_maxFramesPerCamera) / m_fps;
    int64_t oldest   = buf->oldestPts();
    int64_t newest   = buf->newestPts();
    s.oldestSec = static_cast<double>(oldest) / 1'000'000.0;
    s.newestSec = static_cast<double>(newest) / 1'000'000.0;
    return s;
}

} // namespace sm
