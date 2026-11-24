#include "desktopactions.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

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

void macQueryVolume(std::function<void(int, bool, bool)> callback)
{
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
        const int vol = vm.hasMatch() ? qBound(0, vm.captured(1).toInt(), 100) : 50;
        const bool muted = mm.hasMatch() && mm.captured(1) == QLatin1String("true");
        callback(vol, muted, vm.hasMatch());
    });
    proc->start(QStringLiteral("/usr/bin/osascript"),
                {QStringLiteral("-e"), QStringLiteral("get volume settings")});
}

void macSetVolume(int percent)
{
    const int clamped = qBound(0, percent, 100);
    const QString script = (clamped == 0)
        ? QStringLiteral("set volume output volume 0 with output muted")
        : QStringLiteral("set volume output volume %1 without output muted").arg(clamped);
    QProcess::startDetached(QStringLiteral("/usr/bin/osascript"), {QStringLiteral("-e"), script});
}

void macToggleMute(std::function<void(int, bool, bool)> callback)
{
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

} // namespace DesktopActions
