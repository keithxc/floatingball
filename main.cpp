#include "floatingball.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "FloatingBall_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    QCommandLineParser parser;
    parser.setApplicationDescription("Floating shortcuts and recent screenshots");
    parser.addHelpOption();
    QCommandLineOption directoryOption("screenshot-directory", "Use a screenshot folder for this session.", "path");
    QCommandLineOption previewOption("show-screenshots", "Open the screenshot panel on launch.");
    QCommandLineOption captureOption("capture", "Capture a screen region on launch.");
    QCommandLineOption modeOption("capture-mode", "Screenshot target: region, window, screen or desktop.", "mode", "region");
    QCommandLineOption delayOption("capture-delay", "Screenshot delay in seconds (0-30).", "seconds", "0");
    QCommandLineOption cursorOption("capture-cursor", "Include the pointer where the capture mode supports it.");
    QCommandLineOption recordOption("record", "Record screen or window on launch.", "mode");
    parser.addOption(directoryOption);
    parser.addOption(previewOption);
    parser.addOption(captureOption);
    parser.addOptions({modeOption,delayOption,cursorOption,recordOption});
    parser.process(a);
    FloatingBall w(nullptr, parser.value(directoryOption));

    w.show();
    CaptureOptions options;const QStringList modes{"region","window","screen","desktop"};
    const int mode=modes.indexOf(parser.value(modeOption));bool delayValid=false;const int delay=parser.value(delayOption).toInt(&delayValid);
    if(mode<0 || !delayValid || delay<0 || delay>30 || (parser.isSet(recordOption)&&!QStringList{"screen","window"}.contains(parser.value(recordOption))))parser.showHelp(1);
    options.mode=CaptureOptions::Mode(mode);options.delaySeconds=delay;options.includeCursor=parser.isSet(cursorOption);
    if(parser.isSet(recordOption))QTimer::singleShot(0,&w,[&w,&parser,&recordOption]{w.recordScreen(parser.value(recordOption)=="window");});
    else if (parser.isSet(captureOption)) QTimer::singleShot(0, &w, [&w,options] { w.takeScreenshot(options); });
    else if (parser.isSet(previewOption)) QTimer::singleShot(0, &w, [&w] { w.showScreenshots(); });
    return a.exec();
}
