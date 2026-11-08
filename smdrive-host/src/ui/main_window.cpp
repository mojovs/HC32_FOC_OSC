#include "ui/main_window.h"

#include "app/app_config.h"
#include "app/channel_table.h"
#include "data/datahub.h"
#include "data/recorder.h"
#include "protocol/frame_builder.h"
#include "transport/replay_transport.h"
#include "transport/serial_transport.h"
#include "ui/param_panel.h"
#include "ui/table_panel.h"
#include "ui/waveform_view.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace smd {

MainWindow::MainWindow(const Options &opt, QWidget *parent)
    : QMainWindow(parent)
    , m_opt(opt)
{
    m_hub = new DataHub(this);
    m_recorder = new Recorder(this);

    buildUi();

    // ---- 工作线程与两个传输实现（真机 / 回放）----
    m_worker = new QThread(this);
    m_serial = new SerialTransport();
    m_replay = new ReplayTransport();
    m_serial->moveToThread(m_worker);
    m_replay->moveToThread(m_worker);
    m_worker->start();

    m_active = m_serial;
    wireSignals();

    m_hub->configure(m_opt.sampleRateHz, AppConfig::historySeconds());
    m_rateBox->setValue(m_opt.sampleRateHz);
    m_windowBox->setValue(m_opt.windowSeconds);
    m_wave->setWindowSeconds(m_opt.windowSeconds);

    m_tick = new QTimer(this);
    m_tick->setInterval(16);              // ≈60 Hz
    connect(m_tick, &QTimer::timeout, this, &MainWindow::onTick);
    m_tick->start();

    if (!m_opt.recordRawPath.isEmpty() || !m_opt.recordCsvPath.isEmpty()) {
        startRecording(m_opt.recordRawPath, m_opt.recordCsvPath, false);
    }
    if (!m_opt.replayPath.isEmpty())
        switchToReplay(m_opt.replayPath, m_opt.replayFps);
    else
        onRefreshPorts();

    if (m_opt.autoCloseMs > 0)
        QTimer::singleShot(m_opt.autoCloseMs, qApp, &QCoreApplication::quit);

    // 抓界面截图（开发/验证用）：--shot <png> [--shot-delay ms]
    if (!m_opt.screenshotPath.isEmpty()) {
        QTimer::singleShot(qMax(100, m_opt.screenshotDelayMs), this, [this] {
            const QPixmap shot = grab();
            if (shot.save(m_opt.screenshotPath))
                qInfo("screenshot saved: %s", qPrintable(m_opt.screenshotPath));
            else
                qWarning("screenshot failed: %s", qPrintable(m_opt.screenshotPath));
        });
    }
}

MainWindow::~MainWindow()
{
    if (m_recorder)
        m_recorder->stop();
    // 退出时打印一次统计（ASCII，便于脚本化验证与现场排查）
    qInfo("smdrive-host stats: dataFrames=%llu authFrames=%llu droppedFrames=%llu bytesIn=%llu rows=%llu serial=%s",
          static_cast<unsigned long long>(m_hub ? m_hub->framesData() : 0),
          static_cast<unsigned long long>(m_hub ? m_hub->framesAuth() : 0),
          static_cast<unsigned long long>(m_decoderDrop),
          static_cast<unsigned long long>(m_bytesIn),
          static_cast<unsigned long long>(m_recorder ? m_recorder->rowsWritten() : 0),
          m_serialHex.isEmpty() ? "-" : qPrintable(m_serialHex));
    if (m_serial)
        QMetaObject::invokeMethod(m_serial, "close", Qt::BlockingQueuedConnection);
    if (m_replay)
        QMetaObject::invokeMethod(m_replay, "close", Qt::BlockingQueuedConnection);
    if (m_worker) {
        m_worker->quit();
        m_worker->wait(2000);
    }
}

namespace {

// 把 QAction 绑到按钮上：文字/勾选状态随 QAction 自动同步，避免两份状态
QToolButton *makeActionButton(QAction *action, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumHeight(30);
    return button;
}

QGroupBox *makeGroup(const QString &title, QWidget *parent, QVBoxLayout **outLayout)
{
    auto *box = new QGroupBox(title, parent);
    auto *layout = new QVBoxLayout(box);
    layout->setSpacing(6);
    if (outLayout)
        *outLayout = layout;
    return box;
}

} // namespace

