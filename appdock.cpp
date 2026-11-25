#include "appdock.h"
#include "uiicons.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#endif

#ifdef Q_OS_MACOS
namespace AppDockMac {
QList<DockAppEntry> runningApps();
QStringList systemDockPinnedPaths();
QList<DockAppEntry> installedApps(int maxCount);
DockAppEntry entryForPath(const QString &appPath, bool pinned);
DesktopActions::Outcome activateOrLaunch(const DockAppEntry &entry);
DesktopActions::Outcome quitApp(const DockAppEntry &entry);
void *installWorkspaceObservers(AppDockModel *model);
void removeWorkspaceObservers(void *token);
} // namespace AppDockMac
#endif

namespace {

QString normalizeTarget(const QString &target)
{
    QString clean = target.trimmed();
    while (clean.size() > 1 && clean.endsWith(QLatin1Char('/')))
        clean.chop(1);
    return clean;
}

#ifndef Q_OS_MACOS
struct DesktopFileInfo {
    QString id;
    QString name;
    QString genericName;
    QString keywords;
    QStringList categories;
    QString execCmd;
    QString iconName;
    QString wmClass;
    QString desktopPath;
};

void ensureLinuxIconThemeConfigured()
{
    static bool configured = false;
    if (configured) return;
    configured = true;

    QStringList search = QIcon::themeSearchPaths();
    const QString user = qEnvironmentVariable("USER");
    const QStringList extraDirs{
        QDir::homePath() + QStringLiteral("/.local/share/icons"),
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons"),
        QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/icons"),
        QStringLiteral("/run/current-system/sw/share/icons"),
        QStringLiteral("/usr/share/icons"),
        QStringLiteral("/usr/share/pixmaps")
    };
    for (const QString &d : extraDirs) {
        if (QDir(d).exists() && !search.contains(d)) search.append(d);
    }
    QIcon::setThemeSearchPaths(search);

    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        for (const QString &candidate : {QStringLiteral("breeze-dark"), QStringLiteral("breeze"),
                                         QStringLiteral("Adwaita"), QStringLiteral("hicolor")}) {
            bool found = false;
            for (const QString &root : search) {
                if (QDir(root + QLatin1Char('/') + candidate).exists()) {
                    found = true;
                    break;
                }
            }
            if (found) {
                QIcon::setThemeName(candidate);
                break;
            }
        }
    }
    if (QIcon::fallbackThemeName().isEmpty()) {
        QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    }
}

QString stripExecFieldCodes(QString exec)
{
    static const QStringList codes{
        QStringLiteral("%f"), QStringLiteral("%F"),
        QStringLiteral("%u"), QStringLiteral("%U"),
        QStringLiteral("%i"), QStringLiteral("%c"), QStringLiteral("%k")
    };
    for (const QString &code : codes) exec.remove(code);
    return exec.simplified();
}

bool parseDesktopFile(const QString &path, DesktopFileInfo *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream in(&f);
    bool inDesktopEntry = false;
    bool noDisplay = false;
    bool hidden = false;
    QString type;
    QString name;
    QString zhName;
    QString genericName;
    QString zhGenericName;
    QString keywords;
    QString zhKeywords;
    QString categoriesRaw;
    QString exec;
    QString icon;
    QString wmClass;

    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inDesktopEntry) continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Type")) type = val;
        else if (key == QLatin1String("NoDisplay")) noDisplay = (val == QLatin1String("true"));
        else if (key == QLatin1String("Hidden")) hidden = (val == QLatin1String("true"));
        else if (key == QLatin1String("Name")) name = val;
        else if (key == QLatin1String("Name[zh_CN]")) zhName = val;
        else if (key == QLatin1String("GenericName")) genericName = val;
        else if (key == QLatin1String("GenericName[zh_CN]")) zhGenericName = val;
        else if (key == QLatin1String("Keywords")) keywords = val;
        else if (key == QLatin1String("Keywords[zh_CN]")) zhKeywords = val;
        else if (key == QLatin1String("Categories")) categoriesRaw = val;
        else if (key == QLatin1String("Exec")) exec = val;
        else if (key == QLatin1String("Icon")) icon = val;
        else if (key == QLatin1String("StartupWMClass")) wmClass = val;
    }

    if (type != QLatin1String("Application") || noDisplay || hidden || exec.isEmpty())
        return false;
    const QString displayName = !zhName.isEmpty() ? zhName : name;
    if (displayName.isEmpty()) return false;
    if (displayName.compare(QStringLiteral("FloatingBall"), Qt::CaseInsensitive) == 0)
        return false;

    out->id = QFileInfo(path).completeBaseName();
    out->name = displayName;
    out->genericName = !zhGenericName.isEmpty() ? zhGenericName : genericName;
    QStringList kwParts;
    if (!name.isEmpty() && name != displayName) kwParts.append(name);
    if (!genericName.isEmpty()) kwParts.append(genericName);
    if (!zhGenericName.isEmpty()) kwParts.append(zhGenericName);
    if (!keywords.isEmpty()) kwParts.append(keywords);
    if (!zhKeywords.isEmpty()) kwParts.append(zhKeywords);
    out->keywords = kwParts.join(QLatin1Char(' '));
    out->categories = categoriesRaw.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    out->execCmd = stripExecFieldCodes(exec);
    out->iconName = icon;
    out->wmClass = wmClass;
    out->desktopPath = path;
    return true;
}

