/**
 * @file   FeasibleSet.hpp
 * @brief  可行集与淘汰原因输出（selection 单元）——结果身份块（§4.3/
 *         §11.2）、SEL-06 指标素材（裕量/质量/成本——§17.3）、可行集与
 *         运行结果类型（§10.1）、IFeasibleSetBuilder（§14.6 分层汇总/
 *         稳定排序/空集语义/EVI-02 资格）、IRejectionReasonProvider
 *         （§14.6 词表唯一实现点——diagRef 回填）与 sel 域必需证据
 *         Profile 注册面（EVI-01——REQUIREMENTS §8.1 表 4 选型行）。
 *
 * 设计依据：
 *   - units/selection.md §10.1（FeasibleSet/SelectionRunResult 类型基线）、
 *     §10.2（分层与短路边界——空集语义逐类独立标记；候选能力筛选不短路）、
 *     §10.3（淘汰原因词表——每条原因全部字段＋SEL-* 稳定码建议值随本任务
 *     注册）、§10.4（稳定排序——排序键级联＋淘汰原因排序）、§4.3（身份
 *     关系表）、§11.2（Quick/Verified——评估模式进入结果身份）、§17.3
 *     （OPT-07 指标事实——裕量＝工作点对能力的余量，含来源工况）、§13.7
 *     （reporting 消费——可行组合/裕量/质量/成本/来源/逐项淘汰原因）、
 *     §14.6（IFeasibleSetBuilder/IRejectionReasonProvider 接口签名）、
 *     §14.0（通用约定——调用方错误 fail-fast vs 数据类返回素材）、§9.4
 *     （EVI-02 多工况资格——组合仅在全部启用必验工况通过后进入可行集，
 *     任一工况数据不足整体 DataInsufficient 不漏验）
 *   - 需求 SEL-06（输出可行组合、裕量、质量、成本、来源和逐项淘汰原因
 *     （含实际值与阈值））、SEL-05（组合级记录——上游输入）、EVI-01
 *     （RequiredEvidenceProfile 契约——表 4 选型行逐行实例化）、EVI-02
 *     （正式计算必须覆盖全部启用必验工况，不得漏验）、ERR-01（比较型
 *     字段齐备＋稳定诊断引用）、NFR-COR-02（稳定排序与确定性）
 *   - 任务契约 tasks/foundation/WP-19-T06.json（acceptance 1～3：可行
 *     组合＋指标＋逐项淘汰原因输出；多工况资格不漏验；选型域 Profile
 *     必需证据承载齐备）
 *
 * ★ 本头与上游组件的分工（同一类型不分叉——T04/T05 先例延续）：
 *   1. RejectionReason/FeasibilityRecord/ReasonToken/DataGap 复用
 *      Screening.hpp 的 T04 承载（卡 §10.1 基线类型——T04 落位细化 ①）；
 *      CaseCoverageEntry/CompletenessKind/组合校核输入输出复用
 *      Combination.hpp 的 T05 承载。本头只新增"可行集组装"层类型
 *      （SelectionRunResult 及其输入面），不复制任何上游类型。
 *   2. 组装器是纯函数聚合层：可行判定以输入 FeasibilityRecord 与覆盖格
 *      为唯一事实源（不自算任何能力/工作点判定——T04 黄金表已钉住筛选、
 *      T05 已钉住组合校核）；组装器独立复核的只有 EVI-02 覆盖完整性
 *      （记录判可行而覆盖格非 Pass → 组装面防线按更严者降级——见
 *      FeasibleSetBuilder::build 类注第 3 步）。
 *   3. SEL-* 稳定码引用：RejectionReason.diagRef 的回填唯一经
 *      reasonTokenDiagCode()（DiagCodes.hpp——词表→稳定码唯一映射点，
 *      T06 批 28 码已注册登记表）；本头不书写任何码值字面量。
 *
 * 错误语义（卡 §14.0 两分法）：
 *   - 调用方契约违约（记录/组合键失配、重复键、非组合级记录混入、非
 *     有限数值、契约版本为 0）＝fail-fast 抛 std::invalid_argument；
 *   - 数据类（entries 中无校核记录的组合、记录判可行但覆盖素材缺失）＝
 *     合成 DataInsufficient 素材（DataGap 显式标记），不伪造可行、不
 *     fail-fast——数据不足不是调用方错误，是输入事实（§10.2 空集语义表）。
 *
 * 确定性（NFR-COR-02）：排序键级联固定（§10.4——不依赖哈希遍历序、与
 *   线程数无关）；同输入恒同输出；同值稳定次序＝输入序（stable_sort）。
 *
 * 线程安全：FeasibleSetBuilder/RejectionReasonProvider 为无状态纯函数
 *   对象（可重入——卡 §14.10"筛选/曲线/组合构造（纯函数）可重入"）；
 *   makeSelRequiredEvidenceProfile 为纯函数；全部输入由调用方持有，
 *   输出按值返回。
 */

