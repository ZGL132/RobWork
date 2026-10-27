/**
 * @file   FdEngineSession.cpp
 * @brief  正动力学受控引擎会话实现（公共契约面见同名公共头文件头——裁决
 *         脉络/GAP 引用/适配差异均在头，本文件只补装配序与数值细节）。
 *
 * 设计依据（头面引用汇总）：
 *   - GAP-FD-BUILD-01 §1a/§2（vcpkg ode 0.16.6 供给＋sdurwsim_ode 启用——
 *     提案唯一推荐路径；A3 裁决 2026-10-26「A1~A4 全按推荐」放行）；
 *   - REUSE-T52 构造面裁决（模型构造权归 runtime；工厂 TU 双模式编译＝
 *     单一实现——冒烟分支经 targets/sdurwsim_ode.cmake 导入目标供给，
 *     不冒烟降级、不另写备用路径）；
 *   - units/runtime.md §3.2 P-RT-3（sdurwsim_ode 为 REUSE-T53 增登 L1——
 *     本文件的设计依据面）。
 *
 * ★ 装配架构（引擎载体＝RNE 设备工厂复用——H1 直接复用，零第二编译路径）：
 *   会话私有载体经 buildRneConsumptionDevice 构造（其公共头明文预留 WP-09
 *   复用）——帧树"体挂链帧＋基座体承载帧＝设备基座帧"恰为上游 ODE 引擎的
 *   体拓扑约定（父体回溯沿帧链经 DWC 体映射命中上游连杆——S7 体承载帧挂
 *   规范帧之下的产品约定与之错位，实核 ODESimulator.cpp:1180-1206/
 *   ODEVelocityDevice.cpp:358-372/BodyUtil.cpp:62-77）。设备名取
 *   "ird-rne/link"（与连杆体名同前缀——上游 inDevice 名称前缀启发式
 *   DynamicWorkCell.cpp:82-95 将连杆判属设备、基座经通用路径注册：设备路
 *   径独建连杆 ODE 体，通用路径零自由重复体）。WorkCell 采用载体
 *   StateStructure（WorkCell(tree,name) 构造——帧树单一来源零复制）。
 *   重力＝请求 gBase 直传（载体世界＝基座系恒等安装——与 τ_ref 侧 solver
 *   同基座系语义；模型 BaseMount 不进会话物理——基座系动力学与安装表示
 *   无关，旧实现 request.gravityBase 口径保留）。
 */

#include <sdurws/ird/runtime/FdEngineSession.hpp>

#include <sdurws/ird/runtime/RneConsumptionDevice.hpp>  // RNE 设备工厂（公共头——其面预留 WP-09 复用）

#include <sdurws/ird/runtime/Errors.hpp>  // RuntimeError/RuntimeErrorCode（稳定诊断）

#include <ode/ode.h>                              // dCheckConfiguration（双精度核对面）
#include <rwsim/dynamics/DynamicWorkCell.hpp>     // 引擎 DWC（体/设备注册＋重力）
#include <rwsim/dynamics/FixedBody.hpp>           // 会话基座体（静态几何载体）
#include <rwsim/dynamics/MaterialDataMap.hpp>     // 材质/对象类型注册（体创建消费键）
#include <rwsim/dynamics/RigidDevice.hpp>         // 设备句柄/电机目标 API
#include <rwsimlibs/ode/ODESimulator.hpp>         // ODE 引擎（sdurwsim_ode——唯一后端）

#include <rw/core/Exception.hpp>          // rw::core::Exception（转译）
#include <rw/geometry/Geometry.hpp>       // 基座占位几何（越界规避记账面）
#include <rw/geometry/Sphere.hpp>         // 占位球图元
#include <rw/kinematics/Frame.hpp>        // 设备链帧（SerialDevice 构造输入）
#include <rw/kinematics/State.hpp>        // 会话私有 State
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/Q.hpp>                  // 关节向量（设备 API 面）
#include <rw/models/Joint.hpp>            // 限速/限位设值（读回钳制面）
#include <rw/models/JointDevice.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/RigidObject.hpp>      // 体对象载体（RNE 工厂同款）
#include <rw/models/SerialDevice.hpp>     // 会话设备（名称前缀约定对齐）
#include <rw/models/WorkCell.hpp>         // WC（采用载体 StateStructure）

