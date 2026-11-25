#include "systempopover.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "systemstatus.h"
#include "uiicons.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <functional>
#include <memory>

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

// ============================================================================
// Apple macOS / iOS-style Thick Touch Capsule Slider
// ============================================================================
class CapsuleSlider final : public QWidget
{
public:
    explicit CapsuleSlider(const QColor &fillColor, QWidget *parent = nullptr)
        : QWidget(parent), m_fillColor(fillColor)
    {
        setFixedHeight(32);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }

    void setRange(int minVal, int maxVal)
    {
        m_min = minVal;
        m_max = qMax(minVal + 1, maxVal);
        setValue(m_value);
    }

    void setValue(int val)
    {
        const int clamped = qBound(m_min, val, m_max);
        if (clamped == m_value) return;
        m_value = clamped;
        update();
    }

    int value() const { return m_value; }

    void setMutedAppearance(bool muted)
    {
        if (m_muted == muted) return;
        m_muted = muted;
        update();
    }

    std::function<void(int)> onValueChanged;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = r.height() * 0.5;

        QPainterPath track;
        track.addRoundedRect(r, radius, radius);
        p.fillPath(track, QColor(255, 255, 255, 22));
        p.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
        p.drawPath(track);

        const qreal frac = qBound(0.0, qreal(m_value - m_min) / qreal(m_max - m_min), 1.0);
        const qreal minPill = r.height();
        const qreal fillW = (frac <= 0.001) ? 0.0 : qMax(minPill, r.width() * frac);

        if (fillW > 0.5) {
            p.save();
            p.setClipPath(track);
            QRectF fillRect(r.left(), r.top(), fillW, r.height());
            QColor fill = m_muted ? QColor(148, 163, 184, 150) : m_fillColor;
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(fillRect, radius, radius);

            // Subtle Apple-style tactile thumb dot inside the leading edge of the fill
            const qreal knobR = r.height() * 0.34;
            const QPointF knobCenter(fillRect.right() - r.height() * 0.5, r.center().y());
            p.setBrush(QColor(255, 255, 255, m_muted ? 170 : 245));
            p.drawEllipse(knobCenter, knobR, knobR);
            p.restore();
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            updateFromX(e->position().x());
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            updateFromX(e->position().x());
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) m_dragging = false;
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int step = (e->angleDelta().y() > 0) ? 4 : -4;
        const int next = qBound(m_min, m_value + step, m_max);
        if (next != m_value) {
            m_value = next;
            update();
            if (onValueChanged) onValueChanged(m_value);
        }
        e->accept();
    }

private:
    void updateFromX(qreal x)
    {
        const qreal pad = height() * 0.35;
        const qreal span = qMax(1.0, width() - pad * 2.0);
        const qreal frac = qBound(0.0, (x - pad) / span, 1.0);
        const int next = qRound(m_min + frac * (m_max - m_min));
        if (next != m_value) {
            m_value = next;
            update();
            if (onValueChanged) onValueChanged(m_value);
        }
    }

    int m_min = 0;
    int m_max = 100;
    int m_value = 50;
    bool m_muted = false;
    bool m_dragging = false;
    QColor m_fillColor;
};

namespace {

class PopoverDragHeader final : public QWidget
{
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_offset = e->globalPosition().toPoint() - window()->pos();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            window()->move(e->globalPosition().toPoint() - m_offset);
    }
private:
    QPoint m_offset;
};

void clearLayout(QLayout *layout)
{
    if (!layout) return;
    while (QLayoutItem *it = layout->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
}

void repolish(QWidget *w)
{
    if (!w || !w->style()) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
}

QScrollArea *makeTouchScrollArea(QWidget *parent, QVBoxLayout **outInnerLayout)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; margin: 2px 0; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,46); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
    auto *container = new QWidget(scroll);
    container->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 2, 4, 2);
    vbox->setSpacing(6);
    scroll->setWidget(container);
    if (outInnerLayout) *outInnerLayout = vbox;
    return scroll;
}

QString btDeviceEmoji(const QString &iconType, const QString &name)
{
    const QString lo = (iconType + QLatin1Char(' ') + name).toLower();
    if (lo.contains(QLatin1String("mouse"))) return QStringLiteral("🖱");
    if (lo.contains(QLatin1String("keyboard"))) return QStringLiteral("⌨");
    if (lo.contains(QLatin1String("audio")) || lo.contains(QLatin1String("headset")) ||
        lo.contains(QLatin1String("headphone")))
        return QStringLiteral("🎧");
    if (lo.contains(QLatin1String("gaming")) || lo.contains(QLatin1String("xbox")) ||
        lo.contains(QLatin1String("gamepad")))
        return QStringLiteral("🎮");
    if (lo.contains(QLatin1String("phone"))) return QStringLiteral("📱");
    return QStringLiteral("ᛒ");
}

void styleCircleBadge(QLabel *badge, FloatingIcons::Icon icon, const QColor &bg, const QColor &fg, int size = 34)
{
    if (!badge) return;
    badge->setFixedSize(size, size);
    badge->setAlignment(Qt::AlignCenter);
    badge->setPixmap(FloatingIcons::make(icon, fg, size - 16).pixmap(size - 16, size - 16));
    badge->setStyleSheet(QStringLiteral("background:%1; border-radius:%2px; border:none;")
                             .arg(bg.name(QColor::HexArgb))
                             .arg(size / 2));
}

QString elideText(const QString &text, int maxChars)
{
    if (text.size() <= maxChars) return text;
    return text.left(maxChars - 1) + QStringLiteral("…");
}

} // namespace

// ============================================================================
// SystemPopover — Unified macOS / iOS Floating Control Center
// ============================================================================

