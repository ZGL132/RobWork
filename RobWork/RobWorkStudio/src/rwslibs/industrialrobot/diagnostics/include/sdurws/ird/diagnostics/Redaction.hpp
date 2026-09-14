/**
 * @file   Redaction.hpp
 * @brief  脱敏服务（Redaction）——路径四策略/凭据模式零记录/降级不放行/
 *         安全摘要导出（IRedactionService §9.5）。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.7（脱敏规则表——路径/用户名/环境变量/资源内容/
 *     命令载荷/凭据/降级/幂等/导出双保险，全部实现承载行）、§7.3②（管线脱敏
 *     步骤：命中替换 [REDACTED:<kind>:n]；脱敏器自身失败降级
 *     [REDACTED:redaction-failed]＋DIAG-REDACTION-FAILED 开发诊断——绝不放行
 *     原文）、§9.5（IRedactionService 接口原文与契约表）
 *   - 需求 NFR-SEC-07（不记录未经允许的敏感路径、用户数据或完整外部资源
 *     内容；凭据一律不记录）、RPT（exportSafeSummary 双保险入口）、AT-11
 *     观测点（崩溃诊断文件脱敏——凭据零命中、路径按配置）
 *   - 任务契约 tasks/foundation/DIAG-T08.json（≙WP-09-T05）：acceptance 1
 *     （DT-SEC-1~4）、acceptance 4（P-DIAG-1——Hash 路径策略的 SHA-256 只经
 *     core ContentDigester，CR-02 同源纪律）
 *
 * 背景说明（为什么脱敏是"纯函数＋绝不抛出"）：
 *   日志与崩溃文件是排障快照而非权威数据（§7.8），脱敏是它们的最后防线：
 *   防线自身绝不能成为新的故障面。§9.5 契约表因此冻结三条铁律——①纯函数
 *   （同输入同输出，确定性可测——DT-SEC-1）；②绝不抛出（任何内部失败一律
 *   降级为整条 [REDACTED:redaction-failed]，宁可信息缺失不泄露——保守方向，
 *   §7.3②）；③单向（无逆接口，脱敏后文本不可反推原文）。凭据模式清单是
 *   安全契约（只增不减），路径策略枚举冻结（§9.5 稳定性行）。
 *
 * Tier 语义（§7.3②"每条消息强制经过；Tier-U 额外过滤栈/哈希/内部 token
 * 模式"的分工口径——§14.4 v0.9 实现口径登记）：
 *   - redact(raw, LogTier::Dev)：NFR-SEC-07 全量脱敏（凭据/令牌形态/环境
 *     变量/用户名/路径按策略）——两级日志管线步骤②统一以 Dev 档调用（两
 *     Tier 同一脱敏管线；Tier-U 文件的"额外内部模式过滤"仍由管线呈现层承
 *     担，保持 DIAG-T07 v0.8"Tier-D 保留原文、Tier-U 用过滤呈现"的镜像写
 *     入语义不变）；
 *   - redact(raw, LogTier::User)：全量脱敏＋内部 token 模式过滤（0x 地址/
 *     ≥32 位十六进制/栈帧）——供报告类直接消费方（exportSafeSummary 双保
 *     险、安全摘要）使用：报告外发不得携带调用栈/内存地址/内容哈希
 *     （NFR-REL-05/UX-02）。
 *
 * 陷阱处置锚点（契约 knownPitfalls）：
 *   - P-DIAG-1：Hash 路径策略的 SHA-256 只经 core ContentDigester（core.md
 *     v0.1 基线；CR-02 同源纪律——不私设第二哈希路径）；core 冻结 diff 后
 *     增量同步，不私改 core。
 *
 * 线程安全：redact/redactPath/safeSummary 并发安全（模式表构造后只读；策
 * 略快照读——setPolicy 写时拷贝切换，§9.5 契约表）；setPolicy 与 stats 任
 * 意线程。进程级生命周期（§9.5）——注入 failureSink（开发诊断路由）的引用
 * 须覆盖本服务生命周期，本类不接管所有权。
 * 确定性：全部规则为纯文本扫描——不查询进程环境（同输入同输出与环境无关，
 * NFR-COR-01）；替换计数 n 按单次调用内逐类从 1 递增（同输入同输出的组成
 * 部分），累计命中面经 stats() 观测（R-7"模式命中计数可观测"）。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_REDACTION_HPP
#define SDURWS_IRD_DIAGNOSTICS_REDACTION_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <sdurws/ird/diagnostics/Catalog.hpp>   // IDevLogSink（降级开发诊断路由——§7.3②）
#include <sdurws/ird/diagnostics/Logging.hpp>   // LogTier（§9.5 redact 签名——Tier 语义见文件头）

namespace sdurws::ird::diagnostics {

// =====================================================================
// 词表与常量（§7.7/§9.5 原文）
// =====================================================================

/**
 * @brief 脱敏器保留的内部通道 token（降级开发诊断的 channel——§7.3②
 *        "DIAG-REDACTION-FAILED 开发诊断"的路由面）。
 *
 * 日志管线对该通道的行跳过步骤②脱敏（内容为本设施自产常量文本、绝不携带
 * 用户原文——管线侧旁路清单登记于单元卡 §14.4 v0.9；自省行若再过脱敏，其
 * 自身失败会形成入队回环）。通道词表开放（§7.2 LogChannel），本值登记为
 * 脱敏设施保留值。
 */
