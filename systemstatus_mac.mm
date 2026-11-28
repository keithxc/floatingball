#include "systemstatus.h"
#include "desktopactions.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <CoreWLAN/CoreWLAN.h>
#import <Foundation/Foundation.h>
#import <IOBluetooth/IOBluetooth.h>
#import <IOKit/ps/IOPSKeys.h>
#import <IOKit/ps/IOPowerSources.h>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>

namespace SystemStatusMac {

namespace {

int rssiToQuality(NSInteger rssi)
{
    if (rssi == 0) return 0;
    // Map -90 dBm -> 0%, -45 dBm -> 100%
    const int pct = qRound((static_cast<double>(rssi) + 90.0) * (100.0 / 45.0));
    return qBound(5, pct, 100);
}

QString ipv4ForInterface(const QString &iface)
{
    if (iface.isEmpty()) return {};
    const QByteArray target = iface.toUtf8();
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0 || !ifap) return {};
    QString ip;
    for (struct ifaddrs *ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (target != ifa->ifa_name) continue;
        char buf[INET_ADDRSTRLEN]{};
        auto *sin = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
            ip = QString::fromLatin1(buf);
            break;
        }
    }
    freeifaddrs(ifap);
    return ip;
}

bool hasAnyActiveWiredInterface(QString *outName = nullptr)
{
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0 || !ifap) return false;
    bool found = false;
    for (struct ifaddrs *ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if ((ifa->ifa_flags & (IFF_UP | IFF_RUNNING)) != (IFF_UP | IFF_RUNNING)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        const QString name = QString::fromLatin1(ifa->ifa_name);
        if (!name.startsWith(QLatin1String("en"))) continue;
        char buf[INET_ADDRSTRLEN]{};
        auto *sin = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
            const QString ip = QString::fromLatin1(buf);
            if (!ip.startsWith(QLatin1String("169.254."))) {
                found = true;
                if (outName) *outName = name;
                break;
            }
        }
    }
    freeifaddrs(ifap);
    return found;
}

QString securityStringForNetwork(CWNetwork *net)
{
    if (!net) return QStringLiteral("--");
    if ([net supportsSecurity:kCWSecurityWPA3Personal] || [net supportsSecurity:kCWSecurityWPA3Transition])
        return QStringLiteral("WPA3");
    if ([net supportsSecurity:kCWSecurityWPA3Enterprise])
        return QStringLiteral("WPA3-Ent");
    if ([net supportsSecurity:kCWSecurityWPA2Personal] || [net supportsSecurity:kCWSecurityWPAPersonalMixed])
        return QStringLiteral("WPA2");
    if ([net supportsSecurity:kCWSecurityWPA2Enterprise] || [net supportsSecurity:kCWSecurityWPAEnterprise])
        return QStringLiteral("WPA2-Ent");
    if ([net supportsSecurity:kCWSecurityWPAPersonal])
        return QStringLiteral("WPA");
    if ([net supportsSecurity:kCWSecurityWEP])
        return QStringLiteral("WEP");
    return QStringLiteral("--");
}

QString securityStringForInterface(CWInterface *iface)
{
    if (!iface) return QStringLiteral("--");
    switch (iface.security) {
    case kCWSecurityWPA3Personal:
    case kCWSecurityWPA3Transition: return QStringLiteral("WPA3");
    case kCWSecurityWPA3Enterprise: return QStringLiteral("WPA3-Ent");
    case kCWSecurityWPA2Personal:
    case kCWSecurityWPAPersonalMixed: return QStringLiteral("WPA2");
    case kCWSecurityWPA2Enterprise:
    case kCWSecurityWPAEnterprise: return QStringLiteral("WPA2-Ent");
    case kCWSecurityWPAPersonal: return QStringLiteral("WPA");
    case kCWSecurityWEP: return QStringLiteral("WEP");
    default: return QStringLiteral("--");
    }
}

QString bandFreqStringForChannel(CWChannel *ch)
{
    if (!ch) return {};
    switch (ch.channelBand) {
    case kCWChannelBand6GHz: return QStringLiteral("6000 MHz");
    case kCWChannelBand5GHz: return QStringLiteral("5200 MHz");
    case kCWChannelBand2GHz: return QStringLiteral("2412 MHz");
    default: return {};
    }
}

