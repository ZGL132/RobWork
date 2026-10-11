/**
 * @file   TimeParam.cpp
 * @brief  时间参数化算法的唯一实现翻译单元（§12 运动律＋§12.2 限值校验
 *         与时间缩放＋§12.4 节拍输出；WP-16-T08 批；WP-16-T14 批经
 *         §12.7 扩展点新增 septic-spline-c3 运动律并纳入 TRJ-08-S1 关节
 *         jerk 限值消费）。
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
 *   - **结点三阶导恒取 0（WP-16-T14 批）**：septic-spline-c3 七次样条
 *     的同源推广——结点二阶/三阶导恒取 0，相邻段在结点处二阶导同值 0
 *     →C²、三阶导同值 0→**整体 C³（jerk 连续）**，§12.1"jerk 连续属
 *     R2（TRJ-08-S1）"的结构性成立形态。七次 Hermite 段多项式（对归一
 *     化参数 u；m̂=h·m 为对 u 的结点一阶导）：
 *       Q(u) = q0·h0 + m̂0·h1 + q1·h2 + m̂1·h3，其中
 *       h0 = 1 − 35u⁴ + 84u⁵ − 70u⁶ + 20u⁷   （值基，q0 侧）
 *       h1 = u − 20u⁴ + 45u⁵ − 36u⁶ + 10u⁷   （一阶导基，q0 侧）
 *       h2 = 35u⁴ − 84u⁵ + 70u⁶ − 20u⁷       （值基，q1 侧）
 *       h3 = −15u⁴ + 39u⁵ − 34u⁶ + 10u⁷      （一阶导基，q1 侧）
 *     端点条件（逐基验证）：h_i 的 (值,一阶,二阶,三阶导) 在两端为脉冲
 *     形——例如 h1'(0)=1、h1''(0)=h1'''(0)=h1'(1)=h1''(1)=h1'''(1)=0。
 *     二阶/三阶导基项恒缺（结点高阶导 0 纪律）使八条件中四个平凡，段
 *     多项式由 (q0, m̂0, q1, m̂1) 唯一确定——与五次形态共用 SplineSpan
 *     数据结构（零高阶导字段）。黄金算例在测试侧独立书写同一闭式
 *     （NFR-COR-01 双向对照）。
 *   - **初值时间分配公式**（§12.2 步骤 1"按段路径长度/最严关节估计"的
 *     确定性展开）：逐运动段以 kInitialLengthProbeSpans 个等参区间采样
 *     段几何，逐轴折线长度 D_i（rad|m 混合计量逐轴）；需求时长按静止-
 *     静止剖面的解析峰值系数估计——
 *     quintic：t_seg = max( 1.875·max_i(D_i/v_i^eff) ,
 *                          sqrt((10/√3)·max_i(D_i/a_i^eff)) ,
 *                          kInitialDurationFloorS )
 *     septic（WP-16-T14 批追加 jerk 项）：
 *              t_seg = max( 同上两项 ,
 *                          (52.5·max_i(D_i/j_i^eff))^(1/3) ,
 *                          kInitialDurationFloorS )
 *     ——速度项系数 15/8 与加速度项系数 10/√3 是五次剖面（端点零导）峰
 *     值的精确闭式；jerk 项系数 52.5＝840/16 是七次剖面（端点/结点一阶
 *     导为零的高阶导纪律形态）峰值 jerk 的精确闭式：Q'''(u) = 840·D·u·
 *     (1−6u+10u²−5u³)，全局峰值在 jerk 换向的中点 u=1/2 处
 *     |u·(1−6u+10u²−5u³)| = 1/16（代数精确；两侧临界点 u=(1±√(3/5))/2
 *     处为 1/20 次峰），故峰值 |Q'''| = 840·D/16 = 52.5·D，需求时长
 *     (52.5·D/j)^(1/3)。初值恰使
 *     "端点驱动单段"的峰值在线达标（浮动误差被限值校验 1e-9 相对容差吸
 *     收，不触发虚假缩放），多段耦合/interior 结点的更严需求由缩放环兜
 *     底。探针密度是**初值估计的实现参数**（非协议数值；缩放收敛性与
 *     其无关——见下条）。段内结点区间时长按相邻结点构型的关节空间 L1
 *     距离比例分配（同源确定性）。
 *   - **缩放收敛性**（§12.2 步骤 4"等比放大时间轴"的实现依据）：样条形
 *     状只依赖结点值与结点导数；全部运动段时长同乘 α 后，中心差分结点
 *     导数精确 ÷α、段多项式在归一化参数 u=τ/h 下不变 ⇒ 速度峰值精确
 *     ÷α、加速度峰值精确 ÷α²、jerk 峰值精确 ÷α³（三阶导链式法则 ÷h³
 *     ——七次与五次形态同性质；驻留段常值不受影响）。因此 α 由峰值/
 *     限值比直接计算后，一轮缩放即把各量精确带到限值线上（±浮点舍入，
 *     被附录 D 第 10 项 1×10⁻⁹ 容差吸收）——迭代循环实为"检测→缩放→
 *     复检"确认环；"达迭代上限仍超限"是浮点病态防御路径（正常有限输入
 *     不可达），触发时如实产出 TRJ-TIME-PARAM-FAILED＋TRJ-LIMIT-EXCEE-
 *     DED 比较型素材（§12.6），不静默放行。
 *   - **峰值统计口径**：§13.1"全轨迹逐关节最大绝对值（采样+样条解析峰
 *     值取严）"落为采样峰值——采样含全部结点（解析值：结点速度＝中心
 *     差分/0、结点加速度＝0 位级），段内峰值由均匀网格近似；因缩放的精
 *     确重参数化性质，达标判定在"同一采样计划"下自洽（检测与验证同口
 *     径，§6.3"采样计划入身份"）。WP-16-T14 批起峰值统计三维（速度/加
 *     速度/jerk——TimeParamResult.peakJointJerk）；jerk 统计两运动律统
 *     一产出（观察面），jerk 限值**校验**仅 septic 分支执行（约束面——
 *     TRJ-08-S1；v1 五次样条 jerk 分段二次多项式结点跳变，§12.1 不承诺
 *     jerk 连续，也无 jerk 约束语义）。
 *   - **jerk 校验的 ε_abs＝纯相对（WP-16-T14 批登记）**：附录 D C7 运行
 *     校验 ε_abs 按量纲默认表（角度 1e-12/长度 1e-12/速度 1e-9/加速度
 *     1e-9/力矩 1e-9/无量纲 1e-12）**无 jerk 条目**，且 core 词表（
 *     QuantityKind/单位注册表）为 R1 冻结面、"新增须走需求变更"（C7 原
 *     文）——本域不得私设数值自建"jerk 量纲默认"。处置：jerk 达标判定
 *     取 C4 公式的 ε_abs=0 纯相对形态 peak ≤ limit·(1+1×10⁻⁹)（第 10
 *     项原文只承诺"相对 1×10⁻⁹"；限值合法域恒为有限正值，零参考退化
 *     场景不存在——ε_abs 分量在数学上不参与）。若后续需求变更补 jerk
 *     量纲默认，经 core::runtimeAbsoluteTolerance 接入（调用点已按
 *     withinLimit(peak, limit, absTol) 形态留位）。
 *   - **jerk 超限素材的比较型单位＝无量纲倍数（WP-16-T14 批登记）**：
 *     §12.6 比较型三要素（实际/期望/单位）的单位侧承载经 core::UnitToken
 *     （"显示单位（已注册 token）"——core 单位注册表 R1 全量 17 条**无
 *     rad/s³ 词形**，扩表走需求变更）。处置：jerk 超限素材的 comparison
 *     携带"实测峰值相对有效限值的倍数"（无量纲，单位 token "1"——已注
 *     册词形；actual=peak/limit、expected=1.0＝限值线），峰值/限值的 SI
 *     直值（rad/s³|m/s³）经 cause 中文文案承载（文案面自由文本不受单位
 *     注册表约束）——量纲诚实（不谎报既有 token 为 jerk 单位）、UX-03
 *     三要素齐备、判定语义完整（倍数>1 即超限）。速度/加速度素材维持
 *     T08 直值＋rad/s、rad/s² 形态（行为零变化）。
 *   - **相邻段端点位级相等守卫**：§9.3"几何连续性（位置/姿态/分支）：
 *     段构建与段连接时即时检查"归 Continuity（T05）；本头作为时间化的
 *     输入守卫取最强关节维契约——相邻运动段端点构型必须 rw::math::Q 位
 *     级相等（T04~T07 产物形态保证：折线端点＝精确路点、平滑端点强制
 *     不变），位级不等即调用方数据违约 fail-fast，不静默缝合（NFR-COR-03）。
 *   - **C7 ε_abs 的量纲取值**：速度/加速度的 C7 运行校验默认（1×10⁻⁹）
 *     经 core::runtimeAbsoluteTolerance 取值（D-06 单点转写）；转动/移动
 *     两族量纲默认同值，本域限值视图不携带关节类型（投影面最小化），
 *     统一取 AngularVelocity/AngularAcceleration 族转写值——两族同值使
 *     该选择对结果无影响（测试钉扎）。jerk 的 ε_abs 处置见上条（纯相对）。
 *   - **v1 拒绝有限 jerkLimit（WP-16-T14 批登记）**：quintic-spline-c2
 *     是 TRJ-05"按关节速度、加速度限制"的运动律（行原文口径）——jerk
 *     限值不消费；请求携带有限 jerkLimit＝调用方契约违约 fail-fast（拒
 *     绝优于静默忽略：静默忽略会使"配置写了什么"与"算了什么"脱钩，
 *     NFR-COR-03）。默认 +inf（未设值）的既有调用零影响。
 *   - **驻留段的显式常值求值**：§9.1"驻留＝零速零加速度常值段"以独立
 *     分支落地（SplineSpan.constant）——不可套用"m=0、q0==q1"的统一
 *     Hermite 公式：位置虽恒定（值基和恒 1 的代数恒等）但加速度为
 *     −60u²(1−u)·q/h²（值基二阶导和不为零），会误触发加速度超限缩放并
 *     违反驻留语义——实现陷阱，测试以驻留区间三零采样钉扎。七次形态同
 *     陷阱同处置（常值分支在分派之前短路，两形态共享）。
 *   - **连续性守卫的极限比较形态**：§12.5"速度/加速度连续性检查在时间
 *     化完成后全链执行"以"相邻段端点极限直接比较"实现（左段 τ=h vs 右
 *     段 τ=0 的解析值）——不取邻域差分：有限差分带 O(δ·jerk) 固有偏差，
 *     任何 jerk≠0 的合法曲线都会被绝对容差误判（实现陷阱；守卫阈值
 *     1e-12 相对/绝对——qd 两侧经不同段长的舍入路径允许 ulp 级噪声，
 *     qdd 两侧数学恒 0）。WP-16-T14 批 v2 运动律追加第三维 jerk 连续
 *     守卫（C³ 结构承诺的回归拦截面——jerk 两侧数学恒 0，同阈值）。
 *   - 需求 TRJ-05/06、TRJ-08-S1、NFR-COR-01/02/03（见 TimeParam.hpp）；
 *     错误二分见 Errors.hpp。
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

/// 静止-静止七次剖面的解析峰值 jerk 系数（52.5＝840/16——WP-16-T14 批；
/// 初值估计的"jerk 项"系数：h ≥ (系数·D/j)^(1/3) 时峰值 jerk ≤ j）。
/// 闭式推导：静止-静止七次剖面 Q(u)=Δq·(35u⁴−84u⁵+70u⁶−20u⁷)（结点导
/// 数全 0 形态），三阶导 Q'''(u) = Δq·840·u·(1−6u+10u²−5u³)；临界点由
/// 其导数因式分解 1−12u+30u²−20u³ = −(2u−1)(10u²−10u+1) 得 u=1/2 与
/// u=(1±√(3/5))/2；|u(1−6u+10u²−5u³)| 在 u=1/2 处 = 1/16（代数精确：
/// 1/2−3/2+5/4−5/16 = −1/16——中段 jerk 换向处为全局峰值，两侧根处为
/// 1/20 次峰），故峰值 |Q'''| = 840/16·|Δq| = 52.5·|Δq|。测试侧独立书
/// 写同一闭式（NFR-COR-01 双向对照）。
inline constexpr double kSepticPeakJerkFactor = 52.5;

// =====================================================================
// 五次 Hermite 基函数族（u∈[0,1] 归一化段参数；值/一阶/二阶/三阶导数——
// Smooth.cpp 值基族的超集，本 TU 需要速度/加速度/jerk 求值故四阶并列；
// 独立书写于实现侧，测试侧另有黄金参照——NFR-COR-01 双向对照）。
// 三阶导为 WP-16-T14 批增列（peakJointJerk 统计的两形态公共求值面）。
// =====================================================================

/// 单基四阶值（对归一化参数 u 的位置/一阶/二阶/三阶导数——无量纲基值）。
struct BasisValues {
    double p;    ///< 基函数值
    double d1;   ///< 对 u 的一阶导数
    double d2;   ///< 对 u 的二阶导数
    double d3;   ///< 对 u 的三阶导数（WP-16-T14 批增列——jerk 统计面）
};

/// 值基 h0000：端点条件 p(0)=1，其余五条件全 0（1−10u³+15u⁴−6u⁵）。
inline BasisValues basisH0000(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    BasisValues b;
    b.p = 1.0 - 10.0 * u3 + 15.0 * u2 * u2 - 6.0 * u3 * u2;
    b.d1 = -30.0 * u2 + 60.0 * u3 - 30.0 * u2 * u2;
    // 二阶导修正（WP-16-T14 批登记——T08 批遗留笔误）：d1 对 u 求导应为
    // −60u+180u²−120u³，原实现误写 −60u+120u²−60u³。既有黄金算例未暴露
    // 的原因：全部零起点（q0=0 → h0000 加权项为零）＋结点处各基 d2 端点
    // 值两版本同为 0（C² 守卫不敏感）；数值实验（中心差分对照解析）定位。
    // 修正后 h0000''+h0001'' 恒 0（值基和恒 1 的二阶导）恢复结构性成立。
    b.d2 = -60.0 * u + 180.0 * u2 - 120.0 * u3;
    // 三阶导（WP-16-T14 批）：d2 对 u 求导——常值 −60＋180·2u−120·3u²。
    b.d3 = -60.0 + 360.0 * u - 360.0 * u2;
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
    // 三阶导（WP-16-T14 批增列）。
    b.d3 = -36.0 + 192.0 * u - 180.0 * u2;
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
    // 三阶导（WP-16-T14 批增列）。
    b.d3 = 60.0 - 360.0 * u + 360.0 * u2;
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
    // 三阶导（WP-16-T14 批增列）。
    b.d3 = -24.0 + 168.0 * u - 180.0 * u2;
    return b;
}

// =====================================================================
// 七次 Hermite 基函数族（WP-16-T14 批——septic-spline-c3 运动律；端点
// 条件各 4 个：值/一阶/二阶/三阶导——二阶三阶导基项恒缺〔结点高阶导 0
// 纪律〕，段多项式由 (q0, h·m0, q1, h·m1) 唯一确定。闭式推导见文件头
// 注；测试侧独立书写同一闭式作黄金对照——NFR-COR-01 双向对照）
// =====================================================================

/// 七次值基 s0：端点条件 p(0)=1，其余七条件全 0
/// （1 − 35u⁴ + 84u⁵ − 70u⁶ + 20u⁷）。
inline BasisValues septicS0(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    BasisValues b;
    b.p = 1.0 - 35.0 * u4 + 84.0 * u5 - 70.0 * u6 + 20.0 * u5 * u2;
    b.d1 = -140.0 * u3 + 420.0 * u4 - 420.0 * u5 + 140.0 * u6;
    b.d2 = -420.0 * u2 + 1680.0 * u3 - 2100.0 * u4 + 840.0 * u5;
    b.d3 = -840.0 * u + 5040.0 * u2 - 8400.0 * u3 + 4200.0 * u4;
    return b;
}

/// 七次一阶导基 s1：端点条件 p'(0)=1，其余七条件全 0
/// （u − 20u⁴ + 45u⁵ − 36u⁶ + 10u⁷）。
inline BasisValues septicS1(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    BasisValues b;
    b.p = u - 20.0 * u4 + 45.0 * u5 - 36.0 * u6 + 10.0 * u5 * u2;
    b.d1 = 1.0 - 80.0 * u3 + 225.0 * u4 - 216.0 * u5 + 70.0 * u6;
    b.d2 = -240.0 * u2 + 900.0 * u3 - 1080.0 * u4 + 420.0 * u5;
    b.d3 = -480.0 * u + 2700.0 * u2 - 4320.0 * u3 + 2100.0 * u4;
    return b;
}

/// 七次值基 s2：端点条件 p(1)=1，其余七条件全 0
/// （35u⁴ − 84u⁵ + 70u⁶ − 20u⁷——与 s0 的值部互补：s0+s2 恒 1）。
inline BasisValues septicS2(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    BasisValues b;
    b.p = 35.0 * u4 - 84.0 * u5 + 70.0 * u6 - 20.0 * u5 * u2;
    b.d1 = 140.0 * u3 - 420.0 * u4 + 420.0 * u5 - 140.0 * u6;
    b.d2 = 420.0 * u2 - 1680.0 * u3 + 2100.0 * u4 - 840.0 * u5;
    b.d3 = 840.0 * u - 5040.0 * u2 + 8400.0 * u3 - 4200.0 * u4;
    return b;
}

/// 七次一阶导基 s3：端点条件 p'(1)=1，其余七条件全 0
/// （−15u⁴ + 39u⁵ − 34u⁶ + 10u⁷）。
inline BasisValues septicS3(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    BasisValues b;
    b.p = -15.0 * u4 + 39.0 * u5 - 34.0 * u6 + 10.0 * u5 * u2;
    b.d1 = -60.0 * u3 + 195.0 * u4 - 204.0 * u5 + 70.0 * u6;
    // 二阶/三阶导（逐次求导：s3''＝−180u²+780u³−1020u⁴+420u⁵、
    // s3'''＝−360u+2340u²−4080u³+2100u⁴；端点校验 s3''(1)=0、
    // s3'''(1)=0——C³ 结构条件的基级前提，测试端点矩阵钉扎）。
    b.d2 = -180.0 * u2 + 780.0 * u3 - 1020.0 * u4 + 420.0 * u5;
    b.d3 = -360.0 * u + 2340.0 * u2 - 4080.0 * u3 + 2100.0 * u4;
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
 * @brief 运动律形态（§12.7 分派点的实现侧词形——token 字符串在
 *        validateRequest 解析一次后以本枚举贯穿计算面；两值各对应一个
 *        真实实现分支，零空分支）。
 */
