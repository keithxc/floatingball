#include "appdock.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

namespace AppDockMac {

QIcon iconFromNSImage(void *rawImage)
{
    NSImage *nsImage = (__bridge NSImage *)rawImage;
    if (!nsImage) return {};
    const int px = 96;
    NSRect rect = NSMakeRect(0, 0, px, px);
    CGImageRef cgImg = [nsImage CGImageForProposedRect:&rect context:nil hints:nil];
    if (!cgImg) return {};

    const size_t w = CGImageGetWidth(cgImg);
    const size_t h = CGImageGetHeight(cgImg);
    if (w == 0 || h == 0) return {};

    QImage img(static_cast<int>(w), static_cast<int>(h), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        img.bits(), w, h, 8, img.bytesPerLine(), cs,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cgImg);
        CGContextRelease(ctx);
    }
    CGColorSpaceRelease(cs);
    return QIcon(QPixmap::fromImage(img));
}

QHash<QString, QIcon> &macIconCache()
{
    static QHash<QString, QIcon> cache;
    return cache;
}

QIcon iconForPath(const QString &path)
{
    QHash<QString, QIcon> &cache = macIconCache();
    const auto it = cache.constFind(path);
    if (it != cache.constEnd()) return it.value();
    @autoreleasepool {
        NSImage *img = [[NSWorkspace sharedWorkspace] iconForFile:path.toNSString()];
        QIcon icon = iconFromNSImage((__bridge void *)img);
        if (!icon.isNull()) cache.insert(path, icon);
        return icon;
    }
}

bool isSelfBundle(const QString &bundleId, const QString &name, const QString &path)
{
    if (bundleId == QLatin1String("dev.floatingball.FloatingBall") ||
        bundleId.contains(QLatin1String("FloatingBall"), Qt::CaseInsensitive))
        return true;
    if (name.compare(QLatin1String("FloatingBall"), Qt::CaseInsensitive) == 0)
        return true;
    if (path.contains(QLatin1String("/FloatingBall.app"), Qt::CaseInsensitive))
        return true;
    return false;
}

