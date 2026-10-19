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
    explicit AudioStripWidget(uint64_t sourceId, const QString& name, bool isMaster = false,
                              const QString& busCode = "", QWidget* parent = nullptr);

    uint64_t sourceId() const { return m_sourceId; }
    void updateMeters(float peakL, float peakR);
    void setDbValue(float db);
    void setMuteState(bool mute);

signals:
    void gainChanged(uint64_t sourceId, float gain);
    void panChanged(uint64_t sourceId, float pan);
    void muteToggled(uint64_t sourceId, bool mute);
    void soloToggled(uint64_t sourceId, bool solo);
    void afvToggled(uint64_t sourceId, bool afv);
    void busToggled(uint64_t sourceId, int busIndex, bool enabled);
    void headphoneToggled(uint64_t sourceId, bool enabled);

private:
    void setupUI();

    uint64_t m_sourceId = 0;
    QString  m_name;
    bool     m_isMaster = false;
    QString  m_busCode;

    QLabel*       m_badgeLabel = nullptr;
    QLabel*       m_nameLabel = nullptr;
    QLabel*       m_dbLabel = nullptr;
    QSlider*      m_fader = nullptr;
    QPushButton*  m_btnBusM = nullptr;
    QPushButton*  m_btnBusA = nullptr;
    QPushButton*  m_btnBusB = nullptr;
    QPushButton*  m_btnSettings = nullptr;
    QPushButton*  m_btnMute = nullptr;
    QPushButton*  m_btnSolo = nullptr;
    QPushButton*  m_btnAfv = nullptr;
    QPushButton*  m_btnHeadphone = nullptr;
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
    void updateBusMeters(int busIndex, float peakL, float peakR);
    void setChannelMute(uint64_t sourceId, bool mute);

signals:
    void channelGainChanged(uint64_t sourceId, float gain);
    void channelPanChanged(uint64_t sourceId, float pan);
    void channelMuteToggled(uint64_t sourceId, bool mute);
    void channelSoloToggled(uint64_t sourceId, bool solo);
    void channelAfvToggled(uint64_t sourceId, bool afv);
    void masterGainChanged(float gain);
    void busGainChanged(int busIndex, float gain);

private:
    void setupUI();

    QWidget* m_channelsContainer = nullptr;
    std::unordered_map<uint64_t, AudioStripWidget*> m_strips;

    // Fixed right-side buses
    AudioStripWidget* m_busAStrip = nullptr;
    AudioStripWidget* m_busBStrip = nullptr;
    AudioStripWidget* m_masterStrip = nullptr;
};

} // namespace sm
