#include "panelbackdrop.h"
#include <QCursor>
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <objc/runtime.h>

// A decorative sibling cannot take clicks, keyboard focus or accessibility
// focus away from the Qt content view above it.
@interface FloatingPanelVisualEffectView : NSVisualEffectView
@end
@implementation FloatingPanelVisualEffectView
- (NSView *)hitTest:(NSPoint)point { Q_UNUSED(point); return nil; }
- (BOOL)acceptsFirstResponder { return NO; }
- (BOOL)isAccessibilityElement { return NO; }
- (NSArray *)accessibilityChildren { return @[]; }
@end

namespace {

// Runtime non-hit-testing subclass of macOS 26/27+ NSGlassEffectView so we get
// true hardware-accelerated Apple Liquid Glass (with macOS 27 interactive glass)
// while remaining binary- and source-compatible with macOS 14/15 SDKs.
static id glassHitTestNil(id, SEL, NSPoint) { return nil; }
static BOOL glassReturnNo(id, SEL) { return NO; }
static id glassReturnEmptyArray(id, SEL) { return @[]; }

Class floatingPanelGlassEffectClass()
{
    static Class cached = Nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        Class base = NSClassFromString(@"NSGlassEffectView");
        if (!base) return;
        Class existing = objc_lookUpClass("FloatingPanelGlassEffectView");
        if (existing) {
            cached = existing;
            return;
        }
        Class cls = objc_allocateClassPair(base, "FloatingPanelGlassEffectView", 0);
        if (!cls) return;
        class_addMethod(cls, @selector(hitTest:), (IMP)glassHitTestNil, "@@:{CGPoint=dd}");
        class_addMethod(cls, @selector(acceptsFirstResponder), (IMP)glassReturnNo, "B@:");
        class_addMethod(cls, @selector(isAccessibilityElement), (IMP)glassReturnNo, "B@:");
        class_addMethod(cls, @selector(accessibilityChildren), (IMP)glassReturnEmptyArray, "@@:");
        objc_registerClassPair(cls);
        cached = cls;
    });
    return cached;
}

void configureGlassEffectView(NSView *view, CGFloat cornerRadius, bool interactive)
{
    if (!view) return;
    SEL selRadius = NSSelectorFromString(@"setCornerRadius:");
    if ([view respondsToSelector:selRadius]) {
        ((void (*)(id, SEL, CGFloat))objc_msgSend)(view, selRadius, cornerRadius);
    }
    // NSGlassEffectViewStyleRegular = 0 (balanced dark/light adaptive optical refraction)
    SEL selStyle = NSSelectorFromString(@"setStyle:");
    if ([view respondsToSelector:selStyle]) {
        ((void (*)(id, SEL, NSInteger))objc_msgSend)(view, selStyle, 0);
    }
    SEL selTint = NSSelectorFromString(@"setTintColor:");
    if ([view respondsToSelector:selTint]) {
        NSColor *tint = [NSColor colorWithSRGBRed:0.09 green:0.12 blue:0.20 alpha:0.28];
        ((void (*)(id, SEL, NSColor *))objc_msgSend)(view, selTint, tint);
    }
    // macOS 27.0+ native interactive Liquid Glass optical feedback
    SEL selInteractive = NSSelectorFromString(@"setEffectIsInteractive:");
    if ([view respondsToSelector:selInteractive]) {
        ((void (*)(id, SEL, BOOL))objc_msgSend)(view, selInteractive, interactive ? YES : NO);
    }
    view.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
}

