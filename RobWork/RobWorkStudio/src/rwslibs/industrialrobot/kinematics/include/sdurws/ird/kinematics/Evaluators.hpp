/**
 * @file   Evaluators.hpp
 * @brief  kin.pose-metrics 评估器（KIN-01 的 IEngineeringEvaluator 形态）——
 *         descriptor 依赖声明、宿主注入工厂与评估器实现（T03 批次；四评估
 *         器族其余成员随 T04~T07 表尾追加）。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Evaluators.hpp 行——"四个
 *     IEngineeringEvaluator 实现与 descriptor（依赖声明 §4.3）"，任务列
 *     T03~T07）、§4.3（依赖声明表 kin.pose-metrics 行）、§9.3（评估器
 *     注册——本单元不自我注册、不持有注册表）、§9.2 IFkEvaluator（计算
 *     契约）、§14.4 条 2（评估键 4 个＋依赖声明实例）
 *   - evidence 冻结契约（EV-T10/EV-T04 已落位）：IEngineeringEvaluator/
 *     IEvaluatorFactory/EvaluatorDescriptor/DependencyDeclaration/
 *     EvaluationRequest/EvaluationOutput（evidence Evaluator.hpp）；
 *     EvaluationKey 词形闸门 isValidEvaluationKey（evidence Slice.hpp——
 *     [a-z][a-z0-9-]{1,63}，**无点**）
 *   - 治理裁决 O-37（DTB §4.2 closed，2026-09-22）：运行时模型视图＝宿主
 *     注入——评估宿主构建只读视图、**经评估器工厂闭包注入**（registry
 *     create() 无参签名不变；视图随请求生命周期）。本头的工厂构造函数即
 *     注入点；编排者 2026-09-26 就"evidence §9 增补未落卡是否转 blocked"
 *     裁决放行（记录随卡 §14.6——本单元不依赖、不冻结 evidence 侧注入
 *     契约文字）。
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 4（评估器形态）
 *
 * 背景说明（评估键词形的随附同步）：卡面 §4.3 键字面 "kin.pose-metrics"
 * 含点，而 evidence 冻结的评估键词形闸门不接受点（DependencyKey 才允许
 * 点——evidence Slice.hpp 两闸门对照注释）；evidence 自家测试替身先例即
 * kebab 形态（"kin-batch-ik"，EV-T11）。本评估器键取 kebab 形态
 * "kin-pose-metrics"，偏差随卡 §14.6 登记（T04~T07 各键随其落位任务同样
 * 处理）。域载荷 kindToken 不受该闸门约束（evidence §7.1 域词表仅施加
 * 非空＋无 NUL 下限），保留卡面点形 "kin.pose-metrics.v1"。
 *
 * 线程安全：评估器与工厂均无跨调用可变状态（descriptor.stateless＝true
 * ——实例可共享）；注入视图的生命周期约束随工厂构造注（宿主保证
 * evaluate() 期间存活）。
 */

#ifndef IRD_KINEMATICS_EVALUATORS_HPP
#define IRD_KINEMATICS_EVALUATORS_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>          // TaskIdentity（任务五元组——载荷绑定面）
#include <sdurws/ird/core/Evaluation.hpp>        // EvaluationMode（模式词表）
#include <sdurws/ird/evidence/Dependency.hpp>    // DependencyDeclaration（依赖声明值）
#include <sdurws/ird/evidence/Envelope.hpp>      // EvidenceProfileRef（Profile 声明引用）
#include <sdurws/ird/evidence/Evaluator.hpp>     // IEngineeringEvaluator/IEvaluatorFactory/descriptor
#include <sdurws/ird/execution/TaskTypes.hpp>    // execution::TaskCapability（能力声明——§8.3/§12 交接）
#include <sdurws/ird/kinematics/Evidence.hpp>    // BatchQuery/BatchComputation/证据组装器（T05 批量通道值面）
#include <sdurws/ird/kinematics/Fk.hpp>          // FkEvaluator/PoseMetrics（计算委托）
#include <sdurws/ird/kinematics/Ik.hpp>          // IkSolver/IkOutcome/初值策略/碰撞端口（计算委托）
#include <sdurws/ird/kinematics/KinTypes.hpp>    // TcpRef/IKinRuntimeView（注入面）

namespace sdurws::ird::kinematics {

// =====================================================================
// 域常量（唯一书写点——descriptor/载荷/测试共用，禁第二处字面量）
// =====================================================================

/// 评估键（evidence 词形闸门内 kebab 形态——文件头"随附同步"注；
/// 卡面 §4.3 键名的实现形态，偏差登记随卡 §14.6）。
inline constexpr char kPoseMetricsEvaluationKey[] = "kin-pose-metrics";

/// 域载荷登记 token（evidence §7.1 域词表——卡面点形保留；v1 随
/// canonical 布局 codec 版本演进）。
inline constexpr char kPoseMetricsPayloadToken[] = "kin.pose-metrics.v1";

/// 评估器输入/输出契约版本（进切片身份 sliceId——CON-04；实现演进即
/// 递增，一经交付不回退）。
inline constexpr std::uint32_t kPoseMetricsContractVersion = 1U;

// =====================================================================
// PoseMetricsQuery——评估查询值（工厂闭包注入的计算主体）
// =====================================================================

/**
 * @brief kin.pose-metrics 的单次评估查询：TCP 引用＋权威关节向量
 *        （§5.2 输入行；确定性元组 (snapshot, q, tcp) 的后两元——
 *        snapshot 经注入视图承载）。
 *
 * 值语义纯结构；线程安全（并发只读）。q 的语义与约束同 IFkEvaluator::
 * evaluate（权威 q——§5.1；rad/m 逐关节 SI）。
 */
struct PoseMetricsQuery {
    /// TCP 引用（快照内解析——KinTypes.hpp 解析规则）。
    TcpRef tcp;
    /// 权威关节向量（可动关节链序；rad／m）。
    std::vector<double> q;

