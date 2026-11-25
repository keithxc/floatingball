#include "quickdeck.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "uiicons.h"

#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
class DeckHeader final : public QWidget {
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

QuickDeck::QuickDeck(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("quickDeck"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 触控甲板"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setProperty("panelBlurInset", 10);
    setProperty("panelBlurRadius", 24);
    resize(372, 360);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("deckCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#deckCard { background:transparent; border:none; }"
        "QLabel { color:rgba(226,232,240,175); font-size:11px; background:transparent; }"
        "QLabel#deckTitle { color:#f5f8fd; font-size:14.5px; font-weight:700; }"
        "QLabel#sectionTag { color:rgba(203,213,225,165); font-size:11px; font-weight:600; }"
        "QToolButton { background:rgba(255,255,255,11); color:#f2f6fc; border:1px solid rgba(255,255,255,22);"
        "              border-radius:14px; padding:6px 4px; font-size:12px; font-weight:500; }"
        "QToolButton:hover { background:rgba(255,255,255,22); border:1px solid rgba(255,255,255,36); }"
        "QToolButton:pressed { background:rgba(255,255,255,32); }"
        "QToolButton#dangerTile { background:rgba(244,63,94,24); border:1px solid rgba(251,113,133,65); color:#ffdce2; }"
        "QToolButton#dangerTile:hover { background:rgba(244,63,94,46); }"
        "QToolButton#deckMuteBtn[muted=\"true\"] { background:rgba(244,63,94,38); border:1px solid rgba(251,113,133,95); }"
        "QToolButton#deckClose { background:rgba(255,255,255,10); border:1px solid rgba(255,255,255,20); border-radius:15px; }"
        "QToolButton#deckClose:hover { background:rgba(255,255,255,22); }"
        "QSlider::groove:horizontal { height:8px; background:rgba(255,255,255,16); border-radius:4px; }"
        "QSlider::sub-page:horizontal { background:rgba(124,200,255,195); border-radius:4px; }"
        "QSlider[muted=\"true\"]::sub-page:horizontal { background:rgba(148,163,184,95); border-radius:4px; }"
        "QSlider::handle:horizontal { width:20px; height:20px; margin:-6px 0; background:#f2f6fc; border-radius:10px; }"
        "QSlider[muted=\"true\"]::handle:horizontal { background:#94a3b8; }");

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);

    // Header
    auto *header = new DeckHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hRow = new QHBoxLayout(header);
    hRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(QStringLiteral("触控快捷甲板"), header);
    title->setObjectName(QStringLiteral("deckTitle"));
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    hRow->addWidget(title);
    hRow->addStretch(1);
    auto *sub = new QLabel(QStringLiteral("点按直接作用于当前窗口"), header);
    sub->setAttribute(Qt::WA_TransparentForMouseEvents);
    hRow->addWidget(sub);
    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("deckClose"));
    closeBtn->setAccessibleName(QStringLiteral("关闭触控甲板"));
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#dce3ed"), 18));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hRow->addWidget(closeBtn);
    layout->addWidget(header);

