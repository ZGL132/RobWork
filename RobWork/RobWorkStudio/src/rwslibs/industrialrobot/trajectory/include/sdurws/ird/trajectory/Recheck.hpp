/**
 * @file   Recheck.hpp
 * @brief  TRJ-04 复检协议（§15.6 RecheckPipeline——平滑后按冻结协议重新
 *         验证碰撞与关节限制的唯一执行点；WP-16-T07 批——复检半区）。
 *
 * 设计依据：
 *   - units/trajectory.md §11.1（平滑与复检流程图——③TRJ-04 复检（强制，
 *     不可跳过）：端点复检（各段端点/路点）＋段内采样细分；细分双上界：
 *     关节空间最大步长 ∧ 笛卡尔最大步长（最严对象）同时满足才终止；笛卡
 *     尔步长度量对象＝TCP 参考点 ∧ 全部参与碰撞验证几何代表点集之最大位
 *     移（R9）；采样→policy PathSequence（pathParameters 携带）＋关节限
 *     位复检；四分支：全样本已检无碰撞→Passed（coverage 入证据）／检出
 *     碰撞或限位违例→淘汰平滑结果回退→重平滑或重规划／验证器缺失→段级
 *     DataInsufficient（KIN-05 口径，不得视为无碰撞）／预算耗尽→段级
 *     DataInsufficient（附实际最大步长与预算占用——R9，不得默认接受））、
 *     §11.2（P-06 依赖声明——数值零自设；运行时读取顺序 Engineering-
 *     PolicySet 复检字段→P-06 冻结默认；安全间距阈值消费 policy——无私
 *     有线宽/阈值）、§11.3（复检执行要点——复检范围/细分实现/关节限位复
 *     检/"复检数据不足≠无碰撞；复检不足不能默认为通过"）、§11.5（预算占
 *     用记录结构——RecheckBudgetUsage 六字段）、§15.6（接口基线——签名
 *     /前置"P-06 数值可用"/后置"段级结论唯一（三态，无第四态）"/确定性
 *     "细分计划确定性"/非法示例"调用方私传步长阈值覆盖 policy→fail-fast；
 *     finalized=false 的评估被采信→调用方契约违约"）
 *   - REQUIREMENTS TRJ-04 原文（R2 要素：①各段端点（路点）与②段内采样；
 *     细分按"关节空间最大步长＋笛卡尔最大步长"双上界取严；③安全间距阈
 *     值消费工程策略（ARC-05 权威），复检不得使用私有线宽/阈值；④验证器
 *     或碰撞证据缺失（KIN-05 口径）→该段复检结论 DataInsufficient；⑤复
 *     检失败段按 TRJ-06 定位输出。R9 要素：笛卡尔步长以 TCP 参考点与全部
 *     参与碰撞验证几何的代表点集之最大位移计量；细分在双上界同时满足时
 *     终止；达到细分预算仍不满足步长→DataInsufficient（附实际达到的最大
 *     步长与预算占用），不得默认接受）、附录 C《P-06 冻结数值表》（2026-
 *     10-10 冻结——本头 makeP06FrozenRecheckParameters 逐行溯源）、AT-06
 *     （段内碰撞反例必须被复检检出；验证器缺失段判 DataInsufficient；细
 *     分预算耗尽反例；重规划反例）、ARC-05（安全间距阈值唯一来源＝policy）
 *   - 任务契约 tasks/foundation/WP-16-T07.json（acceptance 1——复检协议
 *     全要素＋三反例；acceptance 2——间距阈值消费 policy/验证器缺失
 *     DataInsufficient；acceptance 3——两提交拆分的第二提交）
 *
 * 背景说明（复检协议在轨迹链路中的位置——第一读者须知）：
 *   简化/平滑（Smooth.hpp）改变了候选路径几何，TRJ-04 要求"平滑后必须按
 *   冻结的碰撞验证协议重新验证碰撞与关节限制"。本头实现该协议：
 *     ①细分计划（确定性）——对被检几何（IPathGeometry，平滑产物或折线）
 *       按必检参数划初始子段（端点/路点必含——R2①），逐层二分加密直至
 *       "关节步长与笛卡尔步长双上界同时满足"（取严者生效——R9），细分预
 *       算（P-06 冻结：10 层/1024 子段）耗尽仍不满足→段级 DataInsufficient；
 *     ②代表点集（P-06 行 9 冻结规则）——笛卡尔步长的度量对象：TCP 参考
 *       点＋全部参与碰撞验证几何的代表点；经注入采样器（IRepresentPoint-
 *       Sampler）在基座系求值（生成规则物化归装配面——本域定义消费接口）；
 *     ③双检——最终采样集上先逐点关节限位复检（平滑可能越出原路径包络
 *       ——§11.3），再一次 policy PathSequence 碰撞查询（pathParameters
 *       等长携带——§10.3 约定；AT-19 三入口一致）；
 *     ④三态结论＋素材——Passed（coverage 入证据）/Collision（检出违例
 *       ——碰撞记录＋限位违例记录，淘汰平滑结果）/DataInsufficient（预
 *       算耗尽附实际步长＋预算占用；验证器缺失不视为无碰撞——KIN-05）。
 *
 *   **P-06 数值承载口径（§11.2 的落地面——诚实登记）**：RecheckParameters
 *   由调用方构造传入（policy 优先：EngineeringPolicySet 复检字段若经
 *   policy 卡增量修订引入则由装配面读取；否则用本头 makeP06FrozenRecheck-
 *   Parameters（）——REQUIREMENTS 附录 C《P-06 冻结数值表》的唯一产品物化
 *   点，逐行注释可溯源）。冻结数值是**需求权威数值**而非私设默认——
 *   D-TRJ-5"零自设"禁止的是发明卡面/需求之外的数值；读取顺序（policy 先
 *   于冻结默认）由调用方编排，本域两路都只消费传入值。
 *
 *   **协议范围口径（诚实登记）**：复检协议＝碰撞验证协议——policy 会话
 *   非空是前置（复检面向 collisionCheckApplicable==true 的段；无碰撞验证
 *   义务的段由编排面跳过复检并显式标注，其安全由线性凸组合性质（§7.4）
 *   与时间化限值校验（§12.2）承担）。
 *
 * 头文件依赖纪律（冒烟模式安全——Planner.hpp 同款最小面）：本头对 rw 只
 * 消费 header-only 数学头（Q）；对 policy 只前向声明（会话以 shared_ptr
 * 形态持有）；RecheckParameters 校验与 P-06 冻结表物化为 inline（零 rw 符
 * 号——两模式皆可编译，契约测试直测）；recheckSegment 定义在 src/Recheck.cpp
 * （集成模式条件源——Q/policy 会话消费面，Ptp.cpp 同款 gating）。
 *
 * 线程安全：全部值类型并发只读安全；recheckSegment 纯函数（零副作用/零
 * 修订/零写盘——§15.0），单线程使用；IRepresentPointSampler 实现方声明
 * 线程语义（本域按 const 只读调用）。
 */