SystemPopover::SystemPopover(SystemStatusMonitor *monitor, QWidget *parent)
    : QWidget(parent)
    , m_monitor(monitor)
    , m_net(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("systemPopover"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 控制中心"));
    // Frameless top-most floating panel; accepts soft on-demand activation so
    // Esc and click-outside-to-dismiss work natively on Wayland LayerShellQt.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(382);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 8);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("ccCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#ccCard { background:@panelTint@; border:1px solid rgba(255,255,255,38); border-radius:24px; }"
        "QWidget#bentoGroup { background:rgba(255,255,255,13); border:1px solid rgba(255,255,255,22); border-radius:18px; }"
        "QLabel { color:#cbd5e1; font-size:12px; background:transparent; }"
        "QLabel#ccTitle { color:#f8fafc; font-size:15px; font-weight:700; }"
        "QLabel#ccMeta { color:#94a3b8; font-size:11px; font-weight:600; }"
        "QLabel#ccSub { color:#94a3b8; font-size:11px; }"
        "QLabel#rowTitle { color:#f8fafc; font-size:12.5px; font-weight:600; }"
        "QLabel#rowSub { color:#94a3b8; font-size:11px; }"
        "QLabel#sliderTag { color:#94a3b8; font-size:11px; font-weight:600; }"
        "QLabel#sliderVal { color:#e2e8f0; font-size:11.5px; font-weight:600; }"
        "QToolButton#hdrIconBtn { background:rgba(255,255,255,14); color:#e2e8f0; border:1px solid rgba(255,255,255,20); border-radius:15px; }"
        "QToolButton#hdrIconBtn:hover { background:rgba(255,255,255,28); }"
        "QToolButton#backPill { background:rgba(255,255,255,14); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                       border-radius:14px; padding:4px 10px 4px 6px; font-size:12px; font-weight:600; }"
        "QToolButton#backPill:hover { background:rgba(255,255,255,28); }"
        "QPushButton#connRow { background:transparent; border:none; border-radius:12px; text-align:left; }"
        "QPushButton#connRow:hover { background:rgba(255,255,255,14); }"
        "QPushButton#connRow:pressed { background:rgba(255,255,255,24); }"
        "QPushButton#dndCard { background:rgba(255,255,255,13); border:1px solid rgba(255,255,255,22); border-radius:18px; text-align:left; }"
        "QPushButton#dndCard:hover { background:rgba(255,255,255,22); }"
        "QPushButton#dndCard[on=\"true\"] { background:rgba(139,92,246,48); border:1px solid rgba(167,139,250,150); }"
        "QToolButton#miniTile { background:rgba(255,255,255,13); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                       border-radius:16px; padding:6px 4px; font-size:11.5px; font-weight:600; }"
        "QToolButton#miniTile:hover { background:rgba(255,255,255,24); }"
        "QToolButton#miniTile[accent=\"amber\"] { background:rgba(245,158,11,42); border:1px solid rgba(251,191,36,145); color:#fef3c7; }"
        "QToolButton#miniTile[accent=\"green\"] { background:rgba(16,185,129,40); border:1px solid rgba(52,211,153,145); color:#d1fae5; }"
        "QToolButton#miniTile[accent=\"blue\"]  { background:rgba(59,130,246,38); border:1px solid rgba(96,165,250,140); color:#dbeafe; }"
        "QToolButton#miniTile[accent=\"rose\"]  { background:rgba(244,63,94,42);  border:1px solid rgba(251,113,133,150); color:#ffe4e6; }"
        "QToolButton#muteBtn { background:rgba(255,255,255,16); border:1px solid rgba(255,255,255,22); border-radius:16px; }"
        "QToolButton#muteBtn:hover { background:rgba(255,255,255,28); }"
        "QToolButton#muteBtn[muted=\"true\"] { background:rgba(244,63,94,44); border:1px solid rgba(251,113,133,140); }"
        "QToolButton#mediaBtn { background:rgba(255,255,255,14); border:1px solid rgba(255,255,255,20); border-radius:16px; }"
        "QToolButton#mediaBtn:hover { background:rgba(255,255,255,28); }"
        "QToolButton#footPill { background:rgba(255,255,255,12); color:#e2e8f0; border:1px solid rgba(255,255,255,20);"
        "                       border-radius:14px; padding:7px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#footPill:hover { background:rgba(255,255,255,24); }"
        "QToolButton#dangerPill { background:rgba(244,63,94,28); color:#fecdd3; border:1px solid rgba(251,113,133,70);"
        "                         border-radius:14px; padding:7px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#dangerPill:hover { background:rgba(244,63,94,48); }"
        "QToolButton#tabBtn { background:rgba(255,255,255,12); color:#cbd5e1; border:1px solid rgba(255,255,255,18);"
        "                     border-radius:12px; padding:6px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#tabBtn:hover { background:rgba(255,255,255,24); }"
        "QToolButton#tabBtn[active=\"true\"] { background:rgba(59,130,246,165); color:#ffffff; border:1px solid rgba(147,197,253,190); }"
        "QToolButton#powerBtn { background:rgba(255,255,255,14); color:#e2e8f0; border:1px solid rgba(255,255,255,24);"
        "                       border-radius:12px; padding:6px 12px; font-size:12px; font-weight:600; }"
        "QToolButton#powerBtn[on=\"true\"] { background:rgba(16,185,129,48); color:#ecfdf5; border:1px solid rgba(52,211,153,165); }"
        "QToolButton#modeBtn { background:rgba(255,255,255,12); color:#cbd5e1; border:1px solid rgba(255,255,255,18);"
        "                      border-radius:11px; padding:6px 8px; font-size:12px; font-weight:600; }"
        "QToolButton#modeBtn[active=\"true\"] { background:rgba(56,189,248,55); color:#f0f9ff; border:1px solid rgba(125,211,252,185); }"
        "QPushButton#rowBtn { background:rgba(255,255,255,10); color:#f1f5f9; border:1px solid rgba(255,255,255,16);"
        "                     border-radius:13px; padding:10px 12px; text-align:left; font-size:12.5px; font-weight:500; }"
        "QPushButton#rowBtn:hover { background:rgba(255,255,255,22); }"
        "QPushButton#rowBtn[connected=\"true\"] { background:rgba(56,189,248,34); border:1px solid rgba(125,211,252,135); color:#ffffff; }"
        "QToolButton#footBtn { background:rgba(255,255,255,10); color:#cbd5e1; border:1px solid rgba(255,255,255,18);"
        "                      border-radius:12px; padding:8px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#footBtn:hover { background:rgba(255,255,255,22); color:#ffffff; }");

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    // ---- Shared Top Draggable Header ----
    auto *header = new PopoverDragHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hdrRow = new QHBoxLayout(header);
    hdrRow->setContentsMargins(0, 0, 0, 0);
    hdrRow->setSpacing(8);

    m_backBtn = new QToolButton(header);
    m_backBtn->setObjectName(QStringLiteral("backPill"));
    m_backBtn->setIcon(FloatingIcons::make(FloatingIcons::ChevronLeft, QColor("#e2e8f0"), 14));
    m_backBtn->setText(QStringLiteral("控制中心"));
    m_backBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->hide();
    connect(m_backBtn, &QToolButton::clicked, this, [this] { showPage(PageHome); });
    hdrRow->addWidget(m_backBtn);

    m_titleLbl = new QLabel(QStringLiteral("控制中心"), header);
    m_titleLbl->setObjectName(QStringLiteral("ccTitle"));
    m_titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    hdrRow->addWidget(m_titleLbl);

    hdrRow->addStretch(1);

    m_metaLbl = new QLabel(header);
    m_metaLbl->setObjectName(QStringLiteral("ccMeta"));
    m_metaLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    hdrRow->addWidget(m_metaLbl);

    m_refreshBtn = new QToolButton(header);
    m_refreshBtn->setObjectName(QStringLiteral("hdrIconBtn"));
    m_refreshBtn->setText(QStringLiteral("↻"));
    m_refreshBtn->setToolTip(QStringLiteral("刷新实时状态"));
    m_refreshBtn->setFixedSize(30, 30);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QToolButton::clicked, this, [this] {
        if (m_currentPage == PageWifi) refreshWifi(true);
        else refreshCurrentPage();
    });
    hdrRow->addWidget(m_refreshBtn);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("hdrIconBtn"));
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#cbd5e1"), 14));
    closeBtn->setFixedSize(30, 30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hdrRow->addWidget(closeBtn);
    root->addWidget(header);

    // ---- Inline Action Status Banner ----
    m_statusBanner = new QLabel(card);
    m_statusBanner->setWordWrap(true);
    m_statusBanner->hide();
    root->addWidget(m_statusBanner);

    // ---- Stacked Pages (0 = Bento Home, 1 = Wi-Fi, 2 = Bluetooth, 3 = Mihomo) ----
    m_stack = new QStackedWidget(card);
    m_stack->addWidget(buildHomePage(m_stack));
    m_stack->addWidget(buildWifiPage(m_stack));
    m_stack->addWidget(buildBluetoothPage(m_stack));
    m_stack->addWidget(buildMihomoPage(m_stack));
    root->addWidget(m_stack, 1);

    if (m_monitor) {
        connect(m_monitor, &SystemStatusMonitor::statusChanged, this, [this](const SystemStatusSnapshot &) {
            syncHomeFromSnapshot();
        });
    }

    FloatingWindow::prepare(this, true);
    syncHomeFromSnapshot();
}

// ============================================================================
// Page 0: macOS / iOS Bento Control Center Home
// ============================================================================

