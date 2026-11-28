#include "panelbackdrop.h"
#include <QVariant>
#include <QWidget>

#if !defined(__APPLE__)

#if defined(Q_OS_LINUX) && defined(FLOATINGBALL_WAYLAND_CLIENT) && defined(FLOATINGBALL_QT_GUI_PRIVATE)
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <cmath>
#include <cstring>
#include <wayland-client.h>
#include <qpa/qplatformnativeinterface.h>

namespace {

// ----------------------------------------------------------------------------
// 1. Standard Wayland ext_background_effect_v1 (KDE Plasma 6.6+ / KWin 6.8+)
// ----------------------------------------------------------------------------
static const wl_interface *kEmptyTypes[] = {nullptr};
static const wl_interface *kRegionTypes[] = {&wl_region_interface};

static const wl_message kExtBgSurfaceRequests[] = {
    {"destroy", "", kEmptyTypes},
    {"set_blur_region", "?o", kRegionTypes},
};
static const wl_interface kExtBgSurfaceInterface = {
    "ext_background_effect_surface_v1", 1, 2, kExtBgSurfaceRequests, 0, nullptr
};

static const wl_interface *kExtBgGetTypes[] = {&kExtBgSurfaceInterface, &wl_surface_interface};
static const wl_message kExtBgManagerRequests[] = {
    {"destroy", "", kEmptyTypes},
    {"get_background_effect", "no", kExtBgGetTypes},
};
static const wl_message kExtBgManagerEvents[] = {
    {"capabilities", "u", kEmptyTypes},
};
static const wl_interface kExtBgManagerInterface = {
    "ext_background_effect_manager_v1", 1, 2, kExtBgManagerRequests, 1, kExtBgManagerEvents
};

// ----------------------------------------------------------------------------
// 2. Legacy org_kde_kwin_blur_manager (KDE Plasma 6.0–6.5 fallback)
// ----------------------------------------------------------------------------
static const wl_message kKwinBlurRequests[] = {
    {"commit", "", kEmptyTypes},
    {"set_region", "?o", kRegionTypes},
    {"release", "", kEmptyTypes},
};
static const wl_interface kKwinBlurInterface = {
    "org_kde_kwin_blur", 1, 3, kKwinBlurRequests, 0, nullptr
};

static const wl_interface *kKwinBlurCreateTypes[] = {&kKwinBlurInterface, &wl_surface_interface};
static const wl_interface *kKwinBlurUnsetTypes[] = {&wl_surface_interface};
static const wl_message kKwinBlurManagerRequests[] = {
    {"create", "no", kKwinBlurCreateTypes},
    {"unset", "o", kKwinBlurUnsetTypes},
};
static const wl_interface kKwinBlurManagerInterface = {
    "org_kde_kwin_blur_manager", 1, 2, kKwinBlurManagerRequests, 0, nullptr
};

struct BlurGlobalState {
    wl_display *display = nullptr;
    wl_proxy *extManager = nullptr;
    wl_proxy *kwinManager = nullptr;
    uint32_t extCapabilities = 1; // assume blur capable until server says otherwise
    bool probed = false;
};

BlurGlobalState &blurGlobal()
{
    static BlurGlobalState s;
    return s;
}

struct ExtBgManagerListener {
    void (*capabilities)(void *data, wl_proxy *manager, uint32_t flags);
};

static const ExtBgManagerListener kExtBgListener = {
    [](void *data, wl_proxy *, uint32_t flags) {
        static_cast<BlurGlobalState *>(data)->extCapabilities = flags;
    }
};

bool ensureBlurBackend(wl_display *display)
{
    if (!display) return false;
    auto &st = blurGlobal();
    if (st.probed && st.display == display)
        return (st.extManager && (st.extCapabilities & 1u)) || st.kwinManager != nullptr;

    st.display = display;
    st.extManager = nullptr;
    st.kwinManager = nullptr;
    st.extCapabilities = 0;
    st.probed = true;

    wl_event_queue *queue = wl_display_create_queue(display);
    if (!queue) return false;
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    if (!wrapper) {
        wl_event_queue_destroy(queue);
        return false;
    }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    static const wl_registry_listener kRegListener = {
        [](void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver) {
            auto *g = static_cast<BlurGlobalState *>(data);
            if (!g->extManager && std::strcmp(iface, "ext_background_effect_manager_v1") == 0) {
                g->extManager = static_cast<wl_proxy *>(
                    wl_registry_bind(reg, name, &kExtBgManagerInterface, ver >= 1 ? 1 : ver));
                if (g->extManager) {
                    wl_proxy_add_listener(
                        g->extManager,
                        reinterpret_cast<void (**)(void)>(const_cast<ExtBgManagerListener *>(&kExtBgListener)),
                        g);
                }
            } else if (!g->kwinManager && std::strcmp(iface, "org_kde_kwin_blur_manager") == 0) {
                g->kwinManager = static_cast<wl_proxy *>(
                    wl_registry_bind(reg, name, &kKwinBlurManagerInterface, ver >= 1 ? 1 : ver));
            }
        },
        [](void *, wl_registry *, uint32_t) {}
    };
    wl_registry_add_listener(registry, &kRegListener, &st);
    wl_display_roundtrip_queue(display, queue);
    // Second roundtrip delivers initial ext_background_effect_manager_v1.capabilities event
    if (st.extManager) {
        wl_display_roundtrip_queue(display, queue);
        // Move manager back to default queue so future capability updates are harmlessly dispatched
        wl_proxy_set_queue(st.extManager, nullptr);
    }
    if (st.kwinManager) {
        wl_proxy_set_queue(st.kwinManager, nullptr);
    }
    wl_registry_destroy(registry);
    wl_proxy_wrapper_destroy(wrapper);
    wl_event_queue_destroy(queue);
    return (st.extManager && (st.extCapabilities & 1u)) || st.kwinManager != nullptr;
}

class WaylandBlurFilter final : public QObject
{
public:
    explicit WaylandBlurFilter(QWidget *w)
        : QObject(w), m_widget(w)
    {
        w->installEventFilter(this);
    }

