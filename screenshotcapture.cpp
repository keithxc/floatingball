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
#include <QCursor>
#include <QtMath>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
namespace {
QRect logicalWindowRect(HWND handle,const RECT &rect) {
    MONITORINFOEXW monitor{};monitor.cbSize=sizeof(monitor);
    if(GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTONEAREST),&monitor))
        for(auto screen:QGuiApplication::screens())if(screen->name()==QString::fromWCharArray(monitor.szDevice)) {
            const qreal scale=screen->devicePixelRatio();
            return QRect(screen->geometry().topLeft()+QPoint(qRound((rect.left-monitor.rcMonitor.left)/scale),qRound((rect.top-monitor.rcMonitor.top)/scale)),
                         QSize(qRound((rect.right-rect.left)/scale),qRound((rect.bottom-rect.top)/scale)));
        }
    return QRect(rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top);
}
void paintCursor(QImage &image,const QRect &bounds,qreal scale) {
    CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);
    if(!GetCursorInfo(&cursor) || !(cursor.flags&CURSOR_SHOWING))return;
    ICONINFO icon{};if(!GetIconInfo(cursor.hCursor,&icon))return;
    const QImage pixels=QImage::fromHICON(cursor.hCursor);
    QScreen *screen=QGuiApplication::screenAt(QCursor::pos());
    const qreal ratio=scale/(screen?screen->devicePixelRatio():scale);
    QPainter p(&image);p.drawImage(QRectF((QPointF(QCursor::pos()-bounds.topLeft())*scale)-QPointF(icon.xHotspot,icon.yHotspot)*ratio,QSizeF(pixels.size())*ratio),pixels);
    if(icon.hbmMask)DeleteObject(icon.hbmMask);if(icon.hbmColor)DeleteObject(icon.hbmColor);
}
}
#endif
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

