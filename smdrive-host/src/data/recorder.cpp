#include "data/recorder.h"

#include "app/channel_table.h"

#include <QDateTime>

namespace smd {

namespace {
constexpr int kFlushBytes = 256 * 1024;   // 攒够再落盘，避免 250 帧/s 下的写放大
}

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
}

Recorder::~Recorder()
{
    stop();
}

bool Recorder::startRaw(const QString &path, QString *error)
{
    if (m_raw.isOpen())
        stop();
    m_raw.setFileName(path);
    if (!m_raw.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 %1: %2").arg(path, m_raw.errorString());
        return false;
    }
    m_rawBytes = 0;
    return true;
}

bool Recorder::startCsv(const QString &path, const QVector<int> &channels, int sampleRateHz, QString *error)
{
    if (m_csv.isOpen())
        stop();
    if (channels.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有勾选任何波形通道，无法记录 CSV");
        return false;
    }
    m_csv.setFileName(path);
    if (!m_csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 %1: %2").arg(path, m_csv.errorString());
        return false;
    }
    m_channels = channels;
    m_sampleRate = qMax(1, sampleRateHz);
    m_rows = 0;
    m_headerDone = false;
    m_csvBuf.clear();
    writeHeader();
    return true;
}

void Recorder::writeHeader()
{
    if (m_headerDone)
        return;
    QString header = QStringLiteral("t_s,frame");
    const auto &chans = channelTable();
    for (int c : m_channels) {
        const QString base = (c >= 0 && c < int(chans.size())) ? chans[size_t(c)].name
                                                               : QStringLiteral("ch%1").arg(c);
        for (int s = 0; s < kWaveSamples; ++s)
            header += QStringLiteral(",%1_%2").arg(base).arg(s, 2, 10, QLatin1Char('0'));
    }
    header += QStringLiteral("\n");
    m_csvBuf.append(header.toUtf8());
    m_headerDone = true;
}

void Recorder::stop()
{
    flushIfNeeded(true);
    if (m_raw.isOpen())
        m_raw.close();
    if (m_csv.isOpen())
        m_csv.close();
    m_channels.clear();
    m_headerDone = false;
}

void Recorder::onFrame(const smd::DecodedFrame &frame)
{
    if (!frame.raw.isEmpty() && m_raw.isOpen()) {
        m_rawBuf.append(frame.raw);
        m_rawBytes += frame.raw.size();
    }

    if (frame.kind == DecodedFrame::Kind::Data && m_csv.isOpen()) {
        const double t = double(frame.arrivalMs) / 1000.0;
        QString row = QString::number(t, 'f', 6) + QLatin1Char(',') + QString::number(m_rows + 1);
        for (int c : m_channels) {
            for (int s = 0; s < kWaveSamples; ++s) {
                float v = 0.0f;
                if (c < kWaveFloatCount)
                    v = frame.data.floatWaves[size_t(c)][size_t(s)];
                else if (c < kWaveFloatCount + kWaveIntCount)
                    v = float(frame.data.intWaves[size_t(c - kWaveFloatCount)][size_t(s)]);
                row += QLatin1Char(',');
                row += QString::number(double(v), 'g', 7);
            }
        }
        row += QLatin1Char('\n');
        m_csvBuf.append(row.toUtf8());
        ++m_rows;
    }

    flushIfNeeded();
}

void Recorder::flushIfNeeded(bool force)
{
    if (m_raw.isOpen() && (force || m_rawBuf.size() >= kFlushBytes)) {
        m_raw.write(m_rawBuf);
        m_rawBuf.clear();
        if (force)
            m_raw.flush();
    }
    if (m_csv.isOpen() && (force || m_csvBuf.size() >= kFlushBytes)) {
        m_csv.write(m_csvBuf);
        m_csvBuf.clear();
        if (force)
            m_csv.flush();
    }
}

} // namespace smd
