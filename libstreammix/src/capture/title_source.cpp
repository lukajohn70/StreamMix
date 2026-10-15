/**
 * sm::TitleSource — Implementation
 *
 * Implements high-performance software rasterization of lower thirds,
 * breaking news banners, scoreboards, and live data-bound elements.
 */

#include "streammix/title_source.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace sm {

// ─── 5x7 Minimal Monospace Bitmap Font for Text Rasterization ─────
static const uint8_t s_font5x7[128][7] = {
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    ['/'] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00},
    ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    ['3'] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
    ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    ['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
    ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
    ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
    ['D'] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
    ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
    ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
    ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
    ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
    ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
    ['J'] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
    ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
    ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
    ['M'] = {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11},
    ['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
    ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
    ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
    ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
    ['S'] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E},
    ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
    ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
    ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
    ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
    ['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
    ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}
};

TitleSource::TitleSource(const std::string& name, TitleTemplate templ)
    : Source(name, SourceType::Title), m_template(templ)
{
    m_id = s_nextId.fetch_add(1);
    m_width = 1920;
    m_height = 1080;
    m_fps = 60.0;

    // Default template text
    if (m_template == TitleTemplate::LowerThird) {
        m_elements["headline"] = "PRESENTER NAME";
        m_elements["subline"] = "LIVE BROADCAST GUEST";
    } else if (m_template == TitleTemplate::Scoreboard) {
        m_elements["teamA"] = "HOME";
        m_elements["scoreA"] = "2";
        m_elements["teamB"] = "AWAY";
        m_elements["scoreB"] = "1";
        m_elements["period"] = "2ND HALF";
        m_elements["clock"] = "74:20";
    } else if (m_template == TitleTemplate::BreakingNews) {
        m_elements["badge"] = "BREAKING NEWS";
        m_elements["headline"] = "STREAMMIX RELEASES VMIX 29 CROSS-PLATFORM REPLICA";
        m_elements["ticker"] = "GLOBAL LIVE VIDEO PRODUCTION EXPANDS ACROSS MACOS, WINDOWS, AND LINUX";
    } else if (m_template == TitleTemplate::CountdownTimer) {
        m_elements["title"] = "STARTING SOON";
        m_elements["clock"] = "05:00";
    }
}

TitleSource::~TitleSource() {
    stop();
}

bool TitleSource::start(const SourceSettings& settings) {
    if (m_running.load()) return false;
    m_running.store(true);
    m_state = SourceState::Running;
    render();
    return true;
}

void TitleSource::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_state = SourceState::Stopped;
}

void TitleSource::setTemplate(TitleTemplate templ) {
    std::lock_guard<std::mutex> lock(m_titleMutex);
    m_template = templ;
}

void TitleSource::setText(const std::string& elementName, const std::string& text) {
    std::lock_guard<std::mutex> lock(m_titleMutex);
    m_elements[elementName] = text;
}

std::string TitleSource::getText(const std::string& elementName) const {
    std::lock_guard<std::mutex> lock(m_titleMutex);
    auto it = m_elements.find(elementName);
    return (it != m_elements.end()) ? it->second : "";
}

void TitleSource::bindField(const std::string& elementName, std::shared_ptr<DataSource> source,
                            const std::string& sourceKey) {
    std::lock_guard<std::mutex> lock(m_titleMutex);
    TitleBinding b;
    b.elementName = elementName;
    b.dataSource = source;
    b.sourceKey = sourceKey;
    m_bindings.push_back(b);
}

void TitleSource::unbindField(const std::string& elementName) {
    std::lock_guard<std::mutex> lock(m_titleMutex);
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) {
        if (it->elementName == elementName) {
            m_bindings.erase(it);
            break;
        }
    }
}

void TitleSource::tick(double deltaSeconds) {
    if (!m_running.load()) return;

    {
        std::lock_guard<std::mutex> lock(m_titleMutex);
        // Sync bound data sources
        for (const auto& b : m_bindings) {
            if (b.dataSource) {
                std::string val = b.dataSource->getValue(b.sourceKey);
                if (!val.empty()) {
                    m_elements[b.elementName] = val;
                }
            }
        }

        m_tickerOffset += deltaSeconds * 120.0; // 120 pixels/sec ticker speed
    }

    render();
}

void TitleSource::render() {
    auto frame = std::make_shared<VideoFrame>(VideoFrame::makeRGBA(m_width, m_height));
    std::memset(frame->data.data(), 0, frame->data.size()); // Pristine transparent canvas

    {
        std::lock_guard<std::mutex> lock(m_titleMutex);
        if (m_template == TitleTemplate::LowerThird) {
            renderLowerThird(*frame);
        } else if (m_template == TitleTemplate::Scoreboard) {
            renderScoreboard(*frame);
        } else if (m_template == TitleTemplate::BreakingNews) {
            renderBreakingNews(*frame);
        } else if (m_template == TitleTemplate::CountdownTimer) {
            renderCountdown(*frame);
        }
    }

    pushVideo(std::move(frame));
}