// ---------------------------------------------------------------------------
// 示波器式布局：左侧波形主显示区，右侧竖排调控面板（连接 / 显示 / 运行 / 通道 /
// 参数与数据表 / 记录回放），底部状态栏为读数条。
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("SMotorDrive 上位机（Qt 版）"));
    resize(1420, 880);

    // ---------------- 左：波形窗口 ----------------
    m_wave = new WaveformView(this);
    m_wave->setDataHub(m_hub);

    // ---------------- 右：调控面板 ----------------
    auto *panel = new QWidget(this);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(8, 8, 8, 8);
    panelLayout->setSpacing(10);

    // ---- 连接 ----
    QVBoxLayout *connLayout = nullptr;
    auto *connBox = makeGroup(QStringLiteral("连接"), panel, &connLayout);
    auto *portRow = new QHBoxLayout();
    portRow->addWidget(new QLabel(QStringLiteral("串口"), connBox));
    m_portBox = new QComboBox(connBox);
    m_portBox->setMinimumWidth(160);
    portRow->addWidget(m_portBox, 1);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"), connBox);
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);
    portRow->addWidget(refreshBtn);
    connLayout->addLayout(portRow);

    m_openAct = new QAction(QStringLiteral("打开串口"), this);
    connect(m_openAct, &QAction::triggered, this, &MainWindow::onTogglePort);
    connLayout->addWidget(makeActionButton(m_openAct, connBox));

    m_connLabel = new QLabel(QStringLiteral("未连接"), connBox);
    m_serialLabel = new QLabel(QStringLiteral("串号: -"), connBox);
    m_connLabel->setWordWrap(true);
    m_serialLabel->setWordWrap(true);
    connLayout->addWidget(m_connLabel);
    connLayout->addWidget(m_serialLabel);
    panelLayout->addWidget(connBox);

    // ---- 显示 ----
    QVBoxLayout *dispLayout = nullptr;
    auto *dispBox = makeGroup(QStringLiteral("显示"), panel, &dispLayout);
    auto *rateRow = new QHBoxLayout();
    rateRow->addWidget(new QLabel(QStringLiteral("采样率 (Hz)"), dispBox));
    m_rateBox = new QSpinBox(dispBox);
    m_rateBox->setRange(100, 1000000);
    m_rateBox->setSingleStep(1000);
    m_rateBox->setValue(AppConfig::sampleRateHz());
    rateRow->addWidget(m_rateBox, 1);
    dispLayout->addLayout(rateRow);

    auto *winRow = new QHBoxLayout();
    winRow->addWidget(new QLabel(QStringLiteral("窗口 (s)"), dispBox));
    m_windowBox = new QDoubleSpinBox(dispBox);
    m_windowBox->setRange(0.005, 60.0);
    m_windowBox->setDecimals(3);
    m_windowBox->setSingleStep(0.05);
    m_windowBox->setValue(AppConfig::windowSeconds());
    winRow->addWidget(m_windowBox, 1);
    dispLayout->addLayout(winRow);

    m_pauseAct = new QAction(QStringLiteral("暂停显示"), this);
    m_pauseAct->setCheckable(true);
    connect(m_pauseAct, &QAction::toggled, this, [this](bool on) { m_wave->setPaused(on); });
    auto *resetViewBtn = new QPushButton(QStringLiteral("复位视图/偏移"), dispBox);
    connect(resetViewBtn, &QPushButton::clicked, this, [this] { m_wave->resetView(); });
    auto *dispBtnRow = new QHBoxLayout();
    dispBtnRow->addWidget(makeActionButton(m_pauseAct, dispBox));
    dispBtnRow->addWidget(resetViewBtn);
    dispLayout->addLayout(dispBtnRow);
    panelLayout->addWidget(dispBox);

    // ---- 运行控制 ----
    QVBoxLayout *runLayout = nullptr;
    auto *runBox = makeGroup(QStringLiteral("运行控制"), panel, &runLayout);
    m_authAct = new QAction(QStringLiteral("认证"), this);
    connect(m_authAct, &QAction::triggered, this, &MainWindow::onAuthClicked);
    m_uploadAct = new QAction(QStringLiteral("开始上传"), this);
    connect(m_uploadAct, &QAction::triggered, this, &MainWindow::onStartUploadClicked);
    m_startAct = new QAction(QStringLiteral("启动"), this);
    connect(m_startAct, &QAction::triggered, this, &MainWindow::onStartStopClicked);
    m_accelAct = new QAction(QStringLiteral("加速"), this);
    m_decelAct = new QAction(QStringLiteral("减速"), this);
    connect(m_accelAct, &QAction::triggered, this, &MainWindow::onAccelClicked);
    connect(m_decelAct, &QAction::triggered, this, &MainWindow::onDecelClicked);

    auto *runGrid = new QGridLayout();
    runGrid->addWidget(makeActionButton(m_authAct, runBox), 0, 0);
    runGrid->addWidget(makeActionButton(m_uploadAct, runBox), 0, 1);
    runGrid->addWidget(makeActionButton(m_startAct, runBox), 1, 0);
    runGrid->addWidget(makeActionButton(m_accelAct, runBox), 1, 1);
    runGrid->addWidget(makeActionButton(m_decelAct, runBox), 2, 0);
    runLayout->addLayout(runGrid);
    panelLayout->addWidget(runBox);

    // ---- 波形通道（勾选显示 + 绘图偏移，仿示波器通道旋钮；只改本地显示，不发帧）----
    QVBoxLayout *chanOuter = nullptr;
    auto *chanBox = makeGroup(QStringLiteral("波形通道（偏移只改本地绘图）"), panel, &chanOuter);
    auto *chanGrid = new QGridLayout();
    chanGrid->setHorizontalSpacing(6);
    const auto &meta = channelTable();
    const int rowsPerCol = (int(meta.size()) + 1) / 2;          // 10 路排成两列，省竖直空间
    for (int c = 0; c < int(meta.size()); ++c) {
        auto *cb = new QCheckBox(meta[size_t(c)].name, chanBox);
        cb->setChecked(meta[size_t(c)].visible);
        cb->setToolTip(meta[size_t(c)].desc);
        cb->setStyleSheet(QStringLiteral("QCheckBox{color:%1;}").arg(meta[size_t(c)].color.name()));
        m_wave->setChannelVisible(c, meta[size_t(c)].visible);

        auto *offset = new QDoubleSpinBox(chanBox);
        offset->setRange(-1e9, 1e9);
        offset->setDecimals(3);
        offset->setSingleStep(1.0);
        offset->setPrefix(QStringLiteral("Δ"));          // 窄前缀，避免把右栏挤爆
        offset->setToolTip(QStringLiteral("绘图偏移（只改本地显示，不发帧）"));
        offset->setMinimumWidth(84);
        offset->setMaximumWidth(104);

        connect(cb, &QCheckBox::toggled, this, [this, c](bool on) { m_wave->setChannelVisible(c, on); });
        connect(offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, c](double v) { m_wave->setOffset(c, v); });

        const int row = c % rowsPerCol;
        const int col = (c / rowsPerCol) * 2;
        chanGrid->addWidget(cb, row, col);
        chanGrid->addWidget(offset, row, col + 1);
    }
    chanOuter->addLayout(chanGrid);
    panelLayout->addWidget(chanBox);

    // ---- 参数 / 数据表 ----
    m_table = new TablePanel(this);
    m_params = new ParamPanel(this);
    connect(m_params, &ParamPanel::requestParam, this, [this](int idx, float v) {
        sendCommand(FrameBuilder::param(idx, v));
    });
    connect(m_params, &ParamPanel::requestAllParams, this, [this](const QVector<double> &values) {
        for (int i = 0; i < values.size(); ++i)
            sendCommand(FrameBuilder::param(i, float(values[i])));
    });

    m_tabs = new QTabWidget(panel);
    m_tabs->addTab(m_params, QStringLiteral("参数"));
    m_tabs->addTab(m_table, QStringLiteral("数据表"));
    panelLayout->addWidget(m_tabs, 1);

    // ---- 记录 / 回放 ----
    QVBoxLayout *recLayout = nullptr;
    auto *recBox = makeGroup(QStringLiteral("记录 / 回放"), panel, &recLayout);
    m_recordAct = new QAction(QStringLiteral("开始记录"), this);
    m_recordAct->setCheckable(true);
    connect(m_recordAct, &QAction::toggled, this, &MainWindow::onRecordToggled);
    m_replayAct = new QAction(QStringLiteral("回放…"), this);
    connect(m_replayAct, &QAction::triggered, this, &MainWindow::onReplayClicked);
    auto *recRow = new QHBoxLayout();
    recRow->addWidget(makeActionButton(m_recordAct, recBox));
    recRow->addWidget(makeActionButton(m_replayAct, recBox));
    recLayout->addLayout(recRow);
    panelLayout->addWidget(recBox);

    auto *panelScroll = new QScrollArea(this);
    panelScroll->setWidget(panel);
    panelScroll->setWidgetResizable(true);
    panelScroll->setFrameShape(QFrame::NoFrame);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    panelScroll->setMinimumWidth(470);
    panelScroll->setMaximumWidth(820);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_wave);
    splitter->addWidget(panelScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 520});
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    // ---------------- 底部读数条 ----------------
    m_statusPort = new QLabel(QStringLiteral("未连接"), this);
    m_statusFrames = new QLabel(QStringLiteral("帧: 0"), this);
    m_statusDrop = new QLabel(QStringLiteral("丢帧: 0"), this);
    m_statusRate = new QLabel(QStringLiteral("0.0 帧/s"), this);
    m_statusSerial = new QLabel(QStringLiteral("串号: -"), this);
    m_statusRecord = new QLabel(QStringLiteral("未记录"), this);
    // 全部放右侧（permanent）：左侧留给 showMessage 的临时提示，避免文字互相压盖
    statusBar()->addPermanentWidget(m_statusPort);
    statusBar()->addPermanentWidget(m_statusFrames);
    statusBar()->addPermanentWidget(m_statusDrop);
    statusBar()->addPermanentWidget(m_statusRate);
    statusBar()->addPermanentWidget(m_statusSerial);
    statusBar()->addPermanentWidget(m_statusRecord);
}

