#include "desktopactions.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Carbon/Carbon.h>
#import <CoreAudio/CoreAudio.h>
#include <dlfcn.h>

namespace DesktopActions {

namespace {
class SafeProcess final : public QProcess {
public:
    using QProcess::QProcess;
    ~SafeProcess() override
    {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(300);
        }
    }
};
} // namespace

bool macAccessibilityTrusted(bool promptUser)
{
    @autoreleasepool {
        if (promptUser) {
            NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
            return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
        }
        return AXIsProcessTrusted();
    }
}

Outcome macSendKeyChord(int virtualKey, bool cmd, bool ctrl, bool alt, bool shift, const QString &label)
{
    @autoreleasepool {
        if (!AXIsProcessTrusted()) {
            macAccessibilityTrusted(true);
            return {false, QStringLiteral("请在系统设置 → 辅助功能中允许 FloatingBall 控制按键")};
        }
        CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        CGEventRef down = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(virtualKey), true);
        CGEventRef up = CGEventCreateKeyboardEvent(source, static_cast<CGKeyCode>(virtualKey), false);
        if (!down || !up) {
            if (down) CFRelease(down);
            if (up) CFRelease(up);
            if (source) CFRelease(source);
            return {false, label + QStringLiteral(" 发送失败")};
        }
        uint64_t flags = 0;
        if (cmd) flags |= kCGEventFlagMaskCommand;
        if (ctrl) flags |= kCGEventFlagMaskControl;
        if (alt) flags |= kCGEventFlagMaskAlternate;
        if (shift) flags |= kCGEventFlagMaskShift;
        CGEventSetFlags(down, static_cast<CGEventFlags>(flags));
        CGEventSetFlags(up, static_cast<CGEventFlags>(flags));
        CGEventPost(kCGHIDEventTap, down);
        CGEventPost(kCGHIDEventTap, up);
        CFRelease(down);
        CFRelease(up);
        if (source) CFRelease(source);
        return {true, QStringLiteral("✓ ") + label};
    }
}

Outcome macShowOverview()
{
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("Mission Control")})) {
        return {true, QStringLiteral("✓ 已展开调度中心")};
    }
    return {false, QStringLiteral("无法唤起 macOS 调度中心")};
}

Outcome macShowDesktop()
{
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("Mission Control"),
                                 QStringLiteral("--args"), QStringLiteral("1")})) {
        return {true, QStringLiteral("✓ 显示桌面")};
    }
    return {false, QStringLiteral("无法切换显示桌面")};
}

Outcome macSwitchInputMethod()
{
    @autoreleasepool {
        CFArrayRef list = TISCreateInputSourceList(nullptr, false);
        if (list) {
            TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
            NSString *currentId = current
                ? (__bridge NSString *)TISGetInputSourceProperty(current, kTISPropertyInputSourceID)
                : nil;
            NSMutableArray *selectable = [NSMutableArray array];
            const CFIndex count = CFArrayGetCount(list);
            NSInteger activeIdx = -1;
            for (CFIndex i = 0; i < count; ++i) {
                TISInputSourceRef src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, i);
                CFStringRef category = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceCategory);
                CFBooleanRef canSelect = (CFBooleanRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceIsSelectCapable);
                CFBooleanRef enabled = (CFBooleanRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceIsEnabled);
                if (!category || !CFEqual(category, kTISCategoryKeyboardInputSource)) continue;
                if (!canSelect || !CFBooleanGetValue(canSelect)) continue;
                if (!enabled || !CFBooleanGetValue(enabled)) continue;
                NSString *srcId = (__bridge NSString *)TISGetInputSourceProperty(src, kTISPropertyInputSourceID);
                if (currentId && [srcId isEqualToString:currentId]) activeIdx = selectable.count;
                [selectable addObject:(__bridge id)src];
            }
            if (selectable.count >= 2) {
                const NSInteger nextIdx = (activeIdx + 1) % selectable.count;
                TISInputSourceRef next = (__bridge TISInputSourceRef)selectable[nextIdx];
                const OSStatus err = TISSelectInputSource(next);
                NSString *name = (__bridge NSString *)TISGetInputSourceProperty(next, kTISPropertyLocalizedName);
                if (current) CFRelease(current);
                CFRelease(list);
                if (err == noErr) {
                    return {true, QStringLiteral("✓ 输入法：") + (name ? QString::fromNSString(name) : QStringLiteral("已切换"))};
                }
            } else {
                if (current) CFRelease(current);
                CFRelease(list);
            }
        }
        // Fallback: Ctrl + Space
        return macSendKeyChord(0x31, false, true, false, false, QStringLiteral("切换输入法"));
    }
}

Outcome macToggleVirtualKeyboard()
{
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("KeyboardViewer")}) ||
        QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("/System/Library/Input Methods/AssistiveControl.app")})) {
        return {true, QStringLiteral("✓ 已切换辅助屏幕键盘")};
    }
    return {false, QStringLiteral("无法打开系统辅助键盘")};
}

