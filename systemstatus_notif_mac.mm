#include "systemstatus.h"
#include "appdock.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include <sqlite3.h>
#include <atomic>
#include <memory>

namespace AppDockMac {
QIcon iconForPath(const QString &path);
}

namespace SystemStatusMac {

namespace {

constexpr qint64 kCoreDataEpochOffset = 978307200LL;

QString qStringFromCF(CFTypeRef ref)
{
    if (!ref || CFGetTypeID(ref) != CFStringGetTypeID()) return {};
    return QString::fromNSString((__bridge NSString *)ref);
}

QString resolveAppPathForHint(const QString &bundleId, const QString &appName)
{
    static QHash<QString, QString> pathCache;
    const QString cacheKey = bundleId.trimmed().toLower() + QLatin1Char('|') + appName.trimmed().toLower();
    if (cacheKey == QLatin1String("|")) return {};
    const auto it = pathCache.constFind(cacheKey);
    if (it != pathCache.constEnd()) return it.value();

    QString found;
    @autoreleasepool {
        const QString bid = bundleId.trimmed();
        if (!bid.isEmpty()) {
            NSURL *url = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:bid.toNSString()];
            if (url && url.path) found = QString::fromNSString(url.path);
            if (found.isEmpty()) {
                for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
                    if (app.bundleIdentifier && app.bundleURL && app.bundleURL.path &&
                        QString::fromNSString(app.bundleIdentifier).compare(bid, Qt::CaseInsensitive) == 0) {
                        found = QString::fromNSString(app.bundleURL.path);
                        break;
                    }
                }
            }
        }
        const QString nm = appName.trimmed();
        if (found.isEmpty() && !nm.isEmpty()) {
            for (NSRunningApplication *app in [NSWorkspace sharedWorkspace].runningApplications) {
                if (app.localizedName && app.bundleURL && app.bundleURL.path &&
                    QString::fromNSString(app.localizedName).compare(nm, Qt::CaseInsensitive) == 0) {
                    found = QString::fromNSString(app.bundleURL.path);
                    break;
                }
            }
        }
        if (found.isEmpty() && !nm.isEmpty()) {
            static const QStringList dirs{
                QStringLiteral("/Applications"),
                QStringLiteral("/Applications/Nix Apps"),
                QDir::homePath() + QStringLiteral("/Applications"),
                QDir::homePath() + QStringLiteral("/Applications/Home Manager Apps"),
                QStringLiteral("/System/Applications"),
                QStringLiteral("/System/Applications/Utilities"),
                QStringLiteral("/System/Library/CoreServices")
            };
            for (const QString &d : dirs) {
                const QString cand = d + QLatin1Char('/') + nm + QStringLiteral(".app");
                if (QFileInfo::exists(cand)) { found = cand; break; }
            }
        }
    }
    if (!found.isEmpty()) pathCache.insert(cacheKey, found);
    return found;
}

QString displayNameForBundleId(const QString &bundleId)
{
    const QString path = resolveAppPathForHint(bundleId, QString());
    if (!path.isEmpty()) {
        @autoreleasepool {
            NSString *disp = [[NSFileManager defaultManager] displayNameAtPath:path.toNSString()];
            if (disp && disp.length > 0) {
                QString q = QString::fromNSString(disp);
                if (q.endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) q.chop(4);
                if (!q.isEmpty()) return q;
            }
        }
        return AppDockModel::displayNameForTarget(path);
    }
    const QString trimmed = bundleId.trimmed();
    const int dot = trimmed.lastIndexOf(QLatin1Char('.'));
    return (dot >= 0 && dot + 1 < trimmed.size()) ? trimmed.mid(dot + 1) : trimmed;
}

bool isBundleFrontmost(const QString &bundleId)
{
    if (bundleId.isEmpty()) return false;
    @autoreleasepool {
        NSRunningApplication *front = [NSWorkspace sharedWorkspace].frontmostApplication;
        return front && front.bundleIdentifier &&
               QString::fromNSString(front.bundleIdentifier).compare(bundleId, Qt::CaseInsensitive) == 0;
    }
}

