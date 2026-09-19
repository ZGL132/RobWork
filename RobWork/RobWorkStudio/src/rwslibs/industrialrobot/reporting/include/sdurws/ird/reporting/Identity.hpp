/**
 * @file   Identity.hpp
 * @brief  reporting 身份类型——ReportId（rpt- tag）＋ReportLevel＋
 *         PublishedReportRecord（内存侧发布事实）。
 *
 * 设计依据：
 *   - units/reporting.md §4.1（身份、版本与三个"报告身份"概念的区别——四
 *     概念互不替代的身份纪律）、§3.1 组成表（`Identity.hpp`｜ReportId／
 *     PublishedReportRecord／ReportLevel）、§7.3（PublishedReportRecord 六
 *     字段原文；报告工件状态图——archiveState 恒 Finalized 的语义来源）、
 *     §4.2（contentIdentity 非零——合法实例纪律）
 *   - 需求 RPT-01（报告对象身份/幂等导出冲突拒绝的数据基础）、CON-05
 *     （内容寻址——内容身份消费 core 唯一算法）、NFR-COR-02（确定性）
 *   - 任务契约 tasks/foundation/RPT-T02.json acceptance 1（往返/token/保留值/
 *     四概念类型边界锁定）、acceptance 4（PublishedReportRecord 字段与
 *     §7.3 一致；摘要一律 SHA-256 经 core ContentDigester，比较用字节等值）
 *     与 acceptance 5（P-RPT-3 自建 tag 类型；P-RPT-9 消费基线）
 *
 * 背景说明（四概念身份纪律，§4.1——本头是"报告侧身份"的类型承载）：
 *   ReportId＝"这是哪一份报告对象"（构建成功时一次分配）——**不承载内容
 *   信息**；同数据源重建报告＝新 ReportId，幂等判定**不用它**。
 *   contentIdentity（core::ContentIdentity）＝"报告全部语义内容的字节摘要"
 *   ——幂等导出与冲突判定的**唯一**依据（§7.4）；与磁盘路径
 *   （reports/<report-id>/）和显示名称（报告标题）分离——重命名/移动不
 *   改变任何身份。四者（ReportId/reportVersion/dataIdentity/contentIdentity）
 *   互不替代——本头以"ReportId 与 core::ContentIdentity 无任何互转"的
 *   强类型边界把该纪律钉进类型系统（契约测试 static_assert 常驻自证）。
 *
 * P-RPT-3 处置（acceptance 5）：core.md §4.1 六类型无 ReportId——ReportId
 *   为 reporting 层**自建** tag 类型（diagnostics FindingId `fnd-` 同案），
 *   句法/保留值/强类型约定遵循 core §4.1 U-1（Id128 形）但**自建实现**：
 *   不调用 core 头内 detail 命名空间（core 头注释明言 detail 非公共契约
 *   ——R-2 纪律，跨单元只消费公共面），不等待 core 收编、不私改 core
 *   公共头；core 收编时（建议一并处理 fnd-/rpt-）再按其公共面增量切换。
 *
 * P-RPT-9 处置（acceptance 5）：core 契约消费（core::ContentIdentity/
 * Digest256）以 core.md v0.1（Draft）签名为基线（本头仅消费 Digest.hpp
 * 公共值类型——ContentIdentity；SHA-256 计算经 core ContentDigester 由
 * 构建器/归档协调任务在上游完成）；core 冻结出 diff 后按影响面增量同步
 * 留痕（DTB §5.4），不私改 core。
 *
 * 线程安全：generate() 使用 thread_local 引擎（多线程并发生成安全）；
 * 其余为纯值操作（无共享状态）。PublishedReportRecord 为不可变值对象
 * （构造后无 setter——发布事实一经产生不改写，PA-2 精神）。
 */

#ifndef SDURWS_IRD_REPORTING_IDENTITY_HPP
#define SDURWS_IRD_REPORTING_IDENTITY_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>   // std::hash 特化
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // core::ContentIdentity（P-RPT-9 基线：core.md v0.1 §4.2）

