#include "applauncher.h"
#include "desktopactions.h"
#include "floatingwindow.h"
#include "uiicons.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

class LauncherDragBar final : public QWidget
{
public:
    using QWidget::QWidget;
protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_offset = e->globalPosition().toPoint() - window()->pos();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            window()->move(e->globalPosition().toPoint() - m_offset);
    }
private:
    QPoint m_offset;
};

class AppGridTile final : public QToolButton
{
public:
    AppGridTile(const DockAppEntry &entry, QWidget *parent = nullptr)
        : QToolButton(parent), m_running(entry.running), m_active(entry.active), m_pinned(entry.pinned)
    {
        setObjectName(QStringLiteral("appTile"));
        setFixedSize(76, 74);
        setCursor(Qt::PointingHandCursor);
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIconSize(QSize(38, 38));
        setIcon(entry.icon.isNull() ? FloatingIcons::make(FloatingIcons::Orb, QColor("#dce8f8"), 38) : entry.icon);

        QString label = entry.name.trimmed();
        if (label.size() > 9) label = label.left(8) + QStringLiteral("…");
        setText(label);
        setToolTip(entry.genericName.isEmpty() ? entry.name
                                               : QStringLiteral("%1 · %2").arg(entry.name, entry.genericName));
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QToolButton::paintEvent(e);
        if (!m_running && !m_pinned) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        if (m_running) {
            // Subtle macOS Dock / Tahoe running indicator pill beneath the tile
            p.setBrush(m_active ? QColor("#7cc8ff") : QColor("#94a3b8"));
            p.drawRoundedRect(QRectF(width() * 0.5 - (m_active ? 7.0 : 2.5), height() - 5.5,
                                     m_active ? 14.0 : 5.0, 2.8),
                              1.4, 1.4);
        }
        if (m_pinned) {
            // Tiny amber favorite dot at top-right corner
            p.setBrush(QColor(251, 191, 36, 210));
            p.drawEllipse(QPointF(width() - 9.0, 9.0), 2.2, 2.2);
        }
    }

private:
    bool m_running = false;
    bool m_active = false;
    bool m_pinned = false;
};

void clearLayout(QLayout *layout)
{
    if (!layout) return;
    while (QLayoutItem *it = layout->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        delete it;
    }
}

void repolish(QWidget *w)
{
    if (!w || !w->style()) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
}

struct CatDef {
    const char *key;
    const char *label;
};

static const CatDef kCategories[]{
    {"all",     "全部"},
    {"top",     "常用"},
    {"dev",     "开发"},
    {"net",     "网络"},
    {"media",   "影音"},
    {"office",  "办公"},
    {"util",    "工具"},
    {"game",    "游戏"},
    {"system",  "系统"}
};

} // namespace

