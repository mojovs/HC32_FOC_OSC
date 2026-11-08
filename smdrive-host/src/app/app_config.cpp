#include "app/app_config.h"

#include <QSettings>
#include <QStandardPaths>

namespace smd {

namespace {
QSettings settings()
{
    return QSettings(QStringLiteral("SMotorDrive"), QStringLiteral("smdrive-host"));
}
} // namespace

QString AppConfig::lastPort() { return settings().value(QStringLiteral("port/name")).toString(); }
void AppConfig::setLastPort(const QString &name) { settings().setValue(QStringLiteral("port/name"), name); }

int AppConfig::baudRate() { return settings().value(QStringLiteral("port/baud"), 9600).toInt(); }
void AppConfig::setBaudRate(int baud) { settings().setValue(QStringLiteral("port/baud"), baud); }

int AppConfig::sampleRateHz() { return settings().value(QStringLiteral("data/sampleRateHz"), 10000).toInt(); }
void AppConfig::setSampleRateHz(int hz) { settings().setValue(QStringLiteral("data/sampleRateHz"), hz); }

double AppConfig::windowSeconds() { return settings().value(QStringLiteral("view/windowSeconds"), 0.2).toDouble(); }
void AppConfig::setWindowSeconds(double sec) { settings().setValue(QStringLiteral("view/windowSeconds"), sec); }

double AppConfig::historySeconds() { return settings().value(QStringLiteral("data/historySeconds"), 20.0).toDouble(); }
void AppConfig::setHistorySeconds(double sec) { settings().setValue(QStringLiteral("data/historySeconds"), sec); }

QString AppConfig::lastReportDir()
{
    const QString def = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return settings().value(QStringLiteral("record/dir"), def).toString();
}
void AppConfig::setLastReportDir(const QString &dir) { settings().setValue(QStringLiteral("record/dir"), dir); }

QString AppConfig::lastParamProfile() { return settings().value(QStringLiteral("param/profile")).toString(); }
void AppConfig::setLastParamProfile(const QString &path) { settings().setValue(QStringLiteral("param/profile"), path); }

QStringList AppConfig::sendHistory()
{
    return settings().value(QStringLiteral("serial/sendHistory")).toStringList().mid(0, 20);
}
void AppConfig::setSendHistory(const QStringList &history)
{
    const QStringList trimmed = history.mid(0, 20);   // mid() 是 QList<QString>，先转回 QStringList
    settings().setValue(QStringLiteral("serial/sendHistory"), trimmed);
}

} // namespace smd
