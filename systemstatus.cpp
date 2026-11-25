#include "systemstatus.h"
#include "desktopactions.h"
#include "uiicons.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QWindow>
#include <cstring>
#include <memory>
#include <csignal>
#include <sys/prctl.h>
#ifdef FLOATINGBALL_WAYLAND_CLIENT
#include <wayland-client.h>
#ifdef FLOATINGBALL_QT_GUI_PRIVATE
#include <qpa/qplatformnativeinterface.h>
#endif
#endif
#endif

#ifdef Q_OS_MACOS
// SystemStatusMac declarations live in systemstatus.h
#endif

namespace {

bool snapshotsEqual(const SystemStatusSnapshot &a, const SystemStatusSnapshot &b)
{
    return a.brightnessAvailable == b.brightnessAvailable &&
           a.brightnessPercent == b.brightnessPercent &&
           a.volumeAvailable == b.volumeAvailable &&
           a.volumePercent == b.volumePercent &&
           a.volumeMuted == b.volumeMuted &&
           a.batteryPresent == b.batteryPresent &&
           a.batteryPercent == b.batteryPercent &&
           a.batteryPluggedIn == b.batteryPluggedIn &&
           a.networkConnected == b.networkConnected &&
           a.signalQuality == b.signalQuality &&
           a.networkText == b.networkText &&
           a.inputPresent == b.inputPresent &&
           a.inputLabel == b.inputLabel &&
           a.micActive == b.micActive &&
           a.micUsers == b.micUsers &&
           a.cameraActive == b.cameraActive &&
           a.cameraUsers == b.cameraUsers &&
           a.unreadNotifications == b.unreadNotifications &&
           a.trayNeedsAttention == b.trayNeedsAttention &&
           a.lastNotificationSummary == b.lastNotificationSummary &&
           a.virtualDesktops == b.virtualDesktops &&
           a.bluetoothAvailable == b.bluetoothAvailable &&
           a.bluetoothPowered == b.bluetoothPowered &&
           a.dndEnabled == b.dndEnabled &&
           a.nightLightEnabled == b.nightLightEnabled &&
           a.powerProfile == b.powerProfile;
}

#ifdef Q_OS_LINUX
class SafeStatusProcess final : public QProcess {
public:
    explicit SafeStatusProcess(QObject *parent = nullptr) : QProcess(parent)
    {
        setChildProcessModifier([] {
            ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        });
    }
    ~SafeStatusProcess() override
    {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(250);
        }
    }
};

QString mapFcitxToLabel(const QString &im)
{
    if (im.isEmpty()) return {};
    if (im.startsWith(QLatin1String("keyboard-"), Qt::CaseInsensitive) ||
        im.compare(QStringLiteral("us"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("EN");
    return QStringLiteral("双");
}

QList<VirtualDesktopInfo> parseKWinDesktopsVariant(const QVariant &v, const QString &currentId)
{
    QList<VirtualDesktopInfo> result;
    if (!v.canConvert<QDBusArgument>()) return result;
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        uint pos = 0;
        QString id;
        QString name;
        arg >> pos >> id >> name;
        arg.endStructure();
        VirtualDesktopInfo info;
        info.id = id;
        info.name = name.isEmpty() ? QString::number(pos + 1) : name;
        info.current = (!currentId.isEmpty() && id == currentId);
        result.append(info);
    }
    arg.endArray();
    return result;
}

QIcon decodeSniPixmapVariant(const QVariant &v)
{
    if (!v.isValid() || !v.canConvert<QDBusArgument>()) return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    QImage bestImg;
    int bestWidth = 0;
    arg.beginArray();
    while (!arg.atEnd()) {
        int w = 0, h = 0;
        QByteArray bytes;
        arg.beginStructure();
        arg >> w >> h >> bytes;
        arg.endStructure();
        if (w <= 0 || h <= 0 || bytes.size() < w * h * 4) continue;
        if (w > bestWidth && w <= 256) {
            QImage img(w, h, QImage::Format_ARGB32);
            const auto *src = reinterpret_cast<const uchar *>(bytes.constData());
            for (int y = 0; y < h; ++y) {
                auto *dst = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const int off = (y * w + x) * 4;
                    dst[x] = qRgba(src[off + 1], src[off + 2], src[off + 3], src[off]);
                }
            }
            bestImg = img;
            bestWidth = w;
        }
    }
    arg.endArray();
    if (bestImg.isNull()) return {};
    return QIcon(QPixmap::fromImage(bestImg));
}

QString parseSniToolTipTitle(const QVariant &v)
{
    if (!v.isValid() || !v.canConvert<QDBusArgument>()) return {};
    const QDBusArgument arg = v.value<QDBusArgument>();
    QString iconName;
    QString title;
    QString subtitle;
    arg.beginStructure();
    arg >> iconName;
    // Skip pixmap array a(iiay)
    arg.beginArray();
    while (!arg.atEnd()) {
        int w = 0, h = 0;
        QByteArray bytes;
        arg.beginStructure();
        arg >> w >> h >> bytes;
        arg.endStructure();
    }
    arg.endArray();
    arg >> title >> subtitle;
    arg.endStructure();
    return title.trimmed();
}

QIcon resolveSniIcon(const QString &iconName, const QString &iconThemePath, const QVariant &pixmapVar)
{
    auto tryPathFile = [](const QString &basePath, const QString &name) -> QIcon {
        if (basePath.isEmpty() || name.isEmpty()) return {};
        static const QStringList exts{QStringLiteral(".svg"), QStringLiteral(".png"), QStringLiteral(".xpm"), QStringLiteral("")};
        for (const QString &ext : exts) {
            const QString cand = basePath + QLatin1Char('/') + name + ext;
            if (QFileInfo::exists(cand)) {
                QIcon ico(cand);
                if (!ico.isNull()) return ico;
            }
        }
        return {};
    };

    if (!iconThemePath.isEmpty() && !iconName.isEmpty()) {
        QIcon ico = tryPathFile(iconThemePath, iconName);
        if (!ico.isNull()) return ico;
        for (const QString &sub : {QStringLiteral("/hicolor/scalable/apps"), QStringLiteral("/hicolor/48x48/apps"),
                                   QStringLiteral("/hicolor/32x32/apps"), QStringLiteral("/hicolor/24x24/apps"),
                                   QStringLiteral("/hicolor/22x22/apps")}) {
            ico = tryPathFile(iconThemePath + sub, iconName);
            if (!ico.isNull()) return ico;
        }
    }

    if (!iconName.isEmpty()) {
        if (QFileInfo::exists(iconName)) {
            QIcon ico(iconName);
            if (!ico.isNull()) return ico;
        }
        QIcon themed = QIcon::fromTheme(iconName);
        if (!themed.isNull()) return themed;
        if (iconName.endsWith(QLatin1String("-symbolic"))) {
            const QString stripped = iconName.left(iconName.size() - 9);
            themed = QIcon::fromTheme(stripped);
            if (!themed.isNull()) return themed;
        }
        // Scan NixOS/Breeze/hicolor paths directly for status & app icons
        const QString user = qEnvironmentVariable("USER");
        const QStringList roots{
            QDir::homePath() + QStringLiteral("/.local/share/icons"),
            QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/icons"),
            QStringLiteral("/run/current-system/sw/share/icons"),
            QStringLiteral("/run/current-system/sw/share/pixmaps"),
            QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/pixmaps")
        };
        static const QStringList subpaths{
            QStringLiteral(""),
            QStringLiteral("/hicolor/scalable/apps"),
            QStringLiteral("/hicolor/48x48/apps"),
            QStringLiteral("/hicolor/64x64/apps"),
            QStringLiteral("/hicolor/128x128/apps"),
            QStringLiteral("/hicolor/256x256/apps"),
            QStringLiteral("/hicolor/512x512/apps"),
            QStringLiteral("/breeze-dark/status/22"),
            QStringLiteral("/breeze-dark/status/24"),
            QStringLiteral("/breeze-dark/apps/48"),
            QStringLiteral("/breeze-dark/apps/32"),
            QStringLiteral("/breeze-dark/apps/22"),
            QStringLiteral("/breeze/status/22"),
            QStringLiteral("/breeze/apps/48"),
            QStringLiteral("/breeze/apps/32")
        };
        const QStringList namesToTry = iconName.endsWith(QLatin1String("-symbolic"))
            ? QStringList{iconName, iconName.left(iconName.size() - 9)}
            : QStringList{iconName};
        for (const QString &nm : namesToTry) {
            for (const QString &root : roots) {
                for (const QString &sub : subpaths) {
                    QIcon ico = tryPathFile(root + sub, nm);
                    if (!ico.isNull()) return ico;
                }
            }
        }
    }

    QIcon pixIco = decodeSniPixmapVariant(pixmapVar);
    if (!pixIco.isNull()) return pixIco;
    return FloatingIcons::make(FloatingIcons::TrayHub, QColor("#dce8f8"), 28);
}

void sendDBusMenuEvent(const QString &service, const QString &menuPath, int itemId)
{
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        service, menuPath, QStringLiteral("com.canonical.dbusmenu"), QStringLiteral("Event"));
    msg.setArguments({itemId, QStringLiteral("clicked"),
                      QVariant::fromValue(QDBusVariant(0)),
                      static_cast<uint>(QDateTime::currentSecsSinceEpoch())});
    bus.asyncCall(msg, 400);
}

