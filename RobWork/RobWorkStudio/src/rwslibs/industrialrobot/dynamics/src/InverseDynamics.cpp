/**
 * @file   InverseDynamics.cpp
 * @brief  RNEA 逆动力学评估器实现（units/dynamics.md §5）——模型提取、
 *         末端体合成、几何/力学双递推、摩擦叠加与样本行组装。
 *
 * 设计依据（契约面见同名公共头 InverseDynamics.hpp 文件头）：
 *   - units/dynamics.md §5.1（输入取自编译产物 CanonicalModel——不重编译、
 *     不自建第二套关节链）、§5.2（分项图：外向/内向递推＋重力折叠＋摩擦
 *     叠加＋外力恒 0）、§5.3（输入覆盖清单）、§5.4（负载事件时间线与模型
 *     变体——保守边界规则）、§5.5（摩擦 sgn₀(0)=0 与 DYN-06 三层降级）、
 *     §5.6（数值稳定性、失败语义与耦合链防御）、§10.0（通用约定）
 *   - 需求 DYN-01/02、MDL-16（摩擦输入链）、MDL-22（重力投影单一消费）、
 *     DYN-04（关节侧结果与候选传动无关）、NFR-COR-02/03
 *   - 任务契约 tasks/foundation/WP-17-T03.json
 *
 * 实现要点（为什么全部自持 Vec3/Mat3 逐元素算术）：
 *   冒烟模式（DTB §5.1 双模式之一）对 rw 只提供模板头 header-only 可达、
 *   不链接框架库——Rotation3D::operator*、Transform3D::operator*、
 *   InertiaMatrix 的运算符等均为框架 .cpp 外联符号，引用即未解析（runtime
 *   RT-T03 先例纪律）。故 rw::math 值类型只出现在"编译产物→内部表示"的
 *   提取边界（逐元素读取 Vector3D[i]/Rotation3D(i,j)/InertiaMatrix(i,j)，
 *   与 runtime BaseWorldTransform.cpp 同款已被冒烟验证的用法集），RNEA
 *   全部矩阵/向量运算以自持 3 维向量/3×3 矩阵的 double 逐元素算术完成。
 *   该纪律同时服务于确定性（NFR-COR-02）：同输入在同二进制内逐位同输出。
 *
 * 算法总览（与单元卡 §5.2 分项图逐步对应，符号约定全链统一）：
 *   - 坐标系：全部递推在**基座系**进行（连杆 0 系＝基座系）；重力已是基
 *     座系投影（调用方自快照 gravityBase() 取——本域零二次旋转，D-DYN-3；
 *     接口参数面只存在 g_base 向量、不存在 R_world_base——二次旋转在结构
 *     上不可表达，AT-37/RT-BW-4 同类缺陷的接口级拦截）；
 *   - 关节角：输入轨迹为权威角 q_auth（§4.5），关节实际旋转角＝
 *     q_rw = q_auth − zeroOffset（runtime 口径 q_authoritative = q_zeroOffset
 *     ＋ q_rw 的反解——变换链用 q_rw，输出样本行回记 q_auth）；
 *   - 体号约定：力学体 j＝规范连杆 links[j]（j=1..n，随关节 j-1 运动；
 *     links[0] 为基座连杆，固连基座、其物性不产生任何关节力矩故不入递推
 *     ——链约定"下标 i 的连杆是关节 i 的父体"，units/runtime.md §4.3.3）；
 *     工具与已挂负载按 §5.4 合成为法兰体（links[n]）参数；
 *   - 重力折叠：基座原点线加速度取 a_p0 = −g_base（等效"基座反向加速"，
 *     重力以惯性力形式进入全链——Craig 递归牛顿—欧拉标准做法，应用
 *     恰好一次，禁止任何下游二次旋转）；
 *   - 分项拆分：τ_total/τ_gravity/τ_inertia/τ_coriolis 四通道＝同一几何
 *     递推结果下的四组 (g, q̇, q̈) 输入组合（动力学方程 M(q)q̈＋C(q,q̇)q̇
 *     ＋G(q) 对 q̈ 线性、对 q̇ 二次齐次、重力线性，无交叉项——三通道之和
 *     恒等于全量通道，黄金算例校验该恒等式）；
 *   - 摩擦叠加：τ_fric,i = fv_i·q̇_i + fc_i·sgn₀(q̇_i) + bias_i（§5.5，
 *     sgn₀(0)=0——零速含驻留库仑项为零，静摩擦不在 R1 模型）；
 *   - 外力项：内向递推末端初值 f/n 恒 0（ExternalWrench R1 NotApplicable
 *     ——P-DYN-3，不伪造零以外的值）。
 */

#include <sdurws/ird/dynamics/InverseDynamics.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp>  // DYN-* 码值常量（唯一书写点——产码共用）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 自持三维向量/3×3 矩阵（行主序）——RNEA 内核的纯数值载体。
// 全部运算逐元素完成：零 rw 外联符号（文件头实现要点）、零动态分配、
// 确定性循环序（NFR-COR-02）。
// =====================================================================

/// 三维向量（物理单位随使用处注明——m、m/s、m/s²、N、N·m、rad/s 等）。
struct Vec3 {
    double x = 0.0; ///< 分量 0（基座系 X）
    double y = 0.0; ///< 分量 1（基座系 Y）
    double z = 0.0; ///< 分量 2（基座系 Z）
};

/// 3×3 矩阵（行主序 m[row*3+col]；用于旋转与惯性张量——单位随使用处）。
struct Mat3 {
    double m[9] = {1.0, 0.0, 0.0,
                   0.0, 1.0, 0.0,
                   0.0, 0.0, 1.0}; ///< 默认单位阵（旋转的恒元）

    /// 逐元素读取（row/col 均 0 基）。
    double at(int row, int col) const { return m[row * 3 + col]; }
    /// 逐元素写入。
    double& at(int row, int col) { return m[row * 3 + col]; }
};

