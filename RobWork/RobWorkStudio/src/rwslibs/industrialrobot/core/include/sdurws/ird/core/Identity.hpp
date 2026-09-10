/**
 * @file   Identity.hpp
 * @brief  身份基础类型——六个 Id128 强类型＋AttemptId＋TaskIdentity 五元组。
 *
 * 设计依据：
 *   - units/core.md §4.1（U-1 Id128 表示与保留值纪律、强类型表、四概念区分）、
 *     §5.1（接口签名与前置/后置/错误）、§8（UT-ID-T 用例矩阵）
 *   - 需求 ARC-04（稳定对象身份、精确等值——附录 D 第 12 项无容差）、CON-01
 *     （身份/版本包络）、TASK-03（任务身份五元组）
 *   - 任务契约 tasks/foundation/CORE-T02.json（≙WP-03-T02）
 *
 * 背景说明（四概念区分，§4.1.1——防止"身份"一词混用）：
 *   对象身份 ObjectId＝"这是哪一个逻辑对象"（跨修订稳定）；
 *   修订身份 RevisionId＝"哪一次命令提交产生此状态"（一次提交一个，PA-2 只增不改）；
 *   内容版本/内容身份（§4.2 Digest.hpp）＝"内容哪一版/整体字节摘要"。
 *   四者互不替代：同内容不同对象可共存（对象身份不承载内容信息）。
 *
 * 强类型纪律（§4.1 硬约束）：六个 Id128 类型各自独立 struct，仅 tag 不同
 * （obj-/prj-/brn-/rev-/run-/evt-），互无 using 别名、互无隐式转换——把
 * rev-… 文本喂给 ObjectId::fromCanonical 在解析边界即失败，这是防误用特性。
 *
 * 保留值纪律：全零字节＝"空/未设置"，isValid() 恒 false；generate() 保证非零。
 * 分配策略（谁在何时生成）归各所有者单元：对象创建→project；运行派发→execution。
 *
 * 线程安全：generate() 使用 thread_local 引擎（多线程并发生成安全）；
 * 其余为纯值操作（无共享状态）。
 *
 * 单调序号不在 RevisionId 内（D-04）：修订顺序/父修订/命令摘要归 project 修订记录，
 * core 只提供不透明稳定身份——避免双权威。
 */

#ifndef SDURWS_IRD_CORE_IDENTITY_HPP
#define SDURWS_IRD_CORE_IDENTITY_HPP

#include <array>
#include <cstdint>
#include <functional>   // std::hash 特化
#include <optional>
#include <string>
#include <string_view>

