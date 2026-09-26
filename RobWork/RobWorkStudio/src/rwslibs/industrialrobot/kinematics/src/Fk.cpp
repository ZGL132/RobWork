/**
 * @file   Fk.cpp
 * @brief  FK 与位姿指标实现（KIN-01）——链式正运动学、基础雅可比、
 *         Eigen SVD 指标链与 canonical 载荷编码。
 *
 * 设计依据：
 *   - units/kinematics.md §5.1/§5.2/§9.2（契约面，见 Fk.hpp 文件头）、
 *     §3.4（数值恒 SI、纯函数确定性）、D-KIN-2（统一尺度规则）、D-KIN-6
 *     （裕量归一化＝设计默认，随黄金数据集锁定）
 *   - 治理登记 O-40：Eigen（vcpkg 经典模式，eigen3 5.0.1）PRIVATE 仅本
 *     计算库实现文件——本翻译单元是全单元唯一 Eigen include 点
 *   - 任务契约 tasks/foundation/WP-15-T03.json acceptance 1/2/3/5
 *
 * 实现纪律（两模式可链接的浮点面约束）：
 *   - rw::math 值类型仅用**头内 inline 面**（元素访问 operator()、构造
 *     函数）——Transform3D 与 Rotation3D 的乘法运算符等 rw 外联
 *     符号在冒烟模式不可达（runtime/BaseWorldTransform.hpp 文件头同款
 *     登记纪律），旋转/复合一律在 Eigen 组件域完成，出界前一次性转回
 *     rw 值类型；
 *   - 基座—世界组合**只经** runtime::composeWorldBaseTcp（§5.1 唯一来源
 *     声明/M-11/AT-37——本单元不写第二套基座变换代数）；
 *   - 全部浮点运算定序固定（逐关节单遍、SVD 单线程）——同输入同字节
 *     （NFR-COR-01，确定性来源登记：无并行归约、无时钟/环境依赖）。
 */

#include <sdurws/ird/kinematics/Fk.hpp>

#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // composeWorldBaseTcp——基座—世界唯一组合点
#include <sdurws/ird/runtime/Description.hpp>         // JointType（可动/固定判定）

#include <Eigen/Core>
#include <Eigen/SVD>  // JacobiSVD——O-40 登记的 SVD 唯一实现点（rw::math 无 SVD）

#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 错误值构造辅助（KinematicsError 值面——§9.1 非异常出口；params 键为
// T03 生产者登记面：expected-dof/actual-dof/nonfinite-index）
// =====================================================================

/// q 维度不符（expected/actual 均为自由度个数，无单位）。
KinematicsError makeIllegalQDimension(std::size_t expectedDof, std::size_t actualDof)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::IllegalQ;
    e.params.emplace_back("expected-dof", std::to_string(expectedDof));
    e.params.emplace_back("actual-dof", std::to_string(actualDof));
    e.detail = "q 维度与设备自由度不符（NFR-COR-03：拒绝，不钳制不置零）";
    return e;
}

/// q 含非有限分量（firstIndex 为首个违例分量的下标——自由度序，无单位）。
KinematicsError makeIllegalQNonFinite(std::size_t firstIndex)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::IllegalQ;
    e.params.emplace_back("nonfinite-index", std::to_string(firstIndex));
    e.detail = "q 含非有限分量（NaN/Inf——NFR-COR-03：拒绝，不置零）";
    return e;
}

/// 无可用设备（§9.6 KIN-NO-DEVICE——结构化错误素材，FK 失败≠工程不可行）。
KinematicsError makeNoDevice()
{
    KinematicsError e;
    e.code = KinematicsErrorCode::NoDevice;
    e.detail = "快照模型无可用设备链（KIN-NO-DEVICE 素材——§9.6）";
    return e;
}

/// TCP 未配置/悬空（§9.6 KIN-NO-TCP 两分语义；variantDetail 区分两分支）。
KinematicsError makeNoTcp(const char* variantDetail)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::NoTcp;
    e.detail = variantDetail;
    return e;
}

