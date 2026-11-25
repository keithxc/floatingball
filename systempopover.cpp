#include "systempopover.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "systemstatus.h"
#include "touchscroll.h"
#include "uiicons.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFile>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
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
// Apple iOS-style Vertical Touch Capsule Slider (Brightness & Volume)
// ============================================================================
class CapsuleSlider final : public QWidget
{
public:
    CapsuleSlider(FloatingIcons::Icon icon, const QColor &fillColor,
                  const QColor &emptyIconColor, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_icon(icon)
        , m_fillColor(fillColor)
        , m_emptyIconColor(emptyIconColor)
    {
        setMinimumSize(52, 136);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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

    void setIcon(FloatingIcons::Icon icon)
    {
        if (m_icon == icon) return;
        m_icon = icon;
        update();
    }

    void setBadgeText(const QString &text)
    {
        if (m_badgeText == text) return;
        m_badgeText = text;
        update();
    }

    void setMutedAppearance(bool muted)
    {
        if (m_muted == muted) return;
        m_muted = muted;
        update();
    }

    std::function<void(int)> onValueChanged;
    std::function<void()> onIconTapped;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 18.0;

        QPainterPath track;
        track.addRoundedRect(r, radius, radius);
        p.fillPath(track, QColor(255, 255, 255, 14));

        const qreal frac = isEnabled()
            ? qBound(0.0, qreal(m_value - m_min) / qreal(m_max - m_min), 1.0)
            : 0.0;
        const qreal fillH = r.height() * frac;
        const qreal fillTop = r.bottom() - fillH;

        if (fillH > 0.5) {
            p.save();
            p.setClipPath(track);
            QRectF fillRect(r.left(), fillTop, r.width(), fillH + 1.0);
            const QColor fill = m_muted ? QColor(148, 163, 184, 96) : m_fillColor;
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(fillRect);
            p.restore();
        }

        // Subtle glass rim (tinted rose when muted)
        p.setPen(QPen(m_muted ? QColor(251, 113, 133, 125) : QColor(255, 255, 255, 26), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(track);

        // Top numeric / status readout with automatic contrast over fill
        if (!m_badgeText.isEmpty()) {
            const bool overLightFill = !m_muted && isEnabled() && (fillTop < r.top() + 24.0);
            QFont f = font();
            f.setPointSize(9);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(m_muted
                         ? QColor("#fda4af")
                         : (overLightFill ? QColor("#0f172a") : QColor(226, 232, 240, 210)));
            p.drawText(QRectF(r.left() + 3.0, r.top() + 8.0, r.width() - 6.0, 16.0),
                       Qt::AlignCenter, m_badgeText);
        }

        // Bottom centered iOS icon with automatic contrast over fill
        const qreal iconSize = 19.0;
        const QRectF iconRect(r.center().x() - iconSize * 0.5,
                              r.bottom() - iconSize - 13.0,
                              iconSize, iconSize);
        const bool iconOverLightFill = !m_muted && isEnabled() && (fillH >= 32.0);
        const QColor iconColor = m_muted
            ? QColor("#fb7185")
            : (iconOverLightFill ? QColor("#1e293b") : m_emptyIconColor);
        FloatingIcons::draw(p, m_icon, iconRect, iconColor);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (!isEnabled()) return;
        if (e->button() == Qt::RightButton && onIconTapped) {
            onIconTapped();
            e->accept();
            return;
        }
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_moved = false;
            m_pressPos = e->position();
            m_pressedIconZone = (onIconTapped && e->position().y() >= height() - 44.0);
            if (!m_pressedIconZone) {
                updateFromY(e->position().y());
            }
            e->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!isEnabled()) return;
        if (m_dragging && (e->buttons() & Qt::LeftButton)) {
            if (!m_moved && qAbs(e->position().y() - m_pressPos.y()) >= 5.0) {
                m_moved = true;
            }
            if (!m_pressedIconZone || m_moved) {
                updateFromY(e->position().y());
            }
            e->accept();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            const bool shouldToggleIcon = m_dragging && m_pressedIconZone && !m_moved && onIconTapped;
            m_dragging = false;
            m_moved = false;
            m_pressedIconZone = false;
            if (shouldToggleIcon) {
                onIconTapped();
            }
            e->accept();
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        if (!isEnabled()) return;
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
    void updateFromY(qreal y)
    {
        const qreal pad = 10.0;
        const qreal span = qMax(1.0, height() - pad * 2.0);
        const qreal frac = qBound(0.0, (height() - pad - y) / span, 1.0);
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
    bool m_moved = false;
    bool m_pressedIconZone = false;
    QPointF m_pressPos;
    FloatingIcons::Icon m_icon;
    QColor m_fillColor;
    QColor m_emptyIconColor;
    QString m_badgeText;
};

// ============================================================================
// Apple iOS-style Capsule Toggle Switch
// ============================================================================
class IosToggleSwitch final : public QWidget
{
public:
    explicit IosToggleSwitch(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(44, 26);
        setCursor(Qt::PointingHandCursor);
    }

    void setChecked(bool checked)
    {
        if (m_checked == checked) return;
        m_checked = checked;
        update();
    }

    bool isChecked() const { return m_checked; }

    void setOnColor(const QColor &color)
    {
        m_onColor = color;
        update();
    }

    std::function<void()> onToggled;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal rad = r.height() * 0.5;
        QColor track = m_checked ? m_onColor : QColor(255, 255, 255, 34);
        if (!isEnabled()) track.setAlpha(track.alpha() / 2);

        p.setPen(QPen(m_checked ? QColor(255, 255, 255, 36) : QColor(255, 255, 255, 30), 1.0));
        p.setBrush(track);
        p.drawRoundedRect(r, rad, rad);

        const qreal knobD = r.height() - 4.0;
        const qreal knobX = m_checked ? (r.right() - knobD - 2.0) : (r.left() + 2.0);
        const QRectF knob(knobX, r.top() + 2.0, knobD, knobD);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 42));
        p.drawEllipse(knob.translated(0.0, 1.0));
        p.setBrush(isEnabled() ? QColor("#ffffff") : QColor(226, 232, 240, 180));
        p.drawEllipse(knob);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (isEnabled() && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
            if (onToggled) onToggled();
            e->accept();
            return;
        }
        QWidget::mouseReleaseEvent(e);
    }

private:
    bool m_checked = false;
    QColor m_onColor{QStringLiteral("#34c759")};
};

namespace {

bool readSysfsNetBytes(const QString &iface, quint64 *rxOut, quint64 *txOut)
{
    if (iface.isEmpty()) return false;
    QFile fRx(QStringLiteral("/sys/class/net/%1/statistics/rx_bytes").arg(iface));
    QFile fTx(QStringLiteral("/sys/class/net/%1/statistics/tx_bytes").arg(iface));
    if (!fRx.open(QIODevice::ReadOnly) || !fTx.open(QIODevice::ReadOnly)) return false;
    bool okRx = false, okTx = false;
    const quint64 rx = QString::fromLatin1(fRx.readAll()).trimmed().toULongLong(&okRx);
    const quint64 tx = QString::fromLatin1(fTx.readAll()).trimmed().toULongLong(&okTx);
    if (!okRx || !okTx) return false;
    if (rxOut) *rxOut = rx;
    if (txOut) *txOut = tx;
    return true;
}

QString formatByteCount(quint64 bytes)
{
    if (bytes < 1024ULL) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024ULL * 1024ULL) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024ULL * 1024ULL * 1024ULL) return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString formatByteRate(quint64 bytesPerSec)
{
    if (bytesPerSec < 1024ULL) return QStringLiteral("%1 B/s").arg(bytesPerSec);
    if (bytesPerSec < 1024ULL * 1024ULL) return QStringLiteral("%1 KB/s").arg(bytesPerSec / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB/s").arg(bytesPerSec / (1024.0 * 1024.0), 0, 'f', 2);
}

QWidget *makeIosSwitchCard(QWidget *parent, const QString &label, IosToggleSwitch **outToggle,
                           const QColor &onColor = QColor(QStringLiteral("#34c759")))
{
    auto *card = new QWidget(parent);
    card->setObjectName(QStringLiteral("bentoGroup"));
    card->setFixedHeight(46);
    auto *hl = new QHBoxLayout(card);
    hl->setContentsMargins(12, 6, 10, 6);
    hl->setSpacing(8);
    auto *lbl = new QLabel(label, card);
    lbl->setObjectName(QStringLiteral("rowTitle"));
    hl->addWidget(lbl, 1);
    auto *sw = new IosToggleSwitch(card);
    sw->setOnColor(onColor);
    hl->addWidget(sw);
    if (outToggle) *outToggle = sw;
    return card;
}

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
        if (QWidget *w = it->widget()) {
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
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
    TouchScrollController::install(scroll);
    if (outInnerLayout) *outInnerLayout = vbox;
    return scroll;
}

void attachTouchScroll(QLayout *innerLayout, QWidget *row)
{
    if (!innerLayout || !row) return;
    QWidget *p = innerLayout->parentWidget();
    while (p) {
        if (auto *sa = qobject_cast<QScrollArea *>(p)) {
            if (auto *ctrl = TouchScrollController::install(sa))
                ctrl->attachWidget(row);
            return;
        }
        p = p->parentWidget();
    }
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
    // Stable fixed geometry prevents Wayland LayerShellQt async configure clipping
    // when switching subpages, refreshing Wi-Fi lists, or showing status banners.
    setFixedSize(344, 404);
    setProperty("panelBlurInset", 10);
    setProperty("panelBlurRadius", 26);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("ccCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#ccCard { background:transparent; border:none; }"
        "QWidget#bentoGroup { background:rgba(255,255,255,18); border:1px solid rgba(255,255,255,24); border-radius:18px; }"
        "QLabel { color:#e2e8f0; font-size:12px; background:transparent; }"
        "QLabel#ccTitle { color:#ffffff; font-size:14px; font-weight:700; }"
        "QLabel#ccMeta { color:#cbd5e1; font-size:11px; font-weight:600; }"
        "QLabel#ccSub { color:rgba(226,232,240,185); font-size:11px; }"
        "QLabel#rowTitle { color:#ffffff; font-size:12.5px; font-weight:600; }"
        "QLabel#rowSub { color:rgba(226,232,240,175); font-size:11px; }"
        "QLabel#sliderTag { color:rgba(226,232,240,190); font-size:11px; font-weight:600; }"
        "QLabel#sliderVal { color:#f8fafc; font-size:11.5px; font-weight:600; }"
        "QToolButton#powerPill { background:rgba(255,255,255,18); color:#e2e8f0; border:1px solid rgba(255,255,255,24);"
        "                        border-radius:17px; padding:0 11px; font-size:12px; font-weight:600; }"
        "QToolButton#powerPill:hover { background:rgba(255,255,255,30); }"
        "QToolButton#powerPill[accent=\"green\"] { background:rgba(16,185,129,48); border:1px solid rgba(52,211,153,150); color:#d1fae5; }"
        "QToolButton#powerPill[accent=\"blue\"]  { background:rgba(59,130,246,44); border:1px solid rgba(96,165,250,145); color:#dbeafe; }"
        "QToolButton#powerPill[accent=\"rose\"]  { background:rgba(244,63,94,48);  border:1px solid rgba(251,113,133,150); color:#ffe4e6; }"
        "QToolButton#hdrIconBtn { background:rgba(255,255,255,18); color:#f1f5f9; border:1px solid rgba(255,255,255,24); border-radius:17px; font-size:16px; }"
        "QToolButton#hdrIconBtn:hover { background:rgba(255,255,255,32); }"
        "QToolButton#hdrDangerBtn { background:rgba(244,63,94,40); color:#ffe4e6; border:1px solid rgba(251,113,133,105); border-radius:17px; }"
        "QToolButton#hdrDangerBtn:hover { background:rgba(244,63,94,68); }"
        "QToolButton#backPill { background:rgba(255,255,255,18); color:#f8fafc; border:1px solid rgba(255,255,255,26);"
        "                       border-radius:17px; padding:0 11px 0 7px; font-size:12.5px; font-weight:600; }"
        "QToolButton#backPill:hover { background:rgba(255,255,255,32); }"
        "QPushButton#connRow { background:transparent; border:none; border-radius:12px; text-align:left; }"
        "QPushButton#connRow:hover { background:rgba(255,255,255,18); }"
        "QPushButton#connRow:pressed { background:rgba(255,255,255,28); }"
        "QPushButton#dndCard { background:rgba(255,255,255,18); border:1px solid rgba(255,255,255,24); border-radius:18px; text-align:left; }"
        "QPushButton#dndCard:hover { background:rgba(255,255,255,28); }"
        "QPushButton#dndCard[on=\"true\"] { background:rgba(139,92,246,64); border:1px solid rgba(167,139,250,165); }"
        "QToolButton#mediaBtn { background:rgba(255,255,255,18); border:1px solid rgba(255,255,255,24); border-radius:15px; }"
        "QToolButton#mediaBtn:hover { background:rgba(255,255,255,32); }"
        "QToolButton#tabBtn { background:rgba(255,255,255,16); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                     border-radius:11px; padding:4px 9px; font-size:11.5px; font-weight:600; }"
        "QToolButton#tabBtn:hover { background:rgba(255,255,255,28); color:#ffffff; }"
        "QToolButton#tabBtn[active=\"true\"] { background:rgba(59,130,246,175); color:#ffffff; border:1px solid rgba(147,197,253,195); }"
        "QToolButton#modeBtn { background:rgba(255,255,255,15); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                      border-radius:12px; padding:6px 6px; font-size:12px; font-weight:600; }"
        "QToolButton#modeBtn:hover { background:rgba(255,255,255,26); }"
        "QToolButton#modeBtn[active=\"true\"] { background:rgba(56,189,248,68); color:#ffffff; border:1px solid rgba(125,211,252,195); }"
        "QToolButton#modeBtn[tone=\"green\"][active=\"true\"] { background:rgba(16,185,129,72); color:#ffffff; border:1px solid rgba(52,211,153,195); }"
        "QToolButton#modeBtn[tone=\"blue\"][active=\"true\"]  { background:rgba(59,130,246,84); color:#ffffff; border:1px solid rgba(147,197,253,205); }"
        "QToolButton#modeBtn[tone=\"amber\"][active=\"true\"] { background:rgba(245,158,11,76); color:#ffffff; border:1px solid rgba(251,191,36,205); }"
        "QPushButton#rowBtn { background:rgba(255,255,255,14); color:#f8fafc; border:1px solid rgba(255,255,255,20);"
        "                     border-radius:12px; padding:8px 11px; text-align:left; font-size:12px; font-weight:500; }"
        "QPushButton#rowBtn:hover { background:rgba(255,255,255,26); }"
        "QPushButton#rowBtn[connected=\"true\"] { background:rgba(56,189,248,44); border:1px solid rgba(125,211,252,150); color:#ffffff; }"
        "QToolButton#footBtn { background:rgba(255,255,255,15); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                      border-radius:12px; padding:7px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#footBtn:hover { background:rgba(255,255,255,28); color:#ffffff; }"
        "QToolButton#primaryBtn { background:rgba(59,130,246,165); color:#ffffff; border:1px solid rgba(147,197,253,190);"
        "                         border-radius:12px; padding:7px 12px; font-size:12px; font-weight:600; }"
        "QToolButton#primaryBtn:hover { background:rgba(59,130,246,210); }"
        "QLineEdit#hsInput { background:rgba(12,18,32,145); color:#ffffff; border:1px solid rgba(255,255,255,30);"
        "                    border-radius:10px; padding:5px 9px; font-size:12px; }"
        "QLineEdit#hsInput:focus { border:1px solid rgba(96,165,250,210); }");

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(9);

    // ---- Shared Top Draggable Header ----
    auto *header = new PopoverDragHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hdrRow = new QHBoxLayout(header);
    hdrRow->setContentsMargins(0, 0, 0, 0);
    hdrRow->setSpacing(5);

    m_backBtn = new QToolButton(header);
    m_backBtn->setObjectName(QStringLiteral("backPill"));
    m_backBtn->setIcon(FloatingIcons::make(FloatingIcons::ChevronLeft, QColor("#f8fafc"), 15));
    m_backBtn->setIconSize(QSize(15, 15));
    m_backBtn->setText(QStringLiteral("返回"));
    m_backBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_backBtn->setFixedHeight(34);
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->hide();
    connect(m_backBtn, &QToolButton::clicked, this, [this] { showPage(PageHome); });
    hdrRow->addWidget(m_backBtn);

    m_titleLbl = new QLabel(header);
    m_titleLbl->setObjectName(QStringLiteral("ccTitle"));
    m_titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_titleLbl->hide();
    hdrRow->addWidget(m_titleLbl);

    // Clickable Battery + Power Profile Pill (cycles power-saver / balanced / performance)
    m_powerPill = new QToolButton(header);
    m_powerPill->setObjectName(QStringLiteral("powerPill"));
    m_powerPill->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_powerPill->setFixedHeight(34);
    m_powerPill->setText(QStringLiteral("平衡"));
    m_powerPill->setToolTip(QStringLiteral("点击切换功耗模式（节能 / 平衡 / 性能）"));
    m_powerPill->setCursor(Qt::PointingHandCursor);
    connect(m_powerPill, &QToolButton::clicked, this, [this] {
        if (!m_monitor) return;
        showStatusBanner(m_monitor->cyclePowerProfile());
        syncHomeFromSnapshot();
    });
    hdrRow->addWidget(m_powerPill);

    hdrRow->addStretch(1);

    auto makeHdrBtn = [&](const QString &objName, FloatingIcons::Icon icon, const QColor &ink, const QString &tip) {
        auto *b = new QToolButton(header);
        b->setObjectName(objName);
        b->setIcon(FloatingIcons::make(icon, ink, 16));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setFixedSize(34, 34);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    m_hdrSettingsBtn = makeHdrBtn(QStringLiteral("hdrIconBtn"), FloatingIcons::Settings, QColor("#e2e8f0"),
                                  QStringLiteral("系统设置"));
    connect(m_hdrSettingsBtn, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openKdeModule(QString());
    });
    hdrRow->addWidget(m_hdrSettingsBtn);

    m_hdrLockBtn = makeHdrBtn(QStringLiteral("hdrIconBtn"), FloatingIcons::Lock, QColor("#f1f5f9"),
                              QStringLiteral("锁屏"));
    connect(m_hdrLockBtn, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->lockScreen();
    });
    hdrRow->addWidget(m_hdrLockBtn);

    m_hdrPowerBtn = makeHdrBtn(QStringLiteral("hdrDangerBtn"), FloatingIcons::Power, QColor("#fda4af"),
                               QStringLiteral("关机 / 休眠"));
    connect(m_hdrPowerBtn, &QToolButton::clicked, this, [this] {
        hide();
        if (m_monitor) m_monitor->openPowerMenu();
    });
    hdrRow->addWidget(m_hdrPowerBtn);

    m_refreshBtn = new QToolButton(header);
    m_refreshBtn->setObjectName(QStringLiteral("hdrIconBtn"));
    m_refreshBtn->setText(QStringLiteral("↻"));
    m_refreshBtn->setToolTip(QStringLiteral("刷新实时状态"));
    m_refreshBtn->setFixedSize(34, 34);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QToolButton::clicked, this, [this] {
        if (m_currentPage == PageWifi) refreshWifi(true);
        else refreshCurrentPage();
    });
    hdrRow->addWidget(m_refreshBtn);

    auto *closeBtn = makeHdrBtn(QStringLiteral("hdrIconBtn"), FloatingIcons::Close, QColor("#cbd5e1"),
                                QStringLiteral("关闭"));
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hdrRow->addWidget(closeBtn);
    root->addWidget(header);

    // ---- Inline Action Status Banner ----
    m_statusBanner = new QLabel(card);
    m_statusBanner->setWordWrap(true);
    m_statusBanner->hide();
    root->addWidget(m_statusBanner);

    loadHotspotPrefs();

    // ---- Stacked Pages (0 = Home, 1 = Wi-Fi, 2 = Hotspot, 3 = Bluetooth, 4 = Mihomo, 5 = 4G) ----
    m_stack = new QStackedWidget(card);
    m_stack->addWidget(buildHomePage(m_stack));
    m_stack->addWidget(buildWifiPage(m_stack));
    m_stack->addWidget(buildHotspotPage(m_stack));
    m_stack->addWidget(buildBluetoothPage(m_stack));
    m_stack->addWidget(buildMihomoPage(m_stack));
    m_stack->addWidget(buildCellularPage(m_stack));
    root->addWidget(m_stack, 1);

    // Live 1s net speed/traffic sampler (runs only when visible on Wi-Fi / 4G / Hotspot page)
    m_netStatsTimer = new QTimer(this);
    m_netStatsTimer->setInterval(1000);
    connect(m_netStatsTimer, &QTimer::timeout, this, &SystemPopover::pollLiveNetStats);

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

    // Left Bento Card: Wi-Fi / 4G (when present) / Hotspot / Bluetooth / Mihomo
    auto *connCard = new QWidget(page);
    connCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *connCol = new QVBoxLayout(connCard);
    connCol->setContentsMargins(7, 6, 7, 6);
    connCol->setSpacing(2);

    auto makeConnRow = [&](const QString &title, const QString &initSub, FloatingIcons::Icon icon,
                           const QColor &badgeBg, QLabel **outBadge, QLabel **outSub,
                           bool hasChevron, std::function<void()> onClick) {
        auto *rowBtn = new QPushButton(connCard);
        rowBtn->setObjectName(QStringLiteral("connRow"));
        rowBtn->setCursor(Qt::PointingHandCursor);
        rowBtn->setMinimumHeight(40);
        auto *hl = new QHBoxLayout(rowBtn);
        hl->setContentsMargins(6, 3, 5, 3);
        hl->setSpacing(8);

        auto *badge = new QLabel(rowBtn);
        badge->setAttribute(Qt::WA_TransparentForMouseEvents);
        styleCircleBadge(badge, icon, badgeBg, QColor("#ffffff"), 30);
        hl->addWidget(badge);

        auto *textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(0);
        auto *tLbl = new QLabel(title, rowBtn);
        tLbl->setObjectName(QStringLiteral("rowTitle"));
        tLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *sLbl = new QLabel(initSub, rowBtn);
        sLbl->setObjectName(QStringLiteral("rowSub"));
        sLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
        textCol->addWidget(tLbl);
        textCol->addWidget(sLbl);
        hl->addLayout(textCol, 1);

        if (hasChevron) {
            auto *chev = new QLabel(rowBtn);
            chev->setPixmap(FloatingIcons::make(FloatingIcons::ChevronRight, QColor("#64748b"), 13).pixmap(13, 13));
            chev->setAttribute(Qt::WA_TransparentForMouseEvents);
            hl->addWidget(chev);
        }

        connect(rowBtn, &QPushButton::clicked, this, [onClick] { onClick(); });
        if (outBadge) *outBadge = badge;
        if (outSub) *outSub = sLbl;
        return rowBtn;
    };

    connCol->addWidget(makeConnRow(QStringLiteral("Wi-Fi"), QStringLiteral("检测中…"),
                                   FloatingIcons::Wifi, QColor("#2563eb"),
                                   &m_homeWifiBadge, &m_homeWifiSub, true,
                                   [this] { showPage(PageWifi); }));
    m_homeWwanRow = makeConnRow(QStringLiteral("4G 网络"), QStringLiteral("已关闭"),
                                FloatingIcons::Cellular, QColor(255, 255, 255, 26),
                                &m_homeWwanBadge, &m_homeWwanSub, true,
                                [this] { showPage(PageCellular); });
    m_homeWwanRow->hide();
    connCol->addWidget(m_homeWwanRow);
    connCol->addWidget(makeConnRow(QStringLiteral("个人热点"), m_hsSsid + QStringLiteral(" · WPA3"),
                                   FloatingIcons::Hotspot, QColor(255, 255, 255, 26),
                                   &m_homeHotspotBadge, &m_homeHotspotSub, true,
                                   [this] { showPage(PageHotspot); }));
    connCol->addWidget(makeConnRow(QStringLiteral("蓝牙"), QStringLiteral("检测中…"),
                                   FloatingIcons::Bluetooth, QColor("#3b82f6"),
                                   &m_homeBtBadge, &m_homeBtSub, true,
                                   [this] { showPage(PageBluetooth); }));
    connCol->addWidget(makeConnRow(QStringLiteral("Mihomo"), QStringLiteral("RULE · 检测中"),
                                   FloatingIcons::Proxy, QColor("#10b981"),
                                   &m_homeMhBadge, &m_homeMhSub, true,
                                   [this] { showPage(PageMihomo); }));
    upper->addWidget(connCard, 168);

    // Right Bento Column: DND wide pill on top + two slender iOS-style vertical sliders below
    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(9);

    // DND Wide Card
    m_dndTile = new QPushButton(page);
    m_dndTile->setObjectName(QStringLiteral("dndCard"));
    m_dndTile->setCursor(Qt::PointingHandCursor);
    m_dndTile->setFixedHeight(52);
    auto *dndRow = new QHBoxLayout(m_dndTile);
    dndRow->setContentsMargins(8, 6, 8, 6);
    dndRow->setSpacing(7);
    m_dndBadge = new QLabel(m_dndTile);
    m_dndBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    styleCircleBadge(m_dndBadge, FloatingIcons::Bell, QColor(255, 255, 255, 26), QColor("#cbd5e1"), 28);
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

    // Side-by-side slender iOS Vertical Brightness & Volume Sliders
    auto *slidersRow = new QHBoxLayout();
    slidersRow->setContentsMargins(0, 0, 0, 0);
    slidersRow->setSpacing(8);

    m_brightnessSlider = new CapsuleSlider(
        FloatingIcons::Sun, QColor(248, 250, 252, 236), QColor("#fcd34d"), page);
    m_brightnessSlider->setToolTip(QStringLiteral("上下拖动或滚轮调节显示器亮度"));
    m_brightnessSlider->setRange(1, 100);
    m_brightnessSlider->setValue(70);
    m_brightnessSlider->setBadgeText(QStringLiteral("70%"));
    slidersRow->addWidget(m_brightnessSlider, 1);

    m_brightnessDebounce = new QTimer(this);
    m_brightnessDebounce->setSingleShot(true);
    m_brightnessDebounce->setInterval(75);
    m_brightnessSlider->onValueChanged = [this](int val) {
        m_brightnessSlider->setBadgeText(QStringLiteral("%1%").arg(val));
        if (m_monitor) m_monitor->updateBrightness(val, true);
        emit brightnessStateChanged(val);
        m_brightnessDebounce->start();
    };
    connect(m_brightnessDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setBrightness(m_brightnessSlider->value());
    });

