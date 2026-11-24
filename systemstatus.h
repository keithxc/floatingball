#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

struct VirtualDesktopInfo {
    QString id;
    QString name;
    bool current = false;

    bool operator==(const VirtualDesktopInfo &o) const
    {
        return id == o.id && name == o.name && current == o.current;
    }
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

    // Activate virtual desktop (0-based index); returns true if request dispatched
    // and sets *alreadyCurrent=true when the target desktop is already active.
    bool switchVirtualDesktop(int index, bool *alreadyCurrent = nullptr, QString *labelOut = nullptr);

    // Direct overrides (for immediate QuickDeck sync and automated UI tests)
    void updateAudio(int percent, bool muted, bool available = true);
    void updateBrightness(int percent, bool available = true);
    void setSnapshotForTest(const SystemStatusSnapshot &snapshot);

signals:
    void statusChanged(const SystemStatusSnapshot &snapshot);

private slots:
#ifdef Q_OS_LINUX
    void refreshLinuxBrightness();
    void refreshLinuxBattery();
    void refreshLinuxNetwork();
    void refreshLinuxCamera();
    void refreshLinuxVirtualDesktops();
#endif

private:
    void startPlatformWatchers();
    void stopPlatformWatchers();
    void pollSlowMetrics();
    void emitIfChanged(const SystemStatusSnapshot &next);

#ifdef Q_OS_LINUX
    void initLinuxDBus();
    void refreshLinuxInputMethod();
    void refreshLinuxMic();
    void refreshLinuxSniAttention();
    void startNotificationMonitor();
    void stopNotificationMonitor();
#endif

    bool m_enabled = false;
    bool m_dbusConnected = false;
    SystemStatusSnapshot m_snapshot;
    QTimer *m_pollTimer = nullptr;
    QProcess *m_notifMonitor = nullptr;
    int m_brightnessMax = 100;
};
