#include "transport/replay_transport.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>

namespace smd {

namespace {
constexpr int kTickMs = 10;             // 100 Hz 调度，按累计时间换算应发帧数
constexpr int kReadBlock = 64 * 1024;
}

ReplayTransport::ReplayTransport(QObject *parent)
    : ITransport(parent)
{
}

ReplayTransport::~ReplayTransport()
{
    close();
}

QString ReplayTransport::describe() const
{
    if (m_path.isEmpty())
        return QStringLiteral("（未选择回放文件）");
    return QStringLiteral("回放 %1 @ %2 帧/s").arg(QFileInfo(m_path).fileName()).arg(m_fps);
}

void ReplayTransport::open()
{
    close();
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        emit errorText(QStringLiteral("无法打开回放文件 %1: %2").arg(m_path, m_file.errorString()));
        return;
    }
    restart();
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(kTickMs);
        connect(m_timer, &QTimer::timeout, this, &ReplayTransport::tick);
    }
    if (!m_clock)
        m_clock = new QElapsedTimer();
    m_clock->start();
    m_timer->start();
    emit opened();
}

void ReplayTransport::close()
{
    if (m_timer)
        m_timer->stop();
    if (m_file.isOpen()) {
        m_file.close();
        emit closed();
    }
    m_ready.clear();
}

void ReplayTransport::restart()
{
    m_file.seek(0);
    m_decoder.reset();
    m_ready.clear();
    m_emitted = 0;
    m_finishedAnnounced = false;
    if (m_clock)
        m_clock->restart();
}

void ReplayTransport::sendBytes(const QByteArray &bytes)
{
    Q_UNUSED(bytes)
    // 回放模式不驱动物理设备；下行命令仅记录，避免误动作
}

bool ReplayTransport::fillReady()
{
    while (m_ready.isEmpty()) {
        if (m_file.atEnd()) {
            if (!m_loop)
                return false;
            restart();
            if (m_file.atEnd())
                return false;
        }
        const QByteArray block = m_file.read(kReadBlock);
        if (block.isEmpty() && m_file.atEnd()) {
            if (!m_loop)
                return false;
            restart();
            continue;
        }
        m_decoder.push(block);
        for (DecodedFrame &f : m_decoder.takeFrames())
            m_ready.append(f);
        if (block.isEmpty())
            return false;
    }
    return true;
}

void ReplayTransport::tick()
{
    if (!m_file.isOpen())
        return;

    const double elapsedS = double(m_clock->elapsed()) / 1000.0;
    const quint64 want = quint64(elapsedS * m_fps);

    while (m_emitted < want) {
        if (!fillReady()) {
            if (!m_finishedAnnounced) {
                m_finishedAnnounced = true;
                emit errorText(QStringLiteral("回放结束（共 %1 帧）").arg(m_emitted));
                emit countersUpdated(m_emitted, m_decoder.stats().droppedFrames,
                                     m_decoder.stats().bytesIn);
            }
            // 一次性把剩余帧发完并停表
            while (!m_ready.isEmpty()) {
                emit frameDecoded(m_ready.takeFirst());
                ++m_emitted;
            }
            if (m_timer)
                m_timer->stop();
            return;
        }
        emit frameDecoded(m_ready.takeFirst());
        ++m_emitted;
    }

    if (m_emitted > 0 && (m_emitted % 250) == 0) {
        emit countersUpdated(m_emitted, m_decoder.stats().droppedFrames, m_decoder.stats().bytesIn);
    }
}

} // namespace smd