/// TCP 帧未解析（§9.2 @错误 行 FrameUnresolved——键不命中即拒绝，不模糊匹配）。
KinematicsError makeFrameUnresolved(const std::string& tcpKey, const std::string& toolName)
{
    KinematicsError e;
    e.code = KinematicsErrorCode::FrameUnresolved;
    e.params.emplace_back("tcp-key", tcpKey);
    e.params.emplace_back("tool-local-name", toolName);
    e.detail = "tcpKey 不命中 canonical TCP 身份（帧未解析——R-4 禁拼串定位）";
    return e;
}

// =====================================================================
// rw::math → Eigen 组件抽取（仅头内 inline 访问面——外联符号不可用）
// =====================================================================

/// 3×3 旋转抽取（行优先；无量纲）。
Eigen::Matrix3d toEigen(const rw::math::Rotation3D<double>& r)
{
    Eigen::Matrix3d m;
    // rw::math::Rotation3D 元素访问 operator()(i, j) 为模板头内 inline——
    // 两模式一致；行主序与 Eigen 默认布局一致，逐元素直拷。
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m(i, j) = r(static_cast<std::size_t>(i), static_cast<std::size_t>(j));
        }
    }
    return m;
}

/// 3 维平移/方向抽取（单位随语义：平移 m、轴向量无量纲单位向量）。
Eigen::Vector3d toEigen(const rw::math::Vector3D<double>& v)
{
    return Eigen::Vector3d(v[0], v[1], v[2]);
}

/// Eigen 旋转回写 rw 值类型（行优先逐元素构造——头内 inline 构造函数）。
rw::math::Rotation3D<double> toRw(const Eigen::Matrix3d& m)
{
    return rw::math::Rotation3D<double>(
        m(0, 0), m(0, 1), m(0, 2),
        m(1, 0), m(1, 1), m(1, 2),
        m(2, 0), m(2, 1), m(2, 2));
}

/// Eigen 平移回写 rw 值类型（单位 m——语义由调用处保证）。
rw::math::Vector3D<double> toRw(const Eigen::Vector3d& v)
{
    return rw::math::Vector3D<double>(v(0), v(1), v(2));
}

/// 关节是否消耗一个自由度（Fixed 为纯结构关节——无 q 分量）。
bool isMovable(runtime::JointType t)
{
    return t != runtime::JointType::Fixed;
}

// =====================================================================
// canonical 编码原语（定宽小端——卡面 §4.4 编码纪律；f64＝IEEE754 位
// 模式小端 8 字节，含 +∞——位模式确定）
// =====================================================================

/// 追加小端 u32。
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

/// 追加小端 f64（位模式经 memcpy 取得——免别名 UB，位表示确定）。
void putF64(std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "f64 位模式直写要求 8 字节 double");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((bits >> (8 * i)) & 0xFFull));
    }
}

}  // namespace

// =====================================================================
// FkEvaluator::evaluate——五步主流程（类注释列纲，逐步对号）
// =====================================================================

