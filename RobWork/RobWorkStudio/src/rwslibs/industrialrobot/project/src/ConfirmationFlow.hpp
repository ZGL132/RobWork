/**
 * @file   ConfirmationFlow.hpp
 * @brief  确认放行流的绑定设施（PRJ-T11）——确认绑定四元组的计算、冻结
 *         与复核（§6.7"可确认诊断放行流"的数据半区；私有实现头，不出
 *         include/——R-2 纪律）。
 *
 * 设计依据：
 *   - units/project.md §6.7（可确认诊断放行流表——"确认绑定：确认记录四
 *     元组随命令摘要持久化（CommandRecord.confirmations[]）：{findingDigest,
 *     policyContentId, commandDigest, baseRevisionId}——四者任一不符的确认
 *     凭据视为失效（编译前复核绑定）"；"确认记录进入摘要：S6 提交时写入
 *     command.json；随修订永久留痕"）、§5.3.3（确认交互回调——回调返回
 *     凭据后的放行面）、§4.4.4（CommandRecord.confirmations[] 字段级契约
 *     ——本设施产出的 ConfirmationRecord 即其内存形态）、§12 PRJ-T11 行
 *     （交互回调编排、确认绑定与留痕、取消/失效路径）；
 *   - units/diagnostics.md §5.3（绑定四元组的计算与复核设施语义——"创建
 *     时计算并冻结；确认提交时〔ui 回调返回后、放行前〕与编译前各复核一
 *     次——四者任一与当前实际值不符 ⇒ 确认凭据失效，命令按未确认处置"）、
 *     §5.5 要点⑦（复核两次的时机锚点）；diagnostics 侧同形设施
 *     Confirmable.hpp FindingBinding 为其半区——project 不直链 diagnostics
 *     库（P-PR-6 处置），四元组计算在本单元自持（两单元消费同一 core
 *     ContentDigester 摘要算法，编码各按其登记——见 .cpp 文件头实现口径①）；
 *   - 需求 MDL-06④（可确认诊断放行——确认不豁免编译）、ERR-01（诊断记
 *     录字段）、CON-06（策略内容身份进确认绑定）、SA-15（确认只在命令边
 *     界）、CR-02（摘要算法唯一路径＝core::ContentDigester）；
 *   - 任务契约 tasks/foundation/PRJ-T11.json acceptance 1/3（PRJ-TX-2 四
 *     路拒绝之"绑定失效"路；确认留痕入 command.json）。
 *
 * 背景说明（本设施在放行流中的位置——"数据半区"边界）：
 *   命令服务（CommandServiceImpl）是放行流的**编排者**：S4 决策映射（回
 *   调三态→Rejected/Aborted）后，本设施承接"确认什么、凭据绑在什么上"的
 *   数据语义——对回调返回的每个凭据，把"finding 内容摘要＋策略内容身份
 *   ＋命令载荷摘要＋确认基线修订"冻结成一条 ConfirmationRecord；放行前
 *   与编译前各复核一次（四元组任一不符 ⇒ 凭据失效）；提交时随命令留痕
 *   写入 command.json（持久化面归 Codec/TxEngine，本设施只产出内存形态）。
 *   绑定的意义（为什么要在凭据之外再冻结四个值）：确认是"用户对**这一版
 *   输入**下的**这一条**超限事实放行"——输入（载荷/基线/策略语境）或事实
 *   内容任一变化，旧确认不得被沿用（diagnostics.md §5.3 失效条件表的
 *   project 侧机制化）。
 *
 * 实现口径登记（DTB §5.4——单元卡 §6.7 未定义判据，详见 .cpp 文件头）：
 *   ①findingDigest 的 canonical 编码表（对什么字节做摘要——core.md §4.2
 *     "对什么做摘要归各所有者单元"的 project 侧登记）；
 *   ②policyContentId 的阶段 A 来源（project 无④策略端口通道——finding
 *     比较语境投影的内容身份；真实域处理器落位时随其任务增量切换）；
 *   ③复核时机落点（确认提交时＝S4 冻结后立即；编译前＝S5 端口调用前）；
 *   ④失配处置归类（Rejected(confirmations-unresolved)——§6.7"命令按未
 *     确认处置"＋diagnostics.md §5.3 同语；不私扩 §5.0 封闭集——CR-08）；
 *   ⑤IConfirmationProbe 接缝（D-10 生产窄接口/测试侧 fake 形态——失配
 *     分支在槽内生产不可达〔§6.7"输入变化不可能"明文〕，PRJ-TX-2 第四路
 *     的注入面）。
 *
 * 线程安全：全部函数纯函数（每次调用独立计算）；IConfirmationProbe 实现
 *   方自行保证并发安全（命令执行槽内调用——§6.1，同上下文天然串行）。
 */