QWidget *SystemPopover::buildHomePage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    // ---- Upper Bento Row: Left Connectivity Card + Right Toggles Column ----
    auto *upper = new QHBoxLayout();
    upper->setSpacing(10);

    // Left Bento Card: Wi-Fi / Bluetooth / Mihomo
    auto *connCard = new QWidget(page);
    connCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *connCol = new QVBoxLayout(connCard);
    connCol->setContentsMargins(8, 8, 8, 8);
    connCol->setSpacing(4);

    auto makeConnRow = [&](const QString &title, const QString &initSub, FloatingIcons::Icon icon,
                           const QColor &badgeBg, QLabel **outBadge, QLabel **outSub, Page targetPage) {
        auto *rowBtn = new QPushButton(connCard);
        rowBtn->setObjectName(QStringLiteral("connRow"));
        rowBtn->setCursor(Qt::PointingHandCursor);
        rowBtn->setMinimumHeight(52);
        auto *hl = new QHBoxLayout(rowBtn);
        hl->setContentsMargins(6, 5, 6, 5);
        hl->setSpacing(9);

        auto *badge = new QLabel(rowBtn);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        styleCircleBadge(badge, icon, badgeBg, QColor("#ffffff"), 34);
        hl->addWidget(badge);

        auto *textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(1);
        auto *tLbl = new QLabel(title, rowBtn);
        tLbl->setObjectName(QStringLiteral("rowTitle"));
        tLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *sLbl = new QLabel(initSub, rowBtn);
        sLbl->setObjectName(QStringLiteral("rowSub"));
        sLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        textCol->addWidget(tLbl);
        textCol->addWidget(sLbl);
        hl->addLayout(textCol, 1);

        auto *chev = new QLabel(rowBtn);
        chev->setPixmap(FloatingIcons::make(FloatingIcons::ChevronRight, QColor("#64748b"), 14).pixmap(14, 14));
        chev->setAttribute(Qt::WA_TransparentForMouseEvents);
        hl->addWidget(chev);

        connect(rowBtn, &QPushButton::clicked, this, [this, targetPage] { showPage(targetPage); });
        if (outBadge) *outBadge = badge;
        if (outSub) *outSub = sLbl;
        return rowBtn;
    };

    connCol->addWidget(makeConnRow(QStringLiteral("Wi-Fi"), QStringLiteral("检测中…"),
                                   FloatingIcons::Wifi, QColor("#2563eb"),
                                   &m_homeWifiBadge, &m_homeWifiSub, PageWifi));
    connCol->addWidget(makeConnRow(QStringLiteral("蓝牙"), QStringLiteral("检测中…"),
                                   FloatingIcons::Bluetooth, QColor("#3b82f6"),
                                   &m_homeBtBadge, &m_homeBtSub, PageBluetooth));
    connCol->addWidget(makeConnRow(QStringLiteral("Mihomo"), QStringLiteral("RULE · 检测中"),
                                   FloatingIcons::Proxy, QColor("#10b981"),
                                   &m_homeMhBadge, &m_homeMhSub, PageMihomo));
    upper->addWidget(connCard, 11);

    // Right Bento Column: DND wide pill + 2x2 quick tiles
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(8);

    // DND Wide Card
    m_dndTile = new QPushButton(page);
    m_dndTile->setObjectName(QStringLiteral("dndCard"));
    m_dndTile->setCursor(Qt::PointingHandCursor);
    m_dndTile->setMinimumHeight(54);
    auto *dndRow = new QHBoxLayout(m_dndTile);
    dndRow->setContentsMargins(10, 6, 10, 6);
    dndRow->setSpacing(9);
    m_dndBadge = new QLabel(m_dndTile);
    m_dndBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    styleCircleBadge(m_dndBadge, FloatingIcons::Bell, QColor(255, 255, 255, 26), QColor("#cbd5e1"), 32);
    dndRow->addWidget(m_dndBadge);
    auto *dndText = new QVBoxLayout();
    dndText->setContentsMargins(0, 0, 0, 0);
    dndText->setSpacing(1);
    auto *dndTitle = new QLabel(QStringLiteral("勿扰模式"), m_dndTile);
    dndTitle->setObjectName(QStringLiteral("rowTitle"));
    dndTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_dndSub = new QLabel(QStringLiteral("已关闭"), m_dndTile);
    m_dndSub->setObjectName(QStringLiteral("rowSub"));
    m_dndSub->setAttribute(Qt::WA_TransparentForMouseEvents);
    dndText->addWidget(dndTitle);
    dndText->addWidget(m_dndSub);
    dndRow->addLayout(dndText, 1);
    connect(m_dndTile, &QPushButton::clicked, this, [this] {
        if (!m_monitor) return;
        showStatusBanner(m_monitor->toggleDoNotDisturb());
        syncHomeFromSnapshot();
    });
    rightCol->addWidget(m_dndTile);

    // 2x2 Square Tiles
    auto *grid = new QGridLayout();
    grid->setSpacing(8);
    auto makeMini = [&](const QString &label, FloatingIcons::Icon icon, const QColor &ink) {
        auto *b = new QToolButton(page);
        b->setObjectName(QStringLiteral("miniTile"));
        b->setText(label);
        b->setIcon(FloatingIcons::make(icon, ink, 19));
        b->setIconSize(QSize(19, 19));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        b->setMinimumHeight(54);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    m_nightTile = makeMini(QStringLiteral("护眼"), FloatingIcons::Moon, QColor("#fbbf24"));
    connect(m_nightTile, &QToolButton::clicked, this, [this] {
        if (!m_monitor) return;
        showStatusBanner(m_monitor->toggleNightLight());
        syncHomeFromSnapshot();
    });

    m_powerTile = makeMini(QStringLiteral("平衡"), FloatingIcons::Bolt, QColor("#38bdf8"));
    connect(m_powerTile, &QToolButton::clicked, this, [this] {
        if (!m_monitor) return;
        showStatusBanner(m_monitor->cyclePowerProfile());
        syncHomeFromSnapshot();
    });

    auto *dispTile = makeMini(QStringLiteral("多屏"), FloatingIcons::Desktop, QColor("#93c5fd"));
    connect(dispTile, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openKdeModule(QStringLiteral("kcm_kscreen"));
    });

    auto *cfgTile = makeMini(QStringLiteral("设置"), FloatingIcons::Settings, QColor("#cbd5e1"));
    connect(cfgTile, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openKdeModule(QString());
    });

    grid->addWidget(m_nightTile, 0, 0);
    grid->addWidget(m_powerTile, 0, 1);
    grid->addWidget(dispTile, 1, 0);
    grid->addWidget(cfgTile, 1, 1);
    rightCol->addLayout(grid, 1);

    upper->addLayout(rightCol, 10);
    v->addLayout(upper);

    // ---- Middle Bento Card: Thick Apple-style Brightness & Volume Capsule Sliders ----
    auto *sliderCard = new QWidget(page);
    sliderCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *sv = new QVBoxLayout(sliderCard);
    sv->setContentsMargins(12, 10, 12, 12);
    sv->setSpacing(10);

    // Brightness block
    m_brightnessBlock = new QWidget(sliderCard);
    auto *bv = new QVBoxLayout(m_brightnessBlock);
    bv->setContentsMargins(0, 0, 0, 0);
    bv->setSpacing(5);
    auto *bHead = new QHBoxLayout();
    bHead->setContentsMargins(2, 0, 2, 0);
    auto *bTag = new QLabel(QStringLiteral("显示器亮度"), m_brightnessBlock);
    bTag->setObjectName(QStringLiteral("sliderTag"));
    m_brightnessValLbl = new QLabel(QStringLiteral("70%"), m_brightnessBlock);
    m_brightnessValLbl->setObjectName(QStringLiteral("sliderVal"));
    bHead->addWidget(bTag);
    bHead->addStretch(1);
    bHead->addWidget(m_brightnessValLbl);
    bv->addLayout(bHead);

    auto *bRow = new QHBoxLayout();
    bRow->setSpacing(8);
    auto *bIcon = new QLabel(m_brightnessBlock);
    styleCircleBadge(bIcon, FloatingIcons::Sun, QColor(255, 255, 255, 16), QColor("#fcd34d"), 32);
    bRow->addWidget(bIcon);

    m_brightnessSlider = new CapsuleSlider(QColor("#f8fafc"), m_brightnessBlock);
    m_brightnessSlider->setRange(1, 100);
    m_brightnessSlider->setValue(70);
    bRow->addWidget(m_brightnessSlider, 1);
    bv->addLayout(bRow);

    m_brightnessDebounce = new QTimer(this);
    m_brightnessDebounce->setSingleShot(true);
    m_brightnessDebounce->setInterval(75);
    m_brightnessSlider->onValueChanged = [this](int val) {
        m_brightnessValLbl->setText(QStringLiteral("%1%").arg(val));
        if (m_monitor) m_monitor->updateBrightness(val, true);
        emit brightnessStateChanged(val);
        m_brightnessDebounce->start();
    };
    connect(m_brightnessDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setBrightness(m_brightnessSlider->value());
    });
    sv->addWidget(m_brightnessBlock);

    // Volume block
    auto *vHead = new QHBoxLayout();
    vHead->setContentsMargins(2, 0, 2, 0);
    auto *vTag = new QLabel(QStringLiteral("声音音量"), sliderCard);
    vTag->setObjectName(QStringLiteral("sliderTag"));
    m_volumeValLbl = new QLabel(QStringLiteral("60%"), sliderCard);
    m_volumeValLbl->setObjectName(QStringLiteral("sliderVal"));
    vHead->addWidget(vTag);
    vHead->addStretch(1);
    vHead->addWidget(m_volumeValLbl);
    sv->addLayout(vHead);

    auto *vRow = new QHBoxLayout();
    vRow->setSpacing(8);
    m_muteBtn = new QToolButton(sliderCard);
    m_muteBtn->setObjectName(QStringLiteral("muteBtn"));
    m_muteBtn->setFixedSize(32, 32);
    m_muteBtn->setIcon(FloatingIcons::make(FloatingIcons::Volume, QColor("#e2e8f0"), 17));
    m_muteBtn->setCursor(Qt::PointingHandCursor);
    connect(m_muteBtn, &QToolButton::clicked, this, [this] {
        QPointer<SystemPopover> self(this);
        DesktopActions::toggleMute([self](int vol, bool muted, bool ok) {
            if (!self || !ok) return;
            self->applyAudioVisuals(vol, muted, true);
            emit self->audioStateChanged(vol, muted);
        });
    });
    vRow->addWidget(m_muteBtn);

    m_volumeSlider = new CapsuleSlider(QColor("#7cc8ff"), sliderCard);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(60);
    vRow->addWidget(m_volumeSlider, 1);
    sv->addLayout(vRow);

    m_volumeDebounce = new QTimer(this);
    m_volumeDebounce->setSingleShot(true);
    m_volumeDebounce->setInterval(75);
    m_volumeSlider->onValueChanged = [this](int val) {
        m_lastVolume = val;
        if (m_muted && val > 0) m_muted = false;
        applyAudioVisuals(val, m_muted, true);
        emit audioStateChanged(val, m_muted);
        m_volumeDebounce->start();
    };
    connect(m_volumeDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setVolume(m_volumeSlider->value());
    });

    v->addWidget(sliderCard);

    // ---- MPRIS Now Playing Card (Auto-Hidden When Nothing Playing) ----
    m_mprisCard = new QWidget(page);
    m_mprisCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *mRow = new QHBoxLayout(m_mprisCard);
    mRow->setContentsMargins(10, 9, 10, 9);
    mRow->setSpacing(10);

    auto *artBadge = new QLabel(m_mprisCard);
    styleCircleBadge(artBadge, FloatingIcons::Music, QColor(59, 130, 246, 75), QColor("#bae6fd"), 36);
    mRow->addWidget(artBadge);

    auto *mText = new QVBoxLayout();
    mText->setContentsMargins(0, 0, 0, 0);
    mText->setSpacing(1);
    m_mprisTitleLbl = new QLabel(m_mprisCard);
    m_mprisTitleLbl->setObjectName(QStringLiteral("rowTitle"));
    m_mprisArtistLbl = new QLabel(m_mprisCard);
    m_mprisArtistLbl->setObjectName(QStringLiteral("rowSub"));
    mText->addWidget(m_mprisTitleLbl);
    mText->addWidget(m_mprisArtistLbl);
    mRow->addLayout(mText, 1);

    auto makeMediaBtn = [&](FloatingIcons::Icon icon, const QString &method) {
        auto *b = new QToolButton(m_mprisCard);
        b->setObjectName(QStringLiteral("mediaBtn"));
        b->setIcon(FloatingIcons::make(icon, QColor("#f1f5f9"), 15));
        b->setFixedSize(32, 32);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [this, method] { triggerMprisAction(method); });
        return b;
    };
    mRow->addWidget(makeMediaBtn(FloatingIcons::MediaPrev, QStringLiteral("Previous")));
    m_mprisPlayBtn = makeMediaBtn(FloatingIcons::MediaPlay, QStringLiteral("PlayPause"));
    mRow->addWidget(m_mprisPlayBtn);
    mRow->addWidget(makeMediaBtn(FloatingIcons::MediaNext, QStringLiteral("Next")));

    m_mprisCard->hide();
    v->addWidget(m_mprisCard);

    // ---- Bottom Compact Utility & Power Bar ----
    auto *footRow = new QHBoxLayout();
    footRow->setSpacing(8);

    auto *deckBtn = new QToolButton(page);
    deckBtn->setObjectName(QStringLiteral("footPill"));
    deckBtn->setIcon(FloatingIcons::make(FloatingIcons::Deck, QColor("#cbd5e1"), 15));
    deckBtn->setText(QStringLiteral(" 触控甲板"));
    deckBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    deckBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    deckBtn->setMinimumHeight(36);
    deckBtn->setCursor(Qt::PointingHandCursor);
    connect(deckBtn, &QToolButton::clicked, this, [this] {
        hide();
        emit quickDeckRequested();
    });

    auto *lockBtn = new QToolButton(page);
    lockBtn->setObjectName(QStringLiteral("footPill"));
    lockBtn->setIcon(FloatingIcons::make(FloatingIcons::Lock, QColor("#e2e8f0"), 15));
    lockBtn->setText(QStringLiteral(" 锁屏"));
    lockBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    lockBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    lockBtn->setMinimumHeight(36);
    lockBtn->setCursor(Qt::PointingHandCursor);
    connect(lockBtn, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->lockScreen();
    });

    auto *pwrBtn = new QToolButton(page);
    pwrBtn->setObjectName(QStringLiteral("dangerPill"));
    pwrBtn->setIcon(FloatingIcons::make(FloatingIcons::Power, QColor("#fda4af"), 15));
    pwrBtn->setText(QStringLiteral(" 关机"));
    pwrBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    pwrBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pwrBtn->setMinimumHeight(36);
    pwrBtn->setCursor(Qt::PointingHandCursor);
    connect(pwrBtn, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openPowerMenu();
    });

    footRow->addWidget(deckBtn);
    footRow->addWidget(lockBtn);
    footRow->addWidget(pwrBtn);
    v->addLayout(footRow);

    return page;
}

