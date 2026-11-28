#ifndef SYSTEMTELEMETRY_H
#define SYSTEMTELEMETRY_H

#include <QString>
#include <QtGlobal>

struct SystemStatusSnapshot;

namespace SystemTelemetry {

struct Sample {
    bool cpuAvailable = false;
    int cpuPercent = 0; // 0..100, EMA-smoothed
    bool memoryAvailable = false;
    int memoryPercent = 0; // 0..100
    double memoryUsedGiB = 0.0;
    double memoryTotalGiB = 0.0;
    bool diskAvailable = false;
    int diskPercent = 0; // 0..100
    double diskUsedGiB = 0.0;
    double diskTotalGiB = 0.0;
};

struct CpuTickSnapshot {
    bool valid = false;
    quint64 active = 0;
    quint64 total = 0;
};

// Pure helpers (unit-testable across macOS & Linux)
int smoothCpuPercent(int rawPercent, bool hasPrevious, double *emaState);
CpuTickSnapshot parseLinuxProcStatLine(const QByteArray &firstLine);
bool parseLinuxMemInfo(const QByteArray &meminfo, int *percentOut,
                       double *usedGiBOut, double *totalGiBOut);
bool computeMacMemoryUsage(quint64 totalBytes, quint64 pageSize,
                           quint64 internalPages, quint64 purgeablePages,
                           quint64 wiredPages, quint64 compressedPages,
                           int *percentOut, double *usedGiBOut, double *totalGiBOut);

class Sampler {
public:
    Sample sample();

private:
    bool readCpuRaw(int *percentOut);
    bool readMemory(int *percentOut, double *usedGiBOut, double *totalGiBOut) const;
    bool readDisk(int *percentOut, double *usedGiBOut, double *totalGiBOut) const;

    CpuTickSnapshot m_prevCpu;
    bool m_hasEma = false;
    double m_cpuEma = 0.0;
    int m_diskPollTick = 0;
    bool m_hasDiskCache = false;
    int m_cachedDiskPct = 0;
    double m_cachedDiskUsedGiB = 0.0;
    double m_cachedDiskTotalGiB = 0.0;
};

void applySample(const Sample &sample, SystemStatusSnapshot *snapshot);
QString openSystemMonitor();

} // namespace SystemTelemetry

#endif // SYSTEMTELEMETRY_H
