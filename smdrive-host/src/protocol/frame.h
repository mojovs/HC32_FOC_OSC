// 协议常量与数据结构 —— 与《上位机逆向_通讯协议报告》§3/§4 及附录 A/B 一致。
// 帧格式: 55 55 <CMD> [payload] AA AA  （无长度字段、无 CRC）
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>

#include <array>
#include <cstdint>

namespace smd {

constexpr quint8 kHeader = 0x55;
constexpr quint8 kFooter = 0xAA;
constexpr int kHeaderLen = 3;          // 55 55 CMD
constexpr int kFooterLen = 2;          // AA AA

enum Cmd : quint8 {
    CMD_AUTH         = 0x01,           // 认证/登录请求
    CMD_START_UPLOAD = 0x03,           // 开始上传（u16BE 参数）
    CMD_CLEAR        = 0x05,           // 固件有分支，本上位机不发送
    CMD_PARAM        = 0x06,           // 下传浮点参数（idx + f32BE）
    CMD_START        = 0x07,           // 启动
    CMD_ACCEL        = 0x08,           // 加速
    CMD_DECEL        = 0x09,           // 减速
    CMD_STOP         = 0x0A,           // 停止/暂停
    RSP_AUTH         = 0x02,           // 驱动器 -> 认证应答
    RSP_DATA         = 0x04,           // 驱动器 -> 波形/数据帧
};

// ---- 数据帧几何（见报告 §4.2）----
constexpr int kWaveFloatCount = 6;
constexpr int kWaveIntCount   = 4;
constexpr int kWaveSamples    = 40;
constexpr int kDataFloatCount = 10;
constexpr int kDataIntCount   = 10;
constexpr int kWaveBlock      = 1 + 4 * kWaveSamples;                              // 161 B
constexpr int kPayloadFloatWaves = kWaveFloatCount * kWaveBlock;                   // 966  B
constexpr int kPayloadIntWaves   = kWaveIntCount   * kWaveBlock;                   // 644  B
constexpr int kPayloadFloatData  = kDataFloatCount * 5;                            // 50   B
constexpr int kPayloadIntData    = kDataIntCount   * 5;                            // 50   B
constexpr int kPayloadLen = kPayloadFloatWaves + kPayloadIntWaves
                          + kPayloadFloatData + kPayloadIntData;                   // 1710 B
constexpr int kAuthSerialLen = 12;
constexpr int kAuthFrameLen  = kHeaderLen + kAuthSerialLen + kFooterLen;           // 17
constexpr int kDataFrameLen  = kHeaderLen + kPayloadLen + kFooterLen;              // 1715
constexpr quint8 kIntWaveType = 0x06;                                              // 固件 int_wave 类型

// 认证应答：55 55 02 + 12B 序列号(3×u32 BE) + AA AA
struct AuthFrame {
    std::array<quint8, kAuthSerialLen> serial{};
    QString serialHex() const;
};

// 波形/数据帧：55 55 04 + 1710B payload + AA AA
struct DataFrame {
    std::array<std::array<float, kWaveSamples>, kWaveFloatCount> floatWaves{};
    std::array<std::array<quint32, kWaveSamples>, kWaveIntCount> intWaves{};
    std::array<quint8, kWaveIntCount> intWaveTypes{};
    std::array<quint8, kWaveFloatCount> floatWaveIdx{};
    std::array<float, kDataFloatCount> floatData{};
    std::array<quint32, kDataIntCount> intData{};
    std::array<quint8, kDataFloatCount> floatDataIdx{};
    std::array<quint8, kDataIntCount> intDataIdx{};
};

struct DecodedFrame {
    enum class Kind { Auth, Data } kind = Kind::Auth;
    AuthFrame auth;
    DataFrame data;
    quint64 arrivalMs = 0;             // 收到时刻（PC 单调时钟）
    QByteArray raw;                    // 原始帧字节（留档/回放用，长度 = 17 或 1715）
};

} // namespace smd

Q_DECLARE_METATYPE(smd::DecodedFrame)