inline constexpr std::string_view kRedactionInternalChannel = "diag/redaction";

/// 脱敏自身失败的整条降级字面量（§7.3② 原文 "[REDACTED:redaction-failed]"；
/// 安全契约——文本冻结不改）。
inline constexpr std::string_view kRedactionFailedToken = "[REDACTED:redaction-failed]";

/**
 * @brief 路径脱敏策略（§9.5 原文 enum class PathPolicy { Keep, Hash,
 *        RootOnly, Strip }；§7.7 路径行逐值语义）。
 *
 * 枚举冻结（§9.5 稳定性行"路径策略枚举冻结"）：
 *   - Keep：原文保留（默认开发机口径——信任本机排障面）；
 *   - Hash：整路径替换为 "[PATH-<16 位小写 hex>]"（SHA-256 前 8 字节——同
 *     路径同 token 可关联、不可逆推；§14.4 v0.9 口径：16 位＝64 位截断，
 *     避开日志管线 Tier-U"≥32 位十六进制→[HASH]"呈现过滤与规范身份
 *     "<tag>-<32 hex>"形态，防二次改写）；
 *   - RootOnly：保留盘符＋一级目录＋"…"＋文件名（如 D:\data\proj\m.stl →
 *     D:\data\…\m.stl；§7.7 文本口径"保留盘符＋一级目录"——卡内示例
 *     D:\…\file.stl 与该文本不一致，按文本实现并登记 §14.4 v0.9；默认值）；
 *   - Strip：仅文件名（…\m.stl → m.stl）。
 */
enum class PathPolicy : std::uint8_t {
    Keep,     ///< keep——原文保留（开发机）
    Hash,     ///< hash——SHA-256 截断 token（[PATH-<16hex>]）
    RootOnly, ///< root-only——盘符＋一级目录＋"…"＋文件名（默认）
    Strip,    ///< strip——仅文件名
};

/**
 * @brief 脱敏策略（§9.5 原文 struct RedactionPolicy——L5/ui 会话可变配置，
 *        PM-14 归属）。
 *
 * 值语义；setPolicy 写时拷贝切换（变更后新调用生效，已写行不回溯——§9.5
 * 配置行）。maxPreviewBytes 为 §7.7"资源内容"行的预览上限策略位：预览由
 * 资源所有方（evidence/io 产码路径）按此上限生成后经 redact 过滤（§7.7
 * "经敏感模式过滤"的分工）——本服务不产资源预览，仅随策略携带并经 policy()
 * 暴露给产码方（§14.4 v0.9 口径登记）。
 */