#ifndef IRD_SELECTION_FEASIBLESET_HPP
#define IRD_SELECTION_FEASIBLESET_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>   // core::EvaluationMode——评估模式进结果身份
#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId/ContentIdentity
#include <sdurws/ird/evidence/Evidence.hpp> // evidence::RequiredEvidenceProfile——EVI-01
#include <sdurws/ird/selection/CatalogTypes.hpp>
#include <sdurws/ird/selection/Combination.hpp>  // CaseCoverageEntry/CompletenessKind/
                                                 //   组合校核输入输出（T05 承载复用）
#include <sdurws/ird/selection/Screening.hpp>    // FeasibilityRecord/ReasonToken/VerdictKind

namespace sdurws::ird::selection {

// =====================================================================
// 结果身份块（卡 §4.3 身份关系表＋§10.1 IdentityBlock＋§11.2"评估模式
// 进入结果身份"——SelectionRunResult 的身份承载）
// =====================================================================

/**
 * @brief 选型运行结果的身份块（§10.1 SelectionRunResult.identity）。
 *
 * 追溯面（卡 §4.3 行语义）：目录身份（条目归属与版本区分）、映射批上游
 * 切片身份（电机侧工作点来源——③端口值传递；直调无映射＝全零诚实标记）、
 * 评估切片身份（缓存键/当前性判据——evidence §5.1；直调无切片＝全零）、
 * 组合校核契约版本（评估器语义版本）与评估模式（Quick/Verified——§11.2
 * 唯一两模式，Preview 不产生正式结果对象）。"来源"（SEL-06）由此承载：
 * CatalogIdentity.source 为目录来源描述、thresholdSource 在每条淘汰原因
 * 上（逐项来源），二者合为 SEL-06"来源"输出面。
 */
struct IdentityBlock {
    CatalogIdentity catalog;                    ///< 目录身份（锁定版本——§4.3）
    core::ContentIdentity mappingSliceId;       ///< 映射批上游切片身份（全零＝直调无映射）
    core::ContentIdentity inputSliceId;         ///< 评估切片身份（全零＝直调无切片）
    std::uint32_t contractVersion = 0;          ///< 组合校核契约版本（>0 合法——0＝未登记）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 评估模式（§11.2）

