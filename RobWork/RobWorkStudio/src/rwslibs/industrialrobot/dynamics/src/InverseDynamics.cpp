/**
 * @file   InverseDynamics.cpp
 * @brief  逆动力学评估器实现（units/dynamics.md §5）——上游递归牛顿—欧拉
 *         消费边界薄适配、四通道分项、摩擦叠加与样本行组装。
 *
 * 设计依据（契约面见同名公共头 InverseDynamics.hpp 文件头）：
 *   - units/dynamics.md §5.1（输入取自编译产物 CanonicalModel——不重编译、
 *     不自建第二套产品关节链）、§5.2（分项图：四通道分项＋重力折叠＋摩擦
 *     叠加＋外力恒 0）、§5.3（输入覆盖清单）、§5.4（负载事件时间线与模型
 *     变体——保守边界规则）、§5.5（摩擦 sgn₀(0)=0 与 DYN-06 三层降级）、
 *     §5.6（数值稳定性、失败语义与耦合链防御）、§10.0（通用约定）
 *   - 需求 DYN-01/02、MDL-16（摩擦输入链）、MDL-22（重力投影单一消费）、
 *     DYN-04（关节侧结果与候选传动无关）、NFR-COR-02/03
 *   - 任务契约 tasks/REUSE-T52.json（migration §13.3 WP-07——RNE 迁移子任务）
 *
 * 上游复用（H1/H2——WP-07 迁移，替换原自写 RNEA 数值核）：
 *   - 计算核＝rwsim::util::RecursiveNewtonEuler（RobWorkSim 公共 API，目标
 *     sdurwsim）：solve(state, q̇, q̈) 返回 DOF+1 个 Wrench6D（前 DOF 项为
 *     各关节传递 wrench——base 系力＋对关节原点的矩；第 DOF+1 项为工具/
 *     环境项）。本文件只做一层消费边界薄适配（H3）：
 *       ①把 CanonicalModel 权威链值物化为一次性 RNE 面设备（StateStructure
 *         ＋帧树＋SerialDevice＋RigidDevice）——关节帧 tAligned＝origin·
 *         R_align、zeroOffset 烘焙（setFixedTransform(getTransform(offset))）、
 *         连杆帧尾巴 R_alignᵀ，全部与 runtime S6 编译器同源语义（见
 *         axisAlignmentRotation 注释）；R_align 镜像经 RneaTest 独立物理
 *         oracle（任意构型解析/能量平衡/黄金数据集）钉住，非平行真值；
 *       ②GAP-RNE-FRAME 薄适配（A2 裁决批准落点，缺口登记见 reuse/functions/
 *         runtime.json 与 migration §13.2）：上游 RNE 把 BodyInfo.masscenter
 *         与 getInertia() 按**关节帧**解释（RecursiveNewtonEuler.cpp 的
 *         baseTlink.R()*masscenter 与 R*I*Rᵀ），而 canonical 值为权威连杆
 *         系表达——权威轴≠z 时直接喂养会产生质心世界位置错位（探针
 *         τ_x=±5.886 在案）。故入设备前统一换算：masscenter＝R_alignᵀ·com、
 *         inertia＝R_alignᵀ·I·R_align（关节帧＝连杆帧∘R_alignᵀ 的坐标变换）。
 *         S7 映射/§8.2 canonical 断言/modeling DWC 导出零触碰（A2 边界）。
 *   - 上游已知差异的处置（REUSE-T30 suitability §6 次要发现）：①RNE 重力
 *     ctor 默认零——每通道显式 setGravity（基座系投影 g_base，应用一次）；
 *     ②getJointForces 工具条目的 payload 力矩臂符号与手算物理相反（本批
 *     代数复核在案，登记于 REUSE-T52 契约 note）——生产接线不消费 setPayload
 *     通道，工具/已挂负载按 §5.4 合成进法兰 BodyInfo（与原实现同一语义），
 *     第 DOF+1 项 wrench 按 R1 ExternalWrench 恒零（P-DYN-3）不读不用；
 *     ③solveMotorTorques 对移动关节也投影 torque 且末元素复用陈旧 z（上游
 *     缺陷）——按关节类型自行投影，不截尾不盲用（migration §13.3 WP-07 行）。
 *
 * 算法总览（§5.2 分项图；符号约定全链统一）：
 *   - 坐标系：上游递推在**基座系**进行（设备基帧＝基座连杆系）；重力已是
 *     基座系投影（调用方自快照 gravityBase() 取——本域零二次旋转，D-DYN-3；
 *     接口参数面只存在 g_base 向量、不存在 R_world_base——二次旋转在结构
 *     上不可表达，AT-37/RT-BW-4 同类缺陷的接口级拦截）；
 *   - 关节角：输入轨迹为**权威角** q_auth（§4.5；units/dynamics.md §5.3
 *     "全链使用权威角，与 rwsim 引擎/状态交互的换算在适配层单点执行"）。
 *     喂给上游 State 的 q_rw＝q_auth−zeroOffset（T13 语义：设备关节变换已
 *     烘焙 offset，q_auth＝zeroOffset+q_rw）；输出样本行回记 q_auth 同值；
 *   - 体号约定：力学体 j＝规范连杆 links[j]（j=1..n，随关节 j-1 运动；
 *     links[0] 为基座连杆，固连基座、其物性不产生任何关节力矩故不入设备
 *     递推体序列）；工具与已挂负载按 §5.4 合成为法兰体（links[n]）参数；
 *   - 重力折叠：上游 solve 内部以基座线加速度 −g_base 起始（重力以惯性力
 *     形式进入全链——Craig 递归牛顿—欧拉标准做法，setGravity 显式传递、
 *     应用恰好一次，禁止任何下游二次旋转）；
 *   - 分项拆分：τ_total/τ_gravity/τ_inertia/τ_coriolis 四通道＝同一设备与
 *     State 下四组 (g, q̇, q̈) 输入组合（动力学方程 M(q)q̈＋C(q,q̇)q̇＋G(q)
 *     对 q̈ 线性、对 q̇ 二次齐次、重力线性，无交叉项——三通道之和恒等于
 *     全量通道，黄金算例校验该恒等式）；
 *   - 摩擦叠加：τ_fric,i = fv_i·q̇_i + fc_i·sgn₀(q̇_i) + bias_i（§5.5，
 *     sgn₀(0)=0——零速含驻留库仑项为零，静摩擦不在 R1 模型）；上游 RNE 无
 *     逐关节摩擦一职（REUSE-T30 §4 边界），摩擦为产品层叠加、不入上游；
 *   - 外力项：第 DOF+1 项 wrench 恒不读（ExternalWrench R1 NotApplicable
 *     ——P-DYN-3，不伪造零以外的值）。
 */

#include <sdurws/ird/dynamics/InverseDynamics.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>  // DYN-* 码值常量（唯一书写点——产码共用）
#include <sdurws/ird/runtime/RneConsumptionDevice.hpp> // runtime RNE 载体工厂（构造权归 runtime——
                                                       // 2026-10-26 所有者构造面上收裁决；dynamics->runtime
                                                       // 登记边消费，dynamics 侧零模型构造）

#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/math/Wrench6D.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/SerialDevice.hpp>  // setQ/getBase 完整类型（构造归 runtime 工厂——本域零构造）
#include <rwsim/dynamics/Body.hpp>
#include <rwsim/dynamics/RigidDevice.hpp>
#include <rwsim/util/RecursiveNewtonEuler.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 模型提取产物（纯值面——摩擦/末端件/关节元数据；几何不再自持，
// 由 RNE 面设备承载——WP-07 迁移删除 Vec3/Mat3/SampleGeometry 旧核）。
// =====================================================================

/**
 * @brief 逐关节摩擦参数（MDL-16 三元组的提取形态——四态已解析）。
 *
 * 缺失语义（§5.5 第 1 层）：任一分量 NotProvided → 对应 present=false 且
 * 数值按 0 计入；同时上层标记 frictionMissing=true＋DYN-FRICTION-MISSING
 * 素材——数值继续、证据不包装精确（两个层面分开，§5.3 输入覆盖表行 7）。
 */