ScreenshotRegionSelector::ScreenshotRegionSelector(const QImage &image, const QRect &screenGeometry,
                                                   const QList<QRect> &windows)
    : frame(image), windowRects(windows) {
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
    if(!windowRects.isEmpty()) {
        const QPoint global=mapToGlobal(current);
        for(const QRect &window:windowRects)if(window.contains(global))
            return window.translated(-geometry().topLeft()).intersected(rect());
        return {};
    }
    return QRect(QPoint(qMin(first.x(),current.x()),qMin(first.y(),current.y())),
                 QSize(qAbs(current.x()-first.x()),qAbs(current.y()-first.y())));
}
void ScreenshotRegionSelector::paintEvent(QPaintEvent *) {
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
    p.drawImage(rect(),frame);
    p.fillRect(rect(),QColor(17,13,27,110));
    const QRect area=selection();
    if((dragging || !windowRects.isEmpty()) && !area.isEmpty()) {
        p.save();p.setClipRect(area);p.drawImage(rect(),frame);p.restore();
        p.setPen(QPen(QColor("#b9a6ef"),1.5));p.setBrush(Qt::NoBrush);p.drawRect(QRectF(area));
        p.setBrush(QColor("#faf9fc"));
        for(const QPoint &corner:{area.topLeft(),area.topRight(),area.bottomLeft(),area.bottomRight()})p.drawEllipse(corner,3,3);
    }
    const QSize pixels=pixelRect(frame.size(),size(),area).size();
    const QString text=!windowRects.isEmpty()?QStringLiteral("点击选择窗口 · Esc 取消"):
        dragging ? QStringLiteral("%1 × %2 · 松开完成 · Esc 取消").arg(pixels.width()).arg(pixels.height())
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
    if(dragging || !windowRects.isEmpty()){current=bounded(event->pos());update();}
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
void ScreenshotCapture::start(const QPoint &anchor, const CaptureOptions &requested) {
    if(active)return;
    active=true;options=requested;options.delaySeconds=qBound(0,options.delaySeconds,30);
    const quint64 run=++generation;
    emit countdownChanged(options.delaySeconds);
    for(int second=1;second<=options.delaySeconds;++second)
        QTimer::singleShot(second*1000,this,[this,run,second]{if(active && generation==run)emit countdownChanged(options.delaySeconds-second);});
    // Let hidden floating surfaces leave the compositor before acquiring pixels.
    QTimer::singleShot(options.delaySeconds*1000+220,this,[this,anchor,run]{if(active && generation==run)captureDesktop(anchor);});
    QTimer::singleShot(600000,this,[this,run]{if(active && generation==run)finish({},QStringLiteral("截图等待超时，请重试"));});
}
void ScreenshotCapture::cancel(){if(active)finish({},{},true);}
void ScreenshotCapture::captureDesktop(const QPoint &anchor) {
    if(QGuiApplication::platformName()==QStringLiteral("offscreen")){finish({},QStringLiteral("当前显示环境不支持屏幕截图"));return;}
#ifdef Q_OS_MACOS
    const QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if(!QDir().mkpath(cache)){finish({},QStringLiteral("无法创建截图临时目录"));return;}
    temporary=std::make_unique<QTemporaryDir>(cache+QStringLiteral("/capture-XXXXXX"));
    if(!temporary->isValid()){finish({},QStringLiteral("无法创建截图临时目录"));return;}
    const QString file=temporary->filePath(QStringLiteral("capture.png"));
    QScreen *display=QGuiApplication::screenAt(anchor);if(!display)display=QGuiApplication::primaryScreen();
    const QRect regionBounds=display->geometry();
    process=new QProcess(this);
    connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)finish({},QStringLiteral("无法启动系统截图工具"));
    });
    const auto singleDisplayFinished=connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,file,regionBounds](int code,QProcess::ExitStatus exit){
        if(!active)return;
        QImageReader reader(file);reader.setAutoTransform(true);const QImage image=reader.read();
        const QString error=process?QString::fromLocal8Bit(process->readAllStandardError()).trimmed():QString();
        if(!image.isNull()){
            if(options.mode==CaptureOptions::Region){
                auto selector=new ScreenshotRegionSelector(image,regionBounds);selectors.append(selector);
                connect(selector,&ScreenshotRegionSelector::selected,this,[this](const QImage &value){finish(value);});
                connect(selector,&ScreenshotRegionSelector::cancelled,this,&ScreenshotCapture::cancel);
                selector->show();selector->raise();selector->activateWindow();selector->setFocus();
            }else finish(image);
            return;
        }
        if(exit==QProcess::NormalExit && error.isEmpty()){finish({},{},true);return;}
        Q_UNUSED(code);
        finish({},QStringLiteral("系统截图失败，请检查屏幕录制权限")+(error.isEmpty()?QString():QStringLiteral("：")+error.left(240)));
    });
    QStringList args{QStringLiteral("-x"),QStringLiteral("-t"),QStringLiteral("png")};
    if(options.mode==CaptureOptions::Region)args<<QStringLiteral("-D")<<QString::number(qMax(0,QGuiApplication::screens().indexOf(display))+1);
    else if(options.mode==CaptureOptions::Window)args<<QStringLiteral("-i")<<QStringLiteral("-w")<<QStringLiteral("-o");
    else {
        if(options.includeCursor)args<<QStringLiteral("-C");
        if(options.mode==CaptureOptions::CurrentScreen) {
            auto screens=QGuiApplication::screens();QScreen *screen=QGuiApplication::screenAt(anchor);
            args<<QStringLiteral("-D")<<QString::number(qMax(0,screens.indexOf(screen))+1);
        } else if(QGuiApplication::screens().size()>1) {
            // Multiple filenames ask the native tool to capture every display.
            QStringList files;
            for(int i=0;i<QGuiApplication::screens().size();++i)files<<temporary->filePath(QStringLiteral("screen-%1.png").arg(i));
            disconnect(singleDisplayFinished);
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,files](int,QProcess::ExitStatus){
                QRect bounds;for(auto screen:QGuiApplication::screens())bounds=bounds.united(screen->geometry());
                qreal scale=1;for(auto screen:QGuiApplication::screens())scale=qMax(scale,screen->devicePixelRatio());
                QImage result(QSize(qRound(bounds.width()*scale),qRound(bounds.height()*scale)),QImage::Format_ARGB32);
                if(result.isNull()){finish({},QStringLiteral("桌面尺寸过大，无法创建截图"));return;}result.fill(Qt::transparent);
                QPainter p(&result);auto screens=QGuiApplication::screens();
                for(int i=0;i<files.size();++i){QImage image(files[i]);if(image.isNull()){p.end();finish({},QStringLiteral("无法读取屏幕截图，请检查权限"));return;}
                    QRectF target(screens[i]->geometry());target.translate(-bounds.topLeft());target=QRectF(target.topLeft()*scale,target.size()*scale);p.drawImage(target,image);}
                p.end();finish(result);
            });
            args<<files;process->start(QStringLiteral("/usr/sbin/screencapture"),args);return;
        }
    }
    args<<file;process->start(QStringLiteral("/usr/sbin/screencapture"),args);
    return;
