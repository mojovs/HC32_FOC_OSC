// 串口调试助手的编解码纯逻辑（不依赖 Widgets，便于单元测试）。
#pragma once

#include <QByteArray>
#include <QString>

namespace smd {

// 发送时追加到文本末尾的换行符（HEX 模式忽略此项）。
enum class SerialLineEnding { None = 0, Lf, CrLf, Cr };

// 接收显示格式（串口助手「显示格式」下拉）。
enum class SerialDisplayFormat { Text = 0, Hex = 1, Binary = 2 };

// 把界面输入编码成写往串口的原始字节。
//   hex = false：按本地 8 位编码（中文 Windows 即 GBK）后追加 lineEnding。
//   hex = true ：解析十六进制串（允许空格/逗号分隔，可带 0x 前缀），忽略 lineEnding。
// 解析失败返回空 QByteArray，并把 *ok 置 false、*error 写入原因。
QByteArray encodeSerialSend(const QString &text, bool hex, SerialLineEnding lineEnding,
                            bool *ok = nullptr, QString *error = nullptr);

// 把收到的字节渲染成显示文本。
//   Text  ：按本地 8 位解码（保留控制字符，保持串口原样）。
//   Hex   ："AA BB CC " 形式（大写、空格分隔、行尾留一个空格便于续接）。
//   Binary：每字节 8 位二进制，"01001000 11111111 " 形式（空格分隔）。
QString renderSerialBytes(const QByteArray &bytes, SerialDisplayFormat format);

} // namespace smd
