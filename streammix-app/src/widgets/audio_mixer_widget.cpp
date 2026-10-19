#include "audio_mixer_widget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <algorithm>
#include <cmath>

namespace sm {

static QString formatDb(float gain) {
    if (gain <= 0.0001f) return "-∞ dB";
    float db = 20.0f * std::log10(gain);
    if (std::abs(db) < 0.05f) return "0.0 dB";
    return QString("%1%2 dB").arg(db > 0 ? "+" : "").arg(db, 0, 'f', 1);
}

// ═════════════════════════════════════════════════════════════════
//  AudioStripWidget (Authentic vMix 29 Studio Channel Strip)
// ═════════════════════════════════════════════════════════════════

AudioStripWidget::AudioStripWidget(uint64_t sourceId, const QString& name, bool isMaster,
                                   const QString& busCode, QWidget* parent)
    : QWidget(parent), m_sourceId(sourceId), m_name(name), m_isMaster(isMaster), m_busCode(busCode)
{
    setupUI();
}

void AudioStripWidget::setupUI() {
    setFixedWidth(78);
    setStyleSheet(R"(
        AudioStripWidget {
            background-color: #1a1b22;
            border: 1px solid #2d303b;
            border-radius: 3px;
            color: #dcdcdc;
            font-family: 'Inter', -apple-system, sans-serif;
        }
        QPushButton {
            background-color: #272a34;
            border: 1px solid #3d4150;
            border-radius: 2px;
            font-size: 8px;
            font-weight: 700;
            padding: 0px;
            color: #a0a6b5;
        }
        QPushButton:hover {
            background-color: #353947;
            color: white;
        }
        QSlider::groove:vertical {
            background: #0f1013;
            width: 5px;
            border-radius: 2px;
        }
        QSlider::handle:vertical {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #757b8a, stop:0.5 #c8cdd9, stop:1.0 #757b8a);
            height: 14px;
            margin: 0 -5px;
            border-radius: 2px;
            border: 1px solid #333640;
        }
        QSlider::groove:horizontal {
            background: #0f1013;
            height: 3px;
            border-radius: 1px;
        }
        QSlider::handle:horizontal {
            background: #94a3b8;
            width: 6px;
            margin: -3px 0;
            border-radius: 1px;
        }
        QProgressBar {
            background-color: #0b0c0e;
            border: none;
            width: 3px;
            border-radius: 1px;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:1, x2:0, y2:0,
                stop:0.0 #10b981, stop:0.65 #10b981, stop:0.85 #f59e0b, stop:1.0 #ef4444);
        }
    )");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // ── Header: Badge + Channel Name (vMix Color Bar) ──
    auto* header = new QWidget(this);
    header->setFixedHeight(18);
    auto* hLayout = new QHBoxLayout(header);
    hLayout->setContentsMargins(3, 0, 3, 0);
    hLayout->setSpacing(2);

    if (m_isMaster) {
        header->setStyleSheet("background-color: #15803d; border-radius: 2px 2px 0 0;");
        m_nameLabel = new QLabel("Master", header);
        m_nameLabel->setAlignment(Qt::AlignCenter);
        m_nameLabel->setStyleSheet("font-weight: 800; font-size: 9px; color: #ffffff; border: none;");
        hLayout->addWidget(m_nameLabel, 1);
    } else if (!m_busCode.isEmpty()) {
        header->setStyleSheet("background-color: #0369a1; border-radius: 2px 2px 0 0;");
        m_nameLabel = new QLabel(QString("Bus %1").arg(m_busCode), header);
        m_nameLabel->setAlignment(Qt::AlignCenter);
        m_nameLabel->setStyleSheet("font-weight: 800; font-size: 9px; color: #ffffff; border: none;");
        hLayout->addWidget(m_nameLabel, 1);
    } else {
        header->setStyleSheet("background-color: #242732; border-radius: 2px 2px 0 0;");
        m_badgeLabel = new QLabel(QString::number(m_sourceId), header);
        m_badgeLabel->setStyleSheet("color: #facc15; font-weight: bold; font-size: 8px; border: none;");
        hLayout->addWidget(m_badgeLabel);

        m_nameLabel = new QLabel(m_name, header);
        m_nameLabel->setStyleSheet("font-weight: 700; font-size: 8px; color: #e2e8f0; border: none;");
        hLayout->addWidget(m_nameLabel, 1);
    }
    layout->addWidget(header);

    // ── Pan Slider (Horizontal Balance with Center 0) ──
    auto* panSlider = new QSlider(Qt::Horizontal, this);
    panSlider->setRange(-100, 100);
    panSlider->setValue(0);
    panSlider->setFixedHeight(10);
    panSlider->setToolTip("Pan / Balance (Center 0)");
    connect(panSlider, &QSlider::valueChanged, this, [this](int val) {
        emit panChanged(m_sourceId, static_cast<float>(val) / 100.0f);
    });
    layout->addWidget(panSlider);

    // ── Middle Section: Buttons Stack on Left, Fader + Meters on Right ──
    auto* midLayout = new QHBoxLayout();
    midLayout->setContentsMargins(1, 0, 1, 0);
    midLayout->setSpacing(2);

    // Left Column: Button stack (S, Route, Settings, Mute)
    auto* btnCol = new QVBoxLayout();
    btnCol->setSpacing(2);
    btnCol->setContentsMargins(0, 0, 0, 0);

    m_btnSolo = new QPushButton("S", this);
    m_btnSolo->setFixedSize(16, 15);
    m_btnSolo->setCheckable(true);
    m_btnSolo->setToolTip("Solo");
    m_btnSolo->setStyleSheet("QPushButton:checked { background-color: #ca8a04; color: white; border-color: #eab308; }");
    connect(m_btnSolo, &QPushButton::toggled, this, [this](bool s) {
        emit soloToggled(m_sourceId, s);
    });
    btnCol->addWidget(m_btnSolo);

    if (!m_isMaster && m_busCode.isEmpty()) {
        m_btnBusM = new QPushButton("M", this);
        m_btnBusM->setCheckable(true);
        m_btnBusM->setChecked(true);
        m_btnBusM->setFixedSize(16, 13);
        m_btnBusM->setStyleSheet("QPushButton { background-color: #1e293b; color: #64748b; font-size: 7px; border: none; } QPushButton:checked { background-color: #15803d; color: white; font-weight: bold; }");
        connect(m_btnBusM, &QPushButton::toggled, this, [this](bool c) { emit busToggled(m_sourceId, Bus_Master, c); });
        btnCol->addWidget(m_btnBusM);

        m_btnBusA = new QPushButton("A", this);
        m_btnBusA->setCheckable(true);
        m_btnBusA->setFixedSize(16, 13);
        m_btnBusA->setStyleSheet("QPushButton { background-color: #1e293b; color: #64748b; font-size: 7px; border: none; } QPushButton:checked { background-color: #0284c7; color: white; font-weight: bold; }");
        connect(m_btnBusA, &QPushButton::toggled, this, [this](bool c) { emit busToggled(m_sourceId, Bus_A, c); });
        btnCol->addWidget(m_btnBusA);

        m_btnAfv = new QPushButton("AFV", this);
        m_btnAfv->setCheckable(true);
        m_btnAfv->setFixedSize(16, 13);
        m_btnAfv->setToolTip("Audio Follows Video");
        m_btnAfv->setStyleSheet("QPushButton { background-color: #1e293b; color: #64748b; font-size: 6px; border: none; } QPushButton:checked { background-color: #059669; color: white; }");
        connect(m_btnAfv, &QPushButton::toggled, this, [this](bool c) { emit afvToggled(m_sourceId, c); });
        btnCol->addWidget(m_btnAfv);
    } else {
        auto* hp = new QPushButton("🎧", this);
        hp->setFixedSize(16, 15);
        hp->setCheckable(true);
        hp->setToolTip("Headphone Monitor");
        hp->setStyleSheet("QPushButton:checked { background-color: #0284c7; color: white; }");
        connect(hp, &QPushButton::toggled, this, [this](bool c) { emit headphoneToggled(m_sourceId, c); });
        btnCol->addWidget(hp);
    }

    m_btnSettings = new QPushButton("⚙", this);
    m_btnSettings->setFixedSize(16, 15);
    m_btnSettings->setToolTip("Audio Settings & VST3 Plugins");
    m_btnSettings->setStyleSheet("QPushButton { border: none; color: #64748b; font-size: 9px; } QPushButton:hover { color: #cbd5e1; }");
    btnCol->addWidget(m_btnSettings);

    btnCol->addStretch();

    // Authentic vMix Mute Button 'M': Bright Green when ON, Dark Red when MUTED
    m_btnMute = new QPushButton("M", this);
    m_btnMute->setCheckable(true);
    m_btnMute->setChecked(false); // unmuted = sound active
    m_btnMute->setFixedSize(18, 18);
    m_btnMute->setToolTip("Mute Toggle");
    m_btnMute->setStyleSheet(R"(
        QPushButton {
            background-color: #15803d;
            border: 1px solid #16a34a;
            border-radius: 2px;
            color: #ffffff;
            font-size: 9px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #16a34a;
        }
        QPushButton:checked {
            background-color: #7f1d1d;
            border-color: #991b1b;
            color: #f87171;
        }
    )");
    connect(m_btnMute, &QPushButton::toggled, this, [this](bool checked) {
        emit muteToggled(m_sourceId, checked);
    });
    btnCol->addWidget(m_btnMute);

    midLayout->addLayout(btnCol);

    // Right Side: Fader + Stereo Meters
    m_fader = new QSlider(Qt::Vertical, this);
    m_fader->setRange(0, 200); // 100 = Unity (1.0), 200 = +6dB
    m_fader->setValue(100);
    connect(m_fader, &QSlider::valueChanged, this, [this](int val) {
        float gain = static_cast<float>(val) / 100.0f;
        m_dbLabel->setText(formatDb(gain));
        emit gainChanged(m_sourceId, gain);
    });
    midLayout->addWidget(m_fader, 1);

    auto* meterCol = new QHBoxLayout();
    meterCol->setSpacing(1);
    meterCol->setContentsMargins(0, 0, 0, 0);

    m_meterL = new QProgressBar(this);
    m_meterL->setOrientation(Qt::Vertical);
    m_meterL->setRange(0, 100);
    m_meterL->setValue(0);
    m_meterL->setFixedWidth(3);
    m_meterL->setTextVisible(false);
    meterCol->addWidget(m_meterL);

    m_meterR = new QProgressBar(this);
    m_meterR->setOrientation(Qt::Vertical);
    m_meterR->setRange(0, 100);
    m_meterR->setValue(0);
    m_meterR->setFixedWidth(3);
    m_meterR->setTextVisible(false);
    meterCol->addWidget(m_meterR);

    midLayout->addLayout(meterCol);
    layout->addLayout(midLayout, 1);

    // ── Numeric dB Readout ──
    m_dbLabel = new QLabel("0.0 dB", this);
    m_dbLabel->setAlignment(Qt::AlignCenter);
    m_dbLabel->setStyleSheet("font-size: 8px; font-weight: bold; color: #94a3b8; border: none; font-family: monospace;");
    layout->addWidget(m_dbLabel);
}

void AudioStripWidget::updateMeters(float peakL, float peakR) {
    m_meterL->setValue(static_cast<int>(std::clamp(peakL * 100.0f, 0.0f, 100.0f)));
    m_meterR->setValue(static_cast<int>(std::clamp(peakR * 100.0f, 0.0f, 100.0f)));
}

void AudioStripWidget::setDbValue(float db) {
    m_dbLabel->setText(QString("%1%2 dB").arg(db > 0 ? "+" : "").arg(db, 0, 'f', 1));
}

void AudioStripWidget::setMuteState(bool mute) {
    if (m_btnMute) {
        m_btnMute->blockSignals(true);
        m_btnMute->setChecked(mute);
        m_btnMute->blockSignals(false);
    }
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
    setMinimumWidth(320);
    setStyleSheet("background-color: #131418; border-left: 1px solid #272933;");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 2, 4, 4);
    mainLayout->setSpacing(2);

