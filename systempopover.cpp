#include "systempopover.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "systemstatus.h"
#include "uiicons.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <algorithm>

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

namespace {

class PopoverDragHeader final : public QWidget {
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

QScrollArea *makeTouchScrollArea(QWidget *parent, QVBoxLayout **outInnerLayout)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: 0; }"
        "QScrollBar:vertical { width: 6px; background: transparent; margin: 2px 0; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,48); border-radius: 3px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));
    auto *content = new QWidget(scroll);
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *v = new QVBoxLayout(content);
    v->setContentsMargins(0, 0, 4, 0);
    v->setSpacing(6);
    v->addStretch(1);
    scroll->setWidget(content);
    if (outInnerLayout) *outInnerLayout = v;
    return scroll;
}

QString btDeviceEmoji(const QString &iconType, const QString &name)
{
    const QString lo = (iconType + QLatin1Char(' ') + name).toLower();
    if (lo.contains(QStringLiteral("mouse"))) return QStringLiteral("🖱");
    if (lo.contains(QStringLiteral("keyboard"))) return QStringLiteral("⌨");
    if (lo.contains(QStringLiteral("audio")) || lo.contains(QStringLiteral("head")) ||
        lo.contains(QStringLiteral("ear")) || name.contains(QStringLiteral("助听")))
        return QStringLiteral("🎧");
    if (lo.contains(QStringLiteral("gaming")) || lo.contains(QStringLiteral("xbox")) ||
        lo.contains(QStringLiteral("controller")))
        return QStringLiteral("🎮");
    if (lo.contains(QStringLiteral("phone"))) return QStringLiteral("📱");
    return QStringLiteral("ᛒ");
}

} // namespace

