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
        const bool changed = m_widget->property("panelBackdropBlurActive").toBool() != active;
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
        if (!m_effect) {
            m_effect = [[FloatingPanelVisualEffectView alloc] initWithFrame:qtView.frame];
            m_effect.material = NSVisualEffectMaterialUnderWindowBackground;
            m_effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
            m_effect.state = NSVisualEffectStateActive;
            m_effect.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            m_effect.emphasized = NO;
            m_effect.alphaValue = 1.0;
            m_effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            m_effect.wantsLayer = YES;
            m_effect.layer.cornerRadius = 18.0;
            m_effect.layer.masksToBounds = YES;
        }
        if (m_effect.superview != parent) {
            [m_effect removeFromSuperview];
            [parent addSubview:m_effect positioned:NSWindowBelow relativeTo:qtView];
        }
        m_effect.frame = qtView.frame;
        m_effect.hidden = !m_widget->isVisible();
        publish(!m_effect.hidden && m_effect.superview == parent,
                m_effect.hidden ? QStringLiteral("hidden") : QStringLiteral("active"));
    }
    QPointer<QWidget> m_widget;
    FloatingPanelVisualEffectView *m_effect = nil;
    id m_accessibilityObserver = nil;
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
           NSClassFromString(@"NSVisualEffectView") != nil;
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