// ============================================================================
// Subpages 1..3: Wi-Fi / Bluetooth / Mihomo (Inline Drill-Down)
// ============================================================================

QWidget *SystemPopover::buildWifiPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    auto *bar = new QHBoxLayout();
    m_wifiSummaryLbl = new QLabel(QStringLiteral("正在读取网络状态…"), page);
    m_wifiSummaryLbl->setObjectName(QStringLiteral("ccSub"));
    m_wifiSummaryLbl->setWordWrap(true);
    bar->addWidget(m_wifiSummaryLbl, 1);

    m_wifiPowerBtn = new QToolButton(page);
    m_wifiPowerBtn->setObjectName(QStringLiteral("powerBtn"));
    m_wifiPowerBtn->setText(QStringLiteral("Wi-Fi 已开"));
    m_wifiPowerBtn->setCursor(Qt::PointingHandCursor);
    connect(m_wifiPowerBtn, &QToolButton::clicked, this, &SystemPopover::toggleWifiRadio);
    bar->addWidget(m_wifiPowerBtn);
    v->addLayout(bar);

    auto *scroll = makeTouchScrollArea(page, &m_wifiListLayout);
    scroll->setMinimumHeight(290);
    v->addWidget(scroll, 1);

    auto *foot = new QToolButton(page);
    foot->setObjectName(QStringLiteral("footBtn"));
    foot->setText(QStringLiteral("打开 KDE 完整网络 / VPN 设置…"));
    foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    foot->setMinimumHeight(36);
    foot->setCursor(Qt::PointingHandCursor);
    connect(foot, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openKdeModule(QStringLiteral("kcm_networkmanagement"));
    });
    v->addWidget(foot);
    return page;
}

QWidget *SystemPopover::buildBluetoothPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    auto *bar = new QHBoxLayout();
    m_btSummaryLbl = new QLabel(QStringLiteral("正在读取蓝牙设备…"), page);
    m_btSummaryLbl->setObjectName(QStringLiteral("ccSub"));
    m_btSummaryLbl->setWordWrap(true);
    bar->addWidget(m_btSummaryLbl, 1);

    m_btPowerBtn = new QToolButton(page);
    m_btPowerBtn->setObjectName(QStringLiteral("powerBtn"));
    m_btPowerBtn->setText(QStringLiteral("蓝牙已开"));
    m_btPowerBtn->setCursor(Qt::PointingHandCursor);
    connect(m_btPowerBtn, &QToolButton::clicked, this, &SystemPopover::toggleBluetoothPower);
    bar->addWidget(m_btPowerBtn);
    v->addLayout(bar);

    auto *scroll = makeTouchScrollArea(page, &m_btListLayout);
    scroll->setMinimumHeight(290);
    v->addWidget(scroll, 1);

    auto *foot = new QToolButton(page);
    foot->setObjectName(QStringLiteral("footBtn"));
    foot->setText(QStringLiteral("配对新设备 / 打开 KDE 蓝牙设置…"));
    foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    foot->setMinimumHeight(36);
    foot->setCursor(Qt::PointingHandCursor);
    connect(foot, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openKdeModule(QStringLiteral("kcm_bluetooth"));
    });
    v->addWidget(foot);
    return page;
}

QWidget *SystemPopover::buildMihomoPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // Mode 3-way pill bar
    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    static const char *kModeIds[3]{"rule", "global", "direct"};
    static const char *kModeLabels[3]{"规则 Rule", "全局 Global", "直连 Direct"};
    for (int i = 0; i < 3; ++i) {
        auto *mb = new QToolButton(page);
        mb->setObjectName(QStringLiteral("modeBtn"));
        mb->setText(QString::fromUtf8(kModeLabels[i]));
        mb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        mb->setMinimumHeight(34);
        mb->setCursor(Qt::PointingHandCursor);
        const QString modeId = QString::fromLatin1(kModeIds[i]);
        connect(mb, &QToolButton::clicked, this, [this, modeId] { setMihomoMode(modeId); });
        modeRow->addWidget(mb);
        m_mhModeBtns[i] = mb;
    }
    v->addLayout(modeRow);

    // TUN toggle + delay test + status summary
    auto *tunRow = new QHBoxLayout();
    tunRow->setSpacing(6);
    m_mhSummaryLbl = new QLabel(QStringLiteral("正在连接 Mihomo API…"), page);
    m_mhSummaryLbl->setObjectName(QStringLiteral("ccSub"));
    tunRow->addWidget(m_mhSummaryLbl, 1);

    m_mhTunBtn = new QToolButton(page);
    m_mhTunBtn->setObjectName(QStringLiteral("powerBtn"));
    m_mhTunBtn->setText(QStringLiteral("TUN 关"));
    m_mhTunBtn->setCursor(Qt::PointingHandCursor);
    connect(m_mhTunBtn, &QToolButton::clicked, this, &SystemPopover::toggleMihomoTun);
    tunRow->addWidget(m_mhTunBtn);

    m_mhDelayBtn = new QToolButton(page);
    m_mhDelayBtn->setObjectName(QStringLiteral("tabBtn"));
    m_mhDelayBtn->setText(QStringLiteral("测速"));
    m_mhDelayBtn->setCursor(Qt::PointingHandCursor);
    connect(m_mhDelayBtn, &QToolButton::clicked, this, [this] {
        if (!m_mhSelectedGroup.isEmpty()) testMihomoGroupDelay(m_mhSelectedGroup);
    });
    tunRow->addWidget(m_mhDelayBtn);
    v->addLayout(tunRow);

    // Group selector pills container
    auto *groupHost = new QWidget(page);
    m_mhGroupBarLayout = new QVBoxLayout(groupHost);
    m_mhGroupBarLayout->setContentsMargins(0, 0, 0, 0);
    m_mhGroupBarLayout->setSpacing(6);
    v->addWidget(groupHost);

    // Node scroll list
    auto *scroll = makeTouchScrollArea(page, &m_mhNodeListLayout);
    scroll->setMinimumHeight(210);
    v->addWidget(scroll, 1);

    auto *foot = new QToolButton(page);
    foot->setObjectName(QStringLiteral("footBtn"));
    foot->setText(QStringLiteral("打开 Mihomo Web 面板 (MetacubeXD)…"));
    foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    foot->setMinimumHeight(36);
    foot->setCursor(Qt::PointingHandCursor);
    connect(foot, &QToolButton::clicked, this, [this] {
        hide();
        QDesktopServices::openUrl(QUrl(mihomoBaseUrl() + QStringLiteral("/ui/")));
    });
    v->addWidget(foot);
    return page;
}

// ============================================================================
// Lifecycle, Reveal, Positioning & Click-Outside Auto-Dismiss
// ============================================================================

void SystemPopover::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_currentPage != PageHome) showPage(PageHome);
        else hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SystemPopover::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        if (isVisible() && m_autoDismissEnabled &&
            QGuiApplication::platformName() != QLatin1String("offscreen") &&
            m_openedTimer.isValid() && m_openedTimer.elapsed() > 260 &&
            !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
            hide();
        }
    }
    QWidget::changeEvent(event);
}

void SystemPopover::positionNear(const QPoint &anchorGlobal)
{
    adjustSize();
    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    setProperty("floatingballAssignedScreen", QVariant::fromValue(screen));
    const QRect b = screen->availableGeometry();
    const int margin = 12;
    const int h = qMax(height(), sizeHint().height());
    int x = (anchorGlobal.x() > b.center().x())
        ? (anchorGlobal.x() - width() - 26)
        : (anchorGlobal.x() + 28);
    int y = anchorGlobal.y() - h / 2;
    x = qBound(b.left() + margin, x, qMax(b.left() + margin, b.right() - width() - margin));
    y = qBound(b.top() + margin, y, qMax(b.top() + margin, b.bottom() - h - margin));
    move(x, y);
}

void SystemPopover::showStatusBanner(const QString &msg, bool isError)
{
    if (msg.isEmpty()) {
        m_statusBanner->hide();
        return;
    }
    m_statusBanner->setStyleSheet(isError ? QStringLiteral("color:#fb7185; font-size:11px; font-weight:600;")
                                          : QStringLiteral("color:#7cc8ff; font-size:11px; font-weight:600;"));
    m_statusBanner->setText(msg);
    m_statusBanner->show();
    QPointer<QLabel> banner(m_statusBanner);
    QTimer::singleShot(2500, this, [banner, msg] {
        if (banner && banner->text() == msg) banner->hide();
    });
}

void SystemPopover::revealHome(const QPoint &anchorGlobal, bool toggle)
{
    revealTab(TabHome, anchorGlobal, toggle);
}

void SystemPopover::revealTab(Tab tab, const QPoint &anchorGlobal, bool toggle)
{
    const Page target = static_cast<Page>(qBound(0, static_cast<int>(tab), 3));
    if (isVisible() && toggle && m_currentPage == target) {
        hide();
        return;
    }
    m_openedTimer.restart();
    showPage(target);
    positionNear(anchorGlobal);
    show();
    raise();
    activateWindow();
}

void SystemPopover::showPage(Page page)
{
    m_currentPage = page;
    m_stack->setCurrentIndex(static_cast<int>(page));
    syncHeaderForPage();
    adjustSize();
    refreshCurrentPage();
}

void SystemPopover::syncHeaderForPage()
{
    const bool isHome = (m_currentPage == PageHome);
    m_backBtn->setVisible(!isHome);
    m_metaLbl->setVisible(isHome);
    if (m_currentPage == PageHome) m_titleLbl->setText(QStringLiteral("控制中心"));
    else if (m_currentPage == PageWifi) m_titleLbl->setText(QStringLiteral("Wi-Fi 网络"));
    else if (m_currentPage == PageBluetooth) m_titleLbl->setText(QStringLiteral("蓝牙设备"));
    else m_titleLbl->setText(QStringLiteral("Mihomo 代理"));
}

