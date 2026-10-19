#include "monitor_widget.h"
#include <QPen>
#include <QColor>
#include <QFont>
#include <QResizeEvent>
#include <algorithm>

namespace sm {

MonitorWidget::MonitorWidget(MonitorType type, QWidget* parent)
    : QWidget(parent), m_type(type)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAttribute(Qt::WA_OpaquePaintEvent);

    m_sourceName = (m_type == MonitorType::Preview) ? "PREVIEW" : "OUTPUT";
    m_safeAreasType = (m_type == MonitorType::Preview) ? SafeAreasType::EBUSMPTE : SafeAreasType::Off;
    m_lastActiveType = SafeAreasType::EBUSMPTE;

    setupSafeAreasMenu();
}

void MonitorWidget::setupSafeAreasMenu() {
    m_btnSafeArea = new QPushButton(this);
    m_btnSafeArea->setFixedSize(20, 18);
    m_btnSafeArea->setText("⌗");
    m_btnSafeArea->setToolTip(tr("Safe Areas & Framing Guides (Click to toggle, Right-click for options)"));
    m_btnSafeArea->setCursor(Qt::PointingHandCursor);
    updateSafeAreaButtonStyle();

    m_safeAreasMenu = new QMenu(this);
    m_safeAreasActionGroup = new QActionGroup(this);
    m_safeAreasActionGroup->setExclusive(true);

    struct OptionDef {
        SafeAreasType type;
        const char* label;
        bool separatorBefore;
    };
    std::vector<OptionDef> options = {
        { SafeAreasType::Off, "Off", false },
        { SafeAreasType::EBUSMPTE, "EBU / SMPTE (93% / 90%)", true },
        { SafeAreasType::EBUSMPTE43, "EBU / SMPTE (4:3 Shoot & Protect)", false },
        { SafeAreasType::EDIT8090, "80% / 90% Guidelines", false },
        { SafeAreasType::EDIT809043, "80% / 90% (4:3)", false },
        { SafeAreasType::GRID3, "Grid 3 (Rule of Thirds)", true },
        { SafeAreasType::GRID4, "Grid 4 (4x4)", false },
        { SafeAreasType::GRID8169, "Grid 8 (16:9 Blocks)", false },
        { SafeAreasType::GRID16, "Grid 16", false },
        { SafeAreasType::GRID32, "Grid 32", false }
    };

    for (const auto& opt : options) {
        if (opt.separatorBefore) {
            m_safeAreasMenu->addSeparator();
        }
        auto* act = m_safeAreasMenu->addAction(tr(opt.label), [this, t = opt.type]() {
            setSafeAreasType(t);
        });
        act->setCheckable(true);
        if (opt.type == m_safeAreasType) act->setChecked(true);
        act->setData(static_cast<int>(opt.type));
        m_safeAreasActionGroup->addAction(act);
    }

    // Left-click on button toggles Off <-> Last active type
    connect(m_btnSafeArea, &QPushButton::clicked, this, [this]() {
        if (m_safeAreasType == SafeAreasType::Off) {
            setSafeAreasType(m_lastActiveType);
        } else {
            setSafeAreasType(SafeAreasType::Off);
        }
    });

    // Custom Context Menu on button or right click
    m_btnSafeArea->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_btnSafeArea, &QPushButton::customContextMenuRequested, this, [this](const QPoint& pos) {
        m_safeAreasMenu->exec(m_btnSafeArea->mapToGlobal(pos));
    });
}

void MonitorWidget::updateSafeAreaButtonStyle() {
    if (!m_btnSafeArea) return;
    if (m_safeAreasType != SafeAreasType::Off) {
        m_btnSafeArea->setStyleSheet(R"(
            QPushButton {
                background-color: #facc15;
                color: #0f172a;
                font-weight: 900;
                font-size: 11px;
                border: 1px solid #ffffff;
                border-radius: 2px;
                padding: 0px;
            }
            QPushButton:hover {
                background-color: #fde047;
            }
        )");
    } else {
        m_btnSafeArea->setStyleSheet(R"(
            QPushButton {
                background-color: rgba(0, 0, 0, 0.35);
                color: #ffffff;
                font-weight: bold;
                font-size: 11px;
                border: 1px solid rgba(255, 255, 255, 0.3);
                border-radius: 2px;
                padding: 0px;
            }
            QPushButton:hover {
                background-color: rgba(255, 255, 255, 0.25);
                border-color: #ffffff;
            }
        )");
    }
}