    // ── Authentic vMix Top Bar: "OUTPUTS" / "INPUTS" + "🔊 Audio Mixer" ──
    auto* topBar = new QHBoxLayout();
    topBar->setContentsMargins(4, 2, 4, 2);

    auto* lblOutputs = new QLabel("OUTPUTS", this);
    lblOutputs->setStyleSheet("font-size: 8px; font-weight: 800; color: #38bdf8; letter-spacing: 1px; border: none;");
    topBar->addWidget(lblOutputs);

    auto* lblInputs = new QLabel("INPUTS", this);
    lblInputs->setStyleSheet("font-size: 8px; font-weight: 800; color: #94a3b8; letter-spacing: 1px; margin-left: 120px; border: none;");
    topBar->addWidget(lblInputs);

    topBar->addStretch();

    auto* mixerBadge = new QPushButton("🔊 Audio Mixer", this);
    mixerBadge->setFixedHeight(16);
    mixerBadge->setStyleSheet("QPushButton { background-color: #15803d; border: none; border-radius: 2px; color: white; font-size: 8px; font-weight: bold; padding: 1px 6px; } QPushButton:hover { background-color: #16a34a; }");
    topBar->addWidget(mixerBadge);

    mainLayout->addLayout(topBar);

    // ── Strip Area: Left Outputs (Master, Bus A, Bus B) | Right Inputs (Scrollable) ──
    auto* splitLayout = new QHBoxLayout();
    splitLayout->setContentsMargins(0, 0, 0, 0);
    splitLayout->setSpacing(4);

