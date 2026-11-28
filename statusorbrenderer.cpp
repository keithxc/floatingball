#include "statusorbrenderer.h"
#include "systemstatus.h"

#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QTime>
#include <QtMath>

namespace StatusOrbRenderer {

MetricSlot resolveTopMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured == MetricSlot::Battery)
        return st.batteryPresent ? MetricSlot::Battery : MetricSlot::None;
    if (configured == MetricSlot::Memory)
        return st.memoryAvailable ? MetricSlot::Memory : MetricSlot::None;
    if (st.batteryPresent) return MetricSlot::Battery;
    if (st.memoryAvailable) return MetricSlot::Memory;
    return MetricSlot::None;
}

MetricSlot resolveLeftMetric(const SystemStatusSnapshot &st, MetricSlot configured)
{
    if (configured == MetricSlot::None) return MetricSlot::None;
    if (configured == MetricSlot::Brightness)
        return st.brightnessAvailable ? MetricSlot::Brightness : MetricSlot::None;
    if (configured == MetricSlot::Cpu)
        return st.cpuAvailable ? MetricSlot::Cpu : MetricSlot::None;
    if (st.brightnessAvailable) return MetricSlot::Brightness;
    if (st.cpuAvailable) return MetricSlot::Cpu;
    return MetricSlot::None;
}

QColor memoryGaugeColor(int percent)
{
    if (percent >= 88) return QColor("#ef5350");
    if (percent >= 75) return QColor("#f4c542");
    return QColor("#dbeafe");
}

QColor cpuGaugeColor(int percent)
{
    if (percent >= 85) return QColor("#fb7185");
    if (percent >= 60) return QColor("#fbbf24");
    return QColor("#bae6fd");
}