    m_volumeSlider = new CapsuleSlider(
        FloatingIcons::Volume, QColor(242, 248, 255, 234), QColor("#cbd5e1"), page);
    m_volumeSlider->setToolTip(QStringLiteral("上下拖动调节音量 · 轻点底部图标或右键切换静音"));
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(60);
    m_volumeSlider->setBadgeText(QStringLiteral("60%"));
    slidersRow->addWidget(m_volumeSlider, 1);

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
    m_volumeSlider->onIconTapped = [this] {
        QPointer<SystemPopover> self(this);
        DesktopActions::toggleMute([self](int vol, bool muted, bool ok) {
            if (!self || !ok) return;
            self->applyAudioVisuals(vol, muted, true);
            emit self->audioStateChanged(vol, muted);
        });
    };
    connect(m_volumeDebounce, &QTimer::timeout, this, [this] {
        DesktopActions::setVolume(m_volumeSlider->value());
    });

    rightCol->addLayout(slidersRow, 1);

    upper->addLayout(rightCol, 122);
    v->addLayout(upper);

    // ---- MPRIS Now Playing Card (Auto-Hidden When Nothing Playing) ----
    m_mprisCard = new QWidget(page);
    m_mprisCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *mRow = new QHBoxLayout(m_mprisCard);
    mRow->setContentsMargins(9, 8, 9, 8);
    mRow->setSpacing(8);

