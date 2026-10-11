#include "screenshotpanel.h"
#include "screenshotstore.h"
#include "uiicons.h"
#include "floatingwindow.h"
#include "screenshotcapture.h"
#include "screenrecorder.h"
#include <QGraphicsDropShadowEffect>
#include <QStyledItemDelegate>
#include <QScrollBar>
#include <QWheelEvent>
#include <QFileSystemWatcher>
#include <QCryptographicHash>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEasingCurve>
#include <QProcess>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QInputDialog>
#include <QDesktopServices>
#include <QSaveFile>
#include <QPainterPathStroker>
#include <QLineEdit>
#include <QMenu>
#include <QtMath>

namespace {
class ElidedLabel : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent = nullptr) : QLabel(parent) {
        setMinimumWidth(1);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setPen(palette().color(QPalette::WindowText));
        p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideMiddle, width()));
    }
};

class DragHandle : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) offset = e->globalPosition().toPoint() - window()->pos();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (e->buttons() & Qt::LeftButton) window()->move(e->globalPosition().toPoint() - offset);
    }
private:
    QPoint offset;
};

class ThumbnailDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(76, 58); }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        p->save(); p->setRenderHint(QPainter::Antialiasing); p->setRenderHint(QPainter::SmoothPixmapTransform);
        const bool selected = option.state & QStyle::State_Selected;
        QRectF frame = QRectF(option.rect).adjusted(3, 3, -3, -3);
        p->setBrush(QColor(selected ? "#f0ebfb" : "#efefed"));
        p->setPen(QPen(QColor(selected ? "#9582d1" : "#e6e4e9"), selected ? 1.5 : 1));
        p->drawRoundedRect(frame, 9, 9);
        QPixmap pixmap = qvariant_cast<QIcon>(index.data(Qt::DecorationRole)).pixmap(QSize(128, 88));
        QSize fitted = pixmap.size(); fitted.scale(frame.size().toSize() - QSize(8, 8), Qt::KeepAspectRatio);
        QRect target(QPoint(qRound(frame.center().x()-fitted.width()/2.0), qRound(frame.center().y()-fitted.height()/2.0)), fitted);
        p->drawPixmap(target, pixmap, pixmap.rect()); p->restore();
    }
};

class ThumbnailList : public QListWidget {
protected:
    void wheelEvent(QWheelEvent *e) override {
        const QPoint delta=e->pixelDelta().isNull()?e->angleDelta()/3:e->pixelDelta();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value()-(delta.x()!=0?delta.x():delta.y()));
        e->accept();
    }
};
}