void applyNativeFloatingPolicy(QWidget *widget, bool activateOnShow)
{
    Q_UNUSED(activateOnShow);
    if (!widget || QGuiApplication::platformName() != QStringLiteral("cocoa") || !widget->isWindow()) return;
    NSView *qtView = reinterpret_cast<NSView *>(widget->winId());
    if (!qtView || !qtView.window) return;
    NSWindow *window = qtView.window;
    window.hidesOnDeactivate = NO;
    // Translucent custom-painted overlays (FloatingBall, RadialFanOverlay, Toast)
    // paint their own anti-aliased soft shadows; macOS window shadows on sparse
    // alpha surfaces create jagged dark rings around arcs and tethers.
    if (widget->testAttribute(Qt::WA_TranslucentBackground) &&
        !widget->property("panelOpacityManaged").toBool()) {
        window.hasShadow = NO;
    } else if (widget->property("panelOpacityManaged").toBool() &&
               widget->property("panelBlurInset").toDouble() > 0.0 &&
               floatingPanelGlassEffectClass() != Nil) {
        // Native NSGlassEffectView renders its own concentric macOS 27 glass shadow/refraction.
        window.hasShadow = NO;
    }
    NSWindowCollectionBehavior behavior = window.collectionBehavior;
    behavior &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
    behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
    window.collectionBehavior = behavior;
}

class MacFloatingSurfaceFilter final : public QObject {
public:
    MacFloatingSurfaceFilter(QWidget *widget, bool activateOnShow)
        : QObject(widget), m_widget(widget), m_activateOnShow(activateOnShow)
    {
        widget->installEventFilter(this);
        schedule();
    }
    void setActivateOnShow(bool activate)
    {
        m_activateOnShow = activate;
        schedule();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            schedule();
        }
        return false;
    }
private:
    void schedule()
    {
        if (m_queued || !m_widget) return;
        m_queued = true;
        QTimer::singleShot(0, this, [this] {
            m_queued = false;
            if (m_widget && m_widget->testAttribute(Qt::WA_WState_Created)) {
                applyNativeFloatingPolicy(m_widget, m_activateOnShow);
            }
        });
    }
    QPointer<QWidget> m_widget;
    bool m_activateOnShow = false;
    bool m_queued = false;
};

class MacPanelBackdrop final : public QObject {
public:
    explicit MacPanelBackdrop(QWidget *widget) : QObject(widget), m_widget(widget)
    {
        widget->installEventFilter(this);
        const QPointer<MacPanelBackdrop> owner(this);
        m_accessibilityObserver = [[[NSWorkspace sharedWorkspace].notificationCenter
            addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                        object:nil
                         queue:[NSOperationQueue mainQueue]
                    usingBlock:^(NSNotification *) {
                        if (owner) owner->scheduleSync();
                    }] retain];
    }
    ~MacPanelBackdrop() override
    {
        if (m_widget) m_widget->removeEventFilter(this);
        if (m_accessibilityObserver) {
            [[NSWorkspace sharedWorkspace].notificationCenter removeObserver:m_accessibilityObserver];
            [m_accessibilityObserver release];
        }
        clear();
    }
    bool setEnabled(bool enabled)
    {
        m_enabled = enabled;
        if (m_widget) m_widget->setProperty("panelBackdropBlurEnabled", enabled);
        sync();
        scheduleSync();
        return m_widget && m_widget->property("panelBackdropBlurActive").toBool();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (m_syncing) return false;
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::ParentChange:
            scheduleSync();
            break;
        case QEvent::WinIdChange:
            clear();
            publish(false, QStringLiteral("awaiting-native-view"));
            scheduleSync();
            break;
        case QEvent::Hide:
            if (m_effect) m_effect.hidden = YES;
            publish(false, QStringLiteral("hidden"));
            break;
        case QEvent::Destroy:
            clear();
            m_widget = nullptr;
            break;
        default:
            break;
        }
        return false;
    }
