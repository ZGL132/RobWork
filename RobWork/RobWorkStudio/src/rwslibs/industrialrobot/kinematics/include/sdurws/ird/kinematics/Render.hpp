/**
 * @file   Render.hpp
 * @brief  失败点/薄弱区三维渲染数据（KIN-07）——四类渲染数据结构、
 *         统一状态词投影、可视化点会话回写值与选中联动数据的**数据面**。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Render.hpp 行——T11 落位，
 *     本头随任务卡 §14.6 v0.11 登记）、§9.8（"结果与可视化面板"行——
 *     "失败点/薄弱区/碰撞对象/最差裕量**渲染数据**（KIN-07——数据本单元
 *     产、呈现经 ui View3D/绘图契约）"；L-K1 选中联动行）、§4.5（身份外
 *     元素表"会话姿态"行——可视化点回写零修订/零失效/不入缓存身份）、
 *     §6.2/§6.3（解集视图 worstBy——最差项规范来源）、§7.3（KIN-09/10/11
 *     R2 边界）、§7.1（批量 per-item 状态——NotApplicable/NotRun 语义）
 *   - 需求 KIN-07（三维视图显示失败点、薄弱区、碰撞对象和最差关节裕量）、
 *     KIN-06（回写只改会话姿态，不修改设计模型、不触发结果失效）、
 *     UX-10（七种公共状态统一呈现——ui 侧消费本头投影的 core 状态词）
 *   - 任务契约 tasks/foundation/WP-15-T11.json（acceptance 1~4 全条）
 *
 * 背景说明（第一读者须知——本头在 KIN-07 链路中的位置）：
 *   KIN-07 的分工是"**数据本单元产、呈现归 ui**"（§9.8 面板行原文）：
 *   三维标记怎么画（几何/颜色/图例/拾取）全部归 ui View3D（WP-15-T12
 *   插件面板消费本头数据）；本头只交付渲染的**数据面**——四类渲染数据
 *   的值结构与"从既有计算产物组装渲染数据"的唯一组装点：
 *     - 失败点 ←—— WP-15-T05 批量 per-item 记录（BatchComputation，
 *       Evidence.hpp）中的失败工作项（§7.1 per-item 状态消费）；
 *     - 薄弱区 ←—— 解的归一化关节裕量/条件数（KinematicSolution，
 *       D-KIN-2/6）对照**策略阈值**（policy::JointThresholds——P-POL-2
 *       显式不适用语义，本头不发明默认阈值）；
 *     - 碰撞对象对 ←—— 解的 CollisionStatus.objectIdPairs 与硬过滤
 *       碰撞记录（T07 碰撞链路的诊断明细消费）；
 *     - 最差关节裕量 ←—— **只经** IKinematicSolutionSet::worstBy
 *       （T09 设施复用，§6.2"最差排序的规范来源"）——本头不另设任何
 *       解排序/最差选择（NFR-MNT-04：UI/报告不各自排序）。
 *   两类组装通道对应两条既有数据链：
 *     - 解集通道（assembleSolutionSetRenderData）：单点求解/归档解集视图
 *       （IKinematicSolutionSet——T04/T09 设施）；
 *     - 批量通道（assembleBatchRenderData）：批量任务点验证运行记录
 *       （BatchComputation——T05 设施；逐项最佳解为唯一解级评估对象——
 *       批量记录不携带全解集，跨项"最差"不另设排序，见函数注）。
 *
 * 红线自查锚点（review 对照）：
 *   - 对象标注一律 ObjectId（R-4）：本头所有对象字段都是 core::ObjectId，
 *     **零显示名字段**——显示名由 ui 经其 nameResolver 端口解析（ui.md
 *     C-11，解析权威归 runtime），本单元不拼接/不缓存任何名称；
 *   - 状态标识不自造状态词（acceptance 2）：渲染状态＝core TaskState/
 *     TaskOutcome 双轴投影（core.md §4.7 词表；ui 七态投影 UX-10 消费
 *     同一词表——KIN-07/UX-10 边界）；BatchItemStatus 等域内枚举只作
 *     "失败种类"**明细素材**随行，不是渲染状态词；
 *   - 回写仅会话姿态（KIN-06/acceptance 3）：RenderPointWriteback 结构性
 *     只携带关节向量与构型签名（记录键），零端口/零身份字段——与
 *     KinSessionPose（T08，Commands.hpp）同源纪律；不存在产生修订/
 *     失效/入缓存身份的通道（V-18）；
 *   - KIN-09 边界（acceptance 4）：工作空间点云/投影/PNG 导出是 R2
 *     （WP-15-T14）——本头不做任何提前实现或占位桩。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态）；KinSessionPose
 *   的线程约束（仅 UI 线程）见其头注，本头回写入口沿用该约束。
 * 确定性：全部组装函数同输入→同输出（固定遍历序＋全序比较——NFR-COR-01/
 *   02）；浮点比较为直接比较（阈值判定语义见 WeakZoneKind 注——边界值
 *   归合规侧，D-08 口径），无容差。
 */