    bool operator==(const IdentityBlock& o) const
    {
        return catalog == o.catalog && mappingSliceId == o.mappingSliceId
            && inputSliceId == o.inputSliceId && contractVersion == o.contractVersion
            && mode == o.mode;
    }
    bool operator!=(const IdentityBlock& o) const { return !(*this == o); }
};

// =====================================================================
// SEL-06 指标素材（可行组合的裕量/质量/成本——§17.3 OPT-07 消费面、
// §13.7 reporting 消费面）
// =====================================================================

/**
 * @brief 单维度裕量事实（§17.3"裕量＝工作点对能力的余量，含来源工况"的
 *        承载——最小驱动裕量的定位面）。
 *
 * 数值语义（R1 口径，登记单元卡 §19.3 T06 落位细化）：
 *   - margin ＝ (capability − |actual|) / capability，无量纲，∈(-∞,1]：
 *     正值＝尚有余量，0＝恰好用满，负值＝工作点超过能力（指标事实如实
 *     保留负值——是否淘汰仍由硬筛选判定，本事实不参与判定）；
 *   - actual/capability 为 SI 域数值（单位随 unit token——卡 §4.4 单位表）；
 *   - 工作点取绝对值参与比较（目录能力值为正——转矩/转速/功率幅值口径；
 *     四象限负工作点的量值同按幅值对能力）。
 */
struct MarginFact {
    std::string dimension;    ///< 维度名（R1 词表见 computeFeasibleCombinationMetrics 头注）
    core::ObjectId axisId;    ///< 来源轴（§17.3"含来源工况"的定位面之一）
    CaseId caseId;            ///< 来源工况 ID（定位面之二——EVI-02 矩阵列键）
    double actual = 0.0;      ///< 工作点值（SI 域；可负——幅值口径参与比较）
    double capability = 0.0;  ///< 能力值（SI 域；>0——目录必填字段保证）
    double margin = 0.0;      ///< 相对裕量（无量纲；(capability−|actual|)/capability）
    std::string unit;         ///< 单位 token（core Units 词表，如 "N*m"/"rad/s"/"W"）

    bool operator==(const MarginFact& o) const
    {
        return dimension == o.dimension && axisId == o.axisId && caseId == o.caseId
            && actual == o.actual && capability == o.capability
            && margin == o.margin && unit == o.unit;
    }
    bool operator!=(const MarginFact& o) const { return !(*this == o); }
};

/**
 * @brief 可行组合指标素材（SEL-06"裕量、质量、成本"的组合级承载——
 *        §10.1 SelectionRunResult 的落位扩展字段元素类型）。
 *
 * 成本字段（SEL-06"成本"）：v1 目录无成本字段（T05 落位细化 ⑧ 登记
 * "无数据源≠数据缺口，不伪造"）——恒 nullopt；目录 schema 演进/R2
 * OPT-07 消费时按其任务回填，本头不设默认值不猜测。
 */
struct FeasibleCombinationMetrics {
    DeviceCombinationId combinationId;  ///< 所属组合键（与可行集记录对位）
    double totalMass = 0.0;             ///< 组合质量，单位 kg（Σ 各轴电机＋减速器质量
                                        ///   ——T05 checkCombinations 同源产出镜像）
    std::optional<MarginFact> minMargin; ///< 最小驱动裕量（跨轴×工况×维度最小者；
                                         ///   nullopt＝无数值维度可算——不伪造 0 裕量）
    std::optional<double> cost;         ///< 成本（v1 目录无字段——恒 nullopt，不伪造）

    bool operator==(const FeasibleCombinationMetrics& o) const
    {
        return combinationId == o.combinationId && totalMass == o.totalMass
            && minMargin == o.minMargin && cost == o.cost;
    }
    bool operator!=(const FeasibleCombinationMetrics& o) const { return !(*this == o); }
};

// =====================================================================
// 可行集与运行结果（卡 §10.1 类型基线——落位承载）
// =====================================================================

/**
 * @brief 可行集（卡 §10.1 FeasibleSet 基线）。
 *
 * feasible＝资格判可行的组合实体（DeviceCombination——含逐轴指派构成，
 * 报告/BOM 消费面）；records＝全部组合级可行性记录（可行/淘汰/数据不足
 * 三态齐备——空集语义分类已在记录 verdict 上分轨，卡 §10.2 空集语义表：
 * 全淘汰＝可行集空＋原因全集，不自动等于任务级不可行——判定权在 evidence
 * 汇总，本层只输出事实）。
 */
struct FeasibleSet {
    std::vector<DeviceCombination> feasible;      ///< 可行组合（构成面；稳定排序后）
    std::vector<FeasibilityRecord> records;       ///< 全部组合级记录（同键序）

