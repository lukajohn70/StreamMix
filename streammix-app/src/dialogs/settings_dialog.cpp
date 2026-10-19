#include "settings_dialog.h"
#include "streammix/context.h"
#include "streammix/stream_manager.h"
#include "streammix/record_output.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>

namespace sm {

SettingsDialog::SettingsDialog(Context& context, QWidget* parent)
    : QDialog(parent), m_context(context)
{
    setupUI();
}

void SettingsDialog::setupUI() {
    setWindowTitle(tr("StreamMix Settings — Broadcast Configuration"));
    resize(640, 480);
    setStyleSheet(R"(
        QDialog {
            background-color: #16171d;
            color: #dcdcdc;
            font-family: 'Inter', -apple-system, sans-serif;
            font-size: 12px;
        }
        QTabWidget::pane {
            border: 1px solid #282a36;
            background-color: #1a1c23;
            border-radius: 4px;
        }
        QTabBar::tab {
            background-color: #20222a;
            color: #94a3b8;
            padding: 8px 14px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            font-weight: 600;
        }
        QTabBar::tab:selected {
            background-color: #1a1c23;
            color: #60a5fa;
            border-bottom: 2px solid #3b82f6;
        }
        QGroupBox {
            font-weight: bold;
            border: 1px solid #2d313e;
            border-radius: 4px;
            margin-top: 12px;
            padding-top: 14px;
            color: #cbd5e1;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 4px;
        }
        QLabel {
            color: #cbd5e1;
        }
        QLineEdit, QComboBox, QSpinBox {
            background-color: #0f1015;
            border: 1px solid #333846;
            border-radius: 3px;
            color: #e2e8f0;
            padding: 4px 8px;
            min-height: 22px;
        }
        QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
            border-color: #3b82f6;
        }
        QPushButton {
            background-color: #242732;
            border: 1px solid #363b4a;
            border-radius: 3px;
            color: #e2e8f0;
            padding: 6px 16px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #313746;
        }
        QPushButton#primaryBtn {
            background-color: #1d4ed8;
            border-color: #2563eb;
            color: white;
        }
        QPushButton#primaryBtn:hover {
            background-color: #1e40af;
        }
    )");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(createDisplayTab(), tr("Display / Master"));
    m_tabWidget->addTab(createOutputsTab(), tr("Outputs"));
    m_tabWidget->addTab(createAudioTab(), tr("Audio"));
    m_tabWidget->addTab(createStreamingTab(), tr("Streaming"));
    m_tabWidget->addTab(createRecordingTab(), tr("Recording"));
    m_tabWidget->addTab(createWebTab(), tr("Web Controller"));
    root->addWidget(m_tabWidget);

    // Bottom Action Buttons
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();

    auto* btnCancel = new QPushButton(tr("Cancel"), this);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(btnCancel);

    auto* btnApply = new QPushButton(tr("Apply"), this);
    connect(btnApply, &QPushButton::clicked, this, &SettingsDialog::onApply);
    btnRow->addWidget(btnApply);

    auto* btnOk = new QPushButton(tr("OK"), this);
    btnOk->setObjectName("primaryBtn");
    connect(btnOk, &QPushButton::clicked, this, &SettingsDialog::onOk);
    btnRow->addWidget(btnOk);

    root->addLayout(btnRow);
}

