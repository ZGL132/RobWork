/**
 * @file   IoPlatform.hpp
 * @brief  io 单元私有平台助手（不出公共面）——UTF-8↔UTF-16 转换、诊断
 *         脱敏呈现路径、Win32 错误四分类（§4.2.5）、会话随机标签。
 *
 * 设计依据：
 *   - units/io.md §4.2.5（路径错误四分类：不存在/权限不足/介质只读/锁
 *     竞争——互斥稳定错误，"不把一切失败都报找不到文件"）、§4.2.1 步骤 2
 *     （保留原样副本 display 用于诊断脱敏呈现）、§4.3.2（display＝UTF-8、
 *     去除 \\?\ 前缀）、§7.5（临时区 8hex 会话标签）、§1.4（零 Qt——
 *     Win32/标准库直用）
 *   - NFR-MNT-04（无重复包装——单一实现点：本头是 io 内全部翻译单元共用
 *     的平台助手；ResourceIo.cpp 自 IO-T06 起同样消费本头，替换其本地副本）
 *
 * 私有头纪律（R-2）：本头位于 src/（绝不出 include/）——仅 io 实现翻译
 * 单元可包含；公共头与跨单元消费者不可见（io/CMakeLists.txt 文件头注）。
 *
 * 线程安全：全部函数为纯函数/无共享可变状态（randomHex8 内部互斥保护
 * 计数器）——并发调用安全。确定性：转换与分类是纯字节/码值映射；随机
 * 标签仅用于临时区/暂存文件命名（会话级名字不作任何对象身份——SP-5，
 * 不参与确定性输出面）。
 */

#ifndef SDURWS_IRD_IO_SRC_IOPLATFORM_HPP
#define SDURWS_IRD_IO_SRC_IOPLATFORM_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// GetFileAttributesEx/ReplaceFile 等 API 需要 Vista+ 目标宏——RobWork 构建
// 树全局把 _WIN32_WINNT 钉在旧值（XP 基线），此处抬到 Win7（NFR-DEP-01
// Windows x64 口径内；ResourceIo.cpp 同款先例）。
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601
#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x0601
#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

#include <sdurws/ird/io/IoError.hpp>   // IoError/IoErrorCode——错误构造产物

namespace sdurws::ird::io::platform {

// =====================================================================
// UTF-8 ↔ UTF-16 转换（接口面 UTF-8/宽路径并存——§4.2；Win32 转换 API。
// 失败返回空串：调用方把空结果按防御性内部错误拒绝——编码违例的输入
// 不可能来自本软件自身接口面，静默吞掉比显式拒绝危险）
// =====================================================================

/// UTF-8 → UTF-16（MB_ERR_INVALID_CHARS——无效序列拒绝，不猜测）。
inline std::wstring utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                        static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

/// UTF-16 → UTF-8（WC_ERR_INVALID_CHARS——无效序列拒绝，不替换）。
inline std::string wideToUtf8(const std::wstring& wide)
{
    if (wide.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                                      static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                        static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

/// 去除 \\?\ / \\?\UNC\ 扩展前缀（诊断 display 形态——§4.3.2）。
inline std::wstring stripExtendedPrefix(const std::wstring& native)
{
    if (native.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        return L"\\\\" + native.substr(8);
    }
    if (native.rfind(L"\\\\?\\", 0) == 0) {
        return native.substr(4);
    }
    return native;
}

/// 诊断用脱敏呈现路径（display——UTF-8、去扩展前缀；敏感值进用户文案前
/// 仍须经 diagnostics 脱敏设施——§10.3，本函数只负责 \\?\ 剥离这一层）。
inline std::string displayOf(const std::filesystem::path& p)
{
    return wideToUtf8(stripExtendedPrefix(p.wstring()));
}

// =====================================================================
// OS 错误四分类（§4.2.5——互斥稳定错误；写方向做 READONLY 属性判别）
// =====================================================================

/// 目标路径是否带只读属性（写方向 ACCESS_DENIED 的 READONLY 判别源——
/// §4.2.5 READONLY 行；V24 码区分断言的语义依据）。
inline bool hasReadonlyAttribute(const std::wstring& nativePath)
{
    const DWORD attr = ::GetFileAttributesW(nativePath.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY) != 0;
}

/// Win32 错误码 → io 资源错误码（§4.2.5 四分类；写方向做 READONLY 判别）。
///
/// 注意：本映射只产出 IO-RES-\* 族。包导入展开写失败中的"磁盘满"
/// （ERROR_DISK_FULL）按 §7.4 威胁矩阵归 IO-PACK-DISK-FULL（包过程族），
/// 由调用方（Package.cpp 展开写面）在调用本映射**之前**截获特判——
/// 四分类映射本身不混入包过程码（§2.5 正交：环境错误≠包事务错误，
/// 两码的用户建议动作不同：换介质/位置 vs 清理空间重试）。
inline IoErrorCode classifyWin32Error(DWORD win32Error, bool writeDirection,
                                      const std::wstring& nativePath)
{
    switch (win32Error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return IoErrorCode::ResNotFound;    // §4.2.5 分类一（用户修正后重试）
    case ERROR_WRITE_PROTECT:               // 介质写保护——READONLY 直判
    case ERROR_NOT_READY:                   // 介质不可用（软只读族）——换介质语义
        return IoErrorCode::ResReadonly;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
        return IoErrorCode::ResLockConflict; // §4.2.5 分类四（io 绝不删对方锁）
    case ERROR_ACCESS_DENIED:
    case ERROR_PRIVILEGE_NOT_HELD:
        // 写方向＋目标实体带只读属性 → IO-RES-READONLY（V24）；读方向
        // 一律 ACCESS-DENIED（V25：不降级为 NOT-FOUND）。
        if (writeDirection && hasReadonlyAttribute(nativePath)) {
            return IoErrorCode::ResReadonly;
        }
        return IoErrorCode::ResAccessDenied;
    default:
        // 四分类外保守归 ACCESS-DENIED（"无法访问"语义最近——Csv.cpp
        // mapSystemError 同款兜底），OS 原码进 params 供开发定位。
        return IoErrorCode::ResAccessDenied;
    }
}

/// 构造带定位与 OS 原码的 IoError（§4.2.5 全表落地；path 参数以脱敏
/// display 形态携带——IoError.hpp 头注"敏感性约束"）。
inline IoError makeOsError(DWORD win32Error, bool writeDirection,
                           const std::filesystem::path& target, std::string context)
{
    IoError e;
    e.code = classifyWin32Error(win32Error, writeDirection, target.wstring());
    e.params.emplace_back("path", displayOf(target));
    e.params.emplace_back("direction", writeDirection ? "write" : "read");
    e.params.emplace_back("os-error", std::to_string(static_cast<unsigned long>(win32Error)));
    e.detail = std::move(context);
    return e;
}

// =====================================================================
// 会话随机标签（§7.5 临时区 8hex——唯一性要求，非身份要求）
// =====================================================================

/**
 * @brief 产出 8 位小写十六进制会话标签（临时区后缀/暂存文件后缀）。
 *
 * 熵来源＝进程级 PRNG（random_device 播种＋steady_clock 混入＋原子计数
 * 器防同 tick 重复）；仅在创建时消费一次，之后是磁盘上的既有名字。
 * 名字唯一性服务于"同目录并发会话不碰撞"，不承载任何身份/寻址语义
 * （SP-5：路径不作身份）。
 */
std::string randomHex8();

} // namespace sdurws::ird::io::platform

#endif // SDURWS_IRD_IO_SRC_IOPLATFORM_HPP
