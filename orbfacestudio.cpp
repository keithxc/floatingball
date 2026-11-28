#include "orbfacestudio.h"
#include "systemstatus.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRadialGradient>
#include <QSignalBlocker>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {

using StatusOrbRenderer::AccentTheme;
using StatusOrbRenderer::CenterMode;
using StatusOrbRenderer::FaceConfig;
using StatusOrbRenderer::MetricSlot;

QString shortSlotCaption(const QString &prefix, MetricSlot resolved, const SystemStatusSnapshot &st)
{
    switch (resolved) {
    case MetricSlot::Battery:     return QStringLiteral("%1 · 电量 %2%").arg(prefix).arg(st.batteryPercent);
    case MetricSlot::Memory:      return QStringLiteral("%1 · 内存 %2%").arg(prefix).arg(st.memoryPercent);
    case MetricSlot::Cpu:         return QStringLiteral("%1 · CPU %2%").arg(prefix).arg(st.cpuPercent);
    case MetricSlot::Disk:        return QStringLiteral("%1 · 磁盘 %2%").arg(prefix).arg(st.diskPercent);
    case MetricSlot::Brightness:  return QStringLiteral("%1 · 亮度 %2%").arg(prefix).arg(st.brightnessPercent);
    case MetricSlot::Volume:
        return st.volumeMuted ? QStringLiteral("%1 · 静音").arg(prefix)
                              : QStringLiteral("%1 · 音量 %2%").arg(prefix).arg(st.volumePercent);
    case MetricSlot::Network:
        return st.networkConnected ? QStringLiteral("%1 · 网络").arg(prefix)
                                   : QStringLiteral("%1 · 离线").arg(prefix);
    case MetricSlot::DayProgress: return QStringLiteral("%1 · 今日进度").arg(prefix);
    case MetricSlot::DialTicks:   return QStringLiteral("%1 · 刻度环").arg(prefix);
    case MetricSlot::None:
    default:                      return QStringLiteral("%1 · 隐藏").arg(prefix);
    }
}

void drawSphereBackdrop(QPainter &p, const QPointF &center, qreal radius)
{
    const QRectF sphereRect(center.x() - radius, center.y() - radius, radius * 2.0, radius * 2.0);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 56));
    p.drawEllipse(sphereRect.translated(0.0, 1.8));

    QLinearGradient surface(sphereRect.topLeft(), sphereRect.bottomRight());
    surface.setColorAt(0.0, QColor(42, 46, 56, 240));
    surface.setColorAt(1.0, QColor(20, 23, 30, 248));
    p.setBrush(surface);
    p.setPen(QPen(QColor(255, 255, 255, 68), 1.25));
    p.drawEllipse(sphereRect);
}

void drawRadarDots(QPainter &p, const QPointF &center, qreal d, qreal radius)
{
    static const qreal kDemoAngles[] = {-2.35, 2.55};
    const qreal rimR = radius * 0.89;
    const qreal dotR = qMax(2.6, d * 0.050);
    for (qreal rad : kDemoAngles) {
        const QPointF pt(center.x() + std::cos(rad) * rimR, center.y() + std::sin(rad) * rimR);
        QRadialGradient glow(pt, dotR * 2.3);
        glow.setColorAt(0.0, QColor(255, 77, 109, 215));
        glow.setColorAt(1.0, QColor(255, 77, 109, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(pt, dotR * 2.3, dotR * 2.3);
        p.setBrush(QColor("#ff4d6d"));
        p.setPen(QPen(QColor("#ffffff"), 1.0));
        p.drawEllipse(pt, dotR, dotR);
    }
}

void addSlotItems(QComboBox *combo, const QList<MetricSlot> &slotsList)
{
    for (MetricSlot s : slotsList) {
        combo->addItem(StatusOrbRenderer::metricSlotLabel(s), StatusOrbRenderer::metricSlotId(s));
    }
}

} // namespace

OrbPreviewCanvas::OrbPreviewCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("orbPreviewCanvas"));
    setMinimumHeight(178);
    setMaximumHeight(192);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void OrbPreviewCanvas::setFaceConfig(const FaceConfig &cfg)
{
    m_config = cfg;
    update();
}

void OrbPreviewCanvas::setSnapshot(const SystemStatusSnapshot &snapshot)
{
    m_liveSnapshot = snapshot;
    update();
}

void OrbPreviewCanvas::setDiameter(int diameterPx)
{
    m_diameter = qBound(42, diameterPx, 96);
    update();
}

