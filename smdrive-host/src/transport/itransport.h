// 传输层抽象：真机串口与离线回放共用同一条下游链路（计划书 §4.1）。
// 实现对象会被 moveToThread 到工作线程，所有槽都在工作线程内执行。
#pragma once

#include "protocol/frame.h"

#include <QByteArray>
#include <QObject>
#include <QString>

namespace smd {

class ITransport : public QObject
{
    Q_OBJECT
public:
    explicit ITransport(QObject *parent = nullptr) : QObject(parent) {}
    ~ITransport() override = default;

    virtual QString describe() const = 0;
    virtual bool isOpen() const = 0;

public slots:
    virtual void open() = 0;
    virtual void close() = 0;
    virtual void sendBytes(const QByteArray &bytes) = 0;

signals:
    void frameDecoded(const smd::DecodedFrame &frame);
    void opened();
    void closed();
    void errorText(const QString &msg);
    void countersUpdated(quint64 framesData, quint64 droppedFrames, quint64 bytesIn);
};

} // namespace smd
