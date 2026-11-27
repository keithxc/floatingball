#include "floatingball.h"
#include "./ui_floatingball.h"
#include "applauncher.h"
#include "clipboardbubble.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "panelbackdrop.h"
#include "quickdeck.h"
#include "systempopover.h"
#include "uiicons.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>
#include <QtMath>
#include <cmath>

namespace {

FloatingIcons::Icon iconForAction(const QString &id)
{
    if (id == QLatin1String("back")) return FloatingIcons::Back;
    if (id == QLatin1String("forward")) return FloatingIcons::Forward;
    if (id == QLatin1String("overview")) return FloatingIcons::Overview;
    if (id == QLatin1String("desktop") || id == QLatin1String("minimize_all")) return FloatingIcons::Desktop;
    if (id == QLatin1String("crosshair") || id == QLatin1String("capture")) return FloatingIcons::Crosshair;
    if (id == QLatin1String("clipboard")) return FloatingIcons::Clipboard;
    if (id == QLatin1String("deck")) return FloatingIcons::Deck;
    if (id == QLatin1String("paste")) return FloatingIcons::Paste;
    if (id == QLatin1String("copy")) return FloatingIcons::Copy;
    if (id == QLatin1String("record")) return FloatingIcons::Record;
    if (id == QLatin1String("ime")) return FloatingIcons::Globe;
    if (id == QLatin1String("terminal")) return FloatingIcons::Terminal;
    if (id == QLatin1String("browser")) return FloatingIcons::Browser;
    if (id == QLatin1String("home")) return FloatingIcons::Folder;
    if (id == QLatin1String("downloads")) return FloatingIcons::Downloads;
    if (id == QLatin1String("trash")) return FloatingIcons::Trash;
    if (id == QLatin1String("tray_hub")) return FloatingIcons::TrayHub;
    if (id == QLatin1String("sys_hub")) return FloatingIcons::SystemHub;
    if (id == QLatin1String("win_hub")) return FloatingIcons::WindowsHub;
    if (id == QLatin1String("tools_hub")) return FloatingIcons::ToolsHub;
    if (id == QLatin1String("launcher")) return FloatingIcons::Launcher;
    if (id == QLatin1String("close_active")) return FloatingIcons::Close;
    if (id == QLatin1String("sys_wifi")) return FloatingIcons::Wifi;
    if (id == QLatin1String("sys_bluetooth")) return FloatingIcons::Bluetooth;
    if (id == QLatin1String("sys_mihomo")) return FloatingIcons::Proxy;
    if (id == QLatin1String("sys_dnd") || id == QLatin1String("notifications")) return FloatingIcons::Bell;
    if (id == QLatin1String("sys_nightlight")) return FloatingIcons::Moon;
    if (id == QLatin1String("sys_power")) return FloatingIcons::Bolt;
    if (id == QLatin1String("sys_display")) return FloatingIcons::Desktop;
    if (id == QLatin1String("sys_audio")) return FloatingIcons::Volume;
    if (id == QLatin1String("sys_lock")) return FloatingIcons::Lock;
    if (id == QLatin1String("sys_sleep")) return FloatingIcons::Power;
    if (id == QLatin1String("sys_settings")) return FloatingIcons::Settings;
    return FloatingIcons::Orb;
}

void drawRedBadgePill(QPainter &p, const QPointF &center, int count, const QFont &baseFont)
{
    p.save();
    const bool showNum = count > 1;
    const QString text = count > 9 ? QStringLiteral("9+") : QString::number(count);
    const qreal glowR = showNum ? 11.5 : 8.2;
    QRadialGradient glow(center, glowR);
    glow.setColorAt(0.0, QColor(255, 68, 102, 235));
    glow.setColorAt(1.0, QColor(255, 68, 102, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(glow);
    p.drawEllipse(center, glowR, glowR);

    if (showNum) {
        const qreal pillW = text.size() > 1 ? 18.0 : 14.5;
        const qreal pillH = 14.5;
        const QRectF r(center.x() - pillW * 0.5, center.y() - pillH * 0.5, pillW, pillH);
        p.setBrush(QColor("#ff3b5c"));
        p.setPen(QPen(QColor("#ffffff"), 1.2));
        p.drawRoundedRect(r, pillH * 0.5, pillH * 0.5);

        QFont f = baseFont;
        f.setPixelSize(9);
        f.setWeight(QFont::Bold);
        p.setFont(f);
        p.setPen(QColor("#ffffff"));
        p.drawText(r, Qt::AlignCenter, text);
    } else {
        p.setBrush(QColor("#ff3b5c"));
        p.setPen(QPen(QColor("#ffffff"), 1.2));
        p.drawEllipse(center, 4.6, 4.6);
    }
    p.restore();
}

double normalizeAngleDeg(double deg)
{
    while (deg <= -180.0) deg += 360.0;
    while (deg > 180.0) deg -= 360.0;
    return deg;
}

double angularDistanceDeg(double a, double b)
{
    return qAbs(normalizeAngleDeg(a - b));
}

QString screenStorageKey(QScreen *screen)
{
    if (!screen) return QStringLiteral("default");
    QString raw = screen->name().trimmed();
    if (raw.isEmpty()) raw = screen->model().trimmed();
    if (raw.isEmpty()) return QStringLiteral("default");
    QString out;
    out.reserve(raw.size());
    for (QChar ch : raw) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('_'))
            out.append(ch);
        else
            out.append(QLatin1Char('_'));
    }
    return out.isEmpty() ? QStringLiteral("default") : out;
}

} // namespace

// ============================================================================
// RadialFanOverlay — Multi-Ring (1..10) Xiaomi Mi 5 + fooView Dock & Crosshair
// ============================================================================

RadialFanOverlay::RadialFanOverlay(FloatingBall *owner)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus),
      m_owner(owner)
{
    setObjectName(QStringLiteral("radialFanOverlay"));
    setProperty("floatingballSurface", true);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);
    FloatingWindow::prepare(this, false);

    // Deliberate 850ms hold opens Level-3 DBusMenu on touchscreen without
    // hijacking normal swipe-release / left-click Activate!
    m_trayDwellTimer = new QTimer(this);
    m_trayDwellTimer->setSingleShot(true);
    m_trayDwellTimer->setInterval(850);
    connect(m_trayDwellTimer, &QTimer::timeout, this, [this] {
        if (!m_owner || (m_mode != FlickFanMode && m_mode != TapFanMode)) return;
        if (m_pendingTrayDwell.service.isEmpty() || m_pendingTrayDwell.menuPath.isEmpty()) return;
        if (!m_drillStack.isEmpty() && m_drillStack.last().key == QLatin1String("tray_menu")) return;
        const TrayItemInfo targetTray = m_pendingTrayDwell;
        const QPointF targetPos = m_pendingTrayPos;
        QPointer<RadialFanOverlay> self(this);
        m_owner->statusMonitor()->fetchTrayMenuEntries(targetTray, [self, targetTray, targetPos](const QList<TrayMenuEntry> &entries) {
            if (!self || entries.isEmpty()) return;
            if (self->m_mode != FlickFanMode && self->m_mode != TapFanMode) return;
            if (!self->m_drillStack.isEmpty() && self->m_drillStack.last().key == QLatin1String("tray_menu")) return;
            self->drillInto(QStringLiteral("tray_menu"), targetTray.title.left(6), QColor("#66d9ff"), targetPos, targetTray);
            if (!self->m_drillStack.isEmpty()) {
                self->m_drillStack.last().trayMenuItems = entries;
                self->recomputeFanLayout();
                self->updateHitMask();
                self->update();
            }
        });
    });
}

QList<QRectF> RadialFanOverlay::slotGlobalRects() const
{
    QList<QRectF> inner;
    for (const RadialSlotItem &s : m_slots) {
        if (s.ringIndex == 0) inner.append(s.rectGlobal);
    }
    return inner;
}

QList<QRectF> RadialFanOverlay::allSlotGlobalRects() const
{
    QList<QRectF> all;
    all.reserve(m_slots.size());
    for (const RadialSlotItem &s : m_slots) all.append(s.rectGlobal);
    return all;
}

void RadialFanOverlay::recomputeFanLayout()
{
    m_slots.clear();
    m_ringRadii.clear();
    m_activeRingCount = 1;
    if (!m_owner) return;

    m_ballCenterGlobal = m_owner->anchorCenterGlobal();
    QScreen *screen = m_owner->assignedScreen();
    if (!screen || !QGuiApplication::screens().contains(screen))
        screen = QGuiApplication::screenAt(m_ballCenterGlobal.toPoint());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    setProperty("floatingballAssignedScreen", QVariant::fromValue(screen));

    const QRect screenGeo = screen->geometry();
    const QRect bounds = screen->availableGeometry();
    if (geometry() != screenGeo) {
        setGeometry(screenGeo);
    }

    const qreal scale = m_owner->screenScale();

    auto trimShort = [](QString s, int maxLen = 7) {
        s = s.trimmed();
        const int dash = s.indexOf(QStringLiteral(" — "));
        if (dash > 0) s = s.mid(dash + 3).trimmed();
        if (s.size() > maxLen) s = s.left(maxLen - 1) + QStringLiteral("…");
        return s;
    };

    // ========================================================================
    // DRILLED SINGLE-RING LEVEL (Kando-style fluid drill-down + ← backtrack!)
    // ========================================================================
    if (!m_drillStack.isEmpty()) {
        const DrillFrame &top = m_drillStack.last();
        QList<RadialSlotItem> children;

        if (top.key == QLatin1String("windows")) {
            const QList<DockAppEntry> apps = m_owner->dockModel()->dockApps(11, false, true);
            for (const DockAppEntry &app : apps) {
                if (!app.running) continue;
                RadialSlotItem item;
                item.kind = RadialSlotItem::AppSlot;
                item.id = QStringLiteral("app:") + (!app.windowId.isEmpty() ? app.windowId : app.id);
                item.label = app.name;
                item.shortLabel = trimShort(app.name, 8);
                item.app = app;
                item.isBranchItem = true;
                item.accentColor = app.active ? QColor("#7cc8ff") : QColor("#38bdf8");
                children.append(item);
            }
            // Dedicated true-minimize-all orb right in the Level-2 Windows ring!
            {
                RadialSlotItem hideAll;
                hideAll.kind = RadialSlotItem::ActionSlot;
                hideAll.id = QStringLiteral("minimize_all");
                hideAll.label = QStringLiteral("隐藏全部窗口（真正显示桌面）");
                hideAll.shortLabel = QStringLiteral("全收桌面");
                hideAll.isBranchItem = true;
                hideAll.accentColor = QColor("#34d399");
                children.append(hideAll);
            }
            if (children.isEmpty()) {
                for (const DockAppEntry &app : apps) {
                    if (children.size() >= 8) break;
                    RadialSlotItem item;
                    item.kind = RadialSlotItem::AppSlot;
                    item.id = QStringLiteral("app:") + (!app.launchTarget.isEmpty() ? app.launchTarget : app.id);
                    item.label = app.name;
                    item.shortLabel = trimShort(app.name, 8);
                    item.app = app;
                    item.isBranchItem = true;
                    item.accentColor = QColor("#38bdf8");
                    children.append(item);
                }
            }
        } else if (top.key == QLatin1String("tray")) {
            auto *mon = m_owner->statusMonitor();
            const QList<NotificationItem> unreadAll = mon->unreadNotifications();
            const int totalUnread = qMax(unreadAll.size(), mon->snapshot().unreadNotifications);

            // Always expose Notification Center at the head of the Tray/Notifications ring
            {
                RadialSlotItem notifHub;
                notifHub.kind = RadialSlotItem::ActionSlot;
                notifHub.id = QStringLiteral("notifications");
                notifHub.unreadCount = totalUnread;
                notifHub.label = totalUnread > 0
                    ? QStringLiteral("通知中心 · %1 条未读").arg(totalUnread)
                    : QStringLiteral("通知中心");
                notifHub.shortLabel = QStringLiteral("通知");
                notifHub.isBranchItem = true;
                notifHub.toggleActive = (totalUnread > 0);
                notifHub.accentColor = totalUnread > 0 ? QColor("#ff5272") : QColor("#9aa8ff");
                children.append(notifHub);
            }

            const auto &trayItems = mon->trayItems();
            for (const TrayItemInfo &ti : trayItems) {
                if (children.size() >= 11) break;
                RadialSlotItem item;
                item.kind = RadialSlotItem::TraySlot;
                item.id = QStringLiteral("tray:") + ti.service + ti.objectPath;
                item.label = ti.title;
                item.shortLabel = trimShort(ti.title, 7);
                item.trayItem = ti;
                item.isBranchItem = true;
                const QList<NotificationItem> tiUnread = mon->unreadNotificationsForApp(ti.id, ti.title);
                item.unreadCount = tiUnread.size();
                if (!tiUnread.isEmpty()) item.notificationPreview = tiUnread.first().previewText();
                item.toggleActive = (ti.status == QLatin1String("NeedsAttention")) || (item.unreadCount > 0);
                item.accentColor = item.toggleActive ? QColor("#ff5272") : QColor("#66d9ff");
                children.append(item);
            }
            if (trayItems.isEmpty() && totalUnread == 0) {
                RadialSlotItem emptyItem;
                emptyItem.kind = RadialSlotItem::TraySlot;
                emptyItem.id = QStringLiteral("tray:empty");
                emptyItem.label = QStringLiteral("当前无托盘图标");
                emptyItem.shortLabel = QStringLiteral("无托盘");
                emptyItem.isBranchItem = true;
                emptyItem.accentColor = QColor("#66d9ff");
                children.append(emptyItem);
            }
        } else if (top.key == QLatin1String("tray_menu")) {
            for (const TrayMenuEntry &me : top.trayMenuItems) {
                if (children.size() >= 10) break;
                RadialSlotItem item;
                item.kind = RadialSlotItem::TrayMenuSlot;
                item.id = QStringLiteral("traymenu:%1").arg(me.id);
                item.label = me.label;
                item.shortLabel = trimShort(me.label, 8);
                item.trayItem = top.traySource;
                item.trayMenuEntry = me;
                item.isBranchItem = true;
                item.toggleActive = me.checked;
                item.accentColor = me.checked ? QColor("#38bdf8") : QColor("#66d9ff");
                children.append(item);
            }
        } else if (top.key == QLatin1String("control")) {
            const SystemStatusSnapshot &st = m_owner->statusMonitor()->snapshot();
            auto addSys = [&](const QString &id, const QString &label, const QString &shortLbl, bool active, const QColor &accent) {
                RadialSlotItem item;
                item.kind = RadialSlotItem::SystemSlot;
                item.id = id;
                item.label = label;
                item.shortLabel = shortLbl;
                item.isBranchItem = true;
                item.toggleActive = active;
                item.accentColor = accent;
                children.append(item);
            };
            const QString wifiLabel = st.networkConnected
                ? (st.networkText.isEmpty() ? QStringLiteral("Wi-Fi · 已连接") : QStringLiteral("Wi-Fi · ") + st.networkText)
                : QStringLiteral("Wi-Fi · 未连接");
            addSys(QStringLiteral("sys_wifi"), wifiLabel, st.networkConnected ? QStringLiteral("Wi-Fi") : QStringLiteral("无网"),
                   st.networkConnected, QColor("#38bdf8"));
            addSys(QStringLiteral("sys_bluetooth"),
                   st.bluetoothPowered ? QStringLiteral("蓝牙 · 设备与电量") : QStringLiteral("蓝牙 · 已关闭"),
                   st.bluetoothPowered ? QStringLiteral("蓝牙") : QStringLiteral("蓝牙关"),
                   st.bluetoothPowered, QColor("#3b82f6"));
            addSys(QStringLiteral("sys_mihomo"),
                   QStringLiteral("Mihomo · 代理模式与节点"),
                   QStringLiteral("Mihomo"),
                   true, QColor("#34d399"));
            addSys(QStringLiteral("sys_dnd"),
                   st.dndEnabled ? QStringLiteral("勿扰模式 · 已开启") : QStringLiteral("勿扰模式 · 已关闭"),
                   st.dndEnabled ? QStringLiteral("勿扰开") : QStringLiteral("勿扰"),
                   st.dndEnabled, QColor("#a855f7"));

            QString pwrShort = QStringLiteral("平衡");
            QColor pwrColor("#38bdf8");
            if (st.powerProfile == QLatin1String("power-saver")) { pwrShort = QStringLiteral("节能"); pwrColor = QColor("#34d399"); }
            else if (st.powerProfile == QLatin1String("performance")) { pwrShort = QStringLiteral("性能"); pwrColor = QColor("#fb7185"); }
            addSys(QStringLiteral("sys_power"), QStringLiteral("电源模式 · ") + pwrShort, pwrShort,
                   st.powerProfile == QLatin1String("performance") || st.powerProfile == QLatin1String("power-saver"), pwrColor);
            addSys(QStringLiteral("sys_nightlight"),
                   st.nightLightEnabled ? QStringLiteral("夜间护眼 · 已开启") : QStringLiteral("夜间护眼 · 已关闭"),
                   st.nightLightEnabled ? QStringLiteral("护眼开") : QStringLiteral("护眼"),
                   st.nightLightEnabled, QColor("#f59e0b"));
            addSys(QStringLiteral("sys_audio"), QStringLiteral("声音与亮度滑条"), QStringLiteral("音/亮"), !st.volumeMuted, QColor("#5eead4"));
            addSys(QStringLiteral("sys_display"), QStringLiteral("显示器与多屏"), QStringLiteral("多屏"), false, QColor("#60a5fa"));
            addSys(QStringLiteral("sys_lock"), QStringLiteral("立即锁屏"), QStringLiteral("锁屏"), false, QColor("#cbd5e1"));
            addSys(QStringLiteral("sys_sleep"), QStringLiteral("休眠 / 关机菜单"), QStringLiteral("关机"), false, QColor("#fb7185"));
#ifdef Q_OS_MACOS
            addSys(QStringLiteral("sys_settings"), QStringLiteral("系统设置"), QStringLiteral("设置"), false, QColor("#9aa8ff"));
#else
            addSys(QStringLiteral("sys_settings"), QStringLiteral("KDE 系统设置"), QStringLiteral("设置"), false, QColor("#9aa8ff"));
#endif
        } else if (top.key == QLatin1String("tools")) {
            struct ToolDef { const char *id; const char *shortLbl; const char *color; };
            static const ToolDef kTools[]{
#ifdef Q_OS_LINUX
                {"crosshair",    "截屏/录屏", "#7cc8ff"},
#else
                {"crosshair",    "截图",     "#7cc8ff"},
                {"record",       "录屏",     "#fb7185"},
#endif
                {"clipboard",    "剪贴板",   "#e2e8f0"},
                {"minimize_all", "全收桌面", "#34d399"},
                {"deck",         "触控甲板", "#60a5fa"},
                {"downloads",    "下载",     "#86e0b8"},
                {"trash",        "废纸篓",   "#ff9dad"},
                {"back",         "返回",     "#cbd5e1"},
                {"overview",     "全览",     "#a5b4fc"}
            };
            for (const ToolDef &td : kTools) {
                RadialSlotItem item;
                item.kind = RadialSlotItem::ActionSlot;
                item.id = QString::fromLatin1(td.id);
                item.label = m_owner->actionLabel(item.id);
                item.shortLabel = QString::fromUtf8(td.shortLbl);
                item.isBranchItem = true;
                item.accentColor = QColor(QString::fromLatin1(td.color));
                children.append(item);
            }
        }

        const qreal itemDiam = qBound(40.0, (children.size() >= 9 ? 44.0 : 46.0) * scale, 48.0);
        const qreal itemHalf = itemDiam * 0.5;
        const qreal centerDiam = qBound(42.0, 46.0 * scale, 48.0);
        const qreal backDiam = qBound(38.0, 42.0 * scale, 44.0);

        // 1. Add ← Backtrack breadcrumb orb
        {
            RadialSlotItem back;
            back.kind = RadialSlotItem::BackSlot;
            back.ringIndex = 0;
            back.id = QStringLiteral("__back__");
            back.label = QStringLiteral("划回上一层");
            back.shortLabel = QStringLiteral("返回");
            back.accentColor = QColor("#fbbf24");
            back.centerGlobal = top.backGlobal;
            back.rectGlobal = QRectF(top.backGlobal.x() - backDiam * 0.5, top.backGlobal.y() - backDiam * 0.5, backDiam, backDiam);
            m_slots.append(back);
        }
        // 2. Add Center title orb
        {
            RadialSlotItem center;
            center.kind = RadialSlotItem::CenterSlot;
            center.ringIndex = 0;
            center.id = QStringLiteral("__center__");
            center.label = top.title + QStringLiteral(" · 点此或划向 ← 返回");
            center.shortLabel = top.title;
            center.accentColor = top.accentColor;
            center.centerGlobal = top.centerGlobal;
            center.rectGlobal = QRectF(top.centerGlobal.x() - centerDiam * 0.5, top.centerGlobal.y() - centerDiam * 0.5, centerDiam, centerDiam);
            m_slots.append(center);
        }

        // 3. Place ALL children in ONE clean single ring around top.centerGlobal,
        // leaving a clear ~70-deg open backtrack corridor directly facing ← !
        const int n = children.size();
        const double backDeg = qRadiansToDegrees(qAtan2(top.backGlobal.y() - top.centerGlobal.y(),
                                                        top.backGlobal.x() - top.centerGlobal.x()));
        const double oppDeg = backDeg + 180.0;
        const double usableSpan = (n <= 4) ? 210.0 : (n <= 6 ? 255.0 : 292.0);
        const double stepDeg = (n > 1) ? (usableSpan / (n - 1)) : 0.0;
        const qreal minR = centerDiam * 0.5 + itemHalf + 22.0 * scale;
        const qreal chordR = (n > 1)
            ? ((itemDiam + 10.0 * scale) / (2.0 * qSin(qDegreesToRadians(qMax(10.0, stepDeg * 0.5)))))
            : minR;
        const qreal maxFitR = qMin(
            qMin(top.centerGlobal.x() - bounds.left(), bounds.right() - top.centerGlobal.x()),
            qMin(top.centerGlobal.y() - bounds.top(), bounds.bottom() - top.centerGlobal.y())) - itemHalf - 16.0;
        const qreal ringR = qMax(minR * 0.85, qMin(qMax(minR, chordR), qMax(minR, maxFitR)));

        const double startDeg = oppDeg - usableSpan * 0.5;
        qreal maxFromBall = 0.0;
        for (int i = 0; i < n; ++i) {
            const double deg = (n == 1) ? oppDeg : (startDeg + stepDeg * i);
            const double rad = qDegreesToRadians(deg);
            const QPointF pt = top.centerGlobal + QPointF(qCos(rad), qSin(rad)) * ringR;
            RadialSlotItem item = children[i];
            item.ringIndex = 1;
            item.finalCenterGlobal = pt;
            item.centerGlobal = pt;
            item.rectGlobal = QRectF(pt.x() - itemHalf, pt.y() - itemHalf, itemDiam, itemDiam);
            m_slots.append(item);
            maxFromBall = qMax(maxFromBall, QLineF(m_ballCenterGlobal, pt).length() + itemHalf);
        }
        m_ringRadii.append(ringR);
        m_outermostRadius = qMax(ringR + itemHalf, maxFromBall);
        m_activeRingCount = 1;
        return;
    }

    // ========================================================================
    // UNIFIED PC DESKTOP + TOUCH MULTI-RING LAYOUT:
    //   • Ring 0 (Innermost): Fixed system action buttons (muscle memory!)
    //   • Ring 1..K (Outer): ALL currently open windows, clustered by app,
    //     automatically expanding across concentric arcs to fit 0..100+ windows!
    // ========================================================================
    m_slotDiameter = qBound(40.0, 46.0 * scale, 50.0);
    const qreal ballRadius = qMax(m_owner->width(), m_owner->height()) / 2.0;
    m_fanRadius = qMax(ballRadius + m_slotDiameter * 0.5 + 15.0 * scale, 66.0 * scale);

    const qreal reach = m_fanRadius + m_slotDiameter * 0.5 + 8.0;
    const bool nearLeft = (m_ballCenterGlobal.x() - reach) < bounds.left();
    const bool nearRight = (m_ballCenterGlobal.x() + reach) > bounds.right();
    const bool nearTop = (m_ballCenterGlobal.y() - reach) < bounds.top();
    const bool nearBottom = (m_ballCenterGlobal.y() + reach) > bounds.bottom();

    const bool onLeftHalf = nearLeft ? true : (nearRight ? false : (m_ballCenterGlobal.x() <= bounds.center().x()));
    double centerDeg = onLeftHalf ? 0.0 : 180.0;
    double sweepDeg = 156.0;
    bool cornerQuadrant = false;

    if ((nearLeft || nearRight) && (nearTop || nearBottom)) {
        cornerQuadrant = true;
        sweepDeg = 94.0;
        if (nearLeft && nearTop) centerDeg = 45.0;
        else if (nearRight && nearTop) centerDeg = 135.0;
        else if (nearRight && nearBottom) centerDeg = -135.0;
        else centerDeg = -45.0;
    } else if (nearTop && !nearLeft && !nearRight) {
        centerDeg = 90.0;
        sweepDeg = 152.0;
    } else if (nearBottom && !nearLeft && !nearRight) {
        centerDeg = -90.0;
        sweepDeg = 152.0;
    }

    // ------------------------------------------------------------------------
    // Ring 0 (Innermost Fixed Ring): 5 core system buttons
    // ------------------------------------------------------------------------
    const QStringList innerActions = m_owner->orbitActions();
    const int count0 = innerActions.size();
    const double step0 = count0 > 1 ? sweepDeg / (count0 - 1) : 36.0;
    const double minDisjointRadius =
        (m_slotDiameter + 6.5 * scale) / (2.0 * qSin(qDegreesToRadians(qMax(12.0, step0 * 0.5))));
    const double maxScreenRadius = qMax(m_fanRadius, qMin(bounds.width(), bounds.height()) * 0.44);
    m_fanRadius = qMin(maxScreenRadius, qMax(m_fanRadius, minDisjointRadius));

    const bool rightSide = qCos(qDegreesToRadians(centerDeg)) < -0.01;
    bool reverseSweep = false;
    if (cornerQuadrant) {
        if (nearRight && nearTop) reverseSweep = true;
        else if (nearLeft && nearBottom) reverseSweep = false;
        else if (nearLeft && nearTop) reverseSweep = (m_owner->dockSide() == FloatingBall::DockTop);
        else reverseSweep = (m_owner->dockSide() != FloatingBall::DockBottom);
    } else if (nearTop && !nearLeft && !nearRight) {
        reverseSweep = true;
    } else if (nearBottom && !nearLeft && !nearRight) {
        reverseSweep = false;
    } else {
        reverseSweep = rightSide;
    }
    const double startDeg0 = reverseSweep ? (centerDeg + sweepDeg * 0.5) : (centerDeg - sweepDeg * 0.5);
    const double dir0 = reverseSweep ? -1.0 : 1.0;

    QRectF group0;
    QList<QPointF> rawCenters0;
    for (int i = 0; i < count0; ++i) {
        const double deg = startDeg0 + dir0 * step0 * i;
        const double rad = qDegreesToRadians(deg);
        const QPointF pt = m_ballCenterGlobal + QPointF(qCos(rad), qSin(rad)) * m_fanRadius;
        rawCenters0.append(pt);
        const QRectF r(pt.x() - m_slotDiameter * 0.5, pt.y() - m_slotDiameter * 0.5,
                       m_slotDiameter, m_slotDiameter);
        group0 = (i == 0) ? r : group0.united(r);
    }

    QPointF clampShift0(0, 0);
    if (group0.left() < bounds.left() + 2) clampShift0.setX(bounds.left() + 2 - group0.left());
    else if (group0.right() > bounds.right() - 2) clampShift0.setX(bounds.right() - 2 - group0.right());
    if (group0.top() < bounds.top() + 2) clampShift0.setY(bounds.top() + 2 - group0.top());
    else if (group0.bottom() > bounds.bottom() - 2) clampShift0.setY(bounds.bottom() - 2 - group0.bottom());

    const qreal prog = (m_mode == FlickFanMode) ? 1.0 : qBound(0.0, m_progress, 1.0);
    const qreal d0Anim = (m_mode == FlickFanMode) ? m_slotDiameter : (m_slotDiameter * (0.35 + 0.65 * prog));
    QList<QPointF> finalCentersPlaced;
    QList<qreal> finalDiametersPlaced;

    auto populateFixedActionMeta = [](RadialSlotItem *item) {
        const QString &id = item->id;
        if (id == QLatin1String("launcher")) {
            item->shortLabel = QStringLiteral("开始");
            item->accentColor = QColor("#7cc8ff");
        } else if (id == QLatin1String("sys_hub")) {
            item->shortLabel = QStringLiteral("控制");
            item->accentColor = QColor("#9aa8ff");
        } else if (id == QLatin1String("tray_hub")) {
            item->shortLabel = QStringLiteral("托盘 ›");
            item->drillKey = QStringLiteral("tray");
            item->isBranchHub = true;
            item->accentColor = QColor("#66d9ff");
        } else if (id == QLatin1String("tools_hub")) {
            item->shortLabel = QStringLiteral("工具 ›");
            item->drillKey = QStringLiteral("tools");
            item->isBranchHub = true;
            item->accentColor = QColor("#86e0b8");
        } else if (id == QLatin1String("win_hub")) {
            item->shortLabel = QStringLiteral("窗口 ›");
            item->drillKey = QStringLiteral("windows");
            item->isBranchHub = true;
            item->accentColor = QColor("#38bdf8");
        } else if (id == QLatin1String("overview")) {
            item->shortLabel = QStringLiteral("全览");
            item->accentColor = QColor("#a5b4fc");
        } else if (id == QLatin1String("minimize_all") || id == QLatin1String("desktop")) {
            item->shortLabel = QStringLiteral("桌面");
            item->accentColor = QColor("#34d399");
        } else if (id == QLatin1String("crosshair") || id == QLatin1String("capture")) {
            item->shortLabel = QStringLiteral("截屏");
            item->accentColor = QColor("#7cc8ff");
        } else if (id == QLatin1String("clipboard")) {
            item->shortLabel = QStringLiteral("剪贴");
            item->accentColor = QColor("#e2e8f0");
        } else if (id == QLatin1String("deck")) {
            item->shortLabel = QStringLiteral("甲板");
            item->accentColor = QColor("#60a5fa");
        } else if (id == QLatin1String("ime")) {
            item->shortLabel = QStringLiteral("输入法");
            item->accentColor = QColor("#7dd3fc");
        } else if (id == QLatin1String("close_active")) {
            item->shortLabel = QStringLiteral("关闭");
            item->accentColor = QColor("#ff5f57");
        }
    };

    for (int i = 0; i < count0; ++i) {
        RadialSlotItem item;
        item.kind = RadialSlotItem::ActionSlot;
        item.ringIndex = 0;
        item.id = innerActions[i];
        item.label = m_owner->actionLabel(item.id);
        populateFixedActionMeta(&item);
        const QPointF finalCenter = rawCenters0[i] + clampShift0;
        item.finalCenterGlobal = finalCenter;
        item.centerGlobal = (m_mode == FlickFanMode)
            ? finalCenter
            : (m_ballCenterGlobal + (finalCenter - m_ballCenterGlobal) * prog);
        item.rectGlobal = QRectF(item.centerGlobal.x() - d0Anim * 0.5,
                                 item.centerGlobal.y() - d0Anim * 0.5,
                                 d0Anim, d0Anim);
        m_slots.append(item);
        finalCentersPlaced.append(finalCenter);
        finalDiametersPlaced.append(m_slotDiameter);
    }

    m_ringRadii.append(m_fanRadius);
    m_outermostRadius = m_fanRadius + m_slotDiameter * 0.5;
    m_activeRingCount = 1;

    if (!m_owner->dockModeEnabled()) return;

    // ------------------------------------------------------------------------
    // Ring 1..K: Auto-Expanding All Currently Open Windows (0..100+ windows!)
    // + Unmatched Unread App Notifications (e.g. minimized-to-tray apps / system alerts)
    // so tapping the ball ALWAYS reveals which exact app triggered any red dot!
    // ------------------------------------------------------------------------
    const QList<DockAppEntry> openWins = m_owner->dockModel()->openWindows();
    auto *mon = m_owner->statusMonitor();
    const QList<NotificationItem> unreadNotifs = mon ? mon->unreadNotifications() : QList<NotificationItem>{};
    if (openWins.isEmpty() && unreadNotifs.isEmpty()) return;

    auto extractWindowMicroTag = [](const DockAppEntry &app) -> QString {
        QString cap = app.caption.trimmed();
        if (!cap.isEmpty()) {
            for (const QString &sep : {QStringLiteral(" — "), QStringLiteral(" - "), QStringLiteral(" – "), QStringLiteral(" | ")}) {
                const int pos = cap.lastIndexOf(sep);
                if (pos > 0) {
                    const QString tail = cap.mid(pos + sep.size()).trimmed();
                    const QString head = cap.left(pos).trimmed();
                    // Strip trailing app-name suffix (e.g. "nvim ball.cpp — Kitty" -> "nvim ball.cpp")
                    if (!app.name.isEmpty() && tail.contains(app.name, Qt::CaseInsensitive)) {
                        cap = head;
                    } else if (head.size() <= 16) {
                        cap = head;
                    }
                    break;
                }
            }
            // Extract concise distinguishing token (filename / host / site)
            cap.remove(QStringLiteral("https://")).remove(QStringLiteral("http://"));
            if (cap.contains(QLatin1Char('/'))) {
                const QString base = cap.section(QLatin1Char('/'), -1).trimmed();
                if (!base.isEmpty()) cap = base;
            }
            if (cap.size() > 5) cap = cap.left(4) + QStringLiteral("…");
            if (!cap.isEmpty()) return cap;
        }
        QString nm = app.name.trimmed();
        if (nm.size() > 5) nm = nm.left(4) + QStringLiteral("…");
        return nm;
    };

    QList<RadialSlotItem> windowQueue;
    windowQueue.reserve(openWins.size() + unreadNotifs.size());
    QSet<quint64> matchedNotifSeqIds;
    for (const DockAppEntry &win : openWins) {
        RadialSlotItem item;
        item.kind = RadialSlotItem::AppSlot;
        item.id = QStringLiteral("app:") + (!win.windowId.isEmpty() ? win.windowId
                                           : (!win.launchTarget.isEmpty() ? win.launchTarget : win.id));
        if (!win.caption.isEmpty() && win.caption.compare(win.name, Qt::CaseInsensitive) != 0) {
            item.label = win.name + QStringLiteral(" — ") + win.caption;
        } else {
            item.label = win.name;
        }
        item.shortLabel = extractWindowMicroTag(win);
        item.app = win;
        if (mon) {
            const QList<NotificationItem> appUnread =
                mon->unreadNotificationsForApp(win.appKey, win.name, win.id);
            if (!appUnread.isEmpty()) {
                item.unreadCount = appUnread.size();
                item.notificationPreview = appUnread.first().previewText();
                item.notificationItem = appUnread.first();
                item.app.demandsAttention = true;
                for (const NotificationItem &n : appUnread) matchedNotifSeqIds.insert(n.seqId);
            }
        }
        item.toggleActive = item.app.demandsAttention;
        windowQueue.append(item);
    }

    // Surface background/minimized-to-tray/system unread notifications as dedicated
    // glowing NotificationSlot orbs grouped by app (appended after open windows so
    // existing open window angles stay 100% spatially stable!).
    QStringList notifGroupOrder;
    QHash<QString, QList<NotificationItem>> notifGroups;
    for (const NotificationItem &n : unreadNotifs) {
        if (matchedNotifSeqIds.contains(n.seqId)) continue;
        QString key = !n.desktopEntry.trimmed().isEmpty()
            ? n.desktopEntry.trimmed().toLower()
            : n.displayAppName().toLower();
        if (key.endsWith(QLatin1String(".desktop"))) key.chop(8);
        if (!notifGroups.contains(key)) notifGroupOrder.append(key);
        notifGroups[key].append(n);
    }
    for (const QString &gKey : notifGroupOrder) {
        const QList<NotificationItem> &grp = notifGroups[gKey];
        if (grp.isEmpty()) continue;
        const NotificationItem &latest = grp.first();
        RadialSlotItem item;
        item.kind = RadialSlotItem::NotificationSlot;
        item.id = QStringLiteral("notif:") + QString::number(latest.seqId);
        item.label = latest.displayAppName();
        item.shortLabel = trimShort(latest.displayAppName(), 6);
        item.notificationItem = latest;
        item.unreadCount = grp.size();
        item.notificationPreview = latest.previewText();
        item.toggleActive = true;
        item.accentColor = QColor("#ff4d6d");
        windowQueue.append(item);
    }
    if (windowQueue.isEmpty()) return;

    const bool includeCloseActive = !openWins.isEmpty();
    const int totalWins = windowQueue.size();
    const int totalOuterSlots = totalWins + (includeCloseActive ? 1 : 0);
    const double halfScanSweep = cornerQuadrant ? 47.0 : 87.0;
    const double preferredCloseTailMag = cornerQuadrant ? 47.0 : 80.0;

    auto scanArcSegments = [&](qreal radius, const QRectF &safeBounds) -> QList<QPair<double, double>> {
        QList<QPair<double, double>> runs;
        double curStart = 0.0;
        bool inRun = false;
        const int steps = qRound(halfScanSweep * 2.0);
        for (int s = 0; s <= steps; ++s) {
            const double offset = -halfScanSweep + s;
            const double rad = qDegreesToRadians(centerDeg + offset);
            const QPointF pt = m_ballCenterGlobal + QPointF(qCos(rad), qSin(rad)) * radius;
            const bool ok = safeBounds.contains(pt);
            if (ok && !inRun) {
                curStart = offset;
                inRun = true;
            } else if (!ok && inRun) {
                if ((offset - 1.0) - curStart >= 4.0)
                    runs.append({curStart, offset - 1.0});
                inRun = false;
            }
        }
        if (inRun && (halfScanSweep - curStart) >= 4.0) {
            runs.append({curStart, halfScanSweep});
        }
        return runs;
    };

    // Adaptive diameter solver: keeps spacious 48-50pt orbs for everyday window counts,
    // and smoothly tightens down only when 40..100+ windows are open so every window fits on-screen!
    qreal outerDiameter = (totalOuterSlots <= 8 ? 50.0
        : (totalOuterSlots <= 20 ? 46.0
           : (totalOuterSlots <= 40 ? 42.0
              : (totalOuterSlots <= 65 ? 37.0 : 33.0)))) * scale;
    outerDiameter = qBound(22.0, outerDiameter, 52.0);

    while (outerDiameter > 22.5) {
        const qreal halfD = outerDiameter * 0.5;
        const qreal stepR = outerDiameter * 0.95 + qMax(4.2, 6.0 * scale);
        const qreal firstGap = (m_slotDiameter + outerDiameter) * 0.5 + qMax(14.0, 18.0 * scale);
        const QRectF safe = QRectF(bounds).adjusted(halfD + 5.0, halfD + 5.0, -halfD - 5.0, -halfD - 5.0);
        int estCap = 0;
        for (int r = 1; r <= 36 && estCap < totalOuterSlots; ++r) {
            const qreal radius = m_fanRadius + firstGap + (r - 1) * stepR;
            auto segs = scanArcSegments(radius, safe);
            if (segs.isEmpty()) {
                if (radius > qMax(bounds.width(), bounds.height())) break;
                continue;
            }
            if (reverseSweep) {
                std::reverse(segs.begin(), segs.end());
            }
            const double chordRatio = qMin(0.95, (outerDiameter + 4.5 * scale) / (2.0 * radius));
            const double minStep = qMax(3.6, qRadiansToDegrees(2.0 * qAsin(chordRatio)));
            for (int si = 0; si < segs.size(); ++si) {
                const auto &seg = segs[si];
                if (r == 1 && includeCloseActive && si == segs.size() - 1) {
                    const double closeOff = reverseSweep
                        ? qBound(seg.first, -preferredCloseTailMag, seg.second)
                        : qBound(seg.first, preferredCloseTailMag, seg.second);
                    estCap += 1; // fixed close_active orb at Ring 1 tail
                    const double appSpan = reverseSweep
                        ? (seg.second - (closeOff + minStep))
                        : ((closeOff - minStep) - seg.first);
                    if (appSpan >= 0.0) {
                        estCap += qMax(1, static_cast<int>(qFloor(appSpan / minStep)) + 1);
                    }
                } else {
                    estCap += qMax(1, static_cast<int>(qFloor((seg.second - seg.first) / minStep)) + 1);
                }
            }
        }
        if (estCap >= totalOuterSlots) break;
        outerDiameter = qMax(22.0, outerDiameter - 2.5);
    }

    const qreal halfItem = outerDiameter * 0.5;
    const qreal ringStep = outerDiameter * 0.95 + qMax(4.2, 6.0 * scale);
    const qreal firstRingGap = (m_slotDiameter + outerDiameter) * 0.5 + qMax(14.0, 18.0 * scale);
    const QRectF safeBounds = QRectF(bounds).adjusted(halfItem + 5.0, halfItem + 5.0,
                                                      -halfItem - 5.0, -halfItem - 5.0);

    auto fitsWithoutOverlap = [&](const QPointF &candidate, qreal candDiam) {
        if (QLineF(candidate, m_ballCenterGlobal).length() < ballRadius + candDiam * 0.5 + 4.0)
            return false;
        for (int k = 0; k < finalCentersPlaced.size(); ++k) {
            const qreal minDist = (candDiam + finalDiametersPlaced[k]) * 0.5 + 3.0;
            if (QLineF(candidate, finalCentersPlaced[k]).length() < minDist)
                return false;
        }
        return true;
    };

    int winCursor = 0;
    bool closePlaced = !includeCloseActive;
    for (int r = 1; r <= 42 && (winCursor < totalWins || !closePlaced); ++r) {
        const qreal radius = m_fanRadius + firstRingGap + (r - 1) * ringStep;
        auto segs = scanArcSegments(radius, safeBounds);
        if (segs.isEmpty()) {
            if (radius > std::hypot(bounds.width(), bounds.height())) break;
            continue;
        }
        if (reverseSweep) {
            std::reverse(segs.begin(), segs.end());
        }

        const double chordRatio = qMin(0.95, (outerDiameter + 4.5 * scale) / (2.0 * radius));
        const double minAngleStep = qMax(3.5, qRadiansToDegrees(2.0 * qAsin(chordRatio)));
        const qreal ringDelay = qMin(0.32, r * 0.035);
        const qreal ringProg = qBound(0.0, (m_progress - ringDelay) / qMax(0.25, 1.0 - ringDelay), 1.0);
        const qreal effProg = (m_mode == FlickFanMode) ? 1.0 : ringProg;

        // Pin "Close Active Window" at the fixed tail anchor of Ring 1 (last segment's tail),
        // so muscle memory stays rock-solid across 1..100+ open windows and apps overflow to Ring 2+.
        double reservedCloseOffset = 0.0;
        QPointF reservedCloseCenter;
        bool hasReservedClose = false;
        if (!closePlaced) {
            const auto &lastSeg = segs.last();
            const double targetOff = reverseSweep
                ? qBound(lastSeg.first, -preferredCloseTailMag, lastSeg.second)
                : qBound(lastSeg.first, preferredCloseTailMag, lastSeg.second);
            const double stepIn = reverseSweep ? 1.0 : -1.0;
            const int maxTries = qMax(1, static_cast<int>(qCeil(lastSeg.second - lastSeg.first)) + 1);
            for (int t = 0; t < maxTries; ++t) {
                const double candOff = targetOff + stepIn * t;
                if (candOff < lastSeg.first - 0.01 || candOff > lastSeg.second + 0.01) break;
                const double rad = qDegreesToRadians(centerDeg + candOff);
                const QPointF pt = m_ballCenterGlobal + QPointF(qCos(rad), qSin(rad)) * radius;
                if (safeBounds.contains(pt) && fitsWithoutOverlap(pt, outerDiameter)) {
                    reservedCloseOffset = candOff;
                    reservedCloseCenter = pt;
                    hasReservedClose = true;
                    break;
                }
            }
        }

        int placedInRing = 0;
        for (int si = 0; si < segs.size(); ++si) {
            if (winCursor >= totalWins) break;
            QPair<double, double> seg = segs[si];
            const bool isCloseSeg = (hasReservedClose && si == segs.size() - 1);
            if (isCloseSeg) {
                if (reverseSweep) {
                    seg.first = reservedCloseOffset + minAngleStep;
                } else {
                    seg.second = reservedCloseOffset - minAngleStep;
                }
                if (seg.second < seg.first - 0.01) {
                    continue;
                }
            }

            const double arcSpanDeg = qMax(0.0, seg.second - seg.first);
            const int segMaxCap = qMax(1, static_cast<int>(qFloor(arcSpanDeg / minAngleStep)) + 1);
            const int remaining = totalWins - winCursor;
            int count = qMin(segMaxCap, remaining);

            // Stagger odd/even parity gently when multiple rings exist so honeycombs interleave cleanly
            if (segs.size() == 1 && count > 1 && remaining > count) {
                const int effectiveCount = count + (isCloseSeg ? 1 : 0);
                const bool wantEven = (r % 2 == 1);
                if (((effectiveCount % 2 == 0) != wantEven) && count - 1 >= 2) {
                    --count;
                }
            }

            const double arcMid = centerDeg + (seg.first + seg.second) * 0.5;
            const double desiredStep = qMax(minAngleStep, qMin(22.0, minAngleStep * 1.08));
            const double effectiveSpan = (count > 1) ? qMin(arcSpanDeg, (count - 1) * desiredStep) : 0.0;
            const double stepDeg = (count > 1) ? (effectiveSpan / (count - 1)) : 0.0;
            const double startDeg = reverseSweep ? (arcMid + effectiveSpan * 0.5) : (arcMid - effectiveSpan * 0.5);
            const double dir = reverseSweep ? -1.0 : 1.0;

            for (int i = 0; i < count && winCursor < totalWins; ++i) {
                const double deg = (count == 1) ? arcMid : (startDeg + dir * stepDeg * i);
                const double rad = qDegreesToRadians(deg);
                const QPointF finalCenter = m_ballCenterGlobal + QPointF(qCos(rad), qSin(rad)) * radius;
                if (!safeBounds.contains(finalCenter)) continue;
                if (!fitsWithoutOverlap(finalCenter, outerDiameter)) continue;
                if (hasReservedClose &&
                    QLineF(finalCenter, reservedCloseCenter).length() < outerDiameter + 3.0) {
                    continue;
                }

                RadialSlotItem item = windowQueue[winCursor++];
                item.ringIndex = r;
                item.finalCenterGlobal = finalCenter;
                item.centerGlobal = (m_mode == FlickFanMode)
                    ? finalCenter
                    : (m_ballCenterGlobal + (finalCenter - m_ballCenterGlobal) * effProg);
                const qreal dNow = (m_mode == FlickFanMode) ? outerDiameter : (outerDiameter * (0.35 + 0.65 * effProg));
                item.rectGlobal = QRectF(item.centerGlobal.x() - dNow * 0.5,
                                         item.centerGlobal.y() - dNow * 0.5,
                                         dNow, dNow);
                m_slots.append(item);
                finalCentersPlaced.append(finalCenter);
                finalDiametersPlaced.append(outerDiameter);
                ++placedInRing;
            }
        }

        if (hasReservedClose) {
            RadialSlotItem closeItem;
            closeItem.kind = RadialSlotItem::ActionSlot;
            closeItem.ringIndex = r;
            closeItem.id = QStringLiteral("close_active");
            closeItem.label = m_owner->actionLabel(closeItem.id);
            populateFixedActionMeta(&closeItem);
            closeItem.finalCenterGlobal = reservedCloseCenter;
            closeItem.centerGlobal = (m_mode == FlickFanMode)
                ? reservedCloseCenter
                : (m_ballCenterGlobal + (reservedCloseCenter - m_ballCenterGlobal) * effProg);
            const qreal dNow = (m_mode == FlickFanMode) ? outerDiameter : (outerDiameter * (0.35 + 0.65 * effProg));
            closeItem.rectGlobal = QRectF(closeItem.centerGlobal.x() - dNow * 0.5,
                                          closeItem.centerGlobal.y() - dNow * 0.5,
                                          dNow, dNow);
            m_slots.append(closeItem);
            finalCentersPlaced.append(reservedCloseCenter);
            finalDiametersPlaced.append(outerDiameter);
            ++placedInRing;
            closePlaced = true;
        }

        if (placedInRing > 0) {
            m_ringRadii.append(radius);
            m_outermostRadius = qMax(m_outermostRadius, radius + halfItem);
            m_activeRingCount = r + 1;
        }
    }
}