#ifndef IRD_TRAJECTORY_RECHECK_HPP
#define IRD_TRAJECTORY_RECHECK_HPP

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/core/Provenance.hpp>      // core::SourcedValue（预算账目四态值面）
#include <sdurws/ird/core/Units.hpp>           // core::UnitToken（比较型素材单位口径）
#include <sdurws/ird/trajectory/Errors.hpp>    // TrajectoryError（fail-fast 载体——validate 用）
#include <sdurws/ird/trajectory/Smooth.hpp>    // IPathGeometry（被检几何输入——平滑产物交付面）
#include <sdurws/ird/trajectory/TrjTypes.hpp>  // CancelSignal/FailedSegmentRecord/kPhaseRecheck

namespace sdurws::ird {

namespace policy {
/// 前向声明（本头仅以 shared_ptr 形态持有碰撞会话——完整类型
/// policy/CollisionEvaluator.hpp 在实现 TU include；头文件依赖纪律见文件
/// 头注）。
class CollisionEvaluationSession;
}  // namespace policy

namespace trajectory {

// =====================================================================
// 关节类型词表（P-06 冻结表行 1/2 的逐轴分类承载——步长按轴型取值）
// =====================================================================

/**
 * @brief 关节类型（复检关节步长的逐轴分类键——P-06 冻结表对旋转/移动关
 *        节分设默认步长与细分上界；rw::math::Q 分量无类型信息，须由调用
 *        方按权威模型的关节类型投影传入）。
 */
enum class JointTypeKind : std::uint8_t {
    /// 旋转关节（步长单位 rad——P-06 表行 1/3）。
    Revolute,
    /// 移动关节（步长单位 m——P-06 表行 2/4）。
    Prismatic,
};

// =====================================================================
// 复检协议数值（P-06 承载——引用值非副本；validate 与冻结表物化 inline）
// =====================================================================

/**
 * @brief 复检协议数值参数集（§15.6 Request"细分参数（policy/P-06 来源引
 *        用）"的值面——运行时读取顺序：EngineeringPolicySet 复检字段（若
 *        policy 卡引入）→makeP06FrozenRecheckParameters（P-06 冻结默认），
 *        由调用方编排后传入；本域代码对协议数值**零其他字面量**——D-TRJ-5）。
 *
 * 字段—冻结表对照（REQUIREMENTS 附录 C《P-06 冻结数值表》行 1~8；行 9 代
 * 表点集规则不经数值承载——它是生成规则，物化在装配面采样器）：
 *   - maxJointStepRevolute          ←行 1（旋转关节步长默认，rad）
 *   - maxJointStepPrismatic         ←行 2（移动关节步长默认，m）
 *   - jointSubdivisionCeilingRevolute  ←行 3（旋转细分上界＝4×行 1，rad）
 *   - jointSubdivisionCeilingPrismatic ←行 4（移动细分上界＝4×行 2，m）
 *   - maxCartesianStep              ←行 5（笛卡尔步长默认——TCP＋代表点集
 *                                     最大位移口径，m）
 *   - cartesianSubdivisionCeiling   ←行 6（笛卡尔细分上界＝4×行 5，m）
 *   - maxSubdivisionDepth           ←行 7（细分预算最大层数——二分）
 *   - maxSubsegments                ←行 8（单段最大子段数——2¹⁰）
 *
 * "细分上界"语义（表行 3~6 类别列"可放宽上界＝硬上限"）：终止条件是步长
 * ≤生效步长值（默认值或 policy 放宽值）；上界只是 policy 放宽的封顶——
 * validate 强制"步长 ≤ 上界"（放宽越顶即配置非法，fail-fast）。
 *
 * 值语义纯结构；线程安全（并发只读）。
 */
struct RecheckParameters {
    /// 旋转关节最大步长（rad；∈(0, jointSubdivisionCeilingRevolute]）。
    double maxJointStepRevolute = 0.0;
    /// 移动关节最大步长（m；∈(0, jointSubdivisionCeilingPrismatic]）。
    double maxJointStepPrismatic = 0.0;
    /// 旋转关节细分上界（rad；policy 放宽的硬上限——P-06 行 3）。
    double jointSubdivisionCeilingRevolute = 0.0;
    /// 移动关节细分上界（m；同上——P-06 行 4）。
    double jointSubdivisionCeilingPrismatic = 0.0;
    /// 笛卡尔最大步长（m；TCP 参考点＋代表点集最大位移口径——P-06 行 5；
    /// ∈(0, cartesianSubdivisionCeiling]）。
    double maxCartesianStep = 0.0;
    /// 笛卡尔细分上界（m；同上封顶语义——P-06 行 6）。
    double cartesianSubdivisionCeiling = 0.0;
    /// 细分预算——最大层数（二分层；≥1——P-06 行 7 冻结值 10）。
    std::uint32_t maxSubdivisionDepth = 0;
    /// 细分预算——单段最大子段数（总子段数上限；≥1——P-06 行 8 冻结值
    /// 1024）。
    std::uint32_t maxSubsegments = 0;

