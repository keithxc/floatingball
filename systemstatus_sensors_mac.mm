#include "systemstatus.h"

#include <QMetaObject>
#include <QPointer>
#include <QVector>

#import <Carbon/Carbon.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMediaIO/CMIOHardware.h>
#import <Foundation/Foundation.h>
#import <objc/message.h>

#include <atomic>
#include <dlfcn.h>
#include <memory>

namespace SystemStatusMac {

void *startNotificationWatchers(SystemStatusMonitor *monitor);
void stopNotificationWatchers(void *raw);

namespace {

struct CBTime { int hour; int minute; };
struct CBSchedule { CBTime fromTime; CBTime toTime; };
struct CBBlueLightStatus {
    BOOL active;
    BOOL enabled;
    BOOL sunSchedulePermitted;
    int mode;
    CBSchedule schedule;
    unsigned long long disableFlags;
    BOOL available;
    uint8_t padding[128];
};

id createBlueLightClient()
{
    static Class cls = Nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        if (dlopen("/System/Library/PrivateFrameworks/CoreBrightness.framework/CoreBrightness", RTLD_LAZY)) {
            cls = NSClassFromString(@"CBBlueLightClient");
        }
    });
    return cls ? [[cls alloc] init] : nil;
}

bool readCurrentInputSource(QString *labelOut, QString *descOut)
{
    TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
    if (!current) return false;
    NSString *srcId = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyInputSourceID);
    NSString *name = (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyLocalizedName);
    const QString qId = srcId ? QString::fromNSString(srcId) : QString();
    const QString qName = name ? QString::fromNSString(name) : QString();
    CFRelease(current);
    if (qId.isEmpty()) return false;
    const bool isAscii = qId.contains(QLatin1String("ABC"), Qt::CaseInsensitive) ||
                         qId.contains(QLatin1String("US"), Qt::CaseInsensitive) ||
                         qId.contains(QLatin1String("British"), Qt::CaseInsensitive);
    if (labelOut) *labelOut = isAscii ? QStringLiteral("EN") : QStringLiteral("双");
    if (descOut) *descOut = qName.isEmpty() ? (isAscii ? QStringLiteral("EN") : QStringLiteral("双")) : qName;
    return true;
}

struct LiveSensorsPack {
    QPointer<SystemStatusMonitor> monitor;
    QString lastImeLabel;
    void *notifToken = nullptr;
    std::atomic<bool> alive{true};
};

void onInputSourceChanged(CFNotificationCenterRef, void *observer, CFNotificationName,
                          const void *, CFDictionaryRef)
{
    auto *pack = static_cast<LiveSensorsPack *>(observer);
    if (!pack || !pack->alive.load() || !pack->monitor) return;
    QString label, desc;
    if (!readCurrentInputSource(&label, &desc)) return;
    if (label == pack->lastImeLabel) return;
    pack->lastImeLabel = label;
    QPointer<SystemStatusMonitor> mon = pack->monitor;
    QMetaObject::invokeMethod(mon, [mon, label, desc]() {
        if (mon) mon->handleMacInputSourceChanged(label, desc);
    }, Qt::QueuedConnection);
}

} // namespace

