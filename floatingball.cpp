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
#include <QScopedValueRollback>
#include <QtMath>

class HoverLabelBubble : public QWidget
{
public:
    explicit HoverLabelBubble(QWidget *owner)
        : QWidget(owner, Qt::ToolTip | Qt::FramelessWindowHint |
                         Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);
        setObjectName(QStringLiteral("floatingballHoverLabel"));
        QFont labelFont = font();
        labelFont.setPointSize(10);
        setFont(labelFont);
        animation = new QVariantAnimation(this);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress = value.toReal();
            move(target + QPoint(0, qRound((1.0 - progress) * 4)));
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, [this]() {
            if (!wanted) hide();
        });
        FloatingWindow::prepare(this, false);
    }

    void reveal(const QString &value, const QRect &anchor)
    {
        text = value;
        setAccessibleName(value);
        resize(fontMetrics().horizontalAdvance(text) + 24, fontMetrics().height() + 16);
        reposition(anchor);
        if (wanted && isVisible()) return;
        wanted = true;
        if (!isVisible()) {
            progress = 0;
            move(target + QPoint(0, 4));
            show();
        }
        animate(1.0, 150);
    }

    void reposition(const QRect &anchor)
    {
        QScreen *screen = QGuiApplication::screenAt(anchor.center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect bounds = screen->availableGeometry();
        int y = anchor.bottom() + 7;
        if (y + height() > bounds.bottom() - 4) y = anchor.top() - height() - 6;
        target = QPoint(qBound(bounds.left() + 4, anchor.center().x() - width() / 2,
                              qMax(bounds.left() + 4, bounds.right() - width() - 3)),
                        qBound(bounds.top() + 4, y,
                              qMax(bounds.top() + 4, bounds.bottom() - height() - 3)));
        move(target + QPoint(0, qRound((1.0 - progress) * 4)));
    }

    void dismiss(bool immediately = false)
    {
        wanted = false;
        if (immediately) { animation->stop(); progress = 0; hide(); }
        else if (isVisible()) animate(0.0, 100);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPixmap face(QSize(qRound(width() * devicePixelRatioF()),
                           qRound(height() * devicePixelRatioF())));
        face.setDevicePixelRatio(devicePixelRatioF());
        face.fill(Qt::transparent);
        QPainter p(&face);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF pill = QRectF(rect()).adjusted(1, 1, -1, -1);
        p.setPen(QPen(QColor(255, 255, 255, 45), 0.7));
        p.setBrush(QColor(33, 35, 41, 205));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        p.setPen(QColor("#f7f7fb"));
        p.setFont(font());
        p.drawText(rect(), Qt::AlignCenter, text);
        p.end();
        QPainter output(this);
        output.setOpacity(progress);
        output.drawPixmap(0, 0, face);
    }

private:
    void animate(qreal end, int duration)
    {
        animation->stop();
        {
            const QSignalBlocker blocker(animation);
            animation->setStartValue(progress);
            animation->setEndValue(end);
            animation->setDuration(duration);
            animation->setEasingCurve(end > progress ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
            animation->setCurrentTime(0);
        }
        animation->start();
    }
    QVariantAnimation *animation = nullptr;
    QString text;
    QPoint target;
    qreal progress = 0;
    bool wanted = false;
};

FloatingBall::FloatingBall(QWidget *parent, const QString &screenshotDirectory)
    : QWidget(parent), ui(new Ui::FloatingBall), m_screenshotDirectory(screenshotDirectory)
{
    ui->setupUi(this);
    setProperty("floatingballSurface",true);

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
    // Retain action text for accessibility; visible labels use a separate bubble.
    ballLabel->setStyleSheet("color:transparent; background:transparent;");
    setAccessibleName(QStringLiteral("FloatingBall"));
    setToolTip(tr("点击展开快捷操作 · 拖动移动"));
    ballLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    ballLabel->hide();

    m_geometryAnimation = new QPropertyAnimation(this, "geometry", this);
    m_geometryAnimation->setDuration(140);
    m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_opacityAnimation = new QVariantAnimation(this);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) {
        m_ballOpacity = value.toReal();
        update();
    });
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(1200);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        animateBallOpacity(0.30, 450);
    });

    updateBallGeometry(QSizeF(64, 64), QPointF(), false, false);
    updateBallStyle("#faf9fc");

    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::TapAndHoldGesture);
    initQuickActions();
    FloatingWindow::prepare(this);
    m_currentScreen = screen();
    for (QScreen *display : QGuiApplication::screens()) watchScreen(display);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen *display) {
        watchScreen(display);
        scheduleScreenAdaptation();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this]() {
        scheduleScreenAdaptation();
    });
    scheduleScreenAdaptation();
}

