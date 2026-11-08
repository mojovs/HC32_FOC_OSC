// 协议层单元测试：golden 用例 + 健壮性（计划书 §9.1 / §9.2）
#include "data/ring_buffer.h"
#include "protocol/frame.h"
#include "protocol/frame_builder.h"
#include "protocol/frame_decoder.h"

#include <QtTest>

#include <cstring>

using namespace smd;

namespace {

QByteArray u32be(quint32 v)
{
    QByteArray b;
    b.append(char((v >> 24) & 0xFF));
    b.append(char((v >> 16) & 0xFF));
    b.append(char((v >> 8) & 0xFF));
    b.append(char(v & 0xFF));
    return b;
}

QByteArray f32be(float f)
{
    quint32 bits = 0;
    std::memcpy(&bits, &f, 4);
    return u32be(bits);
}

QByteArray makeDataFrame(int seed)
{
    QByteArray body;
    body.reserve(kPayloadLen);
    for (int c = 0; c < kWaveFloatCount; ++c) {
        body.append(char(c));                                   // 波形序号
        for (int s = 0; s < kWaveSamples; ++s)
            body.append(f32be(float(seed + c * 100) + s * 0.5f));
    }
    for (int c = 0; c < kWaveIntCount; ++c) {
        body.append(char(kIntWaveType));                        // 类型 0x06
        for (int s = 0; s < kWaveSamples; ++s)
            body.append(u32be(quint32(seed + c * 1000 + s)));
    }
    for (int i = 0; i < kDataFloatCount; ++i) {
        body.append(char(i));
        body.append(f32be(float(seed) + i * 1.25f));
    }
    for (int i = 0; i < kDataIntCount; ++i) {
        body.append(char(i));
        body.append(u32be(quint32(seed + 100 + i)));
    }
    Q_ASSERT(body.size() == kPayloadLen);

    QByteArray frame;
    frame.append(char(kHeader));
    frame.append(char(kHeader));
    frame.append(char(RSP_DATA));
    frame.append(body);
    frame.append(char(kFooter));
    frame.append(char(kFooter));
    Q_ASSERT(frame.size() == kDataFrameLen);
    return frame;
}

QByteArray makeAuthFrame()
{
    QByteArray f;
    f.append(char(kHeader));
    f.append(char(kHeader));
    f.append(char(RSP_AUTH));
    for (int i = 0; i < kAuthSerialLen; ++i)
        f.append(char(0x10 + i));
    f.append(char(kFooter));
    f.append(char(kFooter));
    Q_ASSERT(f.size() == kAuthFrameLen);
    return f;
}

inline bool isDataKind(const DecodedFrame &f) { return f.kind == DecodedFrame::Kind::Data; }
inline bool isAuthKind(const DecodedFrame &f) { return f.kind == DecodedFrame::Kind::Auth; }

} // namespace

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    // ---------- §9.1 golden：与 MainFrame.java 中的原始帧字符串逐字节比对 ----------
    void goldenBuilders()
    {
        QCOMPARE(FrameBuilder::auth().toHex().toUpper(), QByteArray("555501AAAA"));
        QCOMPARE(FrameBuilder::startUpload().toHex().toUpper(), QByteArray("5555032710AAAA"));
        QCOMPARE(FrameBuilder::command(CMD_START).toHex().toUpper(), QByteArray("555507AAAA"));
        QCOMPARE(FrameBuilder::command(CMD_ACCEL).toHex().toUpper(), QByteArray("555508AAAA"));
        QCOMPARE(FrameBuilder::command(CMD_DECEL).toHex().toUpper(), QByteArray("555509AAAA"));
        QCOMPARE(FrameBuilder::command(CMD_STOP).toHex().toUpper(), QByteArray("55550AAAAA"));
        QCOMPARE(FrameBuilder::param(0, 1.0f).toHex().toUpper(), QByteArray("555506013F800000AAAA"));
        QCOMPARE(FrameBuilder::param(1, -1.5f).toHex().toUpper(), QByteArray("55550602BFC00000AAAA"));
        QCOMPARE(FrameBuilder::param(9, 3.14f).toHex().toUpper(), QByteArray("5555060A4048F5C3AAAA"));
    }

    void paramZeroStillFourBytes()
    {
        // 原版在 0 值上会因 Integer.toHexString 不补前导零而发出错位帧；这里必须恒定 4 字节
        const QByteArray f = FrameBuilder::param(2, 0.0f);
        QCOMPARE(f.toHex().toUpper(), QByteArray("5555060300000000AAAA"));
        QCOMPARE(f.size(), 10);          // 55 55 06 idx f32BE AA AA
        // 高位半字节为 0 的位模式同样保持 4 字节
        const QByteArray g = FrameBuilder::paramFromBits(4, 0x0001F90Au);
        QCOMPARE(g.toHex().toUpper(), QByteArray("555506050001F90AAAAA"));
        QCOMPARE(g.size(), 10);
    }

    // ---------- 解码：正常 / 逐字节 / 分包 / 噪声 / 伪同步 ----------
    void decodeSingleFrame()
    {
        FrameDecoder dec;
        dec.push(makeDataFrame(7));
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QVERIFY(isDataKind(frames[0]));
        QCOMPARE(frames[0].raw.size(), kDataFrameLen);
        QCOMPARE(frames[0].data.floatWaves[0][0], 7.0f);
        QCOMPARE(frames[0].data.floatWaves[1][0], 107.0f);
        QCOMPARE(frames[0].data.floatWaves[5][39], float(7 + 500) + 39 * 0.5f);
        QCOMPARE(frames[0].data.intWaves[0][0], quint32(7));
        QCOMPARE(frames[0].data.intWaves[3][39], quint32(7 + 3000 + 39));
        QCOMPARE(frames[0].data.intWaveTypes[0], quint8(kIntWaveType));
        QCOMPARE(frames[0].data.floatData[3], 7.0f + 3 * 1.25f);
        QCOMPARE(frames[0].data.intData[9], quint32(7 + 109));
        QCOMPARE(dec.stats().framesData, quint64(1));
        QCOMPARE(dec.stats().droppedFrames, quint64(0));
        QCOMPARE(dec.stats().suspicious, quint64(0));
    }

    void decodeByteByByte()
    {
        FrameDecoder dec;
        const QByteArray f = makeDataFrame(1);
        for (int i = 0; i < f.size(); ++i)
            dec.push(f.mid(i, 1));
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QCOMPARE(frames[0].raw, f);
        QCOMPARE(dec.stats().bytesGarbage, quint64(0));
    }

    void decodeChunkedWithHalfFrame()
    {
        FrameDecoder dec;
        const QByteArray a = makeDataFrame(1);
        const QByteArray b = makeDataFrame(2);
        const QByteArray stream = a + b + b.left(900);      // 2 整帧 + 半帧

        dec.push(stream.left(2000));                        // 1 整帧 + 285 B
        QCOMPARE(int(dec.takeFrames().size()), 1);

        dec.push(stream.mid(2000));                         // 补齐第 2 帧，半帧仍缺 815 B
        auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QCOMPARE(frames[0].data.floatData[0], 2.0f);

        dec.push(b.mid(900));                               // 补上最后 815 B
        frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QCOMPARE(frames[0].raw, b);
        QCOMPARE(dec.stats().droppedFrames, quint64(0));
    }

    void decodeNoiseAndCorruptFooter()
    {
        FrameDecoder dec;
        QByteArray bad = makeDataFrame(3);
        bad[bad.size() - 1] = char(0x00);                   // 帧尾损坏
        dec.push(QByteArray(37, char(0x33)));               // 前导噪声
        dec.push(bad);
        dec.push(makeDataFrame(4));
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);                    // 只有第 2 帧可用
        QCOMPARE(frames[0].data.floatData[0], 4.0f);
        QVERIFY(dec.stats().droppedFrames >= quint64(1));
        QVERIFY(dec.stats().bytesGarbage >= quint64(37));
    }

    void decodeFakeHeaderInsidePayload()
    {
        FrameDecoder dec;
        QByteArray f = makeDataFrame(5);
        f[100] = char(kHeader);                             // payload 内伪造 55 55 04
        f[101] = char(kHeader);
        f[102] = char(RSP_DATA);
        dec.push(f);
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QCOMPARE(frames[0].raw, f);
        QCOMPARE(dec.stats().droppedFrames, quint64(0));
    }

    void decodeAuthFrame()
    {
        FrameDecoder dec;
        dec.push(makeAuthFrame());
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QVERIFY(isAuthKind(frames[0]));
        QCOMPARE(frames[0].auth.serialHex(), QStringLiteral("101112131415161718191A1B"));
        QCOMPARE(dec.stats().framesAuth, quint64(1));
    }

    void decodeUnknownCommandIsSkipped()
    {
        FrameDecoder dec;
        QByteArray bogus;
        bogus.append(char(kHeader));
        bogus.append(char(kHeader));
        bogus.append(char(0x7F));                           // 未知命令
        bogus.append(QByteArray(64, char(0x00)));
        dec.push(bogus + makeDataFrame(6));
        const auto frames = dec.takeFrames();
        QCOMPARE(int(frames.size()), 1);
        QCOMPARE(frames[0].data.floatData[0], 6.0f);
    }

    void ringBufferWraps()
    {
        RingBuffer<float> rb(8);
        for (int i = 0; i < 20; ++i)
            rb.append(float(i));
        QCOMPARE(int(rb.size()), 8);
        QCOMPARE(rb.total(), quint64(20));
        QCOMPARE(rb.at(0), 12.0f);
        QCOMPARE(rb.at(7), 19.0f);
        QVERIFY(rb.contains(12));
        QVERIFY(!rb.contains(11));
        QCOMPARE(rb.absoluteIndex(0), quint64(12));
    }
};

QTEST_MAIN(TestProtocol)
#include "test_protocol.moc"
