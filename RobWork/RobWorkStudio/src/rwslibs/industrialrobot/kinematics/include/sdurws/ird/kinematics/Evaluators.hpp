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
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>        // EvaluationMode（模式词表）
#include <sdurws/ird/evidence/Dependency.hpp>    // DependencyDeclaration（依赖声明值）
#include <sdurws/ird/evidence/Envelope.hpp>      // EvidenceProfileRef（Profile 声明引用）
#include <sdurws/ird/evidence/Evaluator.hpp>     // IEngineeringEvaluator/IEvaluatorFactory/descriptor
#include <sdurws/ird/kinematics/Fk.hpp>          // FkEvaluator/PoseMetrics（计算委托）
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

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_EVALUATORS_HPP
