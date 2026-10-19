#include "ptz_panel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QString>
#include <QTimer>

namespace sm {

PTZPanel::PTZPanel(PTZController& controller, QWidget* parent)
    : QWidget(parent), m_ptzController(controller)
{
    setupUI();
}

void PTZPanel::setupUI() {
    setFixedWidth(280);
    setStyleSheet(R"(
        QGroupBox {
            color: #9aa0b4;
            font-size: 10px;
            font-weight: bold;
            border: 1px solid #2e303a;
            border-radius: 4px;
            margin-top: 10px;
            padding-top: 6px;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; }
        QPushButton {
            background-color: #2a2c36;
            color: #dcdcdc;
            border: 1px solid #3a3d4a;
            border-radius: 3px;
            font-size: 11px;
            padding: 4px;
        }
        QPushButton:pressed { background-color: #3d4050; }
        QPushButton:checked { background-color: #1a5c8a; border-color: #2980b9; color: white; }
        QComboBox {
            background-color: #2a2c36;
            color: #dcdcdc;
            border: 1px solid #3a3d4a;
            border-radius: 3px;
            padding: 4px;
        }
        QSlider::groove:horizontal { height: 4px; background: #3a3d4a; border-radius: 2px; }
        QSlider::handle:horizontal { background: #2980b9; width: 14px; height: 14px; border-radius: 7px; margin: -5px 0; }
        QLabel { color: #9aa0b4; font-size: 10px; }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // ─── Camera Selector ─────────────────────────────────────────
    auto* camGroup = new QGroupBox("PTZ Camera", this);
    auto* camLayout = new QVBoxLayout(camGroup);
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->addItem("No cameras registered", QVariant::fromValue<uint64_t>(0));
    connect(m_cameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PTZPanel::onCameraSelected);
    camLayout->addWidget(m_cameraCombo);
    root->addWidget(camGroup);

    // ─── Joystick Pad ────────────────────────────────────────────
    auto* joyGroup = new QGroupBox("Pan / Tilt", this);
    auto* joyGrid  = new QGridLayout(joyGroup);
    joyGrid->setSpacing(3);

    auto makeJoyBtn = [&](const QString& label) {
        auto* btn = new QPushButton(label, this);
        btn->setFixedSize(40, 32);
        return btn;
    };

    auto* btnUL = makeJoyBtn("↖");
    auto* btnU  = makeJoyBtn("▲");
    auto* btnUR = makeJoyBtn("↗");
    auto* btnL  = makeJoyBtn("◀");
    auto* btnSt = makeJoyBtn("■");  // Stop
    auto* btnR  = makeJoyBtn("▶");
    auto* btnDL = makeJoyBtn("↙");
    auto* btnD  = makeJoyBtn("▼");
    auto* btnDR = makeJoyBtn("↘");

    joyGrid->addWidget(btnUL, 0, 0); joyGrid->addWidget(btnU,  0, 1); joyGrid->addWidget(btnUR, 0, 2);
    joyGrid->addWidget(btnL,  1, 0); joyGrid->addWidget(btnSt, 1, 1); joyGrid->addWidget(btnR,  1, 2);
    joyGrid->addWidget(btnDL, 2, 0); joyGrid->addWidget(btnD,  2, 1); joyGrid->addWidget(btnDR, 2, 2);

    connect(btnU,  &QPushButton::pressed, this, &PTZPanel::onTiltUp);
    connect(btnD,  &QPushButton::pressed, this, &PTZPanel::onTiltDown);
    connect(btnL,  &QPushButton::pressed, this, &PTZPanel::onPanLeft);
    connect(btnR,  &QPushButton::pressed, this, &PTZPanel::onPanRight);
    connect(btnSt, &QPushButton::pressed, this, &PTZPanel::onStopPanTilt);
    // Diagonals — compound pan+tilt
    connect(btnUL, &QPushButton::pressed, this, [this]{ m_ptzController.panLeft(currentSpeed()); m_ptzController.tiltUp(currentSpeed()); });
    connect(btnUR, &QPushButton::pressed, this, [this]{ m_ptzController.panRight(currentSpeed()); m_ptzController.tiltUp(currentSpeed()); });
    connect(btnDL, &QPushButton::pressed, this, [this]{ m_ptzController.panLeft(currentSpeed()); m_ptzController.tiltDown(currentSpeed()); });
    connect(btnDR, &QPushButton::pressed, this, [this]{ m_ptzController.panRight(currentSpeed()); m_ptzController.tiltDown(currentSpeed()); });
    // Release any button -> stop
    for (auto* btn : {btnUL, btnU, btnUR, btnL, btnR, btnDL, btnD, btnDR}) {
        connect(btn, &QPushButton::released, this, &PTZPanel::onStopPanTilt);
    }
    root->addWidget(joyGroup);

    // ─── Zoom Row ─────────────────────────────────────────────────
    auto* zoomGroup = new QGroupBox("Zoom / Focus", this);
    auto* zoomRow   = new QHBoxLayout(zoomGroup);
    auto* btnZoomW  = new QPushButton("W  ◄◄", this);
    auto* btnZoomT  = new QPushButton("►► T", this);
    btnZoomW->setFixedHeight(28);
    btnZoomT->setFixedHeight(28);
    connect(btnZoomW, &QPushButton::pressed,  this, &PTZPanel::onZoomOut);
    connect(btnZoomT, &QPushButton::pressed,  this, &PTZPanel::onZoomIn);
    connect(btnZoomW, &QPushButton::released, this, [this]{ auto c = m_ptzController.camera(m_ptzController.activeCamera()); if(c) c->stopZoom(); });
    connect(btnZoomT, &QPushButton::released, this, [this]{ auto c = m_ptzController.camera(m_ptzController.activeCamera()); if(c) c->stopZoom(); });

    m_btnAutoFocus = new QPushButton("AF", this);
    m_btnAutoFocus->setCheckable(true);
    m_btnAutoFocus->setChecked(true);
    m_btnAutoFocus->setFixedWidth(32);
    connect(m_btnAutoFocus, &QPushButton::toggled, this, &PTZPanel::onAutoFocusToggled);

    zoomRow->addWidget(btnZoomW);
    zoomRow->addWidget(btnZoomT);
    zoomRow->addWidget(m_btnAutoFocus);
    root->addWidget(zoomGroup);

    // ─── Speed ────────────────────────────────────────────────────
    auto* speedGroup = new QGroupBox("Speed", this);
    auto* speedRow   = new QHBoxLayout(speedGroup);
    m_speedLabel  = new QLabel("10", this);
    m_speedSlider = new QSlider(Qt::Horizontal, this);
    m_speedSlider->setRange(1, 24);
    m_speedSlider->setValue(10);
    connect(m_speedSlider, &QSlider::valueChanged, this, &PTZPanel::onSpeedChanged);
    speedRow->addWidget(new QLabel("Slow", this));
    speedRow->addWidget(m_speedSlider);
    speedRow->addWidget(new QLabel("Fast", this));
    speedRow->addWidget(m_speedLabel);
    root->addWidget(speedGroup);

    // ─── Preset Grid ──────────────────────────────────────────────
    auto* presetGroup = new QGroupBox("Presets", this);
    auto* presetGrid  = new QGridLayout(presetGroup);
    presetGrid->setSpacing(3);

    for (int i = 0; i < 9; ++i) {
        auto* btn = new QPushButton(QString::number(i + 1), this);
        btn->setFixedSize(36, 28);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        int presetNum = i + 1;
        connect(btn, &QPushButton::clicked, this, [this, presetNum]{ onPresetRecall(presetNum); });
        // Right-click saves
        connect(btn, &QPushButton::customContextMenuRequested, this,
                [this, presetNum](const QPoint&){ onPresetSave(presetNum); });
        presetGrid->addWidget(btn, i / 3, i % 3);
        m_presetButtons[i] = btn;
    }

    auto* homeBtn = new QPushButton("⌂ Home", this);
    homeBtn->setFixedHeight(28);
    connect(homeBtn, &QPushButton::clicked, this, &PTZPanel::onHome);
    presetGrid->addWidget(homeBtn, 3, 0, 1, 3);
    root->addWidget(presetGroup);

    root->addStretch();
}

void PTZPanel::refreshCameraList() {
    m_cameraCombo->clear();
    for (uint64_t id : m_ptzController.cameraIds()) {
        auto cam = m_ptzController.camera(id);
        if (cam) {
            m_cameraCombo->addItem(QString::fromStdString(cam->name()),
                                    QVariant::fromValue<quint64>(id));
        }
    }
    if (m_cameraCombo->count() == 0) {
        m_cameraCombo->addItem("No cameras registered",
                                QVariant::fromValue<quint64>(0));
    }
}

void PTZPanel::setActiveCameraId(uint64_t id) {
    for (int i = 0; i < m_cameraCombo->count(); ++i) {
        if (m_cameraCombo->itemData(i).toULongLong() == id) {
            m_cameraCombo->setCurrentIndex(i);
            break;
        }
    }
}

void PTZPanel::onCameraSelected(int index) {
    uint64_t id = m_cameraCombo->itemData(index).toULongLong();
    if (id > 0) m_ptzController.setActiveCamera(id);
}

void PTZPanel::onPanLeft()      { m_ptzController.panLeft(currentSpeed()); }
void PTZPanel::onPanRight()     { m_ptzController.panRight(currentSpeed()); }
void PTZPanel::onTiltUp()       { m_ptzController.tiltUp(currentSpeed()); }
void PTZPanel::onTiltDown()     { m_ptzController.tiltDown(currentSpeed()); }
void PTZPanel::onStopPanTilt()  { m_ptzController.stopPanTilt(); }
void PTZPanel::onZoomIn()       { m_ptzController.zoomIn(currentSpeed() / 3 + 1); }
void PTZPanel::onZoomOut()      { m_ptzController.zoomOut(currentSpeed() / 3 + 1); }
void PTZPanel::onHome()         { m_ptzController.home(); }

void PTZPanel::onPresetRecall(int presetNum) {
    m_ptzController.recallPreset(static_cast<uint8_t>(presetNum),
                                  static_cast<uint8_t>(currentSpeed()));
}

void PTZPanel::onPresetSave(int presetNum) {
    m_ptzController.savePreset(static_cast<uint8_t>(presetNum));
    // Flash button briefly amber to confirm save
    auto* btn = m_presetButtons[presetNum - 1];
    btn->setStyleSheet("background-color: #c07800;");
    QTimer::singleShot(600, btn, [btn]{ btn->setStyleSheet(""); });
}

void PTZPanel::onSpeedChanged(int value) {
    m_speed = value;
    m_speedLabel->setText(QString::number(value));
}

void PTZPanel::onAutoFocusToggled(bool enabled) {
    auto cam = m_ptzController.camera(m_ptzController.activeCamera());
    if (cam) cam->setAutoFocus(enabled);
}

} // namespace sm