QList<qreal> RadialFanOverlay::computeAttentionAnglesRad()
{
    QList<qreal> angles;
    if (!m_owner) return angles;

    // Ensure root layout target positions match the exact current screen & open windows
    const QList<DrillFrame> savedDrill = m_drillStack;
    const qreal savedProg = m_progress;
    m_drillStack.clear();
    m_progress = 1.0;
    recomputeFanLayout();

    const SystemStatusSnapshot &st = m_owner->statusMonitor()->snapshot();
    bool hasWindowAttention = false;
    qreal trayHubAngle = 0.0;
    bool hasTrayHub = false;

    auto addUniqueAngle = [&](qreal rad) {
        for (qreal existing : angles) {
            const double diff = angularDistanceDeg(qRadiansToDegrees(existing), qRadiansToDegrees(rad));
            if (diff < 6.0) return;
        }
        angles.append(rad);
    };

    for (const RadialSlotItem &s : m_slots) {
        const QPointF target = s.finalCenterGlobal.isNull() ? s.centerGlobal : s.finalCenterGlobal;
        const qreal rad = std::atan2(target.y() - m_ballCenterGlobal.y(),
                                     target.x() - m_ballCenterGlobal.x());
        if ((s.kind == RadialSlotItem::AppSlot && (s.app.demandsAttention || s.unreadCount > 0)) ||
            s.kind == RadialSlotItem::NotificationSlot) {
            hasWindowAttention = true;
            addUniqueAngle(rad);
        } else if (s.ringIndex == 0 && s.id == QLatin1String("tray_hub")) {
            hasTrayHub = true;
            trayHubAngle = rad;
        }
    }

    if (st.trayNeedsAttention || (st.unreadNotifications > 0 && !hasWindowAttention)) {
        if (hasTrayHub) {
            addUniqueAngle(trayHubAngle);
        } else {
            // Fallback: point directly inward from the docked screen edge
            double fallbackDeg = 180.0;
            if (m_owner->dockSide() == FloatingBall::DockLeft) fallbackDeg = 0.0;
            else if (m_owner->dockSide() == FloatingBall::DockTop) fallbackDeg = 90.0;
            else if (m_owner->dockSide() == FloatingBall::DockBottom) fallbackDeg = -90.0;
            addUniqueAngle(qDegreesToRadians(fallbackDeg));
        }
    }

    m_drillStack = savedDrill;
    m_progress = savedProg;
    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
    }
    return angles;
}

QString RadialFanOverlay::activeLabelText() const
{
    // Strictly show text ONLY when pointing/hovering/sliding onto a specific orb!
    if (m_highlightedSlot < 0 || m_highlightedSlot >= m_slots.size()) {
        return {};
    }
    const RadialSlotItem &item = m_slots[m_highlightedSlot];
    QString text = item.label;
    if (item.kind == RadialSlotItem::AppSlot) {
        if (item.unreadCount > 0 && !item.notificationPreview.isEmpty()) {
            const QString cnt = item.unreadCount > 1
                ? QStringLiteral(" (%1条)").arg(item.unreadCount)
                : QString();
            text = item.app.name + QStringLiteral(" · 🔴%1 ").arg(cnt) + item.notificationPreview;
        } else if (item.app.demandsAttention) {
            text += QStringLiteral(" · 🔴 新消息");
        } else if (item.app.active) {
            text += QStringLiteral(" · 当前");
        } else if (item.app.minimized) {
            text += QStringLiteral(" · 最小化");
        }
    } else if (item.kind == RadialSlotItem::NotificationSlot) {
        const QString cnt = item.unreadCount > 1
            ? QStringLiteral("%1条未读").arg(item.unreadCount)
            : QStringLiteral("未读消息");
        text = item.notificationPreview.isEmpty()
            ? QStringLiteral("%1 · 🔴 %2").arg(item.label, cnt)
            : QStringLiteral("%1 · 🔴 %2 · %3").arg(item.label, cnt, item.notificationPreview);
    } else if (item.kind == RadialSlotItem::TraySlot) {
        if (!item.notificationPreview.isEmpty()) {
            text += QStringLiteral(" · 🔴 ") + item.notificationPreview;
        } else if (item.toggleActive) {
            text += QStringLiteral(" · 🔴 有新提醒");
        }
    } else if (item.kind == RadialSlotItem::TrayMenuSlot) {
        text = item.trayItem.title + QStringLiteral(" › ") + item.label;
    } else if (item.id == QLatin1String("close_active") && m_owner) {
        const QList<DockAppEntry> openWins = m_owner->dockModel()->openWindows();
        const DockAppEntry *target = nullptr;
        for (const DockAppEntry &w : openWins) {
            if (w.active && !w.minimized) {
                target = &w;
                break;
            }
        }
        if (!target) {
            for (const DockAppEntry &w : openWins) {
                if (w.active) {
                    target = &w;
                    break;
                }
            }
        }
        if (target && !target->name.trimmed().isEmpty()) {
            text = QStringLiteral("关闭当前 · ") + target->name.trimmed();
        } else {
            text = QStringLiteral("关闭当前窗口");
        }
    } else if (item.id == QLatin1String("tray_hub") && m_owner) {
        const SystemStatusSnapshot &st = m_owner->statusMonitor()->snapshot();
        if (st.unreadNotifications > 0) {
            text = QStringLiteral("托盘与通知（%1 条未读） ›").arg(st.unreadNotifications);
        } else if (st.trayNeedsAttention) {
            text = QStringLiteral("托盘与通知（有新提醒） ›");
        } else if (!text.endsWith(QStringLiteral("›"))) {
            text += QStringLiteral(" ›");
        }
    } else if (item.isBranchHub && !text.endsWith(QStringLiteral("›"))) {
        text += QStringLiteral(" ›");
    }
    return text;
}

QRectF RadialFanOverlay::labelPillRectLocal() const
{
    const QString rawText = activeLabelText();
    if (rawText.isEmpty() || m_slots.isEmpty() ||
        m_highlightedSlot < 0 || m_highlightedSlot >= m_slots.size()) {
        return {};
    }

    QFont f = font();
    f.setPointSize(12);
    f.setWeight(QFont::DemiBold);
    const QFontMetrics fm(f);
    const int maxTextW = qMax(120, qMin(width() - 56, 440));
    const QString text = fm.elidedText(rawText, Qt::ElideRight, maxTextW);
    const int pw = qMin(width() - 20, fm.horizontalAdvance(text) + 28);
    const QPointF origin = geometry().topLeft();

    if (m_mode == FlickFanMode) {
        const QPointF fingerLocal = m_fingerGlobal - origin;
        const qreal aboveY = fingerLocal.y() > 64.0 ? (fingerLocal.y() - 56.0) : (fingerLocal.y() + 34.0);
        return QRectF(qBound(8.0, fingerLocal.x() - pw * 0.5, width() - pw - 8.0),
                      qBound(8.0, aboveY, height() - 38.0),
                      pw, 32.0);
    }
    const QRectF slotLocal = m_slots[m_highlightedSlot].rectGlobal.translated(-origin);
    const qreal aboveY = slotLocal.top() > 44.0 ? (slotLocal.top() - 36.0) : (slotLocal.bottom() + 8.0);
    return QRectF(qBound(8.0, slotLocal.center().x() - pw * 0.5, width() - pw - 8.0),
                  qBound(8.0, aboveY, height() - 38.0),
                  pw, 32.0);
}

void RadialFanOverlay::updateHitMask()
{
    winId();
    if (m_mode == TapFanMode) {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        if (windowHandle()) {
            windowHandle()->setFlag(Qt::WindowTransparentForInput, false);
        }
#ifdef Q_OS_MACOS
        // On macOS, a global non-consuming NSEvent monitor dismisses the fan on
        // outside clicks without swallowing the user's click on desktop windows.
        // Pad slot ellipses generously so halos, fisheye hover zoom, and red badge
        // pills never clip, and cover the full drilled focus backdrop when drilled.
        QRegion region;
        const QPointF origin = geometry().topLeft();
        if (!m_drillStack.isEmpty()) {
            const DrillFrame &top = m_drillStack.last();
            const QPointF centerLocal = top.centerGlobal - origin;
            qreal maxR = 96.0;
            for (const RadialSlotItem &s : m_slots) {
                if (s.ringIndex == 1) {
                    maxR = qMax(maxR, QLineF(centerLocal, s.centerGlobal - origin).length() + s.rectGlobal.width() * 0.5);
                }
            }
            const qreal bdR = maxR + 36.0;
            region += QRegion(QRectF(centerLocal.x() - bdR, centerLocal.y() - bdR, bdR * 2.0, bdR * 2.0).toAlignedRect(),
                              QRegion::Ellipse);
        }
        for (const RadialSlotItem &s : m_slots) {
            const int pad = (s.kind == RadialSlotItem::AppSlot ||
                             s.kind == RadialSlotItem::NotificationSlot ||
                             s.id == QLatin1String("close_active")) ? 16 : 12;
            const QRect local = s.rectGlobal.translated(-origin).toAlignedRect().adjusted(-pad, -pad, pad, pad);
            if (!local.isEmpty())
                region += QRegion(local, QRegion::Ellipse);
        }
        const QRectF pill = labelPillRectLocal();
        if (!pill.isEmpty()) {
            region += QRegion(pill.toAlignedRect().adjusted(-4, -4, 4, 4));
        }
        setMask(region);
#else
        clearMask();
#endif
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        // Critical on Wayland (LayerShellQt): WA_TransparentForMouseEvents alone only applies
        // within a widget hierarchy; WindowTransparentForInput sets an empty wl_surface input
        // region so mapping RadialFanOverlay in FlickFanMode NEVER steals FloatingBall's grab!
        if (windowHandle()) {
            windowHandle()->setFlag(Qt::WindowTransparentForInput, false);
            windowHandle()->setFlag(Qt::WindowTransparentForInput, true);
        }
#ifdef Q_OS_MACOS
        clearMask();
#endif
    }
}

int RadialFanOverlay::hitSlotAtGlobal(const QPointF &globalPt, bool magneticSector) const
{
    if (m_slots.isEmpty()) return -1;

    // Drilled single-ring level: hit-test relative to top.centerGlobal with dedicated ← corridor!
    if (!m_drillStack.isEmpty()) {
        const DrillFrame &top = m_drillStack.last();
        // 1. Direct circular hit on ← backtrack orb or any child orb (or center in Tap mode)
        int directBest = -1;
        qreal directBestDist = 1e9;
        for (int i = 0; i < m_slots.size(); ++i) {
            if (magneticSector && m_slots[i].kind == RadialSlotItem::CenterSlot) continue;
            const qreal pad = (m_slots[i].kind == RadialSlotItem::BackSlot) ? 10.0 : 8.0;
            const qreal hitR = m_slots[i].rectGlobal.width() * 0.5 + pad;
            const qreal d = QLineF(globalPt, m_slots[i].centerGlobal).length();
            if (d <= hitR && d < directBestDist) {
                directBestDist = d;
                directBest = i;
            }
        }
        if (directBest >= 0) return directBest;
        if (!magneticSector) return -1;

        // 2. Magnetic sector around top.centerGlobal (skipping the 64-deg backtrack corridor toward ←!)
        const QLineF ray(top.centerGlobal, globalPt);
        if (ray.length() < 34.0) return -1;
        const double fingerDeg = qRadiansToDegrees(qAtan2(globalPt.y() - top.centerGlobal.y(),
                                                          globalPt.x() - top.centerGlobal.x()));
        const double backDeg = qRadiansToDegrees(qAtan2(top.backGlobal.y() - top.centerGlobal.y(),
                                                        top.backGlobal.x() - top.centerGlobal.x()));
        if (angularDistanceDeg(fingerDeg, backDeg) < 32.0) return -1;

        int bestSlot = -1;
        double bestDiff = 26.0;
        for (int i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].ringIndex != 1) continue;
            const QPointF c = m_slots[i].centerGlobal;
            const double slotDeg = qRadiansToDegrees(qAtan2(c.y() - top.centerGlobal.y(),
                                                            c.x() - top.centerGlobal.x()));
            const double diff = angularDistanceDeg(fingerDeg, slotDeg);
            if (diff < bestDiff) {
                bestDiff = diff;
                bestSlot = i;
            }
        }
        return bestSlot;
    }

    const QLineF ray(m_ballCenterGlobal, globalPt);
    const qreal dist = ray.length();
    if (dist < 26.0) return -1;

    // Once the finger/cursor slides past the Ring 0 / Ring 1 midpoint in magnetic slide mode,
    // cleanly release Ring 0 so outward swipes NEVER stick to first-level buttons!
    const bool pastRing0 = magneticSector && m_ringRadii.size() >= 2 &&
        dist >= ((m_ringRadii[0] + m_ringRadii[1]) * 0.5 - 5.0);

    int directBest = -1;
    qreal directBestDist = 1e9;
    for (int i = 0; i < m_slots.size(); ++i) {
        if (pastRing0 && m_slots[i].ringIndex == 0) continue;
        const qreal pad = (m_slots[i].id == QLatin1String("close_active")) ? 3.0 : 6.0;
        const qreal hitR = m_slots[i].rectGlobal.width() * 0.5 + pad;
        const qreal d = QLineF(globalPt, m_slots[i].centerGlobal).length();
        if (d <= hitR && d < directBestDist) {
            directBestDist = d;
            directBest = i;
        }
    }
    if (directBest >= 0) return directBest;
    if (!magneticSector) return -1;

    // Multi-ring magnetic hit at Root:
    // - Inside Ring 0: angular sector match on the 5 fixed buttons.
    // - Past Ring 0: 2D Euclidean Voronoi snap across all outer-ring open windows (ringIndex >= 1),
    //   so partially filled outer rings never create dead zones and Ring 0 never pulls back!
    //   Destructive close_active is excluded from magnetic Voronoi snap so swipes never misfire it.
    if (dist > m_outermostRadius + 40.0) return -1;

    if (pastRing0) {
        int nearestWin = -1;
        qreal nearestDist = 1e9;
        for (int i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].ringIndex < 1) continue;
            if (m_slots[i].id == QLatin1String("close_active")) continue;
            const qreal maxSnap = qMax(48.0, m_slots[i].rectGlobal.width() * 1.18);
            const qreal d = QLineF(globalPt, m_slots[i].centerGlobal).length();
            if (d <= maxSnap && d < nearestDist) {
                nearestDist = d;
                nearestWin = i;
            }
        }
        return nearestWin;
    }

    const double fingerDeg = qRadiansToDegrees(qAtan2(globalPt.y() - m_ballCenterGlobal.y(),
                                                      globalPt.x() - m_ballCenterGlobal.x()));
    int bestSlot = -1;
    double bestDiff = 34.0;
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].ringIndex != 0) continue;
        const QPointF c = m_slots[i].centerGlobal;
        const double slotDeg = qRadiansToDegrees(qAtan2(c.y() - m_ballCenterGlobal.y(),
                                                        c.x() - m_ballCenterGlobal.x()));
        const double diff = angularDistanceDeg(fingerDeg, slotDeg);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestSlot = i;
        }
    }
    return bestSlot;
}

