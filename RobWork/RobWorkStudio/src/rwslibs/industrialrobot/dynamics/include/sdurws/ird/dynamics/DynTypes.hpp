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
 *     本头即其样本行承载面）、tasks/foundation/WP-17-T04.json（输出序列
 *     与峰值/RMS 包络——本头 T04 增列面）
 *
 * ★ 落地面口径（诚实登记，防扩大）：本头分批落地各消费任务所需的 §4.4
 *   类型——WP-17-T03 落地 DynJointType/SampleNumericState/DynamicsSample/
 *   DynamicsValidity 四类型；WP-17-T04 表尾增列 PeakRecord/DynamicsSeries/
 *   PowerEnergySummary/OperatingConditionResult 四类型（序列冻结与统计的
 *   直接承载——SeriesBuilder/Envelope/PowerEnergy 三公共头的消费面）。
 *   §4.4 清单的其余类型：DynamicsEnvelope（连同跨工况 mergeEnvelope）随
 *   WP-17-T07 包络合并落位（DTB §2.18 T07 行——多工况包络合并归 T07）；
 *   DynamicsEvidence 随 T06/T10 证据装配落位；JointSideSeriesPack 随
 *   drivetrain 交接任务落位——均不在本任务预建占位（NFR-MNT-04）；
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
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp> // core::DiagnosticRecord（工况级诊断素材——ERR-01）
#include <sdurws/ird/core/Digest.hpp>   // core::ContentIdentity（序列内容身份——CON-05）
#include <sdurws/ird/core/Identity.hpp> // core::ObjectId（对象级稳定身份——ARC-04）
                                        //   ＋core::TaskIdentity（运行身份五元组）

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

// =====================================================================
// 峰值记录（§4.4 PeakRecord——DYN-03：峰值必须报告持续时间窗和所在轨迹
// 段；WP-17-T04 增列）。
// =====================================================================

/**
 * @brief 单量峰值记录（§4.4 原文契约——DYN-03 硬性口径的三要素：值＋
 *        发生时间＋所在轨迹段，外加持续时间窗与来源工况）。
 *
 * 持续时间窗定义（§7.2/D-DYN-8，全卡固定）：|value| 达到峰值的**连续
 * 样本集合**的最小覆盖时间区间 [windowStartS, windowEndS]——样本级精确
 * 定义，不引入任何比例阈值（窗的数值等值容差仅在黄金算例逐例声明——
 * 测试对照口径，附录 D C7）；平顶峰值窗口自然覆盖整段平顶；孤立单样本
 * 峰值窗宽为零（起点＝终点＝tPeakS）。
 *
 * value 的符号语义（按统计量分列，逐量注释——消费方必须按 token 读）：
 *   - 力矩正向 τ_max⁺＝循环内 max(τ)（带符号实际值——若全循环无正力矩
 *     则为最接近 0 的负值，信息不丢失）；
 *   - 力矩反向 τ_max⁻＝循环内 max(−τ)＝|min(τ)| 的幅值形态（§7.2"反向
 *     分列"——与 max 记号一致，包络跨工况合并对两者都取 max）；
 *   - 速度/加速度峰值＝循环内 max|q̇|／max|q̈|（幅值，恒非负——§7.2 不
 *     分列方向）；
 *   - 功率正向 max(P)、功率反向 max(−P)（§7.2 分列；合并幅值
 *     max(|P|) 仅用于 PowerEnergySummary.powerPeak 单记录位）。
 * 值语义纯结构；线程安全。
 */
struct PeakRecord {
    double value;                ///< 峰值（SI：N·m/N/rad·s⁻¹·m·s⁻¹/rad·s⁻²·m·s⁻²/W 等，
                                 ///<   按量纲行说明与上方符号语义——符号语义逐统计量固定）
    double tPeakS;               ///< 峰值发生时间，单位 s（首个达到峰值的样本时刻——
                                 ///<   同值多样本时取时间轴首个，确定性口径）
    std::uint32_t segmentIndex;  ///< 峰值所在轨迹段（0 基——上游段结构直通）
    double windowStartS;         ///< 持续时间窗起点，单位 s（峰值样本连续等值 run 的首样本 t）
    double windowEndS;           ///< 持续时间窗终点，单位 s（同 run 末样本 t）
    core::ObjectId conditionId;  ///< 来源工况（包络跨工况合并时必填——单工况统计
                                 ///<   内＝该序列自身工况，透传防丢失）
};

