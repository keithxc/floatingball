#include "floatingball.h"
#include "screenshotpanel.h"
#include "floatingwindow.h"
#include "uiicons.h"
#include <QApplication>
#include <QTimer>
#include <QScreen>
#include "./ui_floatingball.h"

#include <QDebug>
#include <QEnterEvent>
#include <QFont>
#include <QGestureEvent>
#include <QLineF>
#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QSignalBlocker>
#include <QMenu>
#include <QPinchGesture>
#include <QProcess>
#include <QRegion>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QTouchEvent>
#include <QTapAndHoldGesture>
#include <QWheelEvent>
#include <QtMath>

FloatingBall::FloatingBall(QWidget *parent, const QString &screenshotDirectory)
    : QWidget(parent), ui(new Ui::FloatingBall), m_screenshotDirectory(screenshotDirectory)
{
    ui->setupUi(this);

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    // Cocoa otherwise hides tool windows whenever another app becomes active.
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_AcceptTouchEvents);
    setMouseTracking(true);

    ballLabel = new QLabel(this);
    ballLabel->setAlignment(Qt::AlignCenter);
    QFont font = ballLabel->font();
    font.setPointSize(10);
    font.setBold(false);
    ballLabel->setFont(font);
    ballLabel->setStyleSheet("color:#514b5f; background:transparent;");
    setAccessibleName(QStringLiteral("FloatingBall"));
    setToolTip(tr("点击展开快捷操作 · 拖动移动"));
    ballLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
    m_geometryAnimation->setDuration(140);
    m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);

    updateBallGeometry(QSizeF(64, 64), QPointF(), false, false);
    updateBallStyle("#faf9fc");

    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::TapAndHoldGesture);
    initQuickActions();
    FloatingWindow::prepare(this);
}

FloatingBall::~FloatingBall()
{
    if (m_orbitAnimation) m_orbitAnimation->stop();
    if (m_geometryAnimation &&
        m_geometryAnimation->state() == QAbstractAnimation::Running) {
        m_geometryAnimation->stop();
    }

    for (FloatingBall *ball : qAsConst(m_orbitBalls)) {
        if (ball) {
            delete ball;
        }
    }
    m_orbitBalls.clear();
    for (QShortcut *shortcut : qAsConst(m_shortcutObjects)) {
        if (shortcut) {
            shortcut->deleteLater();
        }
    }
    m_shortcutObjects.clear();
    delete m_screenshotPanel;
    delete ui;
}

bool FloatingBall::event(QEvent *event)
{
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
        || event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        return handleTouchEvent(static_cast<QTouchEvent *>(event));
    }
    if (event->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent *>(event));
    }
    return QWidget::event(event);
}

void FloatingBall::updateBallGeometry(const QSizeF &size, const QPointF &anchorGlobalPos, bool keepAnchor, bool animate)
{
    const int diameter = qBound(kMinDiameter, qRound(qMin(size.width(), size.height())), kMaxDiameter);
    const QSize targetSize(diameter, diameter);

    QPoint targetPos = pos();
    if (keepAnchor) {
        targetPos = QPoint(qRound(anchorGlobalPos.x()) - targetSize.width() / 2,
                           qRound(anchorGlobalPos.y()) - targetSize.height() / 2);
    } else {
        QPoint center = pos() + QPoint(width() / 2, height() / 2);
        targetPos = QPoint(center.x() - targetSize.width() / 2,
                           center.y() - targetSize.height() / 2);
    }

    const QRect targetGeometry(targetPos, targetSize);
    applyGeometry(targetGeometry, animate);
}

void FloatingBall::startGeometryAnimation(const QRect &targetGeometry)
{
    if (!m_geometryAnimation) {
        m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
        m_geometryAnimation->setDuration(140);
        m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
    }
    if (m_geometryAnimation->state() == QAbstractAnimation::Running) {
        m_geometryAnimation->stop();
    }
    m_geometryAnimation->setStartValue(geometry());
    m_geometryAnimation->setEndValue(targetGeometry);
    m_geometryAnimation->start();
}