QIcon resolveLinuxIcon(const QString &iconName)
{
    ensureLinuxIconThemeConfigured();
    static QHash<QString, QIcon> iconCache;
    if (!iconName.isEmpty()) {
        const auto it = iconCache.constFind(iconName);
        if (it != iconCache.constEnd()) return it.value();

        if (QFileInfo::exists(iconName)) {
            QIcon ico(iconName);
            if (!ico.isNull()) { iconCache.insert(iconName, ico); return ico; }
        }
        QIcon themed = QIcon::fromTheme(iconName);
        if (!themed.isNull()) { iconCache.insert(iconName, themed); return themed; }

        // Direct fallback scan across NixOS / Flatpak / Breeze / hicolor icon directories
        const QString user = qEnvironmentVariable("USER");
        const QStringList roots{
            QDir::homePath() + QStringLiteral("/.local/share/icons"),
            QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/icons"),
            QStringLiteral("/var/lib/flatpak/exports/share/icons"),
            QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/icons"),
            QStringLiteral("/run/current-system/sw/share/icons"),
            QStringLiteral("/run/current-system/sw/share/pixmaps"),
            QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/pixmaps"),
            QStringLiteral("/usr/share/icons"),
            QStringLiteral("/usr/share/pixmaps")
        };
        static const QStringList subpaths{
            QStringLiteral(""),
            QStringLiteral("/hicolor/scalable/apps"),
            QStringLiteral("/hicolor/512x512/apps"),
            QStringLiteral("/hicolor/256x256/apps"),
            QStringLiteral("/hicolor/128x128/apps"),
            QStringLiteral("/hicolor/64x64/apps"),
            QStringLiteral("/hicolor/48x48/apps"),
            QStringLiteral("/hicolor/32x32/apps"),
            QStringLiteral("/breeze-dark/apps/64"),
            QStringLiteral("/breeze-dark/apps/48"),
            QStringLiteral("/breeze-dark/apps/32"),
            QStringLiteral("/breeze/apps/64"),
            QStringLiteral("/breeze/apps/48"),
            QStringLiteral("/breeze/apps/32"),
            QStringLiteral("/breeze-dark/categories/32"),
            QStringLiteral("/breeze/categories/32"),
            QStringLiteral("/breeze-dark/preferences/32"),
            QStringLiteral("/breeze/preferences/32"),
            QStringLiteral("/breeze-dark/places/48"),
            QStringLiteral("/breeze/places/48"),
            QStringLiteral("/Adwaita/scalable/apps")
        };
        static const QStringList exts{QStringLiteral(".svg"), QStringLiteral(".png"), QStringLiteral(".xpm")};
        for (const QString &root : roots) {
            if (!QDir(root).exists()) continue;
            for (const QString &sub : subpaths) {
                for (const QString &ext : exts) {
                    const QString cand = root + sub + QLatin1Char('/') + iconName + ext;
                    if (QFileInfo::exists(cand)) {
                        QIcon ico(cand);
                        if (!ico.isNull()) { iconCache.insert(iconName, ico); return ico; }
                    }
                }
            }
        }
    }
    return FloatingIcons::make(FloatingIcons::Orb, QColor("#dce8f8"), 32);
}

