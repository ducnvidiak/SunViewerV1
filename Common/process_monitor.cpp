#include "process_monitor.h"

namespace common {

    static ULONGLONG fileTimeToInt64(const FILETIME& ft)
    {
        return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    }

    ProcessMonitor::ProcessMonitor()
    {
        FILETIME sysIdle, sysKernel, sysUser;
        FILETIME procCreate, procExit, procKernel, procUser;

        GetSystemTimes(&sysIdle, &sysKernel, &sysUser);
        GetProcessTimes(GetCurrentProcess(), &procCreate, &procExit, &procKernel, &procUser);

        prevSysKernel_ = fileTimeToInt64(sysKernel);
        prevSysUser_ = fileTimeToInt64(sysUser);
        prevProcKernel_ = fileTimeToInt64(procKernel);
        prevProcUser_ = fileTimeToInt64(procUser);
    }
    ProcessStats ProcessMonitor::getStats()
    {
        ProcessStats stats{};

        FILETIME sysIdle, sysKernel, sysUser;
        FILETIME procCreate, procExit, procKernel, procUser;

        if (!GetSystemTimes(&sysIdle, &sysKernel, &sysUser))
            return stats;

        if (!GetProcessTimes(GetCurrentProcess(), &procCreate, &procExit, &procKernel, &procUser))
            return stats;

        ULONGLONG sysKernelNow = fileTimeToInt64(sysKernel);
        ULONGLONG sysUserNow = fileTimeToInt64(sysUser);
        ULONGLONG procKernelNow = fileTimeToInt64(procKernel);
        ULONGLONG procUserNow = fileTimeToInt64(procUser);

        ULONGLONG sysTotalDelta =
            (sysKernelNow - prevSysKernel_) +
            (sysUserNow - prevSysUser_);

        ULONGLONG procTotalDelta =
            (procKernelNow - prevProcKernel_) +
            (procUserNow - prevProcUser_);

        if (sysTotalDelta > 0)
        {
            stats.cpuPercent = (double)procTotalDelta * 100.0 / (double)sysTotalDelta;
        }

        prevSysKernel_ = sysKernelNow;
        prevSysUser_ = sysUserNow;
        prevProcKernel_ = procKernelNow;
        prevProcUser_ = procUserNow;

        // ===== RAM =====
        PROCESS_MEMORY_COUNTERS_EX mem{};
        if (GetProcessMemoryInfo(GetCurrentProcess(),
            (PROCESS_MEMORY_COUNTERS*)&mem,
            sizeof(mem)))
        {
            stats.workingSetMB = mem.WorkingSetSize / (1024 * 1024);
        }

        return stats;
    }

}