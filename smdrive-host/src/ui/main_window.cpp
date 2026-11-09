#include "ui/main_window.h"

#include "app/app_config.h"
#include "app/channel_table.h"
#include "data/datahub.h"
#include "data/recorder.h"
#include "protocol/frame_builder.h"
#include "transport/replay_transport.h"
#include "transport/serial_transport.h"
#include "ui/param_panel.h"
#include "ui/serial_assistant.h"
#include "ui/table_panel.h"
#include "ui/waveform_view.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
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
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

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

    // 启动模式：--ui-mode serial 时直接进入串口助手（截图/自动化用）
    if (m_opt.uiMode.compare(QStringLiteral("serial"), Qt::CaseInsensitive) == 0) {
        m_modeSerialBtn->setChecked(true);
        onModeChanged(1);
    }

    m_hub->configure(m_opt.sampleRateHz, AppConfig::historySeconds());
    m_rateBox->setValue(m_opt.sampleRateHz);
    m_windowBox->setValue(m_opt.windowSeconds);
    m_wave->setWindowSeconds(m_opt.windowSeconds);

    // 启动参数里预设的通道显示增益（--ch-gain "6=0.002,7=0.001"）
    for (const auto &preset : m_opt.channelGains) {
        const int ch = preset.first;
        if (ch < 0 || ch >= m_gainSpins.size())
            continue;
        m_gainSpins[ch]->setValue(preset.second);   // 触发 valueChanged → m_wave->setGain
    }

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

    // ---- 模式切换：控制台 / 串口助手（右栏顶部，两种模式都常驻）----
    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel(QStringLiteral("模式"), panel));
    // 分段按钮：勾选态用高亮底色，两个按钮等宽，避免与主题冲突
    const QString segStyle = QStringLiteral(
        "QToolButton{border:1px solid #b0b0b0;padding:4px 10px;background:#f0f0f0;}"
        "QToolButton:checked{background:#3d7eff;color:white;border-color:#3d7eff;}");
    m_modeControlBtn = new QToolButton(panel);
    m_modeControlBtn->setText(QStringLiteral("控制台"));
    m_modeControlBtn->setCheckable(true);
    m_modeControlBtn->setChecked(true);
    m_modeControlBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_modeControlBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_modeControlBtn->setMinimumHeight(30);
    m_modeControlBtn->setStyleSheet(segStyle);
    m_modeControlBtn->setToolTip(QStringLiteral("波形 / 参数 / 数据表（协议控制台）"));
    m_modeSerialBtn = new QToolButton(panel);
    m_modeSerialBtn->setText(QStringLiteral("串口助手"));
    m_modeSerialBtn->setCheckable(true);
    m_modeSerialBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_modeSerialBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_modeSerialBtn->setMinimumHeight(30);
    m_modeSerialBtn->setStyleSheet(segStyle);
    m_modeSerialBtn->setToolTip(QStringLiteral("原始串口收发终端（类似 XCOM）"));
    modeRow->addWidget(m_modeControlBtn, 1);
    modeRow->addWidget(m_modeSerialBtn, 1);
    auto *modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_modeControlBtn, 0);
    modeGroup->addButton(m_modeSerialBtn, 1);
    connect(modeGroup, &QButtonGroup::idClicked, this, &MainWindow::onModeChanged);
    panelLayout->addLayout(modeRow);

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

    // ---- 右栏内容页 0：控制台（显示 / 通道 / 参数·数据表）----
    auto *controlPage = new QWidget(panel);
    auto *controlLayout = new QVBoxLayout(controlPage);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(10);

    // ---- 显示 ----
    QVBoxLayout *dispLayout = nullptr;
    auto *dispBox = makeGroup(QStringLiteral("显示"), controlPage, &dispLayout);
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
    // 拖动波形 / 拖动底部进度条会自动暂停：回同步「暂停显示」的勾选态
    connect(m_wave, &WaveformView::pausedChanged, this, [this](bool on) {
        if (m_pauseAct->isChecked() == on)
            return;
        m_pauseAct->blockSignals(true);
        m_pauseAct->setChecked(on);
        m_pauseAct->blockSignals(false);
    });
    m_cursorAct = new QAction(QStringLiteral("光标测量"), this);
    m_cursorAct->setCheckable(true);
    m_cursorAct->setStatusTip(QStringLiteral("X1/X2 竖线标时间、Y1/Y2 横线标数值；并在各「测量通道」的波形上"
                                             "取点读出 ΔV。打开后画面冻结，右上角出测量表格，拖动光标线移动"));
    connect(m_cursorAct, &QAction::toggled, this, [this](bool on) { m_wave->setCursorMeasure(on); });

    // 测量通道（多选）：勾中的通道会在 X1/X2 处读出波形值
    auto *measChanRow = new QHBoxLayout();
    measChanRow->addWidget(new QLabel(QStringLiteral("测量通道"), dispBox));
    auto *measChanBtn = new QToolButton(dispBox);
    measChanBtn->setPopupMode(QToolButton::InstantPopup);
    measChanBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    measChanBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *measMenu = new QMenu(measChanBtn);
    auto updateMeasLabel = [measChanBtn, measMenu]() {
        QStringList sel;
        int total = 0;
        for (QAction *a : measMenu->actions()) {
            ++total;
            if (a->isChecked())
                sel << QStringLiteral("CH%1").arg(a->data().toInt());
        }
        measChanBtn->setText(sel.isEmpty() ? QStringLiteral("无")
                             : (sel.size() == total) ? QStringLiteral("全部")
                                                     : sel.join(QLatin1Char(',')));
    };
    const auto &chanMeta = channelTable();
    for (int c = 0; c < int(chanMeta.size()); ++c) {
        QAction *a = measMenu->addAction(chanMeta[size_t(c)].name);
        a->setCheckable(true);
        a->setData(c);
        a->setChecked(m_wave->channelMeasured(c));
        connect(a, &QAction::toggled, this, [this, c, updateMeasLabel](bool on) {
            m_wave->setChannelMeasured(c, on);
            updateMeasLabel();
        });
    }
    measChanBtn->setMenu(measMenu);
    updateMeasLabel();
    measChanRow->addWidget(measChanBtn, 1);
    dispLayout->addLayout(measChanRow);

    auto *resetViewBtn = new QPushButton(QStringLiteral("复位视图/偏移"), dispBox);
    connect(resetViewBtn, &QPushButton::clicked, this, [this] { m_wave->resetView(); });
    auto *dispBtnRow = new QHBoxLayout();
    dispBtnRow->addWidget(makeActionButton(m_pauseAct, dispBox));
    dispBtnRow->addWidget(makeActionButton(m_cursorAct, dispBox));
    dispBtnRow->addWidget(resetViewBtn);
    dispLayout->addLayout(dispBtnRow);
    controlLayout->addWidget(dispBox);

    // ---- 运行控制（认证 / 上传 / 启停 / 加减速）----
    // 会话级低频操作，收进顶部「设备」菜单（见 buildMenus()），右栏不再占位。
    m_authAct = new QAction(QStringLiteral("认证"), this);
    m_authAct->setStatusTip(QStringLiteral("发送认证帧 55 55 01 AA AA，并读取驱动器序列号"));
    connect(m_authAct, &QAction::triggered, this, &MainWindow::onAuthClicked);

    m_uploadAct = new QAction(QStringLiteral("显示波形"), this);
    m_uploadAct->setStatusTip(QStringLiteral("发送 03 命令，请求驱动器开始上传采样数据并实时显示波形"));
    connect(m_uploadAct, &QAction::triggered, this, &MainWindow::onStartUploadClicked);

    m_startAct = new QAction(QStringLiteral("启动"), this);
    m_startAct->setStatusTip(QStringLiteral("07 / 0A：启动或停止电机运行"));
    connect(m_startAct, &QAction::triggered, this, &MainWindow::onStartStopClicked);

    m_accelAct = new QAction(QStringLiteral("加速"), this);
    m_accelAct->setStatusTip(QStringLiteral("08：加速"));
    m_decelAct = new QAction(QStringLiteral("减速"), this);
    m_decelAct->setStatusTip(QStringLiteral("09：减速"));
    connect(m_accelAct, &QAction::triggered, this, &MainWindow::onAccelClicked);
    connect(m_decelAct, &QAction::triggered, this, &MainWindow::onDecelClicked);

    // ---- 波形通道（勾选显示 + 绘图偏移，仿示波器通道旋钮；只改本地显示，不发帧）----
    QVBoxLayout *chanOuter = nullptr;
    auto *chanBox = makeGroup(QStringLiteral("波形通道（偏移只改本地绘图）"), controlPage, &chanOuter);
    auto *chanGrid = new QGridLayout();
    chanGrid->setHorizontalSpacing(4);
    const auto &meta = channelTable();
    const int rowsPerCol = (int(meta.size()) + 1) / 2;          // 10 路排成两列，省竖直空间
    for (int c = 0; c < int(meta.size()); ++c) {
        auto *cb = new QCheckBox(meta[size_t(c)].name, chanBox);
        cb->setChecked(meta[size_t(c)].visible);
        cb->setToolTip(meta[size_t(c)].desc);
        cb->setStyleSheet(QStringLiteral("QCheckBox{color:%1;}").arg(m_wave->channelColor(c).name()));
        m_wave->setChannelVisible(c, meta[size_t(c)].visible);
        m_chanChecks.append(cb);

        auto *gain = new QDoubleSpinBox(chanBox);
        gain->setRange(1e-4, 1e5);
        gain->setDecimals(4);
        gain->setValue(1.0);
        gain->setPrefix(QStringLiteral("×"));
        gain->setToolTip(QStringLiteral("显示增益（示波器 V/div 等价物；只影响绘图，不发帧）"));
        gain->setMinimumWidth(70);
        gain->setMaximumWidth(80);
        m_gainSpins.append(gain);

        auto *offset = new QDoubleSpinBox(chanBox);
        offset->setRange(-1e9, 1e9);
        offset->setDecimals(3);
        offset->setSingleStep(1.0);
        offset->setPrefix(QStringLiteral("Δ"));          // 窄前缀，避免把右栏挤爆
        offset->setToolTip(QStringLiteral("绘图偏移（只改本地显示，不发帧）"));
        offset->setMinimumWidth(64);
        offset->setMaximumWidth(76);

        // 配色 / 线型按钮：点开选颜色与线型（实线/虚线/点线/点划线/双点划线）与线宽
        auto *styleBtn = new QToolButton(chanBox);
        styleBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        styleBtn->setPopupMode(QToolButton::InstantPopup);
        styleBtn->setFixedSize(26, 22);
        styleBtn->setToolTip(QStringLiteral("点击设置该通道的颜色与线型"));
        auto *styleMenu = new QMenu(styleBtn);
        styleBtn->setMenu(styleMenu);
        m_chanStyleBtns.append(styleBtn);
        m_chanStyleMenus.append(styleMenu);
        connect(styleMenu, &QMenu::aboutToShow, this, [this, c, styleMenu] {
            rebuildChannelStyleMenu(c, styleMenu);       // 每次弹出按当前状态重建
        });

        connect(cb, &QCheckBox::toggled, this, [this, c](bool on) { m_wave->setChannelVisible(c, on); });
        connect(gain, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, c](double v) { m_wave->setGain(c, v); });
        connect(offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, c](double v) { m_wave->setOffset(c, v); });

        const int row = c % rowsPerCol;
        const int col = (c / rowsPerCol) * 4;
        chanGrid->addWidget(cb, row, col);
        chanGrid->addWidget(gain, row, col + 1);
        chanGrid->addWidget(offset, row, col + 2);
        chanGrid->addWidget(styleBtn, row, col + 3);
    }
    for (int c = 0; c < int(meta.size()); ++c)
        updateChannelStyleButton(c);                     // 初始化各按钮上的线样图标
    chanOuter->addLayout(chanGrid);
    controlLayout->addWidget(chanBox);

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
    connect(m_params, &ParamPanel::profileStatus, this, [this](const QString &text) {
        statusBar()->showMessage(text, 6000);
    });

    m_tabs = new QTabWidget(controlPage);
    m_tabs->addTab(m_params, QStringLiteral("参数"));
    m_tabs->addTab(m_table, QStringLiteral("数据表"));
    controlLayout->addWidget(m_tabs, 1);

    // ---- 记录 / 回放（文件类低频操作，收进「文件」菜单，见 buildMenus()）----
    m_recordAct = new QAction(QStringLiteral("开始记录"), this);
    m_recordAct->setCheckable(true);
    m_recordAct->setStatusTip(QStringLiteral("选择目录，同时记录 CSV 宽表与原始 .bin 流（可再次回放）"));
    connect(m_recordAct, &QAction::toggled, this, &MainWindow::onRecordToggled);

    m_replayAct = new QAction(QStringLiteral("回放原始流…"), this);
    m_replayAct->setStatusTip(QStringLiteral("喂留档的 .bin 原始字节流，无需硬件即可复现波形"));
    connect(m_replayAct, &QAction::triggered, this, &MainWindow::onReplayClicked);

    // ---- 右栏内容页 1：串口助手参数（terminalWidget 在下面放进左主区）----
    m_assistant = new SerialAssistant(this);
    m_rightStack = new QStackedWidget(panel);
    m_rightStack->addWidget(controlPage);                      // 0 控制台
    m_rightStack->addWidget(m_assistant->optionsWidget());     // 1 串口助手
    panelLayout->addWidget(m_rightStack, 1);

    auto *panelScroll = new QScrollArea(this);
    panelScroll->setWidget(panel);
    panelScroll->setWidgetResizable(true);
    panelScroll->setFrameShape(QFrame::NoFrame);
    panelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    panelScroll->setMinimumWidth(560);
    panelScroll->setMaximumWidth(900);

    // 左主区：0 波形 / 1 串口终端，随右栏模式切换
    m_mainStack = new QStackedWidget(this);
    m_mainStack->addWidget(m_wave);
    m_mainStack->addWidget(m_assistant->terminalWidget());

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_mainStack);
    splitter->addWidget(panelScroll);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({840, 580});
    splitter->setChildrenCollapsible(false);
    setCentralWidget(splitter);

    // ---------------- 底部读数条 ----------------
    m_statusPort = new QLabel(QStringLiteral("未连接"), this);
    m_statusFrames = new QLabel(QStringLiteral("帧: 0"), this);
    m_statusDrop = new QLabel(QStringLiteral("丢帧: 0"), this);
    m_statusRate = new QLabel(QStringLiteral("0.0 帧/s"), this);
    m_statusSerial = new QLabel(QStringLiteral("串号: -"), this);
    m_statusRecord = new QLabel(QStringLiteral("未记录"), this);
    // 读数每帧都在变；字段宽度若随内容变化，整条会左右抖动。按最宽样例给每个字段
    // 设最小宽度：内容不超样例时宽度恒定（不抖动），万一超出也只是变宽而不裁切。
    auto fixField = [](QLabel *l, const QString &widest) {
        l->setMinimumWidth(l->fontMetrics().horizontalAdvance(widest) + 12);
    };
    fixField(m_statusFrames, QStringLiteral("帧: 99999999"));
    fixField(m_statusDrop, QStringLiteral("丢帧: 9999999"));
    fixField(m_statusRate, QStringLiteral("9999.9 帧/s · 9999.9 KB/s"));
    fixField(m_statusSerial, QStringLiteral("串号: 112233445566778899AABBCC"));
    fixField(m_statusRecord, QStringLiteral("记录中: 9999999 行 / 9999.9 MB"));
    m_statusPort->setMinimumWidth(
        m_statusPort->fontMetrics().horizontalAdvance(QStringLiteral("已连接: COM255")) + 10);
    // 全部放右侧（permanent）：左侧留给 showMessage 的临时提示，避免文字互相压盖
    statusBar()->addPermanentWidget(m_statusPort);
    statusBar()->addPermanentWidget(m_statusFrames);
    statusBar()->addPermanentWidget(m_statusDrop);
    statusBar()->addPermanentWidget(m_statusRate);
    statusBar()->addPermanentWidget(m_statusSerial);
    statusBar()->addPermanentWidget(m_statusRecord);

    buildMenus();
}

