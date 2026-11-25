#include "floatingwindow.h"
#include "panelbackdrop.h"
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRadialGradient>
#include <QRectF>
#include <QSettings>
#include <QWidget>

namespace {
void refreshPanelStyle(QWidget *widget)
{
    const auto source = widget->property("panelStyleTemplate").toString();
    if (source.isEmpty()) return;
    const bool reduced = widget->window()->property("panelBackdropReduceTransparency").toBool();
    const int alpha = reduced ? 245 : qRound(FloatingWindow::panelOpacity() * 255);
    QString style = source;
    style.replace(QStringLiteral("@panelTint@"), QStringLiteral("rgba(14,16,22,%1)").arg(alpha));
    widget->setStyleSheet(style);
    widget->ensurePolished();
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->update();
}

class PanelAppearanceFilter final : public QObject {
public:
    explicit PanelAppearanceFilter(QWidget *widget) : QObject(widget) { widget->installEventFilter(this); }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange &&
            static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "panelBackdropReduceTransparency") {
            auto widget = static_cast<QWidget *>(watched);
            refreshPanelStyle(widget);
            for (auto child : widget->findChildren<QWidget *>()) refreshPanelStyle(child);
        }
        return false;
    }
};
} // namespace

#ifdef FLOATINGBALL_LAYER_SHELL
#include <LayerShellQt/Window>
#include <QEvent>
#include <QPointer>
#include <QScreen>
#include <QScopedValueRollback>
#include <QTimer>
#include <QWindow>
#include <climits>

namespace {
class WaylandFloatingSurface : public QObject {
public:
    WaylandFloatingSurface(QWidget *widget, bool activateOnShow)
        : QObject(widget), widget(widget), activateOnShow(activateOnShow)
    {
        widget->winId();
        surface = LayerShellQt::Window::get(widget->windowHandle());
        surface->setScope(QStringLiteral("floatingball"));
        surface->setLayer(LayerShellQt::Window::LayerOverlay);
        surface->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
                            LayerShellQt::Window::AnchorLeft);
        surface->setExclusiveZone(-1);
        surface->setKeyboardInteractivity(
            activateOnShow ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                           : LayerShellQt::Window::KeyboardInteractivityNone);
        surface->setActivateOnShow(activateOnShow);
        widget->installEventFilter(this);
        sync();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        const bool isBall = (QByteArray(widget->metaObject()->className()) == "FloatingBall");
        if (!isBall && event->type() == QEvent::WindowBlocked) {
            surface->setLayer(LayerShellQt::Window::LayerBottom);
            if (widget->isVisible() && widget->windowHandle()) {
                widget->update();
                widget->windowHandle()->requestUpdate();
            }
        } else if (!isBall && event->type() == QEvent::WindowUnblocked) {
            surface->setLayer(LayerShellQt::Window::LayerOverlay);
            if (widget->isVisible() && widget->windowHandle()) {
                widget->update();
                widget->windowHandle()->requestUpdate();
            }
        }
        if (event->type() == QEvent::Close) {
            surfaceClosed = true;
        }
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize)
            sync(false);
        else if (event->type() == QEvent::Show)
            sync(true);
        return false;
    }
