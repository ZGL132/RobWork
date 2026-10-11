/**
 * @file   TimeParam.hpp
 * @brief  时间参数化与节拍（§12/§15.8 TimeParameterize——TRJ-05"使用至少
 *         加速度连续的运动律按关节速度、加速度限制进行时间参数化，输出
 *         总节拍和分段时间"的唯一执行点；WP-16-T08 批；WP-16-T14 批经
 *         §12.7 扩展点纳入 TRJ-08-S1 关节 jerk 限值约束）。
 *
 * 设计依据：
 *   - units/trajectory.md §12.1（运动律——R1 唯一实现＝关节空间五次样条，
 *     分段五次多项式、结点 C² 匹配；端点边界条件：起点/终点零速度零加
 *     速度；驻留结点两侧导数为零；**jerk 连续属 R2（TRJ-08-S1）——
 *     WP-16-T14 批经 §12.7 运动律词表递进落地：septic-spline-c3 七次样
 *     条 C³**）、§12.2（限值校验与时间缩放——唯一判定点：初值时间分配
 *     →构造样条→采样求峰值→峰值 vs 限值（限值＝CanonicalJoint.maxVe-
 *     locity/maxAcceleration×limitsScaleFactor，+inf 语义＝runtime 显式
 *     设值，本域零补值；**WP-16-T14 批 jerk 维度：限值视图增 jerkLimit
 *     消费通道**）→超限等比放大时间轴重算至满足或迭代上限；校验容差＝
 *     附录 D 第 10 项相对 1×10⁻⁹（固定）经 core::closeWithin/allClose-
 *     Within 逐元素判定＋C7 运行校验 ε_abs（速度/加速度按量纲 1×10⁻⁹；
 *     **jerk 纯相对——C7 量纲默认表无 jerk 条目，见实现 TU 头注登记**）；
 *     超限定位入 FailedSegmentRecord）、§12.3（多段同步与驻留——段端边
 *     界条件衔接结点 C²；驻留段＝常值段，时间计入总节拍与分段时长）、
 *     §12.4（节拍输出——totalDurationS＝全部段＋全部驻留；segmentDura-
 *     tionsS 逐段对齐；**无时间参数化结果时不得伪造节拍**——失败态不
 *     输出 0 或估计值，NFR-COR-03；为 OPT-D 节拍指标唯一来源）、§12.5
 *     （时间化后的契约义务——速度/加速度连续性检查在时间化完成后全链
 *     执行；**v2 运动律追加 jerk 连续守卫**）、§12.6（超限与失败定位——
 *     迭代达上限仍超速→TRJ-TIME-PARAM-FAILED＋TRJ-LIMIT-EXCEEDED 比较
 *     型；限值未定义→+inf 语义→时间无界→TRJ-TIME-PARAM-FAILED 素材
 *     〔原因＝限值未定义；建议动作＝补模型限值〕，不伪造节拍）、§12.7
 *     （可扩展维度——motionLawToken 封闭词表 {quintic-spline-c2,
 *     septic-spline-c3}；分派点按 token 分派、词表外 fail-fast 零空分支）
 *   - §15.8（TimeParameterize 接口基线——签名/输出三态 {Ok, LimitUnreach-
 *     able, LawFailed}、校验行"附录 D 第 10 项容差逐元素校验；速度/加
 *     速度连续性（§9.1）"、合法示例"五次样条 C² 结点匹配、峰值≤限值×
 *     (1+1e-9 相对容差内)→Ok"、非法示例"限值全 +inf→LimitUnreachable
 *     （素材，不伪造节拍）"；"本域内部接口实现任务允许按 DTB §5.4 微
 *     调并登记偏差"）、§15.0（通用约定——错误二分/取消/确定性/线程/
 *     副作用）、§9.1（连续性维度表——速度/加速度连续＝结点两侧差≤容
 *     差〔1×10⁻⁹ 相对——附录 D 第 10 项同源〕；时间边界＝knotTimesS 单
 *     调、端点对齐；驻留＝零速零加速度常值段）、§13.1（峰值统计口径——
 *     全轨迹逐关节最大绝对值）
 *   - 需求 TRJ-05（P0——本头即其 R1 承载面）、TRJ-06（限制超标定位——
 *     素材轨）、**TRJ-08-S1（P1/R2——jerk 上限约束：时间参数化纳入关节
 *     jerk 限值；验收＝限值满足断言＋超限段定位诊断）**、NFR-COR-01（解
 *     析算例对照——黄金算例独立书写公式）、NFR-COR-02（确定性——同输
 *     入等价输出：初值分配/中心差分/缩放序列全确定，零随机源）、
 *     NFR-COR-03（非法输入拒绝，不钳制不静默）、ARC-05（限值零私有副
 *     本——本域只做"限值视图×缩放系数"的即时计算，不落任何阈值常量）
 *   - 任务契约 tasks/foundation/WP-16-T08.json（acceptance 1/2——五次
 *     样条 C² 结点匹配用例；限速校验容差按附录 D 第 10 项；总节拍与分
 *     段时间输出〔含驻留〕为 OPT-D 节拍指标唯一来源）、tasks/foundation/
 *     WP-16-T14.json（acceptance 1——限值满足断言＋超限段定位诊断；
 *     acceptance 2——扩展点消费经 EvaluationDims 词表、零空字段/零占位）
 *
 * 背景说明（时间参数化在轨迹链路中的位置——第一读者须知）：
 *   T04~T07 产出的段计划/平滑产物只有**几何**（路径形状），没有时间——
 *   "多快走完这条路径"由本头决定。几何与时间解耦（§7.3 原文"PTP 段的
 *   几何不因限值改变"）：本头在冻结几何上叠加一条单调时间轴，使逐关节
 *   速度/加速度峰值不超限值（附录 D 第 10 项容差内），并输出总节拍与分
 *   段时间（TRJ-05 交付物；optimization 域 OPT-D 的节拍指标唯一来源——
 *   §19.2 接口交接行）。运动律（§12.1）两形态，由请求的 motionLawToken
 *   分派（§12.7 分派点——每 token 一个真实实现分支，词表外拒绝）：
 *
 *   **quintic-spline-c2（R1，TRJ-05）**：分段五次 Hermite 多项式
 *   （h0000/h0010/h0001/h0011 四基——结点二阶导恒取 0，与 Smooth.cpp
 *   §11.4 同一数学纪律：相邻段在结点处二阶导同值 0→整体 C²，"至少加
 *   速度连续"结构性成立）。限值校验二维（速度/加速度——TRJ-05 行原文
 *   口径），jerk 限值不消费（请求携带有限 jerkLimit＝调用方契约违约，
 *   fail-fast——拒绝优于静默忽略，NFR-COR-03）。
 *
 *   **septic-spline-c3（WP-16-T14 批，TRJ-08-S1）**：分段七次 Hermite
 *   多项式（八基——结点二阶/三阶导恒取 0 的构造纪律：相邻段在结点处
 *   二阶导同值 0→C²、三阶导同值 0→**整体 C³（jerk 连续）**，§12.1"jerk
 *   连续属 R2"的结构性成立形态）。限值校验三维（速度/加速度/jerk——
 *   TRJ-08-S1"时间参数化纳入关节 jerk 限值"）。
 *
 *   两形态共用的结点纪律：结点（knot）＝段边界＋段内关键路点＋驻留端
 *   点；结点值＝几何求值器在该处的构型（平滑产物经 IPathGeometry.
 *   sampleAt——T07 产物形态即本头输入形态，在既有形态上扩展不推翻）；
 *   结点一阶导（对时间）：普通内部结点＝时间中心差分
 *   (q_{k+1}−q_{k−1})/(t_{k+1}−t_{k−1})；端点＝0（§12.1 边界条件）；驻
 *   留结点＝0（§12.1"驻留结点两侧导数为零"——驻留段自身是常值多项式，
 *   两端速度/加速度恒 0，§9.1）。高阶导（二阶/三阶）恒 0——无自由度，
 *   确定性补全（T08 批登记纪律的七次推广）。
 *
 *   时间自由度求解（§12.2）：初值时间分配（按段逐轴路径长度/最严关节
 *   估计）→构造样条→按采样计划求逐关节峰值→峰值超限则**等比放大时间
 *   轴**（全部运动段时长同乘 α>1；驻留时长不缩放——驻留是工况事件语义
 *   非运动学量）→重算至达标或达迭代上限。缩放的数学性质（实现收敛性依
 *   据）：样条形状只依赖结点值与结点导数，等比缩放下结点导数精确 ÷α、
 *   段多项式（归一化参数 u=τ/h 下）不变→速度峰值精确 ÷α、加速度峰值精
 *   确 ÷α²、**jerk 峰值精确 ÷α³**（三阶导链式法则 ÷h³）——与采样密度
 *   无关，一轮缩放即精确达标，迭代循环实为"检测→缩放→复检"确认环。
 *
 * 头文件依赖纪律（冒烟模式安全——Smooth.hpp 同款）：本头对 rw 只消费
 * header-only 数学头（Q）；无 policy/runtime 依赖；timeParameterize 的
 * 定义在实现 TU——src/TimeParam.cpp 为集成模式条件源（Q 构造面 gating
 * 同 Ptp.cpp，冒烟口径不受影响）。
 *
 * 线程安全：全部值类型并发只读安全；timeParameterize 为纯函数（零副作
 * 用/零修订/零写盘——§15.0），单线程使用；ITimeCurve 产品实现为无状态
 * 纯计算（并发只读安全）。确定性：同请求等价输出（NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_TIMEPARAM_HPP
#define IRD_TRAJECTORY_TIMEPARAM_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/core/Identity.hpp>     // core::ObjectId（失败素材 subject——ARC-04）
#include <sdurws/ird/trajectory/PlanConfig.hpp>  // kTimeParamMethodQuinticSplineC2（运动律词表——§12.7 唯一书写点）
#include <sdurws/ird/trajectory/Smooth.hpp>   // IPathGeometry（段几何求值器——T07 产物形态即输入形态）
#include <sdurws/ird/trajectory/TrjTypes.hpp> // CancelSignal/FailedSegmentRecord/WaypointKind（域内值面）

namespace sdurws::ird::trajectory {

// =====================================================================
// 域常量（唯一书写点——算法/测试共用，禁第二处字面量）
// =====================================================================

/**
 * @brief 限速校验的相对容差（附录 D 第 10 项——"时间参数化限制校验容差
 *        （速度/加速度上限校验）：相对 1×10⁻⁹，类别＝固定"，TRJ-05/
 *        AT-06 行）。达标判定＝core::closeWithin(峰值, 限值, {1e-9, ε_abs})
 *        ——峰值在限值的 (1+1e-9) 相对＋C7 ε_abs 绝带内视为达标（不触发
 *        缩放、不产超限素材）。绝对分量经 core::runtimeAbsoluteTolerance
 *        取 C7 转写（速度/加速度量纲默认 1×10⁻⁹——D-06 单点转写，本头
 *        不私设数值）。固定容差一经交付只随需求变更（附录 D 类别列）。
 */