void parseDBusMenuNode(const QDBusArgument &arg, QMenu *parentMenu,
                       const QString &service, const QString &menuPath, bool isRoot)
{
    int id = 0;
    QVariantMap props;
    QList<QDBusVariant> children;
    arg.beginStructure();
    arg >> id >> props;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant childVar;
        arg >> childVar;
        children.append(childVar);
    }
    arg.endArray();
    arg.endStructure();

    const bool visible = props.value(QStringLiteral("visible"), true).toBool();
    if (!isRoot && !visible) return;

    if (!isRoot) {
        const QString type = props.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("separator")) {
            parentMenu->addSeparator();
            return;
        }
        QString label = props.value(QStringLiteral("label")).toString();
        // Clean GTK/DBusMenu mnemonic underscores ("_Quit" -> "Quit", "__" -> "_")
        label.replace(QStringLiteral("__"), QStringLiteral("\x01"));
        label.remove(QLatin1Char('_'));
        label.replace(QStringLiteral("\x01"), QStringLiteral("_"));
        if (label.isEmpty() && children.isEmpty()) return;

        const bool enabled = props.value(QStringLiteral("enabled"), true).toBool();
        const QString iconName = props.value(QStringLiteral("icon-name")).toString();
        const QIcon itemIcon = iconName.isEmpty() ? QIcon() : QIcon::fromTheme(iconName);

        if (!children.isEmpty() || props.value(QStringLiteral("children-display")).toString() == QLatin1String("submenu")) {
            QMenu *sub = parentMenu->addMenu(itemIcon, label);
            sub->setEnabled(enabled);
            for (const QDBusVariant &cv : children) {
                if (cv.variant().canConvert<QDBusArgument>())
                    parseDBusMenuNode(cv.variant().value<QDBusArgument>(), sub, service, menuPath, false);
            }
        } else {
            QAction *act = parentMenu->addAction(itemIcon, label);
            act->setEnabled(enabled);
            const QString toggleType = props.value(QStringLiteral("toggle-type")).toString();
            if (toggleType == QLatin1String("checkmark") || toggleType == QLatin1String("radio")) {
                act->setCheckable(true);
                act->setChecked(props.value(QStringLiteral("toggle-state")).toInt() == 1);
            }
            QObject::connect(act, &QAction::triggered, parentMenu, [service, menuPath, id] {
                sendDBusMenuEvent(service, menuPath, id);
            });
        }
        return;
    }

    for (const QDBusVariant &cv : children) {
        if (cv.variant().canConvert<QDBusArgument>())
            parseDBusMenuNode(cv.variant().value<QDBusArgument>(), parentMenu, service, menuPath, false);
    }
}

void collectDBusMenuEntries(const QDBusArgument &arg, QList<TrayMenuEntry> *out, bool isRoot)
{
    int id = 0;
    QVariantMap props;
    QList<QDBusVariant> children;
    arg.beginStructure();
    arg >> id >> props;
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusVariant childVar;
        arg >> childVar;
        children.append(childVar);
    }
    arg.endArray();
    arg.endStructure();

    const bool visible = props.value(QStringLiteral("visible"), true).toBool();
    if (!isRoot && !visible) return;

    if (!isRoot) {
        const QString type = props.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("separator")) return;
        QString label = props.value(QStringLiteral("label")).toString();
        label.replace(QStringLiteral("__"), QStringLiteral("\x01"));
        label.remove(QLatin1Char('_'));
        label.replace(QStringLiteral("\x01"), QStringLiteral("_"));
        label = label.trimmed();
        const bool enabled = props.value(QStringLiteral("enabled"), true).toBool();
        if (!label.isEmpty() && enabled && out->size() < 10) {
            TrayMenuEntry e;
            e.id = id;
            e.label = label;
            e.enabled = enabled;
            const QString iconName = props.value(QStringLiteral("icon-name")).toString();
            if (!iconName.isEmpty()) e.icon = QIcon::fromTheme(iconName);
            const QString toggleType = props.value(QStringLiteral("toggle-type")).toString();
            if (toggleType == QLatin1String("checkmark") || toggleType == QLatin1String("radio")) {
                e.checkable = true;
                e.checked = (props.value(QStringLiteral("toggle-state")).toInt() == 1);
            }
            out->append(e);
        }
        return;
    }

    for (const QDBusVariant &cv : children) {
        if (cv.variant().canConvert<QDBusArgument>())
            collectDBusMenuEntries(cv.variant().value<QDBusArgument>(), out, false);
    }
}
#endif

} // namespace

SystemStatusMonitor::SystemStatusMonitor(QObject *parent) : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(5000);
    connect(m_pollTimer, &QTimer::timeout, this, &SystemStatusMonitor::pollSlowMetrics);
}

SystemStatusMonitor::~SystemStatusMonitor()
{
    stopPlatformWatchers();
}

void SystemStatusMonitor::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    if (m_enabled) {
        startPlatformWatchers();
        QPointer<SystemStatusMonitor> self(this);
        QTimer::singleShot(80, this, [self] {
            if (self && self->m_enabled) self->refreshNow();
        });
    } else {
        stopPlatformWatchers();
    }
}

void SystemStatusMonitor::startPlatformWatchers()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
#ifdef Q_OS_LINUX
    initLinuxDBus();
    startNotificationMonitor();
#endif
    m_pollTimer->start();
}

void SystemStatusMonitor::stopPlatformWatchers()
{
    if (m_pollTimer) m_pollTimer->stop();
#ifdef Q_OS_LINUX
    stopNotificationMonitor();
#endif
}