struct JointFriction {
    bool viscousPresent = false;  ///< 黏性系数 fv 已提供（单位 N·m·s/rad；移动关节 N·s/m）
    bool coulombPresent = false;  ///< 库仑力矩 fc 已提供（单位 N·m；移动关节 N）
    bool biasPresent = false;     ///< 偏置已提供（单位 N·m；移动关节 N）
    double viscous = 0.0;         ///< fv 数值（缺失时占位 0——§5.5 分项按 0 计入）
    double coulomb = 0.0;         ///< fc 数值（同上）
    double bias = 0.0;            ///< 偏置数值（同上）
};

/**
 * @brief 关节行装配/诊断元数据（样本行与素材定位所需的最小身份集）。
 *
 * 几何/物性已进 RNE 面设备；本结构只保留样本行回记（jointType/q 列归属）
 * 与诊断定位（ARC-04/MDL-14）所需字段。值语义纯结构。
 */
struct JointMeta {
    DynJointType type = DynJointType::Revolute; ///< 关节类型（行回记＋投影分支）
    double zeroOffset = 0.0;  ///< 零位偏置，单位 rad（移动关节 m）——q_rw＝q_auth−offset
                              ///<  的适配层单点换算分量（有限性校验＝快照防御面）
    core::ObjectId objectId;  ///< 关节稳定对象 ID（样本行/诊断定位——ARC-04）
    std::string localName;    ///< 关节权威局部名（诊断 localName 字段——MDL-14）
    JointFriction friction;   ///< 摩擦参数（MDL-16——四态已解析）
};

/**
 * @brief 末端合成体的组成件（工具/负载在法兰系的静参数——每变体合成一次）。
 *
 * 所有量在**法兰系**（links[n] 系）表示：comF 为质心在法兰系位置（单位
 * m，已含"质心相对 TCP 的偏置经 T_flange_tcp 旋转"）；inertiaC 为对质心、
 * 法兰系姿态的惯量张量（单位 kg·m²——TCP 系姿态下的张量经 R·I·Rᵀ 旋转）。
 */
struct EndComponent {
    double mass = 0.0; ///< 质量，单位 kg（必填——提取层拒绝非正/非有限值）
    rw::math::Vector3D<double> comF{};       ///< 质心，法兰系下表示，单位 m（缺失→安装点＝TCP，D-DYN-6）
    rw::math::InertiaMatrix<double> inertiaC{}; ///< 惯量（质心系、法兰系姿态），单位 kg·m²（缺失→零张量＝点质量）
    bool estimated = false; ///< com/inertia 存在保守估算或 GeometricEstimate 来源（D-DYN-6）
    core::ObjectId objectId; ///< 组成件对象 ID（工具或负载——诊断定位）
    std::string localName;   ///< 件名（诊断 localName 字段）
};

// =====================================================================
// 物性防御性复检（§5.3 行 2——建模侧 MDL-06 断言前置，dynamics 对跨版本
// 快照的防御面；值形态为 rw 值类型——WP-07 迁移后 rw::math 全程可用）。
// =====================================================================

/**
 * @brief 物性复检：值有限＋惯量张量对称性**相对**容差 1×10⁻¹²。
 *
 * 对称性口径出处：需求附录 D 第 6 项原文「惯量张量对称性数值容差＝相对
 * 1×10⁻¹²」（单元卡 §6.4 两处同口径）。F-645 语义（迁移前实现已修正，
 * 本迁移原样保留）：‖Δ‖_∞ / ‖(I+Iᵀ)/2‖_F ≤ tol 的真相对比较，退化时
 * （对称部分范数为 0）退回绝对容差。建模侧分工：SPD 完整复检不在此
 * 重复——建模侧 MDL-06 断言②已强制，本域不实现第二套 SPD 判定。
 *
 * @param mass    [in] 质量，单位 kg
 * @param com     [in] 质心，体系下表示，单位 m
 * @param inertia [in] 惯量张量（质心系、体系姿态），单位 kg·m²
 * @param subject [in] 诊断文本定位串（连杆/末端件名）
 * @throws DynamicsError 含非有限分量/对称性偏差超相对容差（比较型
 *         detail——输入非法，调用方错误轨）
 */
void assertPropertiesFiniteAndSymmetric(double mass,
                                        const rw::math::Vector3D<double>& com,
                                        const rw::math::InertiaMatrix<double>& inertia,
                                        const std::string& subject)
{
    const bool finite = std::isfinite(mass) && std::isfinite(com[0]) && std::isfinite(com[1])
        && std::isfinite(com[2]) && std::isfinite(inertia(0, 0)) && std::isfinite(inertia(0, 1))
        && std::isfinite(inertia(0, 2)) && std::isfinite(inertia(1, 0)) && std::isfinite(inertia(1, 1))
        && std::isfinite(inertia(1, 2)) && std::isfinite(inertia(2, 0)) && std::isfinite(inertia(2, 1))
        && std::isfinite(inertia(2, 2));
    if (!finite) {
        throw DynamicsError("input-invalid", "物性防御复检失败（含非有限分量）：" + subject);
    }
    // 对称性相对容差（附录 D 第 6 项——无量纲比值上限；元素单位 kg·m²）。
    constexpr double kSymmetryRelTolerance = 1e-12;
    // 第一步：参考模长＝对称化张量 (I+Iᵀ)/2 的 Frobenius 范数（分母取对称
    // 部分：待检对象是反对称偏差，整张量入模会稀释自身比值——F-645）。
    double symNormSq = 0.0;  // ‖(I+Iᵀ)/2‖_F²，单位 (kg·m²)²
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double s = 0.5 * (inertia(i, j) + inertia(j, i));
            symNormSq += s * s;
        }
    }
    // 第二步：最大反对称偏差 ‖Δ‖_∞＝max|I_ij−I_ji|（单位 kg·m²）。
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            worst = std::max(worst, std::abs(inertia(i, j) - inertia(j, i)));
        }
    }
    // 第三步：相对比较；对称部分范数为 0（零张量/纯反对称注入）时退回
    // 绝对容差比较（零张量 worst=0 自然通过；纯反对称 worst>0 必拒）。
    const double symNorm = std::sqrt(symNormSq);  // 单位 kg·m²
    const double limit =
        (symNorm > 0.0) ? kSymmetryRelTolerance * symNorm : kSymmetryRelTolerance;
    if (worst > limit) {
        throw DynamicsError("input-invalid",
                            "物性防御复检失败（惯量张量非对称，最大偏差 " + std::to_string(worst)
                                + " 超容差上限 " + std::to_string(limit)
                                + "＝相对 1e-12×‖(I+Iᵀ)/2‖_F）：" + subject);
    }
}

// =====================================================================
// 末端组成件提取（工具/负载——物性四态解析＋保守估算标记，D-DYN-6）。
// =====================================================================

/**
 * @brief 末端组成件提取（工具或负载——法兰系静参数）。
 *
 * 保守估算规则（D-DYN-6，§5.4）：com 缺失→取安装点（TCP）；inertia 缺失
 * →点质量模型（零转动惯量）——两种情形均强制 estimated=true（计数＋
 * DYN-PROPERTY-DOWNGRADED 素材），绝不包装为精确结论（DYN-06）。值已提供
 * 但 Provenance=GeometricEstimate（MDL-05 估算语义）同样计入估算标记
 * （§5.5 第 2 层）。
 *
 * @param massF          [in] 质量 SourcedValue（必填——Provided 且有限＞0）
 * @param comF           [in] 质心 SourcedValue（TCP 系下表示，单位 m；可缺失）
 * @param inertiaF       [in] 惯量 SourcedValue（质心系、TCP 姿态，kg·m²；可缺失）
 * @param tcpPosInFlange [in] TCP 安装点在法兰系的位置，单位 m（com 缺失时保守取点）
 * @param tcpRotToFlange [in] TCP 系→法兰系旋转（com/惯量姿态变换用）
 * @param objectId       [in] 组成件对象 ID（诊断定位）
 * @param localName      [in] 件权威局部名（诊断 localName 字段）
 * @param subjectLabel   [in] 诊断文本用件类名（"工具"/"负载"）
 * @param outEstimated   [out] 发生保守估算/估算来源时置 true（调用方累计计数）
 * @return 法兰系表示的组成件参数
 *
 * @throws DynamicsError mass 非 Provided/非有限/非正值（调用方错误——
 *         requirements 结构中 mass 为必填〔D-DYN-6 原文〕；MDL-06 分域
 *         语义：缺失走降级的是 com/inertia 可选字段，必填字段缺失/非法
 *         属调用方契约违约，fail-fast）
 */