// ---------------------------------------------------------------------------
// 顶部：菜单栏 + 运行工具栏。低频文件操作留在「文件」菜单，运行操作上工具栏。
//   文件：回放 / 记录 / 参数方案 / 退出
//   工具栏：认证 / 显示波形 / 启动(停止) / 加速 / 减速
//   帮助：关于
// 这些 QAction 只在工具栏出现（不再进菜单），状态由 QAction 自身维护。
// ---------------------------------------------------------------------------
void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    m_replayAct->setShortcut(QKeySequence::Open);                 // Ctrl+O
    fileMenu->addAction(m_replayAct);
    fileMenu->addSeparator();
    m_recordAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    fileMenu->addAction(m_recordAct);
    fileMenu->addSeparator();

    m_loadProfileAct = new QAction(QStringLiteral("加载参数方案…"), this);
    m_loadProfileAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    m_loadProfileAct->setStatusTip(QStringLiteral("从 JSON 读取 10 路参数（仅填入界面，确认后再下发）"));
    connect(m_loadProfileAct, &QAction::triggered, this, [this] {
        m_tabs->setCurrentWidget(m_params);      // 切到「参数」页，让用户看到回填结果
        m_params->promptLoadProfile();
    });
    fileMenu->addAction(m_loadProfileAct);

    m_saveProfileAct = new QAction(QStringLiteral("保存参数方案…"), this);
    m_saveProfileAct->setShortcut(QKeySequence::Save);            // Ctrl+S
    m_saveProfileAct->setStatusTip(QStringLiteral("把 10 路参数连同 idx/名称存成 JSON 留档"));
    connect(m_saveProfileAct, &QAction::triggered, this, [this] {
        m_tabs->setCurrentWidget(m_params);
        m_params->promptSaveProfile();
    });
    fileMenu->addAction(m_saveProfileAct);

    fileMenu->addSeparator();
    m_quitAct = new QAction(QStringLiteral("退出"), this);
    // 用显式 Ctrl+Q：Windows 上 QKeySequence::Quit 不绑按键（那是 macOS/X11 的约定）
    m_quitAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(m_quitAct);

    // 运行操作：顶部工具栏（是这些操作的唯一入口；快捷键仍随 QAction 生效）
    m_authAct->setShortcut(QKeySequence(QStringLiteral("F5")));
    m_uploadAct->setShortcut(QKeySequence(QStringLiteral("F6")));
    m_startAct->setShortcut(QKeySequence(QStringLiteral("F7")));
    m_accelAct->setShortcut(QKeySequence(QStringLiteral("F8")));
    m_decelAct->setShortcut(QKeySequence(QStringLiteral("F9")));
    auto *runBar = addToolBar(QStringLiteral("运行"));
    runBar->setObjectName(QStringLiteral("runToolBar"));
    runBar->setMovable(false);
    runBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    runBar->addAction(m_authAct);
    runBar->addSeparator();
    runBar->addAction(m_uploadAct);
    runBar->addSeparator();
    runBar->addAction(m_startAct);
    runBar->addAction(m_accelAct);
    runBar->addAction(m_decelAct);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    m_aboutAct = new QAction(QStringLiteral("关于 SMotorDrive 上位机"), this);
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::onAbout);
    helpMenu->addAction(m_aboutAct);
}

