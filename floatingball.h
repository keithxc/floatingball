#ifndef FLOATINGBALL_H
#define FLOATINGBALL_H

#include <QColor>
#include <QElapsedTimer>
#include <QGestureEvent>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QString>
#include <QStringList>
#include <QTouchEvent>
#include <QVariantAnimation>
#include <QWidget>
#include "appdock.h"
#include "statusorbrenderer.h"
#include "systemstatus.h"

class AppLauncherPopover;
class ClipboardBubble;
class QuickDeck;
class SystemPopover;
class QScreen;
class QTimer;
class FloatingBall;

QT_BEGIN_NAMESPACE
namespace Ui {
class FloatingBall;
}
QT_END_NAMESPACE

struct RadialSlotItem {
    enum Kind {
        ActionSlot,
        AppSlot,
        NotificationSlot,
        TraySlot,
        SystemSlot,
        TrayMenuSlot,
        BackSlot,
        CenterSlot
    };
    Kind kind = ActionSlot;
    int ringIndex = 0; // 0 = inner ring, 1+ = outer rings / drilled ring
    QString id;
    QString label;
    QString shortLabel; // Compact 2-6 char label shown beneath drilled/hub orbs
    QString drillKey;   // Non-empty for drillable hubs: "windows", "tray", "control", "tools"
    DockAppEntry app;
    NotificationItem notificationItem;
    int unreadCount = 0;
    QString notificationPreview;
    TrayItemInfo trayItem;
    TrayMenuEntry trayMenuEntry;
    bool isBranchHub = false;
    bool isBranchItem = false;
    bool toggleActive = false;
    QColor accentColor;
    QPointF finalCenterGlobal; // Resting target center (independent of open animation progress)
    QPointF centerGlobal;
    QRectF rectGlobal;
};

struct DrillFrame {
    QString key;   // "windows" | "tray" | "control" | "tools" | "tray_menu"
    QString title; // "窗口" | "托盘" | "控制" | "工具" | "<TrayApp>"
    QColor accentColor;
    QPointF centerGlobal;
    QPointF backGlobal;
    TrayItemInfo traySource;
    QList<TrayMenuEntry> trayMenuItems;
};

// Single-canvas overlay that renders both the 1..10 concentric Xiaomi Mi 5 /
// fooView multi-ring radial fan (with zero-jitter hover/slide magnetization
// and live Dock app switching) and the seamless fooView live crosshair reticle
// + dwell ring + region selection rectangle.
class RadialFanOverlay : public QWidget
{
    Q_OBJECT
public:
    enum Mode {
        HiddenMode,
        TapFanMode,
        FlickFanMode,
        CrosshairAimMode,
        CrosshairRectMode
    };

    enum BranchMode {
        BranchNone,
        BranchTray,
        BranchSystem
    };

    explicit RadialFanOverlay(FloatingBall *owner);

    void openTapFan();
    void openFlickFan(const QPointF &fingerGlobal);
    void enterCrosshairAim(const QPointF &fingerGlobal, qreal dwellProgress);
    void updateCrosshairRect(const QPointF &anchorGlobal, const QPointF &fingerGlobal);
    void updateFinger(const QPointF &fingerGlobal, int highlightedSlot, qreal dwellProgress = 0.0);
    void syncProgress(qreal progress);
    void refreshLiveLayout();
    void dismissOverlay(bool immediate = false);

    Mode overlayMode() const { return m_mode; }
    bool isDrilled() const { return !m_drillStack.isEmpty(); }
    void drillInto(const QString &key, const QString &title, const QColor &accent,
                   const QPointF &hubPos, const TrayItemInfo &traySource = {});
    void popDrill();
    void clearDrill();
    void handleContinuousDragAt(const QPointF &globalPt);
    int highlightedSlot() const { return m_highlightedSlot; }
    // Inner ring (ring 0, 5 classic slots) rects for backward compatibility
    QList<QRectF> slotGlobalRects() const;
    // All rings (ring 0 .. activeRingCount-1) slot rects
    QList<QRectF> allSlotGlobalRects() const;
    const QList<RadialSlotItem> &slotsData() const { return m_slots; }
    int activeRingCount() const { return m_activeRingCount; }
    int hitSlotAtGlobal(const QPointF &globalPt, bool magneticSector) const;
    qreal fanOuterRadius() const { return m_outermostRadius; }
    QString activeLabelText() const;
    QList<qreal> computeAttentionAnglesRad();

signals:
    void slotTapped(int slotIndex);
    void slotMiddleClicked(int slotIndex);
    void slotContextMenuRequested(int slotIndex, const QPoint &globalPos);
    void ringCountStepRequested(int delta);
    void dismissedByUser();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPointF pointerGlobalPos(const QPointF &localPos, const QPointF &eventGlobalPos) const;
    void recomputeFanLayout();
    void updateHitMask();
    QRectF labelPillRectLocal() const;

