/**
 * @file   Evidence.hpp
 * @brief  批量任务点验证的值模型与证据组装器（KIN-03）——批量工作项
 *         记录/三态报告素材/完成矩阵素材、IKinematicEvidenceBuilder
 *         （§8.2 表逐行产出的唯一组装点）与批量 canonical 载荷编码。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Evidence.hpp 行——
 *     "IKinematicEvidenceBuilder（EvidenceItem/搜索未果记录/解析界限素材
 *     组装）"，任务 T05/T07；本头为 T05 半区，碰撞证据引用行随 T07 表尾
 *     追加）、§7.1（批量执行图——逐项证据明细/NotRun/完整性自检）、
 *     §8.2（证据生成表——EvidenceItem/SearchExhaustedRecord/证明素材/
 *     DomainVerdictInputs/payload 五行产出；"只产不判"）、§8.4（确定性
 *     ——批内工作项 (pointOid,conditionId) 字典序全序）、§9.2（
 *     IKinematicEvidenceBuilder 接口契约原文）、§5.6（结果绑定六要素）
 *   - evidence 冻结契约：EvaluationOutput/EvidenceItem/SearchExhausted-
 *     Record/DeterministicInfeasibilityProof/DomainVerdictInputs/
 *     DomainPayload（Evidence.hpp/Verdict.hpp/Envelope.hpp/Evaluator.hpp）
 *   - 治理裁决 O-37（宿主注入形态——本头 BatchQuery 由评估宿主经工厂
 *     闭包注入，视图/求解器/检查点通道均为非 owning 指针）；P-KIN-7
 *     （execution 检查点通道 Draft——本头以自有最小端口 IBatchCheckpoint-
 *     Sink 承载批 watermark，真实通道适配归 L5）
 *   - 任务契约 tasks/foundation/WP-15-T05.json acceptance 1/2/3
 *
 * 背景说明（为什么批量值模型不消费 requirements 头）：req-point-set/
 * req-condition-set 的对象 schema 归 requirements 单元，而 kinematics 受
 * R-1 红线约束（业务域单元互链禁止——卡 §3.2/CMake 配置期守卫），禁止
 * include 其任何头。本头按 O-37 裁决同款纪律定义**自有最小注入值**
 * （BatchTaskPoint/BatchCondition——宿主（L5/评估宿主）把切片内 req
 * 对象解析为基座系位姿/要求值后经工厂闭包注入），单元侧只消费投影值。
 * appliesTo 三值词表/需求等级两值词表在本头以 Batch 前缀枚举镜像登记
 * （语义注释指向 requirements 卡 §4.5/§4.3 的权威行；枚举值序一致），
 * 不引入对端类型依赖。
 *
 * "只算不判"纪律（§7.1/§8.2——acceptance 1 的语义边界）：本头的 per-item
 * 计算状态与全部产出都是**素材**——可行/工程不可行/数据不足的三态最终
 * 判定归 evidence aggregateVerdict（五级汇总），本单元不调用、不预判。
 * Must 违例仅指"启用 Must 条目存在解析界限证明素材"这一域内事实填报
 * （REQ-06 口径——违例语义归域，定级归 evidence）。
 *
 * 线程安全：本头全部实体为纯值/纯接口/无状态服务（无共享可变状态）；
 * BatchComputation 冻结后按只读值对待。确定性：载荷编码定宽小端＋字段
 * 定序（同输入同字节——NFR-COR-01/02）。
 */

#ifndef IRD_KINEMATICS_EVIDENCE_HPP
#define IRD_KINEMATICS_EVIDENCE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/DiagData.hpp>          // DiagnosticRecord（诊断承载）
#include <sdurws/ird/core/Identity.hpp>          // ObjectId/TaskIdentity（绑定面）
#include <sdurws/ird/core/Evaluation.hpp>        // EvaluationMode（模式词表）
#include <sdurws/ird/evidence/Evaluator.hpp>     // EvaluationRequest/EvaluationOutput/IEvaluationContext（§9.3 调用契约）
#include <sdurws/ird/kinematics/Bounds.hpp>      // AnalyticBoundMaterial（结局 5 素材）
#include <sdurws/ird/kinematics/Ik.hpp>          // IIkSolver/IkOutcome（计算委托面）
#include <sdurws/ird/kinematics/KinTypes.hpp>    // TcpRef/IKinRuntimeView/IkTargetRef 值

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——评估器 descriptor/证据行/载荷编码/测试共用，
// 禁第二处字面量。落位说明：批量通道的评估键/契约版本常量唯一书写点
// 在本头而非 Evaluators.hpp，理由与 Ik.hpp 承载 kin-task-point-ik 键
// 相同——证明素材 producer 绑定与证据组装器（本头）先于评估器形态
// （Evaluators.hpp）消费该键，放本头避免 include 环）
// =====================================================================

