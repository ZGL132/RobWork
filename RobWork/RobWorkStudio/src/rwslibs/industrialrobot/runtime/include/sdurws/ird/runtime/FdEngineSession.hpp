/**
 * @file   FdEngineSession.hpp
 * @brief  正动力学受控引擎会话（WP-09/REUSE-T53——GAP-FD-BUILD-01 提案
 *         执行的 runtime 承接面）：以 CanonicalModel 私有物化副本装配
 *         RobWorkSim ODE 物理引擎（sdurwsim_ode），向业务侧暴露"提交 SI
 *         广义力＋按步长推进＋读回关节状态"的受控会话 API。
 *
 * 设计依据：
 *   - GAP-FD-BUILD-01 §2（供给提案——runtime 负责真实 DWC/RigidDevice、
 *     初始 State 和引擎会话物化；业务只通过受控任务会话提交 SI 广义力/
 *     状态与步长，不能另建 WorkCell 或跨 worker 传借持句柄；操作只允许
 *     在私有物化副本，不获得权威模型写权）
 *   - migration.md §13.3 WP-09 行（dynamics/src/ForwardDynamics.cpp →
 *     WP-08 已验证引擎；保留公共输入/结果、单位和错误语义）
 *   - REUSE-T52 构造面裁决（2026-10-26——模型构造权归 runtime，消费方经
 *     dynamics->runtime 登记边消费；本工厂为同墙复利：会话物化涉及 S6/S7
 *     编译与引擎装配，全部构造收口在本单元）
 *   - units/runtime.md §3.2（P-RT-3 L1 基线：rwsim dynamics＋sdurwsim_ode
 *     ——REUSE-T53 增登）；ARCHITECTURE §3.6（任务私有会话口径）
 *
 * ★ 所有权与生命周期（ARCH §3.6 值类型/任务私有会话口径）：
 *   - 会话持有 S6 WorkCell/S7 DynamicWorkCell 的引用计数句柄（私有物化
 *     副本——每次工厂调用全新对象树，不与任何快照共享）与 ODESimulator
 *     及其私有 State；销毁序＝引擎 exitPhysics 先于对象树析构（RAII，
 *     析构函数显式收口）。
 *   - 权威模型只读：工厂输入 model 为 const 引用，会话零回写；对引擎的
 *     全部可变操作（重力设值/法兰物性替代/电机目标）均发生在私有副本与
 *     会话私有 State 上。
 *   - 非拷贝（可移动）：拷贝会共享同一引擎实例——类型层禁止；每任务一
 *     会话，不跨线程共享（无内部锁）。
 *
 * ★ 引擎适配差异（2026-10-26 上游行号级实核——GAP-FD-BUILD-01 阶段 B
 *   清单逐项落地，全部为消费边界适配、零上游改动）：
 *   1. 力控仅承载转动关节：ODEJoint::setForce 只走 dJointAddAMotorTorques
 *      （rwsimlibs/ode/ODEJoint.hpp:57-66；移动分支 dJointAddSliderForce
 *      为注释死分支）——链含移动关节时工厂 fail-fast（稳定诊断），不产
 *      生静默错误轨迹；
 *   2. 关节摩擦无引擎消费：上游摩擦模拟为注释死代码（ODEJoint.hpp:63、
 *      ODESimulator.cpp:1372-1381）——会话按 S7 承载的声明摩擦三元组
 *      （MaterialDataMap "ird-joint-friction" 约定）逐子步把 τ_fric(q̇)
 *      从驱动输入中扣除（引擎无摩擦 ⇒ 输入侧补偿），使引擎承载与 RNE
 *      同一摩擦模型；缺失三元组（NotProvided）不补偿（两侧同为无摩擦）；
 *   3. 初速度不随 State 传入引擎：resetScene 清速且 ODEJoint::reset 显式
 *      置零（rwsimlibs/ode/ODEJoint.cpp "TODO: these should be set to the
 *      correct velocities" 原文在案；ODEBody::reset 的速度回种先于关节
 *      清零被执行）——非零初速度经 Velocity 伺服单子步播种（大电机力限
 *      一次性逼近目标速度），零初速度跳过播种（无瞬态）；
 *   4. FixedBody（基座/工具体）为纯静态几何载体（ODEBody.cpp makeFixedBody
 *      无 dBody——质量不进积分）：工具体物性经法兰组合替代并入末连杆体
 *      （buildFdEngineSession 的 flangeOverride——合成数学与 dynamics
 *      RNE 侧 composeFlangeVariant 同源，两侧同变体）。
 *
 * 线程安全：单会话单线程使用（引擎非线程安全——ODEThreading 守卫在案）；
 *   并发＝各线程各建会话。
 * 确定性：同（模型字节＋重力＋初态＋力序列＋步长序列）→等价产出（ODE
 *   固定迭代求解、无随机源；WorldStep 直解法——确定性行与 NFR-COR-02）。
 */

