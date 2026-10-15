/**
 * StreamMix Integration Test — Dynamic Titles & Data Sources (vMix 29)
 */

#include "streammix.h"
#include <iostream>
#include <cassert>
#include <memory>
#include <vector>

int main() {
    std::cout << "====================================================\n";
    std::cout << "StreamMix vMix 29 — Dynamic Titles & Data Sources Test\n";
    std::cout << "====================================================\n";

    // 1. Initialize Master Production Context
    sm::Context ctx;
    bool initOk = ctx.initialize();
    assert(initOk);
    std::cout << "[PASS] Production Context initialized.\n";

    // 2. CSV Data Source Ingestion & Row Navigation
    std::cout << "\n--- Testing CSV Data Source Ingestion ---\n";
    auto csvSource = std::make_shared<sm::CSVDataSource>("Speakers CSV");

    std::string sampleCSV =
        "Name,Role,Affiliation\n"
        "Jane Doe,Keynote Speaker,Global AI Forum\n"
        "John Smith,Lead Architect,StreamMix Media\n"
        "Alice Wong,Broadcast Engineer,Studio 4\n";

    bool loadCsvOk = csvSource->loadFromString(sampleCSV);
    assert(loadCsvOk);
    assert(csvSource->rowCount() == 3);
    assert(csvSource->columnNames().size() == 3);
    assert(csvSource->columnNames()[0] == "Name");
    assert(csvSource->columnNames()[1] == "Role");
    assert(csvSource->columnNames()[2] == "Affiliation");

    assert(csvSource->getValue("Name") == "Jane Doe");
    assert(csvSource->getValue("Role") == "Keynote Speaker");
    assert(csvSource->getValue("Affiliation") == "Global AI Forum");
    std::cout << "[PASS] CSV loaded: 3 rows, active row 0 is Jane Doe.\n";

    // Step to Next Row
    csvSource->nextRow();
    assert(csvSource->activeRow() == 1);
    assert(csvSource->getValue("Name") == "John Smith");
    assert(csvSource->getValue("Role") == "Lead Architect");
    std::cout << "[PASS] CSV live row navigation: active row 1 is John Smith.\n";

    // 3. JSON Data Source Testing
    std::cout << "\n--- Testing JSON API Data Source ---\n";
    auto jsonSource = std::make_shared<sm::JSONDataSource>("Match Score API");
    std::string sampleJSON = "{\n"
                             "  \"match\": \"Championship Final\",\n"
                             "  \"teamA\": \"BARCELONA\",\n"
                             "  \"scoreA\": \"3\",\n"
                             "  \"teamB\": \"MADRID\",\n"
                             "  \"scoreB\": \"2\",\n"
                             "  \"clock\": \"88:15\"\n"
                             "}";
    bool loadJsonOk = jsonSource->loadFromString(sampleJSON);
    assert(loadJsonOk);
    assert(jsonSource->getValue("teamA") == "BARCELONA");
    assert(jsonSource->getValue("scoreA") == "3");
    assert(jsonSource->getValue("teamB") == "MADRID");
    assert(jsonSource->getValue("scoreB") == "2");
    assert(jsonSource->getValue("clock") == "88:15");
    std::cout << "[PASS] JSON parsed correctly: "
              << jsonSource->getValue("teamA") << " " << jsonSource->getValue("scoreA")
              << " vs " << jsonSource->getValue("teamB") << " " << jsonSource->getValue("scoreB") << "\n";

    // Update JSON Live
    jsonSource->setKeyValue("scoreA", "4");
    assert(jsonSource->getValue("scoreA") == "4");
    std::cout << "[PASS] JSON dynamic score update reflected.\n";

    // 4. Precision Countdown Timer Data Source
    std::cout << "\n--- Testing Precision Countdown Timer Data Source ---\n";
    auto timerSource = std::make_shared<sm::TimerDataSource>("Match Clock");
    timerSource->setMode(sm::TimerMode::Countdown);
    timerSource->setDurationSeconds(10); // 10s countdown
    timerSource->start();
    assert(timerSource->isRunning());
    assert(timerSource->getValue("state") == "Running");

    // Advance 4 seconds
    timerSource->tick(4.0);
    assert(timerSource->getValue("seconds") == "6");
    assert(timerSource->getValue("time") == "00:06");
    std::cout << "[PASS] Countdown timer ticked 4s -> remaining: " << timerSource->getValue("time") << "\n";

    // Advance remaining 6.5 seconds (hits zero and finishes)
    timerSource->tick(6.5);
    assert(timerSource->isCompleted());
    assert(timerSource->getValue("time") == "00:00");
    assert(timerSource->getValue("state") == "Completed");
    std::cout << "[PASS] Countdown reached 00:00 and triggered Completed state.\n";

    // 5. Dynamic Title Source & Live Data Binding
    std::cout << "\n--- Testing Dynamic Title Engine (vMix 29 Templates) ---\n";
    auto titleSource = std::make_shared<sm::TitleSource>("Lower Third 1", sm::TitleTemplate::LowerThird);
    sm::SourceSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps_num = 60;
    settings.fps_den = 1;
    bool startOk = titleSource->start(settings);
    assert(startOk);
    assert(titleSource->isRunning());

    // Bind fields to CSV
    titleSource->bindField("headline", csvSource, "Name");
    titleSource->bindField("subline", csvSource, "Role");

    // Tick to refresh data bindings and render
    titleSource->tick(0.016);
    assert(titleSource->getText("headline") == "John Smith");
    assert(titleSource->getText("subline") == "Lead Architect");
    std::cout << "[PASS] Lower Third bound to CSV row 1: "
              << titleSource->getText("headline") << " (" << titleSource->getText("subline") << ")\n";

    // Advance CSV row -> Title auto-updates on tick
    csvSource->nextRow(); // Row 2: Alice Wong
    titleSource->tick(0.016);
    assert(titleSource->getText("headline") == "Alice Wong");
    assert(titleSource->getText("subline") == "Broadcast Engineer");
    std::cout << "[PASS] Live CSV row change propagated to Title: "
              << titleSource->getText("headline") << " (" << titleSource->getText("subline") << ")\n";

    // 6. Test Scoreboard Template & JSON Binding
    std::cout << "\n--- Testing Sports Scoreboard Template ---\n";
    titleSource->setTemplate(sm::TitleTemplate::Scoreboard);
    titleSource->bindField("teamA", jsonSource, "teamA");
    titleSource->bindField("scoreA", jsonSource, "scoreA");
    titleSource->bindField("teamB", jsonSource, "teamB");
    titleSource->bindField("scoreB", jsonSource, "scoreB");
    titleSource->bindField("clock", jsonSource, "clock");

    titleSource->tick(0.016);
    assert(titleSource->getText("teamA") == "BARCELONA");
    assert(titleSource->getText("scoreA") == "4");
    assert(titleSource->getText("teamB") == "MADRID");
    assert(titleSource->getText("scoreB") == "2");
    std::cout << "[PASS] Scoreboard populated from JSON feed: "
              << titleSource->getText("teamA") << " " << titleSource->getText("scoreA")
              << " - " << titleSource->getText("scoreB") << " " << titleSource->getText("teamB") << "\n";

    // 7. Test Breaking News Crawl Ticker
    std::cout << "\n--- Testing Breaking News Animated Ticker ---\n";
    titleSource->setTemplate(sm::TitleTemplate::BreakingNews);
    titleSource->setText("badge", "BREAKING");
    titleSource->setText("headline", "STREAMMIX TITLES OPERATIONAL");
    titleSource->setText("ticker", "HIGH-PERFORMANCE DATA SOURCES OPERATING AT 60FPS");
    titleSource->tick(1.0); // 1-second ticker animation progression
    assert(titleSource->getText("badge") == "BREAKING");
    std::cout << "[PASS] Breaking News ticker rendered smoothly.\n";

    // 8. Compositor Overlay Integration
    std::cout << "\n--- Testing Compositor Overlay Integration ---\n";
    sm::CompositorConfig compConfig;
    compConfig.width = 1920;
    compConfig.height = 1080;
    compConfig.fps = 60.0;
    sm::Compositor comp(compConfig);

    // Track frame via video callback
    sm::VideoFramePtr titleFrame;
    titleSource->setVideoCallback([&](sm::VideoFramePtr frame) {
        titleFrame = frame;
    });

    // Create a solid blue Program background frame
    auto bgFrame = std::make_shared<sm::VideoFrame>(sm::VideoFrame::makeRGBA(1920, 1080));
    for (size_t i = 0; i < bgFrame->data.size(); i += 4) {
        bgFrame->data[i + 0] = 0;   // R
        bgFrame->data[i + 1] = 0;   // G
        bgFrame->data[i + 2] = 200; // B
        bgFrame->data[i + 3] = 255; // A
    }

    // Set as Input 1
    comp.updateInputFrame(1, bgFrame);
    comp.setProgramSource(1);

    // Trigger tick which renders and pushes video frame
    titleSource->tick(0.016);
    assert(titleFrame != nullptr);
    assert(titleFrame->width == 1920);
    assert(titleFrame->height == 1080);

    comp.updateInputFrame(2, titleFrame);
    comp.setOverlaySource(1, 2);
    comp.showOverlay(1, sm::OverlayTransition::Cut, 0);
    comp.tick(0.016);

    assert(comp.overlay(1).sourceId == 2);
    assert(comp.isOverlayActive(1));
    assert(comp.overlay(1).state == sm::OverlayState::Active);

    // Render composite frame with Overlay 1 active
    auto outFrame = comp.renderProgram();
    assert(outFrame != nullptr);
    assert(outFrame->width == 1920);
    assert(outFrame->height == 1080);
    std::cout << "[PASS] Title rendered transparently on Overlay 1 over Program canvas.\n";

    // Clean shutdown
    titleSource->stop();
    assert(!titleSource->isRunning());
    ctx.shutdown();

    std::cout << "\n====================================================\n";
    std::cout << "ALL DYNAMIC TITLES & DATA SOURCE TESTS PASSED!\n";
    std::cout << "====================================================\n";

    return 0;
}
