#include "desktopactions.h"
#include "floatingwindow.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QWidget>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#endif

namespace DesktopActions {

#ifdef Q_OS_MACOS
Outcome macSendKeyChord(int virtualKey, bool cmd, bool ctrl, bool alt, bool shift, const QString &label);
Outcome macShowOverview();
Outcome macShowDesktop();
Outcome macSwitchInputMethod();
Outcome macToggleVirtualKeyboard();
bool macAccessibilityTrusted(bool promptUser);
void macQueryVolume(std::function<void(int, bool, bool)> callback);
void macSetVolume(int percent);
void macToggleMute(std::function<void(int, bool, bool)> callback);
void macQueryBrightness(std::function<void(int, bool)> callback);
void macSetBrightness(int percent);
Outcome macTriggerNativeScreenshot();
Outcome macTriggerNativeScreenRecord();
Outcome macTriggerNativeScrollCapture();
#endif

namespace {

class FloatingToast final : public QWidget {
public:
    FloatingToast()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint |
                           Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput)
    {
        setObjectName(QStringLiteral("floatingballToast"));
        setProperty("floatingballSurface", true);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_MacAlwaysShowToolWindow);

        m_anim = new QVariantAnimation(this);
        connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_progress = v.toReal();
            move(m_targetPos + QPoint(0, qRound((1.0 - m_progress) * -8.0)));
            update();
        });
        connect(m_anim, &QVariantAnimation::finished, this, [this] {
            if (!m_visibleWanted) hide();
        });
        m_hideTimer = new QTimer(this);
        m_hideTimer->setSingleShot(true);
        connect(m_hideTimer, &QTimer::timeout, this, [this] {
            m_visibleWanted = false;
            animateTo(0.0, 180);
        });
        FloatingWindow::prepare(this, false);
    }

    void present(const QString &text, const QPoint &anchor)
    {
        if (text.isEmpty() || QGuiApplication::platformName() == QStringLiteral("offscreen")) return;
        m_text = text;
        QFont f = font();
        f.setPointSize(12);
        f.setWeight(QFont::DemiBold);
        setFont(f);

        const int w = qBound(140, fontMetrics().horizontalAdvance(m_text) + 38, 520);
        const int h = qMax(36, fontMetrics().height() + 18);
        resize(w, h);

        QScreen *screen = anchor.isNull() ? nullptr : QGuiApplication::screenAt(anchor);
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect bounds = screen->availableGeometry();
            m_targetPos = QPoint(bounds.center().x() - w / 2, bounds.top() + 18);
        }
        m_visibleWanted = true;
        if (!isVisible()) {
            m_progress = 0.0;
            move(m_targetPos + QPoint(0, -8));
            show();
        }
        raise();
        animateTo(1.0, 160);
        m_hideTimer->start(1650);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(qBound(0.0, m_progress, 1.0));
        const QRectF pill = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        p.setPen(QPen(QColor(255, 255, 255, 58), 1.0));
        p.setBrush(QColor(20, 23, 30, 232));
        p.drawRoundedRect(pill, pill.height() / 2.0, pill.height() / 2.0);
        p.setPen(QColor("#f4f7fc"));
        p.setFont(font());
        p.drawText(rect().adjusted(14, 0, -14, 0), Qt::AlignCenter,
                   fontMetrics().elidedText(m_text, Qt::ElideRight, width() - 28));
    }

private:
    void animateTo(qreal end, int duration)
    {
        m_anim->stop();
        m_anim->setStartValue(m_progress);
        m_anim->setEndValue(end);
        m_anim->setDuration(duration);
        m_anim->setEasingCurve(end > m_progress ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
        m_anim->start();
    }
    QVariantAnimation *m_anim = nullptr;
    QTimer *m_hideTimer = nullptr;
    QString m_text;
    QPoint m_targetPos;
    qreal m_progress = 0.0;
    bool m_visibleWanted = false;
};

#ifdef Q_OS_LINUX
class SafeProcess final : public QProcess {
public:
    using QProcess::QProcess;
    ~SafeProcess() override
    {
        if (state() != QProcess::NotRunning) {
            kill();
            waitForFinished(300);
        }
    }
};

