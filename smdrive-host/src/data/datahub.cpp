#include "data/datahub.h"

namespace smd {

DataHub::DataHub(QObject *parent)
    : QObject(parent)
{
    configure(m_sampleRate, m_historySeconds);
}

void DataHub::configure(int sampleRateHz, double historySeconds)
{
    m_sampleRate = qMax(1, sampleRateHz);
    m_historySeconds = qMax(0.1, historySeconds);
    const size_t capacity = size_t(qMax<double>(3.0 * kWaveSamples, m_sampleRate * m_historySeconds));
    for (auto &rb : m_waves)
        rb.resize(capacity);
    clear();
}

void DataHub::clear()
{
    for (auto &rb : m_waves)
        rb.clear();
    m_floatData.fill(0.0f);
    m_intData.fill(0u);
    m_framesData = 0;
    m_framesAuth = 0;
}

void DataHub::append(const smd::DecodedFrame &frame)
{
    if (frame.kind == DecodedFrame::Kind::Auth) {
        ++m_framesAuth;
        m_lastSerial = frame.auth.serialHex();
        emit serialNumberSeen(m_lastSerial);
        return;
    }

    ++m_framesData;
    for (int c = 0; c < kWaveFloatCount; ++c) {
        for (int s = 0; s < kWaveSamples; ++s)
            m_waves[size_t(c)].append(frame.data.floatWaves[size_t(c)][size_t(s)]);
    }
    for (int c = 0; c < kWaveIntCount; ++c) {
        const int ch = kWaveFloatCount + c;
        for (int s = 0; s < kWaveSamples; ++s) {
            // 线上是 u32 位模式，固件侧语义是 s32：必须按有符号解释，
            // 否则负样本会显示成 4.29e9 级别的巨值并把自动量程撑爆
            m_waves[size_t(ch)].append(float(qint32(frame.data.intWaves[size_t(c)][size_t(s)])));
        }
    }
    for (int i = 0; i < kFloatData; ++i)
        m_floatData[size_t(i)] = frame.data.floatData[size_t(i)];
    for (int i = 0; i < kIntData; ++i)
        m_intData[size_t(i)] = frame.data.intData[size_t(i)];

    emit dataAppended();
}

} // namespace smd