void populatePrivacyAndNightShift(SystemStatusSnapshot *out)
{
    if (!out) return;

    // 1. Live Microphone activity via CoreAudio
    bool micActive = false;
    QStringList micUsers;
    AudioObjectPropertyAddress devAddr{
        kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
    };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devAddr, 0, nullptr, &dataSize) == noErr && dataSize > 0) {
        QVector<AudioDeviceID> devs(static_cast<int>(dataSize / sizeof(AudioDeviceID)));
        if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devAddr, 0, nullptr, &dataSize, devs.data()) == noErr) {
            for (AudioDeviceID dev : devs) {
                AudioObjectPropertyAddress inAddr{
                    kAudioDevicePropertyStreams, kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain
                };
                UInt32 inSize = 0;
                if (AudioObjectGetPropertyDataSize(dev, &inAddr, 0, nullptr, &inSize) != noErr || inSize == 0) continue;

                AudioObjectPropertyAddress runAddr{
                    kAudioDevicePropertyDeviceIsRunningSomewhere, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
                };
                UInt32 running = 0, runSize = sizeof(running);
                if (AudioObjectGetPropertyData(dev, &runAddr, 0, nullptr, &runSize, &running) == noErr && running != 0) {
                    micActive = true;
                    CFStringRef nameRef = nullptr;
                    UInt32 nameSize = sizeof(nameRef);
                    AudioObjectPropertyAddress nameAddr{
                        kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
                    };
                    if (AudioObjectGetPropertyData(dev, &nameAddr, 0, nullptr, &nameSize, &nameRef) == noErr && nameRef) {
                        const QString nm = QString::fromNSString((__bridge NSString *)nameRef);
                        CFRelease(nameRef);
                        if (!nm.isEmpty() && !micUsers.contains(nm)) micUsers.append(nm);
                    }
                }
            }
        }
    }
    out->micActive = micActive;
    out->micUsers = micUsers;

    // 2. Live Camera activity via CoreMediaIO
    bool camActive = false;
    QStringList camUsers;
    CMIOObjectPropertyAddress cmAddr{
        kCMIOHardwarePropertyDevices, kCMIOObjectPropertyScopeGlobal, kCMIOObjectPropertyElementMain
    };
    UInt32 cmSize = 0;
    if (CMIOObjectGetPropertyDataSize(kCMIOObjectSystemObject, &cmAddr, 0, nullptr, &cmSize) == noErr && cmSize > 0) {
        QVector<CMIODeviceID> cams(static_cast<int>(cmSize / sizeof(CMIODeviceID)));
        UInt32 used = 0;
        if (CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &cmAddr, 0, nullptr, cmSize, &used, cams.data()) == noErr) {
            for (CMIODeviceID cam : cams) {
                CMIOObjectPropertyAddress runAddr{
                    kCMIODevicePropertyDeviceIsRunningSomewhere, kCMIOObjectPropertyScopeGlobal, kCMIOObjectPropertyElementMain
                };
                UInt32 running = 0, rUsed = 0;
                if (CMIOObjectGetPropertyData(cam, &runAddr, 0, nullptr, sizeof(running), &rUsed, &running) == noErr && running != 0) {
                    camActive = true;
                    camUsers.append(QStringLiteral("摄像头使用中"));
                    break;
                }
            }
        }
    }
    out->cameraActive = camActive;
    out->cameraUsers = camUsers;

    // 3. Native Night Shift state via CoreBrightness
    @autoreleasepool {
        id client = createBlueLightClient();
        SEL selGet = NSSelectorFromString(@"getBlueLightStatus:");
        if (client && [client respondsToSelector:selGet]) {
            CBBlueLightStatus st{};
            ((BOOL (*)(id, SEL, CBBlueLightStatus *))objc_msgSend)(client, selGet, &st);
            out->nightLightEnabled = (st.enabled != NO);
        }
        if (client) [client release];
    }
}

QString toggleNightLight()
{
    @autoreleasepool {
        id client = createBlueLightClient();
        SEL selGet = NSSelectorFromString(@"getBlueLightStatus:");
        SEL selSet = NSSelectorFromString(@"setEnabled:");
        if (client && [client respondsToSelector:selGet] && [client respondsToSelector:selSet]) {
            CBBlueLightStatus st{};
            ((BOOL (*)(id, SEL, CBBlueLightStatus *))objc_msgSend)(client, selGet, &st);
            const BOOL next = !st.enabled;
            ((BOOL (*)(id, SEL, BOOL))objc_msgSend)(client, selSet, next);
            [client release];
            return next ? QStringLiteral("✓ 夜览护眼已开启") : QStringLiteral("✓ 夜览护眼已关闭");
        }
        if (client) [client release];
    }
    openSystemSettings(QStringLiteral("display"));
    return QStringLiteral("✓ 已打开显示器与夜览设置");
}

void *startLiveWatchers(SystemStatusMonitor *monitor)
{
    if (!monitor) return nullptr;
    auto *pack = new LiveSensorsPack();
    pack->monitor = monitor;
    readCurrentInputSource(&pack->lastImeLabel, nullptr);
    CFNotificationCenterAddObserver(
        CFNotificationCenterGetDistributedCenter(),
        pack,
        onInputSourceChanged,
        kTISNotifySelectedKeyboardInputSourceChanged,
        nullptr,
        CFNotificationSuspensionBehaviorDeliverImmediately);
    pack->notifToken = startNotificationWatchers(monitor);
    return pack;
}

void stopLiveWatchers(void *raw)
{
    if (!raw) return;
    auto *pack = static_cast<LiveSensorsPack *>(raw);
    pack->alive.store(false);
    CFNotificationCenterRemoveObserver(
        CFNotificationCenterGetDistributedCenter(),
        pack,
        kTISNotifySelectedKeyboardInputSourceChanged,
        nullptr);
    stopNotificationWatchers(pack->notifToken);
    pack->notifToken = nullptr;
    pack->monitor = nullptr;
    delete pack;
}

} // namespace SystemStatusMac

void SystemStatusMonitor::handleMacInputSourceChanged(const QString &label, const QString &description)
{
    SystemStatusSnapshot next = m_snapshot;
    next.inputPresent = !label.isEmpty();
    next.inputLabel = label;
    next.inputDescription = description;
    emitIfChanged(next);
    if (!label.isEmpty()) emit inputMethodActivity(label);
}