void SystemStatusMonitor::clearNotifications()
{
    if (m_snapshot.unreadNotifications == 0 && m_snapshot.lastNotificationSummary.isEmpty()) return;
    SystemStatusSnapshot next = m_snapshot;
    next.unreadNotifications = 0;
    next.lastNotificationSummary.clear();
    emitIfChanged(next);
}

void SystemStatusMonitor::updateAudio(int percent, bool muted, bool available)
{
    SystemStatusSnapshot next = m_snapshot;
    next.volumeAvailable = available;
    next.volumePercent = qBound(0, percent, 100);
    next.volumeMuted = muted;
    emitIfChanged(next);
}

void SystemStatusMonitor::updateBrightness(int percent, bool available)
{
    SystemStatusSnapshot next = m_snapshot;
    next.brightnessAvailable = available;
    next.brightnessPercent = qBound(0, percent, 100);
    emitIfChanged(next);
}

void SystemStatusMonitor::setSnapshotForTest(const SystemStatusSnapshot &snapshot)
{
    emitIfChanged(snapshot);
}

void SystemStatusMonitor::setTrayItemsForTest(const QList<TrayItemInfo> &items)
{
    m_trayItems = items;
    emit trayItemsChanged();
}

void SystemStatusMonitor::emitIfChanged(const SystemStatusSnapshot &next)
{
    if (snapshotsEqual(m_snapshot, next)) return;
    m_snapshot = next;
    emit statusChanged(m_snapshot);
}

bool SystemStatusMonitor::switchVirtualDesktop(int index, bool *alreadyCurrent, QString *labelOut)
{
    if (alreadyCurrent) *alreadyCurrent = false;
    if (labelOut) labelOut->clear();
#ifdef Q_OS_LINUX
    if (index < 0) return false;
    if (index < m_snapshot.virtualDesktops.size()) {
        const VirtualDesktopInfo &vd = m_snapshot.virtualDesktops[index];
        if (labelOut) *labelOut = vd.name;
        if (vd.current) {
            if (alreadyCurrent) *alreadyCurrent = true;
            return true;
        }
        auto session = QDBusConnection::sessionBus();
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"),
            QStringLiteral("/VirtualDesktopManager"),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("Set"));
        msg.setArguments({
            QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
            QStringLiteral("current"),
            QVariant::fromValue(QDBusVariant(vd.id))
        });
        session.asyncCall(msg);
        // Optimistically update current highlight immediately for instant visual feedback
        SystemStatusSnapshot next = m_snapshot;
        for (int i = 0; i < next.virtualDesktops.size(); ++i)
            next.virtualDesktops[i].current = (i == index);
        emitIfChanged(next);
        return true;
    }
    // Fallback if desktops list wasn't populated yet: invoke KWin shortcut "Switch to Desktop N"
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg.setArguments({QStringLiteral("Switch to Desktop %1").arg(index + 1)});
    session.asyncCall(msg);
    if (labelOut) *labelOut = QStringLiteral("桌面 %1").arg(index + 1);
    return true;
#else
    Q_UNUSED(index);
    return false;
#endif
}

void SystemStatusMonitor::refreshNow()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
    QPointer<SystemStatusMonitor> self(this);
    DesktopActions::queryVolume([self](int vol, bool muted, bool ok) {
        if (!self) return;
        self->updateAudio(vol, muted, ok);
    });
    pollSlowMetrics();
}

void SystemStatusMonitor::pollSlowMetrics()
{
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
#ifdef Q_OS_LINUX
    refreshLinuxBrightness();
    refreshLinuxBattery();
    refreshLinuxNetwork();
    refreshLinuxInputMethod();
    refreshLinuxCamera();
    refreshLinuxMic();
    refreshLinuxVirtualDesktops();
    refreshLinuxSniTray();
    refreshLinuxNightLight();
    refreshLinuxDnd();
    refreshLinuxPowerProfile();
    refreshLinuxBluetooth();
#elif defined(Q_OS_MACOS)
    SystemStatusSnapshot next = m_snapshot;
    SystemStatusMac::populateSnapshot(&next);
    emitIfChanged(next);
#endif
}

#ifdef Q_OS_LINUX

void SystemStatusMonitor::initLinuxDBus()
{
    if (m_dbusConnected) return;
    m_dbusConnected = true;

    auto session = QDBusConnection::sessionBus();
    auto system = QDBusConnection::systemBus();

    // 1. KDE Solid BrightnessControl signal
    session.connect(
        QStringLiteral("org.kde.Solid.PowerManagement"),
        QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
        QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
        QStringLiteral("brightnessChanged"),
        this, SLOT(refreshLinuxBrightness()));

    // 2. mynix CameraStatus signal
    session.connect(
        QStringLiteral("org.mynix.CameraStatus"),
        QStringLiteral("/org/mynix/CameraStatus"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshLinuxCamera()));

    // 3. KWin VirtualDesktopManager + NightLight signals
    session.connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        QStringLiteral("currentChanged"),
        this, SLOT(refreshLinuxVirtualDesktops()));
    session.connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        QStringLiteral("desktopDataChanged"),
        this, SLOT(refreshLinuxVirtualDesktops()));
    session.connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/NightLight"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshLinuxNightLight()));

    // 3b. StatusNotifierWatcher register/unregister signals (live tray sync!)
    session.connect(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("StatusNotifierItemRegistered"),
        this, SLOT(refreshLinuxSniTray()));
    session.connect(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("StatusNotifierItemUnregistered"),
        this, SLOT(refreshLinuxSniTray()));

    // 3c. PowerProfiles signal
    system.connect(
        QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("/org/freedesktop/UPower/PowerProfiles"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshLinuxPowerProfile()));

    // 4. UPower battery signal
    system.connect(
        QStringLiteral("org.freedesktop.UPower"),
        QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshLinuxBattery()));

    // 5. NetworkManager state/active-connection change
    system.connect(
        QStringLiteral("org.freedesktop.NetworkManager"),
        QStringLiteral("/org/freedesktop/NetworkManager"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshLinuxNetwork()));
}

void SystemStatusMonitor::refreshLinuxBrightness()
{
    auto session = QDBusConnection::sessionBus();
    auto maxMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Solid.PowerManagement"),
        QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
        QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
        QStringLiteral("brightnessMax"));
    auto *wMax = new QDBusPendingCallWatcher(session.asyncCall(maxMsg, 600), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(wMax, &QDBusPendingCallWatcher::finished, this, [self, wMax] {
        const QDBusMessage maxReply = wMax->reply();
        wMax->deleteLater();
        if (!self) return;
        if (maxReply.type() == QDBusMessage::ReplyMessage && !maxReply.arguments().isEmpty()) {
            const int maxVal = maxReply.arguments().first().toInt();
            if (maxVal > 0) {
                self->m_brightnessMax = maxVal;
                auto curMsg = QDBusMessage::createMethodCall(
                    QStringLiteral("org.kde.Solid.PowerManagement"),
                    QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                    QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                    QStringLiteral("brightness"));
                auto *wCur = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(curMsg, 600), self);
                connect(wCur, &QDBusPendingCallWatcher::finished, self, [self, wCur, maxVal] {
                    const QDBusMessage curReply = wCur->reply();
                    wCur->deleteLater();
                    if (!self) return;
                    if (curReply.type() == QDBusMessage::ReplyMessage && !curReply.arguments().isEmpty()) {
                        const int cur = curReply.arguments().first().toInt();
                        self->updateBrightness(qBound(0, qRound(cur * 100.0 / maxVal), 100), true);
                    }
                });
                return;
            }
        }
        // Fallback: /sys/class/backlight/*
        QDir bl(QStringLiteral("/sys/class/backlight"));
        for (const QString &dev : bl.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile fCur(bl.filePath(dev + QStringLiteral("/brightness")));
            QFile fMax(bl.filePath(dev + QStringLiteral("/max_brightness")));
            if (fCur.open(QIODevice::ReadOnly) && fMax.open(QIODevice::ReadOnly)) {
                const int cur = fCur.readAll().trimmed().toInt();
                const int mx = fMax.readAll().trimmed().toInt();
                if (mx > 0) {
                    self->updateBrightness(qBound(0, qRound(cur * 100.0 / mx), 100), true);
                    return;
                }
            }
        }
        self->updateBrightness(0, false);
    });
}

