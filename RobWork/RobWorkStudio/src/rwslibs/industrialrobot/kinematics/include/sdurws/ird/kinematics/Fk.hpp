/**
 * @file   Fk.hpp
 * @brief  FK 与位姿指标（KIN-01）——PoseMetrics 值模型、IFkEvaluator 接口
 *         与无状态实现 FkEvaluator、PoseMetrics canonical 载荷编码。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Fk.hpp 行——"IFkEvaluator＋
 *     PoseMetrics（KIN-01）"，任务 T03）、§5.2（FK 与位姿指标契约表——
 *     输入/输出/统一尺度规则 D-KIN-2/非法输入/超限位 q 两分语义/确定性/
 *     正反向验证）、§9.2（IFkEvaluator 接口契约——@pre/@post/@错误/@取消/
 *     @所有权 原文）、§5.1（权威关节角 q_authoritative＝q_zeroOffset＋
 *     q_rw——本单元对内计算一律使用权威 q）、§9.1（非异常出口
 *     Expected<T, KinematicsError>）
 *   - REQUIREMENTS KIN-01（FK、TCP、Jacobian、奇异值、条件数、可操作度、
 *     关节裕量；统一尺度规则）、NFR-COR-01（确定性）、NFR-COR-03（非法
 *     输入拒绝不钳制）、附录 D 第 4/9 项（FK 等价 1×10⁻⁹ m/rad；解析算例
 *     标量相对 1×10⁻⁹——本单元解析黄金 UT 的容差出处）
 *   - 治理登记 O-40（DTB §4.2）：Jacobian SVD 经 vcpkg 经典模式引入
 *     Eigen（已装 eigen3 5.0.1）——**PRIVATE 仅计算库**（rw::math 无 SVD）；
 *     本头公共面零 Eigen 类型（Eigen 只出现在 src/Fk.cpp 实现内）。
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 1/2/3/5
 *
 * 背景说明（D-KIN-2 统一尺度规则——为什么是"基础雅可比"）：KIN-01 要求
 * "Jacobian 与可操作度使用统一尺度规则"但未给具体形式，卡面 D-KIN-2 将
 * 其定义为：Jacobian＝基础雅可比（几何雅可比，平移行单位 m、转动行无量纲，
 * **不做关节加权归一**）；可操作度 w＝√det(J·Jᵀ)；条件数＝σmax/σmin
 * （奇异→+∞，以 isFinite 标记承载，**不静默截断**）。规则随黄金数据集
 * 锁定（T13），修改走设计变更（P-KIN-1 处置口径）。
 *
 * 线程安全：FkEvaluator 无状态可重入（§3.4 纯函数服务——并发安全）。
 * 确定性：同 (snapshot, q, tcp) → 同字节输出（含浮点布局——§5.2 确定性
 * 行；实现内浮点运算定序固定、无并行归约、无环境依赖）。
 */

#ifndef IRD_KINEMATICS_FK_HPP
#define IRD_KINEMATICS_FK_HPP

#include <cstdint>
#include <vector>

#include <rw/math/Transform3D.hpp>

#include <sdurws/ird/kinematics/Errors.hpp>     // KinematicsError（错误值面）
#include <sdurws/ird/kinematics/KinTypes.hpp>   // TcpRef/IKinRuntimeView
#include <sdurws/ird/runtime/Description.hpp>   // detail::identityTransform3D()——恒等初值
                                                //   （Transform3D 默认构造走
                                                //   Rotation3D::identity 外联符号——冒烟
                                                //   模式不可链接，runtime 同款规避）
#include <sdurws/ird/runtime/Errors.hpp>        // runtime::Expected（非抛出查询轨载体）