    ~WaylandBlurFilter() override
    {
        releaseBlur(false);
    }

    void setBlurEnabled(bool enabled)
    {
        m_enabled = enabled;
        if (!m_enabled) {
            releaseBlur(true);
            if (m_widget) {
                m_widget->setProperty("panelBackdropBlurActive", false);
                m_widget->setProperty("panelBackdropBlurStatus", QStringLiteral("disabled"));
            }
            return;
        }
        scheduleApply();
    }

protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (!m_enabled) return false;
        switch (ev->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::PlatformSurface:
            scheduleApply();
            break;
        case QEvent::Hide:
            releaseBlur(false);
            break;
        default:
            break;
        }
        return false;
    }

private:
    void scheduleApply()
    {
        if (m_pending || !m_widget) return;
        m_pending = true;
        QPointer<WaylandBlurFilter> self(this);
        QTimer::singleShot(0, this, [self] {
            if (!self) return;
            self->m_pending = false;
            self->applyNow();
        });
    }

    void releaseBlur(bool clearOnSurface)
    {
        if (m_extSurface) {
            if (clearOnSurface && m_lastSurface) {
                wl_proxy_marshal_flags(m_extSurface, 1 /*set_blur_region*/, nullptr, 1, 0, nullptr);
                wl_surface_commit(m_lastSurface);
            }
            wl_proxy_marshal_flags(m_extSurface, 0 /*destroy*/, nullptr, 1, WL_MARSHAL_FLAG_DESTROY);
            m_extSurface = nullptr;
        }
        if (m_kwinBlur) {
            wl_proxy_marshal_flags(m_kwinBlur, 2 /*release*/, nullptr, 1, WL_MARSHAL_FLAG_DESTROY);
            m_kwinBlur = nullptr;
            if (clearOnSurface && m_lastSurface && blurGlobal().kwinManager) {
                wl_proxy_marshal_flags(blurGlobal().kwinManager, 1 /*unset*/, nullptr, 1, 0, m_lastSurface);
                wl_surface_commit(m_lastSurface);
            }
        }
        if (clearOnSurface && blurGlobal().display) {
            wl_display_flush(blurGlobal().display);
        }
        m_lastSurface = nullptr;
    }

    wl_region *buildRoundedRegion(wl_compositor *compositor) const
    {
        const int inset = m_widget->property("panelBlurInset").isValid()
            ? m_widget->property("panelBlurInset").toInt()
            : 6;
        const int radius = m_widget->property("panelBlurRadius").isValid()
            ? m_widget->property("panelBlurRadius").toInt()
            : 22;
        const QRect box = m_widget->rect().adjusted(inset, inset, -inset, -inset);
        if (box.width() <= 2 || box.height() <= 2) return nullptr;

        wl_region *region = wl_compositor_create_region(compositor);
        if (!region) return nullptr;
        const int r = qBound(0, radius, qMin(box.width(), box.height()) / 2);
        if (r <= 1) {
            wl_region_add(region, box.x(), box.y(), box.width(), box.height());
        } else {
            if (box.height() > 2 * r) {
                wl_region_add(region, box.x(), box.y() + r, box.width(), box.height() - 2 * r);
            }
            for (int dy = 0; dy < r; ++dy) {
                const double cy = double(r - dy) - 0.5;
                const int dx = static_cast<int>(std::ceil(double(r) - std::sqrt(qMax(0.0, double(r * r) - cy * cy))));
                const int rowW = box.width() - 2 * dx;
                if (rowW > 0) {
                    wl_region_add(region, box.x() + dx, box.y() + dy, rowW, 1);
                    wl_region_add(region, box.x() + dx, box.bottom() - dy, rowW, 1);
                }
            }
        }
        return region;
    }

