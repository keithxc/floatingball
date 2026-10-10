#include "floatingwindow.h"
#include <QWidget>

#ifdef FLOATINGBALL_LAYER_SHELL
#include <LayerShellQt/Window>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QScopedValueRollback>
#include <QWindow>

namespace {
class WaylandFloatingSurface : public QObject {
public:
    WaylandFloatingSurface(QWidget *widget, bool activateOnShow) : QObject(widget), widget(widget) {
        widget->winId();
        surface=LayerShellQt::Window::get(widget->windowHandle());
        surface->setScope(QStringLiteral("floatingball"));
        surface->setLayer(LayerShellQt::Window::LayerOverlay);
        surface->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) | LayerShellQt::Window::AnchorLeft);
        surface->setExclusiveZone(-1);
        surface->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
        surface->setActivateOnShow(activateOnShow);
        widget->installEventFilter(this);
        sync();
    }
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        // Ordinary Wayland dialogs use xdg-shell and cannot rise above an overlay.
        // Lower blocked surfaces until their modal dialog has finished.
        if(event->type()==QEvent::WindowBlocked)
            surface->setLayer(LayerShellQt::Window::LayerBottom);
        else if(event->type()==QEvent::WindowUnblocked)
            surface->setLayer(LayerShellQt::Window::LayerOverlay);
        if(event->type()==QEvent::Move || event->type()==QEvent::Resize || event->type()==QEvent::Show)sync();
        return false;
    }
private:
    void sync() {
        if(syncing)return;
        QScopedValueRollback<bool> guard(syncing,true);
        QScreen *screen=QGuiApplication::screenAt(widget->geometry().center());
        if(!screen)screen=widget->screen();
        if(!screen)return;
        surface->setScreen(screen);
        if(widget->windowHandle()->screen()!=screen)widget->windowHandle()->setScreen(screen);
        const QPoint offset=widget->pos()-screen->geometry().topLeft();
        surface->setDesiredSize(widget->size());
        surface->setMargins(QMargins(offset.x(),offset.y(),0,0));
        // A move changes protocol margins; repaint commits them to the compositor.
        widget->update();
        widget->windowHandle()->requestUpdate();
    }
    QWidget *widget;
    LayerShellQt::Window *surface;
    bool syncing=false;
};
}
#endif

void FloatingWindow::prepare(QWidget *widget, bool activateOnShow) {
#ifdef FLOATINGBALL_LAYER_SHELL
    if(QGuiApplication::platformName().startsWith(QStringLiteral("wayland")))
        new WaylandFloatingSurface(widget,activateOnShow);
#else
    Q_UNUSED(widget);
    Q_UNUSED(activateOnShow);
#endif
}
