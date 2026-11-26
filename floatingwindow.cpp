#include "floatingwindow.h"
#include "panelbackdrop.h"
#include <QAction>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEventLoop>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointer>
#include <QRadialGradient>
#include <QRectF>
#include <QScreen>
#include <QSettings>
#include <QTimer>
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
        if (!FloatingWindow::isValidScreen(screen)) {
            // Wayland emits a transient QPlaceholderScreen (empty name/geometry) when
            // all wl_outputs vanish during DPMS off / lid-close / eGPU hotplug. Never
            // bind zwlr_layer_surface_v1 to the placeholder; mark closed so it remaps
            // cleanly as soon as a real output returns.
            surfaceClosed = true;
            return;
        }
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
                if (!wPtr || !sPtr || !QGuiApplication::screens().contains(sPtr) ||
                    !FloatingWindow::isValidScreen(sPtr)) return;
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

// Sleek click-through LayerShellQt tooltip pill used on Wayland in place of
// Qt's QTipLabel (which attempts xdg_popup attachment onto zwlr_layer_surface_v1
// and fails with "Cannot attach popup of unknown type").
class WaylandTooltipPill final : public QWidget {
public:
    WaylandTooltipPill()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                           Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput)
    {
        setObjectName(QStringLiteral("floatingballHoverLabel"));
        setProperty("floatingballSurface", true);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        connect(m_hideTimer, &QTimer::timeout, this, &QWidget::hide);
        FloatingWindow::prepare(this, false);
    }

    void showText(const QString &text, const QPoint &globalPos, const QWidget *anchorWidget)
    {
        const QString clean = text.trimmed();
        if (clean.isEmpty()) {
            hide();
            return;
        }
        m_text = clean;
        QFont f = font();
        f.setPointSize(11);
        f.setWeight(QFont::Medium);
        setFont(f);
        const QFontMetrics fm(f);
        const int maxW = 420;
        const QRect textRect = fm.boundingRect(QRect(0, 0, maxW - 24, 240), Qt::TextWordWrap, m_text);
        const int w = qBound(72, textRect.width() + 26, maxW);
        const int h = qMax(30, textRect.height() + 14);
        resize(w, h);

        QScreen *scr = QGuiApplication::screenAt(globalPos);
        if (!FloatingWindow::isValidScreen(scr) && anchorWidget)
            scr = anchorWidget->screen();
        if (!FloatingWindow::isValidScreen(scr)) {
            for (QScreen *cand : QGuiApplication::screens()) {
                if (FloatingWindow::isValidScreen(cand)) { scr = cand; break; }
            }
        }
        if (!FloatingWindow::isValidScreen(scr)) return;
        setProperty("floatingballAssignedScreen", QVariant::fromValue(scr));

        const QRect b = scr->availableGeometry();
        int x = globalPos.x() - w / 2;
        int y = globalPos.y() - h - 18;
        if (y < b.top() + 8) y = globalPos.y() + 22;
        x = qBound(b.left() + 8, x, qMax(b.left() + 8, b.right() - w - 8));
        y = qBound(b.top() + 8, y, qMax(b.top() + 8, b.bottom() - h - 8));
        move(x, y);
        if (!isVisible()) show();
        update();
        m_hideTimer->start(5200);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        p.setPen(QPen(QColor(255, 255, 255, 56), 1.0));
        p.setBrush(QColor(18, 22, 32, 236));
        p.drawRoundedRect(r, 12.0, 12.0);
        p.setPen(QColor("#f4f7fc"));
        p.setFont(font());
        p.drawText(rect().adjusted(12, 6, -12, -6), Qt::AlignCenter | Qt::TextWordWrap, m_text);
    }

private:
    QString m_text;
    QTimer *m_hideTimer = nullptr;
};