QList<DesktopFileInfo> scanLinuxDesktopFiles(int maxCount)
{
    const QString user = qEnvironmentVariable("USER");
    const QStringList dirs{
        QDir::homePath() + QStringLiteral("/.local/share/applications"),
        QDir::homePath() + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
        QStringLiteral("/var/lib/flatpak/exports/share/applications"),
        QStringLiteral("/etc/profiles/per-user/") + user + QStringLiteral("/share/applications"),
        QStringLiteral("/run/current-system/sw/share/applications"),
        QStringLiteral("/usr/share/applications")
    };
    QList<DesktopFileInfo> results;
    QSet<QString> seenNames;
    for (const QString &dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (results.size() >= maxCount) return results;
            DesktopFileInfo info;
            if (!parseDesktopFile(fi.absoluteFilePath(), &info)) continue;
            const QString dedupKey = info.id.toLower();
            if (seenNames.contains(dedupKey)) continue;
            seenNames.insert(dedupKey);
            results.append(info);
        }
    }
    return results;
}

const QList<DesktopFileInfo> &cachedLinuxDesktopCatalog()
{
    static QList<DesktopFileInfo> catalog;
    if (catalog.isEmpty()) catalog = scanLinuxDesktopFiles(360);
    return catalog;
}

const DesktopFileInfo *matchDesktopFile(const QString &desktopFileName, const QString &resourceClass)
{
    const auto &all = cachedLinuxDesktopCatalog();
    auto norm = [](QString s) {
        s = s.trimmed().toLower();
        if (s.endsWith(QLatin1String(".desktop"))) s.chop(8);
        return s;
    };
    const QString df = norm(desktopFileName);
    const QString cls = norm(resourceClass);

    for (const DesktopFileInfo &info : all) {
        if (!df.isEmpty() && info.id.compare(df, Qt::CaseInsensitive) == 0) return &info;
    }
    for (const DesktopFileInfo &info : all) {
        if (!cls.isEmpty() &&
            (info.id.compare(cls, Qt::CaseInsensitive) == 0 ||
             info.wmClass.compare(cls, Qt::CaseInsensitive) == 0 ||
             info.id.endsWith(QLatin1Char('.') + cls, Qt::CaseInsensitive)))
            return &info;
    }
    if (!cls.isEmpty()) {
        for (const DesktopFileInfo &info : all) {
            if (info.id.contains(cls, Qt::CaseInsensitive) ||
                info.wmClass.contains(cls, Qt::CaseInsensitive) ||
                info.name.contains(cls, Qt::CaseInsensitive) ||
                info.execCmd.contains(cls, Qt::CaseInsensitive))
                return &info;
        }
    }
    return nullptr;
}

QStringList defaultLinuxPinnedDesktops()
{
    static const QStringList preferredKeywords{
        QStringLiteral("dolphin"), QStringLiteral("konsole"), QStringLiteral("alacritty"),
        QStringLiteral("kitty"), QStringLiteral("google-chrome"), QStringLiteral("chromium"),
        QStringLiteral("firefox"), QStringLiteral("code"), QStringLiteral("systemsettings"),
        QStringLiteral("kate"), QStringLiteral("obsidian")
    };
    const QList<DesktopFileInfo> &all = cachedLinuxDesktopCatalog();
    QStringList chosen;
    for (const QString &kw : preferredKeywords) {
        for (const DesktopFileInfo &info : all) {
            if (info.id.contains(kw, Qt::CaseInsensitive) && !chosen.contains(info.desktopPath)) {
                chosen.append(info.desktopPath);
                break;
            }
        }
        if (chosen.size() >= 8) break;
    }
    if (chosen.isEmpty()) {
        for (const DesktopFileInfo &info : all) {
            chosen.append(info.desktopPath);
            if (chosen.size() >= 6) break;
        }
    }
    return chosen;
}

#ifdef Q_OS_LINUX
QString sanitizeKWinId(const QString &raw)
{
    QString out;
    for (QChar ch : raw) {
        if (ch.isLetterOrNumber() || ch == QLatin1Char('-') || ch == QLatin1Char('{') ||
            ch == QLatin1Char('}') || ch == QLatin1Char('_'))
            out.append(ch);
    }
    return out;
}