    /// 数值来源标记（证据面/审计的引用登记——ARC-05"引用值非副本"的来源
    /// 半区；本域不解析 policy，来源由调用方如实标注）。
    enum class Source : std::uint8_t {
        /// 工程策略承载（EngineeringPolicySet 复检字段——policy 卡增量修
        /// 订引入后的读取路径，ARC-05 权威）。
        EngineeringPolicy,
        /// P-06 冻结默认（makeP06FrozenRecheckParameters——需求附录 C 冻
        /// 结表物化）。
        P06FrozenDefault,
    };
    /// 本参数集的来源（构造方如实标注）。
    Source source = Source::P06FrozenDefault;

    bool operator==(const RecheckParameters& o) const noexcept
    {
        return maxJointStepRevolute == o.maxJointStepRevolute
            && maxJointStepPrismatic == o.maxJointStepPrismatic
            && jointSubdivisionCeilingRevolute == o.jointSubdivisionCeilingRevolute
            && jointSubdivisionCeilingPrismatic == o.jointSubdivisionCeilingPrismatic
            && maxCartesianStep == o.maxCartesianStep
            && cartesianSubdivisionCeiling == o.cartesianSubdivisionCeiling
            && maxSubdivisionDepth == o.maxSubdivisionDepth
            && maxSubsegments == o.maxSubsegments && source == o.source;
    }
    bool operator!=(const RecheckParameters& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 校验复检协议数值（RecheckParameters 合法域的执行面——非法即抛，
 *        调用方配置错误属 fail-fast 轨，token "trajectory/recheck/params-*"）。
 *
 * 校验序固定（确定性——同一坏参数必报同一首错，NFR-COR-02）：
 *   1. 三组步长有限且 >0；
 *   2. 步长 ≤ 对应细分上界（"放宽受上界封顶"的执行点——P-06 表行 3~6
 *      "可放宽上界＝硬上限"；上界自身须有限 ≥步长）；
 *   3. 两个预算 ≥1（层数/子段数——0 预算使细分协议不可终止）。
 *
 * inline 于头文件：纯值校验零 rw 符号——两模式皆可编译（契约测试直测）。
 *
 * @throws TrajectoryError token "trajectory/recheck/params-*"
 */
inline void validateRecheckParameters(const RecheckParameters& params)
{
    const auto checkStep = [](double step, double ceiling, const char* label,
                              const char* unit) {
        if (!(step > 0.0) || !std::isfinite(step)) {
            throw TrajectoryError(
                "trajectory/recheck/params-step",
                std::string{label} + "步长必须为有限正数（" + unit + "），实际: "
                    + std::to_string(step));
        }
        if (!std::isfinite(ceiling) || step > ceiling) {
            throw TrajectoryError(
                "trajectory/recheck/params-ceiling",
                std::string{label} + "步长超出细分上界（P-06 封顶语义——放宽不"
                    "得越过硬上限），步长 " + std::to_string(step) + " 上界 "
                    + std::to_string(ceiling) + "（" + unit + "）");
        }
    };
    checkStep(params.maxJointStepRevolute, params.jointSubdivisionCeilingRevolute,
              "旋转关节", "rad");
    checkStep(params.maxJointStepPrismatic, params.jointSubdivisionCeilingPrismatic,
              "移动关节", "m");
    checkStep(params.maxCartesianStep, params.cartesianSubdivisionCeiling,
              "笛卡尔", "m");
    if (params.maxSubdivisionDepth < 1U) {
        throw TrajectoryError("trajectory/recheck/params-budget",
                              "细分预算最大层数必须 ≥1，实际: "
                                  + std::to_string(params.maxSubdivisionDepth));
    }
    if (params.maxSubsegments < 1U) {
        throw TrajectoryError("trajectory/recheck/params-budget",
                              "细分预算最大子段数必须 ≥1，实际: "
                                  + std::to_string(params.maxSubsegments));
    }
}

/**
 * @brief 构造 P-06 冻结默认参数集（REQUIREMENTS 附录 C《P-06 冻结数值表》
 *        的唯一产品物化点——2026-10-10 需求所有者确认草案，WP-16-T02 执行
 *        冻结；修订本表任何数值须走需求变更并留痕）。
 *
 * 为什么本函数是"零自设"纪律的合规承载（§11.2 口径，诚实登记）：冻结数
 * 值是需求权威（附录 C），不是本域发明的默认——D-TRJ-5 禁止的是私设数值
 * 与"绕过 policy 私传阈值"（§15.6 非法示例）；运行时读取顺序（policy 复
 * 检字段优先→冻结默认兜底）由调用方编排，本工厂只承载第二路。逐行注释
 * 溯源冻结表，任何数值改动都必须先走需求变更（同步本函数＋单元卡）。
 *
 * @return 冻结参数集（source＝Source::P06FrozenDefault——审计可辨）。
 *
 * inline 于头文件：纯值构造零 rw 符号——两模式皆可编译（契约测试以冻结
 * 表数值逐字段对照钉扎）。
 */
inline RecheckParameters makeP06FrozenRecheckParameters()
{
    RecheckParameters p;
    // 行 1：关节空间最大步长——旋转关节默认 0.05 rad（工程策略默认，放宽
    // 受行 3 上界封顶）。
    p.maxJointStepRevolute = 0.05;
    // 行 2：关节空间最大步长——移动关节默认 0.005 m（同上，封顶见行 4）。
    p.maxJointStepPrismatic = 0.005;
    // 行 3：关节空间细分上界——旋转关节 0.2 rad（4×默认值封顶＝硬上限）。
    p.jointSubdivisionCeilingRevolute = 0.2;
    // 行 4：关节空间细分上界——移动关节 0.02 m（4×默认值封顶＝硬上限）。
    p.jointSubdivisionCeilingPrismatic = 0.02;
    // 行 5：笛卡尔最大步长——默认 0.005 m（TCP 参考点＋代表点集最大位移
    // 口径，关节/笛卡尔双上界取严）。
    p.maxCartesianStep = 0.005;
    // 行 6：笛卡尔细分上界 0.02 m（4×默认值封顶＝硬上限）。
    p.cartesianSubdivisionCeiling = 0.02;
    // 行 7：细分预算——最大层数 10（二分）。
    p.maxSubdivisionDepth = 10U;
    // 行 8：细分预算——单段最大子段数 1024（2¹⁰）。
    p.maxSubsegments = 1024U;
    p.source = RecheckParameters::Source::P06FrozenDefault;
    return p;
}

// =====================================================================
// 代表点集采样器（P-06 行 9 生成规则的消费接口——注入端口形态）
// =====================================================================

/**
 * @brief 代表点集采样器（§15.6 Request"代表点集描述（P-06 规则产物）"的
 *        消费面——P-06 冻结表行 9：TCP 参考点＋全部参与碰撞验证几何体
 *        （连杆/工具/负载碰撞模型）——解析原语取全部顶点与各棱中点，mesh
 *        取全部顶点；帧集合与细分采样一致（段端点必含））。
 *
 * 为什么是注入接口而非本域生成：代表点的世界/基座坐标需要对每个碰撞几
 * 何体做整机正运动学（连杆系位姿×几何局部点）——几何参与集与位姿链归
 * runtime/policy（R-1/R-5：本域零 WorkCell 自建、零 proximity 触碰）；生
 * 成规则（顶点＋棱中点/mesh 顶点）的消费载体是装配面适配器（L5——与
 * IKinematicsComputePort 的 P-KIN-2 宿主注入先例同构）。本域只消费"给定
 * 构型→代表点集（基座系）"的求值结果，按 R9 计量最大位移。
 *
 * 实现方契约（装配面义务）：
 *   - 点集内容恒定：同一被检段的全部构型返回**等长**点序列（同一代表点
 *     集逐构型求位——点序对应固定，否则位移计量无定义——违约属装配面
 *     契约违例，消费面 fail-fast）；
 *   - 至少含 TCP 参考点（P-06 行 9——几何参与集为空时点集＝{TCP}）；
 *   - 坐标系：基座系 {B}（T_world_base 之后——MDL-22 单一不变量，AT-37；
 *     单位 m）；
 *   - 确定性：同构型等价输出（NFR-COR-02——细分计划的确定性基础）。
 *
 * 线程语义：实现方声明（本域按 const 只读调用——单线程使用环境）。
 */
class IRepresentPointSampler {
public:
    virtual ~IRepresentPointSampler() = default;

    /**
     * @brief 求给定构型处的代表点集（基座系坐标）。
     *
     * @param q [in] 权威关节构型（转动 rad／移动 m；链序——与被检几何
     *            同维度）
     *
     * @return 代表点集（基座系 {B}，m；等长契约见类注；空集＝装配面违约
     *         ——消费面 fail-fast）
     *
     * 确定性：同 q → 等价输出；纯计算零副作用。
     */
    virtual std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q& q) const = 0;
};

// =====================================================================
// 预算占用记录（§11.5 结构——TrajectoryQuality.recheckBudget 的字段类型，
// T07 引入；T08 质量面复用）
// =====================================================================

/**
 * @brief 复检预算占用记录（§11.5 原文结构——R9"附实际达到的最大步长与
 *        预算占用"的数据面；证据面据此构造比较型素材与 trj.smooth-recheck-
 *        evidence 证据项）。
 *
 * 字段语义（§11.5 原文）：
 *   - actualMaxJointStep：实际达到的最大关节步长（rad/m 逐轴最严）——未
 *     复检＝NotProvided；预算耗尽时＝停止时刻仍未满足双上界的实测最严值
 *     （不默认接受的面——TRJ-04 R9 原文）；
 *   - actualMaxCartesianStep：实际达到的最大笛卡尔步长（m，代表点集口径
 *     ——R9；二值后端无距离能力不影响它——步长由几何计算非距离查询）；
 *   - subdivisionDepthUsed/subdivisionBudget：实际细分层数/预算上界（来
 *     源＝policy/P-06——引用值非副本，ARC-05）；
 *   - subsegmentsExamined：实际检查子段数（含初始与全部细分产物）；
 *   - budgetExhausted：预算耗尽标记（true→该段 DataInsufficient 素材）。
 *
 * 值语义纯结构；线程安全。
 */
struct RecheckBudgetUsage {
    /// 实际达到的最大关节步长（rad|m 逐轴最严；Provided＝复检已执行）。
    core::SourcedValue<double> actualMaxJointStep;
    /// 实际达到的最大笛卡尔步长（m，代表点集口径——几何计算与距离能力
    /// 无关）。
    core::SourcedValue<double> actualMaxCartesianStep;
    /// 实际细分层数（二分层；0＝初始子段已满足无双上界驱动细分）。
    std::uint32_t subdivisionDepthUsed = 0;
    /// 预算上界——最大层数（引用值——params.maxSubdivisionDepth 透传）。
    std::uint32_t subdivisionBudget = 0;
    /// 实际检查子段数（含初始与细分产物——评估面的真实账目）。
    std::uint32_t subsegmentsExamined = 0;
    /// 预算耗尽标记（true→DataInsufficient——不得默认接受未充分验证段）。
    bool budgetExhausted = false;