/// 向量加法（同单位量——调用方保证量纲一致）。
Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
/// 向量减法。
Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
/// 标量乘（k 的单位使结果量纲正确——如质量 kg×加速度 m/s²）。
Vec3 operator*(double k, const Vec3& v) { return Vec3{k * v.x, k * v.y, k * v.z}; }
/// 点积（力·轴类运算——结果单位由乘数携带）。
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
/// 叉积 a×b（基座系右手系；力矩 r×F、速度矩 ω×r 均经此）。
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return Vec3{a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

/// 矩阵×向量（旋转/惯性张量作用——R·v、I·α）。
Vec3 mul(const Mat3& M, const Vec3& v)
{
    return Vec3{M.at(0, 0) * v.x + M.at(0, 1) * v.y + M.at(0, 2) * v.z,
                M.at(1, 0) * v.x + M.at(1, 1) * v.y + M.at(1, 2) * v.z,
                M.at(2, 0) * v.x + M.at(2, 1) * v.y + M.at(2, 2) * v.z};
}

/// 矩阵乘 A·B（3×3 逐元素三重循环——确定性循环序）。
Mat3 mul(const Mat3& A, const Mat3& B)
{
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double s = 0.0;
            for (int k = 0; k < 3; ++k) {
                s += A.at(i, k) * B.at(k, j);
            }
            r.at(i, j) = s;
        }
    }
    return r;
}

/// 转置（旋转矩阵的逆＝转置——正交前提由提取层防御复检间接保证）。
Mat3 transpose(const Mat3& A)
{
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.at(i, j) = A.at(j, i);
        }
    }
    return r;
}

/**
 * @brief Rodrigues 轴角旋转矩阵 R＝I＋sinθ·[k]×＋(1−cosθ)·[k]×²。
 *
 * @param axis  [in] 旋转轴——**单位向量**（无量纲；CanonicalJoint.axis 已被
 *              编译链单位化，提取层防御复检 ‖axis‖≈1）
 * @param angle [in] 旋转角，单位 rad（不是度——权威角换算后的 q_rw）
 * @return 3×3 旋转矩阵（正交、det＝+1）
 */
Mat3 axisRotation(const Vec3& axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const double t = 1.0 - c;
    const double kx = axis.x, ky = axis.y, kz = axis.z;
    Mat3 r;
    // Rodrigues 闭式逐元素展开（无三角函数复合——同输入逐位同输出）。
    r.at(0, 0) = t * kx * kx + c;
    r.at(0, 1) = t * kx * ky - s * kz;
    r.at(0, 2) = t * kx * kz + s * ky;
    r.at(1, 0) = t * kx * ky + s * kz;
    r.at(1, 1) = t * ky * ky + c;
    r.at(1, 2) = t * ky * kz - s * kx;
    r.at(2, 0) = t * kx * kz - s * ky;
    r.at(2, 1) = t * ky * kz + s * kx;
    r.at(2, 2) = t * kz * kz + c;
    return r;
}

/// 全部分量有限（NFR-COR-03 非有限数拒绝的判定原语）。
bool allFinite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
/// 矩阵全部分量有限。
bool allFinite(const Mat3& M)
{
    for (int i = 0; i < 9; ++i) {
        if (!std::isfinite(M.m[i])) { return false; }
    }
    return true;
}

// =====================================================================
// 模型提取产物（内部表示——纯数值、不含 rw 类型）。
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
 * @brief 力学体参数（连杆 i＋1 或末端合成体——均在**自身体连杆系**表示）。
 *
 * 参考系约定（与 CanonicalLink/CanonicalTool 字段注释一致）：com 在体连
 * 杆系下表示（单位 m）；inertia 在质心系、体连杆系姿态下表示（单位
 * kg·m²；MDL-05 惯量基准）。末端合成体的"体连杆系"＝法兰系（links[n] 系）。
 */
struct RneaBody {
    double mass = 0.0;   ///< 质量，单位 kg（缺失按 0——§5.5 降级，证据不包装精确）
    Vec3 com{};          ///< 质心，体连杆系下表示，单位 m
    Mat3 inertia{};      ///< 惯量张量（质心系、体连杆系姿态），单位 kg·m²
};

/// 单关节提取参数（驱动几何＋所驱动体＋摩擦；下标 i＝关节序，0 基链序）。
struct RneaJoint {
    DynJointType type = DynJointType::Revolute; ///< 关节类型（决定量纲与递推分支）
    Mat3 originR{};      ///< origin 旋转部分：父连杆系→关节系（关节角 0 位），无量纲
    Vec3 originP{};      ///< origin 平移部分：父连杆系下表示，单位 m
    Vec3 axis{};         ///< 关节轴，关节系下**单位向量**（无量纲；编译链已规格化）
    double zeroOffset = 0.0; ///< 零位偏置，单位 rad（移动关节 m）；q_rw = q_auth − zeroOffset
    core::ObjectId objectId; ///< 关节稳定对象 ID（样本行/诊断定位——ARC-04）
    std::string localName;   ///< 关节权威局部名（诊断 localName 字段——MDL-14）
    RneaBody body;       ///< 随本关节运动的体（links[i+1]；末关节已含工具/负载合成）
    JointFriction friction; ///< 摩擦参数（MDL-16——四态已解析）
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
    Vec3 comF{};       ///< 质心，法兰系下表示，单位 m（缺失→安装点＝TCP，D-DYN-6）
    Mat3 inertiaC{};   ///< 惯量（质心系、法兰系姿态），单位 kg·m²（缺失→零张量＝点质量）
    bool estimated = false; ///< com/inertia 存在保守估算或 GeometricEstimate 来源（D-DYN-6）
    core::ObjectId objectId; ///< 组成件对象 ID（工具或负载——诊断定位）
    std::string localName;   ///< 件名（诊断 localName 字段）
};

