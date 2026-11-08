// 串口传输（STM32 USB CDC 虚拟串口，8N1；见计划书 §5.1）。
#pragma once

#include "protocol/frame_decoder.h"
#include "transport/itransport.h"

QT_BEGIN_NAMESPACE
class QSerialPort;
class QTimer;
QT_END_NAMESPACE

namespace smd {

class SerialTransport : public ITransport
{
    Q_OBJECT
public:
    explicit SerialTransport(QObject *parent = nullptr);
    ~SerialTransport() override;

    void setPortName(const QString &name) { m_portName = name; }
    QString portName() const { return m_portName; }
    void setBaudRate(int baud) { m_baud = baud; }
    void setAutoReconnect(bool on) { m_autoReconnect = on; }

    QString describe() const override;
    bool isOpen() const override;

public slots:
    void open() override;
    void close() override;
    void sendBytes(const QByteArray &bytes) override;

private slots:
    void onReadyRead();
    void onErrorOccurred(int error);
    void tryReconnect();

private:
    void publishFrames();
    void fail(const QString &msg);

    QString m_portName;
    int m_baud = 9600;
    bool m_autoReconnect = true;
    bool m_userClosed = false;
    QSerialPort *m_port = nullptr;      // 在工作线程内创建
    QTimer *m_reconnectTimer = nullptr;
    FrameDecoder m_decoder;
    quint64 m_lastStatFrames = 0;
};

} // namespace smd
