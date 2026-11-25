#pragma once

#include "desktopactions.h"
#include <QIcon>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

struct DockAppEntry {
    QString id;
    QString name;
    QString genericName;
    QString keywords;
    QStringList categories;
    QString launchTarget; // macOS .app path or Linux .desktop path / command
    QString windowId;     // Linux KWin internalId for live window switching/closing
    QIcon icon;
    bool running = false;
    bool active = false;
    bool pinned = false;
};

// Native Dock replacement provider for FloatingBall's multi-ring outer orbits.
// Combines user-pinned apps (seeded from the system Dock on macOS or common
// desktop apps on Linux) with live running regular desktop applications.
class AppDockModel : public QObject {
    Q_OBJECT
public:
    explicit AppDockModel(QObject *parent = nullptr);
    ~AppDockModel() override;

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

    DesktopActions::Outcome activateOrLaunch(const DockAppEntry &entry) const;
    DesktopActions::Outcome toggleOrActivate(const DockAppEntry &entry) const;
    DesktopActions::Outcome minimizeAllWindows() const;
    DesktopActions::Outcome launchFresh(const DockAppEntry &entry) const;
    DesktopActions::Outcome quitApp(const DockAppEntry &entry) const;

    static QString displayNameForTarget(const QString &launchTarget);

signals:
    void appsChanged();

private:
    void loadPinned();
    void savePinned() const;

    QStringList m_pinnedTargets;
#ifdef Q_OS_MACOS
    void *m_macObservers = nullptr;
#elif defined(Q_OS_LINUX)
    void initKWinBridge();
    void teardownKWinBridge();
    QList<DockAppEntry> m_linuxRunningApps;
    QObject *m_kwinAdaptor = nullptr;
    QString m_kwinBridgeScriptPath;
#endif
};