AppLauncherPopover::AppLauncherPopover(AppDockModel *dockModel, QWidget *parent)
    : QWidget(parent)
    , m_dockModel(dockModel)
{
    setObjectName(QStringLiteral("appLauncherPopover"));
    setProperty("floatingballSurface", true);
    setWindowTitle(QStringLiteral("FloatingBall · 应用启动器"));
    // Topmost LayerShellQt overlay with soft on-demand activation so keyboard
    // search works immediately and clicking outside auto-dismisses cleanly.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(604, 468);
    setProperty("panelBlurInset", 10);
    setProperty("panelBlurRadius", 26);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("launcherCard"));
    outer->addWidget(card);

    FloatingWindow::setPanelStyleSheet(
        card,
        "QWidget#launcherCard { background:transparent; border:none; }"
        "QWidget#searchWrap { background:rgba(255,255,255,18); border:1px solid rgba(255,255,255,28); border-radius:15px; }"
        "QLineEdit#searchInput { background:transparent; color:#f8fafc; border:none; font-size:13.5px; padding:3px 2px; }"
        "QToolButton#hdrBtn { background:rgba(255,255,255,16); color:#cbd5e1; border:1px solid rgba(255,255,255,22); border-radius:14px; }"
        "QToolButton#hdrBtn:hover { background:rgba(255,255,255,30); color:#ffffff; }"
        "QToolButton#catPill { background:rgba(255,255,255,12); color:#cbd5e1; border:1px solid rgba(255,255,255,18);"
        "                      border-radius:12px; padding:4px 10px; font-size:11.5px; font-weight:600; }"
        "QToolButton#catPill:hover { background:rgba(255,255,255,24); color:#ffffff; }"
        "QToolButton#catPill[active=\"true\"] { background:rgba(59,130,246,175); color:#ffffff; border:1px solid rgba(147,197,253,195); }"
        "QToolButton#appTile { background:transparent; color:#e2e8f0; border:1px solid transparent;"
        "                      border-radius:14px; padding:5px 2px 7px 2px; font-size:11px; font-weight:500; }"
        "QToolButton#appTile:hover { background:rgba(255,255,255,20); border:1px solid rgba(255,255,255,30); color:#ffffff; }"
        "QToolButton#appTile:pressed { background:rgba(255,255,255,32); }"
        "QToolButton#appTile[firstMatch=\"true\"] { background:rgba(56,189,248,34); border:1px solid rgba(125,211,252,125); color:#ffffff; }"
        "QLabel#footInfo { color:rgba(226,232,240,175); font-size:11px; background:transparent; }");

    auto *root = new QVBoxLayout(card);
    root->setContentsMargins(14, 12, 14, 10);
    root->setSpacing(8);

    // ---- Top Search & Drag Bar ----
    auto *topBar = new LauncherDragBar(card);
    auto *topRow = new QHBoxLayout(topBar);
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);

    auto *searchWrap = new QWidget(topBar);
    searchWrap->setObjectName(QStringLiteral("searchWrap"));
    searchWrap->setFixedHeight(40);
    auto *swRow = new QHBoxLayout(searchWrap);
    swRow->setContentsMargins(12, 2, 8, 2);
    swRow->setSpacing(8);

    auto *searchIco = new QLabel(searchWrap);
    searchIco->setPixmap(FloatingIcons::make(FloatingIcons::Search, QColor("#94a3b8"), 16).pixmap(16, 16));
    searchIco->setAttribute(Qt::WA_TransparentForMouseEvents);
    swRow->addWidget(searchIco);

    m_searchEdit = new QLineEdit(searchWrap);
    m_searchEdit->setObjectName(QStringLiteral("searchInput"));
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索应用（中英文 / 关键词），回车即开…"));
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_clearBtn->setVisible(!t.isEmpty());
        applyFilter();
    });
    swRow->addWidget(m_searchEdit, 1);

    m_clearBtn = new QToolButton(searchWrap);
    m_clearBtn->setObjectName(QStringLiteral("hdrBtn"));
    m_clearBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#cbd5e1"), 12));
    m_clearBtn->setFixedSize(24, 24);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->hide();
    connect(m_clearBtn, &QToolButton::clicked, m_searchEdit, &QLineEdit::clear);
    swRow->addWidget(m_clearBtn);

    topRow->addWidget(searchWrap, 1);

    auto *closeBtn = new QToolButton(topBar);
    closeBtn->setObjectName(QStringLiteral("hdrBtn"));
    closeBtn->setIcon(FloatingIcons::make(FloatingIcons::Close, QColor("#cbd5e1"), 14));
    closeBtn->setFixedSize(32, 32);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QToolButton::clicked, this, &QWidget::hide);
    topRow->addWidget(closeBtn);
    root->addWidget(topBar);

    // ---- Category Filter Pill Strip ----
    auto *catHost = new QWidget(card);
    m_catLayout = new QHBoxLayout(catHost);
    m_catLayout->setContentsMargins(0, 0, 0, 0);
    m_catLayout->setSpacing(6);
    root->addWidget(catHost);

    // ---- 5-Column Touch App Icon Grid ----
    m_gridScroll = new QScrollArea(card);
    m_gridScroll->setWidgetResizable(true);
    m_gridScroll->setFrameShape(QFrame::NoFrame);
    m_gridScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_gridScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; margin: 2px 0; }"
        "QScrollBar::handle:vertical { background: rgba(255,255,255,46); border-radius: 3px; min-height: 28px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"));

    auto *gridContainer = new QWidget(m_gridScroll);
    gridContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    m_gridLayout = new QGridLayout(gridContainer);
    m_gridLayout->setContentsMargins(2, 2, 4, 4);
    m_gridLayout->setHorizontalSpacing(3);
    m_gridLayout->setVerticalSpacing(4);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_gridScroll->setWidget(gridContainer);
    root->addWidget(m_gridScroll, 1);

    // ---- Subtle Footer Hint ----
    m_footerLbl = new QLabel(card);
    m_footerLbl->setObjectName(QStringLiteral("footInfo"));
    root->addWidget(m_footerLbl);

    loadLaunchCounts();
    FloatingWindow::prepare(this, true);
}

void AppLauncherPopover::reveal(const QPoint &anchorGlobal, bool toggle)
{
    if (isVisible() && toggle) {
        hide();
        return;
    }
    m_openedTimer.restart();
    m_searchEdit->clear();
    m_activeCategory = QStringLiteral("all");
    reloadCatalog();
    positionCenteredOnScreen(anchorGlobal);
    show();
    raise();
    activateWindow();
    m_searchEdit->setFocus(Qt::ActiveWindowFocusReason);
}

