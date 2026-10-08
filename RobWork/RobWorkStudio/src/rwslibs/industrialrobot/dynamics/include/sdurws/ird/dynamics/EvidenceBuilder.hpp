/**
 * @file   EvidenceBuilder.hpp
 * @brief  dyn Profile 证据装配器（units/dynamics.md §10.6
 *         IDynamicsEvidenceBuilder 的具体实现载体）——把单工况序列/工况
 *         结果/正动力学检查产出装配为 §8.4 dyn Profile 六项证据素材：
 *         数据不足（摩擦缺失/估算物性/外部验证未完成）经 evidence 证据项
 *         降级表达（Invalid/Unverified＋缺失清单全量列出），限定语纪律
 *         单点承载——估算结果绝不包装为精确结论（DYN-06）。
 *
 * 设计依据：
 *   - units/dynamics.md §5.5（摩擦符号约定与数据不足降级——三层来源
 *     〔缺失 NotProvided／估算 GeometricEstimate／外部验证未完成 Recorded〕
 *     互斥呈现、不混用；"可信性不替代工程判定：DynamicsValidity 只是结果
 *     侧事实；Feasible/DataInsufficient 判定归 evidence 汇总"）、§8.4
 *     （evidence 交接——dyn Profile 六项逐行实例化、评估键/依赖声明、
 *     "DataInsufficient 缺失项全量列出（不短路）"）、§9.4（DYN-* 码）、
 *     §10.6（IDynamicsEvidenceBuilder 契约表：addSeries/addConditionResult/
 *     addForwardCheck/build；后置"必需四项/建议两项状态齐备……缺失全量
 *     列出"；非法示例"跳过 provenance 项装配——缺项即 Missing，不可省略"）、
 *     §4.4（DynamicsEvidence/DynamicsValidity 数据模型）、§4.6（Empty/
 *     非有限/无时间参数的"不伪造"纪律）、§9.6（限定语词表——reporting
 *     §6.4，dynamics 供数据源不供文案；RPT-05 限定语不得弱化）
 *   - units/evidence.md §6.2（EvidenceItem 五态/presence 纪律/EvidenceManifest）、
 *     §6.4.1 决策表④（"必需项存在 Missing/Invalid/Unverified→DataInsufficient，
 *     缺失项全量列出"——本装配器把降级事实表达为必需项不满足，使汇总层
 *     可判定 DataInsufficient；dynamics 自身绝不产出 EngineeringStatus）、
 *     §6.1（RequiredEvidenceProfile——D-14：明细归需求表 4，域逐行实例化）
 *   - 需求 DYN-06（物性或摩擦数据不足时标记可信等级，不把估算结果包装成
 *     精确结论）、MDL-16（摩擦未填写→DataInsufficient 并触发 DYN-06 降级）、
 *     MDL-05（估算来源）、CON-03（外部验证未完成＝Recorded 态）、RPT-05
 *     （限定语冻结）、EVI-01（Profile）、表 4 动力学行（证据明细权威）
 *   - 决策 D-DYN-6（负载 com/inertia 缺失保守估算＋强制 estimated 标记）、
 *     D-DYN-7 无关本头；V-09/V-10 验证行的证据面口径
 *     （property-friction-provenance=Invalid/降级标记）
 *   - 任务契约 tasks/foundation/WP-17-T06.json（acceptance 1/2 的实现面：
 *     "降级经 evidence 证据表达（缺失项全量列入清单，DataInsufficient）；
 *     不把估算结果包装成精确结论（限定语纪律）用例通过"）
 *
 * ★ 契约形态微调（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. §10.6 的接口以**具体类**形态落地（T03 InverseDynamicsEvaluator/T04
 *      DynamicsSeriesBuilder 同款先例——evidence IEngineeringEvaluator
 *      适配与评估器注册面随 WP-17-T10 装配冻结），方法签名与卡面一致。
 *   2. 证据项清单与缺失清单以显式访问器交付（evidenceItems()/missingList()
 *      ——T06 卡面"evidence EvidenceItem 承载"与 §8.4"缺失项全量列出"的
 *      执行面；§10.6 未给出清单访问器签名，本落地面为最直接对应；build()
 *      返回 DynamicsEvidence 与卡面一致）。
 *   3. 数据不足的证据状态映射（卡面无逐态对照表，本落地面按 evidence
 *      §6.2 五态语义＋V-09/V-10 行口径落定，逐条登记于 build 说明）：
 *      摩擦缺失/估算物性→provenance 项 **Invalid**（invalidReason＝
 *      DYN-FRICTION-MISSING/DYN-PROPERTY-DOWNGRADED 稳定码诊断——V-09/
 *      V-10 行"property-friction-provenance=Invalid/降级标记"）；外部验证
 *      未完成→**Unverified**（evidence §6.2"产物存在但未在满足正式要求
 *      的条件下验证"——CON-03 Recorded 态的本义映射）；建议项 NotRun→
 *      Missing（无产物，不阻断）；Skip→NotApplicable（显式不适用，非缺失
 *      ——C2/EV-VER-7）。
 *
 * 背景说明（为什么"降级"不表现为新枚举/新等级字段）：DYN-06 的"标记可信
 *   等级"由既有承载分层表达——①结果侧事实＝DynamicsValidity 三层字段
 *   （frictionMissing/estimatedXxxCount/externalValidationPending，§4.4
 *   明文"不是证据等级"）；②证据侧降级＝必需项 Invalid/Unverified＋限定语
 *   token＋缺失清单（本头）；③工程判定（Feasible/DataInsufficient）＝
 *   evidence aggregateVerdict 决策表④（PA-1 权威唯一——dynamics 不越权
 *   定级）。新增"可信等级枚举"会在①③之间制造第二权威，与 §4.4 注释
 *   "不是新的全局任务状态、证据等级"直接冲突，故不设。
 *
 * 线程安全：实例单线程使用（每工况一装配器——§10.0 同 SeriesBuilder）；
 *   build 后实例可复用于下一工况。确定性：同输入→同证据项/清单/摘要
 *   （SHA-256 摘要投影纯字节变换，NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_EVIDENCEBUILDER_HPP
#define IRD_DYNAMICS_EVIDENCEBUILDER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagnosticRecord（invalidReason 承载）
#include <sdurws/ird/core/Identity.hpp>       // core::ObjectId（工况/对象引用）
#include <sdurws/ird/dynamics/DynTypes.hpp>   // DynamicsSeries/OperatingConditionResult/
                                              //   DynamicsEvidence/DynamicsValidity
#include <sdurws/ird/dynamics/Errors.hpp>     // DynamicsError（fail-fast 异常轨——契约面）
#include <sdurws/ird/dynamics/ForwardDynamics.hpp> // ForwardCheckOutcome（建议项⑥素材）
#include <sdurws/ird/evidence/Evidence.hpp>   // evidence::EvidenceItem/EvidenceItemStatus/
                                              //   RequiredEvidenceProfile（证据承载——T06 卡面
                                              //   "evidence EvidenceItem 承载"）

namespace sdurws::ird::dynamics {

// =====================================================================
// dyn Profile 身份与证据项 id（§8.4——常量唯一书写点，消费方禁另写字面量；
// itemId 是证据项/缺失清单/报告限定语与 evidence 汇总层对账的关联键，
// 词形经 evidence isValidProfileItemId 词形闸门："<域>.<项>" 两段 kebab）。
// =====================================================================

/// dyn Profile 域 id（§8.4：profileId="dyn"——evidence 五域词表成员）。
inline constexpr std::string_view kDynProfileId = "dyn";

/// dyn Profile 域登记版本（§8.4：version=1；Profile 明细演进＝升版本——
/// 注册表键为 (profileId, version) 二元组，同域多版本可共存）。
inline constexpr std::string_view kDynProfileVersion = "1";

/// 必需项 ①：关节侧广义力序列（表 4 动力学行——RNEA、类型化单位）。
inline constexpr std::string_view kDynItemJointSeries = "dyn.joint-generalized-force-series";
/// 必需项 ②：峰值（含持续时间窗与所在段）与完整循环 RMS（含驻留）。
inline constexpr std::string_view kDynItemPeakRms = "dyn.peak-rms-statistics";
/// 必需项 ③：物性/摩擦参数来源标记（缺失按 DYN-06 降级并列入缺失清单）。
inline constexpr std::string_view kDynItemProvenance = "dyn.property-friction-provenance";
/// 必需项 ④：负载工况标识。
inline constexpr std::string_view kDynItemLoadCondition = "dyn.load-condition-identity";
/// 建议项：机械功率/能量分项（数据完整时产出）。
inline constexpr std::string_view kDynItemPowerEnergy = "dyn.power-energy-split";
/// 建议项：正动力学一致性检查记录（mode=Standard 且前置齐备；Skip→NotRun）。
inline constexpr std::string_view kDynItemForwardCheck = "dyn.forward-dynamics-consistency";

// =====================================================================
// 限定语 token（§9.6 供数义务/reporting §6.4 词表的 dynamics 侧产出三值
// ——唯一书写点；dynamics 供数据源不供文案，token 即机器判读面。RPT-05：
// 消费方必须保留不弱化——估算值/数据不足/外部验证未完成限定语不得在
// 报告渲染中省略或改写为精确表述）。
// =====================================================================

/// 估算限定语（§5.5 第 2 层——GeometricEstimate 来源或保守估算在场：
/// 结果为估算值，不是精确测量/精确输入的结论）。
inline constexpr std::string_view kQualifierEstimated = "estimated";
/// 数据不足限定语（§5.5 第 1 层——摩擦参数缺失（MDL-16）：结果在输入
/// 不完整条件下产出，汇总层据此走 DataInsufficient 降级）。
inline constexpr std::string_view kQualifierDataInsufficient = "data-insufficient";
/// 外部验证未完成限定语（§5.5 第 3 层——CON-03 Recorded 态物性：结果
/// 依赖未经外部验证的引用资源）。
inline constexpr std::string_view kQualifierExternalValidationIncomplete =
    "external-validation-incomplete";

// =====================================================================
// dyn Profile 实例化（§8.4——域按需求表 4 逐行实例化，装配期经
// EvidenceProfileRegistry 注册〔先于评估器注册——evidence §13 顺序〕；
// contentIdentity 为保留值，注册时由 evidence 计算——域不可申报）。
// =====================================================================

/**
 * @brief 构造 dyn Profile（§8.4 表逐行实例化——required 四项＋suggested
 *        两项；明细内容权威归 REQUIREMENTS §8.1 表 4 动力学行，本函数只
 *        是登记面的物化，不增删改写任何项）。
 *
 * 逐行落值口径：全部六项无适用条件（表 4 动力学行无条件列——Always，
 * applicability=nullopt）；替代标志（C2"成功产物类"）：①序列/②统计/
 * ⑤功率能量为评估成功产物——存在有效不可行证明时可被"因不可行而不适用"
 * 替代（substitutableByInfeasibility=true）；③来源标记（降级清单行）与
 * ④负载工况标识（可追溯性行）不属成功产物、不豁免（false）。全部项
 * description 非空（§6.1 注册期校验强制——登记契约的一部分）。
 *
 * @return Profile 值（每次调用新构造——纯函数；contentIdentity 保留值，
 *         注册时被 evidence 覆写）
 *
 * 线程安全：可重入纯函数。
 */