inline constexpr double kTimeParamLimitRelativeTolerance = 1e-9;

// =====================================================================
// 限值视图（§5.3 CanonicalJoint 投影——零私有副本，ARC-05）
// =====================================================================

/**
 * @brief 单关节速度/加速度/jerk 限值视图（§5.3"速度/加速度约束＝
 *        CanonicalJoint.maxVelocity/maxAcceleration（SourcedValue<double>，
 *        SI：rad/s、rad/s²；未显式设值＝+inf）"的时间化消费投影＋
 *        TRJ-08-S1 jerk 限值消费通道）。
 *
 * 为什么是投影而不是模型引用：R-1 红线（业务域互链禁止——卡 §3.2）禁止
 * trajectory include runtime/modeling 头；宿主（评估装配面）把已解析模
 * 型的关节限值投影为本结构序列注入（O-37 同款纪律——PtpCandidate 先例
 * "宿主把上游对象解析为投影值后经工厂闭包注入，单元侧只消费投影值"）。
 * 本域不解析模型、不补值：+inf 原样表示"模型未设限值"（runtime 显式设
 * 值语义），时间化遇 +inf 限值→LimitUnreachable（§12.6——时间无界，不
 * 伪造节拍）。
 *
 * jerkLimit 的消费语义（WP-16-T14 批——TRJ-08-S1，§12.7 限值消费扩展）：
 *   - septic-spline-c3（v2 运动律）：消费——有效限值＝jerkLimit×
 *     limitsScaleFactor；+inf（未设值）→LimitUnreachable（与速度/加速
 *     度缺限值同源纪律：时间参数化的目的是求"满足全部限值的最短时间"，
 *     缺任一限值维度则"最短"无定义——§12.6 行 3 同源）；
 *   - quintic-spline-c2（v1 运动律，TRJ-05 口径"按关节速度、加速度限制"）：
 *     **不消费**——携带任何有限 jerkLimit＝调用方契约违约 fail-fast
 *     （拒绝优于静默忽略：调用方显式要求 jerk 约束而运动律不支持，静默
 *     忽略会使"配置写了什么"与"算了什么"脱钩，NFR-COR-03）。
 *
 * 默认值取舍：jerkLimit 默认 +inf（＝未设值）而 velocity/acceleration
 * 默认 0.0（强制显式填）——既有调用方（聚合初始化两字段）在本字段引入
 * 后语义自动为"未设 jerk 限值"，与 TRJ-08-S1 启用前的世界状态一致（
 * CanonicalJoint 无 jerk 字段——模型侧数据链归 WP-10 支持批），零破坏
 * 演进；0.0 不是合法限值（合法域见下），不能作默认。
 *
 * 合法域：各字段或为有限正值（>0），或为 +inf（未设值）；NaN/负值/零→
 * 调用方契约违约（fail-fast，投影面数据违约）。单位：转动关节 rad/s、
 * rad/s²、rad/s³；移动关节 m/s、m/s²、m/s³（逐轴按关节类型，SI 真值）。
 * 值语义纯结构；线程安全。
 */
