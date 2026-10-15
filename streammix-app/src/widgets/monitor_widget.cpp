#include "monitor_widget.h"
#include <QPen>
#include <QColor>
#include <QFont>

namespace sm {

MonitorWidget::MonitorWidget(MonitorType type, QWidget* parent)
    : QWidget(parent), m_type(type)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_sourceName = (m_type == MonitorType::Preview) ? "PREVIEW" : "PROGRAM";
}

void MonitorWidget::updateFrame(VideoFramePtr frame) {
    if (!frame || frame->empty()) return;

    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        // Create QImage directly wrapping/copying RGBA data
        QImage img(frame->data.data(), frame->width, frame->height, frame->linesize, QImage::Format_RGBA8888);
        m_currentImage = img.copy(); // Own memory
    }
    update();
}

void MonitorWidget::setSourceName(const QString& name) {
    m_sourceName = name;
    update();
}

void MonitorWidget::setTimecode(const QString& timecode) {
    m_timecode = timecode;
    update();
}

void MonitorWidget::setStreamDuration(const QString& duration) {
    m_streamDuration = duration;
    update();
}

void MonitorWidget::setShowSafeAreas(bool show) {
    m_showSafeAreas = show;
    update();
}

void MonitorWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 1. Draw outer background
    p.fillRect(rect(), QColor(18, 19, 22));

    int headerH = 24;
    int footerH = 22;
    int videoH = height() - headerH - footerH;
    int videoW = width();

    // 2. Header Bar
    QRect headerRect(0, 0, width(), headerH);
    p.fillRect(headerRect, QColor(28, 30, 36));

    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(true);
    p.setFont(font);

    // Tally Color: Green for Preview, Red for Program
    QColor tallyColor = (m_type == MonitorType::Preview) ? QColor(46, 204, 113) : QColor(231, 76, 60);

    p.setPen(tallyColor);
    p.drawText(8, 16, (m_type == MonitorType::Preview) ? "PREVIEW" : "PROGRAM");

    font.setBold(false);
    p.setFont(font);
    p.setPen(QColor(220, 220, 220));
    p.drawText(80, 16, m_sourceName);

    // 3. Video Display Area (maintain 16:9 aspect ratio)
    QRect canvasArea(0, headerH, videoW, videoH);
    p.fillRect(canvasArea, Qt::black);

    {
        std::lock_guard<std::mutex> lock(m_imageMutex);
        if (!m_currentImage.isNull()) {
            QSize scaledSize = m_currentImage.size().scaled(canvasArea.size(), Qt::KeepAspectRatio);
            int x = canvasArea.x() + (canvasArea.width() - scaledSize.width()) / 2;
            int y = canvasArea.y() + (canvasArea.height() - scaledSize.height()) / 2;
            QRect targetRect(x, y, scaledSize.width(), scaledSize.height());
            p.drawImage(targetRect, m_currentImage);

            // Safe Area Guides
            if (m_showSafeAreas) {
                p.setPen(QPen(QColor(255, 255, 255, 80), 1, Qt::DashLine));
                // Action Safe (90%)
                QRect actionSafe(targetRect.x() + targetRect.width() * 0.05,
                                 targetRect.y() + targetRect.height() * 0.05,
                                 targetRect.width() * 0.9, targetRect.height() * 0.9);
                p.drawRect(actionSafe);

                // Title Safe (80%)
                QRect titleSafe(targetRect.x() + targetRect.width() * 0.1,
                                targetRect.y() + targetRect.height() * 0.1,
                                targetRect.width() * 0.8, targetRect.height() * 0.8);
                p.drawRect(titleSafe);
            }
        } else {
            // Placeholder icon
            p.setPen(QColor(80, 80, 80));
            font.setPixelSize(14);
            p.setFont(font);
            p.drawText(canvasArea, Qt::AlignCenter, "No Signal");
        }
    }

    // Tally border around video canvas
    p.setPen(QPen(tallyColor, 2));
    p.drawRect(canvasArea.adjusted(1, 1, -1, -1));

    // 4. Footer Bar
    QRect footerRect(0, height() - footerH, width(), footerH);
    p.fillRect(footerRect, QColor(24, 25, 29));

    font.setPixelSize(11);
    font.setFamily("Roboto Mono");
    p.setFont(font);
    p.setPen(QColor(160, 160, 160));
    p.drawText(8, height() - 6, m_timecode);

    if (!m_streamDuration.isEmpty()) {
        p.setPen(QColor(231, 76, 60));
        p.drawText(width() - 80, height() - 6, m_streamDuration);
    }
}

} // namespace sm
