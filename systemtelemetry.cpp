#include "systemtelemetry.h"
#include "systemstatus.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtMath>

#include <cstdlib>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace SystemTelemetry {

namespace {
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
constexpr double kCpuEmaAlpha = 0.45;
} // namespace

int smoothCpuPercent(int rawPercent, bool hasPrevious, double *emaState)
{
    const double clamped = static_cast<double>(qBound(0, rawPercent, 100));
    if (!emaState) return qRound(clamped);
    if (!hasPrevious) {
        *emaState = clamped;
    } else {
        *emaState = kCpuEmaAlpha * clamped + (1.0 - kCpuEmaAlpha) * (*emaState);
    }
    return qBound(0, qRound(*emaState), 100);
}

CpuTickSnapshot parseLinuxProcStatLine(const QByteArray &firstLine)
{
    CpuTickSnapshot snap;
    const QList<QByteArray> parts = firstLine.simplified().split(' ');
    if (parts.size() < 5 || parts[0] != "cpu") return snap;
    quint64 vals[10]{};
    const int n = qMin(10, static_cast<int>(parts.size()) - 1);
    for (int i = 0; i < n; ++i) {
        bool ok = false;
        vals[i] = parts[i + 1].toULongLong(&ok);
        if (!ok) return snap;
    }
    // user, nice, system, idle, iowait, irq, softirq, steal
    const quint64 idleAll = vals[3] + (n > 4 ? vals[4] : 0);
    const quint64 nonIdle = vals[0] + vals[1] + vals[2] +
                            (n > 5 ? vals[5] : 0) + (n > 6 ? vals[6] : 0) + (n > 7 ? vals[7] : 0);
    snap.active = nonIdle;
    snap.total = idleAll + nonIdle;
    snap.valid = (snap.total > 0);
    return snap;
}

bool parseLinuxMemInfo(const QByteArray &meminfo, int *percentOut,
                       double *usedGiBOut, double *totalGiBOut)
{
    quint64 totalKiB = 0;
    quint64 availKiB = 0;
    quint64 freeKiB = 0;
    quint64 buffersKiB = 0;
    quint64 cachedKiB = 0;
    bool hasAvail = false;

    const QList<QByteArray> lines = meminfo.split('\n');
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon <= 0) continue;
        const QByteArray key = line.left(colon).trimmed();
        const QList<QByteArray> rest = line.mid(colon + 1).simplified().split(' ');
        if (rest.isEmpty()) continue;
        bool ok = false;
        const quint64 kb = rest[0].toULongLong(&ok);
        if (!ok) continue;
        if (key == "MemTotal") totalKiB = kb;
        else if (key == "MemAvailable") { availKiB = kb; hasAvail = true; }
        else if (key == "MemFree") freeKiB = kb;
        else if (key == "Buffers") buffersKiB = kb;
        else if (key == "Cached") cachedKiB = kb;
    }
    if (totalKiB == 0) return false;
    const quint64 effAvail = hasAvail ? availKiB : (freeKiB + buffersKiB + cachedKiB);
    const quint64 usedKiB = (totalKiB > effAvail) ? (totalKiB - effAvail) : 0;
    const double totalGiB = static_cast<double>(totalKiB) / (1024.0 * 1024.0);
    const double usedGiB = static_cast<double>(usedKiB) / (1024.0 * 1024.0);
    const int pct = qBound(0, qRound(static_cast<double>(usedKiB) * 100.0 / static_cast<double>(totalKiB)), 100);
    if (percentOut) *percentOut = pct;
    if (usedGiBOut) *usedGiBOut = usedGiB;
    if (totalGiBOut) *totalGiBOut = totalGiB;
    return true;
}

