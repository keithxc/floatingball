#include "screentext.h"
#include <QBuffer>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QThread>
#include <QElapsedTimer>

namespace ScreenText {
#ifdef Q_OS_MACOS
Result recognizeNative(const QImage &image);
#endif

namespace {
bool finishProcess(QProcess &process, int timeoutMs) {
    QElapsedTimer timeout;timeout.start();
    while(process.state()!=QProcess::NotRunning&&timeout.elapsed()<timeoutMs&&!QThread::currentThread()->isInterruptionRequested())
        process.waitForFinished(250);
    return process.state()==QProcess::NotRunning;
}
}

Result recognize(const QImage &image) {
    if (image.isNull()) return {{}, QStringLiteral("请先截取或导入一张图片"), {}};
    if (QThread::currentThread()->isInterruptionRequested()) return {{}, QStringLiteral("文字识别已取消"), {}};
#ifdef Q_OS_MACOS
    return recognizeNative(image);
#else
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (executable.isEmpty())
        return {{}, QStringLiteral("未找到本地 OCR 引擎。请安装 Tesseract 和所需语言包，然后重试。"), QStringLiteral("Tesseract")};

    QProcess languages;
    languages.start(executable, {QStringLiteral("--list-langs")});
    if (!languages.waitForStarted(5000) || !finishProcess(languages,10000)) {
        languages.kill(); languages.waitForFinished(1000);
        return {{}, QThread::currentThread()->isInterruptionRequested()?QStringLiteral("文字识别已取消"):QStringLiteral("无法读取 Tesseract 语言包，请检查本地安装。"), QStringLiteral("Tesseract")};
    }
    QStringList available;
    const auto lines = QString::fromUtf8(languages.readAllStandardOutput()).split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString language = line.trimmed();
        if (QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(language).hasMatch() && language != QStringLiteral("osd"))
            available.append(language);
    }
    if (languages.exitStatus() != QProcess::NormalExit || languages.exitCode() != 0 || available.isEmpty())
        return {{}, QStringLiteral("Tesseract 没有可用的识别语言包。请安装 eng、chi_sim 或所需语言包。"), QStringLiteral("Tesseract")};
    QStringList selected;
    for (const QString &language : {QStringLiteral("chi_sim"), QStringLiteral("chi_tra"), QStringLiteral("eng")})
        if (available.contains(language)) selected.append(language);
    if (selected.isEmpty()) selected.append(available.first());

    QByteArray png;
    QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {{}, QStringLiteral("无法准备识别图片，请重试。"), QStringLiteral("Tesseract")};

    QProcess process;
    process.start(executable, {QStringLiteral("stdin"), QStringLiteral("stdout"), QStringLiteral("-l"), selected.join('+')});
    if (!process.waitForStarted(5000))
        return {{}, QStringLiteral("无法启动 Tesseract：") + process.errorString(), QStringLiteral("Tesseract")};
    process.write(png);
    process.closeWriteChannel();
    if (!finishProcess(process,40000)) {
        process.kill(); process.waitForFinished(1000);
        return {{}, QThread::currentThread()->isInterruptionRequested()?QStringLiteral("文字识别已取消"):QStringLiteral("文字识别超时，请裁剪较小区域后重试。"), QStringLiteral("Tesseract")};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {{}, QStringLiteral("文字识别失败：") + QString::fromUtf8(process.readAllStandardError()).trimmed().left(400), QStringLiteral("Tesseract")};
    const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return {text, text.isEmpty() ? QStringLiteral("没有识别到文字。可尝试放大原图或截取更清晰的区域。") : QString(), QStringLiteral("Tesseract · ") + selected.join('+')};
#endif
}
}
