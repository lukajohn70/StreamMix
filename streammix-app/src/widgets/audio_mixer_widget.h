#pragma once

#include <QWidget>
#include <QSlider>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <vector>
#include <unordered_map>
#include "streammix/audio_mixer.h"

namespace sm {

class AudioStripWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioStripWidget(uint64_t sourceId, const QString& name, QWidget* parent = nullptr);

    uint64_t sourceId() const { return m_sourceId; }
    void updateMeters(float peakL, float peakR);

signals:
    void gainChanged(uint64_t sourceId, float gain);
    void muteToggled(uint64_t sourceId, bool mute);
    void soloToggled(uint64_t sourceId, bool solo);
    void afvToggled(uint64_t sourceId, bool afv);

private:
    uint64_t m_sourceId;
    QLabel* m_nameLabel = nullptr;
    QSlider* m_fader = nullptr;
    QPushButton* m_btnMute = nullptr;
    QPushButton* m_btnSolo = nullptr;
    QPushButton* m_btnAfv = nullptr;
    QProgressBar* m_meterL = nullptr;
    QProgressBar* m_meterR = nullptr;
};

class AudioMixerWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioMixerWidget(QWidget* parent = nullptr);
    ~AudioMixerWidget() override = default;

    void addChannel(uint64_t sourceId, const QString& name);
    void removeChannel(uint64_t sourceId);
    void updateChannelMeters(uint64_t sourceId, float peakL, float peakR);
    void updateMasterMeters(float peakL, float peakR);

signals:
    void channelGainChanged(uint64_t sourceId, float gain);
    void channelMuteToggled(uint64_t sourceId, bool mute);
    void channelSoloToggled(uint64_t sourceId, bool solo);
    void channelAfvToggled(uint64_t sourceId, bool afv);
    void masterGainChanged(float gain);

private:
    void setupUI();

    QWidget* m_channelsContainer = nullptr;
    std::unordered_map<uint64_t, AudioStripWidget*> m_strips;
    QSlider* m_masterFader = nullptr;
    QProgressBar* m_masterMeterL = nullptr;
    QProgressBar* m_masterMeterR = nullptr;
};

} // namespace sm
