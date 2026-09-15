/**
 * @file   PathCanonical.hpp
 * @brief  路径规范化（PathCanonical）——存储实例身份的规范形态（§9.3）。
 *
 * 设计依据：
 *   - units/project.md §9.3（路径规范化与同项目多路径打开：存储实例身份
 *     ＝GetFinalPathNameByHandleW 解析的最终路径，消除 subst/符号链接/
 *     盘符别名；Windows 默认大小写不敏感比较〔NTFS〕；8.3 短名/相对路径
 *     经规范层统一后不再歧义）、§7.2 行 4（路径规范化 API 口径——复核
 *     留痕 traceability/builds/wp04-t02/s72-win32-docs-crosscheck.md）、
 *     §3.4（win32/PathCanonical.{hpp,cpp} 布局位）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 3（路径规范化与
 *     同项目多路径打开）。
 *
 * 背景说明（为什么存储身份必须是"句柄解析的最终路径"）：同一项目目录
 * 可以被用户以多种拼写打开——盘符大小写、正斜杠、相对路径、8.3 短名、
 * subst 虚拟盘、符号链接。若以"用户输入串"作为存储实例身份，同项目的
 * 两次打开会被误判为两个不同存储，双写互斥（写锁）随之失效为"两个锁
 * 文件"的假象吗——不会失效（锁文件在目录内，内核仍然互斥），但进程内
 * 的"同项目重复打开"判定（§9.3：直接拒绝 lock-held-by-other，防自我
 * 双写）与"最近项目按规范路径去重"（PM-10）会失真。规范层把一切拼写
 * 收敛到唯一形态，是这些判定成立的前提。
 *
 * 线程约束：本文件全部函数无共享状态，可任意并发调用。
 *
 * 错误语义：路径不存在/不可达属**环境错误**（调用方在打开协议 PM-02
 * 步骤①已做存在预检——此处失败如实返回原始码，映射归 T08/调用方）；
 * 空串输入属调用方契约违约，返回失败（osError=ERROR_INVALID_PARAMETER）
 * 而非抛异常——本函数处于打开协议的值语义链上，与 ILockOps 同口径。
 */

#ifndef RWS_IRD_PROJECT_SRC_WIN32_PATHCANONICAL_HPP
#define RWS_IRD_PROJECT_SRC_WIN32_PATHCANONICAL_HPP

#include <string>

namespace sdurws::ird::project::win32 {

/// 规范化结果（值语义；与 FileResult 同型的错误承载方式）。
struct PathCanonicalResult {
    bool ok = false;            ///< true＝canonical 携带规范路径
    unsigned long osError = 0;  ///< 失败时的 Win32 原始错误码；成功恒 0
    /// 规范路径：GetFinalPathNameByHandleW(VOLUME_NAME_DOS) 形态
    /// （"\\\\?\\C:\\..." 前缀——超出 MAX_PATH 安全的完整 DOS 路径）。
    /// 该前缀是规范形态的一部分（稳定、可比较），不剥离。
    std::wstring canonical;
};

/**
 * @brief 解析任意拼写的项目路径为规范形态（§9.3 存储实例身份）。
 *
 * 实现＝对路径打开属性查询句柄（dwDesiredAccess=0＋FILE_FLAG_BACKUP_
 * SEMANTICS——目录可开、无需任何访问权限）后调 GetFinalPathNameByHandleW：
 * subst/符号链接/盘符别名/8.3 短名/相对路径/大小写与斜杠差异全部收敛
 * 到同一最终路径（§7.2 行 4 官方口径）。
 *
 * @param anyForm [in] 任意拼写的项目目录（或文件）路径，宽字符；
 *                空串＝返回失败（ERROR_INVALID_PARAMETER）
 * @return ok==true 时 canonical 为规范路径；ok==false 时 osError 为
 *         打开或解析失败的原始码（路径不存在＝ERROR_FILE_NOT_FOUND 等）
 */
PathCanonicalResult canonicalStorePath(const std::wstring& anyForm);

/**
 * @brief 比较两个**已规范化**路径是否指向同一存储（§9.3 大小写不敏感）。
 *
 * NTFS 默认大小写不敏感（§9.3 原文口径）——仅大小写差异的两个规范形态
 * 指向同一目录，必须判等。比较输入约定为 canonicalStorePath 的输出；
 * 对未规范化的输入（相对路径/别名拼写）结果是未定义的（调用方契约）。
 *
 * 边界声明：Windows 10 引入的 per-directory 大小写敏感（WSL 场景）
 * 不在本单元考虑范围（.rwdesign 目录由本软件创建，默认不敏感口径）。
 */
bool sameStorePath(const std::wstring& canonicalA, const std::wstring& canonicalB);

}  // namespace sdurws::ird::project::win32

#endif  // RWS_IRD_PROJECT_SRC_WIN32_PATHCANONICAL_HPP
