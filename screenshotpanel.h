#pragma once
#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QStringList>
#include <QPointer>
#include <QColor>
#include <QPainterPath>
#include "screenshotcapture.h"
class ScreenshotCapture;
class ScreenRecorder;
class QFileSystemWatcher;
class QTimer;
class QLabel;
class QListWidget;
class QToolButton;
class ScreenshotCanvas : public QWidget {
public:
    explicit ScreenshotCanvas(QWidget *parent = nullptr);
    QImage image;
    bool editing = false;
    bool dirty = false;
    QList<QImage> history;
    enum Tool { Pen, Arrow, Rectangle, Marker, Mosaic, Text };
    Tool tool=Pen;
    QColor ink=QColor("#ff637d");
    int strokeWidth=4;
    void setImage(const QImage &value);
    void undo();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    QRect imageRect() const;
    QPoint imagePoint(const QPoint &point) const;
    QPoint last;
    QPoint first;
    QPainterPath markerPath;
    QList<bool> undoDirtyStates;
    bool drawing = false;
};
class ScreenshotPanel : public QWidget {
public:
    explicit ScreenshotPanel(QWidget *parent = nullptr, const QString &directoryOverride = QString());
    ~ScreenshotPanel() override;
    void reveal(const QPoint &anchor, bool toggle=true);
    void captureRegion(const QPoint &anchor);
    void takeScreenshot(const QPoint &anchor, const CaptureOptions &options);
    void showCapture(const QImage &image, const QPoint &anchor);
    void startRecording(bool window=false);
protected:
    void keyPressEvent(QKeyEvent *) override;
private:
    void refresh();
    void select(int row);
    void chooseDirectory();
    void importClipboard();
    void updateWatcher();
    QFileSystemWatcher *watcher;
    QTimer *refreshDelay;
    QByteArray directorySignature;
    ScreenshotCanvas *canvas;
    QListWidget *thumbnails;
    QLabel *status;
    QLabel *details;
    QToolButton *edit;
    QString directory;
    QString currentPath;
    QStringList paths;
    ScreenshotCapture *capture;
    QPoint captureAnchor;
    QList<QPointer<QWidget>> hiddenForCapture;
    QWidget *captureProgress=nullptr;
    bool transientImage=false;
    ScreenRecorder *recorder;
    QString recordingPath;
    void restoreFloatingWindows();
};
