#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

namespace sm {

class Context;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(Context& context, QWidget* parent = nullptr);
    ~SettingsDialog() override = default;

private slots:
    void onApply();
    void onOk();

private:
    Context& m_context;
    void setupUI();
    QWidget* createDisplayTab();
    QWidget* createOutputsTab();
    QWidget* createAudioTab();
    QWidget* createRecordingTab();
    QWidget* createStreamingTab();
    QWidget* createWebTab();

    QTabWidget* m_tabWidget = nullptr;

    // Display
    QComboBox* m_comboResolution = nullptr;
    QComboBox* m_comboFramerate = nullptr;

    // Audio
    QComboBox* m_comboAudioDevice = nullptr;
    QComboBox* m_comboAudioSampleRate = nullptr;
    QCheckBox* m_chkLimiter = nullptr;

    // Streaming
    QComboBox* m_comboStreamService = nullptr;
    QLineEdit* m_editStreamUrl = nullptr;
    QLineEdit* m_editStreamKey = nullptr;
    QComboBox* m_comboStreamEncoder = nullptr;
    QSpinBox*  m_spinStreamBitrate = nullptr;

    // Recording
    QComboBox* m_comboRecordFormat = nullptr;
    QLineEdit* m_editRecordPath = nullptr;
    QSpinBox*  m_spinRecordBitrate = nullptr;

    // Web Controller
    QCheckBox* m_chkWebEnabled = nullptr;
    QSpinBox*  m_spinWebPort = nullptr;
};

} // namespace sm
