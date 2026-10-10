#include "screenshotpanel.h"
#include "floatingball.h"
#include "screenshotstore.h"
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QEventLoop>
#include <QLabel>
#include <QLayout>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>
#include <QDir>
#include <QToolButton>
#include <QListWidget>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QLineF>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_MACOS
void checkNativeFloatingWindows(const QList<QWidget *> &windows, QWidget *panel);
int runNativeFocusFixture();
#endif
class ClipboardGuard {
public:
 ClipboardGuard() {
  const auto original=QApplication::clipboard()->mimeData();
  if(original) {
   saved=new QMimeData;
   for(const auto &format:original->formats()) saved->setData(format,original->data(format));
   if(original->hasImage()) saved->setImageData(original->imageData());
  }
 }
 ~ClipboardGuard(){restore();}
 void restore(){if(restored)return;restored=true;if(saved){QApplication::clipboard()->setMimeData(saved);saved=nullptr;}else QApplication::clipboard()->clear();}
private:
 QMimeData *saved=nullptr;
 bool restored=false;
};
ClipboardGuard *clipboardGuard=nullptr;
void check(bool ok,const char *message){if(!ok){if(clipboardGuard)clipboardGuard->restore();fprintf(stderr,"FAIL: %s\n",message);exit(1);}printf("PASS: %s\n",message);}
int main(int argc,char **argv){
 QApplication app(argc,argv);
#ifdef Q_OS_MACOS
 if(argc==2&&QString::fromLocal8Bit(argv[1])==QStringLiteral("--native-focus-fixture"))return runNativeFocusFixture();
#endif
 ClipboardGuard clipboard; clipboardGuard=&clipboard;
 check(argc==2,"explicit external test directory supplied");
 check(QDir::isAbsolutePath(QString::fromLocal8Bit(argv[1])),"test directory is absolute");
 check(QDir::isAbsolutePath(ScreenshotStore::defaultDirectory()),"platform default screenshot directory resolves");
 printf("DEFAULT_SCREENSHOT_DIRECTORY: %s\n",ScreenshotStore::defaultDirectory().toUtf8().constData());
 QDir().mkpath(QString::fromLocal8Bit(argv[1]));
 QTemporaryDir temporary(QString::fromLocal8Bit(argv[1])+"/screenshot-test-XXXXXX");
 check(temporary.isValid(),"owned test directory created");QString root=temporary.path();
 QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
 QDir().mkpath(root+"/fixtures");QString file=root+"/fixtures/Screenshot.png";
 QImage original(900,500,QImage::Format_ARGB32);original.fill(QColor("#d5e8f9"));{QPainter p(&original);p.setPen(QColor("#263852"));QFont f=p.font();f.setPixelSize(36);p.setFont(f);p.drawText(original.rect(),Qt::AlignCenter,"FloatingBall\nScreenshot workspace");}check(original.save(file),"fixture created");
 check(original.save(QString::fromLocal8Bit(argv[1])+"/sample.png"),"sample preview saved outside source");
 QSettings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall").setValue("screenshots/directory",root+"/fixtures");
 ScreenshotPanel panel;panel.reveal(QPoint(100,300));app.processEvents();
 auto list=panel.findChild<QListWidget *>();check(list&&list->count()==1,"recent image loaded");
 QString copyError;QImage transparent(3,2,QImage::Format_ARGB32);transparent.fill(QColor(90,140,200,80));
 check(ScreenshotStore::copyImage(transparent,&copyError)&&app.clipboard()->image()==transparent,"image clipboard preserves transparent pixels");
 auto click=[&](const QString &text){for(auto b:panel.findChildren<QToolButton *>())if(b->text()==text){b->click();return;}check(false,"button exists");};
 QToolButton *pin=nullptr;for(auto b:panel.findChildren<QToolButton *>())if(b->text()==QStringLiteral("固定"))pin=b;
 check(pin&&pin->isChecked(),"screenshot panel starts pinned for use alongside other apps");
 click(QStringLiteral("复制图片"));check(app.clipboard()->image()==original,"image clipboard matches original");
 click(QStringLiteral("复制路径"));check(app.clipboard()->text()==file,"clipboard path is absolute original path");
 ScreenshotCanvas *canvas=nullptr;for(auto child:panel.findChildren<QWidget *>())if(auto c=dynamic_cast<ScreenshotCanvas *>(child))canvas=c;
 check(canvas!=nullptr,"canvas exists");
 QPoint canvasTop=canvas->mapTo(&panel,QPoint());QPoint listTop=list->mapTo(&panel,QPoint());
 printf("HINTS: minimum=%dx%d preferred=%dx%d layout=%dx%d\n",panel.minimumSizeHint().width(),panel.minimumSizeHint().height(),panel.sizeHint().width(),panel.sizeHint().height(),panel.layout()->minimumSize().width(),panel.layout()->minimumSize().height());
 printf("LAYOUT: panel=%dx%d canvas=(%d,%d,%d,%d) thumbnails=(%d,%d,%d,%d)\n",panel.width(),panel.height(),canvasTop.x(),canvasTop.y(),canvas->width(),canvas->height(),listTop.x(),listTop.y(),list->width(),list->height());
 check(!list->isVisible()||canvasTop.y()+canvas->height()<=listTop.y(),"preview and visible thumbnails do not overlap");
 click(QStringLiteral("画笔批注"));
 QPointF a(canvas->width()/2-20,canvas->height()/2),b(canvas->width()/2+50,canvas->height()/2+30);
 QMouseEvent press(QEvent::MouseButtonPress,a,a,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(canvas,&press);
 QMouseEvent move(QEvent::MouseMove,b,b,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(canvas,&move);
 QMouseEvent release(QEvent::MouseButtonRelease,b,b,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(canvas,&release);
 check(canvas->image!=original,"annotation changes canvas");check(QImage(file)==original,"annotation preserves original file");
 click(QStringLiteral("复制图片"));check(app.clipboard()->image()==canvas->image,"annotated image can be copied");
 check(panel.grab().save(QString::fromLocal8Bit(argv[1])+"/panel.png"),"panel preview saved outside source");
 click(QStringLiteral("撤销"));check(canvas->image==original,"undo restores original");
 QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);app.sendEvent(&panel,&escape);check(!panel.isVisible(),"Escape hides panel");

 QString error;
 check(ScreenshotStore::importImage({},root+"/empty",&error).isEmpty()&&!error.isEmpty()&&!QFileInfo::exists(root+"/empty"),"empty clipboard does not create files");
 QString imported=ScreenshotStore::importImage(original,root+"/new-folder",&error);
 check(!imported.isEmpty()&&QImage(imported)==original,"import creates configured directory and lossless PNG");
 QString second=ScreenshotStore::importImage(original,root+"/new-folder",&error);
 check(imported!=second&&QFileInfo::exists(imported),"repeated imports preserve previous files");
 check(ScreenshotStore::importImage(original,file+"/blocked",&error).isEmpty()&&!error.isEmpty(),"unwritable destination reports failure");
 app.clipboard()->setImage(original);click(QStringLiteral("从剪贴板导入"));
 check(list->count()==2,"clipboard import is shown immediately");
 click(QStringLiteral("复制路径"));check(app.clipboard()->text()!=file&&QImage(app.clipboard()->text())==original,"imported image has a usable absolute path");
 for(int i=0;i<6;++i)check(original.save(root+"/fixtures/Image-"+QString::number(i)+".PNG","PNG"),"uppercase PNG fixture created");
 QFile incomplete(root+"/fixtures/Pending.png");check(incomplete.open(QIODevice::WriteOnly),"partial screenshot fixture opened");incomplete.write("partial");incomplete.close();
 panel.reveal(QPoint(100,300));app.processEvents();
 check(list->count()==8,"all folder images appear, uppercase accepted, incomplete ignored");
 app.processEvents();
 check(canvas->mapTo(&panel,QPoint(0,canvas->height())).y()<=list->mapTo(&panel,QPoint()).y(),"gallery thumbnails remain below the preview");
 check(panel.grab().save(QString::fromLocal8Bit(argv[1])+"/gallery.png"),"gallery preview saved outside source");
 QWheelEvent wheel(QPointF(10,10),list->mapToGlobal(QPoint(10,10)),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
 app.sendEvent(list->viewport(),&wheel);
 check(list->horizontalScrollBar()->value()>0,"mouse wheel scrolls the horizontal screenshot gallery");
 QImage changed=original;changed.fill(Qt::green);check(changed.save(root+"/fixtures/Pending.png"),"partial capture completed");
 auto wait=[&](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};wait(2300);
 check(list->count()==9,"watcher or fallback discovers completed capture");
 check(QFile::remove(root+"/fixtures/Image-0.PNG"),"one folder image removed");wait(2300);
 check(list->count()==8,"deleted image disappears from list");
 click(QStringLiteral("收起 ×"));
 FloatingBall ball;ball.move(300,300);ball.show();
 check(ball.grab().save(QString::fromLocal8Bit(argv[1])+"/ball.png"),"ball preview saved outside source");
 auto tap=[&](QWidget *widget){QPointF point(widget->rect().center());QMouseEvent down(QEvent::MouseButtonPress,point,widget->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(widget,&down);QMouseEvent up(QEvent::MouseButtonRelease,point,widget->mapToGlobal(point.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(widget,&up);};
 tap(&ball);
 FloatingBall *satellite=nullptr;int count=0;for(auto w:app.topLevelWidgets())if(auto b=dynamic_cast<FloatingBall *>(w)){if(b!=&ball&&b->isVisible()){++count;for(auto label:b->findChildren<QLabel *>())if(label->text()==QStringLiteral("截图"))satellite=b;}}
 check(count==6&&satellite,"single click starts opening immediately without a double-click delay");
 QList<FloatingBall *> cells;for(auto w:app.topLevelWidgets())if(auto b=dynamic_cast<FloatingBall *>(w))if(b!=&ball&&b->isVisible())cells.append(b);
 auto visibleCells=[&](){int n=0;for(auto w:app.topLevelWidgets())if(w!=&ball&&dynamic_cast<FloatingBall *>(w)&&w->isVisible())++n;return n;};
 auto drag=[&](const QPoint &delta,const char *message){
  QPoint start=ball.pos();QList<QPoint> positions;for(auto cell:cells)positions.append(cell->pos());
  QPointF local(ball.rect().center()),global(ball.mapToGlobal(local.toPoint()));
  QMouseEvent down(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(&ball,&down);
  QMouseEvent motion(QEvent::MouseMove,local+delta,global+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(&ball,&motion);
  bool follows=ball.pos()==start+delta;for(int i=0;i<cells.size();++i)follows=follows&&cells[i]->pos()==positions[i]+delta;
  check(follows,message);
  QMouseEvent up(QEvent::MouseButtonRelease,local,global+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(&ball,&up);
 };
 for(auto w:app.topLevelWidgets())if(auto b=dynamic_cast<FloatingBall *>(w))if(b!=&ball&&b->isVisible()) {
  QPointF p=b->rect().center();QEnterEvent enter(p,p,b->mapToGlobal(p.toPoint()));app.sendEvent(b,&enter);
 }
 wait(70);drag(QPoint(35,20),"satellites follow drag in the same input event during expansion");wait(200);
 drag(QPoint(25,15),"expanded satellites follow the main ball without tweening behind it");
 QRect orbitBounds=ball.geometry();for(auto w:app.topLevelWidgets())if(dynamic_cast<FloatingBall *>(w)&&w->isVisible())orbitBounds=orbitBounds.united(w->geometry());
 QImage orbit(orbitBounds.size()+QSize(24,24),QImage::Format_ARGB32_Premultiplied);orbit.fill(QColor("#f1f0ed"));
 {QPainter p(&orbit);for(auto w:app.topLevelWidgets())if(dynamic_cast<FloatingBall *>(w)&&w->isVisible())p.drawPixmap(w->pos()-orbitBounds.topLeft()+QPoint(12,12),w->grab());}
 check(orbit.save(QString::fromLocal8Bit(argv[1])+"/orbit.png"),"orbit preview saved outside source");
 for(auto w:app.topLevelWidgets())if(auto b=dynamic_cast<FloatingBall *>(w))if(b!=&ball&&b->isVisible())check(!b->geometry().intersects(ball.geometry()),"hover does not strand an orbit satellite on the main ball");
 const qreal expandedDistance=QLineF(QRectF(ball.geometry()).center(),QRectF(satellite->geometry()).center()).length();
 tap(&ball);check(visibleCells()==6,"closing keeps satellites alive for the return animation");wait(70);
 check(QLineF(QRectF(ball.geometry()).center(),QRectF(satellite->geometry()).center()).length()<expandedDistance&&satellite->width()<64,"closing animates inward and shrinks instead of disappearing");
 drag(QPoint(20,10),"satellites keep following the main ball while returning");
 QList<QRect> reverseFrames;for(auto cell:cells)reverseFrames.append(cell->geometry());
 tap(&ball);bool continuous=visibleCells()==6;for(int i=0;i<cells.size();++i)continuous=continuous&&cells[i]->geometry()==reverseFrames[i];
 check(continuous,"quick reopen reverses from the current frame without recreating satellites");wait(220);
 tap(&ball);wait(210);app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
 check(visibleCells()==0,"completed return removes the satellites");
 tap(&ball);
 QPointF doublePoint(ball.rect().center()),doubleGlobal(ball.mapToGlobal(doublePoint.toPoint()));
 QMouseEvent doubleClick(QEvent::MouseButtonDblClick,doublePoint,doubleGlobal,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(&ball,&doubleClick);
 QMouseEvent doubleUp(QEvent::MouseButtonRelease,doublePoint,doubleGlobal,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(&ball,&doubleUp);
 wait(250);check(visibleCells()==6,"double click does not toggle the already handled first click twice");
 tap(&ball);wait(210);app.sendPostedEvents(nullptr,QEvent::DeferredDelete);check(visibleCells()==0,"single clicks still respond after a double click");
 tap(&ball);wait(250);satellite=nullptr;for(auto w:app.topLevelWidgets())if(auto b=dynamic_cast<FloatingBall *>(w))if(b!=&ball&&b->isVisible()&&b->accessibleName()==QStringLiteral("截图"))satellite=b;
 check(satellite!=nullptr,"screenshot satellite is available after repeated open and close");
 tap(satellite);
 int visible=0;bool panelShown=false;for(auto w:app.topLevelWidgets()){if(dynamic_cast<FloatingBall *>(w)&&w->isVisible())++visible;if(dynamic_cast<ScreenshotPanel *>(w)&&w!=&panel&&w->isVisible())panelShown=true;}
 wait(210);app.sendPostedEvents(nullptr,QEvent::DeferredDelete);visible=0;for(auto w:app.topLevelWidgets())if(dynamic_cast<FloatingBall *>(w)&&w->isVisible())++visible;
 check(panelShown&&visible==1,"screenshot satellite opens panel and closes satellites");
 ScreenshotPanel overridden(nullptr,root+"/new-folder");overridden.reveal(QPoint(100,300));app.processEvents();
 check(overridden.findChild<QListWidget *>()->count()==2,"session override reads the requested folder");
 check(QSettings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall").value("screenshots/directory").toString()==root+"/fixtures","session folder override preserves the saved folder");
 for(auto w:app.topLevelWidgets())if(auto p=dynamic_cast<ScreenshotPanel *>(w))if(p!=&panel)p->hide();
 panel.reveal(QPoint(100,300));tap(&ball);wait(250);
 QList<QWidget *> floatingWindows{&ball,&panel};for(auto w:app.topLevelWidgets())if(w!=&ball&&dynamic_cast<FloatingBall *>(w)&&w->isVisible())floatingWindows.append(w);
#ifdef Q_OS_MACOS
 if(QGuiApplication::platformName()==QStringLiteral("cocoa"))checkNativeFloatingWindows(floatingWindows,&panel);
#endif
 QWidget ordinary;ordinary.setWindowTitle("FloatingBall focus-loss fixture");ordinary.show();
 ordinary.activateWindow();QApplication::setActiveWindow(&ordinary);ordinary.setFocus();wait(50);
 check(panel.isVisible(),"pinned screenshot survives focus moving to another window");
 panel.activateWindow();QApplication::setActiveWindow(&panel);canvas->setFocus();wait(50);pin->click();
 ordinary.activateWindow();QApplication::setActiveWindow(&ordinary);ordinary.setFocus();wait(50);
 check(!panel.isVisible(),"unpinning restores focus-loss dismissal");
 check(ball.isVisible()&&visibleCells()==6,"focus loss keeps the main ball and expanded satellites visible");
 return 0;
}
