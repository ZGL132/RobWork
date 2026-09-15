/**
 * @file   ILockOps.hpp
 * @brief  写锁文件操作窄接口（ILockOps）——StoreLock 的故障注入接缝。
 *
 * 设计依据：
 *   - units/project.md §3.4（win32/StoreLock.{hpp,cpp}——独占句柄锁、心跳
 *     重写、PID 读取；实现层故障接缝形态沿用 PRJ-T02 已落地的 IFileOps
 *     先例：src/win32/ 私有实现头，D-10 生产窄接口形态）、
 *     §9.1～§9.6（锁机制行为契约——本接口是这些行为的 Win32 载体）、
 *     §9.6（失权三道防线——防线②"权威探测"与防线③"I/O 分类"的可验证
 *     性要求把探测/重写动作抽象为可注入接口：句柄异常失效、写冲突等
 *     失权场景无法用真实磁盘稳定复现，测试侧 fake 经本接缝注入）；
 *   - units/testkit.md §6.4/D-10（接缝原则：生产代码不包含 testkit 头、
 *     不加 #ifdef TEST；故障点标识按 <单元>/<接口>/<动作> 命名法）；
 *   - 红线 R-2：本头文件是私有实现头，只能位于 src/win32/，绝不进入
 *     include/ 公共头根（跨单元消费者只见公共契约头 StoreTypes.hpp）。
 *
 * 背景说明（为何不并入 IFileOps）：IFileOps 的打开形态是 AtomicFile 专用的
 * （GENERIC_WRITE＋CREATE_ALWAYS＋share=0——暂存写独占）；写锁的打开形态
 * 由 §9.1 明文规定且语义相反（GENERIC_READ|GENERIC_WRITE＋share=
 * FILE_SHARE_READ——读共享共存才能让第二实例读 PID，写独占由共享模式
 * *缺失* FILE_SHARE_WRITE 达成）。两套打开语义混入一个接口会让每个实现
 * 各带一半死方法；按接缝最小面原则独立成接口（testkit §6.4"窄接口"）。
 * 文件结果类型 FileResult 复用 IFileOps.hpp 的定义（同一单元同一值语义，
 * 不造第二套——NFR-MNT-04）。
 *
 * 线程约束：真实实现 Win32LockOps 无状态（全部状态在调用栈/句柄上），
 * 天然线程安全；带状态 fake 由测试目标自行保证（单线程测试）。StoreLock
 * 对同一句柄的操作经其内部互斥串行（§9.8 writer 互斥在锁原语层的最小
 * 形态——心跳线程与调用方写入口汇合）。
 *
 * 错误语义：与 IFileOps 同口径——文件系统/句柄故障属**环境错误**，以值
 * 返回（FileResult 携带 Win32 原始错误码）不抛异常；错误→失权的分类规则
 * 归 StoreLock（§9.6 防线③的三码表），本接口如实透传原始码不做转译
 * （权威唯一 PA-1）。调用方契约违约（空指针/空路径）走异常 fail-fast。
 */

#ifndef RWS_IRD_PROJECT_SRC_WIN32_ILOCKOPS_HPP
#define RWS_IRD_PROJECT_SRC_WIN32_ILOCKOPS_HPP

// Win32 SDK 版本基线提升（仅本头文件的消费翻译单元）：框架全局编译定义
// 为 XP 基线 _WIN32_WINNT=0x0501（RWS_DEFINITIONS 实测——build/CMakeCache
// 登记；SA-02 框架零修改，不可动全局）。本单元锁实现使用的
// SetFilePointerEx 需要 >= 0x0600 的声明可见性；winsdkver→定义→sdkddkver
// 的三行序在 windows.h 之前重设基线（标准 SDK 用法——声明门控宏，无
// ABI 影响；framework 侧其余翻译单元不受影响）。
#include <winsdkver.h>
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#include <sdkddkver.h>

#include "IFileOps.hpp"  // FileResult（本单元统一的文件操作值结果）

#include <cstddef>
#include <string>