namespace sdurws::ird::core {

/**
 * @brief Id128 级共享实现细节（解析/格式化/生成）。
 *
 * 放 detail 命名空间＝非公共契约（R-2 纪律：私有细节不入跨单元承诺）；
 * 六个强类型的公共接口逐一转调本层，tag 由各类型自己的工厂给定——
 * 保证六类型的解析严格性与格式化规则单点实现、不漂移。
 */
namespace detail {

/// 严格解析 "<tag><32 个小写十六进制>"：tag 逐字符匹配、长度恰 32、
/// 字符集仅 [0-9a-f]（大写拒绝）、无前后缀/空白。失败返回 false（try 轨），
/// 抛出轨迹（fromCanonical）由调用方对 false 抛 CoreError("core/identity/parse: …")。
bool tryParseId128(std::string_view tag, std::string_view text,
                   std::array<std::uint8_t, 16>* out) noexcept;

/// 格式化为 "<tag><32 个小写十六进制>"（bytes 按字节序直出——generate 与
/// parse 使用同一字节序，保证 parse(format(x))==x 往返）。
std::string formatId128(std::string_view tag, const std::array<std::uint8_t, 16>& bytes);

/// FNV-1a 128 位哈希（§4.1：std::hash 规定算法）——以两个 64 位半字模拟
/// 128 位状态（模 2^128 乘法经移位加法实现，FNV-128 素数＝2^88+2^8+0x3B）。
/// 仅为哈希表散列用，非密码学承诺；同字节同哈希、平台无关（uint64 定宽）。
void fnv1a128(const std::uint8_t* data, std::size_t n,
              std::uint64_t* outHi, std::uint64_t* outLo) noexcept;

}  // namespace detail

// =====================================================================
// 六个 Id128 强类型：同形独立 struct，仅 tag 不同。
// 以宏展开保证六份契约逐字一致（§5.1"成员与 ObjectId 逐一相同"）；
// 宏是展开手段而非别名——六类型仍互无转换关系，强类型纪律不受影响。
// 每类型的 tag/含义/分配者差异见各展开点处的注释（§4.1 类型表原文）。
// =====================================================================

/// \def IRD_CORE_DEFINE_ID_TYPE(TYPE_NAME, TAG_LIT, DOC)
/// 展开一个 Id128 强类型：generate/fromCanonical/tryFromCanonical/toCanonical/
/// isValid/==/</bytes＋std::hash 特化。契约同 units/core.md §5.1 ObjectId 行。
#define IRD_CORE_DEFINE_ID_TYPE(TYPE_NAME, TAG_LIT, DOC)                              \
    /** DOC */                                                                        \
    struct TYPE_NAME {                                                                \
        /** 128 位原始字节；全零＝空（保留值纪律，§4.1 U-1）。字节序＝规范文本序。 */     \
        std::array<std::uint8_t, 16> bytes{};                                         \
        /** 生成非零随机新值（thread_local mt19937_64；零则重取——保留值不可出现）。线程安全。 */ \
        static TYPE_NAME generate();                                                  \
        /** 严格解析 "#TAG_LIT<32 小写 hex>"；tag 不符/长度/字符集违约抛 CoreError（§5.1 错误行）。 */ \
        static TYPE_NAME fromCanonical(std::string_view text);                        \
        /** try 轨：解析失败返回 nullopt 不抛（io 错误收集场景，REQ-05/AT-02）。 */      \
        static std::optional<TYPE_NAME> tryFromCanonical(std::string_view text) noexcept; \
        /** 规范文本 "#TAG_LIT<32 小写 hex>"（与 parse 构成 parse(format(x))==x 往返）。 */ \
        std::string toCanonical() const;                                              \
        /** 非全零（保留值恒 false）。noexcept 纯值比较。 */                            \
        bool isValid() const noexcept;                                                \
        /** 字节精确相等（附录 D 第 12 项：无容差）。 */                                 \
        bool operator==(const TYPE_NAME& o) const noexcept { return bytes == o.bytes; } \
        bool operator!=(const TYPE_NAME& o) const noexcept { return !(*this == o); }   \
        /** 字节字典序（容器键用；§5.1 规定 operator< 存在）。 */                        \
        bool operator<(const TYPE_NAME& o) const noexcept { return bytes < o.bytes; }  \
    };                                                                                \

// 逐类型展开（注释＝§4.1 类型表行原文的浓缩；分配者字段供阅读，不改变 core 行为）：
IRD_CORE_DEFINE_ID_TYPE(ObjectId, "obj-",
    /** 持久化对象稳定身份，跨修订不变（ARC-04/§6.2）；分配者＝project（对象创建时一次）；
        编码/编址归 project/io，名称映射归 runtime。tag＝"obj-"。 */
);
IRD_CORE_DEFINE_ID_TYPE(ProjectId, "prj-",
    /** 项目身份（project.json 创建期一次写入；另存为换新 id，PM-05）；分配者＝project。tag＝"prj-"。 */
);
IRD_CORE_DEFINE_ID_TYPE(BranchId, "brn-",
    /** 方案分支身份（PM-12：分支记录 baseRevisionId）；分配者＝project；分支表归 project。tag＝"brn-"。 */
);
IRD_CORE_DEFINE_ID_TYPE(RevisionId, "rev-",
    /** 修订身份（一次命令提交＝一个修订，ARC-01）；分配者＝project。
        单调序号不在 id 内（D-04）——顺序/父修订/命令摘要归 project 修订记录。tag＝"rev-"。 */
);
IRD_CORE_DEFINE_ID_TYPE(RunId, "run-",
    /** 一次评估运行身份；分配者＝execution（派发时）；RunRegistry 登记规则归 execution。tag＝"run-"。 */
);
IRD_CORE_DEFINE_ID_TYPE(EventId, "evt-",
    /** 领域事件身份（投递去重/日志关联）；分配者＝发布方（project/execution）。tag＝"evt-"。 */
);

/// 尝试序号：同一 Run 的第几次尝试（§4.1；分配/递增/取代标记归 execution）。
struct AttemptId {
    std::uint64_t value = 0;   ///< ≥1 合法；0＝空（保留值）——att-0 解析在边界失败

