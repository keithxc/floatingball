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
#include <functional>

// Finger-locked 1:1 touch/drag vertical scrolling with smooth iOS-style kinetic
// momentum AND axis-locked horizontal swipe detection (e.g. swipe left/right to
// switch AppLauncher categories, or swipe right in a subpage to navigate back).
// Works cleanly on Wayland touchscreens (via Qt's synthesized mouse events using
// window-local scenePosition()) as well as direct mouse/trackpad drag, while
// preventing accidental child button clicks whenever a drag/swipe occurs.
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
    // so starting a finger swipe directly on top of a button still scrolls/swipes smoothly.
    void attachWidget(QWidget *child)
    {
        if (!child) return;
        child->installEventFilter(this);
    }

    // Optional horizontal swipe callback:
    //   delta = +1 -> finger swiped RIGHT-to-LEFT (dx < 0, advance to next tab/category)
    //   delta = -1 -> finger swiped LEFT-to-RIGHT (dx > 0, go to previous tab / navigate back)
    std::function<void(int delta)> onHorizontalSwipe;

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

        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(ev);
            if (me->button() != Qt::LeftButton) return false;
            stopMomentum();
            m_tracking = true;
            m_axis = AxisNone;
            m_pressPos = me->scenePosition();
            m_lastPos = m_pressPos;
            m_startScroll = sb ? sb->value() : 0;
            m_velocityY = 0.0;
            m_velocityX = 0.0;
            m_sampleTimer.restart();
            return false;
        }
        case QEvent::MouseMove: {
            if (!m_tracking) return false;
            auto *me = static_cast<QMouseEvent *>(ev);
            if (!(me->buttons() & Qt::LeftButton)) {
                m_tracking = false;
                m_axis = AxisNone;
                return false;
            }
            const QPointF cur = me->scenePosition();
            const qreal totalDx = cur.x() - m_pressPos.x();
            const qreal totalDy = cur.y() - m_pressPos.y();

            if (m_axis == AxisNone) {
                const qreal absX = std::abs(totalDx);
                const qreal absY = std::abs(totalDy);
                if (onHorizontalSwipe && absX >= 12.0 && absX > absY * 1.25) {
                    m_axis = AxisHorizontal;
                    if (auto *btn = qobject_cast<QAbstractButton *>(watched))
                        btn->setDown(false);
                    m_lastPos = cur;
                    m_sampleTimer.restart();
                } else if (sb && absY >= 9.0) {
                    m_axis = AxisVertical;
                    if (auto *btn = qobject_cast<QAbstractButton *>(watched))
                        btn->setDown(false);
                    m_lastPos = cur;
                    m_sampleTimer.restart();
                }
            }

            if (m_axis == AxisHorizontal) {
                const qreal dt = qMax<qreal>(1.0, m_sampleTimer.elapsed());
                const qreal stepDx = cur.x() - m_lastPos.x();
                m_velocityX = m_velocityX * 0.45 + (stepDx / dt) * 0.55;
                m_lastPos = cur;
                m_sampleTimer.restart();
                return true;
            }

            if (m_axis == AxisVertical && sb) {
                const qreal dt = qMax<qreal>(1.0, m_sampleTimer.elapsed());
                const qreal stepDy = cur.y() - m_lastPos.y();
                const qreal instantV = -stepDy / dt; // positive = scroll down
                m_velocityY = m_velocityY * 0.45 + instantV * 0.55;
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
            const Axis finishedAxis = m_axis;
            const qreal totalDx = me->scenePosition().x() - m_pressPos.x();
            m_tracking = false;
            m_axis = AxisNone;

            if (finishedAxis == AxisHorizontal) {
                if (auto *btn = qobject_cast<QAbstractButton *>(watched))
                    btn->setDown(false);
                const bool fastFlick = m_sampleTimer.isValid() && m_sampleTimer.elapsed() < 110 &&
                                       std::abs(totalDx) >= 24.0 && std::abs(m_velocityX) >= 0.30;
                if (onHorizontalSwipe && (std::abs(totalDx) >= 48.0 || fastFlick)) {
                    // Swipe left (dx < 0) -> next (+1); Swipe right (dx > 0) -> prev (-1)
                    onHorizontalSwipe(totalDx < 0.0 ? 1 : -1);
                }
                return true;
            }

            if (finishedAxis == AxisVertical) {
                if (auto *btn = qobject_cast<QAbstractButton *>(watched))
                    btn->setDown(false);
                if (m_sampleTimer.isValid() && m_sampleTimer.elapsed() < 90 &&
                    std::abs(m_velocityY) > 0.14) {
                    startMomentum(m_velocityY);
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
    enum Axis {
        AxisNone = 0,
        AxisVertical,
        AxisHorizontal
    };

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
    qreal m_velocityY = 0.0;
    qreal m_velocityX = 0.0;
    bool m_tracking = false;
    Axis m_axis = AxisNone;
};