ScreenshotCanvas::ScreenshotCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(300, 170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void ScreenshotCanvas::setImage(const QImage &value) { image = value; history.clear(); undoDirtyStates.clear(); dirty=false; drawing = false; update(); }
QRect ScreenshotCanvas::imageRect() const {
    if (image.isNull()) return {};
    QSize fitted = image.size(); fitted.scale(size() - QSize(20,20), Qt::KeepAspectRatio);
    return QRect(QPoint((width()-fitted.width())/2, (height()-fitted.height())/2), fitted);
}
QPoint ScreenshotCanvas::imagePoint(const QPoint &point) const {
    QRect r = imageRect();
    return QPoint(qBound(0, (point.x()-r.x()) * image.width()/qMax(1,r.width()), image.width()-1),
                  qBound(0, (point.y()-r.y()) * image.height()/qMax(1,r.height()), image.height()-1));
}
void ScreenshotCanvas::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen); p.setBrush(QColor("#efefed")); p.drawRoundedRect(rect(),12,12);
    if (!image.isNull()) {
        QPainterPath clip; clip.addRoundedRect(QRectF(imageRect()), 5, 5); p.setClipPath(clip);
        p.drawImage(imageRect(),image);
    } else {
        FloatingIcons::draw(p, FloatingIcons::Screenshot, QRectF(width()/2.0-16, height()/2.0-42, 32, 32), QColor("#a59db4"));
        p.setPen(QColor("#8b8495"));
        p.drawText(rect().adjusted(0,35,0,0),Qt::AlignCenter,QStringLiteral("还没有截图\n选择目录，或从剪贴板导入"));
    }
}
void ScreenshotCanvas::mousePressEvent(QMouseEvent *e) {
    if (!editing || image.isNull() || e->button()!=Qt::LeftButton || !imageRect().contains(e->pos())) return;
    const bool wasDirty=dirty;
    history.append(image);undoDirtyStates.append(wasDirty);dirty=true;drawing=true;first=last=imagePoint(e->pos());
    markerPath=QPainterPath();markerPath.moveTo(first);
    if(tool==Text) {
        bool accepted=false;const QString text=QInputDialog::getText(this,QStringLiteral("文字批注"),QStringLiteral("文字："),QLineEdit::Normal,{},&accepted);
        drawing=false;
        if(accepted && !text.isEmpty()) {QPainter p(&image);p.setPen(ink);QFont f=p.font();f.setPixelSize(qMax(16,strokeWidth*5));p.setFont(f);p.drawText(first,text);update();}
        else {history.removeLast();undoDirtyStates.removeLast();dirty=wasDirty;}
    }
    if(history.size()>12){history.removeFirst();undoDirtyStates.removeFirst();}
}
void ScreenshotCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (!drawing) return;
    QPoint next=imagePoint(e->pos());
    if(tool==Arrow || tool==Rectangle || tool==Marker)image=history.last();
    QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);
    QColor color=ink;if(tool==Marker)color.setAlpha(75);
    const qreal width=tool==Marker?strokeWidth*5:strokeWidth;
    p.setPen(QPen(color,width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(tool==Rectangle)p.drawRect(QRect(first,next).normalized());
    else if(tool==Arrow) {
        p.drawLine(first,next);const qreal angle=qAtan2(next.y()-first.y(),next.x()-first.x()),tip=qMax(12,strokeWidth*4);
        p.setBrush(color);p.setPen(Qt::NoPen);QPolygonF triangle;
        triangle<<QPointF(next)<<QPointF(next)-QPointF(qCos(angle-0.5)*tip,qSin(angle-0.5)*tip)<<QPointF(next)-QPointF(qCos(angle+0.5)*tip,qSin(angle+0.5)*tip);p.drawPolygon(triangle);
    } else if(tool==Marker){markerPath.lineTo(next);p.drawPath(markerPath);}
    else if(tool==Mosaic) {
        const QImage pixels=history.last().scaled(qMax(1,image.width()/12),qMax(1,image.height()/12),Qt::IgnoreAspectRatio,Qt::FastTransformation).scaled(image.size(),Qt::IgnoreAspectRatio,Qt::FastTransformation);
        QPainterPath path;path.moveTo(last);path.lineTo(next);QPainterPathStroker stroker;stroker.setWidth(strokeWidth*7);stroker.setCapStyle(Qt::RoundCap);
        p.setClipPath(stroker.createStroke(path));p.setRenderHint(QPainter::SmoothPixmapTransform,false);p.drawImage(0,0,pixels);
    } else p.drawLine(last,next);
    p.end();last=next;update();
}
void ScreenshotCanvas::mouseReleaseEvent(QMouseEvent *e) {if(drawing)mouseMoveEvent(e);drawing=false;}
void ScreenshotCanvas::undo() { drawing=false;if (!history.isEmpty()) { image=history.takeLast();dirty=undoDirtyStates.takeLast();update(); } }

ScreenshotPanel::ScreenshotPanel(QWidget *parent, const QString &directoryOverride) : QWidget(parent) {
    setProperty("floatingballSurface",true);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("FloatingBall · 截图工作台"));
    resize(440,420);
    auto outer=new QVBoxLayout(this); outer->setContentsMargins(14,12,14,20);
    auto card=new QWidget; card->setObjectName("card"); outer->addWidget(card);
    card->setStyleSheet(
        "QWidget#card { background:#faf9f7; border:1px solid #e2dfe7; border-radius:18px; }"
        "QLabel { color:#777080; font-size:11px; } QLabel#title { color:#36303f; font-size:15px; font-weight:600; }"
        "QToolButton { background:#eeecf0; color:#51495f; border:0; border-radius:9px; padding:0 9px; font-size:12px; }"
        "QToolButton:hover { background:#e5e0ed; } QToolButton:pressed, QToolButton:checked { background:#ddd3f1; }"
        "QToolButton#copyImage { background:#7b68b5; color:white; } QToolButton#copyImage:hover { background:#8a76c7; }"
        "QToolButton#headerButton { background:transparent; padding:0; } QToolButton#headerButton:hover { background:#eeebf3; }"
        "QToolButton#headerButton:checked { background:#ddd3f1; } QToolButton:disabled { color:#afa9b8; }"
        "QComboBox, QSpinBox { background:#eeecf0; color:#51495f; border:0; border-radius:9px; padding:6px 9px; font-size:12px; }"
        "QComboBox::drop-down { border:0; width:18px; } QComboBox QAbstractItemView { background:#faf9f7; color:#51495f; selection-background-color:#ddd3f1; }"
        "QSpinBox::up-button, QSpinBox::down-button { width:16px; border:0; } QCheckBox { color:#51495f; font-size:11px; spacing:4px; }"
        "QListWidget { background:transparent; border:0; outline:0; }");
    auto shadow=new QGraphicsDropShadowEffect(card); shadow->setBlurRadius(22); shadow->setOffset(0,5); shadow->setColor(QColor(40,30,60,36)); card->setGraphicsEffect(shadow);
    auto layout=new QVBoxLayout(card); layout->setContentsMargins(14,12,14,12); layout->setSpacing(8);
    auto header=new DragHandle; header->setCursor(Qt::OpenHandCursor); auto title=new QHBoxLayout(header); title->setContentsMargins(0,0,0,0); title->setSpacing(4);
    auto heading=new QVBoxLayout; heading->setSpacing(2);
    auto titleLabel=new QLabel(QStringLiteral("截图工作台")); titleLabel->setObjectName("title"); titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(titleLabel);
    details=new ElidedLabel; details->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(details); title->addLayout(heading,1);
    auto button=[this](const QString &text, FloatingIcons::Icon icon, bool iconOnly=false) {
        auto b=new QToolButton(this); b->setText(text); b->setAccessibleName(text); b->setToolTip(text);
        b->setIcon(FloatingIcons::make(icon)); b->setIconSize(QSize(17,17)); b->setFixedHeight(32); b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(iconOnly?Qt::ToolButtonIconOnly:Qt::ToolButtonTextBesideIcon);
        if(iconOnly) b->setFixedWidth(32); return b;
    };
    auto take=button(QStringLiteral("截取区域"),FloatingIcons::Capture,true);take->setObjectName("captureRegion");title->addWidget(take);
    auto pin=button(QStringLiteral("固定"),FloatingIcons::Pin,true); pin->setObjectName("headerButton"); pin->setCheckable(true); pin->setChecked(true); title->addWidget(pin);
    auto updatePinHint=[pin](bool fixed){pin->setToolTip(fixed?QStringLiteral("已固定置顶 · 点击后切换应用自动收起"):QStringLiteral("点击固定置顶 · 切换应用时保持展示"));};
    connect(pin,&QToolButton::toggled,this,updatePinHint); updatePinHint(true);
    auto close=button(QStringLiteral("收起 ×"),FloatingIcons::Close,true); close->setObjectName("headerButton"); title->addWidget(close); layout->addWidget(header);
    connect(close,&QToolButton::clicked,this,&QWidget::hide);
    auto captureRow=new QHBoxLayout;captureRow->setSpacing(6);
    auto mode=new QComboBox;mode->setObjectName("captureMode");mode->setAccessibleName(QStringLiteral("截图范围"));
    mode->setMinimumWidth(96);
    mode->addItems({QStringLiteral("区域"),QStringLiteral("窗口"),QStringLiteral("当前屏幕"),QStringLiteral("全部屏幕")});captureRow->addWidget(mode,1);
    auto delay=new QSpinBox;delay->setObjectName("captureDelay");delay->setRange(0,30);delay->setSuffix(QStringLiteral(" 秒"));delay->setSpecialValueText(QStringLiteral("无延时"));delay->setAccessibleName(QStringLiteral("截图延时"));captureRow->addWidget(delay);
    delay->setFixedWidth(86);
    auto cursor=new QCheckBox(QStringLiteral("光标"));cursor->setObjectName("captureCursor");captureRow->addWidget(cursor);
#ifdef Q_OS_MACOS
    auto cursorAvailability=[cursor,mode]{const bool supported=mode->currentIndex()>=2;cursor->setEnabled(supported);if(!supported)cursor->setChecked(false);cursor->setToolTip(QStringLiteral("全屏截图可包含鼠标光标"));};
    connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),this,cursorAvailability);cursorAvailability();