void runOneShotKWinScript(const QString &pluginName, const QString &jsBody)
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString base = runtimeDir.isEmpty() ? QDir::tempPath() : runtimeDir;
    const QString scriptPath = base + QLatin1Char('/') + pluginName + QStringLiteral(".js");
    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    f.write(jsBody.toUtf8());
    f.close();

    auto bus = QDBusConnection::sessionBus();
    auto unload = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload.setArguments({pluginName});
    auto *uw = new QDBusPendingCallWatcher(bus.asyncCall(unload, 400));
    QObject::connect(uw, &QDBusPendingCallWatcher::finished, uw, [uw, scriptPath, pluginName] {
        uw->deleteLater();
        auto b = QDBusConnection::sessionBus();
        auto load = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
        load.setArguments({scriptPath, pluginName});
        auto *w = new QDBusPendingCallWatcher(b.asyncCall(load, 500));
        QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [w, pluginName] {
            w->deleteLater();
            auto b2 = QDBusConnection::sessionBus();
            auto startMsg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("start"));
            b2.asyncCall(startMsg, 400);
            QTimer::singleShot(350, [pluginName] {
                auto ul = QDBusMessage::createMethodCall(
                    QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                    QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
                ul.setArguments({pluginName});
                QDBusConnection::sessionBus().asyncCall(ul, 400);
            });
        });
    });
}
#endif // Q_OS_LINUX
#endif // !Q_OS_MACOS

} // namespace

#ifdef Q_OS_LINUX
class KWinBridgeReceiver final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "dev.floatingball.KWinBridge")
public:
    explicit KWinBridgeReceiver(std::function<void(const QString &)> cb,
                                std::function<QString()> summaryCb,
                                QObject *parent = nullptr)
        : QObject(parent), m_cb(std::move(cb)), m_summaryCb(std::move(summaryCb))
    {
    }
public slots:
    Q_SCRIPTABLE void publishWindows(const QString &json)
    {
        if (m_cb) m_cb(json);
    }
    Q_SCRIPTABLE QString liveWindowSummary() const
    {
        return m_summaryCb ? m_summaryCb() : QString();
    }
private:
    std::function<void(const QString &)> m_cb;
    std::function<QString()> m_summaryCb;
};
#endif

AppDockModel::AppDockModel(QObject *parent) : QObject(parent)
{
    loadPinned();
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == QStringLiteral("cocoa")) {
        m_macObservers = AppDockMac::installWorkspaceObservers(this);
    }
#elif defined(Q_OS_LINUX)
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        ensureLinuxIconThemeConfigured();
        QPointer<AppDockModel> self(this);
        QTimer::singleShot(180, this, [self] {
            if (self) self->initKWinBridge();
        });
    }
#endif
}

AppDockModel::~AppDockModel()
{
#ifdef Q_OS_MACOS
    if (m_macObservers) {
        AppDockMac::removeWorkspaceObservers(m_macObservers);
        m_macObservers = nullptr;
    }
#elif defined(Q_OS_LINUX)
    teardownKWinBridge();
#endif
}

#ifdef Q_OS_LINUX
void AppDockModel::initKWinBridge()
{
    if (m_kwinAdaptor) return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;

    QPointer<AppDockModel> self(this);
    m_kwinAdaptor = new KWinBridgeReceiver([self](const QString &json) {
        if (!self) return;
        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isArray()) return;
        const QJsonArray arr = doc.array();

        QSet<QString> pinnedSet;
        for (const QString &t : self->m_pinnedTargets) pinnedSet.insert(normalizeTarget(t));

        QList<DockAppEntry> live;
        for (const QJsonValue &val : arr) {
            const QJsonObject obj = val.toObject();
            const QString winId = obj.value(QStringLiteral("id")).toString();
            const QString desktopFile = obj.value(QStringLiteral("desktopFile")).toString();
            const QString resClass = obj.value(QStringLiteral("resourceClass")).toString();
            const QString caption = obj.value(QStringLiteral("caption")).toString();
            const bool active = obj.value(QStringLiteral("active")).toBool();
            if (winId.isEmpty()) continue;

            const DesktopFileInfo *matched = matchDesktopFile(desktopFile, resClass);
            DockAppEntry entry;
            entry.windowId = winId;
            entry.running = true;
            entry.active = active;
            if (matched) {
                entry.id = matched->id;
                entry.name = !caption.isEmpty() && caption.size() <= 24 ? caption : matched->name;
                entry.launchTarget = matched->desktopPath;
                entry.icon = resolveLinuxIcon(matched->iconName);
                entry.pinned = pinnedSet.contains(normalizeTarget(matched->desktopPath));
            } else {
                entry.id = !desktopFile.isEmpty() ? desktopFile : resClass;
                entry.name = !caption.isEmpty() ? caption : (!resClass.isEmpty() ? resClass : QStringLiteral("窗口"));
                if (entry.name.size() > 26) entry.name = entry.name.left(25) + QStringLiteral("…");
                entry.launchTarget = desktopFile;
                entry.icon = resolveLinuxIcon(!desktopFile.isEmpty() ? desktopFile : resClass.toLower());
            }
            if (active) live.prepend(entry);
            else live.append(entry);
        }

        self->m_linuxRunningApps = live;
        emit self->appsChanged();
    }, [self]() -> QString {
        if (!self) return {};
        QStringList lines;
        for (const DockAppEntry &e : self->m_linuxRunningApps) {
            lines.append(QStringLiteral("%1 | id=%2 | target=%3 | active=%4")
                             .arg(e.name, e.id, e.launchTarget, e.active ? QStringLiteral("1") : QStringLiteral("0")));
        }
        return QStringLiteral("count=%1\n%2").arg(self->m_linuxRunningApps.size()).arg(lines.join(QLatin1Char('\n')));
    }, this);

    bus.registerService(QStringLiteral("dev.floatingball.KWinBridge"));
    bus.registerObject(QStringLiteral("/Windows"), QStringLiteral("dev.floatingball.KWinBridge"),
                       m_kwinAdaptor, QDBusConnection::ExportScriptableSlots);

    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString base = runtimeDir.isEmpty() ? QDir::tempPath() : runtimeDir;
    m_kwinBridgeScriptPath = base + QStringLiteral("/floatingball-kwin-bridge.js");

    static const char *kBridgeScript = R"JS(