FloatingBall::~FloatingBall()
{
    m_idleTimer->stop();
    m_opacityAnimation->stop();
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
    if (event->type() == QEvent::Hide && m_hoverLabel) m_hoverLabel->dismiss(true);
    if (event->type() == QEvent::ScreenChangeInternal) scheduleScreenAdaptation();
    if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate
        || event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        return handleTouchEvent(static_cast<QTouchEvent *>(event));
    }
    if (event->type() == QEvent::Gesture) {
        return gestureEvent(static_cast<QGestureEvent *>(event));
    }
    return QWidget::event(event);
}

void FloatingBall::watchScreen(QScreen *display)
{
    connect(display, &QScreen::geometryChanged, this, [this]() { scheduleScreenAdaptation(); });
    connect(display, &QScreen::availableGeometryChanged, this, [this]() { scheduleScreenAdaptation(); });
    connect(display, &QScreen::logicalDotsPerInchChanged, this, [this]() { scheduleScreenAdaptation(); });
}

void FloatingBall::scheduleScreenAdaptation()
{
    if (m_actionIndex >= 0 || m_screenAdaptationPending || m_adaptingScreen) return;
    m_screenAdaptationPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_screenAdaptationPending = false;
        if (m_actionIndex < 0) adaptToScreen();
    });
}

void FloatingBall::adaptToScreen()
{
    QScreen *display = m_currentScreen;
    if (!display || !QGuiApplication::screens().contains(display))
        display = QGuiApplication::primaryScreen();
    if (!display) return;
    m_currentScreen = display;
    fitToScreen(display->availableGeometry(), display->geometry().size());
}

void FloatingBall::fitToScreen(const QRect &bounds, const QSize &screenSize)
{
    if (bounds.isEmpty() || m_actionIndex >= 0) return;
    const QScopedValueRollback<bool> guard(m_adaptingScreen, true);
    // Work-area edges can move when an auto-hiding/magnifying dock changes.
    // Preserve the current anchor and only clamp if it actually falls outside.
    const QPointF center = QRectF(geometry()).center();
    m_screenBounds = bounds;
    // Qt reports logical pixels, so high-DPI scaling is already accounted for.
    const QSize logicalSize = screenSize.isEmpty() ? bounds.size() : screenSize;
    m_screenScale = qBound(0.5, qMin(logicalSize.width() / 1280.0, logicalSize.height() / 800.0), 1.0);
    const int diameter = qBound(1, qRound(m_preferredDiameter * m_screenScale),
                                qMin(bounds.width(), bounds.height()));
    const QPoint position(qBound(bounds.left(), qRound(center.x() - diameter / 2.0), bounds.right() - diameter + 1),
                          qBound(bounds.top(), qRound(center.y() - diameter / 2.0), bounds.bottom() - diameter + 1));
    applyGeometry(QRect(position, QSize(diameter, diameter)), false);
    updateOrbitLayout();
    if (m_hoverLabel) m_hoverLabel->reposition(geometry());
}

void FloatingBall::updateBallGeometry(const QSizeF &size, const QPointF &anchorGlobalPos, bool keepAnchor, bool animate)
{
    const int diameter = qBound(qRound(kMinDiameter * m_screenScale),
                               qRound(qMin(size.width(), size.height())),
                               qRound(kMaxDiameter * m_screenScale));
    m_preferredDiameter = diameter / m_screenScale;
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
    updateBallMask();
    if (ballLabel) {
        ballLabel->setGeometry(6, height()/2+3, width()-12, height()/3);
    }
}

void FloatingBall::updateBallStyle(const QString &colorName)
{
    m_ballColor = QColor(colorName);
    update();
}

void FloatingBall::updateBallMask()
{
    setMask(QRegion(rect(), QRegion::Ellipse));
}

void FloatingBall::updateHoverLabel()
{
    if (m_actionIndex >= 0 && m_pointerInside && !m_orbitAnimating &&
        isVisible() && width() >= 24) {
        if (!m_hoverLabel) m_hoverLabel = new HoverLabelBubble(this);
        m_hoverLabel->reveal(ballLabel->text(), geometry());
    } else if (m_hoverLabel) {
        m_hoverLabel->dismiss(m_orbitAnimating || !isVisible());
    }
}

void FloatingBall::animateBallOpacity(qreal target, int duration)
{
    m_opacityAnimation->stop();
    if (qAbs(m_ballOpacity - target) < 0.001) return;
    {
        const QSignalBlocker blocker(m_opacityAnimation);
        m_opacityAnimation->setStartValue(m_ballOpacity);
        m_opacityAnimation->setEndValue(target);
        m_opacityAnimation->setDuration(duration);
        m_opacityAnimation->setCurrentTime(0);
    }
    m_opacityAnimation->start();
}

