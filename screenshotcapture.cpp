#include "screenshotcapture.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QWindow>
#include <QtMath>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

ScreenshotRegionSelector::ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry)
    : frame(image) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("FloatingBall · 框选截图"));
    setObjectName(QStringLiteral("captureSelector"));
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(screenGeometry);
}

QRect ScreenshotRegionSelector::pixelRect(const QSize &imageSize, const QSize &logicalSize, const QRect &selected) {
    if(imageSize.isEmpty() || logicalSize.isEmpty()) return {};
    const QRect rect=selected.intersected(QRect(QPoint(),logicalSize));
    if(rect.isEmpty()) return {};
    const qreal sx=qreal(imageSize.width())/logicalSize.width();
    const qreal sy=qreal(imageSize.height())/logicalSize.height();
    const int left=qFloor(rect.x()*sx), top=qFloor(rect.y()*sy);
    const int right=qCeil((rect.x()+rect.width())*sx), bottom=qCeil((rect.y()+rect.height())*sy);
    return QRect(left,top,right-left,bottom-top).intersected(QRect(QPoint(),imageSize));
}
QImage ScreenshotRegionSelector::crop(const QImage &image, const QSize &logicalSize, const QRect &selected) {
    const QRect pixels=pixelRect(image.size(),logicalSize,selected);
    if(pixels.isEmpty()) return {};
    QImage result=image.copy(pixels);
    result.setDevicePixelRatio(1);
    return result;
}
QPoint ScreenshotRegionSelector::bounded(const QPoint &p) const {
    return QPoint(qBound(0,p.x(),width()),qBound(0,p.y(),height()));
}
QRect ScreenshotRegionSelector::selection() const {
    return QRect(QPoint(qMin(first.x(),current.x()),qMin(first.y(),current.y())),
                 QSize(qAbs(current.x()-first.x()),qAbs(current.y()-first.y())));
}
void ScreenshotRegionSelector::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(),frame);
    p.fillRect(rect(),QColor(17,13,27,110));
    const QRect area=selection();
    if(dragging && !area.isEmpty()) {
        p.save();p.setClipRect(area);p.drawImage(rect(),frame);p.restore();
        p.setPen(QPen(QColor("#b9a6ef"),1.5));p.setBrush(Qt::NoBrush);p.drawRect(QRectF(area));
        p.setBrush(QColor("#faf9fc"));
        for(const QPoint &corner:{area.topLeft(),area.topRight(),area.bottomLeft(),area.bottomRight()})p.drawEllipse(corner,3,3);
    }
    const QSize pixels=pixelRect(frame.size(),size(),area).size();
    const QString text=dragging ? QStringLiteral("%1 × %2 · 松开完成 · Esc 取消").arg(pixels.width()).arg(pixels.height())
                                : QStringLiteral("拖动框选截图 · Esc 取消");
    QFont font=p.font();font.setPixelSize(13);p.setFont(font);
    const int w=p.fontMetrics().horizontalAdvance(text)+32;
    const QRect hint(qMax(8,(width()-w)/2),qMin(32,qMax(8,height()-48)),qMin(w,width()-16),34);
    p.setPen(Qt::NoPen);p.setBrush(QColor(42,35,59,235));p.drawRoundedRect(hint,12,12);
    p.setPen(QColor("#faf9fc"));p.drawText(hint,Qt::AlignCenter,text);
}
void ScreenshotRegionSelector::mousePressEvent(QMouseEvent *event) {
    if(event->button()==Qt::RightButton){resolved=true;emit cancelled();return;}
    if(event->button()!=Qt::LeftButton)return;
    first=current=bounded(event->pos());dragging=true;update();
}
void ScreenshotRegionSelector::mouseMoveEvent(QMouseEvent *event) {
    if(dragging){current=bounded(event->pos());update();}
}
void ScreenshotRegionSelector::mouseReleaseEvent(QMouseEvent *event) {
    if(event->button()!=Qt::LeftButton || !dragging || resolved)return;
    current=bounded(event->pos());const QRect area=selection();dragging=false;
    if(area.width()<2 || area.height()<2){update();return;}
    resolved=true;emit selected(crop(frame,size(),area));
}
void ScreenshotRegionSelector::keyPressEvent(QKeyEvent *event) {
    if(event->key()==Qt::Key_Escape){resolved=true;emit cancelled();return;}
    QWidget::keyPressEvent(event);
}
void ScreenshotRegionSelector::closeEvent(QCloseEvent *event) {
    if(!resolved){resolved=true;emit cancelled();}
    event->accept();
}