#ifndef SDURWS_IRD_PROJECT_SRC_CONFIRMATIONFLOW_HPP
#define SDURWS_IRD_PROJECT_SRC_CONFIRMATIONFLOW_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>

namespace sdurws::ird::project::confirm {

// =====================================================================
// 值类型：绑定四元组的"当前实际值"快照与复核结论
// =====================================================================

/**
 * @brief 绑定四元组的当前实际值（§6.7 复核面的取值形态）。
 *
 * 背景说明：四个成员与 ConfirmationRecord 的前四字段一一对应，全部持
 * **规范文本形态**（摘要＝64 小写 hex；身份＝cid-/rev- core 规范文本）——
 * 与冻结记录逐字段直接可比（verifyBinding 的比对面），无第二表示形态。
 *
 * 线程安全：纯值类型。
 */
struct ConfirmationActuals {
    /// finding 内容摘要（64 小写 hex——§6.7"finding 内容摘要"）。
    std::string findingDigest;
    /// 已解析策略内容身份（cid- 规范文本——CON-06；实现口径②）。
    std::string policyContentId;
    /// 本次命令载荷摘要（64 小写 hex——SHA-256 over payloadCanonical）。
    std::string commandDigest;
    /// 确认所针对的输入版本（rev- 规范文本——复核时点重查的权威 tip）。
    std::string baseRevisionId;
};

/**
 * @brief 绑定复核结论（§6.7 四元组序逐成员判定——首个不符成员即定性）。
 *
 * 背景说明：结论枚举优先于异常轨（状态类失败不是错误——diagnostics.md
 * §9.3 ConfirmOutcome 同款取舍）；逐成员定性供开发诊断定位（哪个维度
 * 失配——reportDev 通道，不进用户码——CR-08）。
 */
enum class BindingVerdict {
    Bound,                   ///< 四元组全部一致（放行继续）
    FindingDigestMismatch,   ///< finding 内容摘要不符（事实内容已变）
    PolicyContentMismatch,   ///< 策略内容身份不符（策略语境已变——CON-06）
    CommandDigestMismatch,   ///< 命令载荷摘要不符（提交输入已变）
    BaseRevisionMismatch,    ///< 输入版本不符（确认基线已前移）
};

// =====================================================================
// 摘要计算（CR-02：算法唯一路径＝core::ContentDigester；编码口径见 .cpp）
// =====================================================================

/**
 * @brief 计算 finding 内容摘要（findingDigest——绑定四元组首成员）。
 *
 * 摘要输入＝core::DiagnosticRecord 的 project 侧 canonical 编码（编码表
 * 登记于 ConfirmationFlow.cpp 文件头——实现口径①；对什么做摘要归所有者
 * 单元，core.md §4.2 边界原句）。同内容必得同摘要（确定性——NFR-COR-02，
 * SHA-256 纯字节变换）。
 *
 * @param record [in] finding 底层记录（取 ConfirmableFinding.record）
 * @return 64 字符小写 hex 摘要文本
 *
 * 纯函数；线程安全；不抛（除 bad_alloc 传播）。
 */
std::string findingDigestHex(const core::DiagnosticRecord& record);

/**
 * @brief 计算 finding 的已解析策略内容身份（policyContentId——CON-06）。
 *
 * 阶段 A 口径（实现口径②，详见 .cpp 文件头）：project 无④策略端口通道
 * （策略判定的阈值读取与判定全在域处理器——§5.3.2/P-PR-3），project 可
 * 见面的"已解析策略内容"＝finding 内嵌的比较语境（码＋比较三要素＋原因
 * ——实际/期望两侧即处理器解析出的策略阈值事实）。本函数对该语境投影
 * 计算内容身份；真实域处理器（modeling 等）落位后，随其任务把④端口的
 * 策略内容身份经计划声明面接入（增量切换，不改四元组形状）。
 *
 * @param record [in] finding 底层记录
 * @return 策略语境内容身份（cid- 规范文本经 toCanonical 取用）
 *
 * 纯函数；线程安全；不抛（除 bad_alloc 传播）。
 */
core::ContentIdentity policyContentIdentity(const core::DiagnosticRecord& record);

/**
 * @brief 计算命令载荷摘要（commandDigest——绑定四元组第三成员）。
 *
 * 摘要输入＝信封负载原样字节（域 canonical——§6.4 project 不解释，直接
 * 对字节做摘要；与对象内容版本同一算法路径 codec::contentVersionOf 的
 * 底层 core::ContentDigester，但此处输出取 64 hex 裸文本——§4.4.4
 * commandDigest 字段形态，不带 cv- tag）。
 *
 * @param payloadCanonical [in] 信封负载字节（长度 0 合法——空负载摘要）
 * @return 64 字符小写 hex 摘要文本
 *
 * 纯函数；线程安全；不抛（除 bad_alloc 传播）。
 */
std::string commandDigestHex(const std::vector<std::uint8_t>& payloadCanonical);

// =====================================================================
// 冻结与复核（§6.7 数据半区的两个动作）
// =====================================================================

/**
 * @brief 冻结一条确认留痕（回调返回的凭据 → §4.4.4 ConfirmationRecord）。
 *
 * 执行序（§6.7"确认记录四元组随命令摘要持久化"的装配点）：
 *   ①四元组前两成员自 finding 内容计算（findingDigest/policyContentId）；
 *   ②后两成员取提交事实（commandDigest＝信封负载摘要；baseRevisionId＝
 *     S2 解析的基线修订——确认所针对的输入版本）；
 *   ③凭据字段转换（principal 透传；confirmedAtUtc 的 time_point→
 *     ISO-8601 带毫秒 UTC 文本——win32::formatIsoMilli，单元内唯一时间
 *     格式来源）。
 *
 * @param envelope     [in] 命令信封（载荷摘要来源）
 * @param finding      [in] 待确认事实（摘要来源；状态不参与——冻结发生在
 *                     core confirm() 推进之前，凭据单独入参）
 * @param credential   [in] 回调返回的确认凭据（与 finding 一一对应）
 * @param baseRevision [in] 确认所针对的输入版本（S2 解析的基线）
 * @return 留痕记录（内存形态；S6 装配进 commitPlan.command.confirmations）
 *
 * 纯函数；线程安全。
 */
ConfirmationRecord freezeConfirmation(const CommandEnvelope& envelope,
                                      const core::ConfirmableFinding& finding,
                                      const core::ConfirmationCredential& credential,
                                      const core::RevisionId& baseRevision);

/**
 * @brief 绑定复核（纯函数——冻结记录 vs 当前实际值）。
 *
 * 复核序（§6.7 四元组序，首个不符成员即定性——失配维度可定位）：
 *   ①findingDigest（事实内容自检——重算 vs 冻结）；
 *   ②policyContentId（策略语境——CON-06）；
 *   ③commandDigest（提交输入）；
 *   ④baseRevisionId（输入版本——字符串比对）。
 * 任一不符 ⇒ 确认凭据失效（调用方按未确认处置——实现口径④）。
 *
 * @param frozen  [in] 确认时点冻结的留痕记录
 * @param actuals [in] 复核时点采集的当前实际值
 * @return 复核结论（Bound＝全部一致）
 *
 * 纯函数；线程安全。
 */
BindingVerdict verifyBinding(const ConfirmationRecord& frozen,
                             const ConfirmationActuals& actuals);

// =====================================================================
// 当前值探针（D-10 生产窄接口/测试侧 fake 形态——IFileOps/ILockOps 先例）
// =====================================================================

/**
 * @brief 绑定复核的"当前实际值"采集接缝（实现口径⑤）。
 *
 * 为什么需要接缝：§6.7 复核的失配分支在**生产执行序内不可达**——确认
 * 等待占用命令执行槽，槽内无并发写（§6.7"输入变化不可能（槽内无并发写，
 * §6.1）"明文），冻结与复核的输入恒一致；该分支是防御纵深（防实现缺陷
 * 与未来演进破坏前提），PRJ-TX-2 第四路（绑定失效拒绝）需要可注入面才
 * 能在 submit 级验证"失配 ⇒ 无修订"的编排行为。本接口即 D-10 形态的
 * 生产窄接口：生产实现（ProductionConfirmationProbe）从命令上下文真值
 * 采集；测试侧 fake 注入扰动值驱动失配分支（同 FaultInterceptor 经
 * IFileOps 接缝注入的先例——testkit.md D-10"生产窄接口，测试侧 fake"）。
 *
 * 咨询点边界：**冻结不经本接口**（S4 冻结直调 freezeConfirmation 生产
 * 真值——若冻结也走探针，测试扰动会同时污染冻结侧，失配无从构造）；
 * 本接口只在复核点被咨询（S4 冻结后／S5 编译前，各每 finding 一次）。
 *
 * 线程约束：仅在命令执行槽内调用（§6.1——同上下文串行，实现方无需为
 * 同上下文并发设防）。
 */
class IConfirmationProbe {
public:
    /// 虚析构：生产/测试实现多态销毁的常规保障。
    virtual ~IConfirmationProbe() = default;