void AppLauncherPopover::positionCenteredOnScreen(const QPoint &anchorGlobal)
{
    QScreen *screen = QGuiApplication::screenAt(anchorGlobal);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    setProperty("floatingballAssignedScreen", QVariant::fromValue(screen));
    const QRect b = screen->availableGeometry();
    const int x = b.left() + (b.width() - width()) / 2;
    const int y = b.top() + (b.height() - height()) / 2;
    move(qBound(b.left() + 8, x, qMax(b.left() + 8, b.right() - width() - 8)),
         qBound(b.top() + 8, y, qMax(b.top() + 8, b.bottom() - height() - 8)));
}

void AppLauncherPopover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    const QRectF base = QRectF(rect()).adjusted(10.0, 10.0, -10.0, -10.0);
    // Soft feathered outer edge fade (macOS/iPadOS frosted glass, zero hard border)
    struct FeatherStep { qreal expand; int alpha; };
    static const FeatherStep kFeather[] = {
        {10.0, 8}, {7.5, 14}, {5.0, 22}, {2.5, 34}
    };
    for (const FeatherStep &s : kFeather) {
        p.setBrush(QColor(12, 17, 29, s.alpha));
        p.drawRoundedRect(base.adjusted(-s.expand, -s.expand, s.expand, s.expand),
                          26.0 + s.expand * 0.5, 26.0 + s.expand * 0.5);
    }

    // Translucent frosted liquid glass gradient over KWin Gaussian blur
    QLinearGradient glass(base.topLeft(), base.bottomRight());
    glass.setColorAt(0.0, QColor(34, 44, 66, 134));
    glass.setColorAt(0.45, QColor(18, 24, 38, 138));
    glass.setColorAt(1.0, QColor(12, 16, 28, 150));
    p.setBrush(glass);
    p.drawRoundedRect(base, 26.0, 26.0);

    // Subtle top-left iPadOS/macOS specular sheen
    QRadialGradient sheen(base.topLeft() + QPointF(base.width() * 0.25, 0.0), base.width() * 0.85);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
    sheen.setColorAt(0.6, QColor(255, 255, 255, 5));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.setBrush(sheen);
    p.drawRoundedRect(base, 26.0, 26.0);
}

void AppLauncherPopover::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (!m_searchEdit->text().isEmpty()) {
            m_searchEdit->clear();
        } else {
            hide();
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

bool AppLauncherPopover::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (!m_filteredApps.isEmpty()) {
                activateApp(m_filteredApps.first(), (ke->modifiers() & Qt::ShiftModifier) != 0);
                return true;
            }
        } else if (ke->key() == Qt::Key_Escape) {
            if (!m_searchEdit->text().isEmpty()) m_searchEdit->clear();
            else hide();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AppLauncherPopover::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !isActiveWindow()) {
        if (isVisible() && m_autoDismissEnabled && !m_menuOpen &&
            QGuiApplication::platformName() != QLatin1String("offscreen") &&
            m_openedTimer.isValid() && m_openedTimer.elapsed() > 260 &&
            !QApplication::activePopupWidget() && !QApplication::activeModalWidget()) {
            hide();
        }
    }
    QWidget::changeEvent(event);
}

void AppLauncherPopover::loadLaunchCounts()
{
    m_launchCounts.clear();
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.beginGroup(QStringLiteral("launcher/counts"));
    for (const QString &k : s.childKeys()) {
        m_launchCounts.insert(k, s.value(k).toInt());
    }
    s.endGroup();
}

void AppLauncherPopover::bumpLaunchCount(const QString &key)
{
    if (key.isEmpty()) return;
    const int next = m_launchCounts.value(key, 0) + 1;
    m_launchCounts.insert(key, next);
    QSettings s(QSettings::defaultFormat(), QSettings::UserScope, "FloatingBall", "FloatingBall");
    s.setValue(QStringLiteral("launcher/counts/") + key, next);
}

int AppLauncherPopover::launchCountFor(const DockAppEntry &entry) const
{
    return m_launchCounts.value(entry.id, 0);
}