enum class LawForm {
    /// quintic-spline-c2（TRJ-05——五次 Hermite，结点二阶导恒 0，C²）。
    Quintic,
    /// septic-spline-c3（TRJ-08-S1——七次 Hermite，结点二/三阶导恒 0，
    /// C³/jerk 连续；jerk 限值参与校验）。
    Septic,
};

/// 段内四阶求值结果（位置/速度/加速度/jerk——TimedSample 无 jerk 成员
/// 〔§6.5 守护〕，jerk 走独立通道供峰值统计与连续性守卫消费，不入离散
/// 样本视图）。
struct SpanDerivs {
    rw::math::Q q;     ///< 位置（rad|m）
    rw::math::Q qd;    ///< 速度（rad/s|m/s）
    rw::math::Q qdd;   ///< 加速度（rad/s²|m/s²）
    rw::math::Q qddd;  ///< jerk（rad/s³|m/s³）
};

/**
 * @brief 段内求值（τ∈[0,h]；Hermite 四阶合成——逐轴 double 循环避免 Q
 *        临时对象，确定性算术序；form 分派五次/七次基族）。
 *
 * 数学（文件头注运动律）：
 *   - quintic：q(τ)=h0000·q0+h0010·h·m0+h0001·q1+h0011·h·m1；qd=q'(u)/h；
 *     qdd=q''(u)/h²；qddd=q'''(u)/h³（u=τ/h；二/三阶导基项〔结点高阶导
 *     0 纪律下〕无自由度——三阶导来自值/一阶导基的导数）。
 *   - septic：同形态换七次基族 s0..s3（结点二/三阶导恒 0——三阶导在结
 *     点两侧恒 0，C³）。
 * 单位换算：h·m0 量纲 rad|m，除 h/h²/h³ 后 qd/qdd/qddd 得 rad/s、
 * rad/s²、rad/s³——链式量纲自洽。
 *
 * 匿名命名空间自由函数（非方法）：连续性守卫需要对同一函数取"段端点极
 * 限"（u=1 与 u=0——两侧极限的直接比较，见守卫环注），故与曲线类解耦。
 */
