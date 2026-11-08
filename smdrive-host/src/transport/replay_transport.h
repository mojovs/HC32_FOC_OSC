// 离线回放：把留档的 .bin 原始字节流按指定帧率喂给同一个解码器（计划书 §5.4）。
#pragma once

#include "protocol/frame_decoder.h"
#include "transport/itransport.h"

#include <QFile>
#include <QVector>

QT_BEGIN_NAMESPACE
class QTimer;
class QElapsedTimer;
QT_END_NAMESPACE

namespace smd {

class ReplayTransport : public ITransport
{
    Q_OBJECT
public:
    explicit ReplayTransport(QObject *parent = nullptr);
    ~ReplayTransport() override;

    void setSourcePath(const QString &path) { m_path = path; }
    QString sourcePath() const { return m_path; }
    void setFramesPerSecond(double fps) { m_fps = qMax(1.0, fps); }
    void setLoop(bool loop) { m_loop = loop; }

    QString describe() const override;
    bool isOpen() const override { return m_file.isOpen(); }
    quint64 framesEmitted() const { return m_emitted; }

public slots:
    void open() override;
    void close() override;
    void sendBytes(const QByteArray &bytes) override;   // 回放模式不接受下行
    void restart();                                     // 回到文件开头

private slots:
    void tick();

private:
    bool fillReady();

    QString m_path;
    QFile m_file;
    QTimer *m_timer = nullptr;
    QElapsedTimer *m_clock = nullptr;
    QVector<DecodedFrame> m_ready;
    FrameDecoder m_decoder;
    double m_fps = 250.0;
    bool m_loop = false;
    quint64 m_emitted = 0;
    bool m_finishedAnnounced = false;
};

} // namespace smd