QWidget* SettingsDialog::createDisplayTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Master Canvas & Frame Rate"), page);
    auto* fl = new QFormLayout(grp);

    m_comboResolution = new QComboBox(grp);
    m_comboResolution->addItems({ "1920x1080 (1080p FHD 16:9)", "3840x2160 (4K UHD 16:9)", "1280x720 (720p HD 16:9)", "1080x1920 (9:16 Vertical)" });
    fl->addRow(tr("Master Resolution:"), m_comboResolution);

    m_comboFramerate = new QComboBox(grp);
    m_comboFramerate->addItems({ "59.94p (Broadcast NTSC)", "60.00p (Standard Web)", "50.00p (Broadcast PAL)", "29.97p", "25.00p", "24.00p (Cinematic)" });
    fl->addRow(tr("Output Frame Rate:"), m_comboFramerate);

    layout->addWidget(grp);

    auto* grpEngine = new QGroupBox(tr("Graphics & Video Engine"), page);
    auto* flEngine = new QFormLayout(grpEngine);
    auto* lblRenderer = new QLabel(tr("Metal / OpenGL 4.1 Core (Hardware Accelerated)"), grpEngine);
    lblRenderer->setStyleSheet("color: #10b981; font-weight: bold;");
    flEngine->addRow(tr("Graphics Backend:"), lblRenderer);

    auto* lblDecoder = new QLabel(tr("Apple VideoToolbox / FFmpeg 7 Universal"), grpEngine);
    lblDecoder->setStyleSheet("color: #60a5fa; font-weight: bold;");
    flEngine->addRow(tr("Hardware Acceleration:"), lblDecoder);

    layout->addWidget(grpEngine);
    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createOutputsTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp1 = new QGroupBox(tr("Fullscreen Display Outputs"), page);
    auto* fl1 = new QFormLayout(grp1);

    auto* comboDisp1 = new QComboBox(grp1);
    comboDisp1->addItems({ tr("Display 1 (Built-in Retina Display)"), tr("Display 2 (HDMI / External Monitor)"), tr("None") });
    fl1->addRow(tr("Fullscreen 1:"), comboDisp1);

    auto* comboDisp2 = new QComboBox(grp1);
    comboDisp2->addItems({ tr("Program Output"), tr("Preview Output"), tr("MultiView (8-Way Grid)") });
    fl1->addRow(tr("Fullscreen 1 Source:"), comboDisp2);

    layout->addWidget(grp1);

    auto* grpNdi = new QGroupBox(tr("NDI / Network Broadcast Outputs"), page);
    auto* flNdi = new QFormLayout(grpNdi);
    auto* chkNdiProg = new QCheckBox(tr("Broadcast Program Output via NDI / OMT"), grpNdi);
    chkNdiProg->setChecked(true);
    flNdi->addRow(chkNdiProg);

    auto* chkNdiPrev = new QCheckBox(tr("Broadcast Preview Output via NDI / OMT"), grpNdi);
    flNdi->addRow(chkNdiPrev);

    layout->addWidget(grpNdi);
    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createAudioTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Audio Device Routing"), page);
    auto* fl = new QFormLayout(grp);

    m_comboAudioDevice = new QComboBox(grp);
    m_comboAudioDevice->addItems({
        tr("Default System Audio (macOS CoreAudio)"),
        tr("MacBook Pro Speakers"),
        tr("External USB Audio Interface"),
        tr("Blackmagic UltraStudio Audio")
    });
    fl->addRow(tr("Master Audio Output:"), m_comboAudioDevice);

    auto* comboHeadphones = new QComboBox(grp);
    comboHeadphones->addItems({
        tr("Headphones / Solo Bus (Default)"),
        tr("Same as Master Output")
    });
    fl->addRow(tr("Headphones / Solo:"), comboHeadphones);

    m_comboAudioSampleRate = new QComboBox(grp);
    m_comboAudioSampleRate->addItems({ "48000 Hz (Broadcast Standard)", "44100 Hz (Music Standard)", "96000 Hz (Studio Hi-Res)" });
    fl->addRow(tr("Internal Sample Rate:"), m_comboAudioSampleRate);

    m_chkLimiter = new QCheckBox(tr("Enable Soft-Knee Anti-Clipping Limiter on Master & Buses"), grp);
    m_chkLimiter->setChecked(true);
    fl->addRow(m_chkLimiter);

    layout->addWidget(grp);

    auto* grpBuses = new QGroupBox(tr("Sub-Bus Configurations (vMix 29 Specification)"), page);
    auto* flBuses = new QFormLayout(grpBuses);
    auto* lblBusDesc = new QLabel(tr("Active sub-buses: Bus A, Bus B, Bus C, Bus D, Bus E, Bus F, Bus G (Mix-Minus Ready)"), grpBuses);
    lblBusDesc->setStyleSheet("color: #94a3b8;");
    flBuses->addRow(lblBusDesc);
    layout->addWidget(grpBuses);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createStreamingTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Live Streaming Output (RTMP / SRT)"), page);
    auto* fl = new QFormLayout(grp);

    m_comboStreamService = new QComboBox(grp);
    m_comboStreamService->addItems({ "Custom RTMP Server", "YouTube Live", "Facebook Live", "Twitch", "Vimeo", "SRT Caller" });
    fl->addRow(tr("Destination:"), m_comboStreamService);

    m_editStreamUrl = new QLineEdit(grp);
    m_editStreamUrl->setText("rtmp://a.rtmp.youtube.com/live2");
    fl->addRow(tr("Stream URL:"), m_editStreamUrl);

    m_editStreamKey = new QLineEdit(grp);
    m_editStreamKey->setEchoMode(QLineEdit::Password);
    m_editStreamKey->setText("live_stream_key_sample");
    fl->addRow(tr("Stream Key:"), m_editStreamKey);

    m_comboStreamEncoder = new QComboBox(grp);
    m_comboStreamEncoder->addItems({ "Apple VideoToolbox H.264 (Hardware)", "Apple VideoToolbox HEVC / H.265 (Hardware)", "libx264 (Software CPU)" });
    fl->addRow(tr("Video Encoder:"), m_comboStreamEncoder);

    m_spinStreamBitrate = new QSpinBox(grp);
    m_spinStreamBitrate->setRange(1000, 50000);
    m_spinStreamBitrate->setValue(6000);
    m_spinStreamBitrate->setSuffix(" kbps");
    fl->addRow(tr("Video Bitrate:"), m_spinStreamBitrate);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createRecordingTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Master Output Recording"), page);
    auto* fl = new QFormLayout(grp);

    m_comboRecordFormat = new QComboBox(grp);
    m_comboRecordFormat->addItems({ "MP4 (H.264 / AAC High Compatibility)", "MOV (Apple ProRes 422 High Quality)", "MKV (Crash-Resistant Matroska)" });
    fl->addRow(tr("Recording Format:"), m_comboRecordFormat);

    auto* pathRow = new QHBoxLayout();
    m_editRecordPath = new QLineEdit(grp);
    m_editRecordPath->setText("/Users/john/Movies/StreamMix/");
    pathRow->addWidget(m_editRecordPath);

    auto* btnBrowse = new QPushButton(tr("Browse..."), grp);
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Select Recording Directory"), m_editRecordPath->text());
        if (!dir.isEmpty()) m_editRecordPath->setText(dir);
    });
    pathRow->addWidget(btnBrowse);
    fl->addRow(tr("Destination Folder:"), pathRow);

    m_spinRecordBitrate = new QSpinBox(grp);
    m_spinRecordBitrate->setRange(5000, 100000);
    m_spinRecordBitrate->setValue(20000);
    m_spinRecordBitrate->setSuffix(" kbps");
    fl->addRow(tr("Video Bitrate:"), m_spinRecordBitrate);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createWebTab() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);

    auto* grp = new QGroupBox(tr("Tally & Web Remote Control"), page);
    auto* fl = new QFormLayout(grp);

    m_chkWebEnabled = new QCheckBox(tr("Enable Built-in HTTP / WebSocket Control Server"), grp);
    m_chkWebEnabled->setChecked(true);
    fl->addRow(m_chkWebEnabled);

    m_spinWebPort = new QSpinBox(grp);
    m_spinWebPort->setRange(1024, 65535);
    m_spinWebPort->setValue(8088);
    fl->addRow(tr("HTTP Port:"), m_spinWebPort);

    auto* lblLink = new QLabel(tr("🌐 Controller URL: <a href='http://localhost:8088'>http://localhost:8088</a>"), grp);
    lblLink->setOpenExternalLinks(true);
    lblLink->setStyleSheet("color: #60a5fa; font-weight: bold; margin-top: 8px;");
    fl->addRow(lblLink);

    auto* lblDesc = new QLabel(tr("Allows wireless iPad, iPhone, and Android tally lights and remote switching via any web browser."), grp);
    lblDesc->setStyleSheet("color: #94a3b8; font-size: 11px;");
    fl->addRow(lblDesc);

    layout->addWidget(grp);
    layout->addStretch();
    return page;
}

void SettingsDialog::onApply() {
    // 1. Resolution
    if (m_comboResolution) {
        QString resText = m_comboResolution->currentText();
        int w = 1920, h = 1080;
        if (resText.contains("3840x2160")) {
            w = 3840; h = 2160;
        } else if (resText.contains("1280x720")) {
            w = 1280; h = 720;
        } else if (resText.contains("1080x1920")) {
            w = 1080; h = 1920;
        } else {
            w = 1920; h = 1080;
        }
        m_context.setMasterResolution(w, h);
    }

    // 2. Live Streaming configuration
    if (m_comboStreamService && m_editStreamUrl && m_editStreamKey && m_spinStreamBitrate) {
        std::string platform = m_comboStreamService->currentText().toStdString();
        std::string url = m_editStreamUrl->text().toStdString();
        std::string key = m_editStreamKey->text().toStdString();
        int bitrate = m_spinStreamBitrate->value();
        m_context.streamManager().configureStream(1, platform, url, key, bitrate);
    }
}

void SettingsDialog::onOk() {
    onApply();
    accept();
}

} // namespace sm
