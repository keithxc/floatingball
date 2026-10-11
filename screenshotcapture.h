#pragma once

#include <QObject>
#include <QImage>
#include <QPointer>
#include <QWidget>
#include <memory>
#include <QVariantMap>

class QProcess;
class QTemporaryDir;

// A frozen screen with screen-local logical coordinates and native image pixels.
class ScreenshotRegionSelector : public QWidget {
    Q_OBJECT
public:
    ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry);
    static QRect pixelRect(const QSize &imageSize, const QSize &logicalSize, const QRect &selection);
    static QImage crop(const QImage &image, const QSize &logicalSize, const QRect &selection);
signals:
    void selected(const QImage &image);
    void cancelled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void closeEvent(QCloseEvent *) override;
private:
    QPoint bounded(const QPoint &point) const;
    QRect selection() const;
    QImage frame;
    QPoint first, current;
    bool dragging=false;
    bool resolved=false;
};

class ScreenshotCapture : public QObject {
    Q_OBJECT
public:
    explicit ScreenshotCapture(QObject *parent=nullptr);
    ~ScreenshotCapture() override;
    bool isActive() const { return active; }
    void start(const QPoint &anchor);
    void cancel();
signals:
    void finished(const QImage &image, const QString &error, bool cancelled);
private slots:
#ifdef Q_OS_LINUX
    void portalResponse(uint response, const QVariantMap &results);
#endif
private:
    void captureDesktop(const QPoint &anchor);
    void finish(const QImage &image={}, const QString &error={}, bool cancelled=false);
    void cleanUp();
    bool active=false;
    quint64 generation=0;
    QList<QPointer<ScreenshotRegionSelector>> selectors;
    QPointer<QProcess> process;
    std::unique_ptr<QTemporaryDir> temporary;
    QString portalPath;
};
