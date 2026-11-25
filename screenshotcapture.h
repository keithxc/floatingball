#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QPolygonF>
#include <QRect>
#include <QVariantMap>
#include <QWidget>
#include <memory>

class QLineEdit;
class QProcess;
class QScreen;
class QTemporaryDir;
class QToolButton;

struct CaptureOptions {
    enum Mode { Region, Window, CurrentScreen, AllScreens };
    Mode mode = Region;
    int delaySeconds = 0;
    bool includeCursor = false;
};

enum class CaptureStudioAction {
    Default = 0, // Legacy immediate select (tests / workbench)
    Copy,        // Copy annotated image to clipboard & dismiss
    Save,        // Save lossless PNG & dismiss
    Pin,         // Pin annotated image onto desktop at selection rect
    Ocr          // Open compact OCR text extraction card
};

// A frozen screen with screen-local logical coordinates and native image pixels.
// When interactiveStudio=true, acts as a full Snipaste/PixPin/Flameshot-grade
// inline studio: 5x pixel magnifier + HEX color picker, auto window snap,
// 8-handle resizable/movable selection, inline vector/mosaic/callout/text
// annotations with undo, and a floating 2026 Liquid Glass action toolbar.
class ScreenshotRegionSelector : public QWidget {
    Q_OBJECT
public:
    ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry,
                             const QList<QRect> &windows = {},
                             bool interactiveStudio = false);
    static QRect pixelRect(const QSize &imageSize, const QSize &logicalSize, const QRect &selection);
    static QImage crop(const QImage &image, const QSize &logicalSize, const QRect &selection);

signals:
    void selected(const QImage &image, const QRect &globalSelection);
    void studioActionSelected(CaptureStudioAction action, const QImage &image, const QRect &globalSelection);
    void cancelled();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;

private:
    enum Tool {
        ToolNone = 0,
        ToolRect,
        ToolEllipse,
        ToolArrow,
        ToolPen,
        ToolMosaic,
        ToolStep,
        ToolText
    };
    enum Handle {
        HandleNone = 0,
        HandleMove,
        HandleTL, HandleT, HandleTR,
        HandleR, HandleBR, HandleB, HandleBL, HandleL
    };
    struct Annotation {
        Tool tool = ToolNone;
        QColor color;
        qreal width = 3.0;
        QPointF p1;
        QPointF p2;
        QPolygonF points;
        QString text;
        int stepNumber = 0;
    };

    QPoint bounded(const QPoint &point) const;
    QRect selection() const;
    QRect cancelPillRect() const;
    QRect hoveredWindowRect(const QPoint &localPos) const;
    Handle hitTestHandle(const QPoint &pos) const;
    void updateHoverCursor(const QPoint &pos);
    void buildStudioToolbar();
    void updateToolbarGeometry();
    void setActiveTool(Tool tool);
    void undoAnnotation();
    void commitInlineText();
    void finishStudioAction(CaptureStudioAction action);
    void paintAnnotations(QPainter &p, const QRect &clipLogical, qreal scaleX, qreal scaleY, const QPointF &originOffset) const;
    void paintMagnifierLoupe(QPainter &p) const;
    QImage renderAnnotatedCrop() const;
    QColor samplePixelColor(const QPoint &localPos) const;

    QImage frame;
    QPoint first, current;
    bool dragging = false;
    bool resolved = false;
    QList<QRect> windowRects;

    // Interactive Studio Mode state
    bool m_studioMode = false;
    bool m_hasSelection = false;
    QRect m_selRect;
    Handle m_dragHandle = HandleNone;
    QPoint m_dragStartPos;
    QRect m_dragStartRect;
    bool m_hoverPressActive = false;
    QPoint m_hoverPressPos;

    Tool m_tool = ToolNone;
    QColor m_color = QColor("#ef4444");
    qreal m_strokeWidth = 3.0;
    int m_nextStep = 1;
    bool m_drawingAnnotation = false;
    Annotation m_liveAnn;
    QList<Annotation> m_annotations;

    QWidget *m_toolbar = nullptr;
    QWidget *m_styleBar = nullptr;
    QList<QToolButton *> m_toolBtns;
    QToolButton *m_undoBtn = nullptr;
    QPointer<QLineEdit> m_inlineTextEdit;
    QPointF m_inlineTextAnchor;
};

class ScreenshotCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenshotCapture(QObject *parent = nullptr);
    ~ScreenshotCapture() override;
    bool isActive() const { return active; }
    void start(const QPoint &anchor, const CaptureOptions &options = {}, bool interactiveStudio = false);
    // Direct silent region capture used by fooView crosshair dwell-and-drag.
    void startDirectRect(QScreen *screen, const QRect &globalRect, int settleMs = 90);
    void cancel();
signals:
    void finished(const QImage &image, const QString &error, bool cancelled);
    void studioFinished(CaptureStudioAction action, const QImage &image, const QRect &globalSelection);
    void countdownChanged(int seconds);
private slots:
#ifdef Q_OS_LINUX
    void portalResponse(uint response, const QVariantMap &results);
#endif
private:
    void captureDesktop(const QPoint &anchor);
    void captureSilentRect(QScreen *screen, const QRect &globalRect);
    void launchSelector(const QImage &image, const QRect &screenBounds, const QList<QRect> &windows);
    void finish(const QImage &image = {}, const QString &error = {}, bool cancelled = false);
    void cleanUp();
    bool active = false;
    bool studioMode = false;
    quint64 generation = 0;
    QList<QPointer<ScreenshotRegionSelector>> selectors;
    QPointer<QProcess> process;
    std::unique_ptr<QTemporaryDir> temporary;
    QString portalPath;
    CaptureOptions options;
    QRect pendingGlobalRect;
};