struct RedactionPolicy {
    /// 路径策略（默认 RootOnly——企业内网可读性与安全的折中，§7.7 行原文）。
    PathPolicy pathPolicy = PathPolicy::RootOnly;
    /// 资源内容十六进制预览上限（字节；默认 32＝§7.7"前 32 B 十六进制预览"）。
    std::size_t maxPreviewBytes = 32;
};

// =====================================================================
// IRedactionService（§9.5 接口原文——签名逐字承载，不增不删）
// =====================================================================

/**
 * @brief 脱敏服务接口（§9.5 原文四方法）。
 *
 * 行为契约（§9.5 契约表逐行冻结）：
 *   - 前置/后置：无前置；输出必为脱敏后文本（不变式：输出不含凭据模式/
 *     未授权原文——DT-SEC-1~3 断言面）。
 *   - 线程：redact* 并发安全（策略快照读）；setPolicy 写时拷贝切换。
 *   - 生命周期：进程级；策略会话可变（用户级设置经 ui/L5，PM-14 归属）。
 *   - 稳定性：凭据模式清单为安全契约（只增不减）；路径策略枚举冻结。
 *   - 合法调用：日志管线（强制）、工厂（快照字段）、exportSafeSummary、
 *     崩溃文件。
 *   - 非法调用：以 redact 后文本反推原文（单向；无逆接口）。
 */
class IRedactionService {
public:
    virtual ~IRedactionService() = default;

    /**
     * @brief 整条消息脱敏（§9.5 原文签名；纯函数——同输入同输出；绝不抛出）。
     *
     * @param raw  [in] 消息原文（UTF-8 字节；任意长度——调用方通常是管线截断
     *             后的消息）
     * @param tier [in] 目标层级（Tier 语义见文件头：Dev＝NFR-SEC-07 全量；
     *             User＝全量＋内部 token 模式过滤）
     * @return 脱敏后文本；内部失败时整条降级为 kRedactionFailedToken
     *         （并按 §7.3② 补 DIAG-REDACTION-FAILED 开发诊断——经构造注入
     *         的 failureSink 路由；原文绝不放行、绝不抛出）
     */
    virtual std::string redact(std::string_view raw, LogTier tier) const noexcept = 0;

    /**
     * @brief 单路径脱敏（§9.5 原文签名；输入整体视作一个路径候选——不作文
     *        本扫描）。
     *
     * @param rawPath [in] 路径原文（Windows/UNC/POSIX 形态均支持；分隔符
     *                与盘符大小写不作规范化——同输入同输出）
     * @return 按当前 PathPolicy 处理后的路径；内部失败同 redact 降级
     */
    virtual std::string redactPath(std::string_view rawPath) const noexcept = 0;

    /**
     * @brief 安全摘要导出（§9.5 原文签名；reporting/崩溃文件消费的双保险
     *        入口）。
     *
     * 语义：redact(raw, LogTier::User) 后按 maxBytes 做 UTF-8 边界安全截断
     * （截断时追加 "[trunc]" 标注——与日志管线 4 KiB 截断同一呈现约定）。
     *
     * @param raw      [in] 原文（如异常消息——崩溃文件按 §7.6 截断 512 B）
     * @param maxBytes [in] 输出上限（字节；0＝不截断——只脱敏）
     * @return 脱敏＋截断后的安全文本；内部失败同 redact 降级
     */
    virtual std::string safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept = 0;

    /**
     * @brief 配置（§9.5 原文签名；L5/ui 会话变更——写时拷贝切换）。
     *
     * 后置：变更后新调用生效，已写行不回溯（§9.5 配置行原文）。
     *
     * @param policy [in] 新策略（按值接管拷贝；并发调用安全——内部互斥）
     */
    virtual void setPolicy(const RedactionPolicy& policy) = 0;
};

// =====================================================================
// 实现类 RedactionService（§7.7 规则表全量承载；§9.5 实现行）
// =====================================================================

/**
 * @brief 单次调用内的逐类替换计数（[REDACTED:<kind>:n] 的 n 来源——按调用
 *        从 1 递增，保留计数不保留原文，§7.3②）。
 *
 * 值语义；纯计数载体（redact 的确定性组成部分）。
 */
