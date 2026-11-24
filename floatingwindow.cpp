#include "floatingwindow.h"
#include "panelbackdrop.h"
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QGuiApplication>
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
#include <QScreen>
#include <QScopedValueRollback>
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
        if (event->type() == QEvent::WindowBlocked)
            surface->setLayer(LayerShellQt::Window::LayerBottom);
        else if (event->type() == QEvent::WindowUnblocked)
            surface->setLayer(LayerShellQt::Window::LayerOverlay);
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
        if (!force && screen == lastScreen && offset == lastOffset && currentSize == lastSize)
            return;
        lastScreen = screen;
        lastOffset = offset;
        lastSize = currentSize;
        surface->setScreen(screen);
        if (widget->windowHandle()->screen() != screen)
            widget->windowHandle()->setScreen(screen);
        surface->setDesiredSize(currentSize);
        surface->setMargins(QMargins(offset.x(), offset.y(), 0, 0));
        if (widget->isVisible() && widget->windowHandle()) {
            widget->update();
            widget->windowHandle()->requestUpdate();
        }
    }
    QWidget *widget;
    LayerShellQt::Window *surface;
    QScreen *lastScreen = nullptr;
    QPoint lastOffset{INT_MIN, INT_MIN};
    QSize lastSize;
    bool activateOnShow = false;
    bool syncing = false;
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
