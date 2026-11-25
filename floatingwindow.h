#pragma once
class QAction;
class QMenu;
class QPainter;
class QPoint;
class QRect;
class QScreen;
class QWidget;
class QString;

namespace FloatingWindow {
// Configure a persistent floating surface before showing a top-level widget.
void prepare(QWidget *widget, bool activateOnShow = false);
// Returns false for null or Wayland placeholder screens (empty name/geometry).
bool isValidScreen(const QScreen *screen);
// Present a QMenu safely over LayerShellQt on Wayland (avoids broken xdg_popup
// attachment onto zwlr_layer_surface_v1 and honors exact global positioning).
QAction *execPopupMenu(QMenu *menu, const QPoint &globalPos);
void popupMenu(QMenu *menu, const QPoint &globalPos);
double panelOpacity();
void setPanelOpacity(double opacity);
// Style templates may use @panelTint@ for the adjustable black backdrop tint.
// Foreground content stays at full opacity.
void setPanelStyleSheet(QWidget *widget, const QString &styleTemplate);
bool panelBlurEnabled();
bool panelBlurSupported();
void setPanelBlurEnabled(bool enabled);
// Paint a 2026 Liquid Glass borderless feathered translucent backdrop with
// optical specular refraction over the compositor Gaussian blur region.
void paintLiquidGlass(QPainter &painter, const QRect &widgetRect, double inset = 10.0, double radius = 26.0);
}