QString iconTypeForBluetoothDevice(IOBluetoothDevice *dev)
{
    if (!dev) return {};
    const BluetoothDeviceClassMajor major = dev.deviceClassMajor;
    const BluetoothDeviceClassMinor minor = dev.deviceClassMinor;
    if (major == kBluetoothDeviceClassMajorAudio) {
        if (minor == kBluetoothDeviceClassMinorAudioLoudspeaker ||
            minor == kBluetoothDeviceClassMinorAudioPortable)
            return QStringLiteral("audio-speakers");
        return QStringLiteral("audio-headset");
    }
    if (major == kBluetoothDeviceClassMajorPeripheral) {
        if (minor & kBluetoothDeviceClassMinorPeripheral1Keyboard)
            return QStringLiteral("input-keyboard");
        if (minor & kBluetoothDeviceClassMinorPeripheral1Pointing)
            return QStringLiteral("input-mouse");
        if (minor == kBluetoothDeviceClassMinorPeripheral2Gamepad ||
            minor == kBluetoothDeviceClassMinorPeripheral2Joystick)
            return QStringLiteral("input-gaming");
    }
    if (major == kBluetoothDeviceClassMajorPhone) return QStringLiteral("phone");
    return {};
}

bool readMacFocusEnabled()
{
    const QString path = QDir::homePath() + QStringLiteral("/Library/DoNotDisturb/DB/Assertions.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    const QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
    for (const QJsonValue &v : data) {
        if (!v.toObject().value(QStringLiteral("storeAssertionRecords")).toArray().isEmpty()) return true;
    }
    return false;
}

using BTSetPowerFn = void (*)(int);
using BTGetPowerFn = int (*)();

void resolveIOBluetoothPower(BTGetPowerFn *getFn, BTSetPowerFn *setFn)
{
    static void *handle = dlopen("/System/Library/Frameworks/IOBluetooth.framework/IOBluetooth", RTLD_LAZY);
    static BTGetPowerFn sGet = handle
        ? reinterpret_cast<BTGetPowerFn>(dlsym(handle, "IOBluetoothPreferenceGetControllerPowerState"))
        : nullptr;
    static BTSetPowerFn sSet = handle
        ? reinterpret_cast<BTSetPowerFn>(dlsym(handle, "IOBluetoothPreferenceSetControllerPowerState"))
        : nullptr;
    if (getFn) *getFn = sGet;
    if (setFn) *setFn = sSet;
}

bool bluetoothPoweredNow()
{
    BTGetPowerFn getFn = nullptr;
    resolveIOBluetoothPower(&getFn, nullptr);
    if (getFn) return getFn() != 0;
    IOBluetoothHostController *ctrl = [IOBluetoothHostController defaultController];
    return ctrl && ctrl.powerState == kBluetoothHCIPowerStateON;
}

} // namespace