class WaylandTooltipInterceptor final : public QObject {
public:
    explicit WaylandTooltipInterceptor(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *w = qobject_cast<QWidget *>(watched);
        if (!w) return false;
        QWidget *top = w->window();
        if (!top || !top->property("floatingballLayerShell").toBool()) return false;
        if (top->objectName() == QLatin1String("floatingballHoverLabel")) return false;

        switch (event->type()) {
        case QEvent::ToolTip: {
            auto *help = static_cast<QHelpEvent *>(event);
            QString tip;
            for (const QWidget *cur = w; cur; cur = cur->parentWidget()) {
                tip = cur->toolTip();
                if (!tip.isEmpty()) break;
            }
            if (tip.isEmpty()) {
                if (m_pill) m_pill->hide();
            } else {
                if (!m_pill) m_pill = new WaylandTooltipPill();
                m_ownerWidget = w;
                m_ownerTop = top;
                m_pill->showText(tip, help->globalPos(), w);
            }
            return true; // Always consume so Qt never creates an xdg_popup QTipLabel!
        }
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::Hide:
        case QEvent::Close:
        case QEvent::WindowDeactivate:
            if (m_pill && m_pill->isVisible() && (w == m_ownerWidget || top == m_ownerTop)) {
                m_pill->hide();
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    QPointer<WaylandTooltipPill> m_pill;
    QPointer<QWidget> m_ownerWidget;
    QPointer<QWidget> m_ownerTop;
};

void ensureWaylandTooltipInterceptor()
{
    static bool installed = false;
    if (installed || !qApp) return;
    installed = true;
    qApp->installEventFilter(new WaylandTooltipInterceptor(qApp));
}

class WaylandMenuFilter final : public QObject {
public:
    explicit WaylandMenuFilter(QMenu *menu) : QObject(menu), m_menu(menu)
    {
        m_opened = false;
        QPointer<WaylandMenuFilter> self(this);
        QTimer::singleShot(140, menu, [self] {
            if (self) self->m_opened = true;
        });
        menu->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (!m_menu) return false;
        if (event->type() == QEvent::WindowDeactivate && m_opened) {
            // Keep open while a child submenu has focus
            for (QMenu *sub : m_menu->findChildren<QMenu *>()) {
                if (sub && sub->isVisible()) return false;
            }
            m_menu->close();
        } else if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Escape) {
                m_menu->close();
                return true;
            }
        }
        return false;
    }

private:
    QPointer<QMenu> m_menu;
    bool m_opened = false;
};

void prepareWaylandMenu(QMenu *menu, const QPoint &globalPos)
{
    if (!menu) return;
    if (menu->styleSheet().isEmpty()) {
        menu->setStyleSheet(QStringLiteral(
            "QMenu { background:rgba(20,24,34,242); color:#f1f5f9;"
            "        border:1px solid rgba(255,255,255,44); border-radius:14px; padding:6px 4px;"
            "        font-size:13px; }"
            "QMenu::item { padding:8px 16px; border-radius:9px; margin:1px 4px; }"
            "QMenu::item:selected { background:rgba(59,130,246,165); color:#ffffff; }"
            "QMenu::item:disabled { color:rgba(148,163,184,135); }"
            "QMenu::separator { height:1px; background:rgba(255,255,255,26); margin:5px 12px; }"
            "QMenu::indicator { width:14px; height:14px; margin-left:6px; }"));
    }
    menu->setParent(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setProperty("floatingballSurface", true);
    menu->setProperty("panelBlurInset", 2);
    menu->setProperty("panelBlurRadius", 14);
    FloatingWindow::prepare(menu, true);

    // Flatten nested submenus gently or prepare them as LayerShellQt overlays too
    for (QMenu *sub : menu->findChildren<QMenu *>()) {
        if (sub && !sub->property("floatingballLayerShell").toBool()) {
            sub->setStyleSheet(menu->styleSheet());
            sub->setAttribute(Qt::WA_TranslucentBackground, true);
            sub->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            FloatingWindow::prepare(sub, true);
        }
    }

    QScreen *scr = QGuiApplication::screenAt(globalPos);
    if (!FloatingWindow::isValidScreen(scr)) {
        for (QScreen *cand : QGuiApplication::screens()) {
            if (FloatingWindow::isValidScreen(cand)) { scr = cand; break; }
        }
    }
    if (FloatingWindow::isValidScreen(scr)) {
        menu->setProperty("floatingballAssignedScreen", QVariant::fromValue(scr));
    }

    menu->adjustSize();
    const QSize sz = menu->sizeHint().expandedTo(menu->size());
    menu->resize(sz);
    if (FloatingWindow::isValidScreen(scr)) {
        const QRect b = scr->availableGeometry();
        const int x = qBound(b.left() + 6, globalPos.x(), qMax(b.left() + 6, b.right() - sz.width() - 6));
        const int y = qBound(b.top() + 6, globalPos.y(), qMax(b.top() + 6, b.bottom() - sz.height() - 6));
        menu->move(x, y);
    } else {
        menu->move(globalPos);
    }

    if (!menu->property("floatingballWaylandMenuFilter").toBool()) {
        new WaylandMenuFilter(menu);
        menu->setProperty("floatingballWaylandMenuFilter", true);
    }
}
} // namespace
#endif

bool FloatingWindow::isValidScreen(const QScreen *screen)
{
    if (!screen || screen->geometry().isEmpty() || screen->availableGeometry().isEmpty())
        return false;
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) &&
        screen->name().trimmed().isEmpty())
        return false;
    return true;
}

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
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        if (!widget->property("floatingballLayerShell").toBool()) {
            widget->setProperty("floatingballLayerShell", true);
            new WaylandFloatingSurface(widget, activateOnShow);
        }
        ensureWaylandTooltipInterceptor();
    }
#endif
}

