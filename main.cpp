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
    parser.addOption(directoryOption);
    parser.addOption(previewOption);
    parser.process(a);
    FloatingBall w(nullptr, parser.value(directoryOption));

    w.show();
    if (parser.isSet(previewOption)) QTimer::singleShot(0, &w, [&w] { w.showScreenshots(); });
    return a.exec();
}
