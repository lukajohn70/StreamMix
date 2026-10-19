#pragma once
/**
 * sm::TitleSource — Vector & HTML5-Class Dynamic Title Engine (vMix 29)
 *
 * Capabilities:
 *   - Transparent alpha RGBA8 title layer generation for inputs and overlays (1–8)
 *   - Built-in templates: Lower Third, Breaking News ticker, Sports Scoreboard, Countdown
 *   - Live dynamic data binding to CSV, Excel, JSON APIs, and countdown timers
 *   - Animated smooth crawling news tickers with variable speed
 */

#include "streammix/source.h"
#include "streammix/data_source.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace sm {

enum class TitleTemplate {
    LowerThird,
    BreakingNews,
    Scoreboard,
    CountdownTimer,
    Custom
};

struct TitleBinding {
    std::string elementName;
    std::shared_ptr<DataSource> dataSource;
    std::string sourceKey;
};

class TitleSource final : public Source {
public:
    explicit TitleSource(const std::string& name = "Title Input",
                         TitleTemplate templ = TitleTemplate::LowerThird);
    ~TitleSource() override;

    bool start(const SourceSettings& settings) override;
    void stop() override;

    // ─── Template & Elements ─────────────────────────────────────
    void setTemplate(TitleTemplate templ);
    TitleTemplate currentTemplate() const { return m_template; }

    void setText(const std::string& elementName, const std::string& text);
    std::string getText(const std::string& elementName) const;

    // ─── Live Data Binding (CSV / JSON / Timer) ──────────────────
    void bindField(const std::string& elementName, std::shared_ptr<DataSource> source,
                   const std::string& sourceKey);
    void unbindField(const std::string& elementName);

    // ─── Engine Clock / Render ───────────────────────────────────
    void tick(double deltaSeconds) override;
    void render();

private:
    void renderLowerThird(VideoFrame& frame);
    void renderBreakingNews(VideoFrame& frame);
    void renderScoreboard(VideoFrame& frame);
    void renderCountdown(VideoFrame& frame);

    void drawBox(VideoFrame& frame, int x, int y, int w, int h, uint32_t color);
    void drawText(VideoFrame& frame, int x, int y, const std::string& text, uint32_t color, int scale = 2);

    TitleTemplate m_template;
    std::atomic<bool> m_running { false };

    mutable std::mutex m_titleMutex;
    std::unordered_map<std::string, std::string> m_elements;
    std::vector<TitleBinding> m_bindings;

    double m_tickerOffset = 0.0;
    double m_renderAccumulator = 0.0;
};

} // namespace sm
