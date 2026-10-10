/**
 * @file   Smooth.hpp
 * @brief  路径简化与平滑（§15.7 SmoothPipeline，TRJ-04"对路径进行简化
 *         和平滑"的承载面；WP-16-T07 批——平滑半区）。
 *
 * 设计依据：
 *   - units/trajectory.md §11.1（平滑与复检流程图——①简化：冗余路点剔除
 *     （smoothTolerance 几何保持容差内删除可省路点）；②平滑：关节空间五次
 *     样条拟合/圆角过渡，端点强制不变；"几何或采样是否变化？——否——沿用
 *     既有复检结论（零变化零复检）"；Smoothed 必触发复检——强制边归编排
 *     面，本头只保证"几何有变化"的产物语义）、§11.3（关节限位复检消费方
 *     是 Recheck——本头只做几何保持）、§11.4（简化/平滑模型与几何保持：
 *     简化＝贪心逐点剔除（保留端点与必经点），剔除后几何偏差（关节维逐轴
 *     ＋TCP 派生维）≤ smoothTolerance；平滑＝关节空间五次样条重拟合，端点
 *     位置/姿态/导数边界不变，平滑前后路径偏差 ≤ smoothTolerance（双域容
 *     差独立配置）；几何保持验证入证据（偏差实际值/容差比较型字段），偏差
 *     超容差→该次平滑作废）、§15.7（SmoothPipeline 接口基线——签名/输出
 *     词表四态/后置"Smoothed 必触发复检；端点不变为硬条件"）
 *   - §15.0（通用约定——错误二分/取消/确定性/线程/副作用）、§5.5（求解配
 *     置——smoothTolerance"关节 rad／TCP m，双域各一"，进 config.trj 身份；
 *     本头经请求字段消费已解析值，不重复解码）、§5.6（必经状态——必经点
 *     简化保护的数据出处：Start/TaskPoint/End 是任务强制经过点，Via 才是
 *     可剔除的规划器/平滑引入点）
 *   - 需求 TRJ-04（P0：简化与平滑；平滑后按冻结协议复检——复检在 Recheck.
 *     hpp，WP-16-T07 同批第二提交）、NFR-COR-02（确定性——同输入等价输出
 *     ：简化判据/导数构造/收缩序列全为确定规则，零随机源）、NFR-COR-03
 *     （非法输入拒绝，不钳制不静默——前置校验 fail-fast）、ARC-05（零策略
 *     副本——本头不消费任何 policy 数值）
 *   - 任务契约 tasks/foundation/WP-16-T07.json（acceptance 1——平滑后按冻
 *     结协议复检的平滑半区；acceptance 3——平滑/复检按卡内任务拆分为两提
 *     交，本文件属第一提交）
 *
 * 背景说明（平滑在轨迹链路中的位置——第一读者须知）：
 *   T04~T06 产出的候选路径（PTP 直连/避障绕行）是折线（离散路点＋线性插
 *   值语义），拐点处关节速度/加速度方向突变，时间参数化（T08）后峰值负载
 *   高。TRJ-04 要求先简化（剔除冗余路点）再平滑（拐点圆角化），产物交给
 *   复检协议（Recheck.hpp）重新验证碰撞与关节限制——平滑改变了几何，复检
 *   是强制边（§11.1"只要几何或采样发生变化就重新复检"）。本头的职责边界：
 *   只做"几何变换＋几何保持验证"，不消费 policy（碰撞/阈值归复检）、不判
 *   任务可行性（N7）。
 *
 *   平滑算法（实现语义权威，DTB §5.4 实现补全登记见实现 TU 头注）：
 *     参数域 u∈[0, n-1]（结点索引，无时间语义——时间律归 §12/T08）上做
 *     分段五次 Hermite 插值：结点值＝路点（插值条件含端点→端点强制不变）
 *     ；结点一阶导＝中心差分×收缩因子 λ（端点导数取原折线端斜率、不收缩
 *     ——"端点导数边界不变"）；结点二阶导恒取 0（相邻段在结点处二阶导同
 *     值→整体 C²——"五次样条拟合，结点 C² 匹配"的卡面语义）。λ 从 1 起
 *     按固定序列收缩，几何保持验证（对原折线的逐轴最大偏差 ≤ 容差）未通
 *     过时收缩重试，迭代上限内仍失败→平滑作废（保留原路径）。
 *
 * 头文件依赖纪律（冒烟模式安全——Planner.hpp/KinematicsPort.hpp 同款）：
 *   本头对 rw 只消费 header-only 的数学头（Q/Vector3D）；无 policy/runtime
 *   依赖；接口类与值类型零 .cpp（makeLinearJointPathGeometry/smooth 的定
 *   义在实现 TU——src/Smooth.cpp 为集成模式条件源，Q 构造面 gating 同
 *   Ptp.cpp，冒烟口径不受影响）。
 *
 * 线程安全：全部值类型并发只读安全；smooth 为纯函数（零副作用/零修订/
 * 零写盘——§15.0），单线程使用；IPathGeometry 实现方自行声明线程语义
 * （本域消费面按 const 只读调用）。
 */