    bool operator==(const RecheckBudgetUsage& o) const noexcept
    {
        return actualMaxJointStep.tryValue() == o.actualMaxJointStep.tryValue()
            && actualMaxJointStep.state() == o.actualMaxJointStep.state()
            && actualMaxCartesianStep.tryValue() == o.actualMaxCartesianStep.tryValue()
            && actualMaxCartesianStep.state() == o.actualMaxCartesianStep.state()
            && subdivisionDepthUsed == o.subdivisionDepthUsed
            && subdivisionBudget == o.subdivisionBudget
            && subsegmentsExamined == o.subsegmentsExamined
            && budgetExhausted == o.budgetExhausted;
    }
    bool operator!=(const RecheckBudgetUsage& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 检出记录（碰撞发现与限位违例——Collision 态的数据面＋素材面）
// =====================================================================

/**
 * @brief 单条碰撞检出记录（§15.6"Collision(pathParameter, 对象对)"的域内
 *        载体——TRJ-RECHECK-COLLISION 素材的定位面）。
 *
 * 字段直映 policy::CollisionFinding 的 Collision 种类发现（SafetyMargin-
 * Violation 间距不足为"非碰撞"——policy §7.4，不进本记录；间距语义唯一
 * 来源＝会话绑定的 EngineeringPolicySet——ARC-05，本域零阈值触碰）。
 * 对象对为 policy 规范序（字节字典序 A<B——NFR-COR-05）。值语义纯结构。
 */
struct RecheckFindingRecord {
    /// 规范序第一端（碰撞对象对；ObjectId——R-4 零名称直存）。
    core::ObjectId objectA;
    /// 规范序第二端。
    core::ObjectId objectB;
    /// 采样位置（最终采样集下标，0 基；无量纲计数）。
    std::size_t sampleIndex = 0;
    /// 段内定位参数 s∈[0,1]（PathSequence findings 必填——TRJ-04 段内定位；
    /// 与本域采样协议的 pathParameters 一致）。
    std::optional<double> pathParameter;

    bool operator==(const RecheckFindingRecord& o) const noexcept
    {
        return objectA == o.objectA && objectB == o.objectB
            && sampleIndex == o.sampleIndex && pathParameter == o.pathParameter;
    }
    bool operator!=(const RecheckFindingRecord& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 单条关节限位违例记录（§11.3"平滑后逐采样限位检查"的检出面——
 *        §11.1 分支二"发现碰撞/限位违例→淘汰该平滑结果"的限位半区；
 *        V-15 锚点）。
 *
 * 素材义务（TRJ-06 定位输出＋ERR-01 比较型）：reasonToken 恒
 * kTrjLimitExceeded（TRJ-LIMIT-EXCEEDED——限位超标；比较型三要素＝实际
 * 构型/限界/单位 rad|m，经 record 字段直接构造——证据面免二次换算）。
 * 值语义纯结构；线程安全。
 */
struct RecheckLimitViolationRecord {
    /// 越限轴（0 基——链序；无量纲索引）。
    std::size_t axis = 0;
    /// 轴型（比较型素材的单位口径——rad 对应 Revolute、m 对应 Prismatic）。
    JointTypeKind axisKind = JointTypeKind::Revolute;
    /// 采样位置（最终采样集下标，0 基）。
    std::size_t sampleIndex = 0;
    /// 段内定位参数 s∈[0,1]（细分采样集的路径参数）。
    double pathParameter = 0.0;
    /// 实际构型分量（rad|m——越限判定值）。
    double actualQ = 0.0;
    /// 被越限的限界值（rad|m；upper==true 时为上界，否则下界）。
    double boundQ = 0.0;
    /// 越限方向（true＝越上界 q>bound；false＝越下界 q<bound）。
    bool upper = true;
    /// 定位素材（TRJ-LIMIT-EXCEEDED——比较型齐备；phase 恒 kPhaseRecheck；
    /// segmentIndex 取请求值）。
    FailedSegmentRecord record;
};

// =====================================================================
// coverage 投影（policy::PairCoverageRecord 的域内数值投影——KIN-05）
// =====================================================================

/**
 * @brief 复检覆盖事实投影（policy §6.2 PairCoverageRecord 的数值子集——
 *        §15.6 输出"coverage 记录"的落地面）。
 *
 * 为什么投影而非透传 policy 类型：本头对 policy 仅前向声明（两模式皆可
 * 编译的依赖最小面——文件头注）；四计数是 KIN-05 口径的判定面（几何缺口
 * /未查部分显式化，防"没检＝无碰撞"），数值投影不改变语义（与 §15.4 端
 * 口镜像词表的"适配器机械映射"同款纪律——语义权威归 policy 卡）。excluded
 * 过滤明细不投影（体积面，证据面经 policy 会话自行取全量）。
 *
 * 计数语义（policy §6.2 原文口径的投影）：
 *   - pairsInScope：作用域内对数；pairsWithGeometry：双端有有效几何的对
 *     数（缺口显式化）；pairsEvaluated：实际被后端查询过的对数（提前终止
 *     时如实反映未查部分）；pairsExcludedByRule：被过滤对数。
 * 值语义纯结构；线程安全。
 */
struct RecheckCoverageRecord {
    std::uint64_t pairsInScope = 0;         ///< 作用域内对数
    std::uint64_t pairsWithGeometry = 0;    ///< 双端有有效几何的对数（KIN-05 缺口）
    std::uint64_t pairsEvaluated = 0;       ///< 实际被后端查询过的对数
    std::uint64_t pairsExcludedByRule = 0;  ///< 被过滤对数

    bool operator==(const RecheckCoverageRecord& o) const noexcept
    {
        return pairsInScope == o.pairsInScope && pairsWithGeometry == o.pairsWithGeometry
            && pairsEvaluated == o.pairsEvaluated
            && pairsExcludedByRule == o.pairsExcludedByRule;
    }
    bool operator!=(const RecheckCoverageRecord& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 复检请求/结果（§15.6 提议签名的字段面）
// =====================================================================

/**
 * @brief 段级复检结论（§15.6 词表三态——封闭，无第四态；"段级结论唯一"）。
 */
enum class RecheckConclusion : std::uint8_t {
    /// 细分满足双上界、全部样本已检且无碰撞/限位违例（coverage 记录入证据
    /// ——§11.1 分支一）。
    Passed,
    /// 检出违例（碰撞发现或关节限位违例——§11.1 分支二）：淘汰该平滑结果
    /// →回退该段→重平滑或重规划（编排面语义——本域只产检出记录与素材，
    /// 不执行回退）；collisionRecords/limitViolations 至少一组非空。
    Collision,
    /// 验证覆盖不充分（§11.1 分支三/四）：预算耗尽（附实际步长＋预算占用
    /// ——R9）或验证器/碰撞证据缺失（KIN-05 口径——不得视为无碰撞）。
    DataInsufficient,
};

/**
 * @brief 复检数据不足原因（DataInsufficient 态的细分词表——封闭两值；
 *        证据面按此构造 TRJ-RECHECK-BUDGET-EXHAUSTED / TRJ-RECHECK-DATA-
 *        INSUFFICIENT 两码的素材路由）。
 */
enum class RecheckDataInsufficientReason : std::uint8_t {
    /// 细分预算耗尽（层数/子段数任一上界到达仍不满足双步长——R9；附实际
    /// 最大步长与预算占用）。
    BudgetExhausted,
    /// 验证器/碰撞证据缺失（policy 评估 Failed/不可采信——KIN-05 口径；
    /// 不得视为无碰撞）。
    EvidenceUnavailable,
};

/**
 * @brief 复检执行结局（顶层二态——取消非错误 UX-03 的承载；§15.6"段级结
 *        论唯一三态"是对 conclusion 字段的约束，执行层的取消语义在本词
 *        表，不污染三态结论——DTB §5.4 实现补全登记）。
 */
enum class RecheckStatus : std::uint8_t {
    /// 复检执行完成（conclusion 有效——三态之一）。
    Completed,
    /// 取消观测命中（细分层边界/查询边界轮询）——零错误素材（UX-03），
    /// conclusion 无效、failure 恒空。
    Canceled,
};

/**
 * @brief 复检请求（§15.6 Request 字段面；前置违约一律 fail-fast，token
 *        "trajectory/recheck/..."）。
 *
 * 前置（§15.6 与 §15.0 纪律的逐项落点）：
 *   - path 非空（被检几何——平滑产物 geometry 或折线求值器）；
 *   - mandatoryParameters 严格升序、首元素==0.0、末元素==1.0（端点必含
 *     ——R2①"各段端点（路点）"与 P-06 行 9"段端点必含"；初值容差位级
 *     比较——调用方以精确字面值传入）；
 *   - jointTypes 与几何维度一致（采样 s=0 处构型维度）；限位区间同维度
 *     且逐轴 lower<upper；
 *   - params 过 validateRecheckParameters（§15.6"调用方私传步长阈值覆盖
 *     policy（R-POL-5 违约→fail-fast）"的执行点：数值合法性在此封闭，
 *     任何越过 policy 直接构造的越顶步长被拒）；
 *   - sampler 非空（代表点集是 R9 的度量对象——缺失即无法计量笛卡尔步
 *     长，协议不可执行）；
 *   - session 非空（复检协议＝碰撞验证协议——协议范围口径见文件头注）。
 *
 * 单位/坐标系：构型为权威关节向量（rad|m）；代表点集为基座系 {B}（m）。
 * 值语义纯结构；线程安全（借用指针/共享会话在调用期存活即可）。
 */
struct RecheckRequest {
    /// 被检段几何（平滑产物或折线——非空；s∈[0,1] 契约见 IPathGeometry）。
    const IPathGeometry* path = nullptr;
    /// 必检参数序列（严格升序；首 0 末 1——段端点＋路点；初始子段＝相邻
    /// 必检参数之间的区间——R2①"端点复检（各段端点/路点）"）。
    std::vector<double> mandatoryParameters;
    /// 逐轴关节类型（与构型维度等长——P-06 行 1/2 步长分类键）。
    std::vector<JointTypeKind> jointTypes;
    /// 评价区间下界（逐轴 rad|m——限位复检的下界；§11.3 限值来源 §5.3
    /// 的请求侧投影）。
    rw::math::Q lowerBoundQ;
    /// 评价区间上界（逐轴 rad|m）。
    rw::math::Q upperBoundQ;
    /// 复检协议数值（policy/P-06 来源引用——validate 必过）。
    RecheckParameters params;
    /// 代表点集采样器（P-06 行 9 规则的装配面物化——非空；R9 度量对象）。
    const IRepresentPointSampler* sampler = nullptr;
    /// policy 碰撞会话（唯一碰撞权威——R-5；非空——协议范围口径见文件头注）。
    std::shared_ptr<const policy::CollisionEvaluationSession> session;
    /// 段序号（0 基——素材定位键；§6.3"诊断/复检/动画共用同一定位键"）。
    std::uint32_t segmentIndex = 0;
    /// 取消观测（可空＝不可取消；细分层边界/查询边界轮询——§15.1）。
    CancelSignal cancel;
};

/**
 * @brief 复检结果（§15.6 输出词表的落地面；值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Completed＋Passed：conclusion=Passed；collisionRecords/limitViolations
 *     恒空；failure 恒空；budget/coverage 如实填写（budget.budgetExhausted
 *     ==false）；
 *   - Completed＋Collision：collisionRecords/limitViolations 至少一组非空
 *     （检出违例——淘汰平滑结果的证据）；failure 必填（kTrjRecheckCollision
 *     ——cause 携带首条检出定位）；budget.budgetExhausted=false（细分已完
 *     成）；
 *   - Completed＋DataInsufficient：reason 必填；BudgetExhausted 时 budget.
 *     budgetExhausted=true 且 failure=kTrjRecheckBudgetExhausted（比较型：
 *     实际步长/预算）；EvidenceUnavailable 时 failure=kTrjRecheckData-
 *     Insufficient（KIN-05）；collisionRecords/limitViolations 恒空；
 *   - Canceled：conclusion/collisionRecords/limitViolations/failure 全部
 *     无效或空（取消不是错误——UX-03）；budget 携带已执行部分的真实账目。
 */
struct RecheckOutcome {
    /// 执行结局（RecheckStatus 二态）。
    RecheckStatus status = RecheckStatus::Canceled;
    /// 段级复检结论（三态；仅 Completed 有效——§15.6"无第四态"）。
    RecheckConclusion conclusion = RecheckConclusion::DataInsufficient;
    /// DataInsufficient 细分原因（仅 conclusion==DataInsufficient 有效）。
    RecheckDataInsufficientReason dataInsufficientReason =
        RecheckDataInsufficientReason::EvidenceUnavailable;
    /// 碰撞检出记录（Collision 态；对象对＋sampleIndex＋pathParameter）。
    std::vector<RecheckFindingRecord> collisionRecords;
    /// 限位违例记录（Collision 态可非空——与碰撞检出并存时同属淘汰分支）。
    std::vector<RecheckLimitViolationRecord> limitViolations;
    /// 预算占用（§11.5——恒填写已执行部分的真实账目）。
    RecheckBudgetUsage budget;
    /// 覆盖事实投影（policy coverage 数值子集——碰撞查询执行后填写；查询
    /// 未执行（预算耗尽/取消）时为默认零值——KIN-05"未查部分"由调用方以
    /// conclusion 判读，零值不冒充"全检"）。
    RecheckCoverageRecord coverage;
    /// 结论级定位素材（Collision/DataInsufficient 必填——TRJ-06/§11.1 分
    /// 支语义；Passed/Canceled 恒空）。
    std::optional<FailedSegmentRecord> failure;
};

// =====================================================================
// §15.6 复检管线入口
// =====================================================================

/**
 * @brief 执行段级 TRJ-04 复检（§15.6 签名——细分协议＋双检＋三态结论的
 *        唯一执行点）。
 *
 * 执行序（每步语义见行内注释；算法细节见 Recheck.cpp 头注）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——token
 *      "trajectory/recheck/..."；含 params 校验——R-POL-5 越顶拒绝）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 初始子段集：相邻必检参数之间的区间（端点/路点必含——R2①）；
 *   4. 细分层循环：逐子段度量关节步长（逐轴按轴型取 P-06 步长）与笛卡尔
 *      步长（代表点集最大位移——R9）→双上界同时满足才通过；未满足子段
 *      二分加密，预算（层数/子段数）耗尽仍不满足→DataInsufficient
 *      （BudgetExhausted——附实际步长＋预算占用，不执行碰撞查询）；
 *   5. 关节限位复检：最终采样集逐点逐轴检查（平滑可能越出原路径包络
 *      ——§11.3）→违例记录（TRJ-LIMIT-EXCEEDED 素材）；
 *   6. 碰撞复检：最终采样集一次 policy PathSequence（pathParameters 等长
 *      携带——§10.3）→findings 转检出记录；评估不可采信（finalized=false
 *      等）→DataInsufficient（EvidenceUnavailable——KIN-05，不得视为无
 *      碰撞）；取消命中→顶层 Canceled；
 *   7. 结论归集（三态唯一）＋预算账目＋coverage 投影＋素材组装。
 *
 * 确定性：细分计划由（几何, 必检参数, params）唯一决定（NFR-COR-02——
 * §15.6"细分计划确定性"）；同请求等价输出（含 policy 查询的确定性——
 * 会话契约）。本函数不判任务可行性（N7——素材经证据面汇总）。
 *
 * @param request [in] 复检请求（RecheckRequest 前置见其注；违约抛
 *                TrajectoryError——fail-fast）
 *
 * @return 复检结果（RecheckOutcome——三态结论＋预算/覆盖/素材）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/recheck/..."）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性。
 */
RecheckOutcome recheckSegment(const RecheckRequest& request);

}  // namespace trajectory
}  // namespace sdurws::ird

#endif  // IRD_TRAJECTORY_RECHECK_HPP