bool computeMacMemoryUsage(quint64 totalBytes, quint64 pageSize,
                           quint64 internalPages, quint64 purgeablePages,
                           quint64 wiredPages, quint64 compressedPages,
                           int *percentOut, double *usedGiBOut, double *totalGiBOut)
{
    if (totalBytes == 0 || pageSize == 0) return false;
    // Match macOS Activity Monitor "Memory Used" = App (internal - purgeable) + Wired + Compressed
    const quint64 appPages = (internalPages > purgeablePages) ? (internalPages - purgeablePages) : 0;
    const quint64 usedPages = appPages + wiredPages + compressedPages;
    quint64 usedBytes = usedPages * pageSize;
    if (usedBytes > totalBytes) usedBytes = totalBytes;
    const double totalGiB = static_cast<double>(totalBytes) / kGiB;
    const double usedGiB = static_cast<double>(usedBytes) / kGiB;
    const int pct = qBound(0, qRound(static_cast<double>(usedBytes) * 100.0 / static_cast<double>(totalBytes)), 100);
    if (percentOut) *percentOut = pct;
    if (usedGiBOut) *usedGiBOut = usedGiB;
    if (totalGiBOut) *totalGiBOut = totalGiB;
    return true;
}

bool Sampler::readCpuRaw(int *percentOut)
{
    if (!percentOut) return false;
    CpuTickSnapshot now;
    int logicalCpus = 1;

#ifdef Q_OS_MACOS
    host_cpu_load_info_data_t cpuInfo{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&cpuInfo), &count) == KERN_SUCCESS) {
        const quint64 active = static_cast<quint64>(cpuInfo.cpu_ticks[CPU_STATE_USER]) +
                               static_cast<quint64>(cpuInfo.cpu_ticks[CPU_STATE_SYSTEM]) +
                               static_cast<quint64>(cpuInfo.cpu_ticks[CPU_STATE_NICE]);
        const quint64 idle = static_cast<quint64>(cpuInfo.cpu_ticks[CPU_STATE_IDLE]);
        now.active = active;
        now.total = active + idle;
        now.valid = (now.total > 0);
    }
    int ncpu = 0;
    size_t sz = sizeof(ncpu);
    if (sysctlbyname("hw.logicalcpu", &ncpu, &sz, nullptr, 0) == 0 && ncpu > 0)
        logicalCpus = ncpu;
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/stat"));
    if (f.open(QIODevice::ReadOnly)) {
        now = parseLinuxProcStatLine(f.readLine());
    }
    const long ncpu = ::sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu > 0) logicalCpus = static_cast<int>(ncpu);
#endif

    if (!now.valid) return false;

    if (m_prevCpu.valid && now.total > m_prevCpu.total && now.active >= m_prevCpu.active) {
        const quint64 dTotal = now.total - m_prevCpu.total;
        const quint64 dActive = now.active - m_prevCpu.active;
        m_prevCpu = now;
        *percentOut = qBound(0, qRound(static_cast<double>(dActive) * 100.0 / static_cast<double>(dTotal)), 100);
        return true;
    }

    m_prevCpu = now;
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    double load[1]{};
    if (getloadavg(load, 1) == 1 && logicalCpus > 0) {
        *percentOut = qBound(0, qRound(load[0] * 100.0 / static_cast<double>(logicalCpus)), 100);
        return true;
    }
#endif
    *percentOut = 0;
    return true;
}

bool Sampler::readMemory(int *percentOut, double *usedGiBOut, double *totalGiBOut) const
{
#ifdef Q_OS_MACOS
    quint64 totalBytes = 0;
    size_t sz = sizeof(totalBytes);
    if (sysctlbyname("hw.memsize", &totalBytes, &sz, nullptr, 0) != 0 || totalBytes == 0)
        return false;
    vm_size_t pageSize = 0;
    if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS || pageSize == 0)
        return false;
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm), &count) != KERN_SUCCESS)
        return false;
    return computeMacMemoryUsage(totalBytes, static_cast<quint64>(pageSize),
                                 static_cast<quint64>(vm.internal_page_count),
                                 static_cast<quint64>(vm.purgeable_count),
                                 static_cast<quint64>(vm.wire_count),
                                 static_cast<quint64>(vm.compressor_page_count),
                                 percentOut, usedGiBOut, totalGiBOut);
#elif defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/meminfo"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    return parseLinuxMemInfo(f.readAll(), percentOut, usedGiBOut, totalGiBOut);
#else
    Q_UNUSED(percentOut);
    Q_UNUSED(usedGiBOut);
    Q_UNUSED(totalGiBOut);
    return false;
#endif
}