void SystemPopover::refreshCurrentPage()
{
    syncHomeFromSnapshot();
    if (m_currentPage == PageHome) {
        syncVolume();
        syncBrightness();
        refreshMpris();
        // Prefetch lightweight connectivity summaries so all 3 Bento rows stay live!
        refreshWifi(false);
        refreshBluetooth();
        refreshMihomo();
    } else if (m_currentPage == PageWifi) {
        refreshWifi(false);
    } else if (m_currentPage == PageBluetooth) {
        refreshBluetooth();
    } else {
        refreshMihomo();
    }
}

void SystemPopover::syncHomeFromSnapshot()
{
    const QString clock = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (!m_monitor) {
        m_metaLbl->setText(clock);
        return;
    }
    const SystemStatusSnapshot &st = m_monitor->snapshot();
    QStringList meta;
    if (st.batteryPresent)
        meta.append(QStringLiteral("%1%%2").arg(st.batteryPercent).arg(st.batteryPluggedIn ? QStringLiteral("⚡") : QString()));
    meta.append(clock);
    m_metaLbl->setText(meta.join(QStringLiteral(" · ")));

    // Home Wi-Fi row
    const bool wifiOn = st.networkConnected || m_wifiRadioEnabled;
    styleCircleBadge(m_homeWifiBadge, FloatingIcons::Wifi,
                     st.networkConnected ? QColor("#2563eb") : QColor(255, 255, 255, 26),
                     st.networkConnected ? QColor("#ffffff") : QColor("#94a3b8"), 34);
    if (!m_cachedWifiSsid.isEmpty() && m_wifiRadioEnabled) {
        m_homeWifiSub->setText(elideText(QStringLiteral("%1 · %2%").arg(m_cachedWifiSsid).arg(m_cachedWifiSignal), 18));
    } else if (st.networkConnected) {
        m_homeWifiSub->setText(elideText(st.networkText.isEmpty() ? QStringLiteral("已连接") : st.networkText, 18));
    } else {
        m_homeWifiSub->setText(wifiOn ? QStringLiteral("未连接") : QStringLiteral("已关闭"));
    }

    // Home Bluetooth row
    const bool btOn = st.bluetoothPowered || m_btPowered;
    styleCircleBadge(m_homeBtBadge, FloatingIcons::Bluetooth,
                     btOn ? QColor("#3b82f6") : QColor(255, 255, 255, 26),
                     btOn ? QColor("#ffffff") : QColor("#94a3b8"), 34);
    if (!btOn) m_homeBtSub->setText(QStringLiteral("已关闭"));
    else if (!m_cachedBtSummary.isEmpty()) m_homeBtSub->setText(elideText(m_cachedBtSummary, 18));
    else m_homeBtSub->setText(QStringLiteral("已开启"));

    // Home Mihomo row
    const QColor mhBg = !m_mhReachable ? QColor(255, 255, 255, 26)
                        : (m_mhMode == QLatin1String("direct") ? QColor("#f59e0b") : QColor("#10b981"));
    styleCircleBadge(m_homeMhBadge, FloatingIcons::Proxy, mhBg,
                     m_mhReachable ? QColor("#ffffff") : QColor("#94a3b8"), 34);
    if (m_mhReachable) {
        const QString tunTxt = m_mhTunEnabled ? QStringLiteral("TUN 开") : QStringLiteral("无TUN");
        m_homeMhSub->setText(QStringLiteral("%1 · %2").arg(m_mhMode.toUpper(), tunTxt));
    } else {
        m_homeMhSub->setText(QStringLiteral("本地未连接"));
    }

    // DND Wide Card
    m_dndTile->setProperty("on", st.dndEnabled);
    repolish(m_dndTile);
    styleCircleBadge(m_dndBadge, FloatingIcons::Bell,
                     st.dndEnabled ? QColor("#8b5cf6") : QColor(255, 255, 255, 24),
                     st.dndEnabled ? QColor("#ffffff") : QColor("#cbd5e1"), 32);
    m_dndSub->setText(st.dndEnabled ? QStringLiteral("已开启") : QStringLiteral("已关闭"));

    // Night Light mini tile
    m_nightTile->setProperty("accent", st.nightLightEnabled ? QStringLiteral("amber") : QString());
    m_nightTile->setText(st.nightLightEnabled ? QStringLiteral("护眼开") : QStringLiteral("护眼"));
    repolish(m_nightTile);

    // Power Profile mini tile
    QString pwrTxt = QStringLiteral("平衡");
    QString pwrAcc = QStringLiteral("blue");
    QColor pwrInk("#7cc8ff");
    if (st.powerProfile == QLatin1String("power-saver")) {
        pwrTxt = QStringLiteral("节能");
        pwrAcc = QStringLiteral("green");
        pwrInk = QColor("#34d399");
    } else if (st.powerProfile == QLatin1String("performance")) {
        pwrTxt = QStringLiteral("性能");
        pwrAcc = QStringLiteral("rose");
        pwrInk = QColor("#fb7185");
    }
    m_powerTile->setText(pwrTxt);
    m_powerTile->setProperty("accent", pwrAcc);
    m_powerTile->setIcon(FloatingIcons::make(FloatingIcons::Bolt, pwrInk, 19));
    repolish(m_powerTile);
}

// ============================================================================
// Audio & Brightness Capsule Sliders
// ============================================================================

void SystemPopover::applyAudioVisuals(int percent, bool muted, bool supported)
{
    m_volumeSlider->setEnabled(supported);
    m_muteBtn->setEnabled(supported);
    if (!supported) {
        m_volumeValLbl->setText(QStringLiteral("--"));
        return;
    }
    m_muted = muted;
    m_lastVolume = qBound(0, percent, 100);
    m_volumeSlider->setValue(m_lastVolume);
    m_volumeSlider->setMutedAppearance(muted);
    m_muteBtn->setProperty("muted", muted);
    repolish(m_muteBtn);
    m_muteBtn->setIcon(FloatingIcons::make(
        muted ? FloatingIcons::VolumeMute : FloatingIcons::Volume,
        muted ? QColor("#fda4af") : QColor("#e2e8f0"), 17));
    m_volumeValLbl->setText(muted ? QStringLiteral("静音") : QStringLiteral("%1%").arg(m_lastVolume));
}

void SystemPopover::syncVolume()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    QPointer<SystemPopover> self(this);
    DesktopActions::queryVolume([self](int vol, bool muted, bool ok) {
        if (!self) return;
        self->applyAudioVisuals(vol, muted, ok);
        if (ok) emit self->audioStateChanged(vol, muted);
    });
}

void SystemPopover::syncBrightness()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    QPointer<SystemPopover> self(this);
    DesktopActions::queryBrightness([self](int pct, bool ok) {
        if (!self || !self->m_brightnessBlock) return;
        self->m_brightnessBlock->setVisible(ok);
        if (ok) {
            const int clamped = qBound(1, pct, 100);
            self->m_brightnessSlider->setValue(clamped);
            self->m_brightnessValLbl->setText(QStringLiteral("%1%").arg(clamped));
            emit self->brightnessStateChanged(clamped);
        }
        self->adjustSize();
    });
}

// ============================================================================
// MPRIS Now Playing (Linux Session DBus, Auto-Hidden When Idle)
// ============================================================================

void SystemPopover::setMprisPreviewForTest(const QString &title, const QString &artist, bool playing)
{
    m_mprisTitle = title;
    m_mprisArtist = artist;
    m_mprisPlaying = playing;
    m_mprisVisible = !title.isEmpty();
    applyMprisVisuals();
}

void SystemPopover::applyMprisVisuals()
{
    if (!m_mprisCard) return;
    m_mprisCard->setVisible(m_mprisVisible);
    if (m_mprisVisible) {
        m_mprisTitleLbl->setText(elideText(m_mprisTitle, 22));
        m_mprisArtistLbl->setText(elideText(m_mprisArtist.isEmpty() ? QStringLiteral("正在播放") : m_mprisArtist, 26));
        m_mprisPlayBtn->setIcon(FloatingIcons::make(
            m_mprisPlaying ? FloatingIcons::MediaPause : FloatingIcons::MediaPlay,
            QColor("#ffffff"), 15));
    }
    adjustSize();
}

