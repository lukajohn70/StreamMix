#include "main_window.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QStatusBar>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QToolBar>
#include <QTabWidget>
#include <QDockWidget>
#include <chrono>

namespace sm {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_instantReplay(InstantReplayConfig{})
{
    m_context.initialize();

    setupUI();
    setupToolBar();
    setupMenuBar();
    setupStatusBar();
    populateSampleInputs();

    // ── Web Controller on port 8088 ──────────────────────────────
    m_webServer = new WebServer(m_context, 8088, this);
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
    if (m_webServer)   m_webServer->stop();
    if (m_midiController) m_midiController->disconnect();
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
    connect(m_audioMixerWidget, &AudioMixerWidget::channelMuteToggled, this, [this](uint64_t id, bool m) {
        m_context.audioMixer().setMute(id, m);
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
    bottomSplitter->addWidget(m_audioMixerWidget);

    // ── Right-Side Tab Panel: PTZ Camera Control + Instant Replay ─
    auto* rightTabs = new QTabWidget(this);
    rightTabs->setTabPosition(QTabWidget::North);
    rightTabs->setMinimumWidth(280);

    m_ptzPanel = new PTZPanel(m_ptzController, this);
    rightTabs->addTab(m_ptzPanel, tr("🎥 PTZ"));

    m_replayPanel = new SM::ReplayPanel(m_instantReplay, this);
    connect(m_replayPanel, &SM::ReplayPanel::clipSaved, this, [](const QString& name) {
        // Could update a status indicator or clip-browser panel
        Q_UNUSED(name)
    });
    rightTabs->addTab(m_replayPanel, tr("⏺ Replay"));

    bottomSplitter->addWidget(rightTabs);

    bottomSplitter->setStretchFactor(0, 6);  // input deck
    bottomSplitter->setStretchFactor(1, 3);  // audio mixer
    bottomSplitter->setStretchFactor(2, 2);  // PTZ / Replay tabs
    rootLayout->addWidget(bottomSplitter, 4);

    setCentralWidget(centralWidget);
}

void MainWindow::setupMenuBar() {
    auto* menuBar = this->menuBar();

    auto* fileMenu = menuBar->addMenu(tr("&File"));
    fileMenu->addAction(tr("New Preset"), this, &MainWindow::onNewPreset, QKeySequence::New);
    fileMenu->addAction(tr("Open Preset..."), this, &MainWindow::onOpenPreset, QKeySequence::Open);
    fileMenu->addAction(tr("Save Preset"), this, &MainWindow::onSavePreset, QKeySequence::Save);
    fileMenu->addAction(tr("Save Preset As..."), this, &MainWindow::onSavePresetAs, QKeySequence::SaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);

    auto* inputMenu = menuBar->addMenu(tr("&Add Input"));
    inputMenu->addAction(tr("Video File..."), this, &MainWindow::onAddVideoFileInput);
    inputMenu->addAction(tr("Camera / Capture Card..."), this, &MainWindow::onAddCameraInput);
    inputMenu->addAction(tr("Zoom Meeting..."), this, &MainWindow::onAddZoomInput);
    inputMenu->addAction(tr("Open Media Transport (OMT)..."), this, &MainWindow::onAddOmtInput);
    inputMenu->addAction(tr("Title / Dynamic Graphics..."), this, &MainWindow::onAddTitleInput);

    auto* settingsMenu = menuBar->addMenu(tr("&Settings"));
    settingsMenu->addAction(tr("Outputs & Streaming..."), [](){});
    settingsMenu->addAction(tr("Audio Buses..."), [](){});
    settingsMenu->addAction(tr("Shortcuts & MIDI..."), [](){});
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

void MainWindow::populateSampleInputs() {
    // 1. Stage Camera
    addInputCard(1, 1, "Stage Camera", "Camera");
    // 2. Guest Feed (Remote)
    addInputCard(2, 2, "Guest Feed", "Remote");
    // 3. Lower Third
    addInputCard(3, 3, "Lower Third", "Title");
    // 4. Clip VT
    addInputCard(4, 4, "Promo Video", "Video");

    m_context.setProgramSource(1);
    m_context.setPreviewSource(2);

    if (m_inputCards.find(1) != m_inputCards.end()) m_inputCards[1]->setActiveProgram(true);
    if (m_inputCards.find(2) != m_inputCards.end()) m_inputCards[2]->setActivePreview(true);
}

void MainWindow::addInputCard(uint64_t sourceId, int index, const QString& name, const QString& type) {
    if (m_inputCards.find(sourceId) != m_inputCards.end()) return;

    auto* card = new InputCardWidget(sourceId, index, name, type, m_inputDeckContainer);
    connect(card, &InputCardWidget::previewClicked, this, &MainWindow::onInputPreviewClicked);
    connect(card, &InputCardWidget::cutClicked, this, &MainWindow::onInputCutClicked);
    connect(card, &InputCardWidget::goClicked, this, &MainWindow::onInputGoClicked);

    auto* layout = qobject_cast<QHBoxLayout*>(m_inputDeckContainer->layout());
    if (layout) {
        layout->insertWidget(layout->count() - 1, card);
    }
    m_inputCards[sourceId] = card;

    // Add to audio mixer panel
    m_audioMixerWidget->addChannel(sourceId, name);
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

    // PTZ tally (fires auto-track on program source change)
    m_ptzController.onProgramSourceChanged(static_cast<int>(progId));

    // Update Audio Meters
    auto masterMeters = m_context.audioMixer().getMasterMeters();
    m_audioMixerWidget->updateMasterMeters(masterMeters.peakL, masterMeters.peakR);

    for (auto& pair : m_inputCards) {
        auto chMeters = m_context.audioMixer().getChannelMeters(pair.first);
        pair.second->updateAudioMeter(chMeters.peakL, chMeters.peakR);
        m_audioMixerWidget->updateChannelMeters(pair.first, chMeters.peakL, chMeters.peakR);
    }

    // Update T-Bar position if transitioning
    if (m_context.compositor().isTransitioning()) {
        m_transitionWidget->setTBarPosition(m_context.compositor().tbarPosition());
    }

    // Update stream / record elapsed counters every ~60 frames
    if (m_frameCount % 60 == 0) {
        updateStreamRecordStatus();
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
    m_context.toggleOverlay(channel);
    bool active = m_context.compositor().isOverlayActive(channel);
    m_transitionWidget->setOverlayActive(channel, active);
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

void MainWindow::onStreamToggled(bool checked) {
    if (checked) {
        m_context.startStreaming();
        m_streamSeconds = 0;
        m_btnStream->setText(tr("🔴 Streaming…"));
    } else {
        m_context.stopStreaming();
        m_btnStream->setText(tr("🔴 Stream"));
        m_statusStream->setText(tr("⬛ Stream: OFF"));
    }
}

void MainWindow::onRecordToggled(bool checked) {
    if (checked) {
        m_context.startRecording();
        m_recordSeconds = 0;
        m_btnRecord->setText(tr("⏺ Recording…"));
    } else {
        m_context.stopRecording();
        m_btnRecord->setText(tr("⏺ Record"));
        m_statusRecord->setText(tr("⬛ Record: OFF"));
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
    settings.format = "avfoundation";
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
    else if (item.contains("Ticker")) tmpl = TitleTemplate::Ticker;

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

} // namespace sm
