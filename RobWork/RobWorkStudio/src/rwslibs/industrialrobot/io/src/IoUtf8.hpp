/**
 * @file   IoUtf8.hpp
 * @brief  io 单元私有共享头——严格 UTF-8 DFA 校验与系统错误映射的单实现。
 *
 * 设计依据：
 *   - io.md §5.9.1（JSON 编码行：仅 UTF-8，无效序列拒绝）、§5.2（CSV 编码行：
 *     UTF-8/UTF-16LE/BE，BOM 判定）、§4.2.5（系统错误四分类映射）
 *   - 任务契约 tasks/REUSE-T38.json（WP-15 批次B：Json.cpp/Csv.cpp 原各持一份
 *     kUtf8Dfa/strictUtf8Check/mapSystemError 副本，两份 DFA 表逐字节同源、
 *     两份错误映射逐行相同——合流为本单元内单实现，消除平行副本；非新建序列
 *     化框架、非新增依赖）
 *
 * 通道差异语义（刻意保留在各自 TU，本头不承载）：
 *   - JSON 仅 UTF-8：UTF-16 BOM 在 parseBytes 入口剥离判定处即拒绝；
 *     无 BOM 字节流经 strictUtf8Check 全文判定。
 *   - CSV 接受 UTF-16LE/BE：BOM 判定后走 DecodedStream 解码路径，其 UTF-8
 *     形态经 utf8Step 逐字节流式推进（残缺/非法＝-2 稳定拒绝）。
 *
 * 私有性：本头位于 src/（R-2 红线：私有实现头不入 include/ 公共头根），
 * 仅 io 单元实现文件包含；符号带 io 命名空间前缀且 inline（ODR 单实体），
 * 不构成跨单元接口。线程安全：全部纯函数/常量表，无共享可变状态。
 */

#ifndef SDURWS_IRD_IO_IOUTF8_HPP
#define SDURWS_IRD_IO_IOUTF8_HPP

#include <sdurws/ird/io/IoError.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <system_error>

namespace sdurws::ird::io {

// =====================================================================
// 严格 UTF-8 校验 DFA（无 BOM 编码判定——§5.9.1"无效序列拒绝"）
// =====================================================================

/// DFA 状态值：0＝接受态，12＝拒绝态（字节序列非法：超长编码/代理区/
/// 超 U+10FFFF/悬空续字节）。
inline constexpr std::uint32_t kUtf8Reject = 12;

/**
 * UTF-8 严格校验转移表——Björn Höhrmann 的经典压缩 DFA（公共域实现，
 * 语义等价于 WHATWG Encoding Standard 的 UTF-8 解码器：拒绝超长编码、
 * UTF-16 代理区码点与 > U+10FFFF）。用法：
 *   state = utf8Step(state, byte)，逐字节推进；
 *   state == kUtf8Reject 即非法。选用表驱动而非手写分支：转移逻辑唯一、
 *   无分支误写空间，且经年公开检验（确定性 NFR-COR-01 的实现面）。
 * 表数据与合流前 Json.cpp/Csv.cpp 各自副本逐字节一致（合流等价 oracle）。
 */
inline const std::uint8_t kUtf8Dfa[] = {
    // 前 256 项：字节 → 字符类（减少转移表尺寸）。
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
   10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,
    // 后 144 项：状态 × 字符类 → 下一状态。
    0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
   12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
   12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
   12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
   12,36,12,12,12,12,12,12,12,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
    0,12,12,12,12,12,12,12, 0,12,12,12, 12,12,12,12, 0,12,12,12,12,12,12,12,
    0,12,12,12,12,12,12,12, 0,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
   12,12,12,12,12,12,12,12,12,12,12,12, 12,12,12,12,12,12,12,12,12,12,12,12,
};

/**
 * @brief DFA 单步转移（JSON 全文校验与 CSV 流式校验共用的唯一推进式）。
 * @param state [in] 当前状态（0..12；kUtf8Reject 后不再推进）
 * @param b     [in] 输入字节
 * @return 下一状态；kUtf8Reject＝序列非法
 */
inline std::uint32_t utf8Step(std::uint32_t state, std::uint8_t b)
{
    return kUtf8Dfa[256 + state + kUtf8Dfa[b]];
}

/**
 * @brief 严格 UTF-8 校验（全文一遍 DFA）——JSON 通道入口判定。
 *
 * JSON 通道与 CSV 通道的编码语义差异（§5.9.1 vs §5.2）：CSV 接受
 * UTF-16LE/BE（BOM 判定后解码）；JSON **仅 UTF-8**——UTF-16 BOM 在
 * parseBytes 入口剥离判定处即被拒绝，无 BOM 的任意字节流经本函数判定，
 * 非法序列＝IO-FORMAT-JSON-ENCODING 稳定拒绝。
 *
 * @param bytes     [in] 待校验字节流（BOM 剥离后）
 * @param failIndex [out] 首个非法字节的下标（0 起——编码错误定位用）
 * @return true＝合法 UTF-8；false＝非法（failIndex 有效）
 */
inline bool strictUtf8Check(std::string_view bytes, std::size_t* failIndex)
{
    std::uint32_t state = 0;   // 起始＝接受态
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const std::uint8_t b = static_cast<std::uint8_t>(bytes[i]);
        state = utf8Step(state, b);
        if (state == kUtf8Reject) {
            *failIndex = i;
            return false;
        }
    }
    // 悬空多字节序列（文档截断在多字节字符中段）同样非法——终态非接受。
    if (state != 0) {
        *failIndex = bytes.size();
        return false;
    }
    return true;
}

// =====================================================================
// std::error_code → io 资源错误码（文件读失败四分类——§4.2.5）
// =====================================================================

/**
 * @brief std::error_code → io 资源错误码（JSON/CSV 通道同一映射：
 *        不存在→NOT-FOUND、只读介质→READONLY、锁竞争→LOCK-CONFLICT、
 *        磁盘满→PACK-DISK-FULL、其余保守归 ACCESS-DENIED——"无法访问"
 *        语义最近）。四分类与诊断码语义与合流前两份副本逐行相同。
 */
inline IoErrorCode mapSystemError(const std::error_code& ec)
{
    if (ec == std::errc::no_such_file_or_directory) {
        return IoErrorCode::ResNotFound;
    }
    if (ec == std::errc::no_space_on_device || ec == std::errc::file_too_large) {
        return IoErrorCode::PackDiskFull;
    }
    if (ec == std::errc::read_only_file_system) {
        return IoErrorCode::ResReadonly;
    }
    if (ec == std::errc::device_or_resource_busy || ec == std::errc::no_lock_available
        || ec == std::errc::resource_unavailable_try_again) {
        return IoErrorCode::ResLockConflict;
    }
    return IoErrorCode::ResAccessDenied;
}

}  // namespace sdurws::ird::io

#endif  // SDURWS_IRD_IO_IOUTF8_HPP
