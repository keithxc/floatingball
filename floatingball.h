#ifndef FLOATINGBALL_H
#define FLOATINGBALL_H

#include <QWidget>
#include <QLabel>
#include <QGestureEvent>
#include <QTouchEvent>
#include <QKeySequence>
#include <QShortcut>
#include <QStringList>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QUrl>
#include <QColor>
#include <functional>
class ScreenshotPanel;

QT_BEGIN_NAMESPACE
namespace Ui {
class FloatingBall;
}
QT_END_NAMESPACE

class FloatingBall : public QWidget
{
    Q_OBJECT

public:
    FloatingBall(QWidget *parent = nullptr, const QString &screenshotDirectory = QString());
    ~FloatingBall();
    void showScreenshots();

protected:
    bool event(QEvent *event) override;
    // 重写鼠标进入事件
    void enterEvent(QEnterEvent *event) override;
    // 重写鼠标离开事件
    void leaveEvent(QEvent *event) override;
    // 重写鼠标按下事件
    void mousePressEvent(QMouseEvent *event) override;
    // 重写鼠标释放事件
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 重写鼠标移动事件
    void mouseMoveEvent(QMouseEvent *event) override;
    // 重写鼠标双击事件
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    // 重写鼠标滚轮事件
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    Ui::FloatingBall *ui;
    QLabel *ballLabel = nullptr;

    void updateBallStyle(const QString &colorName);
    void updateBallGeometry(const QSizeF &size, const QPointF &anchorGlobalPos,
                           bool keepAnchor = false, bool animate = false);
    bool handleTouchEvent(QTouchEvent *event);
    void resetTouchState();
    bool gestureEvent(QGestureEvent *event);
    void initQuickActions();
    void toggleOrbit();
    void updateOrbitLayout();
    ScreenshotPanel *m_screenshotPanel = nullptr;
    QString m_screenshotDirectory;
    std::function<void()> m_clickAction;
    QPoint m_pressPosition;
    bool m_moved = false;
    bool m_doubleClick = false;
    void showQuickActions(const QPoint &globalPos);
    void executeQuickAction(int index);
    void executeKeyboardShortcut(int index);
    void startGeometryAnimation(const QRect &targetGeometry);
    void applyGeometry(const QRect &targetGeometry, bool animate = false);

    struct QuickAction {
        QString name;
        QString command;
        QStringList arguments;
        QUrl openUrl;
    };

    struct KeyboardShortcutAction {
        QString name;
        QKeySequence trigger;
        QString command;
        QStringList arguments;
    };

    QList<QuickAction> m_quickActions;
    QList<KeyboardShortcutAction> m_shortcutActions;
    QList<QShortcut *> m_shortcutObjects;
    QSizeF m_hoverBaseSize;

    int m_dragTouchId = -1;
    QPointF m_dragOffset;
    bool m_isDragging = false;
    bool m_isPinching = false;
    qreal m_initialPinchDistance = 0.0;
    QSizeF m_initialPinchSize;
    QList<FloatingBall *> m_orbitBalls;
    bool m_orbitVisible = false;
    bool m_orbitAnimating = false;
    QPropertyAnimation *m_geometryAnimation = nullptr;
    QVariantAnimation *m_orbitAnimation = nullptr;
    qreal m_orbitProgress = 0.0;
    bool m_hoverScaled = false;
    QColor m_ballColor = QColor("#faf9fc");
    int m_actionIndex = -1;

    static constexpr int kMinDiameter = 48;
    static constexpr int kMaxDiameter = 196;
};
#endif // FLOATINGBALL_H