void MainWindow::wireSignals()
{
    // 传输层 → 主线程（自动排队连接；Decoder 在工作线程完成）
    for (ITransport *t : {static_cast<ITransport *>(m_serial), static_cast<ITransport *>(m_replay)}) {
        connect(t, &ITransport::frameDecoded, this, &MainWindow::onFrameFromTransport);
        connect(t, &ITransport::opened, this, &MainWindow::onTransportOpened);
        connect(t, &ITransport::closed, this, &MainWindow::onTransportClosed);
        connect(t, &ITransport::errorText, this, &MainWindow::onTransportError);
        connect(t, &ITransport::countersUpdated, this, &MainWindow::onCounters);
    }
    connect(m_hub, &DataHub::serialNumberSeen, this, &MainWindow::onSerialSeen);

    connect(m_rateBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int hz) {
        qint64 samples = qint64(m_hub->samplesPerChannel());
        Q_UNUSED(samples)
        AppConfig::setSampleRateHz(hz);
        m_hub->configure(hz, AppConfig::historySeconds());
    });
    connect(m_windowBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double s) {
        AppConfig::setWindowSeconds(s);
        m_wave->setWindowSeconds(s);
    });
    connect(m_portBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_portBox->currentText().isEmpty())
            return;
        AppConfig::setLastPort(m_portBox->currentText());
        if (m_serial)
            m_serial->setPortName(m_portBox->currentText());
    });
}