void paintFace(QPainter &p, const QPointF &sphereCenter, qreal d, qreal radius,
               const QFont &baseFont, const SystemStatusSnapshot &st,
               const QString &hh, const QString &mm, const QString &imeFlash,
               const FaceConfig &cfg)
{
    const QColor ink(QStringLiteral("#f2f6fc"));
    const QColor dimInk(242, 246, 252, 52);

    const qreal rOuter = radius * 0.84;
    const qreal rInner = radius * 0.65;
    const QRectF outerArcRect(sphereCenter.x() - rOuter, sphereCenter.y() - rOuter, rOuter * 2.0, rOuter * 2.0);
    const QRectF innerArcRect(sphereCenter.x() - rInner, sphereCenter.y() - rInner, rInner * 2.0, rInner * 2.0);

    // 1. Left outer continuous arc: Screen Brightness (laptop) or Smoothed CPU Load (desktop)
    const MetricSlot leftSlot = resolveLeftMetric(st, cfg.leftArc);
    if (leftSlot == MetricSlot::Brightness || leftSlot == MetricSlot::Cpu) {
        const qreal trackW = qMax(1.6, d * 0.036);
        const int pct = (leftSlot == MetricSlot::Brightness)
            ? qBound(0, st.brightnessPercent, 100)
            : qBound(0, st.cpuPercent, 100);
        const QColor fillColor = (leftSlot == MetricSlot::Brightness) ? ink : cpuGaugeColor(pct);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(dimInk, trackW, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(outerArcRect, qRound(220.0 * 16), qRound(-80.0 * 16));
        if (pct > 0) {
            const qreal effPct = (leftSlot == MetricSlot::Cpu) ? qMax(4.0, static_cast<qreal>(pct)) : static_cast<qreal>(pct);
            p.setPen(QPen(fillColor, trackW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(outerArcRect, qRound(220.0 * 16), qRound(-80.0 * effPct / 100.0 * 16));
        }
    }

    // 2. Right outer 13-dot arc: Playback Volume (bottom -> top, all dim when muted)
    if (cfg.rightArc != MetricSlot::None && st.volumeAvailable) {
        const int litDots = st.volumeMuted
            ? 0
            : static_cast<int>(qCeil(qBound(0, st.volumePercent, 100) * 13.0 / 100.0));
        const qreal dotR = qMax(0.95, d * 0.019);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 13; ++i) {
            const qreal deg = 40.0 - 80.0 * i / 12.0;
            const qreal rad = qDegreesToRadians(deg);
            const QPointF pt(sphereCenter.x() + rOuter * qCos(rad),
                             sphereCenter.y() + rOuter * qSin(rad));
            p.setBrush(i < litDots ? ink : dimInk);
            p.drawEllipse(pt, dotR, dotR);
        }
    }

    // 3. Top 250-deg inner arc: Battery (laptop) or Unified Memory / RAM (desktop)
    const MetricSlot topSlot = resolveTopMetric(st, cfg.topArc);
    if (topSlot == MetricSlot::Battery || topSlot == MetricSlot::Memory) {
        const qreal arcW = qMax(2.0, d * 0.046);
        const int pct = (topSlot == MetricSlot::Battery)
            ? qBound(0, st.batteryPercent, 100)
            : qBound(0, st.memoryPercent, 100);
        const QColor fillColor = (topSlot == MetricSlot::Battery)
            ? (st.batteryPluggedIn ? QColor("#6edba0")
               : (pct < 15 ? QColor("#ef5350")
                  : (pct < 25 ? QColor("#f4c542") : ink)))
            : memoryGaugeColor(pct);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(dimInk, arcW, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(innerArcRect, qRound(215.0 * 16), qRound(-250.0 * 16));
        if (pct > 0) {
            p.setPen(QPen(fillColor, arcW, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(innerArcRect, qRound(215.0 * 16), qRound(-250.0 * pct / 100.0 * 16));
        }
    }

    // 4. Bottom 5 signal dots: Network (offline = 5 red dots)
    if (cfg.bottomDots != MetricSlot::None) {
        const bool offline = !st.networkConnected;
        const int litSignal = offline ? 5 : qBound(0, static_cast<int>(qCeil(st.signalQuality / 20.0)), 5);
        const QColor sigColor = offline ? QColor("#ef5350") : ink;
        const qreal sigR = qMax(1.15, d * 0.022);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 5; ++i) {
            const qreal deg = 130.0 - i * 20.0;
            const qreal rad = qDegreesToRadians(deg);
            const QPointF pt(sphereCenter.x() + rInner * qCos(rad),
                             sphereCenter.y() + rInner * qSin(rad));
            p.setBrush(i < litSignal ? sigColor : dimInk);
            p.drawEllipse(pt, sigR, sigR);
        }
    }

    // 5. Center Emblem: 2-line 24H clock ("HH"/"mm") or transient IME flash ("双"/"EN")
    if (!imeFlash.isEmpty()) {
        const qreal badgeW = rInner * 1.38;
        const qreal badgeH = rInner * 1.12;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(56, 189, 248, 52));
        p.drawRoundedRect(QRectF(sphereCenter.x() - badgeW * 0.5, sphereCenter.y() - badgeH * 0.5,
                                 badgeW, badgeH), 7.0, 7.0);
        QFont f = baseFont;
        f.setPixelSize(qRound(imeFlash == QStringLiteral("双") ? d * 0.28 : d * 0.23));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        p.setPen(QColor("#7dd3fc"));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - rInner - 0.5,
                          rInner * 2.0, rInner * 2.0),
                   Qt::AlignCenter, imeFlash);
    } else {
        QFont f = baseFont;
        f.setPixelSize(qMax(10, qRound(d * 0.215)));
        f.setWeight(QFont::Bold);
        p.setFont(f);
        const qreal lineH = d * 0.235;
        const qreal vShift = d * 0.102;
        p.setPen(QColor("#f8fafc"));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() - vShift - lineH * 0.5,
                          rInner * 2.0, lineH),
                   Qt::AlignCenter, hh);
        p.setPen(QColor("#cbd5e1"));
        p.drawText(QRectF(sphereCenter.x() - rInner, sphereCenter.y() + vShift - lineH * 0.5,
                          rInner * 2.0, lineH),
                   Qt::AlignCenter, mm);
    }

    // 6. Privacy indicator micro-dots (Top-Right = Mic, Bottom-Right = Camera)
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