    bool operator==(const FeasibleSet& o) const
    {
        return feasible == o.feasible && records == o.records;
    }
    bool operator!=(const FeasibleSet& o) const { return !(*this == o); }
};

/**
 * @brief 选型运行结果（卡 §10.1 SelectionRunResult 基线——payload 主体；
 *        reporting 选型章节与 optimization（R2）的消费面，卡 §9.6）。
 *
 * metrics 为本任务落位扩展字段（表尾追加——登记单元卡 §19.3 T06）：
 * SEL-06"裕量/质量/成本"的组合级素材（卡面 §10.1 未列字段，但 §17.3
 * OPT-07 消费面与 §13.7 reporting 交接行明文要求输出）。
 */
struct SelectionRunResult {
    FeasibleSet set;                              ///< 可行集＋全部记录
    std::vector<CaseCoverageEntry> coverage;      ///< 工况覆盖矩阵素材（EVI-02）
    IdentityBlock identity;                       ///< 身份块（§4.3/§11.2）
    CompletenessKind completeness = CompletenessKind::Complete; ///< 数据完整性
    std::vector<FeasibleCombinationMetrics> metrics; ///< SEL-06 指标素材（组合序）

    bool operator==(const SelectionRunResult& o) const
    {
        return set == o.set && coverage == o.coverage && identity == o.identity
            && completeness == o.completeness && metrics == o.metrics;
    }
    bool operator!=(const SelectionRunResult& o) const { return !(*this == o); }
};

/**
 * @brief 单组合的可行集组装输入（§14.6 build 第 4 参元素——卡面三参签名
 *        的落位扩展，登记单元卡 §19.3 T06：组合实体与指标素材不由记录/
 *        覆盖格承载，须由组装方值传递供给）。
 *
 * metrics 可为 nullopt（组装方未供给指标——如实输出无指标素材，不伪造；
 * SEL-06 指标计算见 computeFeasibleCombinationMetrics——组装器不自算）。
 */
struct FeasibleSetEntry {
    DeviceCombination combination;  ///< 组合实体（构成面——进入可行集的载体）
    std::optional<FeasibleCombinationMetrics> metrics; ///< SEL-06 指标素材（可缺）

    bool operator==(const FeasibleSetEntry& o) const
    {
        return combination == o.combination && metrics == o.metrics;
    }
    bool operator!=(const FeasibleSetEntry& o) const { return !(*this == o); }
};

// =====================================================================
// 可行集组装器（卡 §14.6 IFeasibleSetBuilder——接口与唯一产品实现）
// =====================================================================

/**
 * @brief 可行集组装接口（卡 §14.6 设计基线——签名逐注承载＋落位扩展）。
 *
 * 职责（卡 §14.6 @brief 注）：由资格矩阵＋淘汰原因组装可行集（§10）——
 * 分层汇总（轴→组合→整机素材）、稳定排序（§10.4）、空集语义分类
 * （§10.2）、覆盖矩阵素材。纯函数；不判工程不可行（evidence 汇总）。
 *
 * 卡面签名 build(records, coverage, identity) 的落位扩展（登记 §19.3
 * T06）：追加第 4 参 entries——组合实体（DeviceCombination 的轴表构成）
 * 与 SEL-06 指标素材不在 records/coverage 承载内，须由组装方值传递
 * （组合校核段/组合构造器产物对位）；缺此参则 feasible 无法承载构成、
 * SEL-06 指标无来源。
 */
class IFeasibleSetBuilder {
public:
    virtual ~IFeasibleSetBuilder() = default;