#ifndef IRD_RUNTIME_FDENGINESESSION_HPP
#define IRD_RUNTIME_FDENGINESESSION_HPP

#include <sdurws/ird/runtime/CanonicalModel.hpp>  // CanonicalModel（工厂输入权威）

#include <cstddef>
#include <vector>

#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Vector3D.hpp>

namespace sdurws { namespace ird { namespace runtime {

// =====================================================================
// 法兰组合替代（私有物化副本的末连杆物性覆盖值——dynamics 侧合成产物）。
// =====================================================================

/**
 * @brief 末连杆（法兰体）物性替代值——权威末连杆系表示（承载帧与规范系
 *        重合的 S7 口径，零换算直写 BodyInfo）。
 *
 * 语义：把工具＋当前挂载负载合成进末连杆体（dynamics 侧按 §5.4 变体
 * 规则合成，本工厂不做任何合成/降级判定——纯物性直载，"同一物性不得
 * 重复计入"由合成侧保证：S7 工具体为静态几何惰性体〔见文件头适配差异
 * 4〕，替代值是工具/负载进入引擎动力学的唯一通道）。
 * 值语义纯结构；线程安全。
 */
struct FdFlangeOverride {
    double mass = 0.0;                            ///< 合成总质量，单位 kg（＞0——由合成侧保证）
    rw::math::Vector3D<double> masscenter{};      ///< 合成质心，末连杆系下表示，单位 m
    rw::math::InertiaMatrix<double> inertia{};    ///< 对合成质心的惯量张量（末连杆系姿态），
                                                  ///   单位 kg·m²
};

// =====================================================================
// 受控引擎会话（业务侧唯一操作面——SI 广义力/步长进、关节状态出）。
// =====================================================================

/**
 * @brief 正动力学受控引擎会话（ODE 后端的一次性计算载体——业务侧通过
 *        本会话提交控制输入并推进仿真，不接触引擎实现类/State 结构）。
 *
 * 职责（与 dynamics 正动力学检查的协作序）：
 *   1. setForceTargets：提交本步驱动输入 τ（N·m 或 N，按关节类型——
 *      与逆动力学 τ_ref 同量纲；区间采样保持由调用方按区间提交）；
 *   2. step：按步长推进一子步——内部先把声明摩擦补偿进驱动输入（适配
 *      差异 2），再调引擎 step 并随其把 ODE 结果回写会话私有 State
 *      （设备 postUpdate 回写 q/q̇——getQ/getJointVelocities 读数随之
 *      推进）；
 *   3. q/qd：读回当前关节位置/速度（rad·m / rad·s⁻¹·m·s⁻¹——O(1) 状态
 *      读数，供调用方做误差统计与发散检测）。
 *
 * 前置契约（调用方违约＝未定义行为防护面——step 前必须先 setForceTargets
 * 至少一次；构建时工厂已完成 initPhysics/resetScene/初速播种序列）。
 *
 * @throws RuntimeError 会话内引擎异常转译（step 期 ODE 异常——不吞错）。
 */
class FdEngineSession
{
  public:
    /// 构造仅经工厂（buildFdEngineSession）——默认构造产生不可用空会话
    /// （仅支撑移动语义；对空会话调用任何方法属调用方违约，防御面 fail-fast）。
    FdEngineSession() = default;

    /// 非拷贝（共享引擎实例＝双重驱动——类型层禁止）；可移动（所有权转移）。
    FdEngineSession(const FdEngineSession&) = delete;
    FdEngineSession& operator=(const FdEngineSession&) = delete;
    FdEngineSession(FdEngineSession&&) = default;
    FdEngineSession& operator=(FdEngineSession&&) = default;

    /// 析构＝RAII 收口：引擎 exitPhysics（释放 ODE 世界/体/设备约束）先于
    /// 引擎对象与物化副本析构（成员声明序保证）。
    ~FdEngineSession();

    /**
     * @brief 提交本步驱动输入（采样保持语义由调用方承载——每区间提交一次
     *        或逐子步重提交均合法；内部存值，step 时与摩擦补偿合成）。
     *
     * @param tauJoint [in] 逐关节广义力（N·m 或 N，按关节类型；长度＝会话
     *                 DOF；非有限或长度不符＝调用方错误 fail-fast）
     * @throws RuntimeError 码＝InputInvalid：长度不符或含非有限分量
     */
    void setForceTargets(const std::vector<double>& tauJoint);