/// 评估键（evidence 词形闸门内 kebab 形态——卡面 §4.3 键名
/// "kin.task-points-batch" 的实现形态；kebab 化偏差随卡 §14.6 v0.3
/// 登记、T03/T04 同款先例。DependencyKey 才允许点）。
inline constexpr char kTaskPointsBatchEvaluationKey[] = "kin-task-points-batch";

/// 批量评估器输入/输出契约版本（§8.4"求解器版本入 descriptor.
/// contractVersion 与 payload"；进 sliceId——CON-04；一经交付不回退）。
inline constexpr std::uint32_t kTaskPointsBatchContractVersion = 1U;

/// 域载荷登记 token（evidence §7.1 域词表——卡面点形保留；v1 随
/// canonical 布局 codec 版本演进）。
inline constexpr char kTaskPointsBatchPayloadToken[] = "kin.task-points-batch.v1";

/// 黄金锁定默认批大小（D-KIN-6；§7.1 批量执行图"maxBatch 批次大小，
/// 黄金锁定默认 256"——修改走设计变更并同步卡面）。
inline constexpr std::uint32_t kBatchDefaultBatchSize = 256U;

/// 逐项证据行 id（evidence §6.2 EvidenceItem.itemId 词形 "<域>.<项>"——
/// 表 4 运动学行"每任务点 IK 收敛状态与残差"的批量通道落位行；行内容
/// （结局/最佳解指标/硬过滤记录/要求值对比）在载荷逐项表中，证据行以
/// digest＋subject＋caseScope 溯源绑定，登记随卡 §14.6 v0.5）。
inline constexpr char kKinBatchItemOutcomeRowId[] = "kin.task-point-outcome";

/// 搜索未果证据行 id（表 4 运动学行"搜索未果记录"的批量通道落位行——
/// 聚合记录经 output.searchRecord 交付，本行以 digest 溯源其存在）。
inline constexpr char kKinBatchSearchRowId[] = "kin.search-exhausted";

/// 碰撞证据行 id（WP-15-T07 表尾追加——§8.2 行 1"碰撞证据引用"与
/// acceptance 4"碰撞证据明细交付 {subjectPair(ObjectId×ObjectId)，
/// configurationRef, verdict}"的批量通道落位行；登记随卡 §14.6 v0.7。
/// 行状态语义：Satisfied＝碰撞评价在场（明细三元组以行 digest 绑定，
/// 逐条明细在行内容规范字节内）；Missing＝碰撞要求在场而评价未完成
/// （缺检测器/设施异常——证据缺失素材，绝不视为无碰撞，KIN-05）；
/// 无碰撞要求且未评价不出行（碰撞检查不在范围——V13-01 查询面口径）。
inline constexpr char kKinBatchCollisionRowId[] = "kin.collision-verdict";

// =====================================================================
// 注入值词表镜像（requirements 权威词表的 kinematics 侧投影——文件头注）
// =====================================================================

/**
 * @brief 需求等级两值词表（REQ-06 口径——Must/Should）。
 *
 * 与 requirements::RequirementLevel 两值逐值对应（R-1 禁 include——
 * 文件头注），枚举值序一致；Must 条目的计算结局进入 DomainVerdictInputs
 * 填报（§8.2 表行 4）。
 */
enum class BatchRequirementLevel : std::uint8_t {
    /// Must——违例候选进 mustViolations（⑤级 EngineeringInfeasible 素材；
    /// 判定归 evidence）。
    Must,
    /// Should——违例候选进 shouldViolations（Feasible＋警告素材）。
    Should,
};

/**
 * @brief 工况适用范围三值词表（requirements 卡 §4.5 appliesTo——
 *        D-REQ-5"工况侧声明"的投影镜像）。
 *
 * 与 requirements::AppliesToScope 三值逐值对应；展开语义（§7.1）：
 * AllStations＝适用点集全部启用条目；Stations＝仅适用显式清单内对象
 * （清单外的点不产生工作项——appliesTo 过滤）；None＝当前不适用——
 * **显式产生 NotApplicable 工作项**（不静默丢弃，ERR-01 不伪造）。
 */
enum class BatchAppliesToScope : std::uint8_t {
    /// 适用全部任务点（工位）。
    AllStations,
    /// 适用显式清单（stations 内的 ObjectId——悬空引用产生 InputInvalid
    /// 素材，V-12）。
    Stations,
    /// 当前不适用（显式标记 NotApplicable）。
    None,
};

/**
 * @brief 任务点/工况的要求值集（REQ-04 语义投影——§7.1"要求值对比
 *        （容差/裕量/碰撞要求值 vs 结果）"的要求值侧）。
 *
 * 位姿容差（位置 m／姿态 rad）随 BatchTaskPoint 逐点携带（REQ-01）；
 * 本结构承载任务级要求（碰撞/裕量）。value 语义纯结构；线程安全。
 */
