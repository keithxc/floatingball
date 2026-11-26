#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
struct NotificationItem;
class SystemStatusMonitor;
class CapsuleSlider;
class IosToggleSwitch;

// Unified iPadOS / macOS-style floating Control Center (LayerShellQt overlay
// with native Wayland Gaussian blur + borderless feathered frosted glass).
// Home page keeps a compact Bento layout (top-left 5-row connectivity card,
// top-right DND + dual slender vertical brightness/volume sliders, clickable
// battery+power-profile pill & utility icon buttons in the header, and an
// auto-hiding MPRIS Now Playing card). Each connectivity row drills inline
// into a clean, single-purpose iOS-style subpage.
class SystemPopover : public QWidget
{
    Q_OBJECT
public:
    enum Page {
        PageHome = 0,
        PageWifi = 1,
        PageHotspot = 2,
        PageBluetooth = 3,
        PageMihomo = 4,
        PageCellular = 5,
        PageNotifications = 6
    };
    // Compatibility aliases for direct tab callers
    enum Tab {
        TabHome = PageHome,
        TabWifi = PageWifi,
        TabHotspot = PageHotspot,
        TabBluetooth = PageBluetooth,
        TabMihomo = PageMihomo,
        TabCellular = PageCellular,
        TabNotifications = PageNotifications
    };

    explicit SystemPopover(SystemStatusMonitor *monitor, QWidget *parent = nullptr);
    ~SystemPopover() override;

    void revealHome(const QPoint &anchorGlobal, bool toggle = true);
    void revealTab(Tab tab, const QPoint &anchorGlobal, bool toggle = true);
    void showPage(Page page);
    Page currentPage() const { return m_currentPage; }
    void refreshCurrentPage();