namespace sdurws::ird::reporting {

// =====================================================================
// ReportId（§4.1 第一行——Id128 形 tag 类型，P-RPT-3 自建）
// =====================================================================

/**
 * @brief 报告对象身份（规范文本 "rpt-<32hex>"，Id128 形——core §4.1 U-1 同约定）。
 *
 * 回答"这是哪一份报告对象"：构建成功时一次分配；不承载内容信息——
 * 同数据源重建报告＝新 ReportId（幂等判定不用它，用内容身份——§4.1
 * 身份纪律；幂等/冲突判定的承载见 PublishedReportRecord::contentIdentity）。
 *
 * 句法约定（与 core Id128 逐条同型——自建实现，P-RPT-3）：
 *   - 规范文本＝"rpt-" 前缀＋恰 32 个小写十六进制字符（大写拒绝、无空白、
 *     无前后缀）；总长固定 36。
 *   - 保留值：全零字节＝"空/未设置"，isValid() 恒 false；generate() 保证
 *     非零（零则重取）。
 *   - 字节序＝规范文本序（parse/format 对称——parse(format(x))==x 往返）。
 *   - 强类型：与 core::ContentIdentity（32 字节）及 core 六 Id128 类型
 *     （16 字节 tag 各异）均无互转——报告身份不与任何他类身份互换。
 *
 * 比较：字节精确相等（附录 D 第 12 项精神：无容差）；operator< 仅为
 * 容器键提供字典序，无业务排序语义。
 */
struct ReportId {
    /// 128 位原始字节；全零＝空（保留值纪律）。字节序＝规范文本序。
    std::array<std::uint8_t, 16> bytes{};

    /// 生成非零随机新值（thread_local mt19937_64，random_device 播种；零则
    /// 重取——保留值不可出现）。分配时机归构建器（构建成功时一次——§4.1
    /// "谁产生"列；本类型只提供生成原语）。线程安全。
    static ReportId generate();

    /// 严格解析 "rpt-<32 小写 hex>"；tag 不符/长度/字符集违约抛
    /// ReportError(ReportErrorCode::DataInvalid)——解码边界（持久化文本/
    /// 导出物回读）上的 fail-fast；可恢复路径用 tryFromCanonical
    /// （§1.4 异常行 try* 双轨约定）。
    static ReportId fromCanonical(std::string_view text);

    /// try 轨：解析失败返回 nullopt 不抛（可恢复查询路径——§1.4 约定）。
    static std::optional<ReportId> tryFromCanonical(std::string_view text) noexcept;

    /// 规范文本 "rpt-<32 小写 hex>"（与 parse 构成 parse(format(x))==x 往返）。
    std::string toCanonical() const;

    /// 非全零（保留值恒 false——默认构造即"空/未设置"）。noexcept 纯值比较。
    bool isValid() const noexcept;