function fbPublishWindows() {
    var out = [];
    var wins = workspace.windowList();
    var active = workspace.activeWindow;
    for (var i = 0; i < wins.length; ++i) {
        var w = wins[i];
        if (!w || !w.normalWindow || w.skipTaskbar || w.specialWindow) continue;
        var cls = String(w.resourceClass || "");
        if (cls.toLowerCase().indexOf("floatingball") !== -1) continue;
        out.push({
            id: String(w.internalId),
            desktopFile: String(w.desktopFileName || ""),
            resourceClass: cls,
            caption: String(w.caption || ""),
            active: (w === active),
            minimized: !!w.minimized
        });
    }
    callDBus("dev.floatingball.KWinBridge", "/Windows",
             "dev.floatingball.KWinBridge", "publishWindows",
             JSON.stringify(out));
}
workspace.windowAdded.connect(function(w) {
    if (w && w.normalWindow) {
        w.captionChanged.connect(fbPublishWindows);
        w.minimizedChanged.connect(fbPublishWindows);
    }
    fbPublishWindows();
});
workspace.windowRemoved.connect(fbPublishWindows);
workspace.windowActivated.connect(fbPublishWindows);
fbPublishWindows();
)JS";

    QFile f(m_kwinBridgeScriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    f.write(kBridgeScript);
    f.close();

    const QString pluginName = QStringLiteral("floatingball_window_bridge");
    auto unload = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
        QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
    unload.setArguments({pluginName});
    auto *uw = new QDBusPendingCallWatcher(bus.asyncCall(unload, 500), this);
    connect(uw, &QDBusPendingCallWatcher::finished, this, [this, uw, pluginName] {
        uw->deleteLater();
        auto b = QDBusConnection::sessionBus();
        auto load = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("loadScript"));
        load.setArguments({m_kwinBridgeScriptPath, pluginName});
        auto *w = new QDBusPendingCallWatcher(b.asyncCall(load, 600), this);
        connect(w, &QDBusPendingCallWatcher::finished, this, [w] {
            w->deleteLater();
            auto startMsg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
                QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("start"));
            QDBusConnection::sessionBus().asyncCall(startMsg, 400);
        });
    });
}

void AppDockModel::teardownKWinBridge()
{
    if (!m_kwinAdaptor) return;
    auto bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        auto unload = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"),
            QStringLiteral("org.kde.kwin.Scripting"), QStringLiteral("unloadScript"));
        unload.setArguments({QStringLiteral("floatingball_window_bridge")});
        bus.asyncCall(unload, 300);
        bus.unregisterObject(QStringLiteral("/Windows"));
        bus.unregisterService(QStringLiteral("dev.floatingball.KWinBridge"));
    }
    m_kwinAdaptor = nullptr;
}
#endif

