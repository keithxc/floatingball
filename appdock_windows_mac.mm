#include "appdock.h"
#include "systemstatus.h"

#include <QGuiApplication>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <unistd.h>

extern "C" AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID *outWindowID) __attribute__((weak_import));

namespace AppDockMac {

QIcon iconForPath(const QString &path);
QIcon iconFromNSImage(void *nsImage);
bool isSelfBundle(const QString &bundleId, const QString &name, const QString &path);

namespace {

struct RawMacWindow {
    pid_t pid = 0;
    CGWindowID windowId = 0;
    QString bundleId;
    QString appKey;
    QString appName;
    QString appPath;
    QString title;
    bool active = false;
    bool minimized = false;
};

struct RawScanResult {
    QList<RawMacWindow> windows;
    bool activeFullscreen = false;
};

QString qStringFromCF(CFTypeRef ref)
{
    if (!ref || CFGetTypeID(ref) != CFStringGetTypeID()) return {};
    return QString::fromNSString((__bridge NSString *)ref);
}

bool boolFromCF(CFTypeRef ref)
{
    if (!ref) return false;
    if (CFGetTypeID(ref) == CFBooleanGetTypeID()) return CFBooleanGetValue((CFBooleanRef)ref);
    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
        int val = 0;
        if (CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &val)) return val != 0;
    }
    return false;
}

bool axWindowIsFullscreen(AXUIElementRef win)
{
    if (!win) return false;
    CFTypeRef fsVal = nullptr;
    if (AXUIElementCopyAttributeValue(win, CFSTR("AXFullScreen"), &fsVal) == kAXErrorSuccess && fsVal) {
        const bool isFs = boolFromCF(fsVal);
        CFRelease(fsVal);
        return isFs;
    }
    return false;
}

bool cgRectCoversAnyScreen(const CGRect &bounds)
{
    if (bounds.size.width < 320 || bounds.size.height < 240) return false;
    uint32_t displayCount = 0;
    CGDirectDisplayID displays[16];
    if (CGGetOnlineDisplayList(16, displays, &displayCount) != kCGErrorSuccess) return false;
    const CGPoint center = CGPointMake(CGRectGetMidX(bounds), CGRectGetMidY(bounds));
    for (uint32_t i = 0; i < displayCount; ++i) {
        const CGRect db = CGDisplayBounds(displays[i]);
        if (!CGRectContainsPoint(db, center)) continue;
        if (bounds.size.width >= db.size.width - 2.0 && bounds.size.height >= db.size.height - 2.0)
            return true;
    }
    return false;
}