bool invokeKWinShortcut(const QStringList &candidates)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    for (const QString &shortcut : candidates) {
        auto msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"),
            QStringLiteral("/component/kwin"),
            QStringLiteral("org.kde.kglobalaccel.Component"),
            QStringLiteral("invokeShortcut"));
        msg.setArguments({shortcut});
        const QDBusMessage reply = bus.call(msg, QDBus::Block, 600);
        if (reply.type() != QDBusMessage::ErrorMessage) return true;
    }
    return false;
}

Outcome linuxSendShortcut(const QString &wtypeMod, const QString &wtypeKey,
                          const QString &xdotoolChord, const QString &label)
{
    const bool wayland = QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
    if (wayland) {
        const QString wtype = QStandardPaths::findExecutable(QStringLiteral("wtype"));
        if (!wtype.isEmpty()) {
            QStringList args;
            if (!wtypeMod.isEmpty()) args << QStringLiteral("-M") << wtypeMod;
            args << QStringLiteral("-k") << wtypeKey;
            if (!wtypeMod.isEmpty()) args << QStringLiteral("-m") << wtypeMod;
            if (QProcess::startDetached(wtype, args))
                return {true, QStringLiteral("✓ ") + label};
        }
    }
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    if (!xdotool.isEmpty() && !wayland) {
        if (QProcess::startDetached(xdotool, {QStringLiteral("key"), QStringLiteral("--clearmodifiers"), xdotoolChord}))
            return {true, QStringLiteral("✓ ") + label};
    }
    return {false, label + QStringLiteral("：当前桌面缺少虚拟按键工具（可装 wtype）")};
}
#endif

} // namespace

void showToast(const QString &message, const QPoint &anchor)
{
    static QPointer<FloatingToast> toast;
    if (!toast && qApp) {
        toast = new FloatingToast();
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, toast, &QObject::deleteLater);
    }
    if (toast) toast->present(message, anchor);
}

bool accessibilityTrusted(bool promptUser)
{
#ifdef Q_OS_MACOS
    return macAccessibilityTrusted(promptUser);
#else
    Q_UNUSED(promptUser);
    return true;
#endif
}

Outcome navigateBack()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_LeftBracket = 0x21 -> Cmd + [
    return macSendKeyChord(0x21, true, false, false, false, QStringLiteral("返回上一页"));
#elif defined(Q_OS_LINUX)
    return linuxSendShortcut(QStringLiteral("alt"), QStringLiteral("Left"),
                             QStringLiteral("Alt+Left"), QStringLiteral("返回上一页"));
#else
    return {false, QStringLiteral("当前平台暂未接入全局返回")};
#endif
}

Outcome navigateForward()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_RightBracket = 0x1E -> Cmd + ]
    return macSendKeyChord(0x1E, true, false, false, false, QStringLiteral("前进下一页"));
#elif defined(Q_OS_LINUX)
    return linuxSendShortcut(QStringLiteral("alt"), QStringLiteral("Right"),
                             QStringLiteral("Alt+Right"), QStringLiteral("前进下一页"));
#else
    return {false, QStringLiteral("当前平台暂未接入全局前进")};
#endif
}

Outcome showOverview()
{
#ifdef Q_OS_MACOS
    return macShowOverview();
#elif defined(Q_OS_LINUX)
    if (invokeKWinShortcut({QStringLiteral("Overview"),
                            QStringLiteral("ExposeAll"),
                            QStringLiteral("ShowDesktopGrid")})) {
        return {true, QStringLiteral("✓ 已展开多任务全览")};
    }
    return {false, QStringLiteral("无法唤起桌面全览（需 KDE KWin Overview）")};
#else
    return {false, QStringLiteral("当前平台暂未接入多任务全览")};
#endif
}

Outcome showDesktop()
{
#ifdef Q_OS_MACOS
    return macShowDesktop();
#elif defined(Q_OS_LINUX)
    if (invokeKWinShortcut({QStringLiteral("Show Desktop")})) {
        return {true, QStringLiteral("✓ 显示桌面")};
    }
    return {false, QStringLiteral("无法切换显示桌面")};
#else
    return {false, QStringLiteral("当前平台暂未接入显示桌面")};
#endif
}

