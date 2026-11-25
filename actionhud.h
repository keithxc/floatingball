#pragma once

#include <QImage>
#include <QPoint>
#include <QPointer>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QThread;
class QToolButton;

// fooView-inspired instant floating Action HUD card presented right beside a
// crosshair or region capture, with automatic background local OCR and 1-tap
// touch actions (Copy Image, Copy OCR Text, Pin Reference, Annotate, Save).
class ActionResultHud : public QWidget {
    Q_OBJECT
public:
    explicit ActionResultHud(QWidget *parent = nullptr);
    ~ActionResultHud() override;

    void presentCapture(const QImage &image, const QString &directory, const QPoint &anchor);
    QImage currentImage() const { return m_image; }
    QString recognizedText() const;
    bool isOcrRunning() const { return m_ocrWorker != nullptr; }

signals:
    void pinRequested(const QImage &image, const QString &name, const QPoint &anchor);
    void annotateRequested(const QImage &image, const QString &savedPath, const QPoint &anchor);
    void clipboardSaved(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void startBackgroundOcr();
    void stopOcrWorker();
    void copyImageNow();
    void copyTextNow();
    void savePngNow();
    void searchOcrText();
    void translateOcrText();

    QImage m_image;
    QString m_directory;
    QString m_savedPath;
    QPoint m_anchor;
    bool m_copyTextWhenReady = false;
    qint64 m_ocrImageKey = 0;

    QLabel *m_preview = nullptr;
    QLabel *m_metaLabel = nullptr;
    QLabel *m_ocrBadge = nullptr;
    QWidget *m_textContainer = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QToolButton *m_copyImgBtn = nullptr;
    QToolButton *m_copyTxtBtn = nullptr;
    QToolButton *m_pinBtn = nullptr;
    QToolButton *m_annotateBtn = nullptr;
    QToolButton *m_saveBtn = nullptr;
    QPointer<QThread> m_ocrWorker;
};
