// 配置持久化（QSettings，无外部依赖）
#pragma once

#include <QString>
#include <QStringList>

namespace smd {

class AppConfig
{
public:
    static QString lastPort();
    static void setLastPort(const QString &name);

    static int baudRate();
    static void setBaudRate(int baud);

    static int sampleRateHz();
    static void setSampleRateHz(int hz);

    static double windowSeconds();
    static void setWindowSeconds(double sec);

    static double historySeconds();
    static void setHistorySeconds(double sec);

    static QString lastReportDir();
    static void setLastReportDir(const QString &dir);

    static QString lastParamProfile();
    static void setLastParamProfile(const QString &path);

    // 串口调试助手的发送历史（最新在前，最多保留 20 条）
    static QStringList sendHistory();
    static void setSendHistory(const QStringList &history);
};

} // namespace smd