Outcome nextTab()
{
#ifdef Q_OS_MACOS
    // kVK_Tab = 0x30 -> Ctrl + Tab
    return macSendKeyChord(0x30, false, true, false, false, QStringLiteral("切换下一个标签"));
#elif defined(Q_OS_LINUX)
    return linuxSendShortcut(QStringLiteral("ctrl"), QStringLiteral("Tab"),
                             QStringLiteral(" ctrl+Tab"), QStringLiteral("切换下一个标签"));
#else
    return {false, QStringLiteral("当前平台暂未接入标签切换")};
#endif
}

Outcome closeActiveWindow()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_W = 0x0D -> Cmd + W
    return macSendKeyChord(0x0D, true, false, false, false, QStringLiteral("关闭当前标签 / 窗口"));
#elif defined(Q_OS_LINUX)
    if (invokeKWinShortcut({QStringLiteral("Window Close")})) {
        return {true, QStringLiteral("✓ 已关闭当前窗口")};
    }
    return linuxSendShortcut(QStringLiteral("ctrl"), QStringLiteral("w"),
                             QStringLiteral("ctrl+w"), QStringLiteral("关闭当前标签"));
#else
    return {false, QStringLiteral("当前平台暂未接入窗口关闭")};
#endif
}

Outcome minimizeActiveWindow()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_M = 0x2E -> Cmd + M
    return macSendKeyChord(0x2E, true, false, false, false, QStringLiteral("最小化当前窗口"));
#elif defined(Q_OS_LINUX)
    if (invokeKWinShortcut({QStringLiteral("Window Minimize")})) {
        return {true, QStringLiteral("✓ 已最小化当前窗口")};
    }
    return {false, QStringLiteral("无法最小化当前窗口")};
#else
    return {false, QStringLiteral("当前平台暂未接入窗口最小化")};
#endif
}

Outcome toggleFullscreenActiveWindow()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_F = 0x03 -> Ctrl + Cmd + F
    return macSendKeyChord(0x03, true, true, false, false, QStringLiteral("切换全屏窗口"));
#elif defined(Q_OS_LINUX)
    if (invokeKWinShortcut({QStringLiteral("Window Maximize"), QStringLiteral("Window Fullscreen")})) {
        return {true, QStringLiteral("✓ 已切换窗口最大化")};
    }
    return {false, QStringLiteral("无法切换窗口最大化")};
#else
    return {false, QStringLiteral("当前平台暂未接入全屏切换")};
#endif
}

Outcome copySelection()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_C = 0x08 -> Cmd + C
    return macSendKeyChord(0x08, true, false, false, false, QStringLiteral("复制所选内容"));
#elif defined(Q_OS_LINUX)
    return linuxSendShortcut(QStringLiteral("ctrl"), QStringLiteral("c"),
                             QStringLiteral("ctrl+c"), QStringLiteral("复制所选内容"));
#else
    return {false, QStringLiteral("当前平台暂未接入自动复制")};
#endif
}

Outcome pasteClipboard()
{
#ifdef Q_OS_MACOS
    // kVK_ANSI_V = 0x09 -> Cmd + V
    return macSendKeyChord(0x09, true, false, false, false, QStringLiteral("已粘贴到当前窗口"));
#elif defined(Q_OS_LINUX)
    return linuxSendShortcut(QStringLiteral("ctrl"), QStringLiteral("v"),
                             QStringLiteral("ctrl+v"), QStringLiteral("已粘贴到当前窗口"));
#else
    return {false, QStringLiteral("已复制到剪贴板，请按 Ctrl+V 粘贴")};
#endif
}

