#include "floatingball.h"
#include "clipboardbubble.h"
#include "quickdeck.h"
#include "screenshotpanel.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
#ifndef Q_OS_MACOS
    // Cocoa reads the layered bundle icon; a Qt pixmap would flatten its variants.
    QIcon appIcon;
    for (int size : {16, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QStringLiteral(":/icons/floatingball-%1.png").arg(size), QSize(size, size));
    a.setWindowIcon(appIcon);
#endif
#ifdef Q_OS_LINUX
    QGuiApplication::setDesktopFileName(QStringLiteral("floatingball"));
#endif

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
    parser.setApplicationDescription(
        "Touch-first desktop floating ball (Xiaomi Mi 5 Quick Ball + fooView Crosshair)");
    parser.addHelpOption();
    QCommandLineOption directoryOption("screenshot-directory", "Use a screenshot folder for this session.", "path");
    QCommandLineOption previewOption("show-screenshots", "Open the screenshot workbench on launch.");
    QCommandLineOption clipboardOption("show-clipboard", "Open the touch clipboard bubble on launch.");
    QCommandLineOption deckOption("show-deck", "Open the touch quick deck on launch.");
    QCommandLineOption captureOption("capture", "Capture a screen region on launch.");
    QCommandLineOption modeOption("capture-mode", "Screenshot target: region, window, screen or desktop.", "mode", "region");
    QCommandLineOption delayOption("capture-delay", "Screenshot delay in seconds (0-30).", "seconds", "0");
    QCommandLineOption cursorOption("capture-cursor", "Include the pointer where the capture mode supports it.");
    QCommandLineOption recordOption("record", "Record screen or window on launch.", "mode");
    parser.addOptions({directoryOption, previewOption, clipboardOption, deckOption,
                       captureOption, modeOption, delayOption, cursorOption, recordOption});
    parser.process(a);

    FloatingBall w(nullptr, parser.value(directoryOption));
    w.show();

    CaptureOptions options;
    const QStringList modes{"region", "window", "screen", "desktop"};
    const int mode = modes.indexOf(parser.value(modeOption));
    bool delayValid = false;
    const int delay = parser.value(delayOption).toInt(&delayValid);
    if (mode < 0 || !delayValid || delay < 0 || delay > 30 ||
        (parser.isSet(recordOption) && !QStringList{"screen", "window"}.contains(parser.value(recordOption)))) {
        parser.showHelp(1);
    }
    options.mode = CaptureOptions::Mode(mode);
    options.delaySeconds = delay;
    options.includeCursor = parser.isSet(cursorOption);

    if (parser.isSet(recordOption)) {
        QTimer::singleShot(0, &w, [&w, &parser, &recordOption] {
            w.recordScreen(parser.value(recordOption) == QLatin1String("window"));
        });
    } else if (parser.isSet(captureOption)) {
        QTimer::singleShot(0, &w, [&w, options] { w.takeScreenshot(options); });
    } else if (parser.isSet(previewOption)) {
        QTimer::singleShot(0, &w, [&w] { w.showScreenshots(); });
    } else if (parser.isSet(clipboardOption)) {
        QTimer::singleShot(0, &w, [&w] { w.toggleClipboardBubble(); });
    } else if (parser.isSet(deckOption)) {
        QTimer::singleShot(0, &w, [&w] { w.toggleQuickDeck(); });
    }

    return a.exec();
}
