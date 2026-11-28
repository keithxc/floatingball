#include "statusorbrenderer.h"
#include "systemstatus.h"

#include <QDate>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QTime>
#include <QtMath>

namespace StatusOrbRenderer {

namespace {

struct GaugeReading {
    bool available = false;
    int percent = 0; // 0..100
    QColor fillColor;
    bool allDim = false;
    bool forceAllFive = false; // e.g. offline network -> 5 red dots
    bool minVisiblePip = false;
};

QColor themePrimaryInk(AccentTheme theme)
{
    switch (theme) {
    case AccentTheme::Emerald: return QColor("#d1fae5");
    case AccentTheme::Ocean:   return QColor("#dbeafe");
    case AccentTheme::Amber:   return QColor("#fef3c7");
    case AccentTheme::Obsidian:
    default:                   return QColor("#f2f6fc");
    }
}

QColor themeSecondaryInk(AccentTheme theme)
{
    switch (theme) {
    case AccentTheme::Emerald: return QColor("#6ee7b7");
    case AccentTheme::Ocean:   return QColor("#7dd3fc");
    case AccentTheme::Amber:   return QColor("#fcd34d");
    case AccentTheme::Obsidian:
    default:                   return QColor("#dbeafe");
    }
}

QColor themeCpuNormalInk(AccentTheme theme)
{
    switch (theme) {
    case AccentTheme::Emerald: return QColor("#a7f3d0");
    case AccentTheme::Ocean:   return QColor("#93c5fd");
    case AccentTheme::Amber:   return QColor("#fde68a");
    case AccentTheme::Obsidian:
    default:                   return QColor("#bae6fd");
    }
}

int currentDayProgressPercent()
{
    const QTime now = QTime::currentTime();
    const int secs = now.hour() * 3600 + now.minute() * 60 + now.second();
    return qBound(0, qRound(secs * 100.0 / 86400.0), 100);
}

GaugeReading evaluateMetric(MetricSlot slot, const SystemStatusSnapshot &st, AccentTheme theme)
{
    GaugeReading r;
    const QColor ink = themePrimaryInk(theme);
    switch (slot) {
    case MetricSlot::Battery:
        if (!st.batteryPresent) return r;
        r.available = true;
        r.percent = qBound(0, st.batteryPercent, 100);
        r.fillColor = st.batteryPluggedIn ? QColor("#6edba0")
            : (r.percent < 15 ? QColor("#ef5350")
               : (r.percent < 25 ? QColor("#f4c542") : ink));
        return r;
    case MetricSlot::Memory:
        if (!st.memoryAvailable) return r;
        r.available = true;
        r.percent = qBound(0, st.memoryPercent, 100);
        r.fillColor = memoryGaugeColor(r.percent, theme);
        return r;
    case MetricSlot::Cpu:
        if (!st.cpuAvailable) return r;
        r.available = true;
        r.percent = qBound(0, st.cpuPercent, 100);
        r.fillColor = cpuGaugeColor(r.percent, theme);
        r.minVisiblePip = true;
        return r;
    case MetricSlot::Disk:
        if (!st.diskAvailable) return r;
        r.available = true;
        r.percent = qBound(0, st.diskPercent, 100);
        r.fillColor = diskGaugeColor(r.percent, theme);
        return r;
    case MetricSlot::Brightness:
        if (!st.brightnessAvailable) return r;
        r.available = true;
        r.percent = qBound(0, st.brightnessPercent, 100);
        r.fillColor = ink;
        return r;
    case MetricSlot::Volume:
        if (!st.volumeAvailable) return r;
        r.available = true;
        r.percent = qBound(0, st.volumePercent, 100);
        r.fillColor = ink;
        r.allDim = st.volumeMuted;
        return r;
    case MetricSlot::Network:
        r.available = true;
        r.percent = st.networkConnected ? qBound(0, st.signalQuality, 100) : 100;
        r.fillColor = st.networkConnected ? ink : QColor("#ef5350");
        r.forceAllFive = !st.networkConnected;
        return r;
    case MetricSlot::DayProgress:
        r.available = true;
        r.percent = currentDayProgressPercent();
        r.fillColor = themeSecondaryInk(theme);
        r.minVisiblePip = true;
        return r;
    default:
        return r;
    }
}

void appendMetricTooltip(QStringList &tips, MetricSlot slot, const SystemStatusSnapshot &st)
{
    auto addUnique = [&](const QString &s) {
        if (!s.isEmpty() && !tips.contains(s)) tips.append(s);
    };
    switch (slot) {
    case MetricSlot::Battery:
        if (st.batteryPresent)
            addUnique(QStringLiteral("电量 %1%%2")
                          .arg(st.batteryPercent)
                          .arg(st.batteryPluggedIn ? QStringLiteral("（充电中）") : QString()));
        break;
    case MetricSlot::Memory:
        if (st.memoryAvailable) {
            if (st.memoryTotalGiB > 0.1) {
                addUnique(QStringLiteral("内存 %1% (%2/%3 GB)")
                              .arg(st.memoryPercent)
                              .arg(st.memoryUsedGiB, 0, 'f', 1)
                              .arg(st.memoryTotalGiB, 0, 'f', 0));
            } else {
                addUnique(QStringLiteral("内存 %1%").arg(st.memoryPercent));
            }
        }
        break;
    case MetricSlot::Cpu:
        if (st.cpuAvailable) addUnique(QStringLiteral("CPU %1%").arg(st.cpuPercent));
        break;
    case MetricSlot::Disk:
        if (st.diskAvailable) {
            if (st.diskTotalGiB > 0.5) {
                const double freeGiB = qMax(0.0, st.diskTotalGiB - st.diskUsedGiB);
                addUnique(QStringLiteral("磁盘 %1% (剩 %2 GB)")
                              .arg(st.diskPercent)
                              .arg(freeGiB, 0, 'f', 0));
            } else {
                addUnique(QStringLiteral("磁盘 %1%").arg(st.diskPercent));
            }
        }
        break;
    case MetricSlot::Brightness:
        if (st.brightnessAvailable) addUnique(QStringLiteral("亮度 %1%").arg(st.brightnessPercent));
        break;
    case MetricSlot::Volume:
        if (st.volumeAvailable)
            addUnique(st.volumeMuted ? QStringLiteral("音量 静音") : QStringLiteral("音量 %1%").arg(st.volumePercent));
        break;
    case MetricSlot::Network:
        if (!st.networkText.isEmpty()) addUnique(st.networkText);
        break;
    case MetricSlot::DayProgress:
        addUnique(QStringLiteral("今日已过 %1%").arg(currentDayProgressPercent()));
        break;
    default:
        break;
    }
}

void drawDialTicksRing(QPainter &p, const QPointF &center, qreal rInner, qreal d, AccentTheme theme)
{
    const QColor majorCol = themeSecondaryInk(theme);
    const QColor minorCol(242, 246, 252, 90);
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 12; ++i) {
        const bool major = (i % 3 == 0);
        const qreal rad = qDegreesToRadians(i * 30.0 - 90.0);
        const qreal len = major ? qMax(2.8, d * 0.062) : qMax(1.8, d * 0.038);
        const qreal r1 = rInner + len * 0.45;
        const qreal r0 = r1 - len;
        p.setPen(QPen(major ? majorCol : minorCol, major ? qMax(1.4, d * 0.028) : qMax(1.0, d * 0.018),
                      Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(center.x() + r0 * qCos(rad), center.y() + r0 * qSin(rad)),
                   QPointF(center.x() + r1 * qCos(rad), center.y() + r1 * qSin(rad)));
    }
}

} // namespace

MetricSlot resolveTopMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured == MetricSlot::DialTicks || configured == MetricSlot::DayProgress) return configured;
    if (configured != MetricSlot::Auto) {
        return evaluateMetric(configured, st, AccentTheme::Obsidian).available ? configured : MetricSlot::None;
    }
    if (st.batteryPresent) return MetricSlot::Battery;
    if (st.memoryAvailable) return MetricSlot::Memory;
    return MetricSlot::None;
}

