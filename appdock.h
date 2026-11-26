#pragma once

#include "desktopactions.h"
#include <QIcon>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

struct DockAppEntry {
    QString id;
    QString appKey;       // Normalized app identifier for stable same-app window clustering
    QString name;
    QString caption;      // Live window title (e.g. "nvim floatingball.cpp")
    QString genericName;
    QString keywords;
    QStringList categories;
    QString launchTarget; // macOS .app path or Linux .desktop path / command
    QString windowId;     // Linux KWin internalId for live window switching/closing
    QIcon icon;
    bool running = false;
    bool active = false;
    bool minimized = false;
    bool demandsAttention = false;
    bool pinned = false;
};

// Native Dock & Live Window provider for FloatingBall's multi-ring outer orbits.
class AppDockModel : public QObject {
    Q_OBJECT
public:
    explicit AppDockModel(QObject *parent = nullptr);
    ~AppDockModel() override;

    // Pure currently open windows, stably clustered by application so spatial
    // bearing angles remain predictable across focus changes.
    QList<DockAppEntry> openWindows() const;
    bool activeWindowFullscreen() const { return m_activeFullscreen; }

    // Merged Dock list: when runningFirst is true, currently open/running apps
    // come first (active app at the front), followed by pinned apps not yet
    // running, and optionally installed apps for extra outer rings.
    QList<DockAppEntry> dockApps(int maxCount = 80, bool includeInstalledOverflow = true,
                                 bool runningFirst = true) const;
    QList<DockAppEntry> installedApps(int maxCount = 120) const;

    QStringList pinnedTargets() const { return m_pinnedTargets; }
    void setPinnedTargets(const QStringList &targets);
    bool isPinned(const QString &launchTarget) const;
    bool togglePin(const QString &launchTarget);
    int importFromSystemDock();

    void notifyAppAttention(const QString &appName, const QString &desktopEntry = QString());
    void clearAttentionForApp(const DockAppEntry &entry);
    void clearAllAttention();

    // Test hooks for deterministic automated verification (0..100 windows, attention, fullscreen)
    void setOpenWindowsForTest(const QList<DockAppEntry> &windows);
    void clearOpenWindowsForTest();
    void setActiveFullscreenForTest(bool fullscreen);

#ifdef Q_OS_MACOS
    void applyMacWindowSnapshot(const QList<DockAppEntry> &windows, bool activeFullscreen);
    void handleMacAppActivated(const QString &appKey, const QString &appName, const QString &bundleId);
#endif

    DesktopActions::Outcome activateOrLaunch(const DockAppEntry &entry) const;
    DesktopActions::Outcome toggleOrActivate(const DockAppEntry &entry) const;
    bool activateOrLaunchByHint(const QString &appName,
                                const QString &desktopEntry,
                                DesktopActions::Outcome *out = nullptr) const;
    DesktopActions::Outcome minimizeAllWindows() const;
    DesktopActions::Outcome launchFresh(const DockAppEntry &entry) const;
    DesktopActions::Outcome quitApp(const DockAppEntry &entry);

    static QString displayNameForTarget(const QString &launchTarget);

signals:
    void appsChanged();
    void activeFullscreenChanged(bool fullscreen);
    void windowActivated(const QString &appKey, const QString &appName, const QString &desktopEntry);

private:
    void loadPinned();
    void savePinned() const;

    QStringList m_pinnedTargets;
    QSet<QString> m_notifiedAppKeys;
    bool m_activeFullscreen = false;
    bool m_hasTestWindows = false;
    QList<DockAppEntry> m_testWindows;
#ifdef Q_OS_MACOS
    void *m_macObservers = nullptr;
    QList<DockAppEntry> m_macRunningWindows;
    bool m_macWindowCacheReady = false;
#elif defined(Q_OS_LINUX)
    void initKWinBridge();
    void teardownKWinBridge();
    QList<DockAppEntry> m_linuxRunningApps;
    QObject *m_kwinAdaptor = nullptr;
    QString m_kwinBridgeScriptPath;
#endif
};
