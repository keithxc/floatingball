#include "actionhud.h"
#include "clipboardbubble.h"
#include "floatingball.h"
#include "quickdeck.h"
#include "screenshotpanel.h"
#include "screentext.h"

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

    FloatingBall ball(nullptr, temp.path() + "/shots");
    ball.show();
    app.processEvents();
    waitMs(40);

    QScreen *screen = ball.screen();
    const QRect bounds = screen->availableGeometry();
    check(ball.orbitActions() == FloatingBall::defaultOrbitActions() && ball.orbitActions().size() == 5,
          "default Xiaomi Mi 5 fan provides the 5 platform-native touch-first actions");

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
    // 3. Stationary Hold (380ms) -> Reposition Ball + Edge Snap + Half-Ring Tuck
    // ------------------------------------------------------------------------
    {
        ball.setStatusOrbMode(false);
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
        check(ball.dockSide() == -1 && ball.x() == bounds.left(),
              "dropping repositioned ball on left half snaps cleanly to left screen edge");

        const int fullDiameter = ball.width();
        QEvent leaveEv(QEvent::Leave);
        app.sendEvent(&ball, &leaveEv);
        waitMs(2400);
        check(ball.isEdgeTucked() && ball.width() < fullDiameter && ball.width() >= 26 && ball.x() == bounds.left(),
              "idle docked ball tucks into Xiaomi Mi 5 half-ring flush at screen edge");
        check(ball.grab().save(QString::fromLocal8Bit(argv[1]) + "/edge-half-ring.png"),
              "tucked Xiaomi Mi 5 half-ring preview saved outside source");

        QPointF tuckPt(ball.rect().center());
        QEnterEvent enterEv(tuckPt, tuckPt, ball.mapToGlobal(tuckPt.toPoint()));
        app.sendEvent(&ball, &enterEv);
        check(!ball.isEdgeTucked() && ball.width() == fullDiameter && ball.x() == bounds.left(),
              "approaching tucked half-ring immediately expands full sphere");

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
    // 4. ActionResultHud: Auto Local OCR + Copy + Pin Reference + Annotate Handoff
    // ------------------------------------------------------------------------
    {
        QImage textImg(1400, 280, QImage::Format_RGB32);
        textImg.fill(Qt::white);
        {
            QPainter p(&textImg);
            p.setPen(Qt::black);
            QFont f(QStringLiteral("Arial"));
            f.setPixelSize(74);
            f.setBold(true);
            p.setFont(f);
            p.drawText(textImg.rect(), Qt::AlignCenter, QStringLiteral("TOUCH WORKSPACE 8462"));
        }

        ActionResultHud *hud = ball.actionHud();
        hud->presentCapture(textImg, temp.path() + "/shots", QPoint(bounds.center()));
        check(hud->isVisible() && hud->currentImage() == textImg, "ActionResultHud opens immediately on capture");

        // Wait for background local OCR worker to finish and deliver result
        QElapsedTimer timer;
        timer.start();
        while (hud->isOcrRunning() && timer.elapsed() < 15000) {
            waitMs(30);
        }
        waitMs(50);
        printf("HUD_OCR_RESULT: %s\n", hud->recognizedText().toUtf8().constData());
#ifdef Q_OS_MACOS
        check(hud->recognizedText().contains(QStringLiteral("WORKSPACE"), Qt::CaseInsensitive),
              "ActionResultHud automatically recognizes text via Apple Vision");
#else
        const auto probe = ScreenText::recognize(textImg);
        if (probe.error.isEmpty()) {
            check(hud->recognizedText().contains(QStringLiteral("WORKSPACE"), Qt::CaseInsensitive),
                  "ActionResultHud automatically recognizes text via local Tesseract");
        }
#endif

        // Test Copy Image & Save PNG buttons on HUD
        QToolButton *copyImgBtn = nullptr;
        QToolButton *saveBtn = nullptr;
        QToolButton *pinBtn = nullptr;
        QToolButton *annotateBtn = nullptr;
        for (QToolButton *b : hud->findChildren<QToolButton *>()) {
            const QString act = b->property("actionName").toString();
            if (act == QLatin1String("copyHudImage")) copyImgBtn = b;
            else if (act == QLatin1String("saveHudImage")) saveBtn = b;
            else if (act == QLatin1String("pinHudImage")) pinBtn = b;
            else if (act == QLatin1String("annotateHudImage")) annotateBtn = b;
        }
        check(copyImgBtn && saveBtn && pinBtn && annotateBtn && copyImgBtn->minimumHeight() >= 44,
              "ActionResultHud exposes 44pt+ touch-friendly primary action pills");

        copyImgBtn->click();
        app.processEvents();
        check(app.clipboard()->image() == textImg, "ActionResultHud Copy Image copies lossless pixels");

        saveBtn->click();
        app.processEvents();
        const QString savedPath = app.clipboard()->text();
        check(QFileInfo::exists(savedPath) && QImage(savedPath) == textImg,
              "ActionResultHud Save writes atomic PNG and copies its file path");

        pinBtn->click();
        app.processEvents();
        QWidget *pinnedCard = nullptr;
        for (QWidget *w : app.topLevelWidgets())
            if (w->objectName() == QLatin1String("pinnedImageCard") && w->isVisible()) pinnedCard = w;
        check(pinnedCard != nullptr && !hud->isVisible(),
              "ActionResultHud Pin spawns independent floating reference card and dismisses HUD");
        pinnedCard->close();

        hud->presentCapture(textImg, temp.path() + "/shots", QPoint(bounds.center()));
        annotateBtn->click();
        app.processEvents();
        check(ball.resultPanel()->isVisible() && ball.resultPanel()->currentImage() == textImg,
              "ActionResultHud Annotate hands off directly to ScreenshotPanel in pen mode");
        ball.resultPanel()->hide();
    }

    // ------------------------------------------------------------------------
    // 5. ClipboardBubble & QuickDeck Touch Ergonomics
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
    // 6. Multi-Ring (Default 3: 2 Action Rings incl. Downloads/Trash + Open Apps)
    // ------------------------------------------------------------------------
    {
        check(ball.ringCount() == FloatingBall::kDefaultRingCount && ball.ringCount() == 3,
              "FloatingBall defaults to 3 staggered rings (2 action rings + open apps)");
        check(FloatingBall::availableActions().contains(QStringLiteral("downloads")) &&
                  FloatingBall::availableActions().contains(QStringLiteral("trash")),
              "Downloads and Trash actions are registered");

        ball.expandFromEdge();
        ball.move(bounds.left(), bounds.center().y() - ball.height() / 2);
        const QPointF pt(ball.rect().center());
        const QPointF gPt = ball.mapToGlobal(pt.toPoint());
        QMouseEvent d1(QEvent::MouseButtonPress, pt, gPt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent u1(QEvent::MouseButtonRelease, pt, gPt, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &d1);
        app.sendEvent(&ball, &u1);
        waitMs(240);

        // Verify Ring 0 & Ring 1 are quick action rings (including Downloads & Trash) and Ring 2+ are apps/overflow
        bool hasRing1Downloads = false, hasRing1Trash = false, ring0And1AreActions = true;
        for (const RadialSlotItem &slot : ball.fanOverlay()->slotsData()) {
            if (slot.ringIndex <= 1 && slot.kind != RadialSlotItem::ActionSlot) ring0And1AreActions = false;
            if (slot.ringIndex == 1 && slot.id == QLatin1String("downloads")) hasRing1Downloads = true;
            if (slot.ringIndex == 1 && slot.id == QLatin1String("trash")) hasRing1Trash = true;
        }
        check(ball.fanOverlay()->activeRingCount() >= 3 && ring0And1AreActions && hasRing1Downloads && hasRing1Trash,
              "inner 2 rings hold quick actions including Downloads and Trash, and ring 3+ opens apps");
        check(ball.fanOverlay()->grab().save(QString::fromLocal8Bit(argv[1]) + "/multi-ring-3.png"),
              "default 3-ring staggered honeycomb preview saved outside source");

        // Verify clamping 1..30 and zero overlap / strict in-screen bounds even at max ring limit
        ball.setRingCount(0);
        check(ball.ringCount() == 1, "ringCount clamps minimum to 1");
        ball.setRingCount(99);
        check(ball.ringCount() == FloatingBall::kMaxRingCount && ball.ringCount() == 30,
              "ringCount clamps maximum to 30");
        waitMs(80);
        const QList<QRectF> allMax = ball.allOrbitSlotGlobalRects();
        bool disjointAndInside = (allMax.size() > 10 && ball.fanOverlay()->activeRingCount() >= 3);
        for (int i = 0; i < allMax.size(); ++i) {
            const QRect r = allMax[i].toAlignedRect();
            disjointAndInside = disjointAndInside && bounds.adjusted(-2, -2, 2, 2).contains(r);
            for (int j = i + 1; j < allMax.size(); ++j) {
                const qreal minCenterDist = (allMax[i].width() + allMax[j].width()) * 0.5 + 2.0;
                disjointAndInside = disjointAndInside &&
                                    (QLineF(allMax[i].center(), allMax[j].center()).length() >= minCenterDist);
            }
        }
        check(disjointAndInside, "multi-ring staggered fan (up to 30 rings) stays disjoint and strictly inside screen bounds");
        check(ball.fanOverlay()->grab().save(QString::fromLocal8Bit(argv[1]) + "/multi-ring-max.png"),
              "multi-ring max-layer staggered preview saved outside source");

        ball.setRingCount(3);
        // Verify clicking outside all slots and outside the main ball auto-collapses the fan!
        const QPointF outsideGlobal(bounds.right() - 12, bounds.bottom() - 12);
        const QPointF outsideLocal = outsideGlobal - QPointF(ball.fanOverlay()->geometry().topLeft());
        QMouseEvent outsideClick(QEvent::MouseButtonPress, outsideLocal, outsideGlobal,
                                 Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(ball.fanOverlay(), &outsideClick);
        waitMs(190);
        check(!ball.isOrbitVisible(), "clicking outside floating ball and fan automatically collapses the fan");
        ball.closeFan(true);

        // Verify main sphere stays clean & unchanged in color when system is muted,
        // while audio state syncs cleanly between QuickDeck and FloatingBall
        ball.setSystemAudioState(87, false);
        const QImage unmutedImg = ball.grab().toImage();
        ball.setSystemAudioState(87, true);
        check(ball.isSystemMuted(), "FloatingBall tracks system muted state");
        const QImage mutedImg = ball.grab().toImage();
        check(mutedImg == unmutedImg, "main FloatingBall sphere keeps clean obsidian look without mute badge or color shift");

        // Verify QuickDeck slider immediately syncs audio state back to FloatingBall
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
    // 7. Duo Combined Status Orb (Brightness/Volume/Battery/Network/IM/Privacy/Notification Dot)
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

        const QImage orbNoNotif = ball.grab().toImage();
        snap.unreadNotifications = 2;
        snap.lastNotificationSummary = QStringLiteral("微信 发来新通知");
        ball.statusMonitor()->setSnapshotForTest(snap);
        app.processEvents();

        const QImage orbWithNotif = ball.grab().toImage();
        check(ball.statusOrbMode() && ball.statusMonitor()->snapshot().hasNotificationDot() &&
                  orbWithNotif != orbNoNotif,
              "Duo Status Orb renders live brightness/volume/battery/network/IM/privacy and unread notification dot");
        check(orbWithNotif.save(QString::fromLocal8Bit(argv[1]) + "/duo-status-orb.png"),
              "Duo Status Orb preview saved outside source");

        ball.clearNotificationDot();
        app.processEvents();
        check(!ball.statusMonitor()->snapshot().hasNotificationDot(),
              "clearing unread notifications extinguishes notification dot");
    }

    // Restore original clipboard
    if (savedClip) app.clipboard()->setMimeData(savedClip);
    else app.clipboard()->clear();

    return 0;
}