MetricSlot resolveLeftMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured == MetricSlot::DayProgress) return configured;
    if (configured != MetricSlot::Auto) {
        return evaluateMetric(configured, st, AccentTheme::Obsidian).available ? configured : MetricSlot::None;
    }
    if (st.brightnessAvailable) return MetricSlot::Brightness;
    if (st.cpuAvailable) return MetricSlot::Cpu;
    return MetricSlot::None;
}

MetricSlot resolveRightMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured != MetricSlot::Auto) {
        return evaluateMetric(configured, st, AccentTheme::Obsidian).available ? configured : MetricSlot::None;
    }
    return st.volumeAvailable ? MetricSlot::Volume : MetricSlot::None;
}

MetricSlot resolveBottomMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured != MetricSlot::Auto) {
        return evaluateMetric(configured, st, AccentTheme::Obsidian).available ? configured : MetricSlot::None;
    }
    return MetricSlot::Network;
}

QColor memoryGaugeColor(int percent, AccentTheme theme)
{
    if (percent >= 88) return QColor("#ef5350");
    if (percent >= 75) return QColor("#f4c542");
    return themeSecondaryInk(theme);
}

QColor cpuGaugeColor(int percent, AccentTheme theme)
{
    if (percent >= 85) return QColor("#fb7185");
    if (percent >= 60) return QColor("#fbbf24");
    return themeCpuNormalInk(theme);
}