#ifndef IRD_KINEMATICS_RENDER_HPP
#define IRD_KINEMATICS_RENDER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/core/Evaluation.hpp>      // TaskState/TaskOutcome/toToken（统一状态词——acceptance 2）
#include <sdurws/ird/core/Identity.hpp>        // ObjectId（对象标注唯一形态——R-4）
#include <sdurws/ird/kinematics/Commands.hpp>  // KinSessionPose（回写唯一写点——KIN-06）
#include <sdurws/ird/kinematics/Evidence.hpp>  // BatchComputation/BatchWorkItemRecord/BatchItemStatus/BatchTaskPoint（T05 消费面）
#include <sdurws/ird/kinematics/KinTypes.hpp>  // KinematicSolution/FilteredSolutionRecord（解级指标源）
#include <sdurws/ird/kinematics/SolutionSet.hpp>  // IKinematicSolutionSet/SolutionRef/WorstMetric（worstBy——T09 设施）
#include <sdurws/ird/policy/PolicySet.hpp>     // policy::JointThresholds（阈值唯一来源——P-POL-2）

namespace sdurws::ird::kinematics {

// =====================================================================
// 渲染状态投影（acceptance 2——统一状态词；§9.8 L-K12/KIN-07-UX-10 边界）
// =====================================================================

/**
 * @brief 渲染条目的状态标识（core 词表双轴投影——**不自造状态词**）。
 *
 * 背景（为什么是"投影"而不是新枚举）：ui 的公共状态呈现（UX-10 七态、
 * 任务徽标 L-K12）以 core TaskState/TaskOutcome 词表为统一消费面；若
 * 渲染数据另造一套"渲染态"词，ui 就要做第二套映射（漂移面）。因此本
 * 结构只有两个 core 词：
 *   - taskState：状态机轴（九态——运行通道的如实状态，例如批量运行的
 *     任务态 Completed/Canceled/Interrupted）；
 *   - taskOutcome：信封结果轴（四值——条目级结果投影，失败点恒 Failed）。
 * 两轴正交（core §4.7 不变量：终态同名但属两个轴）。
 *
 * 域内明细（如 BatchItemStatus 七值）**不在**本结构——它作为"失败种类"
 * 素材字段随渲染条目携带（T05 per-item 状态消费，acceptance 3），二者
 * 分工：core 词供 ui 统一状态呈现，域枚举供检查器解释失败原因。
 *
 * token 形态：图例/持久化键一律经 renderTaskStateToken/
 * renderTaskOutcomeToken（core §4.7 冻结 token 表直读——零新 token）。
 * 值语义纯结构；线程安全。
 */
struct RenderStateProjection {
    /// 状态机轴词（core 九态——来源运行通道的如实状态）。
    core::TaskState taskState = core::TaskState::Completed;
    /// 信封结果轴词（core 四值——条目级结果投影）。
    core::TaskOutcome taskOutcome = core::TaskOutcome::Completed;

