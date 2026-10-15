#pragma once
/**
 * PTZPanel — PTZ camera joystick control widget (vMix 29)
 *
 * Contains:
 *   - Camera selector combo box
 *   - 8-directional joystick (arrow buttons + diagonal)
 *   - Zoom in/out controls
 *   - Speed dial (1–24)
 *   - 9-preset quick-recall grid (buttons 1–9)
 *   - Save / Clear preset buttons
 *   - Home button
 *   - Auto-focus toggle
 */

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QGridLayout>
#include <memory>
#include "streammix/ptz_controller.h"

namespace sm {

class PTZPanel : public QWidget {
    Q_OBJECT

public:
    explicit PTZPanel(PTZController& controller, QWidget* parent = nullptr);
    ~PTZPanel() override = default;

    void refreshCameraList();
    void setActiveCameraId(uint64_t id);

private slots:
    void onCameraSelected(int index);
    void onPanLeft();
    void onPanRight();
    void onTiltUp();
    void onTiltDown();
    void onStopPanTilt();
    void onZoomIn();
    void onZoomOut();
    void onHome();
    void onPresetRecall(int presetNum);
    void onPresetSave(int presetNum);
    void onSpeedChanged(int value);
    void onAutoFocusToggled(bool enabled);

private:
    void setupUI();
    uint8_t currentSpeed() const { return static_cast<uint8_t>(m_speed); }

    PTZController& m_ptzController;
    int            m_speed = 10;

    QComboBox*  m_cameraCombo = nullptr;
    QSlider*    m_speedSlider = nullptr;
    QLabel*     m_speedLabel  = nullptr;
    QPushButton* m_btnAutoFocus = nullptr;

    // Preset grid buttons (1–9)
    std::array<QPushButton*, 9> m_presetButtons {};
};

} // namespace sm