ScreenshotCapture::ScreenshotCapture(QObject *parent) : QObject(parent) {}
ScreenshotCapture::~ScreenshotCapture(){active=false;cleanUp();}
void ScreenshotCapture::start(const QPoint &anchor) {
    if(active)return;
    active=true;const quint64 run=++generation;
    // Let hidden floating surfaces leave the compositor before acquiring pixels.
    QTimer::singleShot(180,this,[this,anchor,run]{if(active && generation==run)captureDesktop(anchor);});
    QTimer::singleShot(600000,this,[this,run]{if(active && generation==run)finish({},QStringLiteral("截图等待超时，请重试"));});
}
void ScreenshotCapture::cancel(){if(active)finish({},{},true);}
void ScreenshotCapture::captureDesktop(const QPoint &anchor) {
#ifdef Q_OS_MACOS
    const QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if(!QDir().mkpath(cache)){finish({},QStringLiteral("无法创建截图临时目录"));return;}
    temporary=std::make_unique<QTemporaryDir>(cache+QStringLiteral("/capture-XXXXXX"));
    if(!temporary->isValid()){finish({},QStringLiteral("无法创建截图临时目录"));return;}
    const QString file=temporary->filePath(QStringLiteral("capture.png"));
    process=new QProcess(this);
    connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)finish({},QStringLiteral("无法启动系统截图工具"));
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,file](int code,QProcess::ExitStatus exit){
        if(!active)return;
        QImageReader reader(file);reader.setAutoTransform(true);const QImage image=reader.read();
        const QString error=process?QString::fromLocal8Bit(process->readAllStandardError()).trimmed():QString();
        if(!image.isNull()){finish(image);return;}
        if(exit==QProcess::NormalExit && error.isEmpty()){finish({},{},true);return;}
        Q_UNUSED(code);
        finish({},QStringLiteral("系统截图失败，请检查屏幕录制权限")+(error.isEmpty()?QString():QStringLiteral("：")+error.left(240)));
    });
    process->start(QStringLiteral("/usr/sbin/screencapture"),{QStringLiteral("-i"),QStringLiteral("-s"),QStringLiteral("-x"),QStringLiteral("-t"),QStringLiteral("png"),file});
    Q_UNUSED(anchor);
    return;
#elif defined(Q_OS_LINUX)
    if(QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        auto bus=QDBusConnection::sessionBus();
        if(!bus.isConnected()){finish({},QStringLiteral("无法连接桌面截图服务"));return;}
        const QString token=QStringLiteral("floatingball_")+QUuid::createUuid().toString(QUuid::Id128);
        QString sender=bus.baseService().mid(1);sender.replace('.', '_');
        portalPath=QStringLiteral("/org/freedesktop/portal/desktop/request/")+sender+'/'+token;
        if(!bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"),portalPath,
                QStringLiteral("org.freedesktop.portal.Request"),QStringLiteral("Response"),
                this,SLOT(portalResponse(uint,QVariantMap)))) {
            finish({},QStringLiteral("无法接收桌面截图结果"));return;
        }
        QVariantMap options{{QStringLiteral("handle_token"),token},{QStringLiteral("interactive"),true},{QStringLiteral("modal"),false}};
        // v2 backends offer their own interactive picker; v3 can request an area.
        QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),QStringLiteral("/org/freedesktop/portal/desktop"),QStringLiteral("org.freedesktop.portal.Screenshot"),bus);
        portal.setTimeout(3000);
        if(portal.property("version").toUInt()>=3 && (portal.property("AvailableTargets").toUInt()&4))options.insert(QStringLiteral("target"),uint(4));
        auto message=QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),QStringLiteral("/org/freedesktop/portal/desktop"),QStringLiteral("org.freedesktop.portal.Screenshot"),QStringLiteral("Screenshot"));
        message.setArguments({QString(),options});
        auto watcher=new QDBusPendingCallWatcher(bus.asyncCall(message,5000),this);
        const quint64 run=generation;
        connect(watcher,&QDBusPendingCallWatcher::finished,this,[this,run](QDBusPendingCallWatcher *call){
            QDBusPendingReply<QDBusObjectPath> reply=*call;call->deleteLater();
            if(!active || generation!=run)return;
            if(reply.isError()){finish({},QStringLiteral("桌面截图服务不可用：")+reply.error().message());return;}
            const QString path=reply.value().path();
            if(path!=portalPath){
                auto bus=QDBusConnection::sessionBus();
                bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"),portalPath,QStringLiteral("org.freedesktop.portal.Request"),QStringLiteral("Response"),this,SLOT(portalResponse(uint,QVariantMap)));
                portalPath=path;
                bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"),portalPath,QStringLiteral("org.freedesktop.portal.Request"),QStringLiteral("Response"),this,SLOT(portalResponse(uint,QVariantMap)));
            }
        });
        return;
    }