struct RedactionCounters {
    std::uint32_t credential = 0;  ///< 凭据键值命中计数（kind=credential）
    std::uint32_t token = 0;       ///< 令牌形态命中计数（kind=token）
    std::uint32_t env = 0;         ///< 非白名单环境变量引用计数（kind=env）
    std::uint32_t user = 0;        ///< 用户名/主目录替换计数（[USER]——计数供观测）
    std::uint32_t path = 0;        ///< 路径候选处理计数（含 Keep——进入策略即计）
    std::uint32_t internal = 0;    ///< User 档内部 token 模式命中计数（观测用）
};

/**
 * @brief 累计命中统计（进程级可观测面——R-7"模式命中计数可观测（DT-SEC-1
 *        观测点）"；实现类扩展，不在 §9.5 接口上——seal() 先例）。
 */
struct RedactionStats {
    std::uint64_t credentialHits = 0;  ///< 凭据键值累计命中
    std::uint64_t tokenHits = 0;       ///< 令牌形态累计命中
    std::uint64_t envHits = 0;         ///< 环境变量替换累计（非白名单）
    std::uint64_t userHits = 0;        ///< 用户名/主目录替换累计
    std::uint64_t pathHits = 0;        ///< 路径候选累计处理（含 Keep）
    std::uint64_t internalHits = 0;    ///< User 档内部模式累计命中
    std::uint64_t failures = 0;        ///< 脱敏自身失败降级累计（≠0 即有降级事件）
};

/**
 * @brief 脱敏服务实现（§7.7 规则表逐行承载；L5 装配的单实例）。
 *
 * 规则清单（安全契约——只增不减，§9.5 稳定性行；逐条口径登记 §14.4 v0.9）：
 *   ①凭据键值：键名 ∈ {password, passwd, pwd, secret, token, apikey,
 *     api_key/api-key, access_key, private_key, auth_token, authorization,
 *     credential(s)}（大小写不敏感）后随 ":"/"=" 的值段整体替换
 *     [REDACTED:credential:n]——键名与分隔符保留（键名是结构不是秘密）；
 *   ②令牌形态：Bearer/Basic 认证方案后随的凭证段（**不收 digest 等方案词**
 *     ——与摘要语义同词会系统性误伤开发诊断，R-7，§14.4 v0.9）；独立长凭证
 *     串（≥40 字符、字母数字'.'_+'/''=''-''/'、含字母与数字且非纯十六进制
 *     ——纯十六进制为摘要形态不误伤，规范身份"<tag>-<32 hex>"共 36 字符亦
 *     不命中）；
 *   ③环境变量：%VAR% 与 ${VAR} 引用——IRD_* 前缀（产品自用白名单，§7.7
 *     环境变量行）原样保留；用户身份集 {USERPROFILE, HOME, HOMEPATH,
 *     HOMEDRIVE, USERNAME, APPDATA, LOCALAPPDATA, TEMP, TMP}（大小写不敏感）
 *     →[USER]；其余 →[REDACTED:env:n]（整体不记录）；
 *   ④用户名路径段：<盘>:\Users\<名> 与 /home/<名>（大小写不敏感）→[USER]/
 *     /home/[USER]——先于路径策略执行，用户名不因 PathPolicy::Keep 泄露；
 *   ⑤路径候选（盘符/UNC/POSIX 绝对形态）按 PathPolicy 四策略处理（语义见
 *     PathPolicy 注释；相对路径不是"本机路径"不处理——R-7 防误伤）；
 *   ⑥User 档内部 token 模式（与 Logging.cpp 呈现过滤同形）：0x 十六进制
 *     地址→[ADDR]、≥32 位连续十六进制→[HASH]、栈帧" at 路径:行号"→
 *     " at [FRAME]"（NFR-REL-05/UX-02——报告面不携带调用栈/地址/哈希）。
 *
 * 降级语义（§7.3②/DT-SEC-3）：任何内部失败（含测试注入——见 applyRules）
 * 一律整条降级 kRedactionFailedToken＋DIAG-REDACTION-FAILED 开发诊断（经
 * failureSink；thread_local 防护保证失败链路不递归——"绝不放行原文"优先于
 * 一切可观测性）。
 *
 * 线程安全：redact 系方法与 setPolicy/stats/policy 任意线程并发（模式表
 * 构造后只读；策略 shared_ptr 快照读；stats 原子累计）。析构须在 failureSink
 * 与全部消费方停止使用之后（进程级设施语义，§9.5）。
 *
 * 注：本类不标 final——applyRules 模板方法是 DT-SEC-3 的故障注入缝
 * （testkit D-10 形态），测试子类覆写抛出以确定性复现"脱敏器自身失败"；
 * 产品代码不派生本类（L5 装配只消费 IRedactionService 接口）。
 */
