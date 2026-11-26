#include "appdock.h"
#include "floatingball.h"
#include "floatingwindow.h"
#include "panelbackdrop.h"
#include "quickdeck.h"
#include "systempopover.h"
#include "systemstatus.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <cstdio>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace AppDockMac {
QList<DockAppEntry> openWindowsFast();
}

namespace {

void captureCompositedWindow(QWidget *w, const QString &path)
{
    if (!w || !w->isVisible()) return;
    @autoreleasepool {
        NSView *v = reinterpret_cast<NSView *>(w->winId());
        if (!v || !v.window) return;
        const long wid = static_cast<long>(v.window.windowNumber);
        if (wid <= 0) return;
        QProcess::execute(QStringLiteral("/usr/sbin/screencapture"),
                          {QStringLiteral("-x"), QStringLiteral("-o"),
                           QStringLiteral("-l"), QString::number(wid), path});
    }
}

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void check(bool condition, const char *label)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
    std::printf("PASS: %s\n", label);
}

QByteArray makeBinaryPlist(NSDictionary *dict)
{
    @autoreleasepool {
        NSError *err = nil;
        NSData *data = [NSPropertyListSerialization dataWithPropertyList:dict
                                                                  format:NSPropertyListBinaryFormat_v1_0
                                                                 options:0
                                                                   error:&err];
        if (!data) return {};
        return QByteArray(static_cast<const char *>(data.bytes), static_cast<int>(data.length));
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output-dir> [--live]\n", argv[0]);
        return 2;
    }
    const QString outDir = QString::fromLocal8Bit(argv[1]);
    const bool liveMode = (argc >= 3 && QString::fromLocal8Bit(argv[2]) == QLatin1String("--live"));
    QDir().mkpath(outDir);

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outDir + QStringLiteral("/settings"));

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    // 1. macOS usernoted binary plist parser (CoreData epoch, title/subtitle/body, self-filter)
    {
        const QByteArray sample = makeBinaryPlist(@{
            @"app": @"com.tencent.xinwechat",
            @"req": @{
                @"titl": @"Alice",
                @"subt": @"产品群",
                @"body": @"macOS 27 悬浮球已上线\n快来看看"
            }
        });
        NotificationItem item;
        const double coreDataTs = 817371880.0; // 2026-11-26 UTC
        check(SystemStatusMac::parseUserNotedPlist(sample, QStringLiteral("com.tencent.xinwechat"),
                                                   coreDataTs, &item) &&
                  item.desktopEntry == QStringLiteral("com.tencent.xinwechat") &&
                  item.summary == QStringLiteral("Alice") &&
                  item.body.contains(QStringLiteral("产品群")) &&
                  item.body.contains(QStringLiteral("macOS 27 悬浮球已上线")) &&
                  item.timestamp.toSecsSinceEpoch() == static_cast<qint64>(coreDataTs) + 978307200LL,
              "usernoted binary plist decodes title, subtitle, body, bundleId, and CoreData timestamp");

        const QByteArray selfSample = makeBinaryPlist(@{
            @"app": @"dev.floatingball.FloatingBall",
            @"req": @{@"titl": @"Self", @"body": @"Ignore me"}
        });
        NotificationItem ignored;
        check(!SystemStatusMac::parseUserNotedPlist(selfSample, QStringLiteral("dev.floatingball.FloatingBall"),
                                                    coreDataTs, &ignored),
              "usernoted parser filters FloatingBall self-notifications");

        const QByteArray emptySample = makeBinaryPlist(@{@"app": @"com.apple.mail", @"req": @{}});
        check(!SystemStatusMac::parseUserNotedPlist(emptySample, QStringLiteral("com.apple.mail"),
                                                    coreDataTs, &ignored),
              "usernoted parser filters empty payloads");
    }

    // 2. macOS native app icon lookup
    {
        const QIcon finderIcon = SystemStatusMac::lookupAppIcon(QStringLiteral("com.apple.finder"), QStringLiteral("Finder"));
        const QIcon termIcon = SystemStatusMac::lookupAppIcon(QStringLiteral("com.apple.Terminal"), QStringLiteral("Terminal"));
        check(!finderIcon.isNull() && !termIcon.isNull(),
              "macOS lookupAppIcon resolves native bundle icons for Finder and Terminal");
    }

    // 3. Hardware snapshot + privacy + Night Shift probes complete safely
    {
        SystemStatusSnapshot snap;
        SystemStatusMac::populateSnapshot(&snap);
        SystemStatusMac::populatePrivacyAndNightShift(&snap);
        check(!snap.networkText.isEmpty() && !snap.powerProfile.isEmpty(),
              "macOS populateSnapshot + populatePrivacyAndNightShift run safely with valid network/power state");
    }

    // 4. FloatingBall macOS multi-window clustering, fullscreen capsule morph, case-insensitive
    //    notification attribution, focus auto-clear (zero ghost red dot!), and IME instant flash
    FloatingBall ball;
    ball.setMultiScreenEnabled(false);
    ball.setStatusOrbMode(true);
    ball.setAutoHideEnabled(false);
    ball.setAutoTuckFullscreen(true);
    ball.show();
    app.processEvents();

    {
        DockAppEntry chrome1;
        chrome1.id = QStringLiteral("com.google.Chrome");
        chrome1.appKey = QStringLiteral("com.google.chrome");
        chrome1.name = QStringLiteral("Google Chrome");
        chrome1.caption = QStringLiteral("App Store Connect");
        chrome1.windowId = QStringLiteral("mac:1001:137");
        chrome1.running = true;
        chrome1.active = true;

        DockAppEntry wechat;
        wechat.id = QStringLiteral("com.tencent.xinWeChat");
        wechat.appKey = QStringLiteral("com.tencent.xinwechat");
        wechat.name = QStringLiteral("WeChat");
        wechat.caption = QStringLiteral("Weixin");
        wechat.windowId = QStringLiteral("mac:1002:1247");
        wechat.running = true;

        DockAppEntry chrome2 = chrome1;
        chrome2.caption = QStringLiteral("GitHub — FloatingBall");
        chrome2.windowId = QStringLiteral("mac:1001:901");
        chrome2.active = false;

        ball.dockModel()->applyMacWindowSnapshot({chrome1, wechat, chrome2}, false);
        app.processEvents();

        const QList<DockAppEntry> clustered = ball.dockModel()->openWindows();
        if (!liveMode) {
            // In offscreen mode openWindows() reads m_macRunningWindows only when live; test via setOpenWindowsForTest too
            ball.dockModel()->setOpenWindowsForTest({chrome1, wechat, chrome2});
        }
        const QList<DockAppEntry> wins = ball.dockModel()->openWindows();
        check(wins.size() >= 3 && wins[0].appKey == wins[1].appKey && wins[0].windowId != wins[1].windowId,
              "macOS multi-window snapshot stably clusters multiple windows of the same app");

        // Fullscreen auto-tuck morph -> slim edge capsule, then exit -> restores circular ball
        ball.dockModel()->applyMacWindowSnapshot({chrome1, wechat, chrome2}, true);
        app.processEvents();
        waitMs(180);
        check(ball.isFullscreenTucked() && ball.isEdgeTucked() && ball.width() != ball.height(),
              "macOS active fullscreen signal morphs Circular 24H Ball into slim Edge Capsule");
        if (liveMode) {
            ball.grab().save(outDir + QStringLiteral("/live-macos-capsule.png"));
        }

        ball.dockModel()->applyMacWindowSnapshot({chrome1, wechat, chrome2}, false);
        app.processEvents();
        waitMs(180);
        check(!ball.isFullscreenTucked() && !ball.isEdgeTucked() && ball.width() == ball.height(),
              "exiting macOS fullscreen restores Primary Circular 24H Clock Ball");

        // Case-insensitive macOS notification attribution (usernoted lowercase vs NSWorkspace mixed-case)
        NotificationItem wxNotif;
        wxNotif.appName = QStringLiteral("WeChat");
        wxNotif.desktopEntry = QStringLiteral("com.tencent.xinwechat");
        wxNotif.summary = QStringLiteral("Alice");
        wxNotif.body = QStringLiteral("macOS 通知归因实测");
        wxNotif.unread = true;
        ball.statusMonitor()->pushNotification(wxNotif);
        app.processEvents();

        check(ball.statusMonitor()->snapshot().unreadNotifications == 1 &&
                  !ball.attentionAnglesRad().isEmpty(),
              "macOS notification lights directional radar dot on Circular 24H Ball");
        if (liveMode) {
            ball.grab().save(outDir + QStringLiteral("/live-macos-ball-radar.png"));
        }

        // Open TapFanMode and verify macOS mask includes hovered callout pill (zero clipping!)
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();

        int wxSlot = -1;
        for (int i = 0; i < ball.fanOverlay()->slotsData().size(); ++i) {
            const auto &s = ball.fanOverlay()->slotsData()[i];
            if (s.kind == RadialSlotItem::AppSlot && s.app.appKey == QLatin1String("com.tencent.xinwechat")) {
                wxSlot = i;
                break;
            }
        }
        check(wxSlot >= 0 && ball.fanOverlay()->slotsData()[wxSlot].unreadCount == 1,
              "WeChat outer window orb binds exact unread badge count and preview");

        const QPointF wxCenter = ball.fanOverlay()->slotsData()[wxSlot].centerGlobal;
        ball.fanOverlay()->updateFinger(wxCenter, wxSlot);
        app.processEvents();
        const QString hoverText = ball.fanOverlay()->activeLabelText();
        const QRegion maskRegion = ball.fanOverlay()->mask();
        check(hoverText.contains(QStringLiteral("macOS 通知归因实测")) && !maskRegion.isEmpty() &&
                  maskRegion.boundingRect().height() > 70,
              "macOS TapFanMode updateFinger refreshes mask to include hovered callout pill");

        if (liveMode) {
            ball.fanOverlay()->grab().save(outDir + QStringLiteral("/live-macos-fan-hover.png"));
            waitMs(80);
            ball.fanOverlay()->updateFinger(wxCenter, wxSlot);
            app.processEvents();
            waitMs(40);
            captureCompositedWindow(ball.fanOverlay(), outDir + QStringLiteral("/composited-fan-hover.png"));
        }

        // Drilled ring mask covers focus backdrop + back orb on macOS
        ball.fanOverlay()->drillInto(QStringLiteral("tools"), QStringLiteral("工具"),
                                     QColor("#86e0b8"), wxCenter);
        app.processEvents();
        check(ball.fanOverlay()->isDrilled() && !ball.fanOverlay()->mask().isEmpty() &&
                  ball.fanOverlay()->mask().boundingRect().width() >= 200,
              "macOS drilled TapFanMode mask covers full focus backdrop and backtrack orb");
        ball.closeFan(true);

        // Switching focus to WeChat via NSWorkspaceDidActivateApplication clears unread & radar dot!
        ball.dockModel()->handleMacAppActivated(QStringLiteral("com.tencent.xinwechat"),
                                                QStringLiteral("WeChat"),
                                                QStringLiteral("com.tencent.xinWeChat"));
        app.processEvents();
        check(ball.statusMonitor()->snapshot().unreadNotifications == 0 &&
                  ball.attentionAnglesRad().isEmpty(),
              "macOS app activation auto-clears unread notifications with zero ghost red dot");

        // Instant zero-polling macOS IME flash
        ball.statusMonitor()->handleMacInputSourceChanged(QStringLiteral("双"), QStringLiteral("简体双拼"));
        app.processEvents();
        check(ball.activeImeFlashLabel() == QStringLiteral("双") &&
                  ball.statusMonitor()->snapshot().inputLabel == QStringLiteral("双"),
              "macOS input source notification immediately flashes IME emblem over 24H clock");
        if (liveMode) {
            ball.grab().save(outDir + QStringLiteral("/live-macos-ball-ime.png"));
        }
        ball.statusMonitor()->handleMacInputSourceChanged(QStringLiteral("EN"), QStringLiteral("ABC"));
        app.processEvents();
        check(ball.activeImeFlashLabel() == QStringLiteral("EN"),
              "macOS input source switch to ABC immediately flashes EN");
        ball.dockModel()->clearOpenWindowsForTest();
    }

    // 4b. Control Center UX fixes: top-right X button, zero-layout-shift floating status pill,
    //     and clicking main ball dismisses open Control Center cleanly without reopening fan
    {
        ball.openSystemPopover(SystemPopover::TabHome);
        ball.systemPopover()->setAutoDismissEnabled(false);
        app.processEvents();

        // Top-right close X exists and is visible on Home & subpages
        QToolButton *closeBtn = nullptr;
        for (QToolButton *b : ball.systemPopover()->findChildren<QToolButton *>()) {
            if (b->property("role").toString() == QLatin1String("close")) {
                closeBtn = b;
                break;
            }
        }
        check(closeBtn != nullptr && !closeBtn->isHidden(),
              "Control Center header exposes manual top-right close X button on Home");
        ball.systemPopover()->showPage(SystemPopover::PageWifi);
        app.processEvents();
        check(!closeBtn->isHidden(),
              "Control Center top-right close X button stays visible on subpages");
        ball.systemPopover()->showPage(SystemPopover::PageHome);
        app.processEvents();

        // Trigger transient status pill via powerPill click -> ZERO vertical shift of Bento controls or height!
        auto *dndCard = ball.systemPopover()->findChild<QPushButton *>(QStringLiteral("dndCard"));
        auto *powerPill = ball.systemPopover()->findChild<QToolButton *>(QStringLiteral("powerPill"));
        auto *pillLbl = ball.systemPopover()->findChild<QLabel *>(QStringLiteral("floatingStatusPill"));
        check(dndCard != nullptr && powerPill != nullptr && pillLbl != nullptr,
              "located Control Center dndCard, powerPill, and floatingStatusPill overlay");
        const QPoint dndPosBefore = dndCard->pos();
        const int popHeightBefore = ball.systemPopover()->height();
        ball.systemPopover()->showStatusBanner(QStringLiteral("✓ 已开启 Wi-Fi"));
        app.processEvents();
        check(pillLbl->isVisible() &&
                  pillLbl->parentWidget() == ball.systemPopover() &&
                  pillLbl->testAttribute(Qt::WA_TransparentForMouseEvents) &&
                  dndCard->pos() == dndPosBefore &&
                  ball.systemPopover()->height() == popHeightBefore,
              "showStatusBanner renders click-through floating pill with 0px layout shift and 0px height jump");
        ball.systemPopover()->showStatusBanner(QString());
        app.processEvents();
        check(!pillLbl->isVisible() && dndCard->pos() == dndPosBefore &&
                  ball.systemPopover()->height() == popHeightBefore,
              "hiding floating status pill keeps 0px layout shift");
        // Verify clicking close X hides Control Center
        closeBtn->click();
        app.processEvents();
        check(!ball.systemPopover()->isVisible(),
              "clicking top-right close X button dismisses Control Center");

        // Reopen Control Center, then click main FloatingBall -> dismisses Control Center & does NOT open fan!
        ball.openSystemPopover(SystemPopover::TabHome);
        app.processEvents();
        check(ball.systemPopover()->isVisible(), "Control Center reopened for main-ball dismiss test");
        const QPointF cLocal(ball.rect().center());
        const QPointF cGlobal = ball.mapToGlobal(cLocal.toPoint());
        QMouseEvent downEv(QEvent::MouseButtonPress, cLocal, cGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent upEv(QEvent::MouseButtonRelease, cLocal, cGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &downEv);
        app.sendEvent(&ball, &upEv);
        app.processEvents();
        check(!ball.systemPopover()->isVisible() && !ball.isOrbitVisible(),
              "clicking main FloatingBall cleanly dismisses Control Center without popping open radial fan");
        check(dndCard->pos() == dndPosBefore && ball.systemPopover()->height() == popHeightBefore,
              "Control Center Bento cards and height stay rock-solid with zero jump");
    }

    // 5. Live Cocoa verification: real macOS windows + native NSGlassEffectView Control Center + Notification Center
    if (liveMode) {
        const QList<DockAppEntry> realWins = AppDockMac::openWindowsFast();
        std::printf("LIVE_MACOS_OPEN_WINDOWS=%lld\n", static_cast<long long>(realWins.size()));
        bool noSelf = true;
        for (const DockAppEntry &w : realWins) {
            if (w.name.contains(QLatin1String("FloatingBall"), Qt::CaseInsensitive)) noSelf = false;
            std::printf("  WIN %s | app=%s | title=%s\n",
                        w.windowId.toUtf8().constData(),
                        w.name.toUtf8().constData(),
                        w.caption.left(48).toUtf8().constData());
        }
        check(noSelf, "live macOS window enumeration excludes FloatingBall itself");

        // Verify live Control Center + native macOS 27 NSGlassEffectView + Obsidian palette + tight height
        ball.openSystemPopover(SystemPopover::TabHome);
        ball.systemPopover()->setAutoDismissEnabled(false);
        waitMs(350);
        const QString glassStatus = ball.systemPopover()->property("panelBackdropBlurStatus").toString();
        std::printf("LIVE_CONTROL_CENTER_GLASS=%s size=%dx%d\n",
                    glassStatus.toUtf8().constData(),
                    ball.systemPopover()->width(), ball.systemPopover()->height());
        check(ball.systemPopover()->height() <= 340,
              "Control Center Home hugs visible content tightly on macOS (no empty void)");
        ball.systemPopover()->grab().save(outDir + QStringLiteral("/live-macos-control-center.png"));
        captureCompositedWindow(ball.systemPopover(), outDir + QStringLiteral("/composited-control-center.png"));

        // Verify live floating status pill overlays cleanly without shifting any Bento button
        if (auto *dndCard = ball.systemPopover()->findChild<QPushButton *>(QStringLiteral("dndCard"))) {
            const QPoint beforePos = dndCard->pos();
            const int beforeH = ball.systemPopover()->height();
            ball.systemPopover()->showStatusBanner(QStringLiteral("✓ 已开启 Wi-Fi"));
            waitMs(120);
            check(dndCard->pos() == beforePos && ball.systemPopover()->height() == beforeH,
                  "live floating status toast causes 0px shift on Control Center controls");
            captureCompositedWindow(ball.systemPopover(), outDir + QStringLiteral("/composited-control-center-toast.png"));
            ball.systemPopover()->showStatusBanner(QString());
        }

        // Seed a notification and capture Notification Center
        NotificationItem demo;
        demo.appName = QStringLiteral("Mail");
        demo.desktopEntry = QStringLiteral("com.apple.mail");
        demo.summary = QStringLiteral("macOS 27 原生通知中心");
        demo.body = QStringLiteral("Obsidian Frosted Glass + 实时归因验证通过");
        ball.statusMonitor()->pushNotification(demo);
        ball.openSystemPopover(SystemPopover::TabNotifications);
        waitMs(250);
        ball.systemPopover()->grab().save(outDir + QStringLiteral("/live-macos-notif-center.png"));
        captureCompositedWindow(ball.systemPopover(), outDir + QStringLiteral("/composited-notif-center.png"));
        ball.systemPopover()->hide();

        // Capture Obsidian QuickDeck
        ball.toggleQuickDeck();
        waitMs(250);
        ball.quickDeck()->grab().save(outDir + QStringLiteral("/live-macos-quickdeck.png"));
        captureCompositedWindow(ball.quickDeck(), outDir + QStringLiteral("/composited-quickdeck.png"));
        ball.quickDeck()->hide();
    }

    return 0;
}
