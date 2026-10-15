// streammix-app/src/widgets/replay_panel.cpp
// Instant Replay 2.0 UI Panel implementation
#include "replay_panel.h"

#include <QFrame>
#include <QSizePolicy>
#include <QFont>
#include <QMessageBox>
#include <cmath>

namespace SM {

// ─── construction ────────────────────────────────────────────────────────────

ReplayPanel::ReplayPanel(InstantReplay& replay, QWidget* parent)
    : QWidget(parent), m_replay(replay)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    buildUi();

    // flash timer for "Saved!" label
    m_savedTimer = new QTimer(this);
    m_savedTimer->setSingleShot(true);
    connect(m_savedTimer, &QTimer::timeout, this, [this]() {
        m_savedLabel->setVisible(false);
    });
}

// ─── public API ──────────────────────────────────────────────────────────────

void ReplayPanel::tick(double dt)
{
    m_replay.tick(dt);
}

void ReplayPanel::ingestFrame(const FrameData& frame, int cameraChannel)
{
    m_replay.ingestFrame(frame, cameraChannel);
}

// ─── build UI ────────────────────────────────────────────────────────────────

void ReplayPanel::buildUi()
{
    auto* vl = new QVBoxLayout(this);
    vl->setContentsMargins(4, 4, 4, 4);
    vl->setSpacing(6);

    // Title
    auto* title = new QLabel(tr("⏺  Instant Replay 2.0"), this);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSize(11);
    title->setFont(tf);
    vl->addWidget(title);

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    vl->addWidget(sep);

    buildCaptureRow(vl);
    buildPlayheadRow(vl);
    buildSpeedRow(vl);
    buildClipList(vl);

    // "Saved!" flash label (hidden by default)
    m_savedLabel = new QLabel(tr("✔ Clip Saved!"), this);
    m_savedLabel->setStyleSheet("color: #00c800; font-weight: bold;");
    m_savedLabel->setVisible(false);
    vl->addWidget(m_savedLabel);

    vl->addStretch();
}

void ReplayPanel::buildCaptureRow(QVBoxLayout* vl)
{
    // Camera selector row
    auto* camRow = new QHBoxLayout();
    camRow->addWidget(new QLabel(tr("Camera:"), this));
    m_cameraCombo = new QComboBox(this);
    for (int i = 0; i < 4; ++i)
        m_cameraCombo->addItem(tr("Cam %1").arg(i + 1));
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ReplayPanel::onCameraChanged);
    camRow->addWidget(m_cameraCombo);
    camRow->addStretch();
    vl->addLayout(camRow);

    // Capture shortcuts row
    auto* capGroup = new QGroupBox(tr("Capture"), this);
    auto* capRow   = new QHBoxLayout(capGroup);

    m_btnMarkIn  = new QPushButton(tr("Mark In"),  this);
    m_btnMarkOut = new QPushButton(tr("Mark Out"), this);
    m_btnLast10  = new QPushButton(tr("Last 10s"), this);
    m_btnLast30  = new QPushButton(tr("Last 30s"), this);

    for (auto* b : {m_btnMarkIn, m_btnMarkOut, m_btnLast10, m_btnLast30})
        capRow->addWidget(b);

    connect(m_btnMarkIn,  &QPushButton::clicked, this, &ReplayPanel::onMarkIn);
    connect(m_btnMarkOut, &QPushButton::clicked, this, &ReplayPanel::onMarkOut);
    connect(m_btnLast10,  &QPushButton::clicked, this, &ReplayPanel::onCaptureLast10);
    connect(m_btnLast30,  &QPushButton::clicked, this, &ReplayPanel::onCaptureLast30);

    vl->addWidget(capGroup);
}

void ReplayPanel::buildPlayheadRow(QVBoxLayout* vl)
{
    auto* phGroup = new QGroupBox(tr("Playheads"), this);
    auto* phl     = new QVBoxLayout(phGroup);

    // Playhead selector tabs
    auto* tabRow = new QHBoxLayout();
    for (int i = 0; i < kNumPlayheads; ++i) {
        auto* btn = new QPushButton(tr("PH %1").arg(i + 1), this);
        btn->setCheckable(true);
        btn->setChecked(i == 0);
        m_phTabs[i] = btn;
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            selectPlayhead(i);
        });
        tabRow->addWidget(btn);
    }
    phl->addLayout(tabRow);

    // Transport controls
    auto* xportRow = new QHBoxLayout();
    m_btnStepB  = new QPushButton(tr("⟨"),    this);
    m_btnPlay   = new QPushButton(tr("▶ Play"),   this);
    m_btnPause  = new QPushButton(tr("⏸ Pause"),  this);
    m_btnStop   = new QPushButton(tr("⏹ Stop"),   this);
    m_btnStepF  = new QPushButton(tr("⟩"),    this);

    m_btnStepB->setFixedWidth(36);
    m_btnStepF->setFixedWidth(36);

    for (auto* b : {m_btnStepB, m_btnPlay, m_btnPause, m_btnStop, m_btnStepF})
        xportRow->addWidget(b);

    connect(m_btnPlay,  &QPushButton::clicked, this, &ReplayPanel::onPlay);
    connect(m_btnPause, &QPushButton::clicked, this, &ReplayPanel::onPause);
    connect(m_btnStop,  &QPushButton::clicked, this, &ReplayPanel::onStop);
    connect(m_btnStepB, &QPushButton::clicked, this, &ReplayPanel::onStepBack);
    connect(m_btnStepF, &QPushButton::clicked, this, &ReplayPanel::onStepForward);

    phl->addLayout(xportRow);
    vl->addWidget(phGroup);
}