#include <algorithm>
#include <cmath>
#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace sdurws { namespace ird { namespace runtime {

namespace {

// =====================================================================
// 实现常量（初速播种与装配记账面——黄金算例锁定，见公共头适配差异）。
// =====================================================================

/**
 * 初速播种子步长（单位 s）：Velocity 伺服单子步把关节速度从 0 逼近目标
 * 初速。取 1e-5＝远小于 FD 检查典型 maxStepS——播种期间的位置漂移量级
 * q̇₀·h/2（q̇₀=1 rad/s 时约 5e-6 rad，与引擎离散误差相比可忽略）；伺服
 * 所需力 ∝ 惯量×q̇₀/h（测试规模惯量 ≤10 kg·m²、q̇₀ ≤1 时 <1e6，低于
 * kSeedForceLimit 两个量级——一步内到达目标）。种子步瞬态属于引擎初值
 * 适配的已知代价——登记于单元卡。
 */
constexpr double kSeedSubStepS = 1e-5;

/**
 * 初速播种电机力限（N·m 或 N，逐关节同值）：Velocity 伺服在该力限内施加
 * 驱动以逼近目标速度。取 1e7＝远超正常驱动的量级（保证一步内到达目标）；
 * 力控模式的直接加矩（dJointAddAMotorTorques）不受该限钳制（伺服关闭后
 * FMax=0 仅停用速度伺服——ODEJoint.hpp setMotorEnabled 语义），故该常量
 * 只影响播种子步。
 */
constexpr double kSeedForceLimit = 1e7;

/**
 * 会话设备名（与载体连杆体名 "ird-rne/linkN" 同前缀——上游 inDevice 名称
 * 前缀启发式的约定对齐面：连杆判属设备、经设备路径独建 ODE 体；基座名
 * "ird-rne/base" 不匹配、经通用路径注册进 ODE 体映射——addDevice 的基座
 * 查找由此成立）。会话私有名空间，零产品出口（RNE 载体同款纪律）。
 */
constexpr const char* kSessionDeviceName = "ird-rne/link";

/**
 * 会话默认材质/对象类型注册名（引擎体创建路径的消费键——ODEBody 对未注
 * 册名一律 RW_THROW，ODEBody.cpp:386-387；载体 BodyInfo 留基线空串，会话
 * 注册默认条目并盖章全部体——纯装配记账，不进物理语义）。
 */
constexpr const char* kSessionMaterial = "ird-fd-session-material";
constexpr const char* kSessionObjectType = "ird-fd-session-object";

/**
 * 读回不钳制（rad/s、m/s——逐关节恒设值）：设备速度读回经
 * setJointVelocities 钳制（RigidDevice 语义）——缺省限速 1 或模型声明值
 * 都会截断真实状态（发散检测与误差统计必须消费引擎真实速度，超限读数由
 * 检查器发散检出界承载——kForwardDivergenceMagnitude），恒以远超物理范围
 * 的限值承载"读回不钳制"语义。
 */
constexpr double kUnboundedVelocityLimit = 1e300;

/// 判定向量全部有限（NFR-COR-03 判定原语——初态/驱动输入防御共用）。
bool allFinite(const std::vector<double>& v)
{
    for (const double x : v) {
        if (!std::isfinite(x)) { return false; }
    }
    return true;
}

}  // namespace

// =====================================================================
// 会话实现体（PIMP——公共头仅暴露值面与生命周期，框架类型不入公共头）。
// =====================================================================

