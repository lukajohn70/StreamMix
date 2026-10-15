#include "input_card_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>

namespace sm {

InputCardWidget::InputCardWidget(uint64_t sourceId, int index, const QString& name,
                                 const QString& typeName, QWidget* parent)
    : QWidget(parent), m_sourceId(sourceId), m_index(index), m_name(name), m_typeName(typeName)
{
    setupUI();
}

void InputCardWidget::setupUI() {
    setFixedSize(180, 140);
    setStyleSheet(R"(
        QWidget {
            background-color: #21232a;
            border: 1px solid #353844;
            border-radius: 4px;
            color: #dcdcdc;
            font-family: 'Inter', sans-serif;
            font-size: 11px;
        }
        QPushButton {
            background-color: #2e313b;
            border: 1px solid #434756;
            border-radius: 3px;
            font-size: 10px;
            font-weight: 600;
            padding: 2px 4px;
        }
        QPushButton:hover {
            background-color: #3d4252;
        }
        QProgressBar {
            background-color: #121316;
            border: none;
            border-radius: 1px;
        }
        QProgressBar::chunk {
            background-color: #2ecc71;
        }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(3);

    // Header: Index + Name + Type + Settings
    auto* header = new QHBoxLayout();
    header->setSpacing(4);

    auto* badge = new QLabel(QString::number(m_index), this);
    badge->setStyleSheet("background-color: #34495e; color: white; font-weight: bold; border-radius: 2px; padding: 1px 4px;");
    header->addWidget(badge);

    m_titleLabel = new QLabel(m_name, this);
    m_titleLabel->setStyleSheet("font-weight: 600; font-size: 11px; border: none;");
    header->addWidget(m_titleLabel, 1);

    m_btnSettings = new QPushButton("⚙", this);
    m_btnSettings->setFixedSize(18, 18);
    connect(m_btnSettings, &QPushButton::clicked, this, [this]() {
        emit settingsClicked(m_sourceId);
    });
    header->addWidget(m_btnSettings);
    mainLayout->addLayout(header);

    // Thumbnail + Audio Meters side-by-side
    auto* body = new QHBoxLayout();
    body->setSpacing(3);

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setFixedSize(148, 83);
    m_thumbLabel->setStyleSheet("background-color: #0d0e11; border: 1px solid #1a1c22;");
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setText(m_typeName);
    body->addWidget(m_thumbLabel);

    // Audio Meters
    auto* meterLayout = new QVBoxLayout();
    meterLayout->setSpacing(1);

    m_meterL = new QProgressBar(this);
    m_meterL->setOrientation(Qt::Vertical);
    m_meterL->setRange(0, 100);
    m_meterL->setValue(0);
    m_meterL->setFixedWidth(4);
    meterLayout->addWidget(m_meterL);

    m_meterR = new QProgressBar(this);
    m_meterR->setOrientation(Qt::Vertical);
    m_meterR->setRange(0, 100);
    m_meterR->setValue(0);
    m_meterR->setFixedWidth(4);
    meterLayout->addWidget(m_meterR);

    body->addLayout(meterLayout);
    mainLayout->addLayout(body);

    // Action buttons row: PREVIEW, CUT, GO
    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(3);

    m_btnPreview = new QPushButton("PREV", this);
    m_btnPreview->setFixedHeight(20);
    connect(m_btnPreview, &QPushButton::clicked, this, [this]() {
        emit previewClicked(m_sourceId);
    });
    btnRow->addWidget(m_btnPreview);

    m_btnCut = new QPushButton("CUT", this);
    m_btnCut->setFixedHeight(20);
    m_btnCut->setStyleSheet("background-color: #c0392b; color: white;");
    connect(m_btnCut, &QPushButton::clicked, this, [this]() {
        emit cutClicked(m_sourceId);
    });
    btnRow->addWidget(m_btnCut);

    m_btnGo = new QPushButton("GO", this);
    m_btnGo->setFixedHeight(20);
    m_btnGo->setStyleSheet("background-color: #2980b9; color: white;");
    connect(m_btnGo, &QPushButton::clicked, this, [this]() {
        emit goClicked(m_sourceId);
    });
    btnRow->addWidget(m_btnGo);

    mainLayout->addLayout(btnRow);
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

void InputCardWidget::setActivePreview(bool isPreview) {
    m_isPreview = isPreview;
    if (m_isProgram) {
        setStyleSheet("InputCardWidget { border: 2px solid #e74c3c; background-color: #281e20; }");
    } else if (m_isPreview) {
        setStyleSheet("InputCardWidget { border: 2px solid #2ecc71; background-color: #1e2822; }");
    } else {
        setStyleSheet("InputCardWidget { border: 1px solid #353844; background-color: #21232a; }");
    }
}

void InputCardWidget::setActiveProgram(bool isProgram) {
    m_isProgram = isProgram;
    setActivePreview(m_isPreview);
}

} // namespace sm