// =====================================================================
// 单工况动力学序列（§4.4 DynamicsSeries——逐工况产出；身份块完整；
// WP-17-T04 增列。构建唯一入口＝SeriesBuilder（SeriesBuilder.hpp）——
// 排序/完整性/身份冻结/内容身份计算都在其 finalize 执行）。
// =====================================================================

/**
 * @brief 单工况动力学序列（§4.4 原文字段序——身份块构造期一次冻结，
 *        内容块由构建器组装）。
 *
 * 排序纪律（§4.6/NFR-COR-02）：samples 按 (conditionId, t 升序,
 * jointIndex 升序) 稳定排序——单工况序列内 conditionId 恒同，行序由
 * (t, jointIndex) 决定；同刻度逐关节行序固定。
 *
 * contentIdentity（CON-05）：canonical 内容身份（SHA-256）——身份块＋
 * 有效性块＋全部样本行按固定字段序、固定小端字节编码进摘要（域分隔
 * magic 起头，防跨域摘要混同）；同输入字节必得同摘要（NFR-COR-02 确
 * 定性），任何一行任一字段变化都改变摘要（内容寻址失效判据）。
 *
 * 线程约束：并发只读安全；构造后不修改（值语义、深拷贝安全——§4.4
 * "构造后不可变"）。
 */
struct DynamicsSeries {
    // —— 身份块（构造期一次冻结；全部入 series 内容身份编码）——
    core::ContentIdentity snapshotId;          ///< 绑定快照（评估输入的编译模型来源）
    core::ContentIdentity sliceId;             ///< 绑定切片（当前性判据——evidence）
    core::ContentIdentity trajectoryPayloadId; ///< 上游轨迹 payload 内容身份（Trajectory
                                               ///<   内容身份——不等于轨迹 sliceId，轨迹
                                               ///<   sliceId 另存于上游引用供失效比对）
    core::ObjectId conditionId;                ///< 工况对象 ID（样本行 conditionId 的母值）
    core::ObjectId toolObjectId;               ///< 工具对象 ID（无工具模型＝空 id）
    std::uint32_t evaluatorContractVersion;    ///< 本域契约版本（kInverseDynContractVersion）
    std::string   algorithmVersion;            ///< RNEA/统计实现版本 token（如 kRneaAlgorithmVersion）
    std::string   dynConfigDigest;             ///< config.dyn 摘要（分析配置身份——进结果身份）
    core::TaskIdentity task;                   ///< 运行身份五元组（TASK-03——结果归档关联键）
    // —— 内容块 ——
    std::vector<DynamicsSample> samples;       ///< 逐样本逐关节行（§4.6 排序纪律——见类注释）
    DynamicsValidity validity;                 ///< 完整性/数值质量/来源摘要（§4.4）
    std::vector<core::ObjectId> diagRefs;      ///< 诊断引用（对象级；条目本体随
                                               ///<   EvaluationOutput.diagnostics——builder
                                               ///<   防御性二次校验的标记也落此处）
    core::ContentIdentity contentIdentity;     ///< series canonical 内容身份（SHA-256——
                                               ///<   编码规则见类注释）
};

// =====================================================================
// 功率与能量摘要（§4.4 PowerEnergySummary——建议证据项
// dyn.power-energy-split 的数据面；WP-17-T04 增列）。
// =====================================================================

/**
 * @brief 功率与能量摘要（§4.4 原文契约——计算唯一入口＝
 *        PowerEnergyCalculator（PowerEnergy.hpp），§7.5 符号约定）。
 *
 * 能量量纲口径（§4.5 表能量行/P-DYN-9）：core UnitToken R1 表无 Energy
 * 量纲——能量字段以 SI 真值 double＋本注释承载（单位 J），**不得**引入
 * 产品侧相对校验容差（runtimeAbsoluteTolerance 无功率/能量/惯量/质量
 * 默认——统计校验只能测试对照，黄金算例逐例声明）；强类型化若需补齐
 * 走 core 卡增量修订。
 *
 * 无效语义（§4.6"无时间参数"行）：timeParamAvailable=false 时
 * cycleDurationS/positiveEnergyJ/negativeEnergyJ/netEnergyJ/meanPowerW
 * 全部显式无效（NaN 位模式）并列入缺失清单——**不伪造 0、不伪造积分**
 * （NFR-COR-03）；调用方据 validity/样本数判空。
 * 值语义纯结构；线程安全。
 */
