#ifndef ORBFACESTUDIO_H
#define ORBFACESTUDIO_H

#include "statusorbrenderer.h"
#include "systemstatus.h"

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class SystemStatusMonitor;

class OrbPreviewCanvas final : public QWidget {
    Q_OBJECT
public:
    explicit OrbPreviewCanvas(QWidget *parent = nullptr);

    void setFaceConfig(const StatusOrbRenderer::FaceConfig &cfg);
    void setSnapshot(const SystemStatusSnapshot &snapshot);
    void setDiameter(int diameterPx);
    void setStatusOrbEnabled(bool enabled);
    void setSimulateAlerts(bool simulate);

    SystemStatusSnapshot effectiveSnapshot() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    StatusOrbRenderer::FaceConfig m_config;
    SystemStatusSnapshot m_liveSnapshot;
    int m_diameter = 60;
    bool m_statusOrbEnabled = true;
    bool m_simulateAlerts = false;
};

class OrbFaceStudio final : public QWidget {
    Q_OBJECT
public:
    explicit OrbFaceStudio(SystemStatusMonitor *monitor,
                           const StatusOrbRenderer::FaceConfig &initialConfig,
                           int initialDiameter,
                           bool statusOrbEnabled,
                           QWidget *parent = nullptr);

    StatusOrbRenderer::FaceConfig config() const;
    void setConfig(const StatusOrbRenderer::FaceConfig &cfg);
    void resetDefaults();
    void setDiameter(int diameterPx);
    void setStatusOrbEnabled(bool enabled);
    void setSimulateAlerts(bool simulate);
    OrbPreviewCanvas *previewCanvas() const { return m_canvas; }

signals:
    void configChanged(const StatusOrbRenderer::FaceConfig &cfg);

private:
    void syncUiFromConfig(const StatusOrbRenderer::FaceConfig &cfg);
    void updatePreviewAndTooltip();
    void applyPreset(const QString &presetId);

    QPointer<SystemStatusMonitor> m_monitor;
    OrbPreviewCanvas *m_canvas = nullptr;
    QComboBox *m_topCombo = nullptr;
    QComboBox *m_leftCombo = nullptr;
    QComboBox *m_rightCombo = nullptr;
    QComboBox *m_bottomCombo = nullptr;
    QComboBox *m_centerCombo = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QCheckBox *m_privacyCheck = nullptr;
    QCheckBox *m_radarCheck = nullptr;
    QCheckBox *m_simulateCheck = nullptr;
    QLabel *m_tooltipPreview = nullptr;
    QTimer *m_liveTimer = nullptr;
    bool m_updatingUi = false;
};

#endif // ORBFACESTUDIO_H
