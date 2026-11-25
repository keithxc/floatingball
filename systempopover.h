#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class SystemStatusMonitor;
class CapsuleSlider;

// Unified macOS / iOS-style floating Control Center (LayerShellQt overlay,
// never tiled by Krohnkite, auto-dismisses when clicking outside).
// Home page presents a tactile Bento grid (Wi-Fi / Bluetooth / Mihomo
// connectivity stack, DND / Night Light / Power / Display / Settings tiles,
// thick Apple-style brightness & volume capsule sliders, auto-showing MPRIS
// Now Playing bar, and quick Deck / Lock / Power footer). Tapping any
// connectivity row drills inline into its dedicated subpage without spawning
// a second window or accidentally toggling radios.
class SystemPopover : public QWidget
{
    Q_OBJECT
public:
    enum Page {
        PageHome = 0,
        PageWifi = 1,
        PageBluetooth = 2,
        PageMihomo = 3
    };
    // Compatibility aliases for direct tab callers
    enum Tab {
        TabHome = PageHome,
        TabWifi = PageWifi,
        TabBluetooth = PageBluetooth,
        TabMihomo = PageMihomo
    };

    explicit SystemPopover(SystemStatusMonitor *monitor, QWidget *parent = nullptr);

    void revealHome(const QPoint &anchorGlobal, bool toggle = true);
    void revealTab(Tab tab, const QPoint &anchorGlobal, bool toggle = true);
    void showPage(Page page);
    Page currentPage() const { return m_currentPage; }
    void refreshCurrentPage();

    // Optional hook for automated tests / preview rendering
    void setMprisPreviewForTest(const QString &title, const QString &artist, bool playing);
    void setAutoDismissEnabled(bool enabled) { m_autoDismissEnabled = enabled; }

signals:
    void audioStateChanged(int percent, bool muted);
    void brightnessStateChanged(int percent);
    void quickDeckRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct WifiEntry {
        QString ssid;
        int signal = 0;
        QString security;
        bool active = false;
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
    QWidget *buildBluetoothPage(QWidget *parent);
    QWidget *buildMihomoPage(QWidget *parent);

    void positionNear(const QPoint &anchorGlobal);
    void showStatusBanner(const QString &msg, bool isError = false);
    void syncHeaderForPage();
    void syncHomeFromSnapshot();

    // Audio & Brightness sliders
    void syncVolume();
    void syncBrightness();
    void applyAudioVisuals(int percent, bool muted, bool supported);

    // MPRIS Now Playing (Linux session DBus, fully async)
    void refreshMpris();
    void applyMprisVisuals();
    void triggerMprisAction(const QString &method);

    // Wi-Fi async actions
    void refreshWifi(bool rescan = false);
    void rebuildWifiList(bool wifiEnabled, const QStringList &vpnLinks, const QList<WifiEntry> &entries);
    void toggleWifiRadio();
    void connectWifi(const WifiEntry &entry);

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
    QLabel *m_metaLbl = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QLabel *m_statusBanner = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Home Bento connectivity rows
    QLabel *m_homeWifiBadge = nullptr;
    QLabel *m_homeWifiSub = nullptr;
    QLabel *m_homeBtBadge = nullptr;
    QLabel *m_homeBtSub = nullptr;
    QLabel *m_homeMhBadge = nullptr;
    QLabel *m_homeMhSub = nullptr;

    // Home Bento right toggles
    QPushButton *m_dndTile = nullptr;
    QLabel *m_dndBadge = nullptr;
    QLabel *m_dndSub = nullptr;
    QToolButton *m_nightTile = nullptr;
    QToolButton *m_powerTile = nullptr;

    // Home thick capsule sliders
    QWidget *m_brightnessBlock = nullptr;
    CapsuleSlider *m_brightnessSlider = nullptr;
    QLabel *m_brightnessValLbl = nullptr;
    QTimer *m_brightnessDebounce = nullptr;

    QToolButton *m_muteBtn = nullptr;
    CapsuleSlider *m_volumeSlider = nullptr;
    QLabel *m_volumeValLbl = nullptr;
    QTimer *m_volumeDebounce = nullptr;
    bool m_muted = false;
    int m_lastVolume = 50;

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

    // Wi-Fi subpage widgets
    QToolButton *m_wifiPowerBtn = nullptr;
    QLabel *m_wifiSummaryLbl = nullptr;
    QVBoxLayout *m_wifiListLayout = nullptr;
    bool m_wifiRadioEnabled = true;
    QString m_cachedWifiSsid;
    int m_cachedWifiSignal = 0;

    // Bluetooth subpage widgets
    QToolButton *m_btPowerBtn = nullptr;
    QLabel *m_btSummaryLbl = nullptr;
    QVBoxLayout *m_btListLayout = nullptr;
    bool m_btPowered = true;
    QString m_cachedBtSummary;

    // Mihomo subpage widgets & state
    QToolButton *m_mhModeBtns[3]{}; // rule, global, direct
    QToolButton *m_mhTunBtn = nullptr;
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
