#pragma once
#include <QImage>
#include <QString>

namespace ScreenshotStore {
QString defaultDirectory();
bool copyImage(const QImage &image, QString *error);
// Writes a new PNG atomically. Never overwrites an existing screenshot.
QString importImage(const QImage &image, const QString &directory, QString *error);
QString saveCapture(const QImage &image, const QString &directory, QString *error);
}