void SystemPopover::refreshMpris()
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    auto bus = QDBusConnection::sessionBus();
    auto listMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ListNames"));
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(listMsg, 800), this);
    QPointer<SystemPopover> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusPendingReply<QStringList> rep = *w;
        w->deleteLater();
        if (!self || rep.isError()) return;

        QStringList players;
        for (const QString &name : rep.value()) {
            if (name.startsWith(QLatin1String("org.mpris.MediaPlayer2.")))
                players.append(name);
        }
        if (players.isEmpty()) {
            self->m_mprisService.clear();
            self->m_mprisVisible = false;
            self->applyMprisVisuals();
            return;
        }

        // Probe players asynchronously and pick Playing over Paused
        struct ProbeState {
            int remaining = 0;
            QString bestService;
            QString bestTitle;
            QString bestArtist;
            bool bestPlaying = false;
            int bestScore = 0;
        };
        auto state = std::make_shared<ProbeState>();
        state->remaining = players.size();

        auto bus = QDBusConnection::sessionBus();
        for (const QString &svc : players) {
            auto propMsg = QDBusMessage::createMethodCall(
                svc, QStringLiteral("/org/mpris/MediaPlayer2"),
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
            propMsg << QStringLiteral("org.mpris.MediaPlayer2.Player");
            auto *pw = new QDBusPendingCallWatcher(bus.asyncCall(propMsg, 800), self);
            connect(pw, &QDBusPendingCallWatcher::finished, self, [self, pw, svc, state] {
                const QDBusPendingReply<QVariantMap> r = *pw;
                pw->deleteLater();
                if (!self) return;
                if (!r.isError()) {
                    const QVariantMap props = r.value();
                    const QString status = props.value(QStringLiteral("PlaybackStatus")).toString();
                    QVariantMap meta;
                    const QVariant metaVar = props.value(QStringLiteral("Metadata"));
                    if (metaVar.canConvert<QVariantMap>()) {
                        meta = metaVar.toMap();
                    } else if (metaVar.canConvert<QDBusArgument>()) {
                        const QDBusArgument arg = metaVar.value<QDBusArgument>();
                        arg >> meta;
                    }
                    const QString title = meta.value(QStringLiteral("xesam:title")).toString().trimmed();
                    const QStringList artists = meta.value(QStringLiteral("xesam:artist")).toStringList();
                    QString artist = artists.join(QStringLiteral(", ")).trimmed();
                    if (artist.isEmpty()) {
                        artist = svc.section(QLatin1Char('.'), 3, 3);
                    }
                    const bool playing = (status == QLatin1String("Playing"));
                    const bool paused = (status == QLatin1String("Paused"));
                    int score = 0;
                    if (playing && !title.isEmpty()) score = 4;
                    else if (playing) score = 3;
                    else if (paused && !title.isEmpty()) score = 2;
                    if (score > state->bestScore) {
                        state->bestScore = score;
                        state->bestService = svc;
                        state->bestTitle = title.isEmpty() ? artist : title;
                        state->bestArtist = artist;
                        state->bestPlaying = playing;
                    }
                }
                if (--state->remaining == 0) {
                    if (state->bestScore > 0) {
                        self->m_mprisService = state->bestService;
                        self->m_mprisTitle = state->bestTitle;
                        self->m_mprisArtist = state->bestArtist;
                        self->m_mprisPlaying = state->bestPlaying;
                        self->m_mprisVisible = true;
                    } else {
                        self->m_mprisService.clear();
                        self->m_mprisVisible = false;
                    }
                    self->applyMprisVisuals();
                }
            });
        }
    });
#endif
}

void SystemPopover::triggerMprisAction(const QString &method)
{
#ifdef Q_OS_LINUX
    if (m_mprisService.isEmpty()) return;
    auto msg = QDBusMessage::createMethodCall(
        m_mprisService, QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.mpris.MediaPlayer2.Player"), method);
    QDBusConnection::sessionBus().asyncCall(msg, 600);
    if (method == QLatin1String("PlayPause")) {
        m_mprisPlaying = !m_mprisPlaying;
        applyMprisVisuals();
    }
    QPointer<SystemPopover> self(this);
    QTimer::singleShot(320, this, [self] {
        if (self) self->refreshMpris();
    });
#else
    Q_UNUSED(method);
#endif
}

// ============================================================================
// Wi-Fi Subpage Implementation (nmcli async — zero GUI blocking)
// ============================================================================

void SystemPopover::refreshWifi(bool rescan)
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) {
        m_wifiSummaryLbl->setText(QStringLiteral("未找到 nmcli"));
        return;
    }
    if (rescan) showStatusBanner(QStringLiteral("正在扫描附近 Wi-Fi…"));
    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self) return;

        bool wifiEnabled = true;
        QStringList vpnLinks;
        QList<WifiEntry> entries;
        QHash<QString, int> seenIndex;
        int section = 0;
        const QStringList lines = out.split(QLatin1Char('\n'));
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line == QLatin1String("---ACTIVE---")) { section = 1; continue; }
            if (line == QLatin1String("---WIFI---")) { section = 2; continue; }
            if (line.isEmpty()) continue;

            if (section == 0) {
                wifiEnabled = line.compare(QStringLiteral("enabled"), Qt::CaseInsensitive) == 0;
            } else if (section == 1) {
                const QStringList p = line.split(QLatin1Char(':'));
                if (p.size() >= 2 && (p[1] == QLatin1String("tun") || p[1] == QLatin1String("vpn") ||
                                      p[1] == QLatin1String("wireguard"))) {
                    if (!vpnLinks.contains(p[0])) vpnLinks.append(p[0]);
                }
            } else if (section == 2) {
                QStringList parts;
                QString cur;
                for (int i = 0; i < line.size(); ++i) {
                    if (line[i] == QLatin1Char('\\') && i + 1 < line.size() && line[i + 1] == QLatin1Char(':')) {
                        cur.append(QLatin1Char(':'));
                        ++i;
                    } else if (line[i] == QLatin1Char(':')) {
                        parts.append(cur);
                        cur.clear();
                    } else {
                        cur.append(line[i]);
                    }
                }
                parts.append(cur);
                if (parts.size() < 4) continue;
                const QString ssid = parts[1].trimmed();
                if (ssid.isEmpty()) continue;
                WifiEntry e;
                e.active = (parts[0].trimmed() == QLatin1String("*"));
                e.ssid = ssid;
                e.signal = parts[2].toInt();
                e.security = parts[3].trimmed();
                if (seenIndex.contains(ssid)) {
                    WifiEntry &ex = entries[seenIndex[ssid]];
                    if (e.active) ex.active = true;
                    ex.signal = qMax(ex.signal, e.signal);
                } else {
                    seenIndex.insert(ssid, entries.size());
                    entries.append(e);
                }
            }
        }
        std::sort(entries.begin(), entries.end(), [](const WifiEntry &a, const WifiEntry &b) {
            if (a.active != b.active) return a.active;
            return a.signal > b.signal;
        });
        self->rebuildWifiList(wifiEnabled, vpnLinks, entries);
    });
    const QString rescanFlag = rescan ? QStringLiteral("yes") : QStringLiteral("no");
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral("nmcli radio wifi; echo '---ACTIVE---'; "
                                "nmcli -t -f NAME,TYPE connection show --active; echo '---WIFI---'; "
                                "nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan %1").arg(rescanFlag)});
#else
    Q_UNUSED(rescan);
#endif
}

void SystemPopover::rebuildWifiList(bool wifiEnabled, const QStringList &vpnLinks, const QList<WifiEntry> &entries)
{
    m_wifiRadioEnabled = wifiEnabled;
    m_wifiPowerBtn->setText(wifiEnabled ? QStringLiteral("Wi-Fi 已开") : QStringLiteral("Wi-Fi 已关"));
    m_wifiPowerBtn->setProperty("on", wifiEnabled);
    repolish(m_wifiPowerBtn);

    m_cachedWifiSsid.clear();
    m_cachedWifiSignal = 0;
    for (const WifiEntry &e : entries) {
        if (e.active) {
            m_cachedWifiSsid = e.ssid;
            m_cachedWifiSignal = e.signal;
            break;
        }
    }
    syncHomeFromSnapshot();

    QString summary = !wifiEnabled ? QStringLiteral("无线开关已关闭")
        : (m_cachedWifiSsid.isEmpty() ? QStringLiteral("未连接 Wi-Fi") : QStringLiteral("已连 %1").arg(m_cachedWifiSsid));
    if (!vpnLinks.isEmpty()) summary += QStringLiteral(" · 隧道: ") + vpnLinks.join(QStringLiteral(", "));
    m_wifiSummaryLbl->setText(summary);

    clearLayout(m_wifiListLayout);
    if (!wifiEnabled) {
        auto *lbl = new QLabel(QStringLiteral("点击右上角「Wi-Fi 已关」可重新开启无线网络"), this);
        lbl->setAlignment(Qt::AlignCenter);
        m_wifiListLayout->addWidget(lbl);
        m_wifiListLayout->addStretch(1);
        return;
    }
    if (entries.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("附近暂未发现 Wi-Fi 热点（点右上角 ↻ 扫描）"), this);
        lbl->setAlignment(Qt::AlignCenter);
        m_wifiListLayout->addWidget(lbl);
        m_wifiListLayout->addStretch(1);
        return;
    }

    const int limit = qMin(entries.size(), 14);
    for (int i = 0; i < limit; ++i) {
        const WifiEntry entry = entries[i];
        auto *btn = new QPushButton(this);
        btn->setObjectName(QStringLiteral("rowBtn"));
        btn->setProperty("connected", entry.active);
        btn->setMinimumHeight(46);
        const QString lock = (!entry.security.isEmpty() && entry.security != QLatin1String("--"))
            ? QStringLiteral("🔒 ") : QString();
        const QString rightTag = entry.active
            ? QStringLiteral("已连接 · %1%").arg(entry.signal)
            : QStringLiteral("%1%").arg(entry.signal);
        btn->setText(QStringLiteral("%1%2   [%3]").arg(lock, entry.ssid, rightTag));
        connect(btn, &QPushButton::clicked, this, [this, entry] { connectWifi(entry); });
        m_wifiListLayout->addWidget(btn);
    }
    m_wifiListLayout->addStretch(1);
}

void SystemPopover::toggleWifiRadio()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    const bool turnOn = !m_wifiRadioEnabled;
    QProcess::startDetached(nm, {QStringLiteral("radio"), QStringLiteral("wifi"),
                                 turnOn ? QStringLiteral("on") : QStringLiteral("off")});
    showStatusBanner(turnOn ? QStringLiteral("正在开启 Wi-Fi…") : QStringLiteral("已关闭 Wi-Fi"));
    QTimer::singleShot(1100, this, [this] { refreshWifi(false); });
#endif
}

void SystemPopover::connectWifi(const WifiEntry &entry)
{
#ifdef Q_OS_LINUX
    if (entry.active) {
        showStatusBanner(QStringLiteral("当前已连接 %1").arg(entry.ssid));
        return;
    }
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    showStatusBanner(QStringLiteral("正在连接 %1…").arg(entry.ssid));
    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc, entry](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        if (!self) return;
        if (st == QProcess::NormalExit && code == 0) {
            self->showStatusBanner(QStringLiteral("✓ 已连接到 %1").arg(entry.ssid));
            self->refreshWifi(false);
        } else {
            self->showStatusBanner(QStringLiteral("需输入密码，正在打开 KDE 网络设置…"), true);
            if (self->m_monitor) self->m_monitor->openKdeModule(QStringLiteral("kcm_networkmanagement"));
        }
    });
    proc->start(nm, {QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("connect"), entry.ssid});