struct JointDynamicLimits {
    /// 关节速度限值（rad/s 或 m/s，逐轴按关节类型；+inf＝模型未设值）。
    double velocityLimit = 0.0;
    /// 关节加速度限值（rad/s² 或 m/s²，逐轴按关节类型；+inf＝模型未设值）。
    double accelerationLimit = 0.0;
    /// 关节 jerk 限值（rad/s³ 或 m/s³，逐轴按关节类型；+inf＝模型未设值
    /// ——本结构默认，消费语义见结构注"jerkLimit 的消费语义"）。
    double jerkLimit = std::numeric_limits<double>::infinity();

    bool operator==(const JointDynamicLimits& o) const noexcept
    {
        return velocityLimit == o.velocityLimit
            && accelerationLimit == o.accelerationLimit
            && jerkLimit == o.jerkLimit;
    }
    bool operator!=(const JointDynamicLimits& o) const noexcept
    {
        return !(*this == o);
    }
};

// =====================================================================
// 时间采样点与时间参数化产物（§6.2 TimedSample/TimeParameterization 行
// ——T08 增列的 §6.2 消费面；字段与卡面基线一致）
// =====================================================================

/**
 * @brief 时间采样点（§6.2 TimedSample 行——时间参数化轨迹的离散表达，
 *        样条系数的导出视图，两者并存且同源）。
 *
 * 单位/坐标系（AGENTS §2.5）：q 为权威关节向量（转动 rad／移动 m）；
 * qd 为关节速度（rad/s 或 m/s，逐轴按关节类型）；qdd 为关节加速度
 * （rad/s² 或 m/s²）；t 为自轨迹起点起算的时刻（s，非递减）。tcpPose
 * 为 TCP 位姿（基座系 {B}——T_world_base 之后的设备链）：本批 FK 派生
 * 面不启用（§7.6"纯关节路径不得伪装成笛卡尔路径"——本域时间化不消费
 * FK 端口），恒 nullopt 且语义＝"未计算"，不冒充笛卡尔证据；动画的
 * TCP 视图归 T10 导出/动画批消费 FK 端口后另行产出。值语义纯结构；
 * 线程安全。
 */
