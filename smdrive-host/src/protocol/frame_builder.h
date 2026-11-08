// 命令帧构造（上位机 -> 驱动器）。所有多字节量按大端（见报告 §3）。
#pragma once

#include "protocol/frame.h"

namespace smd {

class FrameBuilder
{
public:
    static QByteArray auth();                                  // 55 55 01 AA AA
    static QByteArray startUpload(quint16 param = 0x2710);      // 55 55 03 <u16BE> AA AA
    static QByteArray param(int index0, float value);           // 55 55 06 <idx+1> <f32BE> AA AA
    static QByteArray paramFromBits(int index0, quint32 bits);  // 同上，直接给位模式（便于测 0.0）
    static QByteArray command(quint8 cmd);                      // 07/08/09/0A

    // 参数索引 0..9 与固件 float_data_download 0..9 对应，线上 idx = index0 + 1
    static constexpr int kParamCount = 10;
};

} // namespace smd