void SystemStatusMonitor::refreshLinuxBattery()
{
    auto system = QDBusConnection::systemBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UPower"),
        QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg.setArguments({QStringLiteral("org.freedesktop.UPower.Device")});
    auto *w = new QDBusPendingCallWatcher(system.asyncCall(msg, 600), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusPendingReply<QVariantMap> reply(*w);
        w->deleteLater();
        if (!self || reply.isError()) return;
        const QVariantMap props = reply.value();
        SystemStatusSnapshot next = self->m_snapshot;
        next.batteryPresent = props.value(QStringLiteral("IsPresent")).toBool();
        next.batteryPercent = qBound(0, qRound(props.value(QStringLiteral("Percentage")).toDouble()), 100);
        const uint state = props.value(QStringLiteral("State")).toUInt();
        next.batteryPluggedIn = (state == 1 || state == 4 || state == 5);
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxNetwork()
{
    auto system = QDBusConnection::systemBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.NetworkManager"),
        QStringLiteral("/org/freedesktop/NetworkManager"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.freedesktop.NetworkManager"), QStringLiteral("ActiveConnections")});
    auto *w = new QDBusPendingCallWatcher(system.asyncCall(msg, 600), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return;

        const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
        const auto activePaths = qdbus_cast<QList<QDBusObjectPath>>(dv.variant());
        if (activePaths.isEmpty()) {
            SystemStatusSnapshot next = self->m_snapshot;
            next.networkConnected = false;
            next.signalQuality = 0;
            next.networkText = QStringLiteral("未联网");
            self->emitIfChanged(next);
            return;
        }

        struct NetGather {
            int pending = 0;
            int bestPrio = 0;
            int bestQuality = 0;
            QString bestText = QStringLiteral("未联网");
        };
        auto gather = std::make_shared<NetGather>();
        gather->pending = activePaths.size();

        auto finishOne = [self, gather]() {
            if (--gather->pending > 0 || !self) return;
            SystemStatusSnapshot next = self->m_snapshot;
            next.networkConnected = (gather->bestPrio > 0);
            next.signalQuality = gather->bestPrio > 0 ? gather->bestQuality : 0;
            next.networkText = gather->bestText;
            self->emitIfChanged(next);
        };

        auto sysBus = QDBusConnection::systemBus();
        for (const QDBusObjectPath &p : activePaths) {
            auto acMsg = QDBusMessage::createMethodCall(
                QStringLiteral("org.freedesktop.NetworkManager"),
                p.path(),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("GetAll"));
            acMsg.setArguments({QStringLiteral("org.freedesktop.NetworkManager.Connection.Active")});
            auto *acW = new QDBusPendingCallWatcher(sysBus.asyncCall(acMsg, 600), self);
            connect(acW, &QDBusPendingCallWatcher::finished, self, [self, acW, gather, finishOne] {
                const QDBusPendingReply<QVariantMap> acReply(*acW);
                acW->deleteLater();
                if (!self || acReply.isError()) { finishOne(); return; }
                const QVariantMap props = acReply.value();
                if (props.value(QStringLiteral("State")).toUInt() != 2) { finishOne(); return; }
                const QString type = props.value(QStringLiteral("Type")).toString();
                const QString id = props.value(QStringLiteral("Id")).toString();
                const bool isDefault = props.value(QStringLiteral("Default")).toBool() ||
                                       props.value(QStringLiteral("Default6")).toBool();
                const int defaultBoost = isDefault ? 10 : 0;
                if (type == QLatin1String("802-3-ethernet")) {
                    const int prio = 3 + defaultBoost;
                    if (prio > gather->bestPrio) {
                        gather->bestPrio = prio;
                        gather->bestQuality = 100;
                        gather->bestText = id.isEmpty() ? QStringLiteral("有线网络") : (QStringLiteral("有线 · ") + id);
                    }
                    finishOne();
                } else if (type == QLatin1String("802-11-wireless")) {
                    const auto apPath = qdbus_cast<QDBusObjectPath>(props.value(QStringLiteral("SpecificObject")));
                    if (!apPath.path().isEmpty() && apPath.path() != QLatin1String("/")) {
                        auto apMsg = QDBusMessage::createMethodCall(
                            QStringLiteral("org.freedesktop.NetworkManager"),
                            apPath.path(),
                            QStringLiteral("org.freedesktop.DBus.Properties"),
                            QStringLiteral("Get"));
                        apMsg.setArguments({QStringLiteral("org.freedesktop.NetworkManager.AccessPoint"),
                                            QStringLiteral("Strength")});
                        auto *apW = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(apMsg, 500), self);
                        connect(apW, &QDBusPendingCallWatcher::finished, self, [apW, gather, id, isDefault, defaultBoost, finishOne] {
                            int strength = 75;
                            const QDBusMessage apReply = apW->reply();
                            apW->deleteLater();
                            if (apReply.type() == QDBusMessage::ReplyMessage && !apReply.arguments().isEmpty()) {
                                const QDBusVariant v = apReply.arguments().first().value<QDBusVariant>();
                                strength = qBound(0, static_cast<int>(v.variant().toUInt()), 100);
                            }
                            // Local AP / Hotspot connections report Strength=0 and are not default-route:
                            // never let a local hotspot override the real 4G/Ethernet upstream signal!
                            if (!isDefault && strength == 0) {
                                finishOne();
                                return;
                            }
                            const int displayStrength = (isDefault && strength == 0) ? 20 : strength;
                            const int prio = 2 + defaultBoost;
                            if (prio > gather->bestPrio || (prio == gather->bestPrio && displayStrength > gather->bestQuality)) {
                                gather->bestPrio = prio;
                                gather->bestQuality = displayStrength;
                                gather->bestText = (id.isEmpty() ? QStringLiteral("Wi-Fi") : id) +
                                                   QStringLiteral(" (%1%)").arg(displayStrength);
                            }
                            finishOne();
                        });
                    } else if (isDefault) {
                        const int prio = 2 + defaultBoost;
                        if (prio > gather->bestPrio) {
                            gather->bestPrio = prio;
                            gather->bestQuality = 75;
                            gather->bestText = id.isEmpty() ? QStringLiteral("Wi-Fi") : id;
                        }
                        finishOne();
                    } else {
                        finishOne();
                    }
                } else if (type == QLatin1String("gsm") || type == QLatin1String("cdma")) {
                    const int prio = 1 + defaultBoost;
                    if (prio > gather->bestPrio) {
                        gather->bestPrio = prio;
                        gather->bestQuality = 80;
                        gather->bestText = id.isEmpty() ? QStringLiteral("4G 移动网络") : id;
                    }
                    finishOne();
                } else {
                    finishOne();
                }
            });
        }
    });
}

void SystemStatusMonitor::refreshLinuxInputMethod()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.fcitx.Fcitx5"),
        QStringLiteral("/controller"),
        QStringLiteral("org.fcitx.Fcitx.Controller1"),
        QStringLiteral("CurrentInputMethod"));
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 400), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self) return;
        SystemStatusSnapshot next = self->m_snapshot;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QString im = reply.arguments().first().toString();
            next.inputLabel = mapFcitxToLabel(im);
            next.inputPresent = !next.inputLabel.isEmpty();
            next.inputDescription = (next.inputLabel == QLatin1String("双")) ? QStringLiteral("双拼") : QStringLiteral("English");
        } else {
            next.inputPresent = false;
            next.inputLabel.clear();
        }
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxCamera()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.mynix.CameraStatus"),
        QStringLiteral("/org/mynix/CameraStatus"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.mynix.CameraStatus"), QStringLiteral("Users")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 400), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self) return;
        SystemStatusSnapshot next = self->m_snapshot;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
            next.cameraUsers = dv.variant().toStringList();
            next.cameraActive = !next.cameraUsers.isEmpty();
        } else {
            next.cameraUsers.clear();
            next.cameraActive = false;
        }
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxVirtualDesktops()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg.setArguments({QStringLiteral("org.kde.KWin.VirtualDesktopManager")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 500), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusPendingReply<QVariantMap> reply(*w);
        w->deleteLater();
        if (!self || reply.isError()) return;
        const QVariantMap props = reply.value();
        const QString currentId = props.value(QStringLiteral("current")).toString();
        const QList<VirtualDesktopInfo> list =
            parseKWinDesktopsVariant(props.value(QStringLiteral("desktops")), currentId);
        if (list.isEmpty()) return;
        SystemStatusSnapshot next = self->m_snapshot;
        next.virtualDesktops = list;
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxMic()
{
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (pactl.isEmpty()) return;

    auto *proc = new SafeStatusProcess(this);
    QPointer<SystemStatusMonitor> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self || status != QProcess::NormalExit || code != 0) return;

        QStringList users;
        const QStringList blocks = out.split(QStringLiteral("Source Output #"), Qt::SkipEmptyParts);
        for (const QString &blk : blocks) {
            if (blk.contains(QStringLiteral("Corked: yes"), Qt::CaseInsensitive)) continue;
            if (blk.contains(QStringLiteral(".monitor"), Qt::CaseInsensitive)) continue;
            if (blk.contains(QStringLiteral("pavucontrol"), Qt::CaseInsensitive) ||
                blk.contains(QStringLiteral("plasma-pa"), Qt::CaseInsensitive) ||
                blk.contains(QStringLiteral("Peak detect"), Qt::CaseInsensitive))
                continue;
            QRegularExpression nameRe(QStringLiteral("application\\.name = \"([^\"]+)\""));
            const auto m = nameRe.match(blk);
            const QString appName = m.hasMatch() ? m.captured(1) : QStringLiteral("录音应用");
            if (!users.contains(appName)) users.append(appName);
        }

        SystemStatusSnapshot next = self->m_snapshot;
        next.micUsers = users;
        next.micActive = !users.isEmpty();
        self->emitIfChanged(next);
    });
    proc->start(pactl, {QStringLiteral("list"), QStringLiteral("source-outputs")});
}