    auto makeTile = [card](const QString &label, FloatingIcons::Icon icon, const char *name, bool danger = false) {
        auto *b = new QToolButton(card);
        b->setObjectName(QLatin1String(danger ? "dangerTile" : name));
        b->setProperty("tileId", QLatin1String(name));
        b->setText(label);
        b->setAccessibleName(label);
        b->setIcon(FloatingIcons::make(icon, danger ? QColor("#ffb8c4") : QColor("#eef4fc"), 20));
        b->setIconSize(QSize(20, 20));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setMinimumSize(76, 58);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    auto runAndFeedback = [this](std::function<DesktopActions::Outcome()> fn, bool autoClose = false) {
        const QPoint center = geometry().center();
        if (autoClose) hide();
        QTimer::singleShot(autoClose ? 35 : 0, this, [fn, center] {
            const auto res = fn();
            DesktopActions::showToast(res.message, center);
        });
    };

    // Row 1: Window Touch Rescue (replaces 12pt traffic lights!)
    auto *winTag = new QLabel(QStringLiteral("当前窗口与标签控制"), card);
    winTag->setObjectName(QStringLiteral("sectionTag"));
    layout->addWidget(winTag);

    auto *grid = new QGridLayout();
    grid->setSpacing(8);
    auto *closeWin = makeTile(QStringLiteral("关闭标签/窗"), FloatingIcons::Close, "deckCloseWindow", true);
    auto *minWin = makeTile(QStringLiteral("最小化"), FloatingIcons::Minimize, "deckMinimizeWindow");
    auto *fullWin = makeTile(QStringLiteral("全屏/最大化"), FloatingIcons::Maximize, "deckFullscreenWindow");
    auto *nextTab = makeTile(QStringLiteral("切标签页"), FloatingIcons::TabNext, "deckNextTab");

    auto *backBtn = makeTile(QStringLiteral("返回"), FloatingIcons::Back, "deckBack");
    auto *fwdBtn = makeTile(QStringLiteral("前进"), FloatingIcons::Forward, "deckForward");
    auto *copyBtn = makeTile(QStringLiteral("复制"), FloatingIcons::Copy, "deckCopy");
    auto *pasteBtn = makeTile(QStringLiteral("粘贴"), FloatingIcons::Paste, "deckPaste");

    grid->addWidget(closeWin, 0, 0);
    grid->addWidget(minWin, 0, 1);
    grid->addWidget(fullWin, 0, 2);
    grid->addWidget(nextTab, 0, 3);
    grid->addWidget(backBtn, 1, 0);
    grid->addWidget(fwdBtn, 1, 1);
    grid->addWidget(copyBtn, 1, 2);
    grid->addWidget(pasteBtn, 1, 3);
    layout->addLayout(grid);

    connect(closeWin, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::closeActiveWindow, true); });
    connect(minWin, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::minimizeActiveWindow, true); });
    connect(fullWin, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::toggleFullscreenActiveWindow, true); });
    connect(nextTab, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::nextTab, false); });
    connect(backBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::navigateBack, false); });
    connect(fwdBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::navigateForward, false); });
    connect(copyBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::copySelection, false); });
    connect(pasteBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::pasteClipboard, true); });

    // Row 3: System Volume Touch Slider
    auto *volRow = new QHBoxLayout();
    volRow->setSpacing(8);
    m_muteBtn = new QToolButton(card);
    m_muteBtn->setObjectName(QStringLiteral("deckMuteBtn"));
    m_muteBtn->setAccessibleName(QStringLiteral("静音切换"));
    m_muteBtn->setIcon(FloatingIcons::make(FloatingIcons::Volume, QColor("#eef4fc"), 20));
    m_muteBtn->setFixedSize(42, 42);
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    volRow->addWidget(m_muteBtn);

    m_volumeSlider = new QSlider(Qt::Horizontal, card);
    m_volumeSlider->setObjectName(QStringLiteral("deckVolumeSlider"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(60);
    m_volumeSlider->setMinimumHeight(36);
    volRow->addWidget(m_volumeSlider, 1);

    m_volumeValue = new QLabel(QStringLiteral("60%"), card);
    m_volumeValue->setFixedWidth(40);
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volRow->addWidget(m_volumeValue);
    layout->addLayout(volRow);

    m_volumeDebounce = new QTimer(this);
    m_volumeDebounce->setSingleShot(true);
    m_volumeDebounce->setInterval(90);
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        const bool nowMuted = (val <= 0);
        applyAudioVisuals(val, nowMuted, true);
        emit audioStateChanged(val, nowMuted);
        m_volumeDebounce->start();
    });
    connect(m_volumeDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setVolume(m_volumeSlider->value());
    });
    connect(m_muteBtn, &QToolButton::clicked, this, [this] {
        QPointer<QuickDeck> self(this);
        DesktopActions::toggleMute([self](int vol, bool muted, bool ok) {
            if (!self || !ok) return;
            self->applyAudioVisuals(vol, muted, true);
            emit self->audioStateChanged(vol, muted);
        });
    });

    // Row 3b: Screen Brightness Touch Slider (auto-shown when hardware brightness is supported, e.g. Linux laptops)
    m_brightnessRow = new QWidget(card);
    m_brightnessRow->setObjectName(QStringLiteral("deckBrightnessRow"));
    auto *brightLayout = new QHBoxLayout(m_brightnessRow);
    brightLayout->setContentsMargins(0, 0, 0, 0);
    brightLayout->setSpacing(8);
    auto *brightIcon = new QToolButton(m_brightnessRow);
    brightIcon->setEnabled(false);
    brightIcon->setIcon(FloatingIcons::make(FloatingIcons::Sun, QColor("#fde68a"), 19));
    brightIcon->setFixedSize(42, 42);
    brightLayout->addWidget(brightIcon);

    m_brightnessSlider = new QSlider(Qt::Horizontal, m_brightnessRow);
    m_brightnessSlider->setObjectName(QStringLiteral("deckBrightnessSlider"));
    m_brightnessSlider->setRange(1, 100);
    m_brightnessSlider->setValue(70);
    m_brightnessSlider->setMinimumHeight(36);
    brightLayout->addWidget(m_brightnessSlider, 1);

    m_brightnessValue = new QLabel(QStringLiteral("70%"), m_brightnessRow);
    m_brightnessValue->setFixedWidth(40);
    m_brightnessValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    brightLayout->addWidget(m_brightnessValue);
    m_brightnessRow->hide();
    layout->addWidget(m_brightnessRow);

    m_brightnessDebounce = new QTimer(this);
    m_brightnessDebounce->setSingleShot(true);
    m_brightnessDebounce->setInterval(80);
    connect(m_brightnessSlider, &QSlider::valueChanged, this, [this](int val) {
        m_brightnessValue->setText(QStringLiteral("%1%").arg(val));
        emit brightnessStateChanged(val);
        m_brightnessDebounce->start();
    });
    connect(m_brightnessDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setBrightness(m_brightnessSlider->value());
    });

    // Row 4: Utilities
    auto *utilRow = new QHBoxLayout();
    utilRow->setSpacing(8);
    auto *imeBtn = makeTile(QStringLiteral("切输入法"), FloatingIcons::Globe, "deckIme");