    /// 严格解析 "att-<十进制>"：仅数字、无符号/前导零/空白；0 与 2^64 溢出拒绝。
    static AttemptId fromCanonical(std::string_view text);
    /// try 轨：失败返回 nullopt 不抛。
    static std::optional<AttemptId> tryFromCanonical(std::string_view text) noexcept;
    /// 规范文本 "att-<十进制>"。
    std::string toCanonical() const;
    /// value ≥ 1。
    bool isValid() const noexcept { return value >= 1; }
    bool operator==(AttemptId o) const noexcept { return value == o.value; }
    bool operator!=(AttemptId o) const noexcept { return value != o.value; }
    bool operator< (AttemptId o) const noexcept { return value <  o.value; }
};

/// 任务身份五元组（TASK-03、ARCH §4.5）：请求与完成事件携带的身份组。
/// 接纳判定规则（查表/失配拒绝/幂等）归 execution——core 只定义值语义与精确等值。
struct TaskIdentity {
    ProjectId   project;    ///< 项目身份（五字段之一）
    BranchId    branch;     ///< 方案分支身份
    RevisionId  revision;   ///< 被评估修订身份
    RunId       run;        ///< 本次运行身份
    AttemptId   attempt;    ///< 同 Run 内尝试序号

    /// 五字段全 isValid（缺一即 false——UT-ID-T 钉住）。
    bool isValid() const noexcept;
    /// 五字段全等（附录 D 第 12 项：精确等值，无容差）。
    bool operator==(const TaskIdentity& o) const noexcept;
    bool operator!=(const TaskIdentity& o) const noexcept { return !(*this == o); }
    /// 字典序（project→branch→revision→run→attempt）；仅容器键用，无业务排序语义。
    bool operator<(const TaskIdentity& o) const noexcept;
};

}  // namespace sdurws::ird::core

// ---- std::hash 特化（§4.1：FNV-1a 128 over bytes；五元组＝字段组合） ----
namespace sdurws::ird::core {

/// 六个 Id128 类型的 hash 基模板（FNV-1a 128 的 (hi,lo) 两半异或合并为 size_t）。
/// 置于本命名空间（std 内不放自定义模板）；std::hash 特化经继承取得。
template <typename T> struct IrdCoreIdHash {
    std::size_t operator()(const T& id) const noexcept {
        std::uint64_t hi = 0, lo = 0;
        detail::fnv1a128(id.bytes.data(), id.bytes.size(), &hi, &lo);
        return static_cast<std::size_t>(hi ^ lo);
    }
};

}  // namespace sdurws::ird::core

namespace std {

template <> struct hash<sdurws::ird::core::ObjectId>    : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::ObjectId> {};
template <> struct hash<sdurws::ird::core::ProjectId>   : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::ProjectId> {};
template <> struct hash<sdurws::ird::core::BranchId>    : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::BranchId> {};
template <> struct hash<sdurws::ird::core::RevisionId>  : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::RevisionId> {};
template <> struct hash<sdurws::ird::core::RunId>       : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::RunId> {};
template <> struct hash<sdurws::ird::core::EventId>     : sdurws::ird::core::IrdCoreIdHash<sdurws::ird::core::EventId> {};

template <> struct hash<sdurws::ird::core::AttemptId> {
    /// 64 位值直接散列（0＝空保留值同样可散列——放入容器是调用方决定）。
    std::size_t operator()(const sdurws::ird::core::AttemptId& a) const noexcept {
        return std::hash<std::uint64_t>{}(a.value);
    }
};

template <> struct hash<sdurws::ird::core::TaskIdentity> {
    /// 五字段顺序组合（size_t 溢出按无回绕加法惯例——散列质量非契约项）。
    std::size_t operator()(const sdurws::ird::core::TaskIdentity& t) const noexcept {
        const std::size_t h1 = std::hash<sdurws::ird::core::ProjectId>{}(t.project);
        const std::size_t h2 = std::hash<sdurws::ird::core::BranchId>{}(t.branch);
        const std::size_t h3 = std::hash<sdurws::ird::core::RevisionId>{}(t.revision);
        const std::size_t h4 = std::hash<sdurws::ird::core::RunId>{}(t.run);
        const std::size_t h5 = std::hash<sdurws::ird::core::AttemptId>{}(t.attempt);
        std::size_t h = h1;
        h ^= h2 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= h3 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= h4 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= h5 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

}  // namespace std

#endif  // SDURWS_IRD_CORE_IDENTITY_HPP
