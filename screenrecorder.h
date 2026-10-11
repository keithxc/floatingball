#pragma once
#include <QObject>
#include <QPointer>
#include <QPoint>
class QMediaCaptureSession;
class QMediaRecorder;
class QScreenCapture;
class QWindowCapture;
class QVideoSink;
class QWidget;

class ScreenRecorder : public QObject {
    Q_OBJECT
public:
    explicit ScreenRecorder(QObject *parent=nullptr);
    ~ScreenRecorder() override;
    bool isActive() const { return active; }
    void start(const QString &directory,const QPoint &anchor,bool window);
    void stop();
signals:
    void finished(const QString &file,const QString &error);
    void started();
private:
    void complete(const QString &error={});
    void createCaptureSources();
    QMediaCaptureSession *session;
    QMediaRecorder *recorder;
    QScreenCapture *screenCapture=nullptr;
    QWindowCapture *windowCapture=nullptr;
    QVideoSink *videoSink;
    QPointer<QWidget> controls;
    QString target;
    bool active=false;
    bool recorded=false;
    bool receivedFrame=false;
    void announceStarted();
    quint64 generation=0;
};