bool Sampler::readDisk(int *percentOut, double *usedGiBOut, double *totalGiBOut) const
{
    // Query user's home volume so macOS APFS reports /System/Volumes/Data instead of the read-only OS snapshot.
    QStorageInfo storage(QDir::homePath());
    if (!storage.isValid() || storage.bytesTotal() <= 0) storage = QStorageInfo::root();
    if (!storage.isValid() || storage.bytesTotal() <= 0) return false;
    const qint64 total = storage.bytesTotal();
    const qint64 avail = qMax<qint64>(0, storage.bytesAvailable());
    const qint64 used = qMax<qint64>(0, total - avail);
    const double totalGiB = static_cast<double>(total) / kGiB;
    const double usedGiB = static_cast<double>(used) / kGiB;
    const int pct = qBound(0, qRound(static_cast<double>(used) * 100.0 / static_cast<double>(total)), 100);
    if (percentOut) *percentOut = pct;
    if (usedGiBOut) *usedGiBOut = usedGiB;
    if (totalGiBOut) *totalGiBOut = totalGiB;
    return true;
}

Sample Sampler::sample()
{
    Sample out;
    int rawCpu = 0;
    if (readCpuRaw(&rawCpu)) {
        out.cpuAvailable = true;
        out.cpuPercent = smoothCpuPercent(rawCpu, m_hasEma, &m_cpuEma);
        m_hasEma = true;
    }
    int memPct = 0;
    double usedGiB = 0.0;
    double totalGiB = 0.0;
    if (readMemory(&memPct, &usedGiB, &totalGiB)) {
        out.memoryAvailable = true;
        out.memoryPercent = memPct;
        out.memoryUsedGiB = usedGiB;
        out.memoryTotalGiB = totalGiB;
    }
    // Refresh disk statfs every ~25s (every 10 samples) to stay virtually zero-cost.
    if (!m_hasDiskCache || (m_diskPollTick++ % 10) == 0) {
        int diskPct = 0;
        double dUsed = 0.0, dTotal = 0.0;
        if (readDisk(&diskPct, &dUsed, &dTotal)) {
            m_hasDiskCache = true;
            m_cachedDiskPct = diskPct;
            m_cachedDiskUsedGiB = dUsed;
            m_cachedDiskTotalGiB = dTotal;
        }
    }
    if (m_hasDiskCache) {
        out.diskAvailable = true;
        out.diskPercent = m_cachedDiskPct;
        out.diskUsedGiB = m_cachedDiskUsedGiB;
        out.diskTotalGiB = m_cachedDiskTotalGiB;
    }
    return out;
}

void applySample(const Sample &sample, SystemStatusSnapshot *snapshot)
{
    if (!snapshot) return;
    snapshot->cpuAvailable = sample.cpuAvailable;
    snapshot->cpuPercent = sample.cpuPercent;
    snapshot->memoryAvailable = sample.memoryAvailable;
    snapshot->memoryPercent = sample.memoryPercent;
    snapshot->memoryUsedGiB = sample.memoryUsedGiB;
    snapshot->memoryTotalGiB = sample.memoryTotalGiB;
    snapshot->diskAvailable = sample.diskAvailable;
    snapshot->diskPercent = sample.diskPercent;
    snapshot->diskUsedGiB = sample.diskUsedGiB;
    snapshot->diskTotalGiB = sample.diskTotalGiB;
}

QString openSystemMonitor()
{
#ifdef Q_OS_MACOS
    if (QProcess::startDetached(QStringLiteral("/usr/bin/open"),
                                {QStringLiteral("-a"), QStringLiteral("Activity Monitor")})) {
        return QStringLiteral("✓ 已打开活动监视器");
    }
    return QStringLiteral("无法打开活动监视器");
#elif defined(Q_OS_LINUX)
    for (const char *bin : {"plasma-systemmonitor", "ksysguard", "gnome-system-monitor"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(bin));
        if (!path.isEmpty() && QProcess::startDetached(path, {})) {
            return QStringLiteral("✓ 已打开系统监视器");
        }
    }
    return QStringLiteral("未找到系统监视器");
#else
    return QStringLiteral("当前平台暂未接入系统监视器");
#endif
}

} // namespace SystemTelemetry

void SystemStatusMonitor::refreshTelemetry()
{
    if (!m_telemetrySampler) m_telemetrySampler = std::make_unique<SystemTelemetry::Sampler>();
    SystemStatusSnapshot next = m_snapshot;
    SystemTelemetry::applySample(m_telemetrySampler->sample(), &next);
    emitIfChanged(next);
}