    /// 字节精确相等（无容差——§4.1 比较纪律）。
    bool operator==(const ReportId& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const ReportId& o) const noexcept { return !(*this == o); }
    /// 字节字典序（容器键用；无业务排序语义）。
    bool operator<(const ReportId& o) const noexcept { return bytes < o.bytes; }
};

// =====================================================================
// ReportLevel（§4.2 level 字段——RPT-01 分级子级的类型承载）
// =====================================================================

/**
 * @brief 报告级别（§4.2 字段表：`B`/`C`，token `level-b`/`level-c`）。
 *
 * RPT-01-B（基础级，8 章节）/RPT-01-C（正式评审级，14 章节＋签署块）的
 * 报告侧分级值；级别决定章节合法组合（§4.6 矩阵）与范围契约（§5）——
 * 级别校验语义归 Sections/Builder（RPT-T04/T05），本类型只承载值面与
 * 稳定 token（持久化/渲染层的级别表达）。
 *
 * token 句法：恰为 "level-b"/"level-c" 全小写字面（大小写敏感——与
 * Id128 小写 hex 同一口径：规范文本唯一形态，非规范输入在解析边界拒绝
 * 而非归一化，避免"宽容入口"造成持久化文本二义）。无保留值——两级均
 * 有效（枚举域即合法域）。
 */
enum class ReportLevel : std::uint8_t {
    B,   ///< 基础级报告（RPT-01-B；token "level-b"）
    C,   ///< 正式评审级报告（RPT-01-C；token "level-c"）
};

/**
 * @brief 取报告级别的稳定 token（§4.2 字段表原文）。
 * @param level [in] 报告级别（全枚举 2 值均有 token）
 * @return "level-b"/"level-c"（静态存储期；同值同串——NFR-COR-02）
 */
std::string_view token(ReportLevel level) noexcept;

/**
 * @brief try 轨解析级别 token：恰接受 "level-b"/"level-c"，其余（含大小写
 *        变体、空白、前后缀）返回 nullopt 不抛（可恢复路径——§1.4 约定）。
 */
std::optional<ReportLevel> tryLevelFromToken(std::string_view token) noexcept;

/**
 * @brief 抛出轨迹解析级别 token（与 tryLevelFromToken 同判据）。
 * @throws ReportError(ReportErrorCode::DataInvalid) token 非规范两级字面
 *         （解码边界 fail-fast——持久化文本中的级别字段非法）。
 */
ReportLevel levelFromToken(std::string_view token);

// =====================================================================
// ReportArchiveState＋PublishedReportRecord（§7.3 内存侧发布事实）
// =====================================================================

/**
 * @brief 报告工件归档状态（§7.3 PublishedReportRecord 的 archiveState 承载）。
 *
 * 单值枚举是刻意的类型系统约束：§7.3 状态图全貌（无→Staged→Partial→
 * Finalized）是**磁盘工件目录**生命周期（project 实现——D-13/D-14 模式，
 * Staged/Partial 从不经 reporting 类型表达）；而本记录是**内存侧发布
 * 事实**——仅在发布成功后存在，archiveState 恒为 Finalized（§7.3 原文
 * "archiveState(Finalized)"）。单值枚举把"记录不存在半成品"（§4.2 生成
 * 状态表达方式②"文件存在≠已发布"的内存侧对偶）钉进类型：不存在携带
 * 其他归档状态的 PublishedReportRecord。
 */
enum class ReportArchiveState : std::uint8_t {
    Finalized,   ///< manifest 原子发布完成＝完整（唯一合法值——见类注释）
};

/**
 * @brief 已发布报告的内存侧记录（§7.3 原文六字段——"reporting 返回给
 *        调用方"的发布事实值）。
 *
 * 字段与 §7.3 原文逐字对应（acceptance 4）：
 *   {reportId, contentIdentity, archiveState(Finalized), manifestDigest,
 *    artifactRelPaths, publishedAtUtc}
 *
 * 语义锚点：
 *   - contentIdentity：报告全部语义内容的字节摘要——幂等导出与冲突判定
 *     的**唯一**依据（§4.1/§7.4）；与 ReportId（对象身份）、磁盘路径、
 *     显示名称分离。类型＝core::ContentIdentity（SHA-256 经 core
 *     ContentDigester 唯一算法——§4.1 身份纪律＋CR-02 同源；计算归构建器
 *     ReportCodec，RPT-T03），比较用字节等值（ContentIdentity::operator==）。
 *   - manifestDigest：ReportArtifactManifest 自身规范化编码的摘要（§7.3
 *     "自身规范化摘要"——D-14 幂等重投递判据同构 project RunManifest；
 *     SHA-256 经 core ContentDigester，计算归归档协调任务 RPT-T09）。
 *     类型取 core::ContentIdentity 而非字符串/裸数组：以字节身份承载
 *     "比较用字节等值"（§4.1）——与 project 持久化 DTO 的 hex 串字段
 *     （PersistenceFormat.hpp）分工＝内存值对象持字节身份、序列化形态归
 *     写入端口协议。
 *   - artifactRelPaths：工件相对路径清单（report.json 相对路径——
 *     "文件存在≠已发布"：路径是**工件事实**不是身份，不入任何身份比较
 *     语义；报告归档目录 reports/<report-id>/ 由 ReportId 编址——§4.1
 *     "内容身份与磁盘路径分离"）。
 *   - publishedAtUtc：manifest 原子发布完成时刻（UTC——time_point 无包装
 *     类型，core D-02 同源口径）。
 *
 * 生命周期/不可变性：构造后无 setter（值语义）；记录一经产生不改写——
 * 报告值对象与发布记录都不可因后续操作回写（PA-2/§4.2"报告值对象永不
 * 因发布回写"）。所有权：调用方（构建/归档协调返回值）持有。
 *
 * 线程安全：不可变值对象——并发只读安全。
 */
struct PublishedReportRecord {
    /// 报告对象身份（"rpt-<32hex>"——编址 reports/<report-id>/ 的键）。
    ReportId reportId{};
    /// 内容身份（幂等/冲突判定唯一依据——字节等值比较）。
    core::ContentIdentity contentIdentity{};
    /// 归档状态（恒 Finalized——单值枚举约束，见 ReportArchiveState 注释）。
    ReportArchiveState archiveState = ReportArchiveState::Finalized;
    /// manifest 自身规范化摘要（D-14 幂等重投递判据——字节等值比较）。
    core::ContentIdentity manifestDigest{};
    /// 工件相对路径清单（工件事实，非身份；发布成功至少含 manifest 清单
    /// 内工件——非空由归档协调任务 RPT-T09 保证，本值类型不重复校验）。
    std::vector<std::string> artifactRelPaths{};
    /// 发布完成时刻（UTC；system_clock time_point——core D-02 同口径）。
    std::chrono::system_clock::time_point publishedAtUtc{};