QAction *FloatingWindow::execPopupMenu(QMenu *menu, const QPoint &globalPos)
{
    if (!menu) return nullptr;
#ifdef FLOATINGBALL_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        prepareWaylandMenu(menu, globalPos);
        QPointer<QAction> chosen;
        QEventLoop loop;
        auto c1 = QObject::connect(menu, &QMenu::triggered, &loop, [&chosen, &loop, menu](QAction *act) {
            chosen = act;
            menu->close();
            loop.quit();
        });
        auto c2 = QObject::connect(menu, &QMenu::aboutToHide, &loop, &QEventLoop::quit);
        auto c3 = QObject::connect(menu, &QObject::destroyed, &loop, &QEventLoop::quit);
        menu->show();
        menu->raise();
        menu->activateWindow();
        loop.exec();
        QObject::disconnect(c1);
        QObject::disconnect(c2);
        QObject::disconnect(c3);
        return chosen.data();
    }
#endif
    return menu->exec(globalPos);
}

void FloatingWindow::popupMenu(QMenu *menu, const QPoint &globalPos)
{
    if (!menu) return;
#ifdef FLOATINGBALL_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        prepareWaylandMenu(menu, globalPos);
        QObject::connect(menu, &QMenu::triggered, menu, [menu](QAction *) { menu->close(); });
        menu->show();
        menu->raise();
        menu->activateWindow();
        return;
    }
#endif
    menu->popup(globalPos);
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
        // adaptive lensing, specular edge highlights, and concentric shadow. Layer a
        // neutral Obsidian Frosted Glass veil + linear top catch-light so controls stay
        // crisp over bright windows while matching the unified Obsidian glass palette.
        QLinearGradient veil(base.topLeft(), base.bottomRight());
        veil.setColorAt(0.0, QColor(32, 35, 42, 46));
        veil.setColorAt(0.45, QColor(18, 20, 26, 58));
        veil.setColorAt(1.0, QColor(12, 14, 19, 72));
        p.setBrush(veil);
        p.drawRoundedRect(base, radius, radius);

        QPainterPath clip;
        clip.addRoundedRect(base, radius, radius);
        p.save();
        p.setClipPath(clip);
        const qreal sheenH = qMin(40.0, base.height() * 0.22);
        QLinearGradient topCatch(base.topLeft(), QPointF(base.left(), base.top() + sheenH));
        topCatch.setColorAt(0.0, QColor(255, 255, 255, 18));
        topCatch.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(topCatch);
        p.drawRect(QRectF(base.left(), base.top(), base.width(), sheenH));
        p.restore();

        QLinearGradient rimGrad(base.topLeft(), base.bottomRight());
        rimGrad.setColorAt(0.0, QColor(255, 255, 255, 54));
        rimGrad.setColorAt(0.42, QColor(255, 255, 255, 14));
        rimGrad.setColorAt(1.0, QColor(255, 255, 255, 28));
        p.setPen(QPen(QBrush(rimGrad), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(base.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);
        p.restore();
        return;
    }

    // 1. Tight, clean dark ambient drop-shadow (neutral obsidian, no hazy blue-gray halo)
    struct FeatherStep { qreal expand; int alpha; };
    static const FeatherStep kFeather[] = {
        {5.0, 14}, {2.5, 28}
    };
    for (const FeatherStep &s : kFeather) {
        if (s.expand > inset + 0.5) continue;
        p.setBrush(QColor(0, 0, 0, s.alpha));
        p.drawRoundedRect(base.adjusted(-s.expand, -s.expand + 1.0, s.expand, s.expand + 1.5),
                          radius + s.expand * 0.4, radius + s.expand * 0.4);
    }

    // 2. Neutral Obsidian Frosted Glass body over compositor Gaussian blur
    //    (matches FloatingBall's carbon-obsidian sphere & dark KDE desktop with zero blue wash)
    QLinearGradient glass(base.topLeft(), base.bottomRight());
    glass.setColorAt(0.0, QColor(36, 39, 46, 186));
    glass.setColorAt(0.45, QColor(23, 25, 31, 198));
    glass.setColorAt(1.0, QColor(16, 18, 23, 210));
    p.setBrush(glass);
    p.drawRoundedRect(base, radius, radius);

    // 3. Banding-free top-edge linear specular catch-light (subtle beveled glass sheen)
    {
        QPainterPath clip;
        clip.addRoundedRect(base, radius, radius);
        p.save();
        p.setClipPath(clip);
        const qreal sheenH = qMin(42.0, base.height() * 0.24);
        QLinearGradient topCatch(base.topLeft(), QPointF(base.left(), base.top() + sheenH));
        topCatch.setColorAt(0.0, QColor(255, 255, 255, 16));
        topCatch.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(topCatch);
        p.drawRect(QRectF(base.left(), base.top(), base.width(), sheenH));
        p.restore();
    }

    // 4. Crisp 1px optical specular inner rim matching FloatingBall's obsidian sphere edge
    QLinearGradient rimGrad(base.topLeft(), base.bottomRight());
    rimGrad.setColorAt(0.0, QColor(255, 255, 255, 62));
    rimGrad.setColorAt(0.40, QColor(255, 255, 255, 16));
    rimGrad.setColorAt(1.0, QColor(255, 255, 255, 28));
    p.setPen(QPen(QBrush(rimGrad), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(base.adjusted(0.5, 0.5, -0.5, -0.5), radius - 0.5, radius - 0.5);

    p.restore();
}