    auto *artBadge = new QLabel(m_mprisCard);
    styleCircleBadge(artBadge, FloatingIcons::Music, QColor(59, 130, 246, 75), QColor("#bae6fd"), 34);
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
        b->setIcon(FloatingIcons::make(icon, QColor("#f1f5f9"), 14));
        b->setFixedSize(30, 30);
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
    v->addStretch(1);

    return page;
}

// ============================================================================
// Subpages 1..5: Wi-Fi / 4G / Hotspot / Bluetooth / Mihomo (Pure Single-Purpose)
// ============================================================================

QWidget *SystemPopover::buildWifiPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // iOS-style Wi-Fi capsule toggle row
    v->addWidget(makeIosSwitchCard(page, QStringLiteral("Wi-Fi"), &m_wifiToggle, QColor("#34c759")));
    m_wifiToggle->onToggled = [this] { toggleWifiRadio(); };

    // Connected Wi-Fi live details card (SSID, signal, security, IP, rate/band, live speed)
    m_wifiDetailCard = new QWidget(page);
    m_wifiDetailCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *dv = new QVBoxLayout(m_wifiDetailCard);
    dv->setContentsMargins(12, 9, 12, 9);
    dv->setSpacing(3);
    m_wifiDetailTitle = new QLabel(m_wifiDetailCard);
    m_wifiDetailTitle->setObjectName(QStringLiteral("rowTitle"));
    m_wifiDetailMeta = new QLabel(m_wifiDetailCard);
    m_wifiDetailMeta->setObjectName(QStringLiteral("ccSub"));
    m_wifiDetailMeta->setWordWrap(true);
    m_wifiSpeedLbl = new QLabel(QStringLiteral("网速  ↓ 0 B/s  ·  ↑ 0 B/s"), m_wifiDetailCard);
    m_wifiSpeedLbl->setObjectName(QStringLiteral("sliderVal"));
    dv->addWidget(m_wifiDetailTitle);
    dv->addWidget(m_wifiDetailMeta);
    dv->addWidget(m_wifiSpeedLbl);
    m_wifiDetailCard->hide();
    v->addWidget(m_wifiDetailCard);

    auto *scroll = makeTouchScrollArea(page, &m_wifiListLayout);
    v->addWidget(scroll, 1);

    return page;
}

QWidget *SystemPopover::buildCellularPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    v->addWidget(makeIosSwitchCard(page, QStringLiteral("4G 移动网络"), &m_cellToggle, QColor("#34c759")));
    m_cellToggle->onToggled = [this] { toggleWwanRadio(); };

    auto *infoCard = new QWidget(page);
    infoCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *iv = new QVBoxLayout(infoCard);
    iv->setContentsMargins(12, 10, 12, 12);
    iv->setSpacing(7);

    auto addInfoRow = [&](const QString &k, QLabel **outVal, const QString &init) {
        auto *r = new QHBoxLayout();
        r->setContentsMargins(0, 0, 0, 0);
        r->setSpacing(8);
        auto *kl = new QLabel(k, infoCard);
        kl->setObjectName(QStringLiteral("sliderTag"));
        kl->setFixedWidth(64);
        auto *vl = new QLabel(init, infoCard);
        vl->setObjectName(QStringLiteral("rowTitle"));
        vl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        vl->setWordWrap(true);
        r->addWidget(kl);
        r->addWidget(vl, 1);
        iv->addLayout(r);
        if (outVal) *outVal = vl;
    };

    addInfoRow(QStringLiteral("运营商"), &m_cellOpLbl, QStringLiteral("检测中…"));
    addInfoRow(QStringLiteral("信号状态"), &m_cellSignalLbl, QStringLiteral("--"));
    addInfoRow(QStringLiteral("IP / 接口"), &m_cellIpLbl, QStringLiteral("--"));
    addInfoRow(QStringLiteral("实时网速"), &m_cellSpeedLbl, QStringLiteral("↓ 0 B/s  ·  ↑ 0 B/s"));
    addInfoRow(QStringLiteral("本次流量"), &m_cellTrafficLbl, QStringLiteral("↓ 0 B  ·  ↑ 0 B"));

    v->addWidget(infoCard);
    v->addStretch(1);
    return page;
}