void FloatingBall::refreshBallOpacity()
{
    // Paint alpha directly so idle fading works on Wayland surfaces too.
    // Expanded actions stay as clear as the main ball, including after hover
    // ends. Return fading is handled separately by the orbit animation.
    if (m_actionIndex >= 0 || m_pointerInside || m_isDragging || m_isPinching || m_orbitVisible || m_menuOpen) {
        m_idleTimer->stop();
        animateBallOpacity(m_isDragging || m_isPinching ? 0.94 : 0.85, 120);
    } else if (!m_idleTimer->isActive()) {
        m_idleTimer->start();
    }
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
            refreshBallOpacity();
            return true;
        }

        if (points.size() == 1) {
            m_isPinching = false;
            m_isDragging = true;
            m_dragTouchId = points.first().id();
            m_dragOffset = points.first().globalPosition() - pos();
            m_pressPosition = points.first().globalPosition().toPoint();
            m_moved = false;
            refreshBallOpacity();
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
            refreshBallOpacity();
            return true;
        }
    }

    if (event->type() == QEvent::TouchEnd && m_isDragging && !m_isPinching && !m_moved) {
        if (m_clickAction) m_clickAction();
        else toggleOrbit();
    }
    resetTouchState();
    refreshBallOpacity();
    return true;
}