Outcome switchInputMethod()
{
#ifdef Q_OS_MACOS
    return macSwitchInputMethod();
#elif defined(Q_OS_LINUX)
    const QString fcitx = QStandardPaths::findExecutable(QStringLiteral("fcitx5-remote"));
    if (!fcitx.isEmpty() && QProcess::startDetached(fcitx, {QStringLiteral("-t")})) {
        return {true, QStringLiteral("✓ 已切换输入法")};
    }
    return linuxSendShortcut(QStringLiteral("ctrl"), QStringLiteral("space"),
                             QStringLiteral("ctrl+space"), QStringLiteral("切换输入法"));
#else
    return {false, QStringLiteral("当前平台暂未接入输入法切换")};
#endif
}

Outcome toggleVirtualKeyboard()
{
#ifdef Q_OS_MACOS
    return macToggleVirtualKeyboard();
#elif defined(Q_OS_LINUX)
    for (const QString &candidate : {QStringLiteral("qtvirtualkeyboard"), QStringLiteral("onboard"), QStringLiteral("maliit-keyboard")}) {
        const QString exe = QStandardPaths::findExecutable(candidate);
        if (!exe.isEmpty() && QProcess::startDetached(exe, {}))
            return {true, QStringLiteral("✓ 已唤起屏幕键盘")};
    }
    return {false, QStringLiteral("未检测到已安装的屏幕软键盘")};
#else
    return {false, QStringLiteral("当前平台暂未接入屏幕键盘")};
#endif
}

Outcome openTerminal()
{
#ifdef Q_OS_MACOS
    if (QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal")}))
        return {true, QStringLiteral("✓ 已打开终端")};
#elif defined(Q_OS_WIN)
    if (QProcess::startDetached(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("cmd")}))
        return {true, QStringLiteral("✓ 已打开终端")};
#else
    for (const QString &candidate : {QStringLiteral("konsole"), QStringLiteral("kitty"),
                                     QStringLiteral("alacritty"), QStringLiteral("gnome-terminal"),
                                     QStringLiteral("x-terminal-emulator"), QStringLiteral("xterm")}) {
        const QString exe = QStandardPaths::findExecutable(candidate);
        if (!exe.isEmpty() && QProcess::startDetached(exe, {}))
            return {true, QStringLiteral("✓ 已打开终端")};
    }
#endif
    return {false, QStringLiteral("没有找到可用的终端程序")};
}

Outcome openBrowser()
{
    if (QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.google.com"))))
        return {true, QStringLiteral("✓ 已打开默认浏览器")};
    return {false, QStringLiteral("无法打开默认浏览器")};
}

Outcome openHomeFolder()
{
    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(home)))
        return {true, QStringLiteral("✓ 已打开主目录")};
    return {false, QStringLiteral("无法打开主目录")};
}

Outcome openDownloadsFolder()
{
    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty())
        downloads = QDir::homePath() + QStringLiteral("/Downloads");
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(downloads)))
        return {true, QStringLiteral("✓ 已打开下载目录")};
    return {false, QStringLiteral("无法打开下载目录")};
}

Outcome openTrash()
{
#ifdef Q_OS_MACOS
    if (QProcess::startDetached(
            QStringLiteral("/usr/bin/osascript"),
            {QStringLiteral("-e"), QStringLiteral("tell application \"Finder\" to open trash"),
             QStringLiteral("-e"), QStringLiteral("tell application \"Finder\" to activate")})) {
        return {true, QStringLiteral("✓ 已打开废纸篓")};
    }
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QDir::homePath() + QStringLiteral("/.Trash")}))
        return {true, QStringLiteral("✓ 已打开废纸篓")};
    return {false, QStringLiteral("无法打开废纸篓")};
#elif defined(Q_OS_WIN)
    if (QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("shell:RecycleBinFolder")}))
        return {true, QStringLiteral("✓ 已打开回收站")};
    return {false, QStringLiteral("无法打开回收站")};
#else
    if (QDesktopServices::openUrl(QUrl(QStringLiteral("trash:///"))))
        return {true, QStringLiteral("✓ 已打开回收站")};
    return {false, QStringLiteral("无法打开回收站")};
#endif
}

