#include "floatingball.h"
#include "clipboardbubble.h"
#include "panelbackdrop.h"
#include "quickdeck.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

QString singleInstanceServerName()
{
#ifdef Q_OS_MACOS
    const QString dir = QDir::homePath() + QStringLiteral("/Library/Caches/dev.floatingball.FloatingBall");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/instance.sock");
    if (path.toUtf8().size() < 100) return path;
    return QStringLiteral("/tmp/floatingball-%1.sock").arg(static_cast<qlonglong>(::getuid()));
#elif defined(Q_OS_UNIX)
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (!dir.isEmpty()) {
        QDir().mkpath(dir);
        const QString path = dir + QStringLiteral("/floatingball.sock");
        if (path.toUtf8().size() < 100) return path;
    }
    return QStringLiteral("/tmp/floatingball-%1.sock").arg(static_cast<qlonglong>(::getuid()));
#else
    return QStringLiteral("dev.floatingball.FloatingBall.instance");
#endif
}

bool forwardToRunningInstance(const QString &serverName, const QByteArray &command)
{
    QLocalSocket probe;
    probe.connectToServer(serverName, QIODevice::ReadWrite);
    if (!probe.waitForConnected(250)) return false;
    probe.write(command + '\n');
    probe.flush();
    probe.waitForBytesWritten(250);
    probe.waitForReadyRead(400);
    probe.disconnectFromServer();
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Silence benign QtSvg parse warnings from third-party system icon themes
    // (e.g. Breeze camera-photo/cups/printer SVGs rendered in AppLauncher).
    QLoggingCategory::setFilterRules(QStringLiteral("qt.svg.warning=false"));

    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
    PanelBackdrop::configureAccessoryApp();
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
    QCommandLineOption newInstanceOption("new-instance", "Allow running alongside an existing FloatingBall instance.");
    parser.addOptions({clipboardOption, deckOption, captureOption, recordOption, newInstanceOption});
    parser.process(a);

    QByteArray launchCmd = QByteArrayLiteral("show");
    if (parser.isSet(recordOption)) launchCmd = QByteArrayLiteral("record");
    else if (parser.isSet(captureOption)) launchCmd = QByteArrayLiteral("capture");
    else if (parser.isSet(clipboardOption)) launchCmd = QByteArrayLiteral("show-clipboard");
    else if (parser.isSet(deckOption)) launchCmd = QByteArrayLiteral("show-deck");

    const bool singleInstance = !parser.isSet(newInstanceOption) &&
                                qEnvironmentVariableIsEmpty("FLOATINGBALL_ALLOW_MULTI_INSTANCE");
    const QString serverName = singleInstance ? singleInstanceServerName() : QString();
    if (singleInstance && forwardToRunningInstance(serverName, launchCmd)) {
        return 0;
    }

    QLocalServer instanceServer;
    if (singleInstance) {
        instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
        if (!instanceServer.listen(serverName)) {
            QLocalServer::removeServer(serverName);
            instanceServer.listen(serverName);
        }
    }

    FloatingBall w;
    w.show();

    auto dispatchCommand = [&w](const QByteArray &cmd) {
        if (cmd == "record") {
            w.recordScreen();
        } else if (cmd == "capture") {
            w.captureRegion();
        } else if (cmd == "show-clipboard") {
            w.toggleClipboardBubble();
        } else if (cmd == "show-deck") {
            w.toggleQuickDeck();
        } else {
            w.restoreAllScreenBalls();
            w.expandFromEdge();
            w.show();
            w.raise();
        }
    };

    if (singleInstance && instanceServer.isListening()) {
        QObject::connect(&instanceServer, &QLocalServer::newConnection, &w, [&instanceServer, dispatchCommand] {
            while (QLocalSocket *client = instanceServer.nextPendingConnection()) {
                auto handleRead = [client, dispatchCommand]() {
                    if (client->property("fbHandled").toBool()) return;
                    client->setProperty("fbHandled", true);
                    const QByteArray cmd = client->readAll().trimmed();
                    client->write("ok\n");
                    client->flush();
                    client->disconnectFromServer();
                    dispatchCommand(cmd);
                };
                QObject::connect(client, &QLocalSocket::readyRead, client, handleRead);
                QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                if (client->bytesAvailable() > 0) handleRead();
            }
        });
        QObject::connect(&a, &QCoreApplication::aboutToQuit, [&instanceServer, serverName] {
            instanceServer.close();
            QLocalServer::removeServer(serverName);
        });
    }

    if (launchCmd != "show") {
        QTimer::singleShot(0, &w, [dispatchCommand, launchCmd] { dispatchCommand(launchCmd); });
    }

    return a.exec();
}