struct BatchDemands {
    /// 无碰撞要求（布尔要求值——启用时逐工作项做"实际是否无碰撞"对比；
    /// 碰撞未启用＝该检查 evaluated=false（证据缺失，绝不解读为满足——
    /// KIN-05 口径））。
    bool collisionFreeRequired = false;
    /// 最小关节裕量要求值（可选；单位为比较关节的天然单位 rad|m——
    /// requirements 卡 RequirementDemand 行原文口径；实际值由解的归一化
    /// 裕量（D-KIN-6）经评价区间半行程换算回绝对距离后对比）。
    std::optional<double> minimumJointMargin;

    bool operator==(const BatchDemands& o) const
    {
        return collisionFreeRequired == o.collisionFreeRequired
            && minimumJointMargin == o.minimumJointMargin;
    }
    bool operator!=(const BatchDemands& o) const { return !(*this == o); }
};

/**
 * @brief 批量通道的任务点注入值（req-point-set 启用/停用条目的宿主
 *        解析投影——targetInBase 已由宿主解析到基座系 {B}，§5.1）。
 *
 * 生命周期：值语义（宿主组装、工厂闭包捕获）；线程安全（并发只读）。
 * 全部物理量单位：targetInBase 平移 m／旋转 rad；两容差 m／rad。
 */
struct BatchTaskPoint {
    /// 任务点对象身份（req.points 闭包内——结果绑定 pointOid，§5.6）。
    core::ObjectId pointOid;
    /// 需求等级（Must/Should——verdictInputs 填报的分流依据）。
    BatchRequirementLevel level = BatchRequirementLevel::Must;
    /// 启用标记（false＝该点全部工作项显式标记 NotApplicable——§7.1
    /// "disabled→NotApplicable 显式标记"；不静默丢弃）。
    bool enabled = true;
    /// 目标位姿（基座系 {B} 的 TCP 目标——宿主已按点参考系解析；§5.1）。
    rw::math::Transform3D<double> targetInBase = runtime::detail::identityTransform3D();
    /// 位置残差容差（m；>0 有限——I-REQ-5 与 KIN-TARGET-ILLEGAL 双锚）。
    double positionTolerance = 1e-6;
    /// 姿态残差容差（rad；>0 有限）。
    double orientationTolerance = 1e-6;
    /// TCP 覆盖（可空——空＝用查询级 defaultTcp；非空＝该点专用 TCP；
    /// 覆盖语义登记随卡 §14.6 v0.5：TaskPoint.tcpRef（REQ-01 跨聚合浅
    /// 引用）由宿主解析后落入本字段）。
    std::optional<TcpRef> tcpOverride;
    /// 任务级要求值（碰撞/裕量——与工况要求值合并口径见 BatchDemands 注
    /// 与评估器实现注）。
    BatchDemands demands;

    bool operator==(const BatchTaskPoint& o) const
    {
        return pointOid == o.pointOid && level == o.level && enabled == o.enabled
            && tcpOverride == o.tcpOverride && demands == o.demands
            && positionTolerance == o.positionTolerance
            && orientationTolerance == o.orientationTolerance;
    }
    bool operator!=(const BatchTaskPoint& o) const { return !(*this == o); }
};

/**
 * @brief 批量通道的工况注入值（req-condition-set 条目的宿主解析投影
 *        ——caseSubset 内各工况的适用范围与要求值投影）。
 *
 * 生命周期与线程约束同 BatchTaskPoint。
 */
struct BatchCondition {
    /// 工况对象身份（req.conditions 闭包内＝evidence CaseId 同源）。
    core::ObjectId conditionId;
    /// 需求等级（必验派生源——I-REQ-9 口径的投影；本单元只承载不解释）。
    BatchRequirementLevel level = BatchRequirementLevel::Must;
    /// 启用标记（false＝该工况全部工作项显式标记 NotApplicable）。
    bool enabled = true;
    /// 适用范围（三值词表——展开语义见枚举注）。
    BatchAppliesToScope appliesTo = BatchAppliesToScope::AllStations;
    /// 显式工位清单（仅 Stations 有语义；表内 ObjectId 悬空（不在点集）
    /// ＝对应 (悬空点,工况) 工作项 InputInvalid 素材——V-12）。
    std::vector<core::ObjectId> stations;
    /// 工况级要求值（碰撞/裕量——与点级合并：碰撞取"或"、裕量取"更强"
    /// ＝可选值中的较大者；合并口径登记随卡 §14.6 v0.5）。
    BatchDemands demands;

    bool operator==(const BatchCondition& o) const
    {
        return conditionId == o.conditionId && level == o.level
            && enabled == o.enabled && appliesTo == o.appliesTo
            && stations == o.stations && demands == o.demands;
    }
    bool operator!=(const BatchCondition& o) const { return !(*this == o); }
};

// =====================================================================
// 批量计算值模型：per-item 状态/要求值对比/工作项记录/完成矩阵素材
// =====================================================================

/**
 * @brief 单工作项的计算状态（§7.1"per-item 计算状态——不是工程判定"）。
 *
 * ★ 与 acceptance/卡面五值词表的偏差登记（随卡 §14.6 v0.5）：卡面 §7.1
 * 列五值（CandidateFound/NoConvergence/AllFiltered/InputInvalid/
 * NotApplicable），实现增补两值——
 *   - BoundExceeded：结局 5（解析界限证明素材）的承载态。§5.4 铁律
 *     要求结局 5 素材存活且"三态报告素材完备"（工程不可行素材）；
 *     若强行归入 NoConvergence/AllFiltered 会把"工程不可行素材"污染进
 *     "数据不足"——两态素材混淆违反 acceptance 1 的三态完备语义，故
 *     独立成态。
 *   - NotRun：分批取消/失败批的如实标记（§7.1"取消→未完成批如实标记
 *     NotRun"；§9.2 @post"每工作项终态标记齐备（含 NotRun）"——卡面
 *     五值清单未含 NotRun 但 §7.1/§9.2 明文要求该终态存在）。
 * 偏差理由与逐条对照已登记单元卡 §14.6 v0.5（DTB §5.4）。
 */
enum class BatchItemStatus : std::uint8_t {
    /// 求解得到 ≥1 个通过全部硬过滤的解（结局 1/4——最佳解随记录交付；
    /// 可行性素材，判定归 evidence）。
    CandidateFound,
    /// 全部初值迭代至上限未收敛（结局 2——搜索未果素材→DataInsufficient
    /// 候选，不得输出不可行）。
    NoConvergence,
    /// 有收敛候选但全部被硬过滤（结局 3——搜索未果素材，同上）。
    AllFiltered,
    /// 目标超出解析工作半径上界（结局 5——证明素材随记录交付，仅素材
    /// 不裁定；增补态，偏差登记随卡 §14.6 v0.5）。
    BoundExceeded,
    /// 输入非法素材（悬空引用等——V-12；该项不参与求解，记录附细节）。
    InputInvalid,
    /// 不适用（点停用/工况停用/appliesTo=None——显式标记，附原因）。
    NotApplicable,
    /// 未运行（分批取消/失败——未完成批的如实标记，附原因；无伪完成）。
    NotRun,
};

/**
 * @brief 单条要求值对比（§7.1 逐项明细 demandsChecked 的元素——
 *        "要求值对比（实际/要求/单位）"的值承载；§8.2 行 1 内容列）。
 *
 * 语义：evaluated=false＝该检查未执行（无解可查/碰撞未启用——证据缺失
 * 口径，**不解读为满足**，KIN-05 同源）；satisfied 仅在 evaluated=true
 * 时有意义。值语义纯结构；线程安全。
 */
struct BatchDemandCheck {
    /// 检查种类词表（§7.1 逐项明细四类——逐值单位见枚举注）。
    enum class Kind : std::uint8_t {
        PositionResidual,   ///< 位置残差 vs 点容差（实际 m／要求 m）
        OrientationResidual,///< 姿态残差 vs 点容差（实际 rad／要求 rad）
        CollisionFree,      ///< 无碰撞要求（要求=布尔真；实际无标量——
                            ///  actual 恒 0、以 satisfied 承载判定）
        MinJointMargin,     ///< 最小关节裕量（实际 rad|m——取 arg-min 关节
                            ///  的天然单位；要求同单位）
    };

    /// 检查种类。
    Kind kind = Kind::PositionResidual;
    /// 是否执行了该检查（false＝无解可查或碰撞未启用——证据缺失口径）。
    bool evaluated = false;
    /// 对比结论（仅 evaluated=true 有意义）。
    bool satisfied = false;
    /// 实际值（单位随 kind——CollisionFree 恒 0 无量纲）。
    double actual = 0.0;
    /// 要求值（单位随 kind；CollisionFree 恒 0——要求为布尔真）。
    double required = 0.0;
    /// 单位 token（"m"/"rad"/"-"——core Units 词表内取值；记录面）。
    std::string unit;

    bool operator==(const BatchDemandCheck& o) const
    {
        return kind == o.kind && evaluated == o.evaluated && satisfied == o.satisfied
            && actual == o.actual && required == o.required && unit == o.unit;
    }
    bool operator!=(const BatchDemandCheck& o) const { return !(*this == o); }
};

/**
 * @brief 单工作项的批量计算记录（§7.1 逐项证据明细的值承载：
 *        {pointOid, conditionId, 状态, 最佳解?, 硬过滤记录, 搜索未果?,
 *        证明素材?, 要求值对比, 原因文本}）。
 *
 * 不变式（组装器保证）：status∈{CandidateFound} 时 bestSolution 必填；
 * status∈{NoConvergence,AllFiltered} 时 searchRecord 必填；status==
 * BoundExceeded 时 proofMaterial 必填；status∈{NotApplicable,NotRun} 时
 * reason 必填非空（ERR-01 显式标记不伪造）；status==InputInvalid 时
 * reason 必填非空。值语义；线程安全（并发只读）。
 *
 * 等值面说明（实现口径，登记随卡 §14.6 v0.5）：本结构**不定义
 * operator==**——bestSolution/filteredRecords 等成员为求解器产物值
 * （KinematicSolution/FilteredSolutionRecord 等无量等值），逐成员等值
 * 无判定语义；记录的等值判定统一走 canonical 载荷字节
 * （encodeBatchPayloadCanonical——同输入同字节），与 NFR-COR-02 的
 * "字节等值"口径一致。
 */
struct BatchWorkItemRecord {
    /// 任务点对象身份（§5.6 绑定）。
    core::ObjectId pointOid;
    /// 工况对象身份（批量通道必填——§5.6 双重绑定）。
    core::ObjectId conditionId;
    /// 点需求等级（Must/Should——verdictInputs 填报的分流依据，REQ-06；
    /// 记录冗余投影值：组装器只消费 computation，不回查查询值）。
    BatchRequirementLevel pointLevel = BatchRequirementLevel::Must;
    /// 计算状态（七值——枚举注；不是工程判定）。
    BatchItemStatus status = BatchItemStatus::NotRun;
    /// 求解结局（可空——预终结项（NotApplicable/InputInvalid/NotRun）
    /// 无结局）。
    std::optional<IkOutcomeKind> outcomeKind;
    /// 最佳解（稳定排序首位——§7.1 bestSolution；仅 CandidateFound 非空）。
    std::optional<KinematicSolution> bestSolution;
    /// 硬过滤记录（诊断价值——"为什么少了解"；随逐项载荷交付）。
    std::vector<FilteredSolutionRecord> filteredRecords;
    /// 搜索未果记录（结局 2/3 必附——聚合前逐项原始值）。
    std::optional<IkSearchRecord> searchRecord;
    /// 解析界限证明素材（结局 5 必附——producer 绑定在组装器重绑为批量
    /// 评估键，见 KinematicEvidenceBuilder 类注）。
    std::optional<AnalyticBoundMaterial> proofMaterial;
    /// 要求值对比（实际/要求/单位——四类检查逐条）。
    std::vector<BatchDemandCheck> demandChecks;
    /// 碰撞未评价标记（策略未启用碰撞——硬过滤③跳过；证据缺失语义，
    /// KIN-05：绝不解读为无碰撞）。
    bool collisionNotEvaluated = false;
    /// 原因文本（NotApplicable/InputInvalid/NotRun 必填非空——ERR-01；
    /// 其余状态为空）。
    std::string reason;
};

/**
 * @brief 逐工况完成标记（acceptance 2"完成矩阵素材"的值承载——evidence
 *        §6.6 CaseCoverageMatrix 的②级门禁消费素材；本单元只标记不判定
 *        漏验后果）。
 *
 * 三标记互斥（组装器保证：executed/notRun/notApplicable 恰一为 true）。
 * 值语义；线程安全。
 */
struct BatchCaseCompletion {
    /// 工况对象身份（＝caseSubset 条目——逐一对应）。
    core::ObjectId conditionId;
    /// 已执行（该工况全部工作项均有计算终态——覆盖的唯一凭据态）。
    bool executed = false;
    /// 未运行（该工况存在 NotRun 工作项——取消/失败批，如实标记）。
    bool notRun = false;
    /// 不适用（该工况停用/appliesTo=None——显式标记，不计漏验）。
    bool notApplicable = false;
    /// 该工况已计算（求解完成）的工作项数（计数，无量纲）。
    std::uint64_t computedItemCount = 0;
    /// 该工况 NotRun 工作项数（计数，无量纲）。
    std::uint64_t notRunItemCount = 0;

    bool operator==(const BatchCaseCompletion& o) const
    {
        return conditionId == o.conditionId && executed == o.executed
            && notRun == o.notRun && notApplicable == o.notApplicable
            && computedItemCount == o.computedItemCount
            && notRunItemCount == o.notRunItemCount;
    }
    bool operator!=(const BatchCaseCompletion& o) const { return !(*this == o); }
};

/**
 * @brief 批量计算结果（评估器产出→证据组装器输入的中间值——§9.2
 *        IKinematicEvidenceBuilder.build 第一参数 BatchComputation）。
 *
 * 不变式（评估器组装保证、组装器复核——acceptance 2 完整性自检）：
 *   - items 按 (pointOid, conditionId) 字典序**严格递增**（全序、无重复
 *     ——§8.4；重复项已在展开期按集合语义去重）；
 *   - totalWorkItems == processedItemCount + notRunItemCount；
 *   - caseSubset 每项恰有一条 caseCompletion 记录（顺序＝caseSubset
 *     去重后的字典序）；
 *   - incomplete==true 当且仅当 notRunItemCount>0。
 * 值语义；冻结后只读。确定性：同输入同值（NFR-COR-01）。
 */
struct BatchComputation {
    // ---- 结果绑定（§5.6 六要素——由评估请求填充）----
    core::ContentIdentity snapshotId;  ///< 来源快照内容身份
    core::ContentIdentity sliceId;     ///< 冻结输入切片身份（CON-04）
    core::ContentIdentity configDigest; ///< 求解配置摘要（T10 落位前可零值）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 评估模式
    std::uint64_t seed = 0;            ///< 确定性种子（初值策略序列源）
    std::vector<double> referenceQ;    ///< 排序参考构型（rad|m——D-KIN-4）
    core::TaskIdentity task;           ///< 任务五元组（载荷绑定面）
    /// 本批次工况子集（去重后字典序——完成矩阵素材的对账分母）。
    std::vector<core::ObjectId> caseSubset;

    // ---- 工作项与分批统计 ----
    /// 全部工作项记录（全序——§8.4；含预终结项与 NotRun 项）。
    std::vector<BatchWorkItemRecord> items;
    /// 工作项总数（＝items.size()——自检基准）。
    std::uint64_t totalWorkItems = 0;
    /// 已处理项数（含预终结项——各完成批的批内项数之和，Σ批项数）。
    std::uint64_t processedItemCount = 0;
    /// NotRun 项数（取消/失败批未处理项——acceptance 2 自检第二元）。
    std::uint64_t notRunItemCount = 0;
    /// 批总数（maxBatch 分批后）。
    std::uint64_t batchCount = 0;
    /// 已完成批数（检查点 watermark 已推进到的批号）。
    std::uint64_t completedBatchCount = 0;
    /// 不完整标记（notRunItemCount>0——调用侧据此不产 Completed envelope，
    /// §7.1 完整性校验）。
    bool incomplete = false;
    /// 逐工况完成标记（caseSubset 逐一对应——完成矩阵素材）。
    std::vector<BatchCaseCompletion> caseCompletion;
};
// （BatchComputation 不定义 operator==——items 含求解器产物值，等值判定
// 统一走 canonical 载荷字节，理由同 BatchWorkItemRecord 注。）

// =====================================================================
// IBatchCheckpointSink——批 watermark 的最小消费端口（P-KIN-7 处置形态）
// =====================================================================

/**
 * @brief 批检查点 watermark 上报端口（§8.3"检查点：每批完成→watermark
 *        （批号）（execution 检查点通道）"的本单元侧最小投影）。
 *
 * 语义边界（P-KIN-7 处置——IKinCollisionSession 同款纪律）：检查点**写
 * 出设施**（checkpoints/<run-id>/ 落盘、CheckpointRecord 组装、CON-04
 * 兼容判定）唯一归 execution（ICheckpointCoordinator）；本接口只投影
 * 评估器实际产生的事实——"第 N 批已完成"——宿主以适配器实现本接口把
 * watermark 接入真实检查点通道（适配归 L5/执行宿主）。评估器对空指针
 * 容忍（纯计算无检查点通道合法——§9.2 @副作用"归档/检查点经宿主通道"）。
 *
 * 生命周期与线程：宿主持有并保证 evaluate() 期间存活（非 owning）；
 * 单工作线程内被调用（threadCount>1 时由分片合并后的主控线程调用，
 * 实现方无须内部加锁——调用不并发）。
 */
class IBatchCheckpointSink {
public:
    virtual ~IBatchCheckpointSink() = default;

    /**
     * @brief 上报批 watermark（每完成一批恰一次——批粒度检查点）。
     *
     * @param completedBatchCount [in] 已完成批数（≥1，单调递增；幂等键
     *                            ＝批号+sliceId——§8.3，恢复侧按其续跑）
     * @param totalBatchCount     [in] 批总数（≥1；续跑统计基点）
     */
    virtual void batchWatermark(std::uint64_t completedBatchCount,
                                std::uint64_t totalBatchCount) = 0;
};

// =====================================================================
// EvidenceContext 与 IKinematicEvidenceBuilder（§9.2 接口契约原文）
// =====================================================================

/**
 * @brief 证据组装上下文（§9.2 build 第二参数 EvidenceContext 的值承载
 *        ——组装所需的评估请求只读引用）。
 *
 * 生命周期：request 为**非 owning** 指针——调用方（评估器）保证 build()
 * 调用期间存活；组装器不修改请求（冻结值）。空指针＝调用方契约违约
 * （构造期不校验、build 期 fail-fast——见 KinematicEvidenceBuilder）。
 * 值语义（指针拷贝即浅拷贝——指向同一冻结请求）。
 */
struct EvidenceContext {
    /// 评估请求（任务五元组/模式/快照/切片——绑定事实来源）。
    const evidence::EvaluationRequest* request = nullptr;
};

/**
 * @brief 证据组装器接口（§9.2 七个必需接口之六——EvidenceItem/搜索未果
 *        记录/解析界限素材/verdictInputs 的**唯一组装点**，NFR-MNT-04）。
 *
 * @post 产出符合 evidence §9.3 EvaluationOutput 形状；证明素材自带
 *       expectedSliceId 绑定（D-09 可校验）；诊断全部经已注册码
 *       （KIN-RESULT-INCOMPLETE/KIN-POINT-REF-DANGLING——§9.6）。
 * @确定性 同输入→同输出（证据序＝工作项全序，§8.4）。
 */
class IKinematicEvidenceBuilder {
public:
    virtual ~IKinematicEvidenceBuilder() = default;

    /**
     * @brief 把批量计算结果组装为 evidence EvaluationOutput（§8.2 表五行
     *        产出的唯一落点）。
     *
     * @param computation [in] 批量计算结果（冻结值；调用方持有）
     * @param ctx         [in] 组装上下文（request 指针须非空且存活）
     * @return 评估产出（只产不判——判定归 evidence aggregateVerdict）
     *
     * @throws std::invalid_argument ctx.request 为空指针（调用方契约违约）；
     *         std::logic_error computation 违反 BatchComputation 不变式
     *         （排序/自检/完成矩阵——内部实现缺陷，不静默）
     */
    virtual evidence::EvaluationOutput
    build(const BatchComputation& computation, const EvidenceContext& ctx) const = 0;
};

/**
 * @brief IKinematicEvidenceBuilder 的无状态实现（§3.1 计算库落点）。
 *
 * 组装口径（§8.2 表逐行；登记随卡 §14.6 v0.5）：
 *   - **逐项证据行**（行 1）：每工作项一条 EvidenceItem（itemId=
 *     kKinBatchItemOutcomeRowId；subject=pointOid、caseScope={conditionId}
 *     ——逐项溯源；artifactDigest＝批量载荷 canonical 字节摘要，逐项
 *     明细在载荷表内、证据行以摘要＋对象范围溯源其条目）。状态映射：
 *     四种计算终态（CandidateFound/NoConvergence/AllFiltered/
 *     BoundExceeded）→Satisfied（产物存在）；NotApplicable→
 *     NotApplicable＋原因（C2 不计缺失）；InputInvalid→Invalid＋
 *     KIN-POINT-REF-DANGLING 原因诊断（ERR-01）；NotRun→Missing（无产物
 *     ——漏验素材交 evidence 覆盖矩阵）。
 *   - **SearchExhaustedRecord**（行 2）：聚合全部 NoConvergence/
 *     AllFiltered 工作项的记录（预算/初值数求和、逐解过滤记录按全序
 *     拼接）→output.searchRecord；有聚合内容时另出
 *     kKinBatchSearchRowId 汇总行（Satisfied＋载荷摘要）。
 *   - **证明素材**（行 3）：全序首个 BoundExceeded 项的素材 →
 *     output.proof；producer 重绑为本评估器键＋契约版本（对外交付的
 *     产生者＝批量评估器——validateProof 的注册查证对象；素材内容与
 *     snapshotId/sliceId 绑定保持原值）。
 *   - **DomainVerdictInputs**（行 4）：REQ-06 口径填报——仅"启用 Must/
 *     Should 点存在解析界限证明素材"产生违例条目（itemId＝点对象规范
 *     文本 obj-<hex>、detail＝素材摘要语）；搜索未果**不**产生违例
 *     （C5/C8——DataInsufficient 口径，不得升级为不可行）；满足/不适用
 *     不产生条目（DomainVerdictInputs 只承载违例清单）。
 *   - **payload**（行 5）：encodeBatchPayloadCanonical（canonical、
 *     五元组绑定 §5.6）＋SHA-256 摘要（CR-02）。
 *   - **碰撞证据行**（WP-15-T07 表尾追加——acceptance 4）：碰撞评价在场的
 *     工作项逐项一条 kKinBatchCollisionRowId 行（Satisfied＋行内容规范
 *     字节摘要——三元组 {subjectPair(ObjectId×ObjectId), configurationRef,
 *     verdict} 逐条编码于行内容字节）；碰撞要求在场而评价未完成的项一条
 *     Missing 行（证据缺失素材——绝不视为无碰撞，KIN-05）；无要求且未
 *     评价不出行（不在范围——V13-01）。诊断面：存在"要求在场＋未评价"
 *     项时聚合一条 KIN-COLLISION-UNAVAILABLE（§9.6 行 9——T07 产码面）。
 *   - **诊断**：incomplete==true → KIN-RESULT-INCOMPLETE 一条（NotRun
 *     清单摘要进 cause）；InputInvalid 逐项 → KIN-POINT-REF-DANGLING。
 */
class KinematicEvidenceBuilder final : public IKinematicEvidenceBuilder {
public:
    /// 无状态——默认构造即可用（§9.4"无状态建议共享"）。
    KinematicEvidenceBuilder() = default;

    evidence::EvaluationOutput build(const BatchComputation& computation,
                                     const EvidenceContext& ctx) const override;
};

// =====================================================================
// 批量载荷 canonical 编码（§5.6 结果绑定六要素的字节面）
// =====================================================================

/**
 * @brief 将批量计算结果编码为域 canonical 字节（定宽小端＋字段定序）。
 *
 * 编码布局（codec 版本 1；magic "IRDBP01"＋版本 u32）：incomplete u8＋
 * 绑定块（snapshotId/sliceId/configDigest 各 32B＋mode u8＋seed u64＋
 * referenceQ u32＋f64×n＋任务五元组 4×16B＋attempt u64＋evaluationKey
 * u32+len＋contractVersion u32）＋caseSubset u32×16B＋统计（total/
 * processed/notRun/batchCount/completedBatchCount 各 u64）＋caseCompletion
 * u32×{conditionId 16B＋三标记 u8＋computed/notRun u64×2}＋工作项
 * u32×{pointOid 16B＋conditionId 16B＋status u8＋outcomeKind present u8
 * [+u8]＋collisionNotEvaluated u8＋demandChecks u32×{kind u8＋evaluated
 * u8＋satisfied u8＋actual f64＋required f64＋unit u32+len}＋bestSolution
 * present u8[+解块（q u32+f64×n＋残差 f64×2＋margins u32+f64×n＋
 * minMargin/manipulability/conditionNumber f64×3＋碰撞 evaluated/
 * inCollision u8×2＋对象对 u32×16B＋sourceInitIndex/iterations u32×2＋
 * signature u32+len＋solverVersion u32）]＋filteredRecords u32×{signature
 * u32+len＋reason u8}＋searchRecord present u8[+budget/guesses u64×2＋
 * itersPerInit u32×u32]＋proofMaterial present u8＋reason u32+len}。
 * f64＝IEEE754 位模式小端（含 +∞）。
 *
 * @param computation [in] 批量计算结果（冻结值）
 * @param task        [in] 任务五元组（绑定面——computation.task 同值，
 *                    显式入参与 T04 编码器同构）
 * @return canonical 字节（确定性：同输入同字节——NFR-COR-01）
 *
 * 纯函数；线程安全；不抛。
 */
std::vector<std::uint8_t> encodeBatchPayloadCanonical(const BatchComputation& computation,
                                                      const core::TaskIdentity& task);

/**
 * @brief 碰撞证据行的行内容编码（WP-15-T07——acceptance 4 明细三元组的
 *        公开字节面；kKinBatchCollisionRowId 行的 artifactDigest 取值
 *        源，公开性供测试与下游核对行内容摘要）。
 *
 * 布局（定宽小端＋字段定序；确定性——同项同字节，NFR-COR-01）：条目数
 * u32＋逐条 {configurationRef len(4)+bytes＋objectA 16B＋objectB 16B＋
 * verdict u8}。三元组映射（§8.2/acceptance 4）：subjectPair＝(objectA,
 * objectB)（ObjectId 对——policy 会话规范序 A<B；零明细形态以全零保留
 * 值占位）、configurationRef＝构型签名（I-KIN-3 记录键）、verdict＝
 * 1 碰撞／0 评价为无碰撞。条目来源与序（确定性）：最佳解（评价在场）
 * 一条在前，Collision 原因过滤记录按全序逐条在后；条目只在评价在场时
 * 产生——缺失态不出条目（行状态 Missing 承载，KIN-05 不伪造已检）。
 *
 * @param item [in] 单个工作项记录（冻结值）
 * @return 行内容规范字节（确定性；纯函数不抛）
 */
std::vector<std::uint8_t> encodeCollisionVerdictRowCanonical(const BatchWorkItemRecord& item);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_EVIDENCE_HPP