struct TimedSample {
    /// 时刻（s；自轨迹起点起算，非递减——§6.2 原文）。
    double t = 0.0;
    /// 关节位置（SI；rad|m 逐轴按关节类型）。
    rw::math::Q q;
    /// 关节速度（SI；rad/s 或 m/s，逐轴按关节类型）。
    rw::math::Q qd;
    /// 关节加速度（SI；rad/s² 或 m/s²，逐轴按关节类型）。
    rw::math::Q qdd;
    /// TCP 位姿（基座系 {B}；FK 派生观察——本批恒 nullopt，见结构注）。
    std::optional<rw::math::Transform3D<double>> tcpPose;

    bool operator==(const TimedSample& o) const
    {
        return t == o.t && q == o.q && qd == o.qd && qdd == o.qdd
            && tcpPose == o.tcpPose;
    }
    bool operator!=(const TimedSample& o) const { return !(*this == o); }
};

/**
 * @brief 时间参数化产物（§6.2 TimeParameterization 行——运动律系数＋采
 *        样视图；"运动律词表"＝§12.7 封闭词表两值）。
 *
 * 不变量（实现保证，测试钉扎）：
 *   - motionLawToken 恒等于请求的合法 token（词表两值之一——§12.7 按
 *     token 分派，词表外请求在校验即拒绝，产物不携带词表外值）；
 *   - knotTimesS 严格递增、首元素＝0、末元素＝totalDurationS（端点对齐
 *     段边界——§9.1 时间边界行）；结点集合＝段边界＋段内关键路点＋驻
 *     留端点（§12.1 结点集语义）；
 *   - samples 覆盖 [0, totalDurationS]（采样计划：事件对齐点〔全部结
 *     点时刻〕必含＋均匀步长填充，步长≤请求 sampleStepS；采样计划入
 *     身份——§6.3，由调用方以请求字段经 config/policy 投影承载）；
 *   - totalDurationS＝Σ运动段时长＋Σ驻留时长（§12.4——总节拍，OPT-D
 *     唯一来源）。
 *
 * 样条系数本身不落本结构（分段 Hermite 段表是内部实现细节）——连续求
 * 值面经 TimeParamResult.timeCurve（ITimeCurve）交付，离散视图与本结构
 * 同源（同一样条求值）。值语义纯结构；线程安全（并发只读）。
 */
struct TimeParameterization {
    /// 运动律 token（§12.7 封闭词表两值之一："quintic-spline-c2"（TRJ-05
    /// ／"septic-spline-c3"（TRJ-08-S1，WP-16-T14 批）——回填请求值，
    /// 消费面据此选择连续求值面与证据语义）。
    std::string motionLawToken;
    /// 结点时刻（s；严格递增，端点对齐段边界——见结构注不变量）。
    std::vector<double> knotTimesS;
    /// 统一采样视图（事件对齐＋均匀步长——见结构注不变量）。
    std::vector<TimedSample> samples;
    /// 总时长（s）＝全部运动段＋全部驻留（§12.4；＝knotTimesS.back()）。
    double totalDurationS = 0.0;

    bool operator==(const TimeParameterization& o) const
    {
        return motionLawToken == o.motionLawToken && knotTimesS == o.knotTimesS
            && samples == o.samples && totalDurationS == o.totalDurationS;
    }
    bool operator!=(const TimeParameterization& o) const
    {
        return !(*this == o);
    }
};

// =====================================================================
// 时间曲线求值接口（连续视图——C² 检查/动画/导出的公共消费面）
// =====================================================================

