// 留档：原始字节流(.bin，可回放) + CSV 宽表（每行一帧，便于 Excel 直接看）。
#pragma once

#include "protocol/frame.h"

#include <QFile>
#include <QObject>
#include <QVector>

namespace smd {

class Recorder : public QObject
{
    Q_OBJECT
public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    bool startRaw(const QString &path, QString *error);
    bool startCsv(const QString &path, const QVector<int> &channels, int sampleRateHz, QString *error);
    void stop();
    bool isActive() const { return m_raw.isOpen() || m_csv.isOpen(); }
    bool rawActive() const { return m_raw.isOpen(); }
    bool csvActive() const { return m_csv.isOpen(); }
    quint64 rowsWritten() const { return m_rows; }
    qint64 rawBytesWritten() const { return m_rawBytes; }

    /// 记录一帧（数据帧写波形/标量；认证帧只写原始流）
    void onFrame(const smd::DecodedFrame &frame);

private:
    void flushIfNeeded(bool force = false);
    void writeHeader();

    QFile m_raw;
    QFile m_csv;
    QVector<int> m_channels;
    int m_sampleRate = 10000;
    QByteArray m_rawBuf;
    QByteArray m_csvBuf;
    quint64 m_rows = 0;
    qint64 m_rawBytes = 0;
    bool m_headerDone = false;
};

} // namespace smd