evidence::RequiredEvidenceProfile dynProfile();

// =====================================================================
// 可信等级标记（DYN-06 第①层表达——结果侧限定语的单点判定）
// =====================================================================

/**
 * @brief 从有效性摘要产出报告限定语 token 清单（DYN-06"不把估算结果包装
 *        成精确结论"的限定语纪律唯一实现点；§5.5 三层→§9.6 词表三值，
 *        层序＝§5.5 登记序：缺失→估算→外部验证，确定性输出）。
 *
 * 映射（§5.5 三层逐行；互斥呈现指条目归层，多字段并存时逐层各出一条）：
 *   - validity.frictionMissing                → "data-insufficient"
 *   - estimatedLinkCount+estimatedPayloadCount>0 → "estimated"
 *   - validity.externalValidationPending      → "external-validation-incomplete"
 * 三层全无 → 空清单（结果可作精确结论呈现——无降级限定语）。
 *
 * @param validity [in] 有效性摘要（评估器/构建器产出的事实面；只读）
 * @return 限定语 token 清单（顺序固定；元素指向静态串——无所有权转移）
 *
 * 线程安全：可重入纯函数。
 */
std::vector<std::string_view> trustQualifiers(const DynamicsValidity& validity);

// =====================================================================
// 缺失清单条目（§8.4"DataInsufficient 缺失项全量列出（不短路）"的承载）
// =====================================================================