struct PowerEnergySummary {
    /**
     * @brief 逐关节功率/能量分项行（§4.4 原文字段；量纲按关节类型——
     *        转动/连续关节 W·s=J 与 N·m 力矩配套、移动关节 W 与 N 配套，
     *        能量单位恒 J——§4.5 表，类型化不混算：逐关节独立积分）。
     */
    struct JointPowerEnergy {
        std::uint32_t jointIndex;    ///< 关节序号（0 基，链序）
        double positiveEnergyJ;      ///< E⁺=∫max(P,0)dt，单位 J（驱动/提升/加速——正功）
        double negativeEnergyJ;      ///< E⁻=∫min(P,0)dt，单位 J（≤0；制动/下降/发电——负功）
        double netEnergyJ;           ///< E_net=E⁺+E⁻，单位 J
        double meanPowerW;           ///< 平均功率 E_net/T_cycle，单位 W
        PeakRecord powerPeak;        ///< 功率峰值（含窗与段——幅值形态 max(|P|)，
                                     ///<   见 PeakRecord 符号语义；单记录位取合并幅值）
    };
    std::vector<JointPowerEnergy> joints; ///< 逐关节行（jointIndex 升序——稳定序）
    double cycleDurationS;           ///< 完整任务循环时长（含驻留），单位 s＝t_N−t₀
                                     ///<   （积分边界全跨度）；缺失→NaN（NotProvided
                                     ///<   语义，不伪造 0）
    bool includesDwell;              ///< 驻留是否计入（Verified 必须 true——§7.3）。
                                     ///<   统计器口径＝"积分已按序列全程时间轴执行"：
                                     ///<   序列可积分（有效样本≥2 且跨度>0）即 true——
                                     ///<   上游轨迹含驻留段时驻留自动计入（统计器无
                                     ///<   段类型信息、不做任何速度阈值判定）；序列为
                                     ///<   部分区间时调用方不得将本摘要当完整循环
    bool timeParamAvailable;         ///< 时间参数可用性（false 时能量/平均功率字段无效
                                     ///<   且显式标记——§4.6 不伪造）
};

// =====================================================================
// 单工况结果（§4.4 OperatingConditionResult——payload 内聚合形态；
// WP-17-T04 增列：类型面随统计落位，装配归编排层）。
// =====================================================================

/**
 * @brief 单工况结果（§4.4 原文契约——序列＋工况级峰值＋功率能量摘要的
 *        聚合形态；由编排层把 SeriesBuilder/EnvelopeCalculator/
 *        PowerEnergyCalculator 的产出装配进来——本单元不提供编排器，
 *        多工况循环归上层（§10.9））。
 * 值语义纯结构；线程安全。
 */
struct OperatingConditionResult {
    core::ObjectId conditionId;      ///< 工况 ID（与 series.conditionId 恒同）
    std::vector<core::ObjectId> caseScope;  ///< 覆盖的 case 集（通常单工况自身；
                                            ///<   EVI-02 关联——装配层填）
    DynamicsSeries series;           ///< 序列（身份块见 DynamicsSeries）
    std::vector<PeakRecord> peaks;   ///< 工况级峰值（逐关节逐量——
                                     ///<   EnvelopeCalculator::computePeaks 产出；
                                     ///<   行序＝(jointIndex 升序, 量纲 token 序)，
                                     ///<   见 Envelope.hpp 的 token 表）
    PowerEnergySummary powerEnergy;  ///< 功率/能量摘要（PowerEnergyCalculator 产出）
    DynamicsValidity validity;       ///< 工况级完整性（＝series.validity 的编排层透传）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 工况级诊断（失败定位到
                                     ///<   工况/段/样本——core::DiagnosticRecord 素材）
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_DYNTYPES_HPP