namespace {

bool isSelfBundleOrName(NSString *bundleId, NSString *name, NSString *path)
{
    return isSelfBundle(bundleId ? QString::fromNSString(bundleId) : QString(),
                        name ? QString::fromNSString(name) : QString(),
                        path ? QString::fromNSString(path) : QString());
}

QString localizedAppName(const QString &appPath)
{
    @autoreleasepool {
        NSString *nsPath = appPath.toNSString();
        NSString *display = [[NSFileManager defaultManager] displayNameAtPath:nsPath];
        if (display && display.length > 0) {
            QString q = QString::fromNSString(display);
            if (q.endsWith(QLatin1String(".app"), Qt::CaseInsensitive))
                q.chop(4);
            return q;
        }
        return QFileInfo(appPath).completeBaseName();
    }
}

QString bundleIdForPath(const QString &appPath)
{
    @autoreleasepool {
        NSBundle *bundle = [NSBundle bundleWithPath:appPath.toNSString()];
        if (bundle && bundle.bundleIdentifier)
            return QString::fromNSString(bundle.bundleIdentifier);
        return {};
    }
}

QStringList categoriesForPath(const QString &appPath, const QString &bundleId, const QString &name)
{
    QStringList cats;
    @autoreleasepool {
        NSBundle *bundle = [NSBundle bundleWithPath:appPath.toNSString()];
        NSString *uti = bundle ? [bundle objectForInfoDictionaryKey:@"LSApplicationCategoryType"] : nil;
        const QString raw = [uti isKindOfClass:[NSString class]] ? QString::fromNSString(uti).toLower() : QString();
        if (raw.contains(QLatin1String("developer-tools")))
            cats << QStringLiteral("Development");
        else if (raw.contains(QLatin1String("social-networking")))
            cats << QStringLiteral("Network");
        else if (raw.contains(QLatin1String("music")) || raw.contains(QLatin1String("video")) ||
                 raw.contains(QLatin1String("photography")) || raw.contains(QLatin1String("graphics-design")) ||
                 raw.contains(QLatin1String("entertainment")))
            cats << QStringLiteral("AudioVideo");
        else if (raw.contains(QLatin1String("productivity")) || raw.contains(QLatin1String("business")) ||
                 raw.contains(QLatin1String("education")) || raw.contains(QLatin1String("finance")) ||
                 raw.contains(QLatin1String("reference")))
            cats << QStringLiteral("Office");
        else if (raw.contains(QLatin1String("games")))
            cats << QStringLiteral("Game");
        else if (raw.contains(QLatin1String("utilities")))
            cats << QStringLiteral("System");
    }
    if (cats.isEmpty()) {
        const QString key = (bundleId + QLatin1Char(' ') + name).toLower();
        if (key.contains(QLatin1String("xcode")) || key.contains(QLatin1String("vscode")) ||
            key.contains(QLatin1String("cursor")) || key.contains(QLatin1String("terminal")) ||
            key.contains(QLatin1String("iterm")) || key.contains(QLatin1String("ghostty")) ||
            key.contains(QLatin1String("warp")) || key.contains(QLatin1String("antigravity")) ||
            key.contains(QLatin1String("jetbrains")) || key.contains(QLatin1String("sublime"))) {
            cats << QStringLiteral("Development");
        } else if (key.contains(QLatin1String("safari")) || key.contains(QLatin1String("chrome")) ||
                   key.contains(QLatin1String("firefox")) || key.contains(QLatin1String("arc")) ||
                   key.contains(QLatin1String("wechat")) || key.contains(QLatin1String("telegram")) ||
                   key.contains(QLatin1String("slack")) || key.contains(QLatin1String("discord")) ||
                   key.contains(QLatin1String("mail")) || key.contains(QLatin1String("messages")) ||
                   key.contains(QLatin1String("feishu")) || key.contains(QLatin1String("lark")) ||
                   key.contains(QLatin1String("clash")) || key.contains(QLatin1String("mihomo"))) {
            cats << QStringLiteral("Network");
        } else if (key.contains(QLatin1String("music")) || key.contains(QLatin1String("spotify")) ||
                   key.contains(QLatin1String("photos")) || key.contains(QLatin1String("iina")) ||
                   key.contains(QLatin1String("vlc")) || key.contains(QLatin1String("quicktime")) ||
                   key.contains(QLatin1String("tv")) || key.contains(QLatin1String("podcasts")) ||
                   key.contains(QLatin1String("blender")) || key.contains(QLatin1String("figma"))) {
            cats << QStringLiteral("AudioVideo");
        } else if (key.contains(QLatin1String("notes")) || key.contains(QLatin1String("obsidian")) ||
                   key.contains(QLatin1String("notion")) || key.contains(QLatin1String("pages")) ||
                   key.contains(QLatin1String("numbers")) || key.contains(QLatin1String("keynote")) ||
                   key.contains(QLatin1String("calendar")) || key.contains(QLatin1String("reminders")) ||
                   key.contains(QLatin1String("word")) || key.contains(QLatin1String("excel")) ||
                   key.contains(QLatin1String("wps"))) {
            cats << QStringLiteral("Office");
        } else if (appPath.startsWith(QLatin1String("/System/Applications/Utilities")) ||
                   key.contains(QLatin1String("systempreferences")) ||
                   key.contains(QLatin1String("system settings")) ||
                   key.contains(QLatin1String("activitymonitor")) ||
                   key.contains(QLatin1String("finder"))) {
            cats << QStringLiteral("System");
        }
    }
    return cats;
}

} // namespace

QList<DockAppEntry> runningApps()
{
    QList<DockAppEntry> list;
    @autoreleasepool {
        const pid_t selfPid = ::getpid();
        NSArray<NSRunningApplication *> *apps = [NSWorkspace sharedWorkspace].runningApplications;
        for (NSRunningApplication *app in apps) {
            if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
            if (app.isTerminated || app.processIdentifier == selfPid) continue;
            NSString *bundleId = app.bundleIdentifier;
            NSString *name = app.localizedName;
            NSString *path = app.bundleURL ? app.bundleURL.path : nil;
            if (isSelfBundleOrName(bundleId, name, path)) continue;

            DockAppEntry entry;
            entry.id = bundleId ? QString::fromNSString(bundleId) : QString();
            entry.launchTarget = path ? QString::fromNSString(path) : QString();
            entry.name = name ? QString::fromNSString(name) : AppDockModel::displayNameForTarget(entry.launchTarget);
            entry.appKey = (!entry.id.isEmpty() ? entry.id : entry.name).toLower();
            if (!entry.launchTarget.isEmpty())
                entry.icon = iconForPath(entry.launchTarget);
            if (entry.icon.isNull() && app.icon)
                entry.icon = iconFromNSImage((__bridge void *)app.icon);
            entry.running = true;
            entry.active = app.isActive;
            entry.pinned = false;
            if (!entry.name.isEmpty()) list.append(entry);
        }
    }
    return list;
}