EndComponent extractEndComponent(const core::SourcedValue<double>& massF,
                                 const core::SourcedValue<rw::math::Vector3D<double>>& comF,
                                 const core::SourcedValue<rw::math::InertiaMatrix<double>>& inertiaF,
                                 const rw::math::Vector3D<double>& tcpPosInFlange,
                                 const rw::math::Rotation3D<double>& tcpRotToFlange,
                                 const core::ObjectId& objectId,
                                 const std::string& localName,
                                 const std::string& subjectLabel,
                                 bool& outEstimated)
{
    // 质量：必填字段——缺失/非法一律 fail-fast（不伪造 0 质量件继续评估）。
    const std::optional<double> massOpt = massF.tryValue();
    if (!massOpt.has_value() || !std::isfinite(*massOpt) || *massOpt <= 0.0) {
        throw DynamicsError("input-invalid",
                            subjectLabel + "质量必填且须为有限正值（kg）：" + localName
                                + "（" + objectId.toCanonical() + "）");
    }
    EndComponent c;
    c.mass = *massOpt;
    c.objectId = objectId;
    c.localName = localName;

    // 质心：缺失→保守取安装点（TCP 在法兰系位置——低估力臂侧，D-DYN-6）。
    if (const std::optional<rw::math::Vector3D<double>> com = comF.tryValue()) {
        c.comF = tcpPosInFlange + tcpRotToFlange * *com;  // TCP 偏置→法兰系
        if (comF.provenance().kind == core::ProvenanceKind::GeometricEstimate) {
            c.estimated = true;  // 值已提供但来源＝几何估算（§5.5 第 2 层）
        }
    } else {
        c.comF = tcpPosInFlange;
        c.estimated = true;      // 缺失→保守估算（D-DYN-6 强制 estimated）
    }

    // 惯量：缺失→点质量模型（零转动惯量——"低估惯性"提示语义，D-DYN-6）；
    // 已提供→姿态旋转到法兰系（R·I·Rᵀ）并做有限性/对称性防御复检。
    // ★ 惯量缺失分支显式置零（WP-17-T05 修复语义——InertiaMatrix 默认构造
    //   为零张量〔非单位阵〕，此处显式化以自证"缺失＝零贡献"契约）。
    if (const std::optional<rw::math::InertiaMatrix<double>> inertia = inertiaF.tryValue()) {
        const rw::math::InertiaMatrix<double> iTcp = *inertia;  // 质心系、TCP 姿态（kg·m²）
        c.inertiaC = tcpRotToFlange * iTcp * rw::math::inverse(tcpRotToFlange);
        assertPropertiesFiniteAndSymmetric(c.mass, c.comF, iTcp,
                                           subjectLabel + " " + localName);
        if (inertiaF.provenance().kind == core::ProvenanceKind::GeometricEstimate) {
            c.estimated = true;
        }
    } else {
        c.inertiaC = rw::math::InertiaMatrix<double>();  // 零张量（点质量无转动惯量）
        c.estimated = true;
    }
    if (c.estimated) { outEstimated = true; }
    return c;
}

// =====================================================================
// RNE 消费面（上游 RecursiveNewtonEuler 的宿主——设备/State/求解器/
// 摩擦与变体合成保留面的所有权与生命周期集合）。
// =====================================================================

/**
 * @brief RNE 消费面集合（一次 evaluate 的完整计算载体）。
 *
 * 所有权与生命周期（ARCH §3.6 值类型/任务私有会话口径）：
 *   - rne（runtime RneConsumptionDevice 工厂产物——模型构造权归 runtime，
 *     2026-10-26 所有者构造面上收裁决）持有帧树/设备/State 的所有权链，
 *     随本结构销毁整链消亡；solver 持 rigidDevice 引用——销毁序＝成员逆序
 *     （solver→rne），无悬垂窗口；
 *   - state 为任务私有 State（evaluate 栈上生命周期）——不触共享快照
 *     State（runtime §5.7 线程纪律）；并发＝各线程各持请求与本结构；
 *   - 设备帧名为 "ird-rne/*" 私有名空间——零产品名称权威出口（本载体是
 *     evaluate 内一次性计算载体，不持久化、不外露，CM-0 登记语义）。
 */
struct RneConsumptionChain {
    // ---- 上游对象链（runtime 工厂构造——构造序即依赖序；solver 无默认
    //      构造，以 optional 在设备就绪后 emplace）----
    runtime::RneConsumptionDevice rne;                          ///< RNE 载体（帧树/设备/State 所有权根——runtime 工厂产物）
    std::optional<rwsim::util::RecursiveNewtonEuler> solver;    ///< 上游 RNE（ctor 即 validate——不可用即 RW_THROW）

    // ---- 行装配/诊断元数据（纯值——链序对齐；摩擦/估算/降级语义在本域）----
    std::vector<JointMeta> meta;                                ///< 关节元数据（objectId/localName/摩擦/行回记）

    // ---- 末端变体合成保留面（§5.4——法兰基线在 rne.flangeBase*，只读）----
    std::optional<EndComponent> tool;                           ///< 默认 TCP 工具件（无工具模型＝nullopt）
    rw::math::Vector3D<double> tcpPosInFlange{};                ///< TCP 安装点在法兰系的位置，单位 m（无工具＝法兰原点）
    rw::math::Rotation3D<double> tcpRotToFlange{};              ///< TCP 系→法兰系旋转（负载惯量姿态旋转用）
    core::ObjectId toolObjectId;                                ///< 工具对象 ID（样本行透传；无工具＝空 id）
    bool frictionMissing = false;                               ///< 任一关节摩擦分量缺失（§5.5 第 1 层——MDL-16 闭环）
    std::size_t estimatedLinkCount = 0;                         ///< 估算物性连杆数（连杆物性来源＝GeometricEstimate）
    std::size_t estimatedPayloadCount = 0;                      ///< 估算物性末端件数（工具/负载 com/inertia 保守估算）
};

// =====================================================================
// 提取：CanonicalModel → RNE 消费面（§5.1——编译产物唯一读取点）。
// =====================================================================

/**
 * @brief 从 CanonicalModel 构建 RNE 消费面（验证提取＋runtime 工厂调用）。
 *
 * 职责分界（2026-10-26 所有者构造面上收裁决——模型构造权归 runtime）：
 *   - 本函数做**纯值验证/提取**：链词表/轴单位/零位偏置有限性防御复检、
 *     物性有限性＋对称性复检（F-645）、摩擦四态解析（MDL-16/F-632）、估算
 *     计数、工具面提取——全部 DynamicsError 语义保持（调用方错误轨）；
 *   - 设备构造与 GAP-RNE-FRAME 关节帧换算归 runtime 工厂
 *     runtime::buildRneConsumptionDevice（经已登记 dynamics->runtime 边；
 *     R_align 知识回归模型 owner 本位——本文件零模型构造）。
 *
 * @param model [in] 快照规范模型（只读——唯一输入权威）
 * @return 就绪的消费面（设备可用性由工厂构造＋上游 validate 保证）
 *
 * @throws DynamicsError 调用方错误：空链/Fixed 词表外关节/轴非单位/零位
 *         偏置非有限/物性复检失败/摩擦已提供非有限/工具质量非法（逐项
 *         detail 中文定位）；runtime RuntimeError 为工厂不可达防御轨
 */