    bool operator==(const PoseMetricsQuery& o) const
    {
        return tcp == o.tcp && q == o.q;
    }
    bool operator!=(const PoseMetricsQuery& o) const { return !(*this == o); }
};

// =====================================================================
// makePoseMetricsDescriptor——依赖声明与形态（§4.3 kin.pose-metrics 行）
// =====================================================================

/**
 * @brief 组装 kin.pose-metrics 的评估器描述符（§4.3 依赖声明行的值面）。
 *
 * 字段落值（与 evidence EvaluatorDescriptor 冻结字段一一对应）：
 *   - key＝kPoseMetricsEvaluationKey（词形经 evidence 闸门——文件头注）；
 *   - contractVersion＝kPoseMetricsContractVersion（进 sliceId——CON-04）；
 *   - inputs＝§4.3 行五条依赖（全部 Required；条件依赖无——本评估器不
 *     声明 collision-models，碰撞属 region-coverage/碰撞接入任务 T07）：
 *       model.robot-design(Object)／tcp(Object→tool-definition)／
 *       policy.resolved(Policy)／namemap(NameMap)／config.ik(Configuration)；
 *   - profile＝{profileId="kin", version="1", contentIdentity=零值}——
 *     注册期由 evidence Profile 注册表计算权威身份（域不可申报——
 *     evidence §9.5/R-3；注册归 L5 装配清单，本单元不自我注册 §9.3）；
 *   - supportedModes＝{Preview, Quick, Verified}（§4.3 行模式列全三值）；
 *   - stateless＝true（无跨调用状态——实例可共享，§9.4）。
 *
 * @return 描述符值（每次调用返回新值；注册时由 EvaluatorRegistry 执行
 *         注册期校验——key 词形/契约版本/模式集/Profile 解析/声明闭包）
 *
 * 纯函数；线程安全；确定性（同卡面同值——NFR-COR-02）。
 */
evidence::EvaluatorDescriptor makePoseMetricsDescriptor();

// =====================================================================
// PoseMetricsEvaluator——IEngineeringEvaluator 实现（评估器形态）
// =====================================================================

/**
 * @brief kin.pose-metrics 评估器：把 IFkEvaluator 纯函数服务包装为
 *        evidence 评估器契约（descriptor＋evaluate）。
 *
 * 形态要点（acceptance 4 逐项）：
 *   - 依赖声明/模式/无状态：见 makePoseMetricsDescriptor（成员持有——
 *     descriptor() 返回引用须指向稳定存储）；
 *   - 运行时模型视图经宿主注入消费：视图指针由工厂闭包传入（O-37 裁决
 *     形态），**非 owning**——宿主保证 evaluate() 期间视图存活（§9.4
 *     "每请求构建、请求结束失效"）；
 *   - 确定性＝同 (snapshot, q, tcp) 同字节输出：payload.canonicalBytes
 *     由 encodePoseMetricsCanonical 对确定性计算结果编码（浮点布局含
 *     于字节——§5.2 确定性行）；
 *   - 取消：不可取消（§9.2 IFkEvaluator @取消 行——可预测 <1 s 内联
 *     计算；不周期查询 context.cancellationRequested()，契约显式豁免
 *     长评估轮询义务）。
 *
 * 错误分轨（域约定，随卡 §14.6 登记——evidence §9.3"抛 EvidenceError
 * 或返回诊断，域自选"的 kin.pose-metrics 落值）：
 *   - **装配期 fail-fast**（工厂/评估器构造函数，std::invalid_argument）：
 *     查询非法——视图空指针、q 维度≠设备自由度、q 含非有限分量
 *     （NFR-COR-03 调用方错误轨道——不进入评估输出面）；
 *   - **评估期结构化诊断**（EvaluationOutput.diagnostics，零 payload、
 *     零证据项）：视图无设备链 → KIN-NO-DEVICE；TCP 未配置/悬空 →
 *     KIN-NO-TCP（§9.6 两码 error 级；经 kinematicsDiagCode 映射取注册
 *     码值——禁字符串拼码）。FK 失败≠工程不可行：不产证据/不产证明，
 *     判定留 evidence（§5.2 两分语义）。
 */
class PoseMetricsEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /**
     * @brief 构造绑定（视图, 查询）的评估器实例（工厂经闭包调用）。
     *
     * @param view   [in] 宿主注入的只读模型视图（非 owning；调用方保证
     *                    evaluate() 期间存活；空指针→std::invalid_argument）
     * @param query  [in] 评估查询（值持有）；q 维度/有限性不符→
     *                    std::invalid_argument（装配期 fail-fast——类注
     *                    错误分轨）
     *
     * @throws std::invalid_argument 装配期查询非法（视图空/q 维度不符/
     *         q 非有限）
     */
    PoseMetricsEvaluator(const IKinRuntimeView* view, PoseMetricsQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次位姿指标评估（§9.3 调用约定；纯计算——不派发任务、
     *        不写项目、不产生修订）。
     *
     * @param request [in] 评估请求（任务五元组/模式/快照/切片——本评估
     *                     器计算面只消费其 mode 合法性：descriptor 声明
     *                     全三模式，任意合法请求模式均可评估；模式对
     *                     证据效力的门禁在汇总/包络层，不影响计算字节
     *                     ——确定性元组不含 mode）
     * @param context [in] 宿主调用上下文（取消查询对本评估器无效果——
     *                     类注"不可取消"；不读取对象字节——模型经注入
     *                     视图消费）
     *
     * @return 成功＝payload（DomainPayload{token, canonical 字节, 摘要}；
     *         摘要对 canonicalBytes 由 core ContentDigester 计算——CR-02）；
     *         失败＝diagnostics 携带 KIN-NO-DEVICE/KIN-NO-TCP 结构化素材
     *         （零 payload——错误面不产正式载荷）
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning——见类注）
    PoseMetricsQuery m_query;                   ///< 评估查询（值持有）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

// =====================================================================
// PoseMetricsEvaluatorFactory——宿主注入工厂（O-37 裁决形态落点）
// =====================================================================

/**
 * @brief kin.pose-metrics 的 IEvaluatorFactory 实现：宿主构建实例时把
 *        (视图, 查询) 捕获进闭包，create() 保持**无参签名**（O-37 裁决
 *        原文"registry.create() 无参签名不变"——注册表路径兼容）。
 *
 * 生命周期：宿主按请求构建工厂（视图随请求生命周期——裁决原文）；工厂
 * 与其产出的评估器实例由调用方持有。线程安全：无跨调用可变状态——
 * create() 可并发调用（产出的评估器各自独立持有描述符副本）。
 */
class PoseMetricsEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    /**
     * @brief 构造绑定 (视图, 查询) 的工厂（注入点）。
     *
     * @param view   [in] 宿主注入视图（非 owning——须比工厂及全部产出
     *                    实例活得久；空指针→std::invalid_argument）
     * @param query  [in] 评估查询（值持有；校验同评估器构造——装配期
     *                    fail-fast）
     *
     * @throws std::invalid_argument 装配期查询非法（同 PoseMetricsEvaluator）
     */
    PoseMetricsEvaluatorFactory(const IKinRuntimeView* view, PoseMetricsQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /// 无参签名（O-37 裁决——注册表兼容）；每次调用产出独立实例。
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning）
    PoseMetricsQuery m_query;                   ///< 闭包捕获的查询（值）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

// =====================================================================
// 以下为 T04 批次表尾追加（WP-15-T04——§3.3 布局表 Evaluators.hpp 行
// 任务列 T03~T07 的 T04 半区：kin.task-point-ik 评估器族；既有 T03
// 声明不重排）。评估键/契约版本常量的唯一书写点在 Ik.hpp（证明素材
// producer 绑定共用——禁第二处字面量），本头消费不重定义。
// =====================================================================

/// 域载荷登记 token（evidence §7.1 域词表——卡面点形保留；v1 随
/// canonical 布局 codec 版本演进）。
inline constexpr char kTaskPointIkPayloadToken[] = "kin.task-point-ik.v1";

// =====================================================================
// TaskPointIkQuery——kin.task-point-ik 的单点评估查询值
// =====================================================================

/**
 * @brief kin.task-point-ik 的单次评估查询：目标位姿＋求解配置（§5.3
 *        IkRequest 的评估查询面投影——求解参数随查询显式携带，本评估器
 *        不读任何会话/全局状态）。
 *
 * 值语义纯结构；线程安全（并发只读）。物理单位：目标位姿平移 m／旋转
 * rad（基座系 {B}）；容差 m／rad；去重阈值 rad|m 逐轴；referenceQ 逐
 * 自由度 rad|m。
 */
struct TaskPointIkQuery {
    /// TCP 引用（快照内解析——KinTypes.hpp 解析规则）。
    TcpRef tcp;
    /// 任务点对象身份（结果绑定 pointOid——§5.6）。
    core::ObjectId pointOid;
    /// 工况对象身份（可空——批量通道 T05 必填）。
    std::optional<core::ObjectId> conditionId;
    /// 目标位姿（基座系 {B} 的 TCP 目标——§5.3 字段 1）。
    rw::math::Transform3D<double> targetInBase = runtime::detail::identityTransform3D();
    /// 排序参考构型（rad／m——D-KIN-4 显式输入；默认值的落点在请求
    /// 装配层，本评估器不隐式读会话姿态）。
    std::vector<double> referenceQ;
    /// 初值策略（§5.3 三值——makeInitialValues 的策略入参）。
    InitialValueStrategy initialStrategy = InitialValueStrategy::JointGrid;
    /// 初值数量（≥1——SeededRandom/JointGrid 消费；ReferenceQ 忽略）。
    std::uint32_t initialValuesCount = 1U;
    /// 单初值迭代上限（≥1）。
    std::uint32_t iterationLimit = 1U;
    /// 位置残差容差（m；默认 1e-6——附录 D 第 1 项）。
    double positionTolerance = 1e-6;
    /// 姿态残差容差（rad；默认 1e-6——附录 D 第 2 项）。
    double orientationTolerance = 1e-6;
    /// 去重阈值（rad|m 逐轴；默认 1e-6——附录 D 第 3 项）。
    double dedupThresholdPerAxis = 1e-6;
    /// 确定性种子（SeededRandom 必非 0——I-KIN-4；其余策略可 0）。
    std::uint64_t seed = 0;
    /// 求解配置摘要（config.ik——T10 落位前允许零值；入结果身份）。
    core::ContentIdentity configDigest;
    /// 碰撞会话句柄（非 owning；空＝策略未启用碰撞——硬过滤③跳过并
    /// 标记 collisionNotEvaluated；真实④端口会话组装归 T07）。
    const IKinCollisionSession* collisionSession = nullptr;

    bool operator==(const TaskPointIkQuery& o) const
    {
        return tcp == o.tcp && pointOid == o.pointOid && conditionId == o.conditionId
            && referenceQ == o.referenceQ && initialStrategy == o.initialStrategy
            && initialValuesCount == o.initialValuesCount
            && iterationLimit == o.iterationLimit
            && positionTolerance == o.positionTolerance
            && orientationTolerance == o.orientationTolerance
            && dedupThresholdPerAxis == o.dedupThresholdPerAxis && seed == o.seed
            && configDigest == o.configDigest
            && collisionSession == o.collisionSession;
    }
    bool operator!=(const TaskPointIkQuery& o) const { return !(*this == o); }
};

// =====================================================================
// makeTaskPointIkDescriptor——依赖声明与形态（§4.3 kin.task-point-ik 行）
// =====================================================================

/**
 * @brief 组装 kin.task-point-ik 的评估器描述符（§4.3 行的值面）。
 *
 * 与 makePoseMetricsDescriptor 同构（字段落值见其注），差异仅两处：
 *   - key＝kTaskPointIkEvaluationKey（Ik.hpp 唯一书写点——kebab 词形，
 *     卡面点形键的随附同步偏差同 T03 口径）；
 *   - inputs＝pose-metrics 五条依赖＋`req.points`(Object)（§4.3 行原文
 *     "上述＋req.points(Object)"——任务点闭包为 KIN-02 的消费对象面）。
 *
 * @return 描述符值（每次调用返回新值；注册期校验归 EvaluatorRegistry）
 *
 * 纯函数；线程安全；确定性（同卡面同值——NFR-COR-02）。
 */
evidence::EvaluatorDescriptor makeTaskPointIkDescriptor();

// =====================================================================
// TaskPointIkEvaluator——IEngineeringEvaluator 实现（评估器形态）
// =====================================================================

/**
 * @brief kin.task-point-ik 评估器：把 IIkSolver 纯函数服务包装为
 *        evidence 评估器契约（descriptor＋evaluate；评估键＝
 *        "kin-task-point-ik"——trajectory WP-16-T05 对端消费）。
 *
 * 形态要点（与 PoseMetricsEvaluator 同轨，差异随注）：
 *   - 运行时模型视图经宿主注入消费（O-37 裁决形态；非 owning）；
 *   - 长运算——**必须**周期查询取消（§9.2 IIkSolver @取消 行与 FK 的
 *     豁免不同）：取消探针经构造时闭包接线到 IEvaluationContext::
 *     cancellationRequested()，取消返回零素材输出（取消不是结局）；
 *   - 确定性＝同 (snapshot, query) 同字节输出（payload 由
 *     encodeTaskPointIkPayloadCanonical 对确定性求解结果编码）。
 *
 * 错误分轨（域约定，随卡 §14.6 v0.4 登记——evidence §9.3"域自选"）：
 *   - **装配期 fail-fast**（构造函数，std::invalid_argument）：视图空
 *     指针、目标位姿非有限/容差非法（KIN-TARGET-ILLEGAL 语义锚）、
 *     referenceQ 维度/有限性违例、initialValuesCount/iterationLimit=0、
 *     去重阈值非法、SeededRandom 且 seed=0（I-KIN-4 拒绝不静默替换）、
 *     tcpKey 不命中（工具可解析时）——调用方错误轨，不进入评估输出面；
 *   - **评估期结构化诊断**（零 payload）：
 *       TCP 未配置/悬空 → KIN-NO-TCP（工具侧缺失保留到评估期——T03
 *       同款两分口径）；
 *       残差复验过滤解 → KIN-RESIDUAL-EXCEEDED（比较型：实际 m／期望 m
 *       与实际 rad／期望 rad——§9.6 行 4 requiresComparison 的生产面）；
 *       限位过滤解 → KIN-JOINT-LIMIT-VIOLATED（§9.6 行 5）；
 *       结局 2/3 → KIN-SEARCH-EXHAUSTED（§9.6 行 10——附搜索未果记录
 *       经 output.searchRecord 交付）；
 *       碰撞过滤解的诊断（KIN-COLLISION-FILTERED）归 T07（§9.6 任务列
 *       分工——本任务只产过滤记录，不产该码诊断）；
 *   - **结局映射**（§5.4 → evidence EvaluationOutput）：1/4 → payload
 *     （canonical 解集字节）；2/3 → searchRecord＋诊断；5 → proof
 *     （仅素材）；cancelled → 零素材输出。
 */
class TaskPointIkEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /**
     * @brief 构造绑定 (视图, 查询) 的评估器实例（工厂经闭包调用）。
     *
     * @param view   [in] 宿主注入的只读模型视图（非 owning；调用方保证
     *                    evaluate() 期间存活；空指针→std::invalid_argument）
     * @param query  [in] 评估查询（值持有）；装配期非法→std::invalid_argument
     *                    （类注错误分轨——KIN-TARGET-ILLEGAL 等语义锚）
     *
     * @throws std::invalid_argument 装配期查询非法
     */
    TaskPointIkEvaluator(const IKinRuntimeView* view, TaskPointIkQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次任务点 IK 评估（§9.3 调用约定；纯计算——不派发
     *        任务、不写项目、不产生修订）。
     *
     * @param request [in] 评估请求（snapshotId/sliceId/mode/task 五元组
     *                     经其填充结果绑定——§5.6；模式对证据效力的
     *                     门禁在汇总/包络层，Quick/Preview 载荷以 mode
     *                     字段承载 screening-only 语义——§8.4）
     * @param context [in] 宿主调用上下文（取消探针周期查询其
     *                     cancellationRequested()——kIkCancellationProbeInterval
     *                     迭代一次；不读取对象字节——模型经注入视图消费）
     *
     * @return 结局映射见类注（payload/proof/searchRecord/诊断的互斥
     *         组合；判定与包络构造归调用侧）
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning——见类注）
    TaskPointIkQuery m_query;                   ///< 评估查询（值持有）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    std::optional<KinematicsError> m_setupError; ///< 评估期结构化素材（TCP 缺失——两分口径）
};

// =====================================================================
// TaskPointIkEvaluatorFactory——宿主注入工厂（O-37 裁决形态落点）
// =====================================================================

/**
 * @brief kin.task-point-ik 的 IEvaluatorFactory 实现：宿主构建实例时把
 *        (视图, 查询) 捕获进闭包，create() 保持**无参签名**（O-37 裁决
 *        原文——注册表路径兼容）。生命周期与线程约束同
 *        PoseMetricsEvaluatorFactory（类注）。
 */
class TaskPointIkEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    /**
     * @brief 构造绑定 (视图, 查询) 的工厂（注入点；校验同评估器构造）。
     *
     * @throws std::invalid_argument 装配期查询非法（同 TaskPointIkEvaluator）
     */
    TaskPointIkEvaluatorFactory(const IKinRuntimeView* view, TaskPointIkQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /// 无参签名（O-37 裁决——注册表兼容）；每次调用产出独立实例。
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning）
    TaskPointIkQuery m_query;                   ///< 闭包捕获的查询（值）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

// =====================================================================
// 载荷 canonical 编码（§5.6 结果绑定五元组的字节面）
// =====================================================================

/**
 * @brief 将任务点 IK 结果编码为域 canonical 字节（定宽小端＋字段定序）。
 *
 * 编码布局（codec 版本 1；magic "IRDIK01"＋版本 u32）：outcomeKind u8＋
 * cancelled
 * u8＋collisionNotEvaluated u8＋请求身份块（snapshotId/sliceId/
 * configDigest 各 32B＋mode u8＋seed u64＋referenceQ u32+f64×n）＋对象
 * 绑定（pointOid 16B＋conditionId present u8[+16B]）＋任务五元组
 * 4×16B＋attempt u64＋evaluationKey u32+len＋contractVersion u32（§5.6
 * 绑定六要素）＋统计 4×u64＋solutions u32×{…}＋filteredRecords u32×{…}＋
 * searchRecord present u8[{…}]。f64＝IEEE754 位模式小端（含 +∞）。
 *
 * 要求值分离（§5.6）：解记录只携带**达成值**（残差/裕量/指标），要求
 * 值对比（实际/要求/单位）经证据明细面（§8.2 EvidenceItem）交付——
 * 本编码不把目标位姿混入解记录（目标经绑定面溯源）。
 *
 * 证明素材不入本载荷（结局 5 经 EvaluationOutput.proof 交付——evidence
 * §6.3 校验面，非 opaque 载荷）。
 *
 * @param outcome [in] 求解结果（cancelled=true 时产出的字节无消费语义
 *                     ——调用方在取消轨不产 payload）
 * @param task    [in] 任务五元组（EvaluationRequest.task——绑定面）
 * @return canonical 字节（确定性：同输入同字节——NFR-COR-01）
 *
 * 纯函数；线程安全；不抛（f64 位模式直写）。
 */
std::vector<std::uint8_t> encodeTaskPointIkPayloadCanonical(
    const IkOutcome& outcome, const core::TaskIdentity& task);

// =====================================================================
// 以下为 T05 批量表尾追加（WP-15-T05——§3.3 布局表 Evaluators.hpp 行
// 任务列 T03~T07 的 T05 半区：kin.task-points-batch 评估器族；既有 T03/
// T04 声明不重排）。批量值模型/证据组装器/键常量的唯一书写点在
// Evidence.hpp（本头消费不重定义——键常量放 Value/Builder 侧的理由见
// 其文件头注）。
// =====================================================================

// =====================================================================
// BatchQuery——批量评估查询值（工厂闭包注入的计算主体）
// =====================================================================

/**
 * @brief kin.task-points-batch 的批量评估查询：注入点集/工况投影＋求解
 *        配置＋执行协作通道（§7.1 批量执行图的查询面投影）。
 *
 * 注入面纪律（O-37 裁决同款）：points/conditions 为宿主解析后的投影值
 * （req 对象 schema 不进本单元——R-1，Evidence.hpp 文件头注）；solver
 * 与 checkpointSink 为**非 owning** 可选指针——
 *   - solver 为空＝使用内置 IkSolver（生产路径）；测试以替身 IIkSolver
 *     注入（§10.1"可控求解器测试替身"口径的 T05 提前承载——T13 收口）；
 *   - checkpointSink 为空＝无检查点通道（纯计算合法——§9.2 @副作用
 *     "归档/检查点经宿主通道"；生产宿主注入 IBatchCheckpointSink 适配器
 *     接入 execution 检查点通道——P-KIN-7 最小端口处置）。
 *
 * 值语义纯结构（指针成员浅拷贝——指向对象须比 evaluate() 调用活得久，
 * 生命周期约束同视图）；线程安全（并发只读）。单位：referenceQ 逐自由
 * 度 rad|m；去重阈值 rad|m 逐轴；批大小/线程数无量纲计数。
 */
struct BatchQuery {
    // ---- 注入投影值（宿主解析）----
    /// 任务点投影集（启用与停用条目都注入——停用点产 NotApplicable 显式
    /// 标记，§7.1；重复 pointOid 按集合语义去重）。
    std::vector<BatchTaskPoint> points;
    /// 工况投影集（caseSubset 各条目的适用范围/要求值投影；重复
    /// conditionId 按集合语义去重）。
    std::vector<BatchCondition> conditions;
    /// 默认 TCP（点无 tcpOverride 时使用——KIN-14 默认 TCP 的查询级承载；
    /// 解析规则同 KinTypes.hpp TcpRef 注）。
    TcpRef defaultTcp;

    // ---- 求解配置（config.ik 的 T10 前直传投影——T04 单点查询同构）----
    /// 排序参考构型（rad|m——D-KIN-4 显式输入；维度须＝设备自由度）。
    std::vector<double> referenceQ;
    /// 初值策略（§5.3 三值）。
    InitialValueStrategy initialStrategy = InitialValueStrategy::JointGrid;
    /// 初值数量（≥1）。
    std::uint32_t initialValuesCount = 1U;
    /// 单初值迭代上限（≥1）。
    std::uint32_t iterationLimit = 1U;
    /// 去重阈值（rad|m 逐轴；>0 有限）。
    double dedupThresholdPerAxis = 1e-6;
    /// 确定性种子（SeededRandom 必非 0——I-KIN-4；其余策略可 0）。
    std::uint64_t seed = 0;
    /// 求解配置摘要（config.ik——T10 落位前允许零值；入结果身份）。
    core::ContentIdentity configDigest;
    /// 碰撞会话句柄（非 owning；空＝策略未启用碰撞——硬过滤③跳过并
    /// 标记 collisionNotEvaluated；真实④端口会话组装归 T07）。
    const IKinCollisionSession* collisionSession = nullptr;

    // ---- 批量执行协作参数（§7.1/§8.3/§8.4）----
    /// 批大小上限（≥1；默认 kBatchDefaultBatchSize=256——D-KIN-6 黄金
    /// 锁定值，修改走设计变更）。
    std::uint32_t maxBatchSize = kBatchDefaultBatchSize;
    /// 并行分片线程数（≥1；§8.4——分片＝全序工作项连续区间、合并按全
    /// 序归并；线程数是执行参数、**不进结果身份**——同输入异线程数等价
    /// 集合＋稳定排序一致（本实现逐位一致））。
    std::uint32_t threadCount = 1U;
    /// 求解器替身注入口（非 owning；空＝内置 IkSolver——生产路径）。
    const IIkSolver* solver = nullptr;
    /// 批检查点 watermark 通道（非 owning；空＝无检查点通道——纯计算）。
    IBatchCheckpointSink* checkpointSink = nullptr;

    bool operator==(const BatchQuery& o) const
    {
        return points == o.points && conditions == o.conditions
            && defaultTcp == o.defaultTcp && referenceQ == o.referenceQ
            && initialStrategy == o.initialStrategy
            && initialValuesCount == o.initialValuesCount
            && iterationLimit == o.iterationLimit
            && dedupThresholdPerAxis == o.dedupThresholdPerAxis && seed == o.seed
            && configDigest == o.configDigest
            && collisionSession == o.collisionSession
            && maxBatchSize == o.maxBatchSize && threadCount == o.threadCount
            && solver == o.solver && checkpointSink == o.checkpointSink;
    }
    bool operator!=(const BatchQuery& o) const { return !(*this == o); }
};

// =====================================================================
// makeTaskPointsBatchDescriptor——依赖声明与形态（§4.3 行）
// =====================================================================

/**
 * @brief 组装 kin.task-points-batch 的评估器描述符（§4.3 行的值面）。
 *
 * 与 makeTaskPointIkDescriptor 同构（字段落值见其注），差异三处：
 *   - key＝kTaskPointsBatchEvaluationKey（Evidence.hpp 唯一书写点——
 *     kebab 词形，卡面点形键的随附同步偏差同 T03/T04 口径）；
 *   - inputs＝task-point-ik 六条依赖＋`req.conditions`(Object)（§4.3 行
 *     原文"上述＋req.conditions(Object)（caseSubset 分批）"）；
 *   - supportedModes＝{Quick, Verified}（§4.3 行模式列两值——批量通道
 *     不做 Preview；stateless=true＋threadSafety=FullyThreadSafe（§9.2
 *     IKinematicBatchEvaluator 头注原文——评估器无跨调用状态且支持
 *     并行分片）。
 *
 * @return 描述符值（每次调用返回新值；注册期校验归 EvaluatorRegistry）
 *
 * 纯函数；线程安全；确定性（同卡面同值——NFR-COR-02）。
 */
evidence::EvaluatorDescriptor makeTaskPointsBatchDescriptor();

// =====================================================================
// TaskPointsBatchEvaluator——IEngineeringEvaluator 实现（批量评估器）
// =====================================================================

/**
 * @brief kin.task-points-batch 评估器：批量任务点验证（KIN-03——切片内
 *        启用点集×caseSubset 展开、分批求解、三态素材与完成矩阵素材
 *        组装；§9.2 IKinematicBatchEvaluator 的实现形态）。
 *
 * 评估主流程（§7.1 批量执行图逐步；实现细节见 Evaluators.cpp 注）：
 *   1. 展开——启用点集×caseSubset 产 (point,condition) 工作项：appliesTo
 *      过滤（Stations 清单外不产项）、停用点/停用工况/None→NotApplicable
 *      显式标记、重复项按集合语义去重、悬空引用→InputInvalid 素材
 *      （V-12，附 KIN-POINT-REF-DANGLING 诊断）；
 *   2. 全序——工作项按 (pointOid, conditionId) 字典序排序（§8.4，分片
 *      前全序确定）；
 *   3. 分批——每批 ≤maxBatchSize（默认 256——D-KIN-6）；
 *   4. 逐批求解——批前协作取消查询（每批至少一次；观测到取消即停止
 *      派发新批——V-22 本单元侧，ARCH §4.4 的 2 s 界由批间查询点承载），
 *      批内逐项 IIkSolver 求解（并行分片＝连续区间、结果按全序槽位写回
 *      ——异线程数等价集合）；每项要求值对比（实际/要求/单位）；
 *   5. 批完成——检查点 watermark 上报（批粒度）＋reportProgress（批
 *      粒度，phase="solve-batch"）；
 *   6. 组装——完整性自检（工作项总数＝Σ批项数＋NotRun 数；caseSubset
 *      每项有终态标记）后经 KinematicEvidenceBuilder 产出 EvaluationOutput
 *      （唯一组装点——本评估器不自拼证据行）。
 *
 * 错误分轨（域约定，随卡 §14.6 v0.5 登记——evidence §9.3"域自选"）：
 *   - **装配期 fail-fast**（构造函数，std::invalid_argument）：视图空
 *     指针、referenceQ 维度/有限性违例、逐点目标非有限/容差非法
 *     （KIN-TARGET-ILLEGAL 语义锚）、objectId 保留值、caseSubset 引用
 *     投影集中不存在的工况、求解参数违例（计数 0/阈值非法/SeededRandom
 *     seed=0——I-KIN-4）、maxBatchSize/threadCount=0、tcpKey 不命中
 *     ——调用方错误轨，不进入评估输出面；
 *   - **评估期结构化素材**：悬空引用→InputInvalid 工作项＋
 *     KIN-POINT-REF-DANGLING（§9.6 行 16）；批量不完整→incomplete 标记
 *     ＋KIN-RESULT-INCOMPLETE（§9.6 行 15）——均经组装器落 output；
 *   - **取消**：协作式（批间查询＋批内求解探针）——未完成批如实 NotRun、
 *     输出带 incomplete 标记（取消不是结局——不产"取消"状态值）。
 *
 * 确定性（§8.4；acceptance 5）：同输入同线程数→逐位一致；异线程数→
 * 等价集合＋稳定排序一致（本实现结果按全序槽位写回，实际逐位一致）。
 * 线程安全：实例无跨调用可变状态（descriptor.stateless=true——可共享）。
 */
class TaskPointsBatchEvaluator final : public evidence::IEngineeringEvaluator {
public:
    /**
     * @brief 构造绑定 (视图, 批量查询) 的评估器实例（工厂经闭包调用）。
     *
     * @param view  [in] 宿主注入的只读模型视图（非 owning；调用方保证
     *                   evaluate() 期间存活；空指针→std::invalid_argument）
     * @param query [in] 批量评估查询（值持有；装配期非法→
     *                   std::invalid_argument——类注错误分轨）
     *
     * @throws std::invalid_argument 装配期查询非法
     */
    TaskPointsBatchEvaluator(const IKinRuntimeView* view, BatchQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /**
     * @brief 执行一次批量任务点验证评估（§9.3 调用约定；纯计算——不派
     *        发任务、不写项目、不产生修订；检查点/归档经宿主通道）。
     *
     * @param request [in] 评估请求（snapshotId/sliceId/mode/task 五元组/
     *                     caseSubset 经其填充绑定与展开分母——§5.6/EVI-02）
     * @param context [in] 宿主调用上下文（协作取消：批前查询其
     *                     cancellationRequested()——每批至少一次；批内
     *                     逐项求解经 IkRequest.cancellationProbe 周期查询
     *                     同一上下文；进度 reportProgress 批粒度上报）
     *
     * @return 评估产出（经组装器：逐项证据行/搜索未果聚合/证明素材/
     *         verdictInputs/payload/诊断；incomplete 时带 KIN-RESULT-
     *         INCOMPLETE 诊断——调用侧据此不产 Completed envelope，§7.1）
     */
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext& context) override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning——见类注）
    BatchQuery m_query;                         ///< 批量评估查询（值持有）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
    std::optional<KinematicsError> m_setupError; ///< 评估期结构化素材（TCP 缺失——
                                                 ///  批量级零素材轨，T04 两分口径同源）
};

// =====================================================================
// runBatchComputation——批量计算核心（纯函数服务——§3.1"计算库可被模型
// 测试直调"NFR-MNT-01 的批量落点；评估器形态与计算面的分离线）
// =====================================================================

/**
 * @brief 执行一次批量任务点计算（§7.1 批量执行图步骤 1~6：展开→全序→
 *        分批→逐批求解（取消/检查点/进度）→完成矩阵素材→完整性自检）。
 *
 * 与评估器形态的分工：本函数是**纯计算面**——只产 BatchComputation，
 * 不产证据（证据组装唯一经 KinematicEvidenceBuilder，NFR-MNT-04）；
 * TaskPointsBatchEvaluator::evaluate 即"装配校验＋本函数＋组装器"。
 * 独立可见性使批量展开/分批/取消语义可被单元与契约测试直调断言
 * （可控求解器替身注入口＝BatchQuery::solver）。
 *
 * @param view    [in] 宿主注入的只读模型视图（非 owning；调用期间存活）
 * @param query   [in] 批量评估查询（先经 validateBatchQuery 同款装配校验
 *                     ——违例 fail-fast；solver/checkpointSink 注入口生效）
 * @param request [in] 评估请求（snapshotId/sliceId/mode/task/caseSubset
 *                     ——绑定与展开分母；caseSubset 引用投影集中不存在的
 *                     工况→std::invalid_argument，请求装配契约违约）
 * @param context [in] 宿主调用上下文（协作取消批间查询＋进度上报）
 * @return 批量计算结果（不变式见 BatchComputation 注——违例 logic_error）
 *
 * @throws std::invalid_argument 装配/请求契约违约（同构造期校验面）
 * @throws std::logic_error 内部不变量破坏（排序/计数守恒/完成矩阵）
 *
 * 纯计算（不派发任务/不写项目/不产生修订）；线程安全（无跨调用状态，
 * 并行分片按 query.threadCount 执行）；确定性（§8.4——同输入同线程数
 * 逐位一致）。
 */
BatchComputation runBatchComputation(const IKinRuntimeView& view,
                                     const BatchQuery& query,
                                     const evidence::EvaluationRequest& request,
                                     evidence::IEvaluationContext& context);

// =====================================================================
// TaskPointsBatchEvaluatorFactory——宿主注入工厂（O-37 裁决形态落点）
// =====================================================================

/**
 * @brief kin.task-points-batch 的 IEvaluatorFactory 实现：宿主构建实例时
 *        把 (视图, 批量查询) 捕获进闭包，create() 保持**无参签名**（O-37
 *        裁决原文——注册表路径兼容）。生命周期与线程约束同
 *        TaskPointIkEvaluatorFactory（类注）；批量查询内投影值随宿主
 *        组装（每请求新建工厂）。
 */
class TaskPointsBatchEvaluatorFactory final : public evidence::IEvaluatorFactory {
public:
    /**
     * @brief 构造绑定 (视图, 批量查询) 的工厂（注入点；校验同评估器构造）。
     *
     * @throws std::invalid_argument 装配期查询非法（同 TaskPointsBatchEvaluator）
     */
    TaskPointsBatchEvaluatorFactory(const IKinRuntimeView* view, BatchQuery query);

    const evidence::EvaluatorDescriptor& descriptor() const override;

    /// 无参签名（O-37 裁决——注册表兼容）；每次调用产出独立实例。
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override;

private:
    const IKinRuntimeView* m_view;              ///< 宿主注入视图（非 owning）
    BatchQuery m_query;                         ///< 闭包捕获的批量查询（值）
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor() 引用所指）
};

// =====================================================================
// 任务类型能力声明（§8.3/§12 交接行——execution 通道消费面）
// =====================================================================

/**
 * @brief kin.task-points-batch 任务类型的执行期能力声明（§8.3 提交行
 *        原文的值面：支持取消 ✓、检查点＝批 watermark（Checkpoint-
 *        Granularity::Batch）、暂停不支持（R1 如实声明——收到暂停请求
 *        给明确状态反馈，EX-SM-7 口径）、强制终止代价＝低（Cheap——批
 *        间边界即安全中止点，无跨批不变量））。
 *
 * 消费方式（§12 交接）：L5 装配经 execution::EvaluatorRuntimeCapabilities::
 * declare(kTaskPointsBatchEvaluationKey, taskPointsBatchCapability()) 注册
 * （P-EX-7——执行能力归 execution 侧注册表扩展声明，evidence descriptor
 * 不混装；本单元只产出声明值）。键与评估键同一常量（装配清单一致性）。
 *
 * @return 能力声明值（纯函数——每次调用同值，确定性 NFR-COR-02）
 *
 * 线程安全：可重入纯函数。
 */
execution::TaskCapability taskPointsBatchCapability();

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_EVALUATORS_HPP