void FloatingBall::applyGeometry(const QRect &targetGeometry, bool animate)
{
    if (targetGeometry.isEmpty()) {
        return;
    }
    if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running) {
        m_geometryAnimation->stop();
    }

    if (animate && targetGeometry != geometry()) {
        startGeometryAnimation(targetGeometry);
        return;
    }

    setGeometry(targetGeometry);
    setMask(QRegion(rect(), QRegion::Ellipse));
    if (ballLabel) {
        ballLabel->setGeometry(6, height()/2+3, width()-12, height()/3);
    }
}

void FloatingBall::updateBallStyle(const QString &colorName)
{
    m_ballColor = QColor(colorName);
    update();
}

void FloatingBall::resetTouchState()
{
    m_dragTouchId = -1;
    m_dragOffset = QPointF();
    m_isDragging = false;
    m_isPinching = false;
    m_initialPinchDistance = 0.0;
    m_initialPinchSize = QSizeF();
}

bool FloatingBall::handleTouchEvent(QTouchEvent *event)
{
    const auto points = event->points();

    if (event->type() == QEvent::TouchBegin) {
        m_orbitAnimating = false;
        if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running) {
            m_geometryAnimation->stop();
        }
        if (points.size() >= 2) {
            m_isPinching = true;
            m_isDragging = false;
            m_dragTouchId = -1;
            m_initialPinchDistance = QLineF(points.at(0).globalPosition(), points.at(1).globalPosition()).length();
            m_initialPinchSize = QSizeF(size());
            return true;
        }

        if (points.size() == 1) {
            m_isPinching = false;
            m_isDragging = true;
            m_dragTouchId = points.first().id();
            m_dragOffset = points.first().globalPosition() - pos();
            m_pressPosition = points.first().globalPosition().toPoint();
            m_moved = false;
            return true;
        }
    }

    if (event->type() == QEvent::TouchUpdate) {
        if (points.size() >= 2 && m_isPinching && m_initialPinchDistance > 0) {
            const qreal distance = QLineF(points.at(0).globalPosition(), points.at(1).globalPosition()).length();
            if (distance > 0) {
                const qreal scale = distance / m_initialPinchDistance;
                const QPointF anchor = (points.at(0).globalPosition() + points.at(1).globalPosition()) / 2;
                updateBallGeometry(m_initialPinchSize * scale, anchor, true, false);
            }
            return true;
        }

        if (points.size() == 1 && (m_isDragging || m_dragTouchId == points.first().id())) {
            const auto &point = points.first();
            if (m_dragTouchId < 0) {
                m_dragTouchId = point.id();
            }
            if (point.id() == m_dragTouchId) {
                if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running) {
                    m_geometryAnimation->stop();
                }
                if ((point.globalPosition().toPoint() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance()) m_moved = true;
                if (m_moved && !m_clickAction) move((point.globalPosition() - m_dragOffset).toPoint());
            }
            return true;
        }

        if (points.size() == 1 && !m_isDragging && !m_isPinching) {
            m_isDragging = true;
            m_dragTouchId = points.first().id();
            m_dragOffset = points.first().globalPosition() - pos();
            return true;
        }
    }

    if (event->type() == QEvent::TouchEnd && m_isDragging && !m_isPinching && !m_moved) {
        if (m_clickAction) m_clickAction();
        else toggleOrbit();
    }
    resetTouchState();
    return true;
}