#ifndef IRD_TRAJECTORY_SMOOTH_HPP
#define IRD_TRAJECTORY_SMOOTH_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>

#include <sdurws/ird/trajectory/TrjTypes.hpp>  // CancelSignal/WaypointKind（域内值面）

namespace sdurws::ird::trajectory {

/// FK 端口前向声明（完整类型 KinematicsPort.hpp——本头仅以裸指针形态
/// 借持，实现 TU include；头文件依赖纪律见文件头注）。
class IKinematicsComputePort;

// =====================================================================
// 平滑产物几何——段几何采样求值接口（复检协议的被检几何输入面）
// =====================================================================

/**
 * @brief 段几何采样求值接口（平滑管线的产物交付形态——§11.1 流程图
 *        "③TRJ-04 复检"的被检几何输入；WP-16-T07 批引入）。
 *
 * 为什么需要接口而不是路点序列：平滑产物是连续曲线（五次样条），复检
 * 细分协议要在任意参数 s∈[0,1] 处求构型（二分加密采样）——离散路点只
 * 能表达折线，无法承载平滑后的真实几何；若复检按折线检查而执行走样条，
 * 复检就失真（检查对象≠执行对象）。因此平滑产物以"可求值几何"交付：
 *   - Smoothed：SmoothOutcome.geometry＝五次 Hermite 曲线求值器；
 *   - NoChange/作废：调用方对本头工厂 makeLinearJointPathGeometry 以原
 *     路点构造折线求值器（几何未变→复检样本计划可沿用既有结论——§11.1
 *     "零变化零复检"由调用方以路点集等值判定，本域不代判）。
 *
 * 参数化契约：s∈[0,1] 为归一化路径参数；s=0/1 恒映射到路径端点构型
 * （端点强制不变的后置——实现保证）。s 的中间取值与路点的对应关系由
 * 实现定义（本域样条：结点 u_i=i 均匀归一化，s_k=k/(n-1) 处恒等于第
 * k 路点——插值性，测试钉扎）。
 *
 * 线程语义：消费面按 const 只读调用；实现方若带缓存须自行保证并发安全
 * 或声明单线程（本域产品实现为无状态纯计算——并发只读安全）。
 */
class IPathGeometry {
public:
    virtual ~IPathGeometry() = default;

