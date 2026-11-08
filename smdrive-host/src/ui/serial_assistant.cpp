#include "ui/serial_assistant.h"

#include "app/app_config.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTime>
#include <QVBoxLayout>

namespace smd {

namespace {

constexpr int kMaxLogBlocks = 5000;      // 日志最多保留的行数（超出丢最旧）
constexpr int kInlineWrapChars = 4096;   // 非时间戳模式下，单行超过这么多字符就强制换行

void setMonospace(QWidget *w)
{
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (f.pointSizeF() > 0.0)
        f.setPointSizeF(f.pointSizeF() + 0.5);
    w->setFont(f);
}

} // namespace

SerialAssistant::SerialAssistant(QWidget *parent)
    : QWidget(parent)
{
    m_terminalPanel = new QWidget(this);
    m_optionsPanel = new QWidget(this);
    buildTerminal();
    buildOptions();
}

SerialAssistant::~SerialAssistant()
{
    saveHistory();
}

// ---------------------------------------------------------------------------
// 左主区：接收日志 + 发送行
// ---------------------------------------------------------------------------
void SerialAssistant::buildTerminal()
{
    auto *layout = new QVBoxLayout(m_terminalPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *header = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("串口调试助手"), m_terminalPanel);
    m_statusDot = new QLabel(QStringLiteral("\u25CF"), m_terminalPanel);   // ●
    {
        QFont dotFont = m_statusDot->font();
        dotFont.setPointSizeF(dotFont.pointSizeF() + 4.0);
        m_statusDot->setFont(dotFont);
    }
    m_statusText = new QLabel(m_terminalPanel);
    m_countLabel = new QLabel(QStringLiteral("收 0 B · 发 0 B"), m_terminalPanel);
    m_connBtn = new QPushButton(QStringLiteral("打开串口"), m_terminalPanel);
    m_connBtn->setToolTip(QStringLiteral("打开 / 断开当前串口连接"));
    connect(m_connBtn, &QPushButton::clicked, this, &SerialAssistant::requestToggleConnection);
    auto *clearRxBtn = new QPushButton(QStringLiteral("清除接收"), m_terminalPanel);
    clearRxBtn->setToolTip(QStringLiteral("清空接收区显示并复位累计收字节数（发送回显也会一并清掉）"));
    connect(clearRxBtn, &QPushButton::clicked, this, &SerialAssistant::clearRx);
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), m_terminalPanel);
    clearBtn->setToolTip(QStringLiteral("清空日志与收发计数"));
    connect(clearBtn, &QPushButton::clicked, this, &SerialAssistant::clearAll);
    header->addWidget(title);
    header->addSpacing(12);
    header->addWidget(m_statusDot);
    header->addWidget(m_statusText);
    header->addStretch(1);
    header->addWidget(m_countLabel);
    header->addWidget(m_connBtn);
    header->addWidget(clearRxBtn);
    header->addWidget(clearBtn);
    layout->addLayout(header);

    m_log = new QPlainTextEdit(m_terminalPanel);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(kMaxLogBlocks);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);   // XCOM 风格：不折行、横向滚动
    m_log->setPlaceholderText(QStringLiteral("接收区：按显示格式渲染，二进制建议切到 HEX"));
    setMonospace(m_log);
    layout->addWidget(m_log, 1);

    auto *sendRow = new QHBoxLayout();
    m_sendBox = new QComboBox(m_terminalPanel);
    m_sendBox->setEditable(true);
    m_sendBox->setInsertPolicy(QComboBox::NoInsert);   // 历史手动管理
    for (const QString &h : AppConfig::sendHistory())
        m_sendBox->addItem(h);
    m_sendBox->lineEdit()->setPlaceholderText(
        QStringLiteral("输入发送内容（文本直接输入；HEX 如 \"55 55 01\"）。回车发送，↑/↓ 翻历史"));
    m_sendBox->lineEdit()->installEventFilter(this);
    setMonospace(m_sendBox);
    connect(m_sendBox->lineEdit(), &QLineEdit::returnPressed, this, &SerialAssistant::onSendClicked);
    sendRow->addWidget(m_sendBox, 1);

    m_sendBtn = new QPushButton(QStringLiteral("发送"), m_terminalPanel);
    m_sendBtn->setMinimumWidth(72);
    connect(m_sendBtn, &QPushButton::clicked, this, &SerialAssistant::onSendClicked);
    sendRow->addWidget(m_sendBtn);
    layout->addLayout(sendRow);
}

