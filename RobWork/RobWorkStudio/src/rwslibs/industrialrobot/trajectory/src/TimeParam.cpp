/**
 * @file   TimeParam.cpp
 * @brief  时间参数化算法的唯一实现翻译单元（§12 运动律＋§12.2 限值校验
 *         与时间缩放＋§12.4 节拍输出；WP-16-T08 批）。
 *
 * 设计依据（头文件 TimeParam.hpp 的设计依据此处不重复；本注登记实现补
 * 全——DTB §5.4 偏差/补全登记，单元卡 §22 同批登记）：
 *   - **结点二阶导恒取 0**：§12.1"结点 C² 匹配"的五次样条在两端点边界
 *     条件（端点零速零加速度）与驻留结点条件（两侧导数为零）之外的内部
 *     自由度需要确定性补全——本实现取"结点二阶导恒 0"（与 Smooth.cpp
 *     §11.4"结点二阶导恒取 0（相邻段在结点处二阶导同值→整体 C²）"同
 *     一数学纪律；五次 Hermite 段在结点处加速度两侧同值 0→C² 结构性成
 *     立，段内三阶导可跳——jerk 连续属 R2 不承诺）。端点零加速度与驻
 *     留结点零加速度由该纪律天然满足；驻留结点的"两侧导数为零"对一阶
 *     导单独强制（中心差分只用于普通内部结点）。
 *   - **初值时间分配公式**（§12.2 步骤 1"按段路径长度/最严关节估计"的
 *     确定性展开）：逐运动段以 kInitialLengthProbeSpans 个等参区间采样
 *     段几何，逐轴折线长度 D_i（rad|m 混合计量逐轴）；需求时长按静止-
 *     静止五次剖面的解析峰值系数估计——
 *     t_seg = max( 1.875·max_i(D_i/v_i^eff) , sqrt((10/√3)·max_i(D_i/
 *     a_i^eff)) , kInitialDurationFloorS )——速度项系数 15/8 与加速度项
 *     系数 10/√3 是五次剖面（端点零导）峰值的精确闭式：初值恰使"端点
 *     驱动单段"的峰值在线达标（浮动误差被限值校验 1e-9 相对容差吸收，
 *     不触发虚假缩放），多段耦合/interior 结点的更严需求由缩放环兜底。
 *     探针密度是**初值估计的实现参数**（非协议数值；缩放收敛性与其无
 *     关——见下条）。段内结点区间时长按相邻结点构型的关节空间 L1 距
 *     离比例分配（同源确定性）。
 *   - **缩放收敛性**（§12.2 步骤 4"等比放大时间轴"的实现依据）：样条形
 *     状只依赖结点值与结点导数；全部运动段时长同乘 α 后，中心差分结点
 *     导数精确 ÷α、段多项式在归一化参数 u=τ/h 下不变 ⇒ 速度峰值精确
 *     ÷α、加速度峰值精确 ÷α²（驻留段常值不受影响）。因此 α 由峰值/限
 *     值比直接计算后，一轮缩放即把各量精确带到限值线上（±浮点舍入，被
 *     附录 D 第 10 项 1×10⁻⁹ 容差吸收）——迭代循环实为"检测→缩放→复
 *     检"确认环；"达迭代上限仍超限"是浮点病态防御路径（正常有限输入不
 *     可达），触发时如实产出 TRJ-TIME-PARAM-FAILED＋TRJ-LIMIT-EXCEEDED
 *     比较型素材（§12.6），不静默放行。
 *   - **峰值统计口径**：§13.1"全轨迹逐关节最大绝对值（采样+样条解析峰
 *     值取严）"落为采样峰值——采样含全部结点（解析值：结点速度＝中心
 *     差分/0、结点加速度＝0 位级），段内峰值由均匀网格近似；因缩放的精
 *     确重参数化性质，达标判定在"同一采样计划"下自洽（检测与验证同口
 *     径，§6.3"采样计划入身份"）。
 *   - **相邻段端点位级相等守卫**：§9.3"几何连续性（位置/姿态/分支）：
 *     段构建与段连接时即时检查"归 Continuity（T05）；本头作为时间化的
 *     输入守卫取最强关节维契约——相邻运动段端点构型必须 rw::math::Q 位
 *     级相等（T04~T07 产物形态保证：折线端点＝精确路点、平滑端点强制
 *     不变），位级不等即调用方数据违约 fail-fast，不静默缝合（NFR-COR-03）。
 *   - **C7 ε_abs 的量纲取值**：速度/加速度的 C7 运行校验默认（1×10⁻⁹）
 *     经 core::runtimeAbsoluteTolerance 取值（D-06 单点转写）；转动/移动
 *     两族量纲默认同值，本域限值视图不携带关节类型（投影面最小化），
 *     统一取 AngularVelocity/AngularAcceleration 族转写值——两族同值使
 *     该选择对结果无影响（测试钉扎）。
 *   - **驻留段的显式常值求值**：§9.1"驻留＝零速零加速度常值段"以独立
 *     分支落地（SplineSpan.constant）——不可套用"m=0、q0==q1"的统一
 *     Hermite 公式：位置虽恒定（值基和恒 1 的代数恒等）但加速度为
 *     −60u²(1−u)·q/h²（值基二阶导和不为零），会误触发加速度超限缩放并
 *     违反驻留语义——实现陷阱，测试以驻留区间三零采样钉扎。
 *   - **连续性守卫的极限比较形态**：§12.5"速度/加速度连续性检查在时间
 *     化完成后全链执行"以"相邻段端点极限直接比较"实现（左段 τ=h vs 右
 *     段 τ=0 的解析值）——不取邻域差分：有限差分带 O(δ·jerk) 固有偏差，
 *     任何 jerk≠0 的合法曲线都会被绝对容差误判（实现陷阱；守卫阈值
 *     1e-12 相对/绝对——qd 两侧经不同段长的舍入路径允许 ulp 级噪声，
 *     qdd 两侧数学恒 0）。
 *   - 需求 TRJ-05/06、NFR-COR-01/02/03（见 TimeParam.hpp）；错误二分见
 *     Errors.hpp。
 *
 * 集成模式条件源（Ptp.cpp/Smooth.cpp 同款 gating）：Q 构造析构面消费
 * rw::math::Q 的库内虚析构符号——冒烟模式无框架库可链，不编译本 TU；
 * TimeParam.hpp 的值类型/接口声明两模式皆可编译。
 */

#include <sdurws/ird/trajectory/TimeParam.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Compare.hpp>      // core::closeWithin/Tolerance（附录 D C4 公式）
#include <sdurws/ird/core/Provenance.hpp>   // core::SourcedValue（比较型素材值面）
#include <sdurws/ird/core/Units.hpp>        // core::QuantityKind/UnitToken（C7 转写与单位 token）
#include <sdurws/ird/trajectory/DiagCodes.hpp>  // TRJ-* 码常量（素材 reasonToken 唯一书写点）
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast 载体）

