#include "audio_mixer_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <algorithm>

namespace sm {

// ═════════════════════════════════════════════════════════════════
//  AudioStripWidget
// ═════════════════════════════════════════════════════════════════

AudioStripWidget::AudioStripWidget(uint64_t sourceId, const QString& name, QWidget* parent)
    : QWidget(parent), m_sourceId(sourceId)
{
    setFixedWidth(64);
    setStyleSheet(R"(
        QWidget {
            background-color: #1a1b20;
            border: 1px solid #2e303a;
            border-radius: 3px;
            color: #dcdcdc;
            font-family: 'Inter', sans-serif;
            font-size: 10px;
        }
        QPushButton {
            background-color: #2b2d38;
            border: 1px solid #3d4050;
            border-radius: 2px;
            font-weight: 700;
            height: 16px;
        }
        QSlider::groove:vertical {
            background: #101114;
            width: 8px;
            border-radius: 4px;
        }
        QSlider::handle:vertical {
            background: #bdc3c7;
            height: 18px;
            margin: 0 -5px;
            border-radius: 2px;
        }
        QProgressBar {
            background-color: #0c0d10;
            border: none;
            width: 4px;
        }
        QProgressBar::chunk {
            background-color: #2ecc71;
        }
    )");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 4, 3, 4);
    layout->setSpacing(3);

    m_nameLabel = new QLabel(name, this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setStyleSheet("font-weight: bold; border: none; font-size: 9px;");
    layout->addWidget(m_nameLabel);

    // Meters & Fader row
    auto* faderRow = new QHBoxLayout();
    faderRow->setSpacing(2);

    m_meterL = new QProgressBar(this);
    m_meterL->setOrientation(Qt::Vertical);
    m_meterL->setRange(0, 100);
    faderRow->addWidget(m_meterL);

    m_meterR = new QProgressBar(this);
    m_meterR->setOrientation(Qt::Vertical);
    m_meterR->setRange(0, 100);
    faderRow->addWidget(m_meterR);

    m_fader = new QSlider(Qt::Vertical, this);
    m_fader->setRange(0, 200);
    m_fader->setValue(100); // Unity (1.0)
    connect(m_fader, &QSlider::valueChanged, this, [this](int val) {
        emit gainChanged(m_sourceId, static_cast<float>(val) / 100.0f);
    });
    faderRow->addWidget(m_fader);

    layout->addLayout(faderRow, 1);

    // MUTE, SOLO, AFV buttons
    m_btnMute = new QPushButton("M", this);
    m_btnMute->setCheckable(true);
    connect(m_btnMute, &QPushButton::toggled, this, [this](bool checked) {
        m_btnMute->setStyleSheet(checked ? "background-color: #e74c3c; color: white;" : "");
        emit muteToggled(m_sourceId, checked);
    });
    layout->addWidget(m_btnMute);

    m_btnSolo = new QPushButton("S", this);
    m_btnSolo->setCheckable(true);
    connect(m_btnSolo, &QPushButton::toggled, this, [this](bool checked) {
        m_btnSolo->setStyleSheet(checked ? "background-color: #f39c12; color: white;" : "");
        emit soloToggled(m_sourceId, checked);
    });
    layout->addWidget(m_btnSolo);

    m_btnAfv = new QPushButton("AFV", this);
    m_btnAfv->setCheckable(true);
    connect(m_btnAfv, &QPushButton::toggled, this, [this](bool checked) {
        m_btnAfv->setStyleSheet(checked ? "background-color: #27ae60; color: white;" : "");
        emit afvToggled(m_sourceId, checked);
    });
    layout->addWidget(m_btnAfv);
}

void AudioStripWidget::updateMeters(float peakL, float peakR) {
    m_meterL->setValue(static_cast<int>(std::clamp(peakL * 100.0f, 0.0f, 100.0f)));
    m_meterR->setValue(static_cast<int>(std::clamp(peakR * 100.0f, 0.0f, 100.0f)));
}

// ═════════════════════════════════════════════════════════════════
//  AudioMixerWidget
// ═════════════════════════════════════════════════════════════════

AudioMixerWidget::AudioMixerWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void AudioMixerWidget::setupUI() {
    setMinimumWidth(280);
    setStyleSheet("background-color: #141519; border-left: 1px solid #282a34;");

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // Scrollable container for input channel strips
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("border: none; background: transparent;");

    m_channelsContainer = new QWidget(scrollArea);
    auto* channelsLayout = new QHBoxLayout(m_channelsContainer);
    channelsLayout->setContentsMargins(0, 0, 0, 0);
    channelsLayout->setSpacing(4);
    channelsLayout->addStretch();
    scrollArea->setWidget(m_channelsContainer);
    mainLayout->addWidget(scrollArea, 1);

    // Master Strip (always pinned on right)
    auto* masterStrip = new QWidget(this);
    masterStrip->setFixedWidth(64);
    masterStrip->setStyleSheet("background-color: #1f2027; border: 1px solid #3d4150; border-radius: 3px;");

    auto* masterLayout = new QVBoxLayout(masterStrip);
    masterLayout->setContentsMargins(3, 4, 3, 4);
    masterLayout->setSpacing(3);

    auto* masterTitle = new QLabel("MASTER", masterStrip);
    masterTitle->setAlignment(Qt::AlignCenter);
    masterTitle->setStyleSheet("font-weight: bold; font-size: 10px; color: #e74c3c; border: none;");
    masterLayout->addWidget(masterTitle);

    auto* mFaderRow = new QHBoxLayout();
    mFaderRow->setSpacing(2);

    m_masterMeterL = new QProgressBar(masterStrip);
    m_masterMeterL->setOrientation(Qt::Vertical);
    m_masterMeterL->setRange(0, 100);
    m_masterMeterL->setFixedWidth(4);
    m_masterMeterL->setStyleSheet("border: none; background-color: #0c0d10;");
    mFaderRow->addWidget(m_masterMeterL);

    m_masterMeterR = new QProgressBar(masterStrip);
    m_masterMeterR->setOrientation(Qt::Vertical);
    m_masterMeterR->setRange(0, 100);
    m_masterMeterR->setFixedWidth(4);
    m_masterMeterR->setStyleSheet("border: none; background-color: #0c0d10;");
    mFaderRow->addWidget(m_masterMeterR);

    m_masterFader = new QSlider(Qt::Vertical, masterStrip);
    m_masterFader->setRange(0, 200);
    m_masterFader->setValue(100);
    connect(m_masterFader, &QSlider::valueChanged, this, [this](int val) {
        emit masterGainChanged(static_cast<float>(val) / 100.0f);
    });
    mFaderRow->addWidget(m_masterFader);

    masterLayout->addLayout(mFaderRow, 1);

    auto* masterBusLabel = new QLabel("BUS M", masterStrip);
    masterBusLabel->setAlignment(Qt::AlignCenter);
    masterBusLabel->setStyleSheet("font-size: 9px; color: #7f8c8d; border: none;");
    masterLayout->addWidget(masterBusLabel);

    mainLayout->addWidget(masterStrip);
}

void AudioMixerWidget::addChannel(uint64_t sourceId, const QString& name) {
    if (m_strips.find(sourceId) != m_strips.end()) return;

    auto* strip = new AudioStripWidget(sourceId, name, m_channelsContainer);
    connect(strip, &AudioStripWidget::gainChanged, this, &AudioMixerWidget::channelGainChanged);
    connect(strip, &AudioStripWidget::muteToggled, this, &AudioMixerWidget::channelMuteToggled);
    connect(strip, &AudioStripWidget::soloToggled, this, &AudioMixerWidget::channelSoloToggled);
    connect(strip, &AudioStripWidget::afvToggled, this, &AudioMixerWidget::channelAfvToggled);

    auto* layout = qobject_cast<QHBoxLayout*>(m_channelsContainer->layout());
    if (layout) {
        layout->insertWidget(layout->count() - 1, strip);
    }
    m_strips[sourceId] = strip;
}

void AudioMixerWidget::removeChannel(uint64_t sourceId) {
    auto it = m_strips.find(sourceId);
    if (it != m_strips.end()) {
        it->second->deleteLater();
        m_strips.erase(it);
    }
}

void AudioMixerWidget::updateChannelMeters(uint64_t sourceId, float peakL, float peakR) {
    auto it = m_strips.find(sourceId);
    if (it != m_strips.end()) {
        it->second->updateMeters(peakL, peakR);
    }
}

void AudioMixerWidget::updateMasterMeters(float peakL, float peakR) {
    m_masterMeterL->setValue(static_cast<int>(std::clamp(peakL * 100.0f, 0.0f, 100.0f)));
    m_masterMeterR->setValue(static_cast<int>(std::clamp(peakR * 100.0f, 0.0f, 100.0f)));
}

} // namespace sm
