#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QImage>
#include <QPainter>
#include <memory>
#include <mutex>
#include "streammix/frame.h"

namespace sm {

enum class MonitorType {
    Preview,
    Program
};

enum class SafeAreasType {
    Off,
    EBUSMPTE,      // EBU R95 / SMPTE RP 218: 93% Action Safe, 90% Title Safe, Center Crosshair
    EBUSMPTE43,    // EBU / SMPTE with 4:3 Shoot & Protect vertical pillar lines
    EDIT8090,      // NLE Guidelines: 90% Action Safe, 80% Title Safe, Center Crosshair
    EDIT809043,    // 80/90% with 4:3 Shoot & Protect vertical pillar lines
    GRID3,         // Rule of Thirds (3x3 grid)
    GRID4,         // 4x4 Grid
    GRID8169,      // 8x8 / 16:9 Aspect proportional grid
    GRID16,        // 16x16 Fine alignment grid
    GRID32         // 32x32 Precision pixel grid
};

class MonitorWidget : public QWidget {
    Q_OBJECT

public:
    explicit MonitorWidget(MonitorType type, QWidget* parent = nullptr);
    ~MonitorWidget() override = default;

    void updateFrame(VideoFramePtr frame);
    void setSourceName(const QString& name);
    void setTimecode(const QString& timecode);
    void setStreamDuration(const QString& duration);
    void updateAudioMeters(float peakL, float peakR);
    void setSafeAreasType(SafeAreasType type);
    SafeAreasType safeAreasType() const { return m_safeAreasType; }
    void setShowSafeAreas(bool show);

signals:
    void safeAreasChanged(SafeAreasType type);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupSafeAreasMenu();
    void updateSafeAreaButtonStyle();

    MonitorType m_type;
    QString m_sourceName;
    QString m_timecode = "00:00:00:00";
    QString m_streamDuration = "";
    float   m_peakL = 0.0f;
    float   m_peakR = 0.0f;

    SafeAreasType m_safeAreasType = SafeAreasType::Off;
    SafeAreasType m_lastActiveType = SafeAreasType::EBUSMPTE;

    QPushButton*  m_btnSafeArea = nullptr;
    QMenu*        m_safeAreasMenu = nullptr;
    QActionGroup* m_safeAreasActionGroup = nullptr;

    QImage m_currentImage;
    std::mutex m_imageMutex;
};

} // namespace sm
