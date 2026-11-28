#ifndef STATUSORBRENDERER_H
#define STATUSORBRENDERER_H

#include <QColor>
#include <QFont>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;
class QSettings;
struct SystemStatusSnapshot;

namespace StatusOrbRenderer {

enum class MetricSlot {
    Auto,
    Battery,
    Memory,
    Brightness,
    Cpu,
    Disk,
    Volume,
    Network,
    DayProgress,
    DialTicks,
    None
};

enum class CenterMode {
    Clock24H,      // Two-line HH / mm with transient IME flash
    Clock24HNoIme, // Pure two-line HH / mm without IME flash
    ClockSingle,   // Single-line HH:mm with MM/DD date below
    ImeAlways,     // Persistent input method badge ("双" / "EN")
    CpuMemory,     // Numeric telemetry: CPU% over RAM%
    MinimalRing    // Classic dual concentric obsidian ring
};

enum class AccentTheme {
    Obsidian, // Restrained frosted white + semantic amber/coral alerts (default)
    Emerald,  // Mint-emerald instrument tint
    Ocean,    // Ice-cyan / sky-blue instrument tint
    Amber     // Warm amber / champagne watch-face tint
};

struct FaceConfig {
    MetricSlot topArc = MetricSlot::Auto;
    MetricSlot leftArc = MetricSlot::Auto;
    MetricSlot rightArc = MetricSlot::Auto;
    MetricSlot bottomDots = MetricSlot::Auto;
    CenterMode centerMode = CenterMode::Clock24H;
    AccentTheme accentTheme = AccentTheme::Obsidian;
    bool showPrivacyDots = true;
    bool showRadarDots = true;

    bool operator==(const FaceConfig &other) const
    {
        return topArc == other.topArc && leftArc == other.leftArc &&
               rightArc == other.rightArc && bottomDots == other.bottomDots &&
               centerMode == other.centerMode && accentTheme == other.accentTheme &&
               showPrivacyDots == other.showPrivacyDots &&
               showRadarDots == other.showRadarDots;
    }
    bool operator!=(const FaceConfig &other) const { return !(*this == other); }
};

struct HeaderPillState {
    QString text;
    QString tooltip;
    QString accent; // "blue", "green", "rose"
    bool opensMonitor = false;
};

// Serialization & presets
QString metricSlotId(MetricSlot slot);
MetricSlot metricSlotFromId(const QString &id, MetricSlot fallback = MetricSlot::Auto);
QString metricSlotLabel(MetricSlot slot);

QString centerModeId(CenterMode mode);
CenterMode centerModeFromId(const QString &id, CenterMode fallback = CenterMode::Clock24H);
QString centerModeLabel(CenterMode mode);

QString accentThemeId(AccentTheme theme);
AccentTheme accentThemeFromId(const QString &id, AccentTheme fallback = AccentTheme::Obsidian);
QString accentThemeLabel(AccentTheme theme);

FaceConfig loadFaceConfig(const QSettings &settings);
void saveFaceConfig(QSettings &settings, const FaceConfig &cfg);
FaceConfig presetConfig(const QString &presetId);

// Slot resolution & colors
MetricSlot resolveTopMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
MetricSlot resolveLeftMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
MetricSlot resolveRightMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
MetricSlot resolveBottomMetric(const SystemStatusSnapshot &st, MetricSlot configured = MetricSlot::Auto);
QColor memoryGaugeColor(int percent, AccentTheme theme = AccentTheme::Obsidian);
QColor cpuGaugeColor(int percent, AccentTheme theme = AccentTheme::Obsidian);
QColor diskGaugeColor(int percent, AccentTheme theme = AccentTheme::Obsidian);

// Reusable rendering primitives (used by both live FloatingBall and OrbFaceStudio preview)
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
