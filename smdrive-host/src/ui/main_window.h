// 主窗口：工具栏 + 波形 + 数据表/参数/通道页 + 状态栏；串口与回放共用一个工作线程。
#pragma once

#include "protocol/frame.h"

#include <QMainWindow>
#include <QString>
#include <QThread>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTabWidget;
class QTimer;
QT_END_NAMESPACE

namespace smd {

class DataHub;
class ITransport;
class ParamPanel;
class Recorder;
class SerialTransport;
class ReplayTransport;
class TablePanel;
class WaveformView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    struct Options {
        QString replayPath;          // 非空则启动即进入回放模式
        double replayFps = 250.0;
        QString recordRawPath;       // 启动即记录原始流
        QString recordCsvPath;       // 启动即记录 CSV
        QString screenshotPath;      // 抓一张界面截图（开发/验证用）
        int screenshotDelayMs = 1500;
        QVector<QPair<int, double>> channelGains;   // 启动时预设的每通道显示增益
        int autoCloseMs = 0;         // >0 则到点自动退出（自动化验证用）
        int sampleRateHz = 10000;
        double windowSeconds = 0.2;
    };

    explicit MainWindow(const Options &opt, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onRefreshPorts();
    void onTogglePort();
    void onAuthClicked();
    void onStartUploadClicked();
    void onStartStopClicked();
    void onAccelClicked();
    void onDecelClicked();
    void onRecordToggled(bool on);
    void onReplayClicked();
    void onTick();                                   // 60 Hz 刷新
    void onFrameFromTransport(const smd::DecodedFrame &frame);
    void onTransportOpened();
    void onTransportClosed();
    void onTransportError(const QString &msg);
    void onCounters(quint64 framesData, quint64 dropped, quint64 bytesIn);
    void onSerialSeen(const QString &serial);

private:
    void buildUi();                 // 示波器式布局：左波形 / 右调控面板
    void wireSignals();
    void sendCommand(const QByteArray &bytes);
    void switchToReplay(const QString &path, double fps);
    void startRecording(const QString &rawPath, const QString &csvPath, bool askDir);
    void stopRecording();
    QVector<int> visibleChannels() const;

    Options m_opt;
    QThread *m_worker = nullptr;
    SerialTransport *m_serial = nullptr;
    ReplayTransport *m_replay = nullptr;
    ITransport *m_active = nullptr;

    DataHub *m_hub = nullptr;
    Recorder *m_recorder = nullptr;

    WaveformView *m_wave = nullptr;
    TablePanel *m_table = nullptr;
    ParamPanel *m_params = nullptr;
    QTabWidget *m_tabs = nullptr;

    QComboBox *m_portBox = nullptr;
    QSpinBox *m_rateBox = nullptr;
    QDoubleSpinBox *m_windowBox = nullptr;
    QVector<QDoubleSpinBox *> m_gainSpins;   // 每通道显示增益输入框
    QLabel *m_connLabel = nullptr;      // 右侧面板内的连接状态
    QLabel *m_serialLabel = nullptr;    // 右侧面板内的串号
    QLabel *m_statusPort = nullptr;
    QLabel *m_statusFrames = nullptr;
    QLabel *m_statusDrop = nullptr;
    QLabel *m_statusRate = nullptr;
    QLabel *m_statusSerial = nullptr;
    QLabel *m_statusRecord = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_authAct = nullptr;
    QAction *m_uploadAct = nullptr;
    QAction *m_startAct = nullptr;
    QAction *m_accelAct = nullptr;
    QAction *m_decelAct = nullptr;
    QAction *m_recordAct = nullptr;
    QAction *m_replayAct = nullptr;
    QAction *m_pauseAct = nullptr;

    QTimer *m_tick = nullptr;
    quint64 m_lastFrames = 0;
    quint64 m_lastTickMs = 0;
    double m_rateFps = 0.0;
    quint64 m_decoderDrop = 0;
    quint64 m_bytesIn = 0;
    QString m_serialHex;
    bool m_started = false;
    bool m_recordingActive = false;
    bool m_suppressRecordToggle = false;   // 程序化 setChecked 时抑制 toggled 回调（否则会弹对话框）
    bool m_pendingAuth = false;
};

} // namespace smd