QWidget *SystemPopover::buildHotspotPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // Pure iOS Hotspot capsule toggle row
    v->addWidget(makeIosSwitchCard(page, QStringLiteral("个人热点"), &m_hsToggle, QColor("#34c759")));
    m_hsToggle->onToggled = [this] { toggleHotspot(); };

    // ---- View Mode Card (Default: SSID / Band / Security / Password + Edit button + Live Traffic) ----
    m_hsViewCard = new QWidget(page);
    m_hsViewCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *vv = new QVBoxLayout(m_hsViewCard);
    vv->setContentsMargins(12, 9, 10, 10);
    vv->setSpacing(6);

    auto *vh = new QHBoxLayout();
    vh->setSpacing(6);
    m_hsInfoTitle = new QLabel(m_hsViewCard);
    m_hsInfoTitle->setObjectName(QStringLiteral("rowTitle"));
    vh->addWidget(m_hsInfoTitle, 1);
    auto *editBtn = new QToolButton(m_hsViewCard);
    editBtn->setObjectName(QStringLiteral("tabBtn"));
    editBtn->setText(QStringLiteral("编辑"));
    editBtn->setCursor(Qt::PointingHandCursor);
    connect(editBtn, &QToolButton::clicked, this, [this] { setHotspotEditMode(true); });
    vh->addWidget(editBtn);
    vv->addLayout(vh);

    auto *vp = new QHBoxLayout();
    vp->setSpacing(6);
    auto *pTag = new QLabel(QStringLiteral("密码"), m_hsViewCard);
    pTag->setObjectName(QStringLiteral("sliderTag"));
    m_hsPassValueLbl = new QLabel(QStringLiteral("••••••••"), m_hsViewCard);
    m_hsPassValueLbl->setObjectName(QStringLiteral("ccMeta"));
    vp->addWidget(pTag);
    vp->addWidget(m_hsPassValueLbl, 1);

    m_hsViewEyeBtn = new QToolButton(m_hsViewCard);
    m_hsViewEyeBtn->setObjectName(QStringLiteral("tabBtn"));
    m_hsViewEyeBtn->setText(QStringLiteral("显示"));
    m_hsViewEyeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_hsViewEyeBtn, &QToolButton::clicked, this, [this] {
        m_hsPassRevealed = !m_hsPassRevealed;
        rebuildHotspotView();
    });
    vp->addWidget(m_hsViewEyeBtn);

    auto *copyBtn = new QToolButton(m_hsViewCard);
    copyBtn->setObjectName(QStringLiteral("tabBtn"));
    copyBtn->setText(QStringLiteral("复制"));
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QToolButton::clicked, this, [this] {
        if (m_hsPassword.isEmpty()) return;
        if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(m_hsPassword);
        showStatusBanner(QStringLiteral("✓ 已复制热点密码"));
    });
    vp->addWidget(copyBtn);
    vv->addLayout(vp);

    m_hsTrafficLbl = new QLabel(QStringLiteral("速率  ↓ 0 B/s · ↑ 0 B/s   |   累计  ↓ 0 B · ↑ 0 B"), m_hsViewCard);
    m_hsTrafficLbl->setObjectName(QStringLiteral("ccSub"));
    m_hsTrafficLbl->setWordWrap(true);
    vv->addWidget(m_hsTrafficLbl);
    v->addWidget(m_hsViewCard);

    // ---- Edit Mode Card (Shown when user clicks "编辑", saves & returns to View Mode) ----
    m_hsEditCard = new QWidget(page);
    m_hsEditCard->setObjectName(QStringLiteral("bentoGroup"));
    auto *ev = new QVBoxLayout(m_hsEditCard);
    ev->setContentsMargins(11, 9, 11, 10);
    ev->setSpacing(7);

    auto *ssidRow = new QHBoxLayout();
    ssidRow->setSpacing(6);
    auto *ssidLbl = new QLabel(QStringLiteral("名称"), m_hsEditCard);
    ssidLbl->setObjectName(QStringLiteral("sliderTag"));
    ssidLbl->setFixedWidth(44);
    m_hsSsidEdit = new QLineEdit(m_hsSsid, m_hsEditCard);
    m_hsSsidEdit->setObjectName(QStringLiteral("hsInput"));
    m_hsSsidEdit->setPlaceholderText(defaultHotspotSsid());
    ssidRow->addWidget(ssidLbl);
    ssidRow->addWidget(m_hsSsidEdit, 1);
    ev->addLayout(ssidRow);

    auto *passRow = new QHBoxLayout();
    passRow->setSpacing(6);
    auto *passLbl = new QLabel(QStringLiteral("密码"), m_hsEditCard);
    passLbl->setObjectName(QStringLiteral("sliderTag"));
    passLbl->setFixedWidth(44);
    m_hsPassEdit = new QLineEdit(m_hsPassword, m_hsEditCard);
    m_hsPassEdit->setObjectName(QStringLiteral("hsInput"));
    m_hsPassEdit->setPlaceholderText(QStringLiteral("至少 8 位密码"));
    passRow->addWidget(passLbl);
    passRow->addWidget(m_hsPassEdit, 1);
    ev->addLayout(passRow);

    auto *secRow = new QHBoxLayout();
    secRow->setSpacing(6);
    auto *secLbl = new QLabel(QStringLiteral("加密"), m_hsEditCard);
    secLbl->setObjectName(QStringLiteral("sliderTag"));
    secLbl->setFixedWidth(44);
    secRow->addWidget(secLbl);
    static const char *kSecLabels[2]{"WPA3 (推荐)", "WPA2 兼容"};
    for (int i = 0; i < 2; ++i) {
        auto *b = new QToolButton(m_hsEditCard);
        b->setObjectName(QStringLiteral("modeBtn"));
        b->setText(QString::fromUtf8(kSecLabels[i]));
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        b->setMinimumHeight(28);
        b->setCursor(Qt::PointingHandCursor);
        const bool wantWpa3 = (i == 0);
        connect(b, &QToolButton::clicked, this, [this, wantWpa3] {
            m_hsUseWpa3 = wantWpa3;
            rebuildHotspotView();
        });
        secRow->addWidget(b);
        m_hsSecBtns[i] = b;
    }
    ev->addLayout(secRow);

    auto *actRow = new QHBoxLayout();
    actRow->setSpacing(8);
    auto *cancelBtn = new QToolButton(m_hsEditCard);
    cancelBtn->setObjectName(QStringLiteral("footBtn"));
    cancelBtn->setText(QStringLiteral("取消"));
    cancelBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cancelBtn->setMinimumHeight(30);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QToolButton::clicked, this, [this] { setHotspotEditMode(false); });

    auto *saveBtn = new QToolButton(m_hsEditCard);
    saveBtn->setObjectName(QStringLiteral("primaryBtn"));
    saveBtn->setText(QStringLiteral("保存"));
    saveBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    saveBtn->setMinimumHeight(30);
    saveBtn->setCursor(Qt::PointingHandCursor);
    connect(saveBtn, &QToolButton::clicked, this, [this] { applyHotspotConfig(m_hsActive); });
    actRow->addWidget(cancelBtn, 1);
    actRow->addWidget(saveBtn, 1);
    ev->addLayout(actRow);

    m_hsEditCard->hide();
    v->addWidget(m_hsEditCard);

    // Connected hotspot clients section
    m_hsClientsHeader = new QLabel(QStringLiteral("已连接客户端 (0)"), page);
    m_hsClientsHeader->setObjectName(QStringLiteral("sliderTag"));
    v->addWidget(m_hsClientsHeader);

    auto *scroll = makeTouchScrollArea(page, &m_hsClientsLayout);
    v->addWidget(scroll, 1);

    rebuildHotspotView();
    return page;
}

QWidget *SystemPopover::buildBluetoothPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    v->addWidget(makeIosSwitchCard(page, QStringLiteral("蓝牙"), &m_btToggle, QColor("#3b82f6")));
    m_btToggle->onToggled = [this] { toggleBluetoothPower(); };

    m_btSummaryLbl = new QLabel(QStringLiteral("正在读取蓝牙设备…"), page);
    m_btSummaryLbl->setObjectName(QStringLiteral("ccSub"));
    m_btSummaryLbl->setWordWrap(true);
    v->addWidget(m_btSummaryLbl);

    auto *scroll = makeTouchScrollArea(page, &m_btListLayout);
    v->addWidget(scroll, 1);
    return page;
}

QWidget *SystemPopover::buildMihomoPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // Prominent 3-way Quick Proxy Mode Switcher (走规则 / 全局代理 / 全部直连)
    auto *modeRow = new QHBoxLayout();
    modeRow->setSpacing(6);
    static const char *kModeIds[3]{"rule", "global", "direct"};
    static const char *kModeLabels[3]{"走规则", "全局代理", "全部直连"};
    static const char *kModeTones[3]{"green", "blue", "amber"};
    for (int i = 0; i < 3; ++i) {
        auto *mb = new QToolButton(page);
        mb->setObjectName(QStringLiteral("modeBtn"));
        mb->setProperty("tone", QString::fromLatin1(kModeTones[i]));
        mb->setText(QString::fromUtf8(kModeLabels[i]));
        mb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        mb->setMinimumHeight(36);
        mb->setCursor(Qt::PointingHandCursor);
        const QString modeId = QString::fromLatin1(kModeIds[i]);
        connect(mb, &QToolButton::clicked, this, [this, modeId] { setMihomoMode(modeId); });
        modeRow->addWidget(mb);
        m_mhModeBtns[i] = mb;
    }
    v->addLayout(modeRow);

    // Compact iOS TUN toggle + Delay test bar
    auto *tunCard = new QWidget(page);
    tunCard->setObjectName(QStringLiteral("bentoGroup"));
    tunCard->setFixedHeight(44);
    auto *tunRow = new QHBoxLayout(tunCard);
    tunRow->setContentsMargins(12, 6, 10, 6);
    tunRow->setSpacing(8);
    m_mhSummaryLbl = new QLabel(QStringLiteral("TUN 透明代理"), tunCard);
    m_mhSummaryLbl->setObjectName(QStringLiteral("rowTitle"));
    tunRow->addWidget(m_mhSummaryLbl, 1);

    m_mhDelayBtn = new QToolButton(tunCard);
    m_mhDelayBtn->setObjectName(QStringLiteral("tabBtn"));
    m_mhDelayBtn->setText(QStringLiteral("测速"));
    m_mhDelayBtn->setCursor(Qt::PointingHandCursor);
    connect(m_mhDelayBtn, &QToolButton::clicked, this, [this] {
        if (!m_mhSelectedGroup.isEmpty()) testMihomoGroupDelay(m_mhSelectedGroup);
    });
    tunRow->addWidget(m_mhDelayBtn);

    m_mhTunToggle = new IosToggleSwitch(tunCard);
    m_mhTunToggle->setOnColor(QColor("#10b981"));
    m_mhTunToggle->onToggled = [this] { toggleMihomoTun(); };
    tunRow->addWidget(m_mhTunToggle);
    v->addWidget(tunCard);

    // Group selector pills container
    auto *groupHost = new QWidget(page);
    m_mhGroupBarLayout = new QVBoxLayout(groupHost);
    m_mhGroupBarLayout->setContentsMargins(0, 0, 0, 0);
    m_mhGroupBarLayout->setSpacing(6);
    v->addWidget(groupHost);

    // Node scroll list
    auto *scroll = makeTouchScrollArea(page, &m_mhNodeListLayout);
    v->addWidget(scroll, 1);
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

void SystemPopover::hideEvent(QHideEvent *event)
{
    if (m_netStatsTimer) m_netStatsTimer->stop();
    m_hasLastStats = false;
    QWidget::hideEvent(event);
}

void SystemPopover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    // Borderless iPadOS / macOS Liquid Glass edge feathering (smooth alpha gradient
    // fading outward into the desktop with ZERO hard border line)
    const QRectF base = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
    struct FeatherStep {
        qreal expand;
        int alpha;
    };
    static const FeatherStep kFeather[] = {
        {10.0, 7},
        {7.5, 13},
        {5.0, 21},
        {2.5, 32},
    };
    for (const FeatherStep &s : kFeather) {
        const QRectF hr = base.adjusted(-s.expand, -s.expand, s.expand, s.expand);
        p.setBrush(QColor(14, 19, 32, s.alpha));
        p.drawRoundedRect(hr, 26.0 + s.expand * 0.5, 26.0 + s.expand * 0.5);
    }

    // Translucent frosted glass tint over real Wayland Gaussian blur
    QLinearGradient glass(base.topLeft(), base.bottomRight());
    glass.setColorAt(0.0, QColor(36, 46, 70, 126));
    glass.setColorAt(0.45, QColor(19, 25, 40, 132));
    glass.setColorAt(1.0, QColor(12, 16, 28, 144));
    p.setBrush(glass);
    p.drawRoundedRect(base, 26.0, 26.0);

    // Subtle iPadOS / macOS frosted top-left specular sheen gradient
    QRadialGradient sheen(base.topLeft() + QPointF(base.width() * 0.25, 0.0), base.width() * 0.9);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 28));
    sheen.setColorAt(0.55, QColor(255, 255, 255, 7));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(sheen);
    p.drawRoundedRect(base, 26.0, 26.0);
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
    const Page target = static_cast<Page>(qBound(0, static_cast<int>(tab), 5));
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
    syncNetStatsTimer();
}