void AppDockModel::loadPinned()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    const bool initialized = s.value("dock/initialized", false).toBool();
    if (initialized) {
        const QStringList raw = s.value("dock/pinnedTargets").toStringList();
        m_pinnedTargets.clear();
        for (const QString &t : raw) {
            const QString norm = normalizeTarget(t);
            if (!norm.isEmpty() && !m_pinnedTargets.contains(norm))
                m_pinnedTargets.append(norm);
        }
        return;
    }
    importFromSystemDock();
}

void AppDockModel::savePinned() const
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue("dock/initialized", true);
    s.setValue("dock/pinnedTargets", m_pinnedTargets);
}

void AppDockModel::setPinnedTargets(const QStringList &targets)
{
    QStringList clean;
    for (const QString &t : targets) {
        const QString norm = normalizeTarget(t);
        if (!norm.isEmpty() && !clean.contains(norm))
            clean.append(norm);
    }
    m_pinnedTargets = clean;
    savePinned();
    emit appsChanged();
}

bool AppDockModel::isPinned(const QString &launchTarget) const
{
    return m_pinnedTargets.contains(normalizeTarget(launchTarget));
}

bool AppDockModel::togglePin(const QString &launchTarget)
{
    const QString norm = normalizeTarget(launchTarget);
    if (norm.isEmpty()) return false;
    bool nowPinned = false;
    if (m_pinnedTargets.contains(norm)) {
        m_pinnedTargets.removeAll(norm);
        nowPinned = false;
    } else {
        m_pinnedTargets.append(norm);
        nowPinned = true;
    }
    savePinned();
    emit appsChanged();
    return nowPinned;
}

int AppDockModel::importFromSystemDock()
{
    QStringList imported;
#ifdef Q_OS_MACOS
    imported = AppDockMac::systemDockPinnedPaths();
#else
    imported = defaultLinuxPinnedDesktops();
#endif
    QStringList clean;
    for (const QString &t : imported) {
        const QString norm = normalizeTarget(t);
        if (!norm.isEmpty() && !clean.contains(norm))
            clean.append(norm);
    }
    m_pinnedTargets = clean;
    savePinned();
    emit appsChanged();
    return m_pinnedTargets.size();
}

