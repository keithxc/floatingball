#include "floatingball.h"
#include "orbfacestudio.h"
#include "statusorbrenderer.h"
#include "systempopover.h"
#include "systemstatus.h"
#include "systemtelemetry.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QtMath>
#include <cstdio>

namespace {

void check(bool condition, const char *label)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
    std::printf("PASS: %s\n", label);
}

int countBrightPixelsInArc(const QImage &img, double degStart, double degEnd,
                           double rMinRatio, double rMaxRatio)
{
    const double cx = img.width() * 0.5;
    const double cy = img.height() * 0.5;
    const double baseR = qMin(img.width(), img.height()) * 0.5 - 2.2;
    int count = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const double dx = x + 0.5 - cx;
            const double dy = cy - (y + 0.5); // math CCW degrees (0=right, 90=top, 180=left)
            const double r = std::hypot(dx, dy);
            if (r < baseR * rMinRatio || r > baseR * rMaxRatio) continue;
            double deg = qRadiansToDegrees(std::atan2(dy, dx));
            if (deg < 0.0) deg += 360.0;
            if (deg < degStart || deg > degEnd) continue;
            const QColor c = img.pixelColor(x, y);
            // Bright gauge pixels stand clearly above the dark obsidian sphere (~35..46 luma)
            const int luma = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
            if (c.alpha() > 180 && luma >= 120) ++count;
        }
    }
    return count;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const QString outDir = QString::fromLocal8Bit(argv[1]);
    QDir().mkpath(outDir);
    QDir(outDir + QStringLiteral("/settings")).removeRecursively();

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, outDir + QStringLiteral("/settings"));

    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    // 1. Pure parser & math checks (Linux /proc/stat, /proc/meminfo, macOS VM pages, EMA smoothing)
    {
        const auto s1 = SystemTelemetry::parseLinuxProcStatLine("cpu  1000 100 400 8500 50 0 0 0 0 0\n");
        const auto s2 = SystemTelemetry::parseLinuxProcStatLine("cpu  1300 100 500 9100 50 0 0 0 0 0\n");
        check(s1.valid && s2.valid && s2.total > s1.total && (s2.active - s1.active) == 400 &&
                  (s2.total - s1.total) == 1000,
              "parseLinuxProcStatLine extracts exact active and total jiffies");

        int pct = 0;
        double usedGiB = 0.0, totalGiB = 0.0;
        const QByteArray sampleMem =
            "MemTotal:       33554432 kB\n"
            "MemFree:         2097152 kB\n"
            "MemAvailable:   16777216 kB\n";
        check(SystemTelemetry::parseLinuxMemInfo(sampleMem, &pct, &usedGiB, &totalGiB) &&
                  pct == 50 && qAbs(usedGiB - 16.0) < 0.01 && qAbs(totalGiB - 32.0) < 0.01,
              "parseLinuxMemInfo computes MemTotal - MemAvailable percentage and GiB");

        // Real mm4 64 GiB Apple Silicon vm_stat numbers (~52% used)
        check(SystemTelemetry::computeMacMemoryUsage(
                  68719476736ULL, 16384ULL, 1486537ULL, 71086ULL, 289926ULL, 496190ULL,
                  &pct, &usedGiB, &totalGiB) &&
                  pct == 52 && qAbs(totalGiB - 64.0) < 0.01 && usedGiB > 33.0 && usedGiB < 34.5,
              "computeMacMemoryUsage matches macOS Activity Monitor (app + wired + compressed)");

        double ema = 0.0;
        const int first = SystemTelemetry::smoothCpuPercent(20, false, &ema);
        const int second = SystemTelemetry::smoothCpuPercent(80, true, &ema);
        check(first == 20 && second == 47,
              "smoothCpuPercent seeds first sample directly and applies EMA smoothing thereafter");
    }

    // 2. Live native host telemetry check (works on both macOS and Linux!)
    {
        SystemTelemetry::Sampler sampler;
        const auto sample1 = sampler.sample();
        const auto sample2 = sampler.sample();
        check(sample1.cpuAvailable && sample1.cpuPercent >= 0 && sample1.cpuPercent <= 100 &&
                  sample2.cpuAvailable && sample2.cpuPercent >= 0 && sample2.cpuPercent <= 100,
              "native SystemTelemetry::Sampler reads valid CPU load on current host");
        check(sample1.memoryAvailable && sample1.memoryTotalGiB > 0.5 &&
                  sample1.memoryUsedGiB >= 0.0 && sample1.memoryUsedGiB <= sample1.memoryTotalGiB + 0.01 &&
                  sample1.memoryPercent >= 0 && sample1.memoryPercent <= 100,
              "native SystemTelemetry::Sampler reads valid Memory usage on current host");
    }

    // 3. Slot resolution & color thresholds (Desktop vs Laptop vs Mixed)
    {
        SystemStatusSnapshot desktop;
        desktop.batteryPresent = false;
        desktop.brightnessAvailable = false;
        desktop.cpuAvailable = true;
        desktop.cpuPercent = 42;
        desktop.memoryAvailable = true;
        desktop.memoryPercent = 64;
        desktop.memoryUsedGiB = 41.0;
        desktop.memoryTotalGiB = 64.0;
        desktop.volumeAvailable = true;
        desktop.volumePercent = 70;
        desktop.networkConnected = true;
        desktop.signalQuality = 100;
        desktop.networkText = QStringLiteral("有线网络 (en0)");

        check(StatusOrbRenderer::resolveTopMetric(desktop) == StatusOrbRenderer::MetricSlot::Memory &&
                  StatusOrbRenderer::resolveLeftMetric(desktop) == StatusOrbRenderer::MetricSlot::Cpu,
              "desktop snapshot automatically maps Top -> Memory and Left -> CPU");
        const QString deskTip = StatusOrbRenderer::buildTooltip(desktop);
        check(deskTip.contains(QStringLiteral("内存 64% (41.0/64 GB)")) &&
                  deskTip.contains(QStringLiteral("CPU 42%")),
              "desktop orb tooltip shows Memory (with GB) and CPU telemetry");
        const auto deskPill = StatusOrbRenderer::formatHeaderPill(desktop);
        check(deskPill.text == QStringLiteral("C 42% · M 64%") &&
                  deskPill.accent == QLatin1String("blue") && deskPill.opensMonitor,
              "desktop Control Center header pill shows compact non-eliding C/M readout and opens Activity Monitor");

        desktop.memoryPercent = 90;
        const auto hotPill = StatusOrbRenderer::formatHeaderPill(desktop);
        check(hotPill.accent == QLatin1String("rose") &&
                  StatusOrbRenderer::memoryGaugeColor(90) == QColor("#ef5350") &&
                  StatusOrbRenderer::memoryGaugeColor(80) == QColor("#f4c542") &&
                  StatusOrbRenderer::cpuGaugeColor(88) == QColor("#fb7185") &&
                  StatusOrbRenderer::cpuGaugeColor(65) == QColor("#fbbf24"),
              "telemetry color thresholds highlight warning amber and critical coral/red");

        SystemStatusSnapshot laptop = desktop;
        laptop.batteryPresent = true;
        laptop.batteryPercent = 82;
        laptop.batteryPluggedIn = true;
        laptop.brightnessAvailable = true;
        laptop.brightnessPercent = 75;
        laptop.powerProfile = QStringLiteral("balanced");
        check(StatusOrbRenderer::resolveTopMetric(laptop) == StatusOrbRenderer::MetricSlot::Battery &&
                  StatusOrbRenderer::resolveLeftMetric(laptop) == StatusOrbRenderer::MetricSlot::Brightness,
              "laptop snapshot preserves Battery (top) and Brightness (left) with zero regression");
        const auto lapPill = StatusOrbRenderer::formatHeaderPill(laptop);
        check(lapPill.text == QStringLiteral("82%⚡ 平衡") && !lapPill.opensMonitor,
              "laptop Control Center header pill preserves battery + power profile cycling");

        SystemStatusSnapshot externalMonLaptop = laptop;
        externalMonLaptop.brightnessAvailable = false;
        check(StatusOrbRenderer::resolveTopMetric(externalMonLaptop) == StatusOrbRenderer::MetricSlot::Battery &&
                  StatusOrbRenderer::resolveLeftMetric(externalMonLaptop) == StatusOrbRenderer::MetricSlot::Cpu,
              "laptop on external monitor cleanly pairs Battery (top) with CPU (left)");
    }

    // 4. Pixel-level verification of circular Desktop Status Orb, Edge Capsule & Control Center pill
    {
        FloatingBall ball;
        ball.setMultiScreenEnabled(false);
        ball.setStatusOrbMode(true);
        ball.setAutoHideEnabled(false);
        ball.show();
        app.processEvents();

        SystemStatusSnapshot desktop;
        desktop.batteryPresent = false;
        desktop.brightnessAvailable = false;
        desktop.cpuAvailable = true;
        desktop.cpuPercent = 78;
        desktop.memoryAvailable = true;
        desktop.memoryPercent = 72;
        desktop.memoryUsedGiB = 46.1;
        desktop.memoryTotalGiB = 64.0;
        desktop.volumeAvailable = true;
        desktop.volumePercent = 65;
        desktop.networkConnected = true;
        desktop.signalQuality = 100;
        desktop.networkText = QStringLiteral("有线网络 (en0)");

        ball.statusMonitor()->setSnapshotForTest(desktop);
        app.processEvents();

        const QImage orbImg = ball.grab().toImage();
        orbImg.save(outDir + QStringLiteral("/desktop-orb-preview.png"));
        const int topArcLit = countBrightPixelsInArc(orbImg, 40.0, 140.0, 0.54, 0.76);
        const int leftArcLit = countBrightPixelsInArc(orbImg, 145.0, 215.0, 0.74, 0.94);
        check(topArcLit >= 12 && leftArcLit >= 8,
              "desktop circular orb visibly renders both Top Memory arc and Left CPU arc");
        check(ball.toolTip().contains(QStringLiteral("内存 72%")) &&
                  ball.toolTip().contains(QStringLiteral("CPU 78%")),
              "FloatingBall widget tooltip updates with Memory + CPU readout");

        // Edge capsule bottom Memory micro-bar
        ball.tuckToEdgeCapsule(true, false);
        app.processEvents();
        const QImage capImg = ball.grab().toImage();
        capImg.save(outDir + QStringLiteral("/desktop-capsule-preview.png"));
        int barLit = 0;
        for (int y = qMax(0, capImg.height() - 14); y < capImg.height() - 4; ++y) {
            for (int x = 4; x < capImg.width() - 4; ++x) {
                const QColor c = capImg.pixelColor(x, y);
                const int luma = qRound(0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue());
                if (c.alpha() > 180 && luma >= 120) ++barLit;
            }
        }
        check(barLit >= 6, "desktop vertical edge capsule renders bottom Memory micro-bar");
        ball.expandFromEdge();
        app.processEvents();

        // Control Center header pill shows live CPU · RAM readout + FloatingBall settings button
        ball.openSystemPopover(SystemPopover::TabHome);
        ball.systemPopover()->setAutoDismissEnabled(false);
        app.processEvents();
        auto *pill = ball.systemPopover()->findChild<QToolButton *>(QStringLiteral("powerPill"));
        auto *orbSetBtn = ball.systemPopover()->findChild<QToolButton *>(QStringLiteral("hdrOrbSettingsBtn"));
        check(pill != nullptr && pill->text() == QStringLiteral("C 78% · M 72%"),
              "Control Center powerPill displays compact non-eliding CPU + RAM summary on desktop");
        check(orbSetBtn != nullptr && orbSetBtn->isVisible() && orbSetBtn->toolTip() == QStringLiteral("悬浮球设置"),
              "Control Center header exposes dedicated FloatingBall Settings button on Home page");
        ball.systemPopover()->grab().save(outDir + QStringLiteral("/desktop-control-center-preview.png"));
        int orbSettingsSignalCount = 0;
        QObject::connect(ball.systemPopover(), &SystemPopover::orbSettingsRequested, &app, [&] {
            ++orbSettingsSignalCount;
        });
        // Prevent modal dialog from blocking during signal-only verification by disconnecting ball's slot temporarily
        QObject::disconnect(ball.systemPopover(), &SystemPopover::orbSettingsRequested, &ball, &FloatingBall::showSettings);
        orbSetBtn->click();
        app.processEvents();
        check(orbSettingsSignalCount == 1 && !ball.systemPopover()->isVisible(),
              "clicking Control Center orb settings button hides popover and emits orbSettingsRequested");
        QObject::connect(ball.systemPopover(), &SystemPopover::orbSettingsRequested, &ball, &FloatingBall::showSettings);

        check(ball.findChildren<QSystemTrayIcon *>().isEmpty(),
              "FloatingBall does not create a system tray icon");

        // 5. FaceConfig persistence, presets, Disk telemetry, custom slots & OrbFaceStudio live preview
        {
            SystemTelemetry::Sampler sampler;
            const auto s = sampler.sample();
            check(s.diskAvailable && s.diskTotalGiB > 1.0 && s.diskPercent >= 0 && s.diskPercent <= 100,
                  "native SystemTelemetry::Sampler reads valid root/home Disk usage");

            StatusOrbRenderer::FaceConfig custom;
            custom.topArc = StatusOrbRenderer::MetricSlot::Disk;
            custom.leftArc = StatusOrbRenderer::MetricSlot::Cpu;
            custom.rightArc = StatusOrbRenderer::MetricSlot::Memory;
            custom.bottomDots = StatusOrbRenderer::MetricSlot::Network;
            custom.centerMode = StatusOrbRenderer::CenterMode::CpuMemory;
            custom.accentTheme = StatusOrbRenderer::AccentTheme::Emerald;
            custom.showPrivacyDots = false;
            custom.showRadarDots = false;

            QSettings cfgStore(outDir + QStringLiteral("/face-roundtrip.ini"), QSettings::IniFormat);
            StatusOrbRenderer::saveFaceConfig(cfgStore, custom);
            cfgStore.sync();
            const auto loaded = StatusOrbRenderer::loadFaceConfig(cfgStore);
            check(loaded == custom, "FaceConfig saves and loads all slots, centerMode, theme, and toggles verbatim");

            // Apply custom face onto live FloatingBall and verify tooltip + persistence
            desktop.diskAvailable = true;
            desktop.diskPercent = 76;
            desktop.diskUsedGiB = 350.0;
            desktop.diskTotalGiB = 460.0;
            ball.statusMonitor()->setSnapshotForTest(desktop);
            ball.setOrbFaceConfig(custom);
            app.processEvents();
            check(ball.orbFaceConfig() == custom &&
                      ball.toolTip().contains(QStringLiteral("磁盘 76%")) &&
                      ball.toolTip().contains(QStringLiteral("CPU 78%")) &&
                      ball.toolTip().contains(QStringLiteral("内存 72%")),
                  "FloatingBall::setOrbFaceConfig updates active config and dynamic tooltip");

            // Pure minimal preset hides outer gauges cleanly
            const auto minimal = StatusOrbRenderer::presetConfig(QStringLiteral("minimal"));
            ball.setOrbFaceConfig(minimal);
            app.processEvents();
            const QImage minImg = ball.grab().toImage();
            const int leftLitMinimal = countBrightPixelsInArc(minImg, 145.0, 215.0, 0.74, 0.94);
            check(leftLitMinimal == 0, "minimal preset cleanly hides left outer arc");

            // Watch preset with 12-hour DialTicks renders visibly
            const auto watch = StatusOrbRenderer::presetConfig(QStringLiteral("watch"));
            ball.setOrbFaceConfig(watch);
            app.processEvents();
            const QImage watchImg = ball.grab().toImage();
            watchImg.save(outDir + QStringLiteral("/watch-face-preview.png"));
            const int tickLit = countBrightPixelsInArc(watchImg, 20.0, 160.0, 0.70, 0.92);
            check(tickLit >= 6, "watch preset renders 12-hour DialTicks ring");

            // OrbFaceStudio interactive widget + 3-view live preview canvas
            OrbFaceStudio studio(ball.statusMonitor(), StatusOrbRenderer::FaceConfig{}, 60, true);
            studio.resize(600, 420);
            studio.show();
            app.processEvents();

            int emittedCount = 0;
            StatusOrbRenderer::FaceConfig lastEmitted;
            QObject::connect(&studio, &OrbFaceStudio::configChanged, &studio,
                             [&](const StatusOrbRenderer::FaceConfig &c) {
                ++emittedCount;
                lastEmitted = c;
            });

            auto *telBtn = studio.findChild<QPushButton *>(QStringLiteral("orbPreset_telemetry"));
            auto *simChk = studio.findChild<QCheckBox *>(QStringLiteral("orbPreviewSimulate"));
            auto *tipLbl = studio.findChild<QLabel *>(QStringLiteral("orbTooltipPreview"));
            auto *centerCombo = studio.findChild<QComboBox *>(QStringLiteral("orbCenterModeCombo"));
            check(telBtn != nullptr && simChk != nullptr && tipLbl != nullptr && centerCombo != nullptr,
                  "OrbFaceStudio exposes preset buttons, simulate toggle, slot combos, and tooltip preview");

            telBtn->click();
            app.processEvents();
            check(emittedCount >= 1 &&
                      lastEmitted == StatusOrbRenderer::presetConfig(QStringLiteral("telemetry")) &&
                      studio.config() == lastEmitted,
                  "clicking telemetry preset updates studio combos and emits configChanged");

            simChk->setChecked(true);
            studio.setDiameter(72);
            app.processEvents();
            const QImage studioShot = studio.grab().toImage();
            studioShot.save(outDir + QStringLiteral("/orb-face-studio-preview.png"));
            check(studioShot.width() >= 580 && studioShot.height() >= 380 &&
                      tipLbl->text().contains(QStringLiteral("CPU 87%")) &&
                      tipLbl->text().contains(QStringLiteral("内存 89%")),
                  "OrbFaceStudio live preview updates canvas and tooltip readout in simulated high-load mode");

            const int singleIdx = centerCombo->findData(QStringLiteral("clock_single"));
            check(singleIdx >= 0, "OrbFaceStudio center combo includes clock_single mode");
            centerCombo->setCurrentIndex(singleIdx);
            app.processEvents();
            check(studio.config().centerMode == StatusOrbRenderer::CenterMode::ClockSingle,
                  "changing center combo updates OrbFaceStudio FaceConfig live");

            studio.resetDefaults();
            app.processEvents();
            check(studio.config() == StatusOrbRenderer::FaceConfig{},
                  "OrbFaceStudio::resetDefaults restores default Auto FaceConfig");
            studio.hide();

            // 6. Unified single-page Settings dialog (merged face + core behavior, zero extra tabs)
            ball.setOrbFaceConfig(StatusOrbRenderer::FaceConfig{});
            const int origWidth = ball.width();
            bool inspectedModal = false;
            bool noTabs = false;
            bool hasCore = false;
            bool removedBloat = false;
            bool liveResized = false;
            int resizedWidth = 0;
            QTimer::singleShot(80, &app, [&] {
                auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dlg) return;
                inspectedModal = true;
                noTabs = dlg->findChildren<QTabWidget *>().isEmpty();
                hasCore =
                    dlg->findChild<OrbFaceStudio *>(QStringLiteral("orbFaceStudio")) != nullptr &&
                    dlg->findChild<QSpinBox *>(QStringLiteral("ballDiameter")) != nullptr &&
                    dlg->findChild<QSpinBox *>(QStringLiteral("ballOpacity")) != nullptr &&
                    dlg->findChild<QCheckBox *>(QStringLiteral("snapEdgeCheck")) != nullptr &&
                    dlg->findChild<QCheckBox *>(QStringLiteral("autoTuckFsCheck")) != nullptr &&
                    dlg->findChild<QCheckBox *>(QStringLiteral("multiScreenCheck")) != nullptr;
                removedBloat =
                    dlg->findChild<QObject *>(QStringLiteral("dockPinnedList")) == nullptr &&
                    dlg->findChild<QObject *>(QStringLiteral("orbitAction0")) == nullptr &&
                    dlg->findChild<QObject *>(QStringLiteral("panelOpacity")) == nullptr &&
                    dlg->findChild<QObject *>(QStringLiteral("panelBlur")) == nullptr &&
                    dlg->findChild<QObject *>(QStringLiteral("dockModeCheck")) == nullptr;
                auto *diamSpin = dlg->findChild<QSpinBox *>(QStringLiteral("ballDiameter"));
                if (diamSpin) diamSpin->setValue(72);
                app.processEvents();
                resizedWidth = ball.width();
                liveResized = (resizedWidth > origWidth);
                dlg->grab().save(outDir + QStringLiteral("/unified-settings-dialog-preview.png"));
                dlg->reject();
            });
            ball.showSettings();
            app.processEvents();
            if (!(inspectedModal && noTabs && hasCore && removedBloat && liveResized && ball.width() == origWidth)) {
                std::fprintf(stderr,
                             "DIAG: inspected=%d noTabs=%d hasCore=%d removedBloat=%d liveResized=%d origW=%d resizedW=%d finalW=%d\n",
                             int(inspectedModal), int(noTabs), int(hasCore), int(removedBloat),
                             int(liveResized), origWidth, resizedWidth, ball.width());
            }
            check(inspectedModal && noTabs && hasCore && removedBloat && liveResized && ball.width() == origWidth,
                  "FloatingBall::showSettings presents single-page unified Settings with live resize and Cancel rollback");
        }
    }

    return 0;
}