void SystemStatusMonitor::refreshLinuxSniTray()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.kde.StatusNotifierWatcher"),
                      QStringLiteral("RegisteredStatusNotifierItems")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 600), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return;

        const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
        const QStringList items = dv.variant().toStringList();
        if (items.isEmpty()) {
            if (!self->m_trayItems.isEmpty()) {
                self->m_trayItems.clear();
                emit self->trayItemsChanged();
            }
            SystemStatusSnapshot next = self->m_snapshot;
            next.trayNeedsAttention = false;
            self->emitIfChanged(next);
            return;
        }

        struct SniGather {
            int pending = 0;
            bool attention = false;
            QVector<TrayItemInfo> collected;
            QVector<bool> valid;
        };
        auto gather = std::make_shared<SniGather>();
        gather->pending = items.size();
        gather->collected.resize(items.size());
        gather->valid.fill(false, items.size());

        auto finishOne = [self, gather]() {
            if (--gather->pending > 0 || !self) return;
            QList<TrayItemInfo> result;
            for (int i = 0; i < gather->collected.size(); ++i) {
                if (gather->valid[i]) result.append(gather->collected[i]);
            }
            self->m_trayItems = result;
            emit self->trayItemsChanged();

            SystemStatusSnapshot next = self->m_snapshot;
            next.trayNeedsAttention = gather->attention;
            self->emitIfChanged(next);
        };

        auto bus = QDBusConnection::sessionBus();
        for (int idx = 0; idx < items.size(); ++idx) {
            const QString &entry = items[idx];
            const int slash = entry.indexOf(QLatin1Char('/'));
            if (slash <= 0) { finishOne(); continue; }
            const QString svc = entry.left(slash);
            const QString path = entry.mid(slash);

            auto sMsg = QDBusMessage::createMethodCall(
                svc, path,
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("GetAll"));
            sMsg.setArguments({QStringLiteral("org.kde.StatusNotifierItem")});
            auto *sw = new QDBusPendingCallWatcher(bus.asyncCall(sMsg, 600), self);
            connect(sw, &QDBusPendingCallWatcher::finished, self, [sw, gather, finishOne, idx, svc, path] {
                const QDBusPendingReply<QVariantMap> r(*sw);
                sw->deleteLater();
                if (!r.isError()) {
                    const QVariantMap props = r.value();
                    const QString id = props.value(QStringLiteral("Id")).toString();
                    const QString rawTitle = props.value(QStringLiteral("Title")).toString();
                    if (id.compare(QStringLiteral("FloatingBall"), Qt::CaseInsensitive) != 0 &&
                        rawTitle.compare(QStringLiteral("FloatingBall"), Qt::CaseInsensitive) != 0) {
                        TrayItemInfo info;
                        info.service = svc;
                        info.objectPath = path;
                        info.id = id;
                        const QString tipTitle = parseSniToolTipTitle(props.value(QStringLiteral("ToolTip")));
                        info.title = !tipTitle.isEmpty() ? tipTitle : (!rawTitle.isEmpty() ? rawTitle : id);
                        if (info.title.isEmpty()) info.title = QStringLiteral("托盘图标");
                        info.status = props.value(QStringLiteral("Status"), QStringLiteral("Active")).toString();
                        info.itemIsMenu = props.value(QStringLiteral("ItemIsMenu")).toBool();
                        if (props.contains(QStringLiteral("Menu"))) {
                            const QVariant mv = props.value(QStringLiteral("Menu"));
                            if (mv.canConvert<QDBusObjectPath>())
                                info.menuPath = mv.value<QDBusObjectPath>().path();
                            else
                                info.menuPath = mv.toString();
                        }
                        const QString iconName = props.value(QStringLiteral("IconName")).toString();
                        const QString themePath = props.value(QStringLiteral("IconThemePath")).toString();
                        info.icon = resolveSniIcon(iconName, themePath, props.value(QStringLiteral("IconPixmap")));
                        if (info.status == QLatin1String("NeedsAttention"))
                            gather->attention = true;
                        gather->collected[idx] = info;
                        gather->valid[idx] = true;
                    }
                }
                finishOne();
            });
        }
    });
}

