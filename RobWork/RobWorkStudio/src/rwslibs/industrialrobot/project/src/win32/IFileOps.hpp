/**
 * @file   IFileOps.hpp
 * @brief  project 单元文件操作窄接口（IFileOps）——AtomicFile 的故障注入接缝。
 *
 * 设计依据：
 *   - units/project.md §3.4（源码目录布局：win32/AtomicFile 原语自带
 *     "fault 注入接缝 IFileOps"；私有实现头放 src/，不入 include/）、
 *     §7.6 故障注入矩阵头注（"注入方式（testkit FaultInterceptor 经
 *     IFileOps 接缝）"——F2 写中途失败/F4 发布失败/F5 HEAD 切换失败等
 *     文件系统故障都经本接缝注入）；
 *   - units/testkit.md §6.4 接缝原则与 D-10 形态：生产代码不包含 testkit
 *     头、不加 #ifdef TEST；故障接缝＝生产代码本就要求的窄接口，fake 实现
 *     编译进测试目标。因此本接口是**生产窄接口**（AtomicFile 生产代码的
 *     唯一文件操作通道），测试侧 fake 经构造注入（消费面＝PRJ-T15 契约
 *     测试与 testkit FaultInterceptor）；
 *   - 红线 R-2：本头文件是私有实现头，只能位于 src/win32/，绝不进入
 *     include/ 公共头根（跨单元消费者只见到公共契约头，见不到 Win32 细节）。
 *
 * 背景说明（为什么需要这层间接）：NFR-REL-01 的单文件原语层（暂存＋原子
 * 替换）必须能被故障注入验证——"任一步失败不留半写目标"这类验收语句无法
 * 用真实磁盘稳定地复现（磁盘满/权限故障不可脚本化注入），因此把 Win32
 * 文件操作抽象为可替换接口，测试注入受控失败的实现来驱动 AtomicFile 的
 * 失败路径（testkit §6.4：文件系统故障覆盖打开失败/写中途失败/磁盘满模拟/
 * rename 失败，驱动 AT-13 类测试）。接口粒度刻意细到"单次写块/单次 flush/
 * 单次 rename"——粗粒度接口（如"写入整个文件"）会让注入点只能落在文件
 * 边界，覆盖不了 F2 的"第 N 字节写失败"边界。
 *
 * 线程约束：实现类可以是**无状态**的（真实实现 Win32FileOps 即无状态，
 * 全部状态都在调用栈上），无状态实现天然线程安全；带状态 fake 的线程
 * 安全性由测试目标自行保证（单线程测试）。AtomicFile 只经本接口指针
 * 串行调用，不引入共享可变状态。
 *
 * 错误语义：文件系统故障属**环境错误**（磁盘满/权限/共享冲突——§5.0
 * 错误分类），以值返回（FileResult 携带 Win32 错误码），不抛异常——
 * 事务引擎（PRJ-T07）据原始错误码映射 StoreErrorCode（WriteRejected/
 * DiskFull/StoreCorrupt 等）；调用方契约违约（如空路径）才走异常
 * fail-fast，在 AtomicFile 层断言。
 */

#ifndef RWS_IRD_PROJECT_SRC_WIN32_IFILEOPS_HPP
#define RWS_IRD_PROJECT_SRC_WIN32_IFILEOPS_HPP

// 本头文件是 project 唯一的 Win32 依赖隔离位（§1.4/§3.2：kernel32 为设计
// 内依赖，隔离于 src/win32/）。WIN32_LEAN_AND_MEAN/NOMINMAX 收窄 windows.h
// 的宏污染面（min/max 宏会破坏 std::min/max 调用点；本单元不用 Win32
// Socket/Crypto 等组件），仅影响包含本头的翻译单元，不设为全局编译定义。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <string>

