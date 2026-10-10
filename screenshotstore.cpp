#include "screenshotstore.h"
#include <QDateTime>
#include <QBuffer>
#include <QClipboard>
#include <QColorSpace>
#include <QGuiApplication>
#include <QWindow>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cstring>
#include <limits>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

bool ScreenshotStore::copyImage(const QImage &image, QString *error)
{
    if (error) error->clear();
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (image.isNull()) return fail(QStringLiteral("没有可复制的图片"));
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        // Qt 6.8's DIBV5 writer appends duplicate masks after its V5 header.
        // Native consumers then read pixels 12 bytes early. Publish a standard
        // V5 DIB directly, plus lossless PNG for alpha and color-profile readers.
        QWindow *owner = QGuiApplication::focusWindow();
        if (!owner) for (QWindow *window : QGuiApplication::topLevelWindows())
            if (window->isVisible()) { owner = window; break; }
        if (!owner) return fail(QStringLiteral("无法访问剪贴板窗口"));
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG")) return fail(QStringLiteral("图片编码失败"));
        QImage pixels = image.colorSpace().isValid()
            ? image.convertedToColorSpace(QColorSpace::SRgb) : image;
        pixels = pixels.convertToFormat(QImage::Format_ARGB32);
        const quint64 bytes = quint64(pixels.width()) * pixels.height() * 4;
        if (bytes > std::numeric_limits<DWORD>::max()) return fail(QStringLiteral("图片过大，无法复制"));
        BITMAPV5HEADER header = {};
        header.bV5Size = sizeof(header); header.bV5Width = pixels.width();
        header.bV5Height = -pixels.height(); header.bV5Planes = 1; header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS; header.bV5SizeImage = DWORD(bytes);
        header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00;
        header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
        header.bV5CSType = LCS_sRGB; header.bV5Intent = LCS_GM_IMAGES;
        HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE,SIZE_T(sizeof(header)+bytes));
        HGLOBAL encoded = GlobalAlloc(GMEM_MOVEABLE,SIZE_T(png.size()));
        void *dibData = dib ? GlobalLock(dib) : nullptr;
        void *pngData = encoded ? GlobalLock(encoded) : nullptr;
        if (!dibData || !pngData) {
            if (dibData) GlobalUnlock(dib); if (pngData) GlobalUnlock(encoded);
            if (dib) GlobalFree(dib); if (encoded) GlobalFree(encoded);
            return fail(QStringLiteral("剪贴板内存不足"));
        }
        std::memcpy(dibData,&header,sizeof(header));
        for (int y=0;y<pixels.height();++y)
            std::memcpy(static_cast<char *>(dibData)+sizeof(header)+quint64(y)*pixels.width()*4,
                        pixels.constScanLine(y),size_t(pixels.width())*4);
        std::memcpy(pngData,png.constData(),size_t(png.size()));
        GlobalUnlock(dib); GlobalUnlock(encoded);
        const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        if (!pngFormat || !OpenClipboard(reinterpret_cast<HWND>(owner->winId()))) {
            GlobalFree(dib); GlobalFree(encoded);
            return fail(QStringLiteral("剪贴板正忙，请重试"));
        }
        const bool emptied = EmptyClipboard();
        const bool pngSet = emptied && SetClipboardData(pngFormat,encoded);
        const bool dibSet = emptied && SetClipboardData(CF_DIBV5,dib);
        CloseClipboard();
        if (!pngSet) GlobalFree(encoded); if (!dibSet) GlobalFree(dib);
        return pngSet && dibSet ? true : fail(QStringLiteral("图片复制失败，请重试"));
    }
#endif
    QGuiApplication::clipboard()->setImage(image);
    return true;
}

QString ScreenshotStore::defaultDirectory()
{
#ifdef Q_OS_MAC
    QProcess process;
    process.start(QStringLiteral("/usr/bin/defaults"),
                  {QStringLiteral("read"), QStringLiteral("com.apple.screencapture"), QStringLiteral("location")});
    QString configured;
    if (process.waitForFinished(1000) && process.exitCode() == 0)
        configured = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    else if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
    }
    if (configured.startsWith(QStringLiteral("~/"))) configured.replace(0, 1, QDir::homePath());
    if (!configured.isEmpty() && QDir::isAbsolutePath(configured)) return QDir::cleanPath(configured);
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
#elif defined(Q_OS_WIN)
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Screenshots, KF_FLAG_DONT_VERIFY, nullptr, &path);
    const QString configured = SUCCEEDED(result) && path ? QString::fromWCharArray(path) : QString();
    CoTaskMemFree(path);
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
#endif
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) pictures = QDir::homePath() + QStringLiteral("/Pictures");
    // Linux screenshot tools have individual settings. These are folder candidates,
    // not an assumption about a compositor's current capture destination.
    for (const QString &name : {QStringLiteral("Screenshots"), QStringLiteral("截图"), QStringLiteral("截屏")}) {
        const QString candidate = QDir(pictures).filePath(name);
        if (QFileInfo(candidate).isDir()) return candidate;
    }
    return QDir(pictures).filePath(QStringLiteral("Screenshots"));
}

QString ScreenshotStore::importImage(const QImage &image, const QString &directory, QString *error)
{
    if (error) error->clear();
    auto fail = [error](const QString &message) { if (error) *error = message; return QString(); };
    if (image.isNull()) return fail(QStringLiteral("剪贴板中没有图片"));
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory))
        return fail(QStringLiteral("请先选择有效的截图目录"));
    if (!QDir().mkpath(directory)) return fail(QStringLiteral("无法创建截图目录，请选择可写目录"));
    const QString name = QStringLiteral("Clipboard-%1-%2.png")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmsszzz")),
             QUuid::createUuid().toString(QUuid::Id128));
    const QString path = QDir(directory).absoluteFilePath(name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("无法写入截图目录：") + file.errorString());
    QImageWriter writer(&file, "png");
    if (!writer.write(image)) return fail(QStringLiteral("图片保存失败：") + writer.errorString());
    if (!file.commit()) return fail(QStringLiteral("图片保存失败：") + file.errorString());
    return path;
}