/// 提取后的整链（RNEA 输入的内部形态——每工况构建一次，与样本数无关）。
struct RneaChain {
    std::vector<RneaJoint> joints;    ///< 关节链（0 基链序；非空——提取层保证）
    RneaBody flangeBase{};            ///< 法兰体（links[n]）的**未合成基线物性**——
                                      ///<  每次末端合成的起点（合成值只写回
                                      ///<  joints[n-1].body，基线不可被污染——变体
                                      ///<  反复合成时避免工具/负载重复累加）
    std::optional<EndComponent> tool; ///< 默认 TCP 工具件（无工具模型＝nullopt）
    Vec3 tcpPosInFlange{};            ///< TCP 安装点在法兰系的位置，单位 m（无工具＝法兰原点）
    Mat3 tcpRotToFlange{};            ///< TCP 系→法兰系旋转（负载惯量姿态旋转用）
    core::ObjectId toolObjectId;      ///< 工具对象 ID（样本行透传；无工具＝空 id）
    bool frictionMissing = false;     ///< 任一关节摩擦分量缺失（§5.5 第 1 层——MDL-16 闭环）
    std::size_t estimatedLinkCount = 0;   ///< 估算物性连杆数（连杆物性来源＝GeometricEstimate）
    std::size_t estimatedPayloadCount = 0; ///< 估算物性末端件数（工具/负载 com/inertia 保守估算）
};

// =====================================================================
// rw 值→内部表示的提取助手（逐元素读取——唯一允许的 rw 用法集，文件头）。
// =====================================================================

/// rw 三维向量 → 内部 Vec3（逐元素——单位随源字段）。
Vec3 toVec(const rw::math::Vector3D<double>& v) { return Vec3{v[0], v[1], v[2]}; }

/// rw 旋转 → 内部 Mat3（逐元素——无量纲正交阵）。
Mat3 toMat(const rw::math::Rotation3D<double>& R)
{
    Mat3 m;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m.at(i, j) = R(i, j);
        }
    }
    return m;
}

/// rw 惯量张量 → 内部 Mat3（逐元素——单位 kg·m²；不用其运算符/外联符号）。
Mat3 toMat(const rw::math::InertiaMatrix<double>& I)
{
    Mat3 m;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m.at(i, j) = I(i, j);
        }
    }
    return m;
}

// =====================================================================
// 物性防御性复检（§5.3 行 2——建模侧 MDL-06 断言前置，dynamics 对跨版本
// 快照的防御面）。
// =====================================================================

/**
 * @brief 物性复检：值有限＋惯量张量对称性**相对**容差 1×10⁻¹²。
 *
 * 对称性口径出处：需求附录 D 第 6 项原文「惯量张量对称性数值容差＝相对
 * 1×10⁻¹²」（单元卡 §6.4 两处同口径）。F-645 修正：原实现注释称"相对
 * 容差"实为绝对差 |I_ij−I_ji| > 1e-12 比较——绝对口径对大幅值张量过严
 * （100 kg·m² 级大惯量部件的 1e-11 双精度表示噪声会被误拒）、对微小
 * 张量过松（0.01 级连杆上 5e-13 偏差的相对比值已达 5×10⁻¹¹、超容差
 * 50 倍却放行），均偏离需求语义，本实现改为
 * ‖Δ‖_∞ / ‖(I+Iᵀ)/2‖_F ≤ tol 的真相对比较。
 *
 * 建模侧分工：SPD 完整复检不在此重复——建模侧 MDL-06 断言②已强制
 * （Provided 值非 SPD 在 CanonicalModelBuilder::build() 即拒绝），本域不
 * 实现第二套 SPD 判定（登记于单元卡 §1.2 实现口径）。注意建模侧
 * （runtime 单元 requireValidInertia）的对称性实现现为绝对差口径——与本域
 * 相对口径存在层面差异；该差异属 runtime 单元的登记事项，本批次任务面
 * 仅限 dynamics 单元，不代改他单元源码。
 *
 * @throws DynamicsError 含非有限分量/对称性偏差超相对容差（比较型
 *         detail——输入非法，调用方错误轨）
 */
void assertPropertiesFiniteAndSymmetric(const RneaBody& body, const std::string& subject)
{
    if (!std::isfinite(body.mass) || !allFinite(body.com) || !allFinite(body.inertia)) {
        throw DynamicsError("input-invalid", "物性防御复检失败（含非有限分量）：" + subject);
    }
    // 对称性相对容差（附录 D 第 6 项——无量纲比值上限；元素单位 kg·m²）。
    constexpr double kSymmetryRelTolerance = 1e-12;
    // 第一步：求参考模长＝对称化张量 (I+Iᵀ)/2 的 Frobenius 范数（单位
    // kg·m²）。分母取对称部分而非整张量：待检对象是反对称偏差，若整张量
    // 入模，偏差自身会放大分母（大幅值反对称分量稀释自身比值）——对称
    // 部分才是惯量的物理名义主体。
    double symNormSq = 0.0;  // ‖(I+Iᵀ)/2‖_F²，单位 (kg·m²)²
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double s = 0.5 * (body.inertia.at(i, j) + body.inertia.at(j, i));
            symNormSq += s * s;
        }
    }
    // 第二步：求最大反对称偏差 ‖Δ‖_∞＝max|I_ij−I_ji|（单位 kg·m²）。
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            worst = std::max(worst, std::abs(body.inertia.at(i, j) - body.inertia.at(j, i)));
        }
    }
    // 第三步：相对比较。退化防线：对称部分范数为 0（零张量/纯反对称注入）
    // 时相对比值无定义——退回绝对容差比较（零张量 worst=0 自然通过；纯
    // 反对称注入 worst>0 必拒），不做 0 除。
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
                                 const Vec3& tcpPosInFlange,
                                 const Mat3& tcpRotToFlange,
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
        c.comF = tcpPosInFlange + mul(tcpRotToFlange, toVec(*com));  // TCP 偏置→法兰系
        if (comF.provenance().kind == core::ProvenanceKind::GeometricEstimate) {
            c.estimated = true;  // 值已提供但来源＝几何估算（§5.5 第 2 层）
        }
    } else {
        c.comF = tcpPosInFlange;
        c.estimated = true;      // 缺失→保守估算（D-DYN-6 强制 estimated）
    }

    // 惯量：缺失→点质量模型（零转动惯量——"低估惯性"提示语义，D-DYN-6）；
    // 已提供→姿态旋转到法兰系（R·I·Rᵀ）并做有限性/对称性防御复检。
    if (const std::optional<rw::math::InertiaMatrix<double>> inertia = inertiaF.tryValue()) {
        const Mat3 iTcp = toMat(*inertia);  // 质心系、TCP 姿态（单位 kg·m²）
        c.inertiaC = mul(mul(tcpRotToFlange, iTcp), transpose(tcpRotToFlange));
        assertPropertiesFiniteAndSymmetric(RneaBody{c.mass, c.comF, iTcp},
                                           subjectLabel + " " + localName);
        if (inertiaF.provenance().kind == core::ProvenanceKind::GeometricEstimate) {
            c.estimated = true;
        }
    } else {
        // 点质量：对质心零转动惯量（D-DYN-6"低估惯性"——保守下界）。
        // ★ 显式置零（WP-17-T05 修复——同连杆缺失分支：Mat3 默认构造为
        //   单位阵〔旋转恒元语义〕，点质量路径若保留默认即"每个缺失惯量
        //   的负载自带 1 kg·m² 惯量"的静默错误值；缺失语义＝零贡献）。
        for (int r = 0; r < 3; ++r) {
            for (int cc = 0; cc < 3; ++cc) {
                c.inertiaC.at(r, cc) = 0.0;  // 零张量（点质量无转动惯量）
            }
        }
        c.estimated = true;
    }
    if (c.estimated) { outEstimated = true; }
    return c;
}