Expected<PoseMetrics> FkEvaluator::evaluate(const IKinRuntimeView& view,
                                            const TcpRef& tcp,
                                            const std::vector<double>& q) const
{
    // ---- 第 1 步：模型获取与结构守卫（§5.1 唯一来源——模型只经视图）----
    // 链空＝无可用设备（CanonicalModel 构造器已禁空链，此处为注入视图面
    // 的契约守卫——替身/适配器实现违约时 fail-fast 值面，KIN-NO-DEVICE
    // 素材；FK 失败≠工程不可行，§5.2 两分语义）。
    const runtime::CanonicalModel& model = view.model();
    const std::vector<runtime::CanonicalJoint>& joints = model.chain().joints;
    if (joints.empty()) {
        return Expected<PoseMetrics>::err(makeNoDevice());
    }

    // 自由度清点：Fixed 关节不消耗 q（§5.2"q 维度=device DOF"的 DOF 定义
    // ——可动关节（Revolute/Continuous/Prismatic）链序计数）。
    std::size_t dof = 0;
    for (const auto& j : joints) {
        if (isMovable(j.type)) { ++dof; }
    }

    // ---- 第 2 步：非法输入 fail-fast（V-03 前半/NFR-COR-03）----
    // 先维度后有限性，两检独立可辨（params 键区分）——不钳制不置零。
    if (q.size() != dof) {
        return Expected<PoseMetrics>::err(makeIllegalQDimension(dof, q.size()));
    }
    for (std::size_t k = 0; k < q.size(); ++k) {
        if (!std::isfinite(q[k])) {
            return Expected<PoseMetrics>::err(makeIllegalQNonFinite(k));
        }
    }

    // ---- 第 3 步：TCP 解析（KinTypes.hpp 解析规则；NoTcp 两分语义）----
    const std::vector<runtime::CanonicalTool>& tools = model.tools();
    if (tools.empty()) {
        // 未配置：快照模型无任何工具（§9.6 KIN-NO-TCP"TCP 未配置"）。
        return Expected<PoseMetrics>::err(
            makeNoTcp("TCP 未配置：快照模型无工具（KIN-NO-TCP 素材——§9.6）"));
    }
    const auto loc = model.findObject(tcp.toolObject);
    if (!loc.has_value() || loc->kind != runtime::CanonicalModel::ObjectKind::Tool) {
        // 悬空：指名对象不在快照内、或不是工具定义（"引用悬空"分支）。
        return Expected<PoseMetrics>::err(
            makeNoTcp("TCP 引用悬空：toolObject 未解析到快照工具（KIN-NO-TCP 素材——§9.6）"));
    }
    const runtime::CanonicalTool& tool = tools.at(loc->index);
    // 键匹配：空串＝canonical TCP；非空须与工具 localName 精确相等
    // （canonical 面单 TCP——解析规则登记于 KinTypes.hpp/卡 §14.6）。
    if (!tcp.tcpKey.empty() && tcp.tcpKey != tool.localName) {
        return Expected<PoseMetrics>::err(
            makeFrameUnresolved(tcp.tcpKey, tool.localName));
    }

    // ---- 第 4 步：链式 FK＋几何雅可比源点采集（单遍；Eigen 组件域）----
    // T_acc 自基座累计到"当前关节系"：T_acc ← T_acc·(origin_i·M_i(q_i))。
    // 复合展开：R = R·R_o（后随 R·R_m）；p = R·p_o + p——顺序不可换
    // （T_ab·T_bc＝T_ac，core §4.6 读法）。
    Eigen::Matrix3d RAcc = Eigen::Matrix3d::Identity();
    Eigen::Vector3d pAcc = Eigen::Vector3d::Zero();

    // 雅可比源点（基座系）：第 k 个可动关节的轴上点 p_i（关节系原点——
    // 旋转轴过关节原点）与轴方向 a_i＝R_joint·axis（关节运动前的瞬时
    // 值——基础雅可比的标准构造）。
    std::vector<Eigen::Vector3d> axisPointInBase;
    std::vector<Eigen::Vector3d> axisDirInBase;
    std::vector<runtime::JointType> axisKind;
    axisPointInBase.reserve(dof);
    axisDirInBase.reserve(dof);
    axisKind.reserve(dof);

    std::size_t qIndex = 0;  // 可动关节序号（q 向量下标——链序）
    for (const auto& j : joints) {
        // 4a. 固定段：先复合 origin（父连杆系→关节系）。
        const Eigen::Matrix3d Ro = toEigen(j.origin.R());
        const Eigen::Vector3d po = toEigen(j.origin.P());
        const Eigen::Matrix3d RJ = RAcc * Ro;
        const Eigen::Vector3d pJ = RAcc * po + pAcc;

        if (!isMovable(j.type)) {
            // Fixed：纯结构关节——无运动、无源点（q 不前进）。
            RAcc = RJ;
            pAcc = pJ;
            continue;
        }

        // 4b. 可动关节：在运动前记录该关节的基座系轴点/轴方向（雅可比
        // 列源数据；旋转绕过原点的轴——轴点即关节系原点）。
        const Eigen::Vector3d axis = toEigen(j.axis);  // 单位向量（编译规格化——MDL-09）
        axisPointInBase.push_back(pJ);
        axisDirInBase.push_back(RJ * axis);
        axisKind.push_back(j.type);

        // 4c. 关节运动 M(q)（q 为权威值——§5.1，不再叠加 zeroOffset）：
        //     转动（Revolute/Continuous）：绕单位轴 Rodrigues 旋转
        //       （Eigen::AngleAxis 定值公式——确定性；rad）；
        //     移动（Prismatic）：沿轴平移 q（m）。
        if (j.type == runtime::JointType::Prismatic) {
            pAcc = pJ + RJ * (axis * q[qIndex]);
            RAcc = RJ;
        } else {
            const Eigen::Matrix3d Rm =
                Eigen::AngleAxisd(q[qIndex], axis).toRotationMatrix();
            RAcc = RJ * Rm;
            pAcc = pJ;
        }
        ++qIndex;
    }
    // T_base_flange＝RAcc/pAcc（链末——法兰系）。

    // 4d. TCP 偏置：T_base_tcp＝T_base_flange·T_flange_tcp（工具单 TCP——
    // runtime §4.3.4；tcpOffset 正交性由模型构造域保证）。
    const Eigen::Matrix3d Rt = toEigen(tool.tcpOffset.R());
    const Eigen::Vector3d pt = toEigen(tool.tcpOffset.P());
    const Eigen::Matrix3d RTcp = RAcc * Rt;
    const Eigen::Vector3d pTcp = RAcc * pt + pAcc;

    // ---- 第 5 步：指标链（D-KIN-2 统一尺度规则）----
    PoseMetrics m;

    // 5a. TCP 位姿（基座系）——出 Eigen 域，一次性回写 rw 值类型。
    // Transform3D 构造序＝(Vector3D 平移, Rotation3D 旋转)——rw/math/
    // Transform3D.hpp 77 行签名（夹具同款先例）。
    m.tcpInBase = rw::math::Transform3D<double>(toRw(pTcp), toRw(RTcp));

    // 5b. TCP 位姿（世界系）——只经 runtime 唯一组合点（M-11/AT-37：
    // p_world＝T_world_base·p_base；禁止第二套基座变换代数）。
    m.tcpInWorld = runtime::composeWorldBaseTcp(view.worldToBase(), m.tcpInBase);

    // 5c. 基础雅可比（6×n，列＝可动关节链序）：
    //     转动列：Jv＝a×(p_tcp−p_i)（m per rad/s）、Jw＝a（无量纲）；
    //     移动列：Jv＝a（无量纲）、Jw＝0——D-KIN-2"平移行 m、转动行
    //     无量纲、不做关节加权归一"。
    Eigen::MatrixXd J(6, static_cast<Eigen::Index>(dof));
    for (std::size_t k = 0; k < dof; ++k) {
        Eigen::Vector3d jv;
        Eigen::Vector3d jw = Eigen::Vector3d::Zero();
        if (axisKind[k] == runtime::JointType::Prismatic) {
            // 移动关节：线速度列＝轴方向（无量纲）、角速度列＝0。
            jv = axisDirInBase[k];
        } else {
            // 转动关节：线速度列＝a×(p_tcp−p_i)（m per rad/s）、
            // 角速度列＝a（无量纲）。
            jv = axisDirInBase[k].cross(pTcp - axisPointInBase[k]);
            jw = axisDirInBase[k];
        }
        J(0, static_cast<Eigen::Index>(k)) = jv(0);
        J(1, static_cast<Eigen::Index>(k)) = jv(1);
        J(2, static_cast<Eigen::Index>(k)) = jv(2);
        J(3, static_cast<Eigen::Index>(k)) = jw(0);
        J(4, static_cast<Eigen::Index>(k)) = jw(1);
        J(5, static_cast<Eigen::Index>(k)) = jw(2);
    }
    // Eigen 默认列主序存储——行优先展平须经 (i,j)→i·n+j 的显式映射拷贝
    // （字段契约＝行优先；不用 data() 直拷，避免存储序耦合）。
    m.jacobian.assign(static_cast<std::size_t>(6 * dof), 0.0);
    for (std::size_t r = 0; r < 6; ++r) {
        for (std::size_t c = 0; c < dof; ++c) {
            m.jacobian[r * dof + c] = J(static_cast<Eigen::Index>(r),
                                        static_cast<Eigen::Index>(c));
        }
    }

    // 5d. 奇异值（JacobiSVD——O-40 唯一 SVD 实现点；Eigen 契约保证降序；
    // 单线程无归约——确定性）。6×n 的非零奇异值个数＝min(6, n)。
    // dof=0（全固定链）跳过 SVD——零列矩阵无奇异值面（下游按空集承载
    // ——条件数走 5e 的奇异口径）。
    if (dof > 0) {
        const Eigen::JacobiSVD<Eigen::MatrixXd> svd(
            J, Eigen::ComputeThinU | Eigen::ComputeThinV);
        const auto& sv = svd.singularValues();
        m.singularValues.assign(sv.data(), sv.data() + sv.size());
    }

    // 5e. 条件数＝σmax/σmin（降序首末）；σmin＝0 或商非有限（上溢）→
    // +∞＋isFinite=false——D-KIN-2"奇异→+∞，以 isFinite 标记承载，不
    // 静默截断"。零雅可比（全零 σ）落入同一出口（0/0 非有限）。
    if (m.singularValues.empty()) {
        // 全固定链（dof=0）：无奇异值——条件数不定义，按奇异口径承载。
        m.conditionNumber = std::numeric_limits<double>::infinity();
        m.conditionNumberIsFinite = false;
    } else {
        const double smax = m.singularValues.front();
        const double smin = m.singularValues.back();
        if (smin == 0.0) {
            m.conditionNumber = std::numeric_limits<double>::infinity();
            m.conditionNumberIsFinite = false;
        } else {
            const double cond = smax / smin;
            if (std::isfinite(cond)) {
                m.conditionNumber = cond;
                m.conditionNumberIsFinite = true;
            } else {
                m.conditionNumber = std::numeric_limits<double>::infinity();
                m.conditionNumberIsFinite = false;
            }
        }
    }

    // 5f. 可操作度 w＝√det(J·Jᵀ)：由 SVD 恒等式 det(J·Jᵀ)＝∏σᵢ²（满秩
    // 因子）——w＝∏σᵢ（n≥6 时恰为前 6 个；n<6 时 det 恒 0 → w＝0）。
    // 用奇异值积而非显式行列式：数学等价、天然非负（免 Gram 行列式的
    // 舍入负号噪声），确定性更强——实现口径随卡 §14.6 登记。
    if (m.singularValues.size() < 6U) {
        m.manipulability = 0.0;  // 行秩 <6 → det(J·Jᵀ)=0（恒等式）
    } else {
        double w = 1.0;
        for (double s : m.singularValues) {
            w *= s;  // 恰 6 个（min(6,n)=6）——逐个连乘，定序固定
        }
        m.manipulability = w;
    }

    // 5g. 归一化关节裕量（D-KIN-6 设计默认——随黄金数据集锁定）：
    //     有界（Revolute/Prismatic 的 bounds；Continuous 的 workingRange）
    //     margin＝min(q−lo, hi−q)/(行程/2)——中点 1、限位 0、超限为负
    //     （超限 q 可计算、违例素材交调用方——§5.2 两分语义；无量纲）；
    //     Continuous 无工作范围＝无分析限位 → +∞。
    m.jointMargins.reserve(dof);
    double minMargin = std::numeric_limits<double>::infinity();
    bool anyFinite = false;
    qIndex = 0;
    for (const auto& j : joints) {
        if (!isMovable(j.type)) { continue; }
        const double qi = q[qIndex];
        ++qIndex;

        // 评价区间三态：bounds（Revolute/Prismatic 必有）→ workingRange
        // （Continuous 可有——MDL-12 分析消费属性，与 bounds 值面同构：
        // 有限有序区间）→ 均无（Continuous 未配置工作范围）＝无分析限位。
        // 前两态同式评价（区间来源不同、公式相同）。
        double lo = 0.0;
        double hi = 0.0;
        bool bounded = false;
        if (j.bounds.has_value()) {
            lo = j.bounds->lower;
            hi = j.bounds->upper;
            bounded = true;
        } else if (j.workingRange.has_value()) {
            lo = j.workingRange->lower;
            hi = j.workingRange->upper;
            bounded = true;
        }

        if (bounded) {
            const double half = (hi - lo) / 2.0;
            const double margin = (std::min)(qi - lo, hi - qi) / half;
            m.jointMargins.push_back(margin);
            if (std::isfinite(margin) && (!anyFinite || margin < minMargin)) {
                minMargin = margin;
                anyFinite = true;
            }
        } else {
            // 无分析限位：裕量不定义 → +∞（IEEE 位模式确定——确定性）。
            m.jointMargins.push_back(std::numeric_limits<double>::infinity());
        }
    }
    m.minimumJointMargin = anyFinite ? minMargin
                                     : std::numeric_limits<double>::infinity();

    return Expected<PoseMetrics>::ok(std::move(m));
}