class RedactionService : public IRedactionService {
public:
    /**
     * @brief 构造（L5 装配期注入降级诊断路由）。
     *
     * @param policy      [in] 初始策略（默认 RootOnly/32——§9.5 原文默认值）
     * @param failureSink [in] 降级开发诊断路由（§7.3② DIAG-REDACTION-FAILED
     *                    的承载；可为 nullptr＝静默降级——只出字面量不发诊
     *                    断，装配前/轻量消费方的合法形态。非拥有，本类不接
     *                    管所有权，引用须覆盖本服务生命周期）
     */
    explicit RedactionService(const RedactionPolicy& policy = RedactionPolicy{},
                              IDevLogSink* failureSink = nullptr);

    /**
     * @brief 析构（pimpl——须在实现体完整的翻译单元内定义；进程级设施）。
     */
    ~RedactionService() override;

    // 禁拷贝/禁移动：failureSink 引用注入面要求地址稳定（同 StableCodeRegistry
    // 纪律）；策略可变状态拷贝无意义。
    RedactionService(const RedactionService&) = delete;
    RedactionService& operator=(const RedactionService&) = delete;

    // ---- IRedactionService（§9.5 四方法——行为契约见接口注释）----
    std::string redact(std::string_view raw, LogTier tier) const noexcept override;
    std::string redactPath(std::string_view rawPath) const noexcept override;
    std::string safeSummary(std::string_view raw, std::size_t maxBytes) const noexcept override;
    void setPolicy(const RedactionPolicy& policy) override;

    /// @brief 当前策略快照（读侧观测——实现类扩展；§14.4 v0.9 口径）。
    RedactionPolicy policy() const;

    /// @brief 累计命中统计（R-7 可观测面——DT-SEC-1 观测点；原子读）。
    RedactionStats stats() const;

protected:
    /**
     * @brief 规则扫描主链（模板方法——DT-SEC-3 的确定性故障注入缝）。
     *
     * 为什么留缝：DT-SEC-3 要求验证"脱敏器自身失败→降级且不放行原文"（§10
     * 行"构造超限/非法输入"），而产品规则链对正常输入恒成功——故障必须经
     * 缝注入才可确定性复现（testkit D-10 接缝形态，ILogFileOps 同例）。产品
     * 路径不覆写本方法。
     *
     * @param text     [in,out] 就地改写的工作文本
     * @param tier     [in] 目标层级（决定⑥是否执行）
     * @param counters [out] 逐类替换计数（调用方提供、本方法清零后累计）
     *
     * @throws 任何异常均被 redact/redactPath/safeSummary 捕获并降级（noexcept
     *         铁律）；测试子类覆写抛出即复现"脱敏器自身失败"。
     */
    virtual void applyRules(std::string& text, LogTier tier, RedactionCounters& counters) const;

private:
    struct Impl;
    /// pimpl：隔离模式表/互斥/原子统计于头文件之外（公共头最小依赖面——R-2
    /// 纪律；实现见 src/Redaction.cpp）。
    std::unique_ptr<Impl> m_impl;  ///< 实现体（模式表＋策略快照＋统计＋failureSink）
};

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_REDACTION_HPP