#else
    Q_UNUSED(entry);
#endif
}

// ============================================================================
// Bluetooth Subpage Implementation (BlueZ DBus GetManagedObjects + Battery1!)
// ============================================================================

void SystemPopover::refreshBluetooth()
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    auto system = QDBusConnection::systemBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    auto *w = new QDBusPendingCallWatcher(system.asyncCall(msg, 1200), this);
    QPointer<SystemPopover> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self) return;
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty() ||
            !reply.arguments().first().canConvert<QDBusArgument>()) {
            self->m_btSummaryLbl->setText(QStringLiteral("无法读取 BlueZ 蓝牙状态"));
            return;
        }

        bool adapterPowered = false;
        QList<BluetoothDevice> devices;

        const QDBusArgument root = reply.arguments().first().value<QDBusArgument>();
        root.beginMap();
        while (!root.atEnd()) {
            QDBusObjectPath objPath;
            root.beginMapEntry();
            root >> objPath;

            bool isPaired = false;
            BluetoothDevice dev;
            bool hasDevice = false;

            root.beginMap();
            while (!root.atEnd()) {
                QString iface;
                QVariantMap props;
                root.beginMapEntry();
                root >> iface >> props;
                root.endMapEntry();

                if (iface == QLatin1String("org.bluez.Adapter1")) {
                    if (props.value(QStringLiteral("Powered")).toBool())
                        adapterPowered = true;
                } else if (iface == QLatin1String("org.bluez.Device1")) {
                    hasDevice = true;
                    isPaired = props.value(QStringLiteral("Paired")).toBool();
                    dev.address = props.value(QStringLiteral("Address")).toString();
                    dev.name = props.value(QStringLiteral("Alias")).toString();
                    if (dev.name.isEmpty()) dev.name = props.value(QStringLiteral("Name")).toString();
                    if (dev.name.isEmpty()) dev.name = dev.address;
                    dev.iconType = props.value(QStringLiteral("Icon")).toString();
                    dev.connected = props.value(QStringLiteral("Connected")).toBool();
                } else if (iface == QLatin1String("org.bluez.Battery1")) {
                    if (props.contains(QStringLiteral("Percentage")))
                        dev.batteryPercent = props.value(QStringLiteral("Percentage")).toInt();
                }
            }
            root.endMap();
            root.endMapEntry();

            if (hasDevice && isPaired && !dev.address.isEmpty()) {
                devices.append(dev);
            }
        }
        root.endMap();

        std::sort(devices.begin(), devices.end(), [](const BluetoothDevice &a, const BluetoothDevice &b) {
            if (a.connected != b.connected) return a.connected;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        self->rebuildBluetoothList(adapterPowered, devices);
    });
#endif
}

void SystemPopover::rebuildBluetoothList(bool powered, const QList<BluetoothDevice> &devices)
{
    m_btPowered = powered;
    m_btPowerBtn->setText(powered ? QStringLiteral("蓝牙已开") : QStringLiteral("蓝牙已关"));
    m_btPowerBtn->setProperty("on", powered);
    repolish(m_btPowerBtn);

    int connectedCount = 0;
    QString firstConnected;
    for (const BluetoothDevice &d : devices) {
        if (d.connected) {
            ++connectedCount;
            if (firstConnected.isEmpty()) {
                firstConnected = (d.batteryPercent >= 0)
                    ? QStringLiteral("%1 · %2%").arg(d.name).arg(d.batteryPercent)
                    : d.name;
            }
        }
    }
    if (!powered) {
        m_cachedBtSummary = QStringLiteral("已关闭");
    } else if (connectedCount == 1) {
        m_cachedBtSummary = firstConnected;
    } else if (connectedCount > 1) {
        m_cachedBtSummary = QStringLiteral("已连 %1 台设备").arg(connectedCount);
    } else {
        m_cachedBtSummary = QStringLiteral("已开启 · %1 台").arg(devices.size());
    }
    syncHomeFromSnapshot();

    m_btSummaryLbl->setText(powered
        ? QStringLiteral("已配对 %1 台 · 已连接 %2 台（点按单台连/断）").arg(devices.size()).arg(connectedCount)
        : QStringLiteral("蓝牙适配器已关闭"));

    clearLayout(m_btListLayout);
    if (!powered) {
        auto *lbl = new QLabel(QStringLiteral("点击右上角「蓝牙已关」可重新开启蓝牙"), this);
        lbl->setAlignment(Qt::AlignCenter);
        m_btListLayout->addWidget(lbl);
        m_btListLayout->addStretch(1);
        return;
    }
    if (devices.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("暂无已配对蓝牙设备"), this);
        lbl->setAlignment(Qt::AlignCenter);
        m_btListLayout->addWidget(lbl);
        m_btListLayout->addStretch(1);
        return;
    }

    for (const BluetoothDevice &dev : devices) {
        auto *btn = new QPushButton(this);
        btn->setObjectName(QStringLiteral("rowBtn"));
        btn->setProperty("connected", dev.connected);
        btn->setMinimumHeight(48);
        const QString emoji = btDeviceEmoji(dev.iconType, dev.name);
        QString status = dev.connected ? QStringLiteral("已连接") : QStringLiteral("点按连接");
        if (dev.batteryPercent >= 0)
            status += QStringLiteral(" · 🔋%1%").arg(dev.batteryPercent);
        btn->setText(QStringLiteral("%1  %2   [%3]").arg(emoji, dev.name, status));
        connect(btn, &QPushButton::clicked, this, [this, dev] { toggleBluetoothDevice(dev); });
        m_btListLayout->addWidget(btn);
    }
    m_btListLayout->addStretch(1);
}

void SystemPopover::toggleBluetoothPower()
{
    if (!m_monitor) return;
    showStatusBanner(m_monitor->toggleBluetooth());
    QTimer::singleShot(900, this, [this] { refreshBluetooth(); });
}

void SystemPopover::toggleBluetoothDevice(const BluetoothDevice &dev)
{
#ifdef Q_OS_LINUX
    const QString bt = QStandardPaths::findExecutable(QStringLiteral("bluetoothctl"));
    if (bt.isEmpty()) return;
    const QString verb = dev.connected ? QStringLiteral("disconnect") : QStringLiteral("connect");
    showStatusBanner(dev.connected ? QStringLiteral("正在断开 %1…").arg(dev.name)
                                   : QStringLiteral("正在连接 %1…").arg(dev.name));
    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc, dev](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        if (!self) return;
        if (st == QProcess::NormalExit && code == 0) {
            self->showStatusBanner(dev.connected ? QStringLiteral("✓ 已断开 %1").arg(dev.name)
                                                 : QStringLiteral("✓ 已连接 %1").arg(dev.name));
        } else {
            self->showStatusBanner(QStringLiteral("操作 %1 未完成（设备可能未开机/未在附近）").arg(dev.name), true);
        }
        QTimer::singleShot(450, self, [self] { if (self) self->refreshBluetooth(); });
    });
    proc->start(bt, {verb, dev.address});
#else
    Q_UNUSED(dev);
#endif
}

// ============================================================================
// Mihomo REST API Subpage Implementation (127.0.0.1:9090, optional secret)
// ============================================================================

QString SystemPopover::mihomoBaseUrl() const
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    QString url = s.value("mihomo/url", QStringLiteral("http://127.0.0.1:9090")).toString().trimmed();
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    return url.isEmpty() ? QStringLiteral("http://127.0.0.1:9090") : url;
}

QString SystemPopover::mihomoSecret() const
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    return s.value("mihomo/secret").toString().trimmed();
}

void SystemPopover::refreshMihomo()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    auto makeReq = [this](const QString &path) {
        QNetworkRequest req(QUrl(mihomoBaseUrl() + path));
        req.setTransferTimeout(2000);
        const QString sec = mihomoSecret();
        if (!sec.isEmpty())
            req.setRawHeader("Authorization", QByteArray("Bearer ") + sec.toUtf8());
        return req;
    };

    // 1. Fetch /configs (mode + tun status)
    QNetworkReply *cfgReply = m_net->get(makeReq(QStringLiteral("/configs")));
    QPointer<SystemPopover> self(this);
    connect(cfgReply, &QNetworkReply::finished, this, [self, cfgReply] {
        const auto err = cfgReply->error();
        const QByteArray body = cfgReply->readAll();
        cfgReply->deleteLater();
        if (!self) return;
        if (err != QNetworkReply::NoError) {
            self->m_mhReachable = false;
            self->m_mhSummaryLbl->setText(QStringLiteral("Mihomo 未响应 (%1)").arg(self->mihomoBaseUrl()));
            self->syncHomeFromSnapshot();
            return;
        }
        self->m_mhReachable = true;
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        self->m_mhMode = obj.value(QStringLiteral("mode")).toString(QStringLiteral("rule")).toLower();
        const QJsonObject tun = obj.value(QStringLiteral("tun")).toObject();
        self->m_mhTunEnabled = tun.value(QStringLiteral("enable")).toBool();
        self->m_mhTunDevice = tun.value(QStringLiteral("device")).toString(QStringLiteral("TUN"));
        self->rebuildMihomoView();
        self->syncHomeFromSnapshot();
    });

    // 2. Fetch /proxies (groups + active node + latest delay)
    QNetworkReply *prxReply = m_net->get(makeReq(QStringLiteral("/proxies")));
    connect(prxReply, &QNetworkReply::finished, this, [self, prxReply] {
        const auto err = prxReply->error();
        const QByteArray body = prxReply->readAll();
        prxReply->deleteLater();
        if (!self) return;
        if (err != QNetworkReply::NoError) return;

        const QJsonObject allProxies = QJsonDocument::fromJson(body).object().value(QStringLiteral("proxies")).toObject();
        QList<MihomoGroup> groups;
        for (auto it = allProxies.begin(); it != allProxies.end(); ++it) {
            const QString gName = it.key();
            if (gName == QLatin1String("GLOBAL")) continue;
            const QJsonObject gObj = it.value().toObject();
            const QString type = gObj.value(QStringLiteral("type")).toString();
            if (type != QLatin1String("Selector") && type != QLatin1String("URLTest") &&
                type != QLatin1String("Fallback") && type != QLatin1String("LoadBalance"))
                continue;
            MihomoGroup g;
            g.name = gName;
            g.type = type;
            g.now = gObj.value(QStringLiteral("now")).toString();
            const QJsonArray candidates = gObj.value(QStringLiteral("all")).toArray();
            for (const QJsonValue &cv : candidates) {
                const QString nName = cv.toString();
                if (nName.isEmpty()) continue;
                MihomoNode node;
                node.name = nName;
                const QJsonArray hist = allProxies.value(nName).toObject().value(QStringLiteral("history")).toArray();
                if (!hist.isEmpty()) {
                    node.delayMs = hist.last().toObject().value(QStringLiteral("delay")).toInt(-1);
                }
                g.nodes.append(node);
            }
            groups.append(g);
        }
        std::sort(groups.begin(), groups.end(), [](const MihomoGroup &a, const MihomoGroup &b) {
            const bool aMain = (a.name.compare(QStringLiteral("PROXY"), Qt::CaseInsensitive) == 0);
            const bool bMain = (b.name.compare(QStringLiteral("PROXY"), Qt::CaseInsensitive) == 0);
            if (aMain != bMain) return aMain;
            const bool aSel = (a.type == QLatin1String("Selector"));
            const bool bSel = (b.type == QLatin1String("Selector"));
            if (aSel != bSel) return aSel;
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });

        self->m_mhGroups = groups;
        bool hasSelected = false;
        for (const MihomoGroup &g : groups) {
            if (g.name == self->m_mhSelectedGroup) { hasSelected = true; break; }
        }
        if (!hasSelected && !groups.isEmpty()) self->m_mhSelectedGroup = groups.first().name;
        self->rebuildMihomoView();
    });
}

