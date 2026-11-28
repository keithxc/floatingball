#include "appdock.h"

#include <QStringList>

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreFoundation/CoreFoundation.h>

extern "C" AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID *outWindowID) __attribute__((weak_import));

namespace AppDockMac {

namespace {

bool parseWindowIdToken(const QString &token, pid_t *pidOut, CGWindowID *widOut)
{
    if (!token.startsWith(QLatin1String("mac:"))) return false;
    const QStringList parts = token.split(QLatin1Char(':'));
    if (parts.size() != 3) return false;
    bool okPid = false, okWid = false;
    const pid_t pid = static_cast<pid_t>(parts[1].toLongLong(&okPid));
    const CGWindowID wid = static_cast<CGWindowID>(parts[2].toUInt(&okWid));
    if (!okPid || !okWid || pid <= 0 || wid == 0) return false;
    if (pidOut) *pidOut = pid;
    if (widOut) *widOut = wid;
    return true;
}

AXUIElementRef findAxWindow(pid_t pid, CGWindowID targetWid, const QString &fallbackTitle)
{
    if (pid <= 0 || !AXIsProcessTrusted()) return nullptr;
    AXUIElementRef appEl = AXUIElementCreateApplication(pid);
    if (!appEl) return nullptr;
    AXUIElementSetMessagingTimeout(appEl, 0.12f);

    CFTypeRef winsRef = nullptr;
    AXUIElementRef matched = nullptr;
    AXUIElementRef titleFallback = nullptr;
    if (AXUIElementCopyAttributeValue(appEl, kAXWindowsAttribute, &winsRef) == kAXErrorSuccess && winsRef) {
        if (CFGetTypeID(winsRef) == CFArrayGetTypeID()) {
            const CFIndex count = CFArrayGetCount((CFArrayRef)winsRef);
            for (CFIndex i = 0; i < count; ++i) {
                AXUIElementRef w = (AXUIElementRef)CFArrayGetValueAtIndex((CFArrayRef)winsRef, i);
                if (!w) continue;
                if (_AXUIElementGetWindow && targetWid != 0) {
                    CGWindowID wid = 0;
                    if (_AXUIElementGetWindow(w, &wid) == kAXErrorSuccess && wid == targetWid) {
                        matched = (AXUIElementRef)CFRetain(w);
                        break;
                    }
                }
                if (!titleFallback && !fallbackTitle.isEmpty()) {
                    CFTypeRef tRef = nullptr;
                    if (AXUIElementCopyAttributeValue(w, kAXTitleAttribute, &tRef) == kAXErrorSuccess && tRef) {
                        if (CFGetTypeID(tRef) == CFStringGetTypeID() &&
                            QString::fromNSString((__bridge NSString *)tRef) == fallbackTitle) {
                            titleFallback = (AXUIElementRef)CFRetain(w);
                        }
                        CFRelease(tRef);
                    }
                }
            }
        }
        CFRelease(winsRef);
    }
    CFRelease(appEl);
    if (matched) {
        if (titleFallback) CFRelease(titleFallback);
        return matched;
    }
    return titleFallback;
}

} // namespace

bool activateWindow(const DockAppEntry &entry, DesktopActions::Outcome *out)
{
    pid_t pid = 0;
    CGWindowID wid = 0;
    if (!parseWindowIdToken(entry.windowId, &pid, &wid)) return false;
    @autoreleasepool {
        NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (!app || app.isTerminated) return false;
        [app unhide];
        AXUIElementRef win = findAxWindow(pid, wid, entry.caption);
        if (win) {
            AXUIElementSetAttributeValue(win, kAXMinimizedAttribute, kCFBooleanFalse);
            AXUIElementSetAttributeValue(win, kAXMainAttribute, kCFBooleanTrue);
            AXUIElementPerformAction(win, kAXRaiseAction);
            CFRelease(win);
            if (@available(macOS 14.0, *)) {
                [app activateWithOptions:0];
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                [app activateWithOptions:NSApplicationActivateIgnoringOtherApps];
#pragma clang diagnostic pop
            }
            if (out) *out = {true, QStringLiteral("✓ 已切换到 ") + (!entry.caption.isEmpty() ? entry.caption : entry.name)};
            return true;
        }
    }
    return false;
}

bool minimizeWindow(const DockAppEntry &entry, DesktopActions::Outcome *out)
{
    pid_t pid = 0;
    CGWindowID wid = 0;
    if (!parseWindowIdToken(entry.windowId, &pid, &wid)) return false;
    AXUIElementRef win = findAxWindow(pid, wid, entry.caption);
    if (!win) return false;
    const AXError err = AXUIElementSetAttributeValue(win, kAXMinimizedAttribute, kCFBooleanTrue);
    CFRelease(win);
    if (err == kAXErrorSuccess) {
        if (out) *out = {true, QStringLiteral("✓ 已最小化 ") + entry.name};
        return true;
    }
    return false;
}

bool closeWindow(const DockAppEntry &entry, DesktopActions::Outcome *out)
{
    pid_t pid = 0;
    CGWindowID wid = 0;
    if (!parseWindowIdToken(entry.windowId, &pid, &wid)) return false;
    if (!AXIsProcessTrusted()) {
        // Never fall through to quitApp ([app terminate]) when closing a single window without Accessibility.
        NSDictionary *opts = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES};
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
        if (out) *out = {false, QStringLiteral("请在系统设置 → 辅助功能中允许 FloatingBall 关闭窗口")};
        return true;
    }
    AXUIElementRef win = findAxWindow(pid, wid, entry.caption);
    if (win) {
        CFTypeRef closeBtnRef = nullptr;
        bool closed = false;
        if (AXUIElementCopyAttributeValue(win, kAXCloseButtonAttribute, &closeBtnRef) == kAXErrorSuccess && closeBtnRef) {
            closed = (AXUIElementPerformAction((AXUIElementRef)closeBtnRef, kAXPressAction) == kAXErrorSuccess);
            CFRelease(closeBtnRef);
        }
        CFRelease(win);
        if (closed) {
            if (out) *out = {true, QStringLiteral("✓ 已关闭 ") + (!entry.caption.isEmpty() ? entry.caption : entry.name)};
            return true;
        }
    }
    if (entry.active) {
        if (out) *out = DesktopActions::closeActiveWindow();
        return true;
    }
    if (out) *out = {false, QStringLiteral("无法关闭窗口：") + (!entry.caption.isEmpty() ? entry.caption : entry.name)};
    return true;
}

} // namespace AppDockMac