private:
    void publish(bool active, const QString &status)
    {
        if (!m_widget) return;
        QScopedValueRollback<bool> guard(m_syncing, true);
        const bool changed = m_widget->property("panelBackdropBlurActive").toBool() != active ||
                             m_widget->property("panelBackdropBlurStatus").toString() != status;
        m_widget->setProperty("panelBackdropBlurActive", active);
        m_widget->setProperty("panelBackdropBlurStatus", status);
        if (!changed) return;
        const QPointer<QWidget> root = m_widget;
        QList<QPointer<QWidget>> children;
        for (auto child : root->findChildren<QWidget *>()) children.append(child);
        const auto sheet = root->styleSheet();
        if (sheet.isEmpty()) root->setStyleSheet(QStringLiteral("/* native backdrop state refresh */"));
        root->setStyleSheet(sheet);
        for (const auto &child : children) {
            if (child && !child->styleSheet().isEmpty()) child->setStyleSheet(child->styleSheet());
        }
        if (root) root->update();
    }
    void clear()
    {
        if (m_effect) {
            [m_effect removeFromSuperview];
            [m_effect release];
            m_effect = nil;
        }
        m_usingLiquidGlass = false;
    }
    void scheduleSync()
    {
        if (m_queued || !m_widget) return;
        m_queued = true;
        QTimer::singleShot(0, this, [this] { m_queued = false; sync(); });
    }
    void sync()
    {
        if (m_syncing || !m_widget) return;
        QScopedValueRollback<bool> guard(m_syncing, true);
        const bool reduced = [NSWorkspace sharedWorkspace].accessibilityDisplayShouldReduceTransparency;
        m_widget->setProperty("panelBackdropReduceTransparency", reduced);
        if (!m_enabled || reduced || !m_widget->isWindow()) {
            clear();
            publish(false, !m_enabled ? QStringLiteral("disabled")
                         : reduced ? QStringLiteral("reduced-transparency")
                                   : QStringLiteral("not-top-level"));
            return;
        }
        if (!PanelBackdrop::supported()) {
            clear();
            publish(false, QStringLiteral("unsupported-platform"));
            return;
        }
        NSView *qtView = reinterpret_cast<NSView *>(m_widget->winId());
        NSView *parent = qtView.superview;
        if (!qtView || !parent || !qtView.window) {
            clear();
            publish(false, QStringLiteral("awaiting-native-view"));
            return;
        }

        const QVariant insetVar = m_widget->property("panelBlurInset");
        const CGFloat inset = insetVar.isValid() ? static_cast<CGFloat>(qMax(0.0, insetVar.toDouble())) : 0.0;
        const QVariant radiusVar = m_widget->property("panelBlurRadius");
        const CGFloat radius = radiusVar.isValid() ? static_cast<CGFloat>(qMax(0.0, radiusVar.toDouble())) : 18.0;
        const NSRect targetFrame = (inset > 0.5 && qtView.frame.size.width > inset * 2.0 && qtView.frame.size.height > inset * 2.0)
            ? NSInsetRect(qtView.frame, inset, inset)
            : qtView.frame;

        if (!m_effect) {
            Class glassCls = floatingPanelGlassEffectClass();
            if (glassCls) {
                m_effect = [[glassCls alloc] initWithFrame:targetFrame];
                configureGlassEffectView(m_effect, radius, true);
                m_usingLiquidGlass = true;
            } else {
                FloatingPanelVisualEffectView *vfx = [[FloatingPanelVisualEffectView alloc] initWithFrame:targetFrame];
                vfx.material = NSVisualEffectMaterialUnderWindowBackground;
                vfx.blendingMode = NSVisualEffectBlendingModeBehindWindow;
                vfx.state = NSVisualEffectStateActive;
                vfx.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
                vfx.emphasized = NO;
                vfx.alphaValue = 1.0;
                vfx.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                vfx.wantsLayer = YES;
                vfx.layer.cornerRadius = radius;
                vfx.layer.masksToBounds = YES;
                m_effect = vfx;
                m_usingLiquidGlass = false;
            }
        } else if (m_usingLiquidGlass) {
            configureGlassEffectView(m_effect, radius, true);
        } else if (m_effect.layer) {
            m_effect.layer.cornerRadius = radius;
        }

        if (m_effect.superview != parent) {
            [m_effect removeFromSuperview];
            [parent addSubview:m_effect positioned:NSWindowBelow relativeTo:qtView];
        }
        m_effect.frame = targetFrame;
        m_effect.hidden = !m_widget->isVisible();
        const bool active = !m_effect.hidden && m_effect.superview == parent;
        publish(active,
                !active ? QStringLiteral("hidden")
                        : (m_usingLiquidGlass ? QStringLiteral("macos-liquid-glass")
                                              : QStringLiteral("active")));
    }
    QPointer<QWidget> m_widget;
    NSView *m_effect = nil;
    id m_accessibilityObserver = nil;
    bool m_usingLiquidGlass = false;
    bool m_enabled = false, m_syncing = false, m_queued = false;
};

