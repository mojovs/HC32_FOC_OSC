#include "protocol/frame.h"

namespace smd {

QString AuthFrame::serialHex() const
{
    QString s;
    s.reserve(kAuthSerialLen * 2);
    for (quint8 b : serial)
        s += QString("%1").arg(b, 2, 16, QLatin1Char('0')).toUpper();
    return s;
}

} // namespace smd
