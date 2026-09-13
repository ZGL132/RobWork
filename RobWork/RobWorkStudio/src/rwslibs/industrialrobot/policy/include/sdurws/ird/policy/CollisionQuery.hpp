/**
 * @file   CollisionQuery.hpp
 * @brief  碰撞评估的查询与输出类型——CollisionQueryKind/CollisionQuery/
 *         CollisionFinding/PairCoverageRecord/CollisionEvaluation 等词表
 *         与值载体（§6.2 唯一权威章节），评估状态机（§6.3）与稳定排序
 *         （§6.4）所操作的全部数据面。
 *
 * 设计依据：
 *   - units/policy.md §6.2（查询与输出类型——本头全部类型的唯一权威章节）、
 *     §6.3（评估状态机："失败/取消/中断/检测到碰撞"四分——finalized 语义）、
 *     §6.4（确定性与稳定排序——findings/minDistances/appliedFilters 三入口
 *     跨进程逐字节一致，NFR-COR-05/AT-19）、§7.4（距离语义与边界值决策表
 *     ——measuredClearance 的判定语义）、§12 POL-T07 行（本头为 POL-T07
 *     产物之一：CollisionQuery.hpp）
 *   - 需求 NFR-COR-05（三入口一致性——对象 ID 对与原因码）、KIN-02（IK 硬
 *     过滤素材——构型级发现）、KIN-05（无几何≠无碰撞——coverage 覆盖事实）、
 *     TRJ-04（路径复检——PathSequence 的 pathParameter 段内定位）、OPT-03
 *     （静态硬约束——SampleSet 消费）、TASK-02/CON-04（取消/失败不产正式
 *     证据——finalized=false 的消费方契约）
 *   - 验收锚点 POL-EVAL-8/R-POL-5（API 无阈值/模式参数——D-9 结构防覆盖：
 *     本头类型不携带任何阈值/评估模式成员，阈值唯一来源是会话绑定的
 *     EngineeringPolicySet，模式不得隐式改变工程策略——KIN-13/EVI-01）
 *
 * 背景说明（本头在评估链路中的位置——第一读者须知）：
 *   调用方（域评估器/execution 准备段）把查询意图装配成 CollisionQuery
 *   （构型序列＋查询种类＋是否需要最小距离），经
 *   CollisionEvaluationSession::evaluate(q, ctx)（CollisionEvaluator.hpp，
 *   §9.3 契约表）得到 CollisionEvaluation。查询与输出是**纯值类型**——
 *   不持有任何资源、不含任何回调；评估的状态机分支（Completed/Canceled/
 *   Failed）与终态性（finalized）由 evaluate 实现填充（src/CollisionQuery.cpp）。
 *
 *   "为什么查询不带阈值"（R-POL-5/D-9 的结构防覆盖）：阈值若随查询传入，
 *   调用方就能在同会话下逐查询改判——"策略单一权威"（ARC-05）在结构上
 *   失守。因此阈值唯一来自会话绑定的 EngineeringPolicySet（会话构建期
 *   固化、进 sessionIdentity），本类型在**成员清单层面**不存在阈值/模式
 *   （POL-EVAL-8 的 API 类型断言即钉住本头成员清单——增删成员将破坏
 *   测试侧的结构化绑定断言）。
 *
 * 实现纪律：
 *   - 本头含 rw 头（rw/math/Q.hpp、rw/kinematics/State.hpp）——§6.2 原文
 *     将构型序列与基准状态按值纳入查询载体，前向声明无法承载值成员；
 *     include 面纪律同 runtime Snapshot.hpp（runtime.md v0.8② 登记）：
 *     本头仅在集成模式被 TU include（产品面 CollisionQuery.cpp 与集成
 *     测试），冒烟模式无任何 TU include 它——冒烟口径不受影响。
 *   - 全部类型为纯值聚合（无可变共享状态）；operator== 逐字段全量等值
 *     （POL-EVAL-7"输出逐字段一致"的断言基础）。
 *   - 线程安全：纯值——按值传递/按 const 引用传递皆可；不承担同步职责。
 */

