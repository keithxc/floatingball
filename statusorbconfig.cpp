#include "statusorbrenderer.h"
#include "systemstatus.h"

#include <QSettings>
#include <QStringList>

namespace StatusOrbRenderer {

QString metricSlotId(MetricSlot slot)
{
    switch (slot) {
    case MetricSlot::Auto:        return QStringLiteral("auto");
    case MetricSlot::Battery:     return QStringLiteral("battery");
    case MetricSlot::Memory:      return QStringLiteral("memory");
    case MetricSlot::Brightness:  return QStringLiteral("brightness");
    case MetricSlot::Cpu:         return QStringLiteral("cpu");
    case MetricSlot::Disk:        return QStringLiteral("disk");
    case MetricSlot::Volume:      return QStringLiteral("volume");
    case MetricSlot::Network:     return QStringLiteral("network");
    case MetricSlot::DayProgress: return QStringLiteral("day_progress");
    case MetricSlot::DialTicks:   return QStringLiteral("dial_ticks");
    case MetricSlot::None:        return QStringLiteral("none");
    }
    return QStringLiteral("auto");
}

MetricSlot metricSlotFromId(const QString &id, MetricSlot fallback)
{
    const QString k = id.trimmed().toLower();
    if (k == QLatin1String("auto"))         return MetricSlot::Auto;
    if (k == QLatin1String("battery"))      return MetricSlot::Battery;
    if (k == QLatin1String("memory"))       return MetricSlot::Memory;
    if (k == QLatin1String("brightness"))   return MetricSlot::Brightness;
    if (k == QLatin1String("cpu"))          return MetricSlot::Cpu;
    if (k == QLatin1String("disk"))         return MetricSlot::Disk;
    if (k == QLatin1String("volume"))       return MetricSlot::Volume;
    if (k == QLatin1String("network"))      return MetricSlot::Network;
    if (k == QLatin1String("day_progress")) return MetricSlot::DayProgress;
    if (k == QLatin1String("dial_ticks"))   return MetricSlot::DialTicks;
    if (k == QLatin1String("none"))         return MetricSlot::None;
    return fallback;
}

QString metricSlotLabel(MetricSlot slot)
{
    switch (slot) {
    case MetricSlot::Auto:        return QStringLiteral("自动适配");
    case MetricSlot::Battery:     return QStringLiteral("电量");
    case MetricSlot::Memory:      return QStringLiteral("内存占用 (RAM)");
    case MetricSlot::Brightness:  return QStringLiteral("屏幕亮度");
    case MetricSlot::Cpu:         return QStringLiteral("CPU 负载");
    case MetricSlot::Disk:        return QStringLiteral("系统盘占用");
    case MetricSlot::Volume:      return QStringLiteral("音量");
    case MetricSlot::Network:     return QStringLiteral("网络信号");
    case MetricSlot::DayProgress: return QStringLiteral("今日时间进度");
    case MetricSlot::DialTicks:   return QStringLiteral("12 点表盘刻度");
    case MetricSlot::None:        return QStringLiteral("隐藏");
    }
    return QStringLiteral("自动适配");
}

QString centerModeId(CenterMode mode)
{
    switch (mode) {
    case CenterMode::Clock24H:      return QStringLiteral("clock_24h");
    case CenterMode::Clock24HNoIme: return QStringLiteral("clock_24h_pure");
    case CenterMode::ClockSingle:   return QStringLiteral("clock_single");
    case CenterMode::ImeAlways:     return QStringLiteral("ime_always");
    case CenterMode::CpuMemory:     return QStringLiteral("cpu_memory");
    case CenterMode::MinimalRing:   return QStringLiteral("minimal_ring");
    }
    return QStringLiteral("clock_24h");
}

CenterMode centerModeFromId(const QString &id, CenterMode fallback)
{
    const QString k = id.trimmed().toLower();
    if (k == QLatin1String("clock_24h"))      return CenterMode::Clock24H;
    if (k == QLatin1String("clock_24h_pure")) return CenterMode::Clock24HNoIme;
    if (k == QLatin1String("clock_single"))   return CenterMode::ClockSingle;
    if (k == QLatin1String("ime_always"))     return CenterMode::ImeAlways;
    if (k == QLatin1String("cpu_memory"))     return CenterMode::CpuMemory;
    if (k == QLatin1String("minimal_ring"))   return CenterMode::MinimalRing;
    return fallback;
}

QString centerModeLabel(CenterMode mode)
{
    switch (mode) {
    case CenterMode::Clock24H:      return QStringLiteral("双行 24H 时钟（切输入法闪显）");
    case CenterMode::Clock24HNoIme: return QStringLiteral("双行 24H 纯时钟");
    case CenterMode::ClockSingle:   return QStringLiteral("单行时钟 + 日期");
    case CenterMode::ImeAlways:     return QStringLiteral("常驻输入法徽标（双 / EN）");
    case CenterMode::CpuMemory:     return QStringLiteral("实时数字（CPU / 内存）");
    case CenterMode::MinimalRing:   return QStringLiteral("经典双光环");
    }
    return QStringLiteral("双行 24H 时钟（切输入法闪显）");
}

QString accentThemeId(AccentTheme theme)
{
    switch (theme) {
    case AccentTheme::Obsidian: return QStringLiteral("obsidian");
    case AccentTheme::Emerald:  return QStringLiteral("emerald");
    case AccentTheme::Ocean:    return QStringLiteral("ocean");
    case AccentTheme::Amber:    return QStringLiteral("amber");
    }
    return QStringLiteral("obsidian");
}

AccentTheme accentThemeFromId(const QString &id, AccentTheme fallback)
{
    const QString k = id.trimmed().toLower();
    if (k == QLatin1String("obsidian")) return AccentTheme::Obsidian;
    if (k == QLatin1String("emerald"))  return AccentTheme::Emerald;
    if (k == QLatin1String("ocean"))    return AccentTheme::Ocean;
    if (k == QLatin1String("amber"))    return AccentTheme::Amber;
    return fallback;
}

QString accentThemeLabel(AccentTheme theme)
{
    switch (theme) {
    case AccentTheme::Obsidian: return QStringLiteral("黑曜冰白（默认）");
    case AccentTheme::Emerald:  return QStringLiteral("翡翠薄荷");
    case AccentTheme::Ocean:    return QStringLiteral("深海冰蓝");
    case AccentTheme::Amber:    return QStringLiteral("琥珀暖光");
    }
    return QStringLiteral("黑曜冰白（默认）");
}

FaceConfig loadFaceConfig(const QSettings &settings)
{
    FaceConfig cfg;
    cfg.topArc = metricSlotFromId(settings.value("orbFace/topArc", "auto").toString(), MetricSlot::Auto);
    cfg.leftArc = metricSlotFromId(settings.value("orbFace/leftArc", "auto").toString(), MetricSlot::Auto);
    cfg.rightArc = metricSlotFromId(settings.value("orbFace/rightArc", "auto").toString(), MetricSlot::Auto);
    cfg.bottomDots = metricSlotFromId(settings.value("orbFace/bottomDots", "auto").toString(), MetricSlot::Auto);
    cfg.centerMode = centerModeFromId(settings.value("orbFace/centerMode", "clock_24h").toString(), CenterMode::Clock24H);
    cfg.accentTheme = accentThemeFromId(settings.value("orbFace/accentTheme", "obsidian").toString(), AccentTheme::Obsidian);
    cfg.showPrivacyDots = settings.value("orbFace/showPrivacyDots", true).toBool();
    cfg.showRadarDots = settings.value("orbFace/showRadarDots", true).toBool();
    return cfg;
}

void saveFaceConfig(QSettings &settings, const FaceConfig &cfg)
{
    settings.setValue("orbFace/topArc", metricSlotId(cfg.topArc));
    settings.setValue("orbFace/leftArc", metricSlotId(cfg.leftArc));
    settings.setValue("orbFace/rightArc", metricSlotId(cfg.rightArc));
    settings.setValue("orbFace/bottomDots", metricSlotId(cfg.bottomDots));
    settings.setValue("orbFace/centerMode", centerModeId(cfg.centerMode));
    settings.setValue("orbFace/accentTheme", accentThemeId(cfg.accentTheme));
    settings.setValue("orbFace/showPrivacyDots", cfg.showPrivacyDots);
    settings.setValue("orbFace/showRadarDots", cfg.showRadarDots);
}

FaceConfig presetConfig(const QString &presetId)
{
    FaceConfig cfg;
    if (presetId == QLatin1String("telemetry")) {
        cfg.topArc = MetricSlot::Memory;
        cfg.leftArc = MetricSlot::Cpu;
        cfg.rightArc = MetricSlot::Disk;
        cfg.bottomDots = MetricSlot::Network;
        cfg.centerMode = CenterMode::Clock24H;
        cfg.accentTheme = AccentTheme::Ocean;
    } else if (presetId == QLatin1String("watch")) {
        cfg.topArc = MetricSlot::DialTicks;
        cfg.leftArc = MetricSlot::None;
        cfg.rightArc = MetricSlot::None;
        cfg.bottomDots = MetricSlot::None;
        cfg.centerMode = CenterMode::ClockSingle;
        cfg.accentTheme = AccentTheme::Amber;
    } else if (presetId == QLatin1String("minimal")) {
        cfg.topArc = MetricSlot::None;
        cfg.leftArc = MetricSlot::None;
        cfg.rightArc = MetricSlot::None;
        cfg.bottomDots = MetricSlot::None;
        cfg.centerMode = CenterMode::Clock24H;
        cfg.accentTheme = AccentTheme::Obsidian;
    }
    return cfg;
}

HeaderPillState formatHeaderPill(const SystemStatusSnapshot &st)
{
    if (st.batteryPresent) {
        QString pwrTxt = QStringLiteral("平衡");
        QString pwrAcc = QStringLiteral("blue");
        if (st.powerProfile == QLatin1String("power-saver")) {
            pwrTxt = QStringLiteral("节能");
            pwrAcc = QStringLiteral("green");
        } else if (st.powerProfile == QLatin1String("performance")) {
            pwrTxt = QStringLiteral("性能");
            pwrAcc = QStringLiteral("rose");
        }
        return {
            QStringLiteral("%1%%2 %3")
                .arg(st.batteryPercent)
                .arg(st.batteryPluggedIn ? QStringLiteral("⚡") : QString(), pwrTxt),
            QStringLiteral("点击切换功耗模式（节能 / 平衡 / 性能）"),
            pwrAcc,
            false
        };
    }

    if (st.cpuAvailable || st.memoryAvailable) {
        QStringList parts;
        if (st.cpuAvailable && st.memoryAvailable) {
            parts.append(QStringLiteral("C %1%").arg(st.cpuPercent));
            parts.append(QStringLiteral("M %1%").arg(st.memoryPercent));
        } else if (st.cpuAvailable) {
            parts.append(QStringLiteral("CPU %1%").arg(st.cpuPercent));
        } else if (st.memoryAvailable) {
            parts.append(QStringLiteral("RAM %1%").arg(st.memoryPercent));
        }
        QStringList tipParts;
        if (st.cpuAvailable) tipParts.append(QStringLiteral("CPU %1%").arg(st.cpuPercent));
        if (st.memoryAvailable) {
            if (st.memoryTotalGiB > 0.1) {
                tipParts.append(QStringLiteral("内存 %1% (%2/%3 GB)")
                                    .arg(st.memoryPercent)
                                    .arg(st.memoryUsedGiB, 0, 'f', 1)
                                    .arg(st.memoryTotalGiB, 0, 'f', 0));
            } else {
                tipParts.append(QStringLiteral("内存 %1%").arg(st.memoryPercent));
            }
        }
        tipParts.append(QStringLiteral("点击打开活动监视器"));
        const bool hot = (st.memoryAvailable && st.memoryPercent >= 88) ||
                         (st.cpuAvailable && st.cpuPercent >= 85);
        return {
            parts.join(QStringLiteral(" · ")),
            tipParts.join(QStringLiteral(" · ")),
            hot ? QStringLiteral("rose") : QStringLiteral("blue"),
            true
        };
    }

    QString pwrTxt = QStringLiteral("平衡");
    QString pwrAcc = QStringLiteral("blue");
    if (st.powerProfile == QLatin1String("power-saver")) {
        pwrTxt = QStringLiteral("节能");
        pwrAcc = QStringLiteral("green");
    } else if (st.powerProfile == QLatin1String("performance")) {
        pwrTxt = QStringLiteral("性能");
        pwrAcc = QStringLiteral("rose");
    }
    return {pwrTxt, QStringLiteral("点击切换功耗模式（节能 / 平衡 / 性能）"), pwrAcc, false};
}

} // namespace StatusOrbRenderer