RneConsumptionChain buildRneConsumptionChain(const runtime::CanonicalModel& model)
{
    const runtime::RobotChain& chain = model.chain();
    RneConsumptionChain out;

    // ---- 前置：链非空（§10.1 前置行——空链没有评估意义，fail-fast）----
    if (chain.joints.empty()) {
        throw DynamicsError("input-invalid", "模型链无可动关节（空链）——拒绝评估");
    }

    // 工具对象 ID（KIN-14 默认 TCP 项；无工具模型＝空 id——样本行如实透传）。
    if (model.defaultTcpIndex().has_value()) {
        out.toolObjectId = model.tools().at(*model.defaultTcpIndex()).objectId;
    }

    const std::size_t n = chain.joints.size();
    out.meta.reserve(n);

    for (std::size_t i = 0; i < n; ++i) {
        const runtime::CanonicalJoint& cj = chain.joints.at(i);

        // ---- 关节类型词表（DynJointType 三值；Fixed 词表外 fail-fast）----
        DynJointType type;
        switch (cj.type) {
            case runtime::JointType::Revolute:   type = DynJointType::Revolute; break;
            case runtime::JointType::Continuous: type = DynJointType::Continuous; break;
            case runtime::JointType::Prismatic:  type = DynJointType::Prismatic; break;
            case runtime::JointType::Fixed:
                // 词表外类型（刚性连接、无自由度）——R1 评估面不支持；
                // 上游 RNE validate 同样拒绝（RecursiveNewtonEuler.cpp 的
                // dynamic_cast 词表），fail-fast 不静默跳过（§10.0 调用方
                // 错误轨；S6 虽可编译 Fixed 段，本消费面按既有评估契约拒绝）。
                throw DynamicsError("input-invalid",
                                    "模型链携带 Fixed 关节（词表外类型，关节序 " +
                                        std::to_string(i) + "）——R1 评估面仅支持三个可动类型");
        }

        // 轴单位性防御复检（编译链 build() 已规格化——跨版本快照防御）。
        const double axisNorm = cj.axis.norm2();
        if (!std::isfinite(axisNorm) || std::abs(axisNorm - 1.0) > 1e-9) {
            throw DynamicsError("input-invalid",
                                "关节轴向非单位向量（‖axis‖=" + std::to_string(axisNorm) +
                                    "，关节序 " + std::to_string(i) + "）——编译产物应已规格化");
        }
        if (!std::isfinite(cj.zeroOffset)) {
            throw DynamicsError("input-invalid",
                                "关节零位偏置非有限（关节序 " + std::to_string(i) + "）");
        }

        // ---- 驱动体物性复检＝links[i+1]（链约定：下标 i 的连杆是关节 i 的
        //      父体）。§5.5 降级通道缺失按 0/保守值（工厂同口径填充）；本域
        //      承载证据面（复检/估算计数），数值面归工厂换算。
        const runtime::CanonicalLink& link = chain.links.at(i + 1);
        const double mass = link.mass.tryValue().value_or(0.0);  // 单位 kg
        const rw::math::Vector3D<double> com = link.centerOfMass.tryValue().value_or(
            rw::math::Vector3D<double>(0, 0, 0));                // 连杆系下，单位 m
        const rw::math::InertiaMatrix<double> inertia =
            link.inertia.tryValue().value_or(rw::math::InertiaMatrix<double>());  // 质心系，kg·m²
        assertPropertiesFiniteAndSymmetric(
            mass, com, inertia, "连杆 " + link.localName + "（" + link.objectId.toCanonical() + "）");
        // 估算计数（§5.5 第 2 层）：连杆物性任一分量来源＝GeometricEstimate
        // （MDL-05 估算）——该连杆计 1（缺失走素材通道，不冒充估算计数）。
        const bool massEst = link.mass.tryValue().has_value()
            && link.mass.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        const bool comEst = link.centerOfMass.tryValue().has_value()
            && link.centerOfMass.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        const bool inertiaEst = link.inertia.tryValue().has_value()
            && link.inertia.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        if (massEst || comEst || inertiaEst) { ++out.estimatedLinkCount; }

        // ---- 摩擦三元组（MDL-16——四态独立解析，任一缺失即 frictionMissing）。
        //      F-632 防线（提取源头拦截）：Provided 值逐分量 std::isfinite 复检
        //      （①分量缺失＝能力缺失→§5.5 第 1 层降级按 0 继续；②已提供但
        //      非有限＝数据非法→fail-fast，防 NaN 先污染输出再被样本级检出）。
        JointFriction f;
        if (const std::optional<double> v = cj.friction.viscous.tryValue()) {
            if (!std::isfinite(*v)) {
                throw DynamicsError("input-invalid",
                                    "摩擦参数防御复检失败：黏性系数 fv 已提供但非有限"
                                    "（N·m·s/rad，关节 " + cj.localName + "，"
                                        + cj.objectId.toCanonical() + "）——拒绝进入模型");
            }
            f.viscousPresent = true;
            f.viscous = *v;   // 单位 N·m·s/rad（移动关节 N·s/m）
        }
        if (const std::optional<double> cc = cj.friction.coulomb.tryValue()) {
            if (!std::isfinite(*cc)) {
                throw DynamicsError("input-invalid",
                                    "摩擦参数防御复检失败：库仑系数 fc 已提供但非有限"
                                    "（N·m，关节 " + cj.localName + "，"
                                        + cj.objectId.toCanonical() + "）——拒绝进入模型");
            }
            f.coulombPresent = true;
            f.coulomb = *cc;  // 单位 N·m（移动关节 N）
        }
        if (const std::optional<double> b = cj.friction.bias.tryValue()) {
            if (!std::isfinite(*b)) {
                throw DynamicsError("input-invalid",
                                    "摩擦参数防御复检失败：偏置 bias 已提供但非有限"
                                    "（N·m，关节 " + cj.localName + "，"
                                        + cj.objectId.toCanonical() + "）——拒绝进入模型");
            }
            f.biasPresent = true;
            f.bias = *b;      // 单位 N·m（移动关节 N）
        }
        if (!f.viscousPresent || !f.coulombPresent || !f.biasPresent) {
            out.frictionMissing = true;  // §5.5 第 1 层——数值按 0 继续，证据降级
        }

        JointMeta jm;
        jm.type = type;
        jm.zeroOffset = cj.zeroOffset;  // 单位 rad（移动关节 m）——q_rw 换算分量
        jm.objectId = cj.objectId;
        jm.localName = cj.localName;
        jm.friction = f;
        out.meta.push_back(std::move(jm));
    }

    // ---- 末端工具面（§5.3"末端工具"行——默认 TCP 项）----
    // 法兰系＝links[n] 系（末关节驱动体）；TCP 几何来自工具 tcpOffset
    // （T_flange_tcp——KIN-14 权威）。无工具模型时 TCP＝法兰原点（恒等
    // 偏置）——负载仍可挂法兰原点，评估继续（工具 optional 语义）。
    if (model.defaultTcpIndex().has_value()) {
        const runtime::CanonicalTool& tool = model.tools().at(*model.defaultTcpIndex());
        out.tcpRotToFlange = tool.tcpOffset.R();
        out.tcpPosInFlange = tool.tcpOffset.P();
        bool toolEstimated = false;
        out.tool = extractEndComponent(tool.mass, tool.centerOfMass, tool.inertia,
                                       out.tcpPosInFlange, out.tcpRotToFlange, tool.objectId,
                                       tool.localName, "工具", toolEstimated);
        if (toolEstimated) { ++out.estimatedPayloadCount; }
    }

    // ---- 设备构造（runtime 工厂——构造权/换算归模型 owner；头面引用
    //      GAP-RNE-FRAME 薄适配与裁决脉络）＋求解器挂接（上游 ctor 内
    //      validate——SerialDevice＋关节词表由工厂构造面保证）。
    out.rne = runtime::buildRneConsumptionDevice(model);
    out.solver.emplace(out.rne.rigidDevice);
    return out;
}

