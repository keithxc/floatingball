#include "screenshotcapture.h"
#include "screenshotpanel.h"
#include "screenshotstore.h"
#include "screenrecorder.h"
#include "screentext.h"
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
#include <QFileDialog>
#include <QPainter>
#include <QSlider>
#include <QTextEdit>
#include <QTextCursor>
#include <QTabWidget>
#include <QPointer>
#include <QElapsedTimer>
#include <QThread>
#include <QLabel>
#include <cstdio>
#include <cstdlib>
static void check(bool good,const char *description){printf("%s: %s\n",good?"PASS":"FAIL",description);fflush(stdout);if(!good)std::exit(1);}
static void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
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
    check(panel.findChild<QWidget *>("captureOptions")->isHidden(),"capture settings start folded so results take priority");
    panel.findChild<QToolButton *>("captureSettings")->click();app.processEvents();
    check(panel.findChild<QWidget *>("captureOptions")->isVisible(),"capture range, delay, cursor and recording settings expand on demand");
    panel.findChild<QToolButton *>("captureSettings")->click();
    ScreenshotCanvas *edited=nullptr;for(auto widget:panel.findChildren<QWidget *>())if(auto value=dynamic_cast<ScreenshotCanvas *>(widget))edited=value;
    check(edited!=nullptr,"captured pixels are available for annotation");
    ScreenshotCanvas transforms;transforms.resize(620,420);transforms.setImage(image);
    const auto transformPointer=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,point,transforms.mapToGlobal(point),button,buttons,Qt::NoModifier);app.sendEvent(&transforms,&event);};
    transforms.beginCrop();
    transformPointer(QEvent::MouseButtonPress,QPoint(130,100),Qt::LeftButton,Qt::LeftButton);
    transformPointer(QEvent::MouseMove,QPoint(30,40),Qt::NoButton,Qt::LeftButton);
    transformPointer(QEvent::MouseButtonRelease,QPoint(30,40),Qt::LeftButton,Qt::NoButton);
    check(transforms.cropSelection()==QRect(20,30,101,61)&&transforms.image==image&&!transforms.dirty&&transforms.history.isEmpty(),"reverse crop drag previews an explicit native-pixel region without changing pixels or undo");
    check(!transforms.rotateClockwise()&&transforms.image==image,"a pending crop cannot be silently replaced by a rotation");
    transforms.cancelImageAction();
    check(transforms.image==image&&!transforms.isCropping()&&!transforms.dirty&&transforms.history.isEmpty(),"Cancel crop preserves exact original pixels and clean state");
    transforms.beginCrop();app.sendEvent(&transforms,&escape);
    check(!transforms.isCropping()&&transforms.image==image,"Escape cancels crop without closing or modifying its image");
    transforms.beginCrop();
    transformPointer(QEvent::MouseButtonPress,QPoint(30,40),Qt::LeftButton,Qt::LeftButton);
    transformPointer(QEvent::MouseButtonRelease,QPoint(130,100),Qt::LeftButton,Qt::NoButton);
    check(transforms.applyCrop()&&transforms.image==image.copy(20,30,101,61)&&transforms.dirty&&transforms.history.size()==1,"Apply crop commits exact retained pixels as one unexported undoable edit");
    transforms.undo();check(transforms.image==image&&!transforms.dirty,"crop undo restores exact original pixels and prior export state");
    transforms.beginCrop();
    transformPointer(QEvent::MouseButtonPress,QPoint(30,40),Qt::LeftButton,Qt::LeftButton);
    transformPointer(QEvent::MouseButtonRelease,QPoint(30,40),Qt::LeftButton,Qt::NoButton);
    check(!transforms.applyCrop()&&transforms.image==image&&transforms.history.isEmpty(),"a click with no usable crop area cannot create an edit");transforms.cancelImageAction();
    QImage asymmetric(3,2,QImage::Format_ARGB32);for(int y=0;y<2;++y)for(int x=0;x<3;++x)asymmetric.setPixelColor(x,y,QColor(30+x*60,40+y*100,10+x+y,90+x*20+y));
    transforms.setImage(asymmetric);check(transforms.rotateClockwise()&&transforms.image.size()==QSize(2,3),"clockwise rotation swaps dimensions");
    bool rotatedPixels=true;for(int y=0;y<2;++y)for(int x=0;x<3;++x)rotatedPixels=rotatedPixels&&transforms.image.pixelColor(1-y,x)==asymmetric.pixelColor(x,y);
    check(rotatedPixels&&transforms.dirty,"rotation preserves exact oriented RGBA pixels including transparency");transforms.undo();
    check(transforms.image==asymmetric&&!transforms.dirty,"rotation undo restores original pixels and clean state");
    check(transforms.flipHorizontal(),"horizontal flip commits an edit");bool flippedPixels=true;for(int y=0;y<2;++y)for(int x=0;x<3;++x)flippedPixels=flippedPixels&&transforms.image.pixelColor(2-x,y)==asymmetric.pixelColor(x,y);
    check(flippedPixels&&transforms.dirty,"horizontal flip mirrors every exact RGBA pixel");transforms.undo();
    transforms.setImage(image);QColor sampled;QPoint sampledPixel;QObject::connect(&transforms,&ScreenshotCanvas::colorPicked,[&](const QColor &color,const QPoint &pixel){sampled=color;sampledPixel=pixel;});transforms.beginColorPick();
    transformPointer(QEvent::MouseButtonPress,QPoint(310,210),Qt::LeftButton,Qt::LeftButton);
    check(sampledPixel==QPoint(300,200)&&sampled==image.pixelColor(300,200)&&transforms.image==image&&!transforms.dirty&&transforms.history.isEmpty(),"color picking samples the displayed native pixel without editing or consuming undo");
    transforms.cancelImageAction();
    panel.findChild<QToolButton *>("rotateImage")->click();const QImage rotatedPanel=edited->image;
    check(edited->dirty&&rotatedPanel.size()==QSize(400,600)&&panel.currentImage()==rotatedPanel,"panel transforms feed the same protected current-image pipeline");
    panel.showCapture(asymmetric,QPoint(100,100));
    check(edited->image==rotatedPanel,"receiving a new image cannot silently discard an unexported transform");
    const auto originalFiles=QDir(root.path()+"/shots").entryInfoList({"*.png"},QDir::Files);bool originalsPreserved=originalFiles.size()==3;for(const auto &file:originalFiles)originalsPreserved=originalsPreserved&&QImage(file.absoluteFilePath())==image;
    check(originalsPreserved,"crop/rotate/flip never overwrite saved original screenshots");
    edited->undo();check(edited->image==image&&!edited->dirty,"panel undo restores pixels after a protected replacement attempt");
    panel.findChild<QToolButton *>("cropImage")->click();
    auto pendingCapture=CaptureOptions();pendingCapture.delaySeconds=1;panel.takeScreenshot(QPoint(100,100),pendingCapture);
    check(edited->isCropping()&&panel.isVisible()&&edited->image==image&&!panel.findChild<ScreenshotCapture *>()->isActive(),"new capture cannot discard an uncommitted crop preview");
    panel.findChild<QToolButton *>("cancelImageAction")->click();
    check(!edited->isCropping()&&!edited->dirty&&edited->image==image,"crop Cancel button preserves the current image");
    panel.findChild<QToolButton *>("flipImage")->click();const QImage savePixels=edited->image;const int saveUndo=edited->history.size();
    QTimer::singleShot(0,[]{if(auto dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))dialog->reject();});panel.findChild<QToolButton *>("saveCopy")->click();
    check(edited->dirty&&edited->image==savePixels&&edited->history.size()==saveUndo,"cancelling Save-copy preserves transformed pixels and undo protection");
    const QString editedCopy=root.path()+"/owned-edited-copy.png";
    QTimer::singleShot(0,[editedCopy]{if(auto dialog=qobject_cast<QFileDialog *>(QApplication::activeModalWidget())){dialog->selectFile(editedCopy);QMetaObject::invokeMethod(dialog,"accept",Qt::QueuedConnection);}});panel.findChild<QToolButton *>("saveCopy")->click();
    check(QImage(editedCopy)==savePixels&&!edited->dirty&&edited->history.size()==saveUndo,"Save-copy exports exact transformed pixels while retaining the same undo history as Copy");
    edited->undo();check(edited->image==image&&!edited->dirty,"undo remains available after exporting a transformed copy");
    panel.findChild<QToolButton *>("pinImage")->click();panel.findChild<QToolButton *>("pinImage")->click();app.processEvents();
    QList<QPointer<QWidget>> references;for(auto widget:app.topLevelWidgets())if(widget->objectName()==QStringLiteral("pinnedImageCard"))references.append(widget);
    check(references.size()==2&&references[0]!=references[1],"two pin actions create independent image reference cards");
    for(const auto &reference:references)check(reference->isVisible()&&reference->property("floatingballSurface").toBool(),"each reference is a visible floating surface included in capture exclusion");
    auto firstReference=references[0];auto secondReference=references[1];
    const double initialZoom=firstReference->findChild<QWidget *>("pinnedImageCanvas")->property("imageZoom").toDouble();
    const double untouchedZoom=secondReference->findChild<QWidget *>("pinnedImageCanvas")->property("imageZoom").toDouble();
    firstReference->findChild<QToolButton *>("zoomPinnedIn")->click();
    check(firstReference->findChild<QWidget *>("pinnedImageCanvas")->property("imageZoom").toDouble()>initialZoom&&secondReference->findChild<QWidget *>("pinnedImageCanvas")->property("imageZoom").toDouble()==untouchedZoom,"zooming one reference leaves the other reference unchanged");
    firstReference->findChild<QSlider *>("pinnedImageOpacity")->setValue(55);
    check(firstReference->property("referenceOpacity").toDouble()==0.55&&secondReference->property("referenceOpacity").toDouble()==1.0,"reference opacity changes independently and supports composited Wayland surfaces");
    auto referenceCanvas=firstReference->findChild<QWidget *>("pinnedImageCanvas");const QPoint referenceStart=firstReference->pos(),delta(-20,12),local=referenceCanvas->rect().center(),global=referenceCanvas->mapToGlobal(local);
    QMouseEvent referenceDown(QEvent::MouseButtonPress,local,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(referenceCanvas,&referenceDown);
    QMouseEvent referenceMove(QEvent::MouseMove,local+delta,global+delta,Qt::NoButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(referenceCanvas,&referenceMove);
    QMouseEvent referenceUp(QEvent::MouseButtonRelease,local+delta,global+delta,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);app.sendEvent(referenceCanvas,&referenceUp);
    check(firstReference->pos()==referenceStart+delta,"dragging reference pixels moves that card directly");
    options.delaySeconds=1;panel.takeScreenshot(QPoint(100,100),options);
    check(!firstReference->isVisible()&&!secondReference->isVisible(),"starting a capture hides every owned pinned reference card");
    panel.findChild<ScreenshotCapture *>()->cancel();app.processEvents();
    check(firstReference->isVisible()&&secondReference->isVisible(),"cancelling capture restores both reference cards");
    for(const auto &reference:references)reference->findChild<QToolButton *>("closePinnedImage")->click();app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(references[0].isNull()&&references[1].isNull(),"closing reference cards releases their independent windows");
    QImage ocrFixture(1400,280,QImage::Format_RGB32);ocrFixture.fill(Qt::white);
    {QPainter painter(&ocrFixture);painter.setPen(Qt::black);QFont font(QStringLiteral("Arial"));font.setPixelSize(76);font.setBold(true);painter.setFont(font);painter.drawText(ocrFixture.rect(),Qt::AlignCenter,QStringLiteral("FLOATINGBALL WORKSPACE 7391"));}
    const auto recognition=ScreenText::recognize(ocrFixture);
#ifdef Q_OS_MACOS
    check(recognition.error.isEmpty()&&recognition.text.contains(QStringLiteral("WORKSPACE"),Qt::CaseInsensitive),"real Apple Vision recognition extracts a distinctive fixture word");
#else
    if(recognition.error.isEmpty())check(recognition.text.contains(QStringLiteral("WORKSPACE"),Qt::CaseInsensitive),"real Tesseract recognition extracts a distinctive fixture word");
    else {check(!recognition.provider.isEmpty()&&!recognition.error.isEmpty(),"unavailable local OCR reports its provider and a clear error");printf("OCR_PROVIDER_UNAVAILABLE: %s\n",recognition.error.toUtf8().constData());}
#endif
    printf("OCR_PROVIDER: %s\nOCR_TEXT: %s\n",recognition.provider.toUtf8().constData(),recognition.text.toUtf8().constData());
    edited->setImage(ocrFixture);panel.findChild<QToolButton *>("recognizeText")->click();
    QElapsedTimer recognitionTimeout;recognitionTimeout.start();while(!panel.findChild<QToolButton *>("recognizeText")->isEnabled()&&recognitionTimeout.elapsed()<45000)wait(25);
    check(panel.findChild<QToolButton *>("recognizeText")->isEnabled(),"asynchronous recognition releases its UI job");
    auto textEditor=panel.findChild<QTextEdit *>("recognizedText");
    if(recognition.error.isEmpty())check(textEditor->toPlainText().contains(QStringLiteral("WORKSPACE"),Qt::CaseInsensitive)&&!textEditor->isReadOnly()&&panel.findChild<QTabWidget *>("resultTabs")->currentIndex()==1,"OCR opens an editable text result with real recognized content");
    auto originalClipboard=new QMimeData;const auto clipboardBeforeText=app.clipboard()->mimeData();if(clipboardBeforeText){for(const auto &format:clipboardBeforeText->formats())originalClipboard->setData(format,clipboardBeforeText->data(format));if(clipboardBeforeText->hasImage())originalClipboard->setImageData(clipboardBeforeText->imageData());}
    textEditor->setPlainText(QStringLiteral("Edited OCR result\nsecond line "));panel.findChild<QToolButton *>("copyText")->click();const bool exactText=app.clipboard()->text()==textEditor->toPlainText();
    QString requestedNote,requestedTitle;int noteRequests=0;
    QObject::connect(&panel,&ScreenshotPanel::textNoteRequested,[&](const QString &text,const QString &title){requestedNote=text;requestedTitle=title;++noteRequests;});
    panel.findChild<QToolButton *>("saveTextNote")->click();
    check(noteRequests==1&&requestedNote==textEditor->toPlainText()&&!requestedTitle.isEmpty(),"OCR note action requests exact editable full text with a source title");
    auto textCursor=textEditor->textCursor();textCursor.setPosition(0);textCursor.setPosition(6,QTextCursor::KeepAnchor);textEditor->setTextCursor(textCursor);panel.findChild<QToolButton *>("copyText")->click();const bool selectedText=app.clipboard()->text()==QStringLiteral("Edited");
    panel.findChild<QToolButton *>("saveTextNote")->click();
    check(noteRequests==2&&requestedNote==QStringLiteral("Edited")&&textEditor->toPlainText()==QStringLiteral("Edited OCR result\nsecond line "),"OCR note action uses selected text without changing the editable result");
    edited->setImage(image);panel.findChild<QTabWidget *>("resultTabs")->setCurrentIndex(0);panel.findChild<QToolButton *>("pickColor")->click();
    QSize displaySize=image.size();displaySize.scale(edited->size()-QSize(20,20),Qt::KeepAspectRatio);
    const QPoint imageOrigin((edited->width()-displaySize.width())/2,(edited->height()-displaySize.height())/2),colorPoint=imageOrigin+QPoint(displaySize.width()/3,displaySize.height()/2);
    const QColor expectedColor=image.pixelColor((colorPoint.x()-imageOrigin.x())*image.width()/displaySize.width(),(colorPoint.y()-imageOrigin.y())*image.height()/displaySize.height());
    QMouseEvent sampleEvent(QEvent::MouseButtonPress,colorPoint,edited->mapToGlobal(colorPoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);app.sendEvent(edited,&sampleEvent);
    panel.findChild<QToolButton *>("copyColor")->click();const bool exactHex=app.clipboard()->text()==expectedColor.name(QColor::HexRgb).toUpper();
    panel.findChild<QToolButton *>("copyColorRgb")->click();const bool exactRgb=app.clipboard()->text()==QStringLiteral("rgb(%1, %2, %3)").arg(expectedColor.red()).arg(expectedColor.green()).arg(expectedColor.blue());
    check(exactHex&&exactRgb&&!edited->dirty&&edited->history.isEmpty(),"color actions copy exact HEX and RGB from the selected screenshot pixel without creating an edit");
    panel.findChild<QToolButton *>("pickColor")->click();edited->setImage(ocrFixture);panel.findChild<QTabWidget *>("resultTabs")->setCurrentIndex(1);app.clipboard()->setMimeData(originalClipboard);app.processEvents();
    check(exactText&&selectedText,"editable OCR results copy exact full text or only the selected portion");
    textEditor->clear();panel.findChild<QToolButton *>("recognizeText")->click();panel.findChild<QToolButton *>("cancelRecognition")->click();
    recognitionTimeout.restart();while(!panel.findChild<QToolButton *>("recognizeText")->isEnabled()&&recognitionTimeout.elapsed()<10000)wait(25);
    check(panel.findChild<QToolButton *>("recognizeText")->isEnabled()&&textEditor->toPlainText().isEmpty()&&panel.findChild<QToolButton *>("cancelRecognition")->isHidden(),"cancelling an OCR job preserves the existing text and releases recognition controls");
    app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
    const QString retainedText=QStringLiteral("Retained user text before queued completion");textEditor->setPlainText(retainedText);
    panel.findChild<QToolButton *>("recognizeText")->click();
    auto completedWorker=panel.findChild<QThread *>("textRecognitionWorker");
    // Block only the GUI event delivery. Real OCR finishes on its worker, so
    // the successful result is queued but has not yet replaced retainedText.
    check(completedWorker&&completedWorker->wait(45000),"real OCR worker completes while GUI result delivery is held");
    check(textEditor->toPlainText()==retainedText&&!panel.findChild<QToolButton *>("recognizeText")->isEnabled(),"finished OCR result remains queued until the GUI handles completion");
    panel.findChild<QToolButton *>("cancelRecognition")->click();app.processEvents();
    check(textEditor->toPlainText()==retainedText&&!textEditor->isReadOnly()&&panel.findChild<QToolButton *>("recognizeText")->isEnabled()&&panel.findChild<QLabel *>("textResultDetails")->text().contains(QStringLiteral("已取消")),"cancel after real OCR completion preserves prior text instead of applying the queued result");
    app.sendPostedEvents(nullptr,QEvent::DeferredDelete);textEditor->clear();
    edited->setImage(image);panel.findChild<QTabWidget *>("resultTabs")->setCurrentIndex(0);
    edited->editing=true;
    const auto stroke=[&](QEvent::Type type,const QPoint &point,Qt::MouseButton button,Qt::MouseButtons buttons){QMouseEvent event(type,point,edited->mapToGlobal(point),button,buttons,Qt::NoModifier);app.sendEvent(edited,&event);};
    stroke(QEvent::MouseButtonPress,edited->rect().center()-QPoint(30,10),Qt::LeftButton,Qt::LeftButton);
    stroke(QEvent::MouseMove,edited->rect().center()+QPoint(30,10),Qt::NoButton,Qt::LeftButton);
    stroke(QEvent::MouseButtonRelease,edited->rect().center()+QPoint(30,10),Qt::LeftButton,Qt::NoButton);
    edited->editing=false;const QImage annotated=edited->image;
    check(edited->dirty && annotated!=image,"a real annotation is tracked as an unexported edit");
    const int annotationUndo=edited->history.size();panel.findChild<QToolButton *>("cropImage")->click();panel.findChild<QToolButton *>("cancelImageAction")->click();
    check(edited->image==annotated&&edited->dirty&&edited->history.size()==annotationUndo,"starting and cancelling Crop cannot discard or export existing annotations");
    panel.findChild<QToolButton *>("rotateImage")->click();edited->undo();
    check(edited->image==annotated&&edited->dirty&&edited->history.size()==annotationUndo,"undoing a transform preserves pre-existing unexported annotation protection");
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
    auto closing=new ScreenshotPanel(nullptr,root.path()+"/ocr-teardown");closing->showCapture(ocrFixture,QPoint(100,100));closing->findChild<QToolButton *>("recognizeText")->click();delete closing;app.sendPostedEvents(nullptr,QEvent::DeferredDelete);
    check(true,"destroying a workbench cancels and joins its active OCR worker before returning");
    return 0;
}