void SystemPopover::showPage(Page page)
{
    m_currentPage = page;
    m_stack->setCurrentIndex(static_cast<int>(page));
    if (page != PageHotspot && m_hsEditing) {
        m_hsEditing = false;
        if (m_hsViewCard) m_hsViewCard->show();
        if (m_hsEditCard) m_hsEditCard->hide();
    }
    syncHeaderForPage();
    syncNetStatsTimer();
    refreshCurrentPage();
}

void SystemPopover::syncHeaderForPage()
{
    const bool isHome = (m_currentPage == PageHome);
    m_backBtn->setVisible(!isHome);
    m_titleLbl->setVisible(!isHome);
    if (m_powerPill) m_powerPill->setVisible(isHome);
    if (m_hdrSettingsBtn) m_hdrSettingsBtn->setVisible(isHome);
    if (m_hdrLockBtn) m_hdrLockBtn->setVisible(isHome);
    if (m_hdrPowerBtn) m_hdrPowerBtn->setVisible(isHome);
    if (m_currentPage == PageWifi) m_titleLbl->setText(QStringLiteral("Wi-Fi"));
    else if (m_currentPage == PageCellular) m_titleLbl->setText(QStringLiteral("4G 网络"));
    else if (m_currentPage == PageHotspot) m_titleLbl->setText(QStringLiteral("个人热点"));
    else if (m_currentPage == PageBluetooth) m_titleLbl->setText(QStringLiteral("蓝牙"));
    else if (m_currentPage == PageMihomo) m_titleLbl->setText(QStringLiteral("Mihomo"));
}

void SystemPopover::refreshCurrentPage()
{
    syncHomeFromSnapshot();
    if (m_currentPage == PageHome) {
        syncVolume();
        syncBrightness();
        refreshMpris();
        refreshWifi(false);
        refreshCellular();
        refreshHotspot();
        refreshBluetooth();
        refreshMihomo();
    } else if (m_currentPage == PageWifi) {
        refreshWifi(false);
    } else if (m_currentPage == PageCellular) {
        refreshCellular();
    } else if (m_currentPage == PageHotspot) {
        refreshHotspot();
    } else if (m_currentPage == PageBluetooth) {
        refreshBluetooth();
    } else {
        refreshMihomo();
    }
    pollLiveNetStats();
}

void SystemPopover::syncNetStatsTimer()
{
    if (!m_netStatsTimer) return;
    m_hasLastStats = false;
    m_lastStatsIface.clear();
    const bool needLive = isVisible() &&
        (m_currentPage == PageWifi || m_currentPage == PageCellular || m_currentPage == PageHotspot);
    if (needLive) {
        pollLiveNetStats();
        if (!m_netStatsTimer->isActive()) m_netStatsTimer->start();
    } else {
        m_netStatsTimer->stop();
    }
}

void SystemPopover::pollLiveNetStats()
{
#ifdef Q_OS_LINUX
    QString iface;
    if (m_currentPage == PageWifi && !m_cachedWifiSsid.isEmpty() && !m_hsActive) {
        iface = m_wifiIface;
    } else if (m_currentPage == PageCellular && m_wwanConnected) {
        iface = m_cellNetIface;
    } else if (m_currentPage == PageHotspot && m_hsActive) {
        iface = m_hsWifiDev;
    }

    if (iface.isEmpty()) {
        if (m_currentPage == PageCellular) {
            if (m_cellSpeedLbl) m_cellSpeedLbl->setText(QStringLiteral("↓ 0 B/s  ·  ↑ 0 B/s"));
            if (m_cellTrafficLbl) m_cellTrafficLbl->setText(QStringLiteral("↓ 0 B  ·  ↑ 0 B"));
        } else if (m_currentPage == PageHotspot && m_hsTrafficLbl) {
            m_hsTrafficLbl->setText(QStringLiteral("速率  ↓ 0 B/s · ↑ 0 B/s  |  累计  ↓ 0 B · ↑ 0 B"));
        }
        return;
    }

    quint64 rx = 0, tx = 0;
    if (!readSysfsNetBytes(iface, &rx, &tx)) return;

    quint64 rxRate = 0, txRate = 0;
    if (m_hasLastStats && m_lastStatsIface == iface) {
        rxRate = (rx >= m_lastRxBytes) ? (rx - m_lastRxBytes) : 0;
        txRate = (tx >= m_lastTxBytes) ? (tx - m_lastTxBytes) : 0;
    }
    m_lastStatsIface = iface;
    m_lastRxBytes = rx;
    m_lastTxBytes = tx;
    m_hasLastStats = true;

    if (m_currentPage == PageWifi && m_wifiSpeedLbl) {
        m_wifiSpeedLbl->setText(QStringLiteral("实时网速  ↓ %1  ·  ↑ %2")
                                    .arg(formatByteRate(rxRate), formatByteRate(txRate)));
    } else if (m_currentPage == PageCellular) {
        if (m_cellSpeedLbl)
            m_cellSpeedLbl->setText(QStringLiteral("↓ %1  ·  ↑ %2")
                                        .arg(formatByteRate(rxRate), formatByteRate(txRate)));
        if (m_cellTrafficLbl)
            m_cellTrafficLbl->setText(QStringLiteral("↓ %1  ·  ↑ %2")
                                          .arg(formatByteCount(rx), formatByteCount(tx)));
    } else if (m_currentPage == PageHotspot && m_hsTrafficLbl) {
        // On AP interface, tx_bytes = downstream delivered to clients, rx_bytes = upstream from clients
        m_hsTrafficLbl->setText(QStringLiteral("速率  ↓ %1 · ↑ %2\n累计  ↓ %3 · ↑ %4")
                                    .arg(formatByteRate(txRate), formatByteRate(rxRate),
                                         formatByteCount(tx), formatByteCount(rx)));
    }
#endif
}

void SystemPopover::syncHomeFromSnapshot()
{
    if (!m_monitor) return;
    const SystemStatusSnapshot &st = m_monitor->snapshot();

    // Header integrated Battery + Power Profile Pill
    if (m_powerPill) {
        QString pwrTxt = QStringLiteral("平衡");
        QString pwrAcc = QStringLiteral("blue");
        if (st.powerProfile == QLatin1String("power-saver")) {
            pwrTxt = QStringLiteral("节能");
            pwrAcc = QStringLiteral("green");
        } else if (st.powerProfile == QLatin1String("performance")) {
            pwrTxt = QStringLiteral("性能");
            pwrAcc = QStringLiteral("rose");
        }
        const QString label = st.batteryPresent
            ? QStringLiteral("%1%%2 %3")
                  .arg(st.batteryPercent)
                  .arg(st.batteryPluggedIn ? QStringLiteral("⚡") : QString(), pwrTxt)
            : pwrTxt;
        m_powerPill->setText(label);
        m_powerPill->setProperty("accent", pwrAcc);
        repolish(m_powerPill);
    }

    // Home Wi-Fi row
    const bool wifiOn = !m_cachedWifiSsid.isEmpty() && m_wifiRadioEnabled && !m_hsActive;
    styleCircleBadge(m_homeWifiBadge, FloatingIcons::Wifi,
                     wifiOn ? QColor("#2563eb") : QColor(255, 255, 255, 28),
                     wifiOn ? QColor("#ffffff") : QColor("#94a3b8"), 30);
    if (m_hsActive) {
        m_homeWifiSub->setText(QStringLiteral("热点共享中"));
    } else if (wifiOn) {
        m_homeWifiSub->setText(elideText(QStringLiteral("%1 · %2%").arg(m_cachedWifiSsid).arg(m_cachedWifiSignal), 13));
    } else {
        m_homeWifiSub->setText(m_wifiRadioEnabled ? QStringLiteral("未连接") : QStringLiteral("已关闭"));
    }

    // Home 4G WWAN row
    if (m_homeWwanRow) {
        m_homeWwanRow->setVisible(m_wwanAvailable);
        styleCircleBadge(m_homeWwanBadge, FloatingIcons::Cellular,
                         m_wwanEnabled ? QColor("#10b981") : QColor(255, 255, 255, 28),
                         m_wwanEnabled ? QColor("#ffffff") : QColor("#94a3b8"), 30);
        if (!m_wwanEnabled) m_homeWwanSub->setText(QStringLiteral("已关闭"));
        else if (m_wwanConnected) {
            const QString tech = m_cellTech.isEmpty() ? QStringLiteral("4G") : m_cellTech;
            m_homeWwanSub->setText(m_cellSignal >= 0
                ? QStringLiteral("%1 · %2%").arg(tech).arg(m_cellSignal)
                : QStringLiteral("已连接 · %1").arg(tech));
        } else m_homeWwanSub->setText(QStringLiteral("已开启"));
    }

    // Home Hotspot row
    styleCircleBadge(m_homeHotspotBadge, FloatingIcons::Hotspot,
                     m_hsActive ? QColor("#10b981") : QColor(255, 255, 255, 28),
                     m_hsActive ? QColor("#ffffff") : QColor("#94a3b8"), 30);
    if (m_hsActive) {
        const QString sec = m_hsUseWpa3 ? QStringLiteral("WPA3") : QStringLiteral("WPA2");
        const QString sub = m_hsClients.isEmpty()
            ? QStringLiteral("%1 · %2").arg(m_hsSsid, sec)
            : QStringLiteral("%1 · %2台").arg(m_hsSsid).arg(m_hsClients.size());
        m_homeHotspotSub->setText(elideText(sub, 13));
    } else {
        m_homeHotspotSub->setText(QStringLiteral("已关闭"));
    }

    // Home Bluetooth row
    const bool btOn = st.bluetoothPowered || m_btPowered;
    styleCircleBadge(m_homeBtBadge, FloatingIcons::Bluetooth,
                     btOn ? QColor("#3b82f6") : QColor(255, 255, 255, 28),
                     btOn ? QColor("#ffffff") : QColor("#94a3b8"), 30);
    if (!btOn) m_homeBtSub->setText(QStringLiteral("已关闭"));
    else if (!m_cachedBtSummary.isEmpty()) m_homeBtSub->setText(elideText(m_cachedBtSummary, 13));
    else m_homeBtSub->setText(QStringLiteral("已开启"));

    // Home Mihomo row
    const QColor mhBg = !m_mhReachable ? QColor(255, 255, 255, 28)
                        : (m_mhMode == QLatin1String("direct") ? QColor("#f59e0b") : QColor("#10b981"));
    styleCircleBadge(m_homeMhBadge, FloatingIcons::Proxy, mhBg,
                     m_mhReachable ? QColor("#ffffff") : QColor("#94a3b8"), 30);
    if (m_mhReachable) {
        const QString modeCn = (m_mhMode == QLatin1String("global")) ? QStringLiteral("全局")
                             : (m_mhMode == QLatin1String("direct") ? QStringLiteral("直连") : QStringLiteral("规则"));
        const QString tunTxt = m_mhTunEnabled ? QStringLiteral("TUN 开") : QStringLiteral("无TUN");
        m_homeMhSub->setText(QStringLiteral("%1 · %2").arg(modeCn, tunTxt));
    } else {
        m_homeMhSub->setText(QStringLiteral("本地未连接"));
    }

    // DND Wide Card
    m_dndTile->setProperty("on", st.dndEnabled);
    repolish(m_dndTile);
    styleCircleBadge(m_dndBadge, FloatingIcons::Bell,
                     st.dndEnabled ? QColor("#8b5cf6") : QColor(255, 255, 255, 28),
                     st.dndEnabled ? QColor("#ffffff") : QColor("#cbd5e1"), 28);
    m_dndSub->setText(st.dndEnabled ? QStringLiteral("已开启") : QStringLiteral("已关闭"));
}