namespace sdurws::ird::project::win32 {

/// 故障点标识常量（testkit.md §6.4 命名法：<单元>/<接口>/<动作>）。
/// 测试侧 FaultInterceptor/fake 按这些 ID 匹配注入计划（FaultTrigger.
/// faultPointId），ID 与方法的对应关系一经消费（PRJ-T15）即冻结，不得
/// 更名——登记在本处是唯一权威来源，避免字符串散落各测试文件。
namespace faultpoint {

/// openWriteThrough 失败注入点（CreateFileW 打开/创建失败——testkit §6.4
/// "打开失败"覆盖项）。
inline constexpr const char* kOpen = "project/atomic-file/open";
/// writeChunk 失败注入点（单次 WriteFile 失败——F2"第 N 字节写失败"＝
/// 第 N 个 chunk 命中；FaultTrigger.occurrence 计 chunk 序号）。
inline constexpr const char* kWriteChunk = "project/atomic-file/write-chunk";
/// flush 失败注入点（FlushFileBuffers 失败——持久性闸门故障）。
inline constexpr const char* kFlush = "project/atomic-file/flush";
/// publishNew 失败注入点（只增发布 rename 失败——F4"第 k 个 rename 失败"
/// 的单文件层）。
inline constexpr const char* kPublishNew = "project/atomic-file/publish-new";
/// replaceExisting 失败注入点（原子替换 rename 失败——F5"HEAD 切换失败"
/// 的单文件层）。
inline constexpr const char* kReplaceExisting =
    "project/atomic-file/replace-existing";
/// createDirectories 失败注入点（目录树创建失败——§7.6 F1"`.staging`
/// 目录创建失败（权限/只读）"的注入载体；第 4 步对象/修订目录创建同走
/// 本故障点，occurrence 计全局调用序）。
///
/// 登记说明（PRJ-T07 增补）：§7.6 表头明定故障注入"经 IFileOps 接缝"，
/// 而 F1/F7 的边界（暂存目录创建、事务目录清理）是目录级动作，原五点
/// （open/write-chunk/flush/publish-new/replace-existing）只覆盖文件级。
/// 两个新点随 PRJ-T07 消费（PRJ-T15 契约测试按同 ID 匹配），命名沿用
/// "<单元>/<接口>/<动作>" 法——接口段取消费方组件名 tx-engine（动作的
/// 协议归属是事务七步协议，而非 AtomicFile 门面）。
inline constexpr const char* kCreateDirectories = "project/tx-engine/create-dirs";
/// removeTree 失败注入点（目录树删除失败——§7.6 F7"清理失败：删除
/// .staging 失败"的注入载体；预期＝已提交状态完好、残留仅诊断）。
inline constexpr const char* kRemoveTree = "project/tx-engine/remove-tree";

}  // namespace faultpoint

/**
 * @brief 单个文件操作的结果（值语义，环境错误的载体）。
 *
 * 设计说明：不用异常承载磁盘/权限类失败——§5.0 错误语义中这类失败由
 * 事务层映射稳定诊断码后上报，原语层只负责把 Win32 原始错误码如实上抛；
 * 保留原始码（而非此处转译）是因为错误分类规则归 PRJ-T07 事务引擎所有
 * （权威唯一 PA-1），原语层转译会造成两处分类逻辑。
 */
struct FileResult {
    /// true＝操作完全成功；false＝失败（osError 携带 Win32 原始错误码）。
    bool ok = false;
    /// Win32 GetLastError() 原始错误码（DWORD）；ok==true 时恒为 0——
    /// 无"成功但错误码残留"的歧义态。单位：Win32 系统错误码（无物理单位）。
    unsigned long osError = 0;
};

/**
 * @brief 文件操作窄接口——AtomicFile 的全部 Win32 文件操作都经本接口。
 *
 * 生命周期与所有权：实现对象由注入方（生产装配点：AtomicFile 默认构造；
 * 测试：fake 对象）持有并保证生存期覆盖消费方；本接口不管理任何资源。
 *
 * 方法与故障点一一对应（见 faultpoint 命名空间）；closeHandle 有意**不设**
 * 故障点：持久性闸门是 FlushFileBuffers（§7.2 行 1），CloseHandle 在 flush
 * 之后不再承载数据完整性语义，对它注入失败只会制造无意义的失败分支
 * （句柄泄漏防护由 AtomicFile 内 RAII 承担，见 AtomicFile.cpp）。
 */
class IFileOps {
public:
    /// 虚析构：经接口指针删除实现对象是多态所有权的常规路径。
    virtual ~IFileOps() = default;