    // Optional hooks for automated tests / preview rendering
    void setMprisPreviewForTest(const QString &title, const QString &artist, bool playing);
    void setAutoDismissEnabled(bool enabled) { m_autoDismissEnabled = enabled; }
    void showStatusBanner(const QString &msg, bool isError = false);
    QString defaultHotspotSsid() const;

signals:
    void audioStateChanged(int percent, bool muted);
    void brightnessStateChanged(int percent);
    void quickDeckRequested();
    void notificationActivated(const NotificationItem &item);

protected:
    void paintEvent(QPaintEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct WifiEntry {
        QString ssid;
        int signal = 0;
        QString security;
        QString rate;
        QString freq;
        bool active = false;
        bool saved = false;
        QString savedUuid;
    };
    struct HotspotClient {
        QString ip;
        QString mac;
        QString host;
    };
    struct BluetoothDevice {
        QString address;
        QString name;
        QString iconType;
        bool connected = false;
        int batteryPercent = -1; // -1 if unavailable
    };
    struct MihomoNode {
        QString name;
        int delayMs = -1; // -1 unknown, 0 timeout
    };
    struct MihomoGroup {
        QString name;
        QString type; // Selector, Fallback, URLTest, etc.
        QString now;
        QList<MihomoNode> nodes;
    };

    QWidget *buildHomePage(QWidget *parent);
    QWidget *buildWifiPage(QWidget *parent);
    QWidget *buildCellularPage(QWidget *parent);
    QWidget *buildHotspotPage(QWidget *parent);
    QWidget *buildBluetoothPage(QWidget *parent);
    QWidget *buildMihomoPage(QWidget *parent);
    QWidget *buildNotificationsPage(QWidget *parent);
    void rebuildNotificationsView();

    void positionNear(const QPoint &anchorGlobal);
    void updatePopoverHeight();
    void positionStatusBanner();
    void installOutsideDismissMonitor();
    void removeOutsideDismissMonitor();
    void syncHeaderForPage();
    void syncHomeFromSnapshot();

    // Live 1s net speed & traffic sampler (reads /sys/class/net/<if>/statistics directly)
    void syncNetStatsTimer();
    void pollLiveNetStats();

    // Audio & Brightness sliders
    void syncVolume();
    void syncBrightness();
    void applyAudioVisuals(int percent, bool muted, bool supported);

    // MPRIS Now Playing (Linux session DBus, fully async)
    void refreshMpris();
    void applyMprisVisuals();
    void triggerMprisAction(const QString &method);

    // Wi-Fi async actions (Wi-Fi only)
    void refreshWifi(bool rescan = false);
    void rebuildWifiList(bool wifiEnabled, const QList<WifiEntry> &entries);
    void toggleWifiRadio();
    void connectWifi(const WifiEntry &entry);
    void connectWifiWithPassword(const WifiEntry &entry, const QString &password);
    void showWifiPasswordPrompt(const WifiEntry &entry, const QString &hint = QString());
    void hideWifiPasswordPrompt();

    // 4G Cellular async actions (4G only)
    void refreshCellular();
    void rebuildCellularView();
    void toggleWwanRadio();

    // Hotspot (Wi-Fi AP, pure Hotspot view + Edit/Save mode + clients & traffic)
    void loadHotspotPrefs();
    void saveHotspotPrefs();
    bool shouldSkip5gHotspotFor(const QString &wifiDev) const;
    void markSkip5gHotspotFor(const QString &wifiDev, bool skip);
    void refreshHotspot();
    void rebuildHotspotView();
    void setHotspotEditMode(bool editing);
    void toggleHotspot();
    void applyHotspotConfig(bool restartIfActive);
    void startHotspotAsync();
    void stopHotspotAsync();

    // Bluetooth async actions
    void refreshBluetooth();
    void rebuildBluetoothList(bool powered, const QList<BluetoothDevice> &devices);
    void toggleBluetoothPower();
    void toggleBluetoothDevice(const BluetoothDevice &dev);

    // Mihomo REST API async actions
    QString mihomoBaseUrl() const;
    QString mihomoSecret() const;
    void refreshMihomo();
    void rebuildMihomoView();
    void setMihomoMode(const QString &mode);
    void toggleMihomoTun();
    void selectMihomoNode(const QString &group, const QString &node);
    void testMihomoGroupDelay(const QString &group);

    SystemStatusMonitor *m_monitor = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    Page m_currentPage = PageHome;
    bool m_autoDismissEnabled = true;
    QElapsedTimer m_openedTimer;

    // Shared top header
    QToolButton *m_backBtn = nullptr;
    QLabel *m_titleLbl = nullptr;
    QToolButton *m_powerPill = nullptr;
    QToolButton *m_hdrNotifBtn = nullptr;
    QToolButton *m_hdrSettingsBtn = nullptr;
    QToolButton *m_hdrLockBtn = nullptr;
    QToolButton *m_hdrPowerBtn = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QToolButton *m_closeBtn = nullptr;
    QLabel *m_statusBanner = nullptr;
    quint64 m_statusBannerSeq = 0;
    void *m_outsideClickMonitor = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Home Bento connectivity rows (tap left glass orb = instant toggle; tap text/chevron = subpage)
    QToolButton *m_homeWifiBadge = nullptr;
    QLabel *m_homeWifiSub = nullptr;
    QWidget *m_homeWwanRow = nullptr;
    QToolButton *m_homeWwanBadge = nullptr;
    QLabel *m_homeWwanSub = nullptr;
    QToolButton *m_homeHotspotBadge = nullptr;
    QLabel *m_homeHotspotSub = nullptr;
    QToolButton *m_homeBtBadge = nullptr;
    QLabel *m_homeBtSub = nullptr;
    QToolButton *m_homeMhBadge = nullptr;
    QLabel *m_homeMhSub = nullptr;

    // Home Bento right column (DND + two slender iOS vertical sliders)
    QPushButton *m_dndTile = nullptr;
    QLabel *m_dndBadge = nullptr;
    QLabel *m_dndSub = nullptr;
    CapsuleSlider *m_brightnessSlider = nullptr;
    QWidget *m_desktopQuickCol = nullptr;
    QTimer *m_brightnessDebounce = nullptr;
    CapsuleSlider *m_volumeSlider = nullptr;
    QTimer *m_volumeDebounce = nullptr;
    bool m_muted = false;
    int m_lastVolume = 50;

    // Home Unread Notification Preview Banner (auto-hides when 0 unread)
    QPushButton *m_notifHomeCard = nullptr;
    QLabel *m_notifHomeIcon = nullptr;
    QLabel *m_notifHomeTitle = nullptr;
    QLabel *m_notifHomeSub = nullptr;

    // Notifications subpage widgets
    QLabel *m_notifCountLbl = nullptr;
    QToolButton *m_notifMarkAllBtn = nullptr;
    QToolButton *m_notifClearAllBtn = nullptr;
    QVBoxLayout *m_notifListLayout = nullptr;

    // Home MPRIS Now Playing card (auto-hides when idle)
    QWidget *m_mprisCard = nullptr;
    QLabel *m_mprisTitleLbl = nullptr;
    QLabel *m_mprisArtistLbl = nullptr;
    QToolButton *m_mprisPlayBtn = nullptr;
    QString m_mprisService;
    QString m_mprisTitle;
    QString m_mprisArtist;
    bool m_mprisPlaying = false;
    bool m_mprisVisible = false;

    // Live 1s speed/traffic stats
    QTimer *m_netStatsTimer = nullptr;
    QTimer *m_netStatusRefreshDebounce = nullptr;
    QString m_lastMonitorNetKey;
    QString m_lastStatsIface;
    quint64 m_lastRxBytes = 0;
    quint64 m_lastTxBytes = 0;
    bool m_hasLastStats = false;

    // Wi-Fi subpage widgets & state (pure Wi-Fi)
    IosToggleSwitch *m_wifiToggle = nullptr;
    QWidget *m_wifiDetailCard = nullptr;
    QLabel *m_wifiDetailTitle = nullptr;
    QLabel *m_wifiDetailMeta = nullptr;
    QLabel *m_wifiSpeedLbl = nullptr;
    QWidget *m_wifiPassCard = nullptr;
    QLabel *m_wifiPassTitle = nullptr;
    QLineEdit *m_wifiPassEdit = nullptr;
    QToolButton *m_wifiPassEyeBtn = nullptr;
    WifiEntry m_pendingWifiEntry;
    QVBoxLayout *m_wifiListLayout = nullptr;
    bool m_wifiRadioEnabled = true;
    bool m_wifiRadioBusy = false;
    bool m_wifiConnecting = false;
    quint64 m_wifiRefreshSeq = 0;
    QString m_wifiIface;
    QString m_wifiIp;
    QString m_wifiRate;
    QString m_wifiFreq;
    QString m_wifiSecurity;
    QString m_cachedWifiSsid;
    int m_cachedWifiSignal = 0;

    // 4G Cellular subpage widgets & state (pure 4G)
    IosToggleSwitch *m_cellToggle = nullptr;
    QLabel *m_cellOpLbl = nullptr;
    QLabel *m_cellSignalLbl = nullptr;
    QLabel *m_cellIpLbl = nullptr;
    QLabel *m_cellSpeedLbl = nullptr;
    QLabel *m_cellTrafficLbl = nullptr;
    bool m_wwanAvailable = false;
    bool m_wwanEnabled = false;
    bool m_wwanConnected = false;
    bool m_wwanBusy = false;
    quint64 m_cellRefreshSeq = 0;
    QString m_cellOperator;
    QString m_cellTech;
    QString m_cellStateText;
    QString m_cellModemModel;
    QString m_cellNetIface;
    QString m_cellIp;
    int m_cellSignal = -1;

    // Hotspot subpage widgets & state (pure Hotspot: view mode + edit mode + clients & traffic)
    IosToggleSwitch *m_hsToggle = nullptr;
    QWidget *m_hsViewCard = nullptr;
    QWidget *m_hsEditCard = nullptr;
    QLabel *m_hsInfoTitle = nullptr;
    QLabel *m_hsPassValueLbl = nullptr;
    QToolButton *m_hsViewEyeBtn = nullptr;
    QLabel *m_hsTrafficLbl = nullptr;
    QLineEdit *m_hsSsidEdit = nullptr;
    QLineEdit *m_hsPassEdit = nullptr;
    QToolButton *m_hsSecBtns[2]{}; // 0 = WPA3 (sae), 1 = WPA2 (wpa-psk)
    QLabel *m_hsClientsHeader = nullptr;
    QVBoxLayout *m_hsClientsLayout = nullptr;
    QString m_hsWifiDev;
    bool m_hsActive = false;
    bool m_hsBusy = false;
    bool m_hsEditing = false;
    bool m_hsPassRevealed = false;
    quint64 m_hsRefreshSeq = 0;
    QString m_hsSsid;
    QString m_hsPassword;
    bool m_hsUseWpa3 = true; // default WPA3-Personal (SAE)
    QString m_hsActiveBand;  // "5GHz" or "2.4GHz"
    QList<HotspotClient> m_hsClients;

    // Bluetooth subpage widgets
    IosToggleSwitch *m_btToggle = nullptr;
    QLabel *m_btSummaryLbl = nullptr;
    QVBoxLayout *m_btListLayout = nullptr;
    bool m_btPowered = true;
    QString m_cachedBtSummary;

    // Mihomo subpage widgets & state
    QToolButton *m_mhModeBtns[3]{}; // rule, global, direct
    IosToggleSwitch *m_mhTunToggle = nullptr;
    QToolButton *m_mhDelayBtn = nullptr;
    QLabel *m_mhSummaryLbl = nullptr;
    QVBoxLayout *m_mhGroupBarLayout = nullptr;
    QVBoxLayout *m_mhNodeListLayout = nullptr;
    QString m_mhMode = QStringLiteral("rule");
    bool m_mhTunEnabled = false;
    bool m_mhReachable = false;
    QString m_mhTunDevice;
    QList<MihomoGroup> m_mhGroups;
    QString m_mhSelectedGroup;
};