// =====================================================================
// 提取：CanonicalModel → RneaChain（§5.1——编译产物唯一读取点）。
// =====================================================================

/**
 * @brief 从 CanonicalModel 提取 RNEA 链（关节几何/驱动体/摩擦/工具面；
 *        负载池在 evaluate 内从请求提取——负载物性属于工况而非模型，
 *        §4.1 输入清单分层）。
 *
 * @throws DynamicsError 调用方错误：空链/Fixed 词表外关节/轴非单位/零位
 *         偏置非有限/物性复检失败/工具质量非法（逐项 detail 中文定位）
 */
RneaChain buildRneaChain(const runtime::CanonicalModel& model)
{
    const runtime::RobotChain& chain = model.chain();
    RneaChain out;

    // ---- 前置：链非空（§10.1 前置行——空链没有评估意义，fail-fast）----
    if (chain.joints.empty()) {
        throw DynamicsError("input-invalid", "模型链无可动关节（空链）——拒绝评估");
    }

    // 工具对象 ID（KIN-14 默认 TCP 项；无工具模型＝空 id——样本行如实透传）。
    if (model.defaultTcpIndex().has_value()) {
        out.toolObjectId = model.tools().at(*model.defaultTcpIndex()).objectId;
    }

    // ---- 逐关节提取（几何＋驱动体＋摩擦）----
    const std::size_t n = chain.joints.size();
    for (std::size_t i = 0; i < n; ++i) {
        const runtime::CanonicalJoint& cj = chain.joints.at(i);
        RneaJoint rj;
        rj.localName = cj.localName;
        switch (cj.type) {
            case runtime::JointType::Revolute:   rj.type = DynJointType::Revolute; break;
            case runtime::JointType::Continuous: rj.type = DynJointType::Continuous; break;
            case runtime::JointType::Prismatic:  rj.type = DynJointType::Prismatic; break;
            case runtime::JointType::Fixed:
                // 词表外类型（刚性连接、无自由度）——R1 评估面不支持（
                // DynJointType 词表三值），建模链不应产出此类链；fail-fast
                // 不静默跳过（§10.0 调用方错误轨）。
                throw DynamicsError("input-invalid",
                                    "模型链携带 Fixed 关节（词表外类型，关节序 " +
                                        std::to_string(i) + "）——R1 评估面仅支持三个可动类型");
        }

        // 轴单位性防御复检（编译链 build() 已规格化——跨版本快照防御）。
        const Vec3 axis = toVec(cj.axis);
        const double axisNorm = std::sqrt(dot(axis, axis));
        if (!std::isfinite(axisNorm) || std::abs(axisNorm - 1.0) > 1e-9) {
            throw DynamicsError("input-invalid",
                                "关节轴向非单位向量（‖axis‖=" + std::to_string(axisNorm) +
                                    "，关节序 " + std::to_string(i) + "）——编译产物应已规格化");
        }
        rj.originR = toMat(cj.origin.R());
        rj.originP = toVec(cj.origin.P());
        rj.axis = axis;
        rj.zeroOffset = cj.zeroOffset;  // 单位 rad（移动关节 m）——权威角换算分量
        if (!std::isfinite(rj.zeroOffset)) {
            throw DynamicsError("input-invalid",
                                "关节零位偏置非有限（关节序 " + std::to_string(i) + "）");
        }
        rj.objectId = cj.objectId;

        // ---- 驱动体＝links[i+1]（链约定：下标 i 的连杆是关节 i 的父体）----
        const runtime::CanonicalLink& link = chain.links.at(i + 1);
        RneaBody body;
        // 物性四态（§5.5 降级通道）：NotProvided 按 0/保守值计入数值（质量
        // →0、质心→体系原点、惯量→零张量）——数值继续，证据经素材降级，
        // 不包装精确；与"必填负载 mass 非法即阻止"的 MDL-06 分域语义不同
        // （连杆物性在建模侧本为可选能力，缺失属能力缺失而非调用方违约）。
        // ★ 惯量缺失分支必须显式置零（WP-17-T05 修复——T03 缺陷：RneaBody
        //   的 Mat3 默认构造为单位阵〔旋转恒元语义〕，缺失分支不赋值即保留
        //   单位阵＝"每个缺失连杆自带 1 kg·m² 惯量"的静默错误值；单位阵默
        //   认值只为旋转变量设计，物性字段缺失语义＝零贡献，NFR-COR-03
        //   "缺失≠伪造默认物理量"）。
        for (int r = 0; r < 3; ++r) {
            for (int cc = 0; cc < 3; ++cc) {
                body.inertia.at(r, cc) = 0.0;  // 缺失＝零张量（无惯量贡献）
            }
        }
        if (const std::optional<double> m = link.mass.tryValue()) {
            body.mass = *m;                                   // 单位 kg
        }
        if (const std::optional<rw::math::Vector3D<double>> c = link.centerOfMass.tryValue()) {
            body.com = toVec(*c);                             // 连杆系下，单位 m
        }
        if (const std::optional<rw::math::InertiaMatrix<double>> I = link.inertia.tryValue()) {
            body.inertia = toMat(*I);                         // 质心系，单位 kg·m²
        }
        assertPropertiesFiniteAndSymmetric(
            body, "连杆 " + link.localName + "（" + link.objectId.toCanonical() + "）");
        // 估算计数（§5.5 第 2 层）：连杆物性任一分量来源＝GeometricEstimate
        // （MDL-05 估算）——该连杆计 1（缺失走素材通道，不冒充估算计数）。
        const bool massEst = link.mass.tryValue().has_value()
            && link.mass.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        const bool comEst = link.centerOfMass.tryValue().has_value()
            && link.centerOfMass.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        const bool inertiaEst = link.inertia.tryValue().has_value()
            && link.inertia.provenance().kind == core::ProvenanceKind::GeometricEstimate;
        if (massEst || comEst || inertiaEst) { ++out.estimatedLinkCount; }
        rj.body = body;

        // ---- 摩擦三元组（MDL-16——四态独立解析，任一缺失即 frictionMissing）----
        // F-632 防线（提取源头拦截）：Provided 值逐分量 std::isfinite 复检。
        // 为什么在提取处拒绝而不是放行给下游：两类"坏数据"语义不同——
        //   ①分量**缺失**＝能力缺失，走 §5.5 第 1 层降级（数值按 0 继续＋
        //     frictionMissing 标记，语义保持不变，见下方缺失分支）；
        //   ②分量**已提供但非有限**（NaN/±Inf）＝数据非法，不是缺失——
        //     若放行进入模型，下游摩擦叠加（frictionTerm，样本行组装段）
        //     位于样本级非有限判定（NonFiniteInput 通道——只覆盖 q/q/qdd
        //     输入与 RNEA 四通道输出）之后，NaN 会先污染 τ_friction/τ_total
        //     再被检出，防线顺序存在缺口。
        // 处置面按同函数连杆物性先例（上方 assertPropertiesFiniteAndSymmetric
        // 的"防御复检失败"语义）：input-invalid fail-fast 异常轨（调用方
        // 错误），消息区分"已提供但非有限"，可定位到关节。
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
        rj.friction = f;

        out.joints.push_back(std::move(rj));
    }

    // 法兰体合成基线备份（末关节驱动体＝links[n] 的原始物性——composeEnd
    // Effector 的只读基项，防止变体反复合成时工具/负载重复累加）。
    out.flangeBase = out.joints.back().body;

    // ---- 末端工具面（§5.3"末端工具"行——默认 TCP 项）----
    // 法兰系＝links[n] 系（末关节驱动体）；TCP 几何来自工具 tcpOffset
    // （T_flange_tcp——KIN-14 权威）。无工具模型时 TCP＝法兰原点（恒等
    // 偏置）——负载仍可挂法兰原点，评估继续（工具 optional 语义）。
    if (model.defaultTcpIndex().has_value()) {
        const runtime::CanonicalTool& tool = model.tools().at(*model.defaultTcpIndex());
        out.tcpRotToFlange = toMat(tool.tcpOffset.R());
        out.tcpPosInFlange = toVec(tool.tcpOffset.P());
        bool toolEstimated = false;
        out.tool = extractEndComponent(tool.mass, tool.centerOfMass, tool.inertia,
                                       out.tcpPosInFlange, out.tcpRotToFlange, tool.objectId,
                                       tool.localName, "工具", toolEstimated);
        if (toolEstimated) { ++out.estimatedPayloadCount; }
    }
    return out;
}