struct FdEngineSession::Members
{
    // ---- 析构序（§8.5 精神的会话侧承载）：显式 exitPhysics 释放 ODE
    //      世界/体/设备约束后，成员逆序析构——engine（最末声明）最先析构，
    //      其后 device/dwc/carrier（引擎持有物化引用，必须后于引擎消亡）。
    ~Members()
    {
        if (!engine.isNull()) {
            engine->exitPhysics();  // ODE 资源显式收口（析构器只清窄相/日志——实核）
        }
    }

    RneConsumptionDevice carrier;                            ///< 会话私有载体（帧树/设备所有权根）
    rw::core::Ptr<rwsim::dynamics::DynamicWorkCell> dwc;     ///< 引擎 DWC（载体 WC 包装）
    rw::core::Ptr<rwsim::dynamics::RigidDevice> device;      ///< 会话设备句柄（引擎 DWC 持有）
    rw::kinematics::State state;                             ///< 会话私有 State（非共享）
    std::vector<double> zeroOffsets;                         ///< 逐关节零位偏置（rad/m——
                                                             ///   q_auth＝q_rw＋offset 读回换算）
    std::vector<double> frictionViscous;                     ///< 声明粘性摩擦（N·m·s/rad；缺失=空）
    std::vector<double> frictionCoulomb;                     ///< 声明库仑摩擦（N·m）
    std::vector<double> frictionBias;                        ///< 声明摩擦偏置（N·m）
    rw::core::Ptr<rwsim::simulator::ODESimulator> engine;     ///< ODE 引擎（声明最末——先析构）
};

FdEngineSession::~FdEngineSession() = default;

void FdEngineSession::setForceTargets(const std::vector<double>& tauJoint)
{
    // 空会话防御（默认构造仅支撑移动语义——调用属违约）。
    if (m.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "stage=FdSession; 空会话不可用（默认构造产物）——拒绝提交驱动输入");
    }
    if (tauJoint.size() != m_dof) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 驱动输入长度不符（实际 "
                               + std::to_string(tauJoint.size()) + "，会话 DOF "
                               + std::to_string(m_dof) + "）——调用方契约违约");
    }
    if (!allFinite(tauJoint)) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 驱动输入含非有限分量——调用方契约违约");
    }
    m_targets = tauJoint;  // 存值——step 时与摩擦补偿合成写入（采样保持由调用方承载）
}

void FdEngineSession::step(double dtS)
{
    if (m.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "stage=FdSession; 空会话不可用（默认构造产物）——拒绝推进");
    }
    if (!std::isfinite(dtS) || dtS <= 0.0) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 子步长必须为有限正值（实际 "
                               + std::to_string(dtS) + " s）——调用方契约违约");
    }
    if (m_targets.size() != m_dof) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; step 前未提交驱动输入（setForceTargets）"
                           "——调用方契约违约");
    }

    // ---- 摩擦补偿（适配差异 2）：引擎无关节摩擦消费——按声明三元组把
    //      τ_fric(q̇) 从驱动输入扣除（τ_in = τ_ctrl − F(q̇)，引擎无摩擦
    //      动力学 M q̈+C+G = τ_in 与声明模型 M q̈+C+G+F = τ_ctrl 等价）。
    //      F 以当前子步起点的设备速度评估（显式处理——与 RNE sgn₀ 同式）。
    rw::math::Q tauQ(static_cast<int>(m_dof));
    if (!m->frictionViscous.empty()) {
        const rw::math::Q qdQ = m->device->getJointVelocities(m->state);
        for (std::size_t i = 0; i < m_dof; ++i) {
            const double qd = qdQ[static_cast<int>(i)];
            const double sign0 = (qd > 0.0) ? 1.0 : (qd < 0.0 ? -1.0 : 0.0);  // R1 sgn₀ 口径
            const double fric = m->frictionViscous[i] * qd + m->frictionCoulomb[i] * sign0
                                + m->frictionBias[i];
            tauQ[static_cast<int>(i)] = m_targets[i] - fric;  // 单位 N·m 或 N（随关节类型）
        }
    }
    else {
        for (std::size_t i = 0; i < m_dof; ++i) {
            tauQ[static_cast<int>(i)] = m_targets[i];
        }
    }
    m->device->setMotorForceTargets(tauQ, m->state);

    // ---- 引擎推进（异常转译不吞错——step 内 RW_THROW 经 §8.4 同款转译）。
    //      步进不变量检测（双子面）：①引擎全局错误态（isInErrorGlobal——
    //      上游内部错误处理器置位）下 step 静默 no-op；②LCP 重试的时间膨
    //      胀（上游步进循环对坏解减半重试后按减半值推进——请求步长欠账，
    //      ODESimulator.cpp step 循环）——两者都以"引擎时间推进＝请求步长"
    //      不变量显式化（静默时间语义显式化，不吞错：调用方的采样保
    //      持区间账目以真实推进为准）。
    const double engineTimeBefore = m->engine->getTime();
    try {
        m->engine->step(dtS, m->state);
    }
    catch (const rw::core::Exception& e) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           std::string{"stage=FdSession; 引擎 step 异常："} + e.what());
    }
    catch (const std::exception& e) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           std::string{"stage=FdSession; 引擎 step 标准异常："} + e.what());
    }
    const double elapsed = m->engine->getTime() - engineTimeBefore;
    if (!(elapsed > 0.0) || std::abs(elapsed - dtS) > dtS * 1e-6) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           std::string{"stage=FdSession; 引擎步进时间账目失衡（请求 "}
                               + std::to_string(dtS) + " s，实际推进 "
                               + std::to_string(elapsed)
                               + " s——驱动输入超引擎可解算范围或引擎全局错误态）");
    }
}