Outcome triggerNativeScreenshot()
{
#ifdef Q_OS_MACOS
    return macTriggerNativeScreenshot();
#elif defined(Q_OS_LINUX)
    // Match KDE Plasma's native PrtSc (_launch of org.kde.spectacle.desktop)
    // so users get their exact familiar Spectacle overlay (screenshot + recording).
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty() && QProcess::startDetached(spectacle, {})) {
        return {true, {}};
    }
    const QString flameshot = QStandardPaths::findExecutable(QStringLiteral("flameshot"));
    if (!flameshot.isEmpty() && QProcess::startDetached(flameshot, {QStringLiteral("gui")})) {
        return {true, {}};
    }
    const QString gnomeShot = QStandardPaths::findExecutable(QStringLiteral("gnome-screenshot"));
    if (!gnomeShot.isEmpty() && QProcess::startDetached(gnomeShot, {QStringLiteral("-a")})) {
        return {true, {}};
    }
    return {false, QStringLiteral("未找到系统截图工具（建议安装 KDE Spectacle 或 Flameshot）")};
#elif defined(Q_OS_WIN)
    if (QDesktopServices::openUrl(QUrl(QStringLiteral("ms-screenclip:"))))
        return {true, {}};
    return {false, QStringLiteral("无法启动 Windows 截图工具")};
#else
    return {false, QStringLiteral("当前系统暂不支持快捷截图")};
#endif
}

Outcome triggerNativeScreenRecord()
{
#ifdef Q_OS_MACOS
    return macTriggerNativeScreenRecord();
#elif defined(Q_OS_LINUX)
    // Match KDE Plasma's PrtSc Spectacle entry so users can pick full-screen or
    // region recording directly inside Spectacle's native UI.
    const QString spectacle = QStandardPaths::findExecutable(QStringLiteral("spectacle"));
    if (!spectacle.isEmpty() && QProcess::startDetached(spectacle, {})) {
        return {true, {}};
    }
    const QString obs = QStandardPaths::findExecutable(QStringLiteral("obs"));
    if (!obs.isEmpty() && QProcess::startDetached(obs, {})) {
        return {true, QStringLiteral("✓ 已启动 OBS 录屏")};
    }
    return {false, QStringLiteral("未找到系统录屏工具（建议安装 KDE Spectacle）")};
#else
    return {false, QStringLiteral("当前系统暂不支持快捷录屏")};
#endif
}

Outcome triggerNativeScrollCapture()
{
#ifdef Q_OS_MACOS
    return macTriggerNativeScrollCapture();
#else
    return triggerNativeScreenshot();
#endif
}

void queryVolume(std::function<void(int, bool, bool)> callback)
{
    if (!callback) return;
#ifdef Q_OS_MACOS
    macQueryVolume(std::move(callback));
#elif defined(Q_OS_LINUX)
    const QString wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    if (!wpctl.isEmpty()) {
        auto proc = new SafeProcess(qApp);
        QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), qApp,
                         [proc, callback](int code, QProcess::ExitStatus status) {
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            proc->deleteLater();
            if (status != QProcess::NormalExit || code != 0) {
                callback(50, false, false);
                return;
            }
            // Output format: "Volume: 0.65" or "Volume: 0.65 [MUTED]"
            const QRegularExpression re(QStringLiteral("([0-9]+\\.[0-9]+)"));
            const auto match = re.match(out);
            const int vol = match.hasMatch() ? qBound(0, qRound(match.captured(1).toDouble() * 100.0), 100) : 50;
            callback(vol, out.contains(QStringLiteral("MUTED")), true);
        });
        proc->start(wpctl, {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
        return;
    }
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        auto proc = new QProcess(qApp);
        QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), qApp,
                         [proc, callback](int code, QProcess::ExitStatus status) {
            const QString out = QString::fromUtf8(proc->readAllStandardOutput());
            proc->deleteLater();
            if (status != QProcess::NormalExit || code != 0) {
                callback(50, false, false);
                return;
            }
            const QRegularExpression re(QStringLiteral("([0-9]+)%"));
            const auto match = re.match(out);
            const int vol = match.hasMatch() ? qBound(0, match.captured(1).toInt(), 100) : 50;
            callback(vol, false, true);
        });
        proc->start(pactl, {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
        return;
    }
    callback(50, false, false);
#else
    callback(50, false, false);
#endif
}