namespace sdurws::ird::trajectory {
namespace {

// =====================================================================
// 算法内部常量（实现参数——非协议数值，依据见文件头注登记）
// =====================================================================

/// 初值时间分配的逐段等参探针区间数（无量纲计数；仅影响首轮峰值估计的
/// 精度——缩放收敛性与初值无关，见文件头注登记）。
inline constexpr std::size_t kInitialLengthProbeSpans = 8;

/// 运动段初值时长下限（s）：零行程段/限值极大时的防退化下限，保证结点
/// 时刻严格递增（§9.1 时间边界）；非协议数值（分析配置面由调用方的
/// sampleStepS/maxIterations 主导，本值仅防数值退化）。
inline constexpr double kInitialDurationFloorS = 1e-6;

/// 静止-静止五次剖面的解析峰值速度系数（15/8＝1.875——h0001'(1/2) 的精
/// 确值；初值估计的"速度项"系数：h ≥ 系数·D/v 时峰值速度 ≤ v）。
inline constexpr double kQuinticPeakVelocityFactor = 1.875;

/// 静止-静止五次剖面的解析峰值加速度系数（10/√3≈5.7735——h0001'' 极值
/// 的精确闭式；初值估计的"加速度项"系数：h ≥ sqrt(系数·D/a) 时峰值加
/// 速度 ≤ a）。√3 以正确舍入的十进制字面书写（constexpr 要求编译期常
/// 量——std::sqrt 非 constexpr）：IEEE 754 的 sqrt 运算同为正确舍入，
/// 该字面与 std::sqrt(3.0) 位级一致（测试侧以 std::sqrt(3.0) 独立书写
/// 同一闭式，两处位级一致使"初值恰达限值线不触发虚假缩放"的钉扎成立）。
inline constexpr double kQuinticPeakAccelFactor =
    10.0 / 1.7320508075688772935274463415059;

// =====================================================================
// 五次 Hermite 基函数族（u∈[0,1] 归一化段参数；值/一阶/二阶导数——
// Smooth.cpp 值基族的超集，本 TU 需要速度/加速度求值故三阶并列；独立
// 书写于实现侧，测试侧另有黄金参照——NFR-COR-01 双向对照）
// =====================================================================

/// 单基三阶值（对归一化参数 u 的位置/一阶/二阶导数——无量纲基值）。
struct BasisValues {
    double p;    ///< 基函数值
    double d1;   ///< 对 u 的一阶导数
    double d2;   ///< 对 u 的二阶导数
};

/// 值基 h0000：端点条件 p(0)=1，其余五条件全 0（1−10u³+15u⁴−6u⁵）。
inline BasisValues basisH0000(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    BasisValues b;
    b.p = 1.0 - 10.0 * u3 + 15.0 * u2 * u2 - 6.0 * u3 * u2;
    b.d1 = -30.0 * u2 + 60.0 * u3 - 30.0 * u2 * u2;
    b.d2 = -60.0 * u + 120.0 * u2 - 60.0 * u3;
    return b;
}

/// 一阶导基 h0010：端点条件 p'(0)=1，其余五条件全 0（u−6u³+8u⁴−3u⁵）。
inline BasisValues basisH0010(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    BasisValues b;
    b.p = u - 6.0 * u3 + 8.0 * u2 * u2 - 3.0 * u3 * u2;
    b.d1 = 1.0 - 18.0 * u2 + 32.0 * u3 - 15.0 * u2 * u2;
    b.d2 = -36.0 * u + 96.0 * u2 - 60.0 * u3;
    return b;
}

/// 值基 h0001：端点条件 p(1)=1，其余五条件全 0（10u³−15u⁴+6u⁵）。
inline BasisValues basisH0001(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    BasisValues b;
    b.p = 10.0 * u3 - 15.0 * u2 * u2 + 6.0 * u3 * u2;
    b.d1 = 30.0 * u2 - 60.0 * u3 + 30.0 * u2 * u2;
    b.d2 = 60.0 * u - 180.0 * u2 + 120.0 * u3;
    return b;
}

/// 一阶导基 h0011：端点条件 p'(1)=1，其余五条件全 0（−4u³+7u⁴−3u⁵）。
inline BasisValues basisH0011(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    BasisValues b;
    b.p = -4.0 * u3 + 7.0 * u2 * u2 - 3.0 * u3 * u2;
    b.d1 = -12.0 * u2 + 28.0 * u3 - 15.0 * u2 * u2;
    b.d2 = -24.0 * u + 84.0 * u2 - 60.0 * u3;
    return b;
}

// =====================================================================
// 样条内部数据（结点表＋段表——构建期可变、提交即冻结的域中间态）
// =====================================================================

/**
 * @brief 全局结点（§12.1 结点集＝段边界＋段内关键路点＋驻留端点）。
 *
 * t 严格递增（s）；q 为结点构型（权威角 rad|m）；dwellBoundary 标记该
 * 结点与驻留区间相邻（一阶导强制 0——§12.1"驻留结点两侧导数为零"）；
 * segmentIndex 记录所属请求段（超限定位素材的段序号来源）。
 */
struct SplineKnot {
    double t = 0.0;                 ///< 结点时刻（s；全表严格递增）
    rw::math::Q q;                  ///< 结点构型（rad|m）
    bool dwellBoundary = false;     ///< 与驻留区间相邻→一阶导强制 0
    std::uint32_t segmentIndex = 0; ///< 所属请求段序号（定位键）
};

/**
 * @brief 样条段（全局结点表的相邻结点对之间的五次 Hermite 区间）。
 *
 * 时长 h>0（s）；m0/m1 为结点一阶时间导（dq/dt，rad/s|m/s）；s0<s1 为
 * 该区间在所属请求段内的归一化路径参数（超限定位 pathParameter 的换算
 * 基准；驻留段 s0/s1 无路径语义，置 0）。二阶导不落字段——结点二阶导
 * 恒 0 的构造纪律使其无自由度（文件头注登记）。constant 标记驻留常值
 * 段：**必须显式分支求值**（q 恒、qd/qdd 恒 0）——若套用 m=0、q0==q1
 * 的统一 Hermite 公式，位置虽恒定（值基和恒 1）但加速度为
 * −60u²(1−u)·q/h²（值基二阶导和不为零），违反 §9.1"驻留＝零速零加速
 * 度常值段"并会误触发加速度超限缩放——该陷阱以常值分支消除。
 */
struct SplineSpan {
    double t0 = 0.0;                ///< 段起始时刻（s）
    double h = 0.0;                 ///< 段时长（s；>0）
    rw::math::Q q0;                 ///< 起结点构型（rad|m）
    rw::math::Q q1;                 ///< 末结点构型（rad|m）
    rw::math::Q m0;                 ///< 起结点一阶时间导（rad/s|m/s）
    rw::math::Q m1;                 ///< 末结点一阶时间导（rad/s|m/s）
    std::uint32_t segmentIndex = 0; ///< 所属请求段序号（定位键）
    double s0 = 0.0;                ///< 区间起点的段内归一化路径参数（无量纲）
    double s1 = 0.0;                ///< 区间终点的段内归一化路径参数（无量纲）
    bool constant = false;          ///< 驻留常值段（显式零导数求值——见结构注）
};

/**
 * @brief 段描述（构建期中间产物——结点表下标对＋s 区间；导数计算后经
 *        一次性组装展开为 SplineSpan 段表）。
 *
 * 为什么需要中间描述：中心差分求结点导数需要**全局**结点表（相邻结点
 * 跨段共享——段边界结点只入表一次），而段表组装又依赖导数；三阶段（建
 * 表→导数→组装）之间用本结构携带"哪个结点对构成哪个区间＋该区间在请
 * 求段内的 s 范围"。
 */
struct SpanDesc {
    std::size_t knotA = 0;          ///< 起结点在全局结点表中的下标
    std::size_t knotB = 0;          ///< 末结点在全局结点表中的下标
    std::uint32_t segmentIndex = 0; ///< 所属请求段序号（定位键）
    double s0 = 0.0;                ///< 区间起点的段内归一化路径参数
    double s1 = 0.0;                ///< 区间终点的段内归一化路径参数
    bool constant = false;          ///< 驻留常值段（显式零导数求值——SplineSpan 注）
};

/**
 * @brief 段内求值（τ∈[0,h]；五次 Hermite 三阶合成——逐轴 double 循环避
 *        免 Q 临时对象，确定性算术序）。
 *
 * 数学（文件头注运动律）：q(τ)=h0000·q0+h0010·h·m0+h0001·q1+h0011·
 * h·m1；qd=q'(u)/h；qdd=q''(u)/h²（u=τ/h；二阶导基项恒缺——结点二
 * 阶导 0 纪律）。单位换算：m 的积（h·m0）量纲 rad、除 h² 后 qdd 得
 * rad/s²——链式量纲自洽。
 *
 * 匿名命名空间自由函数（非方法）：连续性守卫需要对同一函数取"段端点极
 * 限"（u=1 与 u=0——两侧极限的直接比较，见守卫环注），故与曲线类解耦。
 */
TimedSample evaluateSpan(const SplineSpan& span, double tau)
{
    const std::size_t n = static_cast<std::size_t>(span.q0.size());
    TimedSample sample;
    sample.t = span.t0 + tau;
    // 注意（框架语义）：rw::math::Q(n) 单参构造**不零初始化**（Eigen 动
    // 态向量默认构造语义）——零导数面（qd/qdd）必须显式 Q::zero，否则
    // 常值段/边界结点携带未定义值（实现陷阱，登记于单元卡 §22）。
    sample.q = rw::math::Q::zero(n);
    sample.qd = rw::math::Q::zero(n);
    sample.qdd = rw::math::Q::zero(n);
    // 驻留常值段：显式零导数求值（§9.1"驻留＝零速零加速度常值段"——
    // 不可套用 Hermite 公式，见 SplineSpan 结构注的陷阱登记）；q0/q1
    // 位级相等（构建保证），取 q0。
    if (span.constant) {
        sample.q = span.q0;
        return sample;  // qd/qdd 已零初始化（TimedSample 构造面）
    }
    // τ 钳位检查（段内舍入可能给出 τ=−ε/+ε——钳位到 [0,h] 而非越
    // 界抛错：本调用点 t 已在总区间内，段边界舍入属数值噪声，静默
    // 归边不影响位级可复现性；与 sampleAt 的越界 fail-fast 分工——
    // 那里是调用方语义越界，这里是实现内数值归边）。
    const double u = std::clamp(tau / span.h, 0.0, 1.0);
    const BasisValues b00 = basisH0000(u);
    const BasisValues b10 = basisH0010(u);
    const BasisValues b01 = basisH0001(u);
    const BasisValues b11 = basisH0011(u);
    // 逐轴合成：四基加权（位置）＋导数基加权后除 h、h²（时间导数链
    // 式法则）——每轴独立的标量算术，无跨轴归约（确定性算术序）。
    for (std::size_t i = 0; i < n; ++i) {
        const double q0 = span.q0[i];
        const double q1 = span.q1[i];
        const double hm0 = span.h * span.m0[i];  // h·m0：量纲 rad|m
        const double hm1 = span.h * span.m1[i];
        sample.q[i] = b00.p * q0 + b10.p * hm0 + b01.p * q1 + b11.p * hm1;
        sample.qd[i] = (b00.d1 * q0 + b10.d1 * hm0 + b01.d1 * q1
                        + b11.d1 * hm1)
                       / span.h;
        sample.qdd[i] = (b00.d2 * q0 + b10.d2 * hm0 + b01.d2 * q1
                         + b11.d2 * hm1)
                        / (span.h * span.h);
    }
    return sample;
}

/**
 * @brief 时间曲线求值器（ITimeCurve 产品实现——段表二分定位＋五次
 *        Hermite 三阶求值；无状态纯计算，并发只读安全）。
 *
 * 求值语义（TimeParam.hpp ITimeCurve 契约的落地）：t∈[t_first, t_last]
 * 越界 fail-fast；结点时刻取左段极限（二分 upper_bound−1——C² 保证左
 * 右极限位级相等，取侧无观测差异）；驻留段经统一 Hermite 公式退化为常
 * 值（m=0 且 q0==q1 时多项式代数恒等于常值——无需特判分支，减少分叉
 * 即减少不一致面）。
 */
class QuinticTimeCurve final : public ITimeCurve {
public:
    /// 构造（段表构建期组装后一次冻结；spans 非空且 h>0——构建面保证）。
    explicit QuinticTimeCurve(std::vector<SplineSpan> spans)
        : m_spans(std::move(spans))
    {
    }

