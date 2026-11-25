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

        // Verify sys_hub directly reveals macOS/iOS-style Control Center (PageHome)
        // with inline Wi-Fi / Bluetooth / Mihomo subpages and auto-hiding MPRIS bar
        ball.executeAction(QStringLiteral("sys_hub"));
        app.processEvents();
        check(ball.systemPopover() != nullptr && ball.systemPopover()->isVisible() &&
                  ball.systemPopover()->currentPage() == SystemPopover::PageHome,
              "sys_hub opens unified Control Center directly on Bento Home page");
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
    }

    // Restore original clipboard
    if (savedClip) app.clipboard()->setMimeData(savedClip);
    else app.clipboard()->clear();

    return 0;
}
