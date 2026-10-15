// streammix-app/src/widgets/replay_panel.h
// Instant Replay 2.0 UI Panel — wraps libstreammix InstantReplay engine
// Provides 4-playhead control, speed slider, Mark In/Out, Quad-View toggle,
// camera selector, clip list and quick-capture shortcuts.
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QListWidget>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QSpinBox>
#include <array>

#include "streammix/instant_replay.h"

namespace SM {

/// ReplayPanel — Qt6 widget that wraps InstantReplay
///
/// Layout (top → bottom):
///   [Camera selector]  [Capture: Last-10s | Last-30s | Mark In | Mark Out]
///   ──────────────────────────────────────────────
///   Playhead tabs (1–4), each with:
///     [▶ Play] [⏸ Pause] [⏹ Stop] [⟨ Step-] [Step+ ⟩]
///   ──────────────────────────────────────────────
///   Speed: [slider 10%–200%]  [label]
///   [Quad-View ON/OFF]
///   ──────────────────────────────────────────────
///   Clip List (QListWidget)  [Delete Clip]
class ReplayPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ReplayPanel(InstantReplay& replay, QWidget* parent = nullptr);
    ~ReplayPanel() override = default;

    /// Call from the host's frame-tick slot (every ~16 ms) to keep the
    /// ring-buffer and playheads advancing.
    void tick(double dt);

    /// Push a raw RGBA frame into the ring-buffer for the currently-selected
    /// camera channel.
    void ingestFrame(const FrameData& frame, int cameraChannel = 0);

public slots:
    void onPlay();
    void onPause();
    void onStop();
    void onStepBack();
    void onStepForward();
    void onMarkIn();
    void onMarkOut();
    void onCaptureLast10();
    void onCaptureLast30();
    void onToggleQuadView();
    void onSpeedChanged(int value);   ///< slider value 10–200 (percent)
    void onCameraChanged(int index);
    void onDeleteClip();
    void refreshClipList();

signals:
    void quadViewToggled(bool enabled);
    void speedChanged(double factor);   ///< 0.1 – 2.0
    void clipSaved(const QString& name);

private:
    void buildUi();
    void buildCaptureRow(QVBoxLayout* vl);
    void buildPlayheadRow(QVBoxLayout* vl);
    void buildSpeedRow(QVBoxLayout* vl);
    void buildClipList(QVBoxLayout* vl);
    void selectPlayhead(int idx);
    void flashSaved();

    InstantReplay& m_replay;

    // Camera selector
    QComboBox*   m_cameraCombo  = nullptr;
    int          m_activeCamera = 0;

    // Capture shortcuts
    QPushButton* m_btnLast10  = nullptr;
    QPushButton* m_btnLast30  = nullptr;
    QPushButton* m_btnMarkIn  = nullptr;
    QPushButton* m_btnMarkOut = nullptr;

    // Playhead tabs (4 playheads)
    static constexpr int kNumPlayheads = 4;
    int m_activePlayhead = 0;
    std::array<QPushButton*, kNumPlayheads> m_phTabs{};
    QPushButton* m_btnPlay    = nullptr;
    QPushButton* m_btnPause   = nullptr;
    QPushButton* m_btnStop    = nullptr;
    QPushButton* m_btnStepB   = nullptr;
    QPushButton* m_btnStepF   = nullptr;

    // Speed
    QSlider* m_speedSlider = nullptr;
    QLabel*  m_speedLabel  = nullptr;

    // Quad-view
    QPushButton* m_btnQuadView = nullptr;
    bool         m_quadViewOn  = false;

    // Clip list
    QListWidget* m_clipList   = nullptr;
    QPushButton* m_btnDelClip = nullptr;
    QLabel*      m_savedLabel = nullptr;
    QTimer*      m_savedTimer = nullptr;
};

} // namespace SM