/**
 * @brief 时间曲线求值接口（时间参数化产物的连续视图——§12.5"速度/加
 *        速度连续性检查在时间化完成后全链执行"的检查面与 §14.5.1 动画
 *        数据的复用面）。
 *
 * 为什么需要接口而不是只有离散 samples：C² 结点匹配（acceptance 1）的
 * 严格验证需要"结点两侧任意邻域"的导数求值——离散采样视图只能给采样
 * 点值；且复检后的动画/导出（T10）按任意时刻查构型。本接口把样条连续
 * 求值暴露为域内最小面：sampleAt(t) 返回 t 时刻的位置/速度/加速度。
 *
 * 求值契约：t∈[0, totalDurationS]；结点时刻处取**左段极限**（文档化消
 * 毒点——C² 保证左右极限位级相等，取哪侧无观测差异；本域产品实现取左
 * 侧，段序二分定位）。确定性：同 t → 位级同输出（NFR-COR-02）。线程语
 * 义：消费面按 const 只读调用；产品实现无状态纯计算（并发只读安全）。
 */
class ITimeCurve {
public:
    virtual ~ITimeCurve() = default;

    /**
     * @brief 求时刻 t 处的采样点（位置/速度/加速度——样条及其一、二阶
     *        时间导数）。
     *
     * @param t [in] 时刻，单位 s，∈[0, totalDurationS]；越界输入属调用
     *              方违约——实现 fail-fast（不静默截断——NFR-COR-03）
     *
     * @return 采样点（tcpPose 恒 nullopt——TimedSample 注）
     *
     * 确定性：同 t → 位级同输出；纯计算零副作用。
     */
    virtual TimedSample sampleAt(double t) const = 0;
};

// =====================================================================
// 请求/结果（§15.8 提议签名的字段面——全部值语义）
// =====================================================================

/**
 * @brief 时间参数化的单段输入条目（段计划的时间化投影——"段序列（冻
 *        结几何）"的承载形态）。
 *
 * 两种条目（二选一，构造纪律见 TimeParamRequest 校验序）：
 *   - **运动段**：geometry 非空（T04~T07 产物形态——折线经
 *     makeLinearJointPathGeometry、平滑产物经 SmoothOutcome.geometry），
 *     dwellDurationS 恒 0；interiorS 可选携带段内关键路点的归一化参数
 *     （§12.1"结点集＝段边界＋驻留点＋（可选）段内关键路点"——平滑后
 *     保留路点由调用方按 IPathGeometry 的参数化契约换算为 s 值）；
 *   - **驻留段**：geometry 为空且 dwellDurationS>0（s——工况 Dwell 事件
 *     的时长，§5.4 步骤 3；时间轴上的常值段，§9.1"驻留＝零速零加速度
 *     常值段"；interiorS 恒空）。
 *
 * segmentIndex 与 Trajectory.segments 段序号对齐（§6.3 定位键——失败素
 * 材/节拍分段时间共用）；sourceTaskPoint 为失败素材 subject 的投影（可
 * 空——无来源站时 nullopt）。值语义纯结构（geometry 为借持 shared_ptr，
 * 调用期存活即可）；线程安全。
 */
struct TimeParamSegment {
    /// 段序号（0 基，全轨迹单调连续——§6.3 定位键）。
    std::uint32_t segmentIndex = 0;
    /// 冻结几何求值器（运动段非空——sampleAt(s) 的 s∈[0,1] 为归一化路
    /// 径参数；驻留段恒空）。
    std::shared_ptr<const IPathGeometry> geometry;
    /// 段后不适用：本字段仅驻留段携带时长（s；>0），运动段恒 0（语义二
    /// 分见结构注——"驻留段是独立条目"而非运动段附件，使 segmentDura-
    /// tionsS 逐段对齐〔含驻留段〕，§12.4/§13.1）。
    double dwellDurationS = 0.0;
    /// 段内关键路点参数（s∈(0,1) 严格升序去重；运动段可选——§12.1 结点
    /// 集的段内扩展；驻留段恒空）。
    std::vector<double> interiorS;
    /// 来源任务点（失败素材 subject 投影；可空——§6.2 FailedSegmentRecord）。
    std::optional<core::ObjectId> sourceTaskPoint;

    bool operator==(const TimeParamSegment& o) const
    {
        return segmentIndex == o.segmentIndex && geometry == o.geometry
            && dwellDurationS == o.dwellDurationS && interiorS == o.interiorS
            && sourceTaskPoint == o.sourceTaskPoint;
    }
    bool operator!=(const TimeParamSegment& o) const { return !(*this == o); }
};