// =====================================================================
// 末端体合成（§5.4——工具＋已挂负载并入法兰体；平行轴定理）＋薄适配回写。
// =====================================================================

/**
 * @brief 把工具与挂载负载合成进末关节驱动体（links[n]——法兰体）。
 *
 * 合成规则（全部在法兰系表示，单位：kg / m / kg·m²）：
 *   1. 总质量 M ＝ 法兰连杆基线质量 ＋ Σ 组件质量；
 *   2. 总质心 c^F ＝ Σ m_k·c_k^F / M（质量加权——一阶矩守恒）；
 *   3. 对总质心的合成惯量 ＝ Σ [I_k^F ＋ m_k·(|d_k|²E − d_k d_kᵀ)]
 *      （平行轴定理；d_k 为各体质心到总质心的位移向量）。
 *
 * ★ 合成基项＝chain.flangeBase*（提取时的法兰连杆原始物性，只读基线）——
 *   **绝不**以上次合成值为基项（否则变体每次切换会重复累加工具/负载）。
 *
 * 合成值经 R_alignᵀ 换算为关节帧表达后写回设备末连杆 BodyInfo
 * （Body::getInfo 非 const 访问器——上游 setMass 同款写入面）。合成在
 * 法兰系完成、换算单点在写回处：变体切换零几何重算。
 *
 * 边界：M ≤ 0（全部物性缺失的极端形态）——质心取法兰原点、惯量取零
 * （力矩贡献为零、数值安全），不抛异常（缺失已走 §5.5 降级素材通道）。
 *
 * @param chain           [in,out] 消费面（末连杆 BodyInfo 被合成值覆盖；
 *                        flangeBase* 基线只读不动）
 * @param mountedPayloads [in] 当前挂载的负载组件（法兰系静参数）
 */
void composeFlangeVariant(RneConsumptionChain& chain,
                          const std::vector<EndComponent>& mountedPayloads)
{
    // 组件清单＝工具（若有）＋挂载负载。
    std::vector<const EndComponent*> parts;
    if (chain.tool.has_value()) { parts.push_back(&*chain.tool); }
    for (const EndComponent& p : mountedPayloads) { parts.push_back(&p); }

    // 第一步：总质量与总质心（质量加权一阶矩；全在法兰系）。
    double totalMass = chain.rne.flangeBaseMass;
    rw::math::Vector3D<double> firstMoment = chain.rne.flangeBaseMass * chain.rne.flangeBaseCom;
    for (const EndComponent* p : parts) {
        totalMass += p->mass;
        firstMoment += p->mass * p->comF;
    }
    rw::math::Vector3D<double> compositeCom{};
    rw::math::InertiaMatrix<double> compositeInertia = rw::math::InertiaMatrix<double>();
    if (totalMass > 0.0) {
        compositeCom = firstMoment / totalMass;  // 质心（法兰系，m）
        // 第二步：对总质心的合成惯量（平行轴定理逐项累加——确定性求和序）。
        // 修正项 (r,cc) 元＝δrc·|d|² − d_r·d_c；基项为法兰连杆自身（对总
        // 质心平行轴），组件惯量已旋转到法兰系姿态（extractEndComponent）。
        auto addParallelAxis = [&compositeInertia, &compositeCom](
                                   const rw::math::InertiaMatrix<double>& inertia,
                                   const rw::math::Vector3D<double>& comF, double mass) {
            const rw::math::Vector3D<double> d = comF - compositeCom;
            const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    const double dOuter = d[r] * d[c];
                    const double correction =
                        ((r == c) ? dd : 0.0) - dOuter;  // δrc·|d|² − d_r·d_c
                    compositeInertia(r, c) += inertia(r, c) + mass * correction;
                }
            }
        };
        addParallelAxis(chain.rne.flangeBaseInertia, chain.rne.flangeBaseCom, chain.rne.flangeBaseMass);
        for (const EndComponent* p : parts) {
            addParallelAxis(p->inertiaC, p->comF, p->mass);
        }
    }
    // totalMass==0：质心/惯量保持零初始化（零力矩贡献、数值安全——§5.5）。

    // 薄适配回写：合成值（法兰系）→ 关节帧表达 → 设备末连杆 BodyInfo。
    // 写入面＝Body::getInfo() 非 const 访问器（上游 setMass 同款）；合成
    // 只影响末体（links[n]），其余连杆 BodyInfo 构造期定值不动。
    // 惯量为相似变换：I_关节帧＝R_alignᵀ·I_法兰系·R_align（与连杆体构造
    // 期 info.inertia＝rAlignT·I·rAlign 同式——两处共轭必须一致）。
    rwsim::dynamics::BodyInfo& flangeInfo =
        chain.rne.rigidDevice->getLinks().back()->getInfo();
    flangeInfo.mass = totalMass;
    flangeInfo.masscenter = chain.rne.flangeAlignInverse * compositeCom;
    flangeInfo.inertia =
        chain.rne.flangeAlignInverse * compositeInertia * rw::math::inverse(chain.rne.flangeAlignInverse);
}

// =====================================================================
// 分项通道输入组合（§5.2 恒等式的四通道来源——见公共头类注释）。
// =====================================================================

/// 摩擦项：τ_fric = fv·q̇ + fc·sgn₀(q̇) + bias（§5.5——sgn₀(0)=0）。
double frictionTerm(const JointFriction& f, double qd)
{
    // sgn₀：q̇>0 取 +1、q̇<0 取 −1、q̇==0 取 0（零速含驻留库仑项为零——
    // 静摩擦/起动摩擦不在 R1 模型，不引入平滑阈值避免自设数值）。
    const double sign0 = (qd > 0.0) ? 1.0 : (qd < 0.0 ? -1.0 : 0.0);
    return f.viscous * qd + f.coulomb * sign0 + f.bias;
}

/**
 * @brief 单通道求解：设重力、上游 solve、按关节类型投影。
 *
 * 投影口径（migration §13.3 WP-07 行"按关节类型投影"）：上游 solve 返回
 * DOF+1 个 base 系 wrench（前 DOF 项为各关节传递 wrench——力过关节原点、
 * 矩对关节原点）；广义力＝
 *   - 旋转/连续关节：τ_i ＝ τ_i·z_i（绕轴矩分量，N·m）；
 *   - 移动关节：τ_i ＝ F_i·z_i（沿轴力分量，N）；
 * 其中 z_i＝关节 i 轴的 base 系单位向量（frameTframe(base, joint_i).R()
 * 第 2 列——关节帧 z 恒为权威轴，S6 对齐语义）。不盲用上游
 * solveMotorTorques：其对移动关节也投影 torque、末元素复用陈旧 z（上游
 * 缺陷，RecursiveNewtonEuler.cpp:249-259）——本函数自行按类型分支。
 * 第 DOF+1 项（工具/环境）恒不读（R1 ExternalWrench＝0，P-DYN-3）。
 *
 * State 位置由调用方在通道循环前置入（四通道共享同一位形——q_rw＝
 * q_auth−zeroOffset 换算单点在样本循环内，见 evaluate 第 5 步注释）。
 *
 * @param chain   [in,out] 消费面（重力经 solver 逐通道显式设值——T30 次要
 *                发现①：上游 ctor 默认零重力，不设值即静默无重力）
 * @param gBase   [in] 基座系重力加速度，单位 m/s²（长度 3）
 * @param qd      [in] 关节速度（rad/s 或 m/s；长度 n）
 * @param qdd     [in] 关节加速度（rad/s² 或 m/s²；长度 n）
 * @param tau     [out] 关节广义力（N·m 或 N，按关节类型；长度 n，调用方预分配）
 */