void RadialFanOverlay::drillInto(const QString &key, const QString &title, const QColor &accent,
                                 const QPointF &hubPos, const TrayItemInfo &traySource)
{
    if (!m_owner) return;
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    QScreen *screen = m_owner->assignedScreen();
    if (!screen || !QGuiApplication::screens().contains(screen))
        screen = QGuiApplication::screenAt(m_ballCenterGlobal.toPoint());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen ? screen->availableGeometry() : geometry();

    const QPointF prevCenter = m_drillStack.isEmpty() ? m_ballCenterGlobal : m_drillStack.last().centerGlobal;
    // Clamp new center cleanly inside screen bounds so the full single ring fits comfortably!
    const qreal margin = 128.0 * m_owner->screenScale();
    const QPointF newCenter(
        qBound(bounds.left() + margin, hubPos.x(), bounds.right() - margin),
        qBound(bounds.top() + margin, hubPos.y(), bounds.bottom() - margin));

    // Place amber ← backtrack orb a crisp ~104px back toward prevCenter (clamped in-screen)
    QPointF backVec = prevCenter - newCenter;
    qreal vlen = std::hypot(backVec.x(), backVec.y());
    if (vlen < 1.0) {
        backVec = QPointF(0.0, -1.0);
        vlen = 1.0;
    }
    const qreal backDist = qBound(84.0, vlen, 108.0);
    QPointF backPos = newCenter + (backVec / vlen) * backDist;
    backPos.setX(qBound(bounds.left() + 34.0, backPos.x(), bounds.right() - 34.0));
    backPos.setY(qBound(bounds.top() + 34.0, backPos.y(), bounds.bottom() - 34.0));

    DrillFrame frame;
    frame.key = key;
    frame.title = title;
    frame.accentColor = accent;
    frame.centerGlobal = newCenter;
    frame.backGlobal = backPos;
    frame.traySource = traySource;
    m_drillStack.append(frame);
    m_drillCooldown.restart();
    m_highlightedSlot = -1;

    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
        updateHitMask();
        update();
    }
}

void RadialFanOverlay::popDrill()
{
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    if (m_drillStack.isEmpty()) return;
    m_drillStack.removeLast();
    m_drillCooldown.restart();
    m_highlightedSlot = -1;
    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
        updateHitMask();
        update();
    }
}

void RadialFanOverlay::clearDrill()
{
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    if (m_drillStack.isEmpty()) return;
    m_drillStack.clear();
    m_highlightedSlot = -1;
    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
        updateHitMask();
        update();
    }
}

void RadialFanOverlay::handleContinuousDragAt(const QPointF &globalPt)
{
    const bool cooled = !m_drillCooldown.isValid() || m_drillCooldown.elapsed() >= 210;

    // 1. Instant backtrack when finger slides onto ← 返回 (without lifting!)
    if (!m_drillStack.isEmpty() && cooled) {
        const DrillFrame &top = m_drillStack.last();
        if (QLineF(globalPt, top.backGlobal).length() <= 34.0) {
            popDrill();
            return;
        }
    }

    // 2. NEVER auto-expand Ring 0 › sub-balls (托盘 › / 工具 ›) during a slide!
    //    Ring 0 sits directly along the swipe path to all outer-ring open windows; sub-balls
    //    only open when explicitly clicked (TapFanMode) or released upon (FlickFanMode).
    const int hit = hitSlotAtGlobal(globalPt, true);
    if (hit >= 0 && hit < m_slots.size()) {
        const RadialSlotItem &s = m_slots[hit];
        // Optional Level-3 Tray DBusMenu dwell (hold 850ms on a tray orb with menuPath)
        if (s.kind == RadialSlotItem::TraySlot && !s.trayItem.menuPath.isEmpty() &&
            s.trayItem.menuPath != QLatin1String("/NO_DBUSMENU") &&
            (!m_drillStack.isEmpty() && m_drillStack.last().key == QLatin1String("tray"))) {
            if (m_pendingTrayDwell.service != s.trayItem.service ||
                m_pendingTrayDwell.objectPath != s.trayItem.objectPath ||
                !m_trayDwellTimer->isActive()) {
                m_pendingTrayDwell = s.trayItem;
                m_pendingTrayPos = s.centerGlobal;
                m_trayDwellTimer->start();
            }
            return;
        }
    }
    if (m_trayDwellTimer && m_trayDwellTimer->isActive()) m_trayDwellTimer->stop();
}

void RadialFanOverlay::openTapFan()
{
    m_mode = TapFanMode;
    m_drillStack.clear();
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    m_highlightedSlot = -1;
    m_pressedTapSlot = -1;
    recomputeFanLayout();
    updateHitMask();
    show();
    updateHitMask();
    raise();
    update();
}

void RadialFanOverlay::openFlickFan(const QPointF &fingerGlobal)
{
    m_mode = FlickFanMode;
    m_drillStack.clear();
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    m_drillCooldown.invalidate();
    m_fingerGlobal = fingerGlobal;
    recomputeFanLayout();
    m_highlightedSlot = hitSlotAtGlobal(fingerGlobal, true);
    updateHitMask();
    show();
    updateHitMask();
    raise();
    update();
}

void RadialFanOverlay::enterCrosshairAim(const QPointF &fingerGlobal, qreal dwellProgress)
{
    m_mode = CrosshairAimMode;
    m_fingerGlobal = fingerGlobal;
    m_dwellProgress = dwellProgress;
    m_highlightedSlot = -1;
    updateHitMask();
    show();
    updateHitMask();
    raise();
    update();
}

void RadialFanOverlay::updateCrosshairRect(const QPointF &anchorGlobal, const QPointF &fingerGlobal)
{
    m_mode = CrosshairRectMode;
    m_crosshairAnchorGlobal = anchorGlobal;
    m_fingerGlobal = fingerGlobal;
    m_highlightedSlot = -1;
    updateHitMask();
    show();
    updateHitMask();
    raise();
    update();
}

void RadialFanOverlay::updateFinger(const QPointF &fingerGlobal, int highlightedSlot, qreal dwellProgress)
{
    const bool slotChanged = (m_highlightedSlot != highlightedSlot);
    m_fingerGlobal = fingerGlobal;
    m_highlightedSlot = highlightedSlot;
    m_dwellProgress = dwellProgress;
#ifdef Q_OS_MACOS
    if (m_mode == TapFanMode && slotChanged) updateHitMask();
#else
    Q_UNUSED(slotChanged);
#endif
    update();
}

void RadialFanOverlay::syncProgress(qreal progress)
{
    m_progress = progress;
    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
        updateHitMask();
    }
    update();
}

void RadialFanOverlay::refreshLiveLayout()
{
    if (m_mode == TapFanMode || m_mode == FlickFanMode) {
        recomputeFanLayout();
        updateHitMask();
        update();
    }
}

void RadialFanOverlay::dismissOverlay(bool immediate)
{
    if (m_trayDwellTimer) m_trayDwellTimer->stop();
    if (immediate) {
        m_mode = HiddenMode;
        m_drillStack.clear();
        m_highlightedSlot = -1;
        m_pressedTapSlot = -1;
        m_progress = 0.0;
        hide();
    }
    update();
}

QPointF RadialFanOverlay::pointerGlobalPos(const QPointF &localPos, const QPointF &eventGlobalPos) const
{
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
        return QPointF(geometry().topLeft()) + localPos;
    return eventGlobalPos;
}

bool RadialFanOverlay::event(QEvent *ev)
{
    // Safety net on Wayland: if compositor routes touch updates/ends to RadialFanOverlay
    // while m_owner is in FlickFan/Crosshair, forward straight to m_owner so drags never stall!
    if (m_mode != TapFanMode && m_mode != HiddenMode && m_owner &&
        (ev->type() == QEvent::TouchUpdate || ev->type() == QEvent::TouchEnd)) {
        auto *te = static_cast<QTouchEvent *>(ev);
        if (!te->points().isEmpty()) {
            const auto &pt = te->points().first();
            const QPointF globalPt = pointerGlobalPos(pt.position(), pt.globalPosition());
            if (ev->type() == QEvent::TouchUpdate) m_owner->updatePointerInteraction(globalPt);
            else m_owner->endPointerInteraction(globalPt);
            return true;
        }
    }

    if (m_mode == TapFanMode &&
        (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchUpdate ||
         ev->type() == QEvent::TouchEnd)) {
        auto *te = static_cast<QTouchEvent *>(ev);
        if (!te->points().isEmpty()) {
            const auto &pt = te->points().first();
            const QPointF globalPt = pointerGlobalPos(pt.position(), pt.globalPosition());
            const int slot = hitSlotAtGlobal(globalPt, false);
            const qreal distFromBall = QLineF(m_ballCenterGlobal, globalPt).length();
            if (ev->type() == QEvent::TouchBegin) {
                if (slot < 0 && distFromBall > m_outermostRadius + 28.0) {
                    emit dismissedByUser();
                    return true;
                }
                m_tapPressGlobal = globalPt;
                m_tapDragged = false;
                m_highlightedSlot = slot;
                updateHitMask();
                update();
            } else if (ev->type() == QEvent::TouchUpdate) {
                if (QLineF(globalPt, m_tapPressGlobal).length() >= 8.0) m_tapDragged = true;
                handleContinuousDragAt(globalPt);
                const int refreshedSlot = hitSlotAtGlobal(globalPt, true);
                if (refreshedSlot != m_highlightedSlot) {
                    m_highlightedSlot = refreshedSlot;
                    updateHitMask();
                    update();
                }
            } else if (ev->type() == QEvent::TouchEnd) {
                if (m_trayDwellTimer) m_trayDwellTimer->stop();
                const int target = m_tapDragged ? hitSlotAtGlobal(globalPt, true)
                                                : (slot >= 0 ? slot : m_highlightedSlot);
                m_highlightedSlot = -1;
                m_tapDragged = false;
                if (target >= 0) emit slotTapped(target);
                else emit dismissedByUser();
            }
            return true;
        }
    }
    return QWidget::event(ev);
}

void RadialFanOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mode != TapFanMode && m_mode != HiddenMode && m_owner &&
        (event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        m_owner->updatePointerInteraction(pointerGlobalPos(event->position(), event->globalPosition()));
        event->accept();
        return;
    }
    if (m_mode == TapFanMode) {
        const QPointF globalPt = pointerGlobalPos(event->position(), event->globalPosition());
        if (event->buttons() & Qt::LeftButton) {
            if (QLineF(globalPt, m_tapPressGlobal).length() >= 8.0) m_tapDragged = true;
            handleContinuousDragAt(globalPt);
            const int next = hitSlotAtGlobal(globalPt, true);
            if (next != m_highlightedSlot) {
                m_highlightedSlot = next;
                updateHitMask();
                update();
            }
            return;
        }
        // Pure hover (button NOT held): highlight cleanly with ZERO auto-branch flicker!
        const int next = hitSlotAtGlobal(globalPt, false);
        if (next != m_highlightedSlot) {
            m_highlightedSlot = next;
            setCursor(next >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            updateHitMask();
            update();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void RadialFanOverlay::mousePressEvent(QMouseEvent *event)
{
    if (m_mode == TapFanMode) {
        const QPointF globalPt = pointerGlobalPos(event->position(), event->globalPosition());
        const int slot = hitSlotAtGlobal(globalPt, false);
        const qreal distFromBall = QLineF(m_ballCenterGlobal, globalPt).length();
        if (slot < 0 && (event->button() != Qt::LeftButton || distFromBall > m_outermostRadius + 28.0)) {
            emit dismissedByUser();
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton && slot >= 0) {
            emit slotContextMenuRequested(slot, globalPt.toPoint());
            event->accept();
            return;
        }
        if (event->button() == Qt::MiddleButton && slot >= 0 && slot < m_slots.size() &&
            (m_slots[slot].kind == RadialSlotItem::AppSlot ||
             m_slots[slot].kind == RadialSlotItem::NotificationSlot)) {
            m_pressedTapSlot = slot;
            event->accept();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            m_tapPressGlobal = globalPt;
            m_tapDragged = false;
            m_pressedTapSlot = slot;
            m_highlightedSlot = m_pressedTapSlot;
            updateHitMask();
            update();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void RadialFanOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_mode != TapFanMode && m_mode != HiddenMode && m_owner &&
        (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_owner->endPointerInteraction(pointerGlobalPos(event->position(), event->globalPosition()));
        event->accept();
        return;
    }
    if (m_mode == TapFanMode && event->button() == Qt::MiddleButton) {
        const QPointF globalPt = pointerGlobalPos(event->position(), event->globalPosition());
        const int slot = hitSlotAtGlobal(globalPt, false);
        m_pressedTapSlot = -1;
        if (slot >= 0 && slot < m_slots.size() &&
            (m_slots[slot].kind == RadialSlotItem::AppSlot ||
             m_slots[slot].kind == RadialSlotItem::NotificationSlot)) {
            emit slotMiddleClicked(slot);
        }
        event->accept();
        return;
    }
    if (m_mode == TapFanMode && event->button() == Qt::LeftButton) {
        if (m_trayDwellTimer) m_trayDwellTimer->stop();
        const QPointF globalPt = pointerGlobalPos(event->position(), event->globalPosition());
        const int slot = m_tapDragged ? hitSlotAtGlobal(globalPt, true) : hitSlotAtGlobal(globalPt, false);
        m_highlightedSlot = -1;
        m_pressedTapSlot = -1;
        m_tapDragged = false;
        updateHitMask();
        update();
        if (slot >= 0) emit slotTapped(slot);
        else emit dismissedByUser();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void RadialFanOverlay::wheelEvent(QWheelEvent *event)
{
    if (m_mode == TapFanMode && event->angleDelta().y() != 0) {
        emit ringCountStepRequested(event->angleDelta().y() > 0 ? 1 : -1);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void RadialFanOverlay::leaveEvent(QEvent *event)
{
    if (m_mode == TapFanMode && m_highlightedSlot >= 0) {
#ifdef Q_OS_MACOS
        // Updating setMask() on Cocoa refreshes NSTrackingArea and can emit a
        // synthetic Leave while the pointer is still squarely over a slot.
        if (hitSlotAtGlobal(QCursor::pos(), false) >= 0) {
            QWidget::leaveEvent(event);
            return;
        }
#endif
        m_highlightedSlot = -1;
        updateHitMask();
        update();
    }
    QWidget::leaveEvent(event);
}

void RadialFanOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit dismissedByUser();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RadialFanOverlay::paintEvent(QPaintEvent *)
{
    if (m_mode == HiddenMode || !m_owner) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF origin = geometry().topLeft();

    // ------------------------------------------------------------------------
    // Mode A: fooView Live Crosshair Aim or Crop Rectangle
    // ------------------------------------------------------------------------
    if (m_mode == CrosshairAimMode || m_mode == CrosshairRectMode) {
        const QPointF localFinger = m_fingerGlobal - origin;

        if (m_mode == CrosshairRectMode) {
            const QPointF localAnchor = m_crosshairAnchorGlobal - origin;
            const QRectF cropRect = QRectF(localAnchor, localFinger).normalized();

            QPainterPath dimPath;
            dimPath.setFillRule(Qt::OddEvenFill);
            dimPath.addRect(QRectF(rect()));
            dimPath.addRoundedRect(cropRect, 4, 4);
            p.fillPath(dimPath, QColor(10, 14, 22, 88));

            p.setPen(QPen(QColor("#5bb4ff"), 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(cropRect, 4, 4);

            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#ffffff"));
            for (const QPointF &pt : {cropRect.topLeft(), cropRect.topRight(),
                                      cropRect.bottomLeft(), cropRect.bottomRight()}) {
                p.drawEllipse(pt, 4.5, 4.5);
            }

            const QString badgeText = QStringLiteral("%1 × %2 · 松手立即识字 / 贴图 / 复制")
                                          .arg(qRound(cropRect.width()))
                                          .arg(qRound(cropRect.height()));
            QFont f = font();
            f.setPointSize(12);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            const int bw = p.fontMetrics().horizontalAdvance(badgeText) + 28;
            const qreal by = cropRect.top() > 48 ? cropRect.top() - 38 : cropRect.bottom() + 10;
            const QRectF badge(qBound(8.0, cropRect.center().x() - bw / 2.0, width() - bw - 8.0),
                               qBound(8.0, by, height() - 38.0), bw, 32);
            p.setPen(QPen(QColor(255, 255, 255, 55), 1.0));
            p.setBrush(QColor(18, 22, 30, 232));
            p.drawRoundedRect(badge, 16, 16);
            p.setPen(QColor("#f5f8fd"));
            p.drawText(badge, Qt::AlignCenter, badgeText);
            return;
        }

        // CrosshairAimMode: glowing fooView reticle + 320ms dwell progress arc
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 22, 30, 210));
        p.drawEllipse(localFinger, 26, 26);

        p.setPen(QPen(QColor(255, 255, 255, 65), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(localFinger, 26, 26);

        if (m_dwellProgress > 0.02) {
            p.setPen(QPen(QColor("#5bb4ff"), 3.2, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(localFinger.x() - 26, localFinger.y() - 26, 52, 52),
                      90 * 16, -qRound(m_dwellProgress * 360.0 * 16.0));
        }

        FloatingIcons::draw(p, FloatingIcons::Crosshair,
                            QRectF(localFinger.x() - 14, localFinger.y() - 14, 28, 28),
                            QColor("#7cc8ff"));

        const QString tip = QStringLiteral("停住 0.3 秒直接拉框 · 松手进全屏框选");
        QFont f = font();
        f.setPointSize(11);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const int tw = p.fontMetrics().horizontalAdvance(tip) + 26;
        const qreal ty = localFinger.y() > 74 ? localFinger.y() - 62 : localFinger.y() + 34;
        const QRectF pill(qBound(8.0, localFinger.x() - tw / 2.0, width() - tw - 8.0), ty, tw, 30);
        p.setPen(QPen(QColor(255, 255, 255, 45), 1.0));
        p.setBrush(QColor(18, 22, 30, 225));
        p.drawRoundedRect(pill, 15, 15);
        p.setPen(QColor("#eef4fc"));
        p.drawText(pill, Qt::AlignCenter, tip);
        return;
    }

    // ------------------------------------------------------------------------
    // Mode B: Multi-Ring Concentric Fan (1..10 Rings, Zero Jagged Black Ring)
    // ------------------------------------------------------------------------
    const qreal reveal = qBound(0.0, m_progress, 1.0);
    if (reveal <= 0.01) return;

    const QPointF localBallCenter = m_ballCenterGlobal - origin;
    const bool drilled = !m_drillStack.isEmpty();

    if (drilled) {
        const DrillFrame &top = m_drillStack.last();
        const QPointF centerLocal = top.centerGlobal - origin;
        const QPointF backLocal = top.backGlobal - origin;
        qreal maxR = 96.0;
        for (const RadialSlotItem &s : m_slots) {
            if (s.ringIndex == 1) {
                maxR = qMax(maxR, QLineF(centerLocal, s.centerGlobal - origin).length() + s.rectGlobal.width() * 0.5);
            }
        }
        // Soft circular dark focus backdrop isolating the single drilled ring over any wallpaper
        p.save();
        p.setOpacity(reveal);
        QRadialGradient backdrop(centerLocal, maxR + 34.0);
        backdrop.setColorAt(0.0, QColor(10, 14, 24, 185));
        backdrop.setColorAt(0.68, QColor(10, 14, 24, 140));
        backdrop.setColorAt(0.88, QColor(10, 14, 24, 52));
        backdrop.setColorAt(1.0, QColor(10, 14, 24, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(backdrop);
        p.drawEllipse(centerLocal, maxR + 34.0, maxR + 34.0);

        // Luminous breadcrumb spine connecting ← Backtrack orb -> Drilled Center
        p.setPen(QPen(top.accentColor, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(backLocal, centerLocal);

        // Crisp short straight radial spokes from Center -> each child orb
        qreal centerR = 22.0;
        for (const RadialSlotItem &s : m_slots) {
            if (s.kind == RadialSlotItem::CenterSlot) { centerR = s.rectGlobal.width() * 0.5; break; }
        }
        for (int i = 0; i < m_slots.size(); ++i) {
            const RadialSlotItem &s = m_slots[i];
            if (s.ringIndex != 1) continue;
            const QPointF targetLocal = s.centerGlobal - origin;
            const QPointF delta = targetLocal - centerLocal;
            const qreal dist = std::hypot(delta.x(), delta.y());
            const qreal targetR = s.rectGlobal.width() * 0.5;
            if (dist <= centerR + targetR + 2.0) continue;
            const QPointF u = delta / dist;
            const bool isHi = (i == m_highlightedSlot);
            QColor spokeColor = isHi ? QColor("#7cc8ff") : top.accentColor;
            spokeColor.setAlpha(isHi ? 225 : 105);
            p.setPen(QPen(spokeColor, isHi ? 2.6 : 1.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(centerLocal + u * (centerR - 0.5), targetLocal - u * (targetR - 0.5));
        }
        p.restore();
    } else {
        // Subtle directional bearing rays from main ball toward notifying windows,
        // notification orbs, or tray_hub when tray/unattributed alerts exist!
        p.save();
        QPen attnPen(QColor(255, 82, 114, 118), 1.8, Qt::DashLine, Qt::RoundCap);
        p.setPen(attnPen);
        const qreal startRim = qMax(m_owner->width(), m_owner->height()) * 0.5 + 2.0;
        bool drewSpecificRay = false;
        for (const RadialSlotItem &s : m_slots) {
            const bool isNotifying =
                (s.kind == RadialSlotItem::AppSlot && (s.app.demandsAttention || s.unreadCount > 0)) ||
                (s.kind == RadialSlotItem::NotificationSlot);
            if (isNotifying) {
                drewSpecificRay = true;
                const QPointF targetLocal = s.centerGlobal - origin;
                const QPointF vec = targetLocal - localBallCenter;
                const qreal len = std::hypot(vec.x(), vec.y());
                if (len > startRim + s.rectGlobal.width() * 0.5) {
                    const QPointF u = vec / len;
                    p.drawLine(localBallCenter + u * startRim,
                               targetLocal - u * (s.rectGlobal.width() * 0.52));
                }
            }
        }
        const SystemStatusSnapshot &stNow = m_owner->statusMonitor()->snapshot();
        if (stNow.trayNeedsAttention || (stNow.unreadNotifications > 0 && !drewSpecificRay)) {
            for (const RadialSlotItem &s : m_slots) {
                if (s.ringIndex == 0 && s.id == QLatin1String("tray_hub")) {
                    const QPointF targetLocal = s.centerGlobal - origin;
                    const QPointF vec = targetLocal - localBallCenter;
                    const qreal len = std::hypot(vec.x(), vec.y());
                    if (len > startRim + s.rectGlobal.width() * 0.5) {
                        const QPointF u = vec / len;
                        p.drawLine(localBallCenter + u * startRim,
                                   targetLocal - u * (s.rectGlobal.width() * 0.52));
                    }
                    break;
                }
            }
        }
        if (m_mode == FlickFanMode && m_highlightedSlot >= 0 && m_highlightedSlot < m_slots.size()) {
            const QPointF slotLocal = m_slots[m_highlightedSlot].centerGlobal - origin;
            const QColor rayColor = (m_slots[m_highlightedSlot].id == QLatin1String("close_active"))
                ? QColor(255, 95, 87, 200)
                : QColor(102, 188, 255, 175);
            p.setPen(QPen(rayColor, 2.5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(localBallCenter, slotLocal);
        }
        p.restore();
    }

    // Draw all active ring slots (draw highlighted slot last so dense fisheye magnification never clips!)
    QList<int> drawOrder;
    drawOrder.reserve(m_slots.size());
    for (int i = 0; i < m_slots.size(); ++i) {
        if (i != m_highlightedSlot) drawOrder.append(i);
    }
    if (m_highlightedSlot >= 0 && m_highlightedSlot < m_slots.size()) {
        drawOrder.append(m_highlightedSlot);
    }

    for (int i : drawOrder) {
        const RadialSlotItem &item = m_slots[i];
        QRectF slotRect = item.rectGlobal.translated(-origin);
        const bool selected = (i == m_highlightedSlot);
        if (selected && (item.kind == RadialSlotItem::AppSlot ||
                         item.kind == RadialSlotItem::NotificationSlot ||
                         item.id == QLatin1String("close_active")) &&
            slotRect.width() < 44.0) {
            const qreal grown = qMin(48.0, slotRect.width() * 1.28);
            const QPointF sc = slotRect.center();
            slotRect = QRectF(sc.x() - grown * 0.5, sc.y() - grown * 0.5, grown, grown);
        }
        const qreal ringAlphaFactor = item.ringIndex == 0 ? 1.0 : qMax(0.90, 0.98 - item.ringIndex * 0.012);
        const qreal alpha = (selected ? 0.99 : m_owner->activeOpacity() * ringAlphaFactor) * reveal;

        p.save();
        p.setOpacity(alpha);

        if (item.kind == RadialSlotItem::BackSlot) {
            const QRectF face = slotRect.adjusted(1.5, 1.5, -1.5, -1.5);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 62));
            p.drawEllipse(face.translated(0, 1.6));
            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected) {
                grad.setColorAt(0.0, QColor(245, 158, 11, 245));
                grad.setColorAt(1.0, QColor(180, 83, 9, 248));
            } else {
                grad.setColorAt(0.0, QColor(64, 48, 24, 236));
                grad.setColorAt(1.0, QColor(32, 24, 14, 244));
            }
            p.setBrush(grad);
            p.setPen(QPen(QColor("#fbbf24"), selected ? 2.2 : 1.5));
            p.drawEllipse(face);
            const qreal iconDim = face.width() * 0.48;
            FloatingIcons::draw(p, FloatingIcons::Back,
                                QRectF(face.center().x() - iconDim * 0.5, face.center().y() - iconDim * 0.5, iconDim, iconDim),
                                selected ? QColor("#ffffff") : QColor("#fcd34d"));
        } else if (item.kind == RadialSlotItem::CenterSlot) {
            const QRectF face = slotRect.adjusted(1.4, 1.4, -1.4, -1.4);
            const QColor accent = item.accentColor.isValid() ? item.accentColor : QColor("#7cc8ff");
            QLinearGradient grad(face.topLeft(), face.bottomRight());
            QColor hi = accent; hi.setAlpha(210);
            QColor lo = accent.darker(190); lo.setAlpha(242);
            grad.setColorAt(0.0, hi);
            grad.setColorAt(1.0, lo);
            p.setBrush(grad);
            p.setPen(QPen(accent.lighter(120), 1.9));
            p.drawEllipse(face);
            QFont f = font();
            f.setPixelSize(qRound(face.width() * (item.shortLabel.size() <= 2 ? 0.34 : 0.25)));
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(QColor("#ffffff"));
            p.drawText(face, Qt::AlignCenter, item.shortLabel);
        } else if (item.kind == RadialSlotItem::TrayMenuSlot) {
            const QRectF face = slotRect.adjusted(1.6, 1.6, -1.6, -1.6);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 56));
            p.drawEllipse(face.translated(0, 1.6));
            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected || item.toggleActive) {
                grad.setColorAt(0.0, QColor(42, 112, 186, 245));
                grad.setColorAt(1.0, QColor(24, 62, 118, 248));
            } else {
                grad.setColorAt(0.0, QColor(36, 44, 58, 236));
                grad.setColorAt(1.0, QColor(18, 23, 33, 242));
            }
            p.setBrush(grad);
            p.setPen(QPen(selected ? QColor("#7cc8ff") : QColor(102, 217, 255, 130), selected ? 2.0 : 1.2));
            p.drawEllipse(face);
            if (!item.trayMenuEntry.icon.isNull()) {
                const qreal iconDim = face.width() * 0.56;
                item.trayMenuEntry.icon.paint(&p, QRectF(face.center().x() - iconDim * 0.5,
                                                         face.center().y() - iconDim * 0.5, iconDim, iconDim).toAlignedRect(),
                                              Qt::AlignCenter);
            } else {
                QFont f = font();
                f.setPixelSize(qRound(face.width() * 0.30));
                f.setWeight(QFont::DemiBold);
                p.setFont(f);
                p.setPen(QColor("#f2f6fc"));
                p.drawText(face, Qt::AlignCenter, item.shortLabel.left(2));
            }
        } else if (item.kind == RadialSlotItem::TraySlot) {
            // Branched System Tray orb: frosted obsidian circular capsule + real SNI icon
            const QRectF face = slotRect.adjusted(1.6, 1.6, -1.6, -1.6);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 56));
            p.drawEllipse(face.translated(0, 1.6));

            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected) {
                grad.setColorAt(0.0, QColor(42, 112, 186, 248));
                grad.setColorAt(1.0, QColor(24, 62, 118, 248));
            } else if (item.toggleActive) {
                grad.setColorAt(0.0, QColor(92, 28, 46, 240));
                grad.setColorAt(1.0, QColor(42, 18, 28, 244));
            } else {
                grad.setColorAt(0.0, QColor(36, 44, 58, 236));
                grad.setColorAt(1.0, QColor(18, 23, 33, 242));
            }
            p.setBrush(grad);
            const QColor border = selected ? QColor("#7cc8ff")
                : (item.toggleActive ? QColor("#ff5272") : QColor(102, 217, 255, 115));
            p.setPen(QPen(border, (selected || item.toggleActive) ? 2.0 : 1.2));
            p.drawEllipse(face);

            const qreal iconDim = face.width() * 0.62;
            const QRectF iconRect(face.center().x() - iconDim * 0.5, face.center().y() - iconDim * 0.5,
                                  iconDim, iconDim);
            if (!item.trayItem.icon.isNull()) {
                item.trayItem.icon.paint(&p, iconRect.toAlignedRect(), Qt::AlignCenter);
            } else {
                FloatingIcons::draw(p, FloatingIcons::TrayHub, iconRect.adjusted(3, 3, -3, -3), QColor("#8ad0ff"));
            }
            if (item.toggleActive || item.unreadCount > 0) {
                drawRedBadgePill(p, QPointF(face.right() - 5.5, face.top() + 5.5),
                                 qMax(1, item.unreadCount), font());
            }
        } else if (item.kind == RadialSlotItem::NotificationSlot) {
            // Unread background/tray/system notification orb: warm coral halo + glass ring + real app icon + count badge
            const QPointF c = slotRect.center();
            const qreal haloRadius = slotRect.width() * (selected ? 0.74 : 0.68);
            QRadialGradient halo(c, haloRadius);
            if (selected) {
                halo.setColorAt(0.00, QColor(255, 82, 114, 220));
                halo.setColorAt(0.50, QColor(210, 46, 84, 130));
                halo.setColorAt(1.00, QColor(180, 30, 68, 0));
            } else {
                halo.setColorAt(0.00, QColor(255, 77, 109, 175));
                halo.setColorAt(0.54, QColor(168, 36, 68, 92));
                halo.setColorAt(1.00, QColor(120, 24, 48, 0));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, haloRadius, haloRadius);

            const QRectF face = slotRect.adjusted(1.8, 1.8, -1.8, -1.8);
            QLinearGradient grad(face.topLeft(), face.bottomRight());
            grad.setColorAt(0.0, selected ? QColor(126, 36, 62, 246) : QColor(72, 26, 42, 238));
            grad.setColorAt(1.0, selected ? QColor(74, 20, 38, 248) : QColor(30, 16, 26, 244));
            p.setBrush(grad);
            p.setPen(QPen(selected ? QColor("#ff8fa3") : QColor("#ff5272"), selected ? 2.1 : 1.7));
            p.drawEllipse(face);

            const qreal iconDim = face.width() * 0.66;
            const QRectF iconRect(c.x() - iconDim * 0.5, c.y() - iconDim * 0.5, iconDim, iconDim);
            if (!item.notificationItem.icon.isNull()) {
                item.notificationItem.icon.paint(&p, iconRect.toAlignedRect(), Qt::AlignCenter);
            } else {
                FloatingIcons::draw(p, FloatingIcons::Bell, iconRect.adjusted(4, 4, -4, -4), QColor("#ffe4e6"));
            }

            drawRedBadgePill(p, QPointF(slotRect.right() - slotRect.width() * 0.14,
                                        slotRect.top() + slotRect.height() * 0.14),
                             qMax(1, item.unreadCount), font());
        } else if (item.kind == RadialSlotItem::SystemSlot) {
            // Branched KDE System Control orb (iOS/KDE Control Center tactile toggle glass)
            const QRectF face = slotRect.adjusted(1.6, 1.6, -1.6, -1.6);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 56));
            p.drawEllipse(face.translated(0, 1.6));

            const QColor accent = item.accentColor.isValid() ? item.accentColor : QColor("#9aa8ff");
            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected) {
                QColor hi = accent.lighter(118); hi.setAlpha(246);
                QColor lo = accent.darker(140); lo.setAlpha(248);
                grad.setColorAt(0.0, hi);
                grad.setColorAt(1.0, lo);
            } else if (item.toggleActive) {
                QColor hi = accent; hi.setAlpha(185);
                QColor lo = accent.darker(185); lo.setAlpha(232);
                grad.setColorAt(0.0, hi);
                grad.setColorAt(1.0, lo);
            } else {
                grad.setColorAt(0.0, QColor(36, 42, 56, 234));
                grad.setColorAt(1.0, QColor(18, 22, 32, 242));
            }
            p.setBrush(grad);
            const QColor border = (selected || item.toggleActive) ? accent : QColor(154, 168, 255, 96);
            p.setPen(QPen(border, (selected || item.toggleActive) ? 1.9 : 1.1));
            p.drawEllipse(face);

            const qreal iconDim = face.width() * 0.48;
            const QRectF iconRect(face.center().x() - iconDim * 0.5, face.center().y() - iconDim * 0.5,
                                  iconDim, iconDim);
            const QColor ink = (selected || item.toggleActive) ? QColor("#ffffff") : accent;
            FloatingIcons::draw(p, iconForAction(item.id), iconRect, ink);
        } else if (item.kind == RadialSlotItem::AppSlot) {
            // Borderless large App Icon floating over a soft diffused circular radial-gradient halo
            // that cleanly separates the icon from light/dark desktop backgrounds without a hard circle!
            const QPointF c = slotRect.center();
            const qreal haloRadius = slotRect.width() * (selected ? 0.72 : 0.66);
            QRadialGradient halo(c, haloRadius);
            if (selected) {
                halo.setColorAt(0.00, QColor(68, 152, 255, 215));
                halo.setColorAt(0.46, QColor(42, 114, 224, 138));
                halo.setColorAt(0.76, QColor(28, 82, 178, 46));
                halo.setColorAt(1.00, QColor(20, 60, 140, 0));
            } else if (item.app.demandsAttention || item.unreadCount > 0) {
                halo.setColorAt(0.00, QColor(255, 77, 109, 195));
                halo.setColorAt(0.48, QColor(196, 42, 74, 118));
                halo.setColorAt(0.78, QColor(140, 24, 52, 38));
                halo.setColorAt(1.00, QColor(120, 20, 44, 0));
            } else if (item.app.active) {
                halo.setColorAt(0.00, QColor(36, 98, 186, 178));
                halo.setColorAt(0.50, QColor(18, 44, 88, 112));
                halo.setColorAt(0.80, QColor(14, 24, 44, 34));
                halo.setColorAt(1.00, QColor(12, 16, 24, 0));
            } else {
                halo.setColorAt(0.00, QColor(14, 18, 26, 172));
                halo.setColorAt(0.48, QColor(14, 18, 26, 116));
                halo.setColorAt(0.78, QColor(14, 18, 26, 36));
                halo.setColorAt(1.00, QColor(14, 18, 26, 0));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, haloRadius, haloRadius);

            // Crisp, larger native app icon
            const qreal iconDim = slotRect.width() * 0.84;
            const qreal yShift = item.app.running ? -2.0 : 0.0;
            const QRectF iconRect(c.x() - iconDim * 0.5, c.y() - iconDim * 0.5 + yShift, iconDim, iconDim);
            if (!item.app.icon.isNull()) {
                item.app.icon.paint(&p, iconRect.toAlignedRect(), Qt::AlignCenter);
            } else {
                FloatingIcons::draw(p, FloatingIcons::Orb, iconRect.adjusted(6, 6, -6, -6), QColor("#dce8f8"));
            }

            // Glowing live Dock indicator dot directly beneath running apps
            if (item.app.running) {
                const QPointF dotPos(c.x(), iconRect.bottom() + 3.2);
                QRadialGradient dotGlow(dotPos, 5.5);
                dotGlow.setColorAt(0.0, item.app.active ? QColor(120, 206, 255, 220) : QColor(240, 246, 255, 195));
                dotGlow.setColorAt(1.0, QColor(120, 206, 255, 0));
                p.setBrush(dotGlow);
                p.drawEllipse(dotPos, 5.5, 5.5);
                p.setBrush(item.app.active ? QColor("#78ceff") : QColor(244, 248, 255, 240));
                p.drawEllipse(dotPos, item.app.active ? 2.5 : 2.0, item.app.active ? 2.5 : 2.0);
            }

            // Unread message / demandsAttention badge pill on top-right of window orb!
            if (item.app.demandsAttention || item.unreadCount > 0) {
                drawRedBadgePill(p, QPointF(slotRect.right() - slotRect.width() * 0.14,
                                            slotRect.top() + slotRect.height() * 0.14),
                                 qMax(1, item.unreadCount), font());
            }
        } else if (item.id == QLatin1String("close_active")) {
            // Fixed Ring-1 tail "Close Active Window" crimson frosted glass orb:
            // distinct macOS-inspired red accent with soft aura so it stands apart from open apps.
            const QPointF c = slotRect.center();
            const qreal haloRadius = slotRect.width() * (selected ? 0.72 : 0.64);
            QRadialGradient halo(c, haloRadius);
            if (selected) {
                halo.setColorAt(0.00, QColor(255, 95, 87, 210));
                halo.setColorAt(0.50, QColor(217, 56, 50, 118));
                halo.setColorAt(1.00, QColor(180, 34, 34, 0));
            } else {
                halo.setColorAt(0.00, QColor(188, 46, 54, 128));
                halo.setColorAt(0.55, QColor(110, 24, 32, 58));
                halo.setColorAt(1.00, QColor(80, 16, 24, 0));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, haloRadius, haloRadius);

            const QRectF face = slotRect.adjusted(1.8, 1.8, -1.8, -1.8);
            p.setBrush(QColor(0, 0, 0, 56));
            p.drawEllipse(face.translated(0, 1.6));

            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected) {
                grad.setColorAt(0.0, QColor(255, 95, 87, 248));
                grad.setColorAt(1.0, QColor(212, 48, 44, 250));
            } else {
                grad.setColorAt(0.0, QColor(152, 42, 48, 240));
                grad.setColorAt(1.0, QColor(78, 22, 28, 246));
            }
            p.setBrush(grad);
            p.setPen(QPen(selected ? QColor("#ffaaa4") : QColor(255, 108, 98, 210), selected ? 2.1 : 1.5));
            p.drawEllipse(face);

            const qreal iconDim = face.width() * 0.56;
            const QRectF iconRect(face.center().x() - iconDim * 0.5,
                                  face.center().y() - iconDim * 0.5,
                                  iconDim, iconDim);
            FloatingIcons::draw(p, FloatingIcons::Close, iconRect,
                                selected ? QColor("#ffffff") : QColor("#ffe4e2"));
        } else {
            // Inner rings (Ring 1 & Ring 2): tactile obsidian glass quick-action spheres
            // Special styling for KWin virtual desktop slots (vdesk1..vdesk4 -> 広/栞/発/伝)
            int vdeskIdx = -1;
            if (item.id.startsWith(QLatin1String("vdesk")) && item.id.size() == 6) {
                vdeskIdx = item.id.at(5).digitValue() - 1;
            }
            static const QColor kVDeskColors[4]{
                QColor("#5b9fea"), // 広 (Daily)
                QColor("#65bd8a"), // 栞 (Notes)
                QColor("#e6a35c"), // 発 (Dev/Terminal)
                QColor("#b18ae3")  // 伝 (Comms)
            };
            static const QString kVDeskFallbackGlyphs[4]{
                QStringLiteral("広"), QStringLiteral("栞"), QStringLiteral("発"), QStringLiteral("伝")
            };
            bool isCurrentVDesk = false;
            QString vdeskGlyph;
            QColor vdeskColor = QColor("#f2f6fc");
            if (vdeskIdx >= 0 && vdeskIdx < 4) {
                vdeskColor = kVDeskColors[vdeskIdx];
                vdeskGlyph = kVDeskFallbackGlyphs[vdeskIdx];
                const auto &vds = m_owner->statusMonitor()->snapshot().virtualDesktops;
                if (vdeskIdx < vds.size()) {
                    if (!vds[vdeskIdx].name.isEmpty()) vdeskGlyph = vds[vdeskIdx].name;
                    isCurrentVDesk = vds[vdeskIdx].current;
                }
            }

            const QRectF face = slotRect.adjusted(1.8, 1.8, -1.8, -1.8);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 50));
            p.drawEllipse(face.translated(0, 1.6));

            QLinearGradient grad(face.topLeft(), face.bottomRight());
            if (selected) {
                grad.setColorAt(0.0, QColor(42, 92, 166, 246));
                grad.setColorAt(1.0, QColor(24, 54, 108, 248));
            } else if (isCurrentVDesk) {
                QColor tinted = vdeskColor;
                tinted.setAlpha(72);
                grad.setColorAt(0.0, tinted);
                grad.setColorAt(1.0, QColor(22, 26, 36, 242));
            } else if (item.ringIndex == 1) {
                grad.setColorAt(0.0, QColor(38, 43, 54, 226));
                grad.setColorAt(1.0, QColor(19, 22, 30, 236));
            } else {
                grad.setColorAt(0.0, QColor(44, 48, 58, 234));
                grad.setColorAt(1.0, QColor(22, 25, 33, 242));
            }
            p.setBrush(grad);
            const QColor ringPenColor = selected ? QColor("#7cc8ff")
                : (isCurrentVDesk ? vdeskColor
                   : (item.isBranchHub ? QColor(item.accentColor.red(), item.accentColor.green(), item.accentColor.blue(), 145)
                      : QColor(255, 255, 255, 56)));
            p.setPen(QPen(ringPenColor, (selected || isCurrentVDesk) ? 2.0 : 1.2));
            p.drawEllipse(face);

            if (vdeskIdx >= 0 && vdeskIdx < 4) {
                QFont f = font();
                f.setPixelSize(qRound(face.width() * 0.44));
                f.setWeight(isCurrentVDesk ? QFont::Bold : QFont::DemiBold);
                p.setFont(f);
                p.setPen(selected ? QColor("#ffffff") : vdeskColor);
                p.drawText(face.translated(0, isCurrentVDesk ? -1.5 : 0.0), Qt::AlignCenter, vdeskGlyph);
                if (isCurrentVDesk) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(vdeskColor);
                    const qreal barW = face.width() * 0.32;
                    p.drawRoundedRect(QRectF(face.center().x() - barW * 0.5, face.bottom() - 6.5, barW, 2.4), 1.2, 1.2);
                }
            } else {
                const qreal iconDim = face.width() * 0.48;
                const QRectF iconRect(face.center().x() - iconDim * 0.5,
                                      face.center().y() - iconDim * 0.5,
                                      iconDim, iconDim);
                QColor ink = selected ? QColor("#ffffff") : (item.accentColor.isValid() ? item.accentColor : QColor("#f2f6fc"));
                if (!selected && !item.accentColor.isValid()) {
                    if (item.id == QLatin1String("crosshair")) ink = QColor("#8ad0ff");
                    else if (item.id == QLatin1String("launcher")) ink = QColor("#7cc8ff");
                    else if (item.id == QLatin1String("downloads")) ink = QColor("#86e0b8");
                    else if (item.id == QLatin1String("trash")) ink = QColor("#ff9dad");
                }
                FloatingIcons::draw(p, iconForAction(item.id), iconRect, ink);

                if (item.id == QLatin1String("tray_hub")) {
                    const SystemStatusSnapshot &stHub = m_owner->statusMonitor()->snapshot();
                    if (stHub.trayNeedsAttention || stHub.unreadNotifications > 0) {
                        drawRedBadgePill(p, QPointF(face.right() - 4.5, face.top() + 4.5),
                                         qMax(1, stHub.unreadNotifications), font());
                    }
                } else if (item.id == QLatin1String("notifications") && item.unreadCount > 0) {
                    drawRedBadgePill(p, QPointF(face.right() - 4.5, face.top() + 4.5),
                                     item.unreadCount, font());
                }
            }
        }

        p.restore();
    }

    // Floating callout pill ONLY when pointing/hovering/sliding onto a slot!
    const QString rawLabelText = activeLabelText();
    if (!rawLabelText.isEmpty()) {
        const QRectF pill = labelPillRectLocal();
        if (!pill.isEmpty()) {
            QFont f = font();
            f.setPointSize(12);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            const QFontMetrics fm(f);
            const QString elided = fm.elidedText(rawLabelText, Qt::ElideRight, qMax(80, int(pill.width()) - 24));
            const bool closeHover = (m_highlightedSlot >= 0 && m_highlightedSlot < m_slots.size() &&
                                     m_slots[m_highlightedSlot].id == QLatin1String("close_active"));
            p.setPen(QPen(closeHover ? QColor(255, 95, 87, 165) : QColor(255, 255, 255, 55), closeHover ? 1.2 : 1.0));
            p.setBrush(QColor(18, 22, 30, 232));
            p.drawRoundedRect(pill, 16, 16);
            p.setPen(QColor("#f5f8fd"));
            p.drawText(pill, Qt::AlignCenter, elided);
        }
    }
}

// ============================================================================
// FloatingBall — Xiaomi Mi 5 Edge Ring + Multi-Ring Dock + fooView Controller
// ============================================================================

const QStringList &FloatingBall::availableActions()
{
    static const QStringList ids{
        QStringLiteral("vdesk1"),
        QStringLiteral("vdesk2"),
        QStringLiteral("vdesk3"),
        QStringLiteral("vdesk4"),
        QStringLiteral("back"),
        QStringLiteral("overview"),
        QStringLiteral("crosshair"),
        QStringLiteral("clipboard"),
        QStringLiteral("deck"),
        QStringLiteral("paste"),
        QStringLiteral("copy"),
        QStringLiteral("record"),
        QStringLiteral("desktop"),
        QStringLiteral("minimize_all"),
        QStringLiteral("ime"),
        QStringLiteral("terminal"),
        QStringLiteral("browser"),
        QStringLiteral("home"),
        QStringLiteral("downloads"),
        QStringLiteral("trash"),
        QStringLiteral("launcher"),
        QStringLiteral("win_hub"),
        QStringLiteral("tray_hub"),
        QStringLiteral("sys_hub"),
        QStringLiteral("tools_hub"),
        QStringLiteral("close_active")
    };
    return ids;
}

QStringList FloatingBall::defaultOrbitActions()
{
#ifdef Q_OS_LINUX
    return {
        QStringLiteral("launcher"),
        QStringLiteral("sys_hub"),
        QStringLiteral("tray_hub"),
        QStringLiteral("overview"),
        QStringLiteral("tools_hub")
    };
#elif defined(Q_OS_MACOS)
    return {
        QStringLiteral("launcher"),
        QStringLiteral("sys_hub"),
        QStringLiteral("overview"),
        QStringLiteral("clipboard"),
        QStringLiteral("tools_hub")
    };
#else
    return {
        QStringLiteral("back"),
        QStringLiteral("overview"),
        QStringLiteral("crosshair"),
        QStringLiteral("clipboard"),
        QStringLiteral("deck")
    };
#endif
}

FloatingBall::FloatingBall(QWidget *parent,
                           QScreen *assignedScreen, FloatingBall *primaryPeer)
    : QWidget(parent),
      ui(new Ui::FloatingBall),
      m_assignedScreen(assignedScreen),
      m_primaryPeer(primaryPeer)
{
    ui->setupUi(this);
    setObjectName(m_primaryPeer ? QStringLiteral("floatingBallReplica") : QStringLiteral("floatingBallMain"));
    setProperty("floatingballSurface", true);
    if (!m_assignedScreen) m_assignedScreen = QGuiApplication::primaryScreen();
    if (m_assignedScreen)
        setProperty("floatingballAssignedScreen", QVariant::fromValue(m_assignedScreen.data()));

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::Tool | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("FloatingBall"));
    setToolTip(tr("短按展开多层扇形（含悬浮 Dock）· 按住内拉一划即选 / 拉出外环变准星 · 原地长按移动位置"));

    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
    m_geometryAnimation->setDuration(150);
    m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnimation = new QVariantAnimation(this);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_ballOpacity = v.toReal();
        update();
    });

    m_pickupAnimation = new QVariantAnimation(this);
    m_pickupAnimation->setDuration(240);
    m_pickupAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_pickupAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pickupPulse = v.toReal();
        update();
    });

    // Xiaomi Mi 5 stationary hold timer (380ms -> pick up and reposition ball)
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(380);
    connect(m_holdTimer, &QTimer::timeout, this, [this] {
        if (m_gestureMode != GesturePressed) return;
        closeFan(true);
        m_gestureMode = GestureMoveBall;
        m_dragOffset = m_pressGlobalPos - QPointF(pos());
        setCursor(Qt::ClosedHandCursor);
        m_pickupAnimation->stop();
        m_pickupAnimation->setStartValue(1.0);
        m_pickupAnimation->setEndValue(0.0);
        m_pickupAnimation->start();
        refreshBallOpacity();
        DesktopActions::showToast(QStringLiteral("已抓起悬浮球 · 拖动到顺手边缘松开"), geometry().center());
    });

    // fooView crosshair dwell timer (320ms steady -> arm live crop box!)
    m_dwellAnimation = new QVariantAnimation(this);
    m_dwellAnimation->setStartValue(0.0);
    m_dwellAnimation->setEndValue(1.0);
    m_dwellAnimation->setDuration(320);
    m_dwellAnimation->setEasingCurve(QEasingCurve::Linear);
    connect(m_dwellAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_dwellProgress = v.toReal();
        if (m_gestureMode == GestureCrosshairAim && m_fanOverlay) {
            m_fanOverlay->updateFinger(m_lastGlobalPos, -1, m_dwellProgress);
        }
    });
    connect(m_dwellAnimation, &QVariantAnimation::finished, this, [this] {
        if (m_gestureMode != GestureCrosshairAim) return;
        m_gestureMode = GestureCrosshairRect;
        m_crosshairAnchorGlobal = m_lastGlobalPos;
        if (m_fanOverlay) {
            m_fanOverlay->updateCrosshairRect(m_crosshairAnchorGlobal, m_lastGlobalPos);
        }
    });

    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(2200);
    connect(m_idleTimer, &QTimer::timeout, this, [this] {
        animateBallOpacity(m_idleOpacity, 420);
        if (m_autoHideEnabled && m_dockSide != 0 && !m_pointerInside &&
            m_gestureMode == GestureIdle && !m_orbitVisible && !m_menuOpen && !m_settingsOpen) {
            collapseToEdge();
        }
    });

    // Live 24H two-line clock refresh + transient IME flash timer
    m_clockTimer = new QTimer(this);
    m_clockTimer->setInterval(10000);
    connect(m_clockTimer, &QTimer::timeout, this, [this] {
        if (isVisible()) update();
    });
    m_clockTimer->start();

    m_imeFlashTimer = new QTimer(this);
    m_imeFlashTimer->setSingleShot(true);
    connect(m_imeFlashTimer, &QTimer::timeout, this, [this] {
        m_imeFlashLabel.clear();
        update();
    });

    // Live system mute/volume badge synchronization on the ball itself (primary only)
    if (isPrimaryBall()) {
        m_audioPollTimer = new QTimer(this);
        m_audioPollTimer->setInterval(6000);
        connect(m_audioPollTimer, &QTimer::timeout, this, [this] {
            if (isVisible()) refreshSystemAudioState();
        });
    }

    m_fanOverlay = new RadialFanOverlay(this);
    connect(m_fanOverlay, &RadialFanOverlay::slotMiddleClicked, this, [this](int slot) {
        const auto slotsData = m_fanOverlay->slotsData();
        if (slot < 0 || slot >= slotsData.size()) return;
        const RadialSlotItem item = slotsData[slot];
        if (item.kind == RadialSlotItem::NotificationSlot) {
            statusMonitor()->markNotificationsReadForApp(
                item.notificationItem.desktopEntry,
                item.notificationItem.appName,
                item.notificationItem.desktopEntry);
            statusMonitor()->markNotificationRead(item.notificationItem.seqId);
            DesktopActions::showToast(tr("✓ 已标为已读 · %1").arg(item.notificationItem.displayAppName()),
                                      anchorCenterGlobal().toPoint());
            m_fanOverlay->refreshLiveLayout();
            refreshAttentionBearings();
            return;
        }
        if (item.kind != RadialSlotItem::AppSlot) return;
        const auto out = dockModel()->quitApp(item.app);
        if (!out.message.isEmpty()) DesktopActions::showToast(out.message, anchorCenterGlobal().toPoint());
        m_fanOverlay->refreshLiveLayout();
        refreshAttentionBearings();
    });
    connect(m_fanOverlay, &RadialFanOverlay::slotTapped, this, [this](int slot) {
        const auto slotsData = m_fanOverlay->slotsData();
        if (slot < 0 || slot >= slotsData.size()) return;
        const RadialSlotItem item = slotsData[slot];
        if (item.kind == RadialSlotItem::BackSlot || item.kind == RadialSlotItem::CenterSlot) {
            m_fanOverlay->popDrill();
            return;
        }
        if (!item.drillKey.isEmpty()) {
            QString title = item.shortLabel;
            title.remove(QStringLiteral(" ›")).remove(QStringLiteral("›"));
            m_fanOverlay->drillInto(item.drillKey, title.trimmed(), item.accentColor, item.centerGlobal);
            return;
        }
        // Safe reversible toggles update in-place without closing the control ring.
        // Wi-Fi / Bluetooth / Mihomo open the dedicated SystemPopover instead so
        // swiping never accidentally drops active radios or peripherals.
        if (item.kind == RadialSlotItem::SystemSlot &&
            (item.id == QLatin1String("sys_dnd") ||
             item.id == QLatin1String("sys_nightlight") || item.id == QLatin1String("sys_power"))) {
            executeSlotItem(item);
            m_fanOverlay->refreshLiveLayout();
            return;
        }
        closeFan(false);
        executeSlotItem(item);
    });
    connect(m_fanOverlay, &RadialFanOverlay::slotContextMenuRequested, this, &FloatingBall::showSlotContextMenu);
    connect(m_fanOverlay, &RadialFanOverlay::ringCountStepRequested, this, [this](int delta) {
        const int next = qBound(kMinRingCount, m_ringCount + delta, kMaxRingCount);
        if (next != m_ringCount) {
            setRingCount(next);
            DesktopActions::showToast(QStringLiteral("外围悬浮层数：%1 层").arg(next), anchorCenterGlobal().toPoint());
        }
    });
    connect(m_fanOverlay, &RadialFanOverlay::dismissedByUser, this, [this] { closeFan(false); });

    if (m_primaryPeer) {
        syncSharedPreferencesFrom(m_primaryPeer);
        loadScreenPlacement();
    } else {
        loadPreferences();
        loadScreenPlacement();
    }
    // Size the sphere cleanly before FloatingWindow::prepare, then let adaptToScreen()
    // place it directly on its configured dock edge (never snap against the 400x400 .ui stub!).
    const int initDiam = qBound(kMinDiameter, qRound(m_preferredDiameter), kMaxDiameter);
    resize(initDiam, initDiam);
    updateBallMask();

    grabGesture(Qt::PinchGesture);
    FloatingWindow::prepare(this, false);

    m_currentScreen = m_assignedScreen ? m_assignedScreen.data() : screen();
    if (isPrimaryBall()) {
        for (QScreen *display : QGuiApplication::screens()) watchScreen(display);
        connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *display) {
            if (FloatingWindow::isValidScreen(display)) watchScreen(display);
            QTimer::singleShot(80, this, [this] { recoverVisibleAfterOutputChange(); });
        });
        connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen *removed) {
            if (m_assignedScreen == removed) {
                m_assignedScreen = nullptr;
            }
            QTimer::singleShot(100, this, [this] { recoverVisibleAfterOutputChange(); });
        });
        connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *) {
            QTimer::singleShot(80, this, [this] { recoverVisibleAfterOutputChange(); });
        });

        initTray();
        adaptToScreen();
        refreshBallOpacity();

        statusMonitor()->setEnabled(m_statusOrbMode);
        if (m_dockModeEnabled) dockModel();
        refreshAttentionBearings();
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
            refreshSystemAudioState();
            if (m_audioPollTimer) m_audioPollTimer->start();
        }
        syncScreenReplicas();
    } else {
        if (m_assignedScreen) watchScreen(m_assignedScreen);
        if (SystemStatusMonitor *mon = statusMonitor()) {
            connect(mon, &SystemStatusMonitor::statusChanged, this, [this](const SystemStatusSnapshot &) {
                refreshAttentionBearings();
                update();
                if (m_orbitVisible && m_fanOverlay) m_fanOverlay->update();
            });
            connect(mon, &SystemStatusMonitor::notificationsChanged, this, [this] {
                refreshAttentionBearings();
                update();
                if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
            });
            connect(mon, &SystemStatusMonitor::inputMethodActivity, this, [this](const QString &lbl) {
                triggerImeFlash(lbl);
            });
        }
        if (AppDockModel *dock = dockModel()) {
            connect(dock, &AppDockModel::appsChanged, this, [this] {
                refreshAttentionBearings();
                if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
            });
            connect(dock, &AppDockModel::activeFullscreenChanged, this, [this](bool fs) {
                m_fullscreenTucked = (m_autoTuckFullscreen && fs);
                if (m_fullscreenTucked) tuckToEdgeCapsule(false, true);
                else if (!m_manualTucked) expandFromEdge();
            });
        }
        adaptToScreen();
        refreshAttentionBearings();
        refreshBallOpacity();
    }
}

FloatingBall::~FloatingBall()
{
    if (isPrimaryBall()) {
        const auto replicas = m_screenReplicas.values();
        m_screenReplicas.clear();
        for (const auto &rep : replicas) {
            if (rep) delete rep.data();
        }
    }
    removeOutsideDismissMonitor();
    m_idleTimer->stop();
    m_holdTimer->stop();
    if (m_clockTimer) m_clockTimer->stop();
    if (m_imeFlashTimer) m_imeFlashTimer->stop();
    if (m_audioPollTimer) m_audioPollTimer->stop();
    m_dwellAnimation->stop();
    m_opacityAnimation->stop();
    if (m_orbitAnimation) m_orbitAnimation->stop();
    if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running)
        m_geometryAnimation->stop();

    delete m_fanOverlay;
    if (isPrimaryBall()) {
        delete m_clipboardBubble;
        delete m_quickDeck;
        delete m_systemPopover;
        delete m_appLauncher;
    }
    delete ui;
}

QPointF FloatingBall::anchorCenterGlobal() const
{
    if (m_edgeTucked && !m_expandedDockGeometry.isEmpty())
        return QRectF(m_expandedDockGeometry).center();
    return QRectF(geometry()).center();
}

QList<QRectF> FloatingBall::orbitSlotGlobalRects() const
{
    return m_fanOverlay ? m_fanOverlay->slotGlobalRects() : QList<QRectF>{};
}