QStringList systemDockPinnedPaths()
{
    QStringList paths;
    const QString finder = QStringLiteral("/System/Library/CoreServices/Finder.app");
    if (QFileInfo::exists(finder)) paths.append(finder);

    @autoreleasepool {
        CFPropertyListRef pref = CFPreferencesCopyAppValue(CFSTR("persistent-apps"), CFSTR("com.apple.dock"));
        if (pref && CFGetTypeID(pref) == CFArrayGetTypeID()) {
            NSArray *items = (__bridge NSArray *)pref;
            for (id item in items) {
                if (![item isKindOfClass:[NSDictionary class]]) continue;
                NSDictionary *tileData = item[@"tile-data"];
                if (![tileData isKindOfClass:[NSDictionary class]]) continue;
                NSDictionary *fileData = tileData[@"file-data"];
                if (![fileData isKindOfClass:[NSDictionary class]]) continue;
                NSString *urlStr = fileData[@"_CFURLString"];
                if (![urlStr isKindOfClass:[NSString class]] || urlStr.length == 0) continue;
                NSURL *url = [NSURL URLWithString:urlStr];
                NSString *filePath = url.isFileURL ? url.path : urlStr;
                if (!filePath || filePath.length == 0) continue;
                QString qPath = QString::fromNSString(filePath);
                while (qPath.size() > 1 && qPath.endsWith(QLatin1Char('/'))) qPath.chop(1);
                if (qPath.contains(QLatin1String("FloatingBall.app"))) continue;
                if (QFileInfo::exists(qPath) && !paths.contains(qPath))
                    paths.append(qPath);
            }
        }
        if (pref) CFRelease(pref);
    }

    // Fallback sensible defaults if Dock plist was unreadable
    if (paths.size() <= 1) {
        static const QStringList fallbacks{
            QStringLiteral("/Applications/Google Chrome.app"),
            QStringLiteral("/Applications/Safari.app"),
            QStringLiteral("/System/Applications/Safari.app"),
            QStringLiteral("/System/Applications/Utilities/Terminal.app"),
            QStringLiteral("/System/Applications/System Settings.app"),
            QStringLiteral("/System/Applications/Notes.app"),
            QStringLiteral("/Applications/WeChat.app")
        };
        for (const QString &fb : fallbacks) {
            if (QFileInfo::exists(fb) && !paths.contains(fb))
                paths.append(fb);
        }
    }
    return paths;
}

DockAppEntry entryForPath(const QString &appPath, bool pinned)
{
    DockAppEntry entry;
    entry.launchTarget = appPath;
    entry.id = bundleIdForPath(appPath);
    entry.name = localizedAppName(appPath);
    entry.categories = categoriesForPath(appPath, entry.id, entry.name);
    entry.icon = iconForPath(appPath);
    entry.pinned = pinned;
    return entry;
}

static QStringList macAppSearchDirs()
{
    return {
        QStringLiteral("/Applications"),
        QStringLiteral("/Applications/Nix Apps"),
        QStringLiteral("/Applications/Utilities"),
        QDir::homePath() + QStringLiteral("/Applications"),
        QDir::homePath() + QStringLiteral("/Applications/Home Manager Apps"),
        QStringLiteral("/System/Applications"),
        QStringLiteral("/System/Applications/Utilities")
    };
}