void OrbPreviewCanvas::setStatusOrbEnabled(bool enabled)
{
    m_statusOrbEnabled = enabled;
    update();
}

void OrbPreviewCanvas::setSimulateAlerts(bool simulate)
{
    m_simulateAlerts = simulate;
    update();
}

SystemStatusSnapshot OrbPreviewCanvas::effectiveSnapshot() const
{
    if (!m_simulateAlerts) return m_liveSnapshot;

    SystemStatusSnapshot sim = m_liveSnapshot;
    const bool wantsBattery = (m_config.topArc == MetricSlot::Battery || m_config.bottomDots == MetricSlot::Battery);
    const bool wantsBright = (m_config.leftArc == MetricSlot::Brightness || m_config.rightArc == MetricSlot::Brightness);
    if (sim.batteryPresent || wantsBattery) {
        sim.batteryPresent = true;
        sim.batteryPercent = 22;
        sim.batteryPluggedIn = false;
    }
    if (sim.brightnessAvailable || wantsBright) {
        sim.brightnessAvailable = true;
        sim.brightnessPercent = 72;
    }
    sim.cpuAvailable = true;
    sim.cpuPercent = 87;
    sim.memoryAvailable = true;
    sim.memoryPercent = 89;
    sim.memoryTotalGiB = (m_liveSnapshot.memoryTotalGiB > 1.0) ? m_liveSnapshot.memoryTotalGiB : 64.0;
    sim.memoryUsedGiB = sim.memoryTotalGiB * 0.89;
    sim.diskAvailable = true;
    sim.diskPercent = 84;
    sim.diskTotalGiB = (m_liveSnapshot.diskTotalGiB > 1.0) ? m_liveSnapshot.diskTotalGiB : 460.0;
    sim.diskUsedGiB = sim.diskTotalGiB * 0.84;
    sim.volumeAvailable = true;
    sim.volumePercent = 74;
    sim.volumeMuted = false;
    sim.networkConnected = true;
    sim.signalQuality = 85;
    if (sim.networkText.isEmpty()) sim.networkText = QStringLiteral("有线网络 (en0)");
    sim.inputPresent = true;
    sim.inputLabel = QStringLiteral("双");
    sim.inputDescription = QStringLiteral("简体双拼");
    sim.micActive = true;
    sim.micUsers = {QStringLiteral("模拟麦克风")};
    sim.cameraActive = true;
    sim.cameraUsers = {QStringLiteral("模拟摄像头")};
    sim.unreadNotifications = 2;
    sim.lastNotificationSummary = QStringLiteral("2 条未读消息");
    return sim;
}

void OrbPreviewCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QRectF stage = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
    QLinearGradient bg(stage.topLeft(), stage.bottomRight());
    bg.setColorAt(0.0, QColor("#0d121d"));
    bg.setColorAt(1.0, QColor("#151c2c"));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
    p.drawRoundedRect(stage, 14.0, 14.0);

    // Subtle technical grid backdrop so light & colored gauges show exact contrast
    p.save();
    p.setPen(QPen(QColor(255, 255, 255, 9), 1.0));
    for (int x = 18; x < width() - 12; x += 20)
        p.drawLine(QPointF(x, stage.top() + 6.0), QPointF(x, stage.bottom() - 6.0));
    for (int y = 18; y < height() - 12; y += 20)
        p.drawLine(QPointF(stage.left() + 6.0, y), QPointF(stage.right() - 6.0, y));
    p.restore();

    const SystemStatusSnapshot st = effectiveSnapshot();
    const QTime now = QTime::currentTime();
    const QString hh = now.toString(QStringLiteral("HH"));
    const QString mm = now.toString(QStringLiteral("mm"));
    const QString imeFlash;

    // 1. Hero 2x Magnified Orb (left-center)
    const qreal heroD = qMin(116.0, stage.height() - 56.0);
    const qreal heroR = heroD * 0.5 - 2.0;
    const QPointF heroCenter(stage.left() + stage.width() * 0.34, stage.center().y());
    drawSphereBackdrop(p, heroCenter, heroR);
    if (m_statusOrbEnabled) {
        StatusOrbRenderer::paintFace(p, heroCenter, heroD, heroR, font(), st, hh, mm, imeFlash, m_config);
        if (m_simulateAlerts && m_config.showRadarDots) {
            drawRadarDots(p, heroCenter, heroD, heroR);
        }
    } else {
        FaceConfig classic = m_config;
        classic.topArc = MetricSlot::None;
        classic.leftArc = MetricSlot::None;
        classic.rightArc = MetricSlot::None;
        classic.bottomDots = MetricSlot::None;
        classic.centerMode = CenterMode::MinimalRing;
        StatusOrbRenderer::paintFace(p, heroCenter, heroD, heroR, font(), st, hh, mm, imeFlash, classic);
    }

    // Slot callout tags around the hero orb
    QFont tagFont = font();
    tagFont.setPixelSize(11);
    tagFont.setWeight(QFont::Medium);
    p.setFont(tagFont);
    p.setPen(QColor("#cbd5e1"));
    const QString topTag = m_statusOrbEnabled
        ? shortSlotCaption(QStringLiteral("顶"), StatusOrbRenderer::resolveTopMetric(st, m_config.topArc), st)
        : QStringLiteral("经典圆环模式");
    const QString botTag = m_statusOrbEnabled
        ? shortSlotCaption(QStringLiteral("底"), StatusOrbRenderer::resolveBottomMetric(st, m_config.bottomDots), st)
        : QString();
    const QString leftTag = m_statusOrbEnabled
        ? shortSlotCaption(QStringLiteral("左"), StatusOrbRenderer::resolveLeftMetric(st, m_config.leftArc), st)
        : QString();
    const QString rightTag = m_statusOrbEnabled
        ? shortSlotCaption(QStringLiteral("右"), StatusOrbRenderer::resolveRightMetric(st, m_config.rightArc), st)
        : QString();

    p.drawText(QRectF(heroCenter.x() - 95.0, stage.top() + 5.0, 190.0, 18.0), Qt::AlignCenter, topTag);
    if (!botTag.isEmpty())
        p.drawText(QRectF(heroCenter.x() - 95.0, stage.bottom() - 22.0, 190.0, 18.0), Qt::AlignCenter, botTag);
    if (!leftTag.isEmpty())
        p.drawText(QRectF(stage.left() + 8.0, heroCenter.y() - 10.0, heroCenter.x() - heroR - stage.left() - 16.0, 20.0),
                   Qt::AlignRight | Qt::AlignVCenter, leftTag);
    if (!rightTag.isEmpty())
        p.drawText(QRectF(heroCenter.x() + heroR + 8.0, heroCenter.y() - 10.0, 96.0, 20.0),
                   Qt::AlignLeft | Qt::AlignVCenter, rightTag);

    // Divider line separating magnified stage from 1:1 & capsule previews
    const qreal divX = stage.left() + stage.width() * 0.67;
    p.setPen(QPen(QColor(255, 255, 255, 24), 1.0));
    p.drawLine(QPointF(divX, stage.top() + 18.0), QPointF(divX, stage.bottom() - 18.0));

    // 2. 1:1 Actual-Size Orb preview
    const qreal realD = qBound(42.0, static_cast<qreal>(m_diameter), 92.0);
    const qreal realR = realD * 0.5 - 2.2;
    const QPointF realCenter(divX + (stage.right() - divX) * 0.34, stage.center().y() - 6.0);
    drawSphereBackdrop(p, realCenter, realR);
    if (m_statusOrbEnabled) {
        StatusOrbRenderer::paintFace(p, realCenter, realD, realR, font(), st, hh, mm, imeFlash, m_config);
        if (m_simulateAlerts && m_config.showRadarDots)
            drawRadarDots(p, realCenter, realD, realR);
    } else {
        FaceConfig classic = m_config;
        classic.topArc = MetricSlot::None;
        classic.leftArc = MetricSlot::None;
        classic.rightArc = MetricSlot::None;
        classic.bottomDots = MetricSlot::None;
        classic.centerMode = CenterMode::MinimalRing;
        StatusOrbRenderer::paintFace(p, realCenter, realD, realR, font(), st, hh, mm, imeFlash, classic);
    }
    p.setFont(tagFont);
    p.setPen(QColor("#94a3b8"));
    p.drawText(QRectF(realCenter.x() - 56.0, stage.bottom() - 22.0, 112.0, 18.0),
               Qt::AlignCenter, QStringLiteral("1:1 实际 (%1px)").arg(m_diameter));

    // 3. Vertical Edge Capsule preview
    const qreal capW = 26.0;
    const qreal capH = 66.0;
    const QPointF capCenter(divX + (stage.right() - divX) * 0.78, stage.center().y() - 6.0);
    const QRectF capRect(capCenter.x() - capW * 0.5, capCenter.y() - capH * 0.5, capW, capH);
    QLinearGradient capBg(capRect.topLeft(), capRect.bottomRight());
    capBg.setColorAt(0.0, QColor(34, 40, 52, 240));
    capBg.setColorAt(1.0, QColor(16, 20, 28, 248));
    p.setBrush(capBg);
    p.setPen(QPen(QColor(124, 200, 255, 92), 1.2));
    p.drawRoundedRect(capRect, capW * 0.5, capW * 0.5);

    if (m_statusOrbEnabled) {
        QFont cf = font();
        cf.setPixelSize(11);
        cf.setWeight(QFont::Bold);
        p.setFont(cf);
        p.setPen(QColor("#f8fafc"));
        p.drawText(QRectF(capRect.left(), capCenter.y() - 16.0, capRect.width(), 16.0), Qt::AlignCenter, hh);
        p.setPen(QColor("#cbd5e1"));
        p.drawText(QRectF(capRect.left(), capCenter.y() + 0.5, capRect.width(), 16.0), Qt::AlignCenter, mm);
        StatusOrbRenderer::paintVerticalCapsuleBar(p, capRect, st, m_config);
        if (m_simulateAlerts && m_config.showRadarDots) {
            p.setPen(QPen(QColor("#ffffff"), 0.9));
            p.setBrush(QColor("#ff4d6d"));
            p.drawEllipse(QPointF(capRect.left() + 3.2, capCenter.y() - 12.0), 3.0, 3.0);
        }
    }
    p.setFont(tagFont);
    p.setPen(QColor("#94a3b8"));
    p.drawText(QRectF(capCenter.x() - 36.0, stage.bottom() - 22.0, 72.0, 18.0), Qt::AlignCenter, QStringLiteral("边缘胶囊"));
}

