/**
 * @file   DynTypes.hpp
 * @brief  dynamics 数据模型（units/dynamics.md §4.4 设计基线的本任务落地
 *         面）——关节类型/样本数值状态/单样本动力学记录/可信性摘要。
 *
 * 设计依据：
 *   - units/dynamics.md §4.4（8 个必需类型＋1 个交接 DTO——签名＝设计基线，
 *     "实现任务可按 DTB §5.4 微调并登记"）、§4.5（关节型广义力和单位表——
 *     全卡唯一量纲权威）、§4.6（序列纪律）、§5.5（摩擦降级三层）、§7.5
 *     （功率/能量符号约定）
 *   - 需求 DYN-01/02（逆动力学与输入模型）、DYN-03（类型化广义力——转动
 *     N·m/移动 N 不可混用）、DYN-06（数据不足不包装精确）、MDL-16（摩擦
 *     参数建模层）、NFR-COR-02（稳定排序）/NFR-COR-03（非有限数拒绝）
 *   - 任务契约 tasks/foundation/WP-17-T03.json（RNEA 逆动力学评估器——
 *     本头即其样本行承载面）
 *
 * ★ 落地面口径（诚实登记，防扩大）：本头当前仅落地 WP-17-T03 消费的四个
 *   类型（DynJointType/SampleNumericState/DynamicsSample/DynamicsValidity）。
 *   §4.4 清单的其余类型（DynamicsSeries/PeakRecord/DynamicsEnvelope/
 *   PowerEnergySummary/OperatingConditionResult/DynamicsEvidence/
 *   JointSideSeriesPack）分别由其消费任务落位（T04 序列冻结与统计、T07
 *   包络合并、Handoff 交接面）——本任务不预建占位类型（NFR-MNT-04）；
 *   后续任务在同头文件表尾增列即可，既有类型不重排。
 *
 * 背景说明（值语义与线程约束）：全部类型为纯值（深拷贝安全、构造后按
 *   语义只读——DynamicsSample 构造后不修改）；并发只读安全、构建期单线程
 *   （每工况一评估调用——卡 §10.0 线程安全行）。坐标系口径：广义力为
 *   关节空间标量（沿关节轴 z 的分量），无坐标系分量；重力投影经基座系
 *   （§4.1，runtime 编译产物 gravityBase 的唯一消费——本域零二次旋转，
 *   D-DYN-3）。
 */

#ifndef IRD_DYNAMICS_DYNTYPES_HPP
#define IRD_DYNAMICS_DYNTYPES_HPP

#include <cstdint>

#include <sdurws/ird/core/Identity.hpp>  // core::ObjectId（对象级稳定身份——ARC-04）