RawScanResult scanLiveWindowsBlocking(bool useAccessibility)
{
    RawScanResult result;
    @autoreleasepool {
        struct AppMeta {
            pid_t pid = 0;
            QString bundleId, appKey, name, path;
            bool isActive = false, isHidden = false;
        };
        QHash<pid_t, AppMeta> appByPid;
        QList<pid_t> regularPids;
        pid_t frontPid = 0;

        const pid_t selfPid = ::getpid();
        for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
            if (app.activationPolicy != NSApplicationActivationPolicyRegular || app.isTerminated) continue;
            if (app.processIdentifier == selfPid) continue;
            const QString bid = app.bundleIdentifier ? QString::fromNSString(app.bundleIdentifier) : QString();
            const QString name = app.localizedName ? QString::fromNSString(app.localizedName) : QString();
            const QString path = (app.bundleURL && app.bundleURL.path) ? QString::fromNSString(app.bundleURL.path) : QString();
            if (isSelfBundle(bid, name, path)) continue;

            AppMeta meta{app.processIdentifier, bid, {}, name.isEmpty() ? AppDockModel::displayNameForTarget(path) : name,
                         path, static_cast<bool>(app.isActive), static_cast<bool>(app.isHidden)};
            meta.appKey = (!bid.isEmpty() ? bid : meta.name).toLower();
            if (meta.isActive) frontPid = meta.pid;
            appByPid.insert(meta.pid, meta);
            regularPids.append(meta.pid);
        }

        struct CgWinInfo {
            pid_t pid = 0;
            CGWindowID wid = 0;
            QString title;
            CGRect bounds = CGRectZero;
        };
        QHash<CGWindowID, CgWinInfo> cgByWid;
        QList<CgWinInfo> cgWindows;

        if (CFArrayRef cgList = CGWindowListCopyWindowInfo(
                kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID)) {
            for (NSDictionary *dict in (__bridge NSArray *)cgList) {
                NSNumber *layerNum = dict[(id)kCGWindowLayer];
                if (!layerNum || layerNum.intValue != 0) continue;
                NSNumber *pidNum = dict[(id)kCGWindowOwnerPID];
                NSNumber *widNum = dict[(id)kCGWindowNumber];
                if (!pidNum || !widNum) continue;
                const pid_t pid = static_cast<pid_t>(pidNum.intValue);
                if (!appByPid.contains(pid)) continue;
                NSNumber *alphaNum = dict[(id)kCGWindowAlpha];
                if (alphaNum && alphaNum.doubleValue <= 0.01) continue;

                CGRect bounds = CGRectZero;
                CFDictionaryRef bDict = (__bridge CFDictionaryRef)dict[(id)kCGWindowBounds];
                if (!bDict || !CGRectMakeWithDictionaryRepresentation(bDict, &bounds)) continue;
                if (bounds.size.width < 80.0 || bounds.size.height < 60.0) continue;

                CgWinInfo info;
                info.pid = pid;
                info.wid = static_cast<CGWindowID>(widNum.unsignedIntValue);
                NSString *wName = dict[(id)kCGWindowName];
                if ([wName isKindOfClass:[NSString class]]) info.title = QString::fromNSString(wName).trimmed();
                info.bounds = bounds;
                cgByWid.insert(info.wid, info);
                cgWindows.append(info);
            }
            CFRelease(cgList);
        }

        if (frontPid > 0) {
            const AppMeta &frontMeta = appByPid[frontPid];
            for (const CgWinInfo &cw : cgWindows) {
                if (cw.pid != frontPid) continue;
                if (frontMeta.appKey != QLatin1String("com.apple.finder") && cgRectCoversAnyScreen(cw.bounds))
                    result.activeFullscreen = true;
                break;
            }
        }

        QSet<CGWindowID> seenWids;
        if (useAccessibility && AXIsProcessTrusted()) {
            for (pid_t pid : regularPids) {
                const AppMeta &meta = appByPid[pid];
                AXUIElementRef appEl = AXUIElementCreateApplication(pid);
                if (!appEl) continue;
                AXUIElementSetMessagingTimeout(appEl, 0.06f);

                CGWindowID focusedWid = 0;
                if (meta.isActive) {
                    CFTypeRef focRef = nullptr;
                    if (AXUIElementCopyAttributeValue(appEl, kAXFocusedWindowAttribute, &focRef) != kAXErrorSuccess || !focRef)
                        AXUIElementCopyAttributeValue(appEl, kAXMainWindowAttribute, &focRef);
                    if (focRef) {
                        if (axWindowIsFullscreen((AXUIElementRef)focRef)) result.activeFullscreen = true;
                        if (_AXUIElementGetWindow) _AXUIElementGetWindow((AXUIElementRef)focRef, &focusedWid);
                        CFRelease(focRef);
                    }
                }

                CFTypeRef winsRef = nullptr;
                if (AXUIElementCopyAttributeValue(appEl, kAXWindowsAttribute, &winsRef) == kAXErrorSuccess && winsRef) {
                    if (CFGetTypeID(winsRef) == CFArrayGetTypeID()) {
                        const CFIndex count = CFArrayGetCount((CFArrayRef)winsRef);
                        for (CFIndex i = 0; i < count; ++i) {
                            AXUIElementRef w = (AXUIElementRef)CFArrayGetValueAtIndex((CFArrayRef)winsRef, i);
                            CGWindowID wid = 0;
                            if (!w || !_AXUIElementGetWindow || _AXUIElementGetWindow(w, &wid) != kAXErrorSuccess ||
                                wid == 0 || seenWids.contains(wid))
                                continue;

                            CFTypeRef roleRef = nullptr;
                            if (AXUIElementCopyAttributeValue(w, kAXRoleAttribute, &roleRef) == kAXErrorSuccess && roleRef) {
                                const QString role = qStringFromCF(roleRef);
                                CFRelease(roleRef);
                                if (!role.isEmpty() && role != QLatin1String("AXWindow")) continue;
                            }
                            CFTypeRef subRef = nullptr;
                            if (AXUIElementCopyAttributeValue(w, kAXSubroleAttribute, &subRef) == kAXErrorSuccess && subRef) {
                                const QString sub = qStringFromCF(subRef);
                                CFRelease(subRef);
                                if (!sub.isEmpty() && sub != QLatin1String("AXStandardWindow") &&
                                    sub != QLatin1String("AXDialog") && sub != QLatin1String("AXSystemDialog"))
                                    continue;
                            }

                            QString title;
                            CFTypeRef titleRef = nullptr;
                            if (AXUIElementCopyAttributeValue(w, kAXTitleAttribute, &titleRef) == kAXErrorSuccess && titleRef) {
                                title = qStringFromCF(titleRef).trimmed();
                                CFRelease(titleRef);
                            }
                            if (title.isEmpty() && cgByWid.contains(wid)) title = cgByWid[wid].title;

                            bool minimized = meta.isHidden;
                            CFTypeRef minRef = nullptr;
                            if (AXUIElementCopyAttributeValue(w, kAXMinimizedAttribute, &minRef) == kAXErrorSuccess && minRef) {
                                minimized = minimized || boolFromCF(minRef);
                                CFRelease(minRef);
                            }
                            if (!minimized && !cgByWid.contains(wid) && title.isEmpty()) continue;

                            RawMacWindow rw{pid, wid, meta.bundleId, meta.appKey, meta.name, meta.path, title,
                                            meta.isActive && !minimized && (focusedWid != 0 ? (wid == focusedWid) : (i == 0)),
                                            minimized};
                            seenWids.insert(wid);
                            result.windows.append(rw);
                        }
                    }
                    CFRelease(winsRef);
                }
                CFRelease(appEl);
            }
        }

        bool frontAssigned = std::any_of(result.windows.begin(), result.windows.end(),
                                         [](const RawMacWindow &w) { return w.active; });
        for (const CgWinInfo &cw : cgWindows) {
            if (seenWids.contains(cw.wid)) continue;
            const AppMeta &meta = appByPid[cw.pid];
            const bool isAct = meta.isActive && !frontAssigned;
            if (isAct) frontAssigned = true;
            seenWids.insert(cw.wid);
            result.windows.append({cw.pid, cw.wid, meta.bundleId, meta.appKey, meta.name,
                                   meta.path, cw.title, isAct, meta.isHidden});
        }

        // Stable ascending CGWindowID order preserves spatial bearings across focus switches!
        std::sort(result.windows.begin(), result.windows.end(),
                  [](const RawMacWindow &a, const RawMacWindow &b) { return a.windowId < b.windowId; });
    }
    return result;
}

