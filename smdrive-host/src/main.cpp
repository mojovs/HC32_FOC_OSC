#include "protocol/frame.h"
#include "transport/replay_transport.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("smdrive-host"));
    QApplication::setOrganizationName(QStringLiteral("SMotorDrive"));
    QApplication::setApplicationVersion(QStringLiteral("1.0.0"));   // 与 installer 版本一致，用于「帮助 → 关于」
    qRegisterMetaType<smd::DecodedFrame>("smd::DecodedFrame");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SMotorDrive 上位机（Qt 5.15.2）"));
    parser.addHelpOption();
    const QCommandLineOption replayOpt(QStringLiteral("replay"),
                                       QStringLiteral("启动即回放留档的 .bin 原始流"), QStringLiteral("file"));
    const QCommandLineOption fpsOpt(QStringLiteral("fps"),
                                    QStringLiteral("回放帧率（帧/s）"), QStringLiteral("fps"),
                                    QStringLiteral("250"));
    const QCommandLineOption rawOpt(QStringLiteral("rec-raw"),
                                    QStringLiteral("启动即记录原始流到指定文件"), QStringLiteral("file"));
    const QCommandLineOption csvOpt(QStringLiteral("rec-csv"),
                                    QStringLiteral("启动即记录 CSV 到指定文件"), QStringLiteral("file"));
    const QCommandLineOption closeOpt(QStringLiteral("auto-close"),
                                      QStringLiteral("N 毫秒后自动退出（自动化验证用）"), QStringLiteral("ms"),
                                      QStringLiteral("0"));
    const QCommandLineOption shotOpt(QStringLiteral("shot"),
                                     QStringLiteral("把界面截图存成 PNG（开发/验证用）"),
                                     QStringLiteral("file"));
    const QCommandLineOption shotDelayOpt(QStringLiteral("shot-delay"),
                                          QStringLiteral("截图前等待毫秒数"), QStringLiteral("ms"),
                                          QStringLiteral("1500"));
    const QCommandLineOption gainOpt(QStringLiteral("ch-gain"),
                                     QStringLiteral("启动时预设通道显示增益，如 \"6=0.002,7=0.001\"（通道号=增益）"),
                                     QStringLiteral("list"));
    const QCommandLineOption rateOpt(QStringLiteral("sample-rate"),
                                     QStringLiteral("采样率 Hz（决定时间轴）"), QStringLiteral("hz"),
                                     QStringLiteral("10000"));
    const QCommandLineOption winOpt(QStringLiteral("window"),
                                    QStringLiteral("显示窗口秒数"), QStringLiteral("sec"),
                                    QStringLiteral("0.2"));
    const QCommandLineOption uiModeOpt(QStringLiteral("ui-mode"),
                                       QStringLiteral("启动时的右栏模式: control | serial"),
                                       QStringLiteral("mode"), QStringLiteral("control"));
    parser.addOptions({replayOpt, fpsOpt, rawOpt, csvOpt, closeOpt, shotOpt, shotDelayOpt, gainOpt,
                       rateOpt, winOpt, uiModeOpt});
    parser.process(app);

    smd::MainWindow::Options opt;
    opt.replayPath = parser.value(replayOpt);
    opt.replayFps = parser.value(fpsOpt).toDouble();
    opt.recordRawPath = parser.value(rawOpt);
    opt.recordCsvPath = parser.value(csvOpt);
    opt.autoCloseMs = parser.value(closeOpt).toInt();
    opt.screenshotPath = parser.value(shotOpt);
    opt.screenshotDelayMs = parser.value(shotDelayOpt).toInt();
    opt.sampleRateHz = parser.value(rateOpt).toInt();
    opt.windowSeconds = parser.value(winOpt).toDouble();
    opt.uiMode = parser.value(uiModeOpt);

    for (const QString &item : parser.value(gainOpt).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList kv = item.split(QLatin1Char('='));
        if (kv.size() == 2) {
            bool okIdx = false, okVal = false;
            const int idx = kv.at(0).trimmed().toInt(&okIdx);
            const double val = kv.at(1).trimmed().toDouble(&okVal);
            if (okIdx && okVal && val > 0.0)
                opt.channelGains.append(qMakePair(idx, val));
        }
    }

    smd::MainWindow window(opt);
    window.show();
    return app.exec();
}