namespace {

AudioDeviceID defaultOutputAudioDevice()
{
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 size = sizeof(dev);
    AudioObjectPropertyAddress addr = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, &dev) != noErr)
        return kAudioObjectUnknown;
    return dev;
}

bool readCoreAudioVolume(int *outPercent, bool *outMuted)
{
    const AudioDeviceID dev = defaultOutputAudioDevice();
    if (dev == kAudioObjectUnknown) return false;

    Float32 vol = 0.0f;
    UInt32 volSize = sizeof(vol);
    AudioObjectPropertyAddress volAddr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    if (!AudioObjectHasProperty(dev, &volAddr)) return false;
    if (AudioObjectGetPropertyData(dev, &volAddr, 0, nullptr, &volSize, &vol) != noErr) return false;

    UInt32 muted = 0;
    UInt32 muteSize = sizeof(muted);
    AudioObjectPropertyAddress muteAddr = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    if (AudioObjectHasProperty(dev, &muteAddr)) {
        AudioObjectGetPropertyData(dev, &muteAddr, 0, nullptr, &muteSize, &muted);
    }

    if (outPercent) *outPercent = qBound(0, qRound(vol * 100.0f), 100);
    if (outMuted) *outMuted = (muted != 0);
    return true;
}

bool writeCoreAudioVolume(int percent)
{
    const AudioDeviceID dev = defaultOutputAudioDevice();
    if (dev == kAudioObjectUnknown) return false;

    AudioObjectPropertyAddress volAddr = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    Boolean settable = false;
    if (!AudioObjectHasProperty(dev, &volAddr) ||
        AudioObjectIsPropertySettable(dev, &volAddr, &settable) != noErr || !settable) {
        return false;
    }

    const Float32 target = static_cast<Float32>(qBound(0, percent, 100)) / 100.0f;
    if (AudioObjectSetPropertyData(dev, &volAddr, 0, nullptr, sizeof(target), &target) != noErr)
        return false;

    AudioObjectPropertyAddress muteAddr = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    Boolean muteSettable = false;
    if (AudioObjectHasProperty(dev, &muteAddr) &&
        AudioObjectIsPropertySettable(dev, &muteAddr, &muteSettable) == noErr && muteSettable) {
        UInt32 muteVal = (percent == 0) ? 1 : 0;
        AudioObjectSetPropertyData(dev, &muteAddr, 0, nullptr, sizeof(muteVal), &muteVal);
    }
    return true;
}

bool toggleCoreAudioMute(int *outPercent, bool *outMuted)
{
    const AudioDeviceID dev = defaultOutputAudioDevice();
    if (dev == kAudioObjectUnknown) return false;
    AudioObjectPropertyAddress muteAddr = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    Boolean muteSettable = false;
    if (!AudioObjectHasProperty(dev, &muteAddr) ||
        AudioObjectIsPropertySettable(dev, &muteAddr, &muteSettable) != noErr || !muteSettable) {
        return false;
    }
    UInt32 muted = 0;
    UInt32 size = sizeof(muted);
    if (AudioObjectGetPropertyData(dev, &muteAddr, 0, nullptr, &size, &muted) != noErr)
        return false;
    muted = muted ? 0 : 1;
    if (AudioObjectSetPropertyData(dev, &muteAddr, 0, nullptr, sizeof(muted), &muted) != noErr)
        return false;
    return readCoreAudioVolume(outPercent, outMuted);
}

using DisplayServicesGetBrightnessFn = int (*)(CGDirectDisplayID, float *);
using DisplayServicesSetBrightnessFn = int (*)(CGDirectDisplayID, float);

void resolveDisplayServices(DisplayServicesGetBrightnessFn *getFn, DisplayServicesSetBrightnessFn *setFn)
{
    static void *handle = dlopen("/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices", RTLD_LAZY);
    static DisplayServicesGetBrightnessFn sGet = handle
        ? reinterpret_cast<DisplayServicesGetBrightnessFn>(dlsym(handle, "DisplayServicesGetBrightness"))
        : nullptr;
    static DisplayServicesSetBrightnessFn sSet = handle
        ? reinterpret_cast<DisplayServicesSetBrightnessFn>(dlsym(handle, "DisplayServicesSetBrightness"))
        : nullptr;
    if (getFn) *getFn = sGet;
    if (setFn) *setFn = sSet;
}

} // namespace

void macQueryVolume(std::function<void(int, bool, bool)> callback)
{
    int vol = 50;
    bool muted = false;
    if (readCoreAudioVolume(&vol, &muted)) {
        if (callback) callback(vol, muted, true);
        return;
    }
    auto proc = new SafeProcess(qApp);
    QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), qApp,
                     [proc, callback](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        proc->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            callback(50, false, false);
            return;
        }
        const QRegularExpression volRe(QStringLiteral("output volume:([0-9]+)"));
        const QRegularExpression muteRe(QStringLiteral("output muted:(true|false)"));
        const auto vm = volRe.match(out);
        const auto mm = muteRe.match(out);
        const int v = vm.hasMatch() ? qBound(0, vm.captured(1).toInt(), 100) : 50;
        const bool m = mm.hasMatch() && mm.captured(1) == QLatin1String("true");
        callback(v, m, vm.hasMatch());
    });
    proc->start(QStringLiteral("/usr/bin/osascript"),
                {QStringLiteral("-e"), QStringLiteral("get volume settings")});
}