#ifndef SDURWS_IRD_POLICY_COLLISIONQUERY_HPP
#define SDURWS_IRD_POLICY_COLLISIONQUERY_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

// =====================================================================
// §6.2 查询侧——查询种类与查询载体。
// =====================================================================

/**
 * @brief 碰撞查询种类（§6.2 原文枚举——同一评估器、同一输出类型，差异经
 *        kind 与元数据表达）。
 *
 * 语义冻结（§6.2 注释原文）：
 *   - SingleState  单构型（IK 过滤、任务点构型检查——KIN-02/C8）；
 *   - PathSequence 有序路径采样（TRJ-04 复检——样本由调用方按其细分协议
 *                  生成，policy 不生成采样；样本顺序含路径连续性语义）；
 *   - SampleSet    无序/独立样本集（KIN-04 覆盖、KIN-09 工作空间、OPT-B
 *                  静态约束）。
 */
enum class CollisionQueryKind { SingleState, PathSequence, SampleSet };

/**
 * @brief 碰撞查询载体（§6.2 原文六字段——评估的唯一输入形）。
 *
 * 契约要点（§6.2 注释原文逐条）：
 *   - configurations：SingleState=恰 1 个构型；PathSequence/SampleSet 按
 *     调用方顺序保留（关节构型，单位 rad——与 §6.6 setState 语义一致）；
 *   - baseState：场景基准状态（附件/负载/工具姿态）；缺省（nullopt）＝
 *     场景基准状态（编译产物默认状态——evaluate 侧取 WorkCell 默认态）；
 *   - pathParameters：PathSequence 的各样本路径参数（复检证据用；与
 *     configurations 等长——TRJ-04 段内定位）；其余种类必须为空；
 *   - requestMinDistance：是否计算最小距离（间距检查需要时；输出侧
 *     minDistances 仅在本标志为真时给出）；
 *   - stopAtFirstFinding：提前终止（筛选/淘汰场景；输出仍带 coverage）。
 *
 * ★ 成员清单即契约（R-POL-5/D-9 结构防覆盖——本类型【不携带任何阈值/
 *   开关/评估模式】：阈值唯一来源是会话绑定的 EngineeringPolicySet；
 *   评估模式（Preview/Quick/Verified）不是本类型字段——模式不得隐式改变
 *   工程策略（KIN-13/EVI-01）。测试侧以结构化绑定断言钉住六成员清单
 *   （POL-EVAL-8 的"API 类型断言"）——新增成员即编译失败。
 *
 * kind 与样本数的匹配契约（§9.3 错误类型行"调用方违约→PolicyError"）：
 *   SingleState 要求恰 1 个构型且 pathParameters 为空；PathSequence 要求
 *   构型非空且 pathParameters 与 configurations 等长；SampleSet 要求构型
 *   非空且 pathParameters 为空。违约在 evaluate 入口 fail-fast。
 *
 * 线程安全：纯值。
 */
struct CollisionQuery {
    CollisionQueryKind kind = CollisionQueryKind::SingleState;  ///< 查询种类（§6.2 词表）
    /// 关节构型序列（单位 rad；SingleState=1 个；序列序＝调用方语义序）。
    std::vector<rw::math::Q> configurations;
    /// 场景基准状态（附件/负载/工具姿态）；缺省＝场景基准状态（编译产物默认态）。
    std::optional<rw::kinematics::State> baseState;
    /// 路径参数（PathSequence：与 configurations 等长；其余种类必须为空）。
    std::vector<double> pathParameters;
    bool requestMinDistance = false;  ///< 是否计算最小距离（间距检查需要时）
    bool stopAtFirstFinding = false;  ///< 提前终止（筛选/淘汰场景；输出仍带 coverage）
};

// =====================================================================
// §6.2 输出侧——发现、过滤留痕、覆盖事实与评估结果。
// =====================================================================

/**
 * @brief 碰撞发现种类（§6.2 原文枚举）。
 *
 * Collision＝相交/接触（后端二值语义——含零距离接触，§7.4"后端报告相交"
 * 行）；SafetyMarginViolation＝间距不足（0 ≤ d ＜ m——**非碰撞**，两类
 * 发现互斥，§7.4 决策表）。
 */
