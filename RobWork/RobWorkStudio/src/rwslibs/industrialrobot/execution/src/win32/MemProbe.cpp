/**
 * @file   MemProbe.cpp
 * @brief  Windows 内存采样探针实现——三个系统调用包装的失败语义与
 *         零状态纪律（设计依据与不超诺声明见 MemProbe.hpp 文件头，
 *         此处不重复；EX-T07）。
 */

#include "MemProbe.hpp"

namespace sdurws::ird::execution::win32 {

bool querySystemMemory(SystemMemoryInfo& out) noexcept
{
    // GlobalMemoryStatusEx 的文档约定：调用前必须把 dwLength 置为结构体
    // 大小（Microsoft Learn：GlobalMemoryStatusEx 函数——"dwLength: The
    // size of the MEMORYSTATUSEX structure... must be set before calling"）。
    // 返回 0＝失败（GetLastError 承载原因——调用方按采样失败处置，保持
    // 上一治理状态，见 ResourceController::evaluate）。
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status) == 0) {
        return false;
    }
    out.totalPhysicalBytes = static_cast<std::uint64_t>(status.ullTotalPhys);
    out.availablePhysicalBytes = static_cast<std::uint64_t>(status.ullAvailPhys);
    return true;
}

bool queryCurrentProcessWorkingSetBytes(std::uint64_t& out) noexcept
{
    // GetCurrentProcess() 返回伪句柄（无需关闭）；cb 须先置结构体大小
    // （文档同款初始化约定）。WorkingSetSize＝当前工作集字节（§6.6
    // "主进程经自身……工作集查询"）。PSAPI_VERSION=2 下该符号即
    // K32GetProcessMemoryInfo（kernel32——见 MemProbe.hpp 头注）。
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(counters.WorkingSetSize);
    return true;
}

bool queryJobPeakCommittedBytes(HANDLE job, std::uint64_t& out) noexcept
{
    // 防御：空/已关闭句柄直接失败（JobScope::valid()==false 的作业——
    // 启动失败路径的记录不含有效作业，聚合面按"无读数"跳过）。
    if (job == nullptr) {
        return false;
    }
    // JobObjectExtendedLimitInformation 的 JobMemoryLimit/PeakJobMemoryUsed
    // 承载作业级提交内存（上限值/峰值）——本查询只读峰值（语义见头注：
    // 单调保守口径，与 §6.1"合计峰值"预算行对齐）。返回 0＝失败。
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (::QueryInformationJobObject(job, JobObjectExtendedLimitInformation,
                                    &info, sizeof(info), nullptr) == 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(info.PeakJobMemoryUsed);
    return true;
}

}  // namespace sdurws::ird::execution::win32