/**
 * @brief 时间参数化请求（§15.8 TimeParamRequest 的 T08 字段面——全部值
 *        语义）。
 *
 * 前置（§15.8 与 §15.0 纪律的逐项落点，校验序见 timeParameterize）：
 *   - segments 非空；运动段 geometry 非空、interiorS 严格升序∈(0,1) 去
 *     重；驻留段 geometry 空、dwellDurationS>0 有限；相邻运动段端点构
 *     型**位级相等**（几何连续性的最强关节维契约——段连接检查归 Conti-
 *     nuity（§9.3 即时检查），本头作为守卫位级拒绝，不静默缝合）；
 *   - jointLimits 与段几何同维度（关节数）；逐轴合法域见 JointDynamicLimits
 *     （速度/加速度/jerk 三维同规——>0 有限或 +inf；jerk 的 token 相关
 *     消费语义见 JointDynamicLimits 注：v2 消费、v1 拒绝有限值）；
 *   - limitsScaleFactor ∈ (0,1] 且有限（§5.5——保守缩放，1.0＝全限值）；
 *   - motionLawToken ∈ 封闭词表（当前唯一合法值
 *     kTimeParamMethodQuinticSplineC2——§12.7 单分支分派点，无空分支）；
 *   - sampleStepS 有限且 >0（s——采样步长上限；采样计划入身份，§6.3）；
 *   - maxIterations ≥1（缩放迭代上限——§15.8"迭代上限（配置，进身份）"
 *     的请求侧承载；黄金算例声明所用值——§21.4 开放问题"数值随 T08 黄
 *     金算例声明，不进本卡正文"）；
 *   - cancel 可空（空＝不可取消）。
 *
 * 单位/坐标系：全部关节向量＝权威角（q_authoritative 换算归装配面——
 * §6.4，本层零二次换算）；时间一律 s。值语义纯结构；线程安全（借用指
 * 针在调用期存活即可）。
 */
struct TimeParamRequest {
    /// 段序列（运动段/驻留段按时间顺序排列；≥1 条——§15.8"段序列（冻
    /// 结几何）"）。
    std::vector<TimeParamSegment> segments;
    /// 逐关节限值视图（CanonicalJoint 投影——JointDynamicLimits 注；
    /// 维度＝段几何关节数；jerk 维度消费语义 token 相关——v2 消费、v1
    /// 拒绝有限值，见该结构注）。
    std::vector<JointDynamicLimits> jointLimits;
    /// 限值使用比例（无量纲，(0,1]；§5.5 limitsScaleFactor——有效限值＝
    /// 视图值×本系数，+inf 视图值保持 +inf〔未设值语义不被缩放掩盖〕）。
    double limitsScaleFactor = 1.0;
    /// 运动律 token（§12.7 封闭词表——合法值 {kTimeParamMethodQuinticSplineC2,
    /// kTimeParamMethodSepticSplineC3}；调用方自 config.trj.timeParamMethod
    /// 传入，本域复检一致性；分派点按 token 分派到对应实现分支，词表外
    /// fail-fast）。
    std::string motionLawToken{kTimeParamMethodQuinticSplineC2};
    /// 采样步长上限（s；>0 有限——事件对齐点〔全部结点〕必含＋均匀步
    /// 长填充，§6.2"等步长或事件对齐，采样计划入身份"）。
    double sampleStepS = 0.01;
    /// 缩放迭代上限（无量纲计数，≥1——§15.8；达上限仍超限→LimitUnreach-
    /// able＋比较型素材，§12.6）。
    std::uint32_t maxIterations = 1;
    /// 取消观测（可空＝不可取消；段级/迭代级循环边界轮询——§15.1 纪律）。
    CancelSignal cancel;

    bool operator==(const TimeParamRequest& o) const
    {
        return segments == o.segments && jointLimits == o.jointLimits
            && limitsScaleFactor == o.limitsScaleFactor
            && motionLawToken == o.motionLawToken
            && sampleStepS == o.sampleStepS
            && maxIterations == o.maxIterations;
    }
    bool operator!=(const TimeParamRequest& o) const { return !(*this == o); }
};

/**
 * @brief 时间参数化结局（§15.8 status 词表三态＋取消态——封闭词表）。
 *
 * 词表演进登记（DTB §5.4，单元卡 §22 同批）：卡面三态基础上表尾增列
 * Canceled——§15.0"取消＝非错误，返回 cancelled 语义"要求长计算接口携
 * 带取消承载；时间化可被取消观测中断（缩放迭代/段循环边界轮询），词表
 * 无取消态则取消只能伪装成其他态（违约 UX-03），故表尾追加（既有三值
 * 语义零变化，不收窄不扩大；SmoothStatus 表尾增 Canceled 同款先例）。
 */
enum class TimeParamStatus : std::uint8_t {
    /// 成功——timeParam/segmentDurationsS/峰值统计/曲线求值器全量有效；
    /// 逐关节峰值在限值的附录 D 第 10 项容差内（达标判定语义见
    /// kTimeParamLimitRelativeTolerance 注）。
    Ok,
    /// 限值不可达（§15.8 非法示例"限值全 +inf→LimitUnreachable"；含部分
    /// 关节 +inf——任一关节限值未定义即时间无界）：failureRecords 携带
    /// TRJ-TIME-PARAM-FAILED 素材（原因＝限值未定义；建议动作＝补模型限
    /// 值，§12.6）；**不伪造节拍**——timeParam 为空、时长零输出（§12.4
    /// NFR-COR-03）；另一产面＝迭代缩放达上限仍超限（防御路径——缩放的
    /// 数学精确性使正常输入一轮达标，见实现 TU 头注），此时追加
    /// TRJ-LIMIT-EXCEEDED 比较型素材（逐关节峰值/限值/单位＋段定位）。
    LimitUnreachable,
    /// 运动律失败（样条构造/求值出现非有限中间值——数值病态防御路径；
    /// 正常有限输入不可达）：failureRecords 携带 TRJ-TIME-PARAM-FAILED
    /// 素材＋（守卫检出时）TRJ-CONTINUITY-BROKEN 素材；不伪造节拍。
    LawFailed,
    /// 取消观测命中（UX-03——零错误素材、零产物；failureRecords 恒空）。
    Canceled,
};