void MonitorWidget::setSafeAreasType(SafeAreasType type) {
    m_safeAreasType = type;
    if (type != SafeAreasType::Off) {
        m_lastActiveType = type;
    }
    for (QAction* act : m_safeAreasActionGroup->actions()) {
        if (act->data().toInt() == static_cast<int>(type)) {
            act->setChecked(true);
            break;
        }
    }
    updateSafeAreaButtonStyle();
    emit safeAreasChanged(m_safeAreasType);
    update();
}

void MonitorWidget::setShowSafeAreas(bool show) {
    setSafeAreasType(show ? m_lastActiveType : SafeAreasType::Off);
}

void MonitorWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_btnSafeArea) {
        int rightOffset = (m_type == MonitorType::Program) ? 74 : 26;
        m_btnSafeArea->move(width() - rightOffset, 3);
    }
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

void MonitorWidget::updateAudioMeters(float peakL, float peakR) {
    m_peakL = std::clamp(peakL, 0.0f, 1.0f);
    m_peakR = std::clamp(peakR, 0.0f, 1.0f);
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

    // 2. Header Bar — Authentic vMix Colors (Preview = Green, Output/Program = Red)
    QRect headerRect(0, 0, width(), headerH);
    QColor headerBg = (m_type == MonitorType::Preview) ? QColor(26, 120, 54) : QColor(185, 28, 28);
    p.fillRect(headerRect, headerBg);

    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(true);
    p.setFont(font);

    // Tally Color: Vivid Green (#22c55e) for Preview, Crimson Red (#ef4444) for Program
    QColor tallyColor = (m_type == MonitorType::Preview) ? QColor(34, 197, 94) : QColor(239, 68, 68);

    p.setPen(Qt::white);
    p.drawText(8, 16, (m_type == MonitorType::Preview) ? "PREVIEW" : "OUTPUT");

    font.setBold(false);
    p.setFont(font);
    p.setPen(QColor(240, 240, 240));
    p.drawText(75, 16, m_sourceName);

    if (m_type == MonitorType::Program) {
        font.setBold(true);
        font.setPixelSize(9);
        p.setFont(font);
        QRect liveRect(width() - 48, 4, 40, 16);
        p.fillRect(liveRect, QColor(213, 0, 0));
        p.setPen(Qt::white);
        p.drawText(liveRect, Qt::AlignCenter, "LIVE");
    }

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

            // ── Authentic vMix Safe Areas & Composition Guides ──
            if (m_safeAreasType != SafeAreasType::Off) {
                p.save();
                p.setClipRect(targetRect);
                p.setBrush(Qt::NoBrush);

                QPen shadowPen(QColor(0, 0, 0, 220), 3.0f, Qt::SolidLine);
                QPen actionPen(QColor(255, 255, 255, 240), 1.5f, Qt::SolidLine);
                QPen titlePen(QColor(56, 189, 248, 240), 1.5f, Qt::DashLine); // High-contrast cyan dashed
                QPen gridPen(QColor(255, 255, 255, 160), 1.0f, Qt::DashLine);

                auto drawGuideRect = [&](float marginXPercent, float marginYPercent, const QString& label, bool isTitle = false) {
                    float mx = targetRect.width() * marginXPercent;
                    float my = targetRect.height() * marginYPercent;
                    QRectF r(targetRect.x() + mx, targetRect.y() + my,
                             targetRect.width() - 2.0f * mx, targetRect.height() - 2.0f * my);
                    p.setPen(shadowPen);
                    p.drawRect(r);
                    p.setPen(isTitle ? titlePen : actionPen);
                    p.drawRect(r);

                    if (!label.isEmpty()) {
                        QFont f = p.font();
                        f.setPixelSize(9);
                        f.setBold(true);
                        p.setFont(f);
                        p.setPen(QColor(0, 0, 0, 220));
                        p.drawText(r.x() + 5, r.y() + 12, label);
                        p.setPen(isTitle ? QColor(56, 189, 248, 255) : QColor(255, 255, 255, 255));
                        p.drawText(r.x() + 4, r.y() + 11, label);
                    }
                };

                auto drawCrosshair = [&]() {
                    float cx = targetRect.center().x();
                    float cy = targetRect.center().y();
                    float arm = std::min(targetRect.width(), targetRect.height()) * 0.035f;

                    p.setPen(shadowPen);
                    p.drawLine(QPointF(cx - arm, cy), QPointF(cx + arm, cy));
                    p.drawLine(QPointF(cx, cy - arm), QPointF(cx, cy + arm));

                    p.setPen(actionPen);
                    p.drawLine(QPointF(cx - arm, cy), QPointF(cx + arm, cy));
                    p.drawLine(QPointF(cx, cy - arm), QPointF(cx, cy + arm));
                };

                auto draw43Pillars = [&]() {
                    // 4:3 shoot & protect inside 16:9 (4:3 width = 75% of 16:9 width)
                    float pWidth = targetRect.width() * 0.75f;
                    float pillarMargin = (targetRect.width() - pWidth) / 2.0f;
                    QRectF r43(targetRect.x() + pillarMargin, targetRect.y(), pWidth, targetRect.height());

                    p.setPen(shadowPen);
                    p.drawRect(r43);
                    p.setPen(QPen(QColor(250, 204, 21, 240), 1.5f, Qt::DashLine));
                    p.drawRect(r43);

                    QFont f = p.font();
                    f.setPixelSize(9);
                    f.setBold(true);
                    p.setFont(f);
                    p.setPen(QColor(250, 204, 21, 255));
                    p.drawText(r43.x() + 5, r43.y() + 13, "4:3 Safe");
                };

                switch (m_safeAreasType) {
                    case SafeAreasType::EBUSMPTE: {
                        drawGuideRect(0.035f, 0.035f, "Action 93%");
                        drawGuideRect(0.05f, 0.05f, "Title 90%", true);
                        drawCrosshair();
                        break;
                    }
                    case SafeAreasType::EBUSMPTE43: {
                        drawGuideRect(0.035f, 0.035f, "Action 93%");
                        drawGuideRect(0.05f, 0.05f, "Title 90%", true);
                        draw43Pillars();
                        drawCrosshair();
                        break;
                    }
                    case SafeAreasType::EDIT8090: {
                        drawGuideRect(0.05f, 0.05f, "Action 90%");
                        drawGuideRect(0.10f, 0.10f, "Title 80%", true);
                        drawCrosshair();
                        break;
                    }
                    case SafeAreasType::EDIT809043: {
                        drawGuideRect(0.05f, 0.05f, "Action 90%");
                        drawGuideRect(0.10f, 0.10f, "Title 80%", true);
                        draw43Pillars();
                        drawCrosshair();
                        break;
                    }
                    case SafeAreasType::GRID3: { // Rule of Thirds
                        p.setPen(gridPen);
                        float w3 = targetRect.width() / 3.0f;
                        float h3 = targetRect.height() / 3.0f;
                        for (int i = 1; i < 3; ++i) {
                            p.drawLine(QPointF(targetRect.x() + i * w3, targetRect.y()),
                                       QPointF(targetRect.x() + i * w3, targetRect.bottom()));
                            p.drawLine(QPointF(targetRect.x(), targetRect.y() + i * h3),
                                       QPointF(targetRect.right(), targetRect.y() + i * h3));
                        }
                        drawCrosshair();
                        break;
                    }
                    case SafeAreasType::GRID4: { // 4x4
                        p.setPen(gridPen);
                        float w4 = targetRect.width() / 4.0f;
                        float h4 = targetRect.height() / 4.0f;
                        for (int i = 1; i < 4; ++i) {
                            p.drawLine(QPointF(targetRect.x() + i * w4, targetRect.y()),
                                       QPointF(targetRect.x() + i * w4, targetRect.bottom()));
                            p.drawLine(QPointF(targetRect.x(), targetRect.y() + i * h4),
                                       QPointF(targetRect.right(), targetRect.y() + i * h4));
                        }
                        break;
                    }
                    case SafeAreasType::GRID8169: { // 16:9 proportional grid
                        p.setPen(gridPen);
                        float w8 = targetRect.width() / 8.0f;
                        float h8 = targetRect.height() / 8.0f;
                        for (int i = 1; i < 8; ++i) {
                            p.drawLine(QPointF(targetRect.x() + i * w8, targetRect.y()),
                                       QPointF(targetRect.x() + i * w8, targetRect.bottom()));
                            p.drawLine(QPointF(targetRect.x(), targetRect.y() + i * h8),
                                       QPointF(targetRect.right(), targetRect.y() + i * h8));
                        }
                        break;
                    }
                    case SafeAreasType::GRID16: { // 16x16
                        p.setPen(QPen(QColor(255, 255, 255, 90), 1.0f, Qt::DotLine));
                        float w16 = targetRect.width() / 16.0f;
                        float h16 = targetRect.height() / 16.0f;
                        for (int i = 1; i < 16; ++i) {
                            p.drawLine(QPointF(targetRect.x() + i * w16, targetRect.y()),
                                       QPointF(targetRect.x() + i * w16, targetRect.bottom()));
                            p.drawLine(QPointF(targetRect.x(), targetRect.y() + i * h16),
                                       QPointF(targetRect.right(), targetRect.y() + i * h16));
                        }
                        break;
                    }
                    case SafeAreasType::GRID32: { // 32x32
                        p.setPen(QPen(QColor(255, 255, 255, 60), 1.0f, Qt::DotLine));
                        float w32 = targetRect.width() / 32.0f;
                        float h32 = targetRect.height() / 32.0f;
                        for (int i = 1; i < 32; ++i) {
                            p.drawLine(QPointF(targetRect.x() + i * w32, targetRect.y()),
                                       QPointF(targetRect.x() + i * w32, targetRect.bottom()));
                            p.drawLine(QPointF(targetRect.x(), targetRect.y() + i * h32),
                                       QPointF(targetRect.right(), targetRect.y() + i * h32));
                        }
                        break;
                    }
                    default:
                        break;
                }
                p.restore();
            }
        } else {
            // Placeholder icon
            p.setPen(QColor(80, 80, 80));
            font.setPixelSize(14);
            p.setFont(font);
            p.drawText(canvasArea, Qt::AlignCenter, "No Signal");
        }
    }

    // ── Side Audio Peak Meters (vMix 29 Studio Standard) ────────
    int meterW = 3;
    int meterH = canvasArea.height() - 12;
    int meterX = canvasArea.right() - (meterW * 2 + 6);
    int meterY = canvasArea.top() + 6;

    auto drawMeterBar = [&](int x, float val) {
        QRect bgRect(x, meterY, meterW, meterH);
        p.fillRect(bgRect, QColor(8, 10, 14, 200));

        int barH = static_cast<int>(std::clamp(val, 0.0f, 1.0f) * meterH);
        if (barH > 0) {
            QRect barRect(x, meterY + meterH - barH, meterW, barH);
            QLinearGradient grad(barRect.left(), barRect.bottom(), barRect.left(), barRect.top());
            grad.setColorAt(0.0, QColor(16, 185, 129)); // Green
            grad.setColorAt(0.65, QColor(16, 185, 129));
            grad.setColorAt(0.85, QColor(245, 158, 11)); // Yellow
            grad.setColorAt(1.0, QColor(239, 68, 68));  // Red
            p.fillRect(barRect, grad);
        }
    };

    drawMeterBar(meterX, m_peakL);
    drawMeterBar(meterX + meterW + 1, m_peakR);

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
