#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

namespace sm {

class InputSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit InputSettingsDialog(uint64_t sourceId, int index, const QString& name,
                                const QString& typeName, QWidget* parent = nullptr);
    ~InputSettingsDialog() override = default;

    QString inputName() const;

signals:
    void nameChanged(uint64_t sourceId, const QString& newName);
    void gainChanged(uint64_t sourceId, float gainLinear);

private slots:
    void onApply();
    void onOk();

private:
    void setupUI();
    QWidget* createGeneralTab();
    QWidget* createColourTab();
    QWidget* createAudioTab();
    QWidget* createPositionTab();

    uint64_t m_sourceId;
    int m_index;
    QString m_name;
    QString m_typeName;

    QTabWidget* m_tabWidget = nullptr;

    // General
    QLineEdit* m_editName = nullptr;
    QSpinBox*  m_spinIndex = nullptr;
    QCheckBox* m_chkLoop = nullptr;

    // Colour
    QSlider* m_sliderBrightness = nullptr;
    QSlider* m_sliderContrast = nullptr;
    QSlider* m_sliderSaturation = nullptr;

    // Audio
    QSlider*   m_sliderGain = nullptr;
    QLabel*    m_lblGainValue = nullptr;
    QSlider*   m_sliderPan = nullptr;
    QSpinBox*  m_spinDelay = nullptr;
    QCheckBox* m_chkMute = nullptr;
    QCheckBox* m_chkSolo = nullptr;
};

} // namespace sm