void setVolume(int percent)
{
    percent = qBound(0, percent, 100);
#ifdef Q_OS_MACOS
    macSetVolume(percent);
#elif defined(Q_OS_LINUX)
    const QString wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    if (!wpctl.isEmpty()) {
        QProcess::startDetached(wpctl, {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("0")});
        QProcess::startDetached(wpctl, {QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("%1%").arg(percent)});
        return;
    }
    const QString pactl = QStandardPaths::findExecutable(QStringLiteral("pactl"));
    if (!pactl.isEmpty()) {
        QProcess::startDetached(pactl, {QStringLiteral("set-sink-mute"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("0")});
        QProcess::startDetached(pactl, {QStringLiteral("set-sink-volume"), QStringLiteral("@DEFAULT_SINK@"), QStringLiteral("%1%").arg(percent)});
    }
#endif
}

void toggleMute(std::function<void(int, bool, bool)> callback)
{
#ifdef Q_OS_MACOS
    macToggleMute(std::move(callback));
#elif defined(Q_OS_LINUX)
    const QString wpctl = QStandardPaths::findExecutable(QStringLiteral("wpctl"));
    if (!wpctl.isEmpty()) {
        auto proc = new SafeProcess(qApp);
        QObject::connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), qApp,
                         [proc, callback](int, QProcess::ExitStatus) {
            proc->deleteLater();
            if (callback) queryVolume(callback);
        });
        proc->start(wpctl, {QStringLiteral("set-mute"), QStringLiteral("@DEFAULT_AUDIO_SINK@"), QStringLiteral("toggle")});
        return;
    }
    if (callback) callback(50, false, false);
#else
    if (callback) callback(50, false, false);
#endif
}

void queryBrightness(std::function<void(int, bool)> callback)
{
    if (!callback) return;
#ifdef Q_OS_MACOS
    macQueryBrightness(std::move(callback));
#elif defined(Q_OS_LINUX)
    auto session = QDBusConnection::sessionBus();
    auto maxMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Solid.PowerManagement"),
        QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
        QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
        QStringLiteral("brightnessMax"));
    const QDBusMessage maxReply = session.call(maxMsg, QDBus::Block, 300);
    if (maxReply.type() == QDBusMessage::ReplyMessage && !maxReply.arguments().isEmpty()) {
        const int maxVal = maxReply.arguments().first().toInt();
        if (maxVal > 0) {
            auto curMsg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.Solid.PowerManagement"),
                QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                QStringLiteral("brightness"));
            const QDBusMessage curReply = session.call(curMsg, QDBus::Block, 300);
            if (curReply.type() == QDBusMessage::ReplyMessage && !curReply.arguments().isEmpty()) {
                const int cur = curReply.arguments().first().toInt();
                callback(qBound(0, qRound(cur * 100.0 / maxVal), 100), true);
                return;
            }
        }
    }
    callback(50, false);
#else
    callback(50, false);
#endif
}

void setBrightness(int percent)
{
    percent = qBound(0, percent, 100);
#ifdef Q_OS_MACOS
    macSetBrightness(percent);
#elif defined(Q_OS_LINUX)
    auto session = QDBusConnection::sessionBus();
    auto maxMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.Solid.PowerManagement"),
        QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
        QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
        QStringLiteral("brightnessMax"));
    const QDBusMessage maxReply = session.call(maxMsg, QDBus::Block, 300);
    if (maxReply.type() == QDBusMessage::ReplyMessage && !maxReply.arguments().isEmpty()) {
        const int maxVal = maxReply.arguments().first().toInt();
        if (maxVal > 0) {
            auto setMsg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.Solid.PowerManagement"),
                QStringLiteral("/org/kde/Solid/PowerManagement/Actions/BrightnessControl"),
                QStringLiteral("org.kde.Solid.PowerManagement.Actions.BrightnessControl"),
                QStringLiteral("setBrightness"));
            setMsg.setArguments({qRound(percent * maxVal / 100.0)});
            session.asyncCall(setMsg);
        }
    }
#else
    Q_UNUSED(percent);
#endif
}

} // namespace DesktopActions
