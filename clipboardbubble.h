#pragma once

#include <QImage>
#include <QList>
#include <QPoint>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QToolButton;

// Touch-first Clipboard & Quick-Paste Bubble inspired by fooView's pinboard.
// Non-activating by default so tapping Paste injects directly into the user's
// frontmost window without stealing focus.
class ClipboardBubble : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString text;
        QImage image;
        bool pinned = false;
    };

    explicit ClipboardBubble(QWidget *parent = nullptr);

    void reveal(const QPoint &anchor, bool toggle = true);
    void recordText(const QString &text, bool pinned = false);
    int itemCount() const { return m_items.size(); }
    bool isRecordingEnabled() const;
    void setRecordingEnabled(bool enabled);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void observeSystemClipboard();
    void refreshList();
    void copySelected(bool pasteIntoWindow);
    void togglePinSelected();
    void removeSelected();
    void clearUnpinned();
    void addQuickScratch();

    QList<Item> m_items;
    bool m_internalCopy = false;
    QCheckBox *m_recordToggle = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QLineEdit *m_scratchEdit = nullptr;
    QWidget *m_scratchRow = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_pasteNowBtn = nullptr;
};