QColor diskGaugeColor(int percent, AccentTheme theme)
{
    if (percent >= 90) return QColor("#ef5350");
    if (percent >= 80) return QColor("#f4c542");
    return themeSecondaryInk(theme);
}

void paintFace(QPainter &p, const QPointF &sphereCenter, qreal d, qreal radius,
               const QFont &baseFont, const SystemStatusSnapshot &st,
               const QString &hh, const QString &mm, const QString &imeFlash,
               const FaceConfig &cfg)
{
    const QColor dimInk(242, 246, 252, 52);
    const qreal rOuter = radius * 0.84;
    const qreal rInner = radius * 0.65;
    const QRectF outerArcRect(sphereCenter.x() - rOuter, sphereCenter.y() - rOuter, rOuter * 2.0, rOuter * 2.0);
    const QRectF innerArcRect(sphereCenter.x() - rInner, sphereCenter.y() - rInner, rInner * 2.0, rInner * 2.0);

    // 1. Left outer continuous arc (bottom -> top)
    const MetricSlot leftSlot = resolveLeftMetric(st, cfg.leftArc);
    if (leftSlot != MetricSlot::None) {
        const GaugeReading g = evaluateMetric(leftSlot, st, cfg.accentTheme);
        if (g.available) {
            const qreal trackW = qMax(1.6, d * 0.036);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(dimInk, trackW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(outerArcRect, qRound(220.0 * 16), qRound(-80.0 * 16));
            if (!g.allDim && g.percent > 0) {
                const qreal effPct = g.minVisiblePip ? qMax(4.0, static_cast<qreal>(g.percent)) : static_cast<qreal>(g.percent);
                p.setPen(QPen(g.fillColor, trackW, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(outerArcRect, qRound(220.0 * 16), qRound(-80.0 * effPct / 100.0 * 16));
            }
        }
    }

    // 2. Right outer 13-dot arc (bottom -> top)
    const MetricSlot rightSlot = resolveRightMetric(st, cfg.rightArc);
    if (rightSlot != MetricSlot::None) {
        const GaugeReading g = evaluateMetric(rightSlot, st, cfg.accentTheme);
        if (g.available) {
            const int litDots = g.allDim ? 0 : static_cast<int>(qCeil(g.percent * 13.0 / 100.0));
            const qreal dotR = qMax(0.95, d * 0.019);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 13; ++i) {
                const qreal deg = 40.0 - 80.0 * i / 12.0;
                const qreal rad = qDegreesToRadians(deg);
                const QPointF pt(sphereCenter.x() + rOuter * qCos(rad),
                                 sphereCenter.y() + rOuter * qSin(rad));
                p.setBrush(i < litDots ? g.fillColor : dimInk);
                p.drawEllipse(pt, dotR, dotR);
            }
        }
    }

    // 3. Top 250-deg inner arc (or 12-hour watch dial ticks near bezel rim)
    const MetricSlot topSlot = resolveTopMetric(st, cfg.topArc);
    if (topSlot == MetricSlot::DialTicks) {
        drawDialTicksRing(p, sphereCenter, radius * 0.81, d, cfg.accentTheme);
    } else if (topSlot != MetricSlot::None) {
        const GaugeReading g = evaluateMetric(topSlot, st, cfg.accentTheme);
        if (g.available) {
            const qreal arcW = qMax(2.0, d * 0.046);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(dimInk, arcW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(innerArcRect, qRound(215.0 * 16), qRound(-250.0 * 16));
            if (!g.allDim && g.percent > 0) {
                p.setPen(QPen(g.fillColor, arcW, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(innerArcRect, qRound(215.0 * 16), qRound(-250.0 * g.percent / 100.0 * 16));
            }
        }
    }

    // 4. Bottom 5 dots
    const MetricSlot bottomSlot = resolveBottomMetric(st, cfg.bottomDots);
    if (bottomSlot != MetricSlot::None) {
        const GaugeReading g = evaluateMetric(bottomSlot, st, cfg.accentTheme);
        if (g.available) {
            const int lit = g.forceAllFive ? 5
                : (g.allDim ? 0 : qBound(0, static_cast<int>(qCeil(g.percent / 20.0)), 5));
            const qreal sigR = qMax(1.15, d * 0.022);
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 5; ++i) {
                const qreal deg = 130.0 - i * 20.0;
                const qreal rad = qDegreesToRadians(deg);
                const QPointF pt(sphereCenter.x() + rInner * qCos(rad),
                                 sphereCenter.y() + rInner * qSin(rad));
                p.setBrush(i < lit ? g.fillColor : dimInk);
                p.drawEllipse(pt, sigR, sigR);
            }
        }
    }

    // 5. Center Emblem
    const bool showImeFlash = !imeFlash.isEmpty() && cfg.centerMode == CenterMode::Clock24H;
    if (showImeFlash || cfg.centerMode == CenterMode::ImeAlways) {
        const QString badge = !imeFlash.isEmpty() ? imeFlash
            : (st.inputLabel.isEmpty() ? QStringLiteral("EN") : st.inputLabel);
        if (showImeFlash) {
            const qreal badgeW = rInner * 1.38;
            const qreal badgeH = rInner * 1.12;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(56, 189, 248, 52));
            p.drawRoundedRect(QRectF(sphereCenter.x() - badgeW * 0.5, sphereCenter.y() - badgeH * 0.5,
                                     badgeW, badgeH), 7.0, 7.0);
        }
        QFont f = baseFont;
        f.setPixelSize(qRound(badge == QStringLiteral("双") ? d * 0.28 : d * 0.23));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        p.setPen(showImeFlash ? QColor("#7dd3fc") : themePrimaryInk(cfg.accentTheme));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - rInner - 0.5,
                          rInner * 2.0, rInner * 2.0), Qt::AlignCenter, badge);
    } else if (cfg.centerMode == CenterMode::MinimalRing) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255, 130), qMax(1.4, d * 0.036)));
        p.drawEllipse(sphereCenter, d * 0.26, d * 0.26);
        p.setPen(QPen(themePrimaryInk(cfg.accentTheme), qMax(1.8, d * 0.046)));
        p.drawEllipse(sphereCenter, d * 0.16, d * 0.16);
    } else if (cfg.centerMode == CenterMode::ClockSingle) {
        QFont f = baseFont;
        f.setPixelSize(qMax(9, qRound(d * 0.172)));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        p.setPen(themePrimaryInk(cfg.accentTheme));
        const qreal lineH = d * 0.21;
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - d * 0.08 - lineH * 0.5,
                          rInner * 2.0, lineH), Qt::AlignCenter, hh + QLatin1Char(':') + mm);
        f.setPixelSize(qMax(7, qRound(d * 0.12)));
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(QColor("#94a3b8"));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() + d * 0.105 - lineH * 0.4,
                          rInner * 2.0, lineH * 0.8), Qt::AlignCenter,
                   QDate::currentDate().toString(QStringLiteral("MM/dd")));
    } else if (cfg.centerMode == CenterMode::CpuMemory) {
        QFont f = baseFont;
        f.setPixelSize(qMax(9, qRound(d * 0.175)));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        const qreal lineH = d * 0.21;
        const qreal vShift = d * 0.10;
        const QString topTxt = st.cpuAvailable ? QStringLiteral("C%1").arg(st.cpuPercent) : QStringLiteral("C--");
        const QString botTxt = st.memoryAvailable ? QStringLiteral("M%1").arg(st.memoryPercent) : QStringLiteral("M--");
        p.setPen(cpuGaugeColor(st.cpuPercent, cfg.accentTheme));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - vShift - lineH * 0.5,
                          rInner * 2.0, lineH), Qt::AlignCenter, topTxt);
        p.setPen(memoryGaugeColor(st.memoryPercent, cfg.accentTheme));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() + vShift - lineH * 0.5,
                          rInner * 2.0, lineH), Qt::AlignCenter, botTxt);
    } else {
        QFont f = baseFont;
        f.setPixelSize(qMax(10, qRound(d * 0.215)));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        const qreal lineH = d * 0.235;
        const qreal vShift = d * 0.102;
        p.setPen(themePrimaryInk(cfg.accentTheme));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - vShift - lineH * 0.5,
                          rInner * 2.0, lineH), Qt::AlignCenter, hh);
        p.setPen(QColor("#cbd5e1"));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() + vShift - lineH * 0.5,
                          rInner * 2.0, lineH), Qt::AlignCenter, mm);
    }

    // 6. Privacy indicator micro-dots (Top-Right = Mic, Bottom-Right = Camera)
    if (cfg.showPrivacyDots) {
        const qreal cornerOffset = radius * 0.59;
        const qreal cornerDotR = qMax(2.1, d * 0.042);
        p.setPen(Qt::NoPen);
        if (st.micActive) {
            const QPointF pt(sphereCenter.x() + cornerOffset, sphereCenter.y() - cornerOffset);
            p.setBrush(QColor("#ff9f0a"));
            p.drawEllipse(pt, cornerDotR, cornerDotR);
        }
        if (st.cameraActive) {
            const QPointF pt(sphereCenter.x() + cornerOffset, sphereCenter.y() + cornerOffset);
            p.setBrush(QColor("#34c759"));
            p.drawEllipse(pt, cornerDotR, cornerDotR);
        }
    }
}

