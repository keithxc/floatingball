#pragma once
class QWidget;

namespace FloatingWindow {
// Configure a persistent floating surface before showing a top-level widget.
void prepare(QWidget *widget, bool activateOnShow = false);
}
