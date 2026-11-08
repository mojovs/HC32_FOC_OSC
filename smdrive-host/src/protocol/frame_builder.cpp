#include "protocol/frame_builder.h"

#include <QtEndian>

#include <cstring>

namespace smd {

namespace {

QByteArray wrap(const QByteArray &body)
{
    QByteArray out;
    out.reserve(body.size() + kHeaderLen + kFooterLen);
    out.append(char(kHeader));
    out.append(char(kHeader));
    out.append(body);
    out.append(char(kFooter));
    out.append(char(kFooter));
    return out;
}

} // namespace

QByteArray FrameBuilder::auth()
{
    return wrap(QByteArray(1, char(CMD_AUTH)));
}

QByteArray FrameBuilder::startUpload(quint16 param)
{
    QByteArray body(1, char(CMD_START_UPLOAD));
    body.append(char((param >> 8) & 0xFF));   // u16 大端
    body.append(char(param & 0xFF));
    return wrap(body);
}

QByteArray FrameBuilder::param(int index0, float value)
{
    quint32 bits = 0;
    static_assert(sizeof(float) == 4, "float must be 32 bit");
    std::memcpy(&bits, &value, 4);
    return paramFromBits(index0, bits);
}

QByteArray FrameBuilder::paramFromBits(int index0, quint32 bits)
{
    QByteArray body(1, char(CMD_PARAM));
    body.append(char(quint8(index0 + 1)));    // 线上 idx = 1..10
    // 强制 4 字节大端 —— 修掉原版 Integer.toHexString 不补前导零导致的半字节错位
    body.append(char((bits >> 24) & 0xFF));
    body.append(char((bits >> 16) & 0xFF));
    body.append(char((bits >> 8) & 0xFF));
    body.append(char(bits & 0xFF));
    return wrap(body);
}

QByteArray FrameBuilder::command(quint8 cmd)
{
    return wrap(QByteArray(1, char(cmd)));
}

} // namespace smd