QList<DockAppEntry> materializeEntriesOnGuiThread(const QList<RawMacWindow> &rawList)
{
    QList<DockAppEntry> out;
    out.reserve(rawList.size());
    for (const RawMacWindow &rw : rawList) {
        DockAppEntry e;
        e.id = !rw.bundleId.isEmpty() ? rw.bundleId : rw.appName;
        e.appKey = rw.appKey;
        e.name = rw.appName;
        e.caption = rw.title;
        e.launchTarget = rw.appPath;
        e.windowId = QStringLiteral("mac:%1:%2").arg(static_cast<qlonglong>(rw.pid)).arg(rw.windowId);
        if (!rw.appPath.isEmpty()) e.icon = iconForPath(rw.appPath);
        if (e.icon.isNull()) {
            @autoreleasepool {
                NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:rw.pid];
                if (app && app.icon) e.icon = iconFromNSImage((__bridge void *)app.icon);
            }
        }
        e.running = true;
        e.active = rw.active;
        e.minimized = rw.minimized;
        out.append(e);
    }
    return out;
}

struct WindowObserverPack {
    NSMutableArray *tokens = nil;
    QPointer<QTimer> pollTimer;
    std::atomic<bool> alive{true};
    std::atomic<bool> scanInFlight{false};
};

void dispatchAsyncWindowScan(AppDockModel *model, const std::shared_ptr<WindowObserverPack> &pack)
{
    if (!model || !pack || !pack->alive.load()) return;
    bool expected = false;
    if (!pack->scanInFlight.compare_exchange_strong(expected, true)) return;
    QPointer<AppDockModel> guard(model);
    std::shared_ptr<WindowObserverPack> ref = pack;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const RawScanResult scan = scanLiveWindowsBlocking(true);
        if (guard && ref->alive.load()) {
            QMetaObject::invokeMethod(guard, [guard, ref, scan]() {
                ref->scanInFlight.store(false);
                if (!guard || !ref->alive.load()) return;
                guard->applyMacWindowSnapshot(materializeEntriesOnGuiThread(scan.windows), scan.activeFullscreen);
            }, Qt::QueuedConnection);
        } else {
            ref->scanInFlight.store(false);
        }
    });
}

} // namespace