    TimedSample sampleAt(double t) const override
    {
        // 越界＝调用方契约违约：负时刻/超总时长都没有"钳位到端点"的合
        // 理语义（§15.0 NFR-COR-03——不静默截断，fail-fast 暴露调用侧
        // 病态；端点 t==totalDurationS 合法，落末段 u=1）。
        const double tFirst = m_spans.front().t0;
        const double tLast = m_spans.back().t0 + m_spans.back().h;
        if (!(t >= tFirst && t <= tLast)) {
            throw TrajectoryError(
                "trajectory/time-param/curve-range",
                "时刻越界：请求 t=" + std::to_string(t) + " s，合法域 ["
                    + std::to_string(tFirst) + ", " + std::to_string(tLast)
                    + "] s（ITimeCurve 求值契约）");
        }
        // 二分定位：upper_bound 找到第一个 t0 > t 的段，取其前一段（结
        // 点时刻命中时恰为左段——取左极限语义）。
        const auto it = std::upper_bound(
            m_spans.begin(), m_spans.end(), t,
            [](double value, const SplineSpan& s) { return value < s.t0; });
        // t≥tFirst 保证 it≠begin；防御断言（不可达——越界已拒绝）。
        if (it == m_spans.begin()) {
            throw TrajectoryError("trajectory/time-param/curve-locate",
                                  "内部定位失败（不可达路径——实现缺陷）");
        }
        const SplineSpan& span = *std::prev(it);
        return evaluateSpan(span, t - span.t0);
    }

private:
    /// 段表（构建期一次冻结；求值只读——并发安全）。
    std::vector<SplineSpan> m_spans;
};

// =====================================================================
// 前置校验（调用方契约违约 fail-fast——校验序固定，NFR-COR-02）
// =====================================================================

/**
 * @brief 请求合法域全表校验（timeParameterize 步骤 1；非法即抛——文案
 *        含首错定位与实际值）。
 *
 * 校验序（确定性——同一坏请求必报同一首错）：
 *   1. motionLawToken ∈ 封闭词表（§12.7 当前唯一值）；
 *   2. segments 非空；条目二分形态（运动段：geometry 非空＋dwell==0＋
 *      interiorS 合法；驻留段：geometry 空＋dwell>0 有限＋interiorS 空）；
 *   3. 首条目必须为运动段、驻留段前一条目必须为运动段（常值段语义附着
 *      于运动——无前置运动的驻留没有可驻留的构型）；
 *   4. 相邻运动段端点构型位级相等（文件头注登记的守卫）；
 *   5. jointLimits 与段几何同维度、逐轴合法域（>0 有限或 +inf）；
 *   6. limitsScaleFactor ∈ (0,1] 有限；
 *   7. sampleStepS 有限且 >0（s）；
 *   8. maxIterations ≥1。
 */
void validateRequest(const TimeParamRequest& request)
{
    // 1. 运动律词表（§12.7 单分支分派点——词表外 token 无实现对应，拒
    //    绝而非静默回退：静默回退会使"配置写了什么"与"算了什么"脱钩）。
    if (request.motionLawToken != kTimeParamMethodQuinticSplineC2) {
        throw TrajectoryError(
            "trajectory/time-param/motion-law",
            "运动律 token 非法：请求 \"" + request.motionLawToken
                + "\"，封闭词表当前唯一合法值 \""
                + std::string(kTimeParamMethodQuinticSplineC2)
                + "\"（§12.7——扩展走单元卡增量修订＋实现＋黄金算例同批）");
    }
    // 2. 段序列非空。
    if (request.segments.empty()) {
        throw TrajectoryError("trajectory/time-param/segments-empty",
                              "段序列为空（§15.8——时间参数化至少需要一"
                              "个段条目）");
    }
    // 关节数基准（首个运动段确定；后续逐段校验同维）。
    std::size_t jointCount = 0;
    bool haveJointCount = false;
    // 上一运动段的末构型（位级连续守卫的参照；驻留段不更新——其构型恒
    // 等于前运动段末构型）。
    std::optional<rw::math::Q> lastMotionEnd;
    bool lastWasDwell = false;
    for (const TimeParamSegment& seg : request.segments) {
        if (seg.geometry == nullptr) {
            // —— 驻留段条目 ——
            // 形态：时长 >0 有限（§9.1 驻留＝时间轴常值段——零时长驻留
            // 无事件语义）；interiorS 恒空（常值段无路径参数）。
            if (!(seg.dwellDurationS > 0.0)
                || !std::isfinite(seg.dwellDurationS)) {
                throw TrajectoryError(
                    "trajectory/time-param/dwell-duration",
                    "驻留段时长非法：segmentIndex="
                        + std::to_string(seg.segmentIndex) + "，dwellDurationS="
                        + std::to_string(seg.dwellDurationS) + " s（要求 >0"
                        " 且有限）");
            }
            if (!seg.interiorS.empty()) {
                throw TrajectoryError(
                    "trajectory/time-param/dwell-interior",
                    "驻留段携带段内关键路点：segmentIndex="
                        + std::to_string(seg.segmentIndex)
                        + "（常值段无路径参数——interiorS 恒空）");
            }
            // 3. 驻留前必须有运动段（且不允许连续驻留——两段常值时间轴
            //    连续等价于一个更长驻留，要求调用方合并以保 segmentDura-
            //    tionsS 的段语义单一）。
            if (haveJointCount == false || lastMotionEnd.has_value() == false
                || lastWasDwell) {
                throw TrajectoryError(
                    "trajectory/time-param/dwell-lead",
                    "驻留段前一条目必须为运动段：segmentIndex="
                        + std::to_string(seg.segmentIndex)
                        + "（首条目驻留或连续驻留均非法）");
            }
            lastWasDwell = true;
            continue;
        }
        // —— 运动段条目 ——
        // 段内关键路点参数：s∈(0,1) 开区间、有限、严格升序去重（§12.1
        // 结点集的段内扩展；开区间——端点由段边界结点承担，重复 s 会产
        // 生零长结点区间破坏时刻严格递增）。
        double prevS = 0.0;
        for (double s : seg.interiorS) {
            if (!std::isfinite(s) || !(s > 0.0) || !(s < 1.0)) {
                throw TrajectoryError(
                    "trajectory/time-param/interior-range",
                    "段内关键路点参数非法：segmentIndex="
                        + std::to_string(seg.segmentIndex) + "，s="
                        + std::to_string(s) + "（要求 ∈(0,1) 且有限）");
            }
            if (!(s > prevS)) {
                throw TrajectoryError(
                    "trajectory/time-param/interior-order",
                    "段内关键路点参数须严格升序去重：segmentIndex="
                        + std::to_string(seg.segmentIndex) + "，s="
                        + std::to_string(s) + " 未大于前值 "
                        + std::to_string(prevS));
            }
            prevS = s;
        }
        // 采样端点构型（几何求值器的调用契约：s∈[0,1]——越界由其拒绝）。
        const rw::math::Q startQ = seg.geometry->sampleAt(0.0);
        const rw::math::Q endQ = seg.geometry->sampleAt(1.0);
        const std::size_t n = static_cast<std::size_t>(startQ.size());
        if (n == 0) {
            throw TrajectoryError(
                "trajectory/time-param/geometry-empty",
                "段几何构型维度为 0：segmentIndex="
                    + std::to_string(seg.segmentIndex));
        }
        if (endQ.size() != startQ.size()) {
            throw TrajectoryError(
                "trajectory/time-param/geometry-dim",
                "段几何端点维度不一致：segmentIndex="
                    + std::to_string(seg.segmentIndex) + "，起点 "
                    + std::to_string(startQ.size()) + " 轴、终点 "
                    + std::to_string(endQ.size()) + " 轴");
        }
        if (!haveJointCount) {
            jointCount = n;
            haveJointCount = true;
        } else if (n != jointCount) {
            throw TrajectoryError(
                "trajectory/time-param/geometry-dim",
                "段几何关节数与首运动段不一致：segmentIndex="
                    + std::to_string(seg.segmentIndex) + "，实际 "
                    + std::to_string(n) + " 轴、基准 " + std::to_string(jointCount)
                    + " 轴");
        }
        // 4. 相邻运动段端点位级相等（rw::math::Q operator== 精确逐轴比
        //    较——T04~T07 产物形态保证位级可满足，见文件头注登记）。
        if (lastMotionEnd.has_value() && !(*lastMotionEnd == startQ)) {
            throw TrajectoryError(
                "trajectory/time-param/knot-mismatch",
                "相邻运动段端点构型位级不等：segmentIndex="
                    + std::to_string(seg.segmentIndex)
                    + " 起点与前段终点不一致（几何连续性守卫——段连接检查"
                    "归 Continuity，本处要求位级相等；禁止静默缝合）");
        }
        lastMotionEnd = endQ;
        lastWasDwell = false;
    }
    // 5. 限值视图维度与合法域（+inf＝未设值合法；NaN/≤0＝投影面数据违
    //    约——CanonicalJoint 语义里限值不存在 SourcedValue 无效态直通，
    //    非正限值没有工程意义）。
    if (request.jointLimits.size() != jointCount) {
        throw TrajectoryError(
            "trajectory/time-param/limits-dim",
            "限值视图维度与段几何不一致：jointLimits="
                + std::to_string(request.jointLimits.size()) + " 轴、几何 "
                + std::to_string(jointCount) + " 轴");
    }
    for (std::size_t i = 0; i < request.jointLimits.size(); ++i) {
        const JointDynamicLimits& lim = request.jointLimits[i];
        const bool vOk = std::isinf(lim.velocityLimit)
                         || (std::isfinite(lim.velocityLimit)
                             && lim.velocityLimit > 0.0);
        const bool aOk = std::isinf(lim.accelerationLimit)
                         || (std::isfinite(lim.accelerationLimit)
                             && lim.accelerationLimit > 0.0);
        if (!vOk || !aOk) {
            throw TrajectoryError(
                "trajectory/time-param/limits-domain",
                "限值视图非法（关节 " + std::to_string(i) + "）：velocity="
                    + std::to_string(lim.velocityLimit)
                    + "，acceleration=" + std::to_string(lim.accelerationLimit)
                    + "（要求 >0 有限或 +inf＝未设值；NaN/零/负拒绝）");
        }
    }
    // 6. 限值使用比例（§5.5 (0,1]）。
    if (!std::isfinite(request.limitsScaleFactor)
        || !(request.limitsScaleFactor > 0.0)
        || !(request.limitsScaleFactor <= 1.0)) {
        throw TrajectoryError(
            "trajectory/time-param/scale-factor",
            "limitsScaleFactor 非法：实际 "
                + std::to_string(request.limitsScaleFactor)
                + "（要求 ∈(0,1] 且有限——§5.5）");
    }
    // 7. 采样步长（s）。
    if (!std::isfinite(request.sampleStepS) || !(request.sampleStepS > 0.0)) {
        throw TrajectoryError(
            "trajectory/time-param/sample-step",
            "sampleStepS 非法：实际 " + std::to_string(request.sampleStepS)
                + " s（要求 >0 且有限——采样计划入身份，§6.3）");
    }
    // 8. 缩放迭代上限。
    if (request.maxIterations < 1U) {
        throw TrajectoryError("trajectory/time-param/max-iterations",
                              "maxIterations 非法：实际 "
                                  + std::to_string(request.maxIterations)
                                  + "（要求 ≥1——§15.8 迭代上限）");
    }
}

// =====================================================================
// 限值未定义判定与失败素材组装（§12.6——不伪造节拍）
// =====================================================================

/**
 * @brief 扫描限值视图中的未设值（+inf）；命中返回其下标，否则 nullopt。
 *
 * §12.6"时间参数化输入缺限值（模型未设限速）：+inf 语义→时间无界"——
 * 任一关节任一量缺失即整链不可时间化（部分时间化没有可交付语义：节拍
 * 是全轨迹量）。前置校验已保证视图无 NaN/非正值，本扫描只面对 +inf。
 */
std::optional<std::size_t> findUndefinedLimit(const TimeParamRequest& request)
{
    for (std::size_t i = 0; i < request.jointLimits.size(); ++i) {
        if (std::isinf(request.jointLimits[i].velocityLimit)
            || std::isinf(request.jointLimits[i].accelerationLimit)) {
            return i;
        }
    }
    return std::nullopt;
}

/**
 * @brief 组装"限值未定义"失败素材（TRJ-TIME-PARAM-FAILED；全轨迹级——
 *        segmentIndex=0xFFFFFFFF＋phase 说明，§6.2 FailedSegmentRecord 注）。
 *
 * 非超限类素材：comparison 不填（"超限类素材必填"——本类是限值缺失，
 * 没有"实际 vs 期望"的数值对）；cause/recommendedAction 中文（ERR-01）。
 */
FailedSegmentRecord makeUndefinedLimitRecord(std::size_t jointIndex)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = 0xFFFFFFFFu;              // 全轨迹级失败（§6.2）
    rec.phaseToken = kPhaseTimeParam;            // "time-param"（§14.1.6）
    rec.reasonToken = std::string(kTrjTimeParamFailed);
    rec.cause = "限值未定义：关节 " + std::to_string(jointIndex)
                + " 的速度或加速度限值为 +inf（模型未显式设值）——时间无界，"
                  "不产出节拍（§12.6/NFR-COR-03 不伪造）";
    rec.recommendedAction = "为该关节补设 CanonicalJoint.maxVelocity/"
                            "maxAcceleration（模型侧显式设值）后重新评估";
    return rec;
}

// =====================================================================
// C7 ε_abs 取值（D-06 单点转写——两族量纲同值的钉扎依据见登记）
// =====================================================================

/**
 * @brief 速度校验的绝对容差（rad/s 或 m/s 共用；core::runtimeAbsolute-
 *        Tolerance 的 AngularVelocity 族转写——C7 默认 1e-9）。
 *
 * 量纲声明：限值视图不携带关节类型（投影面最小化），转动（AngularVe-
 * locity）与移动（LinearVelocity）两族的 C7 默认同为 1×10⁻⁹，本域统一
 * 取 Angular 族——选择对结果无影响（测试以两族同值钉扎，默认值变更须
 * 走需求变更并同步本注）。
 */
inline double velocityAbsTolerance()
{
    const std::optional<double> eps =
        core::runtimeAbsoluteTolerance(core::QuantityKind::AngularVelocity);
    // C7 转写对已声明量纲恒有值（core 侧 D-06 表）——nullopt 属实现缺陷。
    if (!eps.has_value()) {
        throw TrajectoryError("trajectory/time-param/tolerance-missing",
                              "core C7 转写缺失 AngularVelocity 默认（实现"
                              "缺陷——D-06 表不完整）");
    }
    return *eps;
}

/// 加速度校验的绝对容差（rad/s² 或 m/s² 共用；口径同上——Angular 族）。
inline double accelerationAbsTolerance()
{
    const std::optional<double> eps = core::runtimeAbsoluteTolerance(
        core::QuantityKind::AngularAcceleration);
    if (!eps.has_value()) {
        throw TrajectoryError("trajectory/time-param/tolerance-missing",
                              "core C7 转写缺失 AngularAcceleration 默认（实"
                              "现缺陷——D-06 表不完整）");
    }
    return *eps;
}

/**
 * @brief 限速达标的单向判定（附录 D 第 10 项＋C7 公式的单向应用：
 *        peak ≤ limit·(1+1×10⁻⁹)＋ε_abs——峰值越过限值在 1×10⁻⁹ 相对
 *        ＋C7 ε_abs 内视为达标，不触发缩放；§15.8 合法示例"峰值≤限值×
 *        (1+1e-9 相对容差内)→Ok"的逐元素执行面）。
 *
 * 为什么不是 core::closeWithin：C4 公式 |value−reference| ≤ ε_rel·|ref|
 * ＋ε_abs 是**双向一致性**判定（值与参考几乎重合）——限值校验是**单向
 * 上界**判定：峰值明显低于限值（采样欠估/保守估计——网格峰值恒 ≤ 解析
 * 峰值）是合法达标态，双向判定会把它误判为"不达标"并引发 α<1 的错误
 * 缩放（实现陷阱，登记于单元卡 §22）。本函数取 C4 公式的单项不等式形
 * 态（reference=限值、仅约束上侧），逐元素由调用方循环执行；卡面 §12.2
 * "经 core::closeWithin/allCloseWithin 逐元素判定"的纪律以同公式单向
 * 化承接——连续性守卫（两侧极限一致性）仍用 closeWithin 双向语义。
 */
bool withinLimit(double peak, double limit, double absTol)
{
    return peak <= limit * (1.0 + kTimeParamLimitRelativeTolerance) + absTol;
}

// =====================================================================
// 比较型素材的构造辅助（UX-03 三要素——来源标记与单位口径单点化）
// =====================================================================

/**
 * @brief 派生量的来源标记（本域对轨迹产物的派生统计量——非用户直输入，
 *        ValueProvenance 五类词表 DerivedReadOnly；methodTag 标注产生面，
 *        Smooth.cpp "smooth-deviation" 同款纪律）。
 */
inline core::ValueProvenance derivedProvenance(const char* methodTag)
{
    return core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly,
                                       {}, {}, methodTag);
}