    FloatingBall *m_owner = nullptr;
    Mode m_mode = HiddenMode;
    QList<DrillFrame> m_drillStack;
    QElapsedTimer m_drillCooldown;
    QTimer *m_trayDwellTimer = nullptr;
    QPointF m_tapPressGlobal;
    bool m_tapDragged = false;
    TrayItemInfo m_pendingTrayDwell;
    QPointF m_pendingTrayPos;
    qreal m_progress = 0.0;
    int m_highlightedSlot = -1;
    int m_pressedTapSlot = -1;
    qreal m_dwellProgress = 0.0;
    QPointF m_ballCenterGlobal;
    QPointF m_fingerGlobal;
    QPointF m_crosshairAnchorGlobal;
    qreal m_fanRadius = 104.0;
    qreal m_outermostRadius = 104.0;
    qreal m_slotDiameter = 52.0;
    int m_activeRingCount = 1;
    QList<qreal> m_ringRadii;
    QList<RadialSlotItem> m_slots;
};

class FloatingBall : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kOrbitSlotCount = 5;
    static constexpr int kMinRingCount = 1;
    static constexpr int kMaxRingCount = 30;
    static constexpr int kDefaultRingCount = 3; // Ring 1 & 2 = quick actions (incl. Downloads & Trash), Ring 3+ = open apps & Dock
    static constexpr int kMinDiameter = 44;
    static constexpr int kMaxDiameter = 128;

    enum DockEdge {
        DockFree = 0,
        DockLeft = -1,
        DockRight = 1,
        DockTop = 2,
        DockBottom = -2
    };

    enum GestureMode {
        GestureIdle,
        GesturePressed,
        GestureMoveBall,
        GestureFlickFan,
        GestureCrosshairAim,
        GestureCrosshairRect
    };

    explicit FloatingBall(QWidget *parent = nullptr,
                          QScreen *assignedScreen = nullptr,
                          FloatingBall *primaryPeer = nullptr);
    ~FloatingBall() override;

    // Actions & panels
    void executeAction(const QString &id);
    void executeSlotItem(const RadialSlotItem &item);
    void openNotificationTarget(const NotificationItem &item);
    void captureRegion();
    void recordScreen();
    void toggleClipboardBubble();
    void toggleQuickDeck();
    void openSystemPopover(int tab);
    void toggleAppLauncher();
    void showSettings();

    ClipboardBubble *clipboardBubble();
    QuickDeck *quickDeck();
    SystemPopover *systemPopover();
    AppLauncherPopover *appLauncher();
    AppDockModel *dockModel();
    RadialFanOverlay *fanOverlay() const { return m_fanOverlay; }

    // Configuration & inspection (used by overlay and automated tests)
    QStringList orbitActions() const { return m_orbitActions; }
    bool setOrbitActions(const QStringList &actions);
    static const QStringList &availableActions();
    static QStringList defaultOrbitActions();
    QString actionLabel(const QString &id) const;

    int ringCount() const { return m_ringCount; }
    void setRingCount(int rings);
    bool dockModeEnabled() const { return m_dockModeEnabled; }
    void setDockModeEnabled(bool enabled);
    bool statusOrbMode() const { return m_statusOrbMode; }
    void setStatusOrbMode(bool enabled);
    const StatusOrbRenderer::FaceConfig &orbFaceConfig() const { return m_orbFaceConfig; }
    void setOrbFaceConfig(const StatusOrbRenderer::FaceConfig &cfg);
    bool autoHideEnabled() const { return m_autoHideEnabled; }
    void setAutoHideEnabled(bool enabled);
    bool autoTuckFullscreen() const { return m_autoTuckFullscreen; }
    void setAutoTuckFullscreen(bool enabled);
    bool isManualTucked() const { return m_manualTucked; }
    bool isFullscreenTucked() const { return m_fullscreenTucked; }
    void tuckToEdgeCapsule(bool manual = true, bool animate = true);
    void triggerImeFlash(const QString &label, int durationMs = 1600);
    QString activeImeFlashLabel() const;
    const QList<qreal> &attentionAnglesRad() const { return m_attentionAnglesRad; }
    void refreshAttentionBearings();

    bool multiScreenEnabled() const { return m_multiScreenEnabled; }
    void setMultiScreenEnabled(bool enabled);
    QScreen *assignedScreen() const { return m_assignedScreen; }
    bool isPrimaryBall() const { return m_primaryPeer == nullptr; }
    QList<FloatingBall *> replicaBalls() const;
    void syncScreenReplicas();
    void closeOtherFans(FloatingBall *except);
    void restoreAllScreenBalls();
    SystemStatusMonitor *statusMonitor();
    void clearNotificationDot();

    bool isSystemMuted() const { return m_systemMuted; }
    int systemVolume() const { return m_systemVolume; }
    void setSystemAudioState(int percent, bool muted);
    void refreshSystemAudioState();

    bool isOrbitVisible() const { return m_orbitVisible; }
    bool isEdgeTucked() const { return m_edgeTucked; }
    int dockSide() const { return m_dockSide; }
    qreal activeOpacity() const { return m_activeOpacity; }
    qreal idleOpacity() const { return m_idleOpacity; }
    qreal screenScale() const { return m_screenScale; }
    QPointF anchorCenterGlobal() const;
    QList<QRectF> orbitSlotGlobalRects() const;
    QList<QRectF> allOrbitSlotGlobalRects() const;
    GestureMode gestureMode() const { return m_gestureMode; }

    void expandFromEdge();
    void collapseToEdge();
    void closeFan(bool immediate = false);