void populateSnapshot(SystemStatusSnapshot *out)
{
    if (!out) return;
    @autoreleasepool {
        // 1. Battery via IOKit Power Sources (present on MacBooks, absent on Mac mini)
        CFTypeRef blob = IOPSCopyPowerSourcesInfo();
        if (blob) {
            CFArrayRef sources = IOPSCopyPowerSourcesList(blob);
            bool foundBattery = false;
            if (sources) {
                const CFIndex count = CFArrayGetCount(sources);
                for (CFIndex i = 0; i < count; ++i) {
                    CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(sources, i));
                    if (!desc) continue;
                    CFStringRef type = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSTypeKey));
                    if (!type || !CFEqual(type, CFSTR(kIOPSInternalBatteryType))) continue;
                    foundBattery = true;
                    int cur = 0, maxCap = 100;
                    if (CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSCurrentCapacityKey)))
                        CFNumberGetValue(n, kCFNumberIntType, &cur);
                    if (CFNumberRef n = (CFNumberRef)CFDictionaryGetValue(desc, CFSTR(kIOPSMaxCapacityKey)))
                        CFNumberGetValue(n, kCFNumberIntType, &maxCap);
                    out->batteryPresent = true;
                    out->batteryPercent = maxCap > 0 ? qBound(0, qRound(cur * 100.0 / maxCap), 100) : 0;
                    CFStringRef psState = (CFStringRef)CFDictionaryGetValue(desc, CFSTR(kIOPSPowerSourceStateKey));
                    out->batteryPluggedIn = (psState && CFEqual(psState, CFSTR(kIOPSACPowerValue)));
                    break;
                }
                CFRelease(sources);
            }
            CFRelease(blob);
            if (!foundBattery) out->batteryPresent = false;
        }

        // 2. macOS Low Power Mode -> powerProfile
        if (@available(macOS 12.0, *)) {
            out->powerProfile = [NSProcessInfo processInfo].isLowPowerModeEnabled
                ? QStringLiteral("power-saver")
                : QStringLiteral("balanced");
        } else {
            out->powerProfile = QStringLiteral("balanced");
        }

        // 3. Wi-Fi & Ethernet via CoreWLAN + getifaddrs
        CWInterface *wifi = [CWWiFiClient sharedWiFiClient].interface;
        const QString wifiIfName = (wifi && wifi.interfaceName) ? QString::fromNSString(wifi.interfaceName) : QString();
        const QString wifiIp = ipv4ForInterface(wifiIfName);
        if (wifi && wifi.powerOn && !wifiIp.isEmpty()) {
            const int qual = rssiToQuality(wifi.rssiValue);
            QString ssid = wifi.ssid ? QString::fromNSString(wifi.ssid) : QString();
            if (ssid.isEmpty()) ssid = QStringLiteral("Wi-Fi");
            out->networkConnected = true;
            out->signalQuality = qual > 0 ? qual : 85;
            out->networkText = QStringLiteral("%1 (%2%)").arg(ssid).arg(out->signalQuality);
        } else {
            QString wiredName;
            if (hasAnyActiveWiredInterface(&wiredName)) {
                out->networkConnected = true;
                out->signalQuality = 100;
                out->networkText = QStringLiteral("有线网络 (%1)").arg(wiredName);
            } else {
                out->networkConnected = false;
                out->signalQuality = 0;
                out->networkText = QStringLiteral("未联网");
            }
        }

        // 4. Bluetooth via IOBluetooth
        IOBluetoothHostController *btCtrl = [IOBluetoothHostController defaultController];
        out->bluetoothAvailable = (btCtrl != nil);
        out->bluetoothPowered = out->bluetoothAvailable && bluetoothPoweredNow();

        // 5. Focus / Do Not Disturb
        out->dndEnabled = readMacFocusEnabled();

        // 6. Current keyboard input source ("双" vs "EN")
        TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
        if (current) {
            NSString *srcId = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyInputSourceID);
            NSString *name = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyLocalizedName);
            const QString qId = srcId ? QString::fromNSString(srcId) : QString();
            const QString qName = name ? QString::fromNSString(name) : QString();
            const bool isAscii = qId.contains(QLatin1String("ABC"), Qt::CaseInsensitive) ||
                                 qId.contains(QLatin1String("US"), Qt::CaseInsensitive) ||
                                 qId.contains(QLatin1String("British"), Qt::CaseInsensitive);
            out->inputPresent = !qId.isEmpty();
            out->inputLabel = isAscii ? QStringLiteral("EN") : QStringLiteral("双");
            out->inputDescription = qName.isEmpty() ? out->inputLabel : qName;
            CFRelease(current);
        }
    }
}

QString toggleWifi()
{
    @autoreleasepool {
        CWInterface *iface = [CWWiFiClient sharedWiFiClient].interface;
        if (!iface) return QStringLiteral("未检测到 macOS Wi-Fi 网卡");
        const BOOL target = !iface.powerOn;
        NSError *err = nil;
        if ([iface setPower:target error:&err]) {
            return target ? QStringLiteral("✓ 已开启 Wi-Fi") : QStringLiteral("✓ 已关闭 Wi-Fi");
        }
        const QString dev = iface.interfaceName ? QString::fromNSString(iface.interfaceName) : QStringLiteral("en0");
        QProcess::startDetached(QStringLiteral("/usr/sbin/networksetup"),
                                {QStringLiteral("-setairportpower"), dev,
                                 target ? QStringLiteral("on") : QStringLiteral("off")});
        return target ? QStringLiteral("✓ 正在开启 Wi-Fi") : QStringLiteral("✓ 已关闭 Wi-Fi");
    }
}

