#include "clipboardbubble.h"
#include "applauncher.h"
#include "floatingball.h"
#include "systempopover.h"
#include "quickdeck.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <cstdio>
#include <cstdlib>

namespace {
void check(bool ok, const char *msg)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", msg);
    fflush(stdout);
    if (!ok) std::exit(1);
}

void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    check(argc == 2 && QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])),
          "explicit external touch-ux test directory supplied");
    QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir temp(QString::fromLocal8Bit(argv[1]) + "/touch-ux-XXXXXX");
    check(temp.isValid(), "owned temporary touch-ux workspace");

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path() + "/settings");

    // Save & restore clipboard cleanly
    QMimeData *savedClip = nullptr;
    if (const QMimeData *orig = app.clipboard()->mimeData()) {
        savedClip = new QMimeData();
        for (const QString &fmt : orig->formats()) savedClip->setData(fmt, orig->data(fmt));
        if (orig->hasImage()) savedClip->setImageData(orig->imageData());
    }

    FloatingBall ball;
    ball.show();
    app.processEvents();
    waitMs(40);

    QScreen *screen = ball.screen();
    const QRect bounds = screen->availableGeometry();
    check(ball.orbitActions() == FloatingBall::defaultOrbitActions() && ball.orbitActions().size() == 5,
          "default inner ring provides the 5 fixed system control actions");
    check(!ball.autoHideEnabled() && ball.statusOrbMode() && ball.autoTuckFullscreen(),
          "default keeps primary circular 24H clock ball always visible and enables fullscreen capsule morph");

    // Isolate deterministic test window list (start with 0 open windows)
    ball.dockModel()->setOpenWindowsForTest({});
    app.processEvents();

    // Configure explicit 5-slot layout for predictable flick-selection assertions across Linux & macOS
    check(ball.setOrbitActions({"back", "overview", "crosshair", "clipboard", "deck"}),
          "explicit 5-slot action set applied for flick workflow check");

    // ------------------------------------------------------------------------
    // 1. Xiaomi Mi 5 Press-Slide-Release (FlickSelect) in One Continuous Stroke
    // ------------------------------------------------------------------------
    {
        ball.expandFromEdge();
        const QPointF startLocal(ball.rect().center());
        const QPointF startGlobal = ball.mapToGlobal(startLocal.toPoint());

        // Press down
        QMouseEvent down(QEvent::MouseButtonPress, startLocal, startGlobal,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down);
        check(ball.gestureMode() == FloatingBall::GesturePressed, "press enters GesturePressed awaiting pull or hold");

        // Immediate inward pull (>= 10px before 380ms hold) -> opens FlickFanMode!
        const qreal pullDir = (ball.dockSide() < 0) ? 1.0 : -1.0;
        const QPointF pullGlobal = startGlobal + QPointF(pullDir * 32.0, 0.0);
        const QPointF pullLocal = pullGlobal - QPointF(ball.pos());
        QMouseEvent pullMove(QEvent::MouseMove, pullLocal, pullGlobal,
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pullMove);

        check(ball.gestureMode() == FloatingBall::GestureFlickFan &&
                  ball.fanOverlay()->overlayMode() == RadialFanOverlay::FlickFanMode &&
                  ball.orbitSlotGlobalRects().size() == 5,
              "immediate pull opens single-canvas Xiaomi Mi 5 FlickFan without waiting for hold");

        // Slide onto slot 3 ("clipboard") and release -> opens ClipboardBubble in one stroke!
        const QPointF clipSlotCenter = ball.orbitSlotGlobalRects().at(3).center();
        const QPointF clipLocal = clipSlotCenter - QPointF(ball.pos());
        QMouseEvent slideToClip(QEvent::MouseMove, clipLocal, clipSlotCenter,
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &slideToClip);
        check(ball.fanOverlay()->highlightedSlot() == 3,
              "sliding finger onto slot 3 magnetizes and highlights Clipboard slot");

        QMouseEvent releaseOnClip(QEvent::MouseButtonRelease, clipLocal, clipSlotCenter,
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &releaseOnClip);
        app.processEvents();

        check(ball.gestureMode() == FloatingBall::GestureIdle &&
                  !ball.isOrbitVisible() &&
                  ball.clipboardBubble()->isVisible(),
              "releasing on Clipboard slot triggers ClipboardBubble and collapses fan in a single stroke");
        ball.clipboardBubble()->hide();

        // Slide onto slot 4 ("deck") and release -> opens QuickDeck in one stroke!
        QMouseEvent down2(QEvent::MouseButtonPress, startLocal, startGlobal,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down2);
        QMouseEvent pull2(QEvent::MouseMove, pullLocal, pullGlobal,
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pull2);
        const QPointF deckCenter = ball.orbitSlotGlobalRects().at(4).center();
        const QPointF deckLocal = deckCenter - QPointF(ball.pos());
        QMouseEvent slideToDeck(QEvent::MouseMove, deckLocal, deckCenter,
                                Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &slideToDeck);
        QMouseEvent releaseOnDeck(QEvent::MouseButtonRelease, deckLocal, deckCenter,
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &releaseOnDeck);
        app.processEvents();
        check(ball.quickDeck()->isVisible() && !ball.isOrbitVisible(),
              "releasing on Deck slot opens QuickDeck and collapses fan");

        // Slide out then back to center ball (< 26px) -> cancels cleanly!
        ball.quickDeck()->hide();
        QMouseEvent down3(QEvent::MouseButtonPress, startLocal, startGlobal,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down3);
        QMouseEvent pull3(QEvent::MouseMove, pullLocal, pullGlobal,
                          Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pull3);
        QMouseEvent slideBack(QEvent::MouseMove, startLocal, startGlobal,
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &slideBack);
        check(ball.fanOverlay()->highlightedSlot() == -1,
              "returning finger to center ball clears slot selection");
        QMouseEvent releaseCancel(QEvent::MouseButtonRelease, startLocal, startGlobal,
                                  Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &releaseCancel);
        check(!ball.quickDeck()->isVisible() && !ball.clipboardBubble()->isVisible() && !ball.isOrbitVisible(),
              "releasing back on center ball cancels without triggering any action");
    }

    // ------------------------------------------------------------------------
    // 2. Seamless fooView Crosshair Handoff + Dwell-to-Rect + Cancel
    // ------------------------------------------------------------------------
    {
        ball.expandFromEdge();
        const QPointF startLocal(ball.rect().center());
        const QPointF startGlobal = ball.mapToGlobal(startLocal.toPoint());
        const qreal pullDir = (ball.dockSide() < 0) ? 1.0 : -1.0;

        QMouseEvent down(QEvent::MouseButtonPress, startLocal, startGlobal,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down);

        // First enter FlickFan
        const QPointF midGlobal = startGlobal + QPointF(pullDir * 40.0, 0.0);
        QMouseEvent pull(QEvent::MouseMove, midGlobal - QPointF(ball.pos()), midGlobal,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pull);
        check(ball.gestureMode() == FloatingBall::GestureFlickFan, "entered FlickFan before outer pull");

        // Pull past fanOuterRadius + 48 -> seamless transition into fooView CrosshairAim!
        const qreal farDist = ball.fanOverlay()->fanOuterRadius() + 90.0;
        const QPointF aimGlobal(qBound(qreal(bounds.left() + 80), startGlobal.x() + pullDir * farDist,
                                       qreal(bounds.right() - 80)),
                                bounds.center().y());
        QMouseEvent pullFar(QEvent::MouseMove, aimGlobal - QPointF(ball.pos()), aimGlobal,
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pullFar);
        check(ball.gestureMode() == FloatingBall::GestureCrosshairAim &&
                  ball.fanOverlay()->overlayMode() == RadialFanOverlay::CrosshairAimMode,
              "pulling past fan outer ring seamlessly transitions into fooView CrosshairAimMode");

        // Slide back inside fan radius BEFORE dwell completes -> seamlessly returns to FlickFan!
        QMouseEvent slideIn(QEvent::MouseMove, midGlobal - QPointF(ball.pos()), midGlobal,
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &slideIn);
        check(ball.gestureMode() == FloatingBall::GestureFlickFan &&
                  ball.fanOverlay()->overlayMode() == RadialFanOverlay::FlickFanMode,
              "sliding back inside fan ring before dwelling returns smoothly to FlickFan");

        // Pull out again and DWELL steady for 360ms -> arms live CrosshairRectMode!
        app.sendEvent(&ball, &pullFar);
        waitMs(370);
        check(ball.gestureMode() == FloatingBall::GestureCrosshairRect &&
                  ball.fanOverlay()->overlayMode() == RadialFanOverlay::CrosshairRectMode,
              "dwelling steady for 320ms arms fooView live region selection rectangle");

        const QPointF boxEnd = aimGlobal + QPointF(pullDir * 120.0, 80.0);
        QMouseEvent dragBox(QEvent::MouseMove, boxEnd - QPointF(ball.pos()), boxEnd,
                            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &dragBox);
        check(ball.fanOverlay()->grab().save(QString::fromLocal8Bit(argv[1]) + "/crosshair-rect.png"),
              "fooView crosshair live crop rectangle rendered cleanly");

        // Right-click cancels crosshair cleanly without capturing
        QMouseEvent rightCancel(QEvent::MouseButtonPress, boxEnd - QPointF(ball.pos()), boxEnd,
                                Qt::RightButton, Qt::RightButton | Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &rightCancel);
        check(ball.gestureMode() == FloatingBall::GestureIdle && !ball.fanOverlay()->isVisible(),
              "secondary press cancels fooView crosshair cleanly");
    }

    // ------------------------------------------------------------------------
    // 3. Stationary Hold (380ms) -> Reposition + Edge Snap + Vertical Capsule Morph
    // ------------------------------------------------------------------------
    {
        ball.setStatusOrbMode(true);
        ball.setAutoHideEnabled(true);
        ball.expandFromEdge();
        const QPointF startLocal(ball.rect().center());
        const QPointF startGlobal = ball.mapToGlobal(startLocal.toPoint());

        QMouseEvent down(QEvent::MouseButtonPress, startLocal, startGlobal,
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down);
        waitMs(420);
        check(ball.gestureMode() == FloatingBall::GestureMoveBall && !ball.isOrbitVisible(),
              "stationary 380ms hold picks up ball for repositioning without opening radial fan");

        // Drag to left side at 35% height and release -> snaps to left edge!
        const QPointF leftTarget(bounds.left() + 45, bounds.top() + bounds.height() * 0.35);
        QMouseEvent dragLeft(QEvent::MouseMove, leftTarget - QPointF(ball.pos()), leftTarget,
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &dragLeft);
        QMouseEvent dropLeft(QEvent::MouseButtonRelease, leftTarget - QPointF(ball.pos()), leftTarget,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &dropLeft);
        waitMs(220);
        check(ball.dockSide() == -1 && ball.x() == bounds.left() && ball.width() == ball.height(),
              "dropping repositioned ball on left half snaps cleanly as primary circle to left screen edge");

        const int fullDiameter = ball.width();
        QEvent leaveEv(QEvent::Leave);
        app.sendEvent(&ball, &leaveEv);
        waitMs(2400);
        check(ball.isEdgeTucked() && ball.width() < fullDiameter && ball.height() > fullDiameter && ball.x() == bounds.left(),
              "when optional idle auto-hide is enabled, docked circle morphs into slim vertical edge capsule");
        check(ball.grab().save(QString::fromLocal8Bit(argv[1]) + "/edge-vertical-capsule.png"),
              "tucked vertical edge capsule preview saved outside source");

        QPointF tuckPt(ball.rect().center());
        QEnterEvent enterEv(tuckPt, tuckPt, ball.mapToGlobal(tuckPt.toPoint()));
        app.sendEvent(&ball, &enterEv);
        check(!ball.isEdgeTucked() && ball.width() == fullDiameter && ball.height() == fullDiameter && ball.x() == bounds.left(),
              "approaching idle-tucked capsule immediately restores primary circular ball");
        ball.setAutoHideEnabled(false);

        // Verify Fullscreen Video / Game Auto-Tuck into Vertical Capsule & Auto-Restore!
        ball.dockModel()->setActiveFullscreenForTest(true);
        waitMs(200);
        check(ball.isEdgeTucked() && ball.isFullscreenTucked() && ball.width() < ball.height(),
              "entering fullscreen video/game automatically morphs primary circle into slim vertical capsule");
        // Passive mouse hover must NOT pop open over fullscreen video/game!
        QPointF fsPt(ball.rect().center());
        QEnterEvent fsHover(fsPt, fsPt, ball.mapToGlobal(fsPt.toPoint()));
        app.sendEvent(&ball, &fsHover);
        check(ball.isEdgeTucked(), "passive hover does not intrude during fullscreen capsule mode");
        ball.dockModel()->setActiveFullscreenForTest(false);
        waitMs(80);
        check(!ball.isEdgeTucked() && ball.width() == ball.height(),
              "exiting fullscreen automatically restores primary circular clock ball");

        // Verify Manual Outward Push toward bezel -> Vertical Capsule, and Tap -> Restore Circle!
        const QPointF pushStartLocal(ball.rect().center());
        const QPointF pushStartGlobal = ball.mapToGlobal(pushStartLocal.toPoint());
        QMouseEvent pDown(QEvent::MouseButtonPress, pushStartLocal, pushStartGlobal,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pDown);
        const QPointF pushOutGlobal = pushStartGlobal + QPointF(-20.0, 0.0); // Left edge -> push further left!
        QMouseEvent pOut(QEvent::MouseMove, pushOutGlobal - QPointF(ball.pos()), pushOutGlobal,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &pOut);
        QMouseEvent pUp(QEvent::MouseButtonRelease, pushOutGlobal - QPointF(ball.pos()), pushOutGlobal,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &pUp);
        waitMs(200);
        check(ball.isEdgeTucked() && ball.isManualTucked() && ball.width() < ball.height(),
              "pushing ball outward toward docked screen bezel morphs into temporary vertical capsule");

        // Tap on manual capsule restores primary circle!
        const QPointF capLocal(ball.rect().center());
        const QPointF capGlobal = ball.mapToGlobal(capLocal.toPoint());
        QMouseEvent tapCapDown(QEvent::MouseButtonPress, capLocal, capGlobal,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent tapCapUp(QEvent::MouseButtonRelease, capLocal, capGlobal,
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &tapCapDown);
        app.sendEvent(&ball, &tapCapUp);
        app.processEvents();
        check(!ball.isEdgeTucked() && !ball.isManualTucked() && ball.width() == ball.height(),
              "clicking tucked vertical capsule restores primary circular clock ball");

        // Verify Top & Bottom edge docking (full 4-edge docking!)
        const QPointF topStartLocal(ball.rect().center());
        const QPointF topStartGlobal = ball.mapToGlobal(topStartLocal.toPoint());
        QMouseEvent downTop(QEvent::MouseButtonPress, topStartLocal, topStartGlobal,
                            Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&ball, &downTop);
        const QPointF topTarget(bounds.center().x(), bounds.top() + 18);
        QMouseEvent dragTop(QEvent::MouseMove, topTarget - QPointF(ball.pos()), topTarget,
                            Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&ball, &dragTop);
        QMouseEvent dropTop(QEvent::MouseButtonRelease, topTarget - QPointF(ball.pos()), topTarget,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &dropTop);
        waitMs(220);
        check(ball.dockSide() == FloatingBall::DockTop && ball.y() == bounds.top(),
              "dropping ball near top edge snaps cleanly to top screen edge");
        // Verify iPad-style dynamic button ordering: Top edge sweeps Left -> Right!
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        {
            const auto topSlots = ball.orbitSlotGlobalRects();
            check(topSlots.size() == 5 &&
                      topSlots.first().center().x() < topSlots.last().center().x(),
                  "top-docked ring sweeps dynamically Left -> Right like iPad volume buttons");
        }
        ball.closeFan(true);

        const QPointF botStartLocal(ball.rect().center());
        const QPointF botStartGlobal = ball.mapToGlobal(botStartLocal.toPoint());
        QMouseEvent downBot(QEvent::MouseButtonPress, botStartLocal, botStartGlobal,
                            Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&ball, &downBot);
        const QPointF botTarget(bounds.center().x(), bounds.bottom() - 18);
        QMouseEvent dragBot(QEvent::MouseMove, botTarget - QPointF(ball.pos()), botTarget,
                            Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&ball, &dragBot);
        QMouseEvent dropBot(QEvent::MouseButtonRelease, botTarget - QPointF(ball.pos()), botTarget,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &dropBot);
        waitMs(220);
        check(ball.dockSide() == FloatingBall::DockBottom &&
                  ball.y() + ball.height() - 1 == bounds.bottom(),
              "dropping ball near bottom edge snaps cleanly to bottom screen edge");
        // Verify iPad-style dynamic button ordering: Bottom edge also sweeps Left -> Right!
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        {
            const auto botSlots = ball.orbitSlotGlobalRects();
            check(botSlots.size() == 5 &&
                      botSlots.first().center().x() < botSlots.last().center().x(),
                  "bottom-docked ring also sweeps dynamically Left -> Right");
        }
        ball.closeFan(true);
    }

    // ------------------------------------------------------------------------
    // 4. ClipboardBubble & QuickDeck Touch Ergonomics
    // ------------------------------------------------------------------------
    {
        ClipboardBubble *clip = ball.clipboardBubble();
        clip->setRecordingEnabled(true);
        clip->recordText(QStringLiteral("Temporary snippet"), false);
        clip->recordText(QStringLiteral("Pinned SSH command"), true);
        clip->reveal(bounds.center(), false);
        app.processEvents();

        auto *list = clip->findChild<QListWidget *>("clipHistoryList");
        check(list && list->count() >= 2 && list->item(0)->text().startsWith(QStringLiteral("★ ")),
              "ClipboardBubble displays pinned items above transient history");

        clip->findChild<QToolButton *>("clipClearBtn")->click();
        check(list->count() == 1 && list->item(0)->text().contains(QStringLiteral("Pinned SSH command")),
              "clearing unpinned clipboard items preserves pinned entries");
        clip->hide();

        QuickDeck *deck = ball.quickDeck();
        deck->reveal(bounds.center(), false);
        app.processEvents();
        int touchTiles = 0;
        for (QToolButton *b : deck->findChildren<QToolButton *>()) {
            if (!b->property("tileId").toString().isEmpty() && b->height() >= 54)
                ++touchTiles;
        }
        check(touchTiles >= 12 && deck->findChild<QSlider *>("deckVolumeSlider") != nullptr,
              "QuickDeck renders 12 large touch window/navigation tiles and volume slider");
        deck->hide();
    }

    // ------------------------------------------------------------------------
    // 6. Inner Fixed Ring + Auto-Expanding All-Open-Windows Outer Rings (0 -> 12 -> 100 Windows!)
    //    + Directional Message Radar Dots + Mouse Middle-Click Window Close
    // ------------------------------------------------------------------------
    {
        ball.setStatusOrbMode(true);
        ball.setOrbitActions(FloatingBall::defaultOrbitActions());
        ball.expandFromEdge();
        // Dock cleanly on right edge like pk4 desktop default
        const QPointF rightTarget(bounds.right() - 12, bounds.center().y());
        QMouseEvent mDown(QEvent::MouseButtonPress, ball.rect().center(), ball.mapToGlobal(ball.rect().center()),
                          Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        QMouseEvent mDrag(QEvent::MouseMove, rightTarget - QPointF(ball.pos()), rightTarget,
                          Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        QMouseEvent mDrop(QEvent::MouseButtonRelease, rightTarget - QPointF(ball.pos()), rightTarget,
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &mDown);
        app.sendEvent(&ball, &mDrag);
        app.sendEvent(&ball, &mDrop);
        waitMs(200);

        // A) 0 open windows -> ONLY Ring 0 (5 fixed system buttons)!
        ball.dockModel()->setOpenWindowsForTest({});
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        check(ball.fanOverlay()->activeRingCount() == 1 &&
                  ball.allOrbitSlotGlobalRects().size() == 5,
              "with 0 open windows, only the 5 fixed inner buttons are shown without filler apps");
        ball.closeFan(true);

        // B) 12 open windows (interleaved apps) -> stably clustered by app + directional message radar dot!
        QList<DockAppEntry> twelveWins;
        const QStringList sampleApps{QStringLiteral("kitty"), QStringLiteral("firefox"), QStringLiteral("code")};
        for (int i = 0; i < 12; ++i) {
            DockAppEntry w;
            w.id = sampleApps[i % 3];
            w.appKey = w.id;
            w.name = w.id.toUpper();
            w.caption = QStringLiteral("win-%1 — %2").arg(i + 1).arg(w.name);
            w.windowId = QStringLiteral("win-%1").arg(i + 1);
            w.running = true;
            w.active = (i == 0);
            w.demandsAttention = (i == 7); // win-8 (firefox) has an unread message!
            twelveWins.append(w);
        }
        const QImage ballBeforeMsg = ball.grab().toImage();
        ball.dockModel()->setOpenWindowsForTest(twelveWins);
        app.processEvents();

        // Verify same-app clustering order (all kitty, then all firefox, then all code)
        const QList<DockAppEntry> clustered = ball.dockModel()->openWindows();
        check(clustered.size() == 12 &&
                  clustered[0].appKey == QLatin1String("kitty") &&
                  clustered[3].appKey == QLatin1String("kitty") &&
                  clustered[4].appKey == QLatin1String("firefox") &&
                  clustered[8].appKey == QLatin1String("code"),
              "openWindows stably clusters windows of the same application together");

        // Verify directional message radar dot on collapsed main ball!
        const QImage ballWithMsg = ball.grab().toImage();
        check(ball.attentionAnglesRad().size() == 1 && ballWithMsg != ballBeforeMsg,
              "notifying window projects a directional radar dot onto the collapsed circular main ball");

        // Open fan and verify the main-ball radar angle points straight to win-8's orb center!
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        int attnSlotIdx = -1;
        for (int i = 0; i < ball.fanOverlay()->slotsData().size(); ++i) {
            const RadialSlotItem &s = ball.fanOverlay()->slotsData()[i];
            if (s.kind == RadialSlotItem::AppSlot && s.app.windowId == QLatin1String("win-8")) {
                attnSlotIdx = i;
                break;
            }
        }
        check(attnSlotIdx >= 0 && ball.allOrbitSlotGlobalRects().size() == 17,
              "all 12 open windows expand in outer rings around the 5 fixed inner buttons");
        const QPointF win8Center = ball.fanOverlay()->slotsData()[attnSlotIdx].centerGlobal;
        const QPointF ballCenter = ball.anchorCenterGlobal();
        const double expectedDeg = qRadiansToDegrees(std::atan2(win8Center.y() - ballCenter.y(),
                                                                win8Center.x() - ballCenter.x()));
        const double radarDeg = qRadiansToDegrees(ball.attentionAnglesRad().first());
        double diffDeg = std::abs(expectedDeg - radarDeg);
        if (diffDeg > 180.0) diffDeg = 360.0 - diffDeg;
        check(diffDeg < 1.0,
              "directional radar dot on main ball aligns (< 1 deg) with notifying window's expanded orb position");

        // Middle-clicking win-8 in TapFanMode closes that window in-place while keeping the fan open!
        const QPointF win8Local = win8Center - QPointF(ball.fanOverlay()->geometry().topLeft());
        QMouseEvent midDown(QEvent::MouseButtonPress, win8Local, win8Center,
                            Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QMouseEvent midUp(QEvent::MouseButtonRelease, win8Local, win8Center,
                          Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(ball.fanOverlay(), &midDown);
        app.sendEvent(ball.fanOverlay(), &midUp);
        app.processEvents();
        check(ball.dockModel()->openWindows().size() == 11 &&
                  ball.allOrbitSlotGlobalRects().size() == 16 &&
                  ball.attentionAnglesRad().isEmpty(),
              "PC middle-click closes window orb in-place and clears directional message radar dot");

        // Clicking outside collapses fan cleanly
        const QPointF outsideGlobal(bounds.left() + 12, bounds.top() + 12);
        const QPointF outsideLocal = outsideGlobal - QPointF(ball.fanOverlay()->geometry().topLeft());
        QMouseEvent outsideClick(QEvent::MouseButtonPress, outsideLocal, outsideGlobal,
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(ball.fanOverlay(), &outsideClick);
        waitMs(190);
        check(!ball.isOrbitVisible(), "clicking outside floating ball and fan automatically collapses the fan");

        // C) Stress test: 100 open windows auto-distributed across multi-layer concentric arcs!
        QList<DockAppEntry> hundredWins;
        hundredWins.reserve(100);
        for (int i = 0; i < 100; ++i) {
            DockAppEntry w;
            w.id = sampleApps[i % 3];
            w.appKey = w.id;
            w.name = w.id.toUpper();
            w.caption = QStringLiteral("task-%1 — %2").arg(i + 1).arg(w.name);
            w.windowId = QStringLiteral("w100-%1").arg(i + 1);
            w.running = true;
            hundredWins.append(w);
        }
        ball.dockModel()->setOpenWindowsForTest(hundredWins);
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();

        const QList<QRectF> all100 = ball.allOrbitSlotGlobalRects();
        bool all100Valid = (all100.size() == 105 && ball.fanOverlay()->activeRingCount() >= 5);
        for (int i = 0; i < all100.size(); ++i) {
            const QRect r = all100[i].toAlignedRect();
            all100Valid = all100Valid && bounds.adjusted(-2, -2, 2, 2).contains(r);
            for (int j = i + 1; j < all100.size(); ++j) {
                const qreal minCenterDist = (all100[i].width() + all100[j].width()) * 0.5 + 1.5;
                all100Valid = all100Valid &&
                              (QLineF(all100[i].center(), all100[j].center()).length() >= minCenterDist);
            }
        }
        check(all100Valid, "100 open windows auto-distribute across concentric rings strictly inside screen bounds with zero overlap");
        check(ball.fanOverlay()->grab().save(QString::fromLocal8Bit(argv[1]) + "/multi-ring-100-windows.png"),
              "100-window multi-ring layout preview saved outside source");
        ball.closeFan(true);
        ball.dockModel()->setOpenWindowsForTest({});

        // Verify classic non-status sphere audio sync & QuickDeck slider sync
        ball.setStatusOrbMode(false);
        ball.setSystemAudioState(87, false);
        const QImage unmutedImg = ball.grab().toImage();
        ball.setSystemAudioState(87, true);
        check(ball.isSystemMuted(), "FloatingBall tracks system muted state");
        const QImage mutedImg = ball.grab().toImage();
        check(mutedImg == unmutedImg, "classic FloatingBall sphere keeps clean obsidian look without mute shift");

        QuickDeck *deck = ball.quickDeck();
        auto *slider = deck->findChild<QSlider *>("deckVolumeSlider");
        check(slider != nullptr, "QuickDeck volume slider accessible");
        slider->setValue(55);
        app.processEvents();
        check(!ball.isSystemMuted() && ball.systemVolume() == 55,
              "dragging QuickDeck volume slider clears mute indicator on FloatingBall immediately");
        slider->setValue(0);
        app.processEvents();
        check(ball.isSystemMuted(), "dragging QuickDeck volume slider to 0 sets mute indicator on FloatingBall");
    }

    // ------------------------------------------------------------------------
    // 7. Primary Circular 24H Two-Line Clock Ball + Transient IME Flash + Status Arcs
    // ------------------------------------------------------------------------
    {
        ball.setStatusOrbMode(true);
        ball.expandFromEdge();
        SystemStatusSnapshot snap;
        snap.brightnessAvailable = true;
        snap.brightnessPercent = 76;
        snap.volumeAvailable = true;
        snap.volumePercent = 62;
        snap.volumeMuted = false;
        snap.batteryPresent = true;
        snap.batteryPercent = 88;
        snap.batteryPluggedIn = true;
        snap.networkConnected = true;
        snap.signalQuality = 85;
        snap.networkText = QStringLiteral("HomeWiFi_5G (85%)");
        snap.inputPresent = true;
        snap.inputLabel = QStringLiteral("双");
        snap.inputDescription = QStringLiteral("双拼");
        snap.micActive = true;
        snap.micUsers = {QStringLiteral("voice-input")};
        snap.cameraActive = true;
        snap.cameraUsers = {QStringLiteral("chrome")};
        snap.unreadNotifications = 0;
        ball.statusMonitor()->setSnapshotForTest(snap);
        app.processEvents();

        const QImage clockBallImg = ball.grab().toImage();
        ball.triggerImeFlash(QStringLiteral("双"), 400);
        app.processEvents();
        const QImage imeFlashImg = ball.grab().toImage();
        check(ball.activeImeFlashLabel() == QStringLiteral("双") && imeFlashImg != clockBallImg,
              "keyboard/IME switch transiently flashes input method emblem over the 2-line 24H clock");
        waitMs(450);
        check(ball.activeImeFlashLabel().isEmpty(),
              "transient IME flash automatically reverts back to 2-line 24H clock");

        const QImage orbNoNotif = ball.grab().toImage();
        snap.unreadNotifications = 2;
        snap.lastNotificationSummary = QStringLiteral("微信 发来新通知");
        ball.statusMonitor()->setSnapshotForTest(snap);
        app.processEvents();

        const QImage orbWithNotif = ball.grab().toImage();
        check(ball.statusOrbMode() && !ball.attentionAnglesRad().isEmpty() &&
                  orbWithNotif != orbNoNotif,
              "Primary Circular Clock Ball renders live status arcs and directional notification dot");
        check(orbWithNotif.save(QString::fromLocal8Bit(argv[1]) + "/duo-status-orb.png"),
              "Primary Circular Clock Ball preview saved outside source");

        ball.clearNotificationDot();
        app.processEvents();
        check(!ball.statusMonitor()->snapshot().hasNotificationDot() && ball.attentionAnglesRad().isEmpty(),
              "clearing unread notifications extinguishes directional radar dot");
    }

    // ------------------------------------------------------------------------
    // 9. Multi-Screen Scheme A: Per-Screen Replica Shares Content, Keeps Own Edge
    // ------------------------------------------------------------------------
    {
        FloatingBall replica(nullptr, screen, &ball);
        replica.show();
        app.processEvents();
        check(!replica.isPrimaryBall() &&
                  replica.statusMonitor() == ball.statusMonitor() &&
                  replica.dockModel() == ball.dockModel() &&
                  replica.quickDeck() == ball.quickDeck() &&
                  replica.clipboardBubble() == ball.clipboardBubble(),
              "replica screen orb shares single SystemStatusMonitor, AppDockModel, QuickDeck, and ClipboardBubble");

        // Changing ringCount from replica propagates back to primary
        replica.setRingCount(4);
        check(ball.ringCount() == 4, "changing ring count on a screen replica updates primary shared state");

        // Dragging replica to a different edge keeps its own independent dockSide
        const QPointF rStartLocal(replica.rect().center());
        const QPointF rStartGlobal = replica.mapToGlobal(rStartLocal.toPoint());
        QMouseEvent rDown(QEvent::MouseButtonPress, rStartLocal, rStartGlobal,
                          Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&replica, &rDown);
        const QPointF rTarget(bounds.left() + 14, bounds.center().y());
        QMouseEvent rDrag(QEvent::MouseMove, rTarget - QPointF(replica.pos()), rTarget,
                          Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
        app.sendEvent(&replica, &rDrag);
        QMouseEvent rDrop(QEvent::MouseButtonRelease, rTarget - QPointF(replica.pos()), rTarget,
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&replica, &rDrop);
        waitMs(200);
        check(replica.dockSide() == FloatingBall::DockLeft && ball.dockSide() != FloatingBall::DockLeft,
              "replica screen orb maintains its own independent edge placement without moving primary");

        // Opening fan on replica closes primary's open fan exclusively
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        replica.closeOtherFans(&replica);
        check(!ball.isOrbitVisible(), "opening a screen orb fan exclusively closes other screens' open fans");
    }

    // ------------------------------------------------------------------------
    // 10. Fluid Continuous-Drag Drill-Down + Backtrack to ← Breadcrumb
    // ------------------------------------------------------------------------
    {
        ball.setStatusOrbMode(true);
        ball.setOrbitActions(FloatingBall::defaultOrbitActions());
        ball.dockModel()->setOpenWindowsForTest({});
        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();
        check(ball.orbitSlotGlobalRects().size() == 5 && ball.allOrbitSlotGlobalRects().size() == 5,
              "root displays clean 5 fixed inner hubs when 0 windows are open");
        check(ball.fanOverlay()->highlightedSlot() == -1 && ball.fanOverlay()->activeLabelText().isEmpty(),
              "expanded fan shows zero text labels at rest until pointing/sliding onto an orb");
        ball.fanOverlay()->drillInto(QStringLiteral("tools"), QStringLiteral("工具"),
                                     QColor("#86e0b8"), QPointF(bounds.center()));
        app.processEvents();
        check(ball.fanOverlay()->isDrilled() && ball.fanOverlay()->slotsData().size() >= 9,
              "drilling into Tools hub opens clean single ring with ← backtrack orb");
        // Wait past anti-bounce cooldown then drag finger onto ← backtrack orb to pop back!
        waitMs(230);
        QPointF backCenter;
        for (const RadialSlotItem &s : ball.fanOverlay()->slotsData()) {
            if (s.kind == RadialSlotItem::BackSlot) { backCenter = s.centerGlobal; break; }
        }
        ball.fanOverlay()->handleContinuousDragAt(backCenter);
        app.processEvents();
        check(!ball.fanOverlay()->isDrilled(),
              "sliding continuously onto ← backtrack orb returns cleanly to root level without releasing");
        ball.closeFan(true);

        // Verify sliding across Ring 0 › hub (托盘 › / 工具 ›) toward Ring 1 open windows
        // NEVER auto-opens the 1st-level sub-ball during the slide, while releasing on it DOES!
        {
            check(ball.setOrbitActions({QStringLiteral("launcher"), QStringLiteral("sys_hub"),
                                        QStringLiteral("tray_hub"), QStringLiteral("overview"),
                                        QStringLiteral("tools_hub")}),
                  "configured pk4 5-slot inner ring with tray_hub and tools_hub");
            QList<DockAppEntry> outerWins;
            for (int i = 0; i < 6; ++i) {
                DockAppEntry w;
                w.id = QStringLiteral("chrome");
                w.appKey = w.id;
                w.name = QStringLiteral("Chrome");
                w.caption = QStringLiteral("Tab %1 — Chrome").arg(i + 1);
                w.windowId = QStringLiteral("slide-win-%1").arg(i + 1);
                w.running = true;
                outerWins.append(w);
            }
            ball.dockModel()->setOpenWindowsForTest(outerWins);
            app.processEvents();

            const QPointF startLocal(ball.rect().center());
            const QPointF startGlobal = ball.mapToGlobal(startLocal.toPoint());
            QMouseEvent pDown(QEvent::MouseButtonPress, startLocal, startGlobal,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            app.sendEvent(&ball, &pDown);
            const qreal pullX = (ball.dockSide() == FloatingBall::DockLeft) ? 28.0 : -28.0;
            const QPointF pullPt = startGlobal + QPointF(pullX, 0.0);
            QMouseEvent pPull(QEvent::MouseMove, pullPt - QPointF(ball.pos()), pullPt,
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            app.sendEvent(&ball, &pPull);

            QPointF trayHubCenter;
            QPointF outerWinCenter;
            for (const RadialSlotItem &s : ball.fanOverlay()->slotsData()) {
                if (s.id == QLatin1String("tray_hub")) trayHubCenter = s.centerGlobal;
                if (s.kind == RadialSlotItem::AppSlot && outerWinCenter.isNull()) outerWinCenter = s.centerGlobal;
            }
            check(!trayHubCenter.isNull() && !outerWinCenter.isNull(),
                  "located Ring 0 tray_hub and Ring 1 outer window slot for slide-through test");

            // Slide directly onto tray_hub AND pause 460ms -> must NOT auto-open 1st-level sub-ball!
            QMouseEvent onHub(QEvent::MouseMove, trayHubCenter - QPointF(ball.pos()), trayHubCenter,
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            app.sendEvent(&ball, &onHub);
            waitMs(460);
            check(!ball.fanOverlay()->isDrilled() && !ball.fanOverlay()->activeLabelText().isEmpty(),
                  "sliding onto or pausing over Ring 0 › hub shows hover label without auto-opening sub-ball");

            // Continue sliding outward onto Ring 1 open window -> cleanly highlights outer window!
            QMouseEvent onOuterWin(QEvent::MouseMove, outerWinCenter - QPointF(ball.pos()), outerWinCenter,
                                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            app.sendEvent(&ball, &onOuterWin);
            const int hl = ball.fanOverlay()->highlightedSlot();
            check(!ball.fanOverlay()->isDrilled() && hl >= 0 &&
                      ball.fanOverlay()->slotsData()[hl].kind == RadialSlotItem::AppSlot,
                  "sliding past Ring 0 smoothly highlights Ring 1 open window without Ring 0 snag");

            // Cancel on center, then verify releasing directly on tray_hub DOES open its sub-ball!
            QMouseEvent backCenterEv(QEvent::MouseMove, startLocal, startGlobal,
                                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent relCenterEv(QEvent::MouseButtonRelease, startLocal, startGlobal,
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            app.sendEvent(&ball, &backCenterEv);
            app.sendEvent(&ball, &relCenterEv);

            app.sendEvent(&ball, &pDown);
            app.sendEvent(&ball, &pPull);
            app.sendEvent(&ball, &onHub);
            QMouseEvent relOnHub(QEvent::MouseButtonRelease, trayHubCenter - QPointF(ball.pos()), trayHubCenter,
                                 Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            app.sendEvent(&ball, &relOnHub);
            app.processEvents();
            check(ball.fanOverlay()->isDrilled() &&
                      ball.fanOverlay()->overlayMode() == RadialFanOverlay::TapFanMode,
                  "releasing directly on Ring 0 › hub intentionally opens its expandable sub-ball");
            ball.closeFan(true);
            ball.dockModel()->setOpenWindowsForTest({});
        }

        // Verify sys_hub directly reveals macOS/iOS-style Control Center (PageHome)
        // with inline Wi-Fi / Bluetooth / Mihomo subpages and auto-hiding MPRIS bar
        ball.executeAction(QStringLiteral("sys_hub"));
        app.processEvents();
        check(ball.systemPopover() != nullptr && ball.systemPopover()->isVisible() &&
                  ball.systemPopover()->currentPage() == SystemPopover::PageHome,
              "sys_hub opens unified Control Center directly on Bento Home page");
        check(ball.systemPopover()->grab().save(QString::fromLocal8Bit(argv[1]) + "/control-center-home-preview.png"),
              "saved Obsidian Liquid Glass Control Center Home preview outside source");
        ball.systemPopover()->showPage(SystemPopover::PageHotspot);
        app.processEvents();
        check(ball.systemPopover()->currentPage() == SystemPopover::PageHotspot &&
                  !ball.systemPopover()->defaultHotspotSsid().isEmpty(),
              "Control Center drills inline into Hotspot subpage with hostname default SSID");
        ball.systemPopover()->showPage(SystemPopover::PageMihomo);
        app.processEvents();
        check(ball.systemPopover()->currentPage() == SystemPopover::PageMihomo,
              "Control Center drills inline into Mihomo subpage");
        ball.systemPopover()->showPage(SystemPopover::PageHome);
        ball.systemPopover()->setMprisPreviewForTest(QStringLiteral("Test Track"), QStringLiteral("Test Artist"), true);
        app.processEvents();
        check(ball.systemPopover()->currentPage() == SystemPopover::PageHome,
              "Control Center returns cleanly to Home page with MPRIS preview active");
        ball.executeAction(QStringLiteral("sys_hub"));
        app.processEvents();
        check(!ball.systemPopover()->isVisible(),
              "re-triggering sys_hub toggles Control Center closed");

        // Verify launcher action opens centered macOS Tahoe-style AppLauncherPopover
        ball.executeAction(QStringLiteral("launcher"));
        app.processEvents();
        check(ball.appLauncher() != nullptr && ball.appLauncher()->isVisible(),
              "launcher action opens centered AppLauncherPopover");
        ball.executeAction(QStringLiteral("launcher"));
        app.processEvents();
        check(!ball.appLauncher()->isVisible(),
              "re-triggering launcher toggles AppLauncherPopover closed");

#ifndef Q_OS_MACOS
        // Verify AppLauncher auto-refreshes across NixOS atomic symlink swaps,
        // mutable .desktop add/remove, and higher-priority NoDisplay overrides!
        {
            const QString catRoot = temp.path() + QStringLiteral("/launcher-catalog");
            const QString localDir = catRoot + QStringLiteral("/local");
            const QString gen1Dir = catRoot + QStringLiteral("/gen1");
            const QString gen2Dir = catRoot + QStringLiteral("/gen2");
            const QString curLink = catRoot + QStringLiteral("/current");
            QDir().mkpath(localDir);
            QDir().mkpath(gen1Dir);
            QDir().mkpath(gen2Dir);

            auto writeDesktop = [](const QString &path, const QString &name, bool noDisplay = false) {
                QFile f(path);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                    f.write("[Desktop Entry]\nType=Application\n");
                    f.write(QStringLiteral("Name=%1\nExec=/bin/true\nCategories=Utility;\n").arg(name).toUtf8());
                    if (noDisplay) f.write("NoDisplay=true\n");
                }
            };
            writeDesktop(gen1Dir + QStringLiteral("/alpha-app.desktop"), QStringLiteral("AlphaApp"));
            writeDesktop(gen2Dir + QStringLiteral("/beta-app.desktop"), QStringLiteral("BetaApp"));
            check(QFile::link(gen1Dir, curLink), "created initial generation symlink for AppLauncher test");

            ball.dockModel()->setDesktopSearchDirsForTest({localDir, curLink});
            ball.appLauncher()->setAutoDismissEnabled(false);
            ball.appLauncher()->reveal(bounds.center(), false);
            app.processEvents();
            check(ball.appLauncher()->visibleAppIds().contains(QStringLiteral("alpha-app")) &&
                      !ball.appLauncher()->visibleAppIds().contains(QStringLiteral("beta-app")),
                  "AppLauncher lists initial generation desktop entries");

            // Simulate NixOS switch swapping symlink target while launcher is open
            QFile::remove(curLink);
            check(QFile::link(gen2Dir, curLink), "swapped generation symlink to gen2");
            check(ball.dockModel()->refreshInstalledCatalog(true),
                  "AppDockModel detects symlink generation swap via canonical path fingerprint");
            app.processEvents();
            check(!ball.appLauncher()->visibleAppIds().contains(QStringLiteral("alpha-app")) &&
                      ball.appLauncher()->visibleAppIds().contains(QStringLiteral("beta-app")),
                  "visible AppLauncher live-updates immediately when generation symlink swaps");

            // Add app in mutable local dir & hide beta-app via higher-priority NoDisplay override
            writeDesktop(localDir + QStringLiteral("/gamma-app.desktop"), QStringLiteral("GammaApp"));
            writeDesktop(localDir + QStringLiteral("/beta-app.desktop"), QStringLiteral("BetaApp"), true);
            check(ball.dockModel()->refreshInstalledCatalog(true),
                  "AppDockModel detects mutable directory desktop entry additions and overrides");
            app.processEvents();
            check(ball.appLauncher()->visibleAppIds().contains(QStringLiteral("gamma-app")) &&
                      !ball.appLauncher()->visibleAppIds().contains(QStringLiteral("beta-app")),
                  "AppLauncher shows added app and respects higher-priority NoDisplay masking");

            // Remove gamma-app and verify it disappears on refresh
            QFile::remove(localDir + QStringLiteral("/gamma-app.desktop"));
            check(ball.dockModel()->refreshInstalledCatalog(true),
                  "AppDockModel detects removed desktop entry");
            app.processEvents();
            check(!ball.appLauncher()->visibleAppIds().contains(QStringLiteral("gamma-app")),
                  "AppLauncher removes uninstalled app automatically");

            ball.appLauncher()->hide();
            ball.appLauncher()->setAutoDismissEnabled(true);
            ball.dockModel()->clearDesktopSearchDirsForTest();
        }
#endif
    }

    // ------------------------------------------------------------------------
    // 11. Notification Attribution, D-Bus Parser, NotificationSlot & Center
    // ------------------------------------------------------------------------
    {
        // 11a. D-Bus Notify block parser (multi-line HTML body + desktop-entry + transient OSD filter)
        const QString sampleNotify = QStringLiteral(
            "method call time=1795615700.123 sender=:1.112 -> destination=org.freedesktop.Notifications serial=77 "
            "path=/org/freedesktop/Notifications; interface=org.freedesktop.Notifications; member=Notify\n"
            "   string \"Telegram Desktop\"\n"
            "   uint32 42\n"
            "   string \"org.telegram.desktop\"\n"
            "   string \"Alice\"\n"
            "   string \"<b>晚上吃什么？</b>\n记得带伞 &amp; 钥匙\"\n"
            "   array [\n"
            "      string \"default\"\n"
            "      string \"Open\"\n"
            "   ]\n"
            "   array [\n"
            "      dict entry(\n"
            "         string \"desktop-entry\"\n"
            "         variant             string \"org.telegram.desktop\"\n"
            "      )\n"
            "      dict entry(\n"
            "         string \"urgency\"\n"
            "         variant             byte 1\n"
            "      )\n"
            "   ]\n"
            "   int32 -1\n");
        NotificationItem parsed;
        bool isTransient = true;
        check(SystemStatusMonitor::parseDbusMonitorNotifyBlock(sampleNotify, &parsed, &isTransient) &&
                  !isTransient &&
                  parsed.appName == QStringLiteral("Telegram Desktop") &&
                  parsed.replacesId == 42 &&
                  parsed.desktopEntry == QStringLiteral("org.telegram.desktop") &&
                  parsed.summary == QStringLiteral("Alice") &&
                  parsed.body.contains(QStringLiteral("晚上吃什么？")) &&
                  parsed.body.contains(QStringLiteral("& 钥匙")) &&
                  !parsed.body.contains(QStringLiteral("<b>")),
              "D-Bus Notify block parser extracts multi-line body, strips HTML, and reads desktop-entry");

        const QString transientOsd = QStringLiteral(
            "method call time=1795615701.000 sender=:1.5 -> destination=org.freedesktop.Notifications serial=78 "
            "path=/org/freedesktop/Notifications; interface=org.freedesktop.Notifications; member=Notify\n"
            "   string \"Plasma Workspace\"\n"
            "   uint32 0\n"
            "   string \"audio-volume-high\"\n"
            "   string \"Volume 70%\"\n"
            "   string \"\"\n"
            "   array [\n"
            "   ]\n"
            "   array [\n"
            "      dict entry(\n"
            "         string \"transient\"\n"
            "         variant             boolean true\n"
            "      )\n"
            "   ]\n"
            "   int32 1500\n");
        NotificationItem osdItem;
        bool osdTransient = false;
        check(SystemStatusMonitor::parseDbusMonitorNotifyBlock(transientOsd, &osdItem, &osdTransient) && osdTransient,
              "transient OSD notifications are flagged and filtered out from unread badge");

        // KDE plasma-nm Network Management connect/disconnect/modem churn (no transient hint!)
        const QString nmNotify = QStringLiteral(
            "method call time=1795703208.123 sender=:1.42 -> destination=org.freedesktop.Notifications serial=91 "
            "path=/org/freedesktop/Notifications; interface=org.freedesktop.Notifications; member=Notify\n"
            "   string \"Network Management\"\n"
            "   uint32 0\n"
            "   string \"applications-internet\"\n"
            "   string \"Mobile Broadband\"\n"
            "   string \"Connection 'Mobile Broadband' activated.\"\n"
            "   array [\n"
            "   ]\n"
            "   array [\n"
            "      dict entry(\n"
            "         string \"desktop-entry\"\n"
            "         variant             string \"networkmanagement\"\n"
            "      )\n"
            "      dict entry(\n"
            "         string \"urgency\"\n"
            "         variant             byte 1\n"
            "      )\n"
            "   ]\n"
            "   int32 -1\n");
        NotificationItem nmItem;
        bool nmTransient = false;
        check(SystemStatusMonitor::parseDbusMonitorNotifyBlock(nmNotify, &nmItem, &nmTransient) && nmTransient,
              "KDE Network Management / Mobile Broadband status notifications are filtered out from unread badge");

        const QString critPowerNotify = QStringLiteral(
            "method call time=1795703209.000 sender=:1.42 -> destination=org.freedesktop.Notifications serial=92 "
            "path=/org/freedesktop/Notifications; interface=org.freedesktop.Notifications; member=Notify\n"
            "   string \"Power Management\"\n"
            "   uint32 0\n"
            "   string \"battery-caution\"\n"
            "   string \"Battery Critical\"\n"
            "   string \"System will suspend soon\"\n"
            "   array [\n"
            "   ]\n"
            "   array [\n"
            "      dict entry(\n"
            "         string \"desktop-entry\"\n"
            "         variant             string \"powerdevil\"\n"
            "      )\n"
            "      dict entry(\n"
            "         string \"urgency\"\n"
            "         variant             byte 2\n"
            "      )\n"
            "   ]\n"
            "   int32 -1\n");
        NotificationItem critItem;
        bool critTransient = true;
        check(SystemStatusMonitor::parseDbusMonitorNotifyBlock(critPowerNotify, &critItem, &critTransient) && !critTransient,
              "critical urgency=2 PowerDevil alerts remain visible and are not filtered out");

        // 11b. Open window notification attribution + auto-clear (ZERO ghost red dot!)
        ball.statusMonitor()->clearAllNotifications();
        ball.clearNotificationDot();
        DockAppEntry kmail;
        kmail.id = QStringLiteral("org.kde.kmail2");
        kmail.appKey = QStringLiteral("org.kde.kmail2");
        kmail.name = QStringLiteral("KMail");
        kmail.caption = QStringLiteral("Inbox — KMail");
        kmail.windowId = QStringLiteral("win-kmail-1");
        kmail.running = true;
        ball.dockModel()->setOpenWindowsForTest({kmail});

        NotificationItem mailNotif;
        mailNotif.appName = QStringLiteral("KMail");
        mailNotif.desktopEntry = QStringLiteral("org.kde.kmail2");
        mailNotif.summary = QStringLiteral("招商银行账单");
        mailNotif.body = QStringLiteral("本月账单已出");
        ball.statusMonitor()->pushNotification(mailNotif);
        app.processEvents();

        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();

        int kmailSlotIdx = -1;
        for (int i = 0; i < ball.fanOverlay()->slotsData().size(); ++i) {
            const auto &s = ball.fanOverlay()->slotsData()[i];
            if (s.kind == RadialSlotItem::AppSlot && s.app.id == QLatin1String("org.kde.kmail2")) {
                kmailSlotIdx = i;
                break;
            }
        }
        check(kmailSlotIdx >= 0 &&
                  ball.fanOverlay()->slotsData()[kmailSlotIdx].app.demandsAttention &&
                  ball.fanOverlay()->slotsData()[kmailSlotIdx].unreadCount == 1 &&
                  ball.fanOverlay()->slotsData()[kmailSlotIdx].notificationPreview.contains(QStringLiteral("招商银行账单")),
              "open-window notification binds exact red badge count and message preview to matching window orb");

        // Hover over KMail orb and verify preview appears in activeLabelText
        const QPointF kmailCenter = ball.fanOverlay()->slotsData()[kmailSlotIdx].centerGlobal;
        ball.fanOverlay()->updateFinger(kmailCenter, kmailSlotIdx);
        check(ball.fanOverlay()->activeLabelText().contains(QStringLiteral("招商银行账单")),
              "hovering notifying window orb reveals exact notification summary in callout pill");

        // Clicking KMail clears BOTH window attention AND unread count -> zero ghost red dot!
        ball.executeSlotItem(ball.fanOverlay()->slotsData()[kmailSlotIdx]);
        ball.closeFan(true);
        app.processEvents();
        check(ball.statusMonitor()->snapshot().unreadNotifications == 0 &&
                  ball.attentionAnglesRad().isEmpty(),
              "activating notifying open window clears unread notification and leaves zero ghost red dot");

        // 11c. Background / tray app notification (no open window) -> surfaces NotificationSlot orb + Notification Center!
        NotificationItem tg1;
        tg1.appName = QStringLiteral("Telegram Desktop");
        tg1.desktopEntry = QStringLiteral("org.telegram.desktop");
        tg1.summary = QStringLiteral("Alice");
        tg1.body = QStringLiteral("晚上吃什么？");
        ball.statusMonitor()->pushNotification(tg1);
        NotificationItem tg2 = tg1;
        tg2.body = QStringLiteral("楼下新开了家拉面");
        ball.statusMonitor()->pushNotification(tg2);
        app.processEvents();

        ball.fanOverlay()->openTapFan();
        ball.fanOverlay()->syncProgress(1.0);
        app.processEvents();

        int notifSlotIdx = -1;
        for (int i = 0; i < ball.fanOverlay()->slotsData().size(); ++i) {
            if (ball.fanOverlay()->slotsData()[i].kind == RadialSlotItem::NotificationSlot) {
                notifSlotIdx = i;
                break;
            }
        }
        check(notifSlotIdx >= 0 &&
                  ball.fanOverlay()->slotsData()[notifSlotIdx].unreadCount == 2 &&
                  ball.fanOverlay()->slotsData()[notifSlotIdx].label == QStringLiteral("Telegram Desktop") &&
                  !ball.attentionAnglesRad().isEmpty(),
              "background/tray unread notifications surface a grouped NotificationSlot orb with exact count and radar bearing");

        ball.fanOverlay()->updateFinger(ball.fanOverlay()->slotsData()[notifSlotIdx].centerGlobal, notifSlotIdx);
        check(ball.fanOverlay()->activeLabelText().contains(QStringLiteral("Telegram Desktop")) &&
                  ball.fanOverlay()->activeLabelText().contains(QStringLiteral("2条未读")),
              "hovering NotificationSlot reveals sender app, unread count, and latest message preview");
        check(ball.fanOverlay()->grab().save(QString::fromLocal8Bit(argv[1]) + "/notif-fan-preview.png"),
              "saved fan preview with NotificationSlot orb outside source");
        ball.closeFan(true);

        // Verify Control Center Notification Center (PageNotifications) displays & clears cleanly
        ball.openSystemPopover(SystemPopover::TabNotifications);
        app.processEvents();
        check(ball.systemPopover()->isVisible() &&
                  ball.systemPopover()->currentPage() == SystemPopover::PageNotifications,
              "Control Center opens built-in Liquid Glass Notification Center directly");
        check(ball.systemPopover()->grab().save(QString::fromLocal8Bit(argv[1]) + "/notif-center-preview.png"),
              "saved Notification Center preview outside source");

        ball.statusMonitor()->clearAllNotifications();
        app.processEvents();
        check(ball.statusMonitor()->notifications().isEmpty() &&
                  ball.statusMonitor()->snapshot().unreadNotifications == 0 &&
                  ball.attentionAnglesRad().isEmpty(),
              "clearing Notification Center removes history and extinguishes all red dots");
        ball.systemPopover()->hide();
        ball.dockModel()->setOpenWindowsForTest({});
    }

    // Restore original clipboard
    if (savedClip) app.clipboard()->setMimeData(savedClip);
    else app.clipboard()->clear();

    return 0;
}