bool FloatingBall::gestureEvent(QGestureEvent *event)
{
    QGesture *rawPinch = event->gesture(Qt::PinchGesture);
    if (QPinchGesture *pinch = qobject_cast<QPinchGesture *>(rawPinch)) {
        m_isPinching = pinch->state() != Qt::GestureFinished && pinch->state() != Qt::GestureCanceled;
        refreshBallOpacity();
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
    m_menuOpen = true;
    refreshBallOpacity();
    QMenu menu;
    auto capture=menu.addAction(tr("截取区域"));
    connect(capture,&QAction::triggered,this,&FloatingBall::captureRegion);
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
    m_menuOpen = false;
    refreshBallOpacity();
}

void FloatingBall::showScreenshots()
{
    if (!m_screenshotPanel) m_screenshotPanel = new ScreenshotPanel(nullptr, m_screenshotDirectory);
    m_screenshotPanel->reveal(geometry().center());
}

void FloatingBall::captureRegion()
{
    takeScreenshot({});
}
void FloatingBall::takeScreenshot(const CaptureOptions &options)
{
    if (!m_screenshotPanel) m_screenshotPanel = new ScreenshotPanel(nullptr, m_screenshotDirectory);
    m_screenshotPanel->takeScreenshot(geometry().center(),options);
}
void FloatingBall::recordScreen(bool window)
{
    if (!m_screenshotPanel) m_screenshotPanel = new ScreenshotPanel(nullptr, m_screenshotDirectory);
    m_screenshotPanel->reveal(geometry().center(),false);
    m_screenshotPanel->startRecording(window);
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
    m_pointerInside = true;
    refreshBallOpacity();
    updateHoverLabel();
    // Hover changes appearance only. Resizing the main surface here would
    // relayout every orbit action on each animation frame.
    updateBallStyle("#ffffff");
}

// 重写鼠标离开事件
void FloatingBall::leaveEvent(QEvent *event)
{
    Q_UNUSED(event)
    m_pointerInside = false;
    refreshBallOpacity();
    updateHoverLabel();
    updateBallStyle(m_actionIndex == 0 ? "#f0ebff" : "#faf9fc");
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
        refreshBallOpacity();
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
        refreshBallOpacity();
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
    refreshBallOpacity();
    if (!m_orbitAnimation) {
        m_orbitAnimation = new QVariantAnimation(this);
        connect(m_orbitAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            m_orbitProgress = value.toReal();
            updateOrbitLayout();
        });
        connect(m_orbitAnimation, &QVariantAnimation::finished, this, [this]() {
            for (FloatingBall *ball : qAsConst(m_orbitBalls)) {
                ball->m_orbitAnimating = false;
                ball->updateHoverLabel();
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
        aBall->updateBallMask();
        aBall->updateBallStyle(i == 0 ? "#f0ebff" : "#faf9fc");
        const QStringList labels = {tr("截图"), tr("终端"), tr("浏览器"), tr("主目录"), tr("复制"), tr("粘贴")};
        aBall->ballLabel->setText(labels[i]);
        aBall->setAccessibleName(labels[i]);
        aBall->setToolTip(QString());
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
        ball->refreshBallOpacity();
        ball->updateHoverLabel();
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
    if (!screen) return;
    const QRect bounds = m_adaptingScreen ? m_screenBounds : screen->availableGeometry();
    const qreal actionSize = qMin(64.0 * m_screenScale, qreal(qMin(bounds.width(), bounds.height())));
    const qreal radius = qMin(qMax(100.0 * m_screenScale, (width()+actionSize)/2.0+24*m_screenScale),
                             qMax(0.0, (qMin(bounds.width(), bounds.height()) - actionSize) / 2.0)) * m_orbitProgress;
    const int diameter = qMax(1, qRound(actionSize*(0.22+0.78*m_orbitProgress)));
    for (int i = 0; i < m_orbitBalls.size(); ++i) {
        FloatingBall *ball = m_orbitBalls[i];
        ball->m_screenScale = m_screenScale;
        ball->m_geometryAnimation->stop();
        const qreal angle = qDegreesToRadians(60.0*i);
        const QPointF pos = center + QPointF(qCos(angle)*radius, qSin(angle)*radius) - QPointF(diameter/2.0,diameter/2.0);
        ball->setGeometry(qBound(bounds.left(),qRound(pos.x()),bounds.right()-diameter+1),
                          qBound(bounds.top(),qRound(pos.y()),bounds.bottom()-diameter+1),diameter,diameter);
        ball->updateHoverLabel();
        ball->m_revealOpacity = qBound(0.0, m_orbitProgress * 1.5, 1.0);
        ball->update();
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
    if (m_actionIndex < 0) {
        animateBallOpacity(0.85, 120);
        refreshBallOpacity();
    }
    event->accept();
}

void FloatingBall::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (ballLabel) {
        ballLabel->setGeometry(6, height()/2+3, width()-12, height()/3);
    }
    updateBallMask();
    if (m_hoverLabel) m_hoverLabel->reposition(geometry());
    updateOrbitLayout();
}

void FloatingBall::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (m_actionIndex < 0 && !m_adaptingScreen) {
        if (QScreen *display = QGuiApplication::screenAt(geometry().center())) {
            if (m_currentScreen != display) {
                m_currentScreen = display;
                // A cross-monitor drag already placed the ball on its new screen.
                m_screenBounds = display->availableGeometry();
                scheduleScreenAdaptation();
            }
        }
    }
    // Update in the input event itself: dragging never queues another tween.
    if (m_hoverLabel) m_hoverLabel->reposition(geometry());
    updateOrbitLayout();
}

void FloatingBall::paintEvent(QPaintEvent *)
{
    // Assemble each circular face before fading once. Action icons keep their
    // size and center while a separate translucent bubble displays hover text.
    QPixmap face(QSize(qRound(width() * devicePixelRatioF()),
                       qRound(height() * devicePixelRatioF())));
    face.setDevicePixelRatio(devicePixelRatioF());
    face.fill(Qt::transparent);
    QPainter p(&face);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF surfaceRect = QRectF(rect()).adjusted(4, 4, -4, -4);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 35));
    p.drawEllipse(surfaceRect.translated(0, 1.5));
    QLinearGradient surface(surfaceRect.topLeft(), surfaceRect.bottomRight());
    const QColor top(m_ballColor.red() / 5 + 8, m_ballColor.green() / 5 + 8,
                     m_ballColor.blue() / 5 + 8, 225);
    QColor bottom = top.darker(165);
    bottom.setAlpha(235);
    surface.setColorAt(0, top);
    surface.setColorAt(1, bottom);
    p.setBrush(surface);
    p.setPen(QPen(QColor(255, 255, 255, 38), 0.8));
    p.drawEllipse(surfaceRect);

    if (m_actionIndex < 0) {
        const QPointF center = QRectF(rect()).center();
        const qreal outerRadius = width() * 0.31;
        p.setPen(QPen(QColor(255, 255, 255, 110), width() * 0.038));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(center, outerRadius, outerRadius);
        p.setPen(QPen(QColor(255, 255, 255, 180), width() * 0.045));
        p.drawEllipse(center, width() * 0.245, width() * 0.245);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 245));
        p.drawEllipse(center, width() * 0.18, width() * 0.18);
    } else {
        const FloatingIcons::Icon icons[] = {FloatingIcons::Screenshot, FloatingIcons::Terminal,
            FloatingIcons::Browser, FloatingIcons::Folder, FloatingIcons::Copy, FloatingIcons::Paste};
        const qreal iconSize = width() * 0.42;
        const QRectF iconRect((width() - iconSize) / 2, (height() - iconSize) / 2,
                             iconSize, iconSize);
        const QColor ink(m_actionIndex == 0 ? "#e3ddff" : "#f7f7fb");
        FloatingIcons::draw(p, icons[m_actionIndex], iconRect, ink);
    }
    p.end();
    QPainter windowPainter(this);
    windowPainter.setOpacity(m_ballOpacity * m_revealOpacity);
    windowPainter.drawPixmap(0, 0, face);
}
