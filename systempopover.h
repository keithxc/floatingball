#pragma once

#include <QColor>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

class QLabel;
class QNetworkAccessManager;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;
class SystemStatusMonitor;

class SystemPopover : public QWidget
{
    Q_OBJECT
public:
    enum Tab {
        TabWifi = 0,
        TabBluetooth = 1,
        TabMihomo = 2
    };

    explicit SystemPopover(SystemStatusMonitor *monitor, QWidget *parent = nullptr);

    void revealTab(Tab tab, const QPoint &anchorGlobal);
    void refreshCurrentTab();

protected:
    void keyPressEvent(QKeyEvent *event) override;

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

    void selectTab(Tab tab);
    void positionNear(const QPoint &anchorGlobal);
    void showStatusBanner(const QString &msg, bool isError = false);

    // Wi-Fi async actions
    void refreshWifi();
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
    Tab m_currentTab = TabWifi;
    QStackedWidget *m_stack = nullptr;
    QToolButton *m_tabBtns[3]{};
    QLabel *m_statusBanner = nullptr;

    // Wi-Fi widgets
    QToolButton *m_wifiPowerBtn = nullptr;
    QLabel *m_wifiSummaryLbl = nullptr;
    QVBoxLayout *m_wifiListLayout = nullptr;
    bool m_wifiRadioEnabled = true;

    // Bluetooth widgets
    QToolButton *m_btPowerBtn = nullptr;
    QLabel *m_btSummaryLbl = nullptr;
    QVBoxLayout *m_btListLayout = nullptr;
    bool m_btPowered = true;

    // Mihomo widgets & state
    QToolButton *m_mhModeBtns[3]{}; // rule, global, direct
    QToolButton *m_mhTunBtn = nullptr;
    QToolButton *m_mhDelayBtn = nullptr;
    QLabel *m_mhSummaryLbl = nullptr;
    QVBoxLayout *m_mhGroupBarLayout = nullptr;
    QVBoxLayout *m_mhNodeListLayout = nullptr;
    QString m_mhMode = QStringLiteral("rule");
    bool m_mhTunEnabled = false;
    QString m_mhTunDevice;
    QList<MihomoGroup> m_mhGroups;
    QString m_mhSelectedGroup;
};