/**
 * @brief 单条缺失清单条目（evidence 决策表④级素材——"必需项存在
 *        Missing/Invalid/Unverified"的逐项定位；不因首个缺失短路，
 *        全量列出）。
 * 值语义纯结构；线程安全。
 */
struct EvidenceGap {
    std::string itemId;                  ///< 证据项 id（§8.4 词表——kDynItem* 常量）
    evidence::EvidenceItemStatus status; ///< 不满足态（Missing/Invalid/Unverified——
                                         ///   NotApplicable 不计缺失、不入清单）
    std::string reason;                  ///< 中文原因（含稳定码/计数——汇总层缺失
                                         ///   项清单与报告缺项呈现的素材）
};

// =====================================================================
// 证据装配器（§10.6 IDynamicsEvidenceBuilder 的具体实现载体——域内具体
// 类；每工况一实例）。
// =====================================================================

/**
 * @brief dyn Profile 证据装配器（§10.6——§8.4 六项素材的唯一组装点）。
 *
 * 职责（一次 build 的执行序——§10.6 契约表逐步对应）：
 *   1. addSeries：注入单工况序列（必需项 ①③④ 的素材源——样本行、
 *      来源事实 validity、工况/工具身份；身份块防御校验沿用 SeriesBuilder
 *      冻结纪律，二次注入 fail-fast）；
 *   2. addConditionResult：注入工况级统计结果（必需项 ②＋建议项 ⑤ 的
 *      素材源；conditionId 与已注入序列不一致＝绑定校验失败 fail-fast——
 *      evidence §6.2"防错误工况引用"）；
 *   3. addForwardCheck：注入正动力学检查产出（建议项 ⑥ 素材）；
 *   4. build：六项证据状态判定（逐项映射见下）→DynamicsEvidence 组织
 *      形态＋EvidenceItem 清单＋缺失清单＋限定语缓存（访问器读取）。
 *
 * 证据状态映射（§10.6 后置"状态齐备……缺失全量列出"；逐条登记见文件头
 * 微调 3——evidence §6.2 五态语义）：
 *   - ① joint-generalized-force-series：未注入/空序列（Empty——§4.6 不做
 *     0 值伪装）→Missing；否则 Satisfied（artifactDigest＝产物摘要投影）。
 *   - ② peak-rms-statistics：未注入/峰值行为空（无 Ok 行→统计器不产出，
 *     §7.6）→Missing；否则 Satisfied。
 *   - ③ property-friction-provenance：未注入→Missing；frictionMissing→
 *     Invalid＋DYN-FRICTION-MISSING（V-09）；估算计数>0→Invalid＋
 *     DYN-PROPERTY-DOWNGRADED（V-10）；externalValidationPending→
 *     Unverified（CON-03——优先序 Invalid>Unverified，保守呈现）；全干净→
 *     Satisfied（来源标记完整＝全部来源已提供且非估算）。
 *   - ④ load-condition-identity：未注入→Missing；否则 Satisfied（身份块
 *     由 SeriesBuilder 冻结校验过——conditionId/工具/payloadVariant 覆盖集
 *     齐备；subject＝conditionId）。
 *   - ⑤ power-energy-split（建议）：未注入/timeParamAvailable=false（§4.6
 *     无时间参数→能量字段 NaN 显式无效，不是产物）→Missing；否则 Satisfied。
 *   - ⑥ forward-dynamics-consistency（建议）：未注入→Missing；NotApplicable
 *     （mode=Skip）→NotApplicable＋原因必填（C2——不计缺失）；NotRun→
 *     Missing（数据不足/取消——建议项不阻断，V-14）；Passed→Satisfied；
 *     Failed→Invalid（invalidReason＝检查器附带的 DYN-FD-* 首条诊断——
 *     失败不能判定模型无效，§6.3.5）。
 */