#ifdef Q_OS_LINUX
    auto *shotsBtn = makeTile(QStringLiteral("截屏 / 录屏"), FloatingIcons::Crosshair, "deckCapture");
    auto *deskBtn = makeTile(QStringLiteral("显示桌面"), FloatingIcons::Desktop, "deckDesktop");
    auto *setBtn = makeTile(QStringLiteral("悬浮球设置"), FloatingIcons::Settings, "deckSettings");
    for (QToolButton *b : {imeBtn, shotsBtn, deskBtn, setBtn}) utilRow->addWidget(b);
    layout->addLayout(utilRow);

    connect(imeBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::switchInputMethod, false); });
    connect(shotsBtn, &QToolButton::clicked, this, [this] { hide(); emit captureRequested(); });
    connect(deskBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::showDesktop, true); });
    connect(setBtn, &QToolButton::clicked, this, [this] { hide(); emit settingsRequested(); });
#else
    auto *shotsBtn = makeTile(QStringLiteral("区域截图"), FloatingIcons::Crosshair, "deckCapture");
    auto *recBtn = makeTile(QStringLiteral("屏幕录制"), FloatingIcons::Record, "deckRecord");
    auto *setBtn = makeTile(QStringLiteral("悬浮球设置"), FloatingIcons::Settings, "deckSettings");
    for (QToolButton *b : {imeBtn, shotsBtn, recBtn, setBtn}) utilRow->addWidget(b);
    layout->addLayout(utilRow);

    connect(imeBtn, &QToolButton::clicked, this, [runAndFeedback] { runAndFeedback(DesktopActions::switchInputMethod, false); });
    connect(shotsBtn, &QToolButton::clicked, this, [this] { hide(); emit captureRequested(); });
    connect(recBtn, &QToolButton::clicked, this, [this] { hide(); emit recordRequested(); });
    connect(setBtn, &QToolButton::clicked, this, [this] { hide(); emit settingsRequested(); });
#endif

    FloatingWindow::prepare(this, false);
}

void QuickDeck::reveal(const QPoint &anchor, bool toggle)
{
    if (isVisible()) {
        if (toggle) hide();
        else raise();
        return;
    }
    syncVolume();
    syncBrightness();

    adjustSize();
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

void QuickDeck::applyAudioVisuals(int percent, bool muted, bool supported)
{
    m_volumeSlider->setEnabled(supported);
    m_muteBtn->setEnabled(supported);
    if (!supported) {
        m_volumeValue->setText(QStringLiteral("--"));
        return;
    }
    m_muted = muted;
    m_lastVolume = qBound(0, percent, 100);
    const QSignalBlocker b(m_volumeSlider);
    m_volumeSlider->setValue(m_lastVolume);
    m_volumeSlider->setProperty("muted", muted);
    m_volumeSlider->style()->unpolish(m_volumeSlider);
    m_volumeSlider->style()->polish(m_volumeSlider);

    m_muteBtn->setProperty("muted", muted);
    m_muteBtn->style()->unpolish(m_muteBtn);
    m_muteBtn->style()->polish(m_muteBtn);
    m_muteBtn->setIcon(FloatingIcons::make(
        muted ? FloatingIcons::VolumeMute : FloatingIcons::Volume,
        muted ? QColor("#ffb8c4") : QColor("#eef4fc"), 20));
    m_muteBtn->setToolTip(muted
        ? QStringLiteral("系统已静音（静音前 %1%），点击取消静音").arg(m_lastVolume)
        : QStringLiteral("系统音量 %1%，点击静音").arg(m_lastVolume));
    m_volumeValue->setText(muted ? QStringLiteral("静音") : QStringLiteral("%1%").arg(m_lastVolume));
    m_volumeValue->setStyleSheet(muted ? QStringLiteral("color:#ff9dad; font-weight:600;") : QString());
}

void QuickDeck::syncVolume()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    QPointer<QuickDeck> self(this);
    DesktopActions::queryVolume([self](int vol, bool muted, bool ok) {
        if (!self) return;
        self->applyAudioVisuals(vol, muted, ok);
        if (ok) emit self->audioStateChanged(vol, muted);
    });
}

void QuickDeck::syncBrightness()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    QPointer<QuickDeck> self(this);
    DesktopActions::queryBrightness([self](int pct, bool ok) {
        if (!self || !self->m_brightnessRow) return;
        self->m_brightnessRow->setVisible(ok);
        if (ok) {
            const QSignalBlocker b(self->m_brightnessSlider);
            self->m_brightnessSlider->setValue(qBound(1, pct, 100));
            self->m_brightnessValue->setText(QStringLiteral("%1%").arg(qBound(1, pct, 100)));
            self->adjustSize();
            emit self->brightnessStateChanged(pct);
        }
    });
}

void QuickDeck::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    FloatingWindow::paintLiquidGlass(p, rect(), 10.0, 24.0);
}

void QuickDeck::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}