std::vector<double> FdEngineSession::q() const
{
    if (m.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "stage=FdSession; 空会话不可用（默认构造产物）——拒绝读回");
    }
    const rw::math::Q qQ = m->device->getModel().getQ(m->state);  // 设备 postUpdate 回写值
    std::vector<double> out(m_dof, 0.0);
    for (std::size_t i = 0; i < m_dof; ++i) {
        out[i] = qQ[static_cast<int>(i)] + m->zeroOffsets[i];  // q_auth＝q_rw＋offset（T13 语义）
    }
    return out;
}

std::vector<double> FdEngineSession::qd() const
{
    if (m.isNull()) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           "stage=FdSession; 空会话不可用（默认构造产物）——拒绝读回");
    }
    const rw::math::Q qdQ = m->device->getJointVelocities(m->state);  // postUpdate 回写值
    std::vector<double> out(m_dof, 0.0);
    for (std::size_t i = 0; i < m_dof; ++i) {
        out[i] = qdQ[static_cast<int>(i)];  // 速度无零位偏置分量（q_auth/q_rw 同值）
    }
    return out;
}

// =====================================================================
// 工厂（会话私有载体装配——装配序与公共头 doc 逐步对应）。
// =====================================================================

FdEngineSession buildFdEngineSession(const CanonicalModel& model,
                                     const double gravityBase[3],
                                     const std::vector<double>& q0,
                                     const std::vector<double>& qd0,
                                     const FdFlangeOverride* flangeOverride)
{
    // ---- 步骤 1：输入防御（重力/初态有限性——调用方错误 fail-fast）。
    for (int k = 0; k < 3; ++k) {
        if (!std::isfinite(gravityBase[k])) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=FdSession; 基座系重力加速度含非有限分量（分量序 "
                                   + std::to_string(k) + "）——拒绝装配");
        }
    }
    if (!allFinite(q0) || !allFinite(qd0)) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 初始关节状态含非有限分量——拒绝装配");
    }

    // ---- 步骤 2：模型级门控（调用方错误/引擎承载边界 fail-fast）。
    //      2a 关节词表：移动关节 fail-fast（适配差异 1——上游 ODEJoint::
    //          setForce 仅承载转动，静默推进会产生错误轨迹，显式拒绝优于
    //          错误结果）；Fixed 关节与 τ_ref 主调用（RNE 载体）同防线。
    //      2b 自由度与初态长度。
    //      2c 物性门控：可动连杆（links[1..n]）mass/com/inertia 全
    //          Provided 且质量有限正值——ODE 刚体创建对零/缺失质量未定义
    //          （dBodySetMass 语义）；"全零物性链不可正解积分"旧语义的
    //          引擎侧对应面（调用方映射 DYN-FD-NUMERIC-ANOMALY 值面）。
    //          基座连杆不入动力学（载体基座为静态占位体），不在门控面。
    std::vector<const CanonicalJoint*> movableJoints;
    for (const CanonicalJoint& cj : model.chain().joints) {
        if (cj.type == JointType::Fixed) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=FdSession; 链携带 Fixed 关节——引擎载体仅支持三个"
                               "可动类型（与逆动力学载体同防线）");
        }
        if (cj.type == JointType::Prismatic) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=FdSession; 链含移动关节——上游 ODE 力控仅承载转动"
                               "关节（ODEJoint::setForce 移动分支为死代码，2026-10-26 实"
                               "核），拒绝装配");
        }
        movableJoints.push_back(&cj);
    }
    const std::size_t dof = movableJoints.size();
    if (dof == 0) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 设备无可动关节（DOF=0）——正动力学无积分对象，"
                           "拒绝装配（与旧核零质量阵奇异同语义面）");
    }
    if (q0.size() != dof || qd0.size() != dof) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "stage=FdSession; 初始状态长度与设备 DOF 不符（q 长度 "
                               + std::to_string(q0.size()) + "，qd 长度 "
                               + std::to_string(qd0.size()) + "，DOF "
                               + std::to_string(dof) + "）——调用方契约违约");
    }
    const RobotChain& chain = model.chain();
    for (std::size_t i = 1; i < chain.links.size(); ++i) {
        const CanonicalLink& link = chain.links.at(i);
        const bool provided = link.mass.tryValue().has_value()
                              && link.centerOfMass.tryValue().has_value()
                              && link.inertia.tryValue().has_value();
        const double mass = link.mass.tryValue().value_or(0.0);
        if (!provided || !std::isfinite(mass) || mass <= 0.0) {
            throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                               "stage=FdSession; 模型无动力学能力（连杆序 "
                                   + std::to_string(i) + " 物性缺失或质量非法）——"
                                                          "引擎会话不可物化，正动力学不可积分");
        }
    }

    // ---- 步骤 3：会话私有载体（RNE 设备工厂复用——H1；工厂内部防御
    //      [空链/轴单位/零位偏置] 与本函数 2a 面互补，违例以 RuntimeError
    //      直接传播不二次包裹）。
    FdEngineSession::Members* members = new FdEngineSession::Members();
    rw::core::Ptr<FdEngineSession::Members> holder(members);
    members->carrier = buildRneConsumptionDevice(model);
    RneConsumptionDevice& carrier = members->carrier;
    members->zeroOffsets = carrier.zeroOffsets;

    // ---- 步骤 3a：关节速度限设值（读回不钳制——载体关节缺省限速 1、模
    //      型声明值均会截断设备读回（postUpdate 经 setJointVelocities 钳
    //      制——RigidDevice 语义），发散检测与误差统计必须消费引擎真实速
    //      度：恒以 kUnboundedVelocityLimit 设值，模型 maxVelocity 不进会
    //      话物理（力控模式的读回面——超限读数由检查器发散检出界承载）。
    for (std::size_t i = 0; i < dof; ++i) {
        carrier.joints[i]->setMaxVelocity(rw::math::Q(1, kUnboundedVelocityLimit));
    }

    // ---- 步骤 3b：声明摩擦三元组提取（模型关节直读——MDL-16 三元组；
    //      全部可动关节全 Provided 才补偿，缺失不补偿〔两侧同为无摩擦——
    //      共源语义；引擎无关节摩擦消费为死代码——适配差异 2〕）。
    {
        bool frictionComplete = true;
        for (std::size_t i = 0; i < dof && frictionComplete; ++i) {
            const CanonicalJointFriction& f = movableJoints[i]->friction;
            const auto fv = f.viscous.tryValue();
            const auto fc = f.coulomb.tryValue();
            const auto bias = f.bias.tryValue();
            if (!fv.has_value() || !fc.has_value() || !bias.has_value()
                || !std::isfinite(*fv) || !std::isfinite(*fc) || !std::isfinite(*bias)) {
                frictionComplete = false;  // 任一关节缺声明——整体不补偿（保守共源）
                break;
            }
            members->frictionViscous.push_back(*fv);    // N·m·s/rad
            members->frictionCoulomb.push_back(*fc);    // N·m
            members->frictionBias.push_back(*bias);     // N·m
        }
        if (!frictionComplete) {
            members->frictionViscous.clear();
            members->frictionCoulomb.clear();
            members->frictionBias.clear();
        }
    }

    // ---- 步骤 3c：会话设备（fresh RigidDevice——设备名与连杆体名同前缀
    //      〔kSessionDeviceName 约定注释〕；fresh SerialDevice 复用载体帧
    //      链——fresh Stateless 实例，Stateless 同实例二次注册即抛
    //      Stateless.cpp:31）。连杆对自载体连杆提取（值拷贝＋对象句柄）。
    //      ★ 物性表达系重写（适配差异 6〔本批实核增列〕）：RNE 载体按
    //      **关节帧**承载连杆 BodyInfo（GAP-RNE-FRAME 薄适配——上游 RNE 以
    //      关节帧解释质心/惯量），而 ODE 引擎以**体承载帧**解释（连杆帧尾
    //      巴 R_alignᵀ 使体承载帧≡权威连杆系）——权威轴≠z 时两表达相差
    //      R_align 旋转（错配即质心/惯量错位、轨迹大偏差）。FD 会话按规范
    //      值（权威连杆系）直写 fresh 连杆 BodyInfo——mass/质心/惯量逐字
    //      映射（S7 承载帧≡规范帧同款零换算口径）。
    std::vector<std::pair<rwsim::dynamics::BodyInfo, rw::models::Object::Ptr>> linkPairs;
    linkPairs.reserve(carrier.rigidDevice->getLinks().size());
    std::vector<rw::kinematics::Frame*> serialChain;
    serialChain.push_back(carrier.rigidDevice->getBase()->getBodyFrame());
    for (std::size_t i = 0; i < carrier.rigidDevice->getLinks().size(); ++i) {
        const rwsim::dynamics::Body::Ptr& lb = carrier.rigidDevice->getLinks()[i];
        const CanonicalLink& link = chain.links.at(i + 1);  // 载体连杆 i ↔ 规范链 links[i+1]
        rwsim::dynamics::BodyInfo info;
        info.mass       = link.mass.tryValue().value_or(0.0);  // kg（门控已保证 Provided＞0）
        info.masscenter = link.centerOfMass.tryValue().value_or(
            rw::math::Vector3D<double>(0, 0, 0));              // 权威连杆系，m
        info.inertia    = link.inertia.tryValue().value_or(
            rw::math::InertiaMatrix<double>());                // 质心系、权威连杆系姿态
        linkPairs.emplace_back(info, lb->getObject());
        serialChain.push_back(carrier.joints[i]);  // J_{i+1}（链序——载体关节序）
        serialChain.push_back(lb->getBodyFrame()); // LF_{i+1}（体承载帧）
    }
    rw::core::Ptr<rw::models::SerialDevice> sessionDevice = rw::core::ownedPtr(
        new rw::models::SerialDevice(serialChain, kSessionDeviceName, carrier.state));
    rwsim::dynamics::RigidDevice::Ptr sessionRigid = rw::core::ownedPtr(
        new rwsim::dynamics::RigidDevice(carrier.rigidDevice->getBase(), linkPairs,
                                         sessionDevice));

    // ---- 步骤 3d：法兰组合替代应用（适配差异 4——工具/负载物性经本替代
    //      进入引擎动力学；合成在调用方完成（权威法兰系＝体承载帧），此处
    //      零换算直写 BodyInfo——写入面＝Body::getInfo 非 const 访问器）。
    if (flangeOverride != nullptr) {
        if (!std::isfinite(flangeOverride->mass) || flangeOverride->mass <= 0.0) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "stage=FdSession; 法兰替代质量必须为有限正值（实际 "
                                   + std::to_string(flangeOverride->mass)
                                   + " kg）——调用方契约违约");
        }
        rwsim::dynamics::BodyInfo& flangeInfo = sessionRigid->getLinks().back()->getInfo();
        flangeInfo.mass       = flangeOverride->mass;         // kg
        flangeInfo.masscenter = flangeOverride->masscenter;   // m（权威法兰系）
        flangeInfo.inertia    = flangeOverride->inertia;      // kg·m²（对质心、法兰系姿态）
    }

    // ---- 步骤 3e：材质/对象类型盖章＋基座占位几何（引擎体创建的消费键
    //      ——载体体留基线空串；会话默认条目统一盖章，纯装配记账不进物理
    //      语义）。基座占位球：上游 ODEBody FIXED 构造对零几何体取
    //      geomIds[0] 越界（ODEBody.cpp:68——上游 FixedBody 恒带几何，空几
    //      何路径上游不出现）；占位球不与链体接触（全链零几何——无第二接
    //      触面），半径 1 mm 纯越界规避记账。
    carrier.rigidDevice->getBase()->getInfo().material   = kSessionMaterial;
    carrier.rigidDevice->getBase()->getInfo().objectType = kSessionObjectType;
    {
        rw::core::Ptr<rw::geometry::Geometry> baseGeom = rw::core::ownedPtr(
            new rw::geometry::Geometry(
                rw::core::ownedPtr(new rw::geometry::Sphere(rw::math::Q(1, 0.001))),
                "ird-fd-base-placeholder"));
        baseGeom->setFrame(carrier.rigidDevice->getBase()->getBodyFrame());
        carrier.rigidDevice->getBase()->getObject().cast<rw::models::RigidObject>()
            ->addGeometry(baseGeom);  // RigidObject 面（Object 基类无几何写入口）
    }
    for (const rwsim::dynamics::Body::Ptr& lb : sessionRigid->getLinks()) {
        lb->getInfo().material   = kSessionMaterial;
        lb->getInfo().objectType = kSessionObjectType;
    }

    // ---- 步骤 4：引擎 DWC（WorkCell 采用载体 StateStructure——帧树单一
    //      来源零复制；体清单＝基座＋会话连杆〔设备路径接管连杆——inDevice
    //      名称前缀启发式〕；重力＝请求 gBase 直传，见文件头装配架构段）。
    const rw::core::Ptr<rw::models::WorkCell> sessionWc = rw::core::ownedPtr(
        new rw::models::WorkCell(carrier.stateStructure, "ird-fd-session"));
    std::vector<rwsim::dynamics::Body::Ptr> engineBodies;
    engineBodies.push_back(carrier.rigidDevice->getBase());
    for (const rwsim::dynamics::Body::Ptr& lb : sessionRigid->getLinks()) {
        engineBodies.push_back(lb);
    }
    rwsim::dynamics::DynamicWorkCell::Ptr engineDwc = rw::core::ownedPtr(
        new rwsim::dynamics::DynamicWorkCell(
            sessionWc, engineBodies, engineBodies,
            rwsim::dynamics::DynamicWorkCell::ConstraintList{},
            rwsim::dynamics::DynamicWorkCell::DeviceList{sessionRigid},
            rwsim::dynamics::DynamicWorkCell::ControllerList{}));
    engineDwc->getMaterialData().add(kSessionMaterial, "IRD FD session default material");
    engineDwc->getContactData().add(kSessionObjectType, "IRD FD session default object type");
    engineDwc->setGravity(rw::math::Vector3D<double>(gravityBase[0], gravityBase[1],
                                                     gravityBase[2]));

    // ---- 步骤 5：引擎装配（ODESimulator 构造含双精度核对——ode_double 在
    //      案；initPhysics 经常规路径完成世界/静态体/设备装配——基座帧同
    //      一与名称前缀约定使其成立）→ resetScene（位形传入＋约束锚定，
    //      清速且不回种——适配差异 3）。State 取默认须后于 DWC 构造（体注
    //      册扩展状态布局）；q_rw＝q_auth−offset 换算单点。
    try {
        members->engine = rw::core::ownedPtr(new rwsim::simulator::ODESimulator(engineDwc));
    }
    catch (const rw::core::Exception& e) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           std::string{"stage=FdSession; ODE 引擎构造异常："} + e.what());
    }
    members->dwc = engineDwc;
    members->device = sessionRigid;

    // 电机力限＝播种量级（力控模式伺服关闭后不受钳制——见 kSeedForceLimit
    // 注；设备路径构造读该限为伺服初值——须在 initPhysics 前设值）。
    members->device->setMotorForceLimits(rw::math::Q(static_cast<int>(dof), kSeedForceLimit));

    members->state = members->dwc->getWorkCell()->getDefaultState();
    rw::math::Q q0Q(static_cast<int>(dof));
    rw::math::Q qd0Q(static_cast<int>(dof));
    for (std::size_t i = 0; i < dof; ++i) {
        q0Q[static_cast<int>(i)] = q0[i] - members->zeroOffsets[i];   // q_rw（rad 或 m）
        qd0Q[static_cast<int>(i)] = qd0[i];                           // rad/s 或 m/s
    }
    members->device->getModel().setQ(q0Q, members->state);
    members->device->setJointVelocities(qd0Q, members->state);
    try {
        members->engine->initPhysics(members->state);
        members->engine->resetScene(members->state);  // 位形传入＋约束锚定（速度清零）
    }
    catch (const rw::core::Exception& e) {
        throw RuntimeError(RuntimeErrorCode::RobWorkError,
                           std::string{"stage=FdSession; 引擎物理初始化异常："} + e.what());
    }

    // ---- 步骤 6：初速播种（任一 |qd0|>0 时）——Velocity 伺服迭代子步把关
    //      节速度从 0 逼近目标：关节约束 CFM=1e-3（上游硬编码
    //      ODESimulator.cpp:1364）使单步伺服欠收敛（实测 ~6% 欠冲）——迭代
    //      至读回速度逐关节进入 1e-9 相对带或 32 步上限（确定性：固定迭代
    //      界＋确定性收敛路径）；零初速跳过（无瞬态）。播种后电机目标留在
    //      Velocity 态——调用方首个 setForceTargets 在下一 step 前切回
    //      Force（模式切换检测在设备 update 内，语义见
    //      ODEVelocityDevice::update）。
    const bool needSeeding = std::any_of(qd0.begin(), qd0.end(),
                                         [](double v) { return v != 0.0; });
    if (needSeeding) {
        members->device->setMotorVelocityTargets(qd0Q, members->state);
        double qd0Worst = 0.0;
        for (std::size_t i = 0; i < dof; ++i) {
            qd0Worst = std::max(qd0Worst, std::abs(qd0[i]));
        }
        const double seedTolerance = 1e-9 * std::max(1.0, qd0Worst);  // 相对收敛带
        for (int seedIter = 0; seedIter < 32; ++seedIter) {
            members->engine->step(kSeedSubStepS, members->state);
            const rw::math::Q qdNow = members->device->getJointVelocities(members->state);
            double worst = 0.0;
            for (std::size_t i = 0; i < dof; ++i) {
                worst = std::max(worst, std::abs(qdNow[static_cast<int>(i)] - qd0[i]));
            }
            if (worst <= seedTolerance) {
                break;  // 收敛——播种完成
            }
        }
    }

    // ---- 步骤 7：产出（targets 置空＝强制"step 前先 setForceTargets"契约）。
    FdEngineSession out;
    out.m = holder;
    out.m_dof = dof;
    return out;
}

}}}  // namespace sdurws::ird::runtime