void SystemPopover::rebuildMihomoView()
{
    static const char *kModes[3]{"rule", "global", "direct"};
    for (int i = 0; i < 3; ++i) {
        if (!m_mhModeBtns[i]) continue;
        m_mhModeBtns[i]->setProperty("active", m_mhMode == QLatin1String(kModes[i]));
        repolish(m_mhModeBtns[i]);
    }

    m_mhTunBtn->setText(m_mhTunEnabled ? QStringLiteral("TUN 开") : QStringLiteral("TUN 关"));
    m_mhTunBtn->setProperty("on", m_mhTunEnabled);
    repolish(m_mhTunBtn);

    m_mhSummaryLbl->setText(QStringLiteral("模式 %1 · %2 · %3 个策略组")
                                .arg(m_mhMode.toUpper(),
                                     m_mhTunEnabled ? (m_mhTunDevice.isEmpty() ? QStringLiteral("TUN") : m_mhTunDevice) : QStringLiteral("无TUN"))
                                .arg(m_mhGroups.size()));

    clearLayout(m_mhGroupBarLayout);
    if (!m_mhGroups.isEmpty()) {
        auto *rowHost = new QWidget(this);
        auto *row = new QHBoxLayout(rowHost);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);
        for (const MihomoGroup &g : m_mhGroups) {
            auto *gb = new QToolButton(rowHost);
            gb->setObjectName(QStringLiteral("tabBtn"));
            gb->setText(QStringLiteral("%1 (%2)").arg(g.name, g.now.isEmpty() ? g.type : g.now));
            gb->setProperty("active", g.name == m_mhSelectedGroup);
            gb->setMinimumHeight(30);
            gb->setCursor(Qt::PointingHandCursor);
            const QString gName = g.name;
            connect(gb, &QToolButton::clicked, this, [this, gName] {
                m_mhSelectedGroup = gName;
                rebuildMihomoView();
            });
            row->addWidget(gb);
        }
        row->addStretch(1);
        m_mhGroupBarLayout->addWidget(rowHost);
    }

    clearLayout(m_mhNodeListLayout);
    const MihomoGroup *activeGroup = nullptr;
    for (const MihomoGroup &g : m_mhGroups) {
        if (g.name == m_mhSelectedGroup) { activeGroup = &g; break; }
    }
    if (!activeGroup) {
        auto *lbl = new QLabel(QStringLiteral("未检测到 Mihomo 策略组（默认端口 127.0.0.1:9090）"), this);
        lbl->setAlignment(Qt::AlignCenter);
        m_mhNodeListLayout->addWidget(lbl);
        m_mhNodeListLayout->addStretch(1);
        return;
    }

    const bool selectable = (activeGroup->type == QLatin1String("Selector"));
    for (const MihomoNode &node : activeGroup->nodes) {
        auto *btn = new QPushButton(this);
        btn->setObjectName(QStringLiteral("rowBtn"));
        const bool isCurrent = (node.name == activeGroup->now);
        btn->setProperty("connected", isCurrent);
        btn->setMinimumHeight(46);
        QString delayStr;
        if (node.delayMs > 0) delayStr = QStringLiteral(" · %1 ms").arg(node.delayMs);
        else if (node.delayMs == 0) delayStr = QStringLiteral(" · 超时");
        const QString prefix = isCurrent ? QStringLiteral("✓ ") : QStringLiteral("○ ");
        const QString suffix = (!selectable && isCurrent) ? QStringLiteral(" [自动]") : QString();
        btn->setText(prefix + node.name + delayStr + suffix);
        const QString gName = activeGroup->name;
        const QString nName = node.name;
        connect(btn, &QPushButton::clicked, this, [this, selectable, gName, nName, activeType = activeGroup->type] {
            if (!selectable) {
                showStatusBanner(QStringLiteral("%1 为 %2 自动策略组，点「测速」可刷新延迟").arg(gName, activeType));
                return;
            }
            selectMihomoNode(gName, nName);
        });
        m_mhNodeListLayout->addWidget(btn);
    }
    m_mhNodeListLayout->addStretch(1);
}

void SystemPopover::setMihomoMode(const QString &mode)
{
    QNetworkRequest req(QUrl(mihomoBaseUrl() + QStringLiteral("/configs")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString sec = mihomoSecret();
    if (!sec.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + sec.toUtf8());
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("mode"), mode}}).toJson(QJsonDocument::Compact);
    QNetworkReply *r = m_net->sendCustomRequest(req, "PATCH", payload);
    QPointer<SystemPopover> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r, mode] {
        const bool ok = (r->error() == QNetworkReply::NoError);
        r->deleteLater();
        if (!self) return;
        if (ok) {
            self->m_mhMode = mode;
            self->showStatusBanner(QStringLiteral("✓ Mihomo 已切到 %1 模式").arg(mode.toUpper()));
            self->rebuildMihomoView();
            self->syncHomeFromSnapshot();
        } else {
            self->showStatusBanner(QStringLiteral("切换模式失败"), true);
        }
    });
}

void SystemPopover::toggleMihomoTun()
{
    const bool nextEnable = !m_mhTunEnabled;
    QNetworkRequest req(QUrl(mihomoBaseUrl() + QStringLiteral("/configs")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString sec = mihomoSecret();
    if (!sec.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + sec.toUtf8());
    const QByteArray payload = QJsonDocument(
        QJsonObject{{QStringLiteral("tun"), QJsonObject{{QStringLiteral("enable"), nextEnable}}}}).toJson(QJsonDocument::Compact);
    QNetworkReply *r = m_net->sendCustomRequest(req, "PATCH", payload);
    QPointer<SystemPopover> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r, nextEnable] {
        const bool ok = (r->error() == QNetworkReply::NoError);
        r->deleteLater();
        if (!self) return;
        if (ok) {
            self->m_mhTunEnabled = nextEnable;
            self->showStatusBanner(nextEnable ? QStringLiteral("✓ Mihomo TUN 已开启") : QStringLiteral("✓ Mihomo TUN 已关闭"));
            self->rebuildMihomoView();
            self->syncHomeFromSnapshot();
        } else {
            self->showStatusBanner(QStringLiteral("切换 TUN 失败"), true);
        }
    });
}

void SystemPopover::selectMihomoNode(const QString &group, const QString &node)
{
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(group));
    QNetworkRequest req(QUrl(mihomoBaseUrl() + QStringLiteral("/proxies/") + encoded));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QString sec = mihomoSecret();
    if (!sec.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + sec.toUtf8());
    const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("name"), node}}).toJson(QJsonDocument::Compact);
    QNetworkReply *r = m_net->put(req, payload);
    QPointer<SystemPopover> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r, group, node] {
        const bool ok = (r->error() == QNetworkReply::NoError);
        r->deleteLater();
        if (!self) return;
        if (ok) {
            self->showStatusBanner(QStringLiteral("✓ %1 → %2").arg(group, node));
            self->refreshMihomo();
        } else {
            self->showStatusBanner(QStringLiteral("切换节点失败"), true);
        }
    });
}

void SystemPopover::testMihomoGroupDelay(const QString &group)
{
    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(group));
    QUrl url(mihomoBaseUrl() + QStringLiteral("/group/") + encoded + QStringLiteral("/delay"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("timeout"), QStringLiteral("3500"));
    q.addQueryItem(QStringLiteral("url"), QStringLiteral("http://www.gstatic.com/generate_204"));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setTransferTimeout(4500);
    const QString sec = mihomoSecret();
    if (!sec.isEmpty()) req.setRawHeader("Authorization", QByteArray("Bearer ") + sec.toUtf8());
    showStatusBanner(QStringLiteral("正在测试 %1 延迟…").arg(group));

    QNetworkReply *r = m_net->get(req);
    QPointer<SystemPopover> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r, group] {
        const bool ok = (r->error() == QNetworkReply::NoError);
        r->deleteLater();
        if (!self) return;
        if (ok) {
            self->showStatusBanner(QStringLiteral("✓ %1 测速完成").arg(group));
            self->refreshMihomo();
        } else {
            self->showStatusBanner(QStringLiteral("测速超时或失败"), true);
        }
    });
}
