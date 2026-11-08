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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace smd {

MainWindow::MainWindow(const Options &opt, QWidget *parent)
    : QMainWindow(parent)
    , m_opt(opt)
{
    m_hub = new DataHub(this);
    m_recorder = new Recorder(this);

    buildUi();
    buildToolbar();

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

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("SMotorDrive 上位机（Qt 版）"));
    resize(1280, 840);

    m_wave = new WaveformView(this);
    m_wave->setDataHub(m_hub);

    m_table = new TablePanel(this);
    m_params = new ParamPanel(this);
    connect(m_params, &ParamPanel::requestParam, this, [this](int idx, float v) {
        sendCommand(FrameBuilder::param(idx, v));
    });
    connect(m_params, &ParamPanel::requestAllParams, this, [this](const QVector<double> &values) {
        for (int i = 0; i < values.size(); ++i)
            sendCommand(FrameBuilder::param(i, float(values[i])));
    });

    // 通道页：显示勾选 + 偏移（只影响绘图，不发帧）
    auto *chanBox = new QGroupBox(QStringLiteral("波形通道 / 绘图偏移（上下移只改本地显示）"), this);
    auto *chanLayout = new QVBoxLayout(chanBox);
    const auto &meta = channelTable();
    for (int c = 0; c < int(meta.size()); ++c) {
        auto *row = new QWidget(chanBox);
        auto *hl = new QHBoxLayout(row);
        auto *cb = new QCheckBox(QStringLiteral("波形%1  %2  —  %3")
                                     .arg(c + 1).arg(meta[size_t(c)].name, meta[size_t(c)].desc), row);
        cb->setChecked(meta[size_t(c)].visible);
        m_wave->setChannelVisible(c, meta[size_t(c)].visible);
        hl->addWidget(cb);

        auto *offset = new QDoubleSpinBox(row);
        offset->setRange(-1e9, 1e9);
        offset->setDecimals(3);
        offset->setSingleStep(1.0);
        offset->setPrefix(QStringLiteral("偏移 "));
        hl->addWidget(offset);
        hl->addStretch(1);

        connect(cb, &QCheckBox::toggled, this, [this, c](bool on) { m_wave->setChannelVisible(c, on); });
        connect(offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, c](double v) { m_wave->setOffset(c, v); });
        chanLayout->addWidget(row);
    }
    auto *resetBtn = new QPushButton(QStringLiteral("复位所有偏移 / 回到实时"), chanBox);
    connect(resetBtn, &QPushButton::clicked, this, [this] { m_wave->resetView(); });
    chanLayout->addWidget(resetBtn);
    chanLayout->addStretch(1);

    auto *chanScroll = new QScrollArea(this);
    chanScroll->setWidget(chanBox);
    chanScroll->setWidgetResizable(true);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(m_table, QStringLiteral("数据表"));
    m_tabs->addTab(m_params, QStringLiteral("参数"));
    m_tabs->addTab(chanScroll, QStringLiteral("通道/显示"));

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_wave);
    splitter->addWidget(m_tabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    setCentralWidget(splitter);

    m_statusPort = new QLabel(QStringLiteral("未连接"), this);
    m_statusFrames = new QLabel(QStringLiteral("帧: 0"), this);
    m_statusDrop = new QLabel(QStringLiteral("丢帧: 0"), this);
    m_statusRate = new QLabel(QStringLiteral("0.0 帧/s"), this);
    m_statusSerial = new QLabel(QStringLiteral("串号: -"), this);
    m_statusRecord = new QLabel(QStringLiteral("未记录"), this);
    statusBar()->addWidget(m_statusPort);
    statusBar()->addPermanentWidget(m_statusFrames);
    statusBar()->addPermanentWidget(m_statusDrop);
    statusBar()->addPermanentWidget(m_statusRate);
    statusBar()->addPermanentWidget(m_statusSerial);
    statusBar()->addPermanentWidget(m_statusRecord);
}

void MainWindow::buildToolbar()
{
    auto *tb = addToolBar(QStringLiteral("主工具栏"));
    tb->setMovable(false);

    m_portBox = new QComboBox(this);
    m_portBox->setMinimumWidth(220);
    tb->addWidget(new QLabel(QStringLiteral(" 串口 "), this));
    tb->addWidget(m_portBox);

    QAction *refresh = tb->addAction(QStringLiteral("刷新"));
    connect(refresh, &QAction::triggered, this, &MainWindow::onRefreshPorts);

    m_openAct = tb->addAction(QStringLiteral("打开串口"));
    connect(m_openAct, &QAction::triggered, this, &MainWindow::onTogglePort);

    tb->addSeparator();
    m_authAct = tb->addAction(QStringLiteral("认证"));
    connect(m_authAct, &QAction::triggered, this, &MainWindow::onAuthClicked);

    m_uploadAct = tb->addAction(QStringLiteral("开始上传"));
    connect(m_uploadAct, &QAction::triggered, this, &MainWindow::onStartUploadClicked);

    m_startAct = tb->addAction(QStringLiteral("启动"));
    connect(m_startAct, &QAction::triggered, this, &MainWindow::onStartStopClicked);

    m_accelAct = tb->addAction(QStringLiteral("加速"));
    m_decelAct = tb->addAction(QStringLiteral("减速"));
    connect(m_accelAct, &QAction::triggered, this, &MainWindow::onAccelClicked);
    connect(m_decelAct, &QAction::triggered, this, &MainWindow::onDecelClicked);

    tb->addSeparator();
    tb->addWidget(new QLabel(QStringLiteral(" 采样率(Hz) "), this));
    m_rateBox = new QSpinBox(this);
    m_rateBox->setRange(100, 1000000);
    m_rateBox->setSingleStep(1000);
    m_rateBox->setValue(AppConfig::sampleRateHz());
    tb->addWidget(m_rateBox);

    tb->addWidget(new QLabel(QStringLiteral(" 显示窗口(s) "), this));
    m_windowBox = new QDoubleSpinBox(this);
    m_windowBox->setRange(0.005, 60.0);
    m_windowBox->setDecimals(3);
    m_windowBox->setSingleStep(0.05);
    m_windowBox->setValue(AppConfig::windowSeconds());
    tb->addWidget(m_windowBox);

    tb->addSeparator();
    m_recordAct = tb->addAction(QStringLiteral("开始记录"));
    m_recordAct->setCheckable(true);
    connect(m_recordAct, &QAction::toggled, this, &MainWindow::onRecordToggled);

    m_replayAct = tb->addAction(QStringLiteral("回放…"));
    connect(m_replayAct, &QAction::triggered, this, &MainWindow::onReplayClicked);

    m_pauseAct = tb->addAction(QStringLiteral("暂停显示"));
    m_pauseAct->setCheckable(true);
    connect(m_pauseAct, &QAction::toggled, this, [this](bool on) { m_wave->setPaused(on); });
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
    m_statusPort->setText(QStringLiteral("已连接: %1").arg(m_active ? m_active->describe() : QString()));
    if (m_pendingAuth) {
        // 打开/重连成功后自动补发认证帧，恢复上位机侧会话
        QTimer::singleShot(200, this, [this] { sendCommand(FrameBuilder::auth()); });
    }
}

void MainWindow::onTransportClosed()
{
    m_openAct->setText(QStringLiteral("打开串口"));
    m_statusPort->setText(QStringLiteral("已断开"));
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
