#pragma once

#include <QPoint>
#include <QString>
#include <functional>

namespace DesktopActions {

struct Outcome {
    bool ok = false;
    QString message;
};

// Navigation & multitasking
Outcome navigateBack();
Outcome navigateForward();
Outcome showOverview();
Outcome showDesktop();
Outcome nextTab();

// Active window touch rescue (replaces tiny 12pt window chrome buttons)
Outcome closeActiveWindow();
Outcome minimizeActiveWindow();
Outcome toggleFullscreenActiveWindow();

// Clipboard & input method helpers on the active external window
Outcome copySelection();
Outcome pasteClipboard();
Outcome switchInputMethod();
Outcome toggleVirtualKeyboard();

// Launchers
Outcome openTerminal();
Outcome openBrowser();
Outcome openHomeFolder();
Outcome openDownloadsFolder();
Outcome openTrash();

// System audio (non-blocking query/update)
void queryVolume(std::function<void(int percent, bool muted, bool supported)> callback);
void setVolume(int percent);
void toggleMute(std::function<void(int percent, bool muted, bool supported)> callback);

// Screen brightness (non-blocking query/update, supported on laptops / KDE PowerManagement)
void queryBrightness(std::function<void(int percent, bool supported)> callback);
void setBrightness(int percent);

// macOS Accessibility permission check (returns true on non-macOS)
bool accessibilityTrusted(bool promptUser = false);

// Non-blocking floating pill toast near top of screen or anchor
void showToast(const QString &message, const QPoint &anchor = QPoint());

} // namespace DesktopActions