    /**
     * @brief 打开/创建写入文件——CreateFileW 封装（§7.2 行 1 的打开半边）。
     *
     * 真实实现的固定形态：GENERIC_WRITE＋CREATE_ALWAYS（暂存路径每次事务
     * 全新，重写同路径＝显式截断语义）＋FILE_ATTRIBUTE_NORMAL＋
     * FILE_FLAG_WRITE_THROUGH（写穿透：数据未经系统缓存直达介质——
     * §7.2 行 1 口径，持久性保证的 API 半边）。
     *
     * @param path   [in] 目标文件绝对路径（宽字符；本接口全程 wstring，
     *               项目根路径可能含非 ANSI 字符——PRJ-T03 PathCanonical
     *               同为宽字符口径）
     * @param handle [out] 成功时接收 Win32 文件句柄；所有权移交调用方，
     *               调用方必须经 closeHandle() 释放。失败时不得解引用。
     * @return ok==true 表示句柄已就位；失败时 osError＝CreateFileW 的
     *         GetLastError()（如 ERROR_ACCESS_DENIED/ERROR_PATH_NOT_FOUND）
     */
    virtual FileResult openWriteThrough(const std::wstring& path,
                                        HANDLE* handle) = 0;

    /**
     * @brief 向已打开句柄写入一段字节——单次 WriteFile（故障注入粒度）。
     *
     * 故障点 project/atomic-file/write-chunk：occurrence 计"第几次本方法
     * 调用"（跨文件累计——测试用 ≥2 个 chunk 的数据触发写中途失败，F2）。
     * 真实实现单次全量写出 length 字节；部分写（WriteFile 实际写出少于
     * 请求量）按失败处理并返回 ERROR_WRITE_FAULT 语义的原始码。
     *
     * @param handle [in] openWriteThrough 返回的句柄（必须仍有效）
     * @param data   [in] 待写字节缓冲，长度 length；调用方保证生存期覆盖
     *               本调用（同步接口，无重叠 I/O）
     * @param length [in] 待写字节数（单位：字节；0 合法＝空写，直接成功）
     * @return ok==false 时 osError＝WriteFile 的 GetLastError()
     */
    virtual FileResult writeChunk(HANDLE handle, const char* data,
                                  std::size_t length) = 0;

    /**
     * @brief 刷新句柄——FlushFileBuffers 封装（§7.2 行 1 的持久性闸门）。
     *
     * "将指定文件的缓冲区刷写到磁盘"（Microsoft Learn：FlushFileBuffers）；
     * 本调用成功＝写入持久性保证成立（暂存文件关闭前数据落盘，§7.1 第 2 步）。
     *
     * @param handle [in] 已写入数据的句柄
     * @return ok==false 时 osError＝FlushFileBuffers 的 GetLastError()
     */
    virtual FileResult flush(HANDLE handle) = 0;

    /**
     * @brief 关闭句柄——CloseHandle 封装（资源释放，非业务语义）。
     *
     * @param handle [in] 待关闭句柄；关闭后句柄不得再用
     * @return ok==false 时 osError＝CloseHandle 的 GetLastError()
     */
    virtual FileResult closeHandle(HANDLE handle) = 0;