/**
 * @brief 时间参数化结果（§15.8 输出行"TimeParamResult{status{Ok, Limit-
 *        Unreachable, LawFailed}, TimeParameterization, 峰值统计, 超限定
 *        位}"的落地面；值语义纯结构；线程安全）。
 *
 * 状态—字段联动（实现保证）：
 *   - Ok：timeParam 非空（§6.2 产物）、segmentDurationsS 与请求段序列逐
 *     条目对齐（运动段＝样条时长、驻留段＝请求时长原值——§12.4 分段时
 *     间逐段对齐含驻留）、峰值统计有效（全轨迹逐关节最大绝对值，§13.1
 *     口径——速度/加速度/jerk 三维；jerk 校验仅 v2 运动律执行，见
 *     peakJointJerk 注）、timeCurve 非空、failureRecords 恒空；
 *   - LimitUnreachable/LawFailed：timeParam 为空（**不伪造节拍**——
 *     §12.4"无时间参数化结果时不得伪造节拍"，调用方不得以 0 或估计值
 *     回填 totalDurationS）、segmentDurationsS 为空、峰值统计不携带有效
 *     值（默认 0 向量——零素材语义）、timeCurve 为空；failureRecords 携
 *     带定位素材（超限类比较型三要素齐备——UX-03）；
 *   - Canceled：全部产物面为空、failureRecords 恒空（零错误素材）。
 *
 * 诊断边界：本结构承载域内素材（FailedSegmentRecord）；DiagnosticRecord
 * 实例（TRJ-* 码经 IDiagnosticFactory、snapshotId/sliceId 必填）归评估
 * 器组装面（WP-16-T09/T10）——§14.4"诊断构造：经 IDiagnosticFactory.create"。
 */
struct TimeParamResult {
    /// 时间参数化结局（TimeParamStatus 四态）。
    TimeParamStatus status = TimeParamStatus::Canceled;
    /// 时间参数化产物（仅 Ok 非空——§6.2 TimeParameterization）。
    std::optional<TimeParameterization> timeParam;
    /// 分段时间（s；仅 Ok 非空——与请求段序列逐条目对齐，含驻留段；
    /// Σ本向量＝timeParam->totalDurationS；§12.4"segmentDurationsS 逐段
    /// 对齐"，OPT-D 节拍指标唯一来源）。
    std::vector<double> segmentDurationsS;
    /// 逐关节峰值速度（rad/s 或 m/s；仅 Ok 有效——全轨迹最大绝对值，
    /// §13.1"采样+样条解析峰值取严"中的采样峰值口径，样条解析峰值经缩
    /// 放精确性等价覆盖，见实现 TU 头注）。
    rw::math::Q peakJointVelocity;
    /// 逐关节峰值加速度（rad/s² 或 m/s²；仅 Ok 有效——口径同上）。
    rw::math::Q peakJointAcceleration;
    /// 逐关节峰值 jerk（rad/s³ 或 m/s³；仅 Ok 有效——口径同上；WP-16-T14
    /// 批增列，TRJ-08-S1"限值满足断言"的观测面：调用方/评估组装以本统
    /// 计对 jerkLimit 作峰值≤限值断言。统计面两运动律统一产出——v1 五次
    /// 样条的 jerk 分段二次多项式在结点处跳变，本统计取采样口径（结点取
    /// 左段极限）与限值检测同口径；**jerk 限值校验本身仅 v2 运动律执行**
    /// ——统计是观察、校验是约束，v1 下 jerk 无约束语义（TRJ-05 口径），
    /// 本字段在 v1 下是纯观察统计，不构成 v1 行为变化（T08 校验/缩放行
    /// 为零改动）。注意 §6.5 守护边界：本字段是 TimeParamResult 的峰值
    /// 统计，不是 TimedSample 成员（"TimedSample 无 jerk 成员"守护不受
    /// 影响——离散采样视图仍只承载 q/qd/qdd；jerk 由样条连续求值面内部
    /// 计算，不入离散样本）。
    rw::math::Q peakJointJerk;
    /// 连续求值面（仅 Ok 非空——ITimeCurve 注；C² 检查/动画/导出的消费
    /// 面；调用方持有 shared_ptr）。
    std::shared_ptr<const ITimeCurve> timeCurve;
    /// 失败/超限定位素材（§12.2 步骤 5"命中关节/时刻/段序号入 FailedSeg-
    /// mentRecord〔比较型字段：实际峰值/限值/单位〕"；仅 LimitUnreachable/
    /// LawFailed 非空——Ok/Canceled 恒空）。
    std::vector<FailedSegmentRecord> failureRecords;

