// 主窗口：菜单栏(文件/设备/帮助) + 波形 + 数据表/参数/通道页 + 状态栏；
// 串口与回放共用一个工作线程。
// 低频操作（认证、记录/回放、参数方案、退出）收进顶部菜单栏，右栏只留高频调控项。
#pragma once

#include "protocol/frame.h"

#include <QMainWindow>
#include <QString>
#include <QThread>
#include <QVector>

QT_BEGIN_NAMESPACE
class QAction;
class QCheckBox;
class QColor;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolButton;
QT_END_NAMESPACE

namespace smd {

class DataHub;
class ITransport;
class ParamPanel;
class Recorder;
class SerialAssistant;
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
        QString uiMode = QStringLiteral("control");   // control | serial（启动时的右栏模式）
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
    void onModeChanged(int id);                      // 控制台 / 串口助手 切换
    void onAbout();                                  // 帮助 → 关于

private:
    void buildUi();                 // 示波器式布局：顶部菜单栏 + 左波形 / 右调控面板
    void buildMenus();              // 文件 / 设备 / 帮助
    void wireSignals();
    void sendCommand(const QByteArray &bytes);
    void rebuildChannelStyleMenu(int c, QMenu *menu);   // 通道配色/线型下拉（每次弹出重建）
    void updateChannelStyleButton(int c);               // 刷新样式按钮上的线样图标
    void applyChannelColor(int c, const QColor &col);   // 改色：波形 + 勾选框文字色 + 图标
    void switchToReplay(const QString &path, double fps);
    void startRecording(const QString &rawPath, const QString &csvPath, bool askDir);
    void stopRecording();
    QVector<int> visibleChannels() const;
    bool serialModeActive() const;   // 当前是否处于「串口助手」模式

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

    SerialAssistant *m_assistant = nullptr;
    QStackedWidget *m_mainStack = nullptr;   // 左主区：0 波形 / 1 串口终端
    QStackedWidget *m_rightStack = nullptr;  // 右栏：0 控制台 / 1 串口参数
    QToolButton *m_modeControlBtn = nullptr;
    QToolButton *m_modeSerialBtn = nullptr;

    QComboBox *m_portBox = nullptr;
    QSpinBox *m_rateBox = nullptr;
    QDoubleSpinBox *m_windowBox = nullptr;
    QVector<QDoubleSpinBox *> m_gainSpins;   // 每通道显示增益输入框
    QVector<QCheckBox *> m_chanChecks;       // 每通道显示勾选框（改色时同步文字色）
    QVector<QToolButton *> m_chanStyleBtns;  // 每通道配色/线型按钮
    QVector<QMenu *> m_chanStyleMenus;       // 对应下拉菜单
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
    QAction *m_cursorAct = nullptr;         // 显示 → 光标测量（X1/X2、Y1/Y2）
    QAction *m_saveProfileAct = nullptr;    // 文件 → 保存参数方案…
    QAction *m_loadProfileAct = nullptr;    // 文件 → 加载参数方案…
    QAction *m_quitAct = nullptr;           // 文件 → 退出
    QAction *m_aboutAct = nullptr;          // 帮助 → 关于

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
