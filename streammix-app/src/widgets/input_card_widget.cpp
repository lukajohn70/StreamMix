#include "input_card_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QStyle>
#include <QMouseEvent>
#include <algorithm>

namespace sm {

InputCardWidget::InputCardWidget(uint64_t sourceId, int index, const QString& name,
                                 const QString& typeName, QWidget* parent)
    : QWidget(parent), m_sourceId(sourceId), m_index(index), m_name(name), m_typeName(typeName)
{
    setupUI();
}

void InputCardWidget::setupUI() {
    // Official vMix specification: 208x172 (with video transport scrubber)
    setFixedSize(208, 172);
    setStyleSheet(R"(
        InputCardWidget {
            background-color: #1a1c22;
            border: 1px solid #2e323e;
            border-radius: 3px;
            color: #dcdcdc;
            font-family: 'Segoe UI', 'Inter', -apple-system, sans-serif;
            font-size: 11px;
        }
        QProgressBar {
            background-color: #0b0c0e;
            border: none;
            border-radius: 1px;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:1, x2:0, y2:0,
                stop:0 #10b981, stop:0.65 #10b981, stop:0.85 #f59e0b, stop:1.0 #ef4444);
        }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 2);
    mainLayout->setSpacing(2);

    // ── 1. HEADER BANNER (vMix colored bar with index, name, and X) ──
    m_headerWidget = new QWidget(this);
    m_headerWidget->setFixedHeight(22);
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    auto* badge = new QLabel(QString::number(m_index), m_headerWidget);
    badge->setStyleSheet("background-color: rgba(0, 0, 0, 0.45); color: #facc15; font-weight: bold; border-radius: 2px; padding: 1px 4px; font-size: 10px;");
    headerLayout->addWidget(badge);

    m_titleLabel = new QLabel(m_name, m_headerWidget);
    m_titleLabel->setStyleSheet("font-weight: 700; font-size: 11px; color: #ffffff; border: none; background: transparent;");
    headerLayout->addWidget(m_titleLabel, 1);

    m_btnClose = new QPushButton("✕", m_headerWidget);
    m_btnClose->setFixedSize(16, 16);
    m_btnClose->setToolTip("Close Input");
    m_btnClose->setStyleSheet("QPushButton { background-color: transparent; border: none; color: #ffffff; font-size: 11px; font-weight: bold; } QPushButton:hover { color: #f87171; }");
    connect(m_btnClose, &QPushButton::clicked, this, [this]() {
        emit closeClicked(m_sourceId);
    });
    headerLayout->addWidget(m_btnClose);

    mainLayout->addWidget(m_headerWidget);

    // ── 2. THUMBNAIL SCREEN + AUDIO METERS (Middle Area) ─────────
    auto* body = new QHBoxLayout();
    body->setContentsMargins(4, 0, 4, 0);
    body->setSpacing(3);

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setFixedSize(188, 92);
    m_thumbLabel->setStyleSheet("background-color: #0d0e11; border: 1px solid #1e2028;");
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setText(m_typeName);
    m_thumbLabel->setCursor(Qt::PointingHandCursor);
    body->addWidget(m_thumbLabel);

    // Click on thumbnail triggers PREVIEW
    m_thumbLabel->installEventFilter(this);

    // Stereo Audio Peak Meters on right edge
    auto* meterLayout = new QVBoxLayout();
    meterLayout->setSpacing(1);

    m_meterL = new QProgressBar(this);
    m_meterL->setOrientation(Qt::Vertical);
    m_meterL->setRange(0, 100);
    m_meterL->setValue(0);
    m_meterL->setFixedWidth(3);
    m_meterL->setTextVisible(false);
    meterLayout->addWidget(m_meterL);

    m_meterR = new QProgressBar(this);
    m_meterR->setOrientation(Qt::Vertical);
    m_meterR->setRange(0, 100);
    m_meterR->setValue(0);
    m_meterR->setFixedWidth(3);
    m_meterR->setTextVisible(false);
    meterLayout->addWidget(m_meterR);

    body->addLayout(meterLayout);
    mainLayout->addLayout(body);

    // ── Video Transport / Scrubber Bar (vMix Video Controls) ──────
    m_transportWidget = new QWidget(this);
    m_transportWidget->setFixedHeight(14);
    auto* tLayout = new QHBoxLayout(m_transportWidget);
    tLayout->setContentsMargins(4, 0, 4, 0);
    tLayout->setSpacing(2);

    m_btnPlay = new QPushButton("⏸", m_transportWidget);
    m_btnPlay->setFixedSize(13, 13);
    m_btnPlay->setToolTip("Play / Pause");
    m_btnPlay->setStyleSheet("QPushButton { background: transparent; border: none; color: #38bdf8; font-size: 7px; font-weight: bold; } QPushButton:hover { color: #fff; }");
    connect(m_btnPlay, &QPushButton::clicked, this, [this]() {
        bool willPlay = (m_btnPlay->text() == "▶");
        m_btnPlay->setText(willPlay ? "⏸" : "▶");
        emit playPauseToggled(m_sourceId, willPlay);
    });
    tLayout->addWidget(m_btnPlay);

    m_btnLoop = new QPushButton("🔁", m_transportWidget);
    m_btnLoop->setFixedSize(13, 13);
    m_btnLoop->setCheckable(true);
    m_btnLoop->setChecked(true);
    m_btnLoop->setToolTip("Loop Playback");
    m_btnLoop->setStyleSheet("QPushButton { background: transparent; border: none; color: #94a3b8; font-size: 7px; } QPushButton:checked { color: #f59e0b; }");
    connect(m_btnLoop, &QPushButton::toggled, this, [this](bool l) {
        emit loopToggled(m_sourceId, l);
    });
    tLayout->addWidget(m_btnLoop);

    m_sliderPosition = new QSlider(Qt::Horizontal, m_transportWidget);
    m_sliderPosition->setRange(0, 1000);
    m_sliderPosition->setValue(0);
    m_sliderPosition->setFixedHeight(8);
    m_sliderPosition->setStyleSheet(R"(
        QSlider::groove:horizontal { background: #0f1013; height: 3px; border-radius: 1px; }
        QSlider::sub-page:horizontal { background: #3b82f6; border-radius: 1px; }
        QSlider::handle:horizontal { background: #e2e8f0; width: 6px; margin: -2px 0; border-radius: 1px; }
    )");
    connect(m_sliderPosition, &QSlider::sliderPressed, this, [this]() { m_userSeeking = true; });
    connect(m_sliderPosition, &QSlider::sliderReleased, this, [this]() {
        m_userSeeking = false;
        if (m_durationSec > 0.0) {
            double target = (m_sliderPosition->value() / 1000.0) * m_durationSec;
            emit seekRequested(m_sourceId, target);
        }
    });
    tLayout->addWidget(m_sliderPosition, 1);

    m_lblTimecode = new QLabel("00:00 / 00:00", m_transportWidget);
    m_lblTimecode->setStyleSheet("color: #94a3b8; font-size: 8px; font-family: monospace;");
    tLayout->addWidget(m_lblTimecode);

    mainLayout->addWidget(m_transportWidget);

    // ── 3. AUTHENTIC vMix 29 FOOTER BAR (2x4 Overlays + GO + Cut + Audio + Gear) ──
    auto* footer = new QHBoxLayout();
    footer->setContentsMargins(4, 0, 4, 1);
    footer->setSpacing(2);

    // 2x4 Mini Overlay Grid (1-4 on row 1, 5-8 on row 2)
    auto* ovGrid = new QGridLayout();
    ovGrid->setContentsMargins(0, 0, 0, 0);
    ovGrid->setSpacing(1);

    m_miniOverlays.resize(8);
    for (int i = 0; i < 8; ++i) {
        int ch = i + 1;
        auto* btn = new QPushButton(QString::number(ch), this);
        btn->setFixedSize(12, 10);
        btn->setCheckable(true);
        btn->setStyleSheet(R"(
            QPushButton {
                background-color: #242732;
                border: none;
                color: #94a3b8;
                font-size: 7px;
                font-weight: bold;
                padding: 0px;
                margin: 0px;
            }
            QPushButton:hover {
                background-color: #3b4252;
                color: #fff;
            }
            QPushButton:checked {
                background-color: #ea580c;
                color: white;
            }
        )");
        connect(btn, &QPushButton::clicked, this, [this, ch]() {
            emit overlayTriggered(m_sourceId, ch);
        });
        m_miniOverlays[i] = btn;
        ovGrid->addWidget(btn, i / 4, i % 4);
    }
    footer->addLayout(ovGrid);

    // GO Button (vMix Blue)
    m_btnGo = new QPushButton("GO", this);
    m_btnGo->setFixedSize(28, 22);
    m_btnGo->setStyleSheet(R"(
        QPushButton {
            background-color: #1d4ed8;
            border: 1px solid #2563eb;
            border-radius: 2px;
            color: white;
            font-size: 10px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #1e40af;
        }
    )");
    connect(m_btnGo, &QPushButton::clicked, this, [this]() {
        emit goClicked(m_sourceId);
    });
    footer->addWidget(m_btnGo);

    // Cut Button (vMix Red)
    m_btnCut = new QPushButton("Cut", this);
    m_btnCut->setFixedSize(28, 22);
    m_btnCut->setStyleSheet(R"(
        QPushButton {
            background-color: #b91c1c;
            border: 1px solid #dc2626;
            border-radius: 2px;
            color: white;
            font-size: 10px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #991b1b;
        }
    )");
    connect(m_btnCut, &QPushButton::clicked, this, [this]() {
        emit cutClicked(m_sourceId);
    });
    footer->addWidget(m_btnCut);

    // Audio Button (vMix Green when on, Gray when muted)
    m_btnAudio = new QPushButton("Audio", this);
    m_btnAudio->setFixedSize(36, 22);
    m_btnAudio->setCheckable(true);
    m_btnAudio->setChecked(m_audioActive);
    m_btnAudio->setStyleSheet(R"(
        QPushButton {
            background-color: #374151;
            border: 1px solid #4b5563;
            border-radius: 2px;
            color: #9ca3af;
            font-size: 9px;
            font-weight: bold;
        }
        QPushButton:checked {
            background-color: #15803d;
            border-color: #16a34a;
            color: white;
        }
        QPushButton:hover {
            opacity: 0.9;
        }
    )");
    connect(m_btnAudio, &QPushButton::toggled, this, [this](bool checked) {
        m_audioActive = checked;
        emit audioToggled(m_sourceId, checked);
    });
    footer->addWidget(m_btnAudio);

    // Settings Gear Button
    m_btnSettings = new QPushButton("⚙", this);
    m_btnSettings->setFixedSize(18, 22);
    m_btnSettings->setToolTip("Input Settings");
    m_btnSettings->setStyleSheet("QPushButton { background-color: #242732; border: 1px solid #374151; border-radius: 2px; color: #cbd5e1; font-size: 10px; } QPushButton:hover { background-color: #333846; }");
    connect(m_btnSettings, &QPushButton::clicked, this, [this]() {
        emit settingsClicked(m_sourceId);
    });
    footer->addWidget(m_btnSettings);

    mainLayout->addLayout(footer);

    updateTallyStyle();
}

void InputCardWidget::updateThumbnail(VideoFramePtr frame) {
    if (!frame || frame->empty()) return;

    QImage img(frame->data.data(), frame->width, frame->height, frame->linesize, QImage::Format_RGBA8888);
    QPixmap pixmap = QPixmap::fromImage(img.scaled(m_thumbLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
    m_thumbLabel->setPixmap(pixmap);
}

void InputCardWidget::updateAudioMeter(float peakL, float peakR) {
    m_meterL->setValue(static_cast<int>(std::clamp(peakL * 100.0f, 0.0f, 100.0f)));
    m_meterR->setValue(static_cast<int>(std::clamp(peakR * 100.0f, 0.0f, 100.0f)));
}

void InputCardWidget::updatePlaybackState(double positionSec, double durationSec, bool isPaused, bool isLooping) {
    m_durationSec = durationSec;
    if (m_btnPlay) {
        m_btnPlay->setText(isPaused ? "▶" : "⏸");
    }
    if (m_btnLoop) {
        m_btnLoop->blockSignals(true);
        m_btnLoop->setChecked(isLooping);
        m_btnLoop->blockSignals(false);
    }
    if (m_sliderPosition && !m_userSeeking && durationSec > 0.0) {
        int val = static_cast<int>((positionSec / durationSec) * 1000.0);
        m_sliderPosition->setValue(val);
    }
    if (m_lblTimecode) {
        int posS = static_cast<int>(positionSec);
        int durS = static_cast<int>(durationSec);
        m_lblTimecode->setText(QString("%1:%2 / %3:%4")
            .arg(posS / 60, 2, 10, QChar('0'))
            .arg(posS % 60, 2, 10, QChar('0'))
            .arg(durS / 60, 2, 10, QChar('0'))
            .arg(durS % 60, 2, 10, QChar('0')));
    }
}

void InputCardWidget::setActivePreview(bool isPreview) {
    if (m_isPreview == isPreview) return;
    m_isPreview = isPreview;
    updateTallyStyle();
}

void InputCardWidget::setActiveProgram(bool isProgram) {
    if (m_isProgram == isProgram) return;
    m_isProgram = isProgram;
    updateTallyStyle();
}

void InputCardWidget::setAudioActive(bool active) {
    m_audioActive = active;
    if (m_btnAudio) {
        m_btnAudio->blockSignals(true);
        m_btnAudio->setChecked(active);
        m_btnAudio->blockSignals(false);
    }
}

void InputCardWidget::setOverlayActive(int channel, bool active) {
    if (channel >= 1 && channel <= 8 && channel <= static_cast<int>(m_miniOverlays.size())) {
        m_miniOverlays[channel - 1]->blockSignals(true);
        m_miniOverlays[channel - 1]->setChecked(active);
        m_miniOverlays[channel - 1]->blockSignals(false);
    }
}

void InputCardWidget::updateTallyStyle() {
    // Authentic vMix 29 Specifications:
    // Active / Program: Crimson Red (#b91c1c banner, #ef4444 outline)
    // Preview: Emerald Green (#1e7a3b banner, #22c55e outline)
    // Idle: Slate (#242833 banner, #2e323e outline)
    if (m_isProgram) {
        setStyleSheet(R"(
            InputCardWidget {
                background-color: #211414;
                border: 2px solid #ef4444;
                border-radius: 3px;
            }
        )");
        if (m_headerWidget) {
            m_headerWidget->setStyleSheet("background-color: #b91c1c; border-top-left-radius: 2px; border-top-right-radius: 2px;");
        }
    } else if (m_isPreview) {
        setStyleSheet(R"(
            InputCardWidget {
                background-color: #141f16;
                border: 2px solid #22c55e;
                border-radius: 3px;
            }
        )");
        if (m_headerWidget) {
            m_headerWidget->setStyleSheet("background-color: #1e7a3b; border-top-left-radius: 2px; border-top-right-radius: 2px;");
        }
    } else {
        setStyleSheet(R"(
            InputCardWidget {
                background-color: #1a1c22;
                border: 1px solid #2e323e;
                border-radius: 3px;
            }
        )");
        if (m_headerWidget) {
            m_headerWidget->setStyleSheet("background-color: #242833; border-top-left-radius: 2px; border-top-right-radius: 2px;");
        }
    }
}

} // namespace sm