QList<QRectF> FloatingBall::allOrbitSlotGlobalRects() const
{
    return m_fanOverlay ? m_fanOverlay->allSlotGlobalRects() : QList<QRectF>{};
}

QString FloatingBall::actionLabel(const QString &id) const
{
    if (id.startsWith(QLatin1String("vdesk")) && id.size() == 6) {
        const int idx = id.at(5).digitValue() - 1;
        static const QString defaults[4]{
            QStringLiteral("桌面 1 · 広"), QStringLiteral("桌面 2 · 栞"),
            QStringLiteral("桌面 3 · 発"), QStringLiteral("桌面 4 · 伝")
        };
        if (idx >= 0 && idx < 4) {
            if (m_statusMonitor && idx < m_statusMonitor->snapshot().virtualDesktops.size()) {
                const auto &vd = m_statusMonitor->snapshot().virtualDesktops[idx];
                return tr("桌面 %1 · %2").arg(idx + 1).arg(vd.name) + (vd.current ? tr("（当前）") : QString());
            }
            return defaults[idx];
        }
    }
    if (id == QLatin1String("back")) return tr("全局返回");
    if (id == QLatin1String("forward")) return tr("全局前进");
    if (id == QLatin1String("overview")) return tr("多任务全览");
    if (id == QLatin1String("desktop")) return tr("显示桌面");
    if (id == QLatin1String("minimize_all")) return tr("全收桌面（隐藏所有窗口）");
#ifdef Q_OS_LINUX
    if (id == QLatin1String("crosshair") || id == QLatin1String("capture")) return tr("截屏 / 录屏");
#else
    if (id == QLatin1String("crosshair") || id == QLatin1String("capture")) return tr("区域截图");
#endif
    if (id == QLatin1String("clipboard")) return tr("触控剪贴板");
    if (id == QLatin1String("deck")) return tr("触控甲板");
    if (id == QLatin1String("paste")) return tr("一键粘贴");
    if (id == QLatin1String("copy")) return tr("一键复制");
    if (id == QLatin1String("record")) return tr("屏幕录制");
    if (id == QLatin1String("ime")) return tr("切换输入法");
    if (id == QLatin1String("terminal")) return tr("打开终端");
    if (id == QLatin1String("browser")) return tr("打开浏览器");
    if (id == QLatin1String("home")) return tr("打开主目录");
    if (id == QLatin1String("downloads")) return tr("下载目录");
    if (id == QLatin1String("trash")) return tr("废纸篓");
    if (id == QLatin1String("launcher")) return tr("开始菜单 · 所有应用");
    if (id == QLatin1String("win_hub")) return tr("当前打开的窗口");
    if (id == QLatin1String("tray_hub")) {
        const int n = m_statusMonitor ? m_statusMonitor->trayItems().size()
            : (m_primaryPeer ? m_primaryPeer->statusMonitor()->trayItems().size() : 0);
        return n > 0 ? tr("托盘与通知 (%1)").arg(n) : tr("托盘与通知");
    }
    if (id == QLatin1String("notifications")) return tr("通知中心");
    if (id == QLatin1String("sys_hub")) return tr("系统控制中心");
    if (id == QLatin1String("tools_hub")) return tr("常用触控工具");
    if (id == QLatin1String("close_active")) return tr("关闭当前窗口");
    return id;
}

void FloatingBall::loadPreferences()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    const int layoutVersion = s.value("ball/ringLayoutVersion", 1).toInt();

    m_orbitActions = s.value("ball/quickFanActions", defaultOrbitActions()).toStringList();
    if (layoutVersion < 4) {
        // v4 migration: Ring 0 holds 5 fixed system hubs; outer rings auto-expand all open windows;
        // primary circular 24H clock ball stays visible by default (autoHide = false).
        m_orbitActions = defaultOrbitActions();
        s.setValue("ball/quickFanActions", m_orbitActions);
        s.setValue("ball/statusOrbMode", true);
        if (!s.contains("ball/autoHideV4Migrated")) {
            s.setValue("ball/autoHide", false);
            s.setValue("ball/autoHideV4Migrated", true);
        }
        if (s.value("ball/idleOpacity", 0.38).toReal() < 0.65) {
            s.setValue("ball/idleOpacity", 0.82);
        }
        s.setValue("ball/ringLayoutVersion", 4);
    }
    if (m_orbitActions.size() != kOrbitSlotCount ||
        QSet<QString>(m_orbitActions.begin(), m_orbitActions.end()).size() != kOrbitSlotCount) {
        m_orbitActions = defaultOrbitActions();
    }
    for (const QString &id : m_orbitActions) {
        if (!availableActions().contains(id)) {
            m_orbitActions = defaultOrbitActions();
            break;
        }
    }

    int savedRings = s.value("ball/ringCount", kDefaultRingCount).toInt();
    m_ringCount = qBound(kMinRingCount, savedRings, kMaxRingCount);
    m_dockModeEnabled = s.value("ball/dockModeEnabled", true).toBool();
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    const bool defaultStatusOrb = true;
#else
    const bool defaultStatusOrb = false;
#endif
    m_statusOrbMode = s.value("ball/statusOrbMode", defaultStatusOrb).toBool();
    m_multiScreenEnabled = s.value("ball/multiScreenEnabled", true).toBool();
    m_preferredDiameter = qBound(qreal(kMinDiameter), s.value("ball/diameter", m_statusOrbMode ? 60.0 : 56.0).toReal(), qreal(kMaxDiameter));
    m_idleOpacity = qBound(0.15, s.value("ball/idleOpacity", 0.82).toReal(), 1.0);
    m_activeOpacity = qBound(m_idleOpacity, s.value("ball/activeOpacity", 0.94).toReal(), 1.0);
    m_snapEnabled = s.value("ball/snapToEdge", true).toBool();
    m_autoHideEnabled = s.value("ball/autoHide", false).toBool();
    m_autoTuckFullscreen = s.value("ball/autoTuckFullscreen", true).toBool();
    m_crosshairPullEnabled = s.value("ball/crosshairPull", true).toBool();
    m_dockSide = s.value("ball/dockSide", DockRight).toInt();
    if (m_dockSide != DockFree && m_dockSide != DockLeft && m_dockSide != DockRight &&
        m_dockSide != DockTop && m_dockSide != DockBottom)
        m_dockSide = DockRight;
    m_verticalFraction = qBound(0.02, s.value("ball/verticalFraction", 0.58).toReal(), 0.98);
    m_ballOpacity = m_idleOpacity;
}

void FloatingBall::savePreferences()
{
    if (m_primaryPeer) {
        saveScreenPlacement();
        return;
    }
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue("ball/quickFanActions", m_orbitActions);
    s.setValue("ball/ringCount", m_ringCount);
    s.setValue("ball/ringLayoutVersion", 4);
    s.setValue("ball/dockModeEnabled", m_dockModeEnabled);
    s.setValue("ball/statusOrbMode", m_statusOrbMode);
    s.setValue("ball/multiScreenEnabled", m_multiScreenEnabled);
    s.setValue("ball/diameter", m_preferredDiameter);
    s.setValue("ball/idleOpacity", m_idleOpacity);
    s.setValue("ball/activeOpacity", m_activeOpacity);
    s.setValue("ball/snapToEdge", m_snapEnabled);
    s.setValue("ball/autoHide", m_autoHideEnabled);
    s.setValue("ball/autoTuckFullscreen", m_autoTuckFullscreen);
    s.setValue("ball/crosshairPull", m_crosshairPullEnabled);
    s.setValue("ball/dockSide", m_dockSide);
    s.setValue("ball/verticalFraction", m_verticalFraction);
    saveScreenPlacement();
}

void FloatingBall::loadScreenPlacement()
{
    QScreen *scr = m_assignedScreen ? m_assignedScreen.data() : QGuiApplication::primaryScreen();
    const QString key = screenStorageKey(scr);
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    const QString prefix = QStringLiteral("screens/") + key + QLatin1Char('/');
    if (s.contains(prefix + QStringLiteral("dockSide"))) {
        int side = s.value(prefix + QStringLiteral("dockSide"), m_dockSide).toInt();
        if (side == DockFree || side == DockLeft || side == DockRight || side == DockTop || side == DockBottom)
            m_dockSide = side;
    }
    if (s.contains(prefix + QStringLiteral("verticalFraction"))) {
        m_verticalFraction = qBound(0.02, s.value(prefix + QStringLiteral("verticalFraction"), m_verticalFraction).toReal(), 0.98);
    }
    if (s.contains(prefix + QStringLiteral("diameter"))) {
        m_preferredDiameter = qBound(qreal(kMinDiameter), s.value(prefix + QStringLiteral("diameter"), m_preferredDiameter).toReal(), qreal(kMaxDiameter));
    }
    m_hiddenOnThisScreen = s.value(prefix + QStringLiteral("hidden"), false).toBool();
}

void FloatingBall::saveScreenPlacement() const
{
    QScreen *scr = m_assignedScreen ? m_assignedScreen.data() : m_currentScreen.data();
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QString key = screenStorageKey(scr);
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    const QString prefix = QStringLiteral("screens/") + key + QLatin1Char('/');
    s.setValue(prefix + QStringLiteral("dockSide"), m_dockSide);
    s.setValue(prefix + QStringLiteral("verticalFraction"), m_verticalFraction);
    s.setValue(prefix + QStringLiteral("diameter"), m_preferredDiameter);
    s.setValue(prefix + QStringLiteral("hidden"), m_hiddenOnThisScreen);
}

void FloatingBall::syncSharedPreferencesFrom(const FloatingBall *source)
{
    if (!source || source == this) return;
    m_orbitActions = source->m_orbitActions;
    m_ringCount = source->m_ringCount;
    m_dockModeEnabled = source->m_dockModeEnabled;
    m_statusOrbMode = source->m_statusOrbMode;
    m_multiScreenEnabled = source->m_multiScreenEnabled;
    m_idleOpacity = source->m_idleOpacity;
    m_activeOpacity = source->m_activeOpacity;
    m_snapEnabled = source->m_snapEnabled;
    m_autoHideEnabled = source->m_autoHideEnabled;
    m_autoTuckFullscreen = source->m_autoTuckFullscreen;
    m_crosshairPullEnabled = source->m_crosshairPullEnabled;
    m_systemMuted = source->m_systemMuted;
    m_systemVolume = source->m_systemVolume;
    if (!m_autoHideEnabled && !m_manualTucked && !m_fullscreenTucked) expandFromEdge();
    if (!m_snapEnabled) m_dockSide = DockFree;
    else if (m_dockSide == DockFree) m_dockSide = DockRight;
    refreshAttentionBearings();
    update();
    refreshBallOpacity();
    if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
}

void FloatingBall::broadcastSharedPreferences()
{
    if (m_primaryPeer) {
        m_primaryPeer->broadcastSharedPreferences();
        return;
    }
    for (auto it = m_screenReplicas.begin(); it != m_screenReplicas.end(); ++it) {
        if (it.value()) it.value()->syncSharedPreferencesFrom(this);
    }
}

QList<FloatingBall *> FloatingBall::replicaBalls() const
{
    QList<FloatingBall *> list;
    for (auto it = m_screenReplicas.constBegin(); it != m_screenReplicas.constEnd(); ++it) {
        if (it.value()) list.append(it.value().data());
    }
    return list;
}

void FloatingBall::syncScreenReplicas()
{
    if (!isPrimaryBall()) return;

    const QList<QScreen *> allScreens = QGuiApplication::screens();
    QList<QScreen *> screens;
    for (QScreen *s : allScreens) {
        if (FloatingWindow::isValidScreen(s)) screens.append(s);
    }
    if (screens.isEmpty()) return;
    if (!m_assignedScreen || !screens.contains(m_assignedScreen)) {
        QScreen *prim = QGuiApplication::primaryScreen();
        m_assignedScreen = FloatingWindow::isValidScreen(prim) ? prim : screens.first();
        if (m_assignedScreen)
            setProperty("floatingballAssignedScreen", QVariant::fromValue(m_assignedScreen.data()));
    }

    QSet<QString> keepKeys;
    if (m_multiScreenEnabled && screens.size() > 1) {
        for (QScreen *scr : screens) {
            if (!scr || scr == m_assignedScreen) continue;
            const QString key = screenStorageKey(scr);
            keepKeys.insert(key);
            QPointer<FloatingBall> existing = m_screenReplicas.value(key);
            if (!existing) {
                auto *replica = new FloatingBall(nullptr, scr, this);
                m_screenReplicas.insert(key, replica);
                if (!m_userHiddenAll && !replica->m_hiddenOnThisScreen && isVisible()) {
                    replica->show();
                }
            } else {
                existing->syncSharedPreferencesFrom(this);
                if (existing->m_assignedScreen != scr) {
                    existing->m_assignedScreen = scr;
                    existing->setProperty("floatingballAssignedScreen", QVariant::fromValue(scr));
                    existing->adaptToScreen();
                }
                if (!m_userHiddenAll && !existing->m_hiddenOnThisScreen && isVisible() && !existing->isVisible()) {
                    existing->show();
                }
            }
        }
    }

    const QStringList keys = m_screenReplicas.keys();
    for (const QString &key : keys) {
        if (!keepKeys.contains(key)) {
            QPointer<FloatingBall> rep = m_screenReplicas.take(key);
            if (rep) delete rep.data();
        }
    }
}

void FloatingBall::closeOtherFans(FloatingBall *except)
{
    if (m_primaryPeer) {
        m_primaryPeer->closeOtherFans(except);
        return;
    }
    if (this != except && m_orbitVisible) closeFan(true);
    for (auto it = m_screenReplicas.begin(); it != m_screenReplicas.end(); ++it) {
        if (it.value() && it.value().data() != except && it.value()->isOrbitVisible())
            it.value()->closeFan(true);
    }
}

void FloatingBall::setMultiScreenEnabled(bool enabled)
{
    if (m_primaryPeer) {
        m_primaryPeer->setMultiScreenEnabled(enabled);
        return;
    }
    if (m_multiScreenEnabled == enabled) return;
    m_multiScreenEnabled = enabled;
    savePreferences();
    syncScreenReplicas();
    broadcastSharedPreferences();
}

void FloatingBall::setHiddenOnThisScreen(bool hidden)
{
    m_hiddenOnThisScreen = hidden;
    saveScreenPlacement();
    if (hidden) {
        closeFan(true);
        hide();
    } else if (!m_userHiddenAll) {
        expandFromEdge();
        show();
        raise();
        refreshBallOpacity();
    }
}

void FloatingBall::restoreAllScreenBalls()
{
    if (m_primaryPeer) {
        m_primaryPeer->restoreAllScreenBalls();
        return;
    }
    m_userHiddenAll = false;
    setHiddenOnThisScreen(false);
    syncScreenReplicas();
    for (auto it = m_screenReplicas.begin(); it != m_screenReplicas.end(); ++it) {
        if (it.value()) it.value()->setHiddenOnThisScreen(false);
    }
}

bool FloatingBall::setOrbitActions(const QStringList &actions)
{
    if (m_primaryPeer) return m_primaryPeer->setOrbitActions(actions);
    if (actions.size() != kOrbitSlotCount ||
        QSet<QString>(actions.begin(), actions.end()).size() != kOrbitSlotCount)
        return false;
    for (const QString &id : actions) {
        if (!availableActions().contains(id)) return false;
    }
    const bool wasOpen = m_orbitVisible;
    closeFan(true);
    m_orbitActions = actions;
    savePreferences();
    broadcastSharedPreferences();
    if (wasOpen) openTapFan();
    return true;
}

void FloatingBall::setRingCount(int rings)
{
    if (m_primaryPeer) {
        m_primaryPeer->setRingCount(rings);
        return;
    }
    const int clamped = qBound(kMinRingCount, rings, kMaxRingCount);
    if (m_ringCount == clamped) return;
    m_ringCount = clamped;
    savePreferences();
    broadcastSharedPreferences();
    if (m_orbitVisible && m_fanOverlay) {
        m_fanOverlay->refreshLiveLayout();
    }
}

void FloatingBall::setDockModeEnabled(bool enabled)
{
    if (m_primaryPeer) {
        m_primaryPeer->setDockModeEnabled(enabled);
        return;
    }
    if (m_dockModeEnabled == enabled) return;
    m_dockModeEnabled = enabled;
    savePreferences();
    broadcastSharedPreferences();
    if (m_orbitVisible && m_fanOverlay) {
        m_fanOverlay->refreshLiveLayout();
    }
}

void FloatingBall::setStatusOrbMode(bool enabled)
{
    if (m_primaryPeer) {
        m_primaryPeer->setStatusOrbMode(enabled);
        return;
    }
    if (m_statusOrbMode == enabled) return;
    m_statusOrbMode = enabled;
    if (m_statusOrbMode && !m_manualTucked && !m_fullscreenTucked) expandFromEdge();
    statusMonitor()->setEnabled(m_statusOrbMode);
    refreshAttentionBearings();
    savePreferences();
    broadcastSharedPreferences();
    update();
    if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
}

void FloatingBall::setAutoHideEnabled(bool enabled)
{
    if (m_primaryPeer) {
        m_primaryPeer->setAutoHideEnabled(enabled);
        return;
    }
    if (m_autoHideEnabled == enabled) return;
    m_autoHideEnabled = enabled;
    if (!m_autoHideEnabled && !m_manualTucked && !m_fullscreenTucked) {
        expandFromEdge();
    }
    savePreferences();
    broadcastSharedPreferences();
    refreshBallOpacity();
}

void FloatingBall::setAutoTuckFullscreen(bool enabled)
{
    if (m_primaryPeer) {
        m_primaryPeer->setAutoTuckFullscreen(enabled);
        return;
    }
    if (m_autoTuckFullscreen == enabled) return;
    m_autoTuckFullscreen = enabled;
    m_fullscreenTucked = (m_autoTuckFullscreen && dockModel()->activeWindowFullscreen());
    if (m_fullscreenTucked) tuckToEdgeCapsule(false, true);
    else if (!m_manualTucked && !m_autoHideEnabled) expandFromEdge();
    savePreferences();
    broadcastSharedPreferences();
}

void FloatingBall::triggerImeFlash(const QString &label, int durationMs)
{
    QString clean = label.trimmed();
    if (clean.isEmpty() && m_statusMonitor) {
        clean = m_statusMonitor->snapshot().inputLabel;
    }
    if (clean.isEmpty()) return;
    m_imeFlashLabel = clean;
    if (m_imeFlashTimer) m_imeFlashTimer->start(qMax(300, durationMs));
    update();
    if (isPrimaryBall()) {
        for (FloatingBall *rep : replicaBalls()) {
            rep->m_imeFlashLabel = clean;
            if (rep->m_imeFlashTimer) rep->m_imeFlashTimer->start(qMax(300, durationMs));
            rep->update();
        }
    }
}

QString FloatingBall::activeImeFlashLabel() const
{
    if (m_imeFlashTimer && m_imeFlashTimer->isActive() && !m_imeFlashLabel.isEmpty())
        return m_imeFlashLabel;
    return {};
}

void FloatingBall::refreshAttentionBearings()
{
    if (!m_fanOverlay) return;
    const QList<qreal> next = m_fanOverlay->computeAttentionAnglesRad();
    if (m_attentionAnglesRad != next) {
        m_attentionAnglesRad = next;
        update();
    }
}

SystemStatusMonitor *FloatingBall::statusMonitor()
{
    if (m_primaryPeer) return m_primaryPeer->statusMonitor();
    if (!m_statusMonitor) {
        m_statusMonitor = new SystemStatusMonitor(this);
        connect(m_statusMonitor, &SystemStatusMonitor::statusChanged, this, [this](const SystemStatusSnapshot &st) {
            if (m_statusOrbMode) {
                QStringList tips;
                tips.append(QTime::currentTime().toString(QStringLiteral("HH:mm")));
                if (st.batteryPresent)
                    tips.append(tr("电量 %1%%2").arg(st.batteryPercent).arg(st.batteryPluggedIn ? tr("（充电中）") : QString()));
                if (!st.networkText.isEmpty()) tips.append(st.networkText);
                if (st.brightnessAvailable) tips.append(tr("亮度 %1%").arg(st.brightnessPercent));
                if (st.volumeAvailable) tips.append(st.volumeMuted ? tr("音量 静音") : tr("音量 %1%").arg(st.volumePercent));
                if (st.inputPresent && !st.inputDescription.isEmpty()) tips.append(tr("输入法 %1").arg(st.inputDescription));
                if (st.micActive) tips.append(tr("麦克风：%1").arg(st.micUsers.join(QStringLiteral("、"))));
                if (st.cameraActive) tips.append(tr("摄像头：%1").arg(st.cameraUsers.join(QStringLiteral("、"))));
                if (st.hasNotificationDot())
                    tips.append(st.lastNotificationSummary.isEmpty() ? tr("有未读通知") : st.lastNotificationSummary);
                if (!tips.isEmpty()) {
                    const QString tipStr = tips.join(QStringLiteral(" · "));
                    setToolTip(tipStr);
                    for (FloatingBall *rep : replicaBalls()) rep->setToolTip(tipStr);
                }
            }
            refreshAttentionBearings();
            update();
            if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
        });
        connect(m_statusMonitor, &SystemStatusMonitor::notificationReceived, this,
                [this](const QString &appName, const QString &desktopEntry, const QString &) {
            dockModel()->notifyAppAttention(appName, desktopEntry);
            refreshAttentionBearings();
            if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
        });
        connect(m_statusMonitor, &SystemStatusMonitor::notificationsChanged, this, [this] {
            refreshAttentionBearings();
            update();
            if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
            for (FloatingBall *rep : replicaBalls()) {
                rep->refreshAttentionBearings();
                rep->update();
                if (rep->isOrbitVisible() && rep->fanOverlay()) rep->fanOverlay()->refreshLiveLayout();
            }
        });
        connect(m_statusMonitor, &SystemStatusMonitor::inputMethodActivity, this,
                [this](const QString &lbl) {
            triggerImeFlash(lbl);
        });
        connect(m_statusMonitor, &SystemStatusMonitor::trayItemsChanged, this, [this] {
            refreshAttentionBearings();
            if (m_orbitVisible && m_fanOverlay && m_fanOverlay->isDrilled())
                m_fanOverlay->refreshLiveLayout();
            for (FloatingBall *rep : replicaBalls()) {
                rep->refreshAttentionBearings();
                if (rep->isOrbitVisible() && rep->fanOverlay() && rep->fanOverlay()->isDrilled())
                    rep->fanOverlay()->refreshLiveLayout();
            }
        });
    }
    return m_statusMonitor;
}

void FloatingBall::clearNotificationDot()
{
    if (m_primaryPeer) {
        m_primaryPeer->clearNotificationDot();
        return;
    }
    if (m_statusMonitor) m_statusMonitor->clearNotifications();
    if (m_dockModel) m_dockModel->clearAllAttention();
    refreshAttentionBearings();
    update();
    for (FloatingBall *rep : replicaBalls()) {
        rep->refreshAttentionBearings();
        rep->update();
    }
}

void FloatingBall::setSystemAudioState(int percent, bool muted)
{
    if (m_primaryPeer) {
        m_primaryPeer->setSystemAudioState(percent, muted);
        return;
    }
    const int clamped = qBound(0, percent, 100);
    if (m_statusMonitor) m_statusMonitor->updateAudio(clamped, muted, true);
    if (m_systemVolume == clamped && m_systemMuted == muted) return;
    m_systemVolume = clamped;
    m_systemMuted = muted;
    if (!m_statusOrbMode) {
        setToolTip(m_systemMuted
            ? tr("系统已静音 · 短按展开多层扇形（含悬浮 Dock）· 原地长按移位")
            : tr("短按展开多层扇形（含悬浮 Dock）· 按住内拉一划即选 / 拉出外环变准星 · 原地长按移位"));
    }
    update();
    for (FloatingBall *rep : replicaBalls()) {
        rep->m_systemVolume = clamped;
        rep->m_systemMuted = muted;
        rep->update();
    }
}

void FloatingBall::refreshSystemAudioState()
{
    if (m_primaryPeer) {
        m_primaryPeer->refreshSystemAudioState();
        return;
    }
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    QPointer<FloatingBall> self(this);
    DesktopActions::queryVolume([self](int vol, bool muted, bool supported) {
        if (!self || !supported) return;
        self->setSystemAudioState(vol, muted);
    });
}

AppDockModel *FloatingBall::dockModel()
{
    if (m_primaryPeer) return m_primaryPeer->dockModel();
    if (!m_dockModel) {
        m_dockModel = new AppDockModel(this);
        connect(m_dockModel, &AppDockModel::appsChanged, this, [this] {
            refreshAttentionBearings();
            if (m_orbitVisible && m_fanOverlay) m_fanOverlay->refreshLiveLayout();
        });
        connect(m_dockModel, &AppDockModel::windowActivated, this,
                [this](const QString &appKey, const QString &appName, const QString &desktopEntry) {
            if (m_statusMonitor) {
                m_statusMonitor->markNotificationsReadForApp(appKey, appName, desktopEntry);
            }
            refreshAttentionBearings();
        });
        connect(m_dockModel, &AppDockModel::activeFullscreenChanged, this, [this](bool fs) {
            m_fullscreenTucked = (m_autoTuckFullscreen && fs);
            if (m_fullscreenTucked) tuckToEdgeCapsule(false, true);
            else if (!m_manualTucked && !m_autoHideEnabled) expandFromEdge();
        });
    }
    return m_dockModel;
}

ClipboardBubble *FloatingBall::clipboardBubble()
{
    if (m_primaryPeer) return m_primaryPeer->clipboardBubble();
    if (!m_clipboardBubble)
        m_clipboardBubble = new ClipboardBubble(nullptr);
    return m_clipboardBubble;
}

QuickDeck *FloatingBall::quickDeck()
{
    if (m_primaryPeer) return m_primaryPeer->quickDeck();
    if (!m_quickDeck) {
        m_quickDeck = new QuickDeck(nullptr);
        connect(m_quickDeck, &QuickDeck::captureRequested, this, &FloatingBall::captureRegion);
        connect(m_quickDeck, &QuickDeck::recordRequested, this, &FloatingBall::recordScreen);
        connect(m_quickDeck, &QuickDeck::settingsRequested, this, &FloatingBall::showSettings);
        connect(m_quickDeck, &QuickDeck::audioStateChanged, this, &FloatingBall::setSystemAudioState);
        connect(m_quickDeck, &QuickDeck::brightnessStateChanged, this, [this](int pct) {
            if (m_statusMonitor) m_statusMonitor->updateBrightness(pct, true);
        });
    }
    return m_quickDeck;
}

SystemPopover *FloatingBall::systemPopover()
{
    if (m_primaryPeer) return m_primaryPeer->systemPopover();
    if (!m_systemPopover) {
        m_systemPopover = new SystemPopover(statusMonitor(), nullptr);
        connect(m_systemPopover, &SystemPopover::audioStateChanged, this, &FloatingBall::setSystemAudioState);
        connect(m_systemPopover, &SystemPopover::brightnessStateChanged, this, [this](int pct) {
            if (m_statusMonitor) m_statusMonitor->updateBrightness(pct, true);
        });
        connect(m_systemPopover, &SystemPopover::quickDeckRequested, this, &FloatingBall::toggleQuickDeck);
        connect(m_systemPopover, &SystemPopover::notificationActivated, this, &FloatingBall::openNotificationTarget);
    }
    return m_systemPopover;
}

void FloatingBall::openSystemPopover(int tab)
{
    if (m_quickDeck && m_quickDeck->isVisible()) m_quickDeck->hide();
    if (m_clipboardBubble && m_clipboardBubble->isVisible()) m_clipboardBubble->hide();
    if (m_appLauncher && m_appLauncher->isVisible()) m_appLauncher->hide();
    const int clamped = qBound(0, tab, static_cast<int>(SystemPopover::TabNotifications));
    systemPopover()->revealTab(static_cast<SystemPopover::Tab>(clamped), anchorCenterGlobal().toPoint());
}

AppLauncherPopover *FloatingBall::appLauncher()
{
    if (m_primaryPeer) return m_primaryPeer->appLauncher();
    if (!m_appLauncher) {
        m_appLauncher = new AppLauncherPopover(dockModel(), nullptr);
    }
    return m_appLauncher;
}

void FloatingBall::toggleAppLauncher()
{
    if (m_quickDeck && m_quickDeck->isVisible()) m_quickDeck->hide();
    if (m_clipboardBubble && m_clipboardBubble->isVisible()) m_clipboardBubble->hide();
    if (m_systemPopover && m_systemPopover->isVisible()) m_systemPopover->hide();
    appLauncher()->reveal(anchorCenterGlobal().toPoint(), true);
}

void FloatingBall::openNotificationTarget(const NotificationItem &item)
{
    const QPoint anchor = anchorCenterGlobal().toPoint();
    auto *mon = statusMonitor();
    if (mon) {
        mon->markNotificationRead(item.seqId);
        mon->markNotificationsReadForApp(item.desktopEntry, item.appName, item.desktopEntry);
    }
    refreshAttentionBearings();

    // 1. Prefer activating an open window or installed app matching the notification
    DesktopActions::Outcome out;
    if (dockModel()->activateOrLaunchByHint(item.appName, item.desktopEntry, &out)) {
        if (!out.message.isEmpty()) DesktopActions::showToast(out.message, anchor);
        return;
    }

    // 2. Otherwise activate a matching minimized-to-tray StatusNotifierItem (e.g. Telegram / Motrix)
    if (mon) {
        for (const TrayItemInfo &ti : mon->trayItems()) {
            if (SystemStatusMonitor::notificationMatchesApp(item, ti.id, ti.title)) {
                mon->activateTrayItem(ti, anchor, this);
                DesktopActions::showToast(tr("✓ 已唤起 %1").arg(ti.title), anchor);
                return;
            }
        }
    }

    // 3. Fallback (e.g. system/daemon alert with no window): open Notification Center directly!
    openSystemPopover(SystemPopover::TabNotifications);
}