QString toggleBluetooth()
{
    @autoreleasepool {
        BTSetPowerFn setFn = nullptr;
        resolveIOBluetoothPower(nullptr, &setFn);
        if (!setFn) {
            openSystemSettings(QStringLiteral("bluetooth"));
            return QStringLiteral("已打开蓝牙设置");
        }
        const bool turnOn = !bluetoothPoweredNow();
        setFn(turnOn ? 1 : 0);
        return turnOn ? QStringLiteral("✓ 蓝牙已开启") : QStringLiteral("✓ 蓝牙已关闭");
    }
}

QString toggleDoNotDisturb()
{
    openSystemSettings(QStringLiteral("focus"));
    return QStringLiteral("✓ 已打开专注模式设置");
}

QString cyclePowerProfile()
{
    openSystemSettings(QStringLiteral("battery"));
    return QStringLiteral("✓ 已打开电池与低电量模式设置");
}

bool openSystemSettings(const QString &moduleName)
{
    @autoreleasepool {
        QString url = QStringLiteral("x-apple.systempreferences:");
        const QString m = moduleName.toLower();
        if (m.contains(QLatin1String("wifi")) || m.contains(QLatin1String("network")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.wifi-settings-extension");
        else if (m.contains(QLatin1String("bluetooth")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.BluetoothSettings");
        else if (m.contains(QLatin1String("sharing")) || m.contains(QLatin1String("hotspot")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Sharing-Settings.extension");
        else if (m.contains(QLatin1String("notification")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Notifications-Settings.extension");
        else if (m.contains(QLatin1String("focus")) || m.contains(QLatin1String("dnd")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Focus-Settings.extension");
        else if (m.contains(QLatin1String("battery")) || m.contains(QLatin1String("power")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Battery-Settings.extension");
        else if (m.contains(QLatin1String("sound")) || m.contains(QLatin1String("audio")) || m.contains(QLatin1String("pulse")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Sound-Settings.extension");
        else if (m.contains(QLatin1String("display")) || m.contains(QLatin1String("kscreen")) || m.contains(QLatin1String("night")))
            url = QStringLiteral("x-apple.systempreferences:com.apple.Displays-Settings.extension");

        NSURL *nsUrl = [NSURL URLWithString:url.toNSString()];
        if (nsUrl && [[NSWorkspace sharedWorkspace] openURL:nsUrl]) return true;
        return QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                       {QStringLiteral("-a"), QStringLiteral("System Settings")});
    }
}

bool lockScreen()
{
    using LockFn = void (*)();
    static void *handle = dlopen("/System/Library/PrivateFrameworks/login.framework/login", RTLD_LAZY);
    static LockFn lockFn = handle ? reinterpret_cast<LockFn>(dlsym(handle, "SACLockScreenImmediate")) : nullptr;
    if (lockFn) {
        lockFn();
        return true;
    }
    return QProcess::startDetached(QStringLiteral("/usr/bin/pmset"), {QStringLiteral("displaysleepnow")});
}

bool openPowerMenu()
{
    @autoreleasepool {
        // Send native AppleEvent kAEShowShutdownDialog to loginwindow -> genuine macOS 27 Shutdown dialog
        const char *bundleId = "com.apple.loginwindow";
        AEAddressDesc targetDesc;
        if (AECreateDesc(typeApplicationBundleID, bundleId, strlen(bundleId), &targetDesc) == noErr) {
            AppleEvent event;
            if (AECreateAppleEvent(kCoreEventClass, kAEShowShutdownDialog, &targetDesc,
                                   kAutoGenerateReturnID, kAnyTransactionID, &event) == noErr) {
                AppleEvent reply;
                const OSStatus err = AESendMessage(&event, &reply, kAENoReply, kAEDefaultTimeout);
                AEDisposeDesc(&event);
                AEDisposeDesc(&targetDesc);
                if (err == noErr) return true;
            } else {
                AEDisposeDesc(&targetDesc);
            }
        }
        return QProcess::startDetached(QStringLiteral("/usr/bin/osascript"),
                                       {QStringLiteral("-e"),
                                        QStringLiteral("tell application \"loginwindow\" to «event aevtrsdn»")});
    }
}

bool openSpotlight()
{
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                   {QStringLiteral("-a"), QStringLiteral("Spotlight")});
}

void queryWifiAsync(QObject *context, bool rescan, std::function<void(const MacWifiState &)> cb)
{
    if (!cb) return;
    QPointer<QObject> guard(context);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        MacWifiState st;
        @autoreleasepool {
            CWInterface *iface = [CWWiFiClient sharedWiFiClient].interface;
            if (iface) {
                st.powered = iface.powerOn;
                st.iface = iface.interfaceName ? QString::fromNSString(iface.interfaceName) : QString();
                st.ip = ipv4ForInterface(st.iface);
                QString curSsid = iface.ssid ? QString::fromNSString(iface.ssid) : QString();
                if (curSsid.isEmpty() && st.powered && !st.ip.isEmpty()) {
                    // Fallback when macOS location permission redacts CWInterface.ssid
                    curSsid = QStringLiteral("已连接");
                }
                const int curSig = rssiToQuality(iface.rssiValue);
                const QString curRate = iface.transmitRate > 0.0
                    ? QStringLiteral("%1 Mbit/s").arg(qRound(iface.transmitRate))
                    : QString();
                const QString curFreq = bandFreqStringForChannel(iface.wlanChannel);
                const QString curSec = securityStringForInterface(iface);

                QHash<QString, int> seenIdx;
                if (st.powered && !st.ip.isEmpty() && !curSsid.isEmpty()) {
                    MacWifiNetwork cur;
                    cur.active = true;
                    cur.ssid = curSsid;
                    cur.signal = curSig > 0 ? curSig : 85;
                    cur.security = curSec;
                    cur.rate = curRate;
                    cur.freq = curFreq;
                    seenIdx.insert(curSsid, 0);
                    st.networks.append(cur);
                }

                if (st.powered) {
                    NSSet<CWNetwork *> *nets = nil;
                    if (rescan) {
                        NSError *err = nil;
                        nets = [iface scanForNetworksWithName:nil error:&err];
                    } else {
                        nets = [iface cachedScanResults];
                        if (!nets || nets.count == 0) {
                            NSError *err = nil;
                            nets = [iface scanForNetworksWithName:nil error:&err];
                        }
                    }
                    for (CWNetwork *net in nets) {
                        if (!net.ssid || net.ssid.length == 0) continue;
                        const QString ssid = QString::fromNSString(net.ssid);
                        const int sig = rssiToQuality(net.rssiValue);
                        const QString sec = securityStringForNetwork(net);
                        const QString freq = bandFreqStringForChannel(net.wlanChannel);
                        if (seenIdx.contains(ssid)) {
                            MacWifiNetwork &ex = st.networks[seenIdx[ssid]];
                            ex.signal = qMax(ex.signal, sig);
                            if (ex.freq.isEmpty()) ex.freq = freq;
                            if (ex.security.isEmpty() || ex.security == QLatin1String("--")) ex.security = sec;
                        } else {
                            MacWifiNetwork e;
                            e.ssid = ssid;
                            e.signal = sig;
                            e.security = sec;
                            e.freq = freq;
                            e.active = (!curSsid.isEmpty() && ssid == curSsid);
                            if (e.active) e.rate = curRate;
                            seenIdx.insert(ssid, st.networks.size());
                            st.networks.append(e);
                        }
                    }
                }
                std::sort(st.networks.begin(), st.networks.end(), [](const MacWifiNetwork &a, const MacWifiNetwork &b) {
                    if (a.active != b.active) return a.active;
                    return a.signal > b.signal;
                });
            }
        }
        if (guard) {
            QMetaObject::invokeMethod(guard, [cb = std::move(cb), st]() { cb(st); }, Qt::QueuedConnection);
        }
    });
}

void connectWifiAsync(QObject *context, const QString &ssid, const QString &password,
                      std::function<void(bool ok, const QString &msg)> cb)
{
    QPointer<QObject> guard(context);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        bool ok = false;
        QString msg;
        @autoreleasepool {
            CWInterface *iface = [CWWiFiClient sharedWiFiClient].interface;
            if (iface) {
                NSError *scanErr = nil;
                NSSet<CWNetwork *> *nets = [iface scanForNetworksWithName:ssid.toNSString() error:&scanErr];
                CWNetwork *target = nets.anyObject;
                if (target) {
                    NSError *assocErr = nil;
                    NSString *pass = password.isEmpty() ? nil : password.toNSString();
                    ok = [iface associateToNetwork:target password:pass error:&assocErr];
                    if (ok) {
                        msg = QStringLiteral("✓ 已连接 ") + ssid;
                    } else {
                        msg = assocErr && assocErr.localizedDescription
                            ? QStringLiteral("连接失败：") + QString::fromNSString(assocErr.localizedDescription)
                            : QStringLiteral("连接 %1 失败").arg(ssid);
                    }
                } else {
                    const QString dev = iface.interfaceName ? QString::fromNSString(iface.interfaceName) : QStringLiteral("en0");
                    QStringList args{QStringLiteral("-setairportnetwork"), dev, ssid};
                    if (!password.isEmpty()) args.append(password);
                    ok = (QProcess::execute(QStringLiteral("/usr/sbin/networksetup"), args) == 0);
                    msg = ok ? (QStringLiteral("✓ 已连接 ") + ssid) : QStringLiteral("连接 %1 失败").arg(ssid);
                }
            } else {
                msg = QStringLiteral("未找到 Wi-Fi 网卡");
            }
        }
        if (guard && cb) {
            QMetaObject::invokeMethod(guard, [cb = std::move(cb), ok, msg]() { cb(ok, msg); }, Qt::QueuedConnection);
        }
    });
}

bool readNetInterfaceBytes(const QString &iface, quint64 *rxBytes, quint64 *txBytes)
{
    if (iface.isEmpty()) return false;
    const QByteArray target = iface.toUtf8();
    struct ifaddrs *ifap = nullptr;
    if (getifaddrs(&ifap) != 0 || !ifap) return false;
    bool ok = false;
    for (struct ifaddrs *ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || !ifa->ifa_addr || !ifa->ifa_data) continue;
        if (ifa->ifa_addr->sa_family != AF_LINK) continue;
        if (target != ifa->ifa_name) continue;
        const auto *data = reinterpret_cast<const struct if_data *>(ifa->ifa_data);
        if (rxBytes) *rxBytes = static_cast<quint64>(data->ifi_ibytes);
        if (txBytes) *txBytes = static_cast<quint64>(data->ifi_obytes);
        ok = true;
        break;
    }
    freeifaddrs(ifap);
    return ok;
}

bool isInternetSharingActive()
{
    return !ipv4ForInterface(QStringLiteral("bridge100")).isEmpty();
}

void queryBluetoothAsync(QObject *context,
                         std::function<void(bool powered, const QList<MacBluetoothDevice> &)> cb)
{
    if (!cb) return;
    QPointer<QObject> guard(context);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        bool powered = false;
        QList<MacBluetoothDevice> list;
        @autoreleasepool {
            powered = bluetoothPoweredNow();
            NSArray *paired = [IOBluetoothDevice pairedDevices];
            for (id obj in paired) {
                if (![obj isKindOfClass:[IOBluetoothDevice class]]) continue;
                IOBluetoothDevice *dev = (IOBluetoothDevice *)obj;
                MacBluetoothDevice d;
                d.address = dev.addressString ? QString::fromNSString(dev.addressString) : QString();
                d.name = dev.name ? QString::fromNSString(dev.name) : d.address;
                if (d.name.isEmpty()) continue;
                d.connected = dev.isConnected;
                d.iconType = iconTypeForBluetoothDevice(dev);
                list.append(d);
            }
            std::sort(list.begin(), list.end(), [](const MacBluetoothDevice &a, const MacBluetoothDevice &b) {
                if (a.connected != b.connected) return a.connected;
                return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
            });
        }
        if (guard) {
            QMetaObject::invokeMethod(guard, [cb = std::move(cb), powered, list]() { cb(powered, list); },
                                      Qt::QueuedConnection);
        }
    });
}

void toggleBluetoothDeviceAsync(QObject *context, const QString &address, bool currentlyConnected,
                                std::function<void(bool ok, const QString &msg)> cb)
{
    QPointer<QObject> guard(context);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        bool ok = false;
        QString msg;
        @autoreleasepool {
            IOBluetoothDevice *dev = [IOBluetoothDevice deviceWithAddressString:address.toNSString()];
            if (!dev) {
                msg = QStringLiteral("未找到该蓝牙设备");
            } else {
                const QString name = dev.name ? QString::fromNSString(dev.name) : address;
                if (currentlyConnected) {
                    ok = ([dev closeConnection] == kIOReturnSuccess);
                    msg = ok ? QStringLiteral("✓ 已断开 %1").arg(name) : QStringLiteral("断开 %1 失败").arg(name);
                } else {
                    ok = ([dev openConnection:nil withPageTimeout:5000 authenticationRequired:NO] == kIOReturnSuccess);
                    msg = ok ? QStringLiteral("✓ 已连接 %1").arg(name)
                             : QStringLiteral("连接 %1 未完成（设备可能未开机/未在附近）").arg(name);
                }
            }
        }
        if (guard && cb) {
            QMetaObject::invokeMethod(guard, [cb = std::move(cb), ok, msg]() { cb(ok, msg); }, Qt::QueuedConnection);
        }
    });
}

void queryNowPlayingAsync(QObject *context,
                          std::function<void(const QString &title, const QString &artist,
                                             bool playing, bool visible)> cb)
{
    if (!cb) return;
    using GetInfoFn = void (*)(dispatch_queue_t, void (^)(CFDictionaryRef));
    using GetPlayingFn = void (*)(dispatch_queue_t, void (^)(Boolean));
    static void *mr = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY);
    static GetInfoFn getInfo = mr ? reinterpret_cast<GetInfoFn>(dlsym(mr, "MRMediaRemoteGetNowPlayingInfo")) : nullptr;
    static GetPlayingFn getPlaying = mr ? reinterpret_cast<GetPlayingFn>(dlsym(mr, "MRMediaRemoteGetNowPlayingApplicationIsPlaying")) : nullptr;
    if (!getInfo) {
        cb({}, {}, false, false);
        return;
    }

    QPointer<QObject> guard(context);
    dispatch_queue_t q = dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    getInfo(q, ^(CFDictionaryRef info) {
        QString title;
        QString artist;
        bool playing = false;
        if (info) {
            NSDictionary *dict = (__bridge NSDictionary *)info;
            NSString *t = dict[@"kMRMediaRemoteNowPlayingInfoTitle"];
            NSString *a = dict[@"kMRMediaRemoteNowPlayingInfoArtist"];
            if (!a || a.length == 0) a = dict[@"kMRMediaRemoteNowPlayingInfoAlbum"];
            NSNumber *rate = dict[@"kMRMediaRemoteNowPlayingInfoPlaybackRate"];
            if ([t isKindOfClass:[NSString class]]) title = QString::fromNSString(t).trimmed();
            if ([a isKindOfClass:[NSString class]]) artist = QString::fromNSString(a).trimmed();
            if ([rate isKindOfClass:[NSNumber class]]) playing = (rate.doubleValue > 0.01);
        }
        const bool visible = !title.isEmpty();
        if (visible && getPlaying) {
            getPlaying(q, ^(Boolean isPlaying) {
                if (guard) {
                    QMetaObject::invokeMethod(guard, [cb, title, artist, p = (playing || isPlaying), visible]() {
                        cb(title, artist, p, visible);
                    }, Qt::QueuedConnection);
                }
            });
        } else if (guard) {
            QMetaObject::invokeMethod(guard, [cb, title, artist, playing, visible]() {
                cb(title, artist, playing, visible);
            }, Qt::QueuedConnection);
        }
    });
}

void sendMediaCommand(const QString &method)
{
    using SendCmdFn = Boolean (*)(unsigned int, CFDictionaryRef);
    static void *mr = dlopen("/System/Library/PrivateFrameworks/MediaRemote.framework/MediaRemote", RTLD_LAZY);
    static SendCmdFn sendCmd = mr ? reinterpret_cast<SendCmdFn>(dlsym(mr, "MRMediaRemoteSendCommand")) : nullptr;
    if (!sendCmd) return;
    // MRCommand: TogglePlayPause = 2, NextTrack = 4, PreviousTrack = 5
    unsigned int cmd = 2;
    if (method == QLatin1String("Next")) cmd = 4;
    else if (method == QLatin1String("Previous")) cmd = 5;
    sendCmd(cmd, nullptr);
}

} // namespace SystemStatusMac
