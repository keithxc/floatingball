#pragma once

#include "systemtelemetry.h"

#include <QDateTime>
#include <QIcon>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

class QProcess;
class QTimer;
class QWidget;

struct NotificationItem {
    quint64 seqId = 0;
    quint32 replacesId = 0;
    QString appName;
    QString desktopEntry;
    QString appIconName;
    QString summary;
    QString body;
    QDateTime timestamp;
    QIcon icon;
    bool unread = true;

    QString displayAppName() const
    {
        const QString a = appName.trimmed();
        if (!a.isEmpty()) return a;
        QString d = desktopEntry.trimmed();
        if (!d.isEmpty()) {
            if (d.endsWith(QLatin1String(".desktop"), Qt::CaseInsensitive)) d.chop(8);
            const int dot = d.lastIndexOf(QLatin1Char('.'));
            if (dot >= 0 && dot + 1 < d.size()) d = d.mid(dot + 1);
            return d;
        }
        return QStringLiteral("系统通知");
    }

    QString previewText() const
    {
        const QString s = summary.trimmed();
        const QString b = body.trimmed();
        if (!s.isEmpty() && !b.isEmpty() && s != b)
            return s + QStringLiteral(" · ") + b;
        return !s.isEmpty() ? s : b;
    }
};

struct VirtualDesktopInfo {
    QString id;
    QString name;
    bool current = false;

    bool operator==(const VirtualDesktopInfo &o) const
    {
        return id == o.id && name == o.name && current == o.current;
    }
};

struct TrayItemInfo {
    QString service;
    QString objectPath;
    QString id;
    QString title;
    QString menuPath;
    QString status; // "Active", "Passive", "NeedsAttention"
    bool itemIsMenu = false;
    QIcon icon;
};

struct TrayMenuEntry {
    int id = 0;
    QString label;
    QIcon icon;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
};

struct SystemStatusSnapshot {
    // Brightness (left continuous outer arc, 0..100)
    bool brightnessAvailable = false;
    int brightnessPercent = 0;

    // Volume (right 13-dot outer arc, 0..100; all dim when muted)
    bool volumeAvailable = false;
    int volumePercent = 0;
    bool volumeMuted = false;

    // Battery (top 250-deg inner arc)
    bool batteryPresent = false;
    int batteryPercent = 0;
    bool batteryPluggedIn = false;

    // Desktop telemetry fallbacks when battery/brightness are absent (or user-selected)
    bool cpuAvailable = false;
    int cpuPercent = 0; // 0..100, EMA-smoothed
    bool memoryAvailable = false;
    int memoryPercent = 0; // 0..100
    double memoryUsedGiB = 0.0;
    double memoryTotalGiB = 0.0;

    // Network (bottom 5 signal dots; red when offline)
    bool networkConnected = false;
    int signalQuality = 0; // 0..100 (100 for wired ethernet)
    QString networkText;   // e.g. "HomeWiFi_5G (86%)" or "有线网络" or "未联网"

    // Input method center emblem ("双" or "EN")
    bool inputPresent = false;
    QString inputLabel;       // "双" or "EN"
    QString inputDescription; // e.g. "双拼" or "English"

    // Privacy dots (top-right orange = mic, bottom-right green = camera)
    bool micActive = false;
    QStringList micUsers;
    bool cameraActive = false;
    QStringList cameraUsers;

    // Notification indicator dot (top-left glowing dot when unread notifications exist)
    int unreadNotifications = 0;
    bool trayNeedsAttention = false;
    QString lastNotificationSummary;

    // Virtual desktops (KWin VirtualDesktopManager: e.g. 広 / 栞 / 発 / 伝)
    QList<VirtualDesktopInfo> virtualDesktops;

    // Live KDE system control toggles (for branched System Control sub-ring)
    bool bluetoothAvailable = false;
    bool bluetoothPowered = false;
    bool dndEnabled = false;
    bool nightLightEnabled = false;
    QString powerProfile; // "power-saver", "balanced", "performance"

    bool hasNotificationDot() const { return unreadNotifications > 0 || trayNeedsAttention; }
};

// Fully asynchronous, non-blocking system status aggregator for FloatingBall's
// Duo Status Orb & KWin virtual desktop switcher. Every Linux DBus call uses
// asyncCall + QDBusPendingCallWatcher so the Qt GUI thread NEVER blocks!
class SystemStatusMonitor : public QObject {
    Q_OBJECT
public:
    explicit SystemStatusMonitor(QObject *parent = nullptr);
    ~SystemStatusMonitor() override;

    const SystemStatusSnapshot &snapshot() const { return m_snapshot; }
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void refreshNow();
    void clearNotifications();
    void clearAllNotifications();

    // Structured notification store (replaces system notification tray/history)
    const QList<NotificationItem> &notifications() const { return m_notifications; }
    QList<NotificationItem> unreadNotifications() const;
    QList<NotificationItem> unreadNotificationsForApp(const QString &appKey,
                                                      const QString &appName = QString(),
                                                      const QString &desktopEntry = QString()) const;
    void markNotificationsReadForApp(const QString &appKey,
                                     const QString &appName = QString(),
                                     const QString &desktopEntry = QString());
    void markNotificationRead(quint64 seqId);
    void dismissNotification(quint64 seqId);
    void pushNotification(NotificationItem item);

    static bool notificationMatchesApp(const NotificationItem &item,
                                       const QString &appKey,
                                       const QString &appName = QString(),
                                       const QString &desktopEntry = QString());
    static bool parseDbusMonitorNotifyBlock(const QString &block,
                                            NotificationItem *out,
                                            bool *isTransientOut = nullptr);

