// 流式帧解码器（无长度字段/无 CRC 前提下的重同步状态机，见计划书 §5.3）。
// 纯逻辑，不依赖事件循环，可在任意线程使用。
#pragma once

#include "protocol/frame.h"

#include <QByteArray>

#include <vector>

namespace smd {

class FrameDecoder
{
public:
    struct Stats {
        quint64 bytesIn = 0;        // 累计输入字节
        quint64 bytesGarbage = 0;   // 被判为噪声丢弃的字节
        quint64 framesAuth = 0;     // 成功解析的认证应答帧
        quint64 framesData = 0;     // 成功解析的数据帧
        quint64 droppedFrames = 0;  // 帧尾校验失败/长度不符而丢弃的候选帧
        quint64 suspicious = 0;     // 结构可疑（序号/类型字段越界）但已接受的帧
    };

    void push(const QByteArray &chunk);          // 喂入任意切分的字节流
    std::vector<DecodedFrame> takeFrames();      // 取出已解析帧（取走即清空）
    const Stats &stats() const { return m_stats; }
    void reset();

    static int expectedLength(quint8 cmd);       // 0 表示未知命令

private:
    void process();

    QByteArray m_buf;
    std::vector<DecodedFrame> m_frames;
    Stats m_stats;
};

} // namespace smd