// ─── Graphics Rasterization Primitives ───────────────────────────
void TitleSource::drawBox(VideoFrame& frame, int x, int y, int w, int h, uint32_t color) {
    int fw = frame.width;
    int fh = frame.height;
    uint8_t* dst = frame.data.data();

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint8_t a = (color >> 24) & 0xFF;

    int x1 = std::clamp(x, 0, fw);
    int x2 = std::clamp(x + w, 0, fw);
    int y1 = std::clamp(y, 0, fh);
    int y2 = std::clamp(y + h, 0, fh);

    for (int row = y1; row < y2; ++row) {
        size_t rowOffset = static_cast<size_t>(row * fw * 4);
        for (int col = x1; col < x2; ++col) {
            size_t idx = rowOffset + static_cast<size_t>(col * 4);
            dst[idx + 0] = r;
            dst[idx + 1] = g;
            dst[idx + 2] = b;
            dst[idx + 3] = a;
        }
    }
}

void TitleSource::drawText(VideoFrame& frame, int x, int y, const std::string& text, uint32_t color, int scale) {
    int curX = x;

    for (char c : text) {
        uint8_t upper = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(c)));
        if (upper >= 128) upper = ' ';

        const uint8_t* glyph = s_font5x7[upper];
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) {
                    drawBox(frame, curX + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        curX += 6 * scale; // 5 width + 1 spacing
    }
}

// ─── Templates Rendering ─────────────────────────────────────────
void TitleSource::renderLowerThird(VideoFrame& frame) {
    // 1. Accent Left Strip (Orange / Coral: #E67E22)
    drawBox(frame, 120, 880, 10, 100, 0xFFE67E22);

    // 2. Primary Headline Box (Dark Charcoal Translucent: #1A1C23)
    drawBox(frame, 130, 880, 600, 60, 0xE61A1C23);

    // 3. Subline Box (Slightly lighter slate: #262933)
    drawBox(frame, 130, 940, 520, 40, 0xE6262933);

    // 4. Headline Text
    drawText(frame, 150, 894, m_elements["headline"], 0xFFFFFFFF, 4);

    // 5. Subline Text
    drawText(frame, 150, 950, m_elements["subline"], 0xFFF39C12, 3);
}

void TitleSource::renderScoreboard(VideoFrame& frame) {
    // Centered Top Scoreboard Box: x=660 to 1260 (width 600)
    int baseX = 660;
    int baseY = 50;

    // Background bar
    drawBox(frame, baseX, baseY, 600, 70, 0xF015171C);

    // Team A
    drawText(frame, baseX + 30, baseY + 20, m_elements["teamA"], 0xFFFFFFFF, 4);
    // Score A Box (Blue)
    drawBox(frame, baseX + 190, baseY + 10, 60, 50, 0xFF2980B9);
    drawText(frame, baseX + 205, baseY + 18, m_elements["scoreA"], 0xFFFFFFFF, 4);

    // Clock in middle
    drawText(frame, baseX + 265, baseY + 24, m_elements["clock"], 0xFFE67E22, 3);

    // Score B Box (Red)
    drawBox(frame, baseX + 350, baseY + 10, 60, 50, 0xFFC0392B);
    drawText(frame, baseX + 365, baseY + 18, m_elements["scoreB"], 0xFFFFFFFF, 4);
    // Team B
    drawText(frame, baseX + 430, baseY + 20, m_elements["teamB"], 0xFFFFFFFF, 4);
}

void TitleSource::renderBreakingNews(VideoFrame& frame) {
    // Red BREAKING NEWS Badge
    drawBox(frame, 100, 920, 240, 50, 0xFFE74C3C);
    drawText(frame, 115, 933, m_elements["badge"], 0xFFFFFFFF, 3);

    // Headline Dark Bar
    drawBox(frame, 340, 920, 1480, 50, 0xF0121316);
    drawText(frame, 360, 933, m_elements["headline"], 0xFFFFFFFF, 3);

    // Bottom Animated Crawling Ticker Bar
    drawBox(frame, 0, 1020, 1920, 40, 0xF50A0B0E);
    int tickerX = 1920 - static_cast<int>(std::fmod(m_tickerOffset, 3000.0));
    drawText(frame, tickerX, 1030, m_elements["ticker"], 0xFFF1C40F, 3);
}

void TitleSource::renderCountdown(VideoFrame& frame) {
    // Big Center Countdown Box
    int bx = 760;
    int by = 420;
    drawBox(frame, bx, by, 400, 180, 0xE6101115);
    drawBox(frame, bx, by, 400, 8, 0xFFE74C3C);

    drawText(frame, bx + 70, by + 30, m_elements["title"], 0xFFA0A5B5, 3);
    drawText(frame, bx + 50, by + 80, m_elements["clock"], 0xFFFFFFFF, 9);
}

} // namespace sm
