// 数据中枢：把解码后的帧落进每通道环形缓冲 + 标量快照（计划书 §4.1 数据层）。
#pragma once

#include "data/ring_buffer.h"
#include "protocol/frame.h"

#include <QObject>
#include <QString>

#include <array>

namespace smd {

class DataHub : public QObject
{
    Q_OBJECT
public:
    static constexpr int kChannels = 10;
    static constexpr int kFloatData = 10;
    static constexpr int kIntData = 10;

    explicit DataHub(QObject *parent = nullptr);

    /// 采样率与历史深度决定缓冲容量；改变会清空数据
    void configure(int sampleRateHz, double historySeconds);
    int sampleRate() const { return m_sampleRate; }
    double historySeconds() const { return m_historySeconds; }

    void clear();

    const RingBuffer<float> &channel(int c) const { return m_waves[size_t(c)]; }
    float floatData(int i) const { return m_floatData[size_t(i)]; }
    quint32 intData(int i) const { return m_intData[size_t(i)]; }
    QString lastSerialHex() const { return m_lastSerial; }

    quint64 framesData() const { return m_framesData; }
    quint64 framesAuth() const { return m_framesAuth; }
    quint64 samplesPerChannel() const { return m_waves[0].total(); }

public slots:
    void append(const smd::DecodedFrame &frame);

signals:
    void dataAppended();          // 仅作通知，UI 用 60 Hz 定时器节流读取
    void serialNumberSeen(const QString &serial);

private:
    std::array<RingBuffer<float>, kChannels> m_waves;
    std::array<float, kFloatData> m_floatData{};
    std::array<quint32, kIntData> m_intData{};
    QString m_lastSerial;
    int m_sampleRate = 10000;
    double m_historySeconds = 20.0;
    quint64 m_framesData = 0;
    quint64 m_framesAuth = 0;
};

} // namespace smd
