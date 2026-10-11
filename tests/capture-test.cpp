#include "screenshotcapture.h"
#include "screenshotpanel.h"
#include "screenshotstore.h"
#include "screenrecorder.h"
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QTemporaryDir>
#include <QSettings>
#include <QEventLoop>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QToolButton>
#include <QComboBox>
#include <QMimeData>
#include <QInputDialog>
#include <QFile>
#include <cstdio>
#include <cstdlib>
static void check(bool good,const char *description){printf("%s: %s\n",good?"PASS":"FAIL",description);fflush(stdout);if(!good)std::exit(1);}
static void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    check(argc==2 && QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])),"explicit external test directory");
    QDir().mkpath(QString::fromLocal8Bit(argv[1]));QTemporaryDir root(QString::fromLocal8Bit(argv[1])+"/capture-test-XXXXXX");check(root.isValid(),"owned test workspace");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root.path()+"/settings");
    QImage image(600,400,QImage::Format_ARGB32);for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)image.setPixelColor(x,y,QColor(x%256,y%256,(x+y)%256));
    check(ScreenshotRegionSelector::pixelRect(image.size(),QSize(300,200),QRect(10,20,60,40))==QRect(20,40,120,80),"retina region maps logical edges to original pixels");
    check(ScreenshotRegionSelector::pixelRect(QSize(450,300),QSize(300,200),QRect(1,1,3,3))==QRect(1,1,5,5),"fractional display scale rounds outwards without dropping edge pixels");
    check(ScreenshotRegionSelector::crop(image,QSize(300,200),QRect(-10,-10,30,20))==image.copy(0,0,40,20),"out-of-bounds region is clamped safely");
    check(ScreenshotRegionSelector::crop(image,QSize(),QRect(1,1,20,20)).isNull(),"invalid display geometry cannot produce a capture");
    ScreenshotRegionSelector selector(image,QRect(-300,-200,300,200));QImage selected;int cancelled=0;
    QObject::connect(&selector,&ScreenshotRegionSelector::selected,[&](const QImage &value){selected=value;});
    QObject::connect(&selector,&ScreenshotRegionSelector::cancelled,[&]{++cancelled;});selector.show();app.processEvents();
    auto send=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,point,selector.mapToGlobal(point),button,buttons,Qt::NoModifier);app.sendEvent(&selector,&event);};
    send(QEvent::MouseButtonPress,QPoint(90,80),Qt::LeftButton,Qt::LeftButton);send(QEvent::MouseMove,QPoint(20,30),Qt::NoButton,Qt::LeftButton);send(QEvent::MouseButtonRelease,QPoint(20,30),Qt::LeftButton,Qt::NoButton);
    check(selected==image.copy(40,60,140,100)&&selected.devicePixelRatio()==1,"reverse drag on a negative-origin screen preserves selected native pixels");selector.hide();
    ScreenshotRegionSelector abort(image,QRect(0,0,300,200));QObject::connect(&abort,&ScreenshotRegionSelector::cancelled,[&]{++cancelled;});
    QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);app.sendEvent(&abort,&escape);check(cancelled==1,"Escape cancels a region without emitting an image");
    ScreenshotCapture capture;int finished=0;bool wasCancelled=false;QString failure;
    QObject::connect(&capture,&ScreenshotCapture::finished,[&](const QImage &,const QString &error,bool cancel){++finished;wasCancelled=cancel;failure=error;});
    CaptureOptions options;options.delaySeconds=1;capture.start({},options);capture.cancel();wait(1250);
    check(!capture.isActive()&&finished==1&&wasCancelled&&failure.isEmpty(),"cancelling a delayed capture invalidates queued acquisition");
    capture.start({});wait(350);check(finished==2&&!capture.isActive()&&!failure.isEmpty(),"unsupported display reports failure and releases the capture session");
    ScreenRecorder recorder;int recordFinished=0;QString recordError;
    QObject::connect(&recorder,&ScreenRecorder::finished,[&](const QString &file,const QString &error){++recordFinished;recordError=error;check(file.isEmpty(),"unsupported recorder does not return a video file");});
    recorder.start(root.path(),QPoint(100,100),false);recorder.stop();
    check(!recorder.isActive()&&recordFinished==1&&!recordError.isEmpty(),"unsupported recorder releases its session and reports one error");
    QString error;const QString first=ScreenshotStore::saveCapture(image,root.path()+"/shots",&error);const QString second=ScreenshotStore::saveCapture(image,root.path()+"/shots",&error);
    check(!first.isEmpty()&&first!=second&&QImage(first)==image&&QImage(second)==image,"captures create unique lossless PNGs without overwriting files");
    ScreenshotPanel panel(nullptr,root.path()+"/shots");panel.reveal(QPoint(100,100));panel.showCapture(image,QPoint(100,100));app.processEvents();
    check(panel.findChild<QListWidget *>()->count()==3,"new capture is saved and appears in the gallery immediately");
    check(panel.findChild<QComboBox *>("captureMode")->count()==4,"workbench offers region, window, screen and desktop captures");
    check(panel.findChild<QComboBox *>("annotationTool")->count()==6,"workbench offers pen, arrow, rectangle, marker, mosaic and text");
    ScreenshotCanvas *edited=nullptr;for(auto widget:panel.findChildren<QWidget *>())if(auto value=dynamic_cast<ScreenshotCanvas *>(widget))edited=value;
    check(edited!=nullptr,"captured pixels are available for annotation");
    edited->editing=true;
    const auto stroke=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,point,edited->mapToGlobal(point),button,buttons,Qt::NoModifier);app.sendEvent(edited,&event);};
    stroke(QEvent::MouseButtonPress,edited->rect().center()-QPoint(30,10),Qt::LeftButton,Qt::LeftButton);
    stroke(QEvent::MouseMove,edited->rect().center()+QPoint(30,10),Qt::NoButton,Qt::LeftButton);
    stroke(QEvent::MouseButtonRelease,edited->rect().center()+QPoint(30,10),Qt::LeftButton,Qt::NoButton);
    edited->editing=false;const QImage annotated=edited->image;
    check(edited->dirty && annotated!=image,"a real annotation is tracked as an unexported edit");
    auto gallery=panel.findChild<QListWidget *>();const int originalRow=gallery->currentRow();gallery->setCurrentRow((originalRow+1)%gallery->count());
    check(edited->image==annotated && gallery->currentRow()==originalRow,"switching thumbnails protects an unexported annotation");
    options.delaySeconds=1;panel.takeScreenshot(QPoint(100,100),options);
    check(panel.isVisible() && edited->image==annotated,"new capture protects an unexported annotation");
    auto preserved=new QMimeData;const auto mime=app.clipboard()->mimeData();
    if(mime){for(const auto &format:mime->formats())preserved->setData(format,mime->data(format));if(mime->hasImage())preserved->setImageData(mime->imageData());}
    panel.findChild<QToolButton *>("copyImage")->click();app.processEvents();const bool exported=app.clipboard()->image()==annotated;
    app.clipboard()->setMimeData(preserved);app.processEvents();
    check(exported&&!edited->dirty,"copy exports annotated pixels and permits the next capture");
    const int retainedUndo=edited->history.size();edited->editing=true;edited->tool=ScreenshotCanvas::Text;
    QTimer::singleShot(0,[]{if(auto dialog=qobject_cast<QInputDialog *>(QApplication::activeModalWidget()))dialog->reject();});
    stroke(QEvent::MouseButtonPress,edited->rect().center(),Qt::LeftButton,Qt::LeftButton);
    check(edited->image==annotated&&!edited->dirty&&edited->history.size()==retainedUndo,"cancelling text leaves exported pixels and undo history intact");
    QImage white(image.size(),QImage::Format_ARGB32);white.fill(Qt::white);edited->setImage(white);edited->tool=ScreenshotCanvas::Marker;
    const QPoint left=edited->rect().center()-QPoint(40,0),right=edited->rect().center()+QPoint(40,0);
    stroke(QEvent::MouseButtonPress,left,Qt::LeftButton,Qt::LeftButton);stroke(QEvent::MouseMove,right,Qt::NoButton,Qt::LeftButton);
    const QColor highlight=edited->image.pixelColor(300,200);
    stroke(QEvent::MouseMove,left,Qt::NoButton,Qt::LeftButton);stroke(QEvent::MouseButtonRelease,right,Qt::LeftButton,Qt::NoButton);
    check(highlight!=Qt::white&&edited->image.pixelColor(300,200)==highlight,"overlapping highlighter segments keep a consistent translucent color");edited->undo();
    check(edited->image==white,"highlighter undo restores exact original pixels");
    edited->setImage(image);edited->tool=ScreenshotCanvas::Mosaic;
    stroke(QEvent::MouseButtonPress,left,Qt::LeftButton,Qt::LeftButton);stroke(QEvent::MouseButtonRelease,right,Qt::LeftButton,Qt::NoButton);
    check(edited->image!=image&&edited->image.pixelColor(0,0)==image.pixelColor(0,0)&&edited->image.pixelColor(300,200)==edited->image.pixelColor(301,200),"mosaic pixelates the selected stroke and preserves pixels outside it");edited->undo();
    check(edited->image==image,"mosaic undo restores exact original pixels");
    edited->setImage(white);edited->tool=ScreenshotCanvas::Pen;
    for(int i=0;i<14;++i){
        stroke(QEvent::MouseButtonPress,left+QPoint(0,i*3),Qt::LeftButton,Qt::LeftButton);
        stroke(QEvent::MouseButtonRelease,right+QPoint(0,i*3),Qt::LeftButton,Qt::NoButton);
    }
    check(edited->history.size()==12,"undo memory retains at most twelve edits");
    for(int i=0;i<12;++i)edited->undo();
    check(edited->dirty&&edited->image!=white,"truncated undo history still protects earlier unexported annotations");
    const QImage retainedPixels=edited->image;edited->editing=false;
    const auto fixtures=QDir(root.path()+"/shots").entryInfoList({"*.png"},QDir::Files);QList<QImage> backup;
    for(const auto &file:fixtures){backup.append(QImage(file.absoluteFilePath()));check(QFile::remove(file.absoluteFilePath()),"owned capture fixture removed for external deletion check");}
    wait(2300);
    check(edited->image==retainedPixels&&edited->dirty,"external file deletion cannot erase edits after undo history is exhausted");
    for(int i=0;i<fixtures.size();++i)check(backup[i].save(fixtures[i].absoluteFilePath(),"PNG"),"owned capture fixture restored");
    edited->setImage(image);edited->editing=false;
    QImage next(320,180,QImage::Format_ARGB32);next.fill(QColor("#38b784"));panel.showCapture(next,QPoint(100,100));app.processEvents();
    check(edited->image==next&&edited->history.isEmpty()&&gallery->count()==4,"new captured image replaces exported edits and clears undo history");
    ScreenshotPanel recovery(nullptr,first+"/not-a-directory");recovery.showCapture(image,QPoint(100,100));wait(2300);
    ScreenshotCanvas *canvas=nullptr;for(auto widget:recovery.findChildren<QWidget *>())if(auto value=dynamic_cast<ScreenshotCanvas *>(widget))canvas=value;
    check(canvas&&canvas->image==image,"failed save retains the unsaved pixels across gallery refresh");
    check(recovery.findChild<QToolButton *>("copyImage")->isEnabled()&&!recovery.findChild<QToolButton *>("copyPath")->isEnabled(),"failed save allows image recovery and disables a nonexistent path");
    return 0;
}