bool FloatingBall::gestureEvent(QGestureEvent *event)
{
    QGesture *rawPinch = event->gesture(Qt::PinchGesture);
    if (QPinchGesture *pinch = qobject_cast<QPinchGesture *>(rawPinch)) {
        if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
            qreal factor = pinch->totalScaleFactor();
            if (factor > 0) {
                updateBallGeometry(QSizeF(size()) * factor, QPointF(), false, false);
            }
        }
    }

    if (QTapAndHoldGesture *tapHold = qobject_cast<QTapAndHoldGesture *>(event->gesture(Qt::TapAndHoldGesture))) {
        if (tapHold->state() == Qt::GestureFinished) {
            m_moved = true;
            showQuickActions(mapToGlobal(tapHold->position().toPoint()));
        }
    }
    return true;
}

void FloatingBall::initQuickActions()
{
    m_shortcutActions.clear();
    m_quickActions.clear();
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

#if defined(Q_OS_WIN)
    m_shortcutActions.append({tr("打开文件资源管理器"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), QStringLiteral("explorer")});
    m_quickActions.append({tr("打开终端"), QStringLiteral("cmd"), {"/c", "start", "cmd"}});
    m_quickActions.append({tr("打开浏览器"), QString(), {}, QUrl(QStringLiteral("https://www.bing.com"))});
    m_quickActions.append({tr("打开主目录"), QString(), {}, QUrl::fromLocalFile(home)});
    m_quickActions.append({tr("打开任务管理器"), QStringLiteral("taskmgr"), {}});
#elif defined(Q_OS_MAC)
    m_shortcutActions.append({tr("切换输入法（Ctrl + Space）"), QKeySequence(Qt::CTRL + Qt::Key_Space),
                              QStringLiteral("osascript"),
                              {QStringLiteral("-e"), QStringLiteral("tell application \"System Events\" to key code 49 using control down")}});
    m_shortcutActions.append({tr("切换输入法（Command + Space）"), QKeySequence(Qt::META + Qt::Key_Space),
                              QStringLiteral("osascript"),
                              {QStringLiteral("-e"), QStringLiteral("tell application \"System Events\" to key code 49 using command down")}});
    m_shortcutActions.append({tr("复制"), QKeySequence(Qt::META + Qt::Key_C),
                              QStringLiteral("osascript"),
                              {QStringLiteral("-e"), QStringLiteral("tell application \"System Events\" to keystroke \"c\" using command down")}});
    m_shortcutActions.append({tr("粘贴"), QKeySequence(Qt::META + Qt::Key_V),
                              QStringLiteral("osascript"),
                              {QStringLiteral("-e"), QStringLiteral("tell application \"System Events\" to keystroke \"v\" using command down")}});
    m_quickActions.append({tr("打开终端"), QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal")}});
    m_quickActions.append({tr("打开浏览器"), QString(), {}, QUrl(QStringLiteral("https://www.baidu.com"))});
    m_quickActions.append({tr("打开主目录"), QString(), {}, QUrl::fromLocalFile(home)});
#else
    m_shortcutActions.append({tr("切换输入法（Ctrl + Space）"), QKeySequence(Qt::CTRL + Qt::Key_Space), QString(), {}});
    m_shortcutActions.append({tr("复制"), QKeySequence(Qt::CTRL + Qt::Key_C), QString(), {}});
    m_shortcutActions.append({tr("粘贴"), QKeySequence(Qt::CTRL + Qt::Key_V), QString(), {}});
    m_quickActions.append({tr("打开终端"), QStringLiteral("x-terminal-emulator"), {}});
    m_quickActions.append({tr("打开浏览器"), QString(), {}, QUrl(QStringLiteral("https://www.bing.com"))});
    m_quickActions.append({tr("打开主目录"), QString(), {}, QUrl::fromLocalFile(home)});
#endif

    for (QShortcut *shortcut : qAsConst(m_shortcutObjects)) {
        if (shortcut) {
            shortcut->deleteLater();
        }
    }
    m_shortcutObjects.clear();

    for (int i = 0; i < m_shortcutActions.size(); ++i) {
        const auto &cfg = m_shortcutActions[i];
        if (cfg.trigger.isEmpty()) {
            continue;
        }
        QShortcut *shortcut = new QShortcut(cfg.trigger, this);
        shortcut->setContext(Qt::ApplicationShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i]() {
            executeKeyboardShortcut(i);
        });
        m_shortcutObjects.append(shortcut);
    }
}

void FloatingBall::showQuickActions(const QPoint &globalPos)
{
    QMenu menu;
    auto screenshots = menu.addAction(tr("最近截图"));
    connect(screenshots, &QAction::triggered, this, [this]() {
        showScreenshots();
    });
    menu.addSeparator();
    for (int i = 0; i < m_quickActions.size(); ++i) {
        QAction *action = menu.addAction(m_quickActions[i].name);
        connect(action, &QAction::triggered, this, [this, i]() {
            executeQuickAction(i);
        });
    }
    menu.addSeparator();
    QAction *title = menu.addAction(tr("快捷键触发动作"));
    title->setEnabled(false);
    for (int i = 0; i < m_shortcutActions.size(); ++i) {
        QAction *action = menu.addAction(m_shortcutActions[i].name + QStringLiteral(" (") + m_shortcutActions[i].trigger.toString() + QLatin1Char(')'));
        connect(action, &QAction::triggered, this, [this, i]() {
            executeKeyboardShortcut(i);
        });
    }
    menu.addSeparator();
    QAction *exitAction = menu.addAction(tr("退出悬浮球"));
    connect(exitAction, &QAction::triggered, this, [this]() {
        close();
    });
    menu.exec(globalPos);
}

void FloatingBall::showScreenshots()
{
    if (!m_screenshotPanel) m_screenshotPanel = new ScreenshotPanel(nullptr, m_screenshotDirectory);
    m_screenshotPanel->reveal(geometry().center());
}

void FloatingBall::executeQuickAction(int index)
{
    if (index < 0 || index >= m_quickActions.size()) {
        return;
    }
    const QuickAction &action = m_quickActions[index];
    if (action.openUrl.isValid()) {
        if (QDesktopServices::openUrl(action.openUrl)) {
            return;
        }
    }
    if (!action.command.isEmpty()) {
        if (QProcess::startDetached(action.command, action.arguments)) {
            return;
        }
    }
    qDebug() << "快捷操作启动失败:" << action.name;
}

void FloatingBall::executeKeyboardShortcut(int index)
{
    if (index < 0 || index >= m_shortcutActions.size()) {
        return;
    }
    const KeyboardShortcutAction &action = m_shortcutActions[index];
    if (!action.command.isEmpty()) {
        if (QProcess::startDetached(action.command, action.arguments)) {
            return;
        }
    }
    qDebug() << "快捷键动作启动失败:" << action.name;
}

    // 重写鼠标进入事件
void FloatingBall::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event)
    // A newly shown satellite can appear beneath the cursor. Let it reach
    // its orbit before hover scaling replaces the geometry animation.
    if (m_orbitAnimating) return;
    updateBallStyle("#ffffff");
    if (!m_hoverScaled) {
        m_hoverScaled = true;
        m_hoverBaseSize = QSizeF(size());
        updateBallGeometry(m_hoverBaseSize * 1.06, QPointF(), false, true);
    }
}