void MainWindow::onRefreshPorts()
{
    const QString keep = m_portBox->currentText();
    m_portBox->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        QString label = info.portName();
        if (!info.description().isEmpty())
            label += QStringLiteral("  (%1)").arg(info.description());
        m_portBox->addItem(label, info.portName());
    }
    if (m_portBox->count() == 0)
        m_portBox->addItem(QStringLiteral("（未检测到串口）"), QString());
    const int idx = m_portBox->findText(keep);
    if (idx >= 0)
        m_portBox->setCurrentIndex(idx);
    for (int i = 0; i < m_portBox->count(); ++i) {
        if (m_portBox->itemData(i).toString() == AppConfig::lastPort()) {
            m_portBox->setCurrentIndex(i);
            break;
        }
    }
}

void MainWindow::onTogglePort()
{
    if (!m_serial)
        return;
    if (m_serial->isOpen()) {
        QMetaObject::invokeMethod(m_serial, "close", Qt::QueuedConnection);
        m_active = m_serial;
        m_openAct->setText(QStringLiteral("打开串口"));
        return;
    }
    const QString port = m_portBox->currentData().toString();
    if (port.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("没有可用串口"), 4000);
        return;
    }
    m_serial->setPortName(port);
    m_serial->setBaudRate(AppConfig::baudRate());
    m_active = m_serial;
    m_pendingAuth = true;
    QMetaObject::invokeMethod(m_serial, "open", Qt::QueuedConnection);
}