enum class CollisionFindingKind { Collision, SafetyMarginViolation };

/**
 * @brief 碰撞/间距发现（§6.2 原文十字段——构型/路径级事实记录）。
 *
 * 字段语义（§6.2 注释原文）：
 *   - objectA/objectB：有序对（canonical 字典序 A<B）——NFR-COR-05 对象
 *     ID 对；与作用域展开产物（ScopedCollisionPair）同序；
 *   - runtimeNameA/B：辅助显示（编译产物整名；不作为身份——CON-06）；
 *   - sampleIndex：采样位置（序列下标，0 基）；
 *   - pathParameter：PathSequence 时必填（段内定位——TRJ-04）；其余种类
 *     恒 nullopt；
 *   - kind：Collision=相交/接触；MarginViolation=间距不足（§7.4）；
 *   - measuredClearance：MarginViolation：实测最小距离（SI m）；Collision
 *     恒 nullopt；
 *   - penetrationDepth：Collision：后端可提供则给；不可提供=显式空（不
 *     伪造——当前内置后端不提供穿透深度，恒 nullopt）；
 *   - level：命中规则的 Must/Should（无规则命中=域默认 Must——§7.1
 *     "域启用即默认必检"的级别落点）。
 *
 * 发现自身不携带快照/运行身份（保持纯事实记录）——绑定由调用方在组装
 * 证据时完成（§8.4 evidence 契约）。
 * 线程安全：纯值。
 */
struct CollisionFinding {
    core::ObjectId objectA;        ///< 规范序第一端（字节字典序较小——A<B）
    core::ObjectId objectB;        ///< 规范序第二端
    std::string runtimeNameA;      ///< A 端编译产物整名（显示辅助——不作身份）
    std::string runtimeNameB;      ///< B 端编译产物整名（显示辅助——不作身份）
    std::size_t sampleIndex = 0;   ///< 采样位置（序列下标，0 基）
    /// PathSequence 段内路径参数（TRJ-04）；其余种类恒 nullopt。
    std::optional<double> pathParameter;
    CollisionFindingKind kind = CollisionFindingKind::Collision;  ///< 发现种类（§7.4 两分）
    /// MarginViolation：实测最小距离（SI m）；Collision 恒 nullopt。
    std::optional<double> measuredClearance;
    /// Collision：后端可提供则给；不可提供=显式空（不伪造）。
    std::optional<double> penetrationDepth;
    PolicyRuleLevel level = PolicyRuleLevel::Must;  ///< 命中规则级别（域默认=Must）

    bool operator==(const CollisionFinding& o) const
    {
        return objectA == o.objectA && objectB == o.objectB && runtimeNameA == o.runtimeNameA
            && runtimeNameB == o.runtimeNameB && sampleIndex == o.sampleIndex
            && pathParameter == o.pathParameter && kind == o.kind
            && measuredClearance == o.measuredClearance
            && penetrationDepth == o.penetrationDepth && level == o.level;
    }
    bool operator!=(const CollisionFinding& o) const { return !(*this == o); }
};

/**
 * @brief 过滤可追溯性记录（§6.2 原文——本次评估实际生效的过滤关系全量
 *        输出，不因首个短路，§7.2③）。
 *
 * ruleOrigin 取值形态（§6.2 注释原文）：
 *   - "policy.excludedPairs[i].reason"——显式排除对（i＝策略承载序下标，
 *     理由原文随策略内容身份可追溯）；
 *   - "default.adjacent-links"——默认相邻过滤（消费模型相邻事实——§7.1）。
 * 线程安全：纯值。
 */
struct AppliedFilterRecord {
    core::ObjectId objectA;  ///< 被过滤对象对第一端（规范序——A<B）
    core::ObjectId objectB;  ///< 被过滤对象对第二端
    std::string ruleOrigin;  ///< 规则来源引用（§6.2 取值形态——可追溯）

