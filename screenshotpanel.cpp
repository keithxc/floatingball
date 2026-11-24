#include "screenshotpanel.h"
#include "screenshotstore.h"
#include "uiicons.h"
#include "floatingwindow.h"
#include "screenshotcapture.h"
#include "screenrecorder.h"
#include "screentext.h"
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
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
#include <QTabWidget>
#include <QTextEdit>
#include <QTextDocument>
#include <QThread>
#include <QUrlQuery>
#include <QSlider>
#include <QSizeGrip>
#include <QMessageBox>
#include <QTransform>
#include <memory>
#include <functional>
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
        p->setBrush(selected ? QColor(90,155,235,42) : QColor(255,255,255,10));
        p->setPen(QPen(selected ? QColor("#75b5ff") : QColor(255,255,255,24), selected ? 1.5 : 1));
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

class ReferenceCanvas : public QWidget {
public:
    ReferenceCanvas(const QImage &image, QWidget *parent = nullptr) : QWidget(parent), pixels(image) {
        setObjectName("pinnedImageCanvas");
        setMinimumSize(160,100);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QStringLiteral("拖动图片移动图卡 · 滚轮缩放"));
    }
    std::function<void(int)> zoomChanged;
    void fit() {
        scale = qMin((width()-12.0)/pixels.width(), (height()-12.0)/pixels.height());
        updateZoom();
    }
    void zoomBy(qreal factor) {
        if (scale <= 0) fit();
        scale = qBound(0.05, scale*factor, 8.0);
        updateZoom();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (scale <= 0) fit();
        QPainter painter(this); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.fillRect(rect(), QColor("#101318"));
        const QSizeF fitted(pixels.width()*scale, pixels.height()*scale);
        const QRectF target(QPointF((width()-fitted.width())/2.0, (height()-fitted.height())/2.0), fitted);
        painter.drawImage(target, pixels);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button()==Qt::LeftButton) {offset=event->globalPosition().toPoint()-window()->pos();setCursor(Qt::ClosedHandCursor);}
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons()&Qt::LeftButton) window()->move(event->globalPosition().toPoint()-offset);
    }
    void mouseReleaseEvent(QMouseEvent *) override {setCursor(Qt::OpenHandCursor);}
    void wheelEvent(QWheelEvent *event) override {
        const int delta=event->angleDelta().y()!=0?event->angleDelta().y():event->pixelDelta().y();
        if(delta!=0) zoomBy(delta>0?1.15:1.0/1.15);
        event->accept();
    }
private:
    void updateZoom() {setProperty("imageZoom",scale);if(zoomChanged)zoomChanged(qRound(scale*100));update();}
    QImage pixels;
    qreal scale=0;
    QPoint offset;
};

class ImageReferenceCard : public QWidget {
public:
    ImageReferenceCard(const QImage &image, const QString &name) : QWidget(nullptr) {
        setObjectName("pinnedImageCard");
        setProperty("floatingballSurface",true);
        setProperty("sourceName",name);
        setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("FloatingBall · 参考图 · ")+name);
        setMinimumSize(280,170);resize(360,290);
        FloatingWindow::setPanelStyleSheet(this,"QWidget#referenceFrame { background:@panelTint@; border:1px solid rgba(255,255,255,28); border-radius:12px; }"
            "QLabel { color:#a6adba; font-size:11px; } QToolButton { border:0; border-radius:6px; background:rgba(255,255,255,12); color:#edf1f7; padding:3px; }"
            "QToolButton:hover { background:rgba(255,255,255,22); } QSlider::groove:horizontal { height:4px; background:rgba(255,255,255,28); border-radius:2px; }"
            "QSlider::handle:horizontal { width:10px; margin:-3px 0; background:#75b5ff; border-radius:5px; }");
        auto outer=new QVBoxLayout(this);outer->setContentsMargins(2,2,2,2);auto frame=new QWidget;frame->setObjectName("referenceFrame");outer->addWidget(frame);
        auto opacityEffect=new QGraphicsOpacityEffect(frame);opacityEffect->setOpacity(1);frame->setGraphicsEffect(opacityEffect);
        auto layout=new QVBoxLayout(frame);layout->setContentsMargins(8,8,8,6);layout->setSpacing(6);
        auto handle=new DragHandle;handle->setCursor(Qt::OpenHandCursor);auto header=new QHBoxLayout(handle);header->setContentsMargins(0,0,0,0);header->setSpacing(4);
        auto title=new ElidedLabel;title->setText(name);title->setAttribute(Qt::WA_TransparentForMouseEvents);header->addWidget(title,1);
        auto makeButton=[header](const QString &text, FloatingIcons::Icon icon,const char *name){auto button=new QToolButton;button->setText(text);button->setToolTip(text);button->setAccessibleName(text);button->setObjectName(name);button->setIcon(FloatingIcons::make(icon,QColor("#dce3ed")));button->setFixedSize(26,26);header->addWidget(button);return button;};
        auto copy=makeButton(QStringLiteral("复制参考图"),FloatingIcons::Copy,"copyPinnedImage");
        auto close=makeButton(QStringLiteral("关闭参考图"),FloatingIcons::Close,"closePinnedImage");
        connect(copy,&QToolButton::clicked,this,[image,copy]{QString error;const bool success=ScreenshotStore::copyImage(image,&error);copy->setToolTip(success?QStringLiteral("✓ 参考图已复制"):error);});
        connect(close,&QToolButton::clicked,this,&QWidget::close);layout->addWidget(handle);
        auto canvas=new ReferenceCanvas(image);layout->addWidget(canvas,1);
        auto footer=new QHBoxLayout;footer->setSpacing(4);
        auto zoomOut=new QToolButton;zoomOut->setText(QStringLiteral("−"));zoomOut->setObjectName("zoomPinnedOut");zoomOut->setAccessibleName(QStringLiteral("缩小参考图"));zoomOut->setFixedSize(26,24);footer->addWidget(zoomOut);
        auto zoom=new QToolButton;zoom->setObjectName("fitPinnedImage");zoom->setToolTip(QStringLiteral("适应图卡大小"));zoom->setFixedSize(52,24);footer->addWidget(zoom);
        auto zoomIn=new QToolButton;zoomIn->setText(QStringLiteral("+"));zoomIn->setObjectName("zoomPinnedIn");zoomIn->setAccessibleName(QStringLiteral("放大参考图"));zoomIn->setFixedSize(26,24);footer->addWidget(zoomIn);
        connect(zoomOut,&QToolButton::clicked,canvas,[canvas]{canvas->zoomBy(1.0/1.15);});connect(zoomIn,&QToolButton::clicked,canvas,[canvas]{canvas->zoomBy(1.15);});connect(zoom,&QToolButton::clicked,canvas,[canvas]{canvas->fit();});
        canvas->zoomChanged=[zoom](int percent){zoom->setText(QString::number(percent)+QStringLiteral("%"));};
        footer->addStretch();auto opacity=new QSlider(Qt::Horizontal);opacity->setObjectName("pinnedImageOpacity");opacity->setRange(30,100);opacity->setValue(100);opacity->setFixedWidth(65);opacity->setAccessibleName(QStringLiteral("参考图透明度"));opacity->setToolTip(QStringLiteral("不透明度 100%"));footer->addWidget(opacity);
        connect(opacity,&QSlider::valueChanged,this,[this,opacity,opacityEffect](int value){opacityEffect->setOpacity(value/100.0);setProperty("referenceOpacity",value/100.0);opacity->setToolTip(QStringLiteral("不透明度 %1%").arg(value));});
        setProperty("referenceOpacity",1.0);
        footer->addWidget(new QSizeGrip(this));layout->addLayout(footer);
        FloatingWindow::prepare(this,false);
    }
};
}