/**
 * @brief 取速度单位 token（"rad/s"——core 注册表编译期冻结；find 失败属
 *        core 注册表缺陷，Ptp.cpp/Smooth.cpp 同款 fail-fast 口径）。
 *
 * 量纲说明：限值视图不携带关节类型（投影面最小化），转动/移动速度的比
 * 较型素材单位在 Token 层不同（rad/s vs m/s）而本域素材统一以转动族标
 * 注——与 velocityAbsTolerance 的量纲取舍同源（两族 C7 默认同值，标注取
 * 一族并在测试钉扎；量纲歧义不影响数值语义，SI 真值口径见 §6.2）。
 */
inline core::UnitToken velocityUnitToken()
{
    const auto token = core::UnitToken::find("rad/s");
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/time-param/unit-token",
                              "core 单位注册表缺少 rad/s token（环境缺陷，"
                              "fail-fast——比较型素材单位口径不可用）");
    }
    return *token;
}

/// 取加速度单位 token（"rad/s^2"——口径同 velocityUnitToken）。
inline core::UnitToken accelerationUnitToken()
{
    const auto token = core::UnitToken::find("rad/s^2");
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/time-param/unit-token",
                              "core 单位注册表缺少 rad/s^2 token（环境缺陷，"
                              "fail-fast——比较型素材单位口径不可用）");
    }
    return *token;
}