SpanDerivs evaluateSpanDerivs(const SplineSpan& span, double tau, LawForm form)
{
    const std::size_t n = static_cast<std::size_t>(span.q0.size());
    SpanDerivs d;
    // 注意（框架语义）：rw::math::Q(n) 单参构造**不零初始化**（Eigen 动
    // 态向量默认构造语义）——零导数面（qd/qdd/qddd）必须显式 Q::zero，
    // 否则常值段/边界结点携带未定义值（实现陷阱，登记于单元卡 §22）。
    d.q = rw::math::Q::zero(n);
    d.qd = rw::math::Q::zero(n);
    d.qdd = rw::math::Q::zero(n);
    d.qddd = rw::math::Q::zero(n);
    // 驻留常值段：显式零导数求值（§9.1"驻留＝零速零加速度常值段"——
    // 不可套用 Hermite 公式，见 SplineSpan 结构注的陷阱登记；陷阱在两
    // 形态同样存在——统一 Hermite 公式对常值段的加速度不为零，故常值
    // 分支在形态分派之前短路）；q0/q1 位级相等（构建保证），取 q0。
    if (span.constant) {
        d.q = span.q0;
        return d;  // qd/qdd/qddd 已零初始化（构造面）
    }
    // τ 钳位检查（段内舍入可能给出 τ=−ε/+ε——钳位到 [0,h] 而非越
    // 界抛错：本调用点 t 已在总区间内，段边界舍入属数值噪声，静默
    // 归边不影响位级可复现性；与 sampleAt 的越界 fail-fast 分工——
    // 那里是调用方语义越界，这里是实现内数值归边）。
    const double u = std::clamp(tau / span.h, 0.0, 1.0);
    const double h2 = span.h * span.h;
    const double h3 = h2 * span.h;
    // 形态分派（§12.7——每分支一个真实运动律；基族选择在此单点）。
    const BasisValues b00 = form == LawForm::Quintic ? basisH0000(u) : septicS0(u);
    const BasisValues b10 = form == LawForm::Quintic ? basisH0010(u) : septicS1(u);
    const BasisValues b01 = form == LawForm::Quintic ? basisH0001(u) : septicS2(u);
    const BasisValues b11 = form == LawForm::Quintic ? basisH0011(u) : septicS3(u);
    // 逐轴合成：四基加权（位置）＋导数基加权后除 h、h²、h³（时间导数
    // 链式法则）——每轴独立的标量算术，无跨轴归约（确定性算术序）。
    for (std::size_t i = 0; i < n; ++i) {
        const double q0 = span.q0[i];
        const double q1 = span.q1[i];
        const double hm0 = span.h * span.m0[i];  // h·m0：量纲 rad|m
        const double hm1 = span.h * span.m1[i];
        d.q[i] = b00.p * q0 + b10.p * hm0 + b01.p * q1 + b11.p * hm1;
        d.qd[i] = (b00.d1 * q0 + b10.d1 * hm0 + b01.d1 * q1
                   + b11.d1 * hm1)
                  / span.h;
        d.qdd[i] = (b00.d2 * q0 + b10.d2 * hm0 + b01.d2 * q1
                    + b11.d2 * hm1)
                   / h2;
        d.qddd[i] = (b00.d3 * q0 + b10.d3 * hm0 + b01.d3 * q1
                     + b11.d3 * hm1)
                    / h3;
    }
    return d;
}