// ============================================================================
// Audio & Brightness Capsule Sliders
// ============================================================================

void SystemPopover::applyAudioVisuals(int percent, bool muted, bool supported)
{
    m_volumeSlider->setEnabled(supported);
    if (!supported) {
        m_volumeSlider->setBadgeText(QStringLiteral("--"));
        m_volumeSlider->setIcon(FloatingIcons::VolumeMute);
        return;
    }
    m_muted = muted;
    m_lastVolume = qBound(0, percent, 100);
    m_volumeSlider->setValue(m_lastVolume);
    m_volumeSlider->setMutedAppearance(muted);
    m_volumeSlider->setIcon(muted ? FloatingIcons::VolumeMute : FloatingIcons::Volume);
    m_volumeSlider->setBadgeText(muted ? QStringLiteral("静音") : QStringLiteral("%1%").arg(m_lastVolume));
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
        if (!self || !self->m_brightnessSlider) return;
        self->m_brightnessSlider->setVisible(ok);
        if (ok) {
            const int clamped = qBound(1, pct, 100);
            self->m_brightnessSlider->setValue(clamped);
            self->m_brightnessSlider->setBadgeText(QStringLiteral("%1%").arg(clamped));
            emit self->brightnessStateChanged(clamped);
        }
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
        const QString artist = m_mprisArtist.isEmpty() ? QStringLiteral("正在播放") : m_mprisArtist;
        m_mprisTitleLbl->setText(m_mprisTitleLbl->fontMetrics().elidedText(m_mprisTitle, Qt::ElideRight, 130));
        m_mprisArtistLbl->setText(m_mprisArtistLbl->fontMetrics().elidedText(artist, Qt::ElideRight, 130));
        m_mprisPlayBtn->setIcon(FloatingIcons::make(
            m_mprisPlaying ? FloatingIcons::MediaPause : FloatingIcons::MediaPlay,
            QColor("#ffffff"), 14));
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
// Wi-Fi Subpage Implementation (Pure Wi-Fi: nmcli async + live speed)
// ============================================================================

void SystemPopover::refreshWifi(bool rescan)
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    if (rescan) showStatusBanner(QStringLiteral("正在扫描附近 Wi-Fi…"));
    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self) return;

        bool wifiEnabled = true;
        QString wifiIface;
        QString wifiIp;
        QList<WifiEntry> entries;
        QHash<QString, int> seenIndex;
        int section = 0;
        for (const QString &raw : out.split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line == QLatin1String("---DEV---")) { section = 1; continue; }
            if (line == QLatin1String("---WIFI---")) { section = 2; continue; }
            if (line.isEmpty()) continue;

            if (section == 0) {
                wifiEnabled = (line.compare(QStringLiteral("enabled"), Qt::CaseInsensitive) == 0);
            } else if (section == 1) {
                if (line.startsWith(QLatin1String("IFACE="))) wifiIface = line.mid(6).trimmed();
                else if (line.startsWith(QLatin1String("IP="))) wifiIp = line.mid(3).trimmed().section(QLatin1Char('/'), 0, 0);
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
                if (parts.size() >= 5) e.rate = parts[4].trimmed();
                if (parts.size() >= 6) e.freq = parts[5].trimmed();
                if (seenIndex.contains(ssid)) {
                    WifiEntry &ex = entries[seenIndex[ssid]];
                    if (e.active) {
                        ex.active = true;
                        if (!e.rate.isEmpty()) ex.rate = e.rate;
                        if (!e.freq.isEmpty()) ex.freq = e.freq;
                    }
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
        self->m_wifiIface = wifiIface;
        self->m_wifiIp = wifiIp;
        self->rebuildWifiList(wifiEnabled, entries);
    });
    const QString rescanFlag = rescan ? QStringLiteral("yes") : QStringLiteral("no");
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral(
                     "nmcli -t -f WIFI radio wifi; echo '---DEV---'; "
                     "WDEV=$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2==\"wifi\"{print $1; exit}'); "
                     "echo \"IFACE=$WDEV\"; "
                     "[ -n \"$WDEV\" ] && echo \"IP=$(nmcli -g IP4.ADDRESS device show \"$WDEV\" 2>/dev/null | head -n1)\"; "
                     "echo '---WIFI---'; "
                     "nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY,RATE,FREQ device wifi list --rescan %1").arg(rescanFlag)});
#else
    Q_UNUSED(rescan);
#endif
}

void SystemPopover::rebuildWifiList(bool wifiEnabled, const QList<WifiEntry> &entries)
{
    m_wifiRadioEnabled = wifiEnabled;
    if (m_wifiToggle) m_wifiToggle->setChecked(wifiEnabled);

    m_cachedWifiSsid.clear();
    m_cachedWifiSignal = 0;
    m_wifiRate.clear();
    m_wifiFreq.clear();
    m_wifiSecurity.clear();
    for (const WifiEntry &e : entries) {
        if (e.active) {
            m_cachedWifiSsid = e.ssid;
            m_cachedWifiSignal = e.signal;
            m_wifiSecurity = e.security;
            m_wifiRate = e.rate;
            m_wifiFreq = e.freq;
            break;
        }
    }
    syncHomeFromSnapshot();

    // Update Connected Wi-Fi Info Card (SSID, signal, security, IP, band/rate, live speed)
    const bool showDetails = wifiEnabled && !m_cachedWifiSsid.isEmpty() && !m_hsActive;
    if (m_wifiDetailCard) {
        m_wifiDetailCard->setVisible(showDetails);
        if (showDetails) {
            const QString sec = (m_wifiSecurity.isEmpty() || m_wifiSecurity == QLatin1String("--"))
                ? QStringLiteral("开放") : m_wifiSecurity;
            m_wifiDetailTitle->setText(QStringLiteral("✓ %1  ·  %2%")
                                           .arg(elideText(m_cachedWifiSsid, 18)).arg(m_cachedWifiSignal));
            QStringList meta;
            if (!m_wifiIp.isEmpty()) meta.append(QStringLiteral("IP ") + m_wifiIp);
            if (!m_wifiFreq.isEmpty()) {
                const int mhz = m_wifiFreq.section(QLatin1Char(' '), 0, 0).toInt();
                if (mhz >= 5900) meta.append(QStringLiteral("6GHz"));
                else if (mhz >= 4900) meta.append(QStringLiteral("5GHz"));
                else if (mhz > 0) meta.append(QStringLiteral("2.4GHz"));
            }
            if (!m_wifiRate.isEmpty()) meta.append(m_wifiRate);
            meta.append(sec);
            m_wifiDetailMeta->setText(meta.join(QStringLiteral(" · ")));
            pollLiveNetStats();
        }
    }

    clearLayout(m_wifiListLayout);
    if (!wifiEnabled) {
        auto *lbl = new QLabel(QStringLiteral("Wi-Fi 已关闭"), m_wifiListLayout->parentWidget());
        lbl->setAlignment(Qt::AlignCenter);
        m_wifiListLayout->addWidget(lbl);
        m_wifiListLayout->addStretch(1);
        return;
    }
    if (m_hsActive) {
        auto *lbl = new QLabel(QStringLiteral("当前无线网卡正在提供个人热点"), m_wifiListLayout->parentWidget());
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignCenter);
        m_wifiListLayout->addWidget(lbl);
        m_wifiListLayout->addStretch(1);
        return;
    }
    if (entries.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("附近暂未发现 Wi-Fi（点右上角 ↻ 扫描）"), m_wifiListLayout->parentWidget());
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignCenter);
        m_wifiListLayout->addWidget(lbl);
        m_wifiListLayout->addStretch(1);
        return;
    }

    QWidget *rowParent = m_wifiListLayout->parentWidget();
    const int limit = qMin(entries.size(), 16);
    for (int i = 0; i < limit; ++i) {
        const WifiEntry entry = entries[i];
        auto *btn = new QPushButton(rowParent);
        btn->setObjectName(QStringLiteral("rowBtn"));
        btn->setProperty("connected", entry.active);
        btn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        btn->setMinimumHeight(40);
        const QString lock = (!entry.security.isEmpty() && entry.security != QLatin1String("--"))
            ? QStringLiteral("🔒 ") : QString();
        const QString rightTag = entry.active
            ? QStringLiteral("已连 · %1%").arg(entry.signal)
            : QStringLiteral("%1%").arg(entry.signal);
        btn->setText(QStringLiteral("%1%2   [%3]").arg(lock, elideText(entry.ssid, 17), rightTag));
        btn->setToolTip(entry.ssid);
        connect(btn, &QPushButton::clicked, this, [this, entry] { connectWifi(entry); });
        attachTouchScroll(m_wifiListLayout, btn);
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

// ============================================================================
// 4G Cellular Subpage Implementation (Pure 4G: nmcli + mmcli + live traffic)
// ============================================================================

void SystemPopover::refreshCellular()
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self) return;

        bool wwanHw = false;
        bool wwanOn = false;
        bool connected = false;
        QString ipIface;
        QString ipAddr;
        QString opName;
        QString tech;
        QString mmState;
        QString model;
        int signal = -1;

        int section = 0;
        for (const QString &raw : out.split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line == QLatin1String("---DEV---")) { section = 1; continue; }
            if (line == QLatin1String("---MM---")) { section = 2; continue; }
            if (line.isEmpty()) continue;

            if (section == 0) {
                const QStringList r = line.split(QLatin1Char(':'));
                if (r.size() >= 2) {
                    wwanHw = (r[0].compare(QStringLiteral("enabled"), Qt::CaseInsensitive) == 0);
                    wwanOn = (r[1].compare(QStringLiteral("enabled"), Qt::CaseInsensitive) == 0);
                }
            } else if (section == 1) {
                if (line.startsWith(QLatin1String("STATE="))) {
                    connected = line.mid(6).contains(QLatin1String("connected"), Qt::CaseInsensitive)
                             && !line.mid(6).contains(QLatin1String("disconnected"), Qt::CaseInsensitive);
                } else if (line.startsWith(QLatin1String("IPIF="))) {
                    ipIface = line.mid(5).trimmed();
                } else if (line.startsWith(QLatin1String("IP="))) {
                    ipAddr = line.mid(3).trimmed().section(QLatin1Char('/'), 0, 0);
                }
            } else if (section == 2) {
                const int colon = line.indexOf(QLatin1Char(':'));
                if (colon <= 0) continue;
                const QString k = line.left(colon).trimmed();
                const QString v = line.mid(colon + 1).trimmed();
                if (v.isEmpty() || v == QLatin1String("--")) continue;
                if (k == QLatin1String("modem.3gpp.operator-name")) opName = v;
                else if (k == QLatin1String("modem.generic.access-technologies.value[1]")) tech = v.toUpper();
                else if (k == QLatin1String("modem.generic.signal-quality.value")) signal = v.toInt();
                else if (k == QLatin1String("modem.generic.state")) mmState = v;
                else if (k == QLatin1String("modem.generic.model")) model = v;
            }
        }

        self->m_wwanAvailable = wwanHw;
        self->m_wwanEnabled = wwanOn;
        self->m_wwanConnected = connected || (mmState == QLatin1String("connected"));
        self->m_cellNetIface = ipIface;
        self->m_cellIp = ipAddr;
        self->m_cellOperator = opName;
        self->m_cellTech = (tech == QLatin1String("LTE")) ? QStringLiteral("4G LTE") : tech;
        self->m_cellSignal = signal;
        self->m_cellStateText = mmState;
        self->m_cellModemModel = model;
        self->rebuildCellularView();
        self->syncHomeFromSnapshot();
    });
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral(
                     "nmcli -t -f WWAN-HW,WWAN radio all; echo '---DEV---'; "
                     "GDEV=$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2==\"gsm\" || $2==\"cdma\"{print $1; exit}'); "
                     "if [ -n \"$GDEV\" ]; then "
                     "  echo \"STATE=$(nmcli -g GENERAL.STATE device show \"$GDEV\" 2>/dev/null | head -n1)\"; "
                     "  echo \"IPIF=$(nmcli -g GENERAL.IP-IFACE device show \"$GDEV\" 2>/dev/null | head -n1)\"; "
                     "  echo \"IP=$(nmcli -g IP4.ADDRESS device show \"$GDEV\" 2>/dev/null | head -n1)\"; "
                     "fi; echo '---MM---'; "
                     "command -v mmcli >/dev/null 2>&1 && mmcli -m any -K 2>/dev/null || true")});