QString AppLauncherPopover::categoryBucketFor(const DockAppEntry &entry)
{
    for (const QString &raw : entry.categories) {
        const QString c = raw.toLower();
        if (c == QLatin1String("development") || c == QLatin1String("ide") || c == QLatin1String("terminalemulator"))
            return QStringLiteral("dev");
        if (c == QLatin1String("network") || c == QLatin1String("webbrowser") ||
            c == QLatin1String("instantmessaging") || c == QLatin1String("email"))
            return QStringLiteral("net");
        if (c == QLatin1String("audiovideo") || c == QLatin1String("audio") ||
            c == QLatin1String("video") || c == QLatin1String("graphics"))
            return QStringLiteral("media");
        if (c == QLatin1String("office") || c == QLatin1String("education"))
            return QStringLiteral("office");
        if (c == QLatin1String("game"))
            return QStringLiteral("game");
        if (c == QLatin1String("settings") || c == QLatin1String("system"))
            return QStringLiteral("system");
    }
    return QStringLiteral("util");
}

void AppLauncherPopover::reloadCatalog()
{
    m_allApps.clear();
    if (!m_dockModel) return;

    // Gather live running state from KWin / Dock model by normalized launchTarget & id
    QHash<QString, DockAppEntry> runningByTarget;
    QHash<QString, DockAppEntry> runningById;
    const QList<DockAppEntry> dockList = m_dockModel->dockApps(64, false, true);
    for (const DockAppEntry &d : dockList) {
        if (!d.running) continue;
        if (!d.launchTarget.isEmpty() && !runningByTarget.contains(d.launchTarget))
            runningByTarget.insert(d.launchTarget, d);
        if (!d.id.isEmpty() && !runningById.contains(d.id.toLower()))
            runningById.insert(d.id.toLower(), d);
    }

    QList<DockAppEntry> installed = m_dockModel->installedApps(260);
    for (DockAppEntry &e : installed) {
        if (runningByTarget.contains(e.launchTarget)) {
            const DockAppEntry &live = runningByTarget.value(e.launchTarget);
            e.running = true;
            e.active = live.active;
            e.windowId = live.windowId;
        } else if (runningById.contains(e.id.toLower())) {
            const DockAppEntry &live = runningById.value(e.id.toLower());
            e.running = true;
            e.active = live.active;
            e.windowId = live.windowId;
        }
    }

    // Sort: pinned -> running -> launch frequency -> alphabetical
    std::sort(installed.begin(), installed.end(), [this](const DockAppEntry &a, const DockAppEntry &b) {
        if (a.pinned != b.pinned) return a.pinned;
        if (a.running != b.running) return a.running;
        const int ca = launchCountFor(a);
        const int cb = launchCountFor(b);
        if (ca != cb) return ca > cb;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    m_allApps = installed;
    rebuildCategoryBar();
    applyFilter();
}

void AppLauncherPopover::rebuildCategoryBar()
{
    clearLayout(m_catLayout);
    QHash<QString, int> counts;
    int topCount = 0;
    for (const DockAppEntry &e : m_allApps) {
        counts[categoryBucketFor(e)] += 1;
        if (e.pinned || e.running || launchCountFor(e) > 0) ++topCount;
    }

    for (const CatDef &cd : kCategories) {
        const QString key = QString::fromLatin1(cd.key);
        if (key == QLatin1String("top") && topCount == 0) continue;
        if (key != QLatin1String("all") && key != QLatin1String("top") && counts.value(key) == 0)
            continue;

        auto *pill = new QToolButton(this);
        pill->setObjectName(QStringLiteral("catPill"));
        pill->setText(QString::fromUtf8(cd.label));
        pill->setProperty("active", key == m_activeCategory);
        pill->setCursor(Qt::PointingHandCursor);
        connect(pill, &QToolButton::clicked, this, [this, key] {
            if (m_activeCategory == key) return;
            m_activeCategory = key;
            rebuildCategoryBar();
            applyFilter();
        });
        m_catLayout->addWidget(pill);
    }
    m_catLayout->addStretch(1);
}

void AppLauncherPopover::applyFilter()
{
    const QString query = m_searchEdit ? m_searchEdit->text().trimmed() : QString();
    m_filteredApps.clear();

    if (!query.isEmpty()) {
        // When typing, search across ALL installed apps regardless of category tab!
        struct Scored {
            DockAppEntry entry;
            int score = 0;
        };
        QList<Scored> matches;
        const QString qLo = query.toLower();
        for (const DockAppEntry &e : m_allApps) {
            const QString nLo = e.name.toLower();
            const QString idLo = e.id.toLower();
            const QString kwLo = e.keywords.toLower();
            int score = 0;
            if (nLo == qLo || idLo == qLo) score = 100;
            else if (nLo.startsWith(qLo)) score = 85;
            else if (idLo.startsWith(qLo)) score = 75;
            else if (nLo.contains(qLo)) score = 60;
            else if (idLo.contains(qLo)) score = 50;
            else if (kwLo.contains(qLo)) score = 35;
            if (score > 0) {
                score += qMin(15, launchCountFor(e) * 2) + (e.running ? 6 : 0) + (e.pinned ? 5 : 0);
                matches.append({e, score});
            }
        }
        std::sort(matches.begin(), matches.end(), [](const Scored &a, const Scored &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.entry.name.compare(b.entry.name, Qt::CaseInsensitive) < 0;
        });
        for (const Scored &m : matches) m_filteredApps.append(m.entry);
    } else {
        for (const DockAppEntry &e : m_allApps) {
            if (m_activeCategory == QLatin1String("all")) {
                m_filteredApps.append(e);
            } else if (m_activeCategory == QLatin1String("top")) {
                if (e.pinned || e.running || launchCountFor(e) > 0)
                    m_filteredApps.append(e);
            } else if (categoryBucketFor(e) == m_activeCategory) {
                m_filteredApps.append(e);
            }
        }
    }

    rebuildGrid();
}

void AppLauncherPopover::rebuildGrid()
{
    clearLayout(m_gridLayout);

    if (m_filteredApps.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("没有匹配的应用（可按 Esc 清空搜索）"), this);
        empty->setStyleSheet(QStringLiteral("color:#94a3b8; font-size:13px; padding:48px 0;"));
        empty->setAlignment(Qt::AlignCenter);
        m_gridLayout->addWidget(empty, 0, 0, 1, 7);
        m_footerLbl->setText(QStringLiteral("0 个匹配 · 右键悬浮球「开始」可呼出 KRunner"));
        return;
    }

    const bool highlightFirst = m_searchEdit && !m_searchEdit->text().trimmed().isEmpty();
    constexpr int kCols = 7;
    for (int i = 0; i < m_filteredApps.size(); ++i) {
        const DockAppEntry entry = m_filteredApps[i];
        auto *tile = new AppGridTile(entry, this);
        if (highlightFirst && i == 0) {
            tile->setProperty("firstMatch", true);
            repolish(tile);
        }
        tile->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tile, &QToolButton::clicked, this, [this, entry] { activateApp(entry, false); });
        connect(tile, &QWidget::customContextMenuRequested, this, [this, tile, entry](const QPoint &pt) {
            showTileContextMenu(entry, tile->mapToGlobal(pt));
        });
        m_gridLayout->addWidget(tile, i / kCols, i % kCols);
    }

    const QString hint = highlightFirst
        ? QStringLiteral("匹配 %1 个应用 · Enter 打开首项 · Shift+Enter 新建窗口").arg(m_filteredApps.size())
        : QStringLiteral("共 %1 个应用 · 蓝点为运行中 · 右键图标可置顶/新开窗口").arg(m_filteredApps.size());
    m_footerLbl->setText(hint);
}