void solveChannel(RneConsumptionChain& chain, const double gBase[3],
                  const std::vector<double>& qd, const std::vector<double>& qdd,
                  std::vector<double>& tau)
{
    const std::size_t n = chain.meta.size();
    // 重力：显式传递（应用一次——上游内部以 −g 作基座线加速度折叠）。
    chain.solver->setGravity(rw::math::Vector3D<double>(gBase[0], gBase[1], gBase[2]));
    rw::math::Q qdQ(static_cast<int>(n));
    rw::math::Q qddQ(static_cast<int>(n));
    for (std::size_t i = 0; i < n; ++i) {
        qdQ[static_cast<int>(i)] = qd[i];
        qddQ[static_cast<int>(i)] = qdd[i];
    }
    const std::vector<rw::math::Wrench6D<>> wrenches =
        chain.solver->solve(chain.rne.state, qdQ, qddQ);  // DOF+1 项（base 系）
    for (std::size_t i = 0; i < n; ++i) {
        const rw::math::Vector3D<double> z = rw::kinematics::Kinematics::frameTframe(
            chain.rne.device->getBase(), chain.rne.joints[i], chain.rne.state).R().getCol(2);
        if (chain.rne.prismatic[i]) {
            tau[i] = rw::math::dot(wrenches[i].force(), z);   // 移动：沿轴力分量（N）
        } else {
            tau[i] = rw::math::dot(wrenches[i].torque(), z);  // 旋转/连续：绕轴矩分量（N·m）
        }
    }
}

}  // namespace

// =====================================================================
// 评估主入口（公共头 InverseDynamicsEvaluator::evaluate 的实现）。
// =====================================================================