/**
 * @brief 组装单条超限素材（TRJ-LIMIT-EXCEEDED 比较型——实际峰值/限值/
 *        单位＋段定位 segmentIndex/pathParameter/subject；§12.2 步骤 5、
 *        §12.6 表行 1）。
 *
 * @param segmentIndex [in] 峰值所在请求段序号
 * @param pathParam    [in] 峰值所在段内归一化路径参数（无量纲，[0,1]）
 * @param subject      [in] 段来源任务点投影（可空——直接透传）
 * @param jointIndex   [in] 超限关节（文案定位）
 * @param quantityName [in] 量名（"速度"/"加速度"——中文文案面）
 * @param peak         [in] 实测峰值（SI；rad/s|m/s 或 rad/s²|m/s²）
 * @param limit        [in] 有效限值（视图×缩放系数；同上量纲）
 * @param action       [in] 中文建议动作（ERR-01）
 */
FailedSegmentRecord makeLimitExceededRecord(std::uint32_t segmentIndex,
                                            double pathParam,
                                            std::optional<core::ObjectId> subject,
                                            std::size_t jointIndex,
                                            const char* quantityName,
                                            double peak, double limit,
                                            const core::UnitToken& unit,
                                            const char* action)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = segmentIndex;
    rec.phaseToken = kPhaseTimeParam;  // "time-param"（§14.1.6 词表）
    rec.reasonToken = std::string(kTrjLimitExceeded);
    rec.pathParameter = pathParam;
    rec.subject = std::move(subject);
    rec.cause = std::string("关节 ") + std::to_string(jointIndex) + " 峰值"
                + quantityName + "超限（缩放迭代达上限仍超——§12.6）";
    rec.recommendedAction = action;
    core::ComparativeFields cmp;
    cmp.actual.quantity =
        core::SourcedValue<double>::provided(peak, derivedProvenance("time-param-peak"));
    cmp.actual.unit = unit;
    cmp.expected.quantity =
        core::SourcedValue<double>::provided(limit, derivedProvenance("time-param-limit"));
    cmp.expected.unit = unit;
    rec.comparison = std::move(cmp);
    return rec;
}

}  // namespace