void AppLauncherPopover::activateApp(const DockAppEntry &entry, bool forceNewWindow)
{
    if (!m_dockModel) return;
    bumpLaunchCount(entry.id);
    const QPoint toastPos = geometry().center();
    hide();
    const auto out = forceNewWindow ? m_dockModel->launchFresh(entry)
                                    : m_dockModel->activateOrLaunch(entry);
    if (!out.message.isEmpty()) DesktopActions::showToast(out.message, toastPos);
}

void AppLauncherPopover::showTileContextMenu(const DockAppEntry &entry, const QPoint &globalPos)
{
    if (!m_dockModel) return;
    m_menuOpen = true;
    QMenu menu(this);
    menu.addAction(entry.running ? tr("切换到 %1").arg(entry.name) : tr("打开 %1").arg(entry.name),
                   this, [this, entry] { activateApp(entry, false); });
    if (entry.running) {
        menu.addAction(tr("新建 %1 窗口").arg(entry.name),
                       this, [this, entry] { activateApp(entry, true); });
    }
    if (!entry.launchTarget.isEmpty()) {
        const bool pinned = m_dockModel->isPinned(entry.launchTarget);
        menu.addAction(pinned ? tr("取消置顶") : tr("置顶到「常用」"), this, [this, entry] {
            m_dockModel->togglePin(entry.launchTarget);
            reloadCatalog();
        });
    }
    if (entry.running) {
        menu.addSeparator();
        menu.addAction(tr("退出 %1").arg(entry.name), this, [this, entry] {
            const auto out = m_dockModel->quitApp(entry);
            DesktopActions::showToast(out.message, geometry().center());
            QTimer::singleShot(350, this, [this] { if (isVisible()) reloadCatalog(); });
        });
    }
    menu.exec(globalPos);
    m_menuOpen = false;
}
