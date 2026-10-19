#include "main_window.h"
#include "streammix/ffmpeg_source.h"
#include "streammix/record_output.h"
#include "streammix/virtual_cam.h"
#include "dialogs/settings_dialog.h"
#include "dialogs/input_settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QToolBar>
#include <QTabWidget>
#include <QDockWidget>
#include <QDesktopServices>
#include <QUrl>
#include <chrono>
#include <filesystem>

namespace sm {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_instantReplay(60.0, 60.0)
{
    m_context.initialize();

    setupUI();
    setupMenuBar();
    setupStatusBar();
    populateSampleInputs();

    // ── Start native audio hardware player ───────────────────────
    m_audioPlayer.start();

    // ── Web Controller on port 8088 ──────────────────────────────
    WebServerConfig webCfg;
    webCfg.port = 8088;
    m_webServer = new WebServer(m_context, webCfg);
    m_webServer->start();

    // ── MIDI Controller (Akai APC Mini preset) ───────────────────
    m_midiController = new MidiController(m_context);
    m_midiController->loadPreset("apc_mini");
    m_midiController->connect();

    // ── 60 FPS render / animation loop (~16.6 ms) ─────────────────
    m_renderTimer = new QTimer(this);
    connect(m_renderTimer, &QTimer::timeout, this, &MainWindow::onFrameTick);
    m_renderTimer->start(16);
}

MainWindow::~MainWindow() {
    if (m_renderTimer) m_renderTimer->stop();
    m_audioPlayer.stop();
    if (m_webServer) {
        m_webServer->stop();
        delete m_webServer;
    }
    if (m_midiController) {
        m_midiController->disconnect();
        delete m_midiController;
    }
    m_context.shutdown();
}

void MainWindow::setupUI() {
    setWindowTitle("StreamMix v1.0 — Live Production Software (vMix 29 Parity)");
    resize(1360, 840);
    setMinimumSize(1024, 680);

    setStyleSheet(R"(
        QMainWindow {
            background-color: #121316;
            color: #dcdcdc;
            font-family: 'Inter', sans-serif;
        }
        QMenuBar {
            background-color: #1a1b20;
            color: #dcdcdc;
            border-bottom: 1px solid #2e303a;
        }
        QMenuBar::item:selected {
            background-color: #2e313b;
        }
        QStatusBar {
            background-color: #1a1b20;
            color: #a0a0a0;
            border-top: 1px solid #282a34;
            font-size: 11px;
        }
        QSplitter::handle {
            background-color: #242630;
        }
    )");

    auto* centralWidget = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    rootLayout->setSpacing(6);

    // ─── TOP SECTION: PREVIEW | SWITCHER CONTROLS | PROGRAM ──────
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(8);

    m_previewMonitor = new MonitorWidget(MonitorType::Preview, this);
    m_previewMonitor->setSafeAreasType(SafeAreasType::EBUSMPTE);
    topRow->addWidget(m_previewMonitor, 5);

    m_transitionWidget = new TransitionWidget(this);
    connect(m_transitionWidget, &TransitionWidget::cutRequested, this, &MainWindow::onCutRequested);
    connect(m_transitionWidget, &TransitionWidget::transitionRequested, this, &MainWindow::onTransitionRequested);
    connect(m_transitionWidget, &TransitionWidget::tbarMoved, this, &MainWindow::onTBarMoved);
    connect(m_transitionWidget, &TransitionWidget::overlayToggled, this, &MainWindow::onOverlayToggled);
    connect(m_transitionWidget, &TransitionWidget::ftbToggled, this, &MainWindow::onFtbToggled);
    topRow->addWidget(m_transitionWidget, 2);

    m_programMonitor = new MonitorWidget(MonitorType::Program, this);
    topRow->addWidget(m_programMonitor, 5);

    rootLayout->addLayout(topRow, 6);

    // ─── BOTTOM SECTION: INPUT DECK + AUDIO MIXER ────────────────
    auto* bottomSplitter = new QSplitter(Qt::Horizontal, this);

    // Input Deck Scroll Area
    auto* deckScroll = new QScrollArea(this);
    deckScroll->setWidgetResizable(true);
    deckScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    deckScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    deckScroll->setStyleSheet("background-color: #17181d; border: 1px solid #272933; border-radius: 4px;");

    m_inputDeckContainer = new QWidget(deckScroll);
    auto* deckLayout = new QHBoxLayout(m_inputDeckContainer);
    deckLayout->setContentsMargins(6, 6, 6, 6);
    deckLayout->setSpacing(6);
    deckLayout->addStretch();
    deckScroll->setWidget(m_inputDeckContainer);
    bottomSplitter->addWidget(deckScroll);

    // Pro Audio Mixer
    m_audioMixerWidget = new AudioMixerWidget(this);
    connect(m_audioMixerWidget, &AudioMixerWidget::channelGainChanged, this, [this](uint64_t id, float g) {
        m_context.audioMixer().setGain(id, g);
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::channelPanChanged, this, [this](uint64_t id, float pan) {
        m_context.audioMixer().setPan(id, pan);
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::channelMuteToggled, this, [this](uint64_t id, bool m) {
        m_context.audioMixer().setMute(id, m);
        auto it = m_inputCards.find(id);
        if (it != m_inputCards.end()) {
            it->second->setAudioActive(!m);
        }
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::channelSoloToggled, this, [this](uint64_t id, bool s) {
        m_context.audioMixer().setSolo(id, s);
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::channelAfvToggled, this, [this](uint64_t id, bool a) {
        m_context.audioMixer().setAFV(id, a);
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::masterGainChanged, this, [this](float g) {
        m_context.audioMixer().setBusMasterGain(Bus_Master, g);
    });
    connect(m_audioMixerWidget, &AudioMixerWidget::busGainChanged, this, [this](int busIndex, float g) {
        m_context.audioMixer().setBusMasterGain(static_cast<AudioBus>(busIndex), g);
    });
    bottomSplitter->addWidget(m_audioMixerWidget);

    // ── Right-Side Tab Panel: PTZ Camera Control + Instant Replay ─
    m_rightTabs = new QTabWidget(this);
    m_rightTabs->setTabPosition(QTabWidget::North);
    m_rightTabs->setMinimumWidth(280);

    m_ptzPanel = new PTZPanel(m_ptzController, this);
    m_rightTabs->addTab(m_ptzPanel, tr("🎥 PTZ"));

    m_replayPanel = new ReplayPanel(m_instantReplay, this);
    connect(m_replayPanel, &ReplayPanel::clipSaved, this, [](const QString& name) {
        Q_UNUSED(name)
    });
    m_rightTabs->addTab(m_replayPanel, tr("⏺ Replay"));

    bottomSplitter->addWidget(m_rightTabs);
    m_rightTabs->hide(); // Hidden by default: frees up width for inputs and audio mixer!

    bottomSplitter->setStretchFactor(0, 7);   // input deck (primary workspace)
    bottomSplitter->setStretchFactor(1, 5);   // audio mixer
    bottomSplitter->setStretchFactor(2, 3);   // PTZ / Replay tabs (when summoned)
    bottomSplitter->setSizes({ 960, 480, 280 });
    rootLayout->addWidget(bottomSplitter, 4);

    // ── Authentic vMix 29 Bottom Control Bar ─────────────────────
    setupBottomControlBar(rootLayout);

    setCentralWidget(centralWidget);
}

void MainWindow::setupMenuBar() {
    auto* menuBar = this->menuBar();

    auto* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->addAction(tr("New Preset"), QKeySequence::New, this, &MainWindow::onNewPreset);
    fileMenu->addAction(tr("Open Preset..."), QKeySequence::Open, this, &MainWindow::onOpenPreset);
    fileMenu->addAction(tr("Save Preset"), QKeySequence::Save, this, &MainWindow::onSavePreset);
    fileMenu->addAction(tr("Save Preset As..."), QKeySequence::SaveAs, this, &MainWindow::onSavePresetAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), QKeySequence::Quit, this, &QWidget::close);

    auto* inputMenu = menuBar->addMenu(tr("&Add Input"));
    inputMenu->addAction(tr("Video File..."), this, &MainWindow::onAddVideoFileInput);
    inputMenu->addAction(tr("Camera / Capture Card..."), this, &MainWindow::onAddCameraInput);
    inputMenu->addAction(tr("Zoom Meeting..."), this, &MainWindow::onAddZoomInput);
    inputMenu->addAction(tr("Open Media Transport (OMT)..."), this, &MainWindow::onAddOmtInput);
    inputMenu->addAction(tr("Title / Dynamic Graphics..."), this, &MainWindow::onAddTitleInput);

    auto* settingsMenu = menuBar->addMenu(tr("&Settings"));
    settingsMenu->addAction(tr("Outputs & Streaming..."), this, &MainWindow::onOpenSettings);
    settingsMenu->addAction(tr("Audio Buses..."), this, &MainWindow::onOpenSettings);
    settingsMenu->addAction(tr("Shortcuts & MIDI..."), this, &MainWindow::onOpenSettings);
}

void MainWindow::setupStatusBar() {
    auto* bar = statusBar();

    m_statusFps    = new QLabel("FPS: 60",       this);
    m_statusRender = new QLabel("Render: 1.2ms", this);
    m_statusGpu    = new QLabel("GPU: 14%",      this);
    m_statusCpu    = new QLabel("CPU: 4%",       this);
    m_statusStream = new QLabel("⬛ Stream: OFF", this);
    m_statusRecord = new QLabel("⬛ Record: OFF", this);

    bar->addPermanentWidget(m_statusStream);
    bar->addPermanentWidget(m_statusRecord);
    bar->addPermanentWidget(m_statusFps);
    bar->addPermanentWidget(m_statusRender);
    bar->addPermanentWidget(m_statusGpu);
    bar->addPermanentWidget(m_statusCpu);
}

void MainWindow::setupToolBar() {
    auto* tb = addToolBar(tr("Production"));
    tb->setMovable(false);
    tb->setIconSize(QSize(18, 18));

    // Stream ON/OFF toggle
    m_btnStream = new QPushButton(tr("🔴 Stream"), this);
    m_btnStream->setCheckable(true);
    m_btnStream->setToolTip(tr("Start / Stop RTMP stream"));
    m_btnStream->setStyleSheet(R"(
        QPushButton { background:#2a2d38; color:#dcdcdc; border:1px solid #404460;
                      border-radius:4px; padding:4px 10px; font-weight:bold; }
        QPushButton:checked { background:#c8002a; color:white; }
        QPushButton:hover   { background:#353848; }
    )");
    connect(m_btnStream, &QPushButton::toggled, this, &MainWindow::onStreamToggled);
    tb->addWidget(m_btnStream);

    tb->addSeparator();

    // Record ON/OFF toggle
    m_btnRecord = new QPushButton(tr("⏺ Record"), this);
    m_btnRecord->setCheckable(true);
    m_btnRecord->setToolTip(tr("Start / Stop recording"));
    m_btnRecord->setStyleSheet(R"(
        QPushButton { background:#2a2d38; color:#dcdcdc; border:1px solid #404460;
                      border-radius:4px; padding:4px 10px; font-weight:bold; }
        QPushButton:checked { background:#8b0000; color:white; }
        QPushButton:hover   { background:#353848; }
    )");
    connect(m_btnRecord, &QPushButton::toggled, this, &MainWindow::onRecordToggled);
    tb->addWidget(m_btnRecord);

    tb->addSeparator();

    // Web Controller link label
    auto* webLbl = new QLabel(tr("  🌐 Web: <a href='http://localhost:8088'>localhost:8088</a>"), this);
    webLbl->setOpenExternalLinks(true);
    webLbl->setStyleSheet("color: #90a0c0; font-size: 11px;");
    tb->addWidget(webLbl);
}

void MainWindow::setupBottomControlBar(QVBoxLayout* rootLayout) {
    auto* bar = new QWidget(this);
    bar->setFixedHeight(36);
    bar->setStyleSheet(R"(
        QWidget {
            background-color: #16171d;
            border-top: 1px solid #282a36;
            color: #dcdcdc;
            font-family: 'Inter', -apple-system, sans-serif;
            font-size: 11px;
        }
        QPushButton {
            background-color: #242732;
            border: 1px solid #363b4a;
            border-radius: 3px;
            color: #e2e8f0;
            padding: 3px 8px;
            font-weight: 600;
            font-size: 10px;
        }
        QPushButton:hover {
            background-color: #313746;
        }
    )");

    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(6, 2, 6, 2);
    layout->setSpacing(5);

    // ── LEFT: Add Input Button + Categories ──
    auto* btnAddInput = new QPushButton(tr("＋ Add Input"), bar);
    btnAddInput->setStyleSheet("QPushButton { background-color: #1d4ed8; color: white; border-color: #2563eb; font-weight: bold; padding: 4px 10px; } QPushButton:hover { background-color: #1e40af; }");
    auto* addMenu = new QMenu(btnAddInput);
    addMenu->addAction(tr("Video File..."), this, &MainWindow::onAddVideoFileInput);
    addMenu->addAction(tr("Camera / Capture Card..."), this, &MainWindow::onAddCameraInput);
    addMenu->addAction(tr("Title / Dynamic Graphics..."), this, &MainWindow::onAddTitleInput);
    addMenu->addAction(tr("Open Media Transport (OMT)..."), this, &MainWindow::onAddOmtInput);
    addMenu->addAction(tr("Zoom Meeting..."), this, &MainWindow::onAddZoomInput);
    btnAddInput->setMenu(addMenu);
    layout->addWidget(btnAddInput);

    // Category Tabs (vMix Authentic Color Chips Strip)
    m_categoryGroup = new QButtonGroup(this);
    m_categoryGroup->setExclusive(true);
    struct CatDef {
        const char* label;
        const char* color;
        const char* tip;
    };
    std::vector<CatDef> cats = {
        { "ALL", "#374151", "All Inputs" },
        { "●", "#dc2626", "Red Category (Live / Main)" },
        { "●", "#ea580c", "Orange Category (Media / VT)" },
        { "●", "#ca8a04", "Yellow Category (Replay)" },
        { "●", "#16a34a", "Green Category (Cameras)" },
        { "●", "#0891b2", "Cyan Category (NDI / Remote)" },
        { "●", "#2563eb", "Blue Category (Calls)" },
        { "●", "#9333ea", "Purple Category (Titles / Graphics)" }
    };
    for (int i = 0; i < static_cast<int>(cats.size()); ++i) {
        auto* catBtn = new QPushButton(cats[i].label, bar);
        catBtn->setToolTip(cats[i].tip);
        catBtn->setCheckable(true);
        if (i == 0) catBtn->setChecked(true);
        catBtn->setFixedSize(i == 0 ? 32 : 22, 22);
        QString style = QString(R"(
            QPushButton {
                background-color: %1;
                border: 1px solid rgba(255, 255, 255, 0.25);
                border-radius: 2px;
                color: white;
                font-size: %2px;
                font-weight: bold;
                padding: 0px;
            }
            QPushButton:hover {
                border-color: #ffffff;
            }
            QPushButton:checked {
                border: 2px solid #ffffff;
            }
        )").arg(cats[i].color).arg(i == 0 ? 9 : 12);
        catBtn->setStyleSheet(style);
        m_categoryGroup->addButton(catBtn, i);
        layout->addWidget(catBtn);
    }
    connect(m_categoryGroup, &QButtonGroup::idClicked, this, &MainWindow::onCategoryFilterChanged);

    layout->addStretch();

    // ── RIGHT: Master Broadcast Triggers (vMix 29 specification) ──
    auto* btnSave = new QPushButton(tr("Save"), bar);
    connect(btnSave, &QPushButton::clicked, this, &MainWindow::onSavePreset);
    layout->addWidget(btnSave);

    m_btnRecord = new QPushButton(tr("Record"), bar);
    m_btnRecord->setCheckable(true);
    m_btnRecord->setStyleSheet("QPushButton:checked { background-color: #dc2626; color: white; border-color: #ef4444; }");
    connect(m_btnRecord, &QPushButton::toggled, this, &MainWindow::onRecordToggled);
    layout->addWidget(m_btnRecord);

    m_btnExternal = new QPushButton(tr("External"), bar);
    m_btnExternal->setCheckable(true);
    m_btnExternal->setStyleSheet("QPushButton:checked { background-color: #059669; color: white; border-color: #10b981; }");
    connect(m_btnExternal, &QPushButton::toggled, this, &MainWindow::onExternalToggled);
    layout->addWidget(m_btnExternal);

    m_btnStream = new QPushButton(tr("Stream"), bar);
    m_btnStream->setCheckable(true);
    m_btnStream->setStyleSheet("QPushButton:checked { background-color: #ea580c; color: white; border-color: #f97316; }");
    connect(m_btnStream, &QPushButton::toggled, this, &MainWindow::onStreamToggled);
    layout->addWidget(m_btnStream);

    m_btnMultiCorder = new QPushButton(tr("MultiCorder"), bar);
    m_btnMultiCorder->setCheckable(true);
    m_btnMultiCorder->setStyleSheet("QPushButton:checked { background-color: #991b1b; color: white; border-color: #b91c1c; }");
    connect(m_btnMultiCorder, &QPushButton::toggled, this, &MainWindow::onMultiCorderToggled);
    layout->addWidget(m_btnMultiCorder);

    m_btnPlaylist = new QPushButton(tr("PlayList"), bar);
    m_btnPlaylist->setCheckable(true);
    m_btnPlaylist->setStyleSheet("QPushButton:checked { background-color: #2563eb; color: white; border-color: #3b82f6; }");
    connect(m_btnPlaylist, &QPushButton::toggled, this, &MainWindow::onPlaylistToggled);
    layout->addWidget(m_btnPlaylist);

    auto* btnFull = new QPushButton(tr("Fullscreen"), bar);
    btnFull->setCheckable(true);
    connect(btnFull, &QPushButton::toggled, this, [this](bool c) {
        if (c) showFullScreen(); else showNormal();
    });
    layout->addWidget(btnFull);

    auto* btnAudioToggle = new QPushButton(tr("Audio"), bar);
    btnAudioToggle->setCheckable(true);
    btnAudioToggle->setChecked(true);
    btnAudioToggle->setStyleSheet("QPushButton:checked { background-color: #15803d; color: white; border-color: #16a34a; }");
    connect(btnAudioToggle, &QPushButton::toggled, this, [this](bool c) {
        m_audioMixerWidget->setVisible(c);
    });
    layout->addWidget(btnAudioToggle);

    auto* btnSettings = new QPushButton(tr("Settings"), bar);
    connect(btnSettings, &QPushButton::clicked, this, &MainWindow::onOpenSettings);
    layout->addWidget(btnSettings);

    auto* btnMenu = new QPushButton(tr("☰"), bar);
    btnMenu->setFixedWidth(26);
    auto* toolsMenu = new QMenu(btnMenu);
    toolsMenu->addAction(tr("Audio Mixer Toggle"), [this, btnAudioToggle]() {
        bool visible = !m_audioMixerWidget->isVisible();
        m_audioMixerWidget->setVisible(visible);
        btnAudioToggle->setChecked(visible);
    });
    toolsMenu->addAction(tr("Audio Bus Manager"), this, &MainWindow::onOpenSettings);
    toolsMenu->addAction(tr("PTZ Camera Control"), [this]() {
        m_rightTabs->setVisible(!m_rightTabs->isVisible());
        if (m_rightTabs->isVisible()) m_rightTabs->setCurrentWidget(m_ptzPanel);
    });
    toolsMenu->addAction(tr("Instant Replay Quad View"), [this]() {
        m_rightTabs->setVisible(!m_rightTabs->isVisible());
        if (m_rightTabs->isVisible()) m_rightTabs->setCurrentWidget(m_replayPanel);
    });
    toolsMenu->addAction(tr("GT Title Designer"), [this]() {
        QMessageBox::information(this, tr("GT Title Designer"), tr("Launch GT Title Designer window for animated XAML/Vector graphics."));
    });
    toolsMenu->addAction(tr("Web Controller (port 8088)"), []() {
        QDesktopServices::openUrl(QUrl("http://localhost:8088"));
    });
    btnMenu->setMenu(toolsMenu);
    layout->addWidget(btnMenu);

    rootLayout->addWidget(bar);
}

void MainWindow::populateSampleInputs() {
    const std::string videoDir = "/Users/john/Movies/Deborah/Quality Time of Prayer/Verse 2/";
    std::string video1 = videoDir + "gck-prayer.mp4";
    std::string video2 = videoDir + "RECAP OO1.mp4";
    std::string video3 = videoDir + "3. river flowing sunlight cinematic aeriaL.mp4";

    uint64_t progId = 0;
    uint64_t prevId = 0;
    int index = 1;

    // 1. Stage Camera (Real Video Ingest)
    if (std::filesystem::exists(video1)) {
        auto src1 = std::make_shared<FFmpegSource>("Stage Camera");
        SourceSettings s1;
        s1.url = video1;
        s1.loop = true;
        if (src1->start(s1)) {
            m_context.addSource(src1);
            addInputCard(src1->id(), index++, "Stage Camera", "Video");
            progId = src1->id();
        }
    }

    // 2. Guest Feed / Recap Clip (Real Video Ingest)
    if (std::filesystem::exists(video2)) {
        auto src2 = std::make_shared<FFmpegSource>("Guest Feed");
        SourceSettings s2;
        s2.url = video2;
        s2.loop = true;
        if (src2->start(s2)) {
            m_context.addSource(src2);
            addInputCard(src2->id(), index++, "Guest Feed", "Video");
            prevId = src2->id();
        }
    }

    // 3. Dynamic Lower Third (vMix GT Title Engine)
    {
        auto src3 = std::make_shared<TitleSource>("Lower Third", TitleTemplate::LowerThird);
        SourceSettings s3;
        src3->setText("headline", "DR. JOHN TAYLOR");
        src3->setText("subline", "GLOBAL BROADCAST NETWORK • LIVE");
        if (src3->start(s3)) {
            m_context.addSource(src3);
            addInputCard(src3->id(), index++, "Lower Third", "Title");
        }
    }

    // 4. Scenic Aerial VT
    if (std::filesystem::exists(video3)) {
        auto src4 = std::make_shared<FFmpegSource>("Aerial VT");
        SourceSettings s4;
        s4.url = video3;
        s4.loop = true;
        if (src4->start(s4)) {
            m_context.addSource(src4);
            addInputCard(src4->id(), index++, "Aerial VT", "Video");
        }
    }

    if (progId > 0) m_context.setProgramSource(progId);
    if (prevId > 0) m_context.setPreviewSource(prevId);

    if (m_inputCards.find(progId) != m_inputCards.end()) m_inputCards[progId]->setActiveProgram(true);
    if (m_inputCards.find(prevId) != m_inputCards.end()) m_inputCards[prevId]->setActivePreview(true);
}

void MainWindow::addInputCard(uint64_t sourceId, int index, const QString& name, const QString& type) {
    if (m_inputCards.find(sourceId) != m_inputCards.end()) return;

    auto* card = new InputCardWidget(sourceId, index, name, type, m_inputDeckContainer);
    connect(card, &InputCardWidget::previewClicked, this, &MainWindow::onInputPreviewClicked);
    connect(card, &InputCardWidget::cutClicked, this, &MainWindow::onInputCutClicked);
    connect(card, &InputCardWidget::goClicked, this, &MainWindow::onInputGoClicked);
    connect(card, &InputCardWidget::closeClicked, this, &MainWindow::onInputCloseClicked);
    connect(card, &InputCardWidget::settingsClicked, this, &MainWindow::onInputSettingsClicked);
    connect(card, &InputCardWidget::audioToggled, this, [this](uint64_t id, bool active) {
        m_context.audioMixer().setMute(id, !active);
        m_audioMixerWidget->setChannelMute(id, !active);
    });
    connect(card, &InputCardWidget::overlayTriggered, this, &MainWindow::onInputOverlayToggled);
    connect(card, &InputCardWidget::seekRequested, this, [this](uint64_t id, double sec) {
        auto src = m_context.getSource(id);
        if (src) src->seek(sec);
    });
    connect(card, &InputCardWidget::playPauseToggled, this, [this](uint64_t id, bool play) {
        auto src = m_context.getSource(id);
        if (src) {
            if (play) src->resume(); else src->pause();
        }
    });
    connect(card, &InputCardWidget::loopToggled, this, [this](uint64_t id, bool loop) {
        auto src = m_context.getSource(id);
        if (src) src->setLoop(loop);
    });

    auto* layout = qobject_cast<QHBoxLayout*>(m_inputDeckContainer->layout());
    if (layout) {
        layout->insertWidget(layout->count() - 1, card);
    }
    m_inputCards[sourceId] = card;

    // Add to audio mixer panel
    m_audioMixerWidget->addChannel(sourceId, name);
}

void MainWindow::onInputCloseClicked(uint64_t sourceId) {
    auto it = m_inputCards.find(sourceId);
    if (it != m_inputCards.end()) {
        it->second->deleteLater();
        m_inputCards.erase(it);
    }
    m_audioMixerWidget->removeChannel(sourceId);
    m_context.removeSource(sourceId);
}

void MainWindow::onFrameTick() {
    m_frameCount++;

    constexpr double dt = 0.0166;

    // Advance engine & replay ring-buffer
    m_context.tick(dt);
    m_replayPanel->tick(dt);

    // Render Master Program and Preview frames
    auto progFrame = m_context.compositor().renderProgram();
    auto prevFrame = m_context.compositor().renderPreview();

    m_programMonitor->updateFrame(progFrame);
    m_previewMonitor->updateFrame(prevFrame);

    // Feed program frame into replay ring-buffer (camera 0)
    m_replayPanel->ingestFrame(progFrame, 0);

    // Update Tally state
    uint64_t progId = m_context.compositor().programSourceId();
    uint64_t prevId = m_context.compositor().previewSourceId();

    for (auto& pair : m_inputCards) {
        pair.second->setActiveProgram(pair.first == progId);
        pair.second->setActivePreview(pair.first == prevId);
    }

    // Smooth input card thumbnail updates (~30fps)
    if (m_frameCount % 2 == 0) {
        for (auto& pair : m_inputCards) {
            auto frame = m_context.compositor().getInputFrame(pair.first);
            if (frame && !frame->empty()) {
                pair.second->updateThumbnail(frame);
            }
        }
    }

    // Update monitor source names
    auto progSrc = m_context.getSource(progId);
    if (progSrc) m_programMonitor->setSourceName(QString::fromStdString(progSrc->name()));
    auto prevSrc = m_context.getSource(prevId);
    if (prevSrc) m_previewMonitor->setSourceName(QString::fromStdString(prevSrc->name()));

    // PTZ tally (fires auto-track on program source change)
    if (progId != m_lastProgramSourceId) {
        m_ptzController.onProgramSourceChanged(progId, m_lastProgramSourceId);
        m_lastProgramSourceId = progId;
    }

    // ── Feed Live Outputs (Recording, External Virtual Cam, MultiCorder) ──
    if (m_context.isRecording() && m_context.recordOutput()) {
        m_context.recordOutput()->pushVideo(progFrame);
    }
    if (m_context.isExternalActive() && m_context.virtualCam()) {
        m_context.virtualCam()->pushVideo(progFrame);
    }
    if (m_context.isMultiCorderActive()) {
        for (const auto& pair : m_inputCards) {
            auto frame = m_context.compositor().getInputFrame(pair.first);
            if (frame) {
                m_context.multiCorder().ingestVideo(pair.first, frame);
            }
        }
    }

    // ── Render matrix audio & feed hardware playback ──────────────
    auto masterAudio = m_context.audioMixer().renderMaster(800);
    m_context.audioMixer().renderBus(Bus_A, 800);
    m_context.audioMixer().renderBus(Bus_B, 800);
    if (masterAudio && !masterAudio->samples.empty()) {
        m_audioPlayer.enqueueFrame(masterAudio);
    }

    // Update Audio Meters on Mixer and Dual Monitors
    auto masterMeters = m_context.audioMixer().getMasterMeters();
    m_audioMixerWidget->updateMasterMeters(masterMeters.peakL, masterMeters.peakR);
    m_programMonitor->updateAudioMeters(masterMeters.peakL, masterMeters.peakR);

    auto prevMeters = m_context.audioMixer().getChannelMeters(prevId);
    m_previewMonitor->updateAudioMeters(prevMeters.peakL, prevMeters.peakR);

    auto busAMeters = m_context.audioMixer().getBusMeters(Bus_A);
    m_audioMixerWidget->updateBusMeters(Bus_A, busAMeters.peakL, busAMeters.peakR);

    auto busBMeters = m_context.audioMixer().getBusMeters(Bus_B);
    m_audioMixerWidget->updateBusMeters(Bus_B, busBMeters.peakL, busBMeters.peakR);

    for (auto& pair : m_inputCards) {
        auto chMeters = m_context.audioMixer().getChannelMeters(pair.first);
        pair.second->updateAudioMeter(chMeters.peakL, chMeters.peakR);
        m_audioMixerWidget->updateChannelMeters(pair.first, chMeters.peakL, chMeters.peakR);

        // Sync video playback scrubber & transport
        auto src = m_context.getSource(pair.first);
        if (src && src->durationSeconds() > 0.0) {
            pair.second->updatePlaybackState(src->positionSeconds(), src->durationSeconds(), src->isPaused(), src->isLooping());
        }
    }

    // Update T-Bar position if transitioning
    if (m_context.compositor().isTransitioning()) {
        m_transitionWidget->setTBarPosition(m_context.compositor().tbarPosition());
    }

    // Update digital broadcast clocks
    if (m_frameCount % 6 == 0) {
        int totalSec = static_cast<int>(m_frameCount / 60);
        int mins = totalSec / 60;
        int secs = totalSec % 60;
        int tenth = static_cast<int>((m_frameCount % 60) * 10 / 60);
        QString elapsedStr = QString("%1:%2.%3")
            .arg(mins, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'))
            .arg(tenth);
        QString todStr = QTime::currentTime().toString("hh:mm:ss AP");
        m_transitionWidget->updateClocks(elapsedStr, todStr);
    }

    // Update stream / record elapsed counters every ~60 frames
    if (m_frameCount % 60 == 0) {
        updateStreamRecordStatus();
    }

    if (m_frameCount == 90 || m_frameCount == 240) {
        QPixmap shot = this->grab();
        shot.save("/Users/john/.gemini/antigravity/brain/4ddaf33b-78f8-49bd-91bb-8a13f89bea07/screen_native_v15.png");
    }
}

void MainWindow::onCutRequested() {
    m_context.cut();
}

void MainWindow::onTransitionRequested(TransitionType type, int durationMs) {
    m_context.startTransition(type, durationMs);
}

void MainWindow::onTBarMoved(float position) {
    m_context.setTBarPosition(position);
}

void MainWindow::onOverlayToggled(int channel) {
    bool wasActive = m_context.compositor().isOverlayActive(channel);
    if (!wasActive) {
        uint64_t prevId = m_context.compositor().previewSourceId();
        if (prevId != 0) {
            m_context.setOverlaySource(channel, prevId);
        }
    }
    m_context.toggleOverlay(channel);
    bool nowActive = m_context.compositor().isOverlayActive(channel);
    m_transitionWidget->setOverlayActive(channel, nowActive);
    for (auto& pair : m_inputCards) {
        pair.second->setOverlayActive(channel, nowActive && (m_context.compositor().overlay(channel).sourceId == pair.first));
    }
}

void MainWindow::onInputOverlayToggled(uint64_t sourceId, int channel) {
    m_context.setOverlaySource(channel, sourceId);
    m_context.toggleOverlay(channel);
    bool nowActive = m_context.compositor().isOverlayActive(channel);
    m_transitionWidget->setOverlayActive(channel, nowActive);
    for (auto& pair : m_inputCards) {
        pair.second->setOverlayActive(channel, nowActive && (m_context.compositor().overlay(channel).sourceId == pair.first));
    }
}

void MainWindow::onFtbToggled() {
    m_context.compositor().toggleFTB();
}

void MainWindow::onInputPreviewClicked(uint64_t sourceId) {
    m_context.setPreviewSource(sourceId);
}

void MainWindow::onInputCutClicked(uint64_t sourceId) {
    m_context.setProgramSource(sourceId);
}

void MainWindow::onInputGoClicked(uint64_t sourceId) {
    m_context.quickPlay(sourceId);
}

void MainWindow::onExternalToggled(bool checked) {
    if (checked) {
        m_context.startExternal();
        if (m_btnExternal) m_btnExternal->setText(tr("External ON"));
        statusBar()->showMessage(tr("Virtual Camera Output active (CoreMediaIO shared buffer ready)"), 3000);
    } else {
        m_context.stopExternal();
        if (m_btnExternal) m_btnExternal->setText(tr("External"));
        statusBar()->showMessage(tr("Virtual Camera Output stopped"), 3000);
    }
}

void MainWindow::onMultiCorderToggled(bool checked) {
    if (checked) {
        m_context.startMultiCorder();
        if (m_btnMultiCorder) m_btnMultiCorder->setText(tr("ISO Rec ON"));
        statusBar()->showMessage(tr("MultiCorder ISO multi-camera recording active"), 3000);
    } else {
        m_context.stopMultiCorder();
        if (m_btnMultiCorder) m_btnMultiCorder->setText(tr("MultiCorder"));
        statusBar()->showMessage(tr("MultiCorder stopped"), 3000);
    }
}

void MainWindow::onPlaylistToggled(bool checked) {
    m_playlistRunning = checked;
    if (checked) {
        if (!m_playlistTimer) {
            m_playlistTimer = new QTimer(this);
            connect(m_playlistTimer, &QTimer::timeout, this, [this]() {
                if (m_inputCards.empty()) return;
                std::vector<uint64_t> ids;
                for (const auto& pair : m_inputCards) ids.push_back(pair.first);
                m_playlistIndex = (m_playlistIndex + 1) % ids.size();
                m_context.setPreviewSource(ids[m_playlistIndex]);
                m_context.startTransition(TransitionType::Fade, 500);
            });
        }
        m_playlistTimer->start(6000); // 6-second auto transition
        if (m_btnPlaylist) m_btnPlaylist->setText(tr("PlayList ON"));
        statusBar()->showMessage(tr("Automated PlayList started (6s cycle)"), 3000);
    } else {
        if (m_playlistTimer) m_playlistTimer->stop();
        if (m_btnPlaylist) m_btnPlaylist->setText(tr("PlayList"));
        statusBar()->showMessage(tr("PlayList stopped"), 3000);
    }
}

void MainWindow::onStreamToggled(bool checked) {
    if (checked) {
        m_context.startStreaming();
        m_streamSeconds = 0;
        if (m_btnStream) m_btnStream->setText(tr("🔴 Streaming…"));
        statusBar()->showMessage(tr("Live Streaming started"), 3000);
    } else {
        m_context.stopStreaming();
        if (m_btnStream) m_btnStream->setText(tr("Stream"));
        m_statusStream->setText(tr("⬛ Stream: OFF"));
        m_statusStream->setStyleSheet("");
        statusBar()->showMessage(tr("Live Streaming stopped"), 3000);
    }
}

void MainWindow::onRecordToggled(bool checked) {
    if (checked) {
        m_context.startRecording();
        m_recordSeconds = 0;
        if (m_btnRecord) m_btnRecord->setText(tr("⏺ Recording…"));
        std::string recPath = m_context.recordOutput() ? m_context.recordOutput()->currentFilePath() : "";
        statusBar()->showMessage(tr("Recording started: %1").arg(QString::fromStdString(recPath)), 4000);
    } else {
        std::string recPath = m_context.recordOutput() ? m_context.recordOutput()->currentFilePath() : "";
        m_context.stopRecording();
        if (m_btnRecord) m_btnRecord->setText(tr("Record"));
        m_statusRecord->setText(tr("⬛ Record: OFF"));
        m_statusRecord->setStyleSheet("");
        statusBar()->showMessage(tr("Recording saved to: %1").arg(QString::fromStdString(recPath)), 5000);
    }
}

void MainWindow::updateStreamRecordStatus() {
    if (m_context.isStreaming()) {
        ++m_streamSeconds;
        int h = m_streamSeconds / 3600;
        int m = (m_streamSeconds % 3600) / 60;
        int s = m_streamSeconds % 60;
        m_statusStream->setText(tr("🔴 Stream: %1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
        m_statusStream->setStyleSheet("color: #ff4040; font-weight: bold;");
    }
    if (m_context.isRecording()) {
        ++m_recordSeconds;
        int h = m_recordSeconds / 3600;
        int m = (m_recordSeconds % 3600) / 60;
        int s = m_recordSeconds % 60;
        m_statusRecord->setText(tr("⏺ Rec: %1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0')));
        m_statusRecord->setStyleSheet("color: #ff8040; font-weight: bold;");
    }
}

void MainWindow::onAddZoomInput() {
    bool ok = false;
    QString meetingId = QInputDialog::getText(this, "Join Zoom Meeting", "Meeting ID:", QLineEdit::Normal, "", &ok);
    if (!ok || meetingId.isEmpty()) return;

    QString passcode = QInputDialog::getText(this, "Zoom Passcode", "Passcode:", QLineEdit::Normal, "", &ok);

    ZoomMeetingCredentials creds;
    creds.meetingNumber = meetingId.toStdString();
    creds.passcode = passcode.toStdString();
    creds.displayName = "StreamMix Switcher";

    auto& zoomMgr = ZoomMeetingManager::instance();
    zoomMgr.joinMeeting(creds);

    auto zoomSrc = zoomMgr.createZoomSource("Zoom Caller", ZoomTargetMode::ActiveSpeaker);
    m_context.addSource(zoomSrc);

    int nextIndex = static_cast<int>(m_inputCards.size()) + 1;
    addInputCard(zoomSrc->id(), nextIndex, "Zoom Caller", "Zoom");

    QMessageBox::information(this, "Zoom Connected", "Joined Zoom Meeting " + meetingId + " as an active input!");
}

void MainWindow::onNewPreset() {
    auto res = QMessageBox::question(this, tr("New Preset"),
        tr("Create a new production preset? Unsaved changes will be lost."),
        QMessageBox::Yes | QMessageBox::No);
    if (res != QMessageBox::Yes) return;

    m_currentPresetPath.clear();
    setWindowTitle(tr("StreamMix v1.0 — Untitled Production"));

    m_context.shutdown();
    m_context.initialize();

    for (auto& pair : m_inputCards) {
        delete pair.second;
    }
    m_inputCards.clear();

    populateSampleInputs();
}

void MainWindow::onOpenPreset() {
    QString path = QFileDialog::getOpenFileName(this, tr("Open Production Preset"), "",
        tr("StreamMix Preset (*.stmix *.vmix);;All Files (*.*)"));
    if (path.isEmpty()) return;

    ProductionPreset preset;
    if (!PresetManager::loadFromFile(path.toStdString(), preset)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to load preset file: %1").arg(path));
        return;
    }

    m_currentPresetPath = path;
    QFileInfo fi(path);
    setWindowTitle(tr("StreamMix v1.0 — %1").arg(fi.fileName()));

    for (auto& pair : m_inputCards) {
        delete pair.second;
    }
    m_inputCards.clear();

    int idx = 1;
    for (const auto& inp : preset.inputs) {
        addInputCard(inp.id, idx++, QString::fromStdString(inp.name), QString::fromStdString(inp.type));
        m_context.audioMixer().setGain(inp.id, inp.volume);
        m_context.audioMixer().setPan(inp.id, inp.pan);
        m_context.audioMixer().setMute(inp.id, inp.muted);
        m_context.audioMixer().setSolo(inp.id, inp.solo);
        m_context.audioMixer().setAFV(inp.id, inp.afv);
    }

    if (preset.programSourceId > 0) m_context.setProgramSource(preset.programSourceId);
    if (preset.previewSourceId > 0) m_context.setPreviewSource(preset.previewSourceId);
    m_context.audioMixer().setMasterVolume(preset.masterVolume);

    QMessageBox::information(this, tr("Preset Loaded"), tr("Loaded production preset: %1 (%2 inputs)")
        .arg(fi.fileName()).arg(preset.inputs.size()));
}

void MainWindow::onSavePreset() {
    if (m_currentPresetPath.isEmpty()) {
        onSavePresetAs();
    } else {
        ProductionPreset preset;
        QFileInfo fi(m_currentPresetPath);
        preset.name = fi.baseName().toStdString();
        preset.programSourceId = m_context.compositor().programSourceId();
        preset.previewSourceId = m_context.compositor().previewSourceId();
        preset.masterVolume = m_context.audioMixer().masterVolume();

        for (const auto& pair : m_inputCards) {
            PresetInputData inp;
            inp.id = pair.first;
            inp.volume = m_context.audioMixer().getGain(pair.first);
            inp.pan = m_context.audioMixer().getPan(pair.first);
            inp.muted = m_context.audioMixer().isMuted(pair.first);
            inp.solo = m_context.audioMixer().isSolo(pair.first);
            inp.afv = m_context.audioMixer().isAFV(pair.first);
            preset.inputs.push_back(inp);
        }

        if (PresetManager::saveToFile(m_currentPresetPath.toStdString(), preset)) {
            statusBar()->showMessage(tr("Preset saved to %1").arg(m_currentPresetPath), 3000);
        } else {
            QMessageBox::warning(this, tr("Error"), tr("Could not save preset to %1").arg(m_currentPresetPath));
        }
    }
}

void MainWindow::onSavePresetAs() {
    QString path = QFileDialog::getSaveFileName(this, tr("Save Production Preset As"), "",
        tr("StreamMix Preset (*.stmix);;vMix Preset (*.vmix)"));
    if (path.isEmpty()) return;

    if (!path.endsWith(".stmix", Qt::CaseInsensitive) && !path.endsWith(".vmix", Qt::CaseInsensitive)) {
        path += ".stmix";
    }

    m_currentPresetPath = path;
    QFileInfo fi(path);
    setWindowTitle(tr("StreamMix v1.0 — %1").arg(fi.fileName()));
    onSavePreset();
}

void MainWindow::onAddVideoFileInput() {
    QString filePath = QFileDialog::getOpenFileName(this, tr("Add Video File Input"), "",
        tr("Video Files (*.mp4 *.mov *.mkv *.avi *.ts *.m4v);;All Files (*.*)"));
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    auto src = std::make_shared<FFmpegSource>(fi.fileName().toStdString());
    SourceSettings settings;
    settings.url = filePath.toStdString();
    settings.loop = true;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps_num = 60;
    settings.fps_den = 1;
    src->start(settings);

    m_context.addSource(src);
    int nextIndex = static_cast<int>(m_inputCards.size()) + 1;
    addInputCard(src->id(), nextIndex, fi.fileName(), "Video");
}

void MainWindow::onAddCameraInput() {
    bool ok = false;
    QString camName = QInputDialog::getText(this, tr("Add Camera / Capture Device"),
        tr("Device Name or Hardware Index:"), QLineEdit::Normal, "FaceTime HD Camera", &ok);
    if (!ok || camName.isEmpty()) return;

    auto src = std::make_shared<FFmpegSource>(camName.toStdString());
    SourceSettings settings;
    settings.url = camName.toStdString();
    settings.width = 1920;
    settings.height = 1080;
    settings.fps_num = 60;
    settings.fps_den = 1;
    src->start(settings);

    m_context.addSource(src);
    int nextIndex = static_cast<int>(m_inputCards.size()) + 1;
    addInputCard(src->id(), nextIndex, camName, "Camera");
}

void MainWindow::onAddOmtInput() {
    bool ok = false;
    QString streamUrl = QInputDialog::getText(this, tr("Add Open Media Transport (OMT) Input"),
        tr("OMT Stream URI (omt://host:port/stream):"), QLineEdit::Normal, "omt://192.168.1.120:9000/live", &ok);
    if (!ok || streamUrl.isEmpty()) return;

    auto src = std::make_shared<OMTSource>("OMT Remote");
    SourceSettings settings;
    settings.url = streamUrl.toStdString();
    src->start(settings);

    m_context.addSource(src);
    int nextIndex = static_cast<int>(m_inputCards.size()) + 1;
    addInputCard(src->id(), nextIndex, "OMT Remote", "OMT");
}

void MainWindow::onAddTitleInput() {
    QStringList items;
    items << tr("Lower Third - News") << tr("Sports Scoreboard") << tr("Breaking News Ticker") << tr("Host Banner");
    bool ok = false;
    QString item = QInputDialog::getItem(this, tr("Add Dynamic Title Input"),
        tr("Choose Title Template:"), items, 0, false, &ok);
    if (!ok || item.isEmpty()) return;

    TitleTemplate tmpl = TitleTemplate::LowerThird;
    if (item.contains("Scoreboard")) tmpl = TitleTemplate::Scoreboard;
    else if (item.contains("Ticker")) tmpl = TitleTemplate::BreakingNews;

    auto src = std::make_shared<TitleSource>(item.toStdString(), tmpl);
    SourceSettings settings;
    settings.width = 1920;
    settings.height = 1080;
    settings.fps_num = 60;
    settings.fps_den = 1;
    src->start(settings);

    m_context.addSource(src);
    int nextIndex = static_cast<int>(m_inputCards.size()) + 1;
    addInputCard(src->id(), nextIndex, item, "Title");
}

void MainWindow::onCategoryFilterChanged(int id) {
    for (auto& pair : m_inputCards) {
        auto* card = pair.second;
        if (!card) continue;
        QString type = card->typeName();
        if (id == 0) { // All
            card->setVisible(true);
        } else if (id == 1) { // Red: Program Active / Live
            card->setVisible(card->isProgram() || card->isPreview());
        } else if (id == 2) { // Orange: Media / VT / Video
            bool match = type.contains("Video", Qt::CaseInsensitive) ||
                         type.contains("Media", Qt::CaseInsensitive) ||
                         type.contains("VT", Qt::CaseInsensitive) ||
                         type.contains("Image", Qt::CaseInsensitive);
            card->setVisible(match);
        } else if (id == 3) { // Yellow: Replay
            bool match = type.contains("Replay", Qt::CaseInsensitive);
            card->setVisible(match);
        } else if (id == 4) { // Green: Cameras / Capture
            bool match = type.contains("Camera", Qt::CaseInsensitive) ||
                         type.contains("Capture", Qt::CaseInsensitive);
            card->setVisible(match);
        } else if (id == 5) { // Cyan: NDI / Remote
            bool match = type.contains("NDI", Qt::CaseInsensitive) ||
                         type.contains("OMT", Qt::CaseInsensitive) ||
                         type.contains("SRT", Qt::CaseInsensitive) ||
                         type.contains("RTMP", Qt::CaseInsensitive);
            card->setVisible(match);
        } else if (id == 6) { // Blue: Calls / Zoom
            bool match = type.contains("Call", Qt::CaseInsensitive) ||
                         type.contains("Zoom", Qt::CaseInsensitive);
            card->setVisible(match);
        } else if (id == 7) { // Purple: Titles / Graphics
            bool match = type.contains("Title", Qt::CaseInsensitive) ||
                         type.contains("Graphic", Qt::CaseInsensitive) ||
                         type.contains("Text", Qt::CaseInsensitive);
            card->setVisible(match);
        } else {
            card->setVisible(true);
        }
    }
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(m_context, this);
    dlg.exec();
}

void MainWindow::onInputSettingsClicked(uint64_t sourceId) {
    auto it = m_inputCards.find(sourceId);
    if (it == m_inputCards.end()) return;
    auto* card = it->second;

    InputSettingsDialog dlg(sourceId, card->index(), card->name(), card->typeName(), this);
    connect(&dlg, &InputSettingsDialog::nameChanged, this, [this](uint64_t id, const QString& newName) {
        auto c = m_inputCards.find(id);
        if (c != m_inputCards.end()) {
            c->second->setName(newName);
        }
    });
    connect(&dlg, &InputSettingsDialog::gainChanged, this, [this](uint64_t id, float gainLinear) {
        m_context.audioMixer().setGain(id, gainLinear);
    });
    dlg.exec();
}

} // namespace sm
