/**
 * @file   Sampling.hpp
 * @brief  ISamplingPlanBuilder——采样计划**定义**的构建/规范化/校验与
 *         计划摘要（§9.5 行原文契约的落位）。样本生成归 kinematics
 *         （KIN-04）——本接口**绝不生成/枚举任何样本点**（§5.2 边界：
 *         "本单元任何接口不得枚举/生成样本点"），接口面零样本出口。
 *
 * 设计依据：
 *   - units/requirements.md §3.3（公共头表 Sampling.hpp 行——T03："不含
 *     样本生成"）、§9.5（ISamplingPlanBuilder 行原文签名："buildPlan(...)
     →PlanOutcome；digest(plan)→PlanDigest；@错误 DegenerateRegion|
 *     NegativeCount|RegionNotBox；@post 产出 SamplingPlan 条目（规范化计
 *     数）；planContentIdentity 的 canonical 输入即此"）、§5.2（采样计划
 *     定义/规范化规则 D-REQ-2/零样本合法性 V-02/冻结语义图）、§6.2（零
 *     样本判定归评估）、§3.4（纯函数总约定）
 *   - 需求 REQ-03（区域采样定义）、KIN-04 冻结前提（样本集随快照冻结
 *     ——复评不得增删更换样本；planContentIdentity→sampleSetIdentity
 *     的输入链第一环在本单元）
 *   - 任务契约 tasks/foundation/WP-14-T03.json acceptance 3（采样计划
 *     确定性用例：GridBySpacing→counts 规范化确定性；counts 乘积=0 合法
 *     存储；ISamplingPlanBuilder 绝不生成样本）
 *
 * 背景说明（计划身份链——O-38 相关面的本单元边界）：planContentIdentity
 * ＝"计划内容身份"，其 canonical 输入＝本单元产出的规范化计数（D-REQ-2：
 * 同义计划同身份）；evidence 侧把 planContentIdentity 与预算/种子再组合
 * 出 sampleSetIdentity（evidence §4.1.4，O-38 格式冻结归 evidence 卡
 * ——三方对账面）。本单元的义务止于：同（区域定义,采样参数）→同规范
 * 化计划→同 canonical 字节；预算/种子的编码不在本单元（若 evidence 侧
 * 冻结格式要求本单元编码字节变更即转 blocked 三方对账——契约 note ②）。
 *
 * 线程安全：builder 无共享可变状态、可重入——多线程并发调用安全（§3.4
 * 总约定 1）。确定性：规范化 floor 规则与摘要均为纯字节/IEEE754 确定运
 * 算——同输入同输出（NFR-COR-01/02）。
 */

#ifndef IRD_REQUIREMENTS_SAMPLING_HPP
#define IRD_REQUIREMENTS_SAMPLING_HPP

#include <sdurws/ird/requirements/RequirementTypes.hpp>  // WorkRegion/SamplingPlan/PositionSampling
#include <sdurws/ird/requirements/Services.hpp>          // 三服务（buildPlan 经 WorkRegionService 复用规范化）

#include <string>
#include <vector>

namespace sdurws::ird::requirements {

// =====================================================================
// 计划规格与产出（§9.5 行文的 Spec/Outcome/Digest 面）
// =====================================================================

/**
 * @brief 采样计划构建规格（SamplingPlanSpec——buildPlan 的输入面）。
 *
 * positionSampling 允许 GridBySpacing 原始间距（构建期规范化为计数——
 * D-REQ-2）；orientationSampling 原样承载（KIN-04 姿态覆盖率口径）。
 * objectId 由调用方（编辑器/命令处理器）经 project 分配后经 planObjectId
 * 注入——本构建器不代分配（身份分配权归 project，O-36 同源口径）；缺省
 * （全零）＝由构建器生成临时句柄（编辑期草稿用，同 createXxx 临时句柄
 * 语义）。
 */
struct SamplingPlanSpec {
    std::optional<core::ObjectId> planObjectId;  ///< 计划条目 id（正式分配后注入；缺省＝临时句柄）
    PositionSampling positionSampling{};         ///< 位置采样定义（GridBySpacing 合法——规范化在此）
    OrientationSampling orientationSampling{};   ///< 姿态采样定义
    std::string note;                            ///< 备注
};

/**
 * @brief 计划构建产出（§9.5 PlanOutcome——两态：合法计划条目或定位错
 *        误；diags 参数为 §9.5 原文签名形状的契约预留位——本单元无已
 *        登记诊断码，错误经值面返回）。
 */
struct PlanOutcome {
    bool ok = false;          ///< true＝plan 有效；false＝error 有效
    SamplingPlan plan{};      ///< ok 态：采样计划条目（规范化计数；Grid 权威形态）
    RequirementError error{}; ///< err 态：首个违例错误（@错误 行逐项语义）
};

/**
 * @brief 计划摘要（§9.5 PlanDigest——供快照组装方填充
 *        SamplingPlanRef.plannedXxxSamples 的来源核对）。
 *
 * positionSamples/orientationSamples＝分母面（KIN-04 覆盖率判定的分母
 * 来源——evidence SamplingPlanRef.plannedPositionSamples/
 * plannedPoseSamples 的核算输入）；planDigest＝计划条目 canonical 字节
 * 的 SHA-256（core::ContentDigester——同计划同摘要，计划变更即换摘要
 * ——§5.2 计划变更与当前性行）。
 */
struct PlanDigest {
    std::uint64_t positionSamples = 0;      ///< 位置样本分母＝counts 乘积（0 合法——零样本由评估判定，V-02）
    std::uint64_t orientationSamples = 0;   ///< 姿态样本分母＝directionSamples×rollSamples（≥1）
    core::Digest256 planDigest{};           ///< 计划条目 canonical 字节摘要（SHA-256）