// ---------------------------------------------------------------------------
// 右栏：串口参数 + 显示/收发开关
// ---------------------------------------------------------------------------
void SerialAssistant::buildOptions()
{
    auto *layout = new QVBoxLayout(m_optionsPanel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto *paramBox = new QGroupBox(QStringLiteral("串口参数"), m_optionsPanel);
    auto *paramLayout = new QVBoxLayout(paramBox);
    paramLayout->setSpacing(6);

    auto *baudRow = new QHBoxLayout();
    baudRow->addWidget(new QLabel(QStringLiteral("波特率"), paramBox));
    m_baudBox = new QComboBox(paramBox);
    m_baudBox->setEditable(true);
    for (int b : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        m_baudBox->addItem(QString::number(b));
    m_baudBox->setCurrentText(QString::number(AppConfig::baudRate()));
    connect(m_baudBox, &QComboBox::currentTextChanged, this, [this](const QString &t) {
        const int b = t.trimmed().toInt();
        if (b > 0)
            emit requestBaudRate(b);
    });
    baudRow->addWidget(m_baudBox, 1);
    paramLayout->addLayout(baudRow);

    auto *endingRow = new QHBoxLayout();
    endingRow->addWidget(new QLabel(QStringLiteral("发送换行"), paramBox));
    m_endingBox = new QComboBox(paramBox);
    m_endingBox->addItem(QStringLiteral("无"), int(SerialLineEnding::None));
    m_endingBox->addItem(QStringLiteral("\\n (LF)"), int(SerialLineEnding::Lf));
    m_endingBox->addItem(QStringLiteral("\\r\\n (CRLF)"), int(SerialLineEnding::CrLf));
    m_endingBox->addItem(QStringLiteral("\\r (CR)"), int(SerialLineEnding::Cr));
    endingRow->addWidget(m_endingBox, 1);
    paramLayout->addLayout(endingRow);

    auto *fmtRow = new QHBoxLayout();
    fmtRow->addWidget(new QLabel(QStringLiteral("发送格式"), paramBox));
    m_sendFormatBox = new QComboBox(paramBox);
    m_sendFormatBox->addItem(QStringLiteral("文本"));
    m_sendFormatBox->addItem(QStringLiteral("HEX"));
    fmtRow->addWidget(m_sendFormatBox, 1);
    fmtRow->addWidget(new QLabel(QStringLiteral("显示格式"), paramBox));
    m_displayFormatBox = new QComboBox(paramBox);
    m_displayFormatBox->addItem(QStringLiteral("文本"));
    m_displayFormatBox->addItem(QStringLiteral("HEX"));
    fmtRow->addWidget(m_displayFormatBox, 1);
    paramLayout->addLayout(fmtRow);
    layout->addWidget(paramBox);

    auto *dispBox = new QGroupBox(QStringLiteral("显示 / 收发"), m_optionsPanel);
    auto *dispLayout = new QVBoxLayout(dispBox);
    dispLayout->setSpacing(6);
    m_showTxBox = new QCheckBox(QStringLiteral("显示发送（本地回显）"), dispBox);
    m_showTxBox->setChecked(true);
    m_timestampBox = new QCheckBox(QStringLiteral("加时间戳（每块一行）"), dispBox);
    m_autoScrollBox = new QCheckBox(QStringLiteral("自动滚动"), dispBox);
    m_autoScrollBox->setChecked(true);
    m_pauseBox = new QCheckBox(QStringLiteral("暂停显示（仍计入统计）"), dispBox);
    dispLayout->addWidget(m_showTxBox);
    dispLayout->addWidget(m_timestampBox);
    dispLayout->addWidget(m_autoScrollBox);
    dispLayout->addWidget(m_pauseBox);
    layout->addWidget(dispBox);
    layout->addStretch(1);

    setConnected(false);
}

// ---------------------------------------------------------------------------
// 收发
// ---------------------------------------------------------------------------
void SerialAssistant::appendRx(const QByteArray &chunk)
{
    if (chunk.isEmpty())
        return;
    m_rxBytes += quint64(chunk.size());
    bumpCounters();
    if (m_pauseBox && m_pauseBox->isChecked())
        return;
    appendDisplay(renderSerialBytes(chunk, hexDisplay()), false);
}

void SerialAssistant::appendTx(const QByteArray &bytes)
{
    if (bytes.isEmpty())
        return;
    m_txBytes += quint64(bytes.size());
    bumpCounters();
    if (m_showTxBox && !m_showTxBox->isChecked())
        return;
    if (m_pauseBox && m_pauseBox->isChecked())
        return;
    appendDisplay(renderSerialBytes(bytes, hexDisplay()), true);
}

void SerialAssistant::appendDisplay(const QString &text, bool isTx)
{
    if (!m_log || text.isEmpty())
        return;

    const bool tsOn = m_timestampBox && m_timestampBox->isChecked();
    QString prefix;
    if (tsOn)
        prefix = QStringLiteral("[%1] ").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    if (isTx)
        prefix += QStringLiteral("→ ");

    QTextCursor cur = m_log->textCursor();
    cur.movePosition(QTextCursor::End);

    const bool lastBlockEmpty = cur.block().text().isEmpty();
    const bool needBreak = !lastBlockEmpty
        && (tsOn || isTx || cur.block().length() > kInlineWrapChars);
    if (needBreak)
        cur.insertText(QStringLiteral("\n"));
    cur.insertText(prefix + text);

    m_log->setTextCursor(cur);
    if (!m_autoScrollBox || m_autoScrollBox->isChecked())
        m_log->ensureCursorVisible();
}

void SerialAssistant::onSendClicked()
{
    const QString text = m_sendBox->currentText();
    const bool hex = hexSend();

    bool ok = false;
    QString err;
    const QByteArray bytes = encodeSerialSend(text, hex, lineEnding(), &ok, &err);
    if (!ok) {
        emit notice(QStringLiteral("发送内容无效：%1").arg(err));
        return;
    }
    if (bytes.isEmpty()) {
        emit notice(QStringLiteral("没有可发送的内容"));
        return;
    }
    if (!m_connected) {
        emit notice(QStringLiteral("未打开串口：内容未发送（%1 字节）").arg(bytes.size()));
        return;
    }

    rememberSend(text);
    emit requestSend(bytes);
}

void SerialAssistant::clearRx()
{
    if (m_log)
        m_log->clear();
    m_rxBytes = 0;
    bumpCounters();
}

void SerialAssistant::clearAll()
{
    if (m_log)
        m_log->clear();
    m_rxBytes = 0;
    m_txBytes = 0;
    bumpCounters();
}

void SerialAssistant::setConnected(bool on)
{
    m_connected = on;
    if (m_connBtn)
        m_connBtn->setText(on ? QStringLiteral("断开串口") : QStringLiteral("打开串口"));
    // 状态圆点 + 文字：绿=已连接 / 红=已断开
    if (m_statusDot)
        m_statusDot->setStyleSheet(on ? QStringLiteral("color: #2e7d32;")    // 绿
                                      : QStringLiteral("color: #d32f2f;"));  // 红
    if (m_statusText)
        m_statusText->setText(on ? QStringLiteral("串口已连接") : QStringLiteral("串口已断开"));
    if (m_sendBtn) {
        m_sendBtn->setEnabled(on);
        m_sendBtn->setToolTip(on ? QString()
                                 : QStringLiteral("先打开串口（点「打开串口」）"));
    }
}

// ---------------------------------------------------------------------------
// 历史与辅助
// ---------------------------------------------------------------------------
void SerialAssistant::rememberSend(const QString &text)
{
    if (!m_sendBox || text.isEmpty())
        return;
    const int existing = m_sendBox->findText(text);
    if (existing >= 0)
        m_sendBox->removeItem(existing);
    m_sendBox->insertItem(0, text);
    while (m_sendBox->count() > 20)
        m_sendBox->removeItem(m_sendBox->count() - 1);
    m_sendBox->setCurrentIndex(0);
    m_historyPos = -1;
    saveHistory();
}

void SerialAssistant::saveHistory() const
{
    if (!m_sendBox)
        return;
    QStringList hist;
    for (int i = 0; i < m_sendBox->count(); ++i)
        hist << m_sendBox->itemText(i);
    AppConfig::setSendHistory(hist);
}

bool SerialAssistant::eventFilter(QObject *watched, QEvent *event)
{
    if (m_sendBox && watched == m_sendBox->lineEdit() && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
            if (m_sendBox->count() == 0)
                return false;
            if (ke->key() == Qt::Key_Up) {
                if (m_historyPos < m_sendBox->count() - 1)
                    ++m_historyPos;
            } else {
                if (m_historyPos > 0)
                    --m_historyPos;
                else {
                    m_historyPos = -1;
                    m_sendBox->lineEdit()->clear();
                    return true;
                }
            }
            if (m_historyPos >= 0)
                m_sendBox->setCurrentText(m_sendBox->itemText(m_historyPos));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SerialAssistant::bumpCounters()
{
    if (m_countLabel)
        m_countLabel->setText(QStringLiteral("收 %1 B · 发 %2 B").arg(m_rxBytes).arg(m_txBytes));
}

bool SerialAssistant::hexDisplay() const
{
    return m_displayFormatBox && m_displayFormatBox->currentIndex() == 1;
}

bool SerialAssistant::hexSend() const
{
    return m_sendFormatBox && m_sendFormatBox->currentIndex() == 1;
}

SerialLineEnding SerialAssistant::lineEnding() const
{
    if (!m_endingBox)
        return SerialLineEnding::None;
    return static_cast<SerialLineEnding>(m_endingBox->currentData().toInt());
}

} // namespace smd
