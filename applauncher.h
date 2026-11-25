#pragma once

#include "appdock.h"
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QToolButton;

// macOS Tahoe-style centered floating App Launcher popover.
// Reads the live XDG .desktop catalog & KWin running window state directly
// from AppDockModel (zero duplicated database), offers instant keyboard search,
// category filter pills, 5-column touch icon grid with running indicator dots,
// launch-frequency ranking, and click-outside / Esc auto-dismiss on Wayland.
class AppLauncherPopover : public QWidget
{
    Q_OBJECT
public:
    explicit AppLauncherPopover(AppDockModel *dockModel, QWidget *parent = nullptr);

    void reveal(const QPoint &anchorGlobal, bool toggle = true);
    void setAutoDismissEnabled(bool enabled) { m_autoDismissEnabled = enabled; }
    int visibleAppCount() const { return m_filteredApps.size(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void positionCenteredOnScreen(const QPoint &anchorGlobal);
    void reloadCatalog();
    void rebuildCategoryBar();
    void stepCategory(int delta);
    void applyFilter();
    void rebuildGrid();
    void activateApp(const DockAppEntry &entry, bool forceNewWindow = false);
    void showTileContextMenu(const DockAppEntry &entry, const QPoint &globalPos);

    void loadLaunchCounts();
    void bumpLaunchCount(const QString &key);
    int launchCountFor(const DockAppEntry &entry) const;
    static QString categoryBucketFor(const DockAppEntry &entry);

    AppDockModel *m_dockModel = nullptr;
    bool m_autoDismissEnabled = true;
    bool m_menuOpen = false;
    QElapsedTimer m_openedTimer;

    QLineEdit *m_searchEdit = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QHBoxLayout *m_catLayout = nullptr;
    QScrollArea *m_gridScroll = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QLabel *m_footerLbl = nullptr;

    QString m_activeCategory = QStringLiteral("all");
    QStringList m_visibleCategories;
    QList<DockAppEntry> m_allApps;
    QList<DockAppEntry> m_filteredApps;
    QHash<QString, int> m_launchCounts;
};
