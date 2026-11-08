// 配置持久化（QSettings，无外部依赖）
#pragma once

#include <QString>

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
};

} // namespace smd
