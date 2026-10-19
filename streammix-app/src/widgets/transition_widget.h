#pragma once

#include <QWidget>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <vector>
#include "streammix/compositor.h"

namespace sm {

class TransitionWidget : public QWidget {
    Q_OBJECT

public:
    explicit TransitionWidget(QWidget* parent = nullptr);
    ~TransitionWidget() override = default;

    void setTBarPosition(float pos);
    void setOverlayActive(int channel, bool active);
    void updateClocks(const QString& elapsed, const QString& tod);

signals:
    void cutRequested();
    void transitionRequested(TransitionType type, int durationMs);
    void tbarMoved(float position);
    void overlayToggled(int channel);
    void ftbToggled();

private slots:
    void onCutClicked();
    void onTransClicked();
    void onTBarChanged(int value);
    void onOverlayClicked();
    void onFtbClicked();

private:
    void setupUI();

    std::vector<QPushButton*> m_overlayButtons;
    std::vector<QPushButton*> m_transButtons;
    QPushButton* m_btnCut = nullptr;
    QPushButton* m_btnGo = nullptr;
    QPushButton* m_btnFtb = nullptr;
    QSlider* m_tbarSlider = nullptr;
    QSpinBox* m_durationSpin = nullptr;
    QLabel* m_clockElapsed = nullptr;
    QLabel* m_clockTod = nullptr;

    TransitionType m_selectedTransition = TransitionType::Fade;
};

} // namespace sm
