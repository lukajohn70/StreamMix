#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QImage>
#include "streammix/frame.h"

namespace sm {

class InputCardWidget : public QWidget {
    Q_OBJECT

public:
    explicit InputCardWidget(uint64_t sourceId, int index, const QString& name,
                             const QString& typeName, QWidget* parent = nullptr);
    ~InputCardWidget() override = default;

    uint64_t sourceId() const { return m_sourceId; }
    void updateThumbnail(VideoFramePtr frame);
    void updateAudioMeter(float peakL, float peakR);
    void setActivePreview(bool isPreview);
    void setActiveProgram(bool isProgram);

signals:
    void previewClicked(uint64_t sourceId);
    void cutClicked(uint64_t sourceId);
    void goClicked(uint64_t sourceId);
    void settingsClicked(uint64_t sourceId);

private:
    void setupUI();

    uint64_t m_sourceId;
    int m_index;
    QString m_name;
    QString m_typeName;

    QLabel* m_thumbLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QProgressBar* m_meterL = nullptr;
    QProgressBar* m_meterR = nullptr;
    QPushButton* m_btnPreview = nullptr;
    QPushButton* m_btnCut = nullptr;
    QPushButton* m_btnGo = nullptr;
    QPushButton* m_btnSettings = nullptr;

    bool m_isPreview = false;
    bool m_isProgram = false;
};

} // namespace sm
