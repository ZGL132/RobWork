/**
 * @file   PathCanonical.cpp
 * @brief  路径规范化实现——GetFinalPathNameByHandleW 句柄解析（§9.3/§7.2 行 4）。
 *
 * 设计依据：
 *   - units/project.md §9.3（存储实例身份＝最终路径；大小写不敏感比较）、
 *     §7.2 行 4（GetFinalPathNameByHandleW："返回系统最终路径（解析符号
 *     链接/subst/盘符挂载）"——Microsoft Learn 文档口径，PRJ-T02 复核
 *     留痕）；
 *   - 任务契约 PRJ-T03.json acceptance 3（路径规范化与同项目多路径打开）。
 */

#include "PathCanonical.hpp"

// Win32 SDK 版本基线提升（仅本翻译单元）：本文件的 GetFinalPathNameBy-
// HandleW 需要 _WIN32_WINNT >= 0x0600 的声明可见性；框架全局编译定义为
// XP 基线 0x0501（RWS_DEFINITIONS——SA-02 框架零修改，不可动全局）。
// winsdkver→定义→sdkddkver 三行序先于 IFileOps.hpp 内的 windows.h 重设
// 基线（声明门控宏，无 ABI 影响）；ILockOps.hpp 内有同构提升（锁面
// SetFilePointerEx）——两处各自先行于本 TU 的首个 windows.h。
#include <winsdkver.h>
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#include <sdkddkver.h>

#include "IFileOps.hpp"  // windows.h（本单元 Win32 依赖隔离位，宏收窄同款）

#include <cwctype>

namespace sdurws::ird::project::win32 {

namespace {

/// ERROR_INVALID_PARAMETER（87）——空输入契约违约的原始码。
constexpr unsigned long kInvalidParameter = 87;

}  // namespace

PathCanonicalResult canonicalStorePath(const std::wstring& anyForm)
{
    PathCanonicalResult out;
    if (anyForm.empty()) {
        // 调用方契约违约走值语义失败（打开协议链上的环境错误同型承载；
        // 不抛异常——见头文件错误语义）。
        out.osError = kInvalidParameter;
        return out;
    }

    // 第一步：为路径打开属性查询句柄。dwDesiredAccess=0＝仅查询属性
    // （无需任何访问权限——只读介质/受限 ACL 下也能解析）；FILE_FLAG_
    // BACKUP_SEMANTICS＝允许打开目录（项目根是目录）；全共享声明＝
    // 不干扰任何并发持有者（规范化的观察者身份，不改变任何锁状态）。
    // OPEN_EXISTING＝路径必须真实存在（规范化是对既存文件系统对象的
    // 命名解析，不存在即失败——ERROR_FILE_NOT_FOUND）。
    HANDLE handle = ::CreateFileW(anyForm.c_str(), 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE
                                      | FILE_SHARE_DELETE,
                                  nullptr /*默认安全属性*/, OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  nullptr /*无模板文件*/);
    if (handle == INVALID_HANDLE_VALUE) {
        out.osError = static_cast<unsigned long>(::GetLastError());
        return out;
    }

    // 第二步：查询最终路径所需缓冲大小（传 nullptr/0＝只取长度；标志 0
    // ＝VOLUME_NAME_DOS，返回 "\\?\C:\..." 形态——规范形态含该前缀，
    // 不剥离：前缀形态对超长路径安全且跨拼写稳定）。
    const DWORD needed =
        ::GetFinalPathNameByHandleW(handle, nullptr, 0, 0);
    if (needed == 0) {
        out.osError = static_cast<unsigned long>(::GetLastError());
        static_cast<void>(::CloseHandle(handle));
        return out;
    }

    // 第三步：实际解析。两次调用之间目标可能被移动（理论竞争窗口）——
    // 第二次返回值与首次不一致时按失败处置（规范化必须可信：宁可失败
    // 重试也不交付半截路径；重试归调用方打开协议）。
    std::wstring buffer(needed, L'\0');
    const DWORD written =
        ::GetFinalPathNameByHandleW(handle, buffer.data(), needed, 0);
    // 句柄使命完成——先行关闭（后续分支不再使用；关闭失败不影响结果）。
    static_cast<void>(::CloseHandle(handle));
    if (written == 0 || written >= needed) {
        // written >= needed＝路径在两次调用间变长（竞争）——内容不可信。
        out.osError = static_cast<unsigned long>(::GetLastError());
        if (out.osError == 0) { out.osError = kInvalidParameter; }
        return out;
    }
    buffer.resize(written);  // 去掉.reserve 的结尾 L'\0' 占位
    out.ok = true;
    out.canonical = std::move(buffer);
    return out;
}

bool sameStorePath(const std::wstring& canonicalA, const std::wstring& canonicalB)
{
    if (canonicalA.size() != canonicalB.size()) { return false; }
    // 逐字符宽字符小写比较（NTFS 默认大小写不敏感——§9.3）。不用
    // _wcsicmp：其受 locale 影响且按"当前 C locale"折叠；towlower 在
    // C locale 下即 ASCII 折叠，与 NTFS 的规范化行为口径一致（路径
    // 比较不做语言学大小写折叠——文件系统名不是自然语言文本）。
    for (std::size_t i = 0; i < canonicalA.size(); ++i) {
        if (std::towlower(canonicalA[i]) != std::towlower(canonicalB[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace sdurws::ird::project::win32
