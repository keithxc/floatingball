#include "clipboardbubble.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "screenshotstore.h"
#include "touchscroll.h"
#include "uiicons.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QSettings clipSettings()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
}

class BubbleHeader final : public QWidget {
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
} // namespace

ClipboardBubble::ClipboardBubble(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("clipboardBubble"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 触控剪贴板"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setProperty("panelBlurInset", 10);
    setProperty("panelBlurRadius", 24);
    resize(372, 444);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("clipCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#clipCard { background:transparent; border:none; }"
        "QLabel { color:rgba(226,232,240,195); font-size:12px; background:transparent; }"
        "QLabel#clipTitle { color:#ffffff; font-size:15px; font-weight:700; }"
        "QLabel#clipStatus { color:rgba(226,232,240,165); font-size:11px; }"
        "QCheckBox { color:#e2e8f0; font-size:12px; spacing:6px; }"
        "QCheckBox::indicator { width:16px; height:16px; border-radius:6px;"
        "                       border:1px solid rgba(255,255,255,56); background:rgba(255,255,255,16); }"
        "QCheckBox::indicator:checked { background:rgba(59,130,246,215); border:1px solid rgba(147,197,253,225); }"
        "QLineEdit { background:rgba(255,255,255,18); color:#ffffff; border:1px solid rgba(255,255,255,28);"
        "            border-radius:12px; padding:7px 10px; font-size:12.5px; }"
        "QLineEdit:focus { border:1px solid rgba(125,211,252,185); }"
        "QListWidget { background:rgba(255,255,255,14); color:#f8fafc; border:1px solid rgba(255,255,255,24);"
        "              border-radius:16px; padding:4px; outline:0; font-size:12.5px; }"
        "QListWidget::item { padding:10px 9px; border-radius:11px; margin:2px 0; }"
        "QListWidget::item:selected { background:rgba(59,130,246,84); color:#ffffff; }"
        "QListWidget::item:hover { background:rgba(255,255,255,18); }"
        "QScrollBar:vertical { width:5px; background:transparent; margin:4px 0; }"
        "QScrollBar::handle:vertical { background:rgba(255,255,255,54); border-radius:2px; min-height:24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
        "QScrollBar:horizontal { height:0px; background:transparent; }"
        "QToolButton { background:rgba(255,255,255,20); color:#f8fafc; border:1px solid rgba(255,255,255,28);"
        "              border-radius:13px; padding:6px 10px; font-size:12.5px; font-weight:600; }"
        "QToolButton:hover { background:rgba(255,255,255,32); }"
        "QToolButton:pressed { background:rgba(255,255,255,44); }"
        "QToolButton#pastePrimary { background:rgba(59,130,246,185); border:1px solid rgba(147,197,253,200); color:#ffffff; font-weight:600; }"
        "QToolButton#pastePrimary:hover { background:rgba(59,130,246,220); }"
        "QToolButton#clipClose { background:rgba(255,255,255,18); border:1px solid rgba(255,255,255,24); border-radius:16px; }");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    auto *header = new BubbleHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hRow = new QHBoxLayout(header);
    hRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("触控剪贴板"), header);
    title->setObjectName(QStringLiteral("clipTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    hRow->addWidget(title);
    hRow->addStretch(1);

    m_recordToggle = new QCheckBox(QStringLiteral("自动收录"), header);
    m_recordToggle->setObjectName(QStringLiteral("clipRecordToggle"));
    m_recordToggle->setChecked(clipSettings().value("clipboard/enabled", true).toBool());
    hRow->addWidget(m_recordToggle);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("clipClose"));
    closeBtn->setAccessibleName(QStringLiteral("关闭剪贴板"));
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#dce3ed"), 18));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hRow->addWidget(closeBtn);
    layout->addWidget(header);

    // Hero Touch Action Row
    auto *heroRow = new QHBoxLayout();
    heroRow->setSpacing(8);
    m_pasteNowBtn = new QToolButton(card);
    m_pasteNowBtn->setObjectName(QStringLiteral("pastePrimary"));
    m_pasteNowBtn->setText(QStringLiteral("粘贴到当前窗口"));
    m_pasteNowBtn->setAccessibleName(QStringLiteral("粘贴到当前窗口"));
    m_pasteNowBtn->setIcon(FloatingIcons::make(FloatingIcons::Paste, QColor("#ffffff"), 18));
    m_pasteNowBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_pasteNowBtn->setMinimumHeight(44);
    m_pasteNowBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_pasteNowBtn->setCursor(Qt::PointingHandCursor);
    heroRow->addWidget(m_pasteNowBtn, 1);

    auto *scratchToggle = new QToolButton(card);
    scratchToggle->setObjectName(QStringLiteral("toggleScratchBtn"));
    scratchToggle->setText(QStringLiteral("＋速记"));
    scratchToggle->setCheckable(true);
    scratchToggle->setMinimumHeight(44);
    scratchToggle->setCursor(Qt::PointingHandCursor);
    heroRow->addWidget(scratchToggle);
    layout->addLayout(heroRow);

    // Quick scratchpad input row
    m_scratchRow = new QWidget(card);
    auto *sRow = new QHBoxLayout(m_scratchRow);
    sRow->setContentsMargins(0, 0, 0, 0);
    sRow->setSpacing(6);
    m_scratchEdit = new QLineEdit(m_scratchRow);
    m_scratchEdit->setObjectName(QStringLiteral("clipScratchInput"));
    m_scratchEdit->setPlaceholderText(QStringLiteral("随手记一句并固定在顶部…"));
    m_scratchEdit->setMinimumHeight(38);
    auto *addScratchBtn = new QToolButton(m_scratchRow);
    addScratchBtn->setObjectName(QStringLiteral("saveScratchBtn"));
    addScratchBtn->setText(QStringLiteral("固定"));
    addScratchBtn->setMinimumHeight(38);
    sRow->addWidget(m_scratchEdit, 1);
    sRow->addWidget(addScratchBtn);
    m_scratchRow->hide();
    layout->addWidget(m_scratchRow);

    connect(scratchToggle, &QToolButton::toggled, this, [this](bool open) {
        m_scratchRow->setVisible(open);
        if (open) {
            activateWindow();
            m_scratchEdit->setFocus();
        }
    });
    connect(addScratchBtn, &QToolButton::clicked, this, &ClipboardBubble::addQuickScratch);
    connect(m_scratchEdit, &QLineEdit::returnPressed, this, &ClipboardBubble::addQuickScratch);

    m_filterEdit = new QLineEdit(card);
    m_filterEdit->setObjectName(QStringLiteral("clipFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("筛选历史记录…"));
    m_filterEdit->setMinimumHeight(36);
    layout->addWidget(m_filterEdit);

    m_list = new QListWidget(card);
    m_list->setObjectName(QStringLiteral("clipHistoryList"));
    m_list->setIconSize(QSize(56, 40));
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    if (auto *ts = TouchScrollController::install(m_list)) {
        ts->onHorizontalSwipe = [this](int delta) {
            if (delta < 0) togglePinSelected();
            else removeSelected();
        };
    }
    layout->addWidget(m_list, 1);

    // Bottom touch action bar
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setSpacing(6);
    auto makeBtn = [card](const QString &label, const char *name) {
        auto *b = new QToolButton(card);
        b->setObjectName(QLatin1String(name));
        b->setText(label);
        b->setAccessibleName(label);
        b->setMinimumHeight(38);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    auto *copyBtn = makeBtn(QStringLiteral("仅复制"), "clipCopyBtn");
    auto *pinBtn = makeBtn(QStringLiteral("★ 置顶"), "clipPinBtn");
    auto *delBtn = makeBtn(QStringLiteral("删除"), "clipDeleteBtn");
    auto *clearBtn = makeBtn(QStringLiteral("清空未置顶"), "clipClearBtn");
    for (QToolButton *b : {copyBtn, pinBtn, delBtn, clearBtn}) bottomRow->addWidget(b);
    layout->addLayout(bottomRow);

    m_status = new QLabel(QStringLiteral("上下滑动浏览 · 轻点复制 · 双击粘贴 · 右划置顶 / 左划删除"), card);
    m_status->setObjectName(QStringLiteral("clipStatus"));
    layout->addWidget(m_status);

    connect(m_pasteNowBtn, &QToolButton::clicked, this, [this] {
        if (m_list->currentItem()) {
            copySelected(true);
        } else {
            const auto out = DesktopActions::pasteClipboard();
            m_status->setText(out.message);
            DesktopActions::showToast(out.message, geometry().center());
            if (out.ok) hide();
        }
    });
    connect(copyBtn, &QToolButton::clicked, this, [this] { copySelected(false); });
    connect(pinBtn, &QToolButton::clicked, this, &ClipboardBubble::togglePinSelected);
    connect(delBtn, &QToolButton::clicked, this, &ClipboardBubble::removeSelected);
    connect(clearBtn, &QToolButton::clicked, this, &ClipboardBubble::clearUnpinned);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &ClipboardBubble::refreshList);
    connect(m_list, &QListWidget::itemClicked, this, [this] { copySelected(false); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { copySelected(true); });

    connect(m_recordToggle, &QCheckBox::toggled, this, [this](bool enabled) {
        clipSettings().setValue("clipboard/enabled", enabled);
        if (!enabled) {
            m_items.clear();
            refreshList();
            m_status->setText(QStringLiteral("已暂停收录并清空内存历史"));
        } else {
            observeSystemClipboard();
            m_status->setText(QStringLiteral("已开启剪贴板收录（仅保存在内存）"));
        }
    });

    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (m_recordToggle->isChecked() && !m_internalCopy) observeSystemClipboard();
    });

    if (m_recordToggle->isChecked()) observeSystemClipboard();

    FloatingWindow::prepare(this, false);
    qApp->installEventFilter(this);
}

bool ClipboardBubble::isRecordingEnabled() const
{
    return m_recordToggle && m_recordToggle->isChecked();
}

void ClipboardBubble::setRecordingEnabled(bool enabled)
{
    if (m_recordToggle) m_recordToggle->setChecked(enabled);
}

void ClipboardBubble::reveal(const QPoint &anchor, bool toggle)
{
    if (isVisible()) {
        if (toggle) hide();
        else raise();
        return;
    }
    if (m_recordToggle->isChecked()) observeSystemClipboard();
    refreshList();

    QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    QPoint target = anchor + QPoint(24, -height() / 2);
    if (screen) {
        const QRect bounds = screen->availableGeometry();
        if (anchor.x() > bounds.center().x())
            target.setX(anchor.x() - width() - 24);
        else
            target.setX(anchor.x() + 28);
        target.setX(qBound(bounds.left() + 6, target.x(), bounds.right() - width() - 5));
        target.setY(qBound(bounds.top() + 6, anchor.y() - height() / 2, bounds.bottom() - height() - 5));
    }
    move(target + QPoint(0, 10));
    show();
    raise();
    auto *anim = new QPropertyAnimation(this, "pos", this);
    anim->setDuration(180);
    anim->setStartValue(pos());
    anim->setEndValue(target);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ClipboardBubble::recordText(const QString &text, bool pinned)
{
    const QString cleaned = text.trimmed();
    if (cleaned.isEmpty() || cleaned.size() > 1024 * 1024) return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].image.isNull() && m_items[i].text == cleaned) {
            const bool keepPin = m_items[i].pinned || pinned;
            m_items.removeAt(i);
            m_items.prepend({cleaned, {}, keepPin});
            refreshList();
            return;
        }
    }
    while (m_items.size() >= 50) {
        int victim = -1;
        for (int i = m_items.size() - 1; i >= 0; --i) {
            if (!m_items[i].pinned) { victim = i; break; }
        }
        if (victim < 0) return;
        m_items.removeAt(victim);
    }
    m_items.prepend({cleaned, {}, pinned});
    refreshList();
}