QString usernotedDbPath()
{
    return QDir::homePath() +
        QStringLiteral("/Library/Group Containers/group.com.apple.usernoted/db2/db");
}

struct RawNotifRecord {
    qint64 recId = 0;
    QString bundleId;
    double deliveredDate = 0.0;
    QByteArray plistBytes;
};

bool openUsernotedReadOnly(const QString &path, sqlite3 **dbOut)
{
    if (!dbOut || !QFileInfo(path).isReadable()) return false;
    if (sqlite3_open_v2(path.toUtf8().constData(), dbOut, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK) {
        sqlite3_busy_timeout(*dbOut, 120);
        return true;
    }
    if (*dbOut) { sqlite3_close(*dbOut); *dbOut = nullptr; }
    const QByteArray uri = QStringLiteral("file:%1?mode=ro&immutable=1").arg(path).toUtf8();
    if (sqlite3_open_v2(uri.constData(), dbOut, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) == SQLITE_OK)
        return true;
    if (*dbOut) { sqlite3_close(*dbOut); *dbOut = nullptr; }
    return false;
}

QList<RawNotifRecord> queryUsernotedRecords(qint64 afterRecId, int limit, qint64 *maxRecIdOut)
{
    QList<RawNotifRecord> out;
    sqlite3 *db = nullptr;
    if (!openUsernotedReadOnly(usernotedDbPath(), &db)) return out;

    sqlite3_stmt *maxStmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(rec_id), 0) FROM record", -1, &maxStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(maxStmt) == SQLITE_ROW && maxRecIdOut) *maxRecIdOut = sqlite3_column_int64(maxStmt, 0);
        sqlite3_finalize(maxStmt);
    }

    const char *sql = (afterRecId >= 0)
        ? "SELECT r.rec_id, a.identifier, r.delivered_date, r.data "
          "FROM record r JOIN app a ON r.app_id = a.app_id WHERE r.rec_id > ? ORDER BY r.rec_id ASC LIMIT ?"
        : "SELECT r.rec_id, a.identifier, r.delivered_date, r.data "
          "FROM record r JOIN app a ON r.app_id = a.app_id ORDER BY r.rec_id DESC LIMIT ?";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (afterRecId >= 0) {
            sqlite3_bind_int64(stmt, 1, afterRecId);
            sqlite3_bind_int(stmt, 2, limit);
        } else {
            sqlite3_bind_int(stmt, 1, limit);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            RawNotifRecord rec;
            rec.recId = sqlite3_column_int64(stmt, 0);
            if (const unsigned char *txt = sqlite3_column_text(stmt, 1))
                rec.bundleId = QString::fromUtf8(reinterpret_cast<const char *>(txt));
            rec.deliveredDate = sqlite3_column_double(stmt, 2);
            const void *blob = sqlite3_column_blob(stmt, 3);
            const int len = sqlite3_column_bytes(stmt, 3);
            if (blob && len > 0) rec.plistBytes = QByteArray(static_cast<const char *>(blob), len);
            out.append(rec);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return out;
}

struct DockBadgeEntry {
    QString bundleId;
    QString appName;
    QString badge;
};