QList<DockAppEntry> AppDockModel::dockApps(int maxCount, bool includeInstalledOverflow,
                                           bool runningFirst) const
{
    QList<DockAppEntry> result;
    if (maxCount <= 0) return result;

#ifdef Q_OS_MACOS
    const QList<DockAppEntry> running = AppDockMac::runningApps();
    QHash<QString, DockAppEntry> runningByPath;
    QHash<QString, DockAppEntry> runningById;
    QSet<QString> pinnedSet;
    for (const QString &t : m_pinnedTargets) pinnedSet.insert(normalizeTarget(t));
    for (const DockAppEntry &r : running) {
        const QString norm = normalizeTarget(r.launchTarget);
        if (!norm.isEmpty()) runningByPath.insert(norm, r);
        if (!r.id.isEmpty()) runningById.insert(r.id, r);
    }

    QSet<QString> addedTargets;
    QSet<QString> addedIds;

    auto appendRunningApps = [&]() {
        QList<DockAppEntry> ordered;
        for (const DockAppEntry &r : running) {
            DockAppEntry e = r;
            const QString norm = normalizeTarget(e.launchTarget);
            e.pinned = !norm.isEmpty() && pinnedSet.contains(norm);
            if (e.active) ordered.prepend(e);
            else ordered.append(e);
        }
        for (const DockAppEntry &e : ordered) {
            if (result.size() >= maxCount) return;
            const QString norm = normalizeTarget(e.launchTarget);
            if ((!norm.isEmpty() && addedTargets.contains(norm)) ||
                (!e.id.isEmpty() && addedIds.contains(e.id)))
                continue;
            result.append(e);
            if (!norm.isEmpty()) addedTargets.insert(norm);
            if (!e.id.isEmpty()) addedIds.insert(e.id);
        }
    };

    auto appendPinnedApps = [&]() {
        for (const QString &target : m_pinnedTargets) {
            if (result.size() >= maxCount) return;
            const QString norm = normalizeTarget(target);
            if (norm.isEmpty() || addedTargets.contains(norm)) continue;
            if (runningByPath.contains(norm)) {
                DockAppEntry entry = runningByPath.value(norm);
                entry.pinned = true;
                if (!entry.id.isEmpty() && addedIds.contains(entry.id)) continue;
                result.append(entry);
                addedTargets.insert(norm);
                if (!entry.id.isEmpty()) addedIds.insert(entry.id);
            } else if (QFileInfo::exists(norm)) {
                DockAppEntry entry = AppDockMac::entryForPath(norm, true);
                if (!entry.name.isEmpty()) {
                    if (!entry.id.isEmpty() && addedIds.contains(entry.id)) continue;
                    if (!entry.id.isEmpty() && runningById.contains(entry.id)) {
                        const DockAppEntry live = runningById.value(entry.id);
                        entry.running = true;
                        entry.active = live.active;
                        if (entry.icon.isNull()) entry.icon = live.icon;
                    }
                    result.append(entry);
                    addedTargets.insert(norm);
                    if (!entry.id.isEmpty()) addedIds.insert(entry.id);
                }
            }
        }
    };

    if (runningFirst) {
        appendRunningApps();
        appendPinnedApps();
    } else {
        appendPinnedApps();
        appendRunningApps();
    }

    if (includeInstalledOverflow && result.size() < maxCount) {
        const QList<DockAppEntry> installed = AppDockMac::installedApps(maxCount + 16);
        for (const DockAppEntry &app : installed) {
            if (result.size() >= maxCount) break;
            const QString norm = normalizeTarget(app.launchTarget);
            if ((!norm.isEmpty() && addedTargets.contains(norm)) ||
                (!app.id.isEmpty() && addedIds.contains(app.id)))
                continue;
            result.append(app);
            if (!norm.isEmpty()) addedTargets.insert(norm);
            if (!app.id.isEmpty()) addedIds.insert(app.id);
        }
    }
#else
    QSet<QString> addedTargets;
    QSet<QString> addedWinIds;

    auto appendLiveWindows = [&]() {
#ifdef Q_OS_LINUX
        for (const DockAppEntry &e : m_linuxRunningApps) {
            if (result.size() >= maxCount) return;
            if (!e.windowId.isEmpty() && addedWinIds.contains(e.windowId)) continue;
            result.append(e);
            if (!e.windowId.isEmpty()) addedWinIds.insert(e.windowId);
            const QString norm = normalizeTarget(e.launchTarget);
            if (!norm.isEmpty()) addedTargets.insert(norm);
        }
#endif
    };

    auto appendPinnedDesktops = [&]() {
        for (const QString &target : m_pinnedTargets) {
            if (result.size() >= maxCount) return;
            const QString norm = normalizeTarget(target);
            if (norm.isEmpty() || addedTargets.contains(norm)) continue;
            DesktopFileInfo info;
            if (parseDesktopFile(norm, &info)) {
                DockAppEntry e;
                e.id = info.id;
                e.name = info.name;
                e.launchTarget = info.desktopPath;
                e.icon = resolveLinuxIcon(info.iconName);
                e.pinned = true;
                result.append(e);
                addedTargets.insert(norm);
            } else {
                DockAppEntry e;
                e.id = norm;
                e.name = displayNameForTarget(norm);
                e.launchTarget = norm;
                e.icon = FloatingIcons::make(FloatingIcons::Orb, QColor("#dce8f8"), 32);
                e.pinned = true;
                result.append(e);
                addedTargets.insert(norm);
            }
        }
    };

    if (runningFirst) {
        appendLiveWindows();
        appendPinnedDesktops();
    } else {
        appendPinnedDesktops();
        appendLiveWindows();
    }

    if (includeInstalledOverflow && result.size() < maxCount) {
        const QList<DesktopFileInfo> &all = cachedLinuxDesktopCatalog();
        for (const DesktopFileInfo &info : all) {
            if (result.size() >= maxCount) break;
            const QString norm = normalizeTarget(info.desktopPath);
            if (addedTargets.contains(norm)) continue;
            DockAppEntry e;
            e.id = info.id;
            e.name = info.name;
            e.launchTarget = info.desktopPath;
            e.icon = resolveLinuxIcon(info.iconName);
            e.pinned = false;
            result.append(e);
            addedTargets.insert(norm);
        }
    }
#endif

    return result;
}