QList<DockAppEntry> openWindowsFast()
{
    return materializeEntriesOnGuiThread(scanLiveWindowsBlocking(false).windows);
}

void *installWorkspaceObservers(AppDockModel *model)
{
    if (!model) return nullptr;
    @autoreleasepool {
        auto pack = std::make_shared<WindowObserverPack>();
        pack->tokens = [[NSMutableArray alloc] init];
        NSNotificationCenter *nc = [NSWorkspace sharedWorkspace].notificationCenter;
        QPointer<AppDockModel> guard(model);

        auto triggerScan = [guard, pack](int delayMs) {
            if (!guard || !pack->alive.load()) return;
            QTimer::singleShot(delayMs, guard, [guard, pack] {
                if (guard && pack->alive.load()) dispatchAsyncWindowScan(guard, pack);
            });
        };

        NSArray<NSNotificationName> *names = @[
            NSWorkspaceDidLaunchApplicationNotification,
            NSWorkspaceDidTerminateApplicationNotification,
            NSWorkspaceDidActivateApplicationNotification,
            NSWorkspaceDidHideApplicationNotification,
            NSWorkspaceDidUnhideApplicationNotification,
            NSWorkspaceActiveSpaceDidChangeNotification
        ];
        for (NSNotificationName n in names) {
            id token = [nc addObserverForName:n object:nil queue:[NSOperationQueue mainQueue]
                                   usingBlock:^(NSNotification *note) {
                if (!guard || !pack->alive.load()) return;
                if ([note.name isEqualToString:NSWorkspaceDidActivateApplicationNotification]) {
                    NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
                    if (app && app.activationPolicy == NSApplicationActivationPolicyRegular) {
                        const QString bid = app.bundleIdentifier ? QString::fromNSString(app.bundleIdentifier) : QString();
                        const QString name = app.localizedName ? QString::fromNSString(app.localizedName) : QString();
                        const QString path = (app.bundleURL && app.bundleURL.path) ? QString::fromNSString(app.bundleURL.path) : QString();
                        if (!isSelfBundle(bid, name, path)) {
                            guard->handleMacAppActivated((!bid.isEmpty() ? bid : name).toLower(), name, bid);
                        }
                    }
                }
                triggerScan(60);
            }];
            if (token) [pack->tokens addObject:token];
        }

        auto *timer = new QTimer(model);
        timer->setInterval(1200);
        QObject::connect(timer, &QTimer::timeout, model, [guard, pack] {
            if (guard && pack->alive.load()) dispatchAsyncWindowScan(guard, pack);
        });
        timer->start();
        pack->pollTimer = timer;
        triggerScan(120);
        return new std::shared_ptr<WindowObserverPack>(std::move(pack));
    }
}

