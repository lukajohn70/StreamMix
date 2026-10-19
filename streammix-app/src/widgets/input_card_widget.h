#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QSlider>
#include <QImage>
#include "streammix/frame.h"

namespace sm {

class InputCardWidget : public QWidget {
    Q_OBJECT

public:
    explicit InputCardWidget(uint64_t sourceId, int index, const QString& name,
                             const QString& typeName, QWidget* parent = nullptr);
    ~InputCardWidget() override = default;

    uint64_t sourceId() const { return m_sourceId; }
    const QString& name() const { return m_name; }
    const QString& typeName() const { return m_typeName; }
    int index() const { return m_index; }
    void setName(const QString& name) {
        m_name = name;
        if (m_titleLabel) m_titleLabel->setText(m_name);
    }
    void updateThumbnail(VideoFramePtr frame);
    void updateAudioMeter(float peakL, float peakR);
    void updatePlaybackState(double positionSec, double durationSec, bool isPaused, bool isLooping);
    void setActivePreview(bool isPreview);
    void setActiveProgram(bool isProgram);
    void setAudioActive(bool active);
    void setOverlayActive(int channel, bool active);
    bool isPreview() const { return m_isPreview; }
    bool isProgram() const { return m_isProgram; }
    bool isAudioActive() const { return m_audioActive; }

signals:
    void previewClicked(uint64_t sourceId);
    void cutClicked(uint64_t sourceId);
    void goClicked(uint64_t sourceId);
    void settingsClicked(uint64_t sourceId);
    void closeClicked(uint64_t sourceId);
    void audioToggled(uint64_t sourceId, bool active);
    void overlayTriggered(uint64_t sourceId, int channel);
    void seekRequested(uint64_t sourceId, double seconds);
    void playPauseToggled(uint64_t sourceId, bool play);
    void loopToggled(uint64_t sourceId, bool loop);

private:
    void setupUI();
    void updateTallyStyle();

    uint64_t m_sourceId;
    int m_index;
    QString m_name;
    QString m_typeName;

    QWidget* m_headerWidget = nullptr;
    QLabel* m_thumbLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QProgressBar* m_meterL = nullptr;
    QProgressBar* m_meterR = nullptr;
    QPushButton* m_btnCut = nullptr;
    QPushButton* m_btnGo = nullptr;
    QPushButton* m_btnAudio = nullptr;
    QPushButton* m_btnSettings = nullptr;
    QPushButton* m_btnClose = nullptr;
    std::vector<QPushButton*> m_miniOverlays;

    // Video Transport Controls
    QWidget*     m_transportWidget = nullptr;
    QPushButton* m_btnPlay = nullptr;
    QPushButton* m_btnLoop = nullptr;
    QSlider*     m_sliderPosition = nullptr;
    QLabel*      m_lblTimecode = nullptr;
    double       m_durationSec = 0.0;
    bool         m_userSeeking = false;

    bool m_isPreview = false;
    bool m_isProgram = false;
    bool m_audioActive = true;
};

} // namespace sm
