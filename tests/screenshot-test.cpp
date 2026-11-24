#include "screenshotpanel.h"
#include "floatingball.h"
#include "screenshotstore.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineF>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_MACOS
void checkNativeFloatingWindows(const QList<QWidget *> &windows, QWidget *panel);
int runNativeFocusFixture();
#endif

class ClipboardGuard {
public:
    ClipboardGuard()
    {
        const auto original = QApplication::clipboard()->mimeData();
        if (original) {
            saved = new QMimeData;
            for (const auto &format : original->formats())
                saved->setData(format, original->data(format));
            if (original->hasImage()) saved->setImageData(original->imageData());
        }
    }
    ~ClipboardGuard() { restore(); }
    void restore()
    {
        if (restored) return;
        restored = true;
        if (saved) {
            QApplication::clipboard()->setMimeData(saved);
            saved = nullptr;
        } else {
            QApplication::clipboard()->clear();
        }
    }
private:
    QMimeData *saved = nullptr;
    bool restored = false;
};

ClipboardGuard *clipboardGuard = nullptr;
void check(bool ok, const char *message)
{
    if (!ok) {
        if (clipboardGuard) clipboardGuard->restore();
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
    printf("PASS: %s\n", message);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
#ifdef Q_OS_MACOS
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--native-focus-fixture"))
        return runNativeFocusFixture();
#else
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--native-focus-fixture")) {
        QWidget window;
        window.setWindowTitle("FloatingBall application-switch fixture");
        window.setGeometry(280, 280, 600, 450);
        window.show();
        window.activateWindow();
        QTimer::singleShot(8000, &app, &QApplication::quit);
        return app.exec();
    }
#endif
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--native-folder-fixture")) {
        ScreenshotPanel panel(nullptr, QString::fromLocal8Bit(argv[2]));
        panel.reveal(QPoint(100, 300));
        QTimer::singleShot(1200, &panel, [&panel] { panel.findChild<QToolButton *>("chooseDirectory")->click(); });
        QTimer::singleShot(6500, &app, [] {
            for (auto w : QApplication::topLevelWidgets())
                if (auto d = qobject_cast<QDialog *>(w)) d->reject();
        });
        QTimer::singleShot(9000, &app, &QApplication::quit);
        return app.exec();
    }
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--native-clipboard-fixture")) {
        ClipboardGuard clipboard;
        const QString path = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
        if (QImage(path).isNull()) return 1;
        ScreenshotPanel panel(nullptr, QFileInfo(path).absolutePath());
        panel.reveal(QPoint(100, 300));
        QTimer::singleShot(400, &app, [&panel] {
            for (const char *name : {"copyImage", "copyPath"}) {
                auto button = panel.findChild<QToolButton *>(name);
                const QPoint point = button->mapToGlobal(button->rect().center());
                printf("CLIPBOARD_BUTTON: %s %d %d\n", name, point.x(), point.y());
            }
            fflush(stdout);
        });
        QTimer::singleShot(18000, &app, &QApplication::quit);
        return app.exec();
    }

    ClipboardGuard clipboard;
    clipboardGuard = &clipboard;
    check(argc == 2, "explicit external test directory supplied");
    check(QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])), "test directory is absolute");
    check(QDir::isAbsolutePath(ScreenshotStore::defaultDirectory()), "platform default screenshot directory resolves");
    printf("DEFAULT_SCREENSHOT_DIRECTORY: %s\n", ScreenshotStore::defaultDirectory().toUtf8().constData());

    QDir().mkpath(QString::fromLocal8Bit(argv[1]));
    QTemporaryDir temporary(QString::fromLocal8Bit(argv[1]) + "/screenshot-test-XXXXXX");
    check(temporary.isValid(), "owned test directory created");
    QString root = temporary.path();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root + "/settings");
    QDir().mkpath(root + "/fixtures");
    QString file = root + "/fixtures/Screenshot.png";

    QImage original(900, 500, QImage::Format_ARGB32);
    original.fill(QColor("#d5e8f9"));
    {
        QPainter p(&original);
        p.setPen(QColor("#263852"));
        QFont f = p.font();
        f.setPixelSize(36);
        p.setFont(f);
        p.drawText(original.rect(), Qt::AlignCenter, "FloatingBall\nScreenshot workspace");
    }
    check(original.save(file), "fixture created");
    check(original.save(QString::fromLocal8Bit(argv[1]) + "/sample.png"), "sample preview saved outside source");
    QSettings(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall")
        .setValue("screenshots/directory", root + "/fixtures");

    ScreenshotPanel panel;
    panel.reveal(QPoint(100, 300));
    app.processEvents();
    auto list = panel.findChild<QListWidget *>();
    check(list && list->count() == 1, "recent image loaded");

    QString copyError;
    QImage transparent(3, 2, QImage::Format_ARGB32);
    transparent.fill(QColor(90, 140, 200, 80));
    const bool transparentCopied = ScreenshotStore::copyImage(transparent, &copyError);
    app.processEvents();
    check(transparentCopied && app.clipboard()->image() == transparent, "image clipboard preserves transparent pixels");

    auto click = [&](const QString &text) {
        for (auto b : panel.findChildren<QToolButton *>()) {
            if (b->text() == text) {
                b->click();
                return;
            }
        }
        check(false, "button exists");
    };
    QToolButton *pin = nullptr;
    for (auto b : panel.findChildren<QToolButton *>())
        if (b->text() == QStringLiteral("固定")) pin = b;
    check(pin && pin->isChecked(), "screenshot panel starts pinned for use alongside other apps");

    click(QStringLiteral("复制图片"));
    check(app.clipboard()->image() == original, "image clipboard matches original");
    click(QStringLiteral("复制路径"));
    check(app.clipboard()->text() == file, "clipboard path is absolute original path");

    ScreenshotCanvas *canvas = nullptr;
    for (auto child : panel.findChildren<QWidget *>())
        if (auto c = dynamic_cast<ScreenshotCanvas *>(child)) canvas = c;
    check(canvas != nullptr, "canvas exists");

    QPoint canvasTop = canvas->mapTo(&panel, QPoint());
    QPoint listTop = list->mapTo(&panel, QPoint());
    check(!list->isVisible() || canvasTop.y() + canvas->height() <= listTop.y(),
          "preview and visible thumbnails do not overlap");

    click(QStringLiteral("画笔批注"));
    QPointF a(canvas->width() / 2 - 20, canvas->height() / 2), b(canvas->width() / 2 + 50, canvas->height() / 2 + 30);
    QMouseEvent press(QEvent::MouseButtonPress, a, a, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    app.sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, b, b, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    app.sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, b, b, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    app.sendEvent(canvas, &release);
    check(canvas->image != original, "annotation changes canvas");
    check(QImage(file) == original, "annotation preserves original file");

    click(QStringLiteral("复制图片"));
    check(app.clipboard()->image() == canvas->image, "annotated image can be copied");
    check(panel.grab().save(QString::fromLocal8Bit(argv[1]) + "/panel.png"), "panel preview saved outside source");
    click(QStringLiteral("撤销"));
    check(canvas->image == original, "undo restores original");

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    app.sendEvent(&panel, &escape);
    check(!panel.isVisible(), "Escape hides panel");

    QString error;
    check(ScreenshotStore::importImage({}, root + "/empty", &error).isEmpty() && !error.isEmpty() &&
              !QFileInfo::exists(root + "/empty"),
          "empty clipboard does not create files");
    QString imported = ScreenshotStore::importImage(original, root + "/new-folder", &error);
    check(!imported.isEmpty() && QImage(imported) == original, "import creates configured directory and lossless PNG");
    QString second = ScreenshotStore::importImage(original, root + "/new-folder", &error);
    check(imported != second && QFileInfo::exists(imported), "repeated imports preserve previous files");
    check(ScreenshotStore::importImage(original, file + "/blocked", &error).isEmpty() && !error.isEmpty(),
          "unwritable destination reports failure");

    app.clipboard()->setImage(original);
    click(QStringLiteral("从剪贴板导入"));
    check(list->count() == 2, "clipboard import is shown immediately");
    click(QStringLiteral("复制路径"));
    check(app.clipboard()->text() != file && QImage(app.clipboard()->text()) == original,
          "imported image has a usable absolute path");

    for (int i = 0; i < 6; ++i)
        check(original.save(root + "/fixtures/Image-" + QString::number(i) + ".PNG", "PNG"),
              "uppercase PNG fixture created");
    QFile incomplete(root + "/fixtures/Pending.png");
    check(incomplete.open(QIODevice::WriteOnly), "partial screenshot fixture opened");
    incomplete.write("partial");
    incomplete.close();
    panel.reveal(QPoint(100, 300));
    app.processEvents();
    check(list->count() == 8, "all folder images appear, uppercase accepted, incomplete ignored");

    QWheelEvent wheel(QPointF(10, 10), list->mapToGlobal(QPoint(10, 10)), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    app.sendEvent(list->viewport(), &wheel);
    check(list->horizontalScrollBar()->value() > 0, "mouse wheel scrolls the horizontal screenshot gallery");

    QImage changed = original;
    changed.fill(Qt::green);
    check(changed.save(root + "/fixtures/Pending.png"), "partial capture completed");
    auto wait = [&](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    wait(2300);
    check(list->count() == 9, "watcher or fallback discovers completed capture");
    check(QFile::remove(root + "/fixtures/Image-0.PNG"), "one folder image removed");
    wait(2300);
    check(list->count() == 8, "deleted image disappears from list");
    click(QStringLiteral("收起 ×"));

    // ========================================================================
    // Unified Single-Canvas Xiaomi Mi 5 5-Slot Fan & Native Stacking Checks
    // ========================================================================
    FloatingBall ball;
    ball.show();
    app.processEvents();
    check(ball.grab().save(QString::fromLocal8Bit(argv[1]) + "/ball.png"), "ball preview saved outside source");

    auto tapBall = [&]() {
        const QPointF pt(ball.rect().center());
        const QPointF globalPt(ball.mapToGlobal(pt.toPoint()));
        QMouseEvent down(QEvent::MouseButtonPress, pt, globalPt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(&ball, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, pt, globalPt, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(&ball, &up);
    };

    tapBall();
    wait(220);
    RadialFanOverlay *fan = ball.fanOverlay();
    check(ball.isOrbitVisible() && fan && fan->isVisible() && ball.orbitSlotGlobalRects().size() == 5,
          "single tap opens the unified 5-slot Xiaomi Mi 5 semicircle fan without delay");

    // Verify hovering across all 5 slots causes ZERO geometry jitter on both ball and slots!
    const QRect initialBallGeo = ball.geometry();
    const QList<QRectF> initialSlotRects = ball.orbitSlotGlobalRects();
    bool hoverStable = true;
    for (int i = 0; i < initialSlotRects.size(); ++i) {
        const QPointF globalCenter = initialSlotRects[i].center();
        const QPointF localCenter = globalCenter - QPointF(fan->geometry().topLeft());
        QMouseEvent hoverMove(QEvent::MouseMove, localCenter, globalCenter,
                              Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        app.sendEvent(fan, &hoverMove);
        hoverStable = hoverStable && (fan->highlightedSlot() == i);
        for (int frame = 0; frame < 6; ++frame) {
            wait(16);
            hoverStable = hoverStable && (ball.geometry() == initialBallGeo) &&
                          (ball.orbitSlotGlobalRects() == initialSlotRects);
        }
    }
    check(hoverStable, "hovering across all 5 radial slots highlights cleanly with zero geometry jitter");

    check(fan->grab().save(QString::fromLocal8Bit(argv[1]) + "/orbit.png"), "radial fan preview saved outside source");

    // Configure custom 5 slots including screenshots and verify slot tap opens ScreenshotPanel
    const QStringList customSlots{"back", "screenshots", "crosshair", "clipboard", "deck"};
    check(!ball.setOrbitActions({"back", "back", "crosshair", "clipboard", "deck"}),
          "duplicate radial slot assignments are rejected");
    check(!ball.setOrbitActions({"back", "unknown", "crosshair", "clipboard", "deck"}),
          "unknown radial slot assignments are rejected");
    check(ball.setOrbitActions(customSlots) && ball.orbitActions() == customSlots,
          "five distinct radial actions can be configured");
    wait(220);
    {
        FloatingBall restored;
        check(restored.orbitActions() == customSlots, "5-slot radial order persists across sessions");
    }

    // Tap the 'screenshots' slot (index 1) on the single-canvas fan
    const QPointF shotGlobal = ball.orbitSlotGlobalRects().at(1).center();
    const QPointF shotLocal = shotGlobal - QPointF(fan->geometry().topLeft());
    QMouseEvent slotDown(QEvent::MouseButtonPress, shotLocal, shotGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    app.sendEvent(fan, &slotDown);
    QMouseEvent slotUp(QEvent::MouseButtonRelease, shotLocal, shotGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    app.sendEvent(fan, &slotUp);
    wait(180);
    check(!ball.isOrbitVisible() && ball.resultPanel()->isVisible(),
          "tapping screenshots radial slot opens workbench and collapses fan");
    ball.resultPanel()->hide();

    // Verify all 5 slots remain disjoint and within screen bounds at all 4 corners and edges
    QScreen *display = ball.screen();
    const QRect bounds = display->availableGeometry();
    const QList<QPoint> edgePositions{
        bounds.topLeft(),
        QPoint(bounds.right() - ball.width() + 1, bounds.top()),
        QPoint(bounds.left(), bounds.bottom() - ball.height() + 1),
        QPoint(bounds.right() - ball.width() + 1, bounds.bottom() - ball.height() + 1),
        QPoint(bounds.left(), bounds.center().y() - ball.height() / 2),
        QPoint(bounds.right() - ball.width() + 1, bounds.center().y() - ball.height() / 2)
    };
    for (const QPoint &pos : edgePositions) {
        ball.expandFromEdge();
        ball.move(pos);
        tapBall();
        wait(220);
        const QList<QRectF> rects = ball.orbitSlotGlobalRects();
        bool fits = (rects.size() == 5);
        for (int i = 0; i < rects.size(); ++i) {
            const QRect aligned = rects[i].toAlignedRect();
            const qreal ballClear = (ball.width() + rects[i].width()) * 0.5 + 2.0;
            fits = fits && bounds.adjusted(-2, -2, 2, 2).contains(aligned) &&
                   QLineF(rects[i].center(), QRectF(ball.geometry()).center()).length() >= ballClear;
            for (int j = i + 1; j < rects.size(); ++j) {
                const qreal minCenter = (rects[i].width() + rects[j].width()) * 0.5 + 2.0;
                fits = fits && QLineF(rects[i].center(), rects[j].center()).length() >= minCenter;
            }
        }
        check(fits, "all 5 Xiaomi Mi 5 radial slots stay inside screen bounds without overlapping");
        ball.closeFan(true);
    }

    // Native Cocoa elevated non-activating tool window verification
    panel.reveal(QPoint(100, 300));
    tapBall();
    wait(220);
    QList<QWidget *> floatingWindows{&ball, fan, &panel};
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
        checkNativeFloatingWindows(floatingWindows, &panel);
#endif

    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QWidget ordinary;
    ordinary.setWindowTitle("FloatingBall focus-loss fixture");
    ordinary.show();
    ordinary.activateWindow();
    QApplication::setActiveWindow(&ordinary);
    ordinary.setFocus();
    wait(50);
    check(panel.isVisible(), "pinned screenshot survives focus moving to another window");
    panel.activateWindow();
    QApplication::setActiveWindow(&panel);
    canvas->setFocus();
    wait(50);
    pin->click();
    ordinary.activateWindow();
    QApplication::setActiveWindow(&ordinary);
    ordinary.setFocus();
    QT_WARNING_POP
    wait(50);
    check(!panel.isVisible(), "unpinning restores focus-loss dismissal");
    check(ball.isVisible() && ball.isOrbitVisible(), "focus loss keeps main ball and expanded fan visible");

    return 0;
}