bool refreshInstalledCatalog(bool force)
{
    static QString lastFingerprint;
    static QElapsedTimer lastCheck;
    static bool initialized = false;
    if (!force && initialized && lastCheck.isValid() && lastCheck.elapsed() < 1500) {
        return false;
    }
    QStringList parts;
    for (const QString &dirPath : macAppSearchDirs()) {
        QFileInfo di(dirPath);
        if (!di.exists() || !di.isDir()) {
            parts.append(dirPath + QStringLiteral("|0"));
            continue;
        }
        parts.append(QStringLiteral("%1|1|%2|%3")
                         .arg(dirPath, di.canonicalFilePath())
                         .arg(di.lastModified().toMSecsSinceEpoch()));
        QDir dir(dirPath);
        const QFileInfoList bundles = dir.entryInfoList(
            {QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : bundles) {
            parts.append(QStringLiteral("%1=%2:%3")
                             .arg(fi.fileName(), fi.canonicalFilePath())
                             .arg(fi.lastModified().toMSecsSinceEpoch()));
        }
    }
    lastCheck.restart();
    const QString fp = parts.join(QLatin1Char('\n'));
    if (initialized && fp == lastFingerprint) return false;
    lastFingerprint = fp;
    initialized = true;
    macIconCache().clear();
    return true;
}

QList<DockAppEntry> installedApps(int maxCount)
{
    refreshInstalledCatalog(true);
    QList<DockAppEntry> list;
    const QStringList searchDirs = macAppSearchDirs();
    QSet<QString> seen;
    for (const QString &dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;
        const QFileInfoList bundles = dir.entryInfoList({QStringLiteral("*.app")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &fi : bundles) {
            if (list.size() >= maxCount) return list;
            const QString p = fi.absoluteFilePath();
            if (p.contains(QLatin1String("FloatingBall.app")) || seen.contains(p)) continue;
            seen.insert(p);
            DockAppEntry e = entryForPath(p, false);
            if (!e.name.isEmpty()) list.append(e);
        }
    }
    return list;
}

DesktopActions::Outcome activateOrLaunch(const DockAppEntry &entry)
{
    @autoreleasepool {
        // 1. If already running, activate immediately without relaunch delay!
        const pid_t selfPid = ::getpid();
        NSArray<NSRunningApplication *> *running = [NSWorkspace sharedWorkspace].runningApplications;
        for (NSRunningApplication *app in running) {
            if (app.isTerminated || app.processIdentifier == selfPid) continue;
            const bool idMatch = !entry.id.isEmpty() && app.bundleIdentifier &&
                                 [app.bundleIdentifier isEqualToString:entry.id.toNSString()];
            const bool pathMatch = !entry.launchTarget.isEmpty() && app.bundleURL &&
                                   [app.bundleURL.path isEqualToString:entry.launchTarget.toNSString()];
            if (idMatch || pathMatch) {
                [app unhide];
                if (@available(macOS 14.0, *)) {
                    [app activateWithOptions:NSApplicationActivateAllWindows];
                } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    [app activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
                }
                // Reopen main window if app had all windows closed (like Finder / Chrome)
                if (!entry.launchTarget.isEmpty()) {
                    NSURL *url = [NSURL fileURLWithPath:entry.launchTarget.toNSString()];
                    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
                    cfg.activates = YES;
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:cfg completionHandler:nil];
                }
                return {true, QStringLiteral("✓ 已切换到 ") + entry.name};
            }
        }

        // 2. Not running yet -> launch via NSWorkspace
        if (!entry.launchTarget.isEmpty() && QFileInfo::exists(entry.launchTarget)) {
            NSURL *url = [NSURL fileURLWithPath:entry.launchTarget.toNSString()];
            NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
            cfg.activates = YES;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:cfg completionHandler:nil];
            return {true, QStringLiteral("✓ 正在打开 ") + entry.name};
        }
        return {false, QStringLiteral("找不到应用：") + entry.name};
    }
}