#endif
    auto shoot=button(QStringLiteral("截图"),FloatingIcons::Capture);shoot->setObjectName("takeScreenshot");captureRow->addWidget(shoot);layout->addLayout(captureRow);
    auto takeConfigured=[this,mode,delay,cursor]{CaptureOptions options;options.mode=CaptureOptions::Mode(mode->currentIndex());options.delaySeconds=delay->value();options.includeCursor=cursor->isChecked();takeScreenshot(geometry().center(),options);};
    connect(take,&QToolButton::clicked,this,takeConfigured);connect(shoot,&QToolButton::clicked,this,takeConfigured);
    auto record=button(QStringLiteral("录屏"),FloatingIcons::Capture);record->setObjectName("recordScreen");captureRow->addWidget(record);
    auto recordMenu=new QMenu(record);recordMenu->addAction(QStringLiteral("录制屏幕…"),this,[this]{startRecording(false);});
    auto recordWindow=recordMenu->addAction(QStringLiteral("录制窗口…"),this,[this]{startRecording(true);});
    if(QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))){recordWindow->setEnabled(false);recordWindow->setToolTip(QStringLiteral("当前 Wayland 后端支持屏幕录制，区域由系统选择器提供"));}
    record->setMenu(recordMenu);record->setPopupMode(QToolButton::MenuButtonPopup);
    connect(record,&QToolButton::clicked,this,[this]{startRecording(false);});
    auto newShortcut=new QShortcut(QKeySequence::New,this);connect(newShortcut,&QShortcut::activated,shoot,&QToolButton::click);
    canvas=new ScreenshotCanvas; layout->addWidget(canvas,1);
    thumbnails=new ThumbnailList; thumbnails->setViewMode(QListView::IconMode); thumbnails->setFlow(QListView::LeftToRight);
    thumbnails->setWrapping(false); thumbnails->setIconSize(QSize(64,44)); thumbnails->setFixedHeight(60);
    thumbnails->setSpacing(0); thumbnails->setGridSize(QSize(76,58)); thumbnails->setItemDelegate(new ThumbnailDelegate(thumbnails));
    thumbnails->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    thumbnails->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); thumbnails->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); layout->addWidget(thumbnails);
    connect(thumbnails,&QListWidget::currentRowChanged,this,[this](int row){select(row);});
    auto actions=new QHBoxLayout; actions->setSpacing(6);
    auto copy=button(QStringLiteral("复制图片"),FloatingIcons::Copy); copy->setObjectName("copyImage"); copy->setIcon(FloatingIcons::make(FloatingIcons::Copy,Qt::white));
    auto path=button(QStringLiteral("复制路径"),FloatingIcons::Path); path->setObjectName("copyPath");
    edit=button(QStringLiteral("画笔批注"),FloatingIcons::Pen,true); edit->setObjectName("annotate"); edit->setCheckable(true);
    auto undo=button(QStringLiteral("撤销"),FloatingIcons::Undo,true); undo->setObjectName("undo");
    auto save=button(QStringLiteral("另存副本"),FloatingIcons::Save,true); save->setObjectName("saveCopy");
    for(auto b:{copy,path,edit,undo,save}) actions->addWidget(b); layout->addLayout(actions);
    connect(copy,&QToolButton::clicked,this,[this]{if(!canvas->image.isNull()){QString error;const bool copied=ScreenshotStore::copyImage(canvas->image,&error);status->setText(copied?QStringLiteral("✓ 图片已复制"):error);if(copied){canvas->dirty=false;transientImage=false;}}});
    connect(path,&QToolButton::clicked,this,[this]{if(QFileInfo::exists(currentPath)){ QApplication::clipboard()->setText(currentPath); status->setText(QStringLiteral("✓ 原图路径已复制")); }});
    connect(edit,&QToolButton::toggled,this,[this](bool enabled){canvas->editing=enabled;canvas->setCursor(enabled?Qt::CrossCursor:Qt::ArrowCursor);});
    auto tools=new QWidget;tools->setObjectName("annotationTools");auto toolRow=new QHBoxLayout(tools);toolRow->setContentsMargins(0,0,0,0);toolRow->setSpacing(6);
    auto tool=new QComboBox;tool->setObjectName("annotationTool");tool->setAccessibleName(QStringLiteral("批注工具"));tool->addItems({QStringLiteral("画笔"),QStringLiteral("箭头"),QStringLiteral("矩形"),QStringLiteral("荧光笔"),QStringLiteral("马赛克"),QStringLiteral("文字")});toolRow->addWidget(tool,1);
    auto color=new QComboBox;color->setObjectName("annotationColor");color->setAccessibleName(QStringLiteral("批注颜色"));
    for(const auto &item:QList<QPair<QString,QString>>{{QStringLiteral("粉红"),"#ff637d"},{QStringLiteral("黄色"),"#ffd84d"},{QStringLiteral("蓝色"),"#55aaff"},{QStringLiteral("白色"),"#ffffff"},{QStringLiteral("黑色"),"#222222"}})color->addItem(item.first,item.second);toolRow->addWidget(color);
    auto thickness=new QSpinBox;thickness->setObjectName("annotationWidth");thickness->setRange(2,20);thickness->setValue(4);thickness->setSuffix(QStringLiteral(" px"));thickness->setAccessibleName(QStringLiteral("线条粗细"));toolRow->addWidget(thickness);
    connect(tool,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int value){canvas->tool=ScreenshotCanvas::Tool(value);});
    connect(color,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,color]{canvas->ink=QColor(color->currentData().toString());});
    connect(thickness,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){canvas->strokeWidth=value;});
    tools->hide();connect(edit,&QToolButton::toggled,tools,&QWidget::setVisible);layout->addWidget(tools);
    connect(undo,&QToolButton::clicked,canvas,&ScreenshotCanvas::undo);
    auto copyShortcut=new QShortcut(QKeySequence::Copy,this); connect(copyShortcut,&QShortcut::activated,copy,&QToolButton::click);
    auto undoShortcut=new QShortcut(QKeySequence::Undo,this); connect(undoShortcut,&QShortcut::activated,undo,&QToolButton::click);
    connect(save,&QToolButton::clicked,this,[this]{
        if(canvas->image.isNull()) return;
        const QString saveDirectory=currentPath.isEmpty()?directory:QFileInfo(currentPath).absolutePath();
        QString target=QFileDialog::getSaveFileName(this,QStringLiteral("保存批注副本"),QDir(saveDirectory).filePath("Annotated-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz")+".png"), "PNG (*.png)");
        if(target.isEmpty()) return;
        if(QFileInfo(target).absoluteFilePath()==QFileInfo(currentPath).absoluteFilePath() || (!QFileInfo(target).canonicalFilePath().isEmpty() && QFileInfo(target).canonicalFilePath()==QFileInfo(currentPath).canonicalFilePath())){status->setText(QStringLiteral("请选择新文件名，保留原截图"));return;}
        QSaveFile file(target);const bool saved=file.open(QIODevice::WriteOnly)&&canvas->image.save(&file,"PNG")&&file.commit();
        status->setText(saved?QStringLiteral("✓ 副本已保存"):QStringLiteral("保存失败，请检查目标目录"));
        if(saved){canvas->setImage(canvas->image);transientImage=false;}
    });
    auto footer=new QHBoxLayout; footer->setSpacing(4); status=new ElidedLabel; status->setFixedHeight(20); footer->addWidget(status,1);
    auto import=button(QStringLiteral("从剪贴板导入"),FloatingIcons::Import,true); import->setObjectName("importClipboard"); footer->addWidget(import);
    connect(import,&QToolButton::clicked,this,[this]{importClipboard();});
    auto folder=button(QStringLiteral("截图目录…"),FloatingIcons::Folder,true); folder->setObjectName("chooseDirectory"); footer->addWidget(folder); layout->addLayout(footer);
    connect(folder,&QToolButton::clicked,this,[this]{chooseDirectory();});
    auto openFolder=button(QStringLiteral("打开目录"),FloatingIcons::Folder,true);openFolder->setObjectName("openDirectory");footer->insertWidget(1,openFolder);
    connect(openFolder,&QToolButton::clicked,this,[this]{if(!QDir().mkpath(directory)||!QDesktopServices::openUrl(QUrl::fromLocalFile(directory)))status->setText(QStringLiteral("无法打开截图目录"));});
    auto recordingRow=new QWidget;recordingRow->setObjectName("recordingResult");auto recordingActions=new QHBoxLayout(recordingRow);recordingActions->setContentsMargins(0,0,0,0);
    auto play=button(QStringLiteral("打开录屏"),FloatingIcons::Browser);auto videoPath=button(QStringLiteral("复制录屏路径"),FloatingIcons::Path);
    recordingActions->addWidget(play);recordingActions->addWidget(videoPath);recordingActions->addStretch();layout->addWidget(recordingRow);recordingRow->hide();
    connect(play,&QToolButton::clicked,this,[this]{if(!QDesktopServices::openUrl(QUrl::fromLocalFile(recordingPath)))status->setText(QStringLiteral("无法打开视频，请检查默认播放器"));});
    connect(videoPath,&QToolButton::clicked,this,[this]{if(QFileInfo::exists(recordingPath)){QApplication::clipboard()->setText(recordingPath);status->setText(QStringLiteral("✓ 录屏路径已复制"));}});
    QSettings settings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall");
    directory=directoryOverride.isEmpty()?settings.value("screenshots/directory").toString():directoryOverride;
    if(directory.startsWith("~/")) directory.replace(0,1,QDir::homePath());
    if(directory.isEmpty()) directory=ScreenshotStore::defaultDirectory();
    directory=QDir(directory).absolutePath();
    watcher=new QFileSystemWatcher(this);
    refreshDelay=new QTimer(this); refreshDelay->setSingleShot(true); refreshDelay->setInterval(180);
    connect(refreshDelay,&QTimer::timeout,this,[this]{if(isVisible()&&!canvas->editing)refresh();});
    connect(watcher,&QFileSystemWatcher::directoryChanged,this,[this]{refreshDelay->start();});
    auto timer=new QTimer(this); timer->setInterval(2000);
    connect(timer,&QTimer::timeout,this,[this]{if(isVisible()&&!canvas->editing)refresh();}); timer->start();
    updateWatcher();
    capture=new ScreenshotCapture(this);
    recorder=new ScreenRecorder(this);
    connect(recorder,&ScreenRecorder::finished,this,[this,recordingRow](const QString &file,const QString &error){
        restoreFloatingWindows();reveal(captureAnchor,false);
        if(!file.isEmpty()){recordingPath=file;recordingRow->show();status->setText(QStringLiteral("✓ 录屏已保存 · ")+QFileInfo(file).fileName());}
        else status->setText(error.isEmpty()?QStringLiteral("录屏已取消"):error);
    });
    connect(capture,&ScreenshotCapture::finished,this,[this,take,shoot](const QImage &image,const QString &error,bool cancelled){
        if(captureProgress){captureProgress->hide();captureProgress->deleteLater();captureProgress=nullptr;}
        take->setEnabled(true);
        shoot->setEnabled(true);
        restoreFloatingWindows();
        if(!image.isNull())showCapture(image,captureAnchor);
        else if(!cancelled){reveal(captureAnchor,false);status->setText(error);}
        else {if(isVisible()){raise();activateWindow();}status->setText(QStringLiteral("截图已取消"));}
    });
    connect(capture,&ScreenshotCapture::countdownChanged,this,[this](int seconds){
        if(seconds==0){if(captureProgress)captureProgress->hide();return;}
        if(!captureProgress){
            captureProgress=new QWidget(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);captureProgress->setObjectName("captureCountdown");
            auto row=new QHBoxLayout(captureProgress);auto message=new QLabel;message->setObjectName("countdownText");row->addWidget(message);
            auto cancel=new QToolButton;cancel->setText(QStringLiteral("取消"));row->addWidget(cancel);connect(cancel,&QToolButton::clicked,capture,&ScreenshotCapture::cancel);
            auto escape=new QShortcut(QKeySequence(Qt::Key_Escape),captureProgress);connect(escape,&QShortcut::activated,capture,&ScreenshotCapture::cancel);
            captureProgress->setStyleSheet("QWidget { background:#302b3a; color:#faf9fc; border-radius:12px; padding:8px; } QToolButton { background:#65597d; }");
            captureProgress->adjustSize();QScreen *screen=QGuiApplication::screenAt(captureAnchor);if(!screen)screen=QGuiApplication::primaryScreen();
            captureProgress->move(screen->availableGeometry().center()-QPoint(captureProgress->width()/2,captureProgress->height()/2));FloatingWindow::prepare(captureProgress,true);
        }
        captureProgress->findChild<QLabel *>("countdownText")->setText(QStringLiteral("%1 秒后截图").arg(seconds));captureProgress->show();captureProgress->adjustSize();
    });
    // macOS shares the "active" appearance across tool windows, so
    // isActiveWindow() does not tell us whether this panel owns keyboard focus.
    auto dismissIfUnpinned=[this,pin]{QTimer::singleShot(0,this,[this,pin]{if(isVisible()&&!pin->isChecked()&&QApplication::activeWindow()!=this&&!QApplication::activeModalWidget())hide();});};
    connect(qApp,&QApplication::focusChanged,this,[dismissIfUnpinned](QWidget *,QWidget *){dismissIfUnpinned();});
    connect(qApp,&QGuiApplication::focusWindowChanged,this,[dismissIfUnpinned](QWindow *){dismissIfUnpinned();});
    connect(qApp,&QGuiApplication::applicationStateChanged,this,[dismissIfUnpinned](Qt::ApplicationState state){if(state!=Qt::ApplicationActive)dismissIfUnpinned();});
    FloatingWindow::prepare(this,true);
}
ScreenshotPanel::~ScreenshotPanel() {
    capture->cancel();
    recorder->stop();
    if(captureProgress)delete captureProgress;
}
void ScreenshotPanel::restoreFloatingWindows() {
    for(const auto &window:hiddenForCapture)if(window)window->show();
    hiddenForCapture.clear();
}
void ScreenshotPanel::startRecording(bool window) {
    if(capture->isActive() || recorder->isActive())return;
    captureAnchor=geometry().center();hiddenForCapture.clear();
    for(QWidget *widget:QApplication::topLevelWidgets())if(widget->isVisible()&&widget->property("floatingballSurface").toBool())hiddenForCapture.append(widget);
    for(const auto &widget:hiddenForCapture)if(widget)widget->hide();
    if(auto popup=QApplication::activePopupWidget())popup->hide();
    recorder->start(directory,captureAnchor,window);
}
void ScreenshotPanel::captureRegion(const QPoint &anchor) {
    takeScreenshot(anchor,{});
}
void ScreenshotPanel::takeScreenshot(const QPoint &anchor,const CaptureOptions &options) {
    if(capture->isActive() || recorder->isActive())return;
    if(canvas->dirty || transientImage){
        reveal(anchor,false);status->setText(QStringLiteral("请先复制或另存批注，再关闭画笔开始截图"));return;
    }
    captureAnchor=anchor;
    findChild<QToolButton *>("captureRegion")->setEnabled(false);
    findChild<QToolButton *>("takeScreenshot")->setEnabled(false);
    hiddenForCapture.clear();
    for(QWidget *window:QApplication::topLevelWidgets())
        if(window->isVisible() && window->property("floatingballSurface").toBool())hiddenForCapture.append(window);
    for(const auto &window:hiddenForCapture)if(window)window->hide();
    if(QWidget *popup=QApplication::activePopupWidget())popup->hide();
    capture->start(anchor,options);
}
void ScreenshotPanel::showCapture(const QImage &image,const QPoint &anchor) {
    if(image.isNull())return;
    QString error;const QString path=ScreenshotStore::saveCapture(image,directory,&error);
    edit->setChecked(false);canvas->editing=false;canvas->setImage(image);
    transientImage=path.isEmpty();
    if(!path.isEmpty()){
        currentPath=path;directorySignature.clear();reveal(anchor,false);
        status->setText(QStringLiteral("✓ 截图已保存"));
    }else{
        reveal(anchor,false);currentPath.clear();canvas->setImage(image);
        details->setText(QStringLiteral("未保存 · %1 × %2").arg(image.width()).arg(image.height()));
        for(const char *name:{"copyImage","annotate","undo","saveCopy"})findChild<QToolButton *>(name)->setEnabled(true);
        findChild<QToolButton *>("copyPath")->setEnabled(false);
        status->setText(QStringLiteral("保存失败，可复制或另存：")+error);
    }
}
void ScreenshotPanel::updateWatcher() {
    QStringList wanted;
    if(QFileInfo(directory).isDir()) wanted.append(directory);
    QString ancestor=QFileInfo(directory).absolutePath();
    while(!QFileInfo(ancestor).isDir()) {
        QString parent=QFileInfo(ancestor).absolutePath();
        if(parent==ancestor) break;
        ancestor=parent;
    }
    if(QFileInfo(ancestor).isDir()&&!wanted.contains(ancestor)) wanted.append(ancestor);
    for(const QString &path:watcher->directories()) if(!wanted.contains(path)) watcher->removePath(path);
    for(const QString &path:wanted) if(!watcher->directories().contains(path)) watcher->addPath(path);
}
void ScreenshotPanel::refresh() {
    updateWatcher();
    // Preserve strokes while editing, even across hide/reveal and external writes.
    if(canvas->editing || canvas->dirty || !canvas->history.isEmpty() || transientImage) return;
    QDir dir(directory); QStringList fresh; QByteArray signature;
    const auto entries=dir.entryInfoList(QDir::Files|QDir::Readable,QDir::Time);
    for(const auto &file:entries) {
        if(!QStringList({"png","jpg","jpeg","webp"}).contains(file.suffix().toLower())) continue;
        fresh.append(file.absoluteFilePath());
        signature+=file.absoluteFilePath().toUtf8()+'\0'+QByteArray::number(file.size())+':'+QByteArray::number(file.lastModified().toMSecsSinceEpoch())+'\n';
    }
    signature=QCryptographicHash::hash(signature,QCryptographicHash::Sha256);
    if(signature==directorySignature) return;
    QString previous=currentPath; directorySignature=signature;
    paths.clear(); thumbnails->blockSignals(true); thumbnails->clear();
    bool incomplete=false;
    for(const QString &file:fresh) {
        QImageReader reader(file); reader.setAutoTransform(true);
        QSize fitted=reader.size(); fitted.scale(QSize(160,120),Qt::KeepAspectRatio);
        if(fitted.isValid()) reader.setScaledSize(fitted);
        QImage thumbnail=reader.read();
        if(thumbnail.isNull()) { incomplete=true; continue; }
        paths.append(file);
        auto item=new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail)),QString(),thumbnails);
        item->setToolTip(QFileInfo(file).fileName());
    }
    // Retry incomplete captures on the fallback timer, including writes that
    // do not generate a directory notification on some filesystems.
    if(incomplete) directorySignature.clear();
    thumbnails->blockSignals(false);
    int row=paths.indexOf(previous); thumbnails->setCurrentRow(row<0?0:row);
    if(paths.isEmpty()){currentPath.clear();canvas->setImage({});}
    thumbnails->setVisible(paths.size()>1);
    for(const QString &name:{QStringLiteral("copyImage"),QStringLiteral("copyPath"),QStringLiteral("annotate"),QStringLiteral("undo"),QStringLiteral("saveCopy")})
        findChild<QToolButton *>(name)->setEnabled(!canvas->image.isNull());
    if(paths.isEmpty()) details->setText(QStringLiteral("选择目录，随手取用"));
    findChild<QToolButton *>("chooseDirectory")->setToolTip(QStringLiteral("截图目录：")+directory);
    status->setText(QFileInfo(directory).isDir()
        ? QStringLiteral("%1 张图片 · %2").arg(paths.size()).arg(QDir(directory).dirName())
        : QStringLiteral("导入时创建截图目录"));
}
void ScreenshotPanel::select(int row) {
    if(row<0||row>=paths.size())return;
    if(canvas->dirty || transientImage){
        status->setText(QStringLiteral("请先复制或另存当前图片，再切换截图"));
        const QSignalBlocker blocker(thumbnails);thumbnails->setCurrentRow(paths.indexOf(currentPath));return;
    }
    currentPath=paths[row]; QImageReader reader(currentPath); reader.setAutoTransform(true);
    canvas->setImage(reader.read()); edit->setChecked(false);
    details->setText(QFileInfo(currentPath).fileName()+QStringLiteral(" · %1 × %2").arg(canvas->image.width()).arg(canvas->image.height()));
    details->setToolTip(currentPath);
    status->setText(canvas->image.isNull()?QStringLiteral("图片读取失败"):QFileInfo(currentPath).fileName());
}
void ScreenshotPanel::chooseDirectory(){
    if(canvas->dirty || transientImage){status->setText(QStringLiteral("请先复制或另存当前图片，再更换目录"));return;}
    QString selected=QFileDialog::getExistingDirectory(this,QStringLiteral("选择截图目录"),directory);
    if(selected.isEmpty())return;
    directory=selected; directorySignature.clear(); currentPath.clear(); canvas->setImage({}); edit->setChecked(false); thumbnails->clear(); QSettings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall").setValue("screenshots/directory",directory);paths.clear();refresh();
}
void ScreenshotPanel::importClipboard() {
    if(canvas->dirty || transientImage) {
        status->setText(QStringLiteral("请先复制或另存当前批注，再关闭画笔导入"));
        return;
    }
    QString error;
    const QString imported=ScreenshotStore::importImage(QApplication::clipboard()->image(),directory,&error);
    if(imported.isEmpty()) {status->setText(error);return;}
    edit->setChecked(false);canvas->setImage({});directorySignature.clear();refresh();
    const int row=paths.indexOf(imported);
    if(row>=0) {thumbnails->setCurrentRow(row);select(row);}
    status->setText(QStringLiteral("✓ 已导入并保存：")+QFileInfo(imported).fileName());
}
void ScreenshotPanel::reveal(const QPoint &anchor,bool toggle){
    if(isVisible()){if(toggle)hide();else{refresh();raise();activateWindow();}return;} refresh();
    QScreen *screen=QGuiApplication::screenAt(anchor);if(!screen)screen=QGuiApplication::primaryScreen();
    QRect bounds=screen->availableGeometry();
    ensurePolished(); layout()->activate();
    const QSize preferred=QSize(520,thumbnails->isHidden()?398:458).expandedTo(minimumSizeHint());
    resize(preferred.boundedTo(bounds.size())); layout()->activate();
    const int x=anchor.x()+width()+36<=bounds.right()?anchor.x()+36:anchor.x()-width()-36;
    QPoint target(qBound(bounds.left(),x,bounds.right()-width()+1),qBound(bounds.top(),anchor.y()-height()/2,bounds.bottom()-height()+1));
    move(target+QPoint(0,18));show();raise();activateWindow();
    auto animation=new QPropertyAnimation(this,"pos",this);animation->setDuration(260);animation->setStartValue(pos());animation->setEndValue(target);animation->setEasingCurve(QEasingCurve::OutBack);animation->start(QAbstractAnimation::DeleteWhenStopped);
}
void ScreenshotPanel::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape)hide();else QWidget::keyPressEvent(e);}