#elif defined(Q_OS_LINUX)
    if(QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        const QString spectacle=QStandardPaths::findExecutable(QStringLiteral("spectacle"));
        if(!spectacle.isEmpty()) {
            temporary=std::make_unique<QTemporaryDir>();
            if(!temporary->isValid()){finish({},QStringLiteral("无法创建截图临时目录"));return;}
            const QString file=temporary->filePath(QStringLiteral("capture.png"));
            process=new QProcess(this);
            connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finish({},QStringLiteral("无法启动系统截图服务"));});
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,file](int code,QProcess::ExitStatus exit){
                QImageReader reader(file);reader.setAutoTransform(true);const QImage image=reader.read();
                if(!image.isNull()){finish(image);return;}
                if(exit==QProcess::NormalExit && (code==0 || code==1)){finish({},{},true);return;}
                finish({},QStringLiteral("系统截图失败，请检查桌面截图服务"));
            });
            const QString modes[]={QStringLiteral("--region"),QStringLiteral("--windowundercursor"),QStringLiteral("--current"),QStringLiteral("--fullscreen")};
            QStringList args{QStringLiteral("--new-instance"),QStringLiteral("--background"),QStringLiteral("--nonotify"),QStringLiteral("--output"),file,modes[options.mode]};
            if(options.mode==CaptureOptions::Window)args<<QStringLiteral("--onclick");
            if(options.includeCursor)args<<QStringLiteral("--pointer");
            process->start(spectacle,args);return;
        }
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
        QVariantMap portalOptions{{QStringLiteral("handle_token"),token},{QStringLiteral("interactive"),options.mode==CaptureOptions::Region || options.mode==CaptureOptions::Window},{QStringLiteral("modal"),false}};
        // v2 backends offer their own interactive picker; v3 can request an area.
        QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),QStringLiteral("/org/freedesktop/portal/desktop"),QStringLiteral("org.freedesktop.portal.Screenshot"),bus);
        portal.setTimeout(3000);
        const uint target=options.mode==CaptureOptions::Region?4:options.mode==CaptureOptions::Window?2:1;
        if(portal.property("version").toUInt()>=3 && (portal.property("AvailableTargets").toUInt()&target))portalOptions.insert(QStringLiteral("target"),target);
        auto message=QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),QStringLiteral("/org/freedesktop/portal/desktop"),QStringLiteral("org.freedesktop.portal.Screenshot"),QStringLiteral("Screenshot"));
        message.setArguments({QString(),portalOptions});
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
    QList<QPair<QScreen *,QImage>> frames;
    for(QScreen *screen:QGuiApplication::screens()) {
        const QImage image=screen->grabWindow(0).toImage();
        if(image.isNull()){finish({},QStringLiteral("无法读取屏幕内容，请检查截图权限"));return;}
        frames.append({screen,image});
    }
    if(frames.isEmpty()){finish({},QStringLiteral("没有可截图的显示器"));return;}
    QRect bounds; qreal scale=1;
    for(const auto &frame:frames){bounds=bounds.united(frame.first->geometry());scale=qMax(scale,frame.first->devicePixelRatio());}
    QImage desktop(QSize(qRound(bounds.width()*scale),qRound(bounds.height()*scale)),QImage::Format_ARGB32);
    if(desktop.isNull()){finish({},QStringLiteral("桌面尺寸过大，无法创建截图"));return;}desktop.fill(Qt::transparent);
    {QPainter p(&desktop);for(const auto &frame:frames){QRectF r(frame.first->geometry());r.translate(-bounds.topLeft());p.drawImage(QRectF(r.topLeft()*scale,r.size()*scale),frame.second);}}
#ifdef Q_OS_WIN
    if(options.includeCursor)paintCursor(desktop,bounds,scale);
#endif
    if(options.mode==CaptureOptions::AllScreens){finish(desktop);return;}
    if(options.mode==CaptureOptions::CurrentScreen){for(const auto &frame:frames)if(frame.first->geometry().contains(anchor)){
        QImage image=frame.second;image.setDevicePixelRatio(1);
#ifdef Q_OS_WIN
        if(options.includeCursor)paintCursor(image,frame.first->geometry(),frame.first->devicePixelRatio());
#endif
        finish(image);return;}finish(frames.first().second);return;}
    QList<QRect> windows;
#ifdef Q_OS_WIN
    if(options.mode==CaptureOptions::Window) {
        EnumWindows([](HWND handle,LPARAM param)->BOOL{
            DWORD pid=0;GetWindowThreadProcessId(handle,&pid);
            if(!IsWindowVisible(handle) || IsIconic(handle) || pid==GetCurrentProcessId())return TRUE;
            RECT rect;if(!GetWindowRect(handle,&rect) || rect.right-rect.left<30 || rect.bottom-rect.top<30)return TRUE;
            reinterpret_cast<QList<QRect> *>(param)->append(logicalWindowRect(handle,rect));return TRUE;
        },reinterpret_cast<LPARAM>(&windows));
        if(windows.isEmpty()){finish({},QStringLiteral("没有可截取的窗口"));return;}
    }
#else
    if(options.mode==CaptureOptions::Window){finish({},QStringLiteral("当前桌面请使用区域模式选择窗口"));return;}
#endif
    auto selector=new ScreenshotRegionSelector(desktop,bounds,windows);selectors.append(selector);
    connect(selector,&ScreenshotRegionSelector::selected,this,[this](const QImage &image){finish(image);});
    connect(selector,&ScreenshotRegionSelector::cancelled,this,&ScreenshotCapture::cancel);
    selector->show();selector->raise();selector->activateWindow();selector->setFocus();
    return;
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