DesktopActions::Outcome toggleOrActivate(const DockAppEntry &entry)
{
    @autoreleasepool {
        if (entry.running && entry.active) {
            NSArray<NSRunningApplication *> *running = [NSWorkspace sharedWorkspace].runningApplications;
            for (NSRunningApplication *app in running) {
                if (app.isTerminated) continue;
                const bool idMatch = !entry.id.isEmpty() && app.bundleIdentifier &&
                                     [app.bundleIdentifier isEqualToString:entry.id.toNSString()];
                const bool pathMatch = !entry.launchTarget.isEmpty() && app.bundleURL &&
                                       [app.bundleURL.path isEqualToString:entry.launchTarget.toNSString()];
                if ((idMatch || pathMatch) && app.isActive) {
                    if ([app hide]) {
                        return {true, QStringLiteral("✓ 已隐藏 ") + entry.name};
                    }
                }
            }
        }
    }
    return activateOrLaunch(entry);
}

DesktopActions::Outcome minimizeAllWindows()
{
    @autoreleasepool {
        // Truly hide every regular running app (including Finder browser windows,
        // while Finder's desktop wallpaper & icons stay visible). Clicking the
        // desktop will NEVER snap windows back like Mission Control's Show Desktop peek.
        int hiddenCount = 0;
        const pid_t selfPid = ::getpid();
        NSArray<NSRunningApplication *> *running = [NSWorkspace sharedWorkspace].runningApplications;
        for (NSRunningApplication *app in running) {
            if (app.isTerminated || app.processIdentifier == selfPid) continue;
            if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
            NSString *bundleId = app.bundleIdentifier;
            NSString *name = app.localizedName;
            NSString *path = app.bundleURL ? app.bundleURL.path : nil;
            if (isSelfBundleOrName(bundleId, name, path)) continue;
            if (!app.isHidden && [app hide]) {
                ++hiddenCount;
            }
        }
        return {true, hiddenCount > 0 ? QStringLiteral("✓ 已隐藏全部窗口，仅显示桌面")
                                      : QStringLiteral("✓ 桌面已清空")};
    }
}

DesktopActions::Outcome quitApp(const DockAppEntry &entry)
{
    @autoreleasepool {
        const pid_t selfPid = ::getpid();
        NSArray<NSRunningApplication *> *running = [NSWorkspace sharedWorkspace].runningApplications;
        for (NSRunningApplication *app in running) {
            if (app.isTerminated || app.processIdentifier == selfPid) continue;
            const bool idMatch = !entry.id.isEmpty() && app.bundleIdentifier &&
                                 [app.bundleIdentifier isEqualToString:entry.id.toNSString()];
            const bool pathMatch = !entry.launchTarget.isEmpty() && app.bundleURL &&
                                   [app.bundleURL.path isEqualToString:entry.launchTarget.toNSString()];
            if (idMatch || pathMatch) {
                if ([app terminate])
                    return {true, QStringLiteral("✓ 已退出 ") + entry.name};
                return {false, QStringLiteral("无法退出 ") + entry.name};
            }
        }
        return {false, entry.name + QStringLiteral(" 当前未运行")};
    }
}

bool launchByBundleId(const QString &bundleId, DesktopActions::Outcome *out)
{
    const QString clean = bundleId.trimmed();
    if (clean.isEmpty()) return false;
    @autoreleasepool {
        for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
            if (app.isTerminated || !app.bundleIdentifier) continue;
            if (QString::fromNSString(app.bundleIdentifier).compare(clean, Qt::CaseInsensitive) == 0) {
                [app unhide];
                if (@available(macOS 14.0, *)) {
                    [app activateWithOptions:NSApplicationActivateAllWindows];
                } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    [app activateWithOptions:NSApplicationActivateAllWindows | NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
                }
                if (app.bundleURL) {
                    NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
                    cfg.activates = YES;
                    [[NSWorkspace sharedWorkspace] openApplicationAtURL:app.bundleURL configuration:cfg completionHandler:nil];
                }
                if (out) {
                    const QString nm = app.localizedName ? QString::fromNSString(app.localizedName) : clean;
                    *out = {true, QStringLiteral("✓ 已切换到 ") + nm};
                }
                return true;
            }
        }
        if (NSURL *url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:clean.toNSString()]) {
            NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
            cfg.activates = YES;
            [[NSWorkspace sharedWorkspace] openApplicationAtURL:url configuration:cfg completionHandler:nil];
            if (out) *out = {true, QStringLiteral("✓ 正在打开 ") + AppDockModel::displayNameForTarget(QString::fromNSString(url.path))};
            return true;
        }
    }
    return false;
}

} // namespace AppDockMac