ScreenshotCanvas::ScreenshotCanvas(QWidget *parent) : QWidget(parent) {
    setMinimumSize(300, 170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
}
void ScreenshotCanvas::setImage(const QImage &value) { image = value; history.clear(); undoDirtyStates.clear(); dirty=false; drawing = false;cancelImageAction();update();emit imageChanged(); }
bool ScreenshotCanvas::isCropping() const { return imageAction==CropImage; }
bool ScreenshotCanvas::isPickingColor() const { return imageAction==PickColor; }
QRect ScreenshotCanvas::cropSelection() const { return pendingCrop; }
void ScreenshotCanvas::beginCrop() {
    if(image.isNull()||drawing)return;
    editing=false;imageAction=CropImage;pendingCrop={};selectingCrop=false;
    setCursor(Qt::CrossCursor);setFocus();update();emit imageActionChanged();
}
void ScreenshotCanvas::beginColorPick() {
    if(image.isNull()||drawing)return;
    editing=false;imageAction=PickColor;pendingCrop={};selectingCrop=false;
    setCursor(Qt::CrossCursor);setFocus();update();emit imageActionChanged();
}
void ScreenshotCanvas::cancelImageAction() {
    imageAction=NoImageAction;pendingCrop={};selectingCrop=false;
    setCursor(editing?Qt::CrossCursor:Qt::ArrowCursor);update();emit imageActionChanged();
}
void ScreenshotCanvas::rememberImage() {
    history.append(image);undoDirtyStates.append(dirty);
    if(history.size()>12){history.removeFirst();undoDirtyStates.removeFirst();}
}
bool ScreenshotCanvas::replaceEditedImage(const QImage &value) {
    if(image.isNull()||value.isNull()||drawing||imageAction!=NoImageAction)return false;
    rememberImage();image=value;dirty=true;update();emit imageChanged();return true;
}
bool ScreenshotCanvas::applyCrop() {
    if(imageAction!=CropImage||selectingCrop||pendingCrop.width()<2||pendingCrop.height()<2)return false;
    const QImage cropped=image.copy(pendingCrop.intersected(image.rect()));
    cancelImageAction();return replaceEditedImage(cropped);
}
bool ScreenshotCanvas::rotateClockwise() {
    return replaceEditedImage(image.transformed(QTransform().rotate(90),Qt::FastTransformation));
}
bool ScreenshotCanvas::flipHorizontal() { return replaceEditedImage(image.transformed(QTransform().scale(-1,1),Qt::FastTransformation)); }
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
    p.setPen(Qt::NoPen); p.setBrush(QColor("#101318")); p.drawRoundedRect(rect(),12,12);
    if (!image.isNull()) {
        QPainterPath clip; clip.addRoundedRect(QRectF(imageRect()), 5, 5); p.setClipPath(clip);
        p.drawImage(imageRect(),image);
        if(imageAction==CropImage&&!pendingCrop.isEmpty()) {
            const QRect r=imageRect();
            const QRectF selected(r.x()+qreal(pendingCrop.x())*r.width()/image.width(),r.y()+qreal(pendingCrop.y())*r.height()/image.height(),qreal(pendingCrop.width())*r.width()/image.width(),qreal(pendingCrop.height())*r.height()/image.height());
            QPainterPath shade;shade.setFillRule(Qt::OddEvenFill);shade.addRect(r);shade.addRect(selected);
            p.fillPath(shade,QColor(12,35,50,110));p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#168ed1"),2,Qt::DashLine));p.drawRect(selected);
        }
    } else {
        FloatingIcons::draw(p, FloatingIcons::Screenshot, QRectF(width()/2.0-16, height()/2.0-42, 32, 32), QColor("#a6adba"));
        p.setPen(QColor("#a6adba"));
        p.drawText(rect().adjusted(0,35,0,0),Qt::AlignCenter,QStringLiteral("还没有截图\n选择目录，或从剪贴板导入"));
    }
}
void ScreenshotCanvas::mousePressEvent(QMouseEvent *e) {
    if(!image.isNull()&&e->button()==Qt::LeftButton&&imageRect().contains(e->pos())) {
        if(imageAction==CropImage) {
            cropStart=imagePoint(e->pos());pendingCrop=QRect(cropStart,QSize(1,1));selectingCrop=true;update();emit imageActionChanged();return;
        }
        if(imageAction==PickColor) {const QPoint pixel=imagePoint(e->pos());emit colorPicked(image.pixelColor(pixel),pixel);return;}
    }
    if (!editing || image.isNull() || e->button()!=Qt::LeftButton || !imageRect().contains(e->pos())) return;
    const bool wasDirty=dirty;
    history.append(image);undoDirtyStates.append(wasDirty);dirty=true;drawing=true;first=last=imagePoint(e->pos());
    markerPath=QPainterPath();markerPath.moveTo(first);
    if(tool==Text) {
        bool accepted=false;const QString text=QInputDialog::getText(this,QStringLiteral("文字批注"),QStringLiteral("文字："),QLineEdit::Normal,{},&accepted);
        drawing=false;
        if(accepted && !text.isEmpty()) {QPainter p(&image);p.setPen(ink);QFont f=p.font();f.setPixelSize(qMax(16,strokeWidth*5));p.setFont(f);p.drawText(first,text);update();emit imageChanged();}
        else {history.removeLast();undoDirtyStates.removeLast();dirty=wasDirty;}
    }
    if(history.size()>12){history.removeFirst();undoDirtyStates.removeFirst();}
}
void ScreenshotCanvas::mouseMoveEvent(QMouseEvent *e) {
    if(selectingCrop) {
        const QPoint next=imagePoint(e->pos());
        // Construct positive inclusive pixel bounds. QRect::normalized() on
        // reversed point endpoints otherwise excludes both boundary pixels.
        pendingCrop=QRect(QPoint(qMin(cropStart.x(),next.x()),qMin(cropStart.y(),next.y())),QSize(qAbs(next.x()-cropStart.x())+1,qAbs(next.y()-cropStart.y())+1)).intersected(image.rect());
        update();emit imageActionChanged();return;
    }
    if(imageAction==PickColor&&(e->buttons()&Qt::LeftButton)&&imageRect().contains(e->pos())) {const QPoint pixel=imagePoint(e->pos());emit colorPicked(image.pixelColor(pixel),pixel);return;}
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
void ScreenshotCanvas::mouseReleaseEvent(QMouseEvent *e) {
    if(selectingCrop&&e->button()==Qt::LeftButton){mouseMoveEvent(e);selectingCrop=false;emit imageActionChanged();return;}
    if(drawing){mouseMoveEvent(e);emit imageChanged();}drawing=false;
}
void ScreenshotCanvas::keyPressEvent(QKeyEvent *e) {
    if(e->key()==Qt::Key_Escape&&imageAction!=NoImageAction){cancelImageAction();e->accept();return;}
    QWidget::keyPressEvent(e);
}
void ScreenshotCanvas::undo() { if(imageAction!=NoImageAction){cancelImageAction();return;}drawing=false;if (!history.isEmpty()) { image=history.takeLast();dirty=undoDirtyStates.takeLast();update();emit imageChanged(); } }

ScreenshotPanel::ScreenshotPanel(QWidget *parent, const QString &directoryOverride) : QWidget(parent) {
    setProperty("floatingballSurface",true);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("FloatingBall · 结果工作台"));
    resize(550,500);
    auto outer=new QVBoxLayout(this); outer->setContentsMargins(14,12,14,20);
    auto card=new QWidget; card->setObjectName("card"); outer->addWidget(card);
    FloatingWindow::setPanelStyleSheet(card,
        "QWidget#card { background:@panelTint@; border:1px solid rgba(255,255,255,28); border-radius:18px; }"
        "QLabel { color:#a6adba; font-size:11px; } QLabel#title { color:#edf1f7; font-size:16px; font-weight:600; }"
        "QToolButton { background:rgba(255,255,255,12); color:#edf1f7; border:0; border-radius:9px; padding:0 9px; font-size:12px; }"
        "QToolButton:hover { background:rgba(255,255,255,22); } QToolButton:pressed, QToolButton:checked { background:rgba(255,255,255,32); }"
        "QToolButton#copyImage, QToolButton#copyText { background:#277ac7; color:white; } QToolButton#copyImage:hover, QToolButton#copyText:hover { background:#348cdf; }"
        "QToolButton#headerButton { background:transparent; padding:0; } QToolButton#headerButton:hover { background:rgba(255,255,255,18); }"
        "QToolButton#headerButton:checked { background:rgba(80,150,235,36); } QToolButton:disabled { color:#68707d; }"
        "QComboBox, QSpinBox { background:rgba(255,255,255,12); color:#edf1f7; border:0; border-radius:9px; padding:6px 9px; font-size:12px; }"
        "QComboBox::drop-down { border:0; width:18px; } QComboBox QAbstractItemView { background:#141922; color:#edf1f7; selection-background-color:#223047; selection-color:#edf1f7; }"
        "QSpinBox::up-button, QSpinBox::down-button { width:16px; border:0; } QCheckBox { color:#dce3ed; font-size:11px; spacing:4px; }"
        "QTabWidget::pane { border:0; } QTabBar::tab { background:transparent; color:#a6adba; padding:7px 20px; border-bottom:2px solid transparent; }"
        "QTabBar::tab:selected { color:#75b5ff; border-bottom:2px solid #75b5ff; }"
        "QTextEdit { border:1px solid rgba(255,255,255,24); border-radius:12px; background:#14181f; color:#edf1f7; font-size:14px; padding:10px; selection-background-color:#223d60; selection-color:#edf1f7; }"
        "QListWidget { background:transparent; border:0; outline:0; }");
    auto shadow=new QGraphicsDropShadowEffect(card); shadow->setBlurRadius(22); shadow->setOffset(0,5); shadow->setColor(QColor(0,0,0,80)); card->setGraphicsEffect(shadow);
    auto layout=new QVBoxLayout(card); layout->setContentsMargins(14,12,14,12); layout->setSpacing(8);
    auto header=new DragHandle; header->setCursor(Qt::OpenHandCursor); auto title=new QHBoxLayout(header); title->setContentsMargins(0,0,0,0); title->setSpacing(4);
    auto heading=new QVBoxLayout; heading->setSpacing(2);
    auto titleLabel=new QLabel(QStringLiteral("结果工作台")); titleLabel->setObjectName("title"); titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(titleLabel);
    details=new ElidedLabel; details->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(details); title->addLayout(heading,1);
    auto button=[this](const QString &text, FloatingIcons::Icon icon, bool iconOnly=false) {
        auto b=new QToolButton(this); b->setText(text); b->setAccessibleName(text); b->setToolTip(text);
        b->setIcon(FloatingIcons::make(icon,QColor("#dce3ed"))); b->setIconSize(QSize(17,17)); b->setFixedHeight(32); b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(iconOnly?Qt::ToolButtonIconOnly:Qt::ToolButtonTextBesideIcon);
        if(iconOnly) b->setFixedWidth(32); return b;
    };
    auto take=button(QStringLiteral("截取区域"),FloatingIcons::Capture,true);take->setObjectName("captureRegion");title->addWidget(take);
    auto pin=button(QStringLiteral("固定"),FloatingIcons::Pin,true); pin->setObjectName("headerButton"); pin->setCheckable(true); pin->setChecked(true); title->addWidget(pin);
    auto updatePinHint=[pin](bool fixed){pin->setToolTip(fixed?QStringLiteral("已固定置顶 · 点击后切换应用自动收起"):QStringLiteral("点击固定置顶 · 切换应用时保持展示"));};
    connect(pin,&QToolButton::toggled,this,updatePinHint); updatePinHint(true);
    auto close=button(QStringLiteral("收起 ×"),FloatingIcons::Close,true); close->setObjectName("headerButton"); title->addWidget(close); layout->addWidget(header);
    connect(close,&QToolButton::clicked,this,&QWidget::hide);
    auto optionsButton=button(QStringLiteral("截图选项"),FloatingIcons::Capture);optionsButton->setObjectName("captureSettings");optionsButton->setCheckable(true);optionsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    optionsButton->setToolTip(QStringLiteral("展开截图范围、延时、光标与录屏选项"));
    auto optionsSummary=new QHBoxLayout;optionsSummary->addWidget(optionsButton);auto captureHint=new QLabel(QStringLiteral("区域截图 · 无延时"));captureHint->setObjectName("captureSummary");optionsSummary->addWidget(captureHint,1);layout->addLayout(optionsSummary);
    auto captureOptions=new QWidget;captureOptions->setObjectName("captureOptions");auto captureRow=new QHBoxLayout(captureOptions);captureRow->setContentsMargins(0,0,0,0);captureRow->setSpacing(6);
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
    auto shoot=button(QStringLiteral("截图"),FloatingIcons::Capture);shoot->setObjectName("takeScreenshot");captureRow->addWidget(shoot);layout->addWidget(captureOptions);captureOptions->hide();
    connect(optionsButton,&QToolButton::toggled,captureOptions,&QWidget::setVisible);
    auto updateCaptureHint=[captureHint,mode,delay,cursor]{captureHint->setText(mode->currentText()+QStringLiteral("截图 · ")+(delay->value()>0?QStringLiteral("%1 秒后").arg(delay->value()):QStringLiteral("无延时"))+(cursor->isChecked()?QStringLiteral(" · 含光标"):QString()));};
    connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),this,[updateCaptureHint](int){updateCaptureHint();});connect(delay,qOverload<int>(&QSpinBox::valueChanged),this,[updateCaptureHint](int){updateCaptureHint();});connect(cursor,&QCheckBox::toggled,this,[updateCaptureHint](bool){updateCaptureHint();});
    auto takeConfigured=[this,mode,delay,cursor]{CaptureOptions options;options.mode=CaptureOptions::Mode(mode->currentIndex());options.delaySeconds=delay->value();options.includeCursor=cursor->isChecked();takeScreenshot(geometry().center(),options);};
    connect(take,&QToolButton::clicked,this,takeConfigured);connect(shoot,&QToolButton::clicked,this,takeConfigured);
    auto record=button(QStringLiteral("录屏"),FloatingIcons::Capture);record->setObjectName("recordScreen");captureRow->addWidget(record);
    auto recordMenu=new QMenu(record);recordMenu->addAction(QStringLiteral("录制屏幕…"),this,[this]{startRecording(false);});
    auto recordWindow=recordMenu->addAction(QStringLiteral("录制窗口…"),this,[this]{startRecording(true);});
    if(QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))){recordWindow->setEnabled(false);recordWindow->setToolTip(QStringLiteral("当前 Wayland 后端支持屏幕录制，区域由系统选择器提供"));}
    record->setMenu(recordMenu);record->setPopupMode(QToolButton::MenuButtonPopup);
    connect(record,&QToolButton::clicked,this,[this]{startRecording(false);});
    auto newShortcut=new QShortcut(QKeySequence::New,this);connect(newShortcut,&QShortcut::activated,shoot,&QToolButton::click);
    resultTabs=new QTabWidget;resultTabs->setObjectName("resultTabs");layout->addWidget(resultTabs,1);
    auto imageResult=new QWidget;auto imageLayout=new QVBoxLayout(imageResult);imageLayout->setContentsMargins(0,6,0,0);imageLayout->setSpacing(8);resultTabs->addTab(imageResult,QStringLiteral("图片"));
    canvas=new ScreenshotCanvas;canvas->setObjectName("resultImage"); imageLayout->addWidget(canvas,1);
    thumbnails=new ThumbnailList; thumbnails->setViewMode(QListView::IconMode); thumbnails->setFlow(QListView::LeftToRight);
    thumbnails->setWrapping(false); thumbnails->setIconSize(QSize(64,44)); thumbnails->setFixedHeight(60);
    thumbnails->setSpacing(0); thumbnails->setGridSize(QSize(76,58)); thumbnails->setItemDelegate(new ThumbnailDelegate(thumbnails));
    thumbnails->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    thumbnails->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); thumbnails->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); imageLayout->addWidget(thumbnails);
    connect(thumbnails,&QListWidget::currentRowChanged,this,[this](int row){select(row);});
    auto actions=new QHBoxLayout; actions->setSpacing(6);
    auto copy=button(QStringLiteral("复制图片"),FloatingIcons::Copy); copy->setObjectName("copyImage"); copy->setIcon(FloatingIcons::make(FloatingIcons::Copy,Qt::white));
    auto recognize=button(QStringLiteral("识别文字"),FloatingIcons::Screenshot);recognize->setObjectName("recognizeText");
    auto reference=button(QStringLiteral("置顶参考"),FloatingIcons::Pin);reference->setObjectName("pinImage");reference->setToolTip(QStringLiteral("创建独立参考图卡 · 拖动、缩放与透明度"));
    actions->addWidget(copy);actions->addWidget(recognize);actions->addWidget(reference);actions->addStretch();imageLayout->addLayout(actions);
    connect(recognize,&QToolButton::clicked,this,[this]{recognizeText();});connect(reference,&QToolButton::clicked,this,[this]{pinCurrentImage();});
    auto secondaryActions=new QHBoxLayout;secondaryActions->setSpacing(6);
    auto path=button(QStringLiteral("复制路径"),FloatingIcons::Path); path->setObjectName("copyPath");
    edit=button(QStringLiteral("画笔批注"),FloatingIcons::Pen,true); edit->setObjectName("annotate"); edit->setCheckable(true);
    auto undo=button(QStringLiteral("撤销"),FloatingIcons::Undo,true); undo->setObjectName("undo");
    auto save=button(QStringLiteral("另存副本"),FloatingIcons::Save,true); save->setObjectName("saveCopy");
    for(auto b:{path,edit,undo,save}) secondaryActions->addWidget(b);secondaryActions->addStretch();auto editingHint=new QLabel(QStringLiteral("编辑保存为副本"));secondaryActions->addWidget(editingHint); imageLayout->addLayout(secondaryActions);
    connect(copy,&QToolButton::clicked,this,[this]{if(!canvas->image.isNull()){QString error;const bool copied=ScreenshotStore::copyImage(canvas->image,&error);status->setText(copied?QStringLiteral("✓ 图片已复制"):error);if(copied){canvas->dirty=false;transientImage=false;}}});
    connect(path,&QToolButton::clicked,this,[this]{if(QFileInfo::exists(currentPath)){ QApplication::clipboard()->setText(currentPath); status->setText(QStringLiteral("✓ 原图路径已复制")); }});
    connect(edit,&QToolButton::toggled,this,[this](bool enabled){if(enabled)canvas->cancelImageAction();canvas->editing=enabled;canvas->setCursor(enabled?Qt::CrossCursor:Qt::ArrowCursor);});
    auto imageTools=new QHBoxLayout;imageTools->setSpacing(6);
    auto crop=button(QStringLiteral("裁剪"),FloatingIcons::Capture);crop->setObjectName("cropImage");imageTools->addWidget(crop);
    auto rotate=button(QStringLiteral("旋转 90°"),FloatingIcons::Undo);rotate->setObjectName("rotateImage");imageTools->addWidget(rotate);
    auto flip=button(QStringLiteral("水平翻转"),FloatingIcons::Screenshot);flip->setObjectName("flipImage");imageTools->addWidget(flip);
    auto pickColor=button(QStringLiteral("取色"),FloatingIcons::Pen);pickColor->setObjectName("pickColor");pickColor->setCheckable(true);imageTools->addWidget(pickColor);imageTools->addStretch();imageLayout->addLayout(imageTools);
    auto cropControls=new QWidget;cropControls->setObjectName("cropControls");auto cropRow=new QHBoxLayout(cropControls);cropRow->setContentsMargins(0,0,0,0);cropRow->setSpacing(6);
    auto cropHint=new QLabel(QStringLiteral("拖动框选要保留的区域"));cropHint->setObjectName("cropSelectionDetails");cropRow->addWidget(cropHint,1);
    auto applyCrop=button(QStringLiteral("应用裁剪"),FloatingIcons::Capture);applyCrop->setObjectName("applyCrop");cropRow->addWidget(applyCrop);
    auto cancelImageAction=button(QStringLiteral("取消"),FloatingIcons::Close);cancelImageAction->setObjectName("cancelImageAction");cropRow->addWidget(cancelImageAction);imageLayout->addWidget(cropControls);cropControls->hide();
    auto colorControls=new QWidget;colorControls->setObjectName("colorControls");auto colorRow=new QHBoxLayout(colorControls);colorRow->setContentsMargins(0,0,0,0);colorRow->setSpacing(6);
    auto colorDetails=new QLabel(QStringLiteral("点击图片中的像素"));colorDetails->setObjectName("sampleColorDetails");colorDetails->setWordWrap(true);colorRow->addWidget(colorDetails,1);
    auto copyColor=button(QStringLiteral("复制 HEX"),FloatingIcons::Copy);copyColor->setObjectName("copyColor");copyColor->setEnabled(false);colorRow->addWidget(copyColor);
    auto copyRgb=button(QStringLiteral("复制 RGB"),FloatingIcons::Copy);copyRgb->setObjectName("copyColorRgb");copyRgb->setEnabled(false);colorRow->addWidget(copyRgb);imageLayout->addWidget(colorControls);colorControls->hide();
    auto sampledColor=std::make_shared<QColor>();
    connect(crop,&QToolButton::clicked,this,[this]{edit->setChecked(false);canvas->beginCrop();status->setText(QStringLiteral("拖动框选区域 · 应用后仍可撤销，原图保留"));});
    connect(applyCrop,&QToolButton::clicked,this,[this]{if(canvas->applyCrop())status->setText(QStringLiteral("✓ 已裁剪 · 复制或另存副本保留修改"));});
    connect(cancelImageAction,&QToolButton::clicked,this,[this]{canvas->cancelImageAction();status->setText(QStringLiteral("已取消裁剪 · 图片与批注保留"));});
    connect(rotate,&QToolButton::clicked,this,[this]{edit->setChecked(false);if(canvas->rotateClockwise())status->setText(QStringLiteral("✓ 已顺时针旋转 · 可撤销"));});
    connect(flip,&QToolButton::clicked,this,[this]{edit->setChecked(false);if(canvas->flipHorizontal())status->setText(QStringLiteral("✓ 已水平翻转 · 可撤销"));});
    connect(pickColor,&QToolButton::toggled,this,[this](bool enabled){if(enabled){edit->setChecked(false);canvas->beginColorPick();status->setText(QStringLiteral("点击图片取色 · 再次点击取色或 Escape 结束"));}else if(canvas->isPickingColor())canvas->cancelImageAction();});
    connect(canvas,&ScreenshotCanvas::imageActionChanged,this,[this,cropControls,cropHint,applyCrop,pickColor,colorControls]{
        const bool cropping=canvas->isCropping();cropControls->setVisible(cropping);
        const QRect selection=canvas->cropSelection();applyCrop->setEnabled(cropping&&selection.width()>1&&selection.height()>1);
        cropHint->setText(selection.isEmpty()?QStringLiteral("拖动框选要保留的区域"):QStringLiteral("%1 × %2 px · 原图保留").arg(selection.width()).arg(selection.height()));
        const QSignalBlocker blocker(pickColor);pickColor->setChecked(canvas->isPickingColor());colorControls->setVisible(canvas->isPickingColor());updateImageActions();
    });
    connect(canvas,&ScreenshotCanvas::colorPicked,this,[sampledColor,colorDetails,copyColor,copyRgb](const QColor &color,const QPoint &pixel){
        *sampledColor=color;colorDetails->setText(QStringLiteral("%1 · RGB(%2, %3, %4)\n像素 (%5, %6)").arg(color.name(QColor::HexRgb).toUpper()).arg(color.red()).arg(color.green()).arg(color.blue()).arg(pixel.x()).arg(pixel.y()));copyColor->setEnabled(true);copyRgb->setEnabled(true);
    });
    connect(copyColor,&QToolButton::clicked,this,[this,sampledColor]{if(sampledColor->isValid()){QApplication::clipboard()->setText(sampledColor->name(QColor::HexRgb).toUpper());status->setText(QStringLiteral("✓ HEX 已复制"));}});
    connect(copyRgb,&QToolButton::clicked,this,[this,sampledColor]{if(sampledColor->isValid()){QApplication::clipboard()->setText(QStringLiteral("rgb(%1, %2, %3)").arg(sampledColor->red()).arg(sampledColor->green()).arg(sampledColor->blue()));status->setText(QStringLiteral("✓ RGB 已复制"));}});
    connect(canvas,&ScreenshotCanvas::imageChanged,this,[sampledColor,colorDetails,copyColor,copyRgb]{*sampledColor=QColor();colorDetails->setText(QStringLiteral("点击图片中的像素"));copyColor->setEnabled(false);copyRgb->setEnabled(false);});
    auto tools=new QWidget;tools->setObjectName("annotationTools");auto toolRow=new QHBoxLayout(tools);toolRow->setContentsMargins(0,0,0,0);toolRow->setSpacing(6);
    auto tool=new QComboBox;tool->setObjectName("annotationTool");tool->setAccessibleName(QStringLiteral("批注工具"));tool->addItems({QStringLiteral("画笔"),QStringLiteral("箭头"),QStringLiteral("矩形"),QStringLiteral("荧光笔"),QStringLiteral("马赛克"),QStringLiteral("文字")});toolRow->addWidget(tool,1);
    auto color=new QComboBox;color->setObjectName("annotationColor");color->setAccessibleName(QStringLiteral("批注颜色"));
    for(const auto &item:QList<QPair<QString,QString>>{{QStringLiteral("粉红"),"#ff637d"},{QStringLiteral("黄色"),"#ffd84d"},{QStringLiteral("蓝色"),"#55aaff"},{QStringLiteral("白色"),"#ffffff"},{QStringLiteral("黑色"),"#222222"}})color->addItem(item.first,item.second);toolRow->addWidget(color);
    auto thickness=new QSpinBox;thickness->setObjectName("annotationWidth");thickness->setRange(2,20);thickness->setValue(4);thickness->setSuffix(QStringLiteral(" px"));thickness->setAccessibleName(QStringLiteral("线条粗细"));toolRow->addWidget(thickness);
    connect(tool,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int value){canvas->tool=ScreenshotCanvas::Tool(value);});
    connect(color,qOverload<int>(&QComboBox::currentIndexChanged),this,[this,color]{canvas->ink=QColor(color->currentData().toString());});
    connect(thickness,qOverload<int>(&QSpinBox::valueChanged),this,[this](int value){canvas->strokeWidth=value;});
    tools->hide();connect(edit,&QToolButton::toggled,tools,&QWidget::setVisible);imageLayout->addWidget(tools);
    connect(undo,&QToolButton::clicked,canvas,&ScreenshotCanvas::undo);
    auto copyShortcut=new QShortcut(QKeySequence::Copy,this); connect(copyShortcut,&QShortcut::activated,this,[this,copy]{if(resultTabs->currentIndex()==1)findChild<QToolButton *>("copyText")->click();else copy->click();});
    auto undoShortcut=new QShortcut(QKeySequence::Undo,this); connect(undoShortcut,&QShortcut::activated,this,[this,undo]{if(resultTabs->currentIndex()==1)recognizedText->undo();else undo->click();});
    connect(save,&QToolButton::clicked,this,[this]{
        if(canvas->image.isNull()) return;
        const QString saveDirectory=currentPath.isEmpty()?directory:QFileInfo(currentPath).absolutePath();
        QString target=QFileDialog::getSaveFileName(this,QStringLiteral("保存图像副本"),QDir(saveDirectory).filePath("Edited-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz")+".png"), "PNG (*.png)");
        if(target.isEmpty()) return;
        if(QFileInfo(target).absoluteFilePath()==QFileInfo(currentPath).absoluteFilePath() || (!QFileInfo(target).canonicalFilePath().isEmpty() && QFileInfo(target).canonicalFilePath()==QFileInfo(currentPath).canonicalFilePath())){status->setText(QStringLiteral("请选择新文件名，保留原截图"));return;}
        QSaveFile file(target);const bool saved=file.open(QIODevice::WriteOnly)&&canvas->image.save(&file,"PNG")&&file.commit();
        status->setText(saved?QStringLiteral("✓ 副本已保存"):QStringLiteral("保存失败，请检查目标目录"));
        if(saved){canvas->dirty=false;transientImage=false;updateImageActions();}
    });
    auto textResult=new QWidget;auto textLayout=new QVBoxLayout(textResult);textLayout->setContentsMargins(0,6,0,0);textLayout->setSpacing(8);resultTabs->addTab(textResult,QStringLiteral("文字"));
    auto textHeader=new QHBoxLayout;auto recognizeAgain=button(QStringLiteral("识别当前图片"),FloatingIcons::Screenshot);recognizeAgain->setObjectName("recognizeTextAgain");textHeader->addWidget(recognizeAgain);textHeader->addStretch();
    auto cancelRecognition=button(QStringLiteral("取消识别"),FloatingIcons::Close);cancelRecognition->setObjectName("cancelRecognition");textHeader->insertWidget(1,cancelRecognition);cancelRecognition->hide();
    connect(cancelRecognition,&QToolButton::clicked,this,[this]{if(textWorker){textWorker->setProperty("cancelledByUser",true);textWorker->requestInterruption();status->setText(QStringLiteral("正在取消文字识别…"));textDetails->setText(QStringLiteral("正在取消文字识别…"));}});
    auto selectedHint=new QLabel(QStringLiteral("选中文字可只处理选中部分"));textHeader->addWidget(selectedHint);textLayout->addLayout(textHeader);
    connect(recognizeAgain,&QToolButton::clicked,this,[this]{recognizeText();});
    recognizedText=new QTextEdit;recognizedText->setObjectName("recognizedText");recognizedText->setAcceptRichText(false);recognizedText->setPlaceholderText(QStringLiteral("识别图片中的文字，或直接粘贴文字。\n可以修改识别结果后再复制、搜索或翻译。"));textLayout->addWidget(recognizedText,1);
    textDetails=new QLabel(QStringLiteral("图片文字在本机识别；搜索和翻译由浏览器打开。"));textDetails->setObjectName("textResultDetails");textDetails->setWordWrap(true);textLayout->addWidget(textDetails);
    auto textActions=new QHBoxLayout;textActions->setSpacing(6);
    auto copyText=button(QStringLiteral("复制文字"),FloatingIcons::Copy);copyText->setObjectName("copyText");copyText->setIcon(FloatingIcons::make(FloatingIcons::Copy,Qt::white));textActions->addWidget(copyText);
    auto saveTextNote=button(QStringLiteral("存为笔记"),FloatingIcons::Save);saveTextNote->setObjectName("saveTextNote");textActions->addWidget(saveTextNote);
    auto searchText=button(QStringLiteral("搜索"),FloatingIcons::Browser);searchText->setObjectName("searchText");searchText->setToolTip(QStringLiteral("在浏览器中搜索所选文字或全文"));textActions->addWidget(searchText);
    auto translateText=button(QStringLiteral("网页翻译"),FloatingIcons::Browser);translateText->setObjectName("translateText");translateText->setToolTip(QStringLiteral("将所选文字或全文交给浏览器中的 Google 翻译"));textActions->addWidget(translateText);textActions->addStretch();textLayout->addLayout(textActions);
    auto providers=new QHBoxLayout;providers->setSpacing(6);providers->addWidget(new QLabel(QStringLiteral("搜索")));
    auto searchProvider=new QComboBox;searchProvider->setObjectName("textSearchProvider");searchProvider->addItem(QStringLiteral("Google"),QStringLiteral("https://www.google.com/search"));searchProvider->addItem(QStringLiteral("DuckDuckGo"),QStringLiteral("https://duckduckgo.com/"));searchProvider->addItem(QStringLiteral("Bing"),QStringLiteral("https://www.bing.com/search"));providers->addWidget(searchProvider);
    providers->addSpacing(8);providers->addWidget(new QLabel(QStringLiteral("翻译为")));auto translationLanguage=new QComboBox;translationLanguage->setObjectName("translationLanguage");translationLanguage->addItem(QStringLiteral("简体中文"),QStringLiteral("zh-CN"));translationLanguage->addItem(QStringLiteral("English"),QStringLiteral("en"));translationLanguage->addItem(QStringLiteral("日本語"),QStringLiteral("ja"));providers->addWidget(translationLanguage);providers->addStretch();textLayout->addLayout(providers);
    auto updateTextActions=[this,copyText,saveTextNote,searchText,translateText]{const bool content=!recognizedText->toPlainText().trimmed().isEmpty();copyText->setEnabled(content);saveTextNote->setEnabled(content);searchText->setEnabled(content);translateText->setEnabled(content);};
    connect(recognizedText,&QTextEdit::textChanged,this,updateTextActions);updateTextActions();
    connect(copyText,&QToolButton::clicked,this,[this]{const QString text=actionText();if(text.trimmed().isEmpty())return;QApplication::clipboard()->setText(text);if(text==recognizedText->toPlainText())recognizedText->document()->setModified(false);status->setText(QStringLiteral("✓ 文字已复制"));});
    connect(saveTextNote,&QToolButton::clicked,this,[this]{const QString text=actionText();if(text.trimmed().isEmpty())return;emit textNoteRequested(text,currentPath.isEmpty()?QStringLiteral("识别文字"):QFileInfo(currentPath).completeBaseName());});
    connect(searchText,&QToolButton::clicked,this,[this,searchProvider]{const QString text=actionText();if(text.isEmpty())return;if(text.size()>2000){status->setText(QStringLiteral("搜索文字较长，请先选中要搜索的部分"));return;}QUrl url(searchProvider->currentData().toString());QUrlQuery query;query.addQueryItem(QStringLiteral("q"),text);url.setQuery(query);status->setText(QDesktopServices::openUrl(url)?QStringLiteral("✓ 已在浏览器打开搜索"):QStringLiteral("无法打开浏览器，请检查默认浏览器设置"));});
    connect(translateText,&QToolButton::clicked,this,[this,translationLanguage]{const QString text=actionText();if(text.isEmpty())return;if(text.size()>5000){status->setText(QStringLiteral("翻译文字较长，请先选中要翻译的部分"));return;}QUrl url(QStringLiteral("https://translate.google.com/"));QUrlQuery query;query.addQueryItem(QStringLiteral("sl"),QStringLiteral("auto"));query.addQueryItem(QStringLiteral("tl"),translationLanguage->currentData().toString());query.addQueryItem(QStringLiteral("text"),text);query.addQueryItem(QStringLiteral("op"),QStringLiteral("translate"));url.setQuery(query);status->setText(QDesktopServices::openUrl(url)?QStringLiteral("✓ 已在浏览器打开翻译"):QStringLiteral("无法打开浏览器，请检查默认浏览器设置"));});
    connect(canvas,&ScreenshotCanvas::imageChanged,this,[this]{updateImageActions();if(!recognizedText->toPlainText().isEmpty()&&recognizedImageKey!=canvas->image.cacheKey())textDetails->setText(QStringLiteral("已保留上一份文字结果 · 点击“识别当前图片”可更新"));});
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
            FloatingWindow::setPanelStyleSheet(captureProgress,"QWidget { background:@panelTint@; color:#edf1f7; border-radius:12px; padding:8px; } QLabel { background:transparent; } QToolButton { background:rgba(255,255,255,18); } QToolButton:hover { background:rgba(255,255,255,28); }");
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
    if(textWorker){textWorker->requestInterruption();textWorker->wait();delete textWorker.data();textWorker=nullptr;}
    capture->cancel();
    recorder->stop();
    if(captureProgress)delete captureProgress;
    for(const auto &card:referenceCards)if(card)delete card;
}
void ScreenshotPanel::updateImageActions() {
    const bool available=!canvas->image.isNull();
    for(const char *name:{"copyImage","annotate","undo","saveCopy","pinImage"})
        if(auto button=findChild<QToolButton *>(name))button->setEnabled(available&&!canvas->isCropping());
    for(const char *name:{"cropImage","rotateImage","flipImage"})
        if(auto button=findChild<QToolButton *>(name))button->setEnabled(available&&!canvas->isCropping()&&!canvas->isPickingColor());
    if(auto pick=findChild<QToolButton *>("pickColor"))pick->setEnabled(available&&!canvas->isCropping());
    if(auto undo=findChild<QToolButton *>("undo"))undo->setEnabled(available&&!canvas->isCropping()&&!canvas->history.isEmpty());
    if(auto path=findChild<QToolButton *>("copyPath"))path->setEnabled(available&&QFileInfo::exists(currentPath));
    for(const char *name:{"recognizeText","recognizeTextAgain"})
        if(auto button=findChild<QToolButton *>(name))button->setEnabled(available&&!textWorker&&!canvas->isCropping());
    if(auto cancel=findChild<QToolButton *>("cancelRecognition")){cancel->setVisible(bool(textWorker));cancel->setEnabled(bool(textWorker));}
}
QImage ScreenshotPanel::currentImage() const { return canvas->image; }
QString ScreenshotPanel::actionText() const {
    const auto cursor=recognizedText->textCursor();
    return cursor.hasSelection()?cursor.selectedText().replace(QChar::ParagraphSeparator,'\n'):recognizedText->toPlainText();
}
void ScreenshotPanel::recognizeText() {
    if(canvas->image.isNull()||textWorker)return;
    if(!recognizedText->toPlainText().isEmpty()&&recognizedText->document()->isModified()) {
        if(QMessageBox::question(this,QStringLiteral("更新文字结果"),QStringLiteral("当前文字包含尚未复制的修改。重新识别将替换它，是否继续？"),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    }
    const QImage pixels=canvas->image;
    const qint64 sourceKey=pixels.cacheKey();
    auto result=std::make_shared<ScreenText::Result>();
    auto worker=QThread::create([pixels,result]{*result=ScreenText::recognize(pixels);});
    worker->setParent(this);worker->setObjectName("textRecognitionWorker");
    textWorker=worker;recognizedText->setReadOnly(true);updateImageActions();
    resultTabs->setCurrentIndex(1);textDetails->setText(QStringLiteral("正在本机识别文字…"));status->setText(QStringLiteral("识别中 · 图片不会上传"));
    connect(worker,&QThread::finished,this,[this,result,sourceKey,worker]{
        textWorker=nullptr;recognizedText->setReadOnly(false);updateImageActions();
        // A finished worker ignores requestInterruption. Keep the GUI's cancel
        // intent until its queued completion has been handled as well.
        if(worker->property("cancelledByUser").toBool()||worker->isInterruptionRequested()) {
            textDetails->setText(QStringLiteral("文字识别已取消"));status->setText(QStringLiteral("文字识别已取消"));return;
        }
        if(sourceKey!=canvas->image.cacheKey()) {
            textDetails->setText(QStringLiteral("识别期间图片已改变 · 请重新识别当前图片"));status->setText(QStringLiteral("图片已改变，旧识别结果未替换文字"));return;
        }
        if(!result->error.isEmpty()) {textDetails->setText(result->error);status->setText(result->error);return;}
        recognizedImageKey=sourceKey;recognizedText->setPlainText(result->text);recognizedText->document()->setModified(false);
        textDetails->setText(result->provider+QStringLiteral(" · 可直接编辑识别结果"));status->setText(QStringLiteral("✓ 已识别 %1 个字符").arg(result->text.size()));
    });
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);
    connect(qApp,&QCoreApplication::aboutToQuit,worker,[worker]{worker->requestInterruption();worker->wait();});
    worker->start();
}
void ScreenshotPanel::pinCurrentImage() {
    if(canvas->image.isNull())return;
    const QString name=currentPath.isEmpty()?QStringLiteral("当前图片"):QFileInfo(currentPath).fileName();
    pinImageDirect(canvas->image,name,pos()+QPoint(width()-80,40));
    status->setText(QStringLiteral("✓ 参考图已置顶 · 拖动移动，滚轮缩放"));
}
QWidget *ScreenshotPanel::pinImageDirect(const QImage &image, const QString &name, const QPoint &anchor) {
    if(image.isNull())return nullptr;
    for(int index=referenceCards.size()-1;index>=0;--index)if(!referenceCards[index])referenceCards.removeAt(index);
    auto card=new ImageReferenceCard(image,name.isEmpty()?QStringLiteral("置顶贴图"):name);
    referenceCards.append(card);
    QScreen *screen=QGuiApplication::screenAt(anchor);if(!screen)screen=QGuiApplication::primaryScreen();
    if(screen){
        const QRect bounds=screen->availableGeometry();
        const QPoint offset(24*(referenceCards.size()%5),24*(referenceCards.size()%5));
        const QPoint target=anchor+offset;
        card->move(qBound(bounds.left(),target.x(),bounds.right()-card->width()+1),
                   qBound(bounds.top(),target.y(),bounds.bottom()-card->height()+1));
    }
    card->show();card->raise();
    return card;
}
void ScreenshotPanel::openForAnnotation(const QImage &image, const QString &savedPath, const QPoint &anchor) {
    if(image.isNull())return;
    QString path=savedPath;
    if(path.isEmpty()){QString err;path=ScreenshotStore::saveCapture(image,directory,&err);}
    edit->setChecked(false);canvas->editing=false;canvas->setImage(image);
    resultTabs->setCurrentIndex(0);
    transientImage=path.isEmpty();
    currentPath=path;
    directorySignature.clear();
    reveal(anchor,false);
    edit->setChecked(true);
    status->setText(QStringLiteral("✓ 已开启画笔批注 · 涂鸦后复制或另存副本"));
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
    if(canvas->isCropping()){reveal(anchor,false);status->setText(QStringLiteral("请先应用或取消当前裁剪"));return;}
    if(canvas->dirty || transientImage){
        reveal(anchor,false);status->setText(QStringLiteral("请先复制或另存当前图像，再开始截图"));return;
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
    if(canvas->isCropping()||canvas->dirty||transientImage){reveal(anchor,false);status->setText(QStringLiteral("请先完成裁剪，复制或另存当前图像，再接收新截图"));return;}
    QString error;const QString path=ScreenshotStore::saveCapture(image,directory,&error);
    edit->setChecked(false);canvas->editing=false;canvas->setImage(image);
    resultTabs->setCurrentIndex(0);
    transientImage=path.isEmpty();
    if(!path.isEmpty()){
        currentPath=path;directorySignature.clear();reveal(anchor,false);
        status->setText(QStringLiteral("✓ 截图已保存"));
    }else{
        reveal(anchor,false);currentPath.clear();canvas->setImage(image);
        details->setText(QStringLiteral("未保存 · %1 × %2").arg(image.width()).arg(image.height()));
        updateImageActions();
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
    if(canvas->editing || canvas->dirty || canvas->isCropping() || canvas->isPickingColor() || !canvas->history.isEmpty() || transientImage) return;
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
    updateImageActions();
    if(paths.isEmpty()) details->setText(QStringLiteral("截图、导入图片，或直接处理文字"));
    findChild<QToolButton *>("chooseDirectory")->setToolTip(QStringLiteral("截图目录：")+directory);
    status->setText(QFileInfo(directory).isDir()
        ? QStringLiteral("%1 张图片 · %2").arg(paths.size()).arg(QDir(directory).dirName())
        : QStringLiteral("导入时创建截图目录"));
}
void ScreenshotPanel::select(int row) {
    if(row<0||row>=paths.size())return;
    if(canvas->dirty || canvas->isCropping() || transientImage){
        status->setText(QStringLiteral("请先复制或另存当前图片，再切换截图"));
        const QSignalBlocker blocker(thumbnails);thumbnails->setCurrentRow(paths.indexOf(currentPath));return;
    }
    currentPath=paths[row]; QImageReader reader(currentPath); reader.setAutoTransform(true);
    canvas->setImage(reader.read()); edit->setChecked(false);
    details->setText(QFileInfo(currentPath).fileName()+QStringLiteral(" · %1 × %2").arg(canvas->image.width()).arg(canvas->image.height()));
    details->setToolTip(currentPath);
    updateImageActions();
    status->setText(canvas->image.isNull()?QStringLiteral("图片读取失败"):QFileInfo(currentPath).fileName());
}
void ScreenshotPanel::chooseDirectory(){
    if(canvas->dirty || canvas->isCropping() || transientImage){status->setText(QStringLiteral("请先完成裁剪，复制或另存当前图片，再更换目录"));return;}
    QString selected=QFileDialog::getExistingDirectory(this,QStringLiteral("选择截图目录"),directory);
    if(selected.isEmpty())return;
    directory=selected; directorySignature.clear(); currentPath.clear(); canvas->setImage({}); edit->setChecked(false); thumbnails->clear(); QSettings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall").setValue("screenshots/directory",directory);paths.clear();refresh();
}
void ScreenshotPanel::importClipboard() {
    if(canvas->dirty || canvas->isCropping() || transientImage) {
        status->setText(QStringLiteral("请先完成裁剪，复制或另存当前图像，再导入"));
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
    const QSize preferred=QSize(560,thumbnails->isHidden()?500:560).expandedTo(minimumSizeHint());
    resize(preferred.boundedTo(bounds.size())); layout()->activate();
    const int x=anchor.x()+width()+36<=bounds.right()?anchor.x()+36:anchor.x()-width()-36;
    QPoint target(qBound(bounds.left(),x,bounds.right()-width()+1),qBound(bounds.top(),anchor.y()-height()/2,bounds.bottom()-height()+1));
    move(target+QPoint(0,18));show();raise();activateWindow();
    auto animation=new QPropertyAnimation(this,"pos",this);animation->setDuration(260);animation->setStartValue(pos());animation->setEndValue(target);animation->setEasingCurve(QEasingCurve::OutBack);animation->start(QAbstractAnimation::DeleteWhenStopped);
}
void ScreenshotPanel::revealText(const QPoint &anchor) {
    reveal(anchor,false);resultTabs->setCurrentIndex(1);
    if(canvas->image.isNull()){status->setText(QStringLiteral("先截图或导入图片，也可以在文字区粘贴内容"));return;}
    if(recognizedText->toPlainText().isEmpty()||recognizedImageKey!=canvas->image.cacheKey())recognizeText();
}
void ScreenshotPanel::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape){if(canvas->isCropping()||canvas->isPickingColor())canvas->cancelImageAction();else hide();}else QWidget::keyPressEvent(e);}