#endif
}

void SystemPopover::rebuildCellularView()
{
    if (m_cellToggle) m_cellToggle->setChecked(m_wwanEnabled);
    if (!m_cellOpLbl) return;

    if (!m_wwanEnabled) {
        m_cellOpLbl->setText(QStringLiteral("已关闭"));
        m_cellSignalLbl->setText(QStringLiteral("无信号"));
        m_cellIpLbl->setText(QStringLiteral("--"));
        m_cellSpeedLbl->setText(QStringLiteral("↓ 0 B/s  ·  ↑ 0 B/s"));
        m_cellTrafficLbl->setText(QStringLiteral("↓ 0 B  ·  ↑ 0 B"));
        return;
    }

    QString opStr = m_cellOperator.isEmpty() ? QStringLiteral("搜网中…") : m_cellOperator;
    if (!m_cellTech.isEmpty()) opStr += QStringLiteral(" · ") + m_cellTech;
    m_cellOpLbl->setText(opStr);

    const QString stCn = m_wwanConnected ? QStringLiteral("已连接")
        : (m_cellStateText == QLatin1String("registered") ? QStringLiteral("已注册") : QStringLiteral("连接中…"));
    m_cellSignalLbl->setText(m_cellSignal >= 0
        ? QStringLiteral("%1 · %2%").arg(stCn).arg(m_cellSignal)
        : stCn);

    QStringList ipParts;
    if (!m_cellIp.isEmpty()) ipParts.append(m_cellIp);
    if (!m_cellNetIface.isEmpty()) ipParts.append(m_cellNetIface);
    m_cellIpLbl->setText(ipParts.isEmpty() ? QStringLiteral("未分配") : ipParts.join(QStringLiteral(" · ")));

    pollLiveNetStats();
}

void SystemPopover::toggleWwanRadio()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    const bool turnOn = !m_wwanEnabled;
    QProcess::startDetached(nm, {QStringLiteral("radio"), QStringLiteral("wwan"),
                                 turnOn ? QStringLiteral("on") : QStringLiteral("off")});
    showStatusBanner(turnOn ? QStringLiteral("正在开启 4G 移动网络…") : QStringLiteral("已关闭 4G 移动网络"));
    QPointer<SystemPopover> self(this);
    QTimer::singleShot(1400, this, [self] {
        if (self) self->refreshCellular();
    });
    if (turnOn) {
        QTimer::singleShot(4000, this, [self] {
            if (self) self->refreshCellular();
        });
    }
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
    if (m_btToggle) m_btToggle->setChecked(powered);

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
        attachTouchScroll(m_btListLayout, btn);
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

    if (m_mhTunToggle) m_mhTunToggle->setChecked(m_mhTunEnabled);

    m_mhSummaryLbl->setText(m_mhTunEnabled
        ? QStringLiteral("TUN 透明代理 · 已开启")
        : QStringLiteral("TUN 透明代理 · 已关闭"));

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
        attachTouchScroll(m_mhNodeListLayout, btn);
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

// ============================================================================
// Personal Hotspot Implementation (NM AP shared mode: WPA3-SAE + 4G/Ethernet)
// ============================================================================

QString SystemPopover::defaultHotspotSsid() const
{
    QString host = QHostInfo::localHostName().section(QLatin1Char('.'), 0, 0).trimmed();
    if (host.isEmpty()) host = QStringLiteral("Hotspot");
    return host;
}

void SystemPopover::loadHotspotPrefs()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    m_hsSsid = s.value(QStringLiteral("hotspot/ssid"), defaultHotspotSsid()).toString().trimmed();
    if (m_hsSsid.isEmpty()) m_hsSsid = defaultHotspotSsid();
    m_hsPassword = s.value(QStringLiteral("hotspot/password")).toString();
    if (m_hsPassword.size() < 8) {
        // Auto-generate an unambiguous 10-character passphrase on first run (never hardcoded)
        static const QString kAlphabet = QStringLiteral("abcdefghjkmnpqrstuvwxyz23456789ABCDEFGHJKLMNPQRSTUVWXYZ");
        QString gen;
        gen.reserve(10);
        for (int i = 0; i < 10; ++i) {
            gen.append(kAlphabet.at(QRandomGenerator::system()->bounded(kAlphabet.size())));
        }
        m_hsPassword = gen;
        s.setValue(QStringLiteral("hotspot/password"), m_hsPassword);
    }
    m_hsUseWpa3 = s.value(QStringLiteral("hotspot/wpa3"), true).toBool();
}

void SystemPopover::saveHotspotPrefs()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue(QStringLiteral("hotspot/ssid"), m_hsSsid);
    s.setValue(QStringLiteral("hotspot/password"), m_hsPassword);
    s.setValue(QStringLiteral("hotspot/wpa3"), m_hsUseWpa3);
}

void SystemPopover::refreshHotspot()
{
#ifdef Q_OS_LINUX
    if (QGuiApplication::platformName() == QLatin1String("offscreen")) return;
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;

    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self) return;

        QString wifiDev;
        bool apActive = false;
        QString apSsid;
        QString apBand;
        QHash<QString, QString> macToHost;
        QList<HotspotClient> clients;

        int section = 0;
        for (const QString &raw : out.split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line == QLatin1String("---AP---")) { section = 1; continue; }
            if (line == QLatin1String("---LEASES---")) { section = 2; continue; }
            if (line == QLatin1String("---NEIGH---")) { section = 3; continue; }
            if (line.isEmpty()) continue;

            if (section == 0) {
                const QStringList p = line.split(QLatin1Char(':'));
                if (p.size() >= 2 && p[1] == QLatin1String("wifi") && wifiDev.isEmpty()) {
                    wifiDev = p[0];
                }
            } else if (section == 1) {
                if (line.startsWith(QLatin1String("MODE="))) {
                    apActive = (line.mid(5).trimmed() == QLatin1String("ap"));
                } else if (line.startsWith(QLatin1String("SSID="))) {
                    apSsid = line.mid(5).trimmed();
                } else if (line.startsWith(QLatin1String("BAND="))) {
                    const QString b = line.mid(5).trimmed();
                    if (b == QLatin1String("a")) apBand = QStringLiteral("5GHz");
                    else if (b == QLatin1String("bg")) apBand = QStringLiteral("2.4GHz");
                }
            } else if (section == 2) {
                const QStringList p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (p.size() >= 4) {
                    const QString mac = p[1].toLower();
                    const QString host = (p[3] == QLatin1String("*")) ? QString() : p[3];
                    if (!host.isEmpty()) macToHost.insert(mac, host);
                }
            } else if (section == 3) {
                const QStringList p = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (p.size() >= 4 && p[1] == QLatin1String("lladdr")) {
                    const QString state = p.last().toUpper();
                    if (state == QLatin1String("FAILED") || state == QLatin1String("INCOMPLETE"))
                        continue;
                    HotspotClient c;
                    c.ip = p[0];
                    c.mac = p[2].toLower();
                    c.host = macToHost.value(c.mac);
                    clients.append(c);
                }
            }
        }

        self->m_hsWifiDev = wifiDev;
        self->m_hsActive = apActive;
        if (apActive && !apSsid.isEmpty()) self->m_hsSsid = apSsid;
        self->m_hsActiveBand = apActive ? apBand : QString();
        self->m_hsClients = apActive ? clients : QList<HotspotClient>{};
        self->rebuildHotspotView();
        self->syncHomeFromSnapshot();
    });

    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral(
                     "nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status; "
                     "WIFI_DEV=$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2==\"wifi\"{print $1; exit}'); "
                     "ACTIVE_CON=$(nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status | awk -F: '$2==\"wifi\" && $3==\"connected\"{print $4; exit}'); "
                     "echo '---AP---'; "
                     "if [ -n \"$ACTIVE_CON\" ]; then "
                     "  echo \"MODE=$(nmcli -g 802-11-wireless.mode connection show \"$ACTIVE_CON\" 2>/dev/null)\"; "
                     "  echo \"SSID=$(nmcli -g 802-11-wireless.ssid connection show \"$ACTIVE_CON\" 2>/dev/null)\"; "
                     "  echo \"BAND=$(nmcli -g 802-11-wireless.band connection show \"$ACTIVE_CON\" 2>/dev/null)\"; "
                     "fi; "
                     "echo '---LEASES---'; "
                     "[ -n \"$WIFI_DEV\" ] && [ -r \"/var/lib/NetworkManager/dnsmasq-$WIFI_DEV.leases\" ] && cat \"/var/lib/NetworkManager/dnsmasq-$WIFI_DEV.leases\" || true; "
                     "echo '---NEIGH---'; "
                     "[ -n \"$WIFI_DEV\" ] && ip -4 neigh show dev \"$WIFI_DEV\" 2>/dev/null || true")});
#endif
}

void SystemPopover::setHotspotEditMode(bool editing)
{
    m_hsEditing = editing;
    if (editing) {
        if (m_hsSsidEdit) m_hsSsidEdit->setText(m_hsSsid);
        if (m_hsPassEdit) {
            m_hsPassEdit->setText(m_hsPassword);
            m_hsPassEdit->setCursorPosition(0);
        }
    }
    if (m_hsViewCard) m_hsViewCard->setVisible(!editing);
    if (m_hsEditCard) m_hsEditCard->setVisible(editing);
    rebuildHotspotView();
}