    // Left outputs container: Master, Bus A, Bus B
    auto* busContainer = new QWidget(this);
    auto* busLayout = new QHBoxLayout(busContainer);
    busLayout->setContentsMargins(0, 0, 0, 0);
    busLayout->setSpacing(3);

    // Master (Green header)
    m_masterStrip = new AudioStripWidget(Bus_Master, "Master", true, "M", busContainer);
    connect(m_masterStrip, &AudioStripWidget::gainChanged, this, [this](uint64_t, float g) {
        emit masterGainChanged(g);
    });
    busLayout->addWidget(m_masterStrip);

    // Bus A (Blue header)
    m_busAStrip = new AudioStripWidget(1001, "Bus A", false, "A", busContainer);
    connect(m_busAStrip, &AudioStripWidget::gainChanged, this, [this](uint64_t, float g) {
        emit busGainChanged(Bus_A, g);
    });
    busLayout->addWidget(m_busAStrip);

    // Bus B (Purple header)
    m_busBStrip = new AudioStripWidget(1002, "Bus B", false, "B", busContainer);
    connect(m_busBStrip, &AudioStripWidget::gainChanged, this, [this](uint64_t, float g) {
        emit busGainChanged(Bus_B, g);
    });
    busLayout->addWidget(m_busBStrip);