class DynamicsEvidenceBuilder {
public:
    DynamicsEvidenceBuilder() = default;

    /// 可拷贝可移动（纯值缓冲——实例仅是组装边界）。
    DynamicsEvidenceBuilder(const DynamicsEvidenceBuilder&) = default;
    DynamicsEvidenceBuilder& operator=(const DynamicsEvidenceBuilder&) = default;

    /**
     * @brief 注入单工况序列（§10.6 addSeries——[in] 值只读，build 前内部
     *        保存引用所需的字段拷贝；[in] 标注＝调用方无需交出所有权）。
     *
     * @param series [in] 冻结序列（SeriesBuilder::finalize 产物——身份块/
     *               排序/contentIdentity 已冻结；本方法校验其完整性）
     *
     * @throws DynamicsError 重复注入（本装配器单工况语义——同一实例第二次
     *         addSeries 即调用方契约违约，token "evidence-duplicate-series"）；
     *         身份块不完整（snapshotId/sliceId/trajectoryPayloadId 全零、
     *         conditionId 空 id、task 不 isValid、algorithmVersion/
     *         dynConfigDigest 空串——缺身份拒绝，§10.0）
     */
    void addSeries(const DynamicsSeries& series);

    /**
     * @brief 注入工况级统计结果（§10.6 addConditionResult——[in] 值只读）。
     *
     * @param result [in] 单工况结果（peaks/powerEnergy/validity 与 series
     *               同源——编排层装配；conditionId 绑定校验见 @throws）
     *
     * @throws DynamicsError 重复注入（token "evidence-duplicate-condition"）；
     *         未先注入同工况序列（addSeries 前置——统计结果的绑定锚是
     *         序列身份块，缺锚无法校验工况一致性）；result.conditionId
     *         与已注入序列的 conditionId 不一致（错误工况引用——绑定校验
     *         失败即调用方契约违约，token "evidence-condition-mismatch"）
     */
    void addConditionResult(const OperatingConditionResult& result);

