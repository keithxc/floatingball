#include "screenshotpanel.h"
#include "screenshotstore.h"
#include "uiicons.h"
#include "floatingwindow.h"
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
void ScreenshotCanvas::setImage(const QImage &value) { image = value; history.clear(); drawing = false; update(); }
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
    if (history.size()>=12) history.removeFirst();
    history.append(image); drawing=true; last=imagePoint(e->pos());
}
void ScreenshotCanvas::mouseMoveEvent(QMouseEvent *e) {
    if (!drawing) return;
    QPoint next=imagePoint(e->pos()); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#ff637d"),qMax(3.0,image.width()/220.0),Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    p.drawLine(last,next); last=next; update();
}
void ScreenshotCanvas::mouseReleaseEvent(QMouseEvent *) { drawing=false; }
void ScreenshotCanvas::undo() { if (!history.isEmpty()) { image=history.takeLast(); update(); } }

ScreenshotPanel::ScreenshotPanel(QWidget *parent, const QString &directoryOverride) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(QStringLiteral("FloatingBall · 最近截图"));
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
        "QListWidget { background:transparent; border:0; outline:0; }");
    auto shadow=new QGraphicsDropShadowEffect(card); shadow->setBlurRadius(22); shadow->setOffset(0,5); shadow->setColor(QColor(40,30,60,36)); card->setGraphicsEffect(shadow);
    auto layout=new QVBoxLayout(card); layout->setContentsMargins(14,12,14,12); layout->setSpacing(8);
    auto header=new DragHandle; header->setCursor(Qt::OpenHandCursor); auto title=new QHBoxLayout(header); title->setContentsMargins(0,0,0,0); title->setSpacing(4);
    auto heading=new QVBoxLayout; heading->setSpacing(2);
    auto titleLabel=new QLabel(QStringLiteral("最近截图")); titleLabel->setObjectName("title"); titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(titleLabel);
    details=new ElidedLabel; details->setAttribute(Qt::WA_TransparentForMouseEvents); heading->addWidget(details); title->addLayout(heading,1);
    auto button=[this](const QString &text, FloatingIcons::Icon icon, bool iconOnly=false) {
        auto b=new QToolButton(this); b->setText(text); b->setAccessibleName(text); b->setToolTip(text);
        b->setIcon(FloatingIcons::make(icon)); b->setIconSize(QSize(17,17)); b->setFixedHeight(32); b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(iconOnly?Qt::ToolButtonIconOnly:Qt::ToolButtonTextBesideIcon);
        if(iconOnly) b->setFixedWidth(32); return b;
    };
    auto pin=button(QStringLiteral("固定"),FloatingIcons::Pin,true); pin->setObjectName("headerButton"); pin->setCheckable(true); pin->setChecked(true); title->addWidget(pin);
    auto updatePinHint=[pin](bool fixed){pin->setToolTip(fixed?QStringLiteral("已固定置顶 · 点击后切换应用自动收起"):QStringLiteral("点击固定置顶 · 切换应用时保持展示"));};
    connect(pin,&QToolButton::toggled,this,updatePinHint); updatePinHint(true);
    auto close=button(QStringLiteral("收起 ×"),FloatingIcons::Close,true); close->setObjectName("headerButton"); title->addWidget(close); layout->addWidget(header);
    connect(close,&QToolButton::clicked,this,&QWidget::hide);
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
    connect(copy,&QToolButton::clicked,this,[this]{if(!canvas->image.isNull()){ QString error; status->setText(ScreenshotStore::copyImage(canvas->image,&error)?QStringLiteral("✓ 图片已复制"):error); }});
    connect(path,&QToolButton::clicked,this,[this]{if(QFileInfo::exists(currentPath)){ QApplication::clipboard()->setText(currentPath); status->setText(QStringLiteral("✓ 原图路径已复制")); }});
    connect(edit,&QToolButton::toggled,this,[this](bool enabled){canvas->editing=enabled;canvas->setCursor(enabled?Qt::CrossCursor:Qt::ArrowCursor);});
    connect(undo,&QToolButton::clicked,canvas,&ScreenshotCanvas::undo);
    auto copyShortcut=new QShortcut(QKeySequence::Copy,this); connect(copyShortcut,&QShortcut::activated,copy,&QToolButton::click);
    auto undoShortcut=new QShortcut(QKeySequence::Undo,this); connect(undoShortcut,&QShortcut::activated,undo,&QToolButton::click);
    connect(save,&QToolButton::clicked,this,[this]{
        if(canvas->image.isNull()) return;
        QString target=QFileDialog::getSaveFileName(this,QStringLiteral("保存批注副本"),QFileInfo(currentPath).absolutePath()+"/Annotated-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz")+".png", "PNG (*.png)");
        if(target.isEmpty()) return;
        if(QFileInfo(target).absoluteFilePath()==QFileInfo(currentPath).absoluteFilePath() || (!QFileInfo(target).canonicalFilePath().isEmpty() && QFileInfo(target).canonicalFilePath()==QFileInfo(currentPath).canonicalFilePath())){status->setText(QStringLiteral("请选择新文件名，保留原截图"));return;}
        status->setText(canvas->image.save(target,"PNG")?QStringLiteral("✓ 副本已保存"):QStringLiteral("保存失败，请检查目标目录"));
    });
    auto footer=new QHBoxLayout; footer->setSpacing(4); status=new ElidedLabel; status->setFixedHeight(20); footer->addWidget(status,1);
    auto import=button(QStringLiteral("从剪贴板导入"),FloatingIcons::Import,true); import->setObjectName("importClipboard"); footer->addWidget(import);
    connect(import,&QToolButton::clicked,this,[this]{importClipboard();});
    auto folder=button(QStringLiteral("截图目录…"),FloatingIcons::Folder,true); folder->setObjectName("chooseDirectory"); footer->addWidget(folder); layout->addLayout(footer);
    connect(folder,&QToolButton::clicked,this,[this]{chooseDirectory();});
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
    // macOS shares the "active" appearance across tool windows, so
    // isActiveWindow() does not tell us whether this panel owns keyboard focus.
    auto dismissIfUnpinned=[this,pin]{QTimer::singleShot(0,this,[this,pin]{if(isVisible()&&!pin->isChecked()&&QApplication::activeWindow()!=this&&!QApplication::activeModalWidget())hide();});};
    connect(qApp,&QApplication::focusChanged,this,[dismissIfUnpinned](QWidget *,QWidget *){dismissIfUnpinned();});
    connect(qApp,&QGuiApplication::focusWindowChanged,this,[dismissIfUnpinned](QWindow *){dismissIfUnpinned();});
    connect(qApp,&QGuiApplication::applicationStateChanged,this,[dismissIfUnpinned](Qt::ApplicationState state){if(state!=Qt::ApplicationActive)dismissIfUnpinned();});
    FloatingWindow::prepare(this,true);
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
    if(canvas->editing) return;
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
    currentPath=paths[row]; QImageReader reader(currentPath); reader.setAutoTransform(true);
    canvas->setImage(reader.read()); edit->setChecked(false);
    details->setText(QFileInfo(currentPath).fileName()+QStringLiteral(" · %1 × %2").arg(canvas->image.width()).arg(canvas->image.height()));
    details->setToolTip(currentPath);
    status->setText(canvas->image.isNull()?QStringLiteral("图片读取失败"):QFileInfo(currentPath).fileName());
}
void ScreenshotPanel::chooseDirectory(){
    QString selected=QFileDialog::getExistingDirectory(this,QStringLiteral("选择截图目录"),directory);
    if(selected.isEmpty())return;
    directory=selected; directorySignature.clear(); currentPath.clear(); canvas->setImage({}); edit->setChecked(false); thumbnails->clear(); QSettings(QSettings::defaultFormat(),QSettings::UserScope,"FloatingBall","FloatingBall").setValue("screenshots/directory",directory);paths.clear();refresh();
}
void ScreenshotPanel::importClipboard() {
    if(canvas->editing && !canvas->history.isEmpty()) {
        status->setText(QStringLiteral("请先复制或另存当前批注，再关闭画笔导入"));
        return;
    }
    QString error;
    const QString imported=ScreenshotStore::importImage(QApplication::clipboard()->image(),directory,&error);
    if(imported.isEmpty()) {status->setText(error);return;}
    edit->setChecked(false); directorySignature.clear(); refresh();
    const int row=paths.indexOf(imported);
    if(row>=0) {thumbnails->setCurrentRow(row);select(row);}
    status->setText(QStringLiteral("✓ 已导入并保存：")+QFileInfo(imported).fileName());
}
void ScreenshotPanel::reveal(const QPoint &anchor){
    if(isVisible()){hide();return;} refresh();
    QScreen *screen=QGuiApplication::screenAt(anchor);if(!screen)screen=QGuiApplication::primaryScreen();
    QRect bounds=screen->availableGeometry();
    ensurePolished(); layout()->activate();
    const QSize preferred=QSize(440,thumbnails->isHidden()?352:420).expandedTo(minimumSizeHint());
    resize(preferred.boundedTo(bounds.size())); layout()->activate();
    const int x=anchor.x()+width()+36<=bounds.right()?anchor.x()+36:anchor.x()-width()-36;
    QPoint target(qBound(bounds.left(),x,bounds.right()-width()+1),qBound(bounds.top(),anchor.y()-height()/2,bounds.bottom()-height()+1));
    move(target+QPoint(0,18));show();raise();activateWindow();
    auto animation=new QPropertyAnimation(this,"pos",this);animation->setDuration(260);animation->setStartValue(pos());animation->setEndValue(target);animation->setEasingCurve(QEasingCurve::OutBack);animation->start(QAbstractAnimation::DeleteWhenStopped);
}
void ScreenshotPanel::keyPressEvent(QKeyEvent *e){if(e->key()==Qt::Key_Escape)hide();else QWidget::keyPressEvent(e);}
