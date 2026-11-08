#include "protocol/frame_decoder.h"

#include <QDateTime>

#include <cstring>

namespace smd {

namespace {

inline float readF32BE(const char *p)
{
    const quint32 bits = (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16)
                       | (quint32(quint8(p[2])) << 8)  |  quint32(quint8(p[3]));
    float v = 0.0f;
    std::memcpy(&v, &bits, 4);
    return v;
}

inline quint32 readU32BE(const char *p)
{
    return (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16)
         | (quint32(quint8(p[2])) << 8)  |  quint32(quint8(p[3]));
}

constexpr int kMaxBuffer = 64 * 1024;   // 噪声保护：缓冲不无限增长

} // namespace

int FrameDecoder::expectedLength(quint8 cmd)
{
    switch (cmd) {
    case RSP_AUTH: return kAuthFrameLen;   // 17
    case RSP_DATA: return kDataFrameLen;   // 1715
    default:       return 0;               // 未知/下行命令，不作为接收帧
    }
}

void FrameDecoder::reset()
{
    m_buf.clear();
    m_frames.clear();
    m_stats = Stats{};
}

void FrameDecoder::push(const QByteArray &chunk)
{
    if (chunk.isEmpty())
        return;
    m_stats.bytesIn += quint64(chunk.size());
    m_buf.append(chunk);
    process();
}

std::vector<DecodedFrame> FrameDecoder::takeFrames()
{
    std::vector<DecodedFrame> out;
    out.swap(m_frames);
    return out;
}

void FrameDecoder::process()
{
    while (!m_buf.isEmpty()) {
        // 1) 找帧头第一个 0x55
        const int sync = m_buf.indexOf(char(kHeader));
        if (sync < 0) {
            // 全是噪声；但保留最后 1 字节（可能是被切开的 0x55）
            const int drop = m_buf.size() - (m_buf.at(m_buf.size() - 1) == char(kHeader) ? 1 : 0);
            if (drop > 0) {
                m_stats.bytesGarbage += quint64(drop);
                m_buf.remove(0, drop);
            }
            break;
        }
        if (sync > 0) {
            m_stats.bytesGarbage += quint64(sync);
            m_buf.remove(0, sync);
        }

        // 2) 需要 55 55 CMD
        if (m_buf.size() < kHeaderLen)
            break;
        if (quint8(m_buf.at(1)) != kHeader) {
            m_stats.bytesGarbage += 1;
            m_buf.remove(0, 1);
            continue;
        }
        const quint8 cmd = quint8(m_buf.at(2));
        const int len = expectedLength(cmd);
        if (len == 0) {
            m_stats.bytesGarbage += 1;
            m_buf.remove(0, 1);   // 未知命令：从第 2 字节继续扫描
            continue;
        }
        if (m_buf.size() < len)
            break;                // 半帧，等更多数据

        // 3) 帧尾校验
        const char *base = m_buf.constData();
        const bool footerOk = quint8(base[len - 2]) == kFooter && quint8(base[len - 1]) == kFooter;
        if (!footerOk) {
            ++m_stats.droppedFrames;
            m_stats.bytesGarbage += 1;
            m_buf.remove(0, 1);   // 滑动 1 字节重新同步
            continue;
        }

        // 4) 解析
        DecodedFrame frame;
        frame.arrivalMs = quint64(QDateTime::currentMSecsSinceEpoch());
        frame.raw = m_buf.left(len);   // 原样保留，供留档与离线回放
        if (cmd == RSP_AUTH) {
            frame.kind = DecodedFrame::Kind::Auth;
            for (int i = 0; i < kAuthSerialLen; ++i)
                frame.auth.serial[size_t(i)] = quint8(base[kHeaderLen + i]);
            ++m_stats.framesAuth;
            m_frames.push_back(frame);
        } else {
            frame.kind = DecodedFrame::Kind::Data;
            const char *p = base + kHeaderLen;
            bool suspicious = false;

            for (int c = 0; c < kWaveFloatCount; ++c) {
                frame.data.floatWaveIdx[size_t(c)] = quint8(p[0]);
                if (quint8(p[0]) > 9)
                    suspicious = true;
                p += 1;
                for (int s = 0; s < kWaveSamples; ++s, p += 4)
                    frame.data.floatWaves[size_t(c)][size_t(s)] = readF32BE(p);
            }
            for (int c = 0; c < kWaveIntCount; ++c) {
                frame.data.intWaveTypes[size_t(c)] = quint8(p[0]);
                if (quint8(p[0]) != kIntWaveType)
                    suspicious = true;
                p += 1;
                for (int s = 0; s < kWaveSamples; ++s, p += 4)
                    frame.data.intWaves[size_t(c)][size_t(s)] = readU32BE(p);
            }
            for (int i = 0; i < kDataFloatCount; ++i, p += 5) {
                frame.data.floatDataIdx[size_t(i)] = quint8(p[0]);
                if (quint8(p[0]) > 9)
                    suspicious = true;
                frame.data.floatData[size_t(i)] = readF32BE(p + 1);
            }
            for (int i = 0; i < kDataIntCount; ++i, p += 5) {
                frame.data.intDataIdx[size_t(i)] = quint8(p[0]);
                if (quint8(p[0]) > 9)
                    suspicious = true;
                frame.data.intData[size_t(i)] = readU32BE(p + 1);
            }
            if (suspicious)
                ++m_stats.suspicious;
            ++m_stats.framesData;
            m_frames.push_back(frame);
        }

        m_buf.remove(0, len);
    }

    if (m_buf.size() > kMaxBuffer) {   // 极端噪声下防止缓冲爆炸
        m_stats.bytesGarbage += quint64(m_buf.size() - kMaxBuffer);
        m_buf.remove(0, m_buf.size() - kMaxBuffer);
    }
}

} // namespace smd
