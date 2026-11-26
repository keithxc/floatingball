#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>
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
bool launchDetachedApp(const QString &program,
                       const QStringList &arguments = {},
                       const QString &appIdHint = QString(),
                       const QString &workingDir = QString());
#ifdef Q_OS_LINUX
QStringList buildSystemdLaunchArgs(const QString &program,
                                   const QStringList &arguments,
                                   const QString &appIdHint = QString(),
                                   const QString &workingDir = QString(),
                                   const QString &uniqueSuffix = QString());
#endif
Outcome openTerminal();
Outcome openBrowser();
Outcome openHomeFolder();
Outcome openDownloadsFolder();
Outcome openTrash();

// Native OS / external screenshot & screen recording triggers
// (KDE Spectacle on Linux, macshot / Screenshot.app on macOS)
Outcome triggerNativeScreenshot();
Outcome triggerNativeScreenRecord();
Outcome triggerNativeScrollCapture();

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
