/**
 * @file   MemProbe.hpp
 * @brief  Windows 内存采样探针（私有实现头）——系统物理内存/主进程工作集/
 *         作业对象提交内存三项只读查询，ResourceController（EX-T07）汇总
 *         "主进程＋全部工作进程"的生产数据源（units/execution.md §6.6
 *         内存采样行、§3.5 win32/MemProbe 行）。
 *
 * 设计依据：
 *   - units/execution.md §6.6（内存采样行：`GlobalMemoryStatusEx`（系统）＋
 *     作业对象内存信息 `QueryInformationJobObject`（JobMemory）——"系统与
 *     作业级内存占用查询"；使用承诺："ResourceController 汇总『主进程＋
 *     全部工作进程』（主进程经自身 Job 或工作集查询；NFR-PERF-04 口径）"）、
 *     §3.5（win32/MemProbe.{hpp,cpp}：主进程＋worker 内存汇总——
 *     GlobalMemoryStatusEx＋job 内存）、§6.1（内存预算行：主进程＋全部
 *     worker 合计峰值默认 ≤ 物理内存 70%——NFR-PERF-04）
 *   - 需求 NFR-PERF-04（70% 内存、先节流后诊断——本探针是其观测面）
 *   - 任务契约 tasks/foundation/EX-T07.json acceptance 2（内存汇总覆盖
 *     主进程＋全部工作进程——§6.6 MemProbe）
 *
 * 不超诺声明（§6.6 纪律——逐项对照 Microsoft Learn 文档口径）：
 *   - GlobalMemoryStatusEx：返回 MEMORYSTATUSEX（dwLength 须先置
 *     sizeof——文档要求的初始化约定）；ullTotalPhys/ullAvailPhys 为物理
 *     内存总量/可用量。文档明示该调用只提供调用瞬间的快照，不承诺两次
 *     调用间的单调性或一致性——本探针只按"快照"语义使用。
 *   - GetProcessMemoryInfo：返回 PROCESS_MEMORY_COUNTERS
 *     （WorkingSetSize＝进程当前工作集字节）。psapi 接口在
 *     PSAPI_VERSION=2（WIN32_WINNT≥0x0501 的默认）下映射为
 *     K32GetProcessMemoryInfo——位于 kernel32，无须额外链接 psapi.lib。
 *   - QueryInformationJobObject(JobObjectExtendedLimitInformation)：
 *     PeakJobMemoryUsed＝作业自创建以来全部进程提交内存（job-wide
 *     committed）的峰值，**单调不回落**（文档语义：peak 值，仅
 *     SetInformationJobObject 才可重置——本工程不重置）。取峰值而非
 *     瞬时值与 §6.1"合计峰值 ≤ 物理内存 70%"的预算口径一致，且对池化
 *     worker（跨任务复用进程）是保守计量；同时避免与作业内进程的瞬时
 *     提交内存求和（无文档化的单次作业级"当前提交内存"查询）。
 *
 * 归置边界：本头是 src/win32/ 私有实现头（R-2 纪律——不入 include/），
 *   仅被同单元实现文件与单元测试目标包含；kernel32 调用按 §3.2 依赖图
 *   隔离于 src/win32/（零 Qt、零跨单元）。
 *
 * 线程安全：三个函数均为无共享状态的系统调用包装（并发只读安全）；返回
 *   值是调用瞬间的快照，调用方（ResourceController/监督器）自行保证采样
 *   频率与决策线程约束。
 */

#ifndef SDURWS_IRD_EXECUTION_WIN32_MEMPROBE_HPP
#define SDURWS_IRD_EXECUTION_WIN32_MEMPROBE_HPP

#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0600
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// PSAPI_VERSION=2（WIN32_WINNT≥0x0501 下的默认）：GetProcessMemoryInfo
// 映射为 K32GetProcessMemoryInfo（kernel32 导出）——不引入 psapi.lib 第二
// 链接面。显式定义防外部编译参数改写默认值。
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2
#endif
#include <psapi.h>

#include <cstdint>

namespace sdurws::ird::execution::win32 {

/// 系统物理内存快照（GlobalMemoryStatusEx——§6.6 内存采样行"系统"半区）。
struct SystemMemoryInfo {
    std::uint64_t totalPhysicalBytes = 0;      ///< 物理内存总量（单位字节）
    std::uint64_t availablePhysicalBytes = 0;  ///< 当前可用物理内存（单位字节；≤总量）
};

/**
 * @brief 查询系统物理内存快照（GlobalMemoryStatusEx）。
 *
 * @param out [out] 成功时写入总量/可用量（单位字节）
 * @return false＝调用失败（如 dwLength 初始化后的参数错误——正常环境
 *         不可达；失败码经 GetLastError 供开发诊断），out 不被写入
 */
bool querySystemMemory(SystemMemoryInfo& out) noexcept;

/**
 * @brief 查询当前进程（主进程）工作集字节（GetProcessMemoryInfo→
 *        WorkingSetSize——§6.6"主进程经自身 Job 或工作集查询"的工作集半区）。
 *
 * @param out [out] 成功时写入工作集字节数（单位字节）
 * @return false＝查询失败（句柄权限等——开发诊断承载），out 不被写入
 */
bool queryCurrentProcessWorkingSetBytes(std::uint64_t& out) noexcept;

/**
 * @brief 查询作业对象的提交内存峰值（QueryInformationJobObject→
 *        JobObjectExtendedLimitInformation::PeakJobMemoryUsed——§6.6
 *        JobMemory 半区；worker 作业由 JobScope 持有，EX-T06 先例）。
 *
 * 语义（文件头"不超诺声明"）：峰值自作业创建起单调，不随进程退出回落
 *   ——对"合计峰值 ≤ 物理内存 70%"（§6.1/NFR-PERF-04）是保守口径；
 *   空作业（未纳入任何进程）恒 0。
 *
 * @param job [in] 作业对象句柄（调用方不授权本函数关闭；无效句柄→false）
 * @param out [out] 成功时写入峰值提交内存（单位字节）
 * @return false＝句柄无效或查询失败（开发诊断承载），out 不被写入
 */
bool queryJobPeakCommittedBytes(HANDLE job, std::uint64_t& out) noexcept;

}  // namespace sdurws::ird::execution::win32

#endif  // SDURWS_IRD_EXECUTION_WIN32_MEMPROBE_HPP