// =====================================================================
// 末端体合成（§5.4——工具＋已挂负载并入法兰体；平行轴定理）。
// =====================================================================

/**
 * @brief 把工具与挂载负载合成进末关节驱动体（links[n]——法兰体）。
 *
 * 合成规则（全部在法兰系表示，单位：kg / m / kg·m²）：
 *   1. 总质量 M ＝ 法兰连杆基线质量 ＋ Σ 组件质量；
 *   2. 总质心 c^F ＝ Σ m_k·c_k^F / M（质量加权——一阶矩守恒）；
 *   3. 对总质心的合成惯量 ＝ Σ [I_k^F ＋ m_k·(|d_k|²E − d_k d_kᵀ)]
 *      （平行轴定理：I_k^F 为各体对自身质心、法兰系姿态的张量；d_k 为
 *      各体质心到总质心的位移向量，单位 m；E 为 3×3 单位阵）。
 *
 * ★ 合成基项＝chain.flangeBase（提取时的法兰连杆原始物性，只读基线）——
 *   合成结果只写回 joints[n-1].body；**绝不**以上次合成值为基项（否则
 *   变体每次切换会重复累加工具/负载——重复合成缺陷的防御点）。
 *
 * 边界：M ≤ 0（全部物性缺失的极端形态）——质心取法兰原点、惯量取零
 * （力矩贡献为零、数值安全），不抛异常（缺失已走 §5.5 降级素材通道，
 * 数值继续语义）。每变体重合成一次（挂载集变化时调用；O(组件数) 与
 * 样本数无关）。
 *
 * @param chain           [in,out] 已提取链（末关节体参数被合成值覆盖；
 *                        flangeBase 基线只读不动）
 * @param mountedPayloads [in] 当前挂载的负载组件（法兰系静参数）
 */