SystemPopover::SystemPopover(SystemStatusMonitor *monitor, QWidget *parent)
    : QWidget(parent),
      m_monitor(monitor),
      m_net(new QNetworkAccessManager(this))
{
    setObjectName(QStringLiteral("systemPopover"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 网络 / 蓝牙 / Mihomo"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(386, 448);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 8);
    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("sysPopoverCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#sysPopoverCard { background:@panelTint@; border:1px solid rgba(255,255,255,40); border-radius:22px; }"
        "QLabel { color:#aab6c8; font-size:11px; background:transparent; }"
        "QLabel#popTitle { color:#f5f8fd; font-size:15px; font-weight:600; }"
        "QLabel#popSub { color:#8e9cb2; font-size:11px; }"
        "QLabel#statusBanner { color:#7cc8ff; font-size:11px; font-weight:500; padding:2px 4px; }"
        "QToolButton#tabBtn { background:rgba(255,255,255,10); color:#b8c4d8; border:1px solid rgba(255,255,255,16);"
        "                     border-radius:12px; padding:6px 10px; font-size:12px; font-weight:600; }"
        "QToolButton#tabBtn[active=\"true\"] { background:rgba(59,130,246,165); color:#ffffff; border-color:rgba(124,200,255,180); }"
        "QToolButton#tabBtn:hover { background:rgba(255,255,255,22); }"
        "QToolButton#powerSwitch { background:rgba(255,255,255,14); color:#cbd5e1; border:1px solid rgba(255,255,255,26);"
        "                          border-radius:14px; padding:6px 12px; font-size:12px; font-weight:600; }"
        "QToolButton#powerSwitch[on=\"true\"] { background:rgba(34,197,94,115); color:#ffffff; border-color:rgba(110,231,183,165); }"
        "QToolButton#actionPill { background:rgba(255,255,255,14); color:#e2e8f0; border:1px solid rgba(255,255,255,22);"
        "                         border-radius:12px; padding:6px 10px; font-size:12px; font-weight:500; }"
        "QToolButton#actionPill:hover { background:rgba(255,255,255,26); }"
        "QToolButton#modePill { background:rgba(255,255,255,12); color:#cbd5e1; border:1px solid rgba(255,255,255,18);"
        "                       border-radius:12px; padding:7px 8px; font-size:12px; font-weight:600; }"
        "QToolButton#modePill[active=\"true\"] { background:rgba(56,189,248,155); color:#ffffff; border-color:rgba(125,211,252,200); }"
        "QPushButton#rowBtn { background:rgba(255,255,255,11); color:#f1f5f9; border:1px solid rgba(255,255,255,18);"
        "                     border-radius:14px; padding:10px 12px; font-size:13px; text-align:left; }"
        "QPushButton#rowBtn:hover { background:rgba(255,255,255,22); border-color:rgba(124,200,255,110); }"
        "QPushButton#rowBtn[connected=\"true\"] { background:rgba(56,130,246,72); border-color:rgba(124,200,255,155); }"
        "QToolButton#popClose { background:transparent; border:0; border-radius:14px; }");

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(9);

    // Header (draggable)
    auto *header = new PopoverDragHeader(card);
    header->setCursor(Qt::OpenHandCursor);
    auto *hRow = new QHBoxLayout(header);
    hRow->setContentsMargins(0, 0, 0, 0);
    auto *titleLbl = new QLabel(QStringLiteral("快捷连接与代理"), header);
    titleLbl->setObjectName(QStringLiteral("popTitle"));
    titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);
    hRow->addWidget(titleLbl);
    hRow->addStretch(1);

    auto *refreshBtn = new QToolButton(header);
    refreshBtn->setObjectName(QStringLiteral("actionPill"));
    refreshBtn->setText(QStringLiteral("刷新"));
    connect(refreshBtn, &QToolButton::clicked, this, &SystemPopover::refreshCurrentTab);
    hRow->addWidget(refreshBtn);

    auto *closeBtn = new QToolButton(header);
    closeBtn->setObjectName(QStringLiteral("popClose"));
    closeBtn->setFixedSize(28, 28);
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#cbd5e1"), 16));
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    hRow->addWidget(closeBtn);
    root->addWidget(header);

    // 3-Tab Switcher (Wi-Fi | 蓝牙 | Mihomo)
    auto *tabRow = new QHBoxLayout();
    tabRow->setSpacing(6);
    static const char *kTabTitles[3]{"Wi-Fi", "蓝牙", "Mihomo"};
    static const FloatingIcons::Icon kTabIcons[3]{FloatingIcons::Wifi, FloatingIcons::Bluetooth, FloatingIcons::Proxy};
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QToolButton(card);
        btn->setObjectName(QStringLiteral("tabBtn"));
        btn->setText(QString::fromUtf8(kTabTitles[i]));
        btn->setIcon(FloatingIcons::make(kTabIcons[i], QColor("#e2e8f0"), 15));
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumHeight(34);
        connect(btn, &QToolButton::clicked, this, [this, i] { selectTab(static_cast<Tab>(i)); });
        m_tabBtns[i] = btn;
        tabRow->addWidget(btn);
    }
    root->addLayout(tabRow);

    m_statusBanner = new QLabel(card);
    m_statusBanner->setObjectName(QStringLiteral("statusBanner"));
    m_statusBanner->hide();
    root->addWidget(m_statusBanner);

    m_stack = new QStackedWidget(card);
    m_stack->setStyleSheet(QStringLiteral("background: transparent;"));
    root->addWidget(m_stack, 1);

    // ========================================================================
    // Page 0: Wi-Fi
    // ========================================================================
    {
        auto *page = new QWidget(m_stack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 2, 0, 0);
        v->setSpacing(8);

        auto *top = new QHBoxLayout();
        m_wifiSummaryLbl = new QLabel(QStringLiteral("正在扫描 Wi-Fi…"), page);
        m_wifiSummaryLbl->setObjectName(QStringLiteral("popSub"));
        m_wifiSummaryLbl->setWordWrap(true);
        top->addWidget(m_wifiSummaryLbl, 1);
        m_wifiPowerBtn = new QToolButton(page);
        m_wifiPowerBtn->setObjectName(QStringLiteral("powerSwitch"));
        m_wifiPowerBtn->setText(QStringLiteral("Wi-Fi 已开"));
        m_wifiPowerBtn->setProperty("on", true);
        connect(m_wifiPowerBtn, &QToolButton::clicked, this, &SystemPopover::toggleWifiRadio);
        top->addWidget(m_wifiPowerBtn);
        v->addLayout(top);

        v->addWidget(makeTouchScrollArea(page, &m_wifiListLayout), 1);

        auto *foot = new QToolButton(page);
        foot->setObjectName(QStringLiteral("actionPill"));
        foot->setText(QStringLiteral("打开 KDE 完整网络 / VPN 设置…"));
        foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        foot->setMinimumHeight(36);
        connect(foot, &QToolButton::clicked, this, [this] {
            hide();
            if (m_monitor) m_monitor->openKdeModule(QStringLiteral("kcm_networkmanagement"));
        });
        v->addWidget(foot);
        m_stack->addWidget(page);
    }

    // ========================================================================
    // Page 1: Bluetooth (Live Paired Devices + Battery % + Per-Device Connect)
    // ========================================================================
    {
        auto *page = new QWidget(m_stack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 2, 0, 0);
        v->setSpacing(8);

        auto *top = new QHBoxLayout();
        m_btSummaryLbl = new QLabel(QStringLiteral("正在读取已配对蓝牙设备…"), page);
        m_btSummaryLbl->setObjectName(QStringLiteral("popSub"));
        top->addWidget(m_btSummaryLbl, 1);
        m_btPowerBtn = new QToolButton(page);
        m_btPowerBtn->setObjectName(QStringLiteral("powerSwitch"));
        m_btPowerBtn->setText(QStringLiteral("蓝牙已开"));
        m_btPowerBtn->setProperty("on", true);
        connect(m_btPowerBtn, &QToolButton::clicked, this, &SystemPopover::toggleBluetoothPower);
        top->addWidget(m_btPowerBtn);
        v->addLayout(top);

        v->addWidget(makeTouchScrollArea(page, &m_btListLayout), 1);

        auto *foot = new QToolButton(page);
        foot->setObjectName(QStringLiteral("actionPill"));
        foot->setText(QStringLiteral("配对新设备 / 打开 KDE 蓝牙设置…"));
        foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        foot->setMinimumHeight(36);
        connect(foot, &QToolButton::clicked, this, [this] {
            hide();
            if (m_monitor) m_monitor->openKdeModule(QStringLiteral("kcm_bluetooth"));
        });
        v->addWidget(foot);
        m_stack->addWidget(page);
    }

    // ========================================================================
    // Page 2: Mihomo Proxy Controller (Rule/Global/Direct + TUN + Groups/Nodes + Delay)
    // ========================================================================
    {
        auto *page = new QWidget(m_stack);
        auto *v = new QVBoxLayout(page);
        v->setContentsMargins(0, 2, 0, 0);
        v->setSpacing(8);

        // Mode switcher row
        auto *modeRow = new QHBoxLayout();
        modeRow->setSpacing(6);
        static const char *kModes[3]{"rule", "global", "direct"};
        static const char *kModeLabels[3]{"规则 Rule", "全局 Global", "直连 Direct"};
        for (int i = 0; i < 3; ++i) {
            auto *b = new QToolButton(page);
            b->setObjectName(QStringLiteral("modePill"));
            b->setText(QString::fromUtf8(kModeLabels[i]));
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            b->setMinimumHeight(34);
            const QString modeKey = QString::fromLatin1(kModes[i]);
            connect(b, &QToolButton::clicked, this, [this, modeKey] { setMihomoMode(modeKey); });
            m_mhModeBtns[i] = b;
            modeRow->addWidget(b);
        }
        v->addLayout(modeRow);

        // TUN & Delay & Summary row
        auto *subRow = new QHBoxLayout();
        subRow->setSpacing(6);
        m_mhSummaryLbl = new QLabel(QStringLiteral("连接 Mihomo 中…"), page);
        m_mhSummaryLbl->setObjectName(QStringLiteral("popSub"));
        subRow->addWidget(m_mhSummaryLbl, 1);

        m_mhTunBtn = new QToolButton(page);
        m_mhTunBtn->setObjectName(QStringLiteral("powerSwitch"));
        m_mhTunBtn->setText(QStringLiteral("TUN"));
        connect(m_mhTunBtn, &QToolButton::clicked, this, &SystemPopover::toggleMihomoTun);
        subRow->addWidget(m_mhTunBtn);

        m_mhDelayBtn = new QToolButton(page);
        m_mhDelayBtn->setObjectName(QStringLiteral("actionPill"));
        m_mhDelayBtn->setText(QStringLiteral("测速"));
        connect(m_mhDelayBtn, &QToolButton::clicked, this, [this] {
            if (!m_mhSelectedGroup.isEmpty()) testMihomoGroupDelay(m_mhSelectedGroup);
        });
        subRow->addWidget(m_mhDelayBtn);
        v->addLayout(subRow);

        // Group selector pills container
        auto *groupBarHost = new QWidget(page);
        m_mhGroupBarLayout = new QVBoxLayout(groupBarHost);
        m_mhGroupBarLayout->setContentsMargins(0, 0, 0, 0);
        m_mhGroupBarLayout->setSpacing(4);
        v->addWidget(groupBarHost);

        v->addWidget(makeTouchScrollArea(page, &m_mhNodeListLayout), 1);

        auto *foot = new QToolButton(page);
        foot->setObjectName(QStringLiteral("actionPill"));
        foot->setText(QStringLiteral("打开 Mihomo Web 面板 (MetacubeXD)…"));
        foot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        foot->setMinimumHeight(36);
        connect(foot, &QToolButton::clicked, this, [this] {
            hide();
            QDesktopServices::openUrl(QUrl(mihomoBaseUrl() + QStringLiteral("/ui/")));
        });
        v->addWidget(foot);
        m_stack->addWidget(page);
    }

    FloatingWindow::prepare(this, false);
}