namespace sdurws::ird::kinematics {

/// 非抛出查询轨的单元别名（§9.1"Expected<T, KinematicsError>"的承载——
/// runtime::Expected 为 C++17 无 std::expected 时代的最小两态变体，
/// modeling/requirements 同款别名先例；index 0＝成功、index 1＝错误）。
template <class T>
using Expected = runtime::Expected<T, KinematicsError>;

// =====================================================================
// PoseMetrics——FK 位姿指标值模型（§5.2 输出行逐字段）
// =====================================================================

/**
 * @brief 单次 FK 评估的全套位姿指标（§5.2 输出行：
 *        tcpInBase/tcpInWorld/jacobian/singularValues/conditionNumber/
 *        manipulability/jointMargins＋minimumJointMargin）。
 *
 * 值语义纯结构；线程安全（并发只读）。确定性：由 FkEvaluator 产出，
 * 同输入同字节（字段序与编码序一致——encodePoseMetricsCanonical）。
 *
 * 单位与坐标系（AGENTS §2.5 高危信息逐项）：
 *   - tcpInBase：TCP 在**基座系** {B} 的位姿（T_base_tcp，读法 core §4.6）；
 *     平移单位 m、旋转无量纲（构成旋转单位 rad 语义）。
 *   - tcpInWorld：TCP 在**世界系** {W} 的位姿（T_world_tcp）——只经
 *     runtime::composeWorldBaseTcp(view.worldToBase(), tcpInBase) 组合
 *     （§5.1 唯一来源声明：禁止第二套基座变换代数，M-11/AT-37）。
 *   - jacobian：6×n 基础雅可比（D-KIN-2），**行优先**展平（长度恒 6·n，
 *     n＝设备自由度）；行 0~2＝线速度行（TCP 线速度对关节速率的偏导，
 *     单位 m/s per (rad/s 或 m/s)——转动关节列 m、移动关节列无量纲）、
 *     行 3~5＝角速度行（无量纲；移动关节列恒 0）。
 *   - singularValues：雅可比奇异值，**降序**（长度＝min(6, n)）；单位随
 *     雅可比行列混合量纲（比较用相对容差——附录 D 第 9 项）。
 *   - conditionNumber：σmax/σmin（无量纲）；奇异（σmin＝0 或除法非有限）
 *     时＝+∞，并置 conditionNumberIsFinite＝false——不静默截断
 *     （D-KIN-2 原文）。
 *   - manipulability：w＝√det(J·Jᵀ)（≥0；量纲为雅可比行列的平方根——
 *     混合量纲，比较按相对容差）。
 *   - jointMargins：逐自由度归一化关节裕量（**无量纲比**；自由度链序，
 *     长度＝n）——有界关节 margin_i＝min(q_i−qmin, qmax−q_i)/(行程/2)：
 *     行程中点＝1、限位处＝0、超限为负（超限 q 可计算、违例素材交调用方
 *     ——§5.2 两分语义）；continuous 关节有工作范围时按工作范围同式
 *     （MDL-12 分析消费属性）、无工作范围时＝+∞。归一化公式为 D-KIN-6
 *     设计默认（随黄金数据集锁定——T13）。
 *   - minimumJointMargin：有界关节裕量的最小值（全无界＝+∞；无量纲）。
 */
struct PoseMetrics {
    /// TCP 位姿——基座系 {B}（T_base_tcp；m/rad）。恒等初值经 runtime
    /// detail 工具（冒烟模式可链接——见 include 注；两成员在 evaluate
    /// 出口前必被赋值）。
    rw::math::Transform3D<double> tcpInBase = runtime::detail::identityTransform3D();
    /// TCP 位姿——世界系 {W}（T_world_tcp；经 view.worldToBase() 唯一
    /// 组合，m/rad）。
    rw::math::Transform3D<double> tcpInWorld = runtime::detail::identityTransform3D();
    /// 基础雅可比（6×n 行优先展平，长度 6·n；单位见结构体注）。
    std::vector<double> jacobian;
    /// 奇异值（降序；长度 min(6, n)）。
    std::vector<double> singularValues;
    /// 条件数 σmax/σmin（无量纲；奇异＝+∞——不静默截断）。
    double conditionNumber = 0.0;
    /// 条件数有限性标记（D-KIN-2"以 isFinite 标记承载"；true＝
    /// conditionNumber 为有限值）。
    bool conditionNumberIsFinite = true;
    /// 可操作度 w＝√det(J·Jᵀ)（≥0）。
    double manipulability = 0.0;
    /// 逐自由度归一化裕量（无量纲；长度 n，自由度链序——字段规则见
    /// 结构体注）。
    std::vector<double> jointMargins;
    /// 有界关节最小裕量（无量纲；全无界＝+∞）。
    double minimumJointMargin = 0.0;
};

// =====================================================================
// PoseMetrics canonical 载荷编码（域契约字节面——确定性"同字节输出"的
// 承载；EvaluationOutput.payload.canonicalBytes 的域编码）
// =====================================================================

/**
 * @brief 将 PoseMetrics 编码为域 canonical 字节（定宽小端＋字段定序——
 *        卡面 §4.4 编码纪律同源）。
 *
 * 编码布局（codec 版本 1；字段序即本函数写序，演进即新版本+magic 推进）：
 *   magic "IRDPM01"（7 字节）＋ codec 版本 u8（=1）＋ dof u32（LE）＋
 *   tcpInBase{R 9×f64 行优先, p 3×f64}＋ tcpInWorld{同构}＋
 *   jacobian 6·dof×f64＋ 奇异值个数 u32＋ 奇异值×f64＋ conditionNumber
 *   f64＋ conditionNumberIsFinite u8＋ manipulability f64＋
 *   minimumJointMargin f64＋ jointMargins dof×f64。
 * f64＝IEEE754 位模式小端 8 字节（含 +∞——位模式确定，确定性成立）。
 *
 * 用途：kin.pose-metrics 评估器的 DomainPayload.canonicalBytes（域登记
 * token "kin.pose-metrics.v1"——evidence §7.1 域载荷契约）；摘要由
 * evidence/core ContentDigester 对本字节计算（CR-02，本函数不重复算）。
 *
 * @param m [in] 位姿指标（FkEvaluator 产出值）
 * @return canonical 字节（确定性：同 m 同字节——NFR-COR-01）
 *
 * 纯函数；线程安全；不抛（f64 位模式直写，无格式化）。
 */
std::vector<std::uint8_t> encodePoseMetricsCanonical(const PoseMetrics& m);

// =====================================================================
// IFkEvaluator——FK 与位姿指标接口（§9.2 原文契约）
// =====================================================================

/**
 * @brief FK 与位姿指标评估接口（KIN-01；§9.2 七个必需接口之首）。
 *
 * 契约（§9.2 行内联原文的展开）：
 *   - @pre view 为请求绑定 RuntimeSnapshot 的只读模型视图（跨 snapshot
 *     使用＝调用方契约违约——fail-fast 轨，不构成本接口返回值）；
 *     q 维度＝设备自由度且全部分量有限。
 *   - @post 输出经黄金解析算例容差（附录 D 第 4/9 项）；不修改 view。
 *   - @错误 KinematicsError{IllegalQ, NoDevice, NoTcp, FrameUnresolved}
 *     （值面返回——§9.1 非异常出口；错误语义与解析规则随实现注释）。
 *   - @取消 不可取消（可预测 <1 s 内联计算——§9.2 原文；超界场景转批量
 *     评估器，属 T05+ 批量通道）。
 *   - @所有权 值语义产出；view 由调用方持有（本接口不接管、不存储引用）。
 *   - 合法＝快照内 q＋快照内 tcpRef；非法＝跨 snapshot 视图、非有限 q
 *     （§9.2"合法与非法调用对照"行）。
 */
class IFkEvaluator {
public:
    virtual ~IFkEvaluator() = default;