void composeEndEffector(RneaChain& chain, const std::vector<EndComponent>& mountedPayloads)
{
    // 组件清单＝工具（若有）＋挂载负载。
    std::vector<const EndComponent*> parts;
    if (chain.tool.has_value()) { parts.push_back(&*chain.tool); }
    for (const EndComponent& p : mountedPayloads) { parts.push_back(&p); }

    RneaJoint& last = chain.joints.back();  // 末关节驱动体＝法兰体（合成目标）
    const RneaBody& base = chain.flangeBase;  // 合成基项＝未合成基线（防重复累加）

    // 第一步：总质量与总质心（质量加权一阶矩；全在法兰系）。
    double totalMass = base.mass;
    Vec3 firstMoment = base.mass * base.com;  // 单位 kg·m（Σ m·c）
    for (const EndComponent* p : parts) {
        totalMass += p->mass;
        firstMoment = firstMoment + p->mass * p->comF;
    }
    RneaBody composed;
    composed.mass = totalMass;
    if (totalMass > 0.0) {
        composed.com = (1.0 / totalMass) * firstMoment;  // 质心（法兰系，m）
    } else {
        // 全零质量边界：质心无定义——取法兰原点（零力矩贡献、数值安全）。
        composed.com = Vec3{};
    }

    // 第二步：对总质心的合成惯量（平行轴定理逐项累加——确定性求和序）。
    // 先清零，再逐项累加"各体对自身质心的惯量＋平行轴修正项 m·(|d|²E −
    // d dᵀ)"——修正项的 (r,cc) 元＝δrc·|d|² − d_r·d_c（单位 kg·m²）。
    for (int r = 0; r < 3; ++r) {
        for (int cc = 0; cc < 3; ++cc) {
            composed.inertia.at(r, cc) = 0.0;
        }
    }
    // 基项（法兰连杆自身——对总质心平行轴）；totalMass==0 时全部项为零。
    auto addParallelAxis = [&composed](const RneaBody& b, const Vec3& comF, double mass) {
        const Vec3 d = comF - composed.com;  // 该体质心→总质心（法兰系，m）
        const double dd = dot(d, d);
        for (int r = 0; r < 3; ++r) {
            for (int cc = 0; cc < 3; ++cc) {
                const double kron = (r == cc) ? dd : 0.0;          // δij·|d|²
                const double dOuter = (cc == 0 ? d.x : (cc == 1 ? d.y : d.z))
                                    * (r == 0 ? d.x : (r == 1 ? d.y : d.z)); // d_r·d_c
                composed.inertia.at(r, cc) += b.inertia.at(r, cc) + mass * (kron - dOuter);
            }
        }
    };
    addParallelAxis(base, base.com, base.mass);
    for (const EndComponent* p : parts) {
        // 组件惯量已旋转到法兰系姿态（extractEndComponent 的 R·I·Rᵀ）。
        addParallelAxis(RneaBody{p->mass, p->comF, p->inertiaC}, p->comF, p->mass);
    }

    last.body = composed;  // 合成值覆盖末体参数（下一样本变体生效）
}

// =====================================================================
// 每样本几何递推（只依赖 q——四条力学通道共享一次计算）。
// =====================================================================

/**
 * @brief 单样本几何（基座系）——正向运动链的一次遍历产物。
 *
 * 参考系全部为基座系：linkOrigin[k] 为连杆 k 系原点位置（单位 m）；
 * jointOrigin[i]＝关节 i 原点位置（单位 m）；zAxis[i]＝关节 i 轴（基座系
 * 单位向量）；comOffset[i]＝**体 links[i+1] 原点→质心**的向量（单位
 * m，基座系——旋转关节体原点与关节原点重合故两说一致，移动关节二者
 * 相差滑移臂 d·z，消费方按参考点自行补正，见内向递推 F-580）；bodyR[i]
 * ＝体 links[i+1] 系→基座系旋转。
 * 值语义纯结构；每样本构建一次（栈上小对象——零堆分配预热面）。
 */
struct SampleGeometry {
    std::vector<Vec3> linkOrigin;   ///< 连杆 k 系原点（k=0..n；单位 m）
    std::vector<Vec3> jointOrigin;  ///< 关节 i 原点（i=0..n-1；单位 m）
    std::vector<Vec3> zAxis;        ///< 关节轴（基座系单位向量）
    std::vector<Vec3> comOffset;    ///< 体 links[i+1] 原点→质心（单位 m；
                                    ///< 旋转关节与"关节 i 原点→质心"重合，
                                    ///< 移动关节差滑移臂——消费方按参考点补正）
    std::vector<Mat3> bodyR;        ///< 体 links[i+1] 姿态（体系→基座系）
};

/**
 * @brief 正向运动链递推（权威角→基座系几何；§5.2 外向递推的几何段）。
 *
 * 逐步（关节 i，权威角 q_auth[i]，实际角 q_rw = q_auth − zeroOffset）：
 *   1. 关节系姿态 Rj ＝ R_link[i]·originR[i]（origin 为父连杆系→关节系）；
 *   2. 关节轴 z[i] ＝ Rj·axis[i]（关节系→基座系）；
 *   3. 关节 i 原点 ＝ 连杆 i 原点 ＋ R_link[i]·originP[i]（origin 平移固
 *      连于父连杆）；
 *   4. 体姿态/原点：旋转/连续关节体＝关节系经 R_axis(q_rw)（绕关节轴转
 *      q_rw）——体原点＝关节 i 原点；移动关节体姿态＝关节系（无旋转）、
 *      体原点＝关节 i 原点＋q_rw·z[i]（沿轴滑移量 q_rw，单位 m）；
 *   5. 质心偏移 rc[i] ＝ R_body[i]·com[i+1]（体连杆系质心→基座系）。
 *
 * 数值防御：q_auth 非有限由调用方逐关节预检（NonFiniteInput 通道）——
 * 本函数不复读。
 */
SampleGeometry computeGeometry(const RneaChain& chain, const std::vector<double>& qAuth)
{
    const std::size_t n = chain.joints.size();
    SampleGeometry g;
    g.linkOrigin.assign(n + 1, Vec3{});
    g.jointOrigin.assign(n, Vec3{});
    g.zAxis.assign(n, Vec3{});
    g.comOffset.assign(n, Vec3{});
    g.bodyR.assign(n, Mat3{});

    // 连杆 0（基座连杆）系＝基座系：原点在基座原点、姿态单位阵。
    Mat3 RLinkPrev = Mat3{};  // 连杆 i 系→基座系旋转
    Vec3 PLinkPrev = Vec3{};  // 连杆 i 系原点（基座系，m）
    for (std::size_t i = 0; i < n; ++i) {
        const RneaJoint& j = chain.joints[i];
        // 步骤 1＋2＋3：关节系位姿与轴向。
        const Mat3 RJoint = mul(RLinkPrev, j.originR);
        const Vec3 z = mul(RJoint, j.axis);
        const Vec3 pj = PLinkPrev + mul(RLinkPrev, j.originP);
        const double d = qAuth[i] - j.zeroOffset;  // q_rw（rad 或 m——权威角换算）
        g.jointOrigin[i] = pj;
        g.zAxis[i] = z;

        // 步骤 4：随动体位姿（旋转/连续 vs 移动——两条分支）。
        Mat3 RBody;
        Vec3 PBody;
        if (j.type == DynJointType::Prismatic) {
            RBody = RJoint;              // 移动关节体姿态＝关节系（无相对旋转）
            PBody = pj + d * z;          // 滑移量 d（单位 m）沿轴
        } else {
            RBody = mul(RJoint, axisRotation(j.axis, d));  // 绕轴转 d（rad）
            PBody = pj;                  // 旋转关节体原点＝关节 i 原点
        }
        // 步骤 5：质心偏移（体连杆系→基座系）。
        g.comOffset[i] = mul(RBody, j.body.com);
        g.bodyR[i] = RBody;
        g.linkOrigin[i] = PLinkPrev;
        g.linkOrigin[i + 1] = PBody;
        RLinkPrev = RBody;
        PLinkPrev = PBody;
    }
    return g;
}