void SystemPopover::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void SystemPopover::positionNear(const QPoint &anchorGlobal)
{
    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    setProperty("floatingballAssignedScreen", QVariant::fromValue(screen));
    const QRect b = screen->availableGeometry();
    const int margin = 12;
    int x = anchorGlobal.x() - width() / 2;
    int y = anchorGlobal.y() + 36;
    if (y + height() > b.bottom() - margin) y = anchorGlobal.y() - height() - 36;
    x = qBound(b.left() + margin, x, qMax(b.left() + margin, b.right() - width() - margin));
    y = qBound(b.top() + margin, y, qMax(b.top() + margin, b.bottom() - height() - margin));
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
    QTimer::singleShot(2600, this, [banner, msg] {
        if (banner && banner->text() == msg) banner->hide();
    });
}

void SystemPopover::revealTab(Tab tab, const QPoint &anchorGlobal)
{
    if (isVisible() && m_currentTab == tab) {
        hide();
        return;
    }
    positionNear(anchorGlobal);
    show();
    raise();
    selectTab(tab);
}

void SystemPopover::selectTab(Tab tab)
{
    m_currentTab = tab;
    m_stack->setCurrentIndex(static_cast<int>(tab));
    for (int i = 0; i < 3; ++i) {
        if (!m_tabBtns[i]) continue;
        m_tabBtns[i]->setProperty("active", i == static_cast<int>(tab));
        m_tabBtns[i]->style()->unpolish(m_tabBtns[i]);
        m_tabBtns[i]->style()->polish(m_tabBtns[i]);
    }
    refreshCurrentTab();
}