// 重写鼠标离开事件
void FloatingBall::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    updateBallStyle(m_actionIndex == 0 ? "#f0ebff" : "#faf9fc");
    if (m_hoverScaled) {
        m_hoverScaled = false;
        updateBallGeometry(m_hoverBaseSize, QPointF(), false, true);
        m_hoverBaseSize = QSizeF();
    }
}

// 重写鼠标按下事件
void FloatingBall::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        showQuickActions(event->globalPosition().toPoint());
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_orbitAnimating = false;
        if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running) {
            m_geometryAnimation->stop();
        }
        m_pressPosition = event->globalPosition().toPoint();
        m_moved = false;
        m_isDragging = true;
        m_dragTouchId = -1;
        m_dragOffset = event->globalPosition() - pos();
        updateBallStyle("#e7e0fa");
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

// 重写鼠标释放事件
void FloatingBall::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!m_moved && !m_doubleClick) {
            if (m_clickAction) m_clickAction();
            else toggleOrbit();
        }
        m_doubleClick = false;
        m_isDragging = false;
        m_dragTouchId = -1;
        unsetCursor();
        updateBallStyle(m_actionIndex == 0 ? "#f0ebff" : "#faf9fc");
    }
    QWidget::mouseReleaseEvent(event);
}

// 重写鼠标移动事件
void FloatingBall::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        if (m_geometryAnimation && m_geometryAnimation->state() == QAbstractAnimation::Running) {
            m_geometryAnimation->stop();
        }
        if ((event->globalPosition().toPoint() - m_pressPosition).manhattanLength() < QApplication::startDragDistance() && !m_moved) return;
        m_moved = true;
        if (!m_clickAction) move(event->globalPosition().toPoint() - m_dragOffset.toPoint());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