namespace sdurws::ird::dynamics {

// =====================================================================
// 关节类型与样本数值状态（§4.4 前两个类型；封闭词表——枚举值一经交付
// 不得改动/插入，语义见逐值注释）。
// =====================================================================

/**
 * @brief 关节类型（随 CanonicalJoint.type 映射；决定广义力量纲——类型化
 *        DYN-03）。
 *
 * 量纲映射（§4.5 唯一权威）：Revolute/Continuous → 广义力单位 N·m（力矩）；
 * Prismatic → N（力）。Continuous（工程工作范围内）按转动处理（卡 §5.3
 * 输入覆盖表行 8）。runtime JointType::Fixed（刚性连接、无自由度）不在本
 * 词表——R1 评估面仅支持三个可动类型，模型链携带 Fixed 关节属输入非法
 * （评估入口 fail-fast——§10.0 调用方错误轨；建模侧本应不产生此类链）。
 */
enum class DynJointType {
    Revolute,   ///< 旋转关节——广义力单位 N·m
    Prismatic,  ///< 移动关节——广义力单位 N
    Continuous, ///< 连续旋转（无限位）——按转动处理，单位 N·m（§5.3）
};

/**
 * @brief 单样本数值状态（样本级；封闭词表——不是全局任务状态、不是证据
 *        等级，§4.4 类型注释原文）。
 *
 * 语义与处置（§4.6 非有限数行＋§5.6 数值稳定性表，逐值对应）：
 *   - Ok：数值完备；
 *   - NonFiniteInput：输入（q/q̇/q̈/物性）非有限——该样本不静默转 0
 *     （NFR-COR-03），样本行保留、力矩字段携带非有限值并以本状态标记，
 *     单样本级素材、评估继续（不阻断序列）；
 *   - NonFiniteOutput / Overflow：RNEA 计算输出非有限/溢出——该样本起
 *     本工况失败记录（DYN-RNEA-FAILED 素材附 t/段/关节定位），后续样本
 *     不产出（序列保留至故障点，不截断伪造）；单工况失败不自动推出其他
 *     工况失败（§5.6）。
 */
enum class SampleNumericState {
    Ok,              ///< 数值完备
    NonFiniteInput,  ///< 输入非有限（样本级标记，评估继续）
    NonFiniteOutput, ///< 输出非有限（工况级失败起点——DYN-RNEA-FAILED）
    Overflow,        ///< 数值溢出（同上——工况级失败起点）
};

// =====================================================================
// 单样本动力学记录（§4.4 DynamicsSample——RNEA 逐样本输出；值语义、
// 构造后不修改）。
// =====================================================================

/**
 * @brief 单样本动力学记录（§4.4 原文字段序——一行＝某工况某时刻某关节）。
 *
 * 序列组装纪律（§4.6）：samples 按 (conditionId, t 升序, jointIndex 升序)
 * 稳定排序；本结构由评估器按 (t, jointIndex) 逐行产出（conditionId 为
 * 调用侧分组键，不在行内重复排序）。
 *
 * 五分项恒等式（§5.2）：tauTotal == tauGravity + tauInertia +
 * tauCoriolisCentrifugal + tauFriction + tauExternal（逐样本成立——黄金
 * 算例 V-03 校验分项可加性；tauTotal 取 RNEA 全量通道值而非五项求和，
 * 恒等式是被验证的性质而非构造恒真式）。
 */
struct DynamicsSample {
    double t;                        ///< 样本时间，单位 s（上游轨迹时间轴；严格递增）
    std::uint32_t segmentIndex;      ///< 所在轨迹段序号（0 基；来自上游轨迹段结构）
    core::ObjectId conditionId;      ///< 工况对象 ID（EVI-02 覆盖矩阵关联键）
    std::uint32_t jointIndex;        ///< 关节序号（0 基，链序）
    core::ObjectId jointObjectId;    ///< 关节稳定对象 ID（ARC-04）
    DynJointType jointType;          ///< 关节类型——决定下列力字段的量纲（转动 N·m/移动 N）
    double q;                        ///< 关节位置：rad（转动/连续）或 m（移动）——权威角，SI 真值
    double qd;                       ///< 关节速度：rad/s 或 m/s
    double qdd;                      ///< 关节加速度：rad/s² 或 m/s²
    double tauGravity;               ///< 重力项广义力（N·m 或 N，按 jointType；静态重力矩通道）
    double tauInertia;               ///< 惯性项（角加速度项——M(q)·q̈，含全耦合；零重力零速通道）
    double tauCoriolisCentrifugal;   ///< 科氏/离心项（q̇ 二次项，含交叉耦合——逐样本精确，不对角化）
    double tauFriction;              ///< 摩擦项（黏性＋库仑＋偏置；§5.5 符号约定 sgn₀(0)=0）
    double tauExternal;              ///< 外力项（R1 恒 0 且 ExternalWrench NotApplicable——P-DYN-3，不伪造零以外值）
    double tauTotal;                 ///< 总广义力（=五分项之和；恒等式由黄金算例校验）
    double mechanicalPower;          ///< 机械功率 P=tauTotal·q̇，单位 W（§7.5 符号：正=驱动输出功）
    double energyIntegralJ;          ///< 能量积分状态 E(t)=∫₀ᵗ P dτ，单位 J（净能量；梯形时间加权——§7.5）
    std::uint32_t payloadVariantIndex; ///< 负载模型变体索引（§5.4 事件时间线；0=基线工具）
    core::ObjectId toolObjectId;     ///< 工具对象 ID（变体所属工具/负载引用；无工具模型＝空 id）
    SampleNumericState numericState; ///< 数值状态（非 Ok 时诊断必附——NFR-COR-03）
};

// =====================================================================
// 可信性/完整性摘要（§4.4 DynamicsValidity——值对象；不是新的全局任务
// 状态、证据等级或工程判定——只作结果侧事实记录）。
// =====================================================================

/**
 * @brief 可信性/完整性摘要（§4.4 原文契约）。
 *
 * 约束（§4.4 注释原文）：本类型只承载"结果完整性＋数值质量＋来源信息"；
 * Feasible/EngineeringInfeasible/DataInsufficient 判定一律归 evidence
 * aggregateVerdict（§4.6）——dynamics 不越权定级（PA-1）。
 *
 * 来源三层的呈现纪律（§5.5）：缺失（frictionMissing＋缺失清单）/估算
 * （estimatedXxxCount＋DYN-PROPERTY-DOWNGRADED 素材）/外部验证未完成
 * （externalValidationPending）互斥呈现、不混用；任何一层存在时下游证据
 * 不得包装为精确结论（DYN-06）。
 */
struct DynamicsValidity {
    /**
     * @brief 序列完整性三态（§4.4 枚举）。
     * Complete：actual==planned 且无非有限故障；Partial：存在缺口/单样本
     * 标记/估算降级（统计可继续——逐段标注）；Empty：无样本（不产出统计，
     * 绝不做 0 值伪装——NFR-COR-03）。
     */
    enum class Completeness { Complete, Partial, Empty };

    Completeness completeness = Completeness::Empty; ///< 序列完整性（初值 Empty——未评估即空）
    std::size_t plannedSampleCount = 0;  ///< 计划样本数（轨迹采样×抽取；样本时刻数）
    std::size_t actualSampleCount = 0;   ///< 实际样本数（缺样本/间隙不计入 actual）
    std::size_t nonFiniteCount = 0;      ///< 非有限值样本数（输入非有限标记的样本时刻数）
    std::size_t estimatedLinkCount = 0;  ///< 估算物性连杆数（物性 SourcedValue 来源＝GeometricEstimate）
    std::size_t estimatedPayloadCount = 0; ///< 估算物性负载数（com/inertia 缺失按 §5.4 保守估算）
    bool frictionMissing = false;        ///< 存在摩擦参数缺失（MDL-16/DYN-06：DataInsufficient 降级素材）
    bool externalValidationPending = false; ///< 外部验证未完成（CON-03 Recorded 态物性——
                                         ///<  T03 输入面无该事实通道，恒 false；见文件尾登记注）
    /**
     * @brief 正动力学一致性检查状态（§6 建议证据项的载体位）。
     * 本任务（T03）恒 NotRun——检查器随 WP-17-T05 落位（IForwardDynamics
     * Validator）；字段先行以便样本/序列承载面一次冻结（T04/T05 不改行）。
     */
    enum class ForwardCheckState { NotRun, Passed, Failed, NotApplicable };
    ForwardCheckState forwardCheck = ForwardCheckState::NotRun;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_DYNTYPES_HPP