MacPanelBackdrop *bridge(QWidget *widget)
{
    for (auto child : widget->children()) {
        if (auto existing = dynamic_cast<MacPanelBackdrop *>(child)) return existing;
    }
    return new MacPanelBackdrop(widget);
}

} // namespace

bool PanelBackdrop::supported()
{
    return QGuiApplication::platformName() == QStringLiteral("cocoa") &&
           (NSClassFromString(@"NSGlassEffectView") != nil ||
            NSClassFromString(@"NSVisualEffectView") != nil);
}

bool PanelBackdrop::setEnabled(QWidget *widget, bool enabled)
{
    if (!widget || QThread::currentThread() != widget->thread()) return false;
    if (!supported()) {
        widget->setProperty("panelBackdropBlurEnabled", enabled);
        widget->setProperty("panelBackdropBlurActive", false);
        widget->setProperty("panelBackdropBlurStatus", QStringLiteral("unsupported-platform"));
        return false;
    }
    return bridge(widget)->setEnabled(enabled);
}

void PanelBackdrop::configureFloatingSurface(QWidget *widget, bool activateOnShow)
{
    if (!widget || QThread::currentThread() != widget->thread()) return;
    if (!activateOnShow) {
        widget->setAttribute(Qt::WA_ShowWithoutActivating, true);
    }
    if (QGuiApplication::platformName() != QStringLiteral("cocoa")) return;
    for (auto child : widget->children()) {
        if (auto existing = dynamic_cast<MacFloatingSurfaceFilter *>(child)) {
            existing->setActivateOnShow(activateOnShow);
            return;
        }
    }
    new MacFloatingSurfaceFilter(widget, activateOnShow);
}

namespace {
struct MacPointerMonitorPack {
    id globalMonitor = nil;
    id localMonitor = nil;
};
} // namespace

void *PanelBackdrop::installGlobalPointerDownMonitor(QObject *owner, std::function<void(const QPoint &)> callback)
{
    if (!owner || !callback || QGuiApplication::platformName() != QStringLiteral("cocoa"))
        return nullptr;
    @autoreleasepool {
        auto *pack = new MacPointerMonitorPack();
        const QPointer<QObject> guard(owner);
        const NSEventMask mask = NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown;

        pack->globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:^(NSEvent *) {
            if (!guard) return;
            const QPoint pt = QCursor::pos();
            QTimer::singleShot(0, guard, [callback, pt] { callback(pt); });
        }];
        pack->localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *ev) {
            if (guard) {
                const QPoint pt = QCursor::pos();
                QTimer::singleShot(0, guard, [callback, pt] { callback(pt); });
            }
            return ev;
        }];
        return pack;
    }
}

void PanelBackdrop::removeGlobalPointerDownMonitor(void *raw)
{
    if (!raw) return;
    @autoreleasepool {
        auto *pack = static_cast<MacPointerMonitorPack *>(raw);
        if (pack->globalMonitor) [NSEvent removeMonitor:pack->globalMonitor];
        if (pack->localMonitor) [NSEvent removeMonitor:pack->localMonitor];
        delete pack;
    }
}