private:
    void sync(bool force = false)
    {
        if (syncing) return;
        QScopedValueRollback<bool> guard(syncing, true);
        QScreen *screen = nullptr;
        const QVariant pinnedVar = widget->property("floatingballAssignedScreen");
        if (pinnedVar.isValid()) {
            auto *pinned = qvariant_cast<QScreen *>(pinnedVar);
            if (pinned && QGuiApplication::screens().contains(pinned))
                screen = pinned;
        }
        if (!screen) screen = QGuiApplication::screenAt(widget->geometry().center());
        if (!screen) screen = widget->screen();
        if (!screen) return;
        const QPoint offset = widget->pos() - screen->geometry().topLeft();
        const QSize currentSize = widget->size();
        const QString screenName = screen->name();
        const bool outputChanged = surfaceClosed ||
            (!lastScreenName.isEmpty() && (lastScreen == nullptr || lastScreen != screen || lastScreenName != screenName));
        if (!force && !outputChanged && screen == lastScreen && offset == lastOffset && currentSize == lastSize)
            return;
        lastScreen = screen;
        lastScreenName = screenName;
        lastOffset = offset;
        lastSize = currentSize;
        surface->setScreen(screen);
        if (widget->windowHandle()->screen() != screen)
            widget->windowHandle()->setScreen(screen);
        surface->setDesiredSize(currentSize);
        surface->setMargins(QMargins(offset.x(), offset.y(), 0, 0));
        if (outputChanged && widget->isVisible() && !remapping) {
            // zwlr_layer_surface_v1 binds wl_output immutably at map time and dies permanently
            // on output removal (clamshell lid-close); hide+show recreates a live layer surface!
            surfaceClosed = false;
            remapping = true;
            QPointer<QWidget> wPtr(widget);
            QPointer<QScreen> sPtr(screen);
            QTimer::singleShot(0, widget, [this, wPtr, sPtr] {
                remapping = false;
                if (!wPtr || !sPtr || !QGuiApplication::screens().contains(sPtr)) return;
                if (!wPtr->isVisible()) return;
                wPtr->hide();
                surface->setScreen(sPtr);
                if (wPtr->windowHandle()) wPtr->windowHandle()->setScreen(sPtr);
                wPtr->show();
            });
            return;
        }
        surfaceClosed = false;
        if (widget->isVisible() && widget->windowHandle()) {
            widget->update();
            widget->windowHandle()->requestUpdate();
        }
    }
    QWidget *widget;
    LayerShellQt::Window *surface;
    QPointer<QScreen> lastScreen;
    QString lastScreenName;
    QPoint lastOffset{INT_MIN, INT_MIN};
    QSize lastSize;
    bool activateOnShow = false;
    bool syncing = false;
    bool remapping = false;
    bool surfaceClosed = false;
};
} // namespace
#endif

void FloatingWindow::prepare(QWidget *widget, bool activateOnShow)
{
    if (!widget) return;
    PanelBackdrop::configureFloatingSurface(widget, activateOnShow);

    // Painted circular/overlay surfaces handle their own per-pixel alpha.
    const QByteArray cls(widget->metaObject()->className());
    const bool painted = cls == "FloatingBall" ||
                         cls == "RadialFanOverlay" ||
                         widget->objectName() == QLatin1String("floatingballHoverLabel") ||
                         widget->objectName() == QLatin1String("floatingballToast");
    if (!painted) {
        widget->setProperty("panelOpacityManaged", true);
        if (!widget->property("panelAppearanceFilterInstalled").toBool()) {
            new PanelAppearanceFilter(widget);
            widget->setProperty("panelAppearanceFilterInstalled", true);
        }
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
            widget->setWindowOpacity(1.0);
        PanelBackdrop::setEnabled(widget, panelBlurEnabled());
    }
#ifdef FLOATINGBALL_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
        new WaylandFloatingSurface(widget, activateOnShow);
#endif
}

double FloatingWindow::panelOpacity()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    return qBound(0.10, s.value("appearance/panelTintOpacity", 0.58).toDouble(), 0.88);
}

void FloatingWindow::setPanelOpacity(double opacity)
{
    opacity = qBound(0.10, opacity, 0.88);
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue("appearance/panelTintOpacity", opacity);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->property("panelOpacityManaged").toBool() &&
            !QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
            w->setWindowOpacity(1.0);
        refreshPanelStyle(w);
        for (QWidget *child : w->findChildren<QWidget *>()) refreshPanelStyle(child);
    }
}

void FloatingWindow::setPanelStyleSheet(QWidget *widget, const QString &styleTemplate)
{
    if (!widget) return;
    widget->setProperty("panelStyleTemplate", styleTemplate);
    refreshPanelStyle(widget);
}

bool FloatingWindow::panelBlurEnabled()
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    return s.value("appearance/panelBlurEnabled", true).toBool();
}

bool FloatingWindow::panelBlurSupported() { return PanelBackdrop::supported(); }

void FloatingWindow::setPanelBlurEnabled(bool enabled)
{
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue("appearance/panelBlurEnabled", enabled);
    for (QWidget *w : QApplication::topLevelWidgets()) {
        if (w->property("panelOpacityManaged").toBool())
            PanelBackdrop::setEnabled(w, enabled);
    }
}