void paintVerticalCapsuleBar(QPainter &p, const QRectF &capRect,
                             const SystemStatusSnapshot &st,
                             const FaceConfig &cfg)
{
    const MetricSlot topSlot = resolveTopMetric(st, cfg.topArc);
    if (topSlot == MetricSlot::None || topSlot == MetricSlot::DialTicks) return;
    const GaugeReading g = evaluateMetric(topSlot, st, cfg.accentTheme);
    if (!g.available) return;

    const QPointF c = capRect.center();
    const qreal bw = capRect.width() * 0.54;
    const QRectF bTrack(c.x() - bw * 0.5, capRect.bottom() - 9.5, bw, 3.2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(242, 246, 252, 52));
    p.drawRoundedRect(bTrack, 1.6, 1.6);

    if (!g.allDim && g.percent > 0) {
        p.setBrush(g.fillColor);
        p.drawRoundedRect(QRectF(bTrack.left(), bTrack.top(), bw * g.percent / 100.0, 3.2), 1.6, 1.6);
    }
}

QString buildTooltip(const SystemStatusSnapshot &st, const FaceConfig &cfg)
{
    QStringList tips;
    tips.append(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    appendMetricTooltip(tips, resolveTopMetric(st, cfg.topArc), st);
    appendMetricTooltip(tips, resolveBottomMetric(st, cfg.bottomDots), st);
    appendMetricTooltip(tips, resolveLeftMetric(st, cfg.leftArc), st);
    appendMetricTooltip(tips, resolveRightMetric(st, cfg.rightArc), st);
    if (cfg.centerMode == CenterMode::CpuMemory) {
        appendMetricTooltip(tips, MetricSlot::Cpu, st);
        appendMetricTooltip(tips, MetricSlot::Memory, st);
    }
    if (st.inputPresent && !st.inputDescription.isEmpty())
        tips.append(QStringLiteral("输入法 %1").arg(st.inputDescription));
    if (cfg.showPrivacyDots && st.micActive)
        tips.append(QStringLiteral("麦克风：%1").arg(st.micUsers.join(QStringLiteral("、"))));
    if (cfg.showPrivacyDots && st.cameraActive)
        tips.append(QStringLiteral("摄像头：%1").arg(st.cameraUsers.join(QStringLiteral("、"))));
    if (cfg.showRadarDots && st.hasNotificationDot())
        tips.append(st.lastNotificationSummary.isEmpty() ? QStringLiteral("有未读通知") : st.lastNotificationSummary);
    return tips.join(QStringLiteral(" · "));
}

} // namespace StatusOrbRenderer