namespace sdurws::ird::project::win32 {

/// 故障点标识常量（testkit.md §6.4 命名法：<单元>/<接口>/<动作>）。
/// 测试侧 fake 按这些 ID 匹配注入计划；ID 与方法的对应关系一经消费即
/// 冻结，登记在本处是唯一权威来源（IFileOps.hpp faultpoint 同款纪律）。
namespace lockfaultpoint {

/// openExclusive 失败注入点（对未持锁文件打开失败——ERROR_ACCESS_DENIED
/// 类环境故障；共享冲突 ERROR_SHARING_VIOLATION 是**预期分支**不是故障，
/// §9.2 第二实例路径，不设注入点）。
inline constexpr const char* kOpenExclusive = "project/store-lock/open-exclusive";
/// rewriteRecord 失败注入点（原地重写失败——防线③ I/O 分类的驱动源：
/// ERROR_INVALID_HANDLE/ERROR_SHARING_VIOLATION 注入即失权）。
inline constexpr const char* kRewriteRecord = "project/store-lock/rewrite-record";
/// probe 失败注入点（防线②权威探测失败——句柄异常失效场景）。
inline constexpr const char* kProbe = "project/store-lock/probe";

}  // namespace lockfaultpoint

/**
 * @brief 写锁文件操作窄接口——StoreLock 的全部 Win32 文件动作都经本接口。
 *
 * 生命周期与所有权：实现对象由注入方持有（生产装配点：StoreLock 默认
 * 使用进程级共享的 Win32LockOps；测试：fake 对象），本接口不管理任何
 * 资源。句柄的所有权约定见各方法注释（打开方移交 StoreLock，由其 RAII
 * 释放）。
 */
class ILockOps {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~ILockOps() = default;

    /**
     * @brief 独占打开/创建锁文件——§9.1 的 D-02 机制载体。
     *
     * 真实实现固定形态：CreateFileW(GENERIC_READ|GENERIC_WRITE,
     * dwShareMode=FILE_SHARE_READ, OPEN_ALWAYS)。共享模式**不含**
     * FILE_SHARE_WRITE ⇒ 其他进程/实例的任何写访问请求得到
     * ERROR_SHARING_VIOLATION（内核裁决，获取动作本身原子——D-02）；
     * 含 FILE_SHARE_READ ⇒ 持有期间第二实例可以只读方式打开读 PID
     * （§9.2，读访问与共享声明共存）。OPEN_ALWAYS＝锁文件已存在则打开
     * （接管场景），不存在则创建（首次获取）；**文件永不删除重建**
     * （§9.4/D-03——本接口因此不提供任何删除动作）。
     *
     * @param path   [in] 锁文件绝对路径（宽字符；与 PathCanonical 同口径）
     * @param handle [out] 成功时接收 Win32 文件句柄；所有权移交调用方
     *               （StoreLock 的 RAII 持有）。失败时写 nullptr。
     * @return ok==true 句柄就位；失败时 osError＝GetLastError()
     *         （ERROR_SHARING_VIOLATION＝锁被持有〔预期分支〕；
     *         ERROR_ACCESS_DENIED＝权限不足〔§9.5 第二类〕）
     */
    virtual FileResult openExclusive(const std::wstring& path,
                                     HANDLE* handle) = 0;

    /**
     * @brief 只读共享打开锁文件——§9.2 第二实例读 PID 的载体。
     *
     * 真实实现固定形态：CreateFileW(GENERIC_READ,
     * FILE_SHARE_READ|FILE_SHARE_WRITE, OPEN_EXISTING)。共享声明允许与
     * 持有者的写访问共存（§9.2：读句柄随即关闭，不长期持有）；
     * OPEN_EXISTING＝文件不存在即失败（ERROR_FILE_NOT_FOUND——锁未创建
     * 过的合法观察，调用方按"无持有者信息"处置而非错误）。
     *
     * @param path   [in] 锁文件路径
     * @param handle [out] 成功时接收句柄（调用方用毕关闭）；失败写 nullptr
     * @return ok==false 时 osError＝GetLastError()
     */
    virtual FileResult openReadShared(const std::wstring& path,
                                      HANDLE* handle) = 0;