OrbFaceStudio::OrbFaceStudio(SystemStatusMonitor *monitor,
                             const FaceConfig &initialConfig,
                             int initialDiameter,
                             bool statusOrbEnabled,
                             QWidget *parent)
    : QWidget(parent), m_monitor(monitor)
{
    setObjectName(QStringLiteral("orbFaceStudio"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 4);
    root->setSpacing(9);

    m_canvas = new OrbPreviewCanvas(this);
    m_canvas->setDiameter(initialDiameter);
    m_canvas->setStatusOrbEnabled(statusOrbEnabled);
    if (m_monitor) m_canvas->setSnapshot(m_monitor->snapshot());
    root->addWidget(m_canvas);

    // Preset row + Simulate high-load/alert toggle
    auto *presetRow = new QHBoxLayout();
    presetRow->setSpacing(6);
    auto *presetLbl = new QLabel(tr("快捷预设："), this);
    presetRow->addWidget(presetLbl);
    struct PresetDef { const char *id; const char *label; };
    static const PresetDef kPresets[]{
        {"auto",      "自动适配"},
        {"telemetry", "主机遥测"},
        {"watch",     "极简表盘"},
        {"minimal",   "纯净时钟"}
    };
    for (const PresetDef &pd : kPresets) {
        auto *btn = new QPushButton(QString::fromUtf8(pd.label), this);
        btn->setObjectName(QStringLiteral("orbPreset_%1").arg(QString::fromLatin1(pd.id)));
        btn->setCursor(Qt::PointingHandCursor);
        const QString id = QString::fromLatin1(pd.id);
        connect(btn, &QPushButton::clicked, this, [this, id] { applyPreset(id); });
        presetRow->addWidget(btn);
    }
    presetRow->addStretch(1);
    m_simulateCheck = new QCheckBox(tr("模拟满载与提醒态"), this);
    m_simulateCheck->setObjectName(QStringLiteral("orbPreviewSimulate"));
    m_simulateCheck->setToolTip(tr("预览高负载黄/红预警、未读方向雷达红点及麦克风/摄像头隐私微点"));
    connect(m_simulateCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->setSimulateAlerts(on);
        updatePreviewAndTooltip();
    });
    presetRow->addWidget(m_simulateCheck);
    root->addLayout(presetRow);

    // 2-column compact slot pickers
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(7);

    m_topCombo = new QComboBox(this);
    m_topCombo->setObjectName(QStringLiteral("orbTopArcCombo"));
    addSlotItems(m_topCombo, {MetricSlot::Auto, MetricSlot::Battery, MetricSlot::Memory,
                              MetricSlot::Cpu, MetricSlot::Disk, MetricSlot::DayProgress,
                              MetricSlot::DialTicks, MetricSlot::None});

    m_leftCombo = new QComboBox(this);
    m_leftCombo->setObjectName(QStringLiteral("orbLeftArcCombo"));
    addSlotItems(m_leftCombo, {MetricSlot::Auto, MetricSlot::Brightness, MetricSlot::Cpu,
                               MetricSlot::Memory, MetricSlot::Disk, MetricSlot::Volume,
                               MetricSlot::DayProgress, MetricSlot::None});

    m_rightCombo = new QComboBox(this);
    m_rightCombo->setObjectName(QStringLiteral("orbRightArcCombo"));
    addSlotItems(m_rightCombo, {MetricSlot::Auto, MetricSlot::Volume, MetricSlot::Brightness,
                                MetricSlot::Cpu, MetricSlot::Memory, MetricSlot::Disk,
                                MetricSlot::None});

    m_bottomCombo = new QComboBox(this);
    m_bottomCombo->setObjectName(QStringLiteral("orbBottomDotsCombo"));
    addSlotItems(m_bottomCombo, {MetricSlot::Auto, MetricSlot::Network, MetricSlot::Battery,
                                 MetricSlot::Memory, MetricSlot::Cpu, MetricSlot::None});

    m_centerCombo = new QComboBox(this);
    m_centerCombo->setObjectName(QStringLiteral("orbCenterModeCombo"));
    for (CenterMode m : {CenterMode::Clock24H, CenterMode::Clock24HNoIme, CenterMode::ClockSingle,
                         CenterMode::ImeAlways, CenterMode::CpuMemory, CenterMode::MinimalRing}) {
        m_centerCombo->addItem(StatusOrbRenderer::centerModeLabel(m), StatusOrbRenderer::centerModeId(m));
    }

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setObjectName(QStringLiteral("orbThemeCombo"));
    for (AccentTheme t : {AccentTheme::Obsidian, AccentTheme::Emerald, AccentTheme::Ocean, AccentTheme::Amber}) {
        m_themeCombo->addItem(StatusOrbRenderer::accentThemeLabel(t), StatusOrbRenderer::accentThemeId(t));
    }

    grid->addWidget(new QLabel(tr("顶部内弧"), this), 0, 0);
    grid->addWidget(m_topCombo, 0, 1);
    grid->addWidget(new QLabel(tr("中央显示"), this), 0, 2);
    grid->addWidget(m_centerCombo, 0, 3);

    grid->addWidget(new QLabel(tr("左侧外弧"), this), 1, 0);
    grid->addWidget(m_leftCombo, 1, 1);
    grid->addWidget(new QLabel(tr("右侧外弧"), this), 1, 2);
    grid->addWidget(m_rightCombo, 1, 3);

    grid->addWidget(new QLabel(tr("底部五点"), this), 2, 0);
    grid->addWidget(m_bottomCombo, 2, 1);
    grid->addWidget(new QLabel(tr("表盘配色"), this), 2, 2);
    grid->addWidget(m_themeCombo, 2, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    root->addLayout(grid);

    // Indicator toggles
    auto *checksRow = new QHBoxLayout();
    checksRow->setSpacing(14);
    m_privacyCheck = new QCheckBox(tr("显示麦克风 / 摄像头隐私微点"), this);
    m_privacyCheck->setObjectName(QStringLiteral("orbPrivacyCheck"));
    m_radarCheck = new QCheckBox(tr("显示未读消息方向雷达红点"), this);
    m_radarCheck->setObjectName(QStringLiteral("orbRadarCheck"));
    checksRow->addWidget(m_privacyCheck);
    checksRow->addWidget(m_radarCheck);
    checksRow->addStretch(1);
    root->addLayout(checksRow);

    // Live tooltip preview readout
    m_tooltipPreview = new QLabel(this);
    m_tooltipPreview->setObjectName(QStringLiteral("orbTooltipPreview"));
    m_tooltipPreview->setWordWrap(true);
    m_tooltipPreview->setStyleSheet(QStringLiteral(
        "QLabel#orbTooltipPreview { background:rgba(128,128,128,24); border-radius:8px;"
        "  padding:6px 10px; font-size:11.5px; }"));
    root->addWidget(m_tooltipPreview);

    auto wireCombo = [this](QComboBox *c) {
        connect(c, &QComboBox::currentIndexChanged, this, [this](int) {
            if (m_updatingUi) return;
            updatePreviewAndTooltip();
            emit configChanged(config());
        });
    };
    for (QComboBox *c : {m_topCombo, m_leftCombo, m_rightCombo, m_bottomCombo, m_centerCombo, m_themeCombo})
        wireCombo(c);
    for (QCheckBox *cb : {m_privacyCheck, m_radarCheck}) {
        connect(cb, &QCheckBox::toggled, this, [this](bool) {
            if (m_updatingUi) return;
            updatePreviewAndTooltip();
            emit configChanged(config());
        });
    }

    if (m_monitor) {
        connect(m_monitor, &SystemStatusMonitor::statusChanged, this, [this](const SystemStatusSnapshot &st) {
            m_canvas->setSnapshot(st);
            updatePreviewAndTooltip();
        });
    }
    if (QGuiApplication::platformName() != QLatin1String("offscreen")) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(1500);
        connect(m_liveTimer, &QTimer::timeout, this, [this] {
            if (m_monitor) m_monitor->refreshTelemetry();
            m_canvas->update();
            updatePreviewAndTooltip();
        });
        m_liveTimer->start();
    }

    setConfig(initialConfig);
}