    /**
     * @brief 计算权威关节向量下的 TCP 位姿与全套指标（§5.2/§9.2）。
     *
     * @param view [in] 请求绑定的只读模型视图（调用方持有；调用期间存活）
     * @param tcp  [in] TCP 引用（快照内解析——KinTypes.hpp 解析规则）
     * @param q    [in] 权威关节向量（逐自由度 SI：转动 rad／移动 m；
     *              顺序＝可动关节链序；q_authoritative＝q_zeroOffset＋
     *              q_rw（§5.1）——调用方传入的即权威 q，本接口不再叠加
     *              零位偏置）
     *
     * @return 成功＝PoseMetrics（值语义）；失败＝KinematicsError 值：
     *           - IllegalQ：q 维度≠设备自由度或含非有限分量（NFR-COR-03
     *             不钳制不置零；params 键 expected-dof/actual-dof/
     *             nonfinite-index——T03 生产者登记面）；
     *           - NoDevice：视图模型无可用设备链（§9.6 KIN-NO-DEVICE——
     *             结构化错误素材，FK 失败≠工程不可行）；
     *           - NoTcp：快照无工具（未配置）或 tcpRef.toolObject 悬空/
     *             非工具（§9.6 KIN-NO-TCP"未配置/悬空"两分）；
     *           - FrameUnresolved：tcpKey 非空且不命中 canonical TCP 身份
     *             （§9.2 @错误 行——KinTypes.hpp 解析规则）。
     *
     * 纯函数；线程安全（无共享可变状态）；确定性（同输入同字节）。
     * 超限位 q **可计算**（几何有效；限位评价交调用方——§5.2 两分语义）。
     */
    virtual Expected<PoseMetrics> evaluate(const IKinRuntimeView& view,
                                           const TcpRef& tcp,
                                           const std::vector<double>& q) const = 0;
};

// =====================================================================
// FkEvaluator——无状态实现（§3.1 计算库"FK/指标"落点）
// =====================================================================

/**
 * @brief IFkEvaluator 的无状态实现（纯函数服务——§3.4 总约定）。
 *
 * 生命周期：无成员状态，可静态/栈构造共享使用（§9.4"无状态建议共享"）。
 * 实现要点（详见 src/Fk.cpp 逐步注释）：
 *   1. 输入校验（fail-fast 值面）：q 维度/有限性 → IllegalQ；
 *   2. TCP 解析：工具索引解析 → NoTcp/FrameUnresolved（KinTypes 规则）；
 *   3. 链式 FK：T_base_flange＝∏(origin_i·M_i(q_i))（Eigen 组件化计算，
 *      零 rw 外联符号——冒烟模式可链接；O-40）；
 *   4. TCP 位姿：tcpInBase＝T_base_flange·tcpOffset；
 *      tcpInWorld＝runtime::composeWorldBaseTcp(view.worldToBase(),
 *      tcpInBase)（基座—世界唯一组合点——M-11/AT-37）；
 *   5. 几何雅可比（基础雅可比，D-KIN-2）＋ Eigen JacobiSVD 奇异值；
 *   6. 条件数/可操作度/关节裕量（规则见 PoseMetrics 字段注）。
 */
class FkEvaluator final : public IFkEvaluator {
public:
    /// 无状态——默认构造即可用（无配置面；求解参数属 AnalysisConfiguration，
    /// T10 落位，不属 FK 纯函数）。
    FkEvaluator() = default;

    Expected<PoseMetrics> evaluate(const IKinRuntimeView& view,
                                   const TcpRef& tcp,
                                   const std::vector<double>& q) const override;
};

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_FK_HPP