    bool operator==(const AppliedFilterRecord& o) const
    {
        return objectA == o.objectA && objectB == o.objectB && ruleOrigin == o.ruleOrigin;
    }
    bool operator!=(const AppliedFilterRecord& o) const { return !(*this == o); }
};

/**
 * @brief KIN-05 口径的覆盖事实（§6.2 原文——防"没检＝无碰撞"）。
 *
 * 计数语义（POL-T07 evaluate 实现口径，与作用域展开产物同源）：
 *   - pairsInScope：作用域内对数（Mandatory＋InScopeDefault——展开产物
 *     inScopePairCount()）；
 *   - pairsWithGeometry：其中双端均有有效碰撞几何的对数（无几何→缺口，
 *     不伪装已检——KIN-05）；
 *   - pairsEvaluated：其中实际被后端查询过的对数（≥1 次查询即计入；
 *     stopAtFirstFinding 提前终止时如实反映未查部分）；
 *   - pairsExcludedByRule：被过滤对数（ExcludedByRule＋
 *     ExcludedByAdjacencyDefault——展开产物 excludedPairCount()）；
 *   - excluded：被过滤关系全量列出（不因首个短路——§7.2③）。
 *
 * coverage 只承诺"已检样本"（§6.2 单状态/路径/区域区别行）：段间未验证、
 * 几何缺口、提前终止的未查部分都由本记录显式计数表达——消费方（evidence）
 * 据此判数据不足，绝不输出"无碰撞"性结论字段。
 * 线程安全：纯值。
 */
struct PairCoverageRecord {
    std::uint64_t pairsInScope = 0;         ///< 作用域内对数（Mandatory＋InScopeDefault）
    std::uint64_t pairsWithGeometry = 0;    ///< 双端有有效几何的对数（KIN-05 缺口显式化）
    std::uint64_t pairsEvaluated = 0;       ///< 实际被后端查询过的对数
    std::uint64_t pairsExcludedByRule = 0;  ///< 被过滤对数（显式排除＋默认相邻）
    std::vector<AppliedFilterRecord> excluded;  ///< 被过滤关系全量（§7.2③ 可追溯）

    bool operator==(const PairCoverageRecord& o) const
    {
        return pairsInScope == o.pairsInScope && pairsWithGeometry == o.pairsWithGeometry
            && pairsEvaluated == o.pairsEvaluated
            && pairsExcludedByRule == o.pairsExcludedByRule && excluded == o.excluded;
    }
    bool operator!=(const PairCoverageRecord& o) const { return !(*this == o); }
};

/**
 * @brief 逐样本最小距离摘要（§6.2 原文——requestMinDistance 时逐样本给
 *        出；minDistance 为该样本内**已实测**对的最小值——发生碰撞的对
 *        不做距离查询〔碰撞判定已由二值语义给出，穿透深度不伪造〕，故
 *        全碰撞样本的 minDistance 为显式 nullopt 而非伪造 0）。
 * 线程安全：纯值。
 */
struct SampleDistanceSummary {
    std::size_t sampleIndex = 0;  ///< 采样位置（序列下标，0 基）
    /// 本样本内已实测对的最小距离（SI m）；无可实测对（全碰撞/全缺口）＝nullopt。
    std::optional<double> minDistance;
    /// 取得最小值的对之 A 端对象（无可实测对＝nullopt）。
    std::optional<core::ObjectId> nearestToA;
    /// 取得最小值的对之 B 端对象（无可实测对＝nullopt）。
    std::optional<core::ObjectId> nearestToB;

    bool operator==(const SampleDistanceSummary& o) const
    {
        return sampleIndex == o.sampleIndex && minDistance == o.minDistance
            && nearestToA == o.nearestToA && nearestToB == o.nearestToB;
    }
    bool operator!=(const SampleDistanceSummary& o) const { return !(*this == o); }
};

/**
 * @brief 评估状态（§6.3 原文枚举——状态机三分；"进程/会话中断"无返回值，
 *        不在本词表）。
 *
 * 语义（§6.3 状态机表）：Completed=评估完成（有/无发现皆可，finalized=
 * true）；Canceled=取消命中（样本间协作检查点，部分 findings 保留，
 * finalized=false，**无错误诊断**——UX-03）；Failed=后端/策略异常（RobWork
 * 异常、几何资源错误、检测器不可用、名称不可解析、上下文失效——已产生
 * 部分保留，finalized=false，POLICY-CLL-* 诊断定位）。
 */