void ClipboardBubble::observeSystemClipboard()
{
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    Item item;
    if (mime->hasImage()) {
        item.image = qvariant_cast<QImage>(mime->imageData());
        if (item.image.isNull() || item.image.sizeInBytes() > 8 * 1024 * 1024) return;
    } else if (mime->hasText()) {
        item.text = mime->text();
        if (item.text.trimmed().isEmpty() || item.text.size() > 1024 * 1024) return;
    } else {
        return;
    }
    if (!m_items.isEmpty() && m_items.first().text == item.text && m_items.first().image == item.image)
        return;

    auto totalImageBytes = [this]() {
        qint64 sum = 0;
        for (const auto &c : m_items) sum += c.image.sizeInBytes();
        return sum;
    };
    while (m_items.size() >= 50 || totalImageBytes() + item.image.sizeInBytes() > 32 * 1024 * 1024) {
        int victim = -1;
        for (int i = m_items.size() - 1; i >= 0; --i) {
            if (!m_items[i].pinned) { victim = i; break; }
        }
        if (victim < 0) return;
        m_items.removeAt(victim);
    }
    m_items.prepend(item);
    refreshList();
}

void ClipboardBubble::refreshList()
{
    m_list->clear();
    const QString filter = m_filterEdit->text().trimmed();
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < m_items.size(); ++i) {
            const auto &c = m_items[i];
            if (c.pinned != (pass == 0)) continue;
            if (!filter.isEmpty() && !c.text.contains(filter, Qt::CaseInsensitive)) continue;
            const QString summary = c.image.isNull()
                ? c.text.simplified().left(110)
                : QStringLiteral("图片 · %1 × %2 px").arg(c.image.width()).arg(c.image.height());
            auto *row = new QListWidgetItem((c.pinned ? QStringLiteral("★ ") : QString()) + summary, m_list);
            row->setData(Qt::UserRole, i);
            if (!c.image.isNull()) {
                row->setIcon(QPixmap::fromImage(
                    c.image.scaled(112, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
        }
    }
    if (m_list->count() > 0 && !m_list->currentItem())
        m_list->setCurrentRow(0);
}

void ClipboardBubble::copySelected(bool pasteIntoWindow)
{
    auto *row = m_list->currentItem();
    if (!row) return;
    const int idx = row->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_items.size()) return;
    const Item item = m_items.at(idx);

    m_internalCopy = true;
    if (item.image.isNull()) {
        QApplication::clipboard()->setText(item.text);
    } else {
        QString err;
        if (!ScreenshotStore::copyImage(item.image, &err)) {
            m_internalCopy = false;
            m_status->setText(err);
            return;
        }
    }
    m_internalCopy = false;

    if (pasteIntoWindow) {
        hide();
        QTimer::singleShot(45, this, [] {
            const auto out = DesktopActions::pasteClipboard();
            DesktopActions::showToast(out.message);
        });
    } else {
        m_status->setText(QStringLiteral("✓ 已写入剪贴板 · 点顶部蓝键可直接贴入窗口"));
    }
}

void ClipboardBubble::togglePinSelected()
{
    auto *row = m_list->currentItem();
    if (!row) return;
    const int idx = row->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_items.size()) return;
    m_items[idx].pinned = !m_items[idx].pinned;
    refreshList();
}

