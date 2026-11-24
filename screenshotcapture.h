#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QVariantMap>
#include <QWidget>
#include <memory>

class QProcess;
class QScreen;
class QTemporaryDir;

struct CaptureOptions {
    enum Mode { Region, Window, CurrentScreen, AllScreens };
    Mode mode = Region;
    int delaySeconds = 0;
    bool includeCursor = false;
};

// A frozen screen with screen-local logical coordinates and native image pixels.
class ScreenshotRegionSelector : public QWidget {
    Q_OBJECT
public:
    ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry,
                             const QList<QRect> &windows = {});
    static QRect pixelRect(const QSize &imageSize, const QSize &logicalSize, const QRect &selection);
    static QImage crop(const QImage &image, const QSize &logicalSize, const QRect &selection);
signals:
    void selected(const QImage &image, const QRect &globalSelection);
    void cancelled();
protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
private:
    QPoint bounded(const QPoint &point) const;
    QRect selection() const;
    QRect cancelPillRect() const;
    QImage frame;
    QPoint first, current;
    bool dragging = false;
    bool resolved = false;
    QList<QRect> windowRects;
};

class ScreenshotCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenshotCapture(QObject *parent = nullptr);
    ~ScreenshotCapture() override;
    bool isActive() const { return active; }
    void start(const QPoint &anchor, const CaptureOptions &options = {});
    // Direct silent region capture used by fooView crosshair dwell-and-drag.
    void startDirectRect(QScreen *screen, const QRect &globalRect, int settleMs = 90);
    void cancel();
signals:
    void finished(const QImage &image, const QString &error, bool cancelled);
    void countdownChanged(int seconds);
private slots:
#ifdef Q_OS_LINUX
    void portalResponse(uint response, const QVariantMap &results);
#endif
private:
    void captureDesktop(const QPoint &anchor);
    void captureSilentRect(QScreen *screen, const QRect &globalRect);
    void finish(const QImage &image = {}, const QString &error = {}, bool cancelled = false);
    void cleanUp();
    bool active = false;
    quint64 generation = 0;
    QList<QPointer<ScreenshotRegionSelector>> selectors;
    QPointer<QProcess> process;
    std::unique_ptr<QTemporaryDir> temporary;
    QString portalPath;
    CaptureOptions options;
    QRect pendingGlobalRect;
};
