#include "transport/serial_transport.h"

#include <QSerialPort>
#include <QTimer>

namespace smd {

SerialTransport::SerialTransport(QObject *parent)
    : ITransport(parent)
{
}

SerialTransport::~SerialTransport()
{
    close();
}

QString SerialTransport::describe() const
{
    if (m_portName.isEmpty())
        return QStringLiteral("（未选择串口）");
    return QStringLiteral("%1 @ %2 8N1").arg(m_portName).arg(m_baud);
}

bool SerialTransport::isOpen() const
{
    return m_port != nullptr && m_port->isOpen();
}

void SerialTransport::open()
{
    m_userClosed = false;
    if (isOpen()) {
        emit opened();
        return;
    }

    if (!m_port) {
        m_port = new QSerialPort(this);
        connect(m_port, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
        connect(m_port, &QSerialPort::errorOccurred, this,
                [this](QSerialPort::SerialPortError e) { onErrorOccurred(int(e)); });
    }

    m_port->setPortName(m_portName);
    // 8N1 + 无流控；DTR/RTS 显式拉低，避免部分 STM32 CDC 实现因电平翻转触发异常握手
    m_port->setBaudRate(m_baud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        fail(QStringLiteral("打开 %1 失败: %2").arg(m_portName, m_port->errorString()));
        return;
    }
    m_port->setDataTerminalReady(false);
    m_port->setRequestToSend(false);
    m_decoder.reset();
    emit opened();
}

void SerialTransport::close()
{
    m_userClosed = true;
    if (m_reconnectTimer)
        m_reconnectTimer->stop();
    if (m_port && m_port->isOpen()) {
        m_port->close();
        emit closed();
    }
}

void SerialTransport::sendBytes(const QByteArray &bytes)
{
    if (!isOpen()) {
        emit errorText(QStringLiteral("串口未打开，%1 字节未发送").arg(bytes.size()));
        return;
    }
    const qint64 n = m_port->write(bytes);
    if (n != bytes.size())
        emit errorText(QStringLiteral("发送不完整: %1/%2 字节").arg(n).arg(bytes.size()));
}

void SerialTransport::onReadyRead()
{
    if (!m_port)
        return;
    const QByteArray chunk = m_port->readAll();
    m_decoder.push(chunk);
    publishFrames();
}

void SerialTransport::publishFrames()
{
    for (DecodedFrame &frame : m_decoder.takeFrames())
        emit frameDecoded(frame);

    const auto &st = m_decoder.stats();
    if (st.framesData != m_lastStatFrames) {
        m_lastStatFrames = st.framesData;
        emit countersUpdated(st.framesData, st.droppedFrames, st.bytesIn);
    }
}

void SerialTransport::onErrorOccurred(int error)
{
    switch (QSerialPort::SerialPortError(error)) {
    case QSerialPort::NoError:
        return;
    case QSerialPort::ResourceError:          // 拔线
        if (!m_port)
            return;
        m_port->close();
        emit closed();
        fail(QStringLiteral("设备已断开（ResourceError）"));
        if (m_autoReconnect && !m_userClosed) {
            if (!m_reconnectTimer) {
                m_reconnectTimer = new QTimer(this);
                m_reconnectTimer->setInterval(1000);
                connect(m_reconnectTimer, &QTimer::timeout, this, &SerialTransport::tryReconnect);
            }
            m_reconnectTimer->start();
        }
        return;
    case QSerialPort::DeviceNotFoundError:
    case QSerialPort::PermissionError:
    case QSerialPort::OpenError:
        fail(QStringLiteral("串口错误(%1): %2").arg(error).arg(m_port ? m_port->errorString() : QString()));
        return;
    default:
        return;                               // 其它非致命错误忽略，等 readyRead
    }
}

void SerialTransport::tryReconnect()
{
    if (m_userClosed) {
        m_reconnectTimer->stop();
        return;
    }
    if (isOpen()) {
        m_reconnectTimer->stop();
        return;
    }
    open();
    if (isOpen() && m_reconnectTimer)
        m_reconnectTimer->stop();
}

void SerialTransport::fail(const QString &msg)
{
    emit errorText(msg);
}

} // namespace smd