// ---------------------------------------------------------------------------
// 通道样式（配色 / 线型 / 线宽）：只改本地绘图，不发帧。菜单每次弹出按当前状态重建。
// ---------------------------------------------------------------------------
void MainWindow::updateChannelStyleButton(int c)
{
    if (c < 0 || c >= m_chanStyleBtns.size())
        return;
    const int w = 34, h = 14;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(m_wave->channelColor(c));
    pen.setStyle(m_wave->channelStyle(c));
    pen.setWidthF(std::max(1.6, m_wave->channelWidth(c)));
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(2, h / 2.0), QPointF(w - 2, h / 2.0));
    p.end();
    m_chanStyleBtns[c]->setIcon(QIcon(pm));
    m_chanStyleBtns[c]->setIconSize(QSize(w, h));
}

void MainWindow::applyChannelColor(int c, const QColor &col)
{
    m_wave->setChannelColor(c, col);
    if (c < m_chanChecks.size() && m_chanChecks[c])
        m_chanChecks[c]->setStyleSheet(QStringLiteral("QCheckBox{color:%1;}").arg(col.name()));
    updateChannelStyleButton(c);
}

void MainWindow::rebuildChannelStyleMenu(int c, QMenu *menu)
{
    menu->clear();

    // ---- 颜色网格 ----
    static const QColor kPalette[] = {
        QColor(0xE6, 0x39, 0x46), QColor(0x2A, 0x9D, 0x8F), QColor(0x1D, 0x35, 0x57), QColor(0xFF, 0x9F, 0x1C),
        QColor(0x45, 0x7B, 0x9D), QColor(0x8E, 0x7C, 0xC3), QColor(0xF9, 0xC7, 0x4F), QColor(0x6A, 0x99, 0x4D),
        QColor(0xA1, 0x6E, 0x3C), QColor(0xF7, 0x8C, 0x6B), QColor(0xFF, 0xFF, 0xFF), QColor(0x00, 0xE5, 0xFF),
        QColor(0xFF, 0x2D, 0x95), QColor(0xB2, 0xFF, 0x59), QColor(0xC7, 0x7D, 0xFF), QColor(0x99, 0xA2, 0xAB),
    };
    const int nColor = int(sizeof(kPalette) / sizeof(kPalette[0]));
    const int cols = 8;
    const QColor cur = m_wave->channelColor(c);

    auto *grid = new QWidget(menu);
    auto *gl = new QGridLayout(grid);
    gl->setContentsMargins(6, 4, 6, 4);
    gl->setSpacing(3);
    for (int i = 0; i < nColor; ++i) {
        const QColor col = kPalette[i];
        auto *b = new QToolButton(grid);
        b->setFixedSize(18, 18);
        b->setToolTip(col.name());
        QString ss = QStringLiteral("QToolButton{background:%1;border:1px solid #555;}"
                                    "QToolButton:hover{border:2px solid #fff;}").arg(col.name());
        if (col == cur)
            ss += QStringLiteral("QToolButton{border:2px solid #3d7eff;}");
        b->setStyleSheet(ss);
        connect(b, &QToolButton::clicked, this, [this, c, col, menu] {
            applyChannelColor(c, col);
            menu->close();
        });
        gl->addWidget(b, i / cols, i % cols);
    }
    auto *custom = new QToolButton(grid);
    custom->setText(QStringLiteral("自定义…"));
    custom->setToolButtonStyle(Qt::ToolButtonTextOnly);
    custom->setFixedHeight(20);
    connect(custom, &QToolButton::clicked, this, [this, c, menu] {
        menu->close();                                   // 先收起菜单再弹取色器
        const QColor col = QColorDialog::getColor(m_wave->channelColor(c), this,
                                                  QStringLiteral("选择通道 CH%1 的颜色").arg(c));
        if (col.isValid())
            applyChannelColor(c, col);
    });
    gl->addWidget(custom, (nColor - 1) / cols + 1, 0, 1, cols);
    auto *gridAct = new QWidgetAction(menu);
    gridAct->setDefaultWidget(grid);
    menu->addAction(gridAct);

    menu->addSeparator();

    // ---- 线型 ----
    auto *styleGroup = new QActionGroup(menu);
    styleGroup->setExclusive(true);
    const struct { const char *name; Qt::PenStyle s; } kStyles[] = {
        {"实线", Qt::SolidLine},   {"虚线", Qt::DashLine},        {"点线", Qt::DotLine},
        {"点划线", Qt::DashDotLine}, {"双点划线", Qt::DashDotDotLine},
    };
    for (const auto &si : kStyles) {
        QAction *a = menu->addAction(QString::fromUtf8(si.name));
        a->setCheckable(true);
        a->setChecked(m_wave->channelStyle(c) == si.s);
        styleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, c, s = si.s] {
            m_wave->setChannelStyle(c, s);
            updateChannelStyleButton(c);
        });
    }

    menu->addSeparator();

    // ---- 线宽 ----
    auto *widthGroup = new QActionGroup(menu);
    widthGroup->setExclusive(true);
    const struct { const char *name; double w; } kWidths[] = {
        {"细", 1.0}, {"中", 1.8}, {"粗", 2.8},
    };
    for (const auto &wi : kWidths) {
        QAction *a = menu->addAction(QString::fromUtf8(wi.name));
        a->setCheckable(true);
        a->setChecked(qFuzzyCompare(m_wave->channelWidth(c), wi.w));
        widthGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, c, w = wi.w] {
            m_wave->setChannelWidth(c, w);
            updateChannelStyleButton(c);
        });
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(
        this, QStringLiteral("关于 SMotorDrive 上位机"),
        QStringLiteral(
            "<b>SMotorDrive 上位机（Qt 版）</b> %1<br>"
            "目标平台：Windows x64 · Qt %2 · MSVC2019<br><br>"
            "替代原厂上位机的调试/标定工具，按与其驱动器固件配套的串口协议实现，"
            "不含联网授权与 MySQL 依赖。<br><br>"
            "<b>下行</b>：<code>55 55 CMD [payload] AA AA</code><br>"
            "01 认证 · 03 显示波形(协议:开始上传) · 06 参数 · 07 启动 · 08 加速 · 09 减速 · 0A 停止<br>"
            "<b>上行</b>：<code>55 55 02 + 序列号</code> · <code>55 55 04 + 1710B 数据</code>")
            .arg(QApplication::applicationVersion(), QStringLiteral(QT_VERSION_STR)));
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

    // 串口调试助手：原始字节双向中转（协议解码照旧并行，互不影响）
    connect(m_serial, &ITransport::bytesReceived, this,
            [this](const QByteArray &chunk) { m_assistant->appendRx(chunk); });
    connect(m_assistant, &SerialAssistant::requestSend, this, [this](const QByteArray &bytes) {
        if (!m_serial || !m_serial->isOpen() || m_active != m_serial) {
            statusBar()->showMessage(QStringLiteral("未打开串口：内容未发送"), 3000);
            return;
        }
        QMetaObject::invokeMethod(m_serial, "sendBytes", Qt::QueuedConnection, Q_ARG(QByteArray, bytes));
        m_assistant->appendTx(bytes);
    });
    connect(m_assistant, &SerialAssistant::requestBaudRate, this, [this](int baud) {
        if (baud <= 0)
            return;
        AppConfig::setBaudRate(baud);
        if (!m_serial)
            return;
        const bool wasOpen = m_serial->isOpen();
        m_serial->setBaudRate(baud);
        if (wasOpen) {   // 端口已开：重开一次让新波特率生效
            QMetaObject::invokeMethod(m_serial, "close", Qt::BlockingQueuedConnection);
            m_pendingAuth = !serialModeActive();
            QMetaObject::invokeMethod(m_serial, "open", Qt::QueuedConnection);
        }
        statusBar()->showMessage(QStringLiteral("波特率已设为 %1").arg(baud), 3000);
    });
    // 助手工具条上的「打开串口 / 断开串口」复用主窗口同一套连接逻辑
    connect(m_assistant, &SerialAssistant::requestToggleConnection, this,
            [this] { onTogglePort(); });
    connect(m_assistant, &SerialAssistant::notice, this,
            [this](const QString &text) { statusBar()->showMessage(text, 4000); });
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

bool MainWindow::serialModeActive() const
{
    return m_modeSerialBtn && m_modeSerialBtn->isChecked();
}

// 控制台 <-> 串口助手：左主区与右栏同时切换。
// 切回控制台且串口已开时，补发一次认证帧，恢复上位机侧会话。
void MainWindow::onModeChanged(int id)
{
    if (m_mainStack)
        m_mainStack->setCurrentIndex(id);
    if (m_rightStack)
        m_rightStack->setCurrentIndex(id);

    if (id == 0 && m_serial && m_serial->isOpen() && m_pendingAuth) {
        QTimer::singleShot(50, this, [this] { sendCommand(FrameBuilder::auth()); });
    }
    statusBar()->showMessage(id == 1 ? QStringLiteral("已切到串口调试助手")
                                     : QStringLiteral("已切到控制台"), 3000);
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
    if (m_assistant)
        m_assistant->setConnected(m_serial && m_active == m_serial && m_serial->isOpen());
    // 串口助手模式下不自动补发认证帧，避免对非本驱动器设备发出意外字节
    if (m_pendingAuth && !serialModeActive()) {
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
    if (m_assistant)
        m_assistant->setConnected(false);
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
