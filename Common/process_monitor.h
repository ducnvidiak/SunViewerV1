#pragma once
#include <windows.h>
#include <psapi.h>

namespace common {

    struct ProcessStats {
        double cpuPercent = 0.0;
        size_t workingSetMB = 0;
    };

    class ProcessMonitor {
    public:
        ProcessMonitor();

        ProcessStats getStats();

    private:
        ULONGLONG prevSysKernel_ = 0;
        ULONGLONG prevSysUser_ = 0;
        ULONGLONG prevProcKernel_ = 0;
        ULONGLONG prevProcUser_ = 0;
    };

}