/**
 * @file   PoseReach.hpp
 * @brief  独立位姿可达性分析（KIN-11）——位置列表×方向采样×工具滚转
 *         采样的确定性目标集生成（复用 §7.2 黄金设施：斐波那契螺旋
 *         方向集×roll 均分——D-KIN-6）、计划预览（IK 目标数——AT-26）、
 *         逐目标位姿 IK 评估（协作取消/进度/检查点——EX-T03/EX-T05
 *         语义，与 T05/T06 批评估执行管线同构、零新跨单元接线）、方向
 *         覆盖率报告（与区域覆盖 KIN-04 互补）与 kin-pose-reachability
 *         评估器形态（§9.3 注册归 L5 装配，本单元不自我注册）。
 *
 * 设计依据：
 *   - units/kinematics.md §7.3（R2 边界——探索式结果**不得**进入 Must
 *     区域判定或必验覆盖；§7.2 冻结样本集是唯一正式口径；复用 §7.2
 *     方向集生成器与解集设施）、§11 T16 行（独立位姿可达性——已立卡
 *     tasks/foundation/WP-15-T16.json）、§180 行（位姿可达性：斐波那契
 *     螺旋方向×滚转、计划预览、协作取消、方向覆盖率——方向集生成器
 *     设计已在 §7.2 登记，R2 复用）、§3.4（随机性唯一来源＝seed 派生
 *     确定性序列；数值恒 SI）、§8.3/§8.4（检查点/确定性/并行——批粒度
 *     watermark＋同输入同线程数逐位一致）
 *   - REQUIREMENTS KIN-11（P1/R2 原文："支持独立位姿可达性分析：位置
 *     列表 × 方向采样 × 工具滚转采样，计划预览（IK 目标数）、后台运行、
 *     协作取消与方向覆盖率报告；与区域覆盖（KIN-04）互补"）、AT-26
 *     （"可达性计划预览与方向覆盖率报告"）
 *   - 任务契约 tasks/foundation/WP-15-T16.json acceptance 1~4
 *
 * 背景说明（为什么本分析不是第二个区域覆盖——与 KIN-04 的四条边界，
 * 登记随卡 §14.6 v0.20）：
 *   1. **位置来源不同**：KIN-04 的位置样本由 requirements 采样计划
 *      （Grid/Random）生成、经 sampleSetIdentity 冻结入快照；本分析的
 *      位置是**用户直接给定的列表**（无区域对象、无计划对象、无冻结
 *      凭据）——因此**没有身份对账轨**（KIN-SAMPLE-IDENTITY-MISMATCH
 *      轨不适用），目标集每次按 query 确定性再生（D-KIN-6 同款）。
 *   2. **不产证据行**：评估器输出只交付 payload（分析报告素材）＋诊断，
 *      **零 EvidenceItem**——R2 边界（§7.3）的结构表达：无证据行＝无
 *      证据资格面，evidence 覆盖矩阵无从消费，探索式结果进不了 Must
 *      判定/必验覆盖（KIN-04 走逐计划证据行——两通道的正式性分野）。
 *   3. **模式面表达**：评估器 supportedModes＝{Quick}（§7.3"Quick/
 *      估算性质，AT-25/26"原文）——Verified 不支持，正式证据链的模式
 *      门禁在声明面上即关闭。
 *   4. **无碰撞要求值**：KIN-05 的"缺检测器→DataInsufficient"承接面
 *      是覆盖通道的 plan 级要求值（BatchDemands，requirements 工况侧
 *      来源）；本分析无工况/无要求值输入，碰撞会话只是求解配置透传
 *      （策略启用时装配层注入，解级硬过滤③照常；缺会话＝策略未启用
 *      ——collisionNotEvaluated 如实标记，绝不解读为无碰撞，KIN-05
 *      口径同源；会话在场而构型评价未完成→该目标 DataInsufficient，
 *      同 T06 设施臂）。
 *
 * 目标状态词表（复用声明）：逐目标状态**直接复用** §7.2 样本状态五值
 * 词表 SampleState（Reached/Unreachable/DataInsufficient/NotApplicable/
 * NotRun）与求解结局映射口径（Sampling.hpp 文件头注）——同一域词表
 * 不第二套（NFR-MNT-04）；NotApplicable 为词表完备性保留值，本通道
 * 生成面不产此态（同 T06）。方向覆盖的聚合口径（存在性——∃roll
 * Reached 即该方向可达）与聚合优先级见 computePoseReachCoverage 注。
 *
 * 线程安全：本头全部实体为纯值/纯接口/无状态服务（无共享可变状态）；
 * 确定性：同 (query) 同目标集同序、同 (query, seed) 同结果（D-KIN-6
 * 同款纪律——方向集/roll 均分为黄金锁定确定性生成，seed 只进 IK 初值
 * 的 SeededRandom 序列）；载荷编码定宽小端＋字段定序（NFR-COR-01/02）。
 */

#ifndef IRD_KINEMATICS_POSEREACH_HPP
#define IRD_KINEMATICS_POSEREACH_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Digest.hpp>        // ContentIdentity（配置摘要/绑定面）
#include <sdurws/ird/core/Evaluation.hpp>    // EvaluationMode（模式词表）
#include <sdurws/ird/core/Identity.hpp>      // TaskIdentity（任务五元组——载荷绑定面）
#include <sdurws/ird/evidence/Evaluator.hpp>  // IEngineeringEvaluator（评估器形态）
#include <sdurws/ird/kinematics/Coverage.hpp>  // CoverageTotals（目标粒度计数面复用）
#include <sdurws/ird/kinematics/Errors.hpp>  // KinematicsError（评估期批量级零素材轨）
#include <sdurws/ird/kinematics/Evidence.hpp>  // IBatchCheckpointSink/kBatchDefaultBatchSize
                                                //   （检查点端口与批量黄金值——T05 复用面）
#include <sdurws/ird/kinematics/Ik.hpp>      // IIkSolver/IKinCollisionSession/InitialValueStrategy
#include <sdurws/ird/kinematics/KinTypes.hpp>  // IKinRuntimeView/TcpRef（宿主注入面）
#include <sdurws/ird/kinematics/Sampling.hpp>  // SampleState（§7.2 五值词表复用——文件头注）
#include <sdurws/ird/runtime/Description.hpp>  // detail::identityTransform3D()——PoseReachTarget
                                                //   位姿恒等初值（Transform3D 默认构造走
                                                //   外联符号——冒烟模式不可链接，Sampling.hpp 同款规避）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——评估器 descriptor/证据面/载荷编码/测试共用，禁
// 第二处字面量。落位说明：键/token/契约版本常量的唯一书写点在本头而非
// Evaluators.hpp——T06 kRegionCoverage* 常量落 Sampling.hpp 的同款取舍：
// 目标生成与载荷编码（本头/本 TU）先于评估器形态消费这些常量，放本头
// 避免 include 环）
// =====================================================================

/// 评估键（evidence 词形闸门内 kebab 形态——T03~T06 四键 kebab 化偏差
/// 的同款随附口径；卡面点形键的登记随卡 §14.6 v0.20）。
inline constexpr char kPoseReachabilityEvaluationKey[] = "kin-pose-reachability";

/// 评估器输入/输出契约版本（进 sliceId——CON-04；一经交付不回退）。
/// ★ v2（KIN-P5/审查 A06）：逐目标初值的 SeededRandom 序列随 64 位完整
///   种子混合（kIkSolverContractVersion 2→3 同批）整体改变——同 (目标集,
///   种子) 在 v1/v2 两代下的可达性结论可不同，版本不升级会让两代评估
///   产出假借同一 slice 身份比较。
inline constexpr std::uint32_t kPoseReachabilityContractVersion = 2U;

/// 域载荷登记 token（evidence §7.1 域词表——卡面点形保留；v1 随
/// canonical 布局 codec 版本演进）。
inline constexpr char kPoseReachabilityPayloadToken[] = "kin.pose-reachability.v1";

/// 逐目标评估的进度/分批 phase token（reportProgress 的 phase 字段落值
/// ——§7.1/§8.3 批粒度上报的位姿可达通道对应物；kRegionCoveragePhase
/// ＝"solve-sample" 的同构面）。
inline constexpr char kPoseReachabilityPhase[] = "solve-pose-target";

// =====================================================================
// PoseReachabilityQuery——分析查询（位置列表×方向×roll＋求解配置）
// =====================================================================

/**
 * @brief 独立位姿可达性分析的单次查询（KIN-11 的全部输入——位置列表
 *        直给＋方向/roll 采样计数＋求解配置＋执行协作参数）。
 *
 * 与 RegionCoverageQuery 的同构与差异（文件头"四条边界"注的查询面）：
 * 求解配置字段（referenceQ/初值/迭代/容差/去重/configDigest）与执行
 * 协作参数（maxBatchSize/threadCount/solver/checkpointSink）逐字段
 * 同构——同一批评估执行管线的消费面；差异＝plans/conditions/budget
 * 三块不出现（无区域/无工况/无采样预算——位置列表直给，seed 独立
 * 承载且语义收窄，见 seed 字段注）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct PoseReachabilityQuery {
    /// 分析位置列表（基座系 {B}，单位 m；**≥1 条且逐条三分量有限**——
    /// 装配期 fail-fast 面：独立分析无目标无意义，空列表＝调用方组装
    /// 错误，不伪造空报告。区别于 KIN-04 的零样本语义（I-REQ-6 是
    /// requirements 采样定义的合法性——计划计数可为 0 走 DataInsufficient；
    /// 本列表是分析工具的直接输入，同求解配置参数的校验位阶）。
    std::vector<rw::math::Vector3D<double>> positions;
    /// 方向采样数（斐波那契螺旋方向集样本数——generateDirections 黄金
    /// 锁定消费；≥1。默认 1＝无权威默认不发明数值（OrientationSampling-
    /// Definition 缺省口径同源——P-POL-2 精神；UI 侧可给推荐值）。无量纲。
    std::uint32_t directionSamples = 1U;
    /// roll 均分样本数（generateRolls 黄金锁定消费；≥1。默认 1 同上）。
    /// 无量纲。
    std::uint32_t rollSamples = 1U;
    /// 默认 TCP（KIN-14 默认 TCP 的查询级承载——解析规则同 KinTypes.hpp
    /// TcpRef 注；无工具/悬空＝评估期批量级 KIN-NO-TCP 零素材，键不命中
    /// ＝装配期 fail-fast——T05/T06 两分口径同源）。
    TcpRef defaultTcp;

    // ---- 求解配置（config.ik 的直传投影——T04/T05/T06 同构字段序）----
    /// 排序参考构型（rad|m——D-KIN-4 显式输入；维度须＝设备自由度）。
    std::vector<double> referenceQ;
    /// 初值策略（§5.3 三值——逐目标 IK 的初值来源）。
    InitialValueStrategy initialStrategy = InitialValueStrategy::JointGrid;
    /// 初值数量（≥1）。
    std::uint32_t initialValuesCount = 1U;
    /// 单初值迭代上限（≥1）。
    std::uint32_t iterationLimit = 1U;
    /// 位置残差容差（m；>0 有限——附录 D 第 1 项）。
    double positionTolerance = 1e-6;
    /// 姿态残差容差（rad；>0 有限——附录 D 第 2 项；本通道全部目标都是
    /// 完整位姿目标，姿态容差逐目标生效）。
    double orientationTolerance = 1e-6;
    /// 去重阈值（rad|m 逐轴；>0 有限——附录 D 第 3 项）。
    double dedupThresholdPerAxis = 1e-6;
    /// 求解配置摘要（config.ik——计算源见 AnalysisConfig.hpp，WP-15-T10；
    /// 入结果身份）。
    core::ContentIdentity configDigest;    /// 确定性种子（**语义收窄于 KIN-04**：本通道方向集/roll 均分为黄金
    /// 锁定确定性生成、不消费 seed；seed 唯一消费面＝初值策略
    /// SeededRandom 的序列源（§3.4 单一种子纪律）。0 合法当且仅当
    /// initialStrategy != SeededRandom（同 IkRequestIdentity.seed"0＝
    /// 未用随机策略"语义）；SeededRandom ＋ seed=0＝装配期 fail-fast
    /// （I-KIN-4 同款——不做 0→1 静默替换，NFR-COR-03）。"同种子重跑
    /// 结果一致"（acceptance 4）由此承载：同 query 同 seed 同初值序
    /// →同结果。无量纲。
    std::uint64_t seed = 0;

    // ---- 碰撞会话（策略透传面——文件头边界 4）----
    /// 碰撞会话句柄（非 owning；空＝检测器不可用/策略未注入——V13-01
    /// 无布尔开关。本通道无"碰撞要求值"（无工况来源），会话在场＝解级
    /// 硬过滤③照常（求解器内部）；缺会话＝策略未启用（正常态，
    /// collisionNotEvaluated 标记如实——绝不解读为无碰撞，KIN-05 口径
    /// 同源）；会话在场而构型评价未完成→该目标 DataInsufficient，T06
    /// 设施臂同款）。
    const IKinCollisionSession* collisionSession = nullptr;

    // ---- 执行协作参数（§7.1/§8.3/§8.4——目标批粒度；T05/T06 同构）----
    /// 目标批大小上限（≥1；默认 kBatchDefaultBatchSize=256——D-KIN-6
    /// 批量黄金值的三通道复用）。无量纲。
    std::uint32_t maxBatchSize = kBatchDefaultBatchSize;
    /// 并行分片线程数（≥1；§8.4——分片＝目标全序连续区间、合并按全序；
    /// 执行参数不入结果身份。无量纲）。
    std::uint32_t threadCount = 1U;
    /// 求解器替身注入口（非 owning；空＝内置 IkSolver——生产路径）。
    const IIkSolver* solver = nullptr;
    /// 目标批检查点 watermark 通道（非 owning；空＝无检查点通道——
    /// §8.3 批粒度落点；取消/失败后 watermark 保留可续，T06 同款）。
    IBatchCheckpointSink* checkpointSink = nullptr;

    bool operator==(const PoseReachabilityQuery& o) const
    {
        return positions == o.positions && directionSamples == o.directionSamples
            && rollSamples == o.rollSamples && defaultTcp == o.defaultTcp
            && referenceQ == o.referenceQ
            && initialStrategy == o.initialStrategy
            && initialValuesCount == o.initialValuesCount
            && iterationLimit == o.iterationLimit
            && positionTolerance == o.positionTolerance
            && orientationTolerance == o.orientationTolerance
            && dedupThresholdPerAxis == o.dedupThresholdPerAxis
            && configDigest == o.configDigest && seed == o.seed
            && collisionSession == o.collisionSession
            && maxBatchSize == o.maxBatchSize && threadCount == o.threadCount
            && solver == o.solver && checkpointSink == o.checkpointSink;
    }
    bool operator!=(const PoseReachabilityQuery& o) const { return !(*this == o); }
};

// =====================================================================
// PoseReachabilityPlan——计划预览（AT-26"计划预览（IK 目标数）"）
// =====================================================================

/**
 * @brief 位姿可达性计划预览（执行前的目标规模快照——AT-26 验收面的
 *        核心数字＝ikTargetCount）。
 *
 * 语义：IK 目标数＝位置数×方向数×roll 数（KIN-11 原文"位置列表 ×
 * 方向采样 × 工具滚转采样"的乘积展开——每个三元组恰一次完整位姿
 * IK）。全部为整数计数（无量纲）；比率/百分比不在此类型（呈现层投影
 * 职责——Coverage.hpp"无比率字段"同款结构纪律）。值语义纯结构；
 * 线程安全（并发只读）。
 */
struct PoseReachabilityPlan {
    /// 位置列表长度（无量纲；与 query.positions.size() 同源）。
    std::uint64_t positionCount = 0;
    /// 方向采样数（＝query.directionSamples——黄金方向集样本数）。
    std::uint32_t directionCount = 0;
    /// roll 均分样本数（＝query.rollSamples）。
    std::uint32_t rollCount = 0;
    /// IK 目标数＝positionCount×directionCount×rollCount（计划预览的
    /// 核心数字——后台执行将逐目标做一次完整位姿 IK；u64 乘积防 32 位
    /// 溢出——大计划预览不截断）。
    std::uint64_t ikTargetCount = 0;

    bool operator==(const PoseReachabilityPlan& o) const
    {
        return positionCount == o.positionCount && directionCount == o.directionCount
            && rollCount == o.rollCount && ikTargetCount == o.ikTargetCount;
    }
    bool operator!=(const PoseReachabilityPlan& o) const { return !(*this == o); }
};

// =====================================================================
// PoseReachTarget/PoseReachTargetSet——确定性目标集（生成产物）
// =====================================================================

/**
 * @brief 单个位姿可达性目标（位置×方向×roll 的三元组实例——逐目标
 *        恰一次完整位姿 IK；值语义；线程安全并发只读）。
 *
 * 全部物理量单位与坐标系：position 单位 m、基座系 {B}（调用方给定的
 * 位置列表原值）；direction 为单位方向向量（无量纲，‖d‖=1，基座系
 * {B}——§7.2"区域投影系即基座系，方向不做额外坐标变换"同口径）；roll
 * 单位 rad（[−π,π) 值域由 generateRolls 保证）；pose 为完整目标位姿
 * T_base_tcp（基座系 {B}，平移 m／旋转 rad——poseFromDirectionRoll
 * 黄金组装产出）。
 *
 * targetIndex 是**全集全局序键**（0 起连续——结果表/分母对齐的唯一
 * 键）；枚举序黄金锁定（generatePoseReachTargets 注）：位置 i 最慢、
 * roll r 最快——index ＝ (i·D＋j)·R＋r（§7.2 位姿样本生成序同构）。
 */
struct PoseReachTarget {
    /// 全局目标序（0 起连续；分母对齐与结果表对齐的唯一键）。
    std::uint64_t targetIndex = 0;
    /// 位置列表下标（0 起连续——方向覆盖报告的分组键）。
    std::uint32_t positionIndex = 0;
    /// 方向集下标 j（0 起连续——generateDirections 生成序）。
    std::uint32_t directionIndex = 0;
    /// roll 均分下标 r（0 起连续——generateRolls 生成序）。
    std::uint32_t rollIndex = 0;
    /// 目标位置（基座系 {B}，单位 m——位置列表原值，不重排不改写）。
    rw::math::Vector3D<double> position = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 目标方向（单位向量，基座系 {B}——generateDirections 黄金产出）。
    rw::math::Vector3D<double> direction = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    /// 工具滚转角（rad；[−π,π)——generateRolls 黄金产出）。
    double roll = 0.0;
    /// 完整目标位姿 T_base_tcp（基座系 {B}——poseFromDirectionRoll(d, roll,
    /// position) 黄金组装；逐目标 IK 的 targetInBase 直供值）。
    rw::math::Transform3D<double> pose = runtime::detail::identityTransform3D();
};

/**
 * @brief 确定性目标集（§7.2"定义→确定性样本集"的目标通道对应物）。
 *
 * 不变式（generatePoseReachTargets 保证）：targets 按全局 targetIndex
 * 升序连续（0..N-1 无洞——分母完整性核查的结构前提）；plannedTargets
 * ＝positionCount×directionCount×rollCount（与 PoseReachabilityPlan.
 * ikTargetCount 同源同值——预览与生成分母一致防两套真值漂移）。
 *
 * 复评不变式（D-KIN-6 同款）：同 query 生成同目标集同序——无冻结
 * 凭据（文件头边界 1：无对账轨），确定性再生即复评一致性本身。
 * 值语义；线程安全（并发只读）。
 */
struct PoseReachTargetSet {
    /// 全部目标（全局序升序连续）。
    std::vector<PoseReachTarget> targets;
    /// 计划目标总数（分母——与计划预览 ikTargetCount 同值）。
    std::uint64_t plannedTargets = 0;
};

// =====================================================================
// PoseReachTargetResult/PoseReachTargetResultSet——逐目标结果值模型
// =====================================================================

/**
 * @brief 单目标评估结果记录（逐目标状态表——方向覆盖报告的明细层；
 *        "报告＝逐目标状态表＋汇总计数"与 KIN-04"覆盖证据＝逐样本
 *        状态表＋汇总计数"同构）。
 *
 * 不变式（评估器保证）：state∈{NotApplicable,NotRun} 时 reason 必填
 * 非空（ERR-01 显式标记不伪造——NotRun 的收尾原因文本由执行核写入）；
 * outcomeKind 仅在目标实际进入求解时非空（取消未派发轨无结局）。
 * 等值面说明：本结构定义 operator==（全字段逐成员——状态表是覆盖
 * 报告的核心面，逐位对齐断言需要它；成员全为标量/枚举/短文本，无量
 * 等值歧义）。值语义；线程安全。
 */
struct PoseReachTargetResult {
    /// 对齐键（＝PoseReachTarget::targetIndex——分母完整性核查键）。
    std::uint64_t targetIndex = 0;
    /// 评估状态（§7.2 五值词表复用——文件头注；映射口径同 T06：
    /// 结局 1/4→Reached、5→Unreachable、2/3→DataInsufficient、
    /// 取消未派发→NotRun）。
    SampleState state = SampleState::NotRun;
    /// 求解结局（可空——实际求解的目标携带，供素材溯源；取消未派发
    /// 轨为空）。
    std::optional<IkOutcomeKind> outcomeKind;
    /// 碰撞未评价标记（策略未启用碰撞且无会话——检查不在范围；绝不
    /// 解读为无碰撞，KIN-05 口径同源。会话在场而构型评价未完成的目标
    /// 也置此标记——证据缺失族同素材面，状态已降为 DataInsufficient）。
    bool collisionNotEvaluated = false;
    /// 原因文本（NotRun 必填非空——ERR-01；会话在场面碰撞评价缺失的
    /// DataInsufficient 也携带原因，其余状态为空）。
    std::string reason;

    bool operator==(const PoseReachTargetResult& o) const
    {
        return targetIndex == o.targetIndex && state == o.state
            && outcomeKind == o.outcomeKind
            && collisionNotEvaluated == o.collisionNotEvaluated
            && reason == o.reason;
    }
    bool operator!=(const PoseReachTargetResult& o) const { return !(*this == o); }
};

/**
 * @brief 逐目标结果集（与 PoseReachTargetSet 按 targetIndex 双射对齐；
 *        completeness 核查在覆盖计算内执行，违例 logic_error 不静默）。
 *        值语义；线程安全。
 */
struct PoseReachTargetResultSet {
    /// 逐目标结果（覆盖全部目标各恰一条——双射核查键 targetIndex）。
    std::vector<PoseReachTargetResult> results;
};

// =====================================================================
// PosePositionCoverage/PoseReachabilityReport——方向覆盖报告值模型
// =====================================================================

/**
 * @brief 单个位置的方向覆盖汇总（KIN-11"方向覆盖率报告"的分组层——
 *        每个位置一行）。
 *
 * 方向聚合口径（语义决策，登记随卡 §14.6 v0.20）：方向 j 在本位置
 * **可达** ⇔ 该方向下存在 ≥1 个 roll 目标 Reached（存在性口径——与
 * KIN-04 位置覆盖率"存在 ≥1 有效解即达"同款；"工具滚转采样"的语义
 * 落点＝同一方向不同 roll 的候选扩张，存在一个可行滚转即该方向可达）。
 * 五态聚合优先级（无 Reached 时逐级保守——绝不伪造确定性结论）：
 *   1. ∃Reached → reached；
 *   2. else 全部 roll ∈ {Unreachable} → unreachable（全部 roll 都是
 *      解析界限确定性证明才可聚合出方向的确定性不可达——C2③ 口径）；
 *   3. else ∃NotRun → notRun（未完成优先——取消后方向判定不完整，
 *      重跑可消，不以局部素材下结论）；
 *   4. else → dataInsufficient（存在 DataInsufficient——含
 *      Unreachable＋DataInsufficient 混合：无分子凭据且非全证明，
 *      保守归数据不足）。
 *
 * 守恒式（computePoseReachCoverage 保证，违例 logic_error）：
 * plannedDirections ＝ reached＋unreachable＋dataInsufficient＋notRun
 * （notApplicable 方向不产——生成面无此态）。poses 为本位置的 roll
 * 粒度（目标粒度）明细计数（CoverageTotals 复用——planned＝方向数×
 * roll 数）。值语义纯结构；线程安全（并发只读）。
 */
struct PosePositionCoverage {
    /// 位置列表下标（分组键——与 PoseReachTarget::positionIndex 同域）。
    std::uint32_t positionIndex = 0;
    /// 方向分母＝方向采样数 D（禁止按评估结果剔除——R8 同款纪律）。
    std::uint64_t plannedDirections = 0;
    /// 分子＝可达方向数（存在性口径——结构体注聚合规则 1）。
    std::uint64_t reachedDirections = 0;
    /// 确定性不可达方向数（全部 roll 解析界限证明——聚合规则 2）。
    std::uint64_t unreachableDirections = 0;
    /// 数据不足方向数（存在 DataInsufficient roll 且无 Reached——聚合
    /// 规则 4；>0 ⇒ 本位置覆盖结论降级）。
    std::uint64_t dataInsufficientDirections = 0;
    /// 未运行方向数（取消未派发——聚合规则 3；>0 ⇒ 本位置不完整）。
    std::uint64_t notRunDirections = 0;
    /// roll 粒度（目标粒度）明细计数（CoverageTotals 复用——守恒式由
    /// computePoseReachCoverage 内置核查）。
    CoverageTotals poses;
    /// 本位置降级标记（dataInsufficientDirections>0——覆盖率计数仍可
    /// 计算作参考，正式结论被降级拦截；素材面，判定归消费侧）。
    bool downgraded = false;
    /// 本位置不完整标记（notRunDirections>0——partial 不产正式覆盖
    /// 结论，重跑同一 query；§7.2 同口径）。
    bool incomplete = false;

    bool operator==(const PosePositionCoverage& o) const
    {
        return positionIndex == o.positionIndex
            && plannedDirections == o.plannedDirections
            && reachedDirections == o.reachedDirections
            && unreachableDirections == o.unreachableDirections
            && dataInsufficientDirections == o.dataInsufficientDirections
            && notRunDirections == o.notRunDirections && poses == o.poses
            && downgraded == o.downgraded && incomplete == o.incomplete;
    }
    bool operator!=(const PosePositionCoverage& o) const { return !(*this == o); }
};

/**
 * @brief 全局覆盖报告汇总（KIN-11"方向覆盖率报告"的全局层——目标
 *        粒度合计＋两标记）。
 *
 * **无比率字段**（结构保证同 Coverage.hpp 文件头注——"方向覆盖率"
 * 的数值由呈现层以 reachedDirections/plannedDirections 投影，报告值
 * 只载整数计数；零除路径不存在）。值语义纯结构；线程安全（并发只读）。
 */
struct PoseReachabilityReport {
    /// 目标粒度全局合计（planned＝ikTargetCount；五态计数与逐目标状态
    /// 表守恒——computePoseReachCoverage 内置核查）。
    CoverageTotals targets;
    /// 全局降级标记（任一位置 downgraded——DataInsufficient 素材面）。
    bool downgraded = false;
    /// 全局不完整标记（任一目标 NotRun——partial 不产正式覆盖结论）。
    bool incomplete = false;

    bool operator==(const PoseReachabilityReport& o) const
    {
        return targets == o.targets && downgraded == o.downgraded
            && incomplete == o.incomplete;
    }
    bool operator!=(const PoseReachabilityReport& o) const { return !(*this == o); }
};

// =====================================================================
// PoseReachabilityComputation——计算结果全量承载（评估器编排产出→
// 载荷编码输入的中间值；RegionCoverageComputation 同位）
// =====================================================================

/**
 * @brief 位姿可达性计算结果（绑定块＋计划预览＋目标与结果＋覆盖报告
 *        的全量承载）。
 *
 * 不变式（执行核保证）：targets/results 按 targetIndex 双射对齐；
 * perPosition 覆盖位置列表逐位置恰一行（positionIndex 升序连续）；
 * report 由 computePoseReachCoverage 从 (targets, results) 计算——
 * 守恒式在载荷编码前复核（违例 logic_error）。值语义；冻结后只读。
 * 确定性：同输入同值（NFR-COR-01）。
 */
struct PoseReachabilityComputation {
    // ---- 结果绑定（§5.6 六要素——由评估请求填充）----
    core::ContentIdentity snapshotId;   ///< 来源快照内容身份
    core::ContentIdentity sliceId;      ///< 冻结输入切片身份（CON-04）
    core::ContentIdentity configDigest; ///< 求解配置摘要（计算源见 AnalysisConfig.hpp）
    core::EvaluationMode mode = core::EvaluationMode::Quick; ///< 评估模式
                                                            ///  （supportedModes＝{Quick}——文件头边界 3）
    std::uint64_t seed = 0;             ///< 确定性种子（初值面——query.seed 原值）
    std::vector<double> referenceQ;     ///< 排序参考构型（rad|m——D-KIN-4）
    core::TaskIdentity task;            ///< 任务五元组（载荷绑定面）

    // ---- 计划预览与目标集（分母同源——预览/生成/报告三面一致）----
    /// 计划预览快照（planning 数字进载荷供呈现层直读——AT-26）。
    PoseReachabilityPlan plan;
    /// 确定性目标集（分母来源——D-KIN-6 同款复评同集同序）。
    PoseReachTargetSet targets;
    /// 逐目标结果（与 targets 双射对齐）。
    PoseReachTargetResultSet results;

    // ---- 方向覆盖报告（KIN-11 的报告面）----
    /// 逐位置方向覆盖（位置列表序——每位置恰一行）。
    std::vector<PosePositionCoverage> perPosition;
    /// 全局汇总（目标粒度合计＋两标记）。
    PoseReachabilityReport report;
};

// =====================================================================
// planPoseReachability/generatePoseReachTargets——计划预览与确定性
// 目标生成（黄金设施复用面）
// =====================================================================

/**
 * @brief 计算位姿可达性计划预览（AT-26"计划预览（IK 目标数）"的唯一
 *        实现点——独立入口，执行前评估目标规模）。
 *
 * 预览与生成/执行同源（分母一致防两套真值漂移）：ikTargetCount 与
 * generatePoseReachTargets 的 plannedTargets、报告 targets.planned
 * 三面同值。纯计数计算，不做任何求解。
 *
 * @param query [in] 分析查询（装配合法性同执行面——违例 fail-fast，
 *                   不产非法预览）
 * @return 计划预览（整数计数——结构体注）
 *
 * @throws std::invalid_argument 装配契约违约（validatePoseReachability-
 *         Query 同款校验面——空位置列表/零方向或 roll 计数/非有限
 *         位置/求解配置非法/SeededRandom＋seed=0 等，NFR-COR-03）
 *
 * 纯函数；线程安全；确定性。
 */
PoseReachabilityPlan planPoseReachability(const PoseReachabilityQuery& query);

/**
 * @brief 生成确定性目标集（"查询→目标"全链的唯一实现点——黄金设施
 *        复用：方向集＝generateDirections（斐波那契螺旋，D-KIN-6 黄金
 *        锁定）、roll 序列＝generateRolls（体心均分，黄金锁定）、目标
 *        位姿＝poseFromDirectionRoll（黄金组装）——本函数零自有采样
 *        数学，任何黄金漂移即时可见）。
 *
 * 生成序（黄金锁定，登记随卡 §14.6 v0.20）：位置 i 最慢×方向 j 居中
 * ×roll r 最快——targetIndex ＝ (i·D＋j)·R＋r（0 起连续无洞；§7.2
 * 位姿样本生成序同构——"位姿样本＝位置序×方向 j 序×roll r 序"）。
 *
 * @param query [in] 分析查询（装配合法性——违例 fail-fast 同预览面）
 * @return 确定性目标集（plannedTargets 与计划预览同值——分母同源）
 *
 * @throws std::invalid_argument 装配契约违约（同 planPoseReachability）
 * @throws std::logic_error 内部不变量破坏（目标数≠分母计数——实现
 *         缺陷，不静默）
 *
 * 纯函数；线程安全；确定性（同 query 同集同序——D-KIN-6 同款）。
 */
PoseReachTargetSet generatePoseReachTargets(const PoseReachabilityQuery& query);

// =====================================================================
// computePoseReachCoverage——方向覆盖计算（唯一实现点）
// =====================================================================

/**
 * @brief 覆盖计算产出值（逐位置表＋全局汇总的成对返回——两值同算
 *        同校验，防调用方拆散后漏用一侧）。
 */
struct PoseReachCoverageOutcome {
    /// 逐位置方向覆盖（位置列表序——每位置恰一行）。
    std::vector<PosePositionCoverage> perPosition;
    /// 全局汇总（目标粒度合计＋两标记）。
    PoseReachabilityReport report;
};

/**
 * @brief 方向覆盖率计算（KIN-11"方向覆盖率报告"的唯一实现点——
 *        NFR-MNT-04"比较/统计唯一实现点"纪律的位姿可达通道落点；
 *        T06 computeCoverage 同款自由函数面）。
 *
 * 计算口径（结构体/文件头注的聚合规则逐条落地）：
 *   - 分母＝目标集计划计数（targets.plannedTargets／逐位置 planned-
 *     Directions＝D——禁止按评估结果剔除，R8 同款）；
 *   - 双射核查：results 的 targetIndex 集合必须与 set.targets 完全
 *     一致（缺项/重复/越界即 std::logic_error）；
 *   - 方向五态聚合按 PosePositionCoverage 注的优先级逐方向判定；
 *   - 守恒式：逐位置 plannedDirections＝五态方向和、poses.planned＝
 *     D×R＝五态目标（该位置）和；全局 targets 五态计数＝逐位置之和
 *     ——任一破坏即 std::logic_error（实现/组装缺陷不静默）；
 *   - downgraded/incomplete 标记的传播规则见 PosePositionCoverage/
 *     PoseReachabilityReport 字段注。
 *
 * @param set     [in] 确定性目标集（分母来源）
 * @param results [in] 逐目标结果集（双射对齐——违例 std::logic_error）
 * @return 覆盖计算产出（整数计数＋标记；同输入同值——确定性）
 *
 * @throws std::logic_error 双射核查/守恒式失败（调用方组装缺陷——
 *         不静默）
 *
 * 纯函数；线程安全；确定性。
 */
PoseReachCoverageOutcome computePoseReachCoverage(const PoseReachTargetSet& set,
                                                  const PoseReachTargetResultSet& results);

// =====================================================================
// runPoseReachabilityComputation——位姿可达计算核心（纯函数服务——
// NFR-MNT-01"计算库可被模型测试直调"；评估器形态与计算面的分离线，
// runRegionCoverageComputation 同构）
// =====================================================================

/**
 * @brief 执行一次位姿可达性计算（计划预览→确定性目标生成→逐目标
 *        评估（分批/协作取消/检查点/进度/并行分片）→方向覆盖计算）。
 *
 * 与评估器形态的分工：本函数是**纯计算面**——只产
 * PoseReachabilityComputation（目标/结果/覆盖），不产证据与诊断素材
 * （T06 分工同款——证据行本通道就不产，见文件头边界 2；诊断组装在
 * 评估器 evaluate 面）。独立可见性使目标生成/逐目标评估/取消语义可
 * 被单元与契约测试直调断言（可控求解器替身注入口＝
 * PoseReachabilityQuery::solver）。
 *
 * 逐目标评估语义（T06 逐样本评估的同构面，映射口径逐条同源）：
 *   - 每目标恰一次完整位姿 IK（IkRequest.targetInBase＝target.pose、
 *     双容差＝query 两容差、初值集共享（query.seed——SeededRandom
 *     序列源）、cancellationProbe＝批间＋批内两级取消探针）；
 *   - 结局→状态映射：1/4→Reached、5→Unreachable、2/3→DataInsufficient
 *     （C5/C8——绝不输出不可行）；取消→NotRun 如实保留（partial 不产
 *     正式覆盖结论）；设施失败→DataInsufficient（F-746③ 值面——
 *     该目标证据缺失，非任务级结论，批内剩余目标照常派发）；
 *   - 碰撞：会话在场→解级硬过滤③照常（求解器内部）；会话不在场→
 *     collisionNotEvaluated 标记（KIN-05 口径——绝不解读为无碰撞）；
 *     会话在场而构型评价未完成→该目标 DataInsufficient＋标记（T06
 *     设施臂同款）。
 *
 * @param view    [in] 宿主注入的只读模型视图（非 owning；调用期间存活）
 * @param query   [in] 位姿可达性分析查询（先经装配校验同款 fail-fast 面；
 *                     solver/checkpointSink 注入口生效）
 * @param request [in] 评估请求（snapshotId/sliceId/mode/task——绑定面）
 * @param context [in] 宿主调用上下文（协作取消批间查询＋进度上报——
 *                     EX-T03/EX-T05 语义载体，与 T05/T06 同一管线）
 * @return 计算结果（targets/results 双射、覆盖守恒式由计算核保证）
 *
 * @throws std::invalid_argument 装配契约违约（同构造期校验面）
 * @throws std::logic_error 内部不变量破坏（双射/守恒——实现缺陷不静默）
 *
 * 纯计算（不派发任务/不写项目/不产生修订）；线程安全（无跨调用状态，
 * 并行分片按 query.threadCount 执行）；确定性（同输入同线程数逐位一致
 * ——§8.4/D-KIN-6 同款）。
 */
PoseReachabilityComputation runPoseReachabilityComputation(
    const IKinRuntimeView& view, const PoseReachabilityQuery& query,
    const evidence::EvaluationRequest& request, evidence::IEvaluationContext& context);

// =====================================================================
// 载荷编码（分析报告素材的 canonical 字节面——Coverage.hpp 同位声明）
// =====================================================================

/**
 * @brief 将位姿可达性计算结果编码为域 canonical 字节（定宽小端＋字段
 *        定序；声明在本头、实现落位 src/PoseReach.cpp——通道单一翻译
 *        单元，T06 载荷编码随值面 TU 的布局同款）。
 *
 * 编码布局（codec 版本 1；magic "IRDPR01" 7 字节 ASCII＋版本 u32）：
 * 标记块（incomplete u8＋downgraded u8）＋绑定块（snapshotId/sliceId/
 * configDigest 各 32B＋mode u8＋seed u64＋referenceQ u32＋f64×n＋任务
 * 五元组 4×16B＋attempt u64＋evaluationKey u32＋len＋contractVersion
 * u32——§5.6 绑定六要素）＋计划块（positionCount u64＋directionCount
 * u32＋rollCount u32＋ikTargetCount u64）＋全局汇总块（目标六计数
 * u64×6）＋逐位置块（u32 计数后每条：positionIndex u32＋方向五计数
 * u64×5＋roll 粒度六计数 u64×6＋downgraded u8＋incomplete u8）＋逐
 * 目标状态表（u32 计数后每条：targetIndex u64＋positionIndex u32＋
 * directionIndex u32＋rollIndex u32＋state u8＋outcomeKind present
 * u8[+u8]＋collisionNotEvaluated u8＋reason u32＋len）。目标几何
 * （位置/方向/roll/位姿坐标）不入载荷——目标由 query 确定性再生
 * （D-KIN-6 同款；大目标集不枚举几何，"身份对计划参数计算"同源）。
 *
 * @param computation [in] 位姿可达性计算结果（冻结值——全部来源）
 * @param task        [in] 任务五元组（绑定面——computation.task 同值，
 *                     显式入参与 T04/T05/T06 编码器同构）
 * @return canonical 字节（确定性：同输入同字节——NFR-COR-01）
 *
 * @throws std::logic_error 对齐核查失败（结果 targetIndex 无对应目标
 *         ——双射破坏，不静默）
 *
 * 纯函数；线程安全；不抛（logic_error 除外——对齐核查面）。
 */
std::vector<std::uint8_t> encodePoseReachabilityPayloadCanonical(
    const PoseReachabilityComputation& computation, const core::TaskIdentity& task);

// =====================================================================
// 评估器形态（§9.3——注册归 L5 装配，本单元不自我注册）
// =====================================================================

/**
 * @brief 组装 kin-pose-reachability 的评估器描述符（§4.3 依赖声明表的
 *        R2 增补行值面——依赖条目取自 KIN-11 分析的真实消费面）。
 *
 * 与 makeRegionCoverageDescriptor 同构（字段落值见其注），差异三处：
 *   - key＝kPoseReachabilityEvaluationKey（本头唯一书写点）；
 *   - inputs＝五条依赖：model.robot-design/tcp/config.ik 三条 Required
 *     ＋collision-models(Object) 一条 **Conditional**（条件依赖语义：
 *     碰撞启用状态只读自 policy——策略未启用碰撞时该键不进切片，
 *     V13-01；T06 同款 resolutionNote）＋policy.resolved(Policy) 一条
 *     Required（碰撞启用状态与策略阈值的只读来源——三通道一致）；
 *     **无 req.regions/req.sampling-plans/req.conditions**（文件头边界
 *     1：位置列表直给、无区域/计划/工况对象——R2 独立分析的依赖面
 *     本来就更窄，非裁剪）；
 *   - supportedModes＝{Quick}（文件头边界 3——§7.3"Quick/估算性质"
 *     的模式面表达；Verified 不支持＝探索式结果不进正式证据链）；
 *     stateless=true＋threadSafety=FullyThreadSafe（逐目标并行分片
 *     要求可重入——§9.2 头注同款）。
 *
 * @return 描述符值（每次调用返回新值；注册期校验归 EvaluatorRegistry）
 *
 * 纯函数；线程安全；确定性（同输入同值——NFR-COR-02）。
 */
evidence::EvaluatorDescriptor makePoseReachabilityDescriptor();

/**
 * @brief kin-pose-reachability 评估器：独立位姿可达性分析（KIN-11——
 *        计划预览→确定性目标→逐目标评估→方向覆盖报告→分析素材
 *        组装；RegionCoverage 评估器的同构形态）。
 *
 * 评估主流程（runPoseReachabilityComputation 分工注＋文件头边界）：
 *   1. 批量级零素材轨——TCP 未配置/悬空→KIN-NO-TCP＋零 payload（T03/
 *      T05/T06 两分口径同源；键不命中已在装配期拒绝）；
 *   2. 纯计算核——runPoseReachabilityComputation（取消/检查点/进度
 *      在计算核内）；
 *   3. 素材组装——payload canonical（计划块＋全局汇总＋逐位置块＋
 *      状态表；**零证据行**——文件头边界 2，R2 边界的结构表达）＋
 *      诊断（不完整→KIN-RESULT-INCOMPLETE；会话在场而碰撞评价缺失
 *      计数>0→KIN-COLLISION-UNAVAILABLE）。
 *
 * 错误分轨（域约定，登记随卡 §14.6 v0.20——evidence §9.3"域自选"）：
 *   - **装配期 fail-fast**（构造函数，std::invalid_argument）：视图空
 *     指针、位置列表空/非有限、方向/roll 计数 0、求解配置违例、
 *     SeededRandom＋seed=0（I-KIN-4）、maxBatchSize/threadCount=0、
 *     tcpKey 不命中（工具可解析时）——调用方错误轨，不进入评估输出面；
 *   - **评估期结构化诊断**（零素材）：TCP 未配置/悬空→KIN-NO-TCP
 *     （批量级零素材）；不完整→KIN-RESULT-INCOMPLETE（NotRun 清单
 *     摘要）；碰撞评价缺失→KIN-COLLISION-UNAVAILABLE；
 *   - **取消**：协作式（批间查询＋批内求解探针）——未评估目标如实
 *     NotRun、report.incomplete=true（不产正式覆盖结论——§7.2 同口径）。
 *
 * 确定性（§8.4）：同输入同线程数逐位一致；异线程数→等价集合＋稳定序
 * 一致（结果按全序槽位写回——本实现逐位一致）。线程安全：实例无跨
 * 调用可变状态（descriptor.stateless=true——可共享）。
 */
class PoseReachabilityEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /**
     * @brief 构造绑定 (视图, 查询) 的评估器实例（工厂经闭包调用）。
     *
     * @param view  [in] 宿主注入的只读模型视图（非 owning；调用方保证
     *                   evaluate() 期间存活；空指针→std::invalid_argument）
     * @param query [in] 位姿可达性分析查询（值持有；装配期非法→
     *                   std::invalid_argument——类注错误分轨）
     *
     * @throws std::invalid_argument 装配期查询非法
     */
    PoseReachabilityEvaluator(const IKinRuntimeView* view, PoseReachabilityQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次位姿可达性分析（§9.3 调用约定；纯计算——不派发
     *        任务、不写项目、不产生修订；检查点/归档经宿主通道）。
     *
     * @param request [in] 评估请求（snapshotId/sliceId/mode/task 五元组
     *                     经其填充绑定；本通道无快照冻结凭据对账——
     *                     文件头边界 1）
     * @param context [in] 宿主调用上下文（协作取消：批间查询其
     *                     cancellationRequested()；批内逐目标求解经
     *                     IkRequest.cancellationProbe 周期查询；进度
     *                     reportProgress 目标批粒度上报）
     *
     * @return 评估产出（payload＋诊断——类注主流程；零证据行；TCP
     *         缺失时零素材）
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning——见类注）
    PoseReachabilityQuery m_query;              ///< 评估查询（值持有）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    std::optional<KinematicsError> m_setupError; ///< 评估期结构化素材（TCP 缺失——
                                                 ///  批量级零素材轨，T05 两分口径同源）
};

/**
 * @brief kin-pose-reachability 的 IEvaluatorFactory 实现：宿主构建实例
 *        时把 (视图, 查询) 捕获进闭包，create() 保持**无参签名**（O-37
 *        裁决形态——注册表路径兼容；WorkspaceSamplerFactory 同构）。
 *        生命周期与线程约束同 WorkspaceSamplerFactory（类注）；查询内
 *        投影值随宿主组装（每请求新建工厂）。
 */
class PoseReachabilityEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    /**
     * @brief 捕获 (视图, 查询) 的工厂构造（注入点——O-37）。
     *
     * @param view  [in] 宿主注入视图（非 owning；宿主保证 evaluate()
     *                   期间存活）
     * @param query [in] 分析查询（值持有进闭包）
     */
    PoseReachabilityEvaluatorFactory(const IKinRuntimeView* view,
                                     PoseReachabilityQuery query);

    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

    const evidence::EvaluatorDescriptor& descriptor() const override;

private:
    const IKinRuntimeView* m_view;      ///< 注入视图（非 owning——宿主持有）
    PoseReachabilityQuery m_query;      ///< 闭包捕获的查询（值持有）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_POSEREACH_HPP
