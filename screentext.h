#pragma once

#include <QImage>
#include <QString>

namespace ScreenText {
struct Result {
    QString text;
    QString error;
    QString provider;
};

// Blocking recognition. Call from a worker, never from the UI thread.
// Pixels stay on this computer; web actions are separate user actions.
Result recognize(const QImage &image);
}