void FloatingBall::executeSlotItem(const RadialSlotItem &item)
{
    const QPoint anchor = anchorCenterGlobal().toPoint();
    if (item.kind == RadialSlotItem::AppSlot) {
        dockModel()->clearAttentionForApp(item.app);
        statusMonitor()->markNotificationsReadForApp(item.app.appKey, item.app.name, item.app.id);
        refreshAttentionBearings();
        const auto outcome = dockModel()->toggleOrActivate(item.app);
        if (!outcome.message.isEmpty())
            DesktopActions::showToast(outcome.message, anchor);
        return;
    }
    if (item.kind == RadialSlotItem::NotificationSlot) {
        openNotificationTarget(item.notificationItem);
        return;
    }
    if (item.kind == RadialSlotItem::TraySlot) {
        if (item.id == QLatin1String("tray:empty")) return;
        statusMonitor()->markNotificationsReadForApp(item.trayItem.id, item.trayItem.title);
        refreshAttentionBearings();
        QWidget *surfaceAnchor = (m_fanOverlay && m_fanOverlay->isVisible() &&
                                  m_fanOverlay->overlayMode() == RadialFanOverlay::TapFanMode)
            ? static_cast<QWidget *>(m_fanOverlay)
            : static_cast<QWidget *>(this);
        statusMonitor()->activateTrayItem(item.trayItem, item.centerGlobal.toPoint(), surfaceAnchor);
        DesktopActions::showToast(tr("✓ %1").arg(item.trayItem.title), anchor);
        return;
    }
    if (item.kind == RadialSlotItem::TrayMenuSlot) {
        statusMonitor()->triggerTrayMenuEntry(item.trayItem, item.trayMenuEntry.id);
        DesktopActions::showToast(tr("✓ %1 · %2").arg(item.trayItem.title, item.trayMenuEntry.label), anchor);
        return;
    }
    if (item.kind == RadialSlotItem::SystemSlot) {
        auto *mon = statusMonitor();
        if (item.id == QLatin1String("sys_wifi")) {
            openSystemPopover(SystemPopover::TabWifi);
        } else if (item.id == QLatin1String("sys_bluetooth")) {
            openSystemPopover(SystemPopover::TabBluetooth);
        } else if (item.id == QLatin1String("sys_mihomo")) {
            openSystemPopover(SystemPopover::TabMihomo);
        } else if (item.id == QLatin1String("sys_dnd")) {
            DesktopActions::showToast(mon->toggleDoNotDisturb(), anchor);
        } else if (item.id == QLatin1String("sys_nightlight")) {
            DesktopActions::showToast(mon->toggleNightLight(), anchor);
        } else if (item.id == QLatin1String("sys_power")) {
            DesktopActions::showToast(mon->cyclePowerProfile(), anchor);
        } else if (item.id == QLatin1String("sys_display")) {
            mon->openKdeModule(QStringLiteral("kcm_kscreen"));
        } else if (item.id == QLatin1String("sys_audio")) {
            toggleQuickDeck();
        } else if (item.id == QLatin1String("sys_lock")) {
            mon->lockScreen();
        } else if (item.id == QLatin1String("sys_sleep")) {
            mon->openPowerMenu();
        } else if (item.id == QLatin1String("sys_settings")) {
            mon->openKdeModule(QString());
        }
        return;
    }
    if (!item.id.isEmpty()) executeAction(item.id);
}

void FloatingBall::showSlotContextMenu(int slotIndex, const QPoint &globalPos)
{
    if (!m_fanOverlay) return;
    const auto slotsData = m_fanOverlay->slotsData();
    if (slotIndex < 0 || slotIndex >= slotsData.size()) return;
    const RadialSlotItem item = slotsData[slotIndex];

    if (item.kind == RadialSlotItem::TraySlot) {
        if (item.id != QLatin1String("tray:empty")) {
            statusMonitor()->openTrayItemMenu(item.trayItem, globalPos, this);
        }
        return;
    }
    if (item.kind == RadialSlotItem::SystemSlot) {
        auto *mon = statusMonitor();
        closeFan(true);
        if (item.id == QLatin1String("sys_wifi")) {
            mon->openKdeModule(QStringLiteral("kcm_networkmanagement"));
        } else if (item.id == QLatin1String("sys_bluetooth")) {
            mon->openKdeModule(QStringLiteral("kcm_bluetooth"));
        } else if (item.id == QLatin1String("sys_mihomo")) {
            QSettings s;
            QString base = s.value(QStringLiteral("mihomo/url"), QStringLiteral("http://127.0.0.1:9090")).toString().trimmed();
            while (base.endsWith(QLatin1Char('/'))) base.chop(1);
            QDesktopServices::openUrl(QUrl(base + QStringLiteral("/ui/")));
        } else if (item.id == QLatin1String("sys_dnd")) {
            mon->openKdeModule(QStringLiteral("kcm_notifications"));
        } else if (item.id == QLatin1String("sys_power")) {
            mon->openKdeModule(QStringLiteral("kcm_powerdevilprofilesconfig"));
        } else if (item.id == QLatin1String("sys_audio")) {
            mon->openKdeModule(QStringLiteral("kcm_pulseaudio"));
        } else if (item.id == QLatin1String("sys_nightlight")) {
            mon->openKdeModule(QStringLiteral("kcm_nightlight"));
        } else if (item.id == QLatin1String("sys_display")) {
            mon->openKdeModule(QStringLiteral("kcm_kscreen"));
        } else {
            mon->openKdeModule(QString());
        }
        return;
    }
    if (item.id == QLatin1String("launcher")) {
        closeFan(true);
        statusMonitor()->openKRunner();
        return;
    }
    if (item.id == QLatin1String("sys_hub")) {
        closeFan(true);
        statusMonitor()->openKdeModule(QString());
        return;
    }
    if (item.kind == RadialSlotItem::NotificationSlot) {
        m_menuOpen = true;
        QMenu menu(this);
        menu.addAction(tr("打开 %1").arg(item.notificationItem.displayAppName()), this, [this, item] {
            closeFan(true);
            openNotificationTarget(item.notificationItem);
        });
        menu.addAction(tr("标为已读"), this, [this, item] {
            statusMonitor()->markNotificationsReadForApp(
                item.notificationItem.desktopEntry,
                item.notificationItem.appName,
                item.notificationItem.desktopEntry);
            statusMonitor()->markNotificationRead(item.notificationItem.seqId);
            if (m_fanOverlay) m_fanOverlay->refreshLiveLayout();
            refreshAttentionBearings();
        });
        menu.addSeparator();
        menu.addAction(tr("打开通知中心"), this, [this] {
            closeFan(true);
            openSystemPopover(SystemPopover::TabNotifications);
        });
        FloatingWindow::execPopupMenu(&menu, globalPos);
        m_menuOpen = false;
        refreshBallOpacity();
        return;
    }
    if (item.kind != RadialSlotItem::AppSlot) {
        showContextMenu(globalPos);
        return;
    }

    m_menuOpen = true;
    QMenu menu(this);
    menu.addAction(tr("切换 / 打开 %1").arg(item.app.name), this, [this, item] {
        closeFan(true);
        executeSlotItem(item);
    });
    if (!item.app.launchTarget.isEmpty()) {
        const bool pinned = dockModel()->isPinned(item.app.launchTarget);
        menu.addAction(pinned ? tr("从悬浮 Dock 取消固定") : tr("固定到悬浮 Dock"), this, [this, item] {
            const bool nowPinned = dockModel()->togglePin(item.app.launchTarget);
            DesktopActions::showToast(
                nowPinned ? tr("★ 已固定 %1 到悬浮 Dock").arg(item.app.name)
                          : tr("已取消固定 %1").arg(item.app.name),
                anchorCenterGlobal().toPoint());
        });
    }
    if (item.app.running) {
        menu.addSeparator();
        menu.addAction(tr("退出 %1").arg(item.app.name), this, [this, item] {
            const auto out = dockModel()->quitApp(item.app);
            DesktopActions::showToast(out.message, anchorCenterGlobal().toPoint());
        });
    }
    FloatingWindow::execPopupMenu(&menu, globalPos);
    m_menuOpen = false;
    refreshBallOpacity();
}

void FloatingBall::executeAction(const QString &id)
{
    const QPoint center = anchorCenterGlobal().toPoint();
    auto toastOutcome = [center](const DesktopActions::Outcome &out) {
        if (!out.message.isEmpty()) DesktopActions::showToast(out.message, center);
    };

    if (id.startsWith(QLatin1String("vdesk")) && id.size() == 6) {
        const int idx = id.at(5).digitValue() - 1;
        if (idx >= 0) {
            bool already = false;
            QString label;
            if (statusMonitor()->switchVirtualDesktop(idx, &already, &label)) {
                if (already) {
                    toastOutcome(DesktopActions::showDesktop());
                } else {
                    DesktopActions::showToast(tr("✓ 已切换至桌面 %1").arg(label.isEmpty() ? QString::number(idx + 1) : label), center);
                }
            } else {
                toastOutcome(DesktopActions::showOverview());
            }
        }
        return;
    }

    if (id == QLatin1String("back")) toastOutcome(DesktopActions::navigateBack());
    else if (id == QLatin1String("forward")) toastOutcome(DesktopActions::navigateForward());
    else if (id == QLatin1String("overview")) toastOutcome(DesktopActions::showOverview());
    else if (id == QLatin1String("desktop")) toastOutcome(DesktopActions::showDesktop());
    else if (id == QLatin1String("minimize_all")) toastOutcome(dockModel()->minimizeAllWindows());
    else if (id == QLatin1String("crosshair") || id == QLatin1String("capture")) captureRegion();
    else if (id == QLatin1String("clipboard")) toggleClipboardBubble();
    else if (id == QLatin1String("deck")) toggleQuickDeck();
    else if (id == QLatin1String("notifications")) openSystemPopover(SystemPopover::TabNotifications);
    else if (id == QLatin1String("paste")) toastOutcome(DesktopActions::pasteClipboard());
    else if (id == QLatin1String("copy")) toastOutcome(DesktopActions::copySelection());
    else if (id == QLatin1String("record")) recordScreen();
    else if (id == QLatin1String("ime")) {
        const auto out = DesktopActions::switchInputMethod();
        toastOutcome(out);
        const QString curIm = statusMonitor()->snapshot().inputLabel;
        triggerImeFlash(curIm == QStringLiteral("双") ? QStringLiteral("EN") : QStringLiteral("双"));
    }
    else if (id == QLatin1String("terminal")) toastOutcome(DesktopActions::openTerminal());
    else if (id == QLatin1String("browser")) toastOutcome(DesktopActions::openBrowser());
    else if (id == QLatin1String("home")) toastOutcome(DesktopActions::openHomeFolder());
    else if (id == QLatin1String("downloads")) toastOutcome(DesktopActions::openDownloadsFolder());
    else if (id == QLatin1String("trash")) toastOutcome(DesktopActions::openTrash());
    else if (id == QLatin1String("launcher")) toggleAppLauncher();
    else if (id == QLatin1String("sys_hub")) openSystemPopover(SystemPopover::TabHome);
    else if (id == QLatin1String("close_active")) {
        const QList<DockAppEntry> openWins = dockModel()->openWindows();
        const DockAppEntry *target = nullptr;
        for (const DockAppEntry &w : openWins) {
            if (w.active && !w.minimized) {
                target = &w;
                break;
            }
        }
        if (!target) {
            for (const DockAppEntry &w : openWins) {
                if (w.active) {
                    target = &w;
                    break;
                }
            }
        }
        if (target) {
            toastOutcome(dockModel()->quitApp(*target));
        } else {
            toastOutcome(DesktopActions::closeActiveWindow());
        }
    }
}

void FloatingBall::captureRegion()
{
    closeFan(true);
    const QPoint center = anchorCenterGlobal().toPoint();
    QTimer::singleShot(60, this, [center] {
        const auto out = DesktopActions::triggerNativeScreenshot();
        if (!out.message.isEmpty()) DesktopActions::showToast(out.message, center);
    });
}

void FloatingBall::recordScreen()
{
    closeFan(true);
    const QPoint center = anchorCenterGlobal().toPoint();
    QTimer::singleShot(60, this, [center] {
        const auto out = DesktopActions::triggerNativeScreenRecord();
        if (!out.message.isEmpty()) DesktopActions::showToast(out.message, center);
    });
}

void FloatingBall::toggleClipboardBubble()
{
    if (m_quickDeck && m_quickDeck->isVisible()) m_quickDeck->hide();
    if (m_systemPopover && m_systemPopover->isVisible()) m_systemPopover->hide();
    if (m_appLauncher && m_appLauncher->isVisible()) m_appLauncher->hide();
    clipboardBubble()->reveal(anchorCenterGlobal().toPoint(), true);
}

void FloatingBall::toggleQuickDeck()
{
    if (m_clipboardBubble && m_clipboardBubble->isVisible()) m_clipboardBubble->hide();
    if (m_systemPopover && m_systemPopover->isVisible()) m_systemPopover->hide();
    if (m_appLauncher && m_appLauncher->isVisible()) m_appLauncher->hide();
    quickDeck()->reveal(anchorCenterGlobal().toPoint(), true);
}

// ============================================================================
// Fan Open / Close & Dwell Control
// ============================================================================

void FloatingBall::animateFanTo(bool open)
{
    m_orbitVisible = open;
    if (!m_orbitAnimation) {
        m_orbitAnimation = new QVariantAnimation(this);
        connect(m_orbitAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_orbitProgress = v.toReal();
            if (m_fanOverlay) m_fanOverlay->syncProgress(m_orbitProgress);
            update();
        });
        connect(m_orbitAnimation, &QVariantAnimation::finished, this, [this] {
            if (!m_orbitVisible && m_fanOverlay &&
                (m_fanOverlay->overlayMode() == RadialFanOverlay::TapFanMode ||
                 m_fanOverlay->overlayMode() == RadialFanOverlay::FlickFanMode)) {
                m_fanOverlay->dismissOverlay(true);
            }
            refreshBallOpacity();
        });
    }
    m_orbitAnimation->stop();
    const qreal target = open ? 1.0 : 0.0;
    const QSignalBlocker b(m_orbitAnimation);
    m_orbitAnimation->setStartValue(m_orbitProgress);
    m_orbitAnimation->setEndValue(target);
    m_orbitAnimation->setDuration(qMax(70, qRound(qAbs(target - m_orbitProgress) * (open ? 175 : 130))));
    m_orbitAnimation->setEasingCurve(open ? QEasingCurve::OutBack : QEasingCurve::InCubic);
    m_orbitAnimation->setCurrentTime(0);
    m_orbitAnimation->start();
}

void FloatingBall::installOutsideDismissMonitor()
{
    if (m_outsideClickMonitor) return;
    QPointer<FloatingBall> self(this);
    m_outsideClickMonitor = PanelBackdrop::installGlobalPointerDownMonitor(this, [self](const QPoint &globalPos) {
        if (!self || !self->m_orbitVisible || self->m_menuOpen || self->m_settingsOpen) return;
        if (!self->m_fanOverlay || self->m_fanOverlay->overlayMode() != RadialFanOverlay::TapFanMode) return;
        if (self->m_fanOpenedTimer.isValid() && self->m_fanOpenedTimer.elapsed() < 140) return;
        if (self->geometry().adjusted(-6, -6, 6, 6).contains(globalPos)) return;
        if (self->m_fanOverlay->hitSlotAtGlobal(globalPos, false) >= 0) return;
        self->closeFan(false);
    });
}

void FloatingBall::removeOutsideDismissMonitor()
{
    if (!m_outsideClickMonitor) return;
    PanelBackdrop::removeGlobalPointerDownMonitor(m_outsideClickMonitor);
    m_outsideClickMonitor = nullptr;
}

void FloatingBall::openTapFan()
{
    expandFromEdge();
    refreshSystemAudioState();
    if (m_orbitVisible && m_fanOverlay->overlayMode() == RadialFanOverlay::TapFanMode) {
        closeFan(false);
        return;
    }
    closeOtherFans(this);
    m_fanOpenedTimer.restart();
    m_fanOverlay->openTapFan();
    animateFanTo(true);
    installOutsideDismissMonitor();
    raise();
    refreshBallOpacity();
}

void FloatingBall::closeFan(bool immediate)
{
    m_orbitVisible = false;
    removeOutsideDismissMonitor();
    if (immediate) {
        if (m_orbitAnimation) m_orbitAnimation->stop();
        m_orbitProgress = 0.0;
        if (m_fanOverlay) m_fanOverlay->dismissOverlay(true);
        update();
        refreshBallOpacity();
    } else {
        animateFanTo(false);
    }
}

void FloatingBall::startCrosshairDwell(const QPointF &globalPos)
{
    m_crosshairDwellOrigin = globalPos;
    m_dwellProgress = 0.0;
    m_dwellAnimation->stop();
    m_dwellAnimation->setCurrentTime(0);
    m_dwellAnimation->start();
}

void FloatingBall::stopCrosshairDwell()
{
    m_dwellAnimation->stop();
    m_dwellProgress = 0.0;
}

// ============================================================================
// Unified Pointer + Touch State Machine (Xiaomi Mi 5 + fooView Multi-Ring Fusion)
// ============================================================================

namespace {
struct BezelGestureMetrics {
    qreal outFromPress = 0.0;
    qreal tanFromPress = 0.0;
    qreal outFromCenter = 0.0;
    qreal tanFromCenter = 0.0;
    qreal distToBezel = 1e6;
    qreal stepOutward = 0.0;
    qreal stepTangential = 0.0;
};

BezelGestureMetrics computeBezelMetrics(const FloatingBall *ball,
                                        QScreen *assignedScreen,
                                        const QRect &fallbackBounds,
                                        int dockSide,
                                        const QPointF &pressGlobalPos,
                                        const QPointF &prevGlobalPos,
                                        const QPointF &globalPos)
{
    BezelGestureMetrics m;
    const QPointF dPress = globalPos - pressGlobalPos;
    const QPointF dCenter = globalPos - ball->anchorCenterGlobal();
    const QPointF step = globalPos - prevGlobalPos;
    QScreen *scr = (assignedScreen && QGuiApplication::screens().contains(assignedScreen))
        ? assignedScreen
        : QGuiApplication::screenAt(ball->anchorCenterGlobal().toPoint());
    if (!scr) scr = ball->screen();
    const QRect bounds = scr ? scr->availableGeometry()
                             : (fallbackBounds.isEmpty() ? QRect(0, 0, 1920, 1080) : fallbackBounds);
    if (dockSide == FloatingBall::DockRight) {
        m.outFromPress = dPress.x(); m.tanFromPress = qAbs(dPress.y());
        m.outFromCenter = dCenter.x(); m.tanFromCenter = qAbs(dCenter.y());
        m.distToBezel = bounds.right() - globalPos.x();
        m.stepOutward = step.x(); m.stepTangential = qAbs(step.y());
    } else if (dockSide == FloatingBall::DockLeft) {
        m.outFromPress = -dPress.x(); m.tanFromPress = qAbs(dPress.y());
        m.outFromCenter = -dCenter.x(); m.tanFromCenter = qAbs(dCenter.y());
        m.distToBezel = globalPos.x() - bounds.left();
        m.stepOutward = -step.x(); m.stepTangential = qAbs(step.y());
    } else if (dockSide == FloatingBall::DockTop) {
        m.outFromPress = -dPress.y(); m.tanFromPress = qAbs(dPress.x());
        m.outFromCenter = -dCenter.y(); m.tanFromCenter = qAbs(dCenter.x());
        m.distToBezel = globalPos.y() - bounds.top();
        m.stepOutward = -step.y(); m.stepTangential = qAbs(step.x());
    } else if (dockSide == FloatingBall::DockBottom) {
        m.outFromPress = dPress.y(); m.tanFromPress = qAbs(dPress.x());
        m.outFromCenter = dCenter.y(); m.tanFromCenter = qAbs(dCenter.x());
        m.distToBezel = bounds.bottom() - globalPos.y();
        m.stepOutward = step.y(); m.stepTangential = qAbs(step.x());
    }
    return m;
}
} // namespace

void FloatingBall::beginPointerInteraction(const QPointF &globalPos, Qt::KeyboardModifiers modifiers)
{
    FloatingBall *primary = m_primaryPeer ? m_primaryPeer.data() : this;
    m_dismissedPanelOnPress = false;
    if (primary) {
        if (primary->m_systemPopover && primary->m_systemPopover->isVisible()) {
            primary->m_systemPopover->hide();
            m_dismissedPanelOnPress = true;
        }
        if (primary->m_appLauncher && primary->m_appLauncher->isVisible()) {
            primary->m_appLauncher->hide();
            m_dismissedPanelOnPress = true;
        }
        if (primary->m_quickDeck && primary->m_quickDeck->isVisible()) {
            primary->m_quickDeck->hide();
            m_dismissedPanelOnPress = true;
        }
        if (primary->m_clipboardBubble && primary->m_clipboardBubble->isVisible()) {
            primary->m_clipboardBubble->hide();
            m_dismissedPanelOnPress = true;
        }
    }

    m_startedInteractionTucked = m_edgeTucked;
    if (m_edgeTucked && !m_manualTucked && !m_fullscreenTucked) {
        expandFromEdge();
        m_startedInteractionTucked = false;
    }
    if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running)
        m_geometryAnimation->stop();

    m_pressGlobalPos = globalPos;
    m_lastGlobalPos = globalPos;
    m_recentOutwardStep = 0.0;
    m_outwardStroke = 0.0;
    m_outwardStrokeTan = 0.0;
    m_outwardMoveTimer.invalidate();
    m_bezelTuckArmed = false;
    m_idleTimer->stop();

    if (modifiers & (Qt::AltModifier | Qt::ControlModifier | Qt::MetaModifier)) {
        m_manualTucked = false;
        expandFromEdge();
        closeFan(true);
        m_gestureMode = GestureMoveBall;
        m_dragOffset = globalPos - QPointF(pos());
        setCursor(Qt::ClosedHandCursor);
        refreshBallOpacity();
        return;
    }

    m_gestureMode = GesturePressed;
    m_holdTimer->start();
    refreshBallOpacity();
    update();
}

void FloatingBall::updatePointerInteraction(const QPointF &globalPos)
{
    const QPointF prevGlobalPos = m_lastGlobalPos;
    m_lastGlobalPos = globalPos;

    if (m_gestureMode == GesturePressed) {
        const QPointF delta = globalPos - m_pressGlobalPos;
        const qreal dist = std::hypot(delta.x(), delta.y());
        if (dist >= 6.0) m_holdTimer->stop();

        if (!m_edgeTucked && m_dockSide != DockFree && dist >= 3.0) {
            const BezelGestureMetrics bm = computeBezelMetrics(this, m_assignedScreen.data(), m_screenBounds,
                                                               m_dockSide, m_pressGlobalPos, prevGlobalPos, globalPos);
            // Smooth direct push toward the docked screen bezel -> instant Edge Capsule morph!
            // Also handles clicks near the outer rim where only a few px of bezel runway remain.
            const bool pushedToBezel =
                (bm.outFromPress >= 7.0 && bm.outFromPress >= bm.tanFromPress * 0.72) ||
                (bm.outFromPress >= 3.0 && bm.distToBezel <= 6.0 && bm.outFromPress >= bm.tanFromPress * 0.5);
            if (pushedToBezel) {
                m_holdTimer->stop();
                m_gestureMode = GestureIdle;
                m_bezelTuckArmed = false;
                tuckToEdgeCapsule(true, true);
                return;
            }
            // User is clearly nudging outward toward the bezel (not inward) -> wait for 7px/bezel
            // instead of prematurely hijacking into GestureFlickFan!
            if (bm.outFromPress > 2.0 && bm.outFromPress >= bm.tanFromPress * 0.65) {
                return;
            }
        }

        if (dist >= 10.0) {
            m_holdTimer->stop();
            // Inward pull (even starting from the tucked Edge Capsule!) -> restore circle & open FlickFan!
            if (m_edgeTucked) {
                m_manualTucked = false;
                expandFromEdge();
                m_startedInteractionTucked = false;
            }
            closeOtherFans(this);
            m_gestureMode = GestureFlickFan;
            m_bezelTuckArmed = false;
            m_recentOutwardStep = 0.0;
            m_outwardStroke = 0.0;
            m_outwardStrokeTan = 0.0;
            m_outwardMoveTimer.invalidate();
            m_orbitVisible = true;
            m_orbitProgress = 1.0;
            m_fanOverlay->syncProgress(1.0);
            m_fanOverlay->openFlickFan(globalPos);
            raise();
            update();
        }
        return;
    }

    if (m_gestureMode == GestureMoveBall) {
        QPoint target = (globalPos - m_dragOffset).toPoint();
        if (m_multiScreenEnabled && m_assignedScreen && QGuiApplication::screens().size() > 1) {
            const QRect b = m_assignedScreen->availableGeometry();
            target.setX(qBound(b.left(), target.x(), qMax(b.left(), b.right() - width() + 1)));
            target.setY(qBound(b.top(), target.y(), qMax(b.top(), b.bottom() - height() + 1)));
        }
        move(target);
        return;
    }

    if (m_gestureMode == GestureFlickFan) {
        if (!m_fanOverlay->isDrilled() && m_dockSide != DockFree) {
            const BezelGestureMetrics bm = computeBezelMetrics(this, m_assignedScreen.data(), m_screenBounds,
                                                               m_dockSide, m_pressGlobalPos, prevGlobalPos, globalPos);
            if (m_outwardMoveTimer.isValid() && m_outwardMoveTimer.elapsed() > 160) {
                m_outwardStroke = 0.0;
                m_outwardStrokeTan = 0.0;
                m_recentOutwardStep = 0.0;
            }
            if (bm.stepOutward > 0.5) {
                m_outwardStroke += bm.stepOutward;
                m_outwardStrokeTan += bm.stepTangential;
                m_outwardMoveTimer.restart();
            } else if (bm.stepOutward < -1.5) {
                m_outwardStroke = 0.0;
                m_outwardStrokeTan = 0.0;
            }
            m_recentOutwardStep = m_recentOutwardStep * 0.45 + bm.stepOutward * 0.55;

            const qreal ballD = qMax(width(), height());
            const qreal tanLimit = qMax(ballD * 3.0, m_fanOverlay->fanOuterRadius() + 40.0);
            // Use direct orb hit (not wide ±34° magnetic sector cone) so flinging into the screen
            // bezel near top/bottom fan angles is never stolen by the top/bottom Ring 0 sector!
            const int directSlot = m_bezelTuckArmed ? -1 : m_fanOverlay->hitSlotAtGlobal(globalPos, false);
            const bool outwardDominantStep = bm.stepOutward >= bm.stepTangential * 0.75;

            // Moving inward first (even exploring fan slots vertically/diagonally) and then pushing or
            // flinging outward toward the docked screen bezel smoothly folds the fan and arms Edge Capsule!
            // Calm or even fast return strictly to ball center (outFromCenter <= 0) never false-triggers.
            const bool inBezelZone = (directSlot < 0) && (bm.tanFromCenter <= tanLimit) &&
                ((bm.distToBezel <= 14.0 && (bm.outFromCenter >= 2.0 || m_outwardStroke >= 14.0)) ||
                 (bm.outFromCenter >= 3.0 && bm.distToBezel <= ballD * 0.85 &&
                  ((outwardDominantStep && (bm.stepOutward >= 4.5 || m_recentOutwardStep >= 4.5)) ||
                   (m_outwardStroke >= 16.0 && m_outwardStroke >= m_outwardStrokeTan * 1.15))));

            if (inBezelZone) {
                if (!m_bezelTuckArmed) {
                    m_bezelTuckArmed = true;
                    m_orbitVisible = false;
                    m_fanOverlay->dismissOverlay(true);
                    update();
                }
                return;
            }
            if (m_bezelTuckArmed) {
                // Pulling back inward disarms capsule tuck and smoothly restores FlickFan
                if (bm.distToBezel > 24.0 && bm.outFromCenter < -4.0) {
                    m_bezelTuckArmed = false;
                    m_orbitVisible = true;
                    m_orbitProgress = 1.0;
                    m_fanOverlay->syncProgress(1.0);
                    m_fanOverlay->openFlickFan(globalPos);
                    raise();
                    update();
                } else {
                    return;
                }
            }
        }

        const qreal distFromBall = QLineF(anchorCenterGlobal(), globalPos).length();
        // Pull past the outermost active ring at Root -> seamless fooView Crosshair handoff!
        // Never steal continuous drags when drilled inside a branch ring!
        if (!m_fanOverlay->isDrilled() && m_crosshairPullEnabled &&
            distFromBall > m_fanOverlay->fanOuterRadius() + 50.0) {
            m_gestureMode = GestureCrosshairAim;
            m_orbitVisible = false;
            startCrosshairDwell(globalPos);
            m_fanOverlay->enterCrosshairAim(globalPos, m_dwellProgress);
            setCursor(Qt::CrossCursor);
            update();
            return;
        }
        m_fanOverlay->handleContinuousDragAt(globalPos);
        const int slot = m_fanOverlay->hitSlotAtGlobal(globalPos, true);
        m_fanOverlay->updateFinger(globalPos, slot);
        return;
    }

    if (m_gestureMode == GestureCrosshairAim) {
        const qreal distFromBall = QLineF(anchorCenterGlobal(), globalPos).length();
        if (distFromBall <= m_fanOverlay->fanOuterRadius() + 24.0) {
            stopCrosshairDwell();
            m_gestureMode = GestureFlickFan;
            m_orbitVisible = true;
            unsetCursor();
            m_fanOverlay->openFlickFan(globalPos);
            raise();
            update();
            return;
        }
        if (QLineF(globalPos, m_crosshairDwellOrigin).length() > 7.0) {
            startCrosshairDwell(globalPos);
        }
        m_fanOverlay->updateFinger(globalPos, -1, m_dwellProgress);
        return;
    }

    if (m_gestureMode == GestureCrosshairRect) {
        m_fanOverlay->updateCrosshairRect(m_crosshairAnchorGlobal, globalPos);
    }
}

