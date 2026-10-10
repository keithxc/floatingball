#pragma once
#include <QWidget>
#include <QImage>
#include <QPoint>
#include <QStringList>
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
    QList<QImage> history;
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
    bool drawing = false;
};
class ScreenshotPanel : public QWidget {
public:
    explicit ScreenshotPanel(QWidget *parent = nullptr, const QString &directoryOverride = QString());
    void reveal(const QPoint &anchor);
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
};
