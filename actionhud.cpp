#include "actionhud.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "screenshotstore.h"
#include "screentext.h"
#include "uiicons.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <memory>

namespace {
class HudDragHeader final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) m_offset = e->globalPosition().toPoint() - window()->pos();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton) window()->move(e->globalPosition().toPoint() - m_offset);
    }
private:
    QPoint m_offset;
};

QPixmap roundedThumbnail(const QImage &src, const QSize &box)
{
    const qreal dpr = 2.0;
    QPixmap out(box * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    if (src.isNull()) return out;

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(box)), 10, 10);
    p.setClipPath(clip);
    p.fillRect(QRect(QPoint(0, 0), box), QColor("#121620"));
    QSize fitted = src.size();
    fitted.scale(box, Qt::KeepAspectRatio);
    const QRect target(QPoint((box.width() - fitted.width()) / 2, (box.height() - fitted.height()) / 2), fitted);
    p.drawImage(target, src);
    p.setClipping(false);
    p.setPen(QPen(QColor(255, 255, 255, 42), 1.0));
    p.drawRoundedRect(QRectF(0.5, 0.5, box.width() - 1, box.height() - 1), 10, 10);
    return out;
}
} // namespace

ActionResultHud::ActionResultHud(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("actionResultHud"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 灵动结果卡"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 8);
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("hudCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#hudCard { background:@panelTint@; border:1px solid rgba(255,255,255,42); border-radius:20px; }"
        "QLabel { color:#c2ccda; font-size:12px; background:transparent; }"
        "QLabel#hudTitle { color:#f5f8fd; font-size:14px; font-weight:600; }"
        "QLabel#ocrBadge { color:#8ac8ff; font-size:12px; font-weight:500; }"
        "QToolButton { background:rgba(255,255,255,15); color:#f2f6fc; border:1px solid rgba(255,255,255,20);"
        "              border-radius:12px; padding:6px 11px; font-size:13px; font-weight:500; }"
        "QToolButton:hover { background:rgba(255,255,255,26); }"
        "QToolButton:pressed { background:rgba(255,255,255,38); }"
        "QToolButton#hudPrimary { background:#2a7de1; border:1px solid #59a2ff; color:#ffffff; font-weight:600; }"
        "QToolButton#hudPrimary:hover { background:#388cf0; }"
        "QToolButton#hudClose { background:transparent; border:0; border-radius:15px; }"
        "QToolButton#hudClose:hover { background:rgba(255,255,255,20); }"
        "QPlainTextEdit { background:rgba(10,13,19,175); color:#eef3fb; border:1px solid rgba(255,255,255,28);"
        "                 border-radius:12px; padding:8px; font-size:13px; selection-background-color:#2c5d99; }");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    // Header: preview + meta + close
    auto *header = new HudDragHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hRow = new QHBoxLayout(header);
    hRow->setContentsMargins(0, 0, 0, 0);
    hRow->setSpacing(10);

    m_preview = new QLabel(header);
    m_preview->setFixedSize(88, 56);
    m_preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    hRow->addWidget(m_preview);

    auto *metaCol = new QVBoxLayout();
    metaCol->setSpacing(3);
    auto *title = new QLabel(QStringLiteral("准星捕获结果"), header);
    title->setObjectName(QStringLiteral("hudTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_metaLabel = new QLabel(header);
    m_metaLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_ocrBadge = new QLabel(header);
    m_ocrBadge->setObjectName(QStringLiteral("ocrBadge"));
    m_ocrBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    metaCol->addWidget(title);
    metaCol->addWidget(m_metaLabel);
    metaCol->addWidget(m_ocrBadge);
    hRow->addLayout(metaCol, 1);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("hudClose"));
    closeBtn->setAccessibleName(QStringLiteral("关闭结果卡"));
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#dce3ed"), 18));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hRow->addWidget(closeBtn, 0, Qt::AlignTop);
    layout->addWidget(header);

    // Inline OCR result box (shown automatically when text is recognized)
    m_textContainer = new QWidget(card);
    auto *textCol = new QVBoxLayout(m_textContainer);
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(6);
    m_textEdit = new QPlainTextEdit(m_textContainer);
    m_textEdit->setObjectName(QStringLiteral("hudOcrText"));
    m_textEdit->setMaximumHeight(96);
    m_textEdit->setPlaceholderText(QStringLiteral("本机识别出的文字会显示在这里…"));
    textCol->addWidget(m_textEdit);

    auto *textTools = new QHBoxLayout();
    textTools->setSpacing(6);
    auto makeMiniBtn = [this](const QString &label, FloatingIcons::Icon icon, const char *objName) {
        auto *b = new QToolButton(this);
        b->setObjectName(QLatin1String(objName));
        b->setText(label);
        b->setAccessibleName(label);
        b->setIcon(FloatingIcons::make(icon, QColor("#dce3ed"), 16));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setMinimumHeight(34);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto *searchBtn = makeMiniBtn(QStringLiteral("搜索文字"), FloatingIcons::Search, "hudSearchText");
    auto *transBtn = makeMiniBtn(QStringLiteral("翻译"), FloatingIcons::Globe, "hudTranslateText");
    connect(searchBtn, &QToolButton::clicked, this, &ActionResultHud::searchOcrText);
    connect(transBtn, &QToolButton::clicked, this, &ActionResultHud::translateOcrText);
    textTools->addWidget(searchBtn);
    textTools->addWidget(transBtn);
    textTools->addStretch(1);
    textCol->addLayout(textTools);
    m_textContainer->hide();
    layout->addWidget(m_textContainer);

    // Touch-sized primary pill actions (44pt height)
    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(6);
    auto makePill = [this](const QString &label, FloatingIcons::Icon icon, const char *objName, bool primary = false) {
        auto *b = new QToolButton(this);
        b->setObjectName(QLatin1String(primary ? "hudPrimary" : objName));
        b->setProperty("actionName", QLatin1String(objName));
        b->setText(label);
        b->setAccessibleName(label);
        b->setIcon(FloatingIcons::make(icon, QColor("#f5f8fd"), 18));
        b->setIconSize(QSize(18, 18));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setMinimumHeight(44);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    m_copyImgBtn = makePill(QStringLiteral("复制图"), FloatingIcons::Copy, "copyHudImage", true);
    m_copyTxtBtn = makePill(QStringLiteral("抠字"), FloatingIcons::Clipboard, "copyHudText");
    m_pinBtn = makePill(QStringLiteral("贴桌面"), FloatingIcons::Pin, "pinHudImage");
    m_annotateBtn = makePill(QStringLiteral("标注"), FloatingIcons::Pen, "annotateHudImage");
    m_saveBtn = makePill(QStringLiteral("保存"), FloatingIcons::Save, "saveHudImage");

    for (QToolButton *b : {m_copyImgBtn, m_copyTxtBtn, m_pinBtn, m_annotateBtn, m_saveBtn})
        actionRow->addWidget(b);
    layout->addLayout(actionRow);

    connect(m_copyImgBtn, &QToolButton::clicked, this, &ActionResultHud::copyImageNow);
    connect(m_copyTxtBtn, &QToolButton::clicked, this, &ActionResultHud::copyTextNow);
    connect(m_pinBtn, &QToolButton::clicked, this, [this] {
        if (m_image.isNull()) return;
        const QString name = m_savedPath.isEmpty() ? QStringLiteral("准星贴图") : QFileInfo(m_savedPath).fileName();
        emit pinRequested(m_image, name, pos());
        DesktopActions::showToast(QStringLiteral("✓ 已钉在桌面 · 双指或滚轮缩放"), geometry().center());
        hide();
    });
    connect(m_annotateBtn, &QToolButton::clicked, this, [this] {
        if (m_image.isNull()) return;
        const QImage img = m_image;
        const QString saved = m_savedPath;
        const QPoint pt = geometry().center();
        hide();
        emit annotateRequested(img, saved, pt);
    });
    connect(m_saveBtn, &QToolButton::clicked, this, &ActionResultHud::savePngNow);

    FloatingWindow::prepare(this, false);
    qApp->installEventFilter(this);
}

ActionResultHud::~ActionResultHud()
{
    stopOcrWorker();
}

QString ActionResultHud::recognizedText() const
{
    return m_textEdit ? m_textEdit->toPlainText().trimmed() : QString();
}

void ActionResultHud::presentCapture(const QImage &image, const QString &directory, const QPoint &anchor)
{
    if (image.isNull()) return;
    stopOcrWorker();
    m_image = image;
    m_directory = directory;
    m_savedPath.clear();
    m_anchor = anchor;
    m_copyTextWhenReady = false;

    m_preview->setPixmap(roundedThumbnail(image, QSize(88, 56)));
    m_metaLabel->setText(QStringLiteral("%1 × %2 px").arg(image.width()).arg(image.height()));
    m_ocrBadge->setText(QStringLiteral("正在本机识别文字…"));
    m_textEdit->clear();
    m_textContainer->hide();

    adjustSize();
    QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    QPoint target = anchor + QPoint(16, 16);
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        if (target.x() + width() > bounds.right()) target.setX(anchor.x() - width() - 16);
        if (target.y() + height() > bounds.bottom()) target.setY(anchor.y() - height() - 16);
        target.setX(qBound(bounds.left() + 6, target.x(), bounds.right() - width() - 5));
        target.setY(qBound(bounds.top() + 6, target.y(), bounds.bottom() - height() - 5));
    }
    move(target + QPoint(0, 12));
    show();
    raise();
    auto *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(200);
    anim->setStartValue(pos());
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutBack);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    startBackgroundOcr();
}

void ActionResultHud::startBackgroundOcr()
{
    const QImage pixels = m_image;
    const qint64 key = pixels.cacheKey();
    m_ocrImageKey = key;
    auto result = std::make_shared<ScreenText::Result>();
    auto *worker = QThread::create([pixels, result] { *result = ScreenText::recognize(pixels); });
    worker->setParent(this);
    m_ocrWorker = worker;
    connect(worker, &QThread::finished, this, [this, result, key, worker] {
        if (m_ocrWorker == worker) m_ocrWorker = nullptr;
        worker->deleteLater();
        if (key != m_ocrImageKey) return;

        const QString text = result->text.trimmed();
        if (!text.isEmpty()) {
            m_textEdit->setPlainText(text);
            m_textContainer->show();
            m_ocrBadge->setText(QStringLiteral("✓ 识别出 %1 字 · 点“抠字”一键复制").arg(text.size()));
            adjustSize();
            if (QScreen *s = QGuiApplication::screenAt(geometry().center())) {
                const QRect b = s->availableGeometry();
                move(qBound(b.left() + 6, x(), b.right() - width() - 5),
                     qBound(b.top() + 6, y(), b.bottom() - height() - 5));
            }
            if (m_copyTextWhenReady) {
                m_copyTextWhenReady = false;
                copyTextNow();
            }
        } else {
            m_ocrBadge->setText(result->error.isEmpty() ? QStringLiteral("未检测到文字") : result->error.left(42));
            if (m_copyTextWhenReady) {
                m_copyTextWhenReady = false;
                DesktopActions::showToast(m_ocrBadge->text(), geometry().center());
            }
        }
    });
    worker->start();
}

void ActionResultHud::stopOcrWorker()
{
    if (m_ocrWorker) {
        m_ocrWorker->requestInterruption();
        m_ocrWorker->wait(1500);
        delete m_ocrWorker.data();
        m_ocrWorker = nullptr;
    }
}

void ActionResultHud::copyImageNow()
{
    QString err;
    if (ScreenshotStore::copyImage(m_image, &err)) {
        m_ocrBadge->setText(QStringLiteral("✓ 图片已复制到剪贴板"));
        DesktopActions::showToast(QStringLiteral("✓ 图片已复制"), geometry().center());
    } else {
        m_ocrBadge->setText(err);
    }
}

void ActionResultHud::copyTextNow()
{
    if (m_ocrWorker) {
        m_copyTextWhenReady = true;
        m_ocrBadge->setText(QStringLiteral("识别完成后将自动复制…"));
        return;
    }
    const QTextCursor cursor = m_textEdit->textCursor();
    const QString text = cursor.hasSelection()
        ? cursor.selectedText().replace(QChar::ParagraphSeparator, '\n')
        : m_textEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_textContainer->show();
        adjustSize();
        m_ocrBadge->setText(QStringLiteral("未识别到文字，可重试更清晰的区域"));
        return;
    }
    QApplication::clipboard()->setText(text);
    emit clipboardSaved(text);
    m_ocrBadge->setText(QStringLiteral("✓ 已复制 %1 个字符").arg(text.size()));
    DesktopActions::showToast(QStringLiteral("✓ 文字已复制 (%1 字)").arg(text.size()), geometry().center());
}

void ActionResultHud::savePngNow()
{
    if (!m_savedPath.isEmpty() && QFileInfo::exists(m_savedPath)) {
        QApplication::clipboard()->setText(m_savedPath);
        m_ocrBadge->setText(QStringLiteral("✓ 已保存并复制路径：") + QFileInfo(m_savedPath).fileName());
        return;
    }
    QString err;
    const QString dir = m_directory.isEmpty() ? ScreenshotStore::defaultDirectory() : m_directory;
    const QString saved = ScreenshotStore::saveCapture(m_image, dir, &err);
    if (saved.isEmpty()) {
        m_ocrBadge->setText(err);
        return;
    }
    m_savedPath = saved;
    QApplication::clipboard()->setText(saved);
    m_ocrBadge->setText(QStringLiteral("✓ 已保存：") + QFileInfo(saved).fileName());
    DesktopActions::showToast(QStringLiteral("✓ 已保存截图并复制路径"), geometry().center());
}

void ActionResultHud::searchOcrText()
{
    const QString text = recognizedText().left(1000);
    if (text.isEmpty()) return;
    QUrl url(QStringLiteral("https://www.google.com/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), text);
    url.setQuery(q);
    QDesktopServices::openUrl(url);
}

void ActionResultHud::translateOcrText()
{
    const QString text = recognizedText().left(3000);
    if (text.isEmpty()) return;
    QUrl url(QStringLiteral("https://translate.google.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("sl"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("tl"), QStringLiteral("zh-CN"));
    q.addQueryItem(QStringLiteral("text"), text);
    q.addQueryItem(QStringLiteral("op"), QStringLiteral("translate"));
    url.setQuery(q);
    QDesktopServices::openUrl(url);
}

void ActionResultHud::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool ActionResultHud::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_textEdit && event->type() == QEvent::MouseButtonPress && isVisible()) {
        activateWindow();
        m_textEdit->setFocus();
    }
    return QWidget::eventFilter(watched, event);
}