    /**
     * @brief 由组合级记录＋资格矩阵＋身份块组装选型运行结果（卡 §14.6）。
     *
     * 组装规则（三步——顺序固定，全部确定性）：
     *  1. 对位校验（调用方契约，fail-fast）：records 须全部为组合级
     *     （deviceKind==Combination——轴级候选记录的汇总已在组合校核段
     *     完成，卡 §10.2 分层不混淆）；记录键/组合键各自唯一；每条记录
     *     的 id 必须在 entries 中有同键组合（记录引用未知组合＝组装前提
     *     破坏）；identity.contractVersion 须 >0（未登记非法——T05 同款
     *     语义）。
     *  2. 逐组合资格判定（EVI-02 不漏验）：
     *     - 记录缺失（entries 有、records 无）→ 合成 DataInsufficient
     *       记录（DataGap 显式标记——无校核记录＝漏验风险，不得进可行集；
     *       取消截断的产出前缀即此形态——§13.4"取消不发布完整可行集"）；
     *     - 记录判 Feasible → 复核覆盖矩阵：无覆盖格＝漏验风险 → 降级
     *       DataInsufficient；任一格非 Pass → 按格判定采纳（格 Rejected
     *       → 组合 Rejected；格 DataInsufficient → 组合 DataInsufficient
     *       ——更严者胜：记录与格矛盾时以格为准，防线语义）；
     *     - 其余（Rejected/DataInsufficient 记录）→ 原样保留（ verdict
     *       已由上游汇总规则给出，组装器不翻案）。
     *  3. 输出装配：可行集＝判定 Feasible 的组合实体；records＝全部记录
     *     （含合成）按 §10.4 排序键级联排序；逐条原因的空 diagRef 回填
     *     稳定码引用（reasonTokenDiagCode——T06 批码已注册，输出满足
     *     §10.3"每条原因全部字段"）；coverage 原样透传；metrics 按
     *     records 排序序对位输出。
     *
     * @param records  [in] 组合级可行性记录（组合校核段产出——T05
     *                 checkCombinations 的 record 列表；调用方持有）
     * @param coverage [in] 资格矩阵素材（组合×工况逐格——EVI-02 复核面）
     * @param identity [in] 结果身份块（contractVersion>0）
     * @param entries  [in] 逐组合组装输入（组合实体＋可缺指标素材；与
     *                 records 按组合键对位）
     * @return 选型运行结果（可行集稳定排序；三态记录齐备）
     *
     * @throws std::invalid_argument 记录含非组合级类别／记录键重复／组合
     *         键重复／记录引用未知组合／identity.contractVersion==0
     *
     * @note 纯函数；同输入恒同输出（NFR-COR-01/02）；可重入。排序键：
     *       R1 固定＝①可行/不足/淘汰（Feasible→DataInsufficient→
     *       Rejected）→②轴序→③候选稳定 ID→④目录版本→⑤组合键→⑦输入
     *       序（stable_sort）。⑥"用户可选排序键"（质量/成本/裕量）为
     *       呈现层功能（WP-19-T10 插件通道——卡 §11.3 Quick 研究态"临时
     *       参考值排序候选"），R1 组装器不设排序参数。
     */
    virtual SelectionRunResult build(const std::vector<FeasibilityRecord>& records,
                                     const std::vector<CaseCoverageEntry>& coverage,
                                     const IdentityBlock& identity,
                                     const std::vector<FeasibleSetEntry>& entries) const = 0;
};

/**
 * @brief 可行集组装器唯一产品实现（无状态纯函数对象——可重入，卡 §14.10）。
 */
class FeasibleSetBuilder final : public IFeasibleSetBuilder {
public:
    SelectionRunResult build(const std::vector<FeasibilityRecord>& records,
                             const std::vector<CaseCoverageEntry>& coverage,
                             const IdentityBlock& identity,
                             const std::vector<FeasibleSetEntry>& entries) const override;
};

// =====================================================================
// 淘汰原因构造器（卡 §14.6 IRejectionReasonProvider——词表唯一实现点）
// =====================================================================

/**
 * @brief 淘汰原因上下文（卡 §14.6 make(token, ctx) 的 ctx 落位承载——
 *        卡面未给签名，登记单元卡 §19.3 T06：字段集＝RejectionReason 除
 *        token/diagRef 外的全部字段（token 由 make 的参数给出、diagRef
 *        由词表→稳定码映射回填——§10.3"SEL-* 稳定码建议值随 WP-19-T06
 *        注册"的执行面））。
 *
 * 数值字段（atTime/actual/required）必须有限（NaN/±Inf＝调用方契约违约
 * fail-fast——NFR-COR-03）；文本类比较（工作制/安装方向/电压等）时
 * actual/required 数值侧无意义、以 actualText/requiredText 承载（单位
 * unit 留空串——T04 同款分轨）。
 */
struct ReasonContext {
    ModelId candidateModelId;         ///< 候选型号稳定 ID（候选定位——目录域身份）
    core::ObjectId axisId;            ///< 轴对象 ID（组合级无单轴时全零——T05 约定）
    CaseId caseId;                    ///< 工况 ID（维度与特定工况无关时空串）
    double atTime = 0.0;              ///< 工作点时间，单位 s（可适用时；不适用时 0）
    std::string segmentId;            ///< 轨迹段 ID（可适用时；不适用时空串）
    double actual = 0.0;              ///< 实际值（SI 域——"实际值"侧，ERR-01）
    double required = 0.0;            ///< 要求值（SI 域——"阈值"侧，ERR-01）
    std::string unit;                 ///< 单位 token（core Units 词表；文本类比较空串）
    std::string thresholdSource;      ///< 阈值来源（目录列名/筛选条件条目/策略条目引用
                                      ///   ——SEL-06/ERR-01 追溯面）
    std::string actualText;           ///< 实际侧文本（文本类比较承载；数值比较空串）
    std::string requiredText;         ///< 要求侧文本（同上）
    CatalogIdentity catalog;          ///< 目录版本（判定所用快照身份——追溯）
    core::ContentIdentity inputSliceId; ///< 输入切片身份（评估器路径回填；直调全零）
    core::ContentIdentity mappingId;    ///< 映射身份（电机侧工作点来源；无映射全零）
    std::string suggestion;           ///< 建议动作（ERR-01——如"更换更大额定转矩型号"）