enum class CollisionEvaluationStatus { Completed, Canceled, Failed };

/**
 * @brief 作用域适用性（§6.2 原文枚举）。
 *
 * Applicable=正常评估；EmptyScope=作用域解析为空（**≠"无碰撞"**——KIN-05
 * 口径由 coverage 表达）；CollisionDisabledByPolicy=策略禁用被误调用
 * （§4.3 enabled=false——评估期应答）。
 */
enum class ScopeApplicability { Applicable, EmptyScope, CollisionDisabledByPolicy };

/**
 * @brief 碰撞评估结果（§6.2 原文十一字段——evaluate 的唯一输出形）。
 *
 * 终态性契约（§6.3 状态机表——消费方必须遵守）：
 *   - finalized 仅在 Completed=true；Canceled/Failed 恒 false——**非终态
 *     输出不得进入正式证据/可行集**（TASK-02/CON-04 口径；evidence 侧
 *     不得采信为 Satisfied——evidence.md §6.2 Invalid/Missing 口径）；
 *   - findings：稳定排序（§6.4：sampleIndex 升序→对象对字典序→kind）；
 *     Canceled/Failed 时为已产生的非终态部分；
 *   - 策略/场景/名称映射身份回填（policyContentIdentity/sceneIdentity/
 *     nameMapIdentity）＝判定依据绑定素材（CON-06/§8.4——policy 不越权
 *     做任务级绑定，§8.3 分工）。
 *
 * 本类型不含 Feasible/EngineeringInfeasible 语义字段（§8.3"policy 不得
 * 自行发布正式工程结论"——任务级判定归 evidence）。
 * 线程安全：纯值。
 */
struct CollisionEvaluation {
    CollisionEvaluationStatus status = CollisionEvaluationStatus::Failed;  ///< 评估状态（§6.3）
    ScopeApplicability applicability = ScopeApplicability::Applicable;     ///< 作用域适用性
    /// 判定依据绑定：策略内容身份（会话绑定的 EngineeringPolicySet——CON-06）。
    core::ContentIdentity policyContentIdentity;
    /// 场景内容身份（编译产物——CR-04 值传递，policy 不重算）。
    core::ContentIdentity sceneIdentity;
    /// 名称映射内容身份（runtime ⑥端口——CON-06 映射变化可观测）。
    core::ContentIdentity nameMapIdentity;
    /// 碰撞/间距发现（稳定排序——§6.4；非终态时为已产生部分）。
    std::vector<CollisionFinding> findings;
    /// 逐样本最小距离摘要（仅 requestMinDistance 且评估推进到距离查询时给出）。
    std::optional<std::vector<SampleDistanceSummary>> minDistances;
    PairCoverageRecord coverage;                 ///< KIN-05 口径覆盖事实（防"没检＝无碰撞"）
    std::vector<AppliedFilterRecord> appliedFilters;  ///< 实际生效过滤关系全量（§7.2③）
    /// 评估期诊断（POLICY-CLL-* 家族；正常取消不产生错误诊断——UX-03）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 仅 Completed=true；Canceled/Failed 恒 false（§6.3——非终态不入正式证据）。
    bool finalized = false;

    bool operator==(const CollisionEvaluation& o) const
    {
        return status == o.status && applicability == o.applicability
            && policyContentIdentity == o.policyContentIdentity
            && sceneIdentity == o.sceneIdentity && nameMapIdentity == o.nameMapIdentity
            && findings == o.findings && minDistances == o.minDistances
            && coverage == o.coverage && appliedFilters == o.appliedFilters
            && diagnostics == o.diagnostics && finalized == o.finalized;
    }
    bool operator!=(const CollisionEvaluation& o) const { return !(*this == o); }
};

}  // namespace sdurws::ird::policy

#endif  // SDURWS_IRD_POLICY_COLLISIONQUERY_HPP