void paintVerticalCapsuleBar(QPainter &p, const QRectF &capRect,
                             const SystemStatusSnapshot &st,
                             const FaceConfig &cfg)
{
    const MetricSlot topSlot = resolveTopMetric(st, cfg.topArc);
    if (topSlot != MetricSlot::Battery && topSlot != MetricSlot::Memory) return;

    const QPointF c = capRect.center();
    const qreal bw = capRect.width() * 0.54;
    const QRectF bTrack(c.x() - bw * 0.5, capRect.bottom() - 9.5, bw, 3.2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(242, 246, 252, 52));
    p.drawRoundedRect(bTrack, 1.6, 1.6);

    const int pct = (topSlot == MetricSlot::Battery)
        ? qBound(0, st.batteryPercent, 100)
        : qBound(0, st.memoryPercent, 100);
    const QColor bCol = (topSlot == MetricSlot::Battery)
        ? (st.batteryPluggedIn ? QColor("#6edba0")
           : (pct < 15 ? QColor("#ef5350")
              : (pct < 25 ? QColor("#f4c542") : QColor("#dce8f8"))))
        : memoryGaugeColor(pct);
    p.setBrush(bCol);
    p.drawRoundedRect(QRectF(bTrack.left(), bTrack.top(), bw * pct / 100.0, 3.2), 1.6, 1.6);
}

QString buildTooltip(const SystemStatusSnapshot &st, const FaceConfig &cfg)
{
    QStringList tips;
    tips.append(QTime::currentTime().toString(QStringLiteral("HH:mm")));
    const MetricSlot topSlot = resolveTopMetric(st, cfg.topArc);
    if (topSlot == MetricSlot::Battery) {
        tips.append(QStringLiteral("电量 %1%%2")
                        .arg(st.batteryPercent)
                        .arg(st.batteryPluggedIn ? QStringLiteral("（充电中）") : QString()));
    } else if (topSlot == MetricSlot::Memory) {
        if (st.memoryTotalGiB > 0.1) {
            tips.append(QStringLiteral("内存 %1% (%2/%3 GB)")
                            .arg(st.memoryPercent)
                            .arg(st.memoryUsedGiB, 0, 'f', 1)
                            .arg(st.memoryTotalGiB, 0, 'f', 0));
        } else {
            tips.append(QStringLiteral("内存 %1%").arg(st.memoryPercent));
        }
    }
    if (!st.networkText.isEmpty()) tips.append(st.networkText);
    const MetricSlot leftSlot = resolveLeftMetric(st, cfg.leftArc);
    if (leftSlot == MetricSlot::Brightness) {
        tips.append(QStringLiteral("亮度 %1%").arg(st.brightnessPercent));
    } else if (leftSlot == MetricSlot::Cpu) {
        tips.append(QStringLiteral("CPU %1%").arg(st.cpuPercent));
    }
    if (st.volumeAvailable)
        tips.append(st.volumeMuted ? QStringLiteral("音量 静音") : QStringLiteral("音量 %1%").arg(st.volumePercent));
    if (st.inputPresent && !st.inputDescription.isEmpty())
        tips.append(QStringLiteral("输入法 %1").arg(st.inputDescription));
    if (st.micActive) tips.append(QStringLiteral("麦克风：%1").arg(st.micUsers.join(QStringLiteral("、"))));
    if (st.cameraActive) tips.append(QStringLiteral("摄像头：%1").arg(st.cameraUsers.join(QStringLiteral("、"))));
    if (st.hasNotificationDot())
        tips.append(st.lastNotificationSummary.isEmpty() ? QStringLiteral("有未读通知") : st.lastNotificationSummary);
    return tips.join(QStringLiteral(" · "));
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
        if (st.cpuAvailable) parts.append(QStringLiteral("CPU %1%").arg(st.cpuPercent));
        if (st.memoryAvailable) parts.append(QStringLiteral("RAM %1%").arg(st.memoryPercent));
        QString tip = QStringLiteral("点击打开活动监视器");
        if (st.memoryAvailable && st.memoryTotalGiB > 0.1) {
            tip = QStringLiteral("内存 %1 / %2 GB · 点击打开活动监视器")
                      .arg(st.memoryUsedGiB, 0, 'f', 1)
                      .arg(st.memoryTotalGiB, 0, 'f', 0);
        }
        const bool hot = (st.memoryAvailable && st.memoryPercent >= 88) ||
                         (st.cpuAvailable && st.cpuPercent >= 85);
        return {
            parts.join(QStringLiteral(" · ")),
            tip,
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