void MainWindow::onAuthClicked()
{
    m_pendingAuth = true;
    sendCommand(FrameBuilder::auth());
}

void MainWindow::onStartUploadClicked()
{
    sendCommand(FrameBuilder::startUpload(0x2710));
}

void MainWindow::onStartStopClicked()
{
    m_started = !m_started;
    sendCommand(FrameBuilder::command(m_started ? CMD_START : CMD_STOP));
    m_startAct->setText(m_started ? QStringLiteral("停止") : QStringLiteral("启动"));
}

void MainWindow::onAccelClicked()
{
    sendCommand(FrameBuilder::command(CMD_ACCEL));
}

void MainWindow::onDecelClicked()
{
    sendCommand(FrameBuilder::command(CMD_DECEL));
}

void MainWindow::sendCommand(const QByteArray &bytes)
{
    if (!m_active || !m_active->isOpen()) {
        statusBar()->showMessage(QStringLiteral("未连接：命令未发送（%1 字节）")
                                     .arg(bytes.size()), 3000);
        return;
    }
    QMetaObject::invokeMethod(m_active, "sendBytes", Qt::QueuedConnection, Q_ARG(QByteArray, bytes));
}

QVector<int> MainWindow::visibleChannels() const
{
    QVector<int> out;
    for (int c = 0; c < 10; ++c) {
        if (m_wave->channelVisible(c))
            out.append(c);
    }
    return out;
}

