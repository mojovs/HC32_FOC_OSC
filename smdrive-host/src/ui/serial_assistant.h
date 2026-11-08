// 串口调试助手（类似 XCOM）：右栏「串口助手」模式下的收发控制台。
// 控件自带两块 UI：
//   terminalWidget() —— 放左侧主区：工具条（打开/断开串口、清除接收、清空）+ 接收日志 + 发送行
//                       （回车发送、↑/↓ 翻历史）；
//   optionsWidget()  —— 放右侧控制栏：串口参数 + 显示/收发开关 + 统计。
// 原始字节由 MainWindow 经 requestSend / appendRx 中转，控件本身不直接碰串口；
// 开关串口也只发 requestToggleConnection，由 MainWindow 复用同一套连接逻辑。
#pragma once

#include "app/serial_text_codec.h"

#include <QByteArray>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
QT_END_NAMESPACE

namespace smd {

class SerialAssistant : public QWidget
{
    Q_OBJECT
public:
    explicit SerialAssistant(QWidget *parent = nullptr);
    ~SerialAssistant() override;

    QWidget *terminalWidget() const { return m_terminalPanel; }   // 左主区
    QWidget *optionsWidget() const { return m_optionsPanel; }     // 右栏

public slots:
    void appendRx(const QByteArray &chunk);      // 串口收到的原始字节 → 日志
    void appendTx(const QByteArray &bytes);      // 本机发出的字节（按开关回显）
    void setConnected(bool on);                  // 串口开/关时切换发送与开关按钮
    void clearRx();                              // 清空接收区显示并复位累计收字节数
    void clearAll();                             // 清空日志与收发计数

signals:
    void requestSend(const QByteArray &bytes);   // 已编码（含换行）的原始字节
    void requestBaudRate(int baud);
    void requestToggleConnection();              // 打开 / 断开串口（复用主窗口的连接逻辑）
    void notice(const QString &text);            // 供状态栏提示

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildTerminal();
    void buildOptions();
    void onSendClicked();
    void appendDisplay(const QString &text, bool isTx);
    void bumpCounters();
    void rememberSend(const QString &text);
    void saveHistory() const;
    bool hexDisplay() const;
    bool hexSend() const;
    SerialLineEnding lineEnding() const;

    QWidget *m_terminalPanel = nullptr;
    QWidget *m_optionsPanel = nullptr;

    // 左主区
    QPlainTextEdit *m_log = nullptr;
    QComboBox *m_sendBox = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_connBtn = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_statusDot = nullptr;    // 状态圆点：绿=已连接 / 红=已断开
    QLabel *m_statusText = nullptr;   // 状态文字：串口已连接 / 串口已断开

    // 右栏
    QComboBox *m_baudBox = nullptr;
    QComboBox *m_endingBox = nullptr;
    QComboBox *m_sendFormatBox = nullptr;
    QComboBox *m_displayFormatBox = nullptr;
    QCheckBox *m_showTxBox = nullptr;
    QCheckBox *m_timestampBox = nullptr;
    QCheckBox *m_autoScrollBox = nullptr;
    QCheckBox *m_pauseBox = nullptr;

    quint64 m_rxBytes = 0;
    quint64 m_txBytes = 0;
    int m_historyPos = -1;
    bool m_connected = false;
};

} // namespace smd