void FloatingBall::endPointerInteraction(const QPointF &globalPos)
{
    m_holdTimer->stop();
    stopCrosshairDwell();
    unsetCursor();

    const GestureMode finishedMode = m_gestureMode;
    m_gestureMode = GestureIdle;

    if (finishedMode == GesturePressed) {
        if (m_dismissedPanelOnPress) {
            m_dismissedPanelOnPress = false;
            refreshBallOpacity();
            update();
            return;
        }
        if (m_startedInteractionTucked && m_edgeTucked) {
            // Clicking / tapping the tucked Edge Capsule restores the Primary Round Ball!
            m_manualTucked = false;
            expandFromEdge();
            refreshBallOpacity();
            return;
        }
        openTapFan();
        return;
    }

    if (finishedMode == GestureMoveBall) {
        snapToEdge(true);
        savePreferences();
        refreshBallOpacity();
        update();
        return;
    }

    if (finishedMode == GestureFlickFan) {
        const int directReleaseSlot = m_bezelTuckArmed ? -1 : m_fanOverlay->hitSlotAtGlobal(globalPos, false);
        if (!m_bezelTuckArmed && directReleaseSlot < 0 && !m_fanOverlay->isDrilled() && m_dockSide != DockFree) {
            const BezelGestureMetrics bm = computeBezelMetrics(this, m_assignedScreen.data(), m_screenBounds,
                                                               m_dockSide, m_pressGlobalPos, m_lastGlobalPos, globalPos);
            const qreal ballD = qMax(width(), height());
            const qreal tanLimit = qMax(ballD * 3.0, m_fanOverlay->fanOuterRadius() + 40.0);
            const bool recentOutward = !m_outwardMoveTimer.isValid() || m_outwardMoveTimer.elapsed() <= 160;
            const bool outwardDominantStep = bm.stepOutward >= bm.stepTangential * 0.75;
            const bool releasedAtBezel = (bm.tanFromCenter <= tanLimit) &&
                ((bm.distToBezel <= 14.0 && (bm.outFromCenter >= 2.0 || m_outwardStroke >= 14.0)) ||
                 (recentOutward && bm.outFromCenter >= 3.0 && bm.distToBezel <= ballD * 0.85 &&
                  ((outwardDominantStep && (bm.stepOutward >= 4.5 || m_recentOutwardStep >= 4.5)) ||
                   (m_outwardStroke >= 16.0 && m_outwardStroke >= m_outwardStrokeTan * 1.15))));
            if (releasedAtBezel) {
                m_bezelTuckArmed = true;
            }
        }
        if (m_bezelTuckArmed && m_dockSide != DockFree) {
            m_bezelTuckArmed = false;
            m_outwardStroke = 0.0;
            m_outwardStrokeTan = 0.0;
            m_outwardMoveTimer.invalidate();
            closeFan(true);
            tuckToEdgeCapsule(true, true);
            refreshBallOpacity();
            return;
        }
        m_bezelTuckArmed = false;
        m_outwardStroke = 0.0;
        m_outwardStrokeTan = 0.0;
        m_outwardMoveTimer.invalidate();
        const int slot = m_fanOverlay->hitSlotAtGlobal(globalPos, true);
        const auto slotsData = m_fanOverlay->slotsData();
        const bool valid = (slot >= 0 && slot < slotsData.size());
        const RadialSlotItem chosen = valid ? slotsData[slot] : RadialSlotItem{};
        if (valid && !chosen.drillKey.isEmpty()) {
            QString title = chosen.shortLabel;
            title.remove(QStringLiteral(" ›")).remove(QStringLiteral("›"));
            m_orbitVisible = true;
            m_orbitProgress = 1.0;
            m_fanOpenedTimer.restart();
            m_fanOverlay->openTapFan();
            m_fanOverlay->syncProgress(1.0);
            m_fanOverlay->drillInto(chosen.drillKey, title.trimmed(), chosen.accentColor, chosen.centerGlobal);
            installOutsideDismissMonitor();
            raise();
            refreshBallOpacity();
            update();
            return;
        }
        const bool isControlNode = valid && (chosen.kind == RadialSlotItem::BackSlot ||
                                             chosen.kind == RadialSlotItem::CenterSlot);
        closeFan(true);
        refreshBallOpacity();
        update();
        if (valid && !isControlNode) executeSlotItem(chosen);
        return;
    }

    if (finishedMode == GestureCrosshairAim) {
        closeFan(true);
        refreshBallOpacity();
        update();
        captureRegion();
        return;
    }

    if (finishedMode == GestureCrosshairRect) {
        const QRect cropRect = QRectF(m_crosshairAnchorGlobal, globalPos).normalized().toAlignedRect();
        closeFan(true);
        refreshBallOpacity();
        update();
        if (cropRect.width() >= 8 && cropRect.height() >= 8) {
            captureRegion();
        }
        return;
    }

    refreshBallOpacity();
    update();
}

void FloatingBall::cancelPointerInteraction()
{
    m_holdTimer->stop();
    stopCrosshairDwell();
    unsetCursor();
    m_gestureMode = GestureIdle;
    m_bezelTuckArmed = false;
    m_recentOutwardStep = 0.0;
    m_outwardStroke = 0.0;
    m_outwardStrokeTan = 0.0;
    m_outwardMoveTimer.invalidate();
    m_activeTouchId = -1;
    m_isPinching = false;
    closeFan(true);
    refreshBallOpacity();
    update();
}

// ============================================================================
// Events (Touch, Gesture, Mouse, Wheel)
// ============================================================================

bool FloatingBall::event(QEvent *ev)
{
    if (ev->type() == QEvent::ScreenChangeInternal) scheduleScreenAdaptation();
    if (ev->type() == QEvent::TouchBegin || ev->type() == QEvent::TouchUpdate ||
        ev->type() == QEvent::TouchEnd || ev->type() == QEvent::TouchCancel) {
        return handleTouchEvent(static_cast<QTouchEvent *>(ev));
    }
    if (ev->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent *>(ev));
    }
    return QWidget::event(ev);
}

QPointF FloatingBall::pointerGlobalPos(const QPointF &localPos, const QPointF &eventGlobalPos) const
{
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
        return QPointF(pos()) + localPos;
    return eventGlobalPos;
}

bool FloatingBall::handleTouchEvent(QTouchEvent *event)
{
    const auto points = event->points();
    if (event->type() == QEvent::TouchCancel) {
        cancelPointerInteraction();
        return true;
    }

    if (points.size() >= 2 &&
        (m_gestureMode == GestureCrosshairAim || m_gestureMode == GestureCrosshairRect ||
         m_gestureMode == GestureFlickFan)) {
        cancelPointerInteraction();
        DesktopActions::showToast(QStringLiteral("已取消准星选择"), geometry().center());
        return true;
    }

    if (event->type() == QEvent::TouchBegin) {
        if (points.size() >= 2) {
            m_holdTimer->stop();
            m_gestureMode = GestureIdle;
            m_isPinching = true;
            m_activeTouchId = -1;
            const QPointF p0 = pointerGlobalPos(points.at(0).position(), points.at(0).globalPosition());
            const QPointF p1 = pointerGlobalPos(points.at(1).position(), points.at(1).globalPosition());
            m_initialPinchDistance = QLineF(p0, p1).length();
            m_initialPinchSize = QSizeF(size());
            refreshBallOpacity();
            return true;
        }
        if (points.size() == 1) {
            m_isPinching = false;
            m_activeTouchId = points.first().id();
            beginPointerInteraction(
                pointerGlobalPos(points.first().position(), points.first().globalPosition()),
                Qt::NoModifier);
            return true;
        }
    }

    if (event->type() == QEvent::TouchUpdate) {
        if (points.size() >= 2 && m_isPinching && m_initialPinchDistance > 0) {
            const QPointF p0 = pointerGlobalPos(points.at(0).position(), points.at(0).globalPosition());
            const QPointF p1 = pointerGlobalPos(points.at(1).position(), points.at(1).globalPosition());
            const qreal d = QLineF(p0, p1).length();
            if (d > 0) {
                const qreal scale = d / m_initialPinchDistance;
                const QPointF mid = (p0 + p1) * 0.5;
                updateBallGeometry(m_initialPinchSize * scale, mid, true, false);
            }
            return true;
        }
        for (const auto &pt : points) {
            if (pt.id() == m_activeTouchId) {
                updatePointerInteraction(pointerGlobalPos(pt.position(), pt.globalPosition()));
                return true;
            }
        }
    }

    if (event->type() == QEvent::TouchEnd) {
        if (m_isPinching) {
            m_isPinching = false;
            snapToEdge(true);
            savePreferences();
            refreshBallOpacity();
            return true;
        }
        for (const auto &pt : points) {
            if (pt.id() == m_activeTouchId) {
                m_activeTouchId = -1;
                endPointerInteraction(pointerGlobalPos(pt.position(), pt.globalPosition()));
                return true;
            }
        }
        m_activeTouchId = -1;
        endPointerInteraction(m_lastGlobalPos);
        return true;
    }
    return true;
}

bool FloatingBall::gestureEvent(QGestureEvent *event)
{
    if (QPinchGesture *pinch = qobject_cast<QPinchGesture *>(event->gesture(Qt::PinchGesture))) {
        m_isPinching = pinch->state() != Qt::GestureFinished && pinch->state() != Qt::GestureCanceled;
        refreshBallOpacity();
        if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
            const qreal factor = pinch->totalScaleFactor();
            if (factor > 0) updateBallGeometry(QSizeF(size()) * factor, QPointF(), false, false);
        }
    }
    return true;
}

void FloatingBall::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    // Only auto-expand on passive mouse hover if tucked due to idle auto-hide;
    // when tucked intentionally (fullscreen video/game or manual bezel push), stay
    // unobtrusive as a slim capsule until clicked or pulled!
    if (m_edgeTucked && !m_manualTucked && !m_fullscreenTucked) {
        expandFromEdge();
    }
    m_pointerInside = true;
    refreshSystemAudioState();
    refreshBallOpacity();
    update();
}

void FloatingBall::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_pointerInside = false;
    refreshBallOpacity();
    update();
}

void FloatingBall::mousePressEvent(QMouseEvent *event)
{
    const QPointF gPos = pointerGlobalPos(event->position(), event->globalPosition());
    if (event->button() == Qt::RightButton) {
        if (m_gestureMode != GestureIdle) {
            cancelPointerInteraction();
            return;
        }
        showContextMenu(gPos.toPoint());
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        expandFromEdge();
        closeFan(true);
        m_gestureMode = GestureMoveBall;
        m_dragOffset = gPos - QPointF(pos());
        setCursor(Qt::ClosedHandCursor);
        refreshBallOpacity();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        beginPointerInteraction(gPos, event->modifiers());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void FloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gestureMode != GestureIdle && (event->buttons() & (Qt::LeftButton | Qt::MiddleButton))) {
        updatePointerInteraction(pointerGlobalPos(event->position(), event->globalPosition()));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void FloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) &&
        m_gestureMode != GestureIdle) {
        endPointerInteraction(pointerGlobalPos(event->position(), event->globalPosition()));
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FloatingBall::wheelEvent(QWheelEvent *event)
{
    if (m_orbitVisible && event->angleDelta().y() != 0) {
        const int next = qBound(kMinRingCount, m_ringCount + (event->angleDelta().y() > 0 ? 1 : -1), kMaxRingCount);
        if (next != m_ringCount) {
            setRingCount(next);
            DesktopActions::showToast(QStringLiteral("外围悬浮层数：%1 层").arg(next), anchorCenterGlobal().toPoint());
        }
        event->accept();
        return;
    }
    expandFromEdge();
    closeFan(true);
    const int delta = event->angleDelta().y() > 0 ? 8 : -8;
    updateBallGeometry(QSizeF(width() + delta, height() + delta), QPointF(), false, true);
    animateBallOpacity(m_activeOpacity, 120);
    refreshBallOpacity();
    savePreferences();
    event->accept();
}

// ============================================================================
// Screen Adaptation & Xiaomi Mi 5 Edge Half-Ring Tucking
// ============================================================================

void FloatingBall::watchScreen(QScreen *display)
{
    connect(display, &QScreen::geometryChanged, this, [this] { scheduleScreenAdaptation(); });
    connect(display, &QScreen::availableGeometryChanged, this, [this] { scheduleScreenAdaptation(); });
    connect(display, &QScreen::logicalDotsPerInchChanged, this, [this] { scheduleScreenAdaptation(); });
}

void FloatingBall::scheduleScreenAdaptation()
{
    if (m_screenAdaptationPending || m_adaptingScreen) return;
    m_screenAdaptationPending = true;
    QTimer::singleShot(0, this, [this] {
        m_screenAdaptationPending = false;
        adaptToScreen();
    });
}

void FloatingBall::adaptToScreen()
{
    QScreen *display = (m_multiScreenEnabled && m_assignedScreen &&
                        QGuiApplication::screens().contains(m_assignedScreen) &&
                        FloatingWindow::isValidScreen(m_assignedScreen))
        ? m_assignedScreen.data()
        : m_currentScreen.data();
    if (!display || !QGuiApplication::screens().contains(display) || !FloatingWindow::isValidScreen(display))
        display = QGuiApplication::primaryScreen();
    if (!FloatingWindow::isValidScreen(display)) {
        display = nullptr;
        for (QScreen *cand : QGuiApplication::screens()) {
            if (FloatingWindow::isValidScreen(cand)) { display = cand; break; }
        }
    }
    if (!display) return;
    m_currentScreen = display;
    if (m_multiScreenEnabled && !m_assignedScreen) m_assignedScreen = display;
    fitToScreen(display->availableGeometry(), display->geometry().size());
}

void FloatingBall::recoverVisibleAfterOutputChange()
{
    if (!isPrimaryBall()) return;
    QList<QScreen *> screens;
    for (QScreen *s : QGuiApplication::screens()) {
        if (FloatingWindow::isValidScreen(s)) screens.append(s);
    }
    if (screens.isEmpty()) {
        // All outputs momentarily vanished (DPMS sleep / lid-close -> Wayland placeholder screen).
        // Mark surface closed so we recreate cleanly when a real output returns.
        m_waylandSurfaceClosed = true;
        return;
    }

    const bool assignedMissing = !m_assignedScreen || !screens.contains(m_assignedScreen);
    if (assignedMissing) {
        QScreen *prim = QGuiApplication::primaryScreen();
        m_assignedScreen = FloatingWindow::isValidScreen(prim) ? prim : screens.first();
        setProperty("floatingballAssignedScreen", QVariant::fromValue(m_assignedScreen.data()));
        loadScreenPlacement();
    }

    if (screens.size() == 1 && m_hiddenOnThisScreen && !m_userHiddenAll) {
        // Never leave zero balls visible when docked in single-monitor clamshell mode!
        m_hiddenOnThisScreen = false;
    }

    adaptToScreen();

    if (!m_userHiddenAll && !m_hiddenOnThisScreen && m_assignedScreen) {
        const bool isWayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
        const bool needsRemap = !isVisible() || assignedMissing || m_waylandSurfaceClosed ||
            (isWayland && windowHandle() && windowHandle()->screen() != m_assignedScreen);
        if (needsRemap) {
            m_waylandSurfaceClosed = false;
            closeFan(true);
            if (isVisible()) hide();
            if (windowHandle()) windowHandle()->setScreen(m_assignedScreen);
            show();
            raise();
            adaptToScreen();
            refreshBallOpacity();
        }
    }

    syncScreenReplicas();
    for (FloatingBall *rep : replicaBalls()) {
        if (!rep) continue;
        if (!m_userHiddenAll && !rep->m_hiddenOnThisScreen && rep->m_assignedScreen &&
            screens.contains(rep->m_assignedScreen)) {
            const bool isWayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
            if (!rep->isVisible() ||
                (isWayland && rep->windowHandle() && rep->windowHandle()->screen() != rep->m_assignedScreen)) {
                rep->closeFan(true);
                if (rep->isVisible()) rep->hide();
                if (rep->windowHandle()) rep->windowHandle()->setScreen(rep->m_assignedScreen);
                rep->show();
                rep->raise();
                rep->adaptToScreen();
                rep->refreshBallOpacity();
            }
        }
    }
}

void FloatingBall::fitToScreen(const QRect &bounds, const QSize &screenSize)
{
    if (bounds.isEmpty()) return;
    const bool wasTucked = m_edgeTucked;
    if (wasTucked) expandFromEdge();
    const QScopedValueRollback<bool> guard(m_adaptingScreen, true);

    m_screenBounds = bounds;
    const QSize logicalSize = screenSize.isEmpty() ? bounds.size() : screenSize;
    m_screenScale = qBound(0.6, qMin(logicalSize.width() / 1280.0, logicalSize.height() / 800.0), 1.0);
    const int diameter = qBound(kMinDiameter, qRound(m_preferredDiameter * m_screenScale),
                                qMin(bounds.width(), bounds.height()));

    QPoint targetPos;
    if (m_snapEnabled && m_dockSide != DockFree) {
        if (m_dockSide == DockTop || m_dockSide == DockBottom) {
            const int minX = bounds.left() + 8;
            const int maxX = qMax(minX, bounds.right() - diameter - 8);
            const int x = minX + qRound(m_verticalFraction * (maxX - minX));
            const int y = (m_dockSide == DockTop) ? bounds.top() : (bounds.bottom() - diameter + 1);
            targetPos = QPoint(x, y);
        } else {
            const int minY = bounds.top() + 8;
            const int maxY = qMax(minY, bounds.bottom() - diameter - 8);
            const int y = minY + qRound(m_verticalFraction * (maxY - minY));
            const int x = (m_dockSide == DockLeft) ? bounds.left() : (bounds.right() - diameter + 1);
            targetPos = QPoint(x, y);
        }
    } else {
        const QPointF c = QRectF(geometry()).center();
        targetPos = QPoint(qBound(bounds.left(), qRound(c.x() - diameter * 0.5), bounds.right() - diameter + 1),
                           qBound(bounds.top(), qRound(c.y() - diameter * 0.5), bounds.bottom() - diameter + 1));
    }

    applyGeometry(QRect(targetPos, QSize(diameter, diameter)), false);
    m_expandedDockGeometry = geometry();
    if (m_orbitVisible && m_fanOverlay) m_fanOverlay->syncProgress(m_orbitProgress);
    if (wasTucked) collapseToEdge();
}

void FloatingBall::updateBallGeometry(const QSizeF &size, const QPointF &anchorGlobalPos,
                                      bool keepAnchor, bool animate)
{
    expandFromEdge();
    const int diameter = qBound(qRound(kMinDiameter * m_screenScale),
                                qRound(qMin(size.width(), size.height())),
                                qRound(kMaxDiameter * m_screenScale));
    m_preferredDiameter = diameter / m_screenScale;
    const QSize targetSize(diameter, diameter);

    QPoint targetPos = pos();
    if (keepAnchor) {
        targetPos = QPoint(qRound(anchorGlobalPos.x()) - targetSize.width() / 2,
                           qRound(anchorGlobalPos.y()) - targetSize.height() / 2);
    } else {
        const QPoint center = geometry().center();
        targetPos = QPoint(center.x() - targetSize.width() / 2,
                           center.y() - targetSize.height() / 2);
    }
    applyGeometry(QRect(targetPos, targetSize), animate);
    if (m_dockSide != 0) snapToEdge(false);
}

void FloatingBall::applyGeometry(const QRect &targetGeometry, bool animate)
{
    if (targetGeometry.isEmpty()) return;
    if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running)
        m_geometryAnimation->stop();

    if (animate && targetGeometry != geometry()) {
        m_geometryAnimation->setStartValue(geometry());
        m_geometryAnimation->setEndValue(targetGeometry);
        m_geometryAnimation->start();
        return;
    }
    setGeometry(targetGeometry);
    updateBallMask();
}

void FloatingBall::snapToEdge(bool animate)
{
    if (!m_snapEnabled || m_edgeTucked) {
        if (!m_snapEnabled) m_dockSide = DockFree;
        return;
    }
    QScreen *display = (m_multiScreenEnabled && m_assignedScreen && QGuiApplication::screens().size() > 1 &&
                        QGuiApplication::screens().contains(m_assignedScreen))
        ? m_assignedScreen.data()
        : QGuiApplication::screenAt(geometry().center());
    if (!display) display = screen();
    if (!display) return;
    const QRect bounds = display->availableGeometry();
    const int d = qMax(width(), height());
    const QPoint center = geometry().center();

    // Snap to whichever of the 4 screen edges (Top / Bottom / Left / Right) is closest!
    const int dLeft = qAbs(center.x() - bounds.left());
    const int dRight = qAbs(bounds.right() - center.x());
    const int dTop = qAbs(center.y() - bounds.top());
    const int dBottom = qAbs(bounds.bottom() - center.y());
    const int minDist = qMin(qMin(dLeft, dRight), qMin(dTop, dBottom));

    QRect targetRect;
    if (minDist == dTop) {
        m_dockSide = DockTop;
        const int minX = bounds.left() + 8;
        const int maxX = qMax(minX, bounds.right() - d - 8);
        const int clampedX = qBound(minX, x(), maxX);
        m_verticalFraction = (maxX > minX) ? double(clampedX - minX) / double(maxX - minX) : 0.85;
        targetRect = QRect(clampedX, bounds.top(), d, d);
    } else if (minDist == dBottom) {
        m_dockSide = DockBottom;
        const int minX = bounds.left() + 8;
        const int maxX = qMax(minX, bounds.right() - d - 8);
        const int clampedX = qBound(minX, x(), maxX);
        m_verticalFraction = (maxX > minX) ? double(clampedX - minX) / double(maxX - minX) : 0.50;
        targetRect = QRect(clampedX, bounds.bottom() - d + 1, d, d);
    } else if (minDist == dLeft) {
        m_dockSide = DockLeft;
        const int minY = bounds.top() + 8;
        const int maxY = qMax(minY, bounds.bottom() - d - 8);
        const int clampedY = qBound(minY, y(), maxY);
        m_verticalFraction = (maxY > minY) ? double(clampedY - minY) / double(maxY - minY) : 0.58;
        targetRect = QRect(bounds.left(), clampedY, d, d);
    } else {
        m_dockSide = DockRight;
        const int minY = bounds.top() + 8;
        const int maxY = qMax(minY, bounds.bottom() - d - 8);
        const int clampedY = qBound(minY, y(), maxY);
        m_verticalFraction = (maxY > minY) ? double(clampedY - minY) / double(maxY - minY) : 0.58;
        targetRect = QRect(bounds.right() - d + 1, clampedY, d, d);
    }

    m_screenBounds = bounds;
    m_expandedDockGeometry = targetRect;
    applyGeometry(targetRect, animate);
    refreshAttentionBearings();
}

void FloatingBall::tuckToEdgeCapsule(bool manual, bool animate)
{
    if (m_dockSide == DockFree) return;
    closeFan(true);
    if (manual) m_manualTucked = true;

    if (!m_edgeTucked) {
        m_expandedDockGeometry = geometry();
    }
    m_edgeTucked = true;

    const QRect bounds = m_screenBounds.isEmpty()
        ? (screen() ? screen()->availableGeometry() : QRect(0, 0, 1920, 1080))
        : m_screenBounds;
    const int d = qMax(m_expandedDockGeometry.width(), m_expandedDockGeometry.height());
    // Supplement Edge Capsule morph: slim vertical capsule on Left/Right edges (~26x84),
    // slim horizontal capsule on Top/Bottom edges (~84x26).
    const int capThick = qBound(24, qRound(d * 0.44), 30);
    const int capLen = qBound(74, qRound(d * 1.38), 102);

    QRect target;
    if (m_dockSide == DockTop) {
        const int cx = m_expandedDockGeometry.center().x();
        const int x = qBound(bounds.left() + 6, cx - capLen / 2, qMax(bounds.left() + 6, bounds.right() - capLen - 6));
        target = QRect(x, bounds.top(), capLen, capThick);
    } else if (m_dockSide == DockBottom) {
        const int cx = m_expandedDockGeometry.center().x();
        const int x = qBound(bounds.left() + 6, cx - capLen / 2, qMax(bounds.left() + 6, bounds.right() - capLen - 6));
        target = QRect(x, bounds.bottom() - capThick + 1, capLen, capThick);
    } else if (m_dockSide == DockLeft) {
        const int cy = m_expandedDockGeometry.center().y();
        const int y = qBound(bounds.top() + 6, cy - capLen / 2, qMax(bounds.top() + 6, bounds.bottom() - capLen - 6));
        target = QRect(bounds.left(), y, capThick, capLen);
    } else {
        const int cy = m_expandedDockGeometry.center().y();
        const int y = qBound(bounds.top() + 6, cy - capLen / 2, qMax(bounds.top() + 6, bounds.bottom() - capLen - 6));
        target = QRect(bounds.right() - capThick + 1, y, capThick, capLen);
    }

    applyGeometry(target, animate);
    update();
}

void FloatingBall::collapseToEdge()
{
    if (m_edgeTucked || !m_autoHideEnabled || m_dockSide == DockFree || m_orbitVisible || m_gestureMode != GestureIdle)
        return;
    tuckToEdgeCapsule(false, false);
}

void FloatingBall::expandFromEdge()
{
    if (!m_edgeTucked) return;
    m_edgeTucked = false;
    applyGeometry(m_expandedDockGeometry, false);
    update();
}

void FloatingBall::updateBallMask()
{
    if (width() != height() || m_edgeTucked) {
        const qreal r = qMin(width(), height()) * 0.5;
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), r, r);
        const QRegion reg(path.toFillPolygon().toPolygon());
        setMask(reg.isEmpty() ? QRegion(rect()) : reg);
    } else {
        setMask(QRegion(rect(), QRegion::Ellipse));
    }
}

void FloatingBall::animateBallOpacity(qreal target, int duration)
{
    m_opacityAnimation->stop();
    if (qAbs(m_ballOpacity - target) < 0.001) return;
    const QSignalBlocker blocker(m_opacityAnimation);
    m_opacityAnimation->setStartValue(m_ballOpacity);
    m_opacityAnimation->setEndValue(target);
    m_opacityAnimation->setDuration(duration);
    m_opacityAnimation->setCurrentTime(0);
    m_opacityAnimation->start();
}

void FloatingBall::refreshBallOpacity()
{
    if (m_pointerInside || m_gestureMode != GestureIdle || m_isPinching || m_orbitVisible || m_menuOpen) {
        m_idleTimer->stop();
        const qreal target = (m_gestureMode != GestureIdle || m_isPinching) ? 0.96 : m_activeOpacity;
        animateBallOpacity(target, 110);
    } else if (!m_idleTimer->isActive()) {
        m_idleTimer->start();
    }
}

void FloatingBall::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBallMask();
    if (m_orbitVisible && m_fanOverlay) m_fanOverlay->syncProgress(m_orbitProgress);
}

void FloatingBall::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (!m_adaptingScreen && !m_edgeTucked) {
        if (!(m_multiScreenEnabled && m_assignedScreen && QGuiApplication::screens().size() > 1)) {
            if (QScreen *display = QGuiApplication::screenAt(geometry().center())) {
                if (m_currentScreen != display) {
                    m_currentScreen = display;
                    m_screenBounds = display->availableGeometry();
                }
            }
        }
    }
    if (m_orbitVisible && m_fanOverlay) m_fanOverlay->syncProgress(m_orbitProgress);
}

void FloatingBall::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (isPrimaryBall() && m_multiScreenEnabled && !m_userHiddenAll) {
        syncScreenReplicas();
        for (FloatingBall *rep : replicaBalls()) {
            if (!rep->m_hiddenOnThisScreen && !rep->isVisible())
                rep->show();
        }
    }
}

void FloatingBall::closeEvent(QCloseEvent *event)
{
    // On Wayland, KWin sends zwlr_layer_surface_v1.closed whenever a display
    // output is disabled/hot-unplugged (e.g. lid close or DeskPort disconnect).
    // Never let an output-closed event permanently hide or terminate FloatingBall!
    if (QCoreApplication::closingDown()) {
        QWidget::closeEvent(event);
        return;
    }
    event->ignore();
    m_waylandSurfaceClosed = true;
    FloatingBall *primary = m_primaryPeer ? m_primaryPeer.data() : this;
    QPointer<FloatingBall> pPtr(primary);
    QTimer::singleShot(140, primary, [pPtr] {
        if (pPtr) pPtr->recoverVisibleAfterOutputChange();
    });
}

// ============================================================================
// Painting — Xiaomi Mi 5 Double-Ring Sphere + Live System Mute Badge
// ============================================================================

