#pragma once

#include <QAbstractButton>
#include <QEasingCurve>
#include <QElapsedTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QVariantAnimation>
#include <QWidget>
#include <cmath>

// Finger-locked 1:1 touch/drag vertical scrolling with smooth iOS-style kinetic
// momentum for QScrollArea containers packed with clickable child buttons
// (e.g. AppLauncherPopover icon grid and SystemPopover Wi-Fi/BT/Mihomo lists).
// Works cleanly on Wayland touchscreens (via Qt's synthesized mouse events) as
// well as direct mouse/trackpad drag, while preventing accidental button clicks
// whenever the pointer moves beyond the drag threshold.
class TouchScrollController final : public QObject
{
public:
    explicit TouchScrollController(QScrollArea *area)
        : QObject(area), m_area(area)
    {
        if (!area) return;
        if (QWidget *vp = area->viewport()) {
            vp->installEventFilter(this);
        }
        if (QWidget *w = area->widget()) {
            w->installEventFilter(this);
        }
    }

    // Call on every dynamically added interactive child (e.g. AppGridTile, rowBtn)
    // so starting a finger swipe directly on top of a button still scrolls smoothly.
    void attachWidget(QWidget *child)
    {
        if (!child) return;
        child->installEventFilter(this);
    }

    static TouchScrollController *install(QScrollArea *area)
    {
        if (!area) return nullptr;
        if (auto *existing = static_cast<TouchScrollController *>(
                area->findChild<QObject *>(QStringLiteral("floatingballTouchScroll")))) {
            return existing;
        }
        auto *ctrl = new TouchScrollController(area);
        ctrl->setObjectName(QStringLiteral("floatingballTouchScroll"));
        return ctrl;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *ev) override
    {
        if (!m_area) return false;
        QScrollBar *sb = m_area->verticalScrollBar();
        if (!sb) return false;

        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(ev);
            if (me->button() != Qt::LeftButton) return false;
            stopMomentum();
            m_tracking = true;
            m_dragging = false;
            m_pressPos = me->scenePosition();
            m_lastPos = m_pressPos;
            m_startScroll = sb->value();
            m_velocityPxPerMs = 0.0;
            m_sampleTimer.restart();
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_tracking) return false;
            auto *me = static_cast<QMouseEvent *>(ev);
            if (!(me->buttons() & Qt::LeftButton)) {
                m_tracking = false;
                m_dragging = false;
                return false;
            }
            const QPointF cur = me->scenePosition();
            const qreal totalDy = cur.y() - m_pressPos.y();
            if (!m_dragging && std::abs(totalDy) >= 9.0) {
                m_dragging = true;
                if (auto *btn = qobject_cast<QAbstractButton *>(watched)) {
                    btn->setDown(false);
                }
                m_lastPos = cur;
                m_sampleTimer.restart();
            }
            if (m_dragging) {
                const qreal dt = qMax<qreal>(1.0, m_sampleTimer.elapsed());
                const qreal stepDy = cur.y() - m_lastPos.y();
                const qreal instantV = -stepDy / dt; // positive = scroll down
                m_velocityPxPerMs = m_velocityPxPerMs * 0.45 + instantV * 0.55;
                m_lastPos = cur;
                m_sampleTimer.restart();

                const int target = qBound(sb->minimum(), qRound(m_startScroll - totalDy), sb->maximum());
                sb->setValue(target);
                return true;
            }
            return false;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_tracking) return false;
            auto *me = static_cast<QMouseEvent *>(ev);
            if (me->button() != Qt::LeftButton) return false;
            const bool wasDragging = m_dragging;
            m_tracking = false;
            m_dragging = false;
            if (wasDragging) {
                if (auto *btn = qobject_cast<QAbstractButton *>(watched)) {
                    btn->setDown(false);
                }
                // Only launch momentum if finger was still moving recently (< 90ms ago)
                if (m_sampleTimer.isValid() && m_sampleTimer.elapsed() < 90 &&
                    std::abs(m_velocityPxPerMs) > 0.14) {
                    startMomentum(m_velocityPxPerMs);
                }
                return true; // Swallow release so button never misfires clicked()
            }
            return false;
        }
        default:
            return false;
        }
    }

private:
    void stopMomentum()
    {
        if (m_momentumAnim) {
            m_momentumAnim->stop();
        }
    }

    void startMomentum(qreal pxPerMs)
    {
        if (!m_area) return;
        QScrollBar *sb = m_area->verticalScrollBar();
        if (!sb || sb->maximum() <= sb->minimum()) return;

        const qreal clampedV = qBound(-3.2, pxPerMs, 3.2);
        const int delta = qRound(clampedV * 240.0);
        if (std::abs(delta) < 12) return;

        const int startVal = sb->value();
        const int endVal = qBound(sb->minimum(), startVal + delta, sb->maximum());
        if (endVal == startVal) return;

        if (!m_momentumAnim) {
            m_momentumAnim = new QVariantAnimation(this);
            m_momentumAnim->setEasingCurve(QEasingCurve::OutCubic);
            connect(m_momentumAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
                if (m_area && m_area->verticalScrollBar()) {
                    m_area->verticalScrollBar()->setValue(v.toInt());
                }
            });
        }
        m_momentumAnim->stop();
        m_momentumAnim->setStartValue(startVal);
        m_momentumAnim->setEndValue(endVal);
        m_momentumAnim->setDuration(qBound(180, qRound(std::abs(endVal - startVal) * 0.9), 480));
        m_momentumAnim->start();
    }

    QPointer<QScrollArea> m_area;
    QVariantAnimation *m_momentumAnim = nullptr;
    QElapsedTimer m_sampleTimer;
    QPointF m_pressPos;
    QPointF m_lastPos;
    int m_startScroll = 0;
    qreal m_velocityPxPerMs = 0.0;
    bool m_tracking = false;
    bool m_dragging = false;
};