void removeWorkspaceObservers(void *raw)
{
    if (!raw) return;
    @autoreleasepool {
        auto *holder = static_cast<std::shared_ptr<WindowObserverPack> *>(raw);
        auto pack = *holder;
        pack->alive.store(false);
        if (pack->pollTimer) {
            pack->pollTimer->stop();
            pack->pollTimer->deleteLater();
        }
        NSNotificationCenter *nc = [NSWorkspace sharedWorkspace].notificationCenter;
        for (id token in pack->tokens) [nc removeObserver:token];
        [pack->tokens release];
        pack->tokens = nil;
        delete holder;
    }
}

} // namespace AppDockMac

void AppDockModel::applyMacWindowSnapshot(const QList<DockAppEntry> &windows, bool activeFullscreen)
{
    if (m_activeFullscreen != activeFullscreen) {
        m_activeFullscreen = activeFullscreen;
        emit activeFullscreenChanged(activeFullscreen);
    }

    QSet<QString> pinnedSet;
    for (const QString &t : m_pinnedTargets) {
        QString clean = t.trimmed();
        while (clean.size() > 1 && clean.endsWith(QLatin1Char('/'))) clean.chop(1);
        pinnedSet.insert(clean);
    }

    QList<DockAppEntry> next = windows;
    QString activatedKey, activatedName, activatedId;
    for (DockAppEntry &e : next) {
        QString norm = e.launchTarget.trimmed();
        while (norm.size() > 1 && norm.endsWith(QLatin1Char('/'))) norm.chop(1);
        e.pinned = !norm.isEmpty() && pinnedSet.contains(norm);
        if (e.active && !e.appKey.isEmpty()) {
            m_notifiedAppKeys.remove(e.appKey);
            activatedKey = e.appKey;
            activatedName = e.name;
            activatedId = e.id;
        }
        e.demandsAttention = !e.active && !e.appKey.isEmpty() && m_notifiedAppKeys.contains(e.appKey);
    }

    auto sameSnapshot = [&]() {
        if (!m_macWindowCacheReady || m_macRunningWindows.size() != next.size()) return false;
        for (int i = 0; i < next.size(); ++i) {
            const auto &a = m_macRunningWindows[i];
            const auto &b = next[i];
            if (a.windowId != b.windowId || a.caption != b.caption || a.name != b.name ||
                a.active != b.active || a.minimized != b.minimized ||
                a.demandsAttention != b.demandsAttention || a.pinned != b.pinned)
                return false;
        }
        return true;
    };

    const bool changed = !sameSnapshot();
    m_macRunningWindows = next;
    m_macWindowCacheReady = true;
    if (!activatedKey.isEmpty() || !activatedName.isEmpty())
        emit windowActivated(activatedKey, activatedName, activatedId);
    if (changed) emit appsChanged();
}

void AppDockModel::handleMacAppActivated(const QString &appKey, const QString &appName, const QString &bundleId)
{
    const QString key = appKey.trimmed().toLower();
    bool cleared = false;
    NotificationItem probe;
    probe.appName = appName;
    probe.desktopEntry = bundleId.isEmpty() ? key : bundleId;

    for (auto it = m_notifiedAppKeys.begin(); it != m_notifiedAppKeys.end();) {
        if (*it == key || *it == bundleId.trimmed().toLower() || *it == appName.trimmed().toLower() ||
            SystemStatusMonitor::notificationMatchesApp(probe, *it, *it, *it)) {
            it = m_notifiedAppKeys.erase(it);
            cleared = true;
        } else {
            ++it;
        }
    }

    auto clearIn = [&](QList<DockAppEntry> &list) {
        for (DockAppEntry &e : list) {
            const QString ek = (e.appKey.isEmpty() ? e.id : e.appKey).trimmed().toLower();
            if ((!key.isEmpty() && ek == key) ||
                SystemStatusMonitor::notificationMatchesApp(probe, ek, e.name, e.id)) {
                if (e.demandsAttention) {
                    e.demandsAttention = false;
                    cleared = true;
                }
            }
        }
    };
    clearIn(m_macRunningWindows);
    if (m_hasTestWindows) clearIn(m_testWindows);

    emit windowActivated(key, appName, bundleId);
    if (cleared) emit appsChanged();
}