// =====================================================================
// §15.8 时间参数化入口（执行序见 TimeParam.hpp 函数注）
// =====================================================================

TimeParamResult timeParameterize(const TimeParamRequest& request)
{
    // ---- 步骤 1：前置校验（调用方契约违约 fail-fast——validateRequest）。
    validateRequest(request);

    // ---- 步骤 2：取消轮询（命中 → Canceled，零素材——UX-03）。
    if (request.cancel && request.cancel()) {
        TimeParamResult result;
        result.status = TimeParamStatus::Canceled;
        return result;
    }

    // ---- 步骤 6 前置半区：限值未定义扫描（§12.6——+inf→时间无界，在
    // 任何构造前快速失败；素材轨返回，不抛不伪造节拍）。
    if (const std::optional<std::size_t> undefined = findUndefinedLimit(request);
        undefined.has_value()) {
        TimeParamResult result;
        result.status = TimeParamStatus::LimitUnreachable;
        result.failureRecords.push_back(makeUndefinedLimitRecord(*undefined));
        return result;
    }

    // ---- 有效限值面（视图×缩放系数——+inf 已排除，全为有限正值；缩放
    // 系数只作用于有限限值，未设值语义不被掩盖——TimeParam.hpp 请求注）。
    const std::size_t jointCount = request.jointLimits.size();
    std::vector<double> vLimit(jointCount);
    std::vector<double> aLimit(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        vLimit[i] = request.jointLimits[i].velocityLimit
                    * request.limitsScaleFactor;
        aLimit[i] = request.jointLimits[i].accelerationLimit
                    * request.limitsScaleFactor;
    }
    const double vAbsTol = velocityAbsTolerance();
    const double aAbsTol = accelerationAbsTolerance();

    // ---- 步骤 3：初值时间分配（§12.2 步骤 1——文件头注登记公式；产
    // 物＝各请求条目的初值时长，缩放只作用于运动段条目）。
    // segmentDurations 跨缩放轮次持久（α 逐轮累乘其运动段项）；驻留项
    // 恒定（工况事件语义，不被缩放——登记于头注）。
    std::vector<double> segmentDurations(request.segments.size(), 0.0);
    for (std::size_t segIdx = 0; segIdx < request.segments.size(); ++segIdx) {
        const TimeParamSegment& seg = request.segments[segIdx];
        if (seg.geometry == nullptr) {
            segmentDurations[segIdx] = seg.dwellDurationS;  // 驻留：原值
            continue;
        }
        // 逐轴折线长度（等参探针——kInitialLengthProbeSpans 区间；D_i 单
        // 位 rad|m 逐轴混合计量，与 §6.2 pathLengthJoint 同口径）。
        std::vector<double> axisLength(jointCount, 0.0);
        rw::math::Q prev = seg.geometry->sampleAt(0.0);
        for (std::size_t k = 1; k <= kInitialLengthProbeSpans; ++k) {
            const double s =
                static_cast<double>(k)
                / static_cast<double>(kInitialLengthProbeSpans);
            const rw::math::Q cur = seg.geometry->sampleAt(s);
            for (std::size_t i = 0; i < jointCount; ++i) {
                axisLength[i] += std::abs(cur[i] - prev[i]);
            }
            prev = cur;
        }
        // 最严关节需求时长（文件头注登记公式的解析系数形态）：速度项
        // 1.875·D/v（静止-静止剖面峰值速度恰达限值的时长）、加速度项
        // sqrt((10/√3)·D/a)（峰值加速度恰达限值的时长）、下限防零退化。
        // 解析系数使初值对"端点驱动的单段"首轮即在线达标（容差内），
        // 缩放环对多段耦合/interior 结点的更严需求兜底。
        double need = 0.0;
        double worstV = 0.0;
        double worstA = 0.0;
        for (std::size_t i = 0; i < jointCount; ++i) {
            worstV = std::max(worstV, axisLength[i] / vLimit[i]);
            worstA = std::max(worstA, axisLength[i] / aLimit[i]);
        }
        need = std::max(need, kQuinticPeakVelocityFactor * worstV);
        need = std::max(need,
                        std::sqrt(kQuinticPeakAccelFactor * worstA));
        segmentDurations[segIdx] = std::max(need, kInitialDurationFloorS);
    }

    // ---- 缩放确认环（§12.2 步骤 2~4；文件头注"检测→缩放→复检"——
    // 检测至多 maxIterations+1 次、缩放至多 maxIterations 次）。
    std::vector<SplineSpan> spans;
    std::vector<SplineKnot> knots;
    rw::math::Q peakV(jointCount);   // 逐关节峰值速度（rad/s|m/s）
    rw::math::Q peakA(jointCount);   // 逐关节峰值加速度（rad/s²|m/s²）
    // 峰值定位（超限素材的段/段内参数——argmax 伴随记录）。
    std::vector<std::uint32_t> peakVSegment(jointCount, 0);
    std::vector<std::uint32_t> peakASegment(jointCount, 0);
    std::vector<double> peakVS(jointCount, 0.0);
    std::vector<double> peakAS(jointCount, 0.0);
    bool withinAllLimits = false;

    for (std::uint32_t iteration = 0; iteration <= request.maxIterations;
         ++iteration) {
        // 迭代边界取消轮询（§15.1——每轮必查，不做逐元素轮询）。
        if (request.cancel && request.cancel()) {
            TimeParamResult result;
            result.status = TimeParamStatus::Canceled;
            return result;
        }

        // ===== 阶段一：全局结点表＋区间描述（§12.1 结点集——段边界＋
        // 段内关键路点＋驻留端点；段边界结点只入表一次：相邻条目共享同
        // 一结点，中心差分因此天然跨段——C² 衔接的导数连续来源）。
        knots.clear();
        std::vector<SpanDesc> spanDescs;
        double tCursor = 0.0;  // 时间轴游标（s；全局累计）
        for (std::size_t segIdx = 0; segIdx < request.segments.size();
             ++segIdx) {
            const TimeParamSegment& seg = request.segments[segIdx];
            if (seg.geometry == nullptr) {
                // —— 驻留段：区间 [tCursor, tCursor+D]，两端结点同位置
                // （前运动段末构型——validateRequest 守卫保证前驱存在且
                // 位级连续），两端结点标驻留边界（一阶导强制 0——§12.1/
                // §9.1"驻留＝零速零加速度常值段"）。常值区间经统一
                // Hermite 公式退化为恒值多项式（m=0 且 q0==q1 的代数恒
                // 等——无特判分支），s 区间无路径语义置 0。
                const std::size_t knotA = knots.size() - 1;
                knots[knotA].dwellBoundary = true;   // 前段末结点标驻留
                const rw::math::Q hold = knots[knotA].q;  // 位级副本
                tCursor += segmentDurations[segIdx];
                knots.push_back({tCursor, hold, true, seg.segmentIndex});
                spanDescs.push_back(
                    {knotA, knots.size() - 1, seg.segmentIndex, 0.0, 0.0,
                     true});
                continue;
            }
            // —— 运动段：结点 s 表＝[0, interiorS..., 1]（§12.1）。
            std::vector<double> knotS;
            knotS.reserve(seg.interiorS.size() + 2);
            knotS.push_back(0.0);
            knotS.insert(knotS.end(), seg.interiorS.begin(),
                         seg.interiorS.end());
            knotS.push_back(1.0);
            const std::size_t knotCount = knotS.size();
            // 结点构型一次取齐（几何求值器纯函数；s=0/1 恒端点——求值
            // 契约，§11.1 IPathGeometry 参数化语义）。
            std::vector<rw::math::Q> knotQ;
            knotQ.reserve(knotCount);
            for (double s : knotS) {
                knotQ.push_back(seg.geometry->sampleAt(s));
            }
            // 区间时长按相邻结点构型 L1 距离比例分配段时长（文件头注登
            // 记；全零行程段均匀分——防除零，保结点时刻严格递增）。
            std::vector<double> spanLen(knotCount - 1, 0.0);
            double totalLen = 0.0;
            for (std::size_t k = 0; k + 1 < knotCount; ++k) {
                double len = 0.0;
                for (std::size_t i = 0; i < jointCount; ++i) {
                    len += std::abs(knotQ[k + 1][i] - knotQ[k][i]);
                }
                spanLen[k] = len;
                totalLen += len;
            }
            // 首结点：段边界共享——非首段复用全局表末结点（位级相等由
            // validateRequest 守卫；驻留后的运动段起点继承驻留边界标记
            // ——驻留侧结点导数为 0 的语义正是"驻留后从零速出发"）。
            std::size_t knotA = 0;
            if (knots.empty()) {
                knots.push_back({tCursor, knotQ[0], false, seg.segmentIndex});
            } else {
                knotA = knots.size() - 1;
            }
            // 逐区间推进时刻并挂结点＋区间描述（s 区间构建期一次记录，
            // 消除二趟回填的 index 匹配脆弱面）。
            for (std::size_t k = 0; k + 1 < knotCount; ++k) {
                const double fraction =
                    totalLen > 0.0
                        ? spanLen[k] / totalLen
                        : 1.0 / static_cast<double>(knotCount - 1);
                tCursor += segmentDurations[segIdx] * fraction;
                knots.push_back({tCursor, knotQ[k + 1], false,
                                 seg.segmentIndex});
                spanDescs.push_back({knotA + k, knotA + k + 1,
                                     seg.segmentIndex, knotS[k],
                                     knotS[k + 1]});
            }
        }

        // ===== 阶段二：结点一阶导（§12.1 边界条件——轨迹端点 0；驻留
        // 边界 0；普通内部结点＝时间中心差分 (q_{k+1}−q_{k−1})/(t_{k+1}−
        // t_{k−1})，rad/s|m/s）。全局表使段间结点的差分天然取跨段邻域。
        std::vector<rw::math::Q> knotVel(knots.size());
        for (std::size_t k = 0; k < knots.size(); ++k) {
            // 零速态（端点/驻留边界）显式 Q::zero——Q(n) 构造不零初始化
            // （框架语义，见 evaluateSpan 注）。
            knotVel[k] = rw::math::Q::zero(jointCount);
            if (k == 0 || k + 1 == knots.size() || knots[k].dwellBoundary) {
                continue;  // 零速态已就位（§12.1/§9.1）
            }
            const double dt = knots[k + 1].t - knots[k - 1].t;
            for (std::size_t i = 0; i < jointCount; ++i) {
                knotVel[k][i] = (knots[k + 1].q[i] - knots[k - 1].q[i]) / dt;
            }
        }

        // ===== 阶段三：段表组装（区间描述→五次 Hermite 段——导数挂接；
        // span.segmentIndex 取请求段〔定位键〕而非共享结点的归属段——超
        // 限定位语义落在"区间归属的请求段"）。
        spans.clear();
        spans.reserve(spanDescs.size());
        for (const SpanDesc& desc : spanDescs) {
            SplineSpan span;
            span.t0 = knots[desc.knotA].t;
            span.h = knots[desc.knotB].t - knots[desc.knotA].t;
            span.q0 = knots[desc.knotA].q;
            span.q1 = knots[desc.knotB].q;
            span.m0 = knotVel[desc.knotA];
            span.m1 = knotVel[desc.knotB];
            span.segmentIndex = desc.segmentIndex;
            span.s0 = desc.s0;
            span.s1 = desc.s1;
            span.constant = desc.constant;
            spans.push_back(std::move(span));
        }

        // ---- 步骤 5：采样求峰值（事件对齐＋均匀步长——每样条段 n=
        // ceil(h/sampleStepS) 等分、段起点必含（结点时刻全采样），总末
        // 点补采；峰值记录携带定位（段序号＋段内归一化参数））。
        peakV = rw::math::Q::zero(jointCount);  // Q(n) 构造不零初始化——显式零
        peakA = rw::math::Q::zero(jointCount);
        std::fill(peakVSegment.begin(), peakVSegment.end(), 0U);
        std::fill(peakASegment.begin(), peakASegment.end(), 0U);
        std::fill(peakVS.begin(), peakVS.end(), 0.0);
        std::fill(peakAS.begin(), peakAS.end(), 0.0);
        QuinticTimeCurve curve(spans);
        const double totalS = knots.back().t;
        for (std::size_t sp = 0; sp < spans.size(); ++sp) {
            const SplineSpan& span = spans[sp];
            const std::size_t divisions =
                std::max<std::size_t>(
                    1, static_cast<std::size_t>(
                           std::ceil(span.h / request.sampleStepS)));
            const double step = span.h / static_cast<double>(divisions);
            // 段起点 + 内部等分点（末点由下一段起点/总末点承担）。
            for (std::size_t j = 0; j < divisions; ++j) {
                const double tSample = span.t0 + static_cast<double>(j) * step;
                const TimedSample sample = curve.sampleAt(tSample);
                const double sNorm =
                    span.s1 > span.s0
                        ? span.s0
                              + (span.s1 - span.s0)
                                    * ((tSample - span.t0) / span.h)
                        : span.s0;
                for (std::size_t i = 0; i < jointCount; ++i) {
                    const double v = std::abs(sample.qd[i]);
                    if (v > peakV[i]) {
                        peakV[i] = v;
                        peakVSegment[i] = span.segmentIndex;
                        peakVS[i] = sNorm;
                    }
                    const double a = std::abs(sample.qdd[i]);
                    if (a > peakA[i]) {
                        peakA[i] = a;
                        peakASegment[i] = span.segmentIndex;
                        peakAS[i] = sNorm;
                    }
                }
            }
        }
        // 总末点补采（totalS 处——末结点：速度/加速度按边界条件为 0，
        // 仍采样以保 samples 覆盖 [0, totalS] 的端点封闭性）。
        {
            const TimedSample sample = curve.sampleAt(totalS);
            for (std::size_t i = 0; i < jointCount; ++i) {
                peakV[i] = std::max(peakV[i], std::abs(sample.qd[i]));
                peakA[i] = std::max(peakA[i], std::abs(sample.qdd[i]));
            }
        }

        // ---- 步骤 6：限值校验（附录 D 第 10 项——逐元素 closeWithin；
        // 全达标→退出确认环）。
        withinAllLimits = true;
        for (std::size_t i = 0; i < jointCount; ++i) {
            if (!withinLimit(peakV[i], vLimit[i], vAbsTol)
                || !withinLimit(peakA[i], aLimit[i], aAbsTol)) {
                withinAllLimits = false;
                break;
            }
        }
        if (withinAllLimits) {
            break;
        }
        // 达迭代上限仍超限→LimitUnreachable＋比较型素材（防御路径——
        // 正常输入一轮精确达标，见文件头注"缩放收敛性"登记）。
        if (iteration == request.maxIterations) {
            break;
        }

        // ---- 步骤 7：等比放大时间轴（α=max(速度比, √加速度比)——全部
        // 运动段时长×α、驻留不变；缩放精确性见文件头注登记）。
        double alpha = 1.0;
        for (std::size_t i = 0; i < jointCount; ++i) {
            if (!withinLimit(peakV[i], vLimit[i], vAbsTol)) {
                alpha = std::max(alpha, peakV[i] / vLimit[i]);
            }
            if (!withinLimit(peakA[i], aLimit[i], aAbsTol)) {
                alpha = std::max(alpha, std::sqrt(peakA[i] / aLimit[i]));
            }
        }
        for (std::size_t segIdx = 0; segIdx < request.segments.size();
             ++segIdx) {
            if (request.segments[segIdx].geometry != nullptr) {
                segmentDurations[segIdx] *= alpha;
            }
        }
    }

    // ---- 确认环退出态分派：仍超限→LimitUnreachable＋TRJ-TIME-PARAM-
    // FAILED（总）＋TRJ-LIMIT-EXCEEDED（逐超限量比较型——§12.6 表行 1）。
    if (!withinAllLimits) {
        TimeParamResult result;
        result.status = TimeParamStatus::LimitUnreachable;
        // 总素材：迭代缩放达上限仍超限（全轨迹级）。
        FailedSegmentRecord total;
        total.segmentIndex = 0xFFFFFFFFu;
        total.phaseToken = kPhaseTimeParam;
        total.reasonToken = std::string(kTrjTimeParamFailed);
        total.cause = "时间参数化失败：缩放迭代达上限（"
                      + std::to_string(request.maxIterations)
                      + " 次）后仍存在关节峰值超限——节拍不产出（§12.6）";
        total.recommendedAction = "核对关节限值与 limitsScaleFactor 配置；"
                                  "若输入几何含极端曲率段，检查上游平滑";
        result.failureRecords.push_back(std::move(total));
        // 逐超限量素材：比较型三要素（实际峰值/限值/单位——UX-03）＋
        // 段定位（segmentIndex＋pathParameter＋subject）——构造经统一辅
        // 助（makeLimitExceededRecord——来源标记/单位口径单点化）。
        const core::UnitToken vUnit = velocityUnitToken();
        const core::UnitToken aUnit = accelerationUnitToken();
        for (std::size_t i = 0; i < jointCount; ++i) {
            if (!withinLimit(peakV[i], vLimit[i], vAbsTol)) {
                result.failureRecords.push_back(makeLimitExceededRecord(
                    peakVSegment[i], peakVS[i],
                    request.segments[peakVSegment[i]].sourceTaskPoint, i,
                    "速度", peakV[i], vLimit[i], vUnit,
                    "放宽该关节速度限值或降低 limitsScaleFactor 后重评"));
            }
            if (!withinLimit(peakA[i], aLimit[i], aAbsTol)) {
                result.failureRecords.push_back(makeLimitExceededRecord(
                    peakASegment[i], peakAS[i],
                    request.segments[peakASegment[i]].sourceTaskPoint, i,
                    "加速度", peakA[i], aLimit[i], aUnit,
                    "放宽该关节加速度限值或降低 limitsScaleFactor 后重评"));
            }
        }
        return result;
    }

    // ---- 步骤 8：连续性守卫（§12.5/§9.1——结点两侧速度/加速度全链
    // 检查；C² 由构造结构性保证：相邻段共享结点导数〔qd 两侧极限同一
    // m_k〕＋结点二阶导恒 0〔qdd 两侧极限同 0〕），守卫取"两侧极限直
    // 接比较"形态——左段 τ=h 与右段 τ=0 的解析端点值。不取邻域差分：
    // 有限差分带 O(δ·jerk) 固有偏差，任何 jerk≠0 的合法曲线都会被 1e-9
    // 级绝对容差误判（实现陷阱，登记于单元卡 §22）。守卫阈值取 1e-12 相
    // 对＋1e-12 绝对（比限值容差严三个数量级的回归拦截线——qd 两侧经
    // (h_L·m)/h_L 与 (h_R·m)/h_R 两条舍入路径，允许 ulp 级噪声；qdd 两
    // 侧数学恒 0，同阈值覆盖）。违约＝实现缺陷→LawFailed＋TRJ-CONTINUITY-
    // BROKEN 比较型素材，不静默放行。
    {
        const core::Tolerance continuityTol{1e-12, 1e-12};
        for (std::size_t sp = 0; sp + 1 < spans.size(); ++sp) {
            const SplineSpan& left = spans[sp];
            const SplineSpan& right = spans[sp + 1];
            // 相邻段共享结点（构建保证——本段 knotB 即下一段 knotA）；
            // 左段取末点极限（τ=h）、右段取起点极限（τ=0）。
            const TimedSample leftEnd = evaluateSpan(left, left.h);
            const TimedSample rightStart = evaluateSpan(right, 0.0);
            for (std::size_t i = 0; i < jointCount; ++i) {
                if (!core::closeWithin(leftEnd.qd[i], rightStart.qd[i],
                                       continuityTol)) {
                    TimeParamResult result;
                    result.status = TimeParamStatus::LawFailed;
                    FailedSegmentRecord rec;
                    rec.segmentIndex = right.segmentIndex;
                    rec.phaseToken = kPhaseTimeParam;
                    rec.reasonToken = std::string(kTrjContinuityBroken);
                    rec.pathParameter = right.s0;
                    rec.subject =
                        request.segments[right.segmentIndex].sourceTaskPoint;
                    rec.cause = "速度连续性守卫违约（段边界 t="
                                + std::to_string(left.t0 + left.h) + " s，关"
                                  "节 " + std::to_string(i)
                                + "）——运动律实现缺陷";
                    rec.recommendedAction = "登记缺陷并回归黄金算例（§12.6）";
                    core::ComparativeFields cmp;
                    cmp.actual.quantity =
                        core::SourcedValue<double>::provided(
                            std::abs(leftEnd.qd[i] - rightStart.qd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.actual.unit = velocityUnitToken();
                    cmp.expected.quantity =
                        core::SourcedValue<double>::provided(
                            1e-12 + 1e-12 * std::abs(rightStart.qd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.expected.unit = velocityUnitToken();
                    rec.comparison = std::move(cmp);
                    result.failureRecords.push_back(std::move(rec));
                    return result;
                }
                if (!core::closeWithin(leftEnd.qdd[i], rightStart.qdd[i],
                                       continuityTol)) {
                    TimeParamResult result;
                    result.status = TimeParamStatus::LawFailed;
                    FailedSegmentRecord rec;
                    rec.segmentIndex = right.segmentIndex;
                    rec.phaseToken = kPhaseTimeParam;
                    rec.reasonToken = std::string(kTrjContinuityBroken);
                    rec.pathParameter = right.s0;
                    rec.subject =
                        request.segments[right.segmentIndex].sourceTaskPoint;
                    rec.cause = "加速度连续性守卫违约（段边界 t="
                                + std::to_string(left.t0 + left.h) + " s，关"
                                  "节 " + std::to_string(i)
                                + "）——运动律实现缺陷（R1 验收锚点）";
                    rec.recommendedAction = "登记缺陷并回归黄金算例（§12.6）";
                    core::ComparativeFields cmp;
                    cmp.actual.quantity =
                        core::SourcedValue<double>::provided(
                            std::abs(leftEnd.qdd[i] - rightStart.qdd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.actual.unit = accelerationUnitToken();
                    cmp.expected.quantity =
                        core::SourcedValue<double>::provided(
                            1e-12 + 1e-12 * std::abs(rightStart.qdd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.expected.unit = accelerationUnitToken();
                    rec.comparison = std::move(cmp);
                    result.failureRecords.push_back(std::move(rec));
                    return result;
                }
            }
        }
    }

    // ---- 步骤 9：组装产物（Ok 态——TimeParamResult 状态—字段联动表）。
    TimeParamResult result;
    result.status = TimeParamStatus::Ok;
    TimeParameterization tp;
    tp.motionLawToken = request.motionLawToken;
    tp.knotTimesS.reserve(knots.size());
    for (const SplineKnot& k : knots) {
        tp.knotTimesS.push_back(k.t);
    }
    tp.totalDurationS = knots.back().t;
    // samples 统一视图（与峰值检测同一采样计划——同源；§6.2"两者并存且
    // 同源"）：重放采样（峰值环内曲线为局部对象，此处重建一次——确定性
    // 同序采样，结果与检测环位级一致）。
    {
        QuinticTimeCurve curve(spans);
        for (std::size_t sp = 0; sp < spans.size(); ++sp) {
            const SplineSpan& span = spans[sp];
            const std::size_t divisions =
                std::max<std::size_t>(
                    1, static_cast<std::size_t>(
                           std::ceil(span.h / request.sampleStepS)));
            const double step = span.h / static_cast<double>(divisions);
            for (std::size_t j = 0; j < divisions; ++j) {
                tp.samples.push_back(
                    curve.sampleAt(span.t0 + static_cast<double>(j) * step));
            }
        }
        tp.samples.push_back(curve.sampleAt(tp.totalDurationS));
    }
    result.timeParam = std::move(tp);
    result.segmentDurationsS = segmentDurations;
    result.peakJointVelocity = peakV;
    result.peakJointAcceleration = peakA;
    result.timeCurve = std::make_shared<const QuinticTimeCurve>(
        std::move(spans));
    return result;
}

}  // namespace sdurws::ird::trajectory