// =====================================================================
// canonical 载荷编码（Fk.hpp 布局表；字段序即写序）
// =====================================================================

std::vector<std::uint8_t> encodePoseMetricsCanonical(const PoseMetrics& m)
{
    std::vector<std::uint8_t> out;
    // 预算上限估算（避免重分配抖动；不改变内容——纯容量优化）。
    out.reserve(7 + 1 + 4 + 24 + 24 + m.jacobian.size() * 8 + 4
                + m.singularValues.size() * 8 + 8 + 1 + 8 + 8
                + m.jointMargins.size() * 8);

    // magic "IRDPM01"（7 字节 ASCII）＋ codec 版本 u8=1（布局演进即推进）。
    const char magic[] = {'I', 'R', 'D', 'P', 'M', '0', '1'};
    out.insert(out.end(), magic, magic + sizeof(magic));
    out.push_back(1U);

    // dof＝自由度数（jacobian 长度恒 6·dof——布局自洽校验隐含于消费方）。
    const auto dof = static_cast<std::uint32_t>(m.jointMargins.size());
    putU32(out, dof);

    // tcpInBase / tcpInWorld：{R 9×f64 行优先, p 3×f64}（m/rad）。
    for (const rw::math::Transform3D<double>* t :
         {&m.tcpInBase, &m.tcpInWorld}) {
        const rw::math::Rotation3D<double>& r = t->R();
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                putF64(out, r(i, j));
            }
        }
        const rw::math::Vector3D<double>& p = t->P();
        for (std::size_t i = 0; i < 3; ++i) {
            putF64(out, p[i]);
        }
    }

    // jacobian 6·dof×f64（行优先——与 PoseMetrics 字段序一致）。
    for (double v : m.jacobian) {
        putF64(out, v);
    }

    // 奇异值块（个数＋降序值）。
    putU32(out, static_cast<std::uint32_t>(m.singularValues.size()));
    for (double v : m.singularValues) {
        putF64(out, v);
    }

    // 条件数＋有限性标记＋可操作度＋最小裕量。
    putF64(out, m.conditionNumber);
    out.push_back(m.conditionNumberIsFinite ? 1U : 0U);
    putF64(out, m.manipulability);
    putF64(out, m.minimumJointMargin);

    // 逐自由度裕量。
    for (double v : m.jointMargins) {
        putF64(out, v);
    }
    return out;
}

}  // namespace sdurws::ird::kinematics