    /**
     * @brief 推进一子步（dtS 必须有限正值——调用方契约；内部完成摩擦补偿
     *        写入→引擎 step→State 回写全链）。
     *
     * @param dtS [in] 子步长，单位 s（有限正值——0 或负值/非有限 fail-fast）
     * @throws RuntimeError 码＝InputInvalid（dtS 非法）／RobWorkError
     *         （引擎 step 异常转译——不吞错）
     */
    void step(double dtS);

    /// 读回当前关节位置（rad 或 m；长度＝DOF——设备 postUpdate 回写值）。
    std::vector<double> q() const;

    /// 读回当前关节速度（rad/s 或 m/s；长度＝DOF——设备 postUpdate 回写值，
    /// 经设备限位钳制语义见 RigidDevice::setJointVelocities）。
    std::vector<double> qd() const;

    /// 会话自由度（可动关节数——与工厂输入 q0/qd0 长度一致）。
    std::size_t dof() const noexcept { return m_dof; }

  private:
    /// 工厂为本类唯一装配入口（friend——私有成员仅由工厂写入）。
    friend FdEngineSession buildFdEngineSession(
        const CanonicalModel& model, const double gravityBase[3],
        const std::vector<double>& q0, const std::vector<double>& qd0,
        const FdFlangeOverride* flangeOverride);

    /// 工厂装配产物（成员声明序即析构逆序依赖——engine 最后置空）。
    struct Members;                 ///< PIMP 体（集成/冒烟共用实现——单一实现）
    rw::core::Ptr<Members> m;       ///< 实现成员（Ptr 空＝默认构造空会话）
    std::size_t m_dof = 0;          ///< 可动关节数（工厂写入）
    std::vector<double> m_targets;  ///< 最近提交的驱动输入（N·m 或 N——step 时消费）
};

// =====================================================================
// 工厂（S6+S7 复用编译＋引擎装配的唯一入口——构造权归 runtime）。
// =====================================================================

/**
 * @brief 从规范模型物化受控 FD 引擎会话（GAP-FD-BUILD-01 §2 承接面——
 *        私有物化副本＋ODE 引擎装配＋初速播种，一次调用全链就绪）。
 *
 * 装配序（实现体逐步对应）：
 *   1. 输入防御（重力有限/初态长度与链可动关节数一致/初态有限/关节类型
 *      词表——移动关节 fail-fast，见文件头适配差异 1）；
 *   2. S6 compileWorkCell＋S7 compileDynamicWorkCell（复用既有编译器——
 *      不新建第二套编译路径；能力缺失〔连杆物性不全〕＝ SkippedNoPhysics
 *      →稳定诊断拒绝——全零物性链不可正解积分的引擎侧对应面）；
 *   3. 法兰替代：flangeOverride 非空时写末连杆 BodyInfo（合成值直载）；
 *   4. 重力显式设值：请求基座系重力经 R_world_base 投影为世界系写入私有
 *      DWC（检查的声明输入 governs 引擎物理——与 τ_ref 侧同一重力语义，
 *      模型自带 gravityWorld 不消费〔请求侧显式值优先——旧实现口径保留〕）；
 *   5. 引擎装配：ODESimulator（双精度校验随构造）→initPhysics→resetScene
 *      （位形传入＋约束锚定）；初速度非零时 Velocity 伺服单子步播种
 *      （适配差异 3——大电机力限常量在实现文件登记）；
 *   6. 电机目标置零态收口（Force 模式待调用方首次 setForceTargets）。
 *
 * @param model          [in] 快照规范模型（只读——唯一输入权威；工厂不修改）
 * @param gravityBase    [in] 基座系重力加速度，单位 m/s²（长度 3，全有限——
 *                       与逆动力学参考段同源的声明输入）
 * @param q0             [in] 初始关节位置（rad 或 m；长度＝可动关节数，全有限）
 * @param qd0            [in] 初始关节速度（rad/s 或 m/s；长度同上，全有限）
 * @param flangeOverride [in] 法兰组合替代（可空指针——空＝无工具/负载变体，
 *                       末连杆保持 S7 编译物性；指向值由调用方持有，仅
 *                       工厂调用期间存活）
 * @return 就绪会话（step 前须先 setForceTargets——见类契约）
 *
 * @throws RuntimeError 码＝InputInvalid（输入防御/移动关节链）、
 *         DwcCompileFailed（S6/S7 编译异常转译）、RobWorkError（引擎
 *         装配异常/空句柄）；SkippedNoPhysics 亦归 DwcCompileFailed
 *         （detail 区分"能力缺失"——全零物性链不可正解积分）
 */
FdEngineSession buildFdEngineSession(const CanonicalModel& model,
                                     const double gravityBase[3],
                                     const std::vector<double>& q0,
                                     const std::vector<double>& qd0,
                                     const FdFlangeOverride* flangeOverride);

}}}  // namespace sdurws::ird::runtime

#endif  // IRD_RUNTIME_FDENGINESESSION_HPP