QList<DockBadgeEntry> queryDockBadgesBlocking()
{
    QList<DockBadgeEntry> out;
    if (!AXIsProcessTrusted()) return out;
    @autoreleasepool {
        NSArray<NSRunningApplication *> *docks =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.dock"];
        if (docks.count == 0) return out;
        AXUIElementRef axDock = AXUIElementCreateApplication(docks.firstObject.processIdentifier);
        if (!axDock) return out;
        AXUIElementSetMessagingTimeout(axDock, 0.08f);

        CFTypeRef childrenRef = nullptr;
        if (AXUIElementCopyAttributeValue(axDock, kAXChildrenAttribute, &childrenRef) == kAXErrorSuccess && childrenRef) {
            if (CFGetTypeID(childrenRef) == CFArrayGetTypeID()) {
                for (CFIndex i = 0; i < CFArrayGetCount((CFArrayRef)childrenRef); ++i) {
                    AXUIElementRef child = (AXUIElementRef)CFArrayGetValueAtIndex((CFArrayRef)childrenRef, i);
                    CFTypeRef roleRef = nullptr;
                    if (AXUIElementCopyAttributeValue(child, kAXRoleAttribute, &roleRef) != kAXErrorSuccess || !roleRef)
                        continue;
                    const bool isList = (qStringFromCF(roleRef) == QLatin1String("AXList"));
                    CFRelease(roleRef);
                    if (!isList) continue;

                    CFTypeRef itemsRef = nullptr;
                    if (AXUIElementCopyAttributeValue(child, kAXChildrenAttribute, &itemsRef) == kAXErrorSuccess && itemsRef) {
                        if (CFGetTypeID(itemsRef) == CFArrayGetTypeID()) {
                            for (CFIndex j = 0; j < CFArrayGetCount((CFArrayRef)itemsRef); ++j) {
                                AXUIElementRef item = (AXUIElementRef)CFArrayGetValueAtIndex((CFArrayRef)itemsRef, j);
                                CFTypeRef badgeRef = nullptr;
                                if (AXUIElementCopyAttributeValue(item, CFSTR("AXStatusLabel"), &badgeRef) != kAXErrorSuccess || !badgeRef)
                                    continue;
                                const QString badge = qStringFromCF(badgeRef).trimmed();
                                CFRelease(badgeRef);
                                if (badge.isEmpty()) continue;

                                QString title, bid;
                                CFTypeRef tRef = nullptr;
                                if (AXUIElementCopyAttributeValue(item, kAXTitleAttribute, &tRef) == kAXErrorSuccess && tRef) {
                                    title = qStringFromCF(tRef).trimmed();
                                    CFRelease(tRef);
                                }
                                CFTypeRef uRef = nullptr;
                                if (AXUIElementCopyAttributeValue(item, kAXURLAttribute, &uRef) == kAXErrorSuccess && uRef) {
                                    if (CFGetTypeID(uRef) == CFURLGetTypeID()) {
                                        NSURL *u = (__bridge NSURL *)uRef;
                                        if (u.path) {
                                            if (NSBundle *b = [NSBundle bundleWithPath:u.path])
                                                if (b.bundleIdentifier) bid = QString::fromNSString(b.bundleIdentifier);
                                        }
                                    }
                                    CFRelease(uRef);
                                }
                                if (!bid.isEmpty() || !title.isEmpty()) out.append({bid, title, badge});
                            }
                        }
                        CFRelease(itemsRef);
                    }
                }
            }
            CFRelease(childrenRef);
        }
        CFRelease(axDock);
    }
    return out;
}

qint64 currentDbStamp()
{
    const QString db = usernotedDbPath();
    QFileInfo fi(db);
    QFileInfo wal(db + QStringLiteral("-wal"));
    if (!fi.exists()) return -1;
    return fi.lastModified().toMSecsSinceEpoch() ^ (fi.size() << 4) ^
           (wal.exists() ? (wal.lastModified().toMSecsSinceEpoch() ^ (wal.size() << 8)) : 0);
}

struct NotifWatcherPack {
    QPointer<SystemStatusMonitor> monitor;
    QPointer<QTimer> notifTimer;
    QPointer<QTimer> dockTimer;
    qint64 lastRecId = -1;
    qint64 lastDbStamp = -1;
    bool seeded = false;
    QHash<QString, QString> lastDockBadges;
    QHash<QString, qint64> lastUsernotedMs;
    std::atomic<bool> alive{true};
    std::atomic<bool> notifBusy{false};
    std::atomic<bool> dockBusy{false};
};

} // namespace

