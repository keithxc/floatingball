#pragma once
class QPainter;
class QRect;
class QWidget;
class QString;

namespace FloatingWindow {
// Configure a persistent floating surface before showing a top-level widget.
void prepare(QWidget *widget, bool activateOnShow = false);
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