void ReplayPanel::buildSpeedRow(QVBoxLayout* vl)
{
    auto* speedGroup = new QGroupBox(tr("Playback Speed"), this);
    auto* sl         = new QHBoxLayout(speedGroup);

    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(10, 200);
    m_speedSlider->setValue(100);
    m_speedSlider->setTickInterval(10);
    m_speedSlider->setTickPosition(QSlider::TicksBelow);

    m_speedLabel = new QLabel(tr("100%"), this);
    m_speedLabel->setFixedWidth(44);

    sl->addWidget(m_speedSlider);
    sl->addWidget(m_speedLabel);

    connect(m_speedSlider, &QSlider::valueChanged,
            this, &ReplayPanel::onSpeedChanged);

    // Quad-view toggle
    m_btnQuadView = new QPushButton(tr("Quad-View OFF"), this);
    m_btnQuadView->setCheckable(true);
    connect(m_btnQuadView, &QPushButton::toggled, this, [this](bool on) {
        m_quadViewOn = on;
        m_btnQuadView->setText(on ? tr("Quad-View ON") : tr("Quad-View OFF"));
        m_btnQuadView->setStyleSheet(
            on ? "background-color: #00a060; color: white; font-weight: bold;"
               : "");
        emit quadViewToggled(on);
    });

    auto* sl2 = new QHBoxLayout();
    sl2->addWidget(m_btnQuadView);
    sl2->addStretch();

    auto* vg = new QVBoxLayout();
    vg->addLayout(sl2);

    auto* outerVl = new QVBoxLayout(speedGroup);
    outerVl->addLayout(sl);
    outerVl->addLayout(sl2);

    // Replace the layout we set above
    delete speedGroup->layout();
    speedGroup->setLayout(outerVl);

    vl->addWidget(speedGroup);
}

void ReplayPanel::buildClipList(QVBoxLayout* vl)
{
    auto* clGroup = new QGroupBox(tr("Saved Clips"), this);
    auto* cll     = new QVBoxLayout(clGroup);

    m_clipList = new QListWidget(this);
    m_clipList->setAlternatingRowColors(true);
    cll->addWidget(m_clipList);

    auto* btnRow = new QHBoxLayout();
    m_btnDelClip = new QPushButton(tr("Delete Clip"), this);
    connect(m_btnDelClip, &QPushButton::clicked, this, &ReplayPanel::onDeleteClip);
    auto* refreshBtn = new QPushButton(tr("Refresh"), this);
    connect(refreshBtn, &QPushButton::clicked, this, &ReplayPanel::refreshClipList);
    btnRow->addWidget(m_btnDelClip);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch();
    cll->addLayout(btnRow);

    vl->addWidget(clGroup);
}

// ─── helpers ─────────────────────────────────────────────────────────────────

void ReplayPanel::selectPlayhead(int idx)
{
    m_activePlayhead = idx;
    for (int i = 0; i < kNumPlayheads; ++i)
        m_phTabs[i]->setChecked(i == idx);
    m_replay.setActivePlayhead(idx);
}

void ReplayPanel::flashSaved()
{
    m_savedLabel->setVisible(true);
    m_savedTimer->start(2000);
}

// ─── slots ───────────────────────────────────────────────────────────────────

void ReplayPanel::onPlay()
{
    double speed = m_speedSlider->value() / 100.0;
    m_replay.play(m_activePlayhead, speed);
}

void ReplayPanel::onPause()
{
    m_replay.pause(m_activePlayhead);
}

void ReplayPanel::onStop()
{
    m_replay.stop(m_activePlayhead);
}

void ReplayPanel::onStepBack()
{
    m_replay.stepBack(m_activePlayhead);
}

void ReplayPanel::onStepForward()
{
    m_replay.stepForward(m_activePlayhead);
}

void ReplayPanel::onMarkIn()
{
    m_replay.markIn(m_activePlayhead);
}

void ReplayPanel::onMarkOut()
{
    m_replay.markOut(m_activePlayhead);
}

void ReplayPanel::onCaptureLast10()
{
    auto clip = m_replay.captureLast(10.0);
    if (!clip.name.empty()) {
        refreshClipList();
        flashSaved();
        emit clipSaved(QString::fromStdString(clip.name));
    }
}

void ReplayPanel::onCaptureLast30()
{
    auto clip = m_replay.captureLast(30.0);
    if (!clip.name.empty()) {
        refreshClipList();
        flashSaved();
        emit clipSaved(QString::fromStdString(clip.name));
    }
}

void ReplayPanel::onToggleQuadView()
{
    m_btnQuadView->toggle();   // triggers the toggled signal
}

void ReplayPanel::onSpeedChanged(int value)
{
    m_speedLabel->setText(tr("%1%").arg(value));
    double factor = value / 100.0;
    // Update active playhead speed if already playing
    m_replay.setPlaybackSpeed(m_activePlayhead, factor);
    emit speedChanged(factor);
}

void ReplayPanel::onCameraChanged(int index)
{
    m_activeCamera = index;
}

void ReplayPanel::onDeleteClip()
{
    auto* item = m_clipList->currentItem();
    if (!item) return;
    QString name = item->text();
    m_replay.deleteClip(name.toStdString());
    delete m_clipList->takeItem(m_clipList->row(item));
}

void ReplayPanel::refreshClipList()
{
    m_clipList->clear();
    for (const auto& clip : m_replay.savedClips()) {
        m_clipList->addItem(QString::fromStdString(clip.name));
    }
}

} // namespace SM