InverseDynOutcome InverseDynamicsEvaluator::evaluate(const InverseDynRequest& request,
                                                     evidence::IEvaluationContext& context) const
{
    // ---- 第 0 步：请求级前置校验（§10.1 前置行——调用方错误 fail-fast，
    //      不产出半成品；详见 Errors.hpp 触发面清单）----
    if (request.model == nullptr) {
        throw DynamicsError("input-invalid", "模型指针为空——拒绝评估");
    }
    if (!request.conditionId.isValid()) {
        throw DynamicsError("input-invalid", "工况对象 ID 为空——工况集空（§8.1 调用侧形态）");
    }
    if (!std::isfinite(request.gravityBase[0]) || !std::isfinite(request.gravityBase[1])
        || !std::isfinite(request.gravityBase[2])) {
        // 重力非有限＝上游投影失效（调用方传入面）——fail-fast（NFR-COR-03）。
        throw DynamicsError("input-invalid", "基座系重力加速度含非有限分量——拒绝评估");
    }
    if (request.samples.empty()) {
        throw DynamicsError("input-invalid", "轨迹激励样本为空——无评估对象");
    }
    // 负载池引用完整性：事件指向的负载下标必须存在（调用方组装错误）。
    for (std::size_t e = 0; e < request.events.size(); ++e) {
        if (request.events[e].payloadIndex >= request.payloads.size()) {
            throw DynamicsError("input-invalid",
                                "负载事件引用越界（事件序 " + std::to_string(e) + "，payloadIndex="
                                    + std::to_string(request.events[e].payloadIndex) + "，池大小="
                                    + std::to_string(request.payloads.size()) + "）");
        }
        if (!std::isfinite(request.events[e].tEvent)) {
            throw DynamicsError("input-invalid",
                                "负载事件时刻非有限（事件序 " + std::to_string(e) + "）");
        }
    }
    for (const EndEffectorPayload& p : request.payloads) {
        if (!p.objectId.isValid()) {
            throw DynamicsError("input-invalid", "负载对象 ID 为空——素材不可定位");
        }
    }

    // ---- 第 1 步：RNE 消费面构建（每工况一次；设备/物性/摩擦/工具面）----
    RneConsumptionChain chain = buildRneConsumptionChain(*request.model);
    const std::size_t n = chain.meta.size();

    // 激励维度与时间轴（§5.6 输入维度不匹配/时间非单调——评估终止语义，
    // 本实现取 fail-fast 异常轨并携带比较数据；稳定码不随异常发布——§10.0）。
    for (std::size_t k = 0; k < request.samples.size(); ++k) {
        const InverseDynSampleInput& s = request.samples[k];
        if (s.q.size() != n || s.qd.size() != n || s.qdd.size() != n) {
            throw DynamicsError("input-invalid",
                                "轨迹激励维度与模型可动关节数不匹配（样本序 " + std::to_string(k)
                                    + "：实际 " + std::to_string(s.q.size()) + "，期望 "
                                    + std::to_string(n) + "）——DYN-DIMENSION-MISMATCH 语义");
        }
        if (!std::isfinite(s.t)) {
            throw DynamicsError("input-invalid", "样本时刻非有限（样本序 " + std::to_string(k) + "）");
        }
        if (k > 0 && !(s.t > request.samples[k - 1].t)) {
            // 严格递增：重复/倒退/零间隔一律拒绝——评估器不排序修复、不插值
            // 抹平（§4.6；DYN-SERIES-NON-MONOTONIC 语义）。
            throw DynamicsError("input-invalid",
                                "轨迹时间非严格递增（样本序 " + std::to_string(k) + "：t="
                                    + std::to_string(s.t) + "，前一 t="
                                    + std::to_string(request.samples[k - 1].t)
                                    + "）——DYN-SERIES-NON-MONOTONIC 语义");
        }
    }

    // ---- 第 2 步：负载池提取（工况面物性——法兰系静参数；每评估一次）----
    std::vector<EndComponent> payloadComponents;
    payloadComponents.reserve(request.payloads.size());
    for (const EndEffectorPayload& p : request.payloads) {
        bool estimated = false;
        payloadComponents.push_back(extractEndComponent(
            p.mass, p.centerOfMass, p.inertia, chain.tcpPosInFlange, chain.tcpRotToFlange,
            p.objectId, "负载" + std::to_string(payloadComponents.size()), "负载", estimated));
        if (estimated) { ++chain.estimatedPayloadCount; }
    }

    // ---- 第 3 步：诊断素材预装配（提取阶段事实——ERR-01 字段齐备）----
    InverseDynOutcome out;
    out.conditionId = request.conditionId;
    auto addDiag = [&out](const std::string& code, const core::ObjectId& subject,
                          const std::string& localName, const std::string& cause) {
        // 稳定码值取自 DiagCodes.hpp 常量（唯一书写点——禁字符串拼码）；
        // context/cause/recommendedAction 必填非空（core C-3 校验强制）。
        out.diagnostics.push_back(core::DiagnosticRecord::make(
            code, subject, localName, std::string{}, std::string{"dynamics 逆动力学评估"},
            cause, std::string{"建模侧补全参数后重编译并复算动力学"}));
    };
    // 摩擦缺失（MDL-16 M-8 闭环→DYN-06 降级素材——§5.5 第 1 层）：逐关节
    // 一条，cause 列缺失分量（数值已按 0 计入，评估继续）。
    for (const JointMeta& j : chain.meta) {
        std::string missing;
        if (!j.friction.viscousPresent) { missing += "fv(黏性) "; }
        if (!j.friction.coulombPresent) { missing += "fc(库仑) "; }
        if (!j.friction.biasPresent) { missing += "bias(偏置) "; }
        if (!missing.empty()) {
            addDiag(std::string{kDynFrictionMissing}, j.objectId, j.localName,
                    "关节摩擦参数缺失（MDL-16）：" + missing + "——分项按 0 计入，"
                    "证据降级 DataInsufficient（DYN-06），不包装精确结论");
        }
    }
    // 物性估算降级（D-DYN-6——§5.5 第 2 层）：连杆估算与末端件估算分条
    // 素材（subject 可定位；估算值不自动升级为精确证据）。
    {
        const runtime::RobotChain& rc = request.model->chain();
        for (std::size_t i = 0; i < rc.links.size(); ++i) {
            const runtime::CanonicalLink& link = rc.links[i];
            if (i == 0) { continue; }  // 基座连杆不入递推（文件头体号约定）
            const bool est = (link.mass.tryValue().has_value()
                                 && link.mass.provenance().kind == core::ProvenanceKind::GeometricEstimate)
                || (link.centerOfMass.tryValue().has_value()
                                 && link.centerOfMass.provenance().kind == core::ProvenanceKind::GeometricEstimate)
                || (link.inertia.tryValue().has_value()
                                 && link.inertia.provenance().kind == core::ProvenanceKind::GeometricEstimate);
            if (est) {
                addDiag(std::string{kDynPropertyDowngraded}, link.objectId, link.localName,
                        "连杆物性来源＝几何估算（MDL-05）——低估惯性提示，证据降级（DYN-06）");
            }
        }
    }
    if (chain.tool.has_value() && chain.tool->estimated) {
        addDiag(std::string{kDynPropertyDowngraded}, chain.tool->objectId, chain.tool->localName,
                "工具物性含保守估算（com 缺失→取 TCP／惯量缺失→点质量，或 GeometricEstimate 来源）"
                "——低估惯性提示，证据降级（DYN-06）");
    }
    for (const EndComponent& p : payloadComponents) {
        if (p.estimated) {
            addDiag(std::string{kDynPropertyDowngraded}, p.objectId, p.localName,
                    "负载物性含保守估算（com 缺失→取安装点／惯量缺失→点质量，D-DYN-6）"
                    "——低估惯性提示，证据降级（DYN-06），不包装精确结论");
        }
    }

    // ---- 第 4 步：事件时间线排序（§5.4——确定性序：tEvent 升序→负载
    //      下标升序；稳定排序保同刻度先后可复现，NFR-COR-02）----
    std::vector<PayloadEvent> events = request.events;
    std::stable_sort(events.begin(), events.end(),
                     [](const PayloadEvent& a, const PayloadEvent& b) {
                         if (a.tEvent != b.tEvent) { return a.tEvent < b.tEvent; }
                         return a.payloadIndex < b.payloadIndex;
                     });

    // 初始挂载集（§5.4"初始变体＝工具＋初始负载"——initiallyMounted 语义）。
    std::vector<char> mounted(request.payloads.size(), 0);
    for (std::size_t k = 0; k < request.payloads.size(); ++k) {
        mounted[k] = request.payloads[k].initiallyMounted ? 1 : 0;
    }
    auto remount = [&chain, &payloadComponents, &mounted]() {
        std::vector<EndComponent> live;
        for (std::size_t k = 0; k < mounted.size(); ++k) {
            if (mounted[k]) { live.push_back(payloadComponents[k]); }
        }
        composeFlangeVariant(chain, live);  // 挂载集变化→重合成末端体（O(组件数)）
    };
    remount();  // 基线变体（变体索引 0）合成

    // ---- 第 5 步：样本主循环（§5.2 分项图逐样本执行；取消逐样本轮询）----
    const std::size_t planned = request.samples.size();
    out.validity.plannedSampleCount = planned;
    out.samples.reserve(planned * n);

    std::vector<double> tauTotal(n, 0.0), tauGrav(n, 0.0), tauInertia(n, 0.0), tauCori(n, 0.0);
    std::vector<double> qdEff(n, 0.0), qddEff(n, 0.0);
    std::vector<double> prevPower(n, 0.0);   ///< 上一时刻同关节功率（W——梯形积分用）
    std::vector<double> energy(n, 0.0);      ///< 能量积分状态（J；逐关节独立累积）
    std::vector<char> energyValid(n, 1);     ///< 能量链有效位（非有限故障后置 0——不再累积）
    bool havePrev = false;                   ///< 是否已有前一时刻（首样本 E=0 起点）
    double prevT = 0.0;                      ///< 上一时刻（s）
    std::size_t actualSamples = 0;           ///< 实际产出样本时刻数
    std::size_t nonFiniteSamples = 0;        ///< 非有限输入样本时刻数
    bool hadFailure = false;                 ///< 本工况发生 RNEA 数值失败（§5.6 失败记录）
    std::uint32_t variantIndex = 0;          ///< 当前负载变体索引（0=基线）
    std::size_t eventCursor = 0;             ///< 事件时间线游标（单调推进）
    const double kNaN = std::numeric_limits<double>::quiet_NaN();

    for (std::size_t k = 0; k < planned; ++k) {
        // 取消轮询（§9.3"长评估必须周期性查询"——每样本一次；取消＝非错误，
        // UX-03：返回部分产出＋cancelled 位，零错误诊断）。
        if (context.cancellationRequested()) {
            out.cancelled = true;
            break;
        }
        context.reportProgress(
            static_cast<std::uint8_t>(std::min<std::size_t>(100, (k * 100) / std::max<std::size_t>(planned, 1))),
            std::string_view{"dyn-rnea-sample"});

        const InverseDynSampleInput& s = request.samples[k];
        const double t = s.t;

        // 事件推进（§5.4 保守边界：样本时刻 t ≥ tEvent 即生效——事件落在
        // 采样间隙时不晚于其后首样本；恰落在样本上时该样本即生效）。
        bool variantChanged = false;
        while (eventCursor < events.size() && events[eventCursor].tEvent <= t) {
            const PayloadEvent& ev = events[eventCursor];
            if (ev.kind == PayloadEvent::Kind::Grasp) {
                mounted[ev.payloadIndex] = 1;   // 夹取＝并入末端
            } else {
                mounted[ev.payloadIndex] = 0;   // 释放＝移除
            }
            ++variantIndex;                     // 每事件切换产生新变体索引
            variantChanged = true;
            ++eventCursor;
        }
        if (variantChanged) { remount(); }

        // 样本级输入有限性预检（§4.6"输入非有限→该样本 NonFiniteInput"）。
        // 样本级处置的原因：动力学方程 M/C/G 含关节间耦合项，任一关节的
        // 非有限输入经耦合传播必然污染全部关节输出——逐关节分离评估无
        // 意义，整样本标记才与"非有限不可静默传播"（NFR-COR-03）一致。
        // 处置：全部关节行标 NonFiniteInput（力矩域 NaN 不转 0）、能量链
        // 显式断裂、评估继续下一样本（样本级素材不阻断序列——§4.6）。
        bool sampleNonFiniteInput = false;
        std::size_t firstNonFiniteJoint = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(s.q[i]) || !std::isfinite(s.qd[i])
                || !std::isfinite(s.qdd[i])) {
                sampleNonFiniteInput = true;
                firstNonFiniteJoint = i;
                break;
            }
        }
        if (sampleNonFiniteInput) {
            for (std::size_t i = 0; i < n; ++i) {
                const JointMeta& j = chain.meta[i];
                DynamicsSample row;
                row.t = t;
                row.segmentIndex = s.segmentIndex;
                row.conditionId = request.conditionId;
                row.jointIndex = static_cast<std::uint32_t>(i);
                row.jointObjectId = j.objectId;
                row.jointType = j.type;
                row.q = s.q[i];
                row.qd = s.qd[i];
                row.qdd = s.qdd[i];
                row.tauGravity = kNaN;             // 非有限输入行：力矩域无意义＝NaN（不转 0）
                row.tauInertia = kNaN;
                row.tauCoriolisCentrifugal = kNaN;
                row.tauFriction = kNaN;
                row.tauExternal = 0.0;             // 外力项恒 0（NotApplicable——结构性事实）
                row.tauTotal = kNaN;
                row.mechanicalPower = kNaN;
                energyValid[i] = 0;                // 能量链断裂——能量积分显式无效
                row.energyIntegralJ = kNaN;
                row.payloadVariantIndex = variantIndex;
                row.toolObjectId = chain.toolObjectId;
                row.numericState = SampleNumericState::NonFiniteInput;
                out.samples.push_back(row);
            }
            // 每样本一条非有限素材（subject＝首个非有限关节——定位语义，
            // cause 注明耦合传播范围＝整样本）。
            addDiag(std::string{kDynNonFinite},
                    chain.meta[firstNonFiniteJoint].objectId,
                    chain.meta[firstNonFiniteJoint].localName,
                    "样本输入含非有限值（t=" + std::to_string(t) + "，段 "
                        + std::to_string(s.segmentIndex) + "，关节序 "
                        + std::to_string(firstNonFiniteJoint)
                        + "）——耦合项传播使整样本标记 NonFiniteInput，不静默转 0"
                        "（NFR-COR-03），评估继续");
            ++nonFiniteSamples;
            ++actualSamples;
            havePrev = true;   // 时间轴推进（能量链已断裂——dt 不再参与有效累积）
            prevT = t;
            continue;
        }

        // 样本权威位置入消费面（四通道共享同一 State 位形——q_rw 换算在
        // solveChannel 内单点执行；此处置于通道循环外避免重复换算语义分叉）。
        {
            rw::math::Q qRw(static_cast<int>(n));
            for (std::size_t i = 0; i < n; ++i) {
                qRw[static_cast<int>(i)] = s.q[i] - chain.meta[i].zeroOffset;
            }
            chain.rne.device->setQ(qRw, chain.rne.state);
        }

        // ---- 四通道 RNEA（§5.2 分项拆分口径；每样本各调一次上游 solve，
        //      关节循环只做投影组装——上游 solve 每次完整走外向/内向递推）----
        // 全量：τ(g, q̇, q̈)。
        solveChannel(chain, request.gravityBase, s.qd, s.qdd, tauTotal);
        // 重力通道：q̇=0、q̈=0（τ_gravity＝G(q)——静态重力矩通道）。
        std::fill(qdEff.begin(), qdEff.end(), 0.0);
        std::fill(qddEff.begin(), qddEff.end(), 0.0);
        solveChannel(chain, request.gravityBase, qdEff, qddEff, tauGrav);
        // 惯性通道：g=0、q̇=0（τ_inertia＝M(q)·q̈——含全耦合质量阵）。
        const double zeroG[3] = {0.0, 0.0, 0.0};
        std::fill(qdEff.begin(), qdEff.end(), 0.0);
        solveChannel(chain, zeroG, qdEff, s.qdd, tauInertia);
        // 科氏/离心通道：g=0、q̈=0（τ_coriolis＝C(q,q̇)·q̇——含交叉耦合）。
        solveChannel(chain, zeroG, s.qd, qddEff, tauCori);

        // 输出有限性（§5.6：RNEA 数值失败→该样本起本工况失败记录，后续
        // 样本不产出；单工况失败不自动推出其他工况失败）。
        bool sampleFailed = false;
        for (std::size_t c = 0; c < n; ++c) {
            if (!(std::isfinite(tauTotal[c]) && std::isfinite(tauGrav[c])
                  && std::isfinite(tauInertia[c]) && std::isfinite(tauCori[c]))) {
                const JointMeta& j = chain.meta[c];
                DynamicsSample row;
                row.t = t;
                row.segmentIndex = s.segmentIndex;
                row.conditionId = request.conditionId;
                row.jointIndex = static_cast<std::uint32_t>(c);
                row.jointObjectId = j.objectId;
                row.jointType = j.type;
                row.q = s.q[c];
                row.qd = s.qd[c];
                row.qdd = s.qdd[c];
                row.tauGravity = kNaN;
                row.tauInertia = kNaN;
                row.tauCoriolisCentrifugal = kNaN;
                row.tauFriction = kNaN;
                row.tauExternal = 0.0;
                row.tauTotal = kNaN;
                row.mechanicalPower = kNaN;
                energyValid[c] = 0;
                row.energyIntegralJ = kNaN;
                row.payloadVariantIndex = variantIndex;
                row.toolObjectId = chain.toolObjectId;
                row.numericState = SampleNumericState::NonFiniteOutput;
                out.samples.push_back(row);
                addDiag(std::string{kDynRneaFailed}, j.objectId, j.localName,
                        "RNEA 计算失败（输出非有限）：t=" + std::to_string(t) + "，段 "
                            + std::to_string(s.segmentIndex) + "，关节序 " + std::to_string(c)
                            + "——本工况自该样本起失败记录，其余工况不受影响（§5.6）");
                sampleFailed = true;
            }
        }
        if (sampleFailed) {
            ++actualSamples;   // 失败样本行已产出（保留至故障点——不截断伪造）
            havePrev = true;
            prevT = t;
            hadFailure = true;
            break;             // §5.6：该样本起本工况失败
        }

        // 功率/能量（§7.5 梯形时间加权——对采样网格；时间基准＝上游轨迹轴）。
        const double dt = havePrev ? (t - prevT) : 0.0;

        for (std::size_t i = 0; i < n; ++i) {
            const JointMeta& j = chain.meta[i];
            const double qi = s.q[i];
            const double qdi = s.qd[i];
            const double qddi = s.qdd[i];

            // ---- 摩擦叠加与总力矩（§5.2 恒等式口径：tauTotal＝全量 RNEA
            //      ＋摩擦项；五分项之和的恒等性由黄金算例校验——§5.2"黄金
            //      算例校验分项可加性"）----
            const double fric = frictionTerm(j.friction, qdi);       // N·m 或 N
            const double totalWithFriction = tauTotal[i] + fric;     // N·m 或 N
            const double power = totalWithFriction * qdi;            // P=τ_total·q̇（W；§7.5）
            if (energyValid[i] != 0) {
                // 梯形累积：E ← E ＋ ½(P_prev＋P_cur)·Δt（时间加权——§7.5；
                // 首样本 E=0 起点）。
                if (havePrev) {
                    energy[i] += 0.5 * (prevPower[i] + power) * dt;
                }
                prevPower[i] = power;
            }

            DynamicsSample row;
            row.t = t;
            row.segmentIndex = s.segmentIndex;
            row.conditionId = request.conditionId;
            row.jointIndex = static_cast<std::uint32_t>(i);
            row.jointObjectId = j.objectId;
            row.jointType = j.type;
            row.q = qi;                            // 权威角（rad 或 m——输入原值回记）
            row.qd = qdi;
            row.qdd = qddi;
            row.tauGravity = tauGrav[i];
            row.tauInertia = tauInertia[i];
            row.tauCoriolisCentrifugal = tauCori[i];
            row.tauFriction = fric;
            row.tauExternal = 0.0;                 // R1 恒 0（NotApplicable——P-DYN-3）
            row.tauTotal = totalWithFriction;      // §5.2 恒等式：五分项之和的承载值
            row.mechanicalPower = power;
            row.energyIntegralJ = energyValid[i] != 0 ? energy[i] : kNaN;
            row.payloadVariantIndex = variantIndex;
            row.toolObjectId = chain.toolObjectId;
            row.numericState = SampleNumericState::Ok;
            out.samples.push_back(row);
        }

        // 样本级计数推进（正常样本——全部关节行 Ok 已产出）。
        ++actualSamples;
        havePrev = true;
        prevT = t;
    }

    // ---- 第 6 步：有效性摘要（§4.4 DynamicsValidity——结果侧事实记录）----
    if (out.samples.empty()) {
        out.validity.completeness = DynamicsValidity::Completeness::Empty;
    } else if (actualSamples == planned && nonFiniteSamples == 0 && !out.cancelled
               && !hadFailure) {
        out.validity.completeness = DynamicsValidity::Completeness::Complete;
    } else {
        out.validity.completeness = DynamicsValidity::Completeness::Partial;
    }
    out.validity.actualSampleCount = actualSamples;
    out.validity.nonFiniteCount = nonFiniteSamples;
    out.validity.estimatedLinkCount = chain.estimatedLinkCount;
    out.validity.estimatedPayloadCount = chain.estimatedPayloadCount;
    out.validity.frictionMissing = chain.frictionMissing;
    // externalValidationPending：CON-03 Recorded 态事实在本任务输入面无通
    // 道（物性 SourcedValue 来源五类无外部验证态）——恒 false，字段语义
    // 保留（登记于单元卡 §1.2 实现口径）。
    out.validity.externalValidationPending = false;
    out.validity.forwardCheck = DynamicsValidity::ForwardCheckState::NotRun;  // 检查器随 T05
    return out;
}

}  // namespace sdurws::ird::dynamics
