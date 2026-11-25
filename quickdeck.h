#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;
class QSlider;
class QTimer;
class QToolButton;

// Touch-first Control Deck anchored beside FloatingBall. Non-activating so
// every large 48pt+ touch tile acts directly on the currently active window
// (replacing tiny 12pt macOS traffic lights & multi-key desktop shortcuts).
class QuickDeck : public QWidget {
    Q_OBJECT
public:
    explicit QuickDeck(QWidget *parent = nullptr);

    void reveal(const QPoint &anchor, bool toggle = true);

signals:
    void captureRequested();
    void recordRequested();
    void settingsRequested();
    void audioStateChanged(int percent, bool muted);
    void brightnessStateChanged(int percent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void syncVolume();
    void syncBrightness();
    void applyAudioVisuals(int percent, bool muted, bool supported);

    QSlider *m_volumeSlider = nullptr;
    QLabel *m_volumeValue = nullptr;
    QToolButton *m_muteBtn = nullptr;
    QTimer *m_volumeDebounce = nullptr;
    QWidget *m_brightnessRow = nullptr;
    QSlider *m_brightnessSlider = nullptr;
    QLabel *m_brightnessValue = nullptr;
    QTimer *m_brightnessDebounce = nullptr;
    bool m_muted = false;
    int m_lastVolume = 60;
};