/**
 * @brief 段内求值的 TimedSample 投影（q/qd/qdd 三阶——ITimeCurve 离散
 *        视图与采样计划的面；jerk 不入 TimedSample〔§6.5 守护——离散
 *        样本视图不扩成员〕，由 evaluateSpanDerivs 的独立通道承载）。
 */
TimedSample evaluateSpan(const SplineSpan& span, double tau, LawForm form)
{
    const SpanDerivs d = evaluateSpanDerivs(span, tau, form);
    TimedSample sample;
    sample.t = span.t0 + tau;
    sample.q = d.q;
    sample.qd = d.qd;
    sample.qdd = d.qdd;
    return sample;
}

/**
 * @brief 时间曲线求值器（ITimeCurve 产品实现——段表二分定位＋Hermite
 *        求值〔五次/七次按构建时冻结的形态分派〕；无状态纯计算——形态
 *        成员为构建期常量，并发只读安全）。
 *
 * 求值语义（TimeParam.hpp ITimeCurve 契约的落地）：t∈[t_first, t_last]
 * 越界 fail-fast；结点时刻取左段极限（二分 upper_bound−1——C²（七次为
 * C³）保证左右极限位级相等，取侧无观测差异）；驻留段经常值分支退化为
 * 常值（evaluateSpanDerivs 的 constant 分支——显式零导数，见该函数注）。
 */
class SplineTimeCurve final : public ITimeCurve {
public:
    /// 构造（段表构建期组装后一次冻结；spans 非空且 h>0——构建面保证；
    /// form＝构建请求解析出的运动律形态——求值与构建同律）。
    SplineTimeCurve(std::vector<SplineSpan> spans, LawForm form)
        : m_spans(std::move(spans)), m_form(form)
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
        return evaluateSpan(span, t - span.t0, m_form);
    }

private:
    /// 段表（构建期一次冻结；求值只读——并发安全）。
    std::vector<SplineSpan> m_spans;
    /// 运动律形态（构建期冻结——与段表的多项式族一致；只读）。
    LawForm m_form;
};

// =====================================================================
// 前置校验（调用方契约违约 fail-fast——校验序固定，NFR-COR-02）
// =====================================================================

