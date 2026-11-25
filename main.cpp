#include "floatingball.h"
#include "clipboardbubble.h"
#include "quickdeck.h"

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
        "Touch-first desktop floating ball (2026 Liquid Glass Control Center, App Launcher & Clipboard)");
    parser.addHelpOption();
    QCommandLineOption clipboardOption("show-clipboard", "Open the touch clipboard bubble on launch.");
    QCommandLineOption deckOption("show-deck", "Open the touch quick deck on launch.");
    QCommandLineOption captureOption("capture", "Trigger native OS screenshot on launch.");
    QCommandLineOption recordOption("record", "Trigger native OS screen recording on launch.");
    parser.addOptions({clipboardOption, deckOption, captureOption, recordOption});
    parser.process(a);

    FloatingBall w;
    w.show();

    if (parser.isSet(recordOption)) {
        QTimer::singleShot(0, &w, [&w] { w.recordScreen(); });
    } else if (parser.isSet(captureOption)) {
        QTimer::singleShot(0, &w, [&w] { w.captureRegion(); });
    } else if (parser.isSet(clipboardOption)) {
        QTimer::singleShot(0, &w, [&w] { w.toggleClipboardBubble(); });
    } else if (parser.isSet(deckOption)) {
        QTimer::singleShot(0, &w, [&w] { w.toggleQuickDeck(); });
    }

    return a.exec();
}
