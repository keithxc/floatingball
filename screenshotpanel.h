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
class QTabWidget;
class QTextEdit;
class QThread;
class ScreenshotCanvas : public QWidget {
    Q_OBJECT
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
    void beginCrop();
    void beginColorPick();
    void cancelImageAction();
    bool applyCrop();
    bool rotateClockwise();
    bool flipHorizontal();
    bool isCropping() const;
    bool isPickingColor() const;
    QRect cropSelection() const;
signals:
    void imageChanged();
    void imageActionChanged();
    void colorPicked(const QColor &color, const QPoint &pixel);
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    QRect imageRect() const;
    QPoint imagePoint(const QPoint &point) const;
    QPoint last;
    QPoint first;
    QPainterPath markerPath;
    QList<bool> undoDirtyStates;
    bool drawing = false;
    enum ImageAction { NoImageAction, CropImage, PickColor };
    ImageAction imageAction=NoImageAction;
    QRect pendingCrop;
    bool selectingCrop=false;
    QPoint cropStart;
    void rememberImage();
    bool replaceEditedImage(const QImage &value);
};
class ScreenshotPanel : public QWidget {
    Q_OBJECT
public:
    explicit ScreenshotPanel(QWidget *parent = nullptr, const QString &directoryOverride = QString());
    ~ScreenshotPanel() override;
    void reveal(const QPoint &anchor, bool toggle=true);
    void revealText(const QPoint &anchor);
    void captureRegion(const QPoint &anchor);
    void takeScreenshot(const QPoint &anchor, const CaptureOptions &options);
    void showCapture(const QImage &image, const QPoint &anchor);
    void openForAnnotation(const QImage &image, const QString &savedPath, const QPoint &anchor);
    QWidget *pinImageDirect(const QImage &image, const QString &name, const QPoint &anchor);
    QString screenshotDirectory() const { return directory; }
    void startRecording(bool window=false);
    QImage currentImage() const;
signals:
    void textNoteRequested(QString text, QString title);
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
    QTabWidget *resultTabs;
    QTextEdit *recognizedText;
    QLabel *textDetails;
    QPointer<QThread> textWorker;
    QList<QPointer<QWidget>> referenceCards;
    qint64 recognizedImageKey=0;
    void recognizeText();
    void pinCurrentImage();
    void updateImageActions();
    QString actionText() const;
    void restoreFloatingWindows();
};