QIcon lookupAppIcon(const QString &desktopEntry, const QString &appName)
{
    const QString path = resolveAppPathForHint(desktopEntry, appName);
    return path.isEmpty() ? QIcon() : AppDockMac::iconForPath(path);
}

bool parseUserNotedPlist(const QByteArray &plistBytes, const QString &bundleId,
                         double deliveredDate, NotificationItem *out)
{
    if (!out || plistBytes.isEmpty()) return false;
    @autoreleasepool {
        NSData *data = [NSData dataWithBytesNoCopy:const_cast<char *>(plistBytes.constData())
                                            length:static_cast<NSUInteger>(plistBytes.size())
                                      freeWhenDone:NO];
        NSError *err = nil;
        id root = [NSPropertyListSerialization propertyListWithData:data options:0 format:nullptr error:&err];
        if (![root isKindOfClass:[NSDictionary class]]) return false;

        NSDictionary *dict = (NSDictionary *)root;
        NSDictionary *req = [dict[@"req"] isKindOfClass:[NSDictionary class]] ? dict[@"req"] : nil;
        QString appId = bundleId.trimmed();
        if (appId.isEmpty() && [dict[@"app"] isKindOfClass:[NSString class]])
            appId = QString::fromNSString(dict[@"app"]).trimmed();
        if (appId.contains(QLatin1String("floatingball"), Qt::CaseInsensitive)) return false;

        auto strFromReq = [&](NSString *key) -> QString {
            id v = req ? req[key] : nil;
            return [v isKindOfClass:[NSString class]] ? QString::fromNSString((NSString *)v).simplified() : QString();
        };
        const QString title = strFromReq(@"titl");
        const QString subt = strFromReq(@"subt");
        const QString body = strFromReq(@"body");
        if (title.isEmpty() && subt.isEmpty() && body.isEmpty()) return false;

        NotificationItem item;
        item.desktopEntry = appId;
        item.appName = displayNameForBundleId(appId);
        item.summary = !title.isEmpty() ? title : subt;
        if (!title.isEmpty() && !subt.isEmpty() && subt != title)
            item.body = body.isEmpty() ? subt : (subt + QStringLiteral(" · ") + body);
        else
            item.body = body;
        item.timestamp = deliveredDate > 1000.0
            ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(deliveredDate) + kCoreDataEpochOffset)
            : QDateTime::currentDateTime();
        item.unread = true;
        *out = item;
        return true;
    }
}