    /**
     * @brief 采集一个 finding 的四元组当前实际值。
     *
     * @param envelope            [in] 命令信封（commandDigest 的输入）
     * @param finding             [in] 待确认事实（findingDigest/policy 的输入）
     * @param currentBaseRevision [in] 复核时点的当前基线（rev-；生产路径＝
     *                            权威元数据重查的分支 tip——S2 解析值或
     *                            更新值）
     * @return 四元组当前值（规范文本形态——与冻结记录直接可比）
     */
    [[nodiscard]] virtual ConfirmationActuals actuals(
        const CommandEnvelope& envelope,
        const core::ConfirmableFinding& finding,
        const core::RevisionId& currentBaseRevision) = 0;
};

/**
 * @brief 生产探针——真值采集（冻结与复核同源同函数，唯一计算点）。
 *
 * 背景说明：全部成员经本文件自由函数计算（findingDigestHex/
 * policyContentIdentity/commandDigestHex）——冻结（S4）与两处复核（S4
 * 尾/S5 前）消费同一实现，"同源"由结构保证（无第二计算路径可漂移）。
 * 无状态——可全局共享（命令槽内调用）。
 */
class ProductionConfirmationProbe final : public IConfirmationProbe {
public:
    [[nodiscard]] ConfirmationActuals actuals(
        const CommandEnvelope& envelope,
        const core::ConfirmableFinding& finding,
        const core::RevisionId& currentBaseRevision) override;
};

}  // namespace sdurws::ird::project::confirm

#endif  // SDURWS_IRD_PROJECT_SRC_CONFIRMATIONFLOW_HPP