    /**
     * @brief 记录合法性（身份字段纪律的集合判定——§4.2 合法实例的发布侧
     *        对偶）。
     *
     * 判据（逐条）：reportId 非零（保留值纪律）；contentIdentity 非零
     * （§4.2 合法实例"contentIdentity 非零"——零内容身份＝没有可判定
     * 幂等的凭据）；manifestDigest 非零（无摘要＝无幂等重投递判据）；
     * archiveState == Finalized（类型系统恒真——显式保留判定以呼应
     * §7.3"唯一完整标志"语义，防御未来字段扩展时静默失守）。
     * artifactRelPaths 非空不在此判定（归档协调任务职责——见字段注释）。
     *
     * @return true＝身份字段齐备的 Finalized 发布记录
     */
    bool isValid() const noexcept
    {
        return reportId.isValid() && contentIdentity.isValid()
               && manifestDigest.isValid() && archiveState == ReportArchiveState::Finalized;
    }

    /// 全字段精确相等（摘要为字节等值；时间点精确相等——附录 D 第 12 项
    /// 精神：身份/事实字段无容差）。C++17 显式逐成员（不用 C++20 默认
    /// 比较——core.md D-01 语言约束，evidence 同款口径）。
    bool operator==(const PublishedReportRecord& o) const
    {
        return reportId == o.reportId && contentIdentity == o.contentIdentity
               && archiveState == o.archiveState && manifestDigest == o.manifestDigest
               && artifactRelPaths == o.artifactRelPaths
               && publishedAtUtc == o.publishedAtUtc;
    }
    bool operator!=(const PublishedReportRecord& o) const { return !(*this == o); }
};

}  // namespace sdurws::ird::reporting

// ---- std::hash 特化（哈希容器键用——散列质量非契约项，core 同款声明） ----
namespace std {

template <> struct hash<sdurws::ird::reporting::ReportId> {
    /// FNV-1a 64 over 16 字节（同字节同哈希、平台无关——uint64 定宽）。
    /// 仅为哈希表散列用，非密码学承诺；身份比较一律走 operator== 字节等值。
    std::size_t operator()(const sdurws::ird::reporting::ReportId& id) const noexcept
    {
        constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;   // FNV-1a 64 素数
        std::uint64_t h = 0xcbf29ce484222325ULL;                // FNV-1a 64 偏移基
        for (const std::uint8_t b : id.bytes) {
            h ^= b;
            h *= kFnvPrime;
        }
        return static_cast<std::size_t>(h);
    }
};

}  // namespace std

#endif  // SDURWS_IRD_REPORTING_IDENTITY_HPP