// 重写鼠标双击事件
void FloatingBall::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return QWidget::mouseDoubleClickEvent(event);
    }

    m_doubleClick = true;
    // The first release already handled the click. Ignore only the second
    // release, rather than delaying every single click to detect a double click.
}

void FloatingBall::toggleOrbit()
{
    m_orbitVisible = !m_orbitVisible;
    if (!m_orbitAnimation) {
        m_orbitAnimation = new QVariantAnimation(this);
        connect(m_orbitAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_orbitProgress = value.toReal();
            updateOrbitLayout();
        });
        connect(m_orbitAnimation, &QVariantAnimation::finished, this, [this]() {
            for (FloatingBall *ball : qAsConst(m_orbitBalls)) {
                ball->m_orbitAnimating = false;
                if (!m_orbitVisible) { ball->hide(); ball->deleteLater(); }
            }
            if (!m_orbitVisible) m_orbitBalls.clear();
        });
    }
    m_orbitAnimation->stop();

    // Keep the same satellites when reversing an animation in progress.
    if (m_orbitVisible && m_orbitBalls.isEmpty()) for (int i = 0; i < 6; ++i) {
        FloatingBall *aBall = new FloatingBall;
        aBall->setWindowFlags(windowFlags());
        aBall->updateBallGeometry(QSizeF(64, 64), QPointF(), false, false);
        aBall->m_actionIndex = i;
        aBall->updateBallStyle(i == 0 ? "#f0ebff" : "#faf9fc");
        const QStringList labels = {tr("截图"), tr("终端"), tr("浏览器"), tr("主目录"), tr("复制"), tr("粘贴")};
        aBall->ballLabel->setText(labels[i]);
        aBall->setAccessibleName(labels[i]);
        aBall->setToolTip(labels[i]);
        aBall->m_clickAction = [this, i]() {
            if (!m_orbitVisible) return;
            if (i == 0) {
                showScreenshots();
            } else if (i <= 3) executeQuickAction(i - 1);
            else {
                for (int j = 0; j < m_shortcutActions.size(); ++j)
                    if (m_shortcutActions[j].name == (i == 4 ? tr("复制") : tr("粘贴"))) executeKeyboardShortcut(j);
            }
            toggleOrbit();
        };
        m_orbitBalls.append(aBall);
    }

    updateOrbitLayout();
    for (FloatingBall *ball : qAsConst(m_orbitBalls)) {
        ball->m_orbitAnimating = true;
        ball->setEnabled(m_orbitVisible);
        ball->show();
    }
    if (m_orbitVisible) raise();
    {
        // Reconfiguring a stopped QVariantAnimation can emit intermediate
        // values at its old time. Preserve the current frame during reversal.
        const QSignalBlocker blocker(m_orbitAnimation);
        m_orbitAnimation->setStartValue(m_orbitProgress);
        m_orbitAnimation->setEndValue(m_orbitVisible ? 1.0 : 0.0);
        m_orbitAnimation->setDuration(qMax(80, qRound(qAbs((m_orbitVisible ? 1.0 : 0.0)-m_orbitProgress)*(m_orbitVisible ? 210 : 160))));
        m_orbitAnimation->setEasingCurve(m_orbitVisible ? QEasingCurve::OutBack : QEasingCurve::InCubic);
        m_orbitAnimation->setCurrentTime(0);
    }
    m_orbitAnimation->start();
}