protected:
    bool event(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    Ui::FloatingBall *ui = nullptr;

    // Unified pointer/touch state machine (Xiaomi Mi 5 + fooView fusion)
    friend class RadialFanOverlay;
    QPointF pointerGlobalPos(const QPointF &localPos, const QPointF &eventGlobalPos) const;
    void beginPointerInteraction(const QPointF &globalPos, Qt::KeyboardModifiers modifiers);
    void updatePointerInteraction(const QPointF &globalPos);
    void endPointerInteraction(const QPointF &globalPos);
    void cancelPointerInteraction();
    bool handleTouchEvent(QTouchEvent *event);
    bool gestureEvent(QGestureEvent *event);

    void openTapFan();
    void animateFanTo(bool open);
    void installOutsideDismissMonitor();
    void removeOutsideDismissMonitor();
    void startCrosshairDwell(const QPointF &globalPos);
    void stopCrosshairDwell();
    void showSlotContextMenu(int slotIndex, const QPoint &globalPos);

    void updateBallMask();
    void refreshBallOpacity();
    void animateBallOpacity(qreal target, int duration);
    void updateBallGeometry(const QSizeF &size, const QPointF &anchorGlobalPos,
                            bool keepAnchor = false, bool animate = false);
    void applyGeometry(const QRect &targetGeometry, bool animate = false);
    void snapToEdge(bool animate = true);
    void watchScreen(QScreen *screen);
    void scheduleScreenAdaptation();
    void adaptToScreen();
    void recoverVisibleAfterOutputChange();
    void fitToScreen(const QRect &bounds, const QSize &screenSize = QSize());
    void showContextMenu(const QPoint &globalPos);
    void loadPreferences();
    void savePreferences();
    void loadScreenPlacement();
    void saveScreenPlacement() const;
    void syncSharedPreferencesFrom(const FloatingBall *source);
    void broadcastSharedPreferences();
    void setHiddenOnThisScreen(bool hidden);
    void setBallVisible(bool visible);

    QStringList m_orbitActions;
    RadialFanOverlay *m_fanOverlay = nullptr;
    AppDockModel *m_dockModel = nullptr;
    SystemStatusMonitor *m_statusMonitor = nullptr;
    ClipboardBubble *m_clipboardBubble = nullptr;
    QuickDeck *m_quickDeck = nullptr;
    SystemPopover *m_systemPopover = nullptr;
    AppLauncherPopover *m_appLauncher = nullptr;

    GestureMode m_gestureMode = GestureIdle;
    int m_activeTouchId = -1;
    bool m_isPinching = false;
    qreal m_initialPinchDistance = 0.0;
    QSizeF m_initialPinchSize;

    QPointF m_pressGlobalPos;
    QPointF m_lastGlobalPos;
    QPointF m_dragOffset;
    QPointF m_crosshairAnchorGlobal;
    QPointF m_crosshairDwellOrigin;
    qreal m_recentOutwardStep = 0.0;
    qreal m_outwardStroke = 0.0;
    qreal m_outwardStrokeTan = 0.0;
    QElapsedTimer m_outwardMoveTimer;

    QTimer *m_holdTimer = nullptr;
    QTimer *m_idleTimer = nullptr;
    QTimer *m_audioPollTimer = nullptr;
    QTimer *m_clockTimer = nullptr;
    QTimer *m_imeFlashTimer = nullptr;
    QVariantAnimation *m_dwellAnimation = nullptr;
    QVariantAnimation *m_orbitAnimation = nullptr;
    QVariantAnimation *m_opacityAnimation = nullptr;
    QVariantAnimation *m_pickupAnimation = nullptr;
    QPropertyAnimation *m_geometryAnimation = nullptr;

    qreal m_dwellProgress = 0.0;
    qreal m_orbitProgress = 0.0;
    qreal m_pickupPulse = 0.0;
    qreal m_ballOpacity = 0.82;
    qreal m_idleOpacity = 0.82;
    qreal m_activeOpacity = 0.94;
    qreal m_preferredDiameter = 60.0;
    qreal m_screenScale = 1.0;
    qreal m_verticalFraction = 0.58;

    int m_ringCount = kDefaultRingCount; // Preserved for settings compat; outer rings auto-size to open windows
    bool m_dockModeEnabled = true;       // outer rings expand all live open windows
    bool m_statusOrbMode = true;         // Primary circular 24H clock + status ball
    StatusOrbRenderer::FaceConfig m_orbFaceConfig;
    bool m_multiScreenEnabled = true;    // Scheme A: independent docked orb per screen, shared content
    bool m_hiddenOnThisScreen = false;
    bool m_userHiddenAll = false;
    bool m_systemMuted = false;
    int m_systemVolume = 60;

    int m_dockSide = 1; // -1 = left edge, +1 = right edge, 2 = top, -2 = bottom, 0 = free
    bool m_snapEnabled = true;
    bool m_autoHideEnabled = false;      // Default OFF: primary circular clock ball stays visible!
    bool m_autoTuckFullscreen = true;    // Default ON: fullscreen video/game morphs ball into slim edge capsule
    bool m_manualTucked = false;
    bool m_fullscreenTucked = false;
    bool m_startedInteractionTucked = false;
    bool m_dismissedPanelOnPress = false;
    bool m_bezelTuckArmed = false;
    bool m_crosshairPullEnabled = true;
    bool m_edgeTucked = false;
    bool m_orbitVisible = false;
    bool m_pointerInside = false;
    bool m_menuOpen = false;
    bool m_settingsOpen = false;
    bool m_screenAdaptationPending = false;
    bool m_adaptingScreen = false;
    bool m_waylandSurfaceClosed = false;
    QString m_imeFlashLabel;
    QList<qreal> m_attentionAnglesRad;

    QRect m_expandedDockGeometry;
    QPointer<QScreen> m_assignedScreen;
    QPointer<QScreen> m_currentScreen;
    QPointer<FloatingBall> m_primaryPeer;
    QHash<QString, QPointer<FloatingBall>> m_screenReplicas;
    QRect m_screenBounds;
    void *m_outsideClickMonitor = nullptr;
    QElapsedTimer m_fanOpenedTimer;
};

#endif // FLOATINGBALL_H