    splitLayout->addWidget(busContainer);

    // Scrollable container for input channel strips
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("border: none; background: transparent;");

    m_channelsContainer = new QWidget(scrollArea);
    auto* channelsLayout = new QHBoxLayout(m_channelsContainer);
    channelsLayout->setContentsMargins(0, 0, 0, 0);
    channelsLayout->setSpacing(3);
    channelsLayout->addStretch();
    scrollArea->setWidget(m_channelsContainer);
    splitLayout->addWidget(scrollArea, 1);

    mainLayout->addLayout(splitLayout, 1);
}

void AudioMixerWidget::addChannel(uint64_t sourceId, const QString& name) {
    if (m_strips.find(sourceId) != m_strips.end()) return;

    auto* strip = new AudioStripWidget(sourceId, name, false, "", m_channelsContainer);
    connect(strip, &AudioStripWidget::gainChanged, this, &AudioMixerWidget::channelGainChanged);
    connect(strip, &AudioStripWidget::panChanged, this, &AudioMixerWidget::channelPanChanged);
    connect(strip, &AudioStripWidget::muteToggled, this, &AudioMixerWidget::channelMuteToggled);
    connect(strip, &AudioStripWidget::soloToggled, this, &AudioMixerWidget::channelSoloToggled);
    connect(strip, &AudioStripWidget::afvToggled, this, &AudioMixerWidget::channelAfvToggled);

    auto* layout = qobject_cast<QHBoxLayout*>(m_channelsContainer->layout());
    if (layout) {
        layout->insertWidget(layout->count() - 1, strip);
    }
    m_strips[sourceId] = strip;
}

void AudioMixerWidget::setChannelMute(uint64_t sourceId, bool mute) {
    auto it = m_strips.find(sourceId);
    if (it != m_strips.end()) {
        it->second->setMuteState(mute);
    }
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
    if (m_masterStrip) {
        m_masterStrip->updateMeters(peakL, peakR);
    }
}

void AudioMixerWidget::updateBusMeters(int busIndex, float peakL, float peakR) {
    if (busIndex == Bus_A && m_busAStrip) {
        m_busAStrip->updateMeters(peakL, peakR);
    } else if (busIndex == Bus_B && m_busBStrip) {
        m_busBStrip->updateMeters(peakL, peakR);
    }
}

} // namespace sm