    // Activate virtual desktop (0-based index); returns true if request dispatched
    // and sets *alreadyCurrent=true when the target desktop is already active.
    bool switchVirtualDesktop(int index, bool *alreadyCurrent = nullptr, QString *labelOut = nullptr);

    // Live system tray items (Linux StatusNotifierItem / DBusMenu)
    const QList<TrayItemInfo> &trayItems() const { return m_trayItems; }
    void activateTrayItem(const TrayItemInfo &item, const QPoint &globalPos, QWidget *menuParent = nullptr);
    void openTrayItemMenu(const TrayItemInfo &item, const QPoint &globalPos, QWidget *menuParent = nullptr);
    void fetchTrayMenuEntries(const TrayItemInfo &item,
                              std::function<void(const QList<TrayMenuEntry> &)> callback);
    void triggerTrayMenuEntry(const TrayItemInfo &item, int menuItemId);

    // Branched KDE status-bar system control actions (non-blocking)
    QString toggleBluetooth();
    QString toggleDoNotDisturb();
    QString toggleNightLight();
    QString cyclePowerProfile();
    QString toggleWifi();
    bool openKdeModule(const QString &moduleName);
    bool openAppLauncher();
    bool openKRunner();
    bool lockScreen();
    bool openPowerMenu();

    // Direct overrides (for immediate QuickDeck sync and automated UI tests)
    void updateAudio(int percent, bool muted, bool available = true);
    void updateBrightness(int percent, bool available = true);
    void setSnapshotForTest(const SystemStatusSnapshot &snapshot);
    void setTrayItemsForTest(const QList<TrayItemInfo> &items);
#ifdef Q_OS_MACOS
    void handleMacInputSourceChanged(const QString &label, const QString &description);
#endif
    void refreshTelemetry();

signals:
    void statusChanged(const SystemStatusSnapshot &snapshot);
    void trayItemsChanged();
    void notificationsChanged();
    void notificationReceived(const QString &appName, const QString &desktopEntry, const QString &summary);
    void inputMethodActivity(const QString &label);

private slots:
#ifdef Q_OS_LINUX
    void refreshLinuxBrightness();
    void refreshLinuxBattery();
    void refreshLinuxNetwork();
    void refreshLinuxCamera();
    void refreshLinuxVirtualDesktops();
    void refreshLinuxSniTray();
    void refreshLinuxNightLight();
    void refreshLinuxPowerProfile();
    void refreshLinuxInputMethod();
#endif

private:
    void startPlatformWatchers();
    void stopPlatformWatchers();
    void pollSlowMetrics();
    void emitIfChanged(const SystemStatusSnapshot &next);
    void syncNotificationSnapshot();
    void processDbusNotifyBlock(const QString &block);

#ifdef Q_OS_LINUX
    void initLinuxDBus();
    void refreshLinuxMic();
    void refreshLinuxDnd();
    void refreshLinuxBluetooth();
    void startNotificationMonitor();
    void stopNotificationMonitor();
#endif

    bool m_enabled = false;
    bool m_dbusConnected = false;
    SystemStatusSnapshot m_snapshot;
    QList<TrayItemInfo> m_trayItems;
    QList<NotificationItem> m_notifications;
    quint64 m_nextNotifSeqId = 1;
    QString m_notifBuffer;
    QTimer *m_notifFlushTimer = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_telemetryTimer = nullptr;
    QTimer *m_imeFastTimer = nullptr;
    QProcess *m_notifMonitor = nullptr;
    std::unique_ptr<SystemTelemetry::Sampler> m_telemetrySampler;
    int m_brightnessMax = 100;
#ifdef Q_OS_MACOS
    void *m_macLiveWatchers = nullptr;
#endif
};

#ifdef Q_OS_MACOS
namespace SystemStatusMac {

struct MacWifiNetwork {
    QString ssid;
    int signal = 0;
    QString security;
    QString rate;
    QString freq;
    bool active = false;
};

struct MacWifiState {
    bool powered = false;
    QString iface;
    QString ip;
    QList<MacWifiNetwork> networks;
};

struct MacBluetoothDevice {
    QString address;
    QString name;
    QString iconType;
    bool connected = false;
    int batteryPercent = -1;
};

void populateSnapshot(SystemStatusSnapshot *out);
void populatePrivacyAndNightShift(SystemStatusSnapshot *out);
QString toggleWifi();
QString toggleBluetooth();
QString toggleDoNotDisturb();
QString toggleNightLight();
QString cyclePowerProfile();
QIcon lookupAppIcon(const QString &desktopEntry, const QString &appName);
bool parseUserNotedPlist(const QByteArray &plistBytes, const QString &bundleId,
                         double deliveredDate, NotificationItem *out);
void *startLiveWatchers(SystemStatusMonitor *monitor);
void stopLiveWatchers(void *token);
bool openSystemSettings(const QString &moduleName);
bool lockScreen();
bool openPowerMenu();
bool openSpotlight();

void queryWifiAsync(QObject *context, bool rescan, std::function<void(const MacWifiState &)> cb);
void connectWifiAsync(QObject *context, const QString &ssid, const QString &password,
                      std::function<void(bool ok, const QString &msg)> cb);
bool readNetInterfaceBytes(const QString &iface, quint64 *rxBytes, quint64 *txBytes);
bool isInternetSharingActive();

void queryBluetoothAsync(QObject *context,
                         std::function<void(bool powered, const QList<MacBluetoothDevice> &)> cb);
void toggleBluetoothDeviceAsync(QObject *context, const QString &address, bool currentlyConnected,
                                std::function<void(bool ok, const QString &msg)> cb);

void queryNowPlayingAsync(QObject *context,
                          std::function<void(const QString &title, const QString &artist,
                                             bool playing, bool visible)> cb);
void sendMediaCommand(const QString &method);

} // namespace SystemStatusMac
#endif