void macSetVolume(int percent)
{
    const int clamped = qBound(0, percent, 100);
    if (writeCoreAudioVolume(clamped)) return;
    const QString script = (clamped == 0)
        ? QStringLiteral("set volume output volume 0 with output muted")
        : QStringLiteral("set volume output volume %1 without output muted").arg(clamped);
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
}

void macToggleMute(std::function<void(int, bool, bool)> callback)
{
    int vol = 50;
    bool muted = false;
    if (toggleCoreAudioMute(&vol, &muted)) {
        if (callback) callback(vol, muted, true);
        return;
    }
    auto proc = new SafeProcess(qApp);
    QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), qApp,
                     [proc, callback](int, QProcess::ExitStatus) {
        proc->deleteLater();
        if (callback) macQueryVolume(callback);
    });
    proc->start(QStringLiteral("/usr/bin/osascript"),
                {QStringLiteral("-e"),
                 QStringLiteral("set volume output muted not (output muted of (get volume settings))")});
}

void macQueryBrightness(std::function<void(int, bool)> callback)
{
    if (!callback) return;
    DisplayServicesGetBrightnessFn getFn = nullptr;
    resolveDisplayServices(&getFn, nullptr);
    if (getFn) {
        float val = 0.0f;
        if (getFn(CGMainDisplayID(), &val) == 0 && val >= 0.0f && val <= 1.01f) {
            callback(qBound(1, qRound(val * 100.0f), 100), true);
            return;
        }
    }
    callback(50, false);
}

void macSetBrightness(int percent)
{
    DisplayServicesSetBrightnessFn setFn = nullptr;
    resolveDisplayServices(nullptr, &setFn);
    if (setFn) {
        const float target = static_cast<float>(qBound(1, percent, 100)) / 100.0f;
        setFn(CGMainDisplayID(), target);
    }
}

namespace {
bool isMacshotInstalled()
{
    @autoreleasepool {
        NSURL *appUrl = [[NSWorkspace sharedWorkspace]
            URLForApplicationWithBundleIdentifier:@"com.sw33tlie.macshot.macshot"];
        if (appUrl) return true;
        NSFileManager *fm = [NSFileManager defaultManager];
        NSArray<NSString *> *paths = @[
            @"/Applications/macshot.app",
            [NSHomeDirectory() stringByAppendingPathComponent:@"Applications/macshot.app"],
            [NSHomeDirectory() stringByAppendingPathComponent:@"Applications/Home Manager Apps/macshot.app"]
        ];
        for (NSString *p in paths) {
            if ([fm fileExistsAtPath:p]) return true;
        }
        return false;
    }
}

bool isMacshotUrlSchemeEnabled()
{
    @autoreleasepool {
        NSUserDefaults *defs = [[NSUserDefaults alloc] initWithSuiteName:@"com.sw33tlie.macshot.macshot"];
        return defs && [defs boolForKey:@"urlSchemeEnabled"];
    }
}

bool openMacUrl(const QString &urlStr)
{
    @autoreleasepool {
        NSURL *url = [NSURL URLWithString:urlStr.toNSString()];
        return url && [[NSWorkspace sharedWorkspace] openURL:url];
    }
}
} // namespace

Outcome macTriggerNativeScreenshot()
{
    if (isMacshotInstalled()) {
        if (isMacshotUrlSchemeEnabled() && openMacUrl(QStringLiteral("macshot://capture"))) {
            return {true, {}};
        }
        if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                    {QStringLiteral("-a"), QStringLiteral("Screenshot")})) {
            return {true, QStringLiteral("已打开系统截屏（在 macshot 开启 URL scheme 后可直连）")};
        }
    }
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("Screenshot")})) {
        return {true, {}};
    }
    return {false, QStringLiteral("无法启动 macOS 截屏工具")};
}

Outcome macTriggerNativeScreenRecord()
{
    if (isMacshotInstalled()) {
        if (isMacshotUrlSchemeEnabled() && openMacUrl(QStringLiteral("macshot://record"))) {
            return {true, {}};
        }
        if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                    {QStringLiteral("-a"), QStringLiteral("Screenshot")})) {
            return {true, QStringLiteral("已打开系统录屏（在 macshot 开启 URL scheme 后可直连）")};
        }
    }
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("Screenshot")})) {
        return {true, {}};
    }
    return {false, QStringLiteral("无法启动 macOS 录屏工具")};
}

Outcome macTriggerNativeScrollCapture()
{
    if (isMacshotInstalled() && isMacshotUrlSchemeEnabled() &&
        openMacUrl(QStringLiteral("macshot://scroll-capture"))) {
        return {true, {}};
    }
    return macTriggerNativeScreenshot();
}

} // namespace DesktopActions