void FloatingWindow::paintLiquidGlass(QPainter &p, const QRect &widgetRect, double inset, double radius)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QRectF base = QRectF(widgetRect).adjusted(inset, inset, -inset, -inset);
    bool nativeMacGlass = false;
    if (const auto *w = dynamic_cast<const QWidget *>(p.device())) {
        nativeMacGlass = (w->property("panelBackdropBlurStatus").toString() == QLatin1String("macos-liquid-glass"));
    }

    if (nativeMacGlass) {
        // On macOS 26/27+, native NSGlassEffectView renders real optical refraction,
        // adaptive lensing, specular edge highlights, and concentric shadow. Keep a
        // whisper-light dark contrast veil + hairline catch-light so foreground controls
        // stay legible over bright windows without muddying Apple's native Liquid Glass.
        QLinearGradient veil(base.topLeft(), base.bottomRight());
        veil.setColorAt(0.0, QColor(34, 46, 72, 42));
        veil.setColorAt(0.45, QColor(18, 24, 40, 48));
        veil.setColorAt(1.0, QColor(12, 16, 28, 60));
        p.setBrush(veil);
        p.drawRoundedRect(base, radius, radius);

        QRadialGradient topSheen(base.topLeft() + QPointF(base.width() * 0.24, 0.0), base.width() * 0.85);
        topSheen.setColorAt(0.0, QColor(255, 255, 255, 22));
        topSheen.setColorAt(0.5, QColor(200, 226, 255, 6));
        topSheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(topSheen);
        p.drawRoundedRect(base, radius, radius);

        QLinearGradient rimGrad(base.topLeft(), base.bottomRight());
        rimGrad.setColorAt(0.0, QColor(255, 255, 255, 36));
        rimGrad.setColorAt(0.45, QColor(255, 255, 255, 6));
        rimGrad.setColorAt(1.0, QColor(165, 216, 255, 18));
        p.setPen(QPen(QBrush(rimGrad), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(base.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
        p.restore();
        return;
    }

    // 1. Borderless feathered outer ambient gradient (no hard outer stroke)
    struct FeatherStep { qreal expand; int alpha; };
    static const FeatherStep kFeather[] = {
        {10.0, 5}, {7.5, 10}, {5.0, 16}, {2.5, 24}
    };
    for (const FeatherStep &s : kFeather) {
        if (s.expand > inset + 0.5) continue;
        p.setBrush(QColor(10, 16, 28, s.alpha));
        p.drawRoundedRect(base.adjusted(-s.expand, -s.expand, s.expand, s.expand),
                          radius + s.expand * 0.45, radius + s.expand * 0.45);
    }

    // 2. High-clarity translucent Liquid Glass body over compositor Gaussian blur
    QLinearGradient glass(base.topLeft(), base.bottomRight());
    glass.setColorAt(0.0, QColor(52, 66, 98, 92));
    glass.setColorAt(0.38, QColor(24, 32, 52, 96));
    glass.setColorAt(1.0, QColor(14, 19, 34, 114));
    p.setBrush(glass);
    p.drawRoundedRect(base, radius, radius);

    // 3. Dual-angle optical specular refraction bloom (top-left warm/white catch-light
    //    + bottom-right cool glass counter-refraction)
    QRadialGradient topSheen(base.topLeft() + QPointF(base.width() * 0.24, 0.0), base.width() * 0.88);
    topSheen.setColorAt(0.0, QColor(255, 255, 255, 36));
    topSheen.setColorAt(0.45, QColor(196, 224, 255, 10));
    topSheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(topSheen);
    p.drawRoundedRect(base, radius, radius);

    QRadialGradient botRefract(base.bottomRight() - QPointF(base.width() * 0.2, 0.0), base.width() * 0.72);
    botRefract.setColorAt(0.0, QColor(125, 211, 252, 18));
    botRefract.setColorAt(1.0, QColor(125, 211, 252, 0));
    p.setBrush(botRefract);
    p.drawRoundedRect(base, radius, radius);

    // 4. Delicate 1px optical specular inner rim (fades through midtones so it reads
    //    as light refraction on liquid glass rather than a solid border)
    QLinearGradient rimGrad(base.topLeft(), base.bottomRight());
    rimGrad.setColorAt(0.0, QColor(255, 255, 255, 52));
    rimGrad.setColorAt(0.35, QColor(255, 255, 255, 10));
    rimGrad.setColorAt(0.7, QColor(255, 255, 255, 6));
    rimGrad.setColorAt(1.0, QColor(165, 216, 255, 28));
    p.setPen(QPen(QBrush(rimGrad), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(base.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);

    p.restore();
}
