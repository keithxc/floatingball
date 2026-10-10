#import <AppKit/AppKit.h>
#include <QApplication>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QList>
#include <QProcess>
#include <QTimer>
#include <QWidget>
#include <cstdio>

void check(bool ok, const char *message);

namespace {
void waitForNativeEvents(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}
NSWindow *nativeWindow(QWidget *widget) {
    // Qt Cocoa's WId is its NSView; inspect only this test's own windows.
    return reinterpret_cast<NSView *>(widget->winId()).window;
}
}

void checkNativeFloatingWindows(const QList<QWidget *> &windows, QWidget *panel) {
    QWidget ordinary;
    ordinary.setWindowTitle("FloatingBall native stacking fixture");
    ordinary.setGeometry(windows.first()->geometry().adjusted(-40,-40,40,40));
    ordinary.show();
    [NSApp activateIgnoringOtherApps:YES];
    panel->activateWindow();
    waitForNativeEvents(150);
    check(NSApp.active, "native test application became active before deactivation");

    bool configured=true;
    for(auto widget:windows) {
        NSWindow *window=nativeWindow(widget);
        configured=configured && window && !window.hidesOnDeactivate
            && window.level>nativeWindow(&ordinary).level;
        printf("NATIVE_WINDOW: level=%ld hidesOnDeactivate=%d visible=%d\n",
            static_cast<long>(window.level),window.hidesOnDeactivate,window.visible);
    }
    check(configured,"Cocoa ball, satellites and screenshot have persistent elevated native windows");
    [nativeWindow(&ordinary) orderFront:nil];
    waitForNativeEvents(50);
    // orderedWindows excludes NSPanel. This API includes this process's panels
    // in screen z-order without enumerating windows belonging to other apps.
    NSArray<NSNumber *> *ordered=[NSWindow windowNumbersWithOptions:0];
    const NSUInteger ordinaryIndex=[ordered indexOfObject:@(nativeWindow(&ordinary).windowNumber)];
    bool above=ordinaryIndex!=NSNotFound;
    for(auto widget:windows) {
        const NSUInteger index=[ordered indexOfObject:@(nativeWindow(widget).windowNumber)];
        above=above && index!=NSNotFound && index<ordinaryIndex;
    }
    check(above,"bringing a normal native window forward leaves floating windows above it");

    // Give focus to a task-owned second process instead of changing or clicking
    // an application belonging to the user. It exits automatically as well.
    QProcess focusFixture;
    focusFixture.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--native-focus-fixture")});
    bool started=focusFixture.waitForStarted(1000);
    QByteArray output;
    QElapsedTimer deadline;deadline.start();
    while(started&&!output.contains("FOCUS_FIXTURE_READY")&&deadline.elapsed()<3000) {
        waitForNativeEvents(20);
        output+=focusFixture.readAllStandardOutput();
        if(focusFixture.state()==QProcess::NotRunning)break;
    }
    printf("FOCUS_FIXTURE_PID: %lld\n",static_cast<long long>(focusFixture.processId()));
    waitForNativeEvents(150);
    const bool switched=started&&output.contains("FOCUS_FIXTURE_READY")&&!NSApp.active;
    bool retained=true;
    ordered=[NSWindow windowNumbersWithOptions:0];
    const NSUInteger inactiveOrdinaryIndex=[ordered indexOfObject:@(nativeWindow(&ordinary).windowNumber)];
    for(auto widget:windows) {
        NSWindow *window=nativeWindow(widget);
        const NSUInteger index=[ordered indexOfObject:@(window.windowNumber)];
        retained=retained && widget->isVisible() && window.visible
            && window.level>nativeWindow(&ordinary).level
            && index!=NSNotFound && inactiveOrdinaryIndex!=NSNotFound && index<inactiveOrdinaryIndex;
    }
    const qint64 fixturePid=focusFixture.processId();
    focusFixture.terminate();
    if(!focusFixture.waitForFinished(1000)){focusFixture.kill();focusFixture.waitForFinished(1000);}
    printf("FOCUS_FIXTURE_CLEANUP: pid=%lld stopped=%d\n",static_cast<long long>(fixturePid),focusFixture.state()==QProcess::NotRunning);
    check(focusFixture.state()==QProcess::NotRunning,"owned focus fixture exited and was reaped");
    check(switched,"native test application deactivated when a second app became active");
    check(retained,"Cocoa floating windows stay visible and elevated after application deactivation");
    ordinary.close();
    [NSApp activateIgnoringOtherApps:YES];
    panel->activateWindow();
    waitForNativeEvents(150);
}

int runNativeFocusFixture() {
    QWidget window;
    window.setWindowTitle("FloatingBall application-switch fixture");
    window.setGeometry(280,280,360,200);
    window.show();
    [NSApp activateIgnoringOtherApps:YES];
    window.activateWindow();
    QTimer::singleShot(150,[]{printf("FOCUS_FIXTURE_READY\n");fflush(stdout);});
    QTimer::singleShot(4000,qApp,&QApplication::quit);
    return qApp->exec();
}