/**
 * @brief 解析运动律 token 为实现形态（§12.7 分派点——token 字符串→
 *        LawForm 的唯一映射；词表外 token 无实现对应，拒绝而非静默回退：
 *        静默回退会使"配置写了什么"与"算了什么"脱钩，NFR-COR-03）。
 *
 * @throws TrajectoryError token 不在封闭词表（两值）
 */
LawForm resolveMotionLaw(const std::string& token)
{
    if (token == kTimeParamMethodQuinticSplineC2) {
        return LawForm::Quintic;
    }
    if (token == kTimeParamMethodSepticSplineC3) {
        return LawForm::Septic;
    }
    throw TrajectoryError(
        "trajectory/time-param/motion-law",
        "运动律 token 非法：请求 \"" + token + "\"，封闭词表合法值 \""
            + std::string(kTimeParamMethodQuinticSplineC2) + "\" / \""
            + std::string(kTimeParamMethodSepticSplineC3)
            + "\"（§12.7——扩展走单元卡增量修订＋实现＋黄金算例同批）");
}

/**
 * @brief 请求合法域全表校验（timeParameterize 步骤 1；非法即抛——文案
 *        含首错定位与实际值）。
 *
 * 校验序（确定性——同一坏请求必报同一首错）：
 *   1. motionLawToken ∈ 封闭词表两值（§12.7——resolveMotionLaw 分派）；
 *   2. segments 非空；条目二分形态（运动段：geometry 非空＋dwell==0＋
 *      interiorS 合法；驻留段：geometry 空＋dwell>0 有限＋interiorS 空）；
 *   3. 首条目必须为运动段、驻留段前一条目必须为运动段（常值段语义附着
 *      于运动——无前置运动的驻留没有可驻留的构型）；
 *   4. 相邻运动段端点构型位级相等（文件头注登记的守卫）；
 *   5. jointLimits 与段几何同维度、逐轴合法域（>0 有限或 +inf——三维
 *      同规）；v1 运动律不消费 jerk 限值——携带有限 jerkLimit 即调用方
 *      契约违约 fail-fast（WP-16-T14 批登记；默认 +inf 零影响）；
 *   6. limitsScaleFactor ∈ (0,1] 有限；
 *   7. sampleStepS 有限且 >0（s）；
 *   8. maxIterations ≥1。
 */
