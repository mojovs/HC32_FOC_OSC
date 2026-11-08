#include "app/serial_text_codec.h"

namespace smd {

namespace {

int hexVal(QChar c)
{
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9')
        return int(u - '0');
    if (u >= 'a' && u <= 'f')
        return int(u - 'a') + 10;
    if (u >= 'A' && u <= 'F')
        return int(u - 'A') + 10;
    return -1;
}

} // namespace

QByteArray encodeSerialSend(const QString &text, bool hex, SerialLineEnding lineEnding,
                            bool *ok, QString *error)
{
    if (ok)
        *ok = true;
    if (error)
        error->clear();

    if (!hex) {
        QByteArray out = text.toLocal8Bit();
        switch (lineEnding) {
        case SerialLineEnding::Lf:
            out.append('\n');
            break;
        case SerialLineEnding::CrLf:
            out.append('\r');
            out.append('\n');
            break;
        case SerialLineEnding::Cr:
            out.append('\r');
            break;
        case SerialLineEnding::None:
            break;
        }
        return out;
    }

    // HEX：去掉分隔符与 0x 前缀后按偶数个十六进制字符解析
    QString s = text;
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('\t'));
    s.remove(QLatin1Char('\r'));
    s.remove(QLatin1Char('\n'));
    s.remove(QLatin1Char(','));
    s.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    if (s.isEmpty())
        return QByteArray();
    if (s.size() % 2 != 0) {
        if (ok)
            *ok = false;
        if (error)
            *error = QStringLiteral("HEX 字符个数必须是偶数（当前 %1）").arg(s.size());
        return QByteArray();
    }

    QByteArray out;
    out.reserve(s.size() / 2);
    for (int i = 0; i < s.size(); i += 2) {
        const int hi = hexVal(s.at(i));
        const int lo = hexVal(s.at(i + 1));
        if (hi < 0 || lo < 0) {
            if (ok)
                *ok = false;
            if (error)
                *error = QStringLiteral("HEX 含非法字符 '%1'").arg(hi < 0 ? s.at(i) : s.at(i + 1));
            return QByteArray();
        }
        out.append(char((hi << 4) | lo));
    }
    return out;
}

QString renderSerialBytes(const QByteArray &bytes, SerialDisplayFormat format)
{
    if (format == SerialDisplayFormat::Text)
        return QString::fromLocal8Bit(bytes);

    if (format == SerialDisplayFormat::Binary) {
        QString out;
        out.reserve(bytes.size() * 9);
        for (char c : bytes) {
            const unsigned char b = static_cast<unsigned char>(c);
            for (int bit = 7; bit >= 0; --bit)
                out.append((b & (1u << bit)) != 0u ? QLatin1Char('1') : QLatin1Char('0'));
            out.append(QLatin1Char(' '));
        }
        return out;
    }

    static const char kDigits[] = "0123456789ABCDEF";
    QString out;
    out.reserve(bytes.size() * 3);
    for (char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        out.append(QLatin1Char(kDigits[b >> 4]));
        out.append(QLatin1Char(kDigits[b & 0x0F]));
        out.append(QLatin1Char(' '));
    }
    return out;
}

} // namespace smd