    /**
     * @brief 只增发布——MoveFileExW（**不带** MOVEFILE_REPLACE_EXISTING）。
     *
     * 语义（§7.2 行 2）：同卷 rename 是文件系统元数据操作，对其他进程的
     * 打开/查询呈现原子可见（要么旧名要么新名，无中间态）；目标已存在时
     * 函数失败（ERROR_ALREADY_EXISTS）——正是"只增不改"（§7.1 第 4 步
     * publishNew：目标已存在不覆盖，摘要一致视为共享的判定归上层对象库
     * PRJ-T05，本原语层一律失败）。
     *
     * @param tempPath   [in] 暂存文件路径（须与 targetPath 同卷——§7.1
     *                   第 4 步发布在项目根内，天然同卷；跨卷由调用方契约
     *                   禁止，本层不校验）
     * @param targetPath [in] 正式路径（目标不得存在）
     * @return ok==false 时 osError＝MoveFileExW 的 GetLastError()
     *         （目标已存在＝ERROR_ALREADY_EXISTS）
     */
    virtual FileResult publishNew(const std::wstring& tempPath,
                                  const std::wstring& targetPath) = 0;

    /**
     * @brief 原子替换——MoveFileExW（MOVEFILE_REPLACE_EXISTING＋
     *        MOVEFILE_WRITE_THROUGH）。
     *
     * 语义（§7.2 行 3）：HEAD 提交点原语——对读者呈现旧/新完整内容之一；
     * MOVEFILE_WRITE_THROUGH 使"函数在文件实际移动到磁盘后才返回"
     * （Microsoft Learn：MoveFileEx），覆盖以复制＋删除方式执行时的刷盘；
     * 同卷 NTFS rename 自身的崩溃原子性依赖 NTFS 元数据日志——本层不宣称
     * 超出文档声明的断电保证（残余窗口由 §7 恢复协议兜底，PM-08）。
     *
     * @param tempPath   [in] 已持久化的新文件路径（同卷约束同 publishNew）
     * @param targetPath [in] 被替换的正式路径（可以存在——替换；不存在＝
     *                   首次创建，语义仍成立）
     * @return ok==false 时 osError＝MoveFileExW 的 GetLastError()
     */
    virtual FileResult replaceExisting(const std::wstring& tempPath,
                                       const std::wstring& targetPath) = 0;

    /**
     * @brief 递归创建目录树（std::filesystem::create_directories 封装）。
     *
     * PRJ-T07 增补（接缝登记见 faultpoint::kCreateDirectories）：事务七步
     * 协议的目录动作（第 1 步 `.staging/<tx-id>/`、第 4 步 objects/<oid>/
     * 与 revisions/<rev-id>/）必须可被 F1 类注入驱动，因此目录创建与文件
     * 操作同走本接口——生产实现是 std::filesystem 薄封装，测试 fake 按故障
     * 点注入失败。幂等语义：目标已存在＝成功（create_directories 对已存在
     * 路径无错误返回，与 ObjectStore.cpp 既有用法同口径）。
     *
     * @param path [in] 待创建的目录路径（逐级补建缺失的父目录）
     * @return ok==false 时 osError＝std::error_code.value()（Windows 上即
     *         OS 错误码，如 ERROR_ACCESS_DENIED；错误映射归事务引擎）
     */
    virtual FileResult createDirectories(const std::wstring& path) = 0;

    /**
     * @brief 递归删除目录树（std::filesystem::remove_all 封装）。
     *
     * 消费点＝事务第 7 步清理（删 `.staging/<tx-id>/` 含 tmp）与恢复扫描
     * 的 `.staging/tmp` 启动清理（§4.1 tmp 行"启动清理"）。目标不存在＝
     * 成功（remove_all 对缺失路径返回 0 且不置错误——清理的幂等语义）。
     * 删除失败（句柄占用/权限）→ ok==false，由调用方按各自协议处置
     * （第 7 步＝仅开发诊断，已提交状态不受影响——§7.1 第 7 步）。
     *
     * @param path [in] 待删除的目录树根（含其全部子项）
     * @return ok==false 时 osError＝std::error_code.value()（同上）
     */
    virtual FileResult removeTree(const std::wstring& path) = 0;
};

}  // namespace sdurws::ird::project::win32

#endif  // RWS_IRD_PROJECT_SRC_WIN32_IFILEOPS_HPP