void FloatingBall::updateOrbitLayout()
{
    if (m_orbitBalls.isEmpty()) return;
    const QPointF center = QRectF(geometry()).center();
    QScreen *screen = QGuiApplication::screenAt(center.toPoint());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen->availableGeometry();
    const qreal radius = qMax(100.0, (width()+64)/2.0+24) * m_orbitProgress;
    const int diameter = qMax(14, qRound(64*(0.22+0.78*m_orbitProgress)));
    for (int i = 0; i < m_orbitBalls.size(); ++i) {
        FloatingBall *ball = m_orbitBalls[i];
        ball->m_geometryAnimation->stop();
        ball->m_hoverScaled = false;
        ball->m_hoverBaseSize = QSizeF();
        const qreal angle = qDegreesToRadians(60.0*i);
        const QPointF pos = center + QPointF(qCos(angle)*radius, qSin(angle)*radius) - QPointF(diameter/2.0,diameter/2.0);
        ball->setGeometry(qBound(bounds.left(),qRound(pos.x()),bounds.right()-diameter+1),
                          qBound(bounds.top(),qRound(pos.y()),bounds.bottom()-diameter+1),diameter,diameter);
        ball->ballLabel->setVisible(diameter>=44);
        ball->setWindowOpacity(qBound(0.0,m_orbitProgress*1.5,1.0));
    }
}

// 重写鼠标滚轮事件
void FloatingBall::wheelEvent(QWheelEvent *event)
{
    m_orbitAnimating = false;
    int target = width();
    if (event->angleDelta().y() > 0) {
        target += 12;
    } else {
        target -= 12;
    }
    updateBallGeometry(QSizeF(target, target), QPointF(), false, true);
    event->accept();
}

void FloatingBall::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (ballLabel) {
        ballLabel->setGeometry(6, height()/2+3, width()-12, height()/3);
    }
    setMask(QRegion(rect(), QRegion::Ellipse));
    updateOrbitLayout();
}

void FloatingBall::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    // Update in the input event itself: dragging never queues another tween.
    updateOrbitLayout();
}

void FloatingBall::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF circle = QRectF(rect()).adjusted(5, 5, -5, -5);
    p.setPen(Qt::NoPen);
    for (int i = 4; i > 0; --i) {
        p.setBrush(QColor(44, 35, 65, 5 + (4-i)*2));
        p.drawEllipse(circle.adjusted(-i, -i+2, i, i+2));
    }
    QLinearGradient surface(circle.topLeft(), circle.bottomRight());
    surface.setColorAt(0, m_ballColor.lighter(103));
    surface.setColorAt(1, m_ballColor.darker(103));
    p.setBrush(surface); p.setPen(QPen(QColor(255, 255, 255, 210), 1));
    p.drawEllipse(circle);
    const FloatingIcons::Icon icons[] = {FloatingIcons::Screenshot, FloatingIcons::Terminal,
        FloatingIcons::Browser, FloatingIcons::Folder, FloatingIcons::Copy, FloatingIcons::Paste};
    const qreal iconSize = width() * (m_actionIndex < 0 ? 0.42 : 0.29);
    QRectF iconRect((width()-iconSize)/2, m_actionIndex < 0 ? (height()-iconSize)/2 : height()*0.21, iconSize, iconSize);
    FloatingIcons::draw(p, m_actionIndex < 0 ? FloatingIcons::Orb : icons[m_actionIndex], iconRect,
        QColor(m_actionIndex <= 0 ? "#7c67bc" : "#625a70"));
}