    /**
     * @brief 求路径参数 s 处的构型（复检细分采样的唯一求值入口）。
     *
     * @param s [in] 归一化路径参数，∈[0,1]（无量纲）；越界输入属调用方
     *              违约——实现方可断言拒绝（本域产品实现钳位检查后
     *              fail-fast，不静默截断——NFR-COR-03）
     *
     * @return 该参数处的权威关节构型（转动 rad／移动 m；链序）
     *
     * 确定性：同 s → 等价输出（NFR-COR-02——复检细分计划的确定性基础）；
     * 纯计算零副作用。
     */
    virtual rw::math::Q sampleAt(double s) const = 0;
};

/**
 * @brief 构造线性折线求值器（未平滑路径的段几何——§11.1"零变化零复检"
 *        路径与复检基准的公共实现点）。
 *
 * 几何语义：s∈[0,1] 均匀映射到路点索引区间——n 个路点把路径分为 n-1 个
 * 等参子段，子段内逐轴线性插值（§7.3 interpolateJointLinear 同语义；此
 * 处独立实现按参数域均匀划分，不消费 Ptp.hpp 的 s∈[0,1] 单段插值——
 * 两处语义不同：本工厂跨多路点）。s=0/1 位级等于首/末路点；s=i/(n-1)
 * 位级等于第 i 路点（插值性）。
 *
 * @param waypoints [in] 路点序列（≥2 个；同维度、全分量有限——违约抛
 *                TrajectoryError，token "trajectory/smooth/geometry-*"，
 *                fail-fast——NFR-COR-03）
 *
 * @return 折线求值器（调用方持有 shared_ptr；无状态纯计算——并发只读
 *         安全；确定性）
 *
 * @throws TrajectoryError 路点数 <2 / 维度不一致 / 含非有限分量
 */
std::shared_ptr<const IPathGeometry> makeLinearJointPathGeometry(
    const std::vector<rw::math::Q>& waypoints);

// =====================================================================
// 平滑请求/结果（§15.7 提议签名的字段面——全部值语义）
// =====================================================================

/**
 * @brief 平滑结局（§15.7 status 词表四态＋取消态——封闭词表）。
 *
 * 词表演进登记（DTB §5.4，单元卡 §1.2 T07 注同步）：卡面四态基础上表尾
 * 增列 Canceled——§15.0"取消＝非错误，返回 cancelled 语义"要求长计算接
 * 口携带取消承载；平滑几何计算可被取消观测中断（请求带 CancelSignal），
 * 词表无取消态则取消只能伪装成其他态（违约 UX-03），故表尾追加（既有四
 * 值语义零变化，不收窄不扩大）。
 */
enum class SmoothStatus : std::uint8_t {
    /// 平滑成功（几何保持验证通过）——geometry 非空（样条曲线），偏差记
    /// 录携带实测值；调用方必须据此触发复检（§11.1 强制边）。
    Smoothed,
    /// 零变化（路点 ≤2 或内部点全为必经点——无可平滑/剔除对象）：沿用户
    /// 输入返回，geometry 为空（调用方以 makeLinearJointPathGeometry 构造
    /// 折线或直接沿用既有复检结论——§11.1"零变化零复检"分支）。
    NoChange,
    /// 几何保持容差被违反且迭代耗尽（§11.4"偏差超容差→该次平滑作废"）：
    /// 产物＝原路径（平滑弃用）、geometry 为空、偏差记录携带末次尝试实测
    /// 值（证据面据此出比较型素材）。单次尝试（maxIterations==1）失败与
    /// 多次收缩重试全部失败的本态语义一致——"作废"。
    ToleranceViolated,
    /// 收缩重试上限内未找到达标强度（§11.3"连续 m 次平滑失败"的迭代上限
    /// 语义面）：与 ToleranceViolated 的区分＝失败发生在重试序列耗尽而非
    /// 单次判定——编排面以本态累计"连续平滑失败次数"（m 计录入诊断的数
    /// 据源），产物同样＝原路径。
    GiveUpAfterRetries,
    /// 取消观测命中（UX-03——零错误素材、零产物；failure 面不携带任何
    /// 失败定位）。
    Canceled,
};

/**
 * @brief 几何保持偏差记录（§11.4"几何保持验证入证据（偏差实际值/容差比
 *        较型字段）"的数据面——证据素材的比较型三要素由此构造）。
 *
 * 偏差定义：平滑产物曲线对**原折线**（简化后路点的线性插值——§11.1 步
 * 骤①产物）的逐轴最大绝对偏差（关节维，转动 rad／移动 m 逐轴混合计量
 * ——与 SegmentConstraint 的量纲口径一致）；TCP 派生维仅在请求提供 FK
 * 端口时检查（TCP 位置最大偏差，m）。
 *
 * "为什么关节维与 TCP 维分离"：平滑是关节空间的几何操作（§11.4），关节
 * 维偏差是平滑强度的直接度量；TCP 派生维是 FK 派生观察（§6.2/§7.6"纯
 * 关节路径不得伪装成笛卡尔路径"——TCP 偏差只作为额外的几何保真检查，
 * 不产生笛卡尔连续性证据）。FK 端口缺席时 TCP 维显式 NotApplicable
 * （tcpChecked=false——不冒充已检查，NFR-COR-03/P-POL-11 同款纪律）。
 * 值语义纯结构；线程安全。
 */
struct SmoothDeviationReport {
    /// 关节维逐轴最大绝对偏差（rad|m；对照容差＝请求的 smoothToleranceJoint）。
    double jointAxisMaxDeviation = 0.0;
    /// 关节维容差（rad|m；请求值透传——证据面构造比较型字段用）。
    double toleranceJoint = 0.0;
    /// TCP 派生维是否已检查（false＝FK 端口缺席——检查 NotApplicable）。
    bool tcpChecked = false;
    /// TCP 位置最大偏差（m；仅 tcpChecked 时有效——对照
    /// smoothToleranceTcp）。
    double tcpMaxDeviation = 0.0;
    /// TCP 维容差（m；请求值透传）。
    double toleranceTcp = 0.0;
    /// 采纳的收缩因子 λ（无量纲，(0,1]；1＝全强度中心差分——算法迭代
    /// 记录，确定性回放锚；NoChange/失败态＝末次尝试值或 0）。
    double adoptedShrinkFactor = 0.0;