    /**
     * @brief 注入正动力学一致性检查产出（§10.6 addForwardCheck——[in] 值
     *        只读；建议项 ⑥ 素材，缺失不阻断）。
     *
     * @param outcome [in] 检查产出（ForwardDynamicsValidator 产出——四态
     *                ＋诊断素材；映射见类注释项 ⑥）
     *
     * @throws DynamicsError 重复注入（token "evidence-duplicate-forward"）
     */
    void addForwardCheck(const ForwardCheckOutcome& outcome);

    /**
     * @brief 冻结并返回证据装配视图（§10.6 build——[out] 域内组织形态；
     *        调用后实例可复用于下一工况）。
     *
     * @return DynamicsEvidence（§4.4——各 refs 组只在对应项 Satisfied 时
     *         非空，引用＝conditionId；两建议可用性位对应建议项 Satisfied）
     *
     * 后置（§10.6）：六项证据状态齐备——evidenceItems() 恒返回 6 行
     * （行序＝§8.4 表行序：①②③④⑤⑥），每行状态/原因/摘要显式；
     * missingList() 全量列出非 Missing 即不满足的必需项与建议项缺失
     * （不短路）；不抛（数据不足是合法产出——不是错误）。
     */
    DynamicsEvidence build();

    /**
     * @brief 证据项清单（T06 卡面"evidence EvidenceItem 承载"的执行面）。
     *
     * @return 6 行 EvidenceItem（行序＝§8.4 表行序；itemId/status/
     *         artifactDigest/caseScope/subject/notApplicableReason/
     *         invalidReason 逐字段满足 evidence §6.2 presence 纪律——可
     *         直接通过 validateEvidenceItems 校验并装配进 EvidenceManifest）
     *
     * 前置：build() 已调用（未 build 返回空清单——不预生成）。
     * 线程安全：build 后并发只读安全（返回值拷贝）。
     */
    std::vector<evidence::EvidenceItem> evidenceItems() const;

    /**
     * @brief 缺失清单（§8.4"DataInsufficient 缺失项全量列出（不短路）"）。
     *
     * @return 全部不满足项（必需项 Missing/Invalid/Unverified＋建议项
     *         Missing/Invalid——NotApplicable 不计缺失不入清单；顺序＝
     *         §8.4 表行序，确定性）
     *
     * 前置：build() 已调用。消费口径：本清单是 evidence 汇总决策表④级
     * 素材——dynamics 不据它判定 DataInsufficient（PA-1），汇总层以
     * checkEvidenceCompleteness 对账 Profile 后自行判定。
     */
    std::vector<EvidenceGap> missingList() const;

    /**
     * @brief 已注入序列的限定语（trustQualifiers 的缓存读——build 后有效）。
     *
     * @return 限定语 token 清单（顺序＝§5.5 层序；无降级事实＝空清单——
     *         消费方据此渲染精确结论，无限定语即不弱化、也不添加）
     */
    std::vector<std::string_view> trustQualifiersCached() const;

private:
    // ---- 已注入素材（拷贝保存——build 后实例复用时清空）----
    bool mHaveSeries = false;              ///< 已注入序列（必需项 ①③④ 锚）
    DynamicsSeries mSeries{};              ///< 序列拷贝（值语义——身份块＋validity＋样本）
    bool mHaveCondition = false;           ///< 已注入工况结果（必需项 ②＋建议项 ⑤）
    OperatingConditionResult mCondition{}; ///< 工况结果拷贝（peaks/powerEnergy/validity）
    bool mHaveForward = false;             ///< 已注入正动力学检查（建议项 ⑥）
    ForwardCheckOutcome mForward{};        ///< 检查产出拷贝（四态＋诊断）
    // ---- build 产物缓存（访问器读取面）----
    bool mBuilt = false;                          ///< build 已执行
    std::vector<evidence::EvidenceItem> mItems;   ///< 六项证据行（§8.4 表行序）
    std::vector<EvidenceGap> mGaps;               ///< 缺失清单（全量、不短路）
    std::vector<std::string_view> mQualifiers;    ///< 限定语缓存（§5.5 层序）
    DynamicsEvidence mEvidence{};                 ///< 域内组织形态（§4.4）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_EVIDENCEBUILDER_HPP