void validateRequest(const TimeParamRequest& request)
{
    // 1. 运动律词表（§12.7 分派点——见 resolveMotionLaw 注）。
    const LawForm form = resolveMotionLaw(request.motionLawToken);
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
    //    非正限值没有工程意义；三维同规——jerk 合法域与速度/加速度一致，
    //    消费与否是 token 相关语义，非法值任何 token 下都拒绝）。
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
        const bool jOk = std::isinf(lim.jerkLimit)
                         || (std::isfinite(lim.jerkLimit)
                             && lim.jerkLimit > 0.0);
        if (!vOk || !aOk || !jOk) {
            throw TrajectoryError(
                "trajectory/time-param/limits-domain",
                "限值视图非法（关节 " + std::to_string(i) + "）：velocity="
                    + std::to_string(lim.velocityLimit)
                    + "，acceleration=" + std::to_string(lim.accelerationLimit)
                    + "，jerk=" + std::to_string(lim.jerkLimit)
                    + "（要求 >0 有限或 +inf＝未设值；NaN/零/负拒绝）");
        }
        // v1 运动律不消费 jerk 限值（TRJ-05 口径"按关节速度、加速度限
        // 制"——WP-16-T14 批登记）：携带有限 jerkLimit＝调用方要求 v1 运
        // 动律执行 jerk 约束，语义不可达——fail-fast 拒绝而非静默忽略
        // （静默忽略会使"请求写了什么"与"算了什么"脱钩，NFR-COR-03）。
        // 默认 +inf（未设值）的既有调用零影响。
        if (form == LawForm::Quintic && !std::isinf(lim.jerkLimit)) {
            throw TrajectoryError(
                "trajectory/time-param/jerk-limit-motion-law",
                "quintic-spline-c2 运动律不消费 jerk 限值（TRJ-05 限值面"
                "＝速度/加速度；TRJ-08-S1 jerk 约束须用运动律 \""
                    + std::string(kTimeParamMethodSepticSplineC3)
                    + "\"）：关节 " + std::to_string(i) + " 的 jerkLimit="
                    + std::to_string(lim.jerkLimit) + "（要求 +inf 未设值）");
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
 * @brief 扫描限值视图中的未设值（+inf）；命中返回其下标与量名，否则
 *        nullopt。
 *
 * §12.6"时间参数化输入缺限值（模型未设限速）：+inf 语义→时间无界"——
 * 任一关节任一量缺失即整链不可时间化（部分时间化没有可交付语义：节拍
 * 是全轨迹量）。前置校验已保证视图无 NaN/非正值，本扫描只面对 +inf。
 * 扫描维度 token 相关（WP-16-T14 批）：v2 运动律三维全扫（jerk 缺失与
 * 速度/加速度缺失同源——"最短时间"对缺任一限值维度无定义）；v1 只扫
 * 速度/加速度（jerk 不消费——validateRequest 已拒绝有限值，+inf 是 v1
 * 下唯一合法形态，不参与扫描）。
 */
std::optional<std::size_t> findUndefinedLimit(const TimeParamRequest& request,
                                              LawForm form)
{
    // jerk 是否参与"未设值"扫描（v2 消费 jerk——+inf 即不可时间化）。
    const bool jerkConsumed = form == LawForm::Septic;
    for (std::size_t i = 0; i < request.jointLimits.size(); ++i) {
        if (std::isinf(request.jointLimits[i].velocityLimit)
            || std::isinf(request.jointLimits[i].accelerationLimit)) {
            return i;
        }
        if (jerkConsumed && std::isinf(request.jointLimits[i].jerkLimit)) {
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
 * missingJerk 区分缺失量（WP-16-T14 批——v2 的 jerk 缺失素材文案与建议
 * 动作按量定位：建议动作指向补设 jerk 限值而非速度/加速度）。
 */
FailedSegmentRecord makeUndefinedLimitRecord(std::size_t jointIndex,
                                             bool missingJerk)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = 0xFFFFFFFFu;              // 全轨迹级失败（§6.2）
    rec.phaseToken = kPhaseTimeParam;            // "time-param"（§14.1.6）
    rec.reasonToken = std::string(kTrjTimeParamFailed);
    if (missingJerk) {
        // TRJ-08-S1 半区：jerk 限值未设（§12.6 行 3 同源——不伪造节拍）。
        rec.cause = "限值未定义：关节 " + std::to_string(jointIndex)
                    + " 的 jerk 限值为 +inf（模型未显式设值）——septic 运动"
                      "律的 jerk 约束无界，时间无界，不产出节拍（§12.6/"
                      "NFR-COR-03 不伪造）";
        rec.recommendedAction = "为该关节补设 jerk 限值（模型侧显式设值；"
                                "或改用 " + std::string(kTimeParamMethodQuinticSplineC2)
                                + " 运动律——TRJ-05 面）后重新评估";
    } else {
        rec.cause = "限值未定义：关节 " + std::to_string(jointIndex)
                    + " 的速度或加速度限值为 +inf（模型未显式设值）——时间无界，"
                      "不产出节拍（§12.6/NFR-COR-03 不伪造）";
        rec.recommendedAction = "为该关节补设 CanonicalJoint.maxVelocity/"
                                "maxAcceleration（模型侧显式设值）后重新评估";
    }
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

/// 取无量纲单位 token（"1"——已注册词形；jerk 比较型素材的倍数标注面，
/// 见 makeJerkLimitExceededRecord 注）。
inline core::UnitToken dimensionlessUnitToken()
{
    const auto token = core::UnitToken::find("1");
    if (!token.has_value()) {
        throw TrajectoryError("trajectory/time-param/unit-token",
                              "core 单位注册表缺少无量纲 token \"1\"（环境"
                              "缺陷，fail-fast——比较型素材单位口径不可用）");
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

/**
 * @brief 组装单条 jerk 超限素材（TRJ-LIMIT-EXCEEDED 比较型——WP-16-T14
 *        批 TRJ-08-S1 半区；比较型单位＝无量纲倍数，见文件头注登记）。
 *
 * 单位承载说明（core 单位注册表 R1 全量 17 条无 rad/s³ 词形——扩表走
 * 需求变更，本域不越权）：comparison.actual＝实测峰值相对有效限值的倍
 * 数（peak/limit，无量纲）、comparison.expected＝1.0（限值线）——单位
 * token 取已注册的无量纲词形 "1"；UX-03 三要素齐备（实际/期望/单位）
 * 且量纲诚实（不谎报既有 token 为 jerk 单位）；峰值/限值 SI 直值
 * （rad/s³|m/s³）经 cause 中文文案承载（文案面自由文本）。定位要素
 * （segmentIndex/pathParameter/subject）与速度/加速度素材同构——超限
 * 段定位语义一致（TRJ-08-S1 验收"超限段定位诊断"的素材承载面）。
 *
 * @param segmentIndex [in] 峰值所在请求段序号
 * @param pathParam    [in] 峰值所在段内归一化路径参数（无量纲，[0,1]）
 * @param subject      [in] 段来源任务点投影（可空——直接透传）
 * @param jointIndex   [in] 超限关节（文案定位）
 * @param peak         [in] 实测峰值 jerk（SI；rad/s³|m/s³——仅入文案）
 * @param limit        [in] 有效限值（视图×缩放系数；同量纲——仅入文案）
 * @param action       [in] 中文建议动作（ERR-01）
 */
FailedSegmentRecord makeJerkLimitExceededRecord(
    std::uint32_t segmentIndex, double pathParam,
    std::optional<core::ObjectId> subject, std::size_t jointIndex,
    double peak, double limit, const char* action)
{
    FailedSegmentRecord rec;
    rec.segmentIndex = segmentIndex;
    rec.phaseToken = kPhaseTimeParam;  // "time-param"（§14.1.6 词表）
    rec.reasonToken = std::string(kTrjLimitExceeded);
    rec.pathParameter = pathParam;
    rec.subject = std::move(subject);
    // 中文文案携带 SI 直值与量纲说明（rad/s³|m/s³——逐轴按关节类型，SI
    // 真值；本域素材统一以转动族词形标注量纲，与速度/加速度素材的量纲
    // 取舍同源〔限值视图不携带关节类型，两族同值〕）。
    rec.cause = std::string("关节 ") + std::to_string(jointIndex)
                + " 峰值 jerk 超限（实测 "
                + std::to_string(peak) + "，限值 "
                + std::to_string(limit) + "，单位 rad/s^3——缩放迭代达上限"
                                          "仍超——§12.6/TRJ-08-S1）";
    rec.recommendedAction = action;
    // 比较型三要素：倍数形态（无量纲——core 注册表无 jerk 单位词形的
    // 合规承载，见函数注/文件头注登记）。
    core::ComparativeFields cmp;
    cmp.actual.quantity = core::SourcedValue<double>::provided(
        peak / limit, derivedProvenance("time-param-peak-jerk-ratio"));
    cmp.actual.unit = dimensionlessUnitToken();
    cmp.expected.quantity = core::SourcedValue<double>::provided(
        1.0, derivedProvenance("time-param-limit"));
    cmp.expected.unit = dimensionlessUnitToken();
    rec.comparison = std::move(cmp);
    return rec;
}

}  // namespace

// =====================================================================
// §15.8 时间参数化入口（执行序见 TimeParam.hpp 函数注）
// =====================================================================

TimeParamResult timeParameterize(const TimeParamRequest& request)
{
    // ---- 步骤 1：前置校验（调用方契约违约 fail-fast——validateRequest；
    // 含运动律词表分派——token 解析一次，形态贯穿计算面）。
    validateRequest(request);
    const LawForm form = resolveMotionLaw(request.motionLawToken);

    // ---- 步骤 2：取消轮询（命中 → Canceled，零素材——UX-03）。
    if (request.cancel && request.cancel()) {
        TimeParamResult result;
        result.status = TimeParamStatus::Canceled;
        return result;
    }

    // ---- 步骤 3：限值未定义扫描（§12.6——+inf→时间无界，在任何构造前
    // 快速失败；素材轨返回，不抛不伪造节拍；v2 扫描含 jerk 维度——缺失
    // 量区分进素材文案，见 makeUndefinedLimitRecord）。
    if (const std::optional<std::size_t> undefined =
            findUndefinedLimit(request, form);
        undefined.has_value()) {
        // 缺失量判定（文案定位用）：该关节的 jerk 是否为 +inf 且 v2 消费
        // jerk——是则按 jerk 缺失组装；否则按速度/加速度缺失（T08 文案）。
        const bool missingJerk =
            form == LawForm::Septic
            && std::isinf(request.jointLimits[*undefined].jerkLimit);
        TimeParamResult result;
        result.status = TimeParamStatus::LimitUnreachable;
        result.failureRecords.push_back(
            makeUndefinedLimitRecord(*undefined, missingJerk));
        return result;
    }

    // ---- 有效限值面（视图×缩放系数——+inf 已排除，全为有限正值；缩放
    // 系数只作用于有限限值，未设值语义不被掩盖——TimeParam.hpp 请求注；
    // v2 起 jerk 维度同规——TRJ-08-S1 限值消费扩展）。
    const std::size_t jointCount = request.jointLimits.size();
    std::vector<double> vLimit(jointCount);
    std::vector<double> aLimit(jointCount);
    std::vector<double> jLimit(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        vLimit[i] = request.jointLimits[i].velocityLimit
                    * request.limitsScaleFactor;
        aLimit[i] = request.jointLimits[i].accelerationLimit
                    * request.limitsScaleFactor;
        // v1 不消费 jerk（validateRequest 已保证 v1 下 jerkLimit 恒 +inf
        // ——不会走到这里；本行对 v1 形态写 0 占位值，永不参与校验——
        // 初值分配/校验/缩放的 jerk 项全部以 jerkConsumed 门控）。
        jLimit[i] = form == LawForm::Septic
                        ? request.jointLimits[i].jerkLimit
                              * request.limitsScaleFactor
                        : 0.0;
    }
    const double vAbsTol = velocityAbsTolerance();
    const double aAbsTol = accelerationAbsTolerance();
    // jerk 校验 ε_abs＝纯相对（0.0）——C7 量纲默认表无 jerk 条目（core
    // 词表冻结，新增须走需求变更），本域不私设数值；第 10 项原文只承诺
    // "相对 1×10⁻⁹"，限值合法域恒有限正值、零参考退化场景不存在（WP-16-
    // T14 批登记，见文件头注）。若需求变更补 jerk 量纲默认，此处改为经
    // core::runtimeAbsoluteTolerance 转写接入。
    const double jAbsTol = 0.0;
    // jerk 限值是否参与校验/缩放（约束面 token 相关——统计面两形态统一）。
    const bool jerkConsumed = form == LawForm::Septic;

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
        // sqrt((10/√3)·D/a)（峰值加速度恰达限值的时长）、jerk 项（v2）
        // (42·D/j)^(1/3)（静止-静止七次剖面峰值 jerk 恰达限值的时长——
        // kSepticPeakJerkFactor 闭式见该常量注）、下限防零退化。
        // 解析系数使初值对"端点驱动的单段"首轮即在线达标（容差内），
        // 缩放环对多段耦合/interior 结点的更严需求兜底。
        double need = 0.0;
        double worstV = 0.0;
        double worstA = 0.0;
        double worstJ = 0.0;
        for (std::size_t i = 0; i < jointCount; ++i) {
            worstV = std::max(worstV, axisLength[i] / vLimit[i]);
            worstA = std::max(worstA, axisLength[i] / aLimit[i]);
            if (jerkConsumed) {
                worstJ = std::max(worstJ, axisLength[i] / jLimit[i]);
            }
        }
        need = std::max(need, kQuinticPeakVelocityFactor * worstV);
        need = std::max(need,
                        std::sqrt(kQuinticPeakAccelFactor * worstA));
        if (jerkConsumed) {
            // 立方根收敛（std::cbrt 正确处理正实数——jerk 项需求时长）。
            need = std::max(
                need, std::cbrt(kSepticPeakJerkFactor * worstJ));
        }
        segmentDurations[segIdx] = std::max(need, kInitialDurationFloorS);
    }

    // ---- 缩放确认环（§12.2 步骤 2~4；文件头注"检测→缩放→复检"——
    // 检测至多 maxIterations+1 次、缩放至多 maxIterations 次）。
    std::vector<SplineSpan> spans;
    std::vector<SplineKnot> knots;
    rw::math::Q peakV(jointCount);   // 逐关节峰值速度（rad/s|m/s）
    rw::math::Q peakA(jointCount);   // 逐关节峰值加速度（rad/s²|m/s²）
    rw::math::Q peakJ(jointCount);   // 逐关节峰值 jerk（rad/s³|m/s³——
                                     //   WP-16-T14 批统计面；校验面 v2 门控）
    // 峰值定位（超限素材的段/段内参数——argmax 伴随记录）。
    std::vector<std::uint32_t> peakVSegment(jointCount, 0);
    std::vector<std::uint32_t> peakASegment(jointCount, 0);
    std::vector<std::uint32_t> peakJSegment(jointCount, 0);
    std::vector<double> peakVS(jointCount, 0.0);
    std::vector<double> peakAS(jointCount, 0.0);
    std::vector<double> peakJS(jointCount, 0.0);
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

        // ---- 步骤 6：采样求峰值（事件对齐＋均匀步长——每样条段 n=
        // ceil(h/sampleStepS) 等分、段起点必含（结点时刻全采样），总末
        // 点补采；峰值记录携带定位（段序号＋段内归一化参数）。jerk 统计
        // 同面采集（WP-16-T14 批——四阶求值一次完成 q/qd/qdd/qddd；采样
        // 视图的 TimedSample 不携带 jerk〔§6.5 守护〕，本环为内部统计通
        // 道）。jerk 限值校验仅 v2 门控执行，统计两形态统一产出。
        peakV = rw::math::Q::zero(jointCount);  // Q(n) 构造不零初始化——显式零
        peakA = rw::math::Q::zero(jointCount);
        peakJ = rw::math::Q::zero(jointCount);
        std::fill(peakVSegment.begin(), peakVSegment.end(), 0U);
        std::fill(peakASegment.begin(), peakASegment.end(), 0U);
        std::fill(peakJSegment.begin(), peakJSegment.end(), 0U);
        std::fill(peakVS.begin(), peakVS.end(), 0.0);
        std::fill(peakAS.begin(), peakAS.end(), 0.0);
        std::fill(peakJS.begin(), peakJS.end(), 0.0);
        SplineTimeCurve curve(spans, form);
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
                const SpanDerivs sample =
                    evaluateSpanDerivs(span, tSample - span.t0, form);
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
                    const double j = std::abs(sample.qddd[i]);
                    if (j > peakJ[i]) {
                        peakJ[i] = j;
                        peakJSegment[i] = span.segmentIndex;
                        peakJS[i] = sNorm;
                    }
                }
            }
        }
        // 总末点补采（totalS 处——末结点：速度/加速度按边界条件为 0，
        // 仍采样以保 samples 覆盖 [0, totalS] 的端点封闭性）。
        {
            const SpanDerivs sample =
                evaluateSpanDerivs(spans.back(), totalS - spans.back().t0,
                                   form);
            for (std::size_t i = 0; i < jointCount; ++i) {
                peakV[i] = std::max(peakV[i], std::abs(sample.qd[i]));
                peakA[i] = std::max(peakA[i], std::abs(sample.qdd[i]));
                peakJ[i] = std::max(peakJ[i], std::abs(sample.qddd[i]));
            }
        }

        // ---- 步骤 7：限值校验（附录 D 第 10 项——逐元素单向判定；
        // jerk 维 v2 门控〔jerkConsumed——TRJ-08-S1〕；全达标→退出确认环）。
        withinAllLimits = true;
        for (std::size_t i = 0; i < jointCount; ++i) {
            if (!withinLimit(peakV[i], vLimit[i], vAbsTol)
                || !withinLimit(peakA[i], aLimit[i], aAbsTol)
                || (jerkConsumed
                    && !withinLimit(peakJ[i], jLimit[i], jAbsTol))) {
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

        // ---- 步骤 8：等比放大时间轴（α=max(速度比, √加速度比〔, ∛jerk
        // 比——v2：峰值 jerk ∝ 1/α³，见文件头注缩放收敛性〕)——全部运动
        // 段时长×α、驻留不变；缩放精确性见文件头注登记）。
        double alpha = 1.0;
        for (std::size_t i = 0; i < jointCount; ++i) {
            if (!withinLimit(peakV[i], vLimit[i], vAbsTol)) {
                alpha = std::max(alpha, peakV[i] / vLimit[i]);
            }
            if (!withinLimit(peakA[i], aLimit[i], aAbsTol)) {
                alpha = std::max(alpha, std::sqrt(peakA[i] / aLimit[i]));
            }
            if (jerkConsumed
                && !withinLimit(peakJ[i], jLimit[i], jAbsTol)) {
                // 立方根收敛（峰值 jerk ∝ α⁻³——α ≥ ∛(峰值/限值)）。
                alpha = std::max(alpha, std::cbrt(peakJ[i] / jLimit[i]));
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
        // 助（makeLimitExceededRecord——来源标记/单位口径单点化；jerk 半
        // 区经 makeJerkLimitExceededRecord——倍数形态单位承载，WP-16-T14
        // 批登记）。段定位即 TRJ-08-S1 验收"超限段定位诊断"的素材承载
        // 面（segmentIndex/pathParameter/subject 与 v/a 同构）。
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
            if (jerkConsumed
                && !withinLimit(peakJ[i], jLimit[i], jAbsTol)) {
                result.failureRecords.push_back(makeJerkLimitExceededRecord(
                    peakJSegment[i], peakJS[i],
                    request.segments[peakJSegment[i]].sourceTaskPoint, i,
                    peakJ[i], jLimit[i],
                    "放宽该关节 jerk 限值或降低 limitsScaleFactor 后重评"));
            }
        }
        return result;
    }

    // ---- 步骤 9：连续性守卫（§12.5/§9.1——结点两侧速度/加速度全链
    // 检查；C² 由构造结构性保证：相邻段共享结点导数〔qd 两侧极限同一
    // m_k〕＋结点二阶导恒 0〔qdd 两侧极限同 0〕），守卫取"两侧极限直
    // 接比较"形态——左段 τ=h 与右段 τ=0 的解析端点值。不取邻域差分：
    // 有限差分带 O(δ·jerk) 固有偏差，任何 jerk≠0 的合法曲线都会被 1e-9
    // 级绝对容差误判（实现陷阱，登记于单元卡 §22）。守卫阈值取 1e-12 相
    // 对＋1e-12 绝对（比限值容差严三个数量级的回归拦截线——qd 两侧经
    // (h_L·m)/h_L 与 (h_R·m)/h_R 两条舍入路径，允许 ulp 级噪声；qdd 两
    // 侧数学恒 0，同阈值覆盖）。v2 运动律追加第三维 jerk 连续守卫（§12.1
    // "jerk 连续属 R2〔TRJ-08-S1〕"的 C³ 结构承诺回归拦截面——jerk 两侧
    // 数学恒 0〔结点三阶导 0 纪律〕，同阈值覆盖）。违约＝实现缺陷→
    // LawFailed＋TRJ-CONTINUITY-BROKEN 比较型素材，不静默放行。
    {
        const core::Tolerance continuityTol{1e-12, 1e-12};
        for (std::size_t sp = 0; sp + 1 < spans.size(); ++sp) {
            const SplineSpan& left = spans[sp];
            const SplineSpan& right = spans[sp + 1];
            // 相邻段共享结点（构建保证——本段 knotB 即下一段 knotA）；
            // 左段取末点极限（τ=h）、右段取起点极限（τ=0）。
            const SpanDerivs leftEnd = evaluateSpanDerivs(left, left.h, form);
            const SpanDerivs rightStart =
                evaluateSpanDerivs(right, 0.0, form);
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
                // jerk 连续第三维（v2 门控——五次样条 jerk 结点跳变是设
                // 计内形态〔§12.1"段内三阶导可跳"〕，不守卫；七次样条的
                // C³ 是结构承诺，违约即实现缺陷）。
                if (jerkConsumed
                    && !core::closeWithin(leftEnd.qddd[i],
                                          rightStart.qddd[i],
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
                    rec.cause = "jerk 连续性守卫违约（段边界 t="
                                + std::to_string(left.t0 + left.h) + " s，关"
                                  "节 " + std::to_string(i)
                                + "）——septic 运动律实现缺陷（C³/TRJ-08-S1"
                                  " 结构承诺）";
                    rec.recommendedAction = "登记缺陷并回归黄金算例（§12.6）";
                    core::ComparativeFields cmp;
                    // 单位承载说明：jerk 差值素材复用加速度族 token
                    // （"rad/s^2"）不可行——量纲谎报；该素材是两侧极限差
                    // 的回归拦截线（预期恒 0——正常不可达路径），与超限
                    // 素材的倍数形态同理取无量纲面（差值量级为守卫阈值
                    // 级，量纲信息经 cause 文案承载）。WP-16-T14 批登记。
                    cmp.actual.quantity =
                        core::SourcedValue<double>::provided(
                            std::abs(leftEnd.qddd[i] - rightStart.qddd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.actual.unit = dimensionlessUnitToken();
                    cmp.expected.quantity =
                        core::SourcedValue<double>::provided(
                            1e-12 + 1e-12 * std::abs(rightStart.qddd[i]),
                            derivedProvenance("time-param-continuity"));
                    cmp.expected.unit = dimensionlessUnitToken();
                    rec.comparison = std::move(cmp);
                    result.failureRecords.push_back(std::move(rec));
                    return result;
                }
            }
        }
    }

    // ---- 步骤 10：组装产物（Ok 态——TimeParamResult 状态—字段联动表）。
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
        SplineTimeCurve curve(spans, form);
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
    // jerk 峰值统计（WP-16-T14 批——TRJ-08-S1"限值满足断言"的观测面；
    // 两形态统一产出——观察面，校验面 v2 门控；v1 下该统计不参与任何校
    // 验/缩放判定，T08 行为零变化）。
    result.peakJointJerk = peakJ;
    result.timeCurve = std::make_shared<const SplineTimeCurve>(
        std::move(spans), form);
    return result;
}

}  // namespace sdurws::ird::trajectory