void SystemPopover::refreshCurrentTab()
{
    if (m_currentTab == TabWifi) refreshWifi();
    else if (m_currentTab == TabBluetooth) refreshBluetooth();
    else refreshMihomo();
}

// ============================================================================
// Wi-Fi Tab Implementation (nmcli async — zero GUI blocking)
// ============================================================================

void SystemPopover::refreshWifi()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) {
        m_wifiSummaryLbl->setText(QStringLiteral("未找到 nmcli"));
        return;
    }
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
                // IN-USE:SSID:SIGNAL:SECURITY (note escaped \: inside SSIDs)
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
    proc->start(QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"),
                 QStringLiteral("nmcli radio wifi; echo '---ACTIVE---'; "
                                "nmcli -t -f NAME,TYPE connection show --active; echo '---WIFI---'; "
                                "nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan no")});
#endif
}

void SystemPopover::rebuildWifiList(bool wifiEnabled, const QStringList &vpnLinks, const QList<WifiEntry> &entries)
{
    m_wifiRadioEnabled = wifiEnabled;
    m_wifiPowerBtn->setText(wifiEnabled ? QStringLiteral("Wi-Fi 已开") : QStringLiteral("Wi-Fi 已关"));
    m_wifiPowerBtn->setProperty("on", wifiEnabled);
    m_wifiPowerBtn->style()->unpolish(m_wifiPowerBtn);
    m_wifiPowerBtn->style()->polish(m_wifiPowerBtn);

    QString activeSsid;
    for (const WifiEntry &e : entries) {
        if (e.active) { activeSsid = e.ssid; break; }
    }
    QString summary = !wifiEnabled ? QStringLiteral("无线开关已关闭")
        : (activeSsid.isEmpty() ? QStringLiteral("未连接 Wi-Fi") : QStringLiteral("已连 %1").arg(activeSsid));
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
        auto *lbl = new QLabel(QStringLiteral("附近暂未发现 Wi-Fi 热点"), this);
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
    QTimer::singleShot(1100, this, [this] { refreshWifi(); });
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
            self->refreshWifi();
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
// Bluetooth Tab Implementation (BlueZ DBus GetManagedObjects + Battery1!)
// ============================================================================

void SystemPopover::refreshBluetooth()
{
#ifdef Q_OS_LINUX
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
    m_btPowerBtn->style()->unpolish(m_btPowerBtn);
    m_btPowerBtn->style()->polish(m_btPowerBtn);

    int connectedCount = 0;
    for (const BluetoothDevice &d : devices) if (d.connected) ++connectedCount;
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
// Mihomo REST API Tab Implementation (127.0.0.1:9090, optional Bearer secret)
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
            self->m_mhSummaryLbl->setText(QStringLiteral("Mihomo 未响应 (%1)").arg(self->mihomoBaseUrl()));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        self->m_mhMode = obj.value(QStringLiteral("mode")).toString(QStringLiteral("rule")).toLower();
        const QJsonObject tun = obj.value(QStringLiteral("tun")).toObject();
        self->m_mhTunEnabled = tun.value(QStringLiteral("enable")).toBool();
        self->m_mhTunDevice = tun.value(QStringLiteral("device")).toString(QStringLiteral("TUN"));
        self->rebuildMihomoView();
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
        const bool active = (m_mhMode == QLatin1String(kModes[i]));
        m_mhModeBtns[i]->setProperty("active", active);
        m_mhModeBtns[i]->style()->unpolish(m_mhModeBtns[i]);
        m_mhModeBtns[i]->style()->polish(m_mhModeBtns[i]);
    }

    m_mhTunBtn->setText(m_mhTunEnabled ? QStringLiteral("TUN 开") : QStringLiteral("TUN 关"));
    m_mhTunBtn->setProperty("on", m_mhTunEnabled);
    m_mhTunBtn->style()->unpolish(m_mhTunBtn);
    m_mhTunBtn->style()->polish(m_mhTunBtn);

    m_mhSummaryLbl->setText(QStringLiteral("模式 %1 · %2 · %3 个策略组")
                                .arg(m_mhMode.toUpper(),
                                     m_mhTunEnabled ? (m_mhTunDevice.isEmpty() ? QStringLiteral("TUN") : m_mhTunDevice) : QStringLiteral("无TUN"))
                                .arg(m_mhGroups.size()));

    // Rebuild strategy group pills
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

    // Rebuild node rows for selected group
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
