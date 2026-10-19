#pragma once

#include <QMainWindow>
#include <QTimer>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QTabWidget>
#include <QButtonGroup>
#include <unordered_map>

#include "streammix.h"
#include "audio_player.h"
#include "widgets/monitor_widget.h"
#include "widgets/transition_widget.h"
#include "widgets/input_card_widget.h"
#include "widgets/audio_mixer_widget.h"
#include "widgets/ptz_panel.h"
#include "widgets/replay_panel.h"

namespace sm {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onFrameTick();
    void onCutRequested();
    void onTransitionRequested(TransitionType type, int durationMs);
    void onTBarMoved(float position);
    void onOverlayToggled(int channel);
    void onFtbToggled();

    void onInputPreviewClicked(uint64_t sourceId);
    void onInputCutClicked(uint64_t sourceId);
    void onInputGoClicked(uint64_t sourceId);
    void onInputCloseClicked(uint64_t sourceId);
    void onInputSettingsClicked(uint64_t sourceId);
    void onCategoryFilterChanged(int id);
    void onOpenSettings();

    // Preset persistence
    void onNewPreset();
    void onOpenPreset();
    void onSavePreset();
    void onSavePresetAs();

    // Input ingestion dialogs
    void onAddVideoFileInput();
    void onAddCameraInput();
    void onAddOmtInput();
    void onAddTitleInput();
    void onAddZoomInput();

    void onStreamToggled(bool checked);
    void onRecordToggled(bool checked);
    void onExternalToggled(bool checked);
    void onMultiCorderToggled(bool checked);
    void onPlaylistToggled(bool checked);
    void onInputOverlayToggled(uint64_t sourceId, int channel);

private:
    QString       m_currentPresetPath;
    AudioPlayer   m_audioPlayer;
    QButtonGroup* m_categoryGroup = nullptr;
    void setupUI();
    void setupBottomControlBar(QVBoxLayout* rootLayout);
    void setupToolBar();
    void setupMenuBar();
    void setupStatusBar();
    void populateSampleInputs();
    void addInputCard(uint64_t sourceId, int index, const QString& name, const QString& type);
    void updateStreamRecordStatus();

    // Core engine
    Context       m_context;
    PTZController m_ptzController;
    InstantReplay m_instantReplay;

    // Web Controller (port 8088)
    WebServer*    m_webServer = nullptr;

    // MIDI / control surface
    MidiController* m_midiController = nullptr;

    // Render loop
    QTimer* m_renderTimer = nullptr;

    // Top monitors + switcher
    MonitorWidget*    m_previewMonitor  = nullptr;
    MonitorWidget*    m_programMonitor  = nullptr;
    TransitionWidget* m_transitionWidget = nullptr;

    // Bottom panels
    AudioMixerWidget* m_audioMixerWidget = nullptr;
    QTabWidget*       m_rightTabs        = nullptr;
    PTZPanel*         m_ptzPanel         = nullptr;
    ReplayPanel*      m_replayPanel      = nullptr;
    uint64_t          m_lastProgramSourceId = 0;

    // Control Bar Buttons
    QPushButton* m_btnStream = nullptr;
    QPushButton* m_btnRecord = nullptr;
    QPushButton* m_btnExternal = nullptr;
    QPushButton* m_btnMultiCorder = nullptr;
    QPushButton* m_btnPlaylist = nullptr;

    // Automated PlayList sequencer
    QTimer* m_playlistTimer = nullptr;
    bool    m_playlistRunning = false;
    size_t  m_playlistIndex = 0;

    // Input deck
    QWidget* m_inputDeckContainer = nullptr;
    std::unordered_map<uint64_t, InputCardWidget*> m_inputCards;

    // Status bar labels
    QLabel* m_statusFps      = nullptr;
    QLabel* m_statusCpu      = nullptr;
    QLabel* m_statusGpu      = nullptr;
    QLabel* m_statusRender   = nullptr;
    QLabel* m_statusStream   = nullptr;
    QLabel* m_statusRecord   = nullptr;

    uint64_t m_frameCount   = 0;
    int      m_streamSeconds = 0;
    int      m_recordSeconds = 0;
};

} // namespace sm
