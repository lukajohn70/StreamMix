#pragma once

#include <QWidget>
#include <QLabel>
#include <QImage>
#include <QPainter>
#include <memory>
#include "streammix/frame.h"

namespace sm {

enum class MonitorType {
    Preview,
    Program
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
    void setShowSafeAreas(bool show);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    MonitorType m_type;
    QString m_sourceName;
    QString m_timecode = "00:00:00:00";
    QString m_streamDuration = "";
    bool m_showSafeAreas = false;

    QImage m_currentImage;
    std::mutex m_imageMutex;
};

} // namespace sm