void ClipboardBubble::removeSelected()
{
    auto *row = m_list->currentItem();
    if (!row) return;
    const int idx = row->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_items.size()) return;
    m_items.removeAt(idx);
    refreshList();
}

void ClipboardBubble::clearUnpinned()
{
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(),
                                 [](const Item &c) { return !c.pinned; }),
                  m_items.end());
    refreshList();
    m_status->setText(QStringLiteral("✓ 已清空未置顶条目"));
}

void ClipboardBubble::addQuickScratch()
{
    const QString text = m_scratchEdit->text().trimmed();
    if (text.isEmpty()) return;
    recordText(text, true);
    m_internalCopy = true;
    QApplication::clipboard()->setText(text);
    m_internalCopy = false;
    m_scratchEdit->clear();
    m_status->setText(QStringLiteral("✓ 已固定速记并复制到剪贴板"));
}

void ClipboardBubble::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    FloatingWindow::paintLiquidGlass(p, rect(), 10.0, 24.0);
}

void ClipboardBubble::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool ClipboardBubble::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == m_filterEdit || watched == m_scratchEdit) &&
        event->type() == QEvent::MouseButtonPress && isVisible()) {
        activateWindow();
        static_cast<QWidget *>(watched)->setFocus();
    }
    return QWidget::eventFilter(watched, event);
}