#endif
    if(QGuiApplication::platformName()==QStringLiteral("offscreen")){finish({},QStringLiteral("当前显示环境不支持屏幕截图"));return;}
    QList<QPair<QScreen *,QImage>> frames;
    for(QScreen *screen:QGuiApplication::screens()) {
        const QImage image=screen->grabWindow(0).toImage();
        if(image.isNull()){finish({},QStringLiteral("无法读取屏幕内容，请检查截图权限"));return;}
        frames.append({screen,image});
    }
    if(frames.isEmpty()){finish({},QStringLiteral("没有可截图的显示器"));return;}
    ScreenshotRegionSelector *focused=nullptr;
    for(const auto &frame:frames) {
        auto selector=new ScreenshotRegionSelector(frame.second,frame.first->geometry());selectors.append(selector);
        connect(selector,&ScreenshotRegionSelector::selected,this,[this](const QImage &image){finish(image);});
        connect(selector,&ScreenshotRegionSelector::cancelled,this,&ScreenshotCapture::cancel);
        selector->winId();selector->windowHandle()->setScreen(frame.first);selector->setGeometry(frame.first->geometry());selector->show();
        if(frame.first->geometry().contains(anchor))focused=selector;
    }
    if(!focused)focused=selectors.first();
    focused->raise();focused->activateWindow();focused->setFocus();
}
#ifdef Q_OS_LINUX
void ScreenshotCapture::portalResponse(uint response, const QVariantMap &results) {
    if(!active)return;
    if(response==1){finish({},{},true);return;}
    if(response!=0){finish({},QStringLiteral("系统未能完成截图，请检查桌面截图权限"));return;}
    const QUrl uri(results.value(QStringLiteral("uri")).toString());
    if(!uri.isLocalFile()){finish({},QStringLiteral("桌面返回了无法读取的截图路径"));return;}
    QImageReader reader(uri.toLocalFile());reader.setAutoTransform(true);const QImage image=reader.read();
    if(image.isNull()){finish({},QStringLiteral("无法读取系统截图：")+reader.errorString());return;}
    finish(image);
}
#endif
void ScreenshotCapture::finish(const QImage &image, const QString &error, bool cancelled) {
    if(!active)return;
    active=false;cleanUp();emit finished(image,error,cancelled);
}
void ScreenshotCapture::cleanUp() {
    for(const auto &selector:selectors)if(selector){selector->hide();selector->deleteLater();}
    selectors.clear();
    if(process) {
        process->disconnect(this);
        if(process->state()!=QProcess::NotRunning){process->terminate();if(!process->waitForFinished(400)){process->kill();process->waitForFinished(400);}}
        process->deleteLater();process=nullptr;
    }
    temporary.reset();
#ifdef Q_OS_LINUX
    if(!portalPath.isEmpty()) {
        auto bus=QDBusConnection::sessionBus();
        bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"),portalPath,QStringLiteral("org.freedesktop.portal.Request"),QStringLiteral("Response"),this,SLOT(portalResponse(uint,QVariantMap)));
        // Request.Close only affects this capture's handle, including on shutdown.
        bus.asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),portalPath,QStringLiteral("org.freedesktop.portal.Request"),QStringLiteral("Close")));
        portalPath.clear();
    }
#endif
}