    bool operator==(const SmoothDeviationReport& o) const noexcept
    {
        return jointAxisMaxDeviation == o.jointAxisMaxDeviation
            && toleranceJoint == o.toleranceJoint && tcpChecked == o.tcpChecked
            && tcpMaxDeviation == o.tcpMaxDeviation && toleranceTcp == o.toleranceTcp
            && adoptedShrinkFactor == o.adoptedShrinkFactor;
    }
    bool operator!=(const SmoothDeviationReport& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 简化/平滑请求（§15.7 提议签名的字段面；前置违约一律 fail-fast，
 *        token "trajectory/smooth/..."）。
 *
 * 前置（§15.7 与 §15.0 纪律的逐项落点）：
 *   - waypoints ≥2 个、同维度、全分量有限（候选路径——T04~T06 产物）；
 *   - mandatoryIndices 下标 ∈ [0, waypoints.size()) 且严格升序无重复
 *     （必经点保护集——§5.6 必经状态的请求侧投影：Start/TaskPoint/End
 *     由调用方按路点 kind 落入本集；端点恒被保护，无须列入）；
 *   - smoothToleranceJoint/smoothToleranceTcp 有限且 >0（rad|m 与 m——
 *     §5.5 双域容差，调用方自 config.trj 解析后传入，本域零默认值）；
 *   - maxIterations ≥1（收缩重试上限——配置进身份的字段，§15.7）；
 *   - fkPort 可空（空＝TCP 派生维检查 NotApplicable——显式标记而非
 *     伪造，见 SmoothDeviationReport 注）。
 *
 * 单位/坐标系：构型为权威关节向量（q_authoritative 换算归装配面——§6.4，
 * 本层零二次换算）。值语义纯结构；线程安全（借用指针在调用期存活即可）。
 */
struct SmoothRequest {
    /// 候选路径路点序列（权威角 rad|m；≥2——折线与平滑的几何基准）。
    std::vector<rw::math::Q> waypoints;
    /// 必经点保护集（waypoints 下标，严格升序——简化不可剔除；端点恒
    /// 保护，无须列入）。
    std::vector<std::size_t> mandatoryIndices;
    /// 关节维几何保持容差（rad|m；>0 有限——§5.5 smoothToleranceJoint）。
    double smoothToleranceJoint = 0.0;
    /// TCP 派生维几何保持容差（m；>0 有限——§5.5 smoothToleranceTcp）。
    double smoothToleranceTcp = 0.0;
    /// 收缩重试上限（无量纲计数，≥1——§15.7"迭代上限（配置，进身份）"）。
    std::uint32_t maxIterations = 1;
    /// FK 端口（可空——TCP 派生维检查的消费面；§15.4 IKinematicsCompute-
    /// Port 的 evaluateFk 半区。端口接口方法为非 const 形态（§15.4 签名
    /// 基线），本域消费面以非 const 裸指针借持、不接管生命周期——AGENTS
    /// §2.5 所有权标注）。
    IKinematicsComputePort* fkPort = nullptr;
    /// 取消观测（可空＝不可取消；剔除/迭代循环边界轮询——§15.1 纪律）。
    CancelSignal cancel;
};

/**
 * @brief 简化/平滑结果（§15.7 输出词表的落地面；值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Smoothed：smoothPath＝简化+平滑后的路点序列（插值结点——样条过全
 *     部结点）、geometry＝样条求值器（非空）、deviation＝实测偏差（达标
 *     值）＋采纳 λ；**调用方必须以 geometry 触发复检**（§11.1 强制边——
 *     几何已变化）；
 *   - NoChange/ToleranceViolated/GiveUpAfterRetries：smoothPath＝原路点
 *     （作废/零变化语义）、geometry 为空、deviation 携带实测值（NoChange
 *     时偏差恒 0）；
 *   - Canceled：smoothPath 为空、geometry 为空、deviation 不携带有效值
 *     （默认值——零素材，UX-03）。
 */
struct SmoothOutcome {
    /// 平滑结局（SmoothStatus 五态）。
    SmoothStatus status = SmoothStatus::Canceled;
    /// 产物路点序列（平滑后保留的插值结点——Smoothed 时为简化+平滑产物；
    /// 其余状态为原路点或空——见结构注联动表）。
    std::vector<rw::math::Q> smoothPath;
    /// 产物几何求值器（仅 Smoothed 非空——复检的被检几何输入）。
    std::shared_ptr<const IPathGeometry> geometry;
    /// 几何保持偏差记录（§11.4 证据素材的数据面）。
    SmoothDeviationReport deviation;
    /// 失败定位素材（ToleranceViolated/GiveUpAfterRetries 时必填——phase
    /// 恒 kPhaseSmooth；Canceled 恒空——UX-03；Smoothed/NoChange 为空）。
    std::optional<FailedSegmentRecord> failure;
};

// =====================================================================
// §15.7 平滑管线入口
// =====================================================================

/**
 * @brief 简化并平滑候选路径（§15.7 签名——TRJ-04"对路径进行简化和平滑"
 *        的唯一执行点）。
 *
 * 执行序（每步语义见行内注释；算法细节与实现补全登记见 Smooth.cpp 头注）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——token
 *      "trajectory/smooth/..."）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 简化：贪心逐点剔除内部非必经点（保留端点与必经集）——剔除判据：
 *      被剔点对替代线性插值的逐轴偏差 ≤ smoothToleranceJoint（FK 端口
 *      在场时叠加 TCP 位置偏差 ≤ smoothToleranceTcp）；
 *   4. 无可平滑对象（路点 ≤2 或内部全必经）→ NoChange（§11.1"零变化零
 *      复检"分支）；
 *   5. 平滑：分段五次 Hermite 样条（结点＝简化后路点、端点值/导数强制
 *      不变、结点二阶导零→C²），收缩因子 λ 从 1 按固定序列重试；
 *   6. 几何保持验证：曲线对简化后折线的逐轴最大偏差 ≤ 容差（TCP 维同
 *      理）→ 首个达标 λ 采纳为 Smoothed；
 *   7. 迭代耗尽仍未达标 → 作废（ToleranceViolated/GiveUpAfterRetries，
 *      产物＝简化后路点，偏差记录末次实测）。
 *
 * 本函数不消费 policy（碰撞/阈值零触碰——复检协议的职责）、不判任务可行
 * 性（N7）；限速/限加速度不在几何阶段判定（§7.4——唯一判定点在 §12.2）。
 *
 * @param request [in] 平滑请求（SmoothRequest 前置见其注；违约抛
 *                TrajectoryError——fail-fast）
 *
 * @return 平滑结果（SmoothOutcome 五态；Smoothed 必触发复检由调用方执行
 *         ——本头产物 geometry 即复检输入）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/smooth/..."）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性（剔
 * 除序/导数构造/收缩序列全确定——NFR-COR-02；同请求等价输出）。
 */
SmoothOutcome smooth(const SmoothRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_SMOOTH_HPP
