#include "input_settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <cmath>

namespace sm {

InputSettingsDialog::InputSettingsDialog(uint64_t sourceId, int index, const QString& name,
                                         const QString& typeName, QWidget* parent)
    : QDialog(parent), m_sourceId(sourceId), m_index(index), m_name(name), m_typeName(typeName)
{
    setupUI();
}

QString InputSettingsDialog::inputName() const {
    return m_editName ? m_editName->text().trimmed() : m_name;
}

void InputSettingsDialog::setupUI() {
    setWindowTitle(tr("Input %1 Settings — %2 (%3)").arg(m_index).arg(m_name, m_typeName));
    resize(520, 420);
    setStyleSheet(R"(
        QDialog {
            background-color: #16171d;
            color: #dcdcdc;
            font-family: 'Inter', -apple-system, sans-serif;
            font-size: 12px;
        }
        QTabWidget::pane {
            border: 1px solid #282a36;
            background-color: #1a1c23;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #20222a;
            color: #94a3b8;
            padding: 7px 12px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            font-weight: 600;
        }
        QTabBar::tab:selected {
            background-color: #1a1c23;
            color: #60a5fa;
            border-bottom: 2px solid #3b82f6;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #2d313e;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 12px;
            color: #cbd5e1;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QLabel {
            color: #cbd5e1;
        }
        QLineEdit, QSpinBox {
            background-color: #0f1015;
            border: 1px solid #333846;
            border-radius: 3px;
            color: #e2e8f0;
            padding: 4px 8px;
            min-height: 22px;
        }
        QSlider::groove:horizontal {
            border: 1px solid #2d313e;
            height: 6px;
            background: #111318;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: #3b82f6;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #e2e8f0;
            border: 1px solid #64748b;
            width: 14px;
            margin-top: -4px;
            margin-bottom: -4px;
            border-radius: 7px;
        }
        QPushButton {
            background-color: #242732;
            border: 1px solid #363b4a;
            border-radius: 3px;
            color: #e2e8f0;
            padding: 6px 14px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #313746;
        }
        QPushButton#primaryBtn {
            background-color: #1d4ed8;
            border-color: #2563eb;
            color: white;
        }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createGeneralTab(), tr("General"));
    m_tabWidget->addTab(createColourTab(), tr("Colour Adjust"));
    m_tabWidget->addTab(createAudioTab(), tr("Audio"));
    m_tabWidget->addTab(createPositionTab(), tr("Position / Zoom"));
    root->addWidget(m_tabWidget);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* btnCancel = new QPushButton(tr("Cancel"), this);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnCancel);

    auto* btnApply = new QPushButton(tr("Apply"), this);
    connect(btnApply, &QPushButton::clicked, this, &InputSettingsDialog::onApply);
    btnRow->addWidget(btnApply);

    auto* btnOk = new QPushButton(tr("OK"), this);
    btnOk->setObjectName("primaryBtn");
    connect(btnOk, &QPushButton::clicked, this, &InputSettingsDialog::onOk);
    btnRow->addWidget(btnOk);

    root->addLayout(btnRow);
}

QWidget* InputSettingsDialog::createGeneralTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Input Properties"), page);
    auto* fl = new QFormLayout(grp);

    m_editName = new QLineEdit(m_name, grp);
    fl->addRow(tr("Input Name:"), m_editName);

    m_spinIndex = new QSpinBox(grp);
    m_spinIndex->setRange(1, 999);
    m_spinIndex->setValue(m_index);
    fl->addRow(tr("Input Number:"), m_spinIndex);

    auto* lblType = new QLabel(m_typeName, grp);
    lblType->setStyleSheet("color: #60a5fa; font-weight: bold;");
    fl->addRow(tr("Input Type:"), lblType);

    m_chkLoop = new QCheckBox(tr("Loop Media on Playback Completion"), grp);
    m_chkLoop->setChecked(true);
    fl->addRow(m_chkLoop);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