QList<DockAppEntry> AppDockModel::installedApps(int maxCount) const
{
#ifdef Q_OS_MACOS
    return AppDockMac::installedApps(maxCount);
#else
    QList<DockAppEntry> list;
    const QList<DesktopFileInfo> &all = cachedLinuxDesktopCatalog();
    for (const DesktopFileInfo &info : all) {
        if (list.size() >= maxCount) break;
        DockAppEntry e;
        e.id = info.id;
        e.name = info.name;
        e.genericName = info.genericName;
        e.keywords = info.keywords;
        e.categories = info.categories;
        e.launchTarget = info.desktopPath;
        e.icon = resolveLinuxIcon(info.iconName);
        e.pinned = isPinned(info.desktopPath);
        list.append(e);
    }
    return list;
#endif
}

DesktopActions::Outcome AppDockModel::launchFresh(const DockAppEntry &entry) const
{
    DockAppEntry fresh = entry;
    fresh.running = false;
    fresh.windowId.clear();
    return activateOrLaunch(fresh);
}

DesktopActions::Outcome AppDockModel::activateOrLaunch(const DockAppEntry &entry) const
{
#ifdef Q_OS_MACOS
    return AppDockMac::activateOrLaunch(entry);
#else
#ifdef Q_OS_LINUX
    if (entry.running && !entry.windowId.isEmpty()) {
        const QString safeId = sanitizeKWinId(entry.windowId);
        if (!safeId.isEmpty()) {
            const QString js = QStringLiteral(
                "(function(){"
                "var wins=workspace.windowList();"
                "for(var i=0;i<wins.length;++i){"
                "  var w=wins[i];"
                "  if(w && String(w.internalId)==='%1'){"
                "    if(w.desktops && w.desktops.length>0 && !w.onAllDesktops){workspace.currentDesktop=w.desktops[0];}"
                "    w.minimized=false;"
                "    workspace.activeWindow=w;"
                "    break;"
                "  }"
                "}"
                "})();").arg(safeId);
            runOneShotKWinScript(QStringLiteral("floatingball_activate_win"), js);
            return {true, QStringLiteral("✓ 已切换到 ") + entry.name};
        }
    }
#endif
    if (entry.launchTarget.endsWith(QLatin1String(".desktop"))) {
        DesktopFileInfo info;
        if (parseDesktopFile(entry.launchTarget, &info)) {
            if (!QStandardPaths::findExecutable(QStringLiteral("gtk-launch")).isEmpty() &&
                QProcess::startDetached(QStringLiteral("gtk-launch"), {info.id})) {
                return {true, QStringLiteral("✓ 已启动 ") + entry.name};
            }
            const QStringList args = QProcess::splitCommand(info.execCmd);
            if (!args.isEmpty() && QProcess::startDetached(args.first(), args.mid(1))) {
                return {true, QStringLiteral("✓ 已启动 ") + entry.name};
            }
        }
    }
    const QStringList args = QProcess::splitCommand(entry.launchTarget);
    if (!args.isEmpty() && QProcess::startDetached(args.first(), args.mid(1)))
        return {true, QStringLiteral("✓ 已启动 ") + entry.name};
    return {false, QStringLiteral("无法启动 ") + entry.name};
#endif
}

DesktopActions::Outcome AppDockModel::quitApp(const DockAppEntry &entry) const
{
#ifdef Q_OS_MACOS
    return AppDockMac::quitApp(entry);
#elif defined(Q_OS_LINUX)
    if (entry.running && !entry.windowId.isEmpty()) {
        const QString safeId = sanitizeKWinId(entry.windowId);
        if (!safeId.isEmpty()) {
            const QString js = QStringLiteral(
                "(function(){"
                "var wins=workspace.windowList();"
                "for(var i=0;i<wins.length;++i){"
                "  var w=wins[i];"
                "  if(w && String(w.internalId)==='%1'){w.closeWindow();break;}"
                "}"
                "})();").arg(safeId);
            runOneShotKWinScript(QStringLiteral("floatingball_close_win"), js);
            return {true, QStringLiteral("✓ 已关闭 ") + entry.name};
        }
    }
    return {false, entry.name + QStringLiteral(" 当前未运行")};
#else
    Q_UNUSED(entry);
    return {false, QStringLiteral("当前平台暂不支持从悬浮环直接退出应用")};
#endif
}

QString AppDockModel::displayNameForTarget(const QString &launchTarget)
{
    const QString norm = normalizeTarget(launchTarget);
    QFileInfo fi(norm);
    QString base = fi.completeBaseName();
    if (base.isEmpty()) base = fi.fileName();
    if (base.isEmpty()) base = norm;
    return base;
}

#ifdef Q_OS_LINUX
#include "appdock.moc"
#endif