FaceConfig OrbFaceStudio::config() const
{
    FaceConfig cfg;
    cfg.topArc = StatusOrbRenderer::metricSlotFromId(m_topCombo->currentData().toString(), MetricSlot::Auto);
    cfg.leftArc = StatusOrbRenderer::metricSlotFromId(m_leftCombo->currentData().toString(), MetricSlot::Auto);
    cfg.rightArc = StatusOrbRenderer::metricSlotFromId(m_rightCombo->currentData().toString(), MetricSlot::Auto);
    cfg.bottomDots = StatusOrbRenderer::metricSlotFromId(m_bottomCombo->currentData().toString(), MetricSlot::Auto);
    cfg.centerMode = StatusOrbRenderer::centerModeFromId(m_centerCombo->currentData().toString(), CenterMode::Clock24H);
    cfg.accentTheme = StatusOrbRenderer::accentThemeFromId(m_themeCombo->currentData().toString(), AccentTheme::Obsidian);
    cfg.showPrivacyDots = m_privacyCheck->isChecked();
    cfg.showRadarDots = m_radarCheck->isChecked();
    return cfg;
}

void OrbFaceStudio::setConfig(const FaceConfig &cfg)
{
    syncUiFromConfig(cfg);
    updatePreviewAndTooltip();
}