QWidget* InputSettingsDialog::createColourTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Colour Correction (Hardware Shader)"), page);
    auto* fl = new QFormLayout(grp);

    m_sliderBrightness = new QSlider(Qt::Horizontal, grp);
    m_sliderBrightness->setRange(-100, 100);
    m_sliderBrightness->setValue(0);
    fl->addRow(tr("Brightness:"), m_sliderBrightness);

    m_sliderContrast = new QSlider(Qt::Horizontal, grp);
    m_sliderContrast->setRange(-100, 100);
    m_sliderContrast->setValue(0);
    fl->addRow(tr("Contrast:"), m_sliderContrast);

    m_sliderSaturation = new QSlider(Qt::Horizontal, grp);
    m_sliderSaturation->setRange(0, 200);
    m_sliderSaturation->setValue(100);
    fl->addRow(tr("Saturation:"), m_sliderSaturation);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

QWidget* InputSettingsDialog::createAudioTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Audio Ingest & Routing"), page);
    auto* fl = new QFormLayout(grp);

    auto* gainRow = new QHBoxLayout();
    m_sliderGain = new QSlider(Qt::Horizontal, grp);
    m_sliderGain->setRange(0, 200); // 100 = unity (0 dB)
    m_sliderGain->setValue(100);
    gainRow->addWidget(m_sliderGain, 1);

    m_lblGainValue = new QLabel("0.0 dB", grp);
    m_lblGainValue->setFixedWidth(50);
    gainRow->addWidget(m_lblGainValue);

    connect(m_sliderGain, &QSlider::valueChanged, this, [this](int v) {
        float linear = v / 100.0f;
        float db = (linear <= 0.001f) ? -60.0f : 20.0f * std::log10(linear);
        m_lblGainValue->setText(QString("%1%2 dB").arg(db > 0 ? "+" : "").arg(db, 0, 'f', 1));
        emit gainChanged(m_sourceId, linear);
    });
    fl->addRow(tr("Channel Gain:"), gainRow);

    m_sliderPan = new QSlider(Qt::Horizontal, grp);
    m_sliderPan->setRange(-100, 100);
    m_sliderPan->setValue(0);
    fl->addRow(tr("Pan (L / R):"), m_sliderPan);

    m_spinDelay = new QSpinBox(grp);
    m_spinDelay->setRange(0, 2000);
    m_spinDelay->setValue(0);
    m_spinDelay->setSuffix(" ms");
    fl->addRow(tr("Audio Sync Delay:"), m_spinDelay);

    auto* chkRow = new QHBoxLayout();
    m_chkMute = new QCheckBox(tr("Mute"), grp);
    chkRow->addWidget(m_chkMute);
    m_chkSolo = new QCheckBox(tr("Solo (PFL)"), grp);
    chkRow->addWidget(m_chkSolo);
    fl->addRow(tr("Audio Flags:"), chkRow);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

QWidget* InputSettingsDialog::createPositionTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Transform & Pan / Zoom"), page);
    auto* fl = new QFormLayout(grp);

    auto* sliderZoom = new QSlider(Qt::Horizontal, grp);
    sliderZoom->setRange(10, 300);
    sliderZoom->setValue(100);
    fl->addRow(tr("Zoom:"), sliderZoom);

    auto* sliderPanX = new QSlider(Qt::Horizontal, grp);
    sliderPanX->setRange(-100, 100);
    sliderPanX->setValue(0);
    fl->addRow(tr("Pan X:"), sliderPanX);

    auto* sliderPanY = new QSlider(Qt::Horizontal, grp);
    sliderPanY->setRange(-100, 100);
    sliderPanY->setValue(0);
    fl->addRow(tr("Pan Y:"), sliderPanY);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

void InputSettingsDialog::onApply() {
    if (m_editName && m_editName->text() != m_name) {
        emit nameChanged(m_sourceId, m_editName->text().trimmed());
    }
}

void InputSettingsDialog::onOk() {
    onApply();
    accept();
}

} // namespace sm