// =====================================================================
// 力学递推（§5.2——外向/内向；g/q̇/q̈ 三组输入参数化，四通道共享几何）。
// =====================================================================

/**
 * @brief RNEA 力学递推：给定 (g_base, q̇, q̈) 产出一组关节广义力 τ[n]。
 *
 * 外向递推（i=0→n-1，逐步物理含义）：
 *   - 连杆 0（基座）固连基座：ω₀=α₀=0；重力折叠＝基座原点线加速度取
 *     a_p0 = −g_base（应用一次——文件头"重力折叠"）；
 *   - 关节 i 原点线加速度（父体连杆 i 携带）：a_p ＝ a_prev ＋ α_prev×r
 *     ＋ ω_prev×(ω_prev×r)，r＝连杆 i 原点→关节 i 原点（移动关节含当前
 *     滑移量）——刚体加速度两点公式；
 *   - 随动体（links[i+1]）角量：旋转/连续关节 ω＝ω_prev＋q̇·z、
 *     α＝α_prev＋q̈·z＋q̇·(ω_prev×z)（轴方向随父体转动产生附加项）；
 *     移动关节 ω/α 与父体相同（无相对转动）；
 *   - 移动关节原点附加项：2ω×(q̇·z)（科氏——滑移相对速度被父体旋转
 *     携带）＋q̈·z（滑移加速度）；
 *   - 质心加速度：a_c ＝ a_p ＋ α×r_c ＋ ω×(ω×r_c)（r_c＝关节原点→质心）；
 *   - 体合力/合力矩：F ＝ m·a_c（N）；N ＝ I_base·α ＋ ω×(I_base·ω)
 *     （N·m；I_base＝R·I_local·Rᵀ——体姿态旋转到基座系）。
 *
 * 内向递推（i=n-1→0）：末端内力/内矩初值＝0（ExternalWrench R1 恒
 * NotApplicable——P-DYN-3）；逐级 f ＝ f_next＋F、n ＝ n_next＋N＋r_c×F
 * ＋(关节 i+1 原点−关节 i 原点)×f_next（对关节 i 原点的合力矩——平移
 * 定理）；关节广义力 τ ＝ n·z（转动/连续，N·m）｜f·z（移动，N）。
 *
 * @param chain [in] 已提取链（末体已按当前变体合成）
 * @param geom  [in] 当前样本几何（computeGeometry 产物——与 (g,q̇,q̈) 无关）
 * @param gBase [in] 基座系重力加速度，单位 m/s²（长度 3）
 * @param qd    [in] 关节速度（rad/s 或 m/s；长度 n）
 * @param qdd   [in] 关节加速度（rad/s² 或 m/s²；长度 n）
 * @param tau   [out] 关节广义力（N·m 或 N，按关节类型；长度 n，调用方预分配）
 */
