#ifndef STATUSORBRENDERER_H
#define STATUSORBRENDERER_H

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;
struct SystemStatusSnapshot;

namespace StatusOrbRenderer {

// Designed to support future user-customizable orb slots & live Settings preview.
enum class MetricSlot {
    Auto,       // Top: Battery -> Memory; Left: Brightness -> CPU; Right: Volume; Bottom: Network
    Battery,
    Memory,
    Brightness,
    Cpu,
    Volume,
    Network,
    None
};

struct FaceConfig {
    MetricSlot topArc = MetricSlot::Auto;
    MetricSlot leftArc = MetricSlot::Auto;
    MetricSlot rightArc = MetricSlot::Auto;
    MetricSlot bottomDots = MetricSlot::Auto;
};

struct HeaderPillState {
    QString text;
    QString tooltip;
    QString accent; // "blue", "green", "rose"
    bool opensMonitor = false;
};

MetricSlot resolveTopMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
MetricSlot resolveLeftMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
QColor memoryGaugeColor(int percent);
QColor cpuGaugeColor(int percent);

void paintFace(QPainter &p, const QPointF &sphereCenter, qreal diameter, qreal radius,
               const QFont &baseFont, const SystemStatusSnapshot &st,
               const QString &hh, const QString &mm, const QString &imeFlash,
               const FaceConfig &cfg = {});

void paintVerticalCapsuleBar(QPainter &p, const QRectF &capRect,
                             const SystemStatusSnapshot &st,
                             const FaceConfig &cfg = {});

QString buildTooltip(const SystemStatusSnapshot &st, const FaceConfig &cfg = {});
HeaderPillState formatHeaderPill(const SystemStatusSnapshot &st);

} // namespace StatusOrbRenderer

#endif // STATUSORBRENDERER_H