    void applyNow()
    {
        if (!m_enabled || !m_widget || !m_widget->isVisible()) return;
        if (!QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) return;
        auto *wlApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
        if (!wlApp || !wlApp->display() || !wlApp->compositor()) return;
        if (!m_widget->windowHandle() || !QGuiApplication::platformNativeInterface()) return;

        auto *surface = static_cast<wl_surface *>(
            QGuiApplication::platformNativeInterface()->nativeResourceForWindow(
                QByteArrayLiteral("surface"), m_widget->windowHandle()));
        if (!surface) return;

        if (!ensureBlurBackend(wlApp->display())) {
            m_widget->setProperty("panelBackdropBlurActive", false);
            m_widget->setProperty("panelBackdropBlurStatus", QStringLiteral("no-wayland-blur-protocol"));
            return;
        }

        auto &st = blurGlobal();
        wl_region *region = buildRoundedRegion(wlApp->compositor());
        if (!region) return;

        if (st.extManager && (st.extCapabilities & 1u)) {
            if (m_lastSurface != surface) {
                releaseBlur(false);
                m_extSurface = wl_proxy_marshal_flags(
                    st.extManager, 1 /*get_background_effect*/,
                    &kExtBgSurfaceInterface, 1, 0, nullptr, surface);
                m_lastSurface = surface;
            }
            if (m_extSurface) {
                wl_proxy_marshal_flags(m_extSurface, 1 /*set_blur_region*/, nullptr, 1, 0, region);
                wl_surface_commit(surface);
                wl_display_flush(wlApp->display());
                m_widget->setProperty("panelBackdropBlurActive", true);
                m_widget->setProperty("panelBackdropBlurStatus", QStringLiteral("wayland-ext-background-effect-v1"));
            }
        } else if (st.kwinManager) {
            if (m_lastSurface != surface) {
                releaseBlur(false);
                m_kwinBlur = wl_proxy_marshal_flags(
                    st.kwinManager, 0 /*create*/,
                    &kKwinBlurInterface, 1, 0, nullptr, surface);
                m_lastSurface = surface;
            }
            if (m_kwinBlur) {
                wl_proxy_marshal_flags(m_kwinBlur, 1 /*set_region*/, nullptr, 1, 0, region);
                wl_proxy_marshal_flags(m_kwinBlur, 0 /*commit*/, nullptr, 1, 0);
                wl_surface_commit(surface);
                wl_display_flush(wlApp->display());
                m_widget->setProperty("panelBackdropBlurActive", true);
                m_widget->setProperty("panelBackdropBlurStatus", QStringLiteral("wayland-kwin-blur"));
            }
        }
        wl_region_destroy(region);
    }

    QPointer<QWidget> m_widget;
    wl_proxy *m_extSurface = nullptr;
    wl_proxy *m_kwinBlur = nullptr;
    wl_surface *m_lastSurface = nullptr;
    bool m_enabled = false;
    bool m_pending = false;
};

} // namespace
#endif // Q_OS_LINUX && FLOATINGBALL_WAYLAND_CLIENT && FLOATINGBALL_QT_GUI_PRIVATE

bool PanelBackdrop::supported()
{
#if defined(Q_OS_LINUX) && defined(FLOATINGBALL_WAYLAND_CLIENT) && defined(FLOATINGBALL_QT_GUI_PRIVATE)
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        if (auto *wlApp = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr) {
            return ensureBlurBackend(wlApp->display());
        }
    }
#endif
    return false;
}

bool PanelBackdrop::setEnabled(QWidget *widget, bool enabled)
{
    if (!widget) return false;
    widget->setProperty("panelBackdropBlurEnabled", enabled);
#if defined(Q_OS_LINUX) && defined(FLOATINGBALL_WAYLAND_CLIENT) && defined(FLOATINGBALL_QT_GUI_PRIVATE)
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        auto *filter = static_cast<WaylandBlurFilter *>(
            widget->findChild<QObject *>(QStringLiteral("waylandBlurFilter")));
        if (!filter && enabled) {
            filter = new WaylandBlurFilter(widget);
            filter->setObjectName(QStringLiteral("waylandBlurFilter"));
        }
        if (filter) {
            filter->setBlurEnabled(enabled);
            return true;
        }
    }
#endif
    widget->setProperty("panelBackdropBlurActive", false);
    widget->setProperty("panelBackdropBlurStatus", QStringLiteral("unsupported-platform"));
    return false;
}

void PanelBackdrop::configureAccessoryApp() {}

void PanelBackdrop::configureFloatingSurface(QWidget *widget, bool activateOnShow)
{
    if (!widget) return;
    if (!activateOnShow) {
        widget->setAttribute(Qt::WA_ShowWithoutActivating, true);
    }
}

void *PanelBackdrop::installGlobalPointerDownMonitor(QObject *, std::function<void(const QPoint &)>)
{
    return nullptr;
}

void PanelBackdrop::removeGlobalPointerDownMonitor(void *) {}

#endif