void rneaTorques(const RneaChain& chain, const SampleGeometry& geom,
                 const double gBase[3], const std::vector<double>& qd,
                 const std::vector<double>& qdd, std::vector<double>& tau)
{
    const std::size_t n = chain.joints.size();
    std::vector<Vec3> bodyForce(n);    ///< 各体合力 F（N；基座系）
    std::vector<Vec3> bodyTorque(n);   ///< 各体对质心的合力矩 N（N·m；基座系）

    // —— 外向递推：基座初始化（ω=α=0；重力折叠应用一次）——
    Vec3 wPrev = Vec3{};                        ///< 连杆 i 的角速度（rad/s；i=0 时为 0）
    Vec3 alPrev = Vec3{};                       ///< 连杆 i 的角加速度（rad/s²）
    Vec3 aOrgPrev = Vec3{-gBase[0], -gBase[1], -gBase[2]};  ///< 连杆 i 原点线加速度
                                                            ///< （m/s²；连杆 0＝−g 折叠）
    for (std::size_t i = 0; i < n; ++i) {
        const RneaJoint& j = chain.joints[i];
        const Vec3 z = geom.zAxis[i];
        const Vec3 rOff = geom.linkOrigin[i + 1] - geom.linkOrigin[i];  ///< 关节承载的
                                                             ///< 原点位移（m；移动关节含滑移）
        // 步骤 A：关节 i 原点线加速度（父体连杆 i 的刚体两点公式）。
        const Vec3 aPivot = aOrgPrev + cross(alPrev, rOff)
                          + cross(wPrev, cross(wPrev, rOff));
        // 步骤 B：随动体角量（旋转/连续 vs 移动两分支）。
        Vec3 wBody;
        Vec3 alBody;
        Vec3 aBodyOrigin;
        if (j.type == DynJointType::Prismatic) {
            wBody = wPrev;                       // 移动关节：无相对转动
            alBody = alPrev;
            // 滑移附加项：科氏 2ω×(q̇·z)（q̇ 单位 m/s——滑移相对速度被父体
            // 旋转携带）＋滑移加速度 q̈·z（q̈ 单位 m/s²）。
            aBodyOrigin = aPivot + 2.0 * cross(wPrev, qd[i] * z) + qdd[i] * z;
        } else {
            wBody = wPrev + qd[i] * z;           // 关节速度直接叠加到轴上
            alBody = alPrev + qdd[i] * z + cross(wPrev, qd[i] * z);  // 轴随父体转动
            aBodyOrigin = aPivot;                // 旋转关节体原点＝关节原点
        }
        // 步骤 C：质心加速度与合力/合力矩。
        // rc＝体原点→质心（基座系）——刚体加速度传输定理的自身体内向量，
        // 与关节类型无关（质心固连于体，体原点为传输基点）。
        const Vec3 rc = geom.comOffset[i];
        const Vec3 aCom = aBodyOrigin + cross(alBody, rc) + cross(wBody, cross(wBody, rc));
        const Mat3 R = geom.bodyR[i];
        const Mat3 IBase = mul(mul(R, j.body.inertia), transpose(R));  ///< 惯量→基座系
        bodyForce[i] = j.body.mass * aCom;       // F＝m·a_c（N）
        bodyTorque[i] = mul(IBase, alBody) + cross(wBody, mul(IBase, wBody));  // N（N·m）
        wPrev = wBody;
        alPrev = alBody;
        aOrgPrev = aBodyOrigin;
    }

    // —— 内向递推：末端初值＝0（外力项恒 0——P-DYN-3），逐级汇到关节 ——
    Vec3 fNext = Vec3{};  ///< 关节 i+1 处内力（N；末端＝ExternalWrench＝0）
    Vec3 nNext = Vec3{};  ///< 关节 i+1 处内矩（N·m）
    for (std::size_t ii = n; ii-- > 0;) {
        const std::size_t i = ii;
        // 质心力臂（关节 i 原点→体 i+1 质心，基座系）＝"体原点→质心"
        //（comOffset）＋"关节 i 原点→体原点"。旋转/连续关节体原点＝关节
        // 原点（第二项恰为零向量——类型统一写法）；**移动关节**体原点＝
        // 关节原点＋d·z（沿轴滑移），第二项即滑移臂。F-580（P1——audit/
        // unit-code-review-20261009）：原实现直接用 comOffset 当力臂，
        // 移动关节近端各转动关节丢失 (d·z)×F 贡献——静力反例：竖直滑移
        // d 的移动关节近端回转关节，重力矩差 m·g·d（外向/内向四通道共用
        // 同一错误力臂，五分项恒等式校验对此不可见，仅解析对照可暴露）。
        const Vec3 rc = geom.comOffset[i]
                      + (geom.linkOrigin[i + 1] - geom.jointOrigin[i]);
        // 平移定理：关节 i+1 内力对关节 i 原点取矩的力臂＝两关节原点差
        // （末端体侧 f_next＝0，力臂不参与——置零向量避免未定义读取）。
        const Vec3 arm = (i + 1 < n) ? (geom.jointOrigin[i + 1] - geom.jointOrigin[i])
                                     : Vec3{};
        const Vec3 f = fNext + bodyForce[i];
        const Vec3 nn = nNext + bodyTorque[i] + cross(rc, bodyForce[i]) + cross(arm, fNext);
        // 广义力：绕轴取矩（转动/连续，N·m）｜沿轴取力（移动，N）。
        if (chain.joints[i].type == DynJointType::Prismatic) {
            tau[i] = dot(f, geom.zAxis[i]);
        } else {
            tau[i] = dot(nn, geom.zAxis[i]);
        }
        fNext = f;
        nNext = nn;
    }
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

    // ---- 第 1 步：模型提取（每工况一次；关节/工具/摩擦/物性复检）----
    RneaChain chain = buildRneaChain(*request.model);
    const std::size_t n = chain.joints.size();

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
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(s.q[i]) || !std::isfinite(s.qd[i]) || !std::isfinite(s.qdd[i])) {
                // 非有限输入是样本级状态（NonFiniteInput——不在此终止），
                // 但必须有限性可判：NaN 通道在样本循环内逐行标记。
                continue;
            }
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
    for (const RneaJoint& j : chain.joints) {
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
    auto remount = [&]() {
        std::vector<EndComponent> live;
        for (std::size_t k = 0; k < mounted.size(); ++k) {
            if (mounted[k]) { live.push_back(payloadComponents[k]); }
        }
        composeEndEffector(chain, live);  // 挂载集变化→重合成末端体（O(组件数)）
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
                const RneaJoint& j = chain.joints[i];
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
                    chain.joints[firstNonFiniteJoint].objectId,
                    chain.joints[firstNonFiniteJoint].localName,
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

        // 共享几何（只依赖 q——四条力学通道一次计算复用）。
        const SampleGeometry geom = computeGeometry(chain, s.q);

        // ---- 四通道 RNEA（§5.2 分项拆分口径；每样本各调一次全链递推，
        //      关节循环只做行组装——避免逐关节重复全链递推的 O(n²) 浪费）----
        // 全量：τ(g, q̇, q̈)。
        rneaTorques(chain, geom, request.gravityBase, s.qd, s.qdd, tauTotal);
        // 重力通道：q̇=0、q̈=0（τ_gravity＝G(q)——静态重力矩通道）。
        std::fill(qdEff.begin(), qdEff.end(), 0.0);
        std::fill(qddEff.begin(), qddEff.end(), 0.0);
        rneaTorques(chain, geom, request.gravityBase, qdEff, qddEff, tauGrav);
        // 惯性通道：g=0、q̇=0（τ_inertia＝M(q)·q̈——含全耦合质量阵）。
        const double zeroG[3] = {0.0, 0.0, 0.0};
        std::fill(qdEff.begin(), qdEff.end(), 0.0);
        rneaTorques(chain, geom, zeroG, qdEff, s.qdd, tauInertia);
        // 科氏/离心通道：g=0、q̈=0（τ_coriolis＝C(q,q̇)·q̇——含交叉耦合）。
        rneaTorques(chain, geom, zeroG, s.qd, qddEff, tauCori);

        // 输出有限性（§5.6：RNEA 数值失败→该样本起本工况失败记录，后续
        // 样本不产出；单工况失败不自动推出其他工况失败）。
        bool sampleFailed = false;
        for (std::size_t c = 0; c < n; ++c) {
            if (!(std::isfinite(tauTotal[c]) && std::isfinite(tauGrav[c])
                  && std::isfinite(tauInertia[c]) && std::isfinite(tauCori[c]))) {
                const RneaJoint& j = chain.joints[c];
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
            const RneaJoint& j = chain.joints[i];
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