    bool operator==(const RenderStateProjection& o) const noexcept
    {
        return taskState == o.taskState && taskOutcome == o.taskOutcome;
    }
    bool operator!=(const RenderStateProjection& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 状态机轴 token（core::toToken 直读——图例/持久化键形态）。
 * @param p [in] 渲染状态投影
 * @return core 冻结 token（如 "completed"/"canceled"——core §4.7 表；
 *         零新词）
 *
 * 纯函数；线程安全；不抛。
 */
inline std::string_view renderTaskStateToken(const RenderStateProjection& p) noexcept
{
    return core::toToken(p.taskState);
}

/**
 * @brief 结果轴 token（core::toToken 直读——图例/持久化键形态）。
 * @param p [in] 渲染状态投影
 * @return core 冻结 token（"completed"/"canceled"/"failed"/"interrupted"）
 *
 * 纯函数；线程安全；不抛。
 */
inline std::string_view renderTaskOutcomeToken(const RenderStateProjection& p) noexcept
{
    return core::toToken(p.taskOutcome);
}

/**
 * @brief 批量工作项 per-item 状态→core 结果轴投影（T05 状态消费的唯一
 *        映射点——KIN-07/UX-10 边界的实现处）。
 *
 * 映射口径（§7.1 per-item 语义对照 core 结果轴）：
 *   - CandidateFound → Completed（计算完成且得到候选——可行性素材在册）；
 *   - NoConvergence/AllFiltered/BoundExceeded → Failed（计算完成但未得
 *     可用解——搜索未果/界限素材；渲染为失败点，acceptance 1）；
 *   - InputInvalid → Failed（输入非法素材——悬空引用等；渲染为失败点，
 *     其"失败"是数据错误的如实呈现，ERR-01）；
 *   - NotApplicable/NotRun → nullopt（**不渲染为失败**——acceptance 3
 *     显式要求：显式不适用与未运行不是失败，伪造即为 ERR-01 违例；调用
 *     方据 nullopt 把这两态排除出失败点集并单独计数）。
 *
 * @param status [in] T05 批量工作项计算状态（七值）
 * @return 结果轴投影；NotApplicable/NotRun → nullopt（排除信号）
 *
 * 纯函数；线程安全；不抛。
 */
std::optional<core::TaskOutcome> projectBatchItemOutcome(BatchItemStatus status) noexcept;

// =====================================================================
// 薄弱区词表与策略阈值投影（acceptance 1——阈值读 policy；P-POL-2）
// =====================================================================

/**
 * @brief 薄弱区种类（KIN-07 明文两类——近限位/近奇异）。
 *
 * 这是**域事实词**（标记哪种薄弱），不是状态词（状态见
 * RenderStateProjection）——渲染图例按本词表分类着色，状态徽标走 core
 * 词表投影，二者不混用。
 */
enum class WeakZoneKind : std::uint8_t {
    /// 近限位：解的某关节归一化裕量 r 严格小于策略 nearLimitRatio 阈值
    /// （policy §7.4 同则语义——r＝距限位余量/区间半宽，D-KIN-6 同口径；
    /// r＝阈值不警告，边界含于合规侧 D-08）。
    NearJointLimit,
    /// 近奇异：解的条件数严格大于策略 conditionNumberWarning 阈值（D-KIN-2
    /// 条件数；等于阈值不警告——D-08 同则；conditionNumber＝+∞（奇异）
    /// 恒命中）。
    NearSingular,
};

/**
 * @brief 薄弱区判定的策略阈值投影（从 policy::JointThresholds 取数的
 *        值载体——组装函数的解耦入参）。
 *
 * 为什么不直接传 EngineeringPolicySet：组装函数只需要两个阈值槽位；
 * 收窄入参使测试无须构造完整发布策略对象（发布门校验重）、也使"阈值
 * 唯一来源＝policy"的读取点收敛到 weakZoneThresholdsOf 单个函数。
 *
 * P-POL-2 保守口径（不发明数值）：nullopt＝该警告检查**显式不适用**
 * （策略未设置且无冻结默认）——对应种类的薄弱区条目恒空集并由
 * WeakZonesRenderData.applicable 标记承载"不适用"事实，绝不伪造成
 * "无薄弱"或编造默认阈值。值语义纯结构；线程安全。
 */
struct WeakZoneThresholds {
    /// 近限位比阈值（无量纲，(0,1]；nullopt＝检查显式不适用——P-POL-2）。
    std::optional<double> nearLimitRatio;
    /// 条件数警告阈值（无量纲，[1,+∞)；nullopt＝同上）。
    std::optional<double> conditionNumberWarning;

    bool operator==(const WeakZoneThresholds& o) const noexcept
    {
        return nearLimitRatio == o.nearLimitRatio
            && conditionNumberWarning == o.conditionNumberWarning;
    }
    bool operator!=(const WeakZoneThresholds& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 从已发布策略的关节阈值子模型投影薄弱区阈值（"阈值读 policy"的
 *        **唯一读取点**——acceptance 1）。
 *
 * @param jointThresholds [in] 已发布策略的 jointThresholds 子模型
 *                        （resolvedPolicy.jointThresholds——policy §4.4；
 *                        阈值槽位均经 PolicyThreshold::make 域校验，
 *                        本函数直取 siValue，零二次校验零换算）
 * @return 阈值投影（optional 槽位原样透传——nullopt＝显式不适用）
 *
 * 纯函数；线程安全；不抛。
 */
WeakZoneThresholds weakZoneThresholdsOf(const policy::JointThresholds& jointThresholds) noexcept;

// =====================================================================
// 四类渲染数据结构（acceptance 1——对象标注一律 ObjectId，R-4）
// =====================================================================

/**
 * @brief 失败点渲染条目（四类之一——批量验证中未得到可用解的工作项）。
 *
 * 失败点集合的口径（acceptance 1/3）：status∈{NoConvergence, AllFiltered,
 * BoundExceeded, InputInvalid} 的工作项——NotApplicable/NotRun **不在**
 * 失败点集合（如实区分，acceptance 3；排除量由 FailurePointsRenderData
 * 计数字段承载）。
 *
 * 坐标系与单位：targetInBase 为基座系 {B} 的 TCP 目标位姿（平移 m、旋转
 * rad——宿主解析投影 BatchTaskPoint.targetInBase 原值；{B} 定义见
 * runtime 卡 §4.3）。nullopt＝无可渲染位置（点对象悬空——InputInvalid
 * 的悬空点项在 resolvedPoints 中无解析值；如实缺位不虚构坐标，ARC-04）。
 *
 * 对象标注（R-4）：pointOid/conditionId 是仅有的对象引用，显示名由 ui
 * nameResolver 解析（本单元零名称字段）。
 * 值语义纯结构；线程安全。
 */
struct FailurePointRenderItem {
    /// 任务点对象身份（§5.6 绑定）。
    core::ObjectId pointOid;
    /// 工况对象身份（批量通道必填）。
    core::ObjectId conditionId;
    /// 失败种类（T05 per-item 状态原值——明细素材，非渲染状态词；
    /// 恒为四失败态之一，见结构注）。
    BatchItemStatus failureKind = BatchItemStatus::NoConvergence;
    /// 批量记录全序下标（computation.items 序——§8.4；检查器跳转定位，
    /// L-K1 联动键）。
    std::uint64_t workItemIndex = 0;
    /// 渲染状态投影（taskOutcome 恒 Failed——失败点定义；taskState 随
    /// 来源运行通道）。
    RenderStateProjection state;
    /// 原因素材（记录 reason 原样透传——T05 不变式：仅预终结态非空；
    /// 其余失败态的失败解释经 workItemIndex 跳转检查器读搜索未果/证明
    /// 素材，本字段不重复拼接文案）。
    std::string reason;
    /// 目标位姿（基座系 {B}；nullopt＝无可渲染位置——见结构注）。
    std::optional<rw::math::Transform3D<double>> targetInBase;

    bool operator==(const FailurePointRenderItem& o) const
    {
        return pointOid == o.pointOid && conditionId == o.conditionId
            && failureKind == o.failureKind && workItemIndex == o.workItemIndex
            && state == o.state && reason == o.reason
            && targetInBase == o.targetInBase;
    }
    bool operator!=(const FailurePointRenderItem& o) const { return !(*this == o); }
};

/**
 * @brief 失败点渲染数据（批量通道产出——assembleBatchRenderData 的成员）。
 *
 * 守恒不变式（组装器保证，acceptance 3 的自证面）：points.size() +
 * candidateFoundCount + notApplicableItemCount + notRunItemCount ==
 * computation.items.size()——每个工作项恰归入一类（失败点/有解/显式
 * 不适用/未运行），零静默丢弃（ERR-01）。
 * 值语义纯结构；线程安全。
 */
struct FailurePointsRenderData {
    /// 失败点条目（computation.items 全序——(pointOid, conditionId) 字典序，
    /// §8.4；确定性序）。
    std::vector<FailurePointRenderItem> points;
    /// 有解工作项数（CandidateFound——非失败，不入失败点）。
    std::uint64_t candidateFoundCount = 0;
    /// 显式不适用工作项数（NotApplicable——**未渲染为失败**的排除计数，
    /// acceptance 3 如实区分面）。
    std::uint64_t notApplicableItemCount = 0;
    /// 未运行工作项数（NotRun——分批取消/失败；同上，排除计数）。
    std::uint64_t notRunItemCount = 0;

    bool operator==(const FailurePointsRenderData& o) const
    {
        return points == o.points && candidateFoundCount == o.candidateFoundCount
            && notApplicableItemCount == o.notApplicableItemCount
            && notRunItemCount == o.notRunItemCount;
    }
    bool operator!=(const FailurePointsRenderData& o) const { return !(*this == o); }
};

/**
 * @brief 薄弱区渲染条目（四类之二——近限位/近奇异）。
 *
 * 判定语义（D-08 边界含于合规侧——严格比较）：
 *   - NearJointLimit：margin[i] < nearLimitRatio（严格小于；连续关节
 *     margin＝+∞ 恒不命中）；value＝命中关节的归一化裕量（无量纲），
 *     threshold＝策略 nearLimitRatio（同量纲——value/threshold 同轴
 *     直接可比，呈现侧可据此显示"距阈值余量"）；
 *   - NearSingular：conditionNumber > conditionNumberWarning（严格大于；
 *     +∞ 恒命中）；value＝解的条件数（无量纲），threshold＝策略阈值。
 *
 * 对象标注（R-4）：jointObject＝命中关节对象身份（链序第 jointIndex 个
 * 关节——宿主注入的链序 ObjectId 列）；近奇异为模型级事实（无单关节
 * 归属），jointObject 为全零保留值（core::ObjectId{}——"未绑定"语义，
 * 与 IkTargetRef.pointOid 保留值同规）。
 * 值语义纯结构；线程安全。
 */
struct WeakZoneRenderItem {
    /// 薄弱种类。
    WeakZoneKind kind = WeakZoneKind::NearJointLimit;
    /// 任务点绑定（解集通道＝组装入参 pointOid；批量通道＝工作项点）。
    core::ObjectId pointOid;
    /// 工况绑定（批量通道＝工作项工况；解集通道＝组装入参，单点求解
    /// 为全零保留值）。
    core::ObjectId conditionId;
    /// 命中关节对象身份（近限位＝链序命中关节；近奇异＝全零保留值——
    /// 见结构注）。
    core::ObjectId jointObject;
    /// 命中关节的链序下标（计数，0 基；近奇异＝全零保留值语义的下标
    /// 恒 0——消费方以 kind 判读，不以下标判读）。
    std::size_t jointIndex = 0;
    /// 实际值（近限位：归一化裕量，无量纲；近奇异：条件数，无量纲——
    /// 语义随 kind 固定）。
    double value = 0.0;
    /// 策略阈值（与 value 同量纲同轴——比较语义见结构注）。
    double threshold = 0.0;
    /// 解定位：解集通道＝sorted() 序下标；批量通道＝nullopt（批量逐项
    /// 最佳解无解集序——其解级定位经 workItemIndex）。
    std::optional<std::size_t> solutionIndex;
    /// 批量工作项定位（批量通道＝items 全序下标；解集通道＝nullopt）。
    std::optional<std::uint64_t> workItemIndex;

    bool operator==(const WeakZoneRenderItem& o) const
    {
        return kind == o.kind && pointOid == o.pointOid && conditionId == o.conditionId
            && jointObject == o.jointObject && jointIndex == o.jointIndex
            && value == o.value && threshold == o.threshold
            && solutionIndex == o.solutionIndex && workItemIndex == o.workItemIndex;
    }
    bool operator!=(const WeakZoneRenderItem& o) const { return !(*this == o); }
};

/**
 * @brief 薄弱区渲染数据（两通道共用的承载——含阈值在场标记）。
 *
 * applicable 双标记（P-POL-2 显式不适用的呈现面）：false＝策略未设置
 * 对应阈值——该种类条目恒空且 ui 应呈现"检查不适用"（证据缺失≠通过，
 * KIN-05 同源保守读法）；true＝阈值在场，空集＝真实无薄弱。
 * 值语义纯结构；线程安全。
 */
struct WeakZonesRenderData {
    /// 薄弱区条目（确定性序：解集通道按 sorted() 解序→先近限位（链序）
    /// 后近奇异的逐解序；批量通道按工作项全序→逐项最佳解同则）。
    std::vector<WeakZoneRenderItem> zones;
    /// 近限位检查在场（策略 nearLimitRatio 已设置——P-POL-2）。
    bool nearLimitApplicable = false;
    /// 近奇异检查在场（策略 conditionNumberWarning 已设置）。
    bool nearSingularApplicable = false;

    bool operator==(const WeakZonesRenderData& o) const
    {
        return zones == o.zones && nearLimitApplicable == o.nearLimitApplicable
            && nearSingularApplicable == o.nearSingularApplicable;
    }
    bool operator!=(const WeakZonesRenderData& o) const { return !(*this == o); }
};

/**
 * @brief 碰撞对象对渲染条目（四类之三——KIN-05 碰撞诊断明细的呈现面）。
 *
 * 来源（两通道一致）：解的 CollisionStatus.objectIdPairs（inCollision=
 * true 时非空——成对展平 [a1,b1,…]）与硬过滤碰撞记录
 * （FilteredSolutionRecord，reason==Collision）的对象对。对象对保持
 * policy 会话规范序（A<B——T07 装配语义，本头零改写）。
 *
 * 定位三键（L-K1 检查器跳转——按来源恰一组非空）：
 *   - 解集通道解内碰撞：solutionIndex；
 *   - 解集通道过滤记录：filteredRecordIndex；
 *   - 批量通道：workItemIndex＋（解内/过滤记录同两键）。
 * 值语义纯结构；线程安全。
 */
struct CollisionPairRenderItem {
    /// 碰撞对象 A（policy 规范序在前——ObjectId，R-4）。
    core::ObjectId objectA;
    /// 碰撞对象 B（规范序在后）。
    core::ObjectId objectB;
    /// 命中构型签名（记录键——I-KIN-3；非身份，仅溯源呈现）。
    std::string configurationSignature;
    /// 任务点绑定（语义同 WeakZoneRenderItem.pointOid）。
    core::ObjectId pointOid;
    /// 工况绑定（语义同 WeakZoneRenderItem.conditionId）。
    core::ObjectId conditionId;
    /// 来源标记（true＝硬过滤碰撞记录的诊断解；false＝解集内碰撞解）。
    bool fromFilteredRecord = false;
    /// 解集序定位（解内碰撞时非空——sorted() 序下标）。
    std::optional<std::size_t> solutionIndex;
    /// 过滤记录定位（诊断解时非空——record 在 FilteredSolutionRecord
    /// 序中的下标）。
    std::optional<std::size_t> filteredRecordIndex;
    /// 批量工作项定位（批量通道非空——items 全序下标）。
    std::optional<std::uint64_t> workItemIndex;

    bool operator==(const CollisionPairRenderItem& o) const
    {
        return objectA == o.objectA && objectB == o.objectB
            && configurationSignature == o.configurationSignature
            && pointOid == o.pointOid && conditionId == o.conditionId
            && fromFilteredRecord == o.fromFilteredRecord
            && solutionIndex == o.solutionIndex
            && filteredRecordIndex == o.filteredRecordIndex
            && workItemIndex == o.workItemIndex;
    }
    bool operator!=(const CollisionPairRenderItem& o) const { return !(*this == o); }
};

/**
 * @brief 最差关节裕量渲染条目（四类之四——KIN-07"最差关节裕量"标记）。
 *
 * ★ 来源纪律（acceptance 1——不另设排序）：本条目**只**由
 * IKinematicSolutionSet::worstBy(WorstMetric::MinimumJointMargin) 产出
 * （§6.2"最差排序的规范来源：裕量最小"——T09 设施复用）；本单元不写
 * 任何第二套"找最差解"的比较/排序。批量记录不携带全解集，因此批量
 * 通道**不产出**跨项最差条目（逐项裕量随薄弱区/失败点素材交付；对
 * 归档解集调 assembleSolutionSetRenderData 即得最差项）。
 *
 * jointObject 取 arg-min 关节（解的 jointMargins 中最小者——链序首个
 * 达最小值者，平局确定性）；全部关节无界（minimumJointMargin＝+∞）时
 * arg-min 无定义，jointObject/jointIndex 为全零/0 保留值（如实承载
 * "+∞ 裕量"事实）。
 * 值语义纯结构；线程安全。
 */
struct WorstJointMarginRenderItem {
    /// 任务点绑定（语义同 WeakZoneRenderItem.pointOid）。
    core::ObjectId pointOid;
    /// arg-min 关节对象身份（链序 ObjectId；全无界＝全零保留值——见结构注）。
    core::ObjectId jointObject;
    /// arg-min 关节链序下标（0 基；全无界＝0 保留值）。
    std::size_t jointIndex = 0;
    /// 最差（最小）归一化关节裕量（无量纲——D-KIN-6；全无界＝+∞）。
    double minimumJointMargin = 0.0;
    /// 最差解的构型签名（记录键——I-KIN-3）。
    std::string configurationSignature;
    /// 最差解在 sorted() 序中的下标（worstBy 产出的 SolutionRef 值——
    /// 解检查器/三维高亮共用定位，L-K1）。
    std::size_t solutionIndex = 0;

    bool operator==(const WorstJointMarginRenderItem& o) const
    {
        return pointOid == o.pointOid && jointObject == o.jointObject
            && jointIndex == o.jointIndex && minimumJointMargin == o.minimumJointMargin
            && configurationSignature == o.configurationSignature
            && solutionIndex == o.solutionIndex;
    }
    bool operator!=(const WorstJointMarginRenderItem& o) const { return !(*this == o); }
};

// =====================================================================
// 解集通道组装（Face A——IKinematicSolutionSet 视图消费；worstBy 唯一
// 最差来源）
// =====================================================================

/**
 * @brief 解集通道渲染数据（assembleSolutionSetRenderData 的产出值——
 *        薄弱区/最差裕量/碰撞对象对三类；失败点属批量通道不在本值）。
 * 值语义纯结构；线程安全。
 */
struct SolutionSetRenderData {
    /// 薄弱区（逐解评估——序见 WeakZonesRenderData.zones 注）。
    WeakZonesRenderData weakZones;
    /// 最差关节裕量（worstBy(WorstMetric::MinimumJointMargin) 产物；
    /// nullopt＝空解集——无解不虚构）。
    std::optional<WorstJointMarginRenderItem> worstJointMargin;
    /// 碰撞对象对（解内碰撞解＋过滤碰撞诊断解——确定性序见函数注）。
    std::vector<CollisionPairRenderItem> collisionPairs;

    bool operator==(const SolutionSetRenderData& o) const
    {
        return weakZones == o.weakZones && worstJointMargin == o.worstJointMargin
            && collisionPairs == o.collisionPairs;
    }
    bool operator!=(const SolutionSetRenderData& o) const { return !(*this == o); }
};

/**
 * @brief 从解集视图组装渲染数据（KIN-07 解集通道唯一组装点——单点求解
 *        面板/归档解集消费）。
 *
 * 组装序（固定——确定性，NFR-COR-02）：
 *   1. 薄弱区：按 set.sorted() 解序逐解评估——每解先近限位（命中关节
 *      链序升序）、后近奇异（至多一条）；阈值不在场的种类整体跳过
 *      （P-POL-2——applicable 标记如实为 false）；
 *   2. 最差裕量：set.worstBy(WorstMetric::MinimumJointMargin)（T09 设施
 *      ——本函数零自设比较）；空集 → nullopt；
 *   3. 碰撞对象对：先解内（sorted() 序，evaluated∧inCollision，对象对
 *      按展平序拆对），后过滤记录（filteredRecords 序，reason==
 *      Collision）。
 *
 * 错误分轨（§9.1：调用方错误 fail-fast；本函数无环境出口）：
 *   - chainJointObjects.size() 与解的 jointMargins 维度不符＝调用方
 *     契约违约 → std::invalid_argument（R-4 关节标注无从谈起，不静默
 *     截断）；
 *   - 解指标含 NaN（margin/conditionNumber——求解器契约排除值）→
 *     std::invalid_argument（NFR-COR-03 非有限拒绝；+∞ 合法——连续
 *     关节裕量/奇异条件数的既定承载值）。
 *
 * @param solutionSet       [in] 解集只读视图（构造时已稳定排序——T04/T09
 *                          设施；调用方保证存活至返回）
 * @param pointOid          [in] 绑定任务点（视图接口不暴露 targetRef，
 *                          绑定由调用方供给——单点求解可为全零保留值）
 * @param conditionId       [in] 绑定工况（批量归档解集填实值；单点求解
 *                          缺省＝全零保留值"未绑定"）
 * @param chainJointObjects [in] 链序关节对象身份（下标 i＝第 i 关节——
 *                          宿主自 CanonicalModel 主链 joints[i].objectId
 *                          投影；维度须与解一致——见错误分轨）
 * @param thresholds        [in] 薄弱区阈值（weakZoneThresholdsOf 的产物
 *                          ——阈值唯一来源 policy，本函数不读第二来源）
 * @return 渲染数据（同输入同值）
 *
 * @throws std::invalid_argument 关节对象列维度不符／解指标含 NaN
 *
 * 纯函数（零输入改写、零副作用）；线程安全；确定性。
 */
SolutionSetRenderData
assembleSolutionSetRenderData(const IKinematicSolutionSet& solutionSet,
                              const core::ObjectId& pointOid,
                              const core::ObjectId& conditionId,
                              const std::vector<core::ObjectId>& chainJointObjects,
                              const WeakZoneThresholds& thresholds);

// =====================================================================
// 批量通道组装（Face B——BatchComputation 消费；T05 per-item 状态）
// =====================================================================

/**
 * @brief 批量通道渲染数据（assembleBatchRenderData 的产出值——失败点/
 *        薄弱区/碰撞对象对三类；最差裕量归解集通道，见
 *        WorstJointMarginRenderItem 注）。
 * 值语义纯结构；线程安全。
 */
struct BatchRenderData {
    /// 失败点（含排除计数——守恒不变式见 FailurePointsRenderData 注）。
    FailurePointsRenderData failurePoints;
    /// 薄弱区（逐项最佳解评估——只覆盖 CandidateFound 项；失败项无解
    /// 无薄弱可言，NotApplicable/NotRun 不评估）。
    WeakZonesRenderData weakZones;
    /// 碰撞对象对（逐项最佳解碰撞对＋逐项过滤碰撞记录——确定性序）。
    std::vector<CollisionPairRenderItem> collisionPairs;

    bool operator==(const BatchRenderData& o) const
    {
        return failurePoints == o.failurePoints && weakZones == o.weakZones
            && collisionPairs == o.collisionPairs;
    }
    bool operator!=(const BatchRenderData& o) const { return !(*this == o); }
};

/**
 * @brief 从批量计算记录组装渲染数据（KIN-07 批量通道唯一组装点——T05
 *        per-item 状态消费）。
 *
 * 组装口径（acceptance 1/3 逐条）：
 *   - 失败点：仅四失败态工作项入集（projectBatchItemOutcome 映射）；
 *     NotApplicable/NotRun 排除并单独计数（如实区分）；守恒不变式
 *     （FailurePointsRenderData 注）在组装器内自检，破坏即
 *     std::logic_error（内部实现缺陷不静默——T05 组装器同则）；
 *   - 位置解析：失败点的 targetInBase 自 resolvedPoints 按 pointOid
 *     查找（首个命中——调用方保证 pointOid 在投影内唯一，req 闭包
 *     集合语义）；查无＝无可渲染位置（nullopt——悬空点如实缺位）；
 *   - 状态投影：taskOutcome＝projectBatchItemOutcome（恒 Failed）；
 *     taskState＝sourceTaskState（来源运行通道的任务态——ui 自
 *     ITaskPresentationModel 取得后传入，本单元不读执行通道，L-K12
 *     边界）；
 *   - 薄弱区/碰撞对：只评估 CandidateFound 项的 bestSolution（批量
 *     记录的唯一解级对象——稳定排序首位，§7.1）；过滤记录随项携带
 *     （T05 记录的 filteredRecords）。
 *
 * 错误分轨：
 *   - 调用方契约违约（维度不符/NaN 指标）→ std::invalid_argument
 *     （同 Face A）；
 *   - computation 违反 T05 不变式（items 序/守恒）→ std::logic_error
 *     （内部缺陷面——同 IKinematicEvidenceBuilder.build 口径）。
 *
 * @param computation       [in] 批量计算结果（冻结值——T05 评估器产出）
 * @param resolvedPoints    [in] 宿主解析的任务点投影（同批注入值——
 *                          位置来源；pointOid 须唯一）
 * @param chainJointObjects [in] 链序关节对象身份（语义同 Face A）
 * @param thresholds        [in] 薄弱区阈值（weakZoneThresholdsOf 产物）
 * @param sourceTaskState   [in] 来源运行通道任务态（core 九态——状态机
 *                          轴投影值；ui 经 L-K12 通道供给）
 * @return 渲染数据（同输入同值）
 *
 * @throws std::invalid_argument 维度不符／NaN 指标；std::logic_error
 *         computation 不变式破坏
 *
 * 纯函数；线程安全；确定性。
 */
BatchRenderData
assembleBatchRenderData(const BatchComputation& computation,
                        const std::vector<BatchTaskPoint>& resolvedPoints,
                        const std::vector<core::ObjectId>& chainJointObjects,
                        const WeakZoneThresholds& thresholds,
                        core::TaskState sourceTaskState);

// =====================================================================
// 可视化点会话回写（acceptance 3——KIN-06/AT-04/V-18；§4.5 身份外清单）
// =====================================================================

/**
 * @brief 可视化点的会话回写值（L-K4"可视化点回写"的渲染数据面出口）。
 *
 * 结构性保证（V-18 同源断言——与 KinSessionPose 同款纪律）：本结构**只有**
 * 两个成员（权威关节向量＋构型签名记录键），零端口/零身份字段——把
 * 一个回写值送入会话在类型层面**不存在**产生修订、触发失效或进入缓存
 * 身份的通道：
 *   - 零修订/零失效：唯一消费口 writebackToSessionPose 只调用
 *     KinSessionPose::setJointConfiguration（T08 纯值容器——其类注的
 *     "零端口"结构性保证原样继承）；
 *   - 不入缓存身份：构型签名是记录键（I-KIN-3），不是任何评估请求身份
 *     的字段（IkRequestIdentity 由显式输入构成，D-KIN-4）——回写动作
 *     不触碰 sliceId/configDigest 等身份面。
 *
 * 单位：q 逐自由度 SI（转动 rad／移动 m，§5.1 链序）。
 * 值语义纯结构；线程安全。
 */
struct RenderPointWriteback {
    /// 权威关节向量（rad／m；链序——写入会话姿态的值）。
    std::vector<double> q;
    /// 构型签名（记录键——回写溯源显示用；不入会话、不入身份）。
    std::string configurationSignature;

    bool operator==(const RenderPointWriteback& o) const
    {
        return q == o.q && configurationSignature == o.configurationSignature;
    }
    bool operator!=(const RenderPointWriteback& o) const { return !(*this == o); }
};

/**
 * @brief 从可行解提取回写值（候选/薄弱区/最差裕量条目的回写出口——
 *        双击候选＝KIN-06 原文场景）。
 * @param solution [in] 可行解（渲染条目的源解）
 * @return 回写值（q＝solution.q 副本；签名＝solution.signature）
 *
 * 纯函数；线程安全；不抛。
 */
RenderPointWriteback writebackOf(const KinematicSolution& solution);

/**
 * @brief 从硬过滤诊断解提取回写值（碰撞过滤解也可回写检查——T09"解∪
 *        过滤记录合并诊断呈现"的会话面延伸；记录携 q 原值）。
 * @param record [in] 被过滤解记录（诊断价值承载）
 * @return 回写值（q＝record.q 副本；签名＝record.signature）
 *
 * 纯函数；线程安全；不抛。
 */
RenderPointWriteback writebackOf(const FilteredSolutionRecord& record);

/**
 * @brief 把回写值写入 ui 会话姿态（L-K4 渲染面板回写的唯一写点）。
 *
 * 语义＝session.setJointConfiguration(writeback.q)（T08 写入口直调——
 * 单一写点防调用方绕过会话容器私设姿态）；构型签名**不**写入会话
 * （记录键非会话语义——防把溯源键误当状态）。零修订、零失效、不入
 * 缓存身份的结构性保证随 KinSessionPose（其类注）与本值类型（结构注）。
 *
 * 线程约束：仅 UI 线程调用（KinSessionPose 非线程安全——ARCH §7.7）。
 *
 * @param session   [in,out] ui 会话姿态容器（ui 持有——本单元不登记
 *                  不持久化）
 * @param writeback [in] 回写值
 *
 * @throws std::invalid_argument writeback.q 含非有限分量（KinSessionPose
 *         写入口的既定契约——NFR-COR-03）
 */
void writebackToSessionPose(KinSessionPose& session, const RenderPointWriteback& writeback);

// =====================================================================
// 选中联动数据（acceptance 4——L-K1；SelectionModel 消费面，呈现归 ui）
// =====================================================================

/**
 * @brief 失败点选中联动数据（L-K1"失败点选中→检查器/三维高亮"的本单元
 *        供给面——ui SelectionModel 消费本值后驱动检查器跳转与三维高亮；
 *        呈现行为全部归 ui）。
 *
 * 定位双键：targetInBase（三维高亮的场景定位——nullopt＝无可定位，ui
 * 仅在检查器清单呈现）＋workItemIndex（检查器跳转——批量记录全序下标，
 * 检查器据此读搜索未果/证明素材明细）。
 * 值语义纯结构；线程安全。
 */
struct FailurePointSelectionLink {
    /// 任务点对象身份（高亮对象标注——显示名归 ui nameResolver，R-4）。
    core::ObjectId pointOid;
    /// 工况对象身份。
    core::ObjectId conditionId;
    /// 失败种类（如实状态——T05 per-item 原值；检查器解释用）。
    BatchItemStatus failureKind = BatchItemStatus::NoConvergence;
    /// 原因素材（记录 reason 透传——语义同 FailurePointRenderItem.reason）。
    std::string reason;
    /// 渲染状态投影（语义同 FailurePointRenderItem.state）。nullopt＝该
    /// 工作项为预终结态（NotApplicable/NotRun）——无结果轴投影，ui 以
    /// "未运行/不适用"中性呈现、不落失败态（acceptance 3 的选中面延伸）；
    /// 失败点条目恒有投影（taskOutcome=Failed）。
    std::optional<RenderStateProjection> state;
    /// 目标位姿（基座系 {B}；nullopt＝无可定位——语义同渲染条目）。
    std::optional<rw::math::Transform3D<double>> targetInBase;
    /// 批量记录全序下标（检查器跳转键）。
    std::uint64_t workItemIndex = 0;

    bool operator==(const FailurePointSelectionLink& o) const
    {
        return pointOid == o.pointOid && conditionId == o.conditionId
            && failureKind == o.failureKind && reason == o.reason && state == o.state
            && targetInBase == o.targetInBase && workItemIndex == o.workItemIndex;
    }
    bool operator!=(const FailurePointSelectionLink& o) const { return !(*this == o); }
};

/**
 * @brief 取批量记录中某工作项的失败点选中联动数据（L-K1 数据面——
 *        对**任意**工作项如实供给：选中非失败项时 failureKind/state
 *        携带其真实状态，由 ui 决定呈现；本函数不做失败态过滤——过滤
 *        语义在失败点集合的组装口径，不在选中面）。
 *
 * @param computation     [in] 批量计算结果（冻结值）
 * @param workItemIndex   [in] 工作项下标（items 全序——失败点条目的
 *                        workItemIndex 值可直接回投）
 * @param resolvedPoints  [in] 宿主解析任务点投影（位置解析来源——语义
 *                        同 assembleBatchRenderData）
 * @param sourceTaskState [in] 来源运行任务态（状态机轴投影值）
 * @return 选中联动数据
 *
 * @throws std::invalid_argument workItemIndex 越界（调用方契约违约）
 *
 * 纯函数；线程安全；确定性。
 */
FailurePointSelectionLink
failurePointSelectionLink(const BatchComputation& computation, std::uint64_t workItemIndex,
                          const std::vector<BatchTaskPoint>& resolvedPoints,
                          core::TaskState sourceTaskState);

/**
 * @brief 按 (pointOid, conditionId) 查找工作项下标（选中→记录定位的
 *        反向查找——对象树/三维拾取发起的选中入口）。
 *
 * @param computation [in] 批量计算结果（items 全序——§8.4）
 * @param pointOid    [in] 任务点身份
 * @param conditionId [in] 工况身份
 * @return 首个命中项的全序下标；无命中 → nullopt（如实——不猜测）
 *
 * 线性扫描（items 全序、量级为批内工作项数——呈现路径量级可接受；
 * 确定性取首命中）。纯函数；线程安全；不抛。
 */
std::optional<std::uint64_t>
findWorkItemIndex(const BatchComputation& computation, const core::ObjectId& pointOid,
                  const core::ObjectId& conditionId) noexcept;

/**
 * @brief 解条目选中联动数据（L-K1"候选选中→检查器/三维高亮"的解级
 *        供给面——双击候选＝KIN-06 原文场景的数据入口）。
 * 值语义纯结构；线程安全。
 */
struct SolutionSelectionLink {
    /// sorted() 序下标（解检查器与三维高亮共用的解定位——SolutionRef 值）。
    std::size_t solutionIndex = 0;
    /// 会话回写值（KIN-06——双击候选只写会话姿态；writebackToSessionPose
    /// 消费）。
    RenderPointWriteback writeback;

    bool operator==(const SolutionSelectionLink& o) const
    {
        return solutionIndex == o.solutionIndex && writeback == o.writeback;
    }
    bool operator!=(const SolutionSelectionLink& o) const { return !(*this == o); }
};

/**
 * @brief 取解集视图中某解的选中联动数据（L-K1 解级数据面）。
 *
 * @param solutionSet [in] 解集只读视图（调用方保证存活至返回）
 * @param ref         [in] 解指称（worstBy/呈现侧选中的产出值）
 * @return 选中联动数据（writeback 自 sorted()[ref.solutionIndex] 提取）
 *
 * @throws std::invalid_argument ref.solutionIndex 越界（调用方契约违约）
 *
 * 纯函数；线程安全；确定性。
 */
SolutionSelectionLink solutionSelectionLink(const IKinematicSolutionSet& solutionSet,
                                            const SolutionRef& ref);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_RENDER_HPP
