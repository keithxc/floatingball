#include "screenrecorder.h"
#include "floatingwindow.h"
#include <QMediaCaptureSession>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QScreenCapture>
#include <QWindowCapture>
#include <QCapturableWindow>
#include <QVideoSink>
#include <QVideoFrame>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QUrl>
#include <QUuid>
#include <QTimer>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QShortcut>

ScreenRecorder::ScreenRecorder(QObject *parent):QObject(parent) {
    session=new QMediaCaptureSession(this);recorder=new QMediaRecorder(this);
    session->setRecorder(recorder);
    videoSink=new QVideoSink(this);session->setVideoSink(videoSink);
    connect(videoSink,&QVideoSink::videoFrameChanged,this,[this](const QVideoFrame &frame){
        if(active && frame.isValid()){receivedFrame=true;announceStarted();}
    });
    connect(recorder,&QMediaRecorder::errorOccurred,this,[this](QMediaRecorder::Error,const QString &error){if(active)complete(QStringLiteral("视频保存失败：")+error);});
    connect(recorder,&QMediaRecorder::recorderStateChanged,this,[this](QMediaRecorder::RecorderState state){
        if(!active)return;
        if(state==QMediaRecorder::RecordingState)announceStarted();
        else if(state==QMediaRecorder::StoppedState && recorded)QTimer::singleShot(0,this,[this]{if(active)complete();});
    });
}
void ScreenRecorder::createCaptureSources() {
    screenCapture=new QScreenCapture(this);windowCapture=new QWindowCapture(this);
    const quint64 run=generation;
    auto begin=[this,run](bool capturing){if(active&&generation==run&&capturing)recorder->record();};
    connect(screenCapture,&QScreenCapture::activeChanged,this,begin);
    connect(windowCapture,&QWindowCapture::activeChanged,this,begin);
    connect(screenCapture,&QScreenCapture::errorOccurred,this,[this,run](QScreenCapture::Error,const QString &error){if(active&&generation==run)complete(QStringLiteral("录屏失败：")+error);});
    connect(windowCapture,&QWindowCapture::errorOccurred,this,[this,run](QWindowCapture::Error,const QString &error){if(active&&generation==run)complete(QStringLiteral("窗口录屏失败：")+error);});
}
void ScreenRecorder::announceStarted() {
    // A portal can report an active capture before a source has been selected.
    if(!active || recorded || !receivedFrame || recorder->recorderState()!=QMediaRecorder::RecordingState)return;
    recorded=true;
    if(controls){controls->findChild<QLabel *>("recordingTime")->setText(QStringLiteral("● 00:00"));controls->findChild<QToolButton *>("pauseRecording")->setEnabled(true);controls->findChild<QToolButton *>("stopRecording")->setText(QStringLiteral("停止并保存"));controls->show();}
    emit started();
}
ScreenRecorder::~ScreenRecorder(){active=false;recorder->stop();if(screenCapture)screenCapture->stop();if(windowCapture)windowCapture->stop();if(controls)delete controls;}
void ScreenRecorder::start(const QString &directory,const QPoint &anchor,bool window) {
    if(active)return;
    active=true;recorded=false;receivedFrame=false;const quint64 run=++generation;
    if(QGuiApplication::platformName()==QStringLiteral("offscreen")){complete(QStringLiteral("当前显示环境不支持录屏"));return;}
    if(window && QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))){complete(QStringLiteral("当前 Wayland 后端暂不支持窗口录屏，请选择屏幕录制"));return;}
    const QString folder=QDir(directory).filePath(QStringLiteral("Recordings"));
    if(!QDir().mkpath(folder)){complete(QStringLiteral("无法创建录屏目录，请选择可写目录"));return;}
    QMediaFormat format(QMediaFormat::MPEG4);format.setVideoCodec(QMediaFormat::VideoCodec::H264);format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    QString extension=QStringLiteral("mp4");
    if(!format.isSupported(QMediaFormat::Encode)){format.setFileFormat(QMediaFormat::WebM);format.setVideoCodec(QMediaFormat::VideoCodec::VP9);format.setAudioCodec(QMediaFormat::AudioCodec::Opus);extension=QStringLiteral("webm");}
    if(!format.isSupported(QMediaFormat::Encode)){complete(QStringLiteral("当前系统没有可用的视频编码器"));return;}
    target=QDir(folder).absoluteFilePath(QStringLiteral("Recording-%1-%2.%3").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz"),QUuid::createUuid().toString(QUuid::Id128),extension));
    recorder->setMediaFormat(format);recorder->setQuality(QMediaRecorder::HighQuality);recorder->setVideoFrameRate(30);recorder->setOutputLocation(QUrl::fromLocalFile(target));
    controls=new QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);controls->setObjectName("recordingControls");controls->setWindowTitle(QStringLiteral("FloatingBall · 录屏中"));
    controls->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    auto row=new QHBoxLayout(controls);auto time=new QLabel(QStringLiteral("等待画面…"));time->setObjectName("recordingTime");row->addWidget(time);
    auto pause=new QToolButton;pause->setObjectName("pauseRecording");pause->setText(QStringLiteral("暂停"));pause->setCheckable(true);pause->setEnabled(false);row->addWidget(pause);
    connect(pause,&QToolButton::toggled,this,[this,pause](bool paused){if(paused)recorder->pause();else recorder->record();pause->setText(paused?QStringLiteral("继续"):QStringLiteral("暂停"));});
    auto stopButton=new QToolButton;stopButton->setObjectName("stopRecording");stopButton->setText(QStringLiteral("取消"));row->addWidget(stopButton);connect(stopButton,&QToolButton::clicked,this,&ScreenRecorder::stop);
    auto escape=new QShortcut(QKeySequence(Qt::Key_Escape),controls);connect(escape,&QShortcut::activated,this,&ScreenRecorder::stop);
    connect(recorder,&QMediaRecorder::durationChanged,time,[this,time](qint64 ms){if(!recorded)return;const qint64 seconds=ms/1000;time->setText(QStringLiteral("● %1:%2").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0')));});
    controls->setStyleSheet("QWidget { background:#302b3a; color:#faf9fc; border-radius:12px; padding:8px; } QToolButton { background:#65597d; } QToolButton:hover { background:#817298; }");
    controls->adjustSize();QScreen *screen=QGuiApplication::screenAt(anchor);if(!screen)screen=QGuiApplication::primaryScreen();
    const QRect bounds=screen->availableGeometry();controls->move(bounds.center().x()-controls->width()/2,bounds.top()+20);FloatingWindow::prepare(controls,true);
    createCaptureSources();
    QTimer::singleShot(220,this,[this,window,screen,run]{
        if(!active || generation!=run)return;
        session->setScreenCapture(nullptr);session->setWindowCapture(nullptr);
        if(window && !QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
            const auto all=QWindowCapture::capturableWindows();QList<QCapturableWindow> windows;QStringList labels;
            for(const auto &item:all)if(item.description()!=QStringLiteral("FloatingBall") && !item.description().startsWith(QStringLiteral("FloatingBall ·")) && !item.description().isEmpty()){windows.append(item);labels.append(item.description()+QStringLiteral(" (%1)").arg(windows.size()));}
            if(windows.isEmpty()){complete(QStringLiteral("没有可录制的窗口"));return;}
            bool accepted=false;const QString selected=QInputDialog::getItem(nullptr,QStringLiteral("选择录制窗口"),QStringLiteral("窗口："),labels,0,false,&accepted);
            if(!active || generation!=run)return;
            if(!accepted){complete();return;}
            windowCapture->setWindow(windows[labels.indexOf(selected)]);session->setWindowCapture(windowCapture);windowCapture->start();
        } else {screenCapture->setScreen(screen);session->setScreenCapture(screenCapture);screenCapture->start();}
        if(active && controls)controls->show();
    });
    QTimer::singleShot(60000,this,[this,run]{if(active && generation==run && !recorded)complete(QStringLiteral("录屏未开始，请检查屏幕录制权限或取消选择后重试"));});
}
void ScreenRecorder::stop(){if(!active)return;if(recorded)recorder->stop();else complete();}
void ScreenRecorder::complete(const QString &error) {
    if(!active)return;active=false;
    recorder->stop();
    session->setScreenCapture(nullptr);session->setWindowCapture(nullptr);
    // Destroy pending portal requests as well as active streams on cancellation.
    if(screenCapture){screenCapture->stop();screenCapture->deleteLater();screenCapture=nullptr;}
    if(windowCapture){windowCapture->stop();windowCapture->deleteLater();windowCapture=nullptr;}
    if(controls){controls->hide();controls->deleteLater();controls=nullptr;}
    const QString actual=recorder->actualLocation().isLocalFile()?recorder->actualLocation().toLocalFile():target;
    const bool saved=recorded && error.isEmpty() && QFileInfo(actual).size()>0;
    if(!saved && QFileInfo(target).size()==0)QFile::remove(target);
    emit finished(saved?actual:QString(),error.isEmpty() && recorded && !saved?QStringLiteral("录屏没有生成有效视频，请重试"):error);
}
