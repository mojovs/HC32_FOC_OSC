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
    const QCommandLineOption rateOpt(QStringLiteral("sample-rate"),
                                     QStringLiteral("采样率 Hz（决定时间轴）"), QStringLiteral("hz"),
                                     QStringLiteral("10000"));
    const QCommandLineOption winOpt(QStringLiteral("window"),
                                    QStringLiteral("显示窗口秒数"), QStringLiteral("sec"),
                                    QStringLiteral("0.2"));
    parser.addOptions({replayOpt, fpsOpt, rawOpt, csvOpt, closeOpt, rateOpt, winOpt});
    parser.process(app);

    smd::MainWindow::Options opt;
    opt.replayPath = parser.value(replayOpt);
    opt.replayFps = parser.value(fpsOpt).toDouble();
    opt.recordRawPath = parser.value(rawOpt);
    opt.recordCsvPath = parser.value(csvOpt);
    opt.autoCloseMs = parser.value(closeOpt).toInt();
    opt.sampleRateHz = parser.value(rateOpt).toInt();
    opt.windowSeconds = parser.value(winOpt).toDouble();

    smd::MainWindow window(opt);
    window.show();
    return app.exec();
}
