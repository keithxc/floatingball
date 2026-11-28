#pragma once
#include <QPoint>
#include <functional>

class QObject;
class QWidget;

namespace PanelBackdrop {
bool supported();
bool setEnabled(QWidget *widget, bool enabled);
// Keep the app hidden from the macOS Dock (accessory policy) even when launched
// directly outside its .app bundle or when a settings dialog is active.
void configureAccessoryApp();
// Configure native floating tool-window behavior (non-activating touch surface,
// persistent across Spaces and full-screen apps on macOS).
void configureFloatingSurface(QWidget *widget, bool activateOnShow);
// Install a non-consuming global + local mouse-down monitor (macOS Cocoa) so
// clicking anywhere outside an expanded fan smoothly dismisses it without
// swallowing the user's click on other apps. Returns nullptr on platforms
// without native global monitors.
void *installGlobalPointerDownMonitor(QObject *owner, std::function<void(const QPoint &globalPos)> callback);
void removeGlobalPointerDownMonitor(void *token);
}