    /**
     * @brief 读取句柄当前全部内容——§9.2 PID 读取／§9.4 残留恢复诊断。
     *
     * 从文件头读至文件尾（SetFilePointer(FILE_BEGIN) 后循环 ReadFile）。
     * 内容可能因持有方心跳原地重写并发而处于撕裂态——解析容忍归
     * StoreLock 的记录解析（§9.2"容忍撕裂读"），本方法只如实交付字节。
     *
     * @param handle [in] 已打开的锁文件句柄（读或写句柄均可）
     * @param bytes  [out] 成功时接收全部字节（可为空串＝空文件）
     * @return ok==false 时 osError＝GetLastError()
     */
    virtual FileResult readAll(HANDLE handle, std::string* bytes) = 0;

    /**
     * @brief 原地重写固定宽度记录——§9.4 心跳/接管写入的唯一形态。
     *
     * 执行序（§9.4 原文"truncate→write→flush"）：SetFilePointer(FILE_
     * BEGIN)→SetEndOfFile（截断）→SetFilePointer(FILE_BEGIN)→WriteFile
     * →FlushFileBuffers。固定宽度记录保证截断重写后文件长度不变（长度
     * 稳定＝锁对象外观不变的观测量之一，D-03 永不删除重建）；
     * FlushFileBuffers＝心跳内容落盘（诊断可信度；非持久性承诺语义——
     * 心跳丢失无害，见 D-03）。
     *
     * @param handle [in] openExclusive 返回的写句柄
     * @param data   [in] 记录字节（固定宽度，长度由 StoreLock 的记录
     *               编码常量决定）；调用方保证生存期覆盖本调用
     * @param length [in] 字节数（单位：字节；0 视为调用方违约——固定
     *               宽度记录不可能为空）
     * @return ok==false 时 osError＝GetLastError()（防线③三码表的输入：
     *         ERROR_SHARING_VIOLATION/ERROR_INVALID_HANDLE/ERROR_ACCESS_DENIED）
     */
    virtual FileResult rewriteRecord(HANDLE handle, const char* data,
                                     std::size_t length) = 0;

    /**
     * @brief 权威探测——§9.6 防线②"写前权威检查"的载体。
     *
     * 真实实现＝GetHandleInformation（轻量：仅读取句柄表标志，不做 I/O）。
     * 句柄已被 OS 关闭/失效时失败——StoreLock 据此把上下文转 LostWrite。
     * 探测失败即失权（§9.6："失败即失权"），不重试不等待。
     *
     * @param handle [in] 持有的锁句柄
     * @return ok==false 时 osError＝GetLastError()（如 ERROR_INVALID_HANDLE）
     */
    virtual FileResult probe(HANDLE handle) = 0;

    /**
     * @brief 关闭句柄——CloseHandle 封装（§9.1：关闭即失权）。
     *
     * @param handle [in] 待关闭句柄；关闭后不得再用
     * @return ok==false 时 osError＝GetLastError()（关闭失败不改变"本
     *         对象不再使用该句柄"的事实——StoreLock 照常进入释放态并
     *         以开发诊断记录该异常）
     */
    virtual FileResult closeHandle(HANDLE handle) = 0;
};

/**
 * @brief 真实 Win32 实现——ILockOps 的生产实现体（无状态）。
 *
 * 各方法即对应 Win32 API 的薄封装（错误码经 GetLastError 如实返回）；
 * 无成员状态，多线程共享单个实例安全。定义于 StoreLock.cpp；生产代码经
 * StoreLock 默认参数间接使用，测试可显式构造驱动真实文件系统断言
 * （Win32FileOps 同款先例）。
 */
class Win32LockOps : public ILockOps {
public:
    FileResult openExclusive(const std::wstring& path,
                             HANDLE* handle) override;
    FileResult openReadShared(const std::wstring& path,
                              HANDLE* handle) override;
    FileResult readAll(HANDLE handle, std::string* bytes) override;
    FileResult rewriteRecord(HANDLE handle, const char* data,
                             std::size_t length) override;
    FileResult probe(HANDLE handle) override;
    FileResult closeHandle(HANDLE handle) override;
};

}  // namespace sdurws::ird::project::win32

#endif  // RWS_IRD_PROJECT_SRC_WIN32_ILOCKOPS_HPP