void SystemStatusMonitor::refreshLinuxNightLight()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/org/kde/KWin/NightLight"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("GetAll"));
    msg.setArguments({QStringLiteral("org.kde.KWin.NightLight")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 400), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusPendingReply<QVariantMap> r(*w);
        w->deleteLater();
        if (!self || r.isError()) return;
        const QVariantMap props = r.value();
        SystemStatusSnapshot next = self->m_snapshot;
        next.nightLightEnabled = props.value(QStringLiteral("enabled")).toBool() ||
                                 props.value(QStringLiteral("running")).toBool();
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxDnd()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"),
        QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Inhibited")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 400), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return;
        const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
        SystemStatusSnapshot next = self->m_snapshot;
        next.dndEnabled = dv.variant().toBool();
        self->emitIfChanged(next);
    });
}

void SystemStatusMonitor::refreshLinuxPowerProfile()
{
    auto system = QDBusConnection::systemBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UPower.PowerProfiles"),
        QStringLiteral("/org/freedesktop/UPower/PowerProfiles"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.freedesktop.UPower.PowerProfiles"), QStringLiteral("ActiveProfile")});
    auto *w = new QDBusPendingCallWatcher(system.asyncCall(msg, 400), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self) return;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
            SystemStatusSnapshot next = self->m_snapshot;
            next.powerProfile = dv.variant().toString();
            self->emitIfChanged(next);
            return;
        }
        const QString pp = QStandardPaths::findExecutable(QStringLiteral("powerprofilesctl"));
        if (pp.isEmpty()) return;
        auto *proc = new SafeStatusProcess(self);
        connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), self,
                [self, proc](int code, QProcess::ExitStatus st) {
            const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            proc->deleteLater();
            if (!self || st != QProcess::NormalExit || code != 0 || out.isEmpty()) return;
            SystemStatusSnapshot next = self->m_snapshot;
            next.powerProfile = out;
            self->emitIfChanged(next);
        });
        proc->start(pp, {QStringLiteral("get")});
    });
}

void SystemStatusMonitor::refreshLinuxBluetooth()
{
    const QString bt = QStandardPaths::findExecutable(QStringLiteral("bluetoothctl"));
    if (bt.isEmpty()) return;
    auto *proc = new SafeStatusProcess(this);
    QPointer<SystemStatusMonitor> self(this);
    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [self, proc](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput());
        proc->deleteLater();
        if (!self || st != QProcess::NormalExit || code != 0) return;
        SystemStatusSnapshot next = self->m_snapshot;
        next.bluetoothAvailable = out.contains(QStringLiteral("Powered:"));
        next.bluetoothPowered = out.contains(QStringLiteral("Powered: yes"));
        self->emitIfChanged(next);
    });
    proc->start(bt, {QStringLiteral("show")});
}

void SystemStatusMonitor::startNotificationMonitor()
{
    if (m_notifMonitor) return;
    const QString dbusMonitor = QStandardPaths::findExecutable(QStringLiteral("dbus-monitor"));
    if (dbusMonitor.isEmpty()) return;

    m_notifMonitor = new SafeStatusProcess(this);
    connect(m_notifMonitor, &QProcess::readyReadStandardOutput, this, [this] {
        const QString chunk = QString::fromUtf8(m_notifMonitor->readAllStandardOutput());
        if (chunk.contains(QStringLiteral("member=Notify"))) {
            SystemStatusSnapshot next = m_snapshot;
            next.unreadNotifications = qMin(99, next.unreadNotifications + 1);
            QRegularExpression strRe(QStringLiteral("string \"([^\"]{1,48})\""));
            auto it = strRe.globalMatch(chunk);
            QString appName;
            if (it.hasNext()) appName = it.next().captured(1);
            next.lastNotificationSummary = appName.isEmpty()
                ? QStringLiteral("收到新通知 (%1)").arg(QTime::currentTime().toString(QStringLiteral("HH:mm")))
                : QStringLiteral("%1 发来新通知").arg(appName);
            emitIfChanged(next);
        }
    });
    m_notifMonitor->start(
        dbusMonitor,
        {QStringLiteral("--session"),
         QStringLiteral("type='method_call',interface='org.freedesktop.Notifications',member='Notify'")});
}

void SystemStatusMonitor::stopNotificationMonitor()
{
    if (!m_notifMonitor) return;
    m_notifMonitor->kill();
    m_notifMonitor->waitForFinished(200);
    m_notifMonitor->deleteLater();
    m_notifMonitor = nullptr;
}

#endif // Q_OS_LINUX

#ifdef Q_OS_LINUX
namespace {

QString requestWaylandActivationToken(QWidget *sourceWidget)
{
#if defined(FLOATINGBALL_WAYLAND_CLIENT) && QT_CONFIG(wayland)
    if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland")))
        return {};
    auto *wlApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!wlApp) return {};
    wl_display *display = wlApp->display();
    if (!display) return {};
    wl_seat *seat = wlApp->lastInputSeat();
    if (!seat) seat = wlApp->seat();
    const uint32_t serial = wlApp->lastInputSerial();

    wl_surface *surface = nullptr;
#ifdef FLOATINGBALL_QT_GUI_PRIVATE
    if (sourceWidget && sourceWidget->windowHandle() && QGuiApplication::platformNativeInterface()) {
        surface = static_cast<wl_surface *>(
            QGuiApplication::platformNativeInterface()->nativeResourceForWindow(
                QByteArrayLiteral("surface"), sourceWidget->windowHandle()));
    }
#else
    Q_UNUSED(sourceWidget);
#endif

    static const wl_interface *kEmptyTypes[] = {nullptr};
    static const wl_interface *kSetSerialTypes[] = {nullptr, &wl_seat_interface};
    static const wl_interface *kSetSurfaceTypes[] = {&wl_surface_interface};
    static const wl_message kTokenRequests[] = {
        {"set_serial", "uo", kSetSerialTypes},
        {"set_app_id", "s", kEmptyTypes},
        {"set_surface", "o", kSetSurfaceTypes},
        {"commit", "", kEmptyTypes},
        {"destroy", "", kEmptyTypes},
    };
    static const wl_message kTokenEvents[] = {
        {"done", "s", kEmptyTypes},
    };
    static const wl_interface kXdgActivationTokenV1Interface = {
        "xdg_activation_token_v1", 1, 5, kTokenRequests, 1, kTokenEvents
    };
    static const wl_interface *kGetTokenTypes[] = {&kXdgActivationTokenV1Interface};
    static const wl_interface *kActivateTypes[] = {nullptr, &wl_surface_interface};
    static const wl_message kActivationRequests[] = {
        {"destroy", "", kEmptyTypes},
        {"get_activation_token", "n", kGetTokenTypes},
        {"activate", "so", kActivateTypes},
    };
    static const wl_interface kXdgActivationV1Interface = {
        "xdg_activation_v1", 1, 3, kActivationRequests, 0, nullptr
    };

    struct BindState {
        wl_proxy *activation = nullptr;
        QString token;
    } state;

    static const wl_registry_listener kRegListener = {
        [](void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver) {
            auto *st = static_cast<BindState *>(data);
            if (!st->activation && std::strcmp(iface, "xdg_activation_v1") == 0) {
                st->activation = static_cast<wl_proxy *>(
                    wl_registry_bind(reg, name, &kXdgActivationV1Interface, ver >= 1 ? 1 : ver));
            }
        },
        [](void *, wl_registry *, uint32_t) {}
    };

    struct TokenListener {
        void (*done)(void *data, wl_proxy *token, const char *tokenStr);
    };
    static const TokenListener kTokenListener = {
        [](void *data, wl_proxy *, const char *tokenStr) {
            if (tokenStr && *tokenStr)
                static_cast<BindState *>(data)->token = QString::fromUtf8(tokenStr);
        }
    };

