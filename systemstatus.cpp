#include "systemstatus.h"
#include "desktopactions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <memory>
#include <csignal>
#include <sys/prctl.h>
#endif

#ifdef Q_OS_MACOS
namespace SystemStatusMac {
void populateSnapshot(SystemStatusSnapshot *out);
}
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
           a.virtualDesktops == b.virtualDesktops;
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
    refreshLinuxSniAttention();
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

    // 3. KWin VirtualDesktopManager signals (instant highlight update on desktop switch!)
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
                if (type == QLatin1String("802-3-ethernet")) {
                    if (3 > gather->bestPrio) {
                        gather->bestPrio = 3;
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
                        connect(apW, &QDBusPendingCallWatcher::finished, self, [apW, gather, id, finishOne] {
                            int strength = 75;
                            const QDBusMessage apReply = apW->reply();
                            apW->deleteLater();
                            if (apReply.type() == QDBusMessage::ReplyMessage && !apReply.arguments().isEmpty()) {
                                const QDBusVariant v = apReply.arguments().first().value<QDBusVariant>();
                                strength = qBound(0, v.variant().toInt(), 100);
                            }
                            if (2 > gather->bestPrio || (2 == gather->bestPrio && strength > gather->bestQuality)) {
                                gather->bestPrio = 2;
                                gather->bestQuality = strength;
                                gather->bestText = (id.isEmpty() ? QStringLiteral("Wi-Fi") : id) +
                                                   QStringLiteral(" (%1%)").arg(strength);
                            }
                            finishOne();
                        });
                    } else {
                        if (2 > gather->bestPrio) {
                            gather->bestPrio = 2;
                            gather->bestQuality = 75;
                            gather->bestText = id.isEmpty() ? QStringLiteral("Wi-Fi") : id;
                        }
                        finishOne();
                    }
                } else if (type == QLatin1String("gsm") || type == QLatin1String("cdma")) {
                    if (1 > gather->bestPrio) {
                        gather->bestPrio = 1;
                        gather->bestQuality = 80;
                        gather->bestText = id.isEmpty() ? QStringLiteral("移动网络") : id;
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

void SystemStatusMonitor::refreshLinuxSniAttention()
{
    auto session = QDBusConnection::sessionBus();
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.StatusNotifierWatcher"),
        QStringLiteral("/StatusNotifierWatcher"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("Get"));
    msg.setArguments({QStringLiteral("org.kde.StatusNotifierWatcher"),
                      QStringLiteral("RegisteredStatusNotifierItems")});
    auto *w = new QDBusPendingCallWatcher(session.asyncCall(msg, 500), this);
    QPointer<SystemStatusMonitor> self(this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [self, w] {
        const QDBusMessage reply = w->reply();
        w->deleteLater();
        if (!self || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return;

        const QDBusVariant dv = reply.arguments().first().value<QDBusVariant>();
        const QStringList items = dv.variant().toStringList();
        if (items.isEmpty()) return;

        struct SniGather {
            int pending = 0;
            bool attention = false;
        };
        auto gather = std::make_shared<SniGather>();
        gather->pending = items.size();

        auto finishOne = [self, gather]() {
            if (--gather->pending > 0 || !self) return;
            SystemStatusSnapshot next = self->m_snapshot;
            next.trayNeedsAttention = gather->attention;
            self->emitIfChanged(next);
        };

        auto bus = QDBusConnection::sessionBus();
        for (const QString &entry : items) {
            const int slash = entry.indexOf(QLatin1Char('/'));
            if (slash <= 0) { finishOne(); continue; }
            auto sMsg = QDBusMessage::createMethodCall(
                entry.left(slash), entry.mid(slash),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("Get"));
            sMsg.setArguments({QStringLiteral("org.kde.StatusNotifierItem"), QStringLiteral("Status")});
            auto *sw = new QDBusPendingCallWatcher(bus.asyncCall(sMsg, 400), self);
            connect(sw, &QDBusPendingCallWatcher::finished, self, [sw, gather, finishOne] {
                const QDBusMessage r = sw->reply();
                sw->deleteLater();
                if (r.type() == QDBusMessage::ReplyMessage && !r.arguments().isEmpty()) {
                    const QDBusVariant st = r.arguments().first().value<QDBusVariant>();
                    if (st.variant().toString() == QLatin1String("NeedsAttention"))
                        gather->attention = true;
                }
                finishOne();
            });
        }
    });
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