    bool operator==(const PlanDigest& o) const noexcept
    {
        return positionSamples == o.positionSamples
            && orientationSamples == o.orientationSamples && planDigest == o.planDigest;
    }
    bool operator!=(const PlanDigest& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// ISamplingPlanBuilder——接口（§9.5 行原文契约）
// =====================================================================

/**
 * @brief 采样计划构建器（§9.5 行原文契约——只构建/规范化/校验"计划
 *        定义"，绝不生成样本〔KIN-04 生成归 kinematics〕）。
 *
 * ★ 接口面边界（acceptance 3 的结构面——V-05/KIN-04 联合观测的对端）：
 * 本接口（及其产品实现）的公开方法仅 buildPlan/digest 两个——签名面
 * 没有任何返回样本集合/样本坐标/样本迭代器的出口；样本坐标的生成算法
 * 归 kinematics（KIN-04），evidence 持冻结凭据。计划条目（SamplingPlan）
 * 本身也不承载样本坐标——只有规范化计数。
 */
class ISamplingPlanBuilder {
public:
    virtual ~ISamplingPlanBuilder() = default;

    /**
     * @brief 构建区域采样计划定义（§9.5 行原文——@post 产出 SamplingPlan
     *        条目（规范化计数）；planContentIdentity 的 canonical 输入
     *        即此）。
     *
     * 校验链（@错误 行逐项；短路返回首个违例）：
     *   ①区域盒非退化（size 三分量>0 有限）→ DegenerateRegion（I-REQ-6
     *     ——零体积区域不可建立采样计划）；
     *   ②区域形态核对（R1 仅 Box——§5.2 区域几何行；WorkRegion 值模型
     *     即 Box 形态，非 Box 源〔未来扩展〕在此拒绝）→ RegionNotBox；
     *   ③采样定义：GridBySpacing 间距三分量正有限 → IllegalTolerance；
     *     姿态计数 ≥1 → NegativeCount（0 合法面仅限位置侧计数与乘积
     *     ——V-02；负数由无符号类型面阻断，此处拦越界语义）；
     *   ④规范化（D-REQ-2）：GridBySpacing → counts[i]=floor(size[i]/
     *     spacing[i])+1（WorkRegionService::normalizeSampling 复用——
     *     语义单源，NFR-MNT-04）；Grid/Random 原样承载。
     *
     * 计数乘积=0（如 Grid counts 全 0 或 Random count=0）**合法存储**
     * （V-02——零样本判定归评估 KIN-04 R8：DataInsufficient、不输出
     * 0%/100%；本构建器不拦不判）。
     *
     * @param region [in] 目标区域（WorkRegion 条目——regionRef 源）
     * @param spec   [in] 计划规格
     * @param diags  [out] 诊断输出（恒不写入——契约预留位同上）
     * @return ok＝采样计划条目（objectId＝spec 注入或临时句柄；位置采样
     *         恒规范化 Grid 形态）；err＝首个违例错误
     *
     * 纯函数；线程安全；确定性（同输入同计划值——临时句柄除外）。
     */
    virtual PlanOutcome buildPlan(const WorkRegion& region, const SamplingPlanSpec& spec,
                                  std::vector<core::DiagnosticRecord>& diags) const = 0;

    /**
     * @brief 计划摘要（§9.5 行原文——供快照组装方填充
     *        SamplingPlanRef.plannedXxxSamples 的来源核对）。
     *
     * @param plan [in] 采样计划条目（恒为规范化 Grid 形态——违约经 err
     *             返回；见 PlanDigest 的 digest 无效面）
     * @param diags [out] 诊断输出（恒不写入——契约预留位）
     * @return ok＝计划摘要（分母面＋canonical 摘要）；err＝计划形态非法
     *
     * 纯函数；线程安全；确定性（同计划同摘要——逐字节，NFR-COR-01/02）。
     */
    virtual Expected<PlanDigest> digest(const SamplingPlan& plan,
                                        std::vector<core::DiagnosticRecord>& diags) const = 0;
};

/// ISamplingPlanBuilder 的产品实现（无状态——可默认构造，拷贝/移动平凡；
/// 内部复用 WorkRegionService::normalizeSampling 与 RequirementCodec——
/// 规范化与摘要的语义单源，NFR-MNT-04）。
class SamplingPlanBuilder final : public ISamplingPlanBuilder {
public:
    PlanOutcome buildPlan(const WorkRegion& region, const SamplingPlanSpec& spec,
                          std::vector<core::DiagnosticRecord>& diags) const override;
    Expected<PlanDigest> digest(const SamplingPlan& plan,
                                std::vector<core::DiagnosticRecord>& diags) const override;
};

}  // namespace sdurws::ird::requirements

#endif  // IRD_REQUIREMENTS_SAMPLING_HPP
