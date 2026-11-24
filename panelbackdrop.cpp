#include "panelbackdrop.h"
#include <QVariant>
#include <QWidget>

#if !defined(__APPLE__)
bool PanelBackdrop::supported() { return false; }
bool PanelBackdrop::setEnabled(QWidget *widget, bool enabled)
{
    if (!widget) return false;
    widget->setProperty("panelBackdropBlurEnabled", enabled);
    widget->setProperty("panelBackdropBlurActive", false);
    widget->setProperty("panelBackdropBlurStatus", QStringLiteral("unsupported-platform"));
    return false;
}
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