    wl_event_queue *queue = wl_display_create_queue(display);
    if (!queue) return {};
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    if (!wrapper) {
        wl_event_queue_destroy(queue);
        return {};
    }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    wl_registry_add_listener(registry, &kRegListener, &state);
    wl_display_roundtrip_queue(display, queue);

    if (state.activation) {
        wl_proxy *tok = wl_proxy_marshal_flags(
            state.activation, 1 /*get_activation_token*/,
            &kXdgActivationTokenV1Interface, 1, 0, nullptr);
        if (tok) {
            wl_proxy_add_listener(tok, reinterpret_cast<void (**)(void)>(const_cast<TokenListener *>(&kTokenListener)), &state);
            if (seat && serial != 0) {
                wl_proxy_marshal_flags(tok, 0 /*set_serial*/, nullptr, 1, 0, serial, seat);
            }
            if (surface) {
                wl_proxy_marshal_flags(tok, 2 /*set_surface*/, nullptr, 1, 0, surface);
            }
            wl_proxy_marshal_flags(tok, 3 /*commit*/, nullptr, 1, 0);
            wl_display_roundtrip_queue(display, queue);
            wl_proxy_marshal_flags(tok, 4 /*destroy*/, nullptr, 1, WL_MARSHAL_FLAG_DESTROY);
        }
        wl_proxy_marshal_flags(state.activation, 0 /*destroy*/, nullptr, 1, WL_MARSHAL_FLAG_DESTROY);
    }

    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapper);
    wl_event_queue_destroy(queue);
    return state.token;
#else
    Q_UNUSED(sourceWidget);
    return {};
#endif
}

} // namespace
#endif // Q_OS_LINUX

void SystemStatusMonitor::activateTrayItem(const TrayItemInfo &item, const QPoint &globalPos, QWidget *menuParent)
{
#ifdef Q_OS_LINUX
    if (item.service.isEmpty() || item.objectPath.isEmpty()) return;
    if (item.itemIsMenu && !item.menuPath.isEmpty() && item.menuPath != QLatin1String("/NO_DBUSMENU")) {
        openTrayItemMenu(item, globalPos, menuParent);
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    const QString primaryIface = QStringLiteral("org.kde.StatusNotifierItem");
    const QString fallbackIface = QStringLiteral("org.freedesktop.StatusNotifierItem");
    const QString activationToken = requestWaylandActivationToken(menuParent);

    auto sendToken = [&](const QString &iface) {
        if (activationToken.isEmpty()) return;
        auto tokMsg = QDBusMessage::createMethodCall(
            item.service, item.objectPath, iface, QStringLiteral("ProvideXdgActivationToken"));
        tokMsg.setArguments({activationToken});
        bus.asyncCall(tokMsg, 400);
    };

    sendToken(primaryIface);
    auto msg = QDBusMessage::createMethodCall(item.service, item.objectPath, primaryIface, QStringLiteral("Activate"));
    msg.setArguments({static_cast<qint32>(globalPos.x()), static_cast<qint32>(globalPos.y())});
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 600), this);
    QPointer<SystemStatusMonitor> self(this);
    QPointer<QWidget> parentPtr(menuParent);
    connect(w, &QDBusPendingCallWatcher::finished, this,
            [self, w, item, globalPos, fallbackIface, activationToken, parentPtr] {
        const QDBusMessage rep = w->reply();
        w->deleteLater();
        if (!self) return;
        if (rep.type() == QDBusMessage::ErrorMessage) {
            auto sessionBus = QDBusConnection::sessionBus();
            if (!activationToken.isEmpty()) {
                auto tokFb = QDBusMessage::createMethodCall(
                    item.service, item.objectPath, fallbackIface, QStringLiteral("ProvideXdgActivationToken"));
                tokFb.setArguments({activationToken});
                sessionBus.asyncCall(tokFb, 400);
            }
            auto fb = QDBusMessage::createMethodCall(
                item.service, item.objectPath, fallbackIface, QStringLiteral("Activate"));
            fb.setArguments({static_cast<qint32>(globalPos.x()), static_cast<qint32>(globalPos.y())});
            auto *fw = new QDBusPendingCallWatcher(sessionBus.asyncCall(fb, 600), self);
            connect(fw, &QDBusPendingCallWatcher::finished, self,
                    [self, fw, item, globalPos, parentPtr] {
                const QDBusMessage fbRep = fw->reply();
                fw->deleteLater();
                if (!self) return;
                if (fbRep.type() == QDBusMessage::ErrorMessage &&
                    !item.menuPath.isEmpty() && item.menuPath != QLatin1String("/NO_DBUSMENU")) {
                    self->openTrayItemMenu(item, globalPos, parentPtr);
                }
            });
        }
    });
#else
    Q_UNUSED(item); Q_UNUSED(globalPos); Q_UNUSED(menuParent);
#endif
}

void SystemStatusMonitor::openTrayItemMenu(const TrayItemInfo &item, const QPoint &globalPos, QWidget *menuParent)
{
#ifdef Q_OS_LINUX
    if (item.service.isEmpty() || item.objectPath.isEmpty()) return;
    auto fallbackContextMenu = [item, globalPos]() {
        auto bus = QDBusConnection::sessionBus();
        auto msg = QDBusMessage::createMethodCall(
            item.service, item.objectPath,
            QStringLiteral("org.kde.StatusNotifierItem"),
            QStringLiteral("ContextMenu"));
        msg.setArguments({globalPos.x(), globalPos.y()});
        bus.asyncCall(msg, 500);
    };

    if (item.menuPath.isEmpty() || item.menuPath == QLatin1String("/NO_DBUSMENU")) {
        fallbackContextMenu();
        return;
    }

    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        item.service, item.menuPath,
        QStringLiteral("com.canonical.dbusmenu"),
        QStringLiteral("GetLayout"));
    msg.setArguments({0, 3, QStringList{}});
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 600), this);
    QPointer<QWidget> parentPtr(menuParent);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, item, globalPos, parentPtr, fallbackContextMenu] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2 ||
            !reply.arguments().at(1).canConvert<QDBusArgument>()) {
            fallbackContextMenu();
            return;
        }
        auto *menu = new QMenu(parentPtr);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        parseDBusMenuNode(reply.arguments().at(1).value<QDBusArgument>(), menu, item.service, item.menuPath, true);
        if (menu->isEmpty()) {
            menu->deleteLater();
            fallbackContextMenu();
            return;
        }
        menu->popup(globalPos);
    });
#else
    Q_UNUSED(item); Q_UNUSED(globalPos); Q_UNUSED(menuParent);
#endif
}

void SystemStatusMonitor::fetchTrayMenuEntries(const TrayItemInfo &item,
                                               std::function<void(const QList<TrayMenuEntry> &)> callback)
{
    if (!callback) return;
#ifdef Q_OS_LINUX
    if (item.service.isEmpty() || item.menuPath.isEmpty() || item.menuPath == QLatin1String("/NO_DBUSMENU")) {
        callback({});
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        item.service, item.menuPath,
        QStringLiteral("com.canonical.dbusmenu"),
        QStringLiteral("GetLayout"));
    msg.setArguments({0, 1, QStringList{}});
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 500), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [w, callback = std::move(callback)] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        QList<TrayMenuEntry> entries;
        if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() >= 2 &&
            reply.arguments().at(1).canConvert<QDBusArgument>()) {
            collectDBusMenuEntries(reply.arguments().at(1).value<QDBusArgument>(), &entries, true);
        }
        callback(entries);
    });