    bool operator==(const ReasonContext& o) const
    {
        return candidateModelId == o.candidateModelId && axisId == o.axisId
            && caseId == o.caseId && atTime == o.atTime && segmentId == o.segmentId
            && actual == o.actual && required == o.required && unit == o.unit
            && thresholdSource == o.thresholdSource && actualText == o.actualText
            && requiredText == o.requiredText && catalog == o.catalog
            && inputSliceId == o.inputSliceId && mappingId == o.mappingId
            && suggestion == o.suggestion;
    }
    bool operator!=(const ReasonContext& o) const { return !(*this == o); }
};

/**
 * @brief 淘汰原因构造接口（卡 §14.6 设计基线——签名逐注承载）。
 *
 * 职责（卡 §14.6 @brief 注）：淘汰原因构造（§10.3）——ReasonToken 词表
 * →比较型记录（实际值/要求值/单位/阈值来源）；SEL-* 稳定码引用（注册后
 * 回填 diagRef——T06 批 28 码已随本任务注册登记表）。词表唯一实现点。
 */
class IRejectionReasonProvider {
public:
    virtual ~IRejectionReasonProvider() = default;

    /**
     * @brief 构造单条逐项淘汰原因（卡 §14.6 make——ERR-01 全字段齐备）。
     *
     * @param token [in] 淘汰原因 token（§10.3 封闭词表——枚举值；越界
     *              整数值不可经类型表达，无须防御）
     * @param ctx   [in] 原因上下文（比较型字段＋身份面；调用方持有）
     * @return 逐项淘汰原因（token/diagRef 由本函数回填——diagRef 经
     *         reasonTokenDiagCode 映射，恒非空 string_view；
     *         其余字段自 ctx 逐项拷贝）
     *
     * @throws std::invalid_argument ctx.atTime/actual/required 非有限
     *
     * @note 纯函数；同输入恒同输出（NFR-COR-02）；可重入。
     */
    virtual RejectionReason make(ReasonToken token, const ReasonContext& ctx) const = 0;
};

/**
 * @brief 淘汰原因构造器唯一产品实现（无状态纯函数对象——可重入）。
 */
class RejectionReasonProvider final : public IRejectionReasonProvider {
public:
    RejectionReason make(ReasonToken token, const ReasonContext& ctx) const override;
};

// =====================================================================
// SEL-06 指标计算（纯函数——组合校核产出→组合级指标素材；组装方调用后
// 经 FeasibleSetEntry.metrics 进入组装器——组装器不自算指标）
// =====================================================================

/**
 * @brief 计算逐组合 SEL-06 指标素材（质量/最小驱动裕量/成本——§17.3
 *        OPT-07 消费面、§13.7 reporting 交接面）。
 *
 * 计算口径（R1，登记单元卡 §19.3 T06 落位细化）：
 *  - totalMass：镜像 outcome.totalMass（T05 checkCombinations ⑨ 已核算
 *    ——Σ 各轴电机＋减速器质量，kg；单一来源不重算）；
 *  - minMargin：遍历该组合的全部（轴×工况），对下列六个驱动维度逐项
 *    计算 MarginFact（工作点与能力值都可得才计算——nullopt 维度跳过，
 *    不伪造零工作点）后取 margin 最小者：
 *      motor-torque-continuous（motorTorqueRms vs ratedTorque，"N*m"）、
 *      motor-torque-peak（motorTorquePeak vs peakTorque，"N*m"）、
 *      motor-speed-peak（motorSpeedPeak vs maxSpeed，"rad/s"）、
 *      motor-power-peak（motorPowerPeak vs ratedPower，"W"——固定额定值
 *      口径，能力曲线口径随 §6.4 落位扩展，R1 裕量不查曲线）、
 *      gearbox-rated-torque（jointTorqueRms vs ratedOutputTorque，"N*m"）、
 *      gearbox-peak-torque（jointTorquePeak vs peakOutputTorque，"N*m"）。
 *    ★ 指标面不参与判定：裕量为基础目录能力口径（不含安全系数复判/
 *    温度降额折减——折减口径随 P-SEL-4/O-11 同域裁决扩展）；可行与否
 *    完全由硬筛选/组合校核判定，本函数只产出数值事实（§17.3"指标事实"）。
 *    全部维度无数值 → minMargin nullopt。
 *  - cost：恒 nullopt（v1 目录无成本字段——不伪造，T05 落位细化 ⑧）。
 *
 * @param input    [in] 组合校核核心输入（快照＋轴事实＋映射批——工作点
 *                 与候选的数值面；snapshot 须非空——调用方契约）
 * @param outcomes [in] 组合校核产出（totalMass 镜像源；序＝组合键对位面）
 * @return 逐组合指标素材（序＝outcomes 序；同键对位）
 *
 * @throws std::invalid_argument input.snapshot 为空指针／outcomes 内
 *         记录键重复（对位前提破坏）
 *
 * @note 纯函数；同输入恒同输出（NFR-COR-02）；可重入。
 */
std::vector<FeasibleCombinationMetrics> computeFeasibleCombinationMetrics(
    const CombinationCheckCoreInput& input,
    const std::vector<CombinationCheckOutcome>& outcomes);

// =====================================================================
// sel 域必需证据 Profile（EVI-01——REQUIREMENTS §8.1 表 4 选型行逐行
// 实例化；L5 装配"Profile 注册在前、评估器注册在后"时序的数据源）
// =====================================================================

// Profile 项 itemId 词表（域内稳定键——唯一书写点；证据产出方引用同一
// 常量登记 EvidenceItem.itemIds，禁第二处字面量。词形经 evidence
// isValidProfileItemId："<域>.<项>" 两段 kebab）。

/// 表 4 必需项①：目录版本锁定标识（SelectionCheckResult.catalog 承载）。
inline constexpr std::string_view kSelProfileItemCatalogLock =
    "sel.catalog-version-lock";
/// 表 4 必需项②：每组合电机侧工作点（τ/ω/P 序列、效率、反射惯量、
/// 惯量比——DriveTrainMappingEvaluator 同一口径；drivetrain dt.mapping
/// 评估器产出 EvidenceItem 已对齐此键——同一 sel Profile 的项级分工）。
inline constexpr std::string_view kSelProfileItemMotorOpPoint =
    "sel.motor-op-point";
/// 表 4 必需项③：逐项淘汰原因（实际值/阈值——组合校核 payload 承载）。
inline constexpr std::string_view kSelProfileItemRejectionReasons =
    "sel.itemized-rejection-reasons";
/// 表 4 必需项④：组合兼容记录（组合校核兼容核对原因记录承载）。
inline constexpr std::string_view kSelProfileItemComboCompatibility =
    "sel.combo-compatibility";
/// 建议项①：成本/质量汇总（SelectionRunResult.metrics——本任务落位）。
inline constexpr std::string_view kSelProfileItemCostMassSummary =
    "sel.cost-mass-summary";
/// 建议项②：优选品牌与供应状态（SEL-07——WP-19-T07 落位）。
inline constexpr std::string_view kSelProfileItemVendorAvailability =
    "sel.preferred-vendor-availability";

/**
 * @brief 构造 sel 域 v1 必需证据 Profile（表 4 选型行逐行实例化——EVI-01
 *        承载；T05 落位细化 ⑦"正式 RequiredEvidenceProfile 注册随
 *        WP-19-T06"的兑现面）。
 *
 * 必需项（表 4 选型行"必需证据项"四行逐行——itemId 词表为域内稳定键，
 * 登记单元卡 §19.3 T06）：
 *  - "sel.catalog-version-lock"：目录版本锁定标识（SelectionCheckResult.
 *    catalog 承载——T05 评估器 payload 内含）；
 *  - "sel.motor-op-point"：每组合电机侧工作点（τ/ω/P 序列、效率、反射
 *    惯量、惯量比——DriveTrainMappingEvaluator 同一口径；drivetrain
 *    dt.mapping 评估器产出 EvidenceItem "sel.motor-op-point" 已对齐此键
 *    ——两域同一 Profile 实例的项级分工）；
 *  - "sel.itemized-rejection-reasons"：逐项淘汰原因（实际值/阈值——
 *    T05 评估器产出 EvidenceItem "sel.combination-check" 的 payload 承载；
 *    item 级产出对齐随汇总接入任务收编，本任务注册 Profile 登记面）；
 *  - "sel.combo-compatibility"：组合兼容记录（同 payload 承载——兼容
 *    核对原因记录）。
 * 建议项（表 4 选型行"建议证据项"两行——缺失不阻断）：
 *  - "sel.cost-mass-summary"：成本/质量汇总（SelectionRunResult.metrics
 *    ——本任务落位）；
 *  - "sel.preferred-vendor-availability"：优选品牌与供应状态（SEL-07
 *    ——WP-19-T07 落位）。
 *
 * 全部项 substitutableByInfeasibility＝false（选型评估在输入齐备时产物
 * 恒可生成——不存在"因不可行而无法生成的成功产物"；Common 类项不登记
 * ——通用必需项由 evidence 内建 commonRequiredItems() 隐式附加，域重复
 * 登记即注册拒绝，§6.1）。
 *
 * @return Profile 实例（profileId="sel"、version="1"——与 T05 评估器
 *         descriptor.profile 绑定同值；contentIdentity 零值——注册时由
 *         evidence 计算回填，域不可申报，§6.1/§9.5）
 *
 * @note 纯函数；同调用恒同值（NFR-COR-02）。注册期校验与内容身份计算归
 *       evidence（validateEvidenceProfile/computeProfileContentIdentity/
 *       EvidenceProfileRegistry.registerProfile——契约测试经真实注册表
 *       验证注册闭环）。
 */
evidence::RequiredEvidenceProfile makeSelRequiredEvidenceProfile();

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_FEASIBLESET_HPP