    /// 逐字段相等。注意：timeCurve 为 shared_ptr 成员，按**引用**（指针
    /// 值）比较——"同一求值器实例"判定；跨调用的结果等价性判定应比较
    /// 其余字段（timeParam 的 samples/knotTimesS 等——确定性断言面），
    /// 不比较 timeCurve 指针（两次独立调用必然产生不同实例——NFR-COR-02
    /// 的等价输出指值等价，非实例同一）。
    bool operator==(const TimeParamResult& o) const
    {
        return status == o.status && timeParam == o.timeParam
            && segmentDurationsS == o.segmentDurationsS
            && peakJointVelocity == o.peakJointVelocity
            && peakJointAcceleration == o.peakJointAcceleration
            && peakJointJerk == o.peakJointJerk
            && timeCurve == o.timeCurve && failureRecords == o.failureRecords;
    }
    bool operator!=(const TimeParamResult& o) const { return !(*this == o); }
};

// =====================================================================
// §15.8 时间参数化入口
// =====================================================================

/**
 * @brief 按关节速度/加速度限制对冻结几何做时间参数化（§12/§15.8 签名
 *        ——TRJ-05 的唯一执行点，限值校验唯一判定点〔§12.2〕；v2 运动律
 *        septic-spline-c3 下并纳入关节 jerk 限值——TRJ-08-S1，WP-16-T14
 *        批）。
 *
 * 执行序（每步语义见行内注释；算法细节与实现补全登记见 TimeParam.cpp
 * 头注）：
 *   1. 前置校验（调用方契约违约 → TrajectoryError fail-fast——token
 *      "trajectory/time-param/..."；含运动律词表分派与 jerk 限值的 token
 *      相关消费校验——v1 拒绝有限 jerkLimit）；
 *   2. 取消轮询（命中 → Canceled，零素材）；
 *   3. 限值未定义扫描（§12.6——+inf→时间无界；v2 运动律含 jerk 维度）；
 *   4. 初值时间分配（§12.2 步骤 1——按段逐轴采样路径长度/最严关节估
 *      计，确定性公式见实现 TU；v2 追加 jerk 项）；
 *   5. 构造样条（§12.1 运动律——按 token 分派：quintic 结点 C² 五次
 *      Hermite / septic 结点 C³ 七次 Hermite；端点零速零加速度、驻留结
 *      点两侧导数为零、内部结点时间中心差分、高阶导恒 0）；
 *   6. 采样求峰值（§12.2 步骤 2——事件对齐＋均匀步长，逐关节 qd/qdd
 *      最大绝对值＋qddd〔jerk〕统计——v2 并作限值检测）；
 *   7. 限值校验（§12.2 步骤 3/5——附录 D 第 10 项相对 1×10⁻⁹＋ε_abs
 *      逐元素判定；限值未定义〔+inf〕→LimitUnreachable＋TRJ-TIME-PARAM-
 *      FAILED 素材，不伪造节拍）；
 *   8. 超限→等比放大时间轴（§12.2 步骤 4——α=max(峰值速度比, √峰值加
 *      速度比〔, ∛峰值 jerk 比——v2〕)，全部运动段时长×α、驻留不变）→
 *      重算，至达标或达迭代上限（达上限→LimitUnreachable＋TRJ-LIMIT-
 *      EXCEEDED 比较型素材——正常输入不可达的防御路径，见实现 TU 头注）；
 *   9. 连续性守卫（§12.5/§9.1——结点两侧速度/加速度差全链检查，违约
 *      →LawFailed＋TRJ-CONTINUITY-BROKEN 素材；实现上 C² 由构造结构性
 *      保证，守卫为黄金回归拦截面；v2 运动律追加 jerk 连续第三维——C³
 *      结构承诺的回归拦截面）；
 *  10. 组装产物（TimeParameterization＋segmentDurationsS＋峰值统计〔含
 *      peakJointJerk〕＋timeCurve）→ Ok。
 *
 * 本函数不消费 policy（碰撞/阈值零触碰——复检协议的职责）、不做几何变
 * 换（缩放只改时间轴不改几何路径——§12.2 步骤 4 括注）、不判任务可行
 * 性（N7）；节拍超标（targetCycleTimeS）的 Should 素材归评估器组装
 * （§12.4——本头只交付节拍事实）。
 *
 * @param request [in] 时间参数化请求（TimeParamRequest 前置见其注；违
 *                约抛 TrajectoryError——fail-fast）
 *
 * @return 时间参数化结果（TimeParamResult 四态；值语义）
 *
 * @throws TrajectoryError 调用方契约违约（token "trajectory/time-param/
 *         ..."——段序列/维度/限值视图/token 词表/jerk 限值与运动律的
 *         消费组合〔v1 拒绝有限 jerkLimit〕/采样步长/迭代上限等；
 *         见各校验点）
 *
 * 纯函数（零副作用/零修订/零写盘）；线程安全（单线程使用）；确定性
 * （初值分配/差分/缩放序列全确定——NFR-COR-02；同请求等价输出）。
 */
TimeParamResult timeParameterize(const TimeParamRequest& request);

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_TIMEPARAM_HPP