#else
    Q_UNUSED(item);
    callback({});
#endif
}

void SystemStatusMonitor::triggerTrayMenuEntry(const TrayItemInfo &item, int menuItemId)
{
#ifdef Q_OS_LINUX
    if (item.service.isEmpty() || item.menuPath.isEmpty()) return;
    sendDBusMenuEvent(item.service, item.menuPath, menuItemId);
#else
    Q_UNUSED(item); Q_UNUSED(menuItemId);
#endif
}

QString SystemStatusMonitor::toggleBluetooth()
{
#ifdef Q_OS_LINUX
    const QString bt = QStandardPaths::findExecutable(QStringLiteral("bluetoothctl"));
    if (bt.isEmpty()) return QStringLiteral("未找到 bluetoothctl");
    const bool turnOn = !m_snapshot.bluetoothPowered;
    QProcess::startDetached(bt, {QStringLiteral("power"), turnOn ? QStringLiteral("on") : QStringLiteral("off")});
    SystemStatusSnapshot next = m_snapshot;
    next.bluetoothAvailable = true;
    next.bluetoothPowered = turnOn;
    emitIfChanged(next);
    QTimer::singleShot(800, this, [this] { refreshLinuxBluetooth(); });
    return turnOn ? QStringLiteral("✓ 蓝牙已开启") : QStringLiteral("✓ 蓝牙已关闭");
#elif defined(Q_OS_MACOS)
    const QString msg = SystemStatusMac::toggleBluetooth();
    QTimer::singleShot(600, this, [this] { pollSlowMetrics(); });
    return msg;
#else
    return {};
#endif
}

QString SystemStatusMonitor::toggleDoNotDisturb()
{
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/plasmashell"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg.setArguments({QStringLiteral("toggle do not disturb")});
    bus.asyncCall(msg, 400);
    SystemStatusSnapshot next = m_snapshot;
    next.dndEnabled = !next.dndEnabled;
    next.unreadNotifications = 0;
    next.lastNotificationSummary.clear();
    emitIfChanged(next);
    QTimer::singleShot(450, this, [this] { refreshLinuxDnd(); });
    return next.dndEnabled ? QStringLiteral("✓ 已开启勿扰模式") : QStringLiteral("✓ 已关闭勿扰模式");
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::toggleDoNotDisturb();
#else
    return {};
#endif
}

QString SystemStatusMonitor::toggleNightLight()
{
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/kwin"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg.setArguments({QStringLiteral("Toggle Night Color")});
    bus.asyncCall(msg, 400);
    SystemStatusSnapshot next = m_snapshot;
    next.nightLightEnabled = !next.nightLightEnabled;
    emitIfChanged(next);
    QTimer::singleShot(450, this, [this] { refreshLinuxNightLight(); });
    return next.nightLightEnabled ? QStringLiteral("✓ 夜间护眼色已切换") : QStringLiteral("✓ 夜间护眼色已切换");
#else
    return {};
#endif
}

QString SystemStatusMonitor::cyclePowerProfile()
{
#ifdef Q_OS_LINUX
    const QString pp = QStandardPaths::findExecutable(QStringLiteral("powerprofilesctl"));
    if (pp.isEmpty()) return QStringLiteral("未找到 powerprofilesctl");
    QString target = QStringLiteral("balanced");
    QString label = QStringLiteral("平衡模式");
    if (m_snapshot.powerProfile == QLatin1String("power-saver")) {
        target = QStringLiteral("balanced");
        label = QStringLiteral("平衡模式");
    } else if (m_snapshot.powerProfile == QLatin1String("balanced")) {
        target = QStringLiteral("performance");
        label = QStringLiteral("性能模式");
    } else {
        target = QStringLiteral("power-saver");
        label = QStringLiteral("节能模式");
    }
    QProcess::startDetached(pp, {QStringLiteral("set"), target});
    SystemStatusSnapshot next = m_snapshot;
    next.powerProfile = target;
    emitIfChanged(next);
    QTimer::singleShot(450, this, [this] { refreshLinuxPowerProfile(); });
    return QStringLiteral("✓ 电源模式：%1").arg(label);
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::cyclePowerProfile();
#else
    return {};
#endif
}

QString SystemStatusMonitor::toggleWifi()
{
#ifdef Q_OS_LINUX
    const QString nm = QStandardPaths::findExecutable(QStringLiteral("nmcli"));
    if (nm.isEmpty()) return QStringLiteral("未找到 nmcli");
    const bool turnOn = !m_snapshot.networkConnected;
    QProcess::startDetached(nm, {QStringLiteral("radio"), QStringLiteral("wifi"),
                                 turnOn ? QStringLiteral("on") : QStringLiteral("off")});
    QTimer::singleShot(1200, this, [this] { refreshLinuxNetwork(); });
    return turnOn ? QStringLiteral("✓ 正在开启 Wi-Fi") : QStringLiteral("✓ 已关闭 Wi-Fi");
#elif defined(Q_OS_MACOS)
    const QString msg = SystemStatusMac::toggleWifi();
    QTimer::singleShot(1000, this, [this] { pollSlowMetrics(); });
    return msg;
#else
    return {};
#endif
}

bool SystemStatusMonitor::openKdeModule(const QString &moduleName)
{
#ifdef Q_OS_LINUX
    const QString kcm = QStandardPaths::findExecutable(QStringLiteral("kcmshell6"));
    if (!kcm.isEmpty() && !moduleName.isEmpty())
        return QProcess::startDetached(kcm, {moduleName});
    const QString sys = QStandardPaths::findExecutable(QStringLiteral("systemsettings"));
    if (!sys.isEmpty()) return QProcess::startDetached(sys, {});
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::openSystemSettings(moduleName);
#else
    Q_UNUSED(moduleName);
#endif
    return false;
}

bool SystemStatusMonitor::openAppLauncher()
{
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.kglobalaccel"),
        QStringLiteral("/component/plasmashell"),
        QStringLiteral("org.kde.kglobalaccel.Component"),
        QStringLiteral("invokeShortcut"));
    msg.setArguments({QStringLiteral("activate application launcher")});
    bus.asyncCall(msg, 500);
    return true;
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::openSpotlight();
#else
    return false;
#endif
}

bool SystemStatusMonitor::openKRunner()
{
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.krunner"),
        QStringLiteral("/App"),
        QStringLiteral("org.kde.krunner.App"),
        QStringLiteral("display"));
    bus.asyncCall(msg, 500);
    return true;
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::openSpotlight();
#else
    return false;
#endif
}

bool SystemStatusMonitor::lockScreen()
{
#ifdef Q_OS_LINUX
    const QString lc = QStandardPaths::findExecutable(QStringLiteral("loginctl"));
    if (!lc.isEmpty()) return QProcess::startDetached(lc, {QStringLiteral("lock-session")});
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("Lock"));
    bus.asyncCall(msg, 400);
    return true;
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::lockScreen();
#else
    return false;
#endif
}

bool SystemStatusMonitor::openPowerMenu()
{
#ifdef Q_OS_LINUX
    auto bus = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.LogoutPrompt"),
        QStringLiteral("/LogoutPrompt"),
        QStringLiteral("org.kde.LogoutPrompt"),
        QStringLiteral("promptAll"));
    bus.asyncCall(msg, 500);
    return true;
#elif defined(Q_OS_MACOS)
    return SystemStatusMac::openPowerMenu();
#else
    return false;
#endif
}