void MainWindow::startRecording(const QString &rawPath, const QString &csvPath, bool askDir)
{
    QString dir;
    if (askDir) {
        dir = QFileDialog::getExistingDirectory(this, QStringLiteral("选择记录目录"),
                                                AppConfig::lastReportDir());
        if (dir.isEmpty())
            return;
        AppConfig::setLastReportDir(dir);
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString raw = rawPath;
    QString csv = csvPath;
    if (raw.isEmpty() && !dir.isEmpty())
        raw = QDir(dir).filePath(QStringLiteral("smd_%1.bin").arg(stamp));
    if (csv.isEmpty() && !dir.isEmpty())
        csv = QDir(dir).filePath(QStringLiteral("smd_%1.csv").arg(stamp));

    QString err;
    bool ok = false;
    if (!raw.isEmpty()) {
        if (m_recorder->startRaw(raw, &err))
            ok = true;
        else
            QMessageBox::warning(this, QStringLiteral("记录失败"), err);
    }
    if (!csv.isEmpty()) {
        if (m_recorder->startCsv(csv, visibleChannels(), m_hub->sampleRate(), &err))
            ok = true;
        else
            QMessageBox::warning(this, QStringLiteral("记录失败"), err);
    }
    m_recordingActive = ok;
    m_suppressRecordToggle = true;         // 避免 setChecked 触发 onRecordToggled 再弹一次目录对话框
    m_recordAct->setChecked(ok);
    m_suppressRecordToggle = false;
    m_statusRecord->setText(ok ? QStringLiteral("记录中 → %1").arg(QFileInfo(raw.isEmpty() ? csv : raw).fileName())
                               : QStringLiteral("未记录"));
}

void MainWindow::stopRecording()
{
    m_recorder->stop();
    m_recordingActive = false;
    m_statusRecord->setText(QStringLiteral("已停止记录"));
}

void MainWindow::onRecordToggled(bool on)
{
    if (m_suppressRecordToggle)
        return;
    if (on)
        startRecording(QString(), QString(), true);
    else if (m_recordingActive)
        stopRecording();
}

void MainWindow::switchToReplay(const QString &path, double fps)
{
    if (!m_replay)
        return;
    QMetaObject::invokeMethod(m_replay, "close", Qt::BlockingQueuedConnection);
    m_replay->setSourcePath(path);
    m_replay->setFramesPerSecond(fps);
    m_active = m_replay;
    m_openAct->setText(QStringLiteral("打开串口"));
    QMetaObject::invokeMethod(m_replay, "open", Qt::QueuedConnection);
    statusBar()->showMessage(QStringLiteral("回放模式：%1 @ %2 帧/s").arg(path).arg(fps), 5000);
}

void MainWindow::onReplayClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择留档的原始流 (*.bin)"),
                                                      AppConfig::lastReportDir(),
                                                      QStringLiteral("原始流 (*.bin);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    AppConfig::setLastReportDir(QFileInfo(path).absolutePath());
    switchToReplay(path, 250.0);
}

void MainWindow::onFrameFromTransport(const smd::DecodedFrame &frame)
{
    m_hub->append(frame);            // 数据层
    m_recorder->onFrame(frame);      // 留档（同一线程，无需加锁）
}

void MainWindow::onTransportOpened()
{
    m_openAct->setText(QStringLiteral("关闭串口"));
    const QString desc = m_active ? m_active->describe() : QString();
    m_statusPort->setText(QStringLiteral("已连接: %1").arg(desc));
    if (m_connLabel)
        m_connLabel->setText(QStringLiteral("已连接: %1").arg(desc));
    if (m_pendingAuth) {
        // 打开/重连成功后自动补发认证帧，恢复上位机侧会话
        QTimer::singleShot(200, this, [this] { sendCommand(FrameBuilder::auth()); });
    }
}

void MainWindow::onTransportClosed()
{
    m_openAct->setText(QStringLiteral("打开串口"));
    m_statusPort->setText(QStringLiteral("已断开"));
    if (m_connLabel)
        m_connLabel->setText(QStringLiteral("已断开"));
}

void MainWindow::onTransportError(const QString &msg)
{
    statusBar()->showMessage(msg, 6000);
}

void MainWindow::onCounters(quint64 framesData, quint64 dropped, quint64 bytesIn)
{
    m_decoderDrop = dropped;
    m_bytesIn = bytesIn;
    Q_UNUSED(framesData)
}

void MainWindow::onSerialSeen(const QString &serial)
{
    m_serialHex = serial;
    m_statusSerial->setText(QStringLiteral("串号: %1").arg(serial));
    if (m_serialLabel)
        m_serialLabel->setText(QStringLiteral("串号: %1").arg(serial));
    statusBar()->showMessage(QStringLiteral("认证应答收到，驱动器序列号 %1（本版本不做联网授权校验）")
                                 .arg(serial), 6000);
}

void MainWindow::onTick()
{
    // 60 Hz 统一刷新：表格 / 波形 / 状态栏（与接收解耦，见计划书 §4.2）
    float floats[DataHub::kFloatData];
    quint32 ints[DataHub::kIntData];
    for (int i = 0; i < DataHub::kFloatData; ++i)
        floats[i] = m_hub->floatData(i);
    for (int i = 0; i < DataHub::kIntData; ++i)
        ints[i] = m_hub->intData(i);
    m_table->setValues(floats, DataHub::kFloatData, ints, DataHub::kIntData);

    m_wave->markDataAppended();

    const quint64 frames = m_hub->framesData();
    const quint64 now = quint64(QDateTime::currentMSecsSinceEpoch());
    if (m_lastTickMs != 0 && now > m_lastTickMs) {
        const double dt = double(now - m_lastTickMs) / 1000.0;
        const double inst = double(frames - m_lastFrames) / dt;
        m_rateFps = (m_rateFps == 0.0) ? inst : (m_rateFps * 0.8 + inst * 0.2);   // 平滑
    }
    m_lastTickMs = now;
    m_lastFrames = frames;

    m_statusFrames->setText(QStringLiteral("帧: %1").arg(frames));
    m_statusDrop->setText(QStringLiteral("丢帧: %1").arg(m_decoderDrop));
    m_statusRate->setText(QStringLiteral("%1 帧/s · %2 KB/s")
                              .arg(m_rateFps, 0, 'f', 1)
                              .arg(double(m_bytesIn) / 1024.0, 0, 'f', 1));
    if (m_recorder->isActive()) {
        m_statusRecord->setText(QStringLiteral("记录中: %1 行 / %2 MB")
                                    .arg(m_recorder->rowsWritten())
                                    .arg(double(m_recorder->rawBytesWritten()) / 1048576.0, 0, 'f', 1));
    }
}

} // namespace smd