void FloatingBall::paintEvent(QPaintEvent *)
{
    const qreal dpr = devicePixelRatioF();
    QPixmap face(QSize(qRound(width() * dpr), qRound(height() * dpr)));
    face.setDevicePixelRatio(dpr);
    face.fill(Qt::transparent);

    QPainter p(&face);
    p.setRenderHint(QPainter::Antialiasing);

    const SystemStatusSnapshot &st = statusMonitor()->snapshot();
    const QString imeFlash = activeImeFlashLabel();
    const QTime now = QTime::currentTime();
    const QString hh = now.toString(QStringLiteral("HH"));
    const QString mm = now.toString(QStringLiteral("mm"));

    // ========================================================================
    // Supplement Morph: Slim Edge Capsule (Vertical on Left/Right, Horizontal on Top/Bottom)
    // ========================================================================
    if (m_edgeTucked || width() != height()) {
        const QRectF capRect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal capR = qMin(capRect.width(), capRect.height()) * 0.5;
        const bool vertical = capRect.height() >= capRect.width();

        QLinearGradient bg(capRect.topLeft(), capRect.bottomRight());
        bg.setColorAt(0.0, QColor(34, 40, 52, 238));
        bg.setColorAt(1.0, QColor(16, 20, 28, 246));
        p.setBrush(bg);
        p.setPen(QPen(QColor(124, 200, 255, 92), 1.2));
        p.drawRoundedRect(capRect, capR, capR);

        if (m_statusOrbMode) {
            const QPointF c = capRect.center();
            if (vertical) {
                // Center 2-line 24H zero-padded clock ("09" / "05") or transient IME flash!
                if (!imeFlash.isEmpty()) {
                    QFont f = font();
                    f.setPixelSize(qRound(capRect.width() * 0.48));
                    f.setWeight(QFont::Bold);
                    p.setFont(f);
                    p.setPen(QColor("#7dd3fc"));
                    p.drawText(capRect, Qt::AlignCenter, imeFlash);
                } else {
                    QFont f = font();
                    f.setPixelSize(qMax(10, qRound(capRect.width() * 0.44)));
                    f.setWeight(QFont::Bold);
                    p.setFont(f);
                    p.setPen(QColor("#f8fafc"));
                    p.drawText(QRectF(capRect.left(), c.y() - 16.0, capRect.width(), 16.0), Qt::AlignCenter, hh);
                    p.setPen(QColor("#cbd5e1"));
                    p.drawText(QRectF(capRect.left(), c.y() + 0.5, capRect.width(), 16.0), Qt::AlignCenter, mm);
                }

                // Bottom compact battery bar
                if (st.batteryPresent) {
                    const qreal bw = capRect.width() * 0.54;
                    const QRectF bTrack(c.x() - bw * 0.5, capRect.bottom() - 9.5, bw, 3.2);
                    p.setPen(Qt::NoPen);
                    p.setBrush(QColor(242, 246, 252, 52));
                    p.drawRoundedRect(bTrack, 1.6, 1.6);
                    const QColor bCol = st.batteryPluggedIn ? QColor("#6edba0")
                        : (st.batteryPercent < 15 ? QColor("#ef5350")
                           : (st.batteryPercent < 25 ? QColor("#f4c542") : QColor("#dce8f8")));
                    p.setBrush(bCol);
                    p.drawRoundedRect(QRectF(bTrack.left(), bTrack.top(),
                                             bw * qBound(0, st.batteryPercent, 100) / 100.0, 3.2), 1.6, 1.6);
                }

                // Project directional message radar dots onto the inner screen-facing edge!
                const qreal innerX = (m_dockSide == DockLeft) ? (capRect.right() - 3.2) : (capRect.left() + 3.2);
                for (qreal rad : m_attentionAnglesRad) {
                    const qreal normY = qBound(-0.82, std::sin(rad), 0.82);
                    const QPointF dp(innerX, c.y() + normY * (capRect.height() * 0.36));
                    p.setPen(QPen(QColor("#ffffff"), 0.9));
                    p.setBrush(QColor("#ff4d6d"));
                    p.drawEllipse(dp, 3.2, 3.2);
                }
            } else {
                // Horizontal capsule on Top/Bottom screen edges
                QFont f = font();
                f.setPixelSize(qMax(10, qRound(capRect.height() * 0.46)));
                f.setWeight(QFont::Bold);
                p.setFont(f);
                p.setPen(!imeFlash.isEmpty() ? QColor("#7dd3fc") : QColor("#f8fafc"));
                p.drawText(capRect, Qt::AlignCenter, !imeFlash.isEmpty() ? imeFlash : (hh + QLatin1Char(':') + mm));

                const qreal innerY = (m_dockSide == DockTop) ? (capRect.bottom() - 3.2) : (capRect.top() + 3.2);
                for (qreal rad : m_attentionAnglesRad) {
                    const qreal normX = qBound(-0.82, std::cos(rad), 0.82);
                    const QPointF dp(c.x() + normX * (capRect.width() * 0.36), innerY);
                    p.setPen(QPen(QColor("#ffffff"), 0.9));
                    p.setBrush(QColor("#ff4d6d"));
                    p.drawEllipse(dp, 3.2, 3.2);
                }
            }
        }

        p.end();
        QPainter out(this);
        out.setOpacity(qMax(0.58, m_ballOpacity));
        out.drawPixmap(0, 0, face);
        return;
    }

    // ========================================================================
    // Primary Hero Form: Circular Status & 2-Line 24H Clock Ball
    // ========================================================================
    const qreal d = qMax(width(), height());
    const QPointF sphereCenter = QRectF(rect()).center();
    const qreal radius = d * 0.5 - 2.2;
    const QRectF sphereRect(sphereCenter.x() - radius, sphereCenter.y() - radius, radius * 2.0, radius * 2.0);

    // Subtle soft shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 48));
    p.drawEllipse(sphereRect.translated(0, 1.4));

    // Obsidian glass sphere body
    QLinearGradient surface(sphereRect.topLeft(), sphereRect.bottomRight());
    if (m_gestureMode == GestureMoveBall || m_pickupPulse > 0.05) {
        surface.setColorAt(0.0, QColor(46, 92, 168, 240));
        surface.setColorAt(1.0, QColor(24, 48, 96, 245));
    } else {
        surface.setColorAt(0.0, QColor(42, 46, 56, 236));
        surface.setColorAt(1.0, QColor(20, 23, 30, 244));
    }
    p.setBrush(surface);
    const QColor rimColor = (m_gestureMode == GestureMoveBall || m_orbitVisible || m_bezelTuckArmed)
        ? QColor("#7cc8ff")
        : QColor(255, 255, 255, 62);
    const qreal rimWidth = (m_gestureMode == GestureMoveBall || m_orbitVisible || m_bezelTuckArmed) ? 1.5 : 1.2;
    p.setPen(QPen(rimColor, rimWidth));
    p.drawEllipse(sphereRect);

    // Subtle bezel-hugging cyan morph arc when inward-then-outward bezel tuck is armed
    if (m_bezelTuckArmed && m_dockSide != DockFree) {
        int centerDeg = 0;
        if (m_dockSide == DockLeft) centerDeg = 180;
        else if (m_dockSide == DockTop) centerDeg = 90;
        else if (m_dockSide == DockBottom) centerDeg = 270;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#7dd3fc"), 2.8, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(sphereRect.adjusted(0.8, 0.8, -0.8, -0.8), (centerDeg - 40) * 16, 80 * 16);
    }

    if (m_orbitVisible) {
        const qreal iconDim = d * 0.34;
        FloatingIcons::draw(p, FloatingIcons::Close,
                            QRectF(sphereCenter.x() - iconDim * 0.5, sphereCenter.y() - iconDim * 0.5,
                                   iconDim, iconDim),
                            QColor("#f5f8fd"));
    } else if (m_statusOrbMode) {
        const QColor ink(QStringLiteral("#f2f6fc"));
        const QColor dimInk(242, 246, 252, 52);

        const qreal rOuter = radius * 0.84;
        const qreal rInner = radius * 0.65;
        const QRectF outerArcRect(sphereCenter.x() - rOuter, sphereCenter.y() - rOuter, rOuter * 2.0, rOuter * 2.0);
        const QRectF innerArcRect(sphereCenter.x() - rInner, sphereCenter.y() - rInner, rInner * 2.0, rInner * 2.0);

        // 1. Left outer continuous arc: Screen Brightness (bottom -> top)
        if (st.brightnessAvailable) {
            const qreal trackW = qMax(1.6, d * 0.036);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(dimInk, trackW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(outerArcRect, qRound(220.0 * 16), qRound(-80.0 * 16));
            if (st.brightnessPercent > 0) {
                p.setPen(QPen(ink, trackW, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(outerArcRect, qRound(220.0 * 16),
                          qRound(-80.0 * qBound(0, st.brightnessPercent, 100) / 100.0 * 16));
            }
        }

        // 2. Right outer 13-dot arc: Playback Volume (bottom -> top, all dim when muted)
        if (st.volumeAvailable) {
            const int litDots = st.volumeMuted
                ? 0
                : static_cast<int>(qCeil(qBound(0, st.volumePercent, 100) * 13.0 / 100.0));
            const qreal dotR = qMax(0.95, d * 0.019);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 13; ++i) {
                const qreal deg = 40.0 - 80.0 * i / 12.0;
                const qreal rad = qDegreesToRadians(deg);
                const QPointF pt(sphereCenter.x() + rOuter * qCos(rad),
                                 sphereCenter.y() + rOuter * qSin(rad));
                p.setBrush(i < litDots ? ink : dimInk);
                p.drawEllipse(pt, dotR, dotR);
            }
        }

        // 3. Top 250-deg inner arc: Battery
        if (st.batteryPresent) {
            const qreal battW = qMax(2.0, d * 0.046);
            const QColor battColor = st.batteryPluggedIn ? QColor("#6edba0")
                : (st.batteryPercent < 15 ? QColor("#ef5350")
                   : (st.batteryPercent < 25 ? QColor("#f4c542") : ink));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(dimInk, battW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(innerArcRect, qRound(215.0 * 16), qRound(-250.0 * 16));
            if (st.batteryPercent > 0) {
                p.setPen(QPen(battColor, battW, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(innerArcRect, qRound(215.0 * 16),
                          qRound(-250.0 * qBound(0, st.batteryPercent, 100) / 100.0 * 16));
            }
        }

        // 4. Bottom 5 signal dots: Network (offline = 5 red dots)
        {
            const bool offline = !st.networkConnected;
            const int litSignal = offline ? 5 : qBound(0, static_cast<int>(qCeil(st.signalQuality / 20.0)), 5);
            const QColor sigColor = offline ? QColor("#ef5350") : ink;
            const qreal sigR = qMax(1.15, d * 0.022);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 5; ++i) {
                const qreal deg = 130.0 - i * 20.0;
                const qreal rad = qDegreesToRadians(deg);
                const QPointF pt(sphereCenter.x() + rInner * qCos(rad),
                                 sphereCenter.y() + rInner * qSin(rad));
                p.setBrush(i < litSignal ? sigColor : dimInk);
                p.drawEllipse(pt, sigR, sigR);
            }
        }

        // 5. Center Emblem:
        //    • Default: 2-Line 24H Zero-Padded Clock ("09" over "05")
        //    • Transient on keyboard/IME activity: crisp IME badge ("双" or "EN")
        if (!imeFlash.isEmpty()) {
            const qreal badgeW = rInner * 1.38;
            const qreal badgeH = rInner * 1.12;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(56, 189, 248, 52));
            p.drawRoundedRect(QRectF(sphereCenter.x() - badgeW * 0.5, sphereCenter.y() - badgeH * 0.5,
                                     badgeW, badgeH), 7.0, 7.0);
            QFont f = font();
            f.setPixelSize(qRound(imeFlash == QStringLiteral("双") ? d * 0.28 : d * 0.23));
            f.setWeight(QFont::Bold);
            p.setFont(f);
            p.setPen(QColor("#7dd3fc"));
            p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - rInner - 0.5,
                              rInner * 2.0, rInner * 2.0),
                       Qt::AlignCenter, imeFlash);
        } else {
            QFont f = font();
            f.setPixelSize(qMax(10, qRound(d * 0.215)));
            f.setWeight(QFont::Bold);
            p.setFont(f);
            const qreal lineH = d * 0.235;
            const qreal vShift = d * 0.102;
            p.setPen(QColor("#f8fafc"));
            p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - vShift - lineH * 0.5,
                              rInner * 2.0, lineH),
                       Qt::AlignCenter, hh);
            p.setPen(QColor("#cbd5e1"));
            p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() + vShift - lineH * 0.5,
                              rInner * 2.0, lineH),
                       Qt::AlignCenter, mm);
        }

        // 6. Privacy indicator micro-dots (Top-Right = Mic, Bottom-Right = Camera)
        const qreal cornerOffset = radius * 0.59;
        const qreal cornerDotR = qMax(2.1, d * 0.042);
        p.setPen(Qt::NoPen);
        if (st.micActive) {
            const QPointF pt(sphereCenter.x() + cornerOffset, sphereCenter.y() - cornerOffset);
            p.setBrush(QColor("#ff9f0a"));
            p.drawEllipse(pt, cornerDotR, cornerDotR);
        }
        if (st.cameraActive) {
            const QPointF pt(sphereCenter.x() + cornerOffset, sphereCenter.y() + cornerOffset);
            p.setBrush(QColor("#34c759"));
            p.drawEllipse(pt, cornerDotR, cornerDotR);
        }
    } else {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 130), qMax(1.4, d * 0.036)));
        p.drawEllipse(sphereCenter, d * 0.29, d * 0.29);

        p.setPen(QPen(QColor(255, 255, 255, 235), qMax(1.8, d * 0.048)));
        p.drawEllipse(sphereCenter, d * 0.19, d * 0.19);
    }

    // ★ 7. Directional Message Radar Dots on the Main Ball Rim! ★
    // Each notifying window in the outer rings (or tray attention) projects a crisp dot
    // at its exact expansion bearing angle on the main ball's perimeter!
    if (m_statusOrbMode && !m_attentionAnglesRad.isEmpty()) {
        const qreal rimR = radius * 0.89;
        const qreal dotR = qMax(2.6, d * 0.050);
        for (qreal rad : m_attentionAnglesRad) {
            const QPointF pt(sphereCenter.x() + std::cos(rad) * rimR,
                             sphereCenter.y() + std::sin(rad) * rimR);
            QRadialGradient glow(pt, dotR * 2.3);
            glow.setColorAt(0.0, QColor(255, 77, 109, 215));
            glow.setColorAt(1.0, QColor(255, 77, 109, 0));
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawEllipse(pt, dotR * 2.3, dotR * 2.3);

            p.setBrush(QColor("#ff4d6d"));
            p.setPen(QPen(QColor("#ffffff"), 1.0));
            p.drawEllipse(pt, dotR, dotR);
        }
    }

    p.end();

    QPainter out(this);
    out.setOpacity(m_ballOpacity);
    out.drawPixmap(0, 0, face);
}

// ============================================================================
// Tray, Context Menu & Settings Dialog (Multi-Ring + Dock Replacement)
// ============================================================================

void FloatingBall::initTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    QIcon trayIco(QStringLiteral(":/icons/floatingball-32.png"));
    if (trayIco.isNull()) trayIco = FloatingIcons::make(FloatingIcons::Orb, QColor("#eef4fc"), 32);
    m_trayIcon = new QSystemTrayIcon(trayIco, this);
    m_trayIcon->setToolTip(QStringLiteral("FloatingBall · 多层触控悬浮球 & 悬浮 Dock"));
    auto *menu = new QMenu(this);
    auto *visibility = menu->addAction(tr("显示悬浮球"));
    visibility->setCheckable(true);
    visibility->setChecked(true);
    connect(visibility, &QAction::toggled, this, &FloatingBall::setBallVisible);
    connect(menu, &QMenu::aboutToShow, this, [this, visibility] {
        const QSignalBlocker b(visibility);
        visibility->setChecked(isVisible());
    });
#ifdef Q_OS_LINUX
    menu->addAction(tr("截屏 / 录屏"), this, &FloatingBall::captureRegion);
#else
    menu->addAction(tr("区域截图"), this, &FloatingBall::captureRegion);
    menu->addAction(tr("屏幕录制"), this, &FloatingBall::recordScreen);
#endif
    menu->addAction(tr("触控剪贴板"), this, &FloatingBall::toggleClipboardBubble);
    menu->addAction(tr("触控快捷甲板"), this, &FloatingBall::toggleQuickDeck);

    auto *ringsMenu = menu->addMenu(tr("外围悬浮层数（1~%1 层）").arg(kMaxRingCount));
    for (int n : {1, 2, 3, 4, 6, 8, 10, 15, 20, 30}) {
        QAction *a = ringsMenu->addAction(
            n == kDefaultRingCount ? tr("%1 层（默认 · 2层动作+已开应用）").arg(n)
                                   : (n == kMaxRingCount ? tr("%1 层（铺满屏幕自适应上限）").arg(n) : tr("%1 层").arg(n)));
        connect(a, &QAction::triggered, this, [this, n] {
            setRingCount(n);
            DesktopActions::showToast(tr("外围悬浮层数已设为 %1 层（自动限屏不越界）").arg(n), anchorCenterGlobal().toPoint());
        });
    }

    menu->addSeparator();
    auto *multiScreenAct = menu->addAction(tr("多显示器每屏独立显示悬浮球"));
    multiScreenAct->setCheckable(true);
    multiScreenAct->setChecked(m_multiScreenEnabled);
    connect(multiScreenAct, &QAction::toggled, this, &FloatingBall::setMultiScreenEnabled);
    connect(menu, &QMenu::aboutToShow, this, [this, multiScreenAct] {
        const QSignalBlocker b(multiScreenAct);
        multiScreenAct->setChecked(m_multiScreenEnabled);
    });
    menu->addAction(tr("恢复全部屏幕悬浮球"), this, &FloatingBall::restoreAllScreenBalls);
    menu->addAction(tr("从系统 Dock 同步常驻应用"), this, [this] {
        const int count = dockModel()->importFromSystemDock();
        DesktopActions::showToast(tr("✓ 已同步 %1 个系统 Dock 应用").arg(count), anchorCenterGlobal().toPoint());
    });
    menu->addAction(tr("设置…"), this, &FloatingBall::showSettings);
    menu->addAction(tr("退出"), qApp, &QApplication::quit);
    m_trayIcon->setContextMenu(menu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            setBallVisible(!isVisible());
    });
    m_trayIcon->show();
}

void FloatingBall::setBallVisible(bool visible)
{
    if (m_primaryPeer) {
        m_primaryPeer->setBallVisible(visible);
        return;
    }
    m_userHiddenAll = !visible;
    if (visible) {
        if (!m_hiddenOnThisScreen) {
            expandFromEdge();
            show();
            raise();
            refreshBallOpacity();
        }
        syncScreenReplicas();
        for (FloatingBall *rep : replicaBalls()) {
            if (!rep->m_hiddenOnThisScreen) {
                rep->expandFromEdge();
                rep->show();
                rep->raise();
                rep->refreshBallOpacity();
            }
        }
        refreshSystemAudioState();
    } else {
        closeFan(true);
        hide();
        for (FloatingBall *rep : replicaBalls()) {
            rep->closeFan(true);
            rep->hide();
        }
    }
}

void FloatingBall::showContextMenu(const QPoint &globalPos)
{
    m_menuOpen = true;
    refreshBallOpacity();
    QMenu menu(this);
#ifdef Q_OS_LINUX
    menu.addAction(tr("截屏 / 录屏"), this, &FloatingBall::captureRegion);
#else
    menu.addAction(tr("区域截图"), this, &FloatingBall::captureRegion);
    menu.addAction(tr("屏幕录制"), this, &FloatingBall::recordScreen);
#endif
    menu.addAction(tr("触控剪贴板"), this, &FloatingBall::toggleClipboardBubble);
    menu.addAction(tr("触控快捷甲板"), this, &FloatingBall::toggleQuickDeck);
    menu.addAction(tr("通知中心"), this, [this] { openSystemPopover(SystemPopover::TabNotifications); });
    menu.addSeparator();

    if (m_dockSide != DockFree) {
        if (m_edgeTucked) {
            menu.addAction(tr("恢复正圆双行时钟主球"), this, [this] {
                m_manualTucked = false;
                expandFromEdge();
            });
        } else {
            menu.addAction(tr("临时靠边收成竖胶囊（看视频 / 游戏免打扰）"), this, [this] {
                tuckToEdgeCapsule(true, true);
            });
        }
    }

    auto *statusOrbAction = menu.addAction(tr("状态仪双行 24H 时钟主球（替代系统状态栏）"));
    statusOrbAction->setCheckable(true);
    statusOrbAction->setChecked(m_statusOrbMode);
    connect(statusOrbAction, &QAction::toggled, this, &FloatingBall::setStatusOrbMode);
    if (!m_attentionAnglesRad.isEmpty() || (m_statusMonitor && m_statusMonitor->snapshot().hasNotificationDot())) {
        menu.addAction(tr("清除未读消息方位红点"), this, &FloatingBall::clearNotificationDot);
    }
    menu.addSeparator();
    if (QGuiApplication::screens().size() > 1) {
        menu.addAction(tr("在此屏幕隐藏悬浮球"), this, [this] { setHiddenOnThisScreen(true); });
        menu.addAction(tr("恢复全部屏幕悬浮球"), this, &FloatingBall::restoreAllScreenBalls);
    }
    menu.addAction(tr("设置…"), this, &FloatingBall::showSettings);
    if (m_trayIcon || (m_primaryPeer && m_primaryPeer->m_trayIcon))
        menu.addAction(tr("隐藏悬浮球"), this, [this] { setBallVisible(false); });
    menu.addSeparator();
    menu.addAction(tr("退出悬浮球"), qApp, &QApplication::quit);
    FloatingWindow::execPopupMenu(&menu, globalPos);
    m_menuOpen = false;
    refreshBallOpacity();
}

void FloatingBall::showSettings()
{
    if (m_primaryPeer) {
        m_primaryPeer->showSettings();
        return;
    }
    if (m_settingsOpen) return;
    const QScopedValueRollback<bool> guard(m_settingsOpen, true);
    expandFromEdge();
    closeFan(true);
    m_idleTimer->stop();

    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("floatingballSettings"));
    dialog.setWindowTitle(tr("FloatingBall · 多层悬浮球与悬浮 Dock 设置"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);

    // Tab 1: Appearance, Multi-Ring & Edge Ring
    auto *appearance = new QWidget(&dialog);
    auto *appForm = new QFormLayout(appearance);

    auto *dockCheck = new QCheckBox(tr("外圈自动展开所有已打开窗口（同应用稳定聚类，鼠标中键快关）"), appearance);
    dockCheck->setObjectName(QStringLiteral("dockModeCheck"));
    dockCheck->setChecked(m_dockModeEnabled);

    auto *statusOrbCheck = new QCheckBox(tr("启用正圆双行 24H 时钟主球 + 方向消息雷达点（替代传统状态栏）"), appearance);
    statusOrbCheck->setObjectName(QStringLiteral("statusOrbCheck"));
    statusOrbCheck->setChecked(m_statusOrbMode);

    auto *multiScreenCheck = new QCheckBox(tr("多显示器每屏独立显示悬浮球（点开内容一致，各屏位置与展开方向独立）"), appearance);
    multiScreenCheck->setObjectName(QStringLiteral("multiScreenCheck"));
    multiScreenCheck->setChecked(m_multiScreenEnabled);

    auto *diameter = new QSpinBox(appearance);
    diameter->setObjectName(QStringLiteral("ballDiameter"));
    diameter->setRange(kMinDiameter, kMaxDiameter);
    diameter->setValue(qRound(m_preferredDiameter));
    diameter->setSuffix(QStringLiteral(" px"));

    auto *idle = new QSpinBox(appearance);
    idle->setRange(15, 100);
    idle->setValue(qRound(m_idleOpacity * 100));
    idle->setSuffix(QStringLiteral(" %"));

    auto *active = new QSpinBox(appearance);
    active->setRange(40, 100);
    active->setValue(qRound(m_activeOpacity * 100));
    active->setSuffix(QStringLiteral(" %"));

    auto *panels = new QSpinBox(appearance);
    panels->setObjectName(QStringLiteral("panelOpacity"));
    panels->setRange(15, 88);
    panels->setValue(qRound(FloatingWindow::panelOpacity() * 100));
    panels->setSuffix(QStringLiteral(" %"));

    auto *blur = new QCheckBox(tr("原生毛玻璃背景（macOS Liquid Glass）"), appearance);
    blur->setObjectName(QStringLiteral("panelBlur"));
    blur->setEnabled(FloatingWindow::panelBlurSupported());
    blur->setChecked(blur->isEnabled() && FloatingWindow::panelBlurEnabled());

    auto *snap = new QCheckBox(tr("松手自动贴靠屏幕四边（上 / 下 / 左 / 右）"), appearance);
    snap->setChecked(m_snapEnabled);

    auto *autoHide = new QCheckBox(tr("闲置自动靠边形变为竖胶囊（默认关闭 · 常驻显示正圆时钟主球）"), appearance);
    autoHide->setChecked(m_autoHideEnabled);

    auto *autoTuckFs = new QCheckBox(tr("全屏看视频 / 打游戏时自动靠边形变为竖胶囊（退出全屏自动恢复圆球）"), appearance);
    autoTuckFs->setChecked(m_autoTuckFullscreen);

    auto *crosshairPull = new QCheckBox(tr("拉出最外层环一键唤起系统截屏（Spectacle / macshot）"), appearance);
    crosshairPull->setChecked(m_crosshairPullEnabled);

    appForm->addRow(dockCheck);
    appForm->addRow(statusOrbCheck);
    appForm->addRow(multiScreenCheck);
    appForm->addRow(tr("悬浮球尺寸"), diameter);
    appForm->addRow(tr("闲置透明度"), idle);
    appForm->addRow(tr("激活透明度"), active);
    appForm->addRow(tr("浮卡暗色罩层"), panels);
    appForm->addRow(blur);
    appForm->addRow(snap);
    appForm->addRow(autoHide);
    appForm->addRow(autoTuckFs);
    appForm->addRow(crosshairPull);

    auto *gestureGuide = new QLabel(
        tr("交互提示：\n"
           "• 最内圈固定 5 个系统总控按钮；第 2 层起自动把已打开窗口分层展开，第 2 层末端固定「关闭当前窗口」红球（任意窗口也支持鼠标中键快关）\n"
           "• 有新消息的窗口会在正圆主球边缘对应方位亮起红点，点开主球顺着方向一眼命中\n"
           "• 向屏幕外边框方向轻推主球可手动临时收成竖胶囊；全屏看视频/游戏会自动靠边形变"),
        appearance);
    gestureGuide->setWordWrap(true);
    appForm->addRow(gestureGuide);
    tabs->addTab(appearance, tr("形态与手势"));

    // Tab 2: Inner Ring (Ring 1) 5-Slot Actions
    auto *fanPage = new QWidget(&dialog);
    auto *fanForm = new QFormLayout(fanPage);
    QList<QComboBox *> slotCombos;
    const QStringList slotNames{
        tr("内圈槽位 1（最上）"),
        tr("内圈槽位 2（中上）"),
        tr("内圈槽位 3（正中 C 位）"),
        tr("内圈槽位 4（中下）"),
        tr("内圈槽位 5（最下）")
    };
    for (int i = 0; i < kOrbitSlotCount; ++i) {
        auto *combo = new QComboBox(fanPage);
        combo->setObjectName(QStringLiteral("orbitAction%1").arg(i));
        for (const QString &id : availableActions())
            combo->addItem(actionLabel(id), id);
        combo->setCurrentIndex(combo->findData(m_orbitActions.value(i)));
        slotCombos.append(combo);
        fanForm->addRow(slotNames[i], combo);
    }
    auto *fanHint = new QLabel(tr("内圈固定为 5 个高频系统触控动作；第 2 层起自动展示你的悬浮 Dock 应用。"), fanPage);
    fanHint->setWordWrap(true);
    fanForm->addRow(fanHint);
    tabs->addTab(fanPage, tr("内圈五槽动作"));

    // Tab 3: Floating Dock Pinned Apps Manager
    auto *dockPage = new QWidget(&dialog);
    auto *dockLayout = new QVBoxLayout(dockPage);
    auto *dockDesc = new QLabel(
        tr("固定在悬浮 Dock 中的应用（排在第 2~10 层前列；其余正在运行的桌面应用也会自动追加在后面）："), dockPage);
    dockDesc->setWordWrap(true);
    dockLayout->addWidget(dockDesc);

    auto *pinnedList = new QListWidget(dockPage);
    pinnedList->setObjectName(QStringLiteral("dockPinnedList"));
    auto populatePinned = [pinnedList](const QStringList &targets) {
        pinnedList->clear();
        for (const QString &t : targets) {
            auto *it = new QListWidgetItem(AppDockModel::displayNameForTarget(t) + QStringLiteral("  (") + t + QStringLiteral(")"));
            it->setData(Qt::UserRole, t);
            pinnedList->addItem(it);
        }
    };
    populatePinned(dockModel()->pinnedTargets());
    dockLayout->addWidget(pinnedList, 1);

    auto *dockBtnRow = new QHBoxLayout();
    auto *syncDockBtn = new QPushButton(tr("从系统 Dock 导入"), dockPage);
    auto *addAppBtn = new QPushButton(tr("添加应用…"), dockPage);
    auto *upBtn = new QPushButton(tr("上移"), dockPage);
    auto *downBtn = new QPushButton(tr("下移"), dockPage);
    auto *removeBtn = new QPushButton(tr("移除"), dockPage);
    for (QPushButton *b : {syncDockBtn, addAppBtn, upBtn, downBtn, removeBtn}) dockBtnRow->addWidget(b);
    dockLayout->addLayout(dockBtnRow);

    connect(syncDockBtn, &QPushButton::clicked, &dialog, [this, populatePinned] {
        dockModel()->importFromSystemDock();
        populatePinned(dockModel()->pinnedTargets());
    });
    connect(addAppBtn, &QPushButton::clicked, &dialog, [&dialog, pinnedList] {
#ifdef Q_OS_MACOS
        const QString picked = QFileDialog::getExistingDirectory(
            &dialog, tr("选择要固定到悬浮 Dock 的应用 (.app)"), QStringLiteral("/Applications"));
#else
        const QString picked = QFileDialog::getOpenFileName(
            &dialog, tr("选择应用 (.desktop)"), QStringLiteral("/usr/share/applications"),
            QStringLiteral("Desktop Entries (*.desktop);;All Files (*)"));
#endif
        if (picked.isEmpty()) return;
        for (int i = 0; i < pinnedList->count(); ++i) {
            if (pinnedList->item(i)->data(Qt::UserRole).toString() == picked) return;
        }
        auto *it = new QListWidgetItem(AppDockModel::displayNameForTarget(picked) + QStringLiteral("  (") + picked + QStringLiteral(")"));
        it->setData(Qt::UserRole, picked);
        pinnedList->addItem(it);
    });
    connect(upBtn, &QPushButton::clicked, &dialog, [pinnedList] {
        const int row = pinnedList->currentRow();
        if (row <= 0) return;
        QListWidgetItem *it = pinnedList->takeItem(row);
        pinnedList->insertItem(row - 1, it);
        pinnedList->setCurrentRow(row - 1);
    });
    connect(downBtn, &QPushButton::clicked, &dialog, [pinnedList] {
        const int row = pinnedList->currentRow();
        if (row < 0 || row >= pinnedList->count() - 1) return;
        QListWidgetItem *it = pinnedList->takeItem(row);
        pinnedList->insertItem(row + 1, it);
        pinnedList->setCurrentRow(row + 1);
    });
    connect(removeBtn, &QPushButton::clicked, &dialog, [pinnedList] {
        const int row = pinnedList->currentRow();
        if (row >= 0) delete pinnedList->takeItem(row);
    });
    tabs->addTab(dockPage, tr("悬浮 Dock 应用"));

    auto *status = new QLabel(&dialog);
    status->setStyleSheet(QStringLiteral("color:#e05268;"));
    status->setWordWrap(true);
    layout->addWidget(status);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QAbstractButton::clicked, &dialog, [=] {
        const QStringList defs = defaultOrbitActions();
        for (int i = 0; i < kOrbitSlotCount; ++i)
            slotCombos[i]->setCurrentIndex(slotCombos[i]->findData(defs[i]));
        dockCheck->setChecked(true);
        statusOrbCheck->setChecked(true);
        multiScreenCheck->setChecked(true);
        diameter->setValue(60);
        idle->setValue(82);
        active->setValue(94);
        panels->setValue(58);
        blur->setChecked(blur->isEnabled());
        snap->setChecked(true);
        autoHide->setChecked(false);
        autoTuckFs->setChecked(true);
        crosshairPull->setChecked(true);
        status->clear();
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this] {
        QStringList chosen;
        for (QComboBox *c : slotCombos) chosen.append(c->currentData().toString());
        if (QSet<QString>(chosen.begin(), chosen.end()).size() != kOrbitSlotCount) {
            status->setText(tr("内圈请选择五个互不重复的快捷动作。"));
            return;
        }
        if (active->value() < idle->value()) {
            status->setText(tr("激活透明度需大于或等于闲置透明度。"));
            return;
        }
        m_dockModeEnabled = dockCheck->isChecked();
        setStatusOrbMode(statusOrbCheck->isChecked());
        m_multiScreenEnabled = multiScreenCheck->isChecked();
        m_snapEnabled = snap->isChecked();
        m_autoHideEnabled = autoHide->isChecked();
        m_autoTuckFullscreen = autoTuckFs->isChecked();
        m_fullscreenTucked = (m_autoTuckFullscreen && dockModel()->activeWindowFullscreen());
        m_crosshairPullEnabled = crosshairPull->isChecked();
        m_idleOpacity = idle->value() / 100.0;
        m_activeOpacity = active->value() / 100.0;
        FloatingWindow::setPanelOpacity(panels->value() / 100.0);
        if (blur->isEnabled()) FloatingWindow::setPanelBlurEnabled(blur->isChecked());
        updateBallGeometry(QSizeF(diameter->value() * m_screenScale, diameter->value() * m_screenScale),
                           QPointF(), false, false);
        setOrbitActions(chosen);

        QStringList newPinned;
        for (int i = 0; i < pinnedList->count(); ++i)
            newPinned.append(pinnedList->item(i)->data(Qt::UserRole).toString());
        dockModel()->setPinnedTargets(newPinned);

        if (!m_snapEnabled) m_dockSide = 0;
        else snapToEdge(false);
        savePreferences();
        syncScreenReplicas();
        broadcastSharedPreferences();
        dialog.accept();
    });

    dialog.resize(520, 480);
    dialog.exec();
    refreshBallOpacity();
}