void SystemPopover::rebuildHotspotView()
{
    if (m_hsToggle) {
        m_hsToggle->setEnabled(!m_hsBusy);
        m_hsToggle->setChecked(m_hsActive);
    }

    const QString secTxt = m_hsUseWpa3 ? QStringLiteral("WPA3") : QStringLiteral("WPA2");
    if (m_hsInfoTitle) {
        const QString bandPart = (m_hsActive && !m_hsActiveBand.isEmpty())
            ? (QStringLiteral(" · ") + m_hsActiveBand) : QString();
        const QString stateTxt = m_hsBusy ? QStringLiteral("切换中…")
                                          : (m_hsActive ? QStringLiteral("运行中") : QStringLiteral("已关闭"));
        m_hsInfoTitle->setText(QStringLiteral("%1 · %2%3 (%4)")
                                   .arg(m_hsSsid, secTxt, bandPart, stateTxt));
    }
    if (m_hsPassValueLbl) {
        m_hsPassValueLbl->setText(m_hsPassRevealed ? m_hsPassword
                                                   : QString(qMin(10, qMax(8, m_hsPassword.size())), QChar(0x2022)));
    }
    if (m_hsViewEyeBtn) {
        m_hsViewEyeBtn->setText(m_hsPassRevealed ? QStringLiteral("隐藏") : QStringLiteral("显示"));
    }

    if (m_hsSecBtns[0] && m_hsSecBtns[1]) {
        m_hsSecBtns[0]->setProperty("active", m_hsUseWpa3);
        m_hsSecBtns[1]->setProperty("active", !m_hsUseWpa3);
        repolish(m_hsSecBtns[0]);
        repolish(m_hsSecBtns[1]);
    }

    if (m_hsClientsHeader) {
        m_hsClientsHeader->setText(QStringLiteral("已连接客户端 (%1)").arg(m_hsClients.size()));
    }

    clearLayout(m_hsClientsLayout);
    if (!m_hsActive) {
        auto *lbl = new QLabel(QStringLiteral("开启热点后将在此显示接入设备"), m_hsClientsLayout->parentWidget());
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignCenter);
        m_hsClientsLayout->addWidget(lbl);
        m_hsClientsLayout->addStretch(1);
    } else if (m_hsClients.isEmpty()) {
        auto *lbl = new QLabel(QStringLiteral("热点已广播（10.42.0.1/24），等待设备连接…"), m_hsClientsLayout->parentWidget());
        lbl->setAlignment(Qt::AlignCenter);
        m_hsClientsLayout->addWidget(lbl);
        m_hsClientsLayout->addStretch(1);
    } else {
        for (const HotspotClient &c : m_hsClients) {
            auto *row = new QPushButton(m_hsClientsLayout->parentWidget());
            row->setObjectName(QStringLiteral("rowBtn"));
            row->setProperty("connected", true);
            row->setMinimumHeight(38);
            const QString title = c.host.isEmpty() ? c.ip : QStringLiteral("%1 (%2)").arg(c.host, c.ip);
            row->setText(QStringLiteral("📱 %1 · %2").arg(title, c.mac));
            connect(row, &QPushButton::clicked, this, [this, c] {
                if (QClipboard *cb = QGuiApplication::clipboard()) cb->setText(c.ip);
                showStatusBanner(QStringLiteral("✓ 已复制设备 IP: %1").arg(c.ip));
            });
            attachTouchScroll(m_hsClientsLayout, row);
            m_hsClientsLayout->addWidget(row);
        }
        m_hsClientsLayout->addStretch(1);
    }
    pollLiveNetStats();
}

void SystemPopover::applyHotspotConfig(bool restartIfActive)
{
    const QString nextSsid = m_hsSsidEdit ? m_hsSsidEdit->text().trimmed() : m_hsSsid;
    const QString nextPass = m_hsPassEdit ? m_hsPassEdit->text() : m_hsPassword;
    if (nextSsid.isEmpty()) {
        showStatusBanner(QStringLiteral("热点名称不能为空"), true);
        return;
    }
    if (nextPass.size() < 8) {
        showStatusBanner(QStringLiteral("密码至少需要 8 位"), true);
        return;
    }
    m_hsSsid = nextSsid;
    m_hsPassword = nextPass;
    saveHotspotPrefs();
    setHotspotEditMode(false);
    syncHomeFromSnapshot();
    if (restartIfActive) {
        startHotspotAsync();
    } else {
        showStatusBanner(QStringLiteral("✓ 热点配置已保存（%1 · %2）")
                             .arg(m_hsSsid, m_hsUseWpa3 ? QStringLiteral("WPA3") : QStringLiteral("WPA2")));
    }
}

void SystemPopover::toggleHotspot()
{
    if (m_hsBusy) return;
    if (m_hsActive) {
        stopHotspotAsync();
    } else {
        if (m_hsEditing) {
            const QString editSsid = m_hsSsidEdit ? m_hsSsidEdit->text().trimmed() : m_hsSsid;
            const QString editPass = m_hsPassEdit ? m_hsPassEdit->text() : m_hsPassword;
            if (!editSsid.isEmpty()) m_hsSsid = editSsid;
            if (editPass.size() >= 8) m_hsPassword = editPass;
            saveHotspotPrefs();
            setHotspotEditMode(false);
        }
        if (m_hsPassword.size() < 8) {
            showStatusBanner(QStringLiteral("请先点击「编辑」设置至少 8 位密码"), true);
            return;
        }
        startHotspotAsync();
    }
}

void SystemPopover::startHotspotAsync()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) {
        showStatusBanner(QStringLiteral("未找到 nmcli"), true);
        return;
    }
    m_hsBusy = true;
    rebuildHotspotView();
    showStatusBanner(QStringLiteral("正在启动 %1 热点 (%2)…")
                         .arg(m_hsSsid, m_hsUseWpa3 ? QStringLiteral("WPA3") : QStringLiteral("WPA2")));

    auto *proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("FB_HS_CON"), QStringLiteral("FloatingBall-Hotspot"));
    env.insert(QStringLiteral("FB_HS_SSID"), m_hsSsid);
    env.insert(QStringLiteral("FB_HS_PSK"), m_hsPassword);
    env.insert(QStringLiteral("FB_HS_SEC"), m_hsUseWpa3 ? QStringLiteral("sae") : QStringLiteral("wpa-psk"));
    proc->setProcessEnvironment(env);

    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int code, QProcess::ExitStatus st) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        proc->deleteLater();
        if (!self) return;
        self->m_hsBusy = false;
        const bool active5g = out.contains(QLatin1String("FB_BAND_5G"));
        const QString sec = self->m_hsUseWpa3 ? QStringLiteral("WPA3") : QStringLiteral("WPA2");
        if (st == QProcess::NormalExit && code == 0) {
            self->m_hsActive = true;
            self->m_hsActiveBand = active5g ? QStringLiteral("5GHz") : QStringLiteral("2.4GHz");
            const QString msg = QStringLiteral("✓ 热点已开启 · %1 (%2 / %3)")
                .arg(self->m_hsSsid, sec, self->m_hsActiveBand);
            self->showStatusBanner(msg);
            if (!self->isVisible()) DesktopActions::showToast(msg, self->geometry().center());
        } else {
            QString msg = err.isEmpty() ? out : err;
            if (msg.size() > 80) msg = msg.left(80) + QStringLiteral("…");
            const QString fullErr = QStringLiteral("开启热点失败: %1")
                .arg(msg.isEmpty() ? QStringLiteral("请检查 Wi-Fi 网卡状态") : msg);
            self->showStatusBanner(fullErr, true);
            if (!self->isVisible()) DesktopActions::showToast(fullErr, self->geometry().center());
        }
        self->refreshHotspot();
        self->refreshWifi(false);
    });

    // Safe env-driven NM WPA3-SAE / WPA2-PSK shared AP startup:
    // Automatically prefers 5GHz first and seamlessly falls back to 2.4GHz
    // when Wi-Fi firmware (such as Intel LAR) disallows 5GHz AP radiation.
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral(
                     "set -e; "
                     "if [ \"$(nmcli radio wifi)\" != \"enabled\" ]; then "
                     "  nmcli radio wifi on; sleep 1.2; "
                     "fi; "
                     "WIFI_DEV=$(nmcli -t -f DEVICE,TYPE device status | awk -F: '$2==\"wifi\"{print $1; exit}'); "
                     "if [ -z \"$WIFI_DEV\" ]; then echo '未找到可用 Wi-Fi 网卡' >&2; exit 2; fi; "
                     "create_ap() { "
                     "  nmcli connection down \"$FB_HS_CON\" >/dev/null 2>&1 || true; "
                     "  nmcli connection delete \"$FB_HS_CON\" >/dev/null 2>&1 || true; "
                     "  if [ \"$FB_HS_SEC\" = \"sae\" ]; then "
                     "    nmcli connection add type wifi ifname \"$WIFI_DEV\" con-name \"$FB_HS_CON\" autoconnect no "
                     "      ssid \"$FB_HS_SSID\" 802-11-wireless.mode ap 802-11-wireless.band \"$1\" "
                     "      ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method ignore "
                     "      wifi-sec.key-mgmt sae wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp wifi-sec.pmf 3 "
                     "      wifi-sec.psk \"$FB_HS_PSK\" >/dev/null; "
                     "  else "
                     "    nmcli connection add type wifi ifname \"$WIFI_DEV\" con-name \"$FB_HS_CON\" autoconnect no "
                     "      ssid \"$FB_HS_SSID\" 802-11-wireless.mode ap 802-11-wireless.band \"$1\" "
                     "      ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method ignore "
                     "      wifi-sec.key-mgmt wpa-psk wifi-sec.proto rsn wifi-sec.pairwise ccmp wifi-sec.group ccmp "
                     "      wifi-sec.psk \"$FB_HS_PSK\" >/dev/null; "
                     "  fi; "
                     "}; "
                     "create_ap a; "
                     "if nmcli -w 6 connection up \"$FB_HS_CON\" >/dev/null 2>&1; then "
                     "  echo 'FB_BAND_5G'; "
                     "else "
                     "  create_ap bg; "
                     "  nmcli -w 15 connection up \"$FB_HS_CON\"; "
                     "  echo 'FB_BAND_24G'; "
                     "fi")});
#endif
}

void SystemPopover::stopHotspotAsync()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return;
    m_hsBusy = true;
    rebuildHotspotView();
    showStatusBanner(QStringLiteral("正在关闭热点…"));

    auto *proc = new QProcess(this);
    QPointer<SystemPopover> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int, QProcess::ExitStatus) {
        proc->deleteLater();
        if (!self) return;
        self->m_hsBusy = false;
        self->m_hsActive = false;
        self->showStatusBanner(QStringLiteral("✓ 热点已关闭"));
        self->refreshHotspot();
        self->refreshWifi(false);
    });
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral(
                     "ACTIVE_CON=$(nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device status | awk -F: '$2==\"wifi\" && $3==\"connected\"{print $4; exit}'); "
                     "if [ -n \"$ACTIVE_CON\" ] && [ \"$(nmcli -g 802-11-wireless.mode connection show \"$ACTIVE_CON\" 2>/dev/null)\" = \"ap\" ]; then "
                     "  nmcli connection down \"$ACTIVE_CON\" >/dev/null 2>&1 || true; "
                     "else "
                     "  nmcli connection down FloatingBall-Hotspot >/dev/null 2>&1 || true; "
                     "fi")});
#endif
}
