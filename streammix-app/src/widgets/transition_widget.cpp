#include "transition_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>

namespace sm {

TransitionWidget::TransitionWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void TransitionWidget::setupUI() {
    setFixedWidth(260);
    setStyleSheet(R"(
        QWidget {
            background-color: #1a1b20;
            color: #dcdcdc;
            font-family: 'Inter', sans-serif;
            font-size: 11px;
        }
        QPushButton {
            background-color: #2a2c34;
            border: 1px solid #3c404d;
            border-radius: 3px;
            padding: 4px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #383c48;
        }
        QPushButton:pressed {
            background-color: #1f2128;
        }
        QPushButton.active {
            background-color: #2980b9;
            border-color: #3498db;
            color: white;
        }
        QPushButton.cut-btn {
            background-color: #c0392b;
            border-color: #e74c3c;
            color: white;
            font-size: 13px;
            font-weight: 700;
        }
        QPushButton.cut-btn:hover {
            background-color: #d63031;
        }
        QPushButton.ftb-btn {
            background-color: #2d3436;
            border-color: #636e72;
            color: #b2bec3;
        }
        QPushButton.ftb-btn.active {
            background-color: #d63031;
            color: white;
        }
        QSlider::groove:vertical {
            background: #121316;
            width: 12px;
            border-radius: 6px;
        }
        QSlider::handle:vertical {
            background: #ecf0f1;
            border: 1px solid #7f8c8d;
            height: 28px;
            margin: 0 -8px;
            border-radius: 4px;
        }
        QSpinBox {
            background-color: #121316;
            border: 1px solid #3c404d;
            border-radius: 3px;
            padding: 2px 4px;
            color: white;
        }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // 1. Overlay Section (8 Overlays - vMix 29)
    auto* ovLabel = new QLabel("OVERLAYS", this);
    ovLabel->setStyleSheet("color: #7f8c8d; font-weight: 700; font-size: 10px;");
    mainLayout->addWidget(ovLabel);

    auto* ovLayout = new QGridLayout();
    ovLayout->setSpacing(3);
    m_overlayButtons.resize(MAX_OVERLAYS);

    for (int i = 0; i < MAX_OVERLAYS; ++i) {
        auto* btn = new QPushButton(QString::number(i + 1), this);
        btn->setProperty("channel", i + 1);
        btn->setFixedHeight(22);
        connect(btn, &QPushButton::clicked, this, &TransitionWidget::onOverlayClicked);
        m_overlayButtons[i] = btn;
        ovLayout->addWidget(btn, i / 4, i % 4);
    }
    mainLayout->addLayout(ovLayout);

    // 2. Transition Type Grid
    auto* transLabel = new QLabel("TRANSITIONS", this);
    transLabel->setStyleSheet("color: #7f8c8d; font-weight: 700; font-size: 10px;");
    mainLayout->addWidget(transLabel);

    auto* transGrid = new QGridLayout();
    transGrid->setSpacing(3);

    struct TransDef {
        const char* label;
        TransitionType type;
    };
    std::vector<TransDef> transList = {
        {"Cut", TransitionType::Cut},
        {"Fade", TransitionType::Fade},
        {"Zoom", TransitionType::Zoom},
        {"Wipe", TransitionType::WipeRight},
        {"Slide", TransitionType::SlideRight},
        {"Fly", TransitionType::Fly},
        {"Cube", TransitionType::Cube},
        {"Stgr 1", TransitionType::Stinger1}
    };

    for (size_t i = 0; i < transList.size(); ++i) {
        auto* btn = new QPushButton(transList[i].label, this);
        btn->setProperty("type", static_cast<int>(transList[i].type));
        btn->setFixedHeight(24);
        if (transList[i].type == m_selectedTransition) {
            btn->setProperty("class", "active");
        }
        connect(btn, &QPushButton::clicked, this, &TransitionWidget::onTransClicked);
        m_transButtons.push_back(btn);
        transGrid->addWidget(btn, static_cast<int>(i / 4), static_cast<int>(i % 4));
    }
    mainLayout->addLayout(transGrid);

    // 3. T-Bar & Controls Row
    auto* middleRow = new QHBoxLayout();

    // Duration + GO + CUT
    auto* leftControls = new QVBoxLayout();

    auto* durLayout = new QHBoxLayout();
    durLayout->addWidget(new QLabel("Ms:", this));
    m_durationSpin = new QSpinBox(this);
    m_durationSpin->setRange(50, 5000);
    m_durationSpin->setValue(500);
    m_durationSpin->setSingleStep(50);
    durLayout->addWidget(m_durationSpin);
    leftControls->addLayout(durLayout);

    m_btnGo = new QPushButton("GO", this);
    m_btnGo->setFixedHeight(36);
    m_btnGo->setStyleSheet("background-color: #2980b9; color: white; font-weight: 700; font-size: 13px;");
    connect(m_btnGo, &QPushButton::clicked, this, [this]() {
        emit transitionRequested(m_selectedTransition, m_durationSpin->value());
    });
    leftControls->addWidget(m_btnGo);

    m_btnCut = new QPushButton("CUT", this);
    m_btnCut->setProperty("class", "cut-btn");
    m_btnCut->setFixedHeight(36);
    connect(m_btnCut, &QPushButton::clicked, this, &TransitionWidget::onCutClicked);
    leftControls->addWidget(m_btnCut);

    m_btnFtb = new QPushButton("FTB", this);
    m_btnFtb->setProperty("class", "ftb-btn");
    m_btnFtb->setFixedHeight(26);
    connect(m_btnFtb, &QPushButton::clicked, this, &TransitionWidget::onFtbClicked);
    leftControls->addWidget(m_btnFtb);

    middleRow->addLayout(leftControls, 2);

    // T-Bar Fader Slider
    m_tbarSlider = new QSlider(Qt::Vertical, this);
    m_tbarSlider->setRange(0, 1000);
    m_tbarSlider->setValue(0);
    connect(m_tbarSlider, &QSlider::valueChanged, this, &TransitionWidget::onTBarChanged);
    middleRow->addWidget(m_tbarSlider, 1, Qt::AlignHCenter);

    mainLayout->addLayout(middleRow);
}

void TransitionWidget::setTBarPosition(float pos) {
    m_tbarSlider->blockSignals(true);
    m_tbarSlider->setValue(static_cast<int>(pos * 1000.0f));
    m_tbarSlider->blockSignals(false);
}

void TransitionWidget::setOverlayActive(int channel, bool active) {
    if (channel >= 1 && channel <= MAX_OVERLAYS) {
        auto* btn = m_overlayButtons[channel - 1];
        if (active) {
            btn->setStyleSheet("background-color: #e67e22; border-color: #f39c12; color: white;");
        } else {
            btn->setStyleSheet("");
        }
    }
}

void TransitionWidget::onCutClicked() {
    emit cutRequested();
}

void TransitionWidget::onTransClicked() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    for (auto* b : m_transButtons) {
        b->setStyleSheet("");
    }
    btn->setStyleSheet("background-color: #2980b9; border-color: #3498db; color: white;");
    m_selectedTransition = static_cast<TransitionType>(btn->property("type").toInt());
}

void TransitionWidget::onTBarChanged(int value) {
    float pos = static_cast<float>(value) / 1000.0f;
    emit tbarMoved(pos);
}

void TransitionWidget::onOverlayClicked() {
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int ch = btn->property("channel").toInt();
    emit overlayToggled(ch);
}

void TransitionWidget::onFtbClicked() {
    emit ftbToggled();
}

} // namespace sm