void *startNotificationWatchers(SystemStatusMonitor *monitor)
{
    if (!monitor) return nullptr;
    auto pack = std::make_shared<NotifWatcherPack>();
    pack->monitor = monitor;

    auto *notifTimer = new QTimer(monitor);
    notifTimer->setInterval(1500);
    auto pollNotifDb = [pack]() {
        if (!pack->alive.load() || !pack->monitor) return;
        const qint64 stamp = currentDbStamp();
        if (stamp < 0 || (pack->seeded && stamp == pack->lastDbStamp)) return;
        bool expected = false;
        if (!pack->notifBusy.compare_exchange_strong(expected, true)) return;

        const bool isSeed = !pack->seeded;
        const qint64 afterId = isSeed ? -1 : pack->lastRecId;
        QPointer<SystemStatusMonitor> mon = pack->monitor;
        std::shared_ptr<NotifWatcherPack> ref = pack;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            qint64 maxId = afterId;
            const QList<RawNotifRecord> recs = queryUsernotedRecords(afterId, isSeed ? 12 : 24, &maxId);
            if (!mon || !ref->alive.load()) { ref->notifBusy.store(false); return; }
            QMetaObject::invokeMethod(mon, [ref, mon, recs, maxId, isSeed, stamp]() {
                ref->notifBusy.store(false);
                if (!mon || !ref->alive.load()) return;
                ref->lastDbStamp = stamp;
                if (maxId > ref->lastRecId) ref->lastRecId = maxId;
                ref->seeded = true;
                const int n = recs.size();
                for (int idx = 0; idx < n; ++idx) {
                    const RawNotifRecord &r = isSeed ? recs[n - 1 - idx] : recs[idx];
                    NotificationItem item;
                    if (!parseUserNotedPlist(r.plistBytes, r.bundleId, r.deliveredDate, &item)) continue;
                    if (isSeed) {
                        item.unread = false;
                    } else {
                        item.unread = !isBundleFrontmost(item.desktopEntry);
                        ref->lastUsernotedMs.insert(item.desktopEntry.toLower(), QDateTime::currentMSecsSinceEpoch());
                    }
                    mon->pushNotification(item);
                }
            }, Qt::QueuedConnection);
        });
    };
    QObject::connect(notifTimer, &QTimer::timeout, monitor, pollNotifDb);
    notifTimer->start();
    pack->notifTimer = notifTimer;
    QTimer::singleShot(250, monitor, pollNotifDb);

    auto *dockTimer = new QTimer(monitor);
    dockTimer->setInterval(3000);
    auto pollDockBadges = [pack]() {
        if (!pack->alive.load() || !pack->monitor || !AXIsProcessTrusted()) return;
        bool expected = false;
        if (!pack->dockBusy.compare_exchange_strong(expected, true)) return;
        QPointer<SystemStatusMonitor> mon = pack->monitor;
        std::shared_ptr<NotifWatcherPack> ref = pack;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
            const QList<DockBadgeEntry> badges = queryDockBadgesBlocking();
            if (!mon || !ref->alive.load()) { ref->dockBusy.store(false); return; }
            QMetaObject::invokeMethod(mon, [ref, mon, badges]() {
                ref->dockBusy.store(false);
                if (!mon || !ref->alive.load()) return;
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                QHash<QString, QString> current;
                for (const DockBadgeEntry &b : badges) {
                    const QString key = (!b.bundleId.isEmpty() ? b.bundleId : b.appName).toLower();
                    current.insert(key, b.badge);
                    const QString prev = ref->lastDockBadges.value(key);
                    const bool changedUp = !prev.isEmpty() ? (b.badge != prev) : !ref->lastDockBadges.isEmpty();
                    if (changedUp && !isBundleFrontmost(b.bundleId) &&
                        (nowMs - ref->lastUsernotedMs.value(key, 0) > 4000)) {
                        NotificationItem item;
                        item.appName = b.appName.isEmpty() ? displayNameForBundleId(b.bundleId) : b.appName;
                        item.desktopEntry = b.bundleId;
                        item.summary = QStringLiteral("未读角标 (%1)").arg(b.badge);
                        item.body = QStringLiteral("来自 macOS 程序坞提醒");
                        item.timestamp = QDateTime::currentDateTime();
                        item.unread = true;
                        mon->pushNotification(item);
                    }
                }
                for (auto it = ref->lastDockBadges.constBegin(); it != ref->lastDockBadges.constEnd(); ++it) {
                    if (!current.contains(it.key()))
                        mon->markNotificationsReadForApp(it.key(), displayNameForBundleId(it.key()), it.key());
                }
                ref->lastDockBadges = current;
            }, Qt::QueuedConnection);
        });
    };
    QObject::connect(dockTimer, &QTimer::timeout, monitor, pollDockBadges);
    dockTimer->start();
    pack->dockTimer = dockTimer;
    QTimer::singleShot(900, monitor, pollDockBadges);

    return new std::shared_ptr<NotifWatcherPack>(std::move(pack));
}

void stopNotificationWatchers(void *raw)
{
    if (!raw) return;
    auto *holder = static_cast<std::shared_ptr<NotifWatcherPack> *>(raw);
    auto pack = *holder;
    pack->alive.store(false);
    if (pack->notifTimer) { pack->notifTimer->stop(); pack->notifTimer->deleteLater(); }
    if (pack->dockTimer) { pack->dockTimer->stop(); pack->dockTimer->deleteLater(); }
    pack->monitor = nullptr;
    delete holder;
}

} // namespace SystemStatusMac