void OrbFaceStudio::resetDefaults()
{
    setConfig(FaceConfig{});
    emit configChanged(config());
}

void OrbFaceStudio::setDiameter(int diameterPx)
{
    m_canvas->setDiameter(diameterPx);
}

void OrbFaceStudio::setStatusOrbEnabled(bool enabled)
{
    m_canvas->setStatusOrbEnabled(enabled);
}

void OrbFaceStudio::setSimulateAlerts(bool simulate)
{
    m_simulateCheck->setChecked(simulate);
}

void OrbFaceStudio::syncUiFromConfig(const FaceConfig &cfg)
{
    m_updatingUi = true;
    auto pick = [](QComboBox *c, const QString &id) {
        const QSignalBlocker b(c);
        const int idx = c->findData(id);
        if (idx >= 0) c->setCurrentIndex(idx);
    };
    pick(m_topCombo, StatusOrbRenderer::metricSlotId(cfg.topArc));
    pick(m_leftCombo, StatusOrbRenderer::metricSlotId(cfg.leftArc));
    pick(m_rightCombo, StatusOrbRenderer::metricSlotId(cfg.rightArc));
    pick(m_bottomCombo, StatusOrbRenderer::metricSlotId(cfg.bottomDots));
    pick(m_centerCombo, StatusOrbRenderer::centerModeId(cfg.centerMode));
    pick(m_themeCombo, StatusOrbRenderer::accentThemeId(cfg.accentTheme));
    {
        const QSignalBlocker b1(m_privacyCheck);
        m_privacyCheck->setChecked(cfg.showPrivacyDots);
    }
    {
        const QSignalBlocker b2(m_radarCheck);
        m_radarCheck->setChecked(cfg.showRadarDots);
    }
    m_updatingUi = false;
}

void OrbFaceStudio::applyPreset(const QString &presetId)
{
    setConfig(StatusOrbRenderer::presetConfig(presetId));
    emit configChanged(config());
}

void OrbFaceStudio::updatePreviewAndTooltip()
{
    const FaceConfig cfg = config();
    m_canvas->setFaceConfig(cfg);
    const QString tip = StatusOrbRenderer::buildTooltip(m_canvas->effectiveSnapshot(), cfg);
    m_tooltipPreview->setText(tr("悬停提示预览：%1").arg(tip));
}
