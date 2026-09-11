/**
 * @file   CanonicalModel.cpp
 * @brief  CanonicalModel 构造不变量（CanonicalModelBuilder::build）、能力派生
 *         （§9.6）与位模式等值（§4.3.6）的实现。
 *
 * 设计依据：
 *   - units/runtime.md §4.3.1～§4.3.5（字段表"合法与非法实例"列——本文件
 *     逐条拦截）、§4.3.6（等价关系冻结——位模式比较）、§4.4（非有限拒绝
 *     NFR-COR-03）、§4.5（集合稳定键排序——确定性编码前置）、§4.6（只读
 *     索引）、§9.6（能力派生规则）
 *   - 需求 ARC-03/CON-05/CON-01/NFR-COR-03；任务契约 tasks/foundation/
 *     RT-T04.json（产物 1）
 *
 * 错误语义（AGENTS.md 错误总纲在本文件的落点）：builder 输入违约＝调用方
 * 契约违约（编译链 S1～S3 已拦截的输入带病到达 S5）→ fail-fast 抛
 * RuntimeError（不返回半成品、不静默修正；轴向规格化是设计文档明文的
 * 构造步骤除外——§4.3.3"编译器规格化"）。
 *
 * 确定性：全部比较/排序/规格化为纯函数——同输入同输出、无环境依赖
 * （NFR-COR-02）；排序键＝ObjectId 规范文本字典序（§4.5 集合稳定键）。
 * 线程安全：无共享可变状态（静态常量表外无静态数据），可重入。
 */

#include <sdurws/ird/runtime/CanonicalModel.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>  // std::memcpy（位模式比较——bitEqual）
#include <cstdint>
#include <string>
#include <utility>  // std::move
#include <vector>

#include <sdurws/ird/runtime/Codec.hpp>  // rtcodec::computeContentIdentity（身份计算）

namespace sdurws::ird::runtime {

namespace {

// =====================================================================
// 常量（出处随注释——魔法数字禁止无出处，AGENTS.md §2.4）。
// =====================================================================

/// 旋转矩阵正交性容差：max|RᵀR−I| ≤ 1×10⁻¹²（§4.3.2/§6.6 原文；与 S3
/// 校验器 DescriptionValidator 的 kOrthoTolerance 同源同值）。
constexpr double kOrthoTolerance = 1e-12;

/// 惯量张量对称性容差：|I_ij−I_ji| ≤ 1×10⁻¹²（§4.3.3"对称性容差附录 D
/// 第 6 项"；与 S3 校验器 kSymmetryTolerance 同源同值）。
constexpr double kSymmetryTolerance = 1e-12;

// =====================================================================
// 基础数值助手（位模式等值——§4.3.6"字节相同"是唯一等价关系）。
// =====================================================================

/// double 按位模式等值：+0.0 与 −0.0 不等、1×10⁻¹⁵ 差异不等（RT-ID-2 的
/// 实现基础——严禁改用 ==：那会把 ±0 判等，破坏"编码字节等值"语义）。
bool bitEqual(double a, double b) noexcept
{
    static_assert(sizeof(double) == 8, "IEEE754 双精度前提（§4.5 编码面）");
    std::uint64_t ia = 0;
    std::uint64_t ib = 0;
    std::memcpy(&ia, &a, sizeof(ia));
    std::memcpy(&ib, &b, sizeof(ib));
    return ia == ib;
}

/// 向量逐分量位模式等值。
bool vecEqual(const rw::math::Vector3D<double>& a, const rw::math::Vector3D<double>& b) noexcept
{
    for (int i = 0; i < 3; ++i) {
        if (!bitEqual(a(i), b(i))) { return false; }
    }
    return true;
}

/// 旋转矩阵 9 分量按行主序位模式等值（§4.5"旋转矩阵按行主序编码"同序）。
bool rotEqual(const rw::math::Rotation3D<double>& a, const rw::math::Rotation3D<double>& b) noexcept
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!bitEqual(a(i, j), b(i, j))) { return false; }
        }
    }
    return true;
}

/// 变换＝旋转＋平移的位模式等值。
bool transformEqual(const rw::math::Transform3D<double>& a,
                    const rw::math::Transform3D<double>& b) noexcept
{
    return rotEqual(a.R(), b.R()) && vecEqual(a.P(), b.P());
}

/// 惯量张量 9 分量按行主序位模式等值。
bool inertiaEqual(const rw::math::InertiaMatrix<double>& a,
                  const rw::math::InertiaMatrix<double>& b) noexcept
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!bitEqual(a(i, j), b(i, j))) { return false; }
        }
    }
    return true;
}

/// SourcedValue<double> 的位模式等值：状态＋值位模式＋来源记录（§4.3.6
/// "来源标记入身份"——来源不同即不等，语义等价不参与身份）。
/// 注意不能用 core 的 operator==：其值比较用 double==，会放过 ±0 位差。
bool svDoubleEqual(const core::SourcedValue<double>& a, const core::SourcedValue<double>& b) noexcept
{
    if (a.state() != b.state()) { return false; }
    switch (a.state()) {
    case core::FieldState::Provided: {
        const auto va = a.tryValue();
        const auto vb = b.tryValue();
        return va.has_value() && vb.has_value() && bitEqual(*va, *vb)
               && a.provenance() == b.provenance();
    }
    case core::FieldState::Invalid:
        return a.invalidRawInput() == b.invalidRawInput();
    default:  // NotProvided/NotApplicable 无载荷
        return true;
    }
}

/// SourcedValue<Vector3D> 位模式等值（质心字段）。
bool svVecEqual(const core::SourcedValue<rw::math::Vector3D<double>>& a,
                const core::SourcedValue<rw::math::Vector3D<double>>& b) noexcept
{
    if (a.state() != b.state()) { return false; }
    if (a.state() != core::FieldState::Provided) { return true; }
    const auto va = a.tryValue();
    const auto vb = b.tryValue();
    return va.has_value() && vb.has_value() && vecEqual(*va, *vb)
           && a.provenance() == b.provenance();
}

/// SourcedValue<InertiaMatrix> 位模式等值（惯量字段）。
bool svInertiaEqual(const core::SourcedValue<rw::math::InertiaMatrix<double>>& a,
                    const core::SourcedValue<rw::math::InertiaMatrix<double>>& b) noexcept
{
    if (a.state() != b.state()) { return false; }
    if (a.state() != core::FieldState::Provided) { return true; }
    const auto va = a.tryValue();
    const auto vb = b.tryValue();
    return va.has_value() && vb.has_value() && inertiaEqual(*va, *vb)
           && a.provenance() == b.provenance();
}

/// ResourceRef 等值按身份承载面比较：resourceId＋contentDigest（§8.6"路径
/// 不作身份"——sourcePathHint/accessVersion/state 的差异不影响等值）。
bool resourceIdentityEqual(const ResourceRef& a, const ResourceRef& b) noexcept
{
    return a.resourceId == b.resourceId && a.contentDigest == b.contentDigest;
}

/// ObjectId 排序键：规范文本字典序（§4.5"对象按 ObjectId 规范文本字典序"——
/// 逐次格式化的开销可接受：阶段 A 模型规模为个位数到十位数对象）。
std::string objectIdSortKey(const core::ObjectId& id) { return id.toCanonical(); }

// =====================================================================
// 校验助手（失败即抛 RuntimeError——调用方契约违约 fail-fast）。
// =====================================================================

/// 构造 InputInvalid 异常（detail 中文定位——review 者不看文档也能定位）。
RuntimeError inputInvalid(std::string detail)
{
    return RuntimeError{RuntimeErrorCode::InputInvalid, std::move(detail)};
}

/// 构造 StructureInvalid 异常（结构/引用/唯一性违约——CM-0 相关）。
RuntimeError structureInvalid(std::string detail)
{
    return RuntimeError{RuntimeErrorCode::StructureInvalid, std::move(detail)};
}

/// 3×3 行主序行列式（旋转矩阵 det 判别用——反射检测）。
double determinant3x3(const rw::math::Rotation3D<double>& r) noexcept
{
    return r(0, 0) * (r(1, 1) * r(2, 2) - r(1, 2) * r(2, 1))
           - r(0, 1) * (r(1, 0) * r(2, 2) - r(1, 2) * r(2, 0))
           + r(0, 2) * (r(1, 0) * r(2, 1) - r(1, 1) * r(2, 0));
}

/**
 * @brief 校验变换合法性（§4.3 各变换字段的公共约束——正交＋非反射＋有限）。
 *
 * 三步与 S3 校验器 checkTransform 同口径：
 *   ①有限性（先于其余——NaN 传播使正交判读无意义）；
 *   ②正交性：max|RᵀR−I| ≤ 1×10⁻¹²；
 *   ③反射：det(R) ≤ 0 即非法（合法旋转 det=+1）。②未过时跳过③（缩放
 *     矩阵的 det 判读会被污染——与 S3 同款防护）。
 *
 * @param t    [in] 待校验变换
 * @param what [in] 字段定位前缀（如 "world.T_world_base"——进异常 detail）
 * @throws RuntimeError InputInvalid（携 what 定位）
 */
void requireValidTransform(const rw::math::Transform3D<double>& t, const std::string& what)
{
    // ①非有限拒绝（NFR-COR-03：不得静默转 0/夹紧）。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(t.P()(i))) {
            throw inputInvalid(what + ".P：平移含非有限分量（NaN/±Inf）——NFR-COR-03 拒绝");
        }
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(t.R()(i, j))) {
                throw inputInvalid(what + ".R：旋转矩阵含非有限分量（NaN/±Inf）——NFR-COR-03 拒绝");
            }
        }
    }
    // ②正交性（§4.3.2 容差 1×10⁻¹²）：逐元素比较 RᵀR 与 I。
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) { dot += t.R()(k, i) * t.R()(k, j); }
            if (std::fabs(dot - (i == j ? 1.0 : 0.0)) > kOrthoTolerance) {
                throw inputInvalid(what + ".R：旋转矩阵非正交（max|RᵀR−I| > 1×10⁻¹²，§4.3.2/§6.6）");
            }
        }
    }
    // ③反射检测（det ≤ 0——手性翻转；RT-BW-6 反例面）。
    if (determinant3x3(t.R()) <= 0.0) {
        throw inputInvalid(what + ".R：旋转矩阵为反射（det ≤ 0）——非法手性");
    }
}

/// 名字合法性：非空且不含 '/'（§4.3.3"非法：空/含 '/'"——'/' 是名称作用域
/// 分隔符保留字，§7.2 合法字符集不含它）。
void requireValidLocalName(const std::string& name, const std::string& what)
{
    if (name.empty()) {
        throw inputInvalid(what + "：局部名为空（非法——§4.3.3）");
    }
    if (name.find('/') != std::string::npos) {
        throw inputInvalid(what + "：局部名含 '/'（作用域分隔符保留——§4.3.3/§7.2）");
    }
}

/// Provided 态 double 的有限性＋正性复核（物性/摩擦/传动比——§4.3.3/§4.3.4
/// "合法：有限正值"）。负值或 0 对这些字段都无物理意义。
void requireProvidedPositive(const core::SourcedValue<double>& v, const std::string& what)
{
    if (v.state() != core::FieldState::Provided) { return; }  // 缺失＝能力缺失，放行
    const auto x = v.tryValue();
    if (!x.has_value() || !std::isfinite(*x)) {
        throw inputInvalid(what + "：Provided 值非有限（NaN/±Inf）——NFR-COR-03 拒绝");
    }
    if (*x <= 0.0) {
        throw inputInvalid(what + "：Provided 值须为正（实测 " + std::to_string(*x)
                           + "）——§4.3.3/§4.3.4 合法域");
    }
}

/// Provided 态 double 的有限性＋非负复核（限速/加速度——§4.3.3"provided(负数)
/// →InputInvalid"；0 合法＝静止上限）。
void requireProvidedNonNegative(const core::SourcedValue<double>& v, const std::string& what)
{
    if (v.state() != core::FieldState::Provided) { return; }
    const auto x = v.tryValue();
    if (!x.has_value() || !std::isfinite(*x)) {
        throw inputInvalid(what + "：Provided 值非有限（NaN/±Inf）——NFR-COR-03 拒绝");
    }
    if (*x < 0.0) {
        throw inputInvalid(what + "：Provided 值须 ≥0（实测 " + std::to_string(*x) + "）——§4.3.3");
    }
}

/**
 * @brief 惯量张量复核（Provided 态）：有限→对称（1×10⁻¹²）→正定（SPD）。
 *
 * 正定判据用 Sylvester 准则（对称矩阵正定 ⟺ 各阶顺序主子式＞0）——与 S3
 * 校验器的对称 Jacobi 最小特征值＞0 判据数学等价（§4.3.3 物理惯量远离奇异
 * 边界，两判据在合法域内结论一致）；选用主子式因其 O(1) 确定性、无迭代。
 *
 * @param m    [in] 惯量张量（质心系，单位 kg·m²）
 * @param what [in] 字段定位前缀
 * @throws RuntimeError InputInvalid
 */
void requireValidInertia(const rw::math::InertiaMatrix<double>& m, const std::string& what)
{
    // ①非有限拒绝。
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(m(i, j))) {
                throw inputInvalid(what + "：惯量张量含非有限分量——NFR-COR-03 拒绝");
            }
        }
    }
    // ②对称性（附录 D 第 6 项容差 1×10⁻¹²）。
    for (int i = 0; i < 3; ++i) {
        for (int j = i + 1; j < 3; ++j) {
            if (std::fabs(m(i, j) - m(j, i)) > kSymmetryTolerance) {
                throw inputInvalid(what + "：惯量张量非对称（|I_ij−I_ji| > 1×10⁻¹²）——MDL-05");
            }
        }
    }
    // ③正定（Sylvester 顺序主子式全＞0；半正定＝质量分布退化，非法）。
    const double d1 = m(0, 0);
    const double d2 = m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0);
    const double d3 = determinant3x3(rw::math::Rotation3D<double>(
        m(0, 0), m(0, 1), m(0, 2), m(1, 0), m(1, 1), m(1, 2), m(2, 0), m(2, 1), m(2, 2)));
    if (!(d1 > 0.0 && d2 > 0.0 && d3 > 0.0)) {
        throw inputInvalid(what + "：惯量张量非正定（SPD 复核失败）——MDL-05/§4.3.3");
    }
}

/**
 * @brief 发布模型诊断块的 error 级稳定码拒绝集（§4.3.5"仅警告级……error 级
 *        ＝编译失败，不进模型"）。
 *
 * 成员＝十段编译链的硬失败码（§10.11 冻结注册码，经 Errors.hpp 的
 * registryCode() 取得——单一事实来源，不复制字符串字面量）：这些码出现
 * 即意味对应段已失败、按 MDL-06 原子性不应有发布产物；带着它们构造模型
 * 属调用方契约违约。
 * 不在集内：Cancelled（非错误路径——UX-03/D-11）、RT-CAPABILITY-MISSING 与
 * RT-ROBWORK-ERROR（诊断事件码——警告/转译路径）、UnknownObject/ContextReleased
 * （fail-fast 轨不发稳定码）、以及未来新增的警告类码（码值权威归 diagnostics，
 * 本单元不私裁收窄）。
 */
bool isHardErrorDiagCode(const std::string& code) noexcept
{
    const RuntimeErrorCode hardErrors[] = {
        RuntimeErrorCode::InputInvalid,          RuntimeErrorCode::StructureInvalid,
        RuntimeErrorCode::UnitMismatch,          RuntimeErrorCode::ResourceMissing,
        RuntimeErrorCode::ResourceChanged,       RuntimeErrorCode::ResourceBudget,
        RuntimeErrorCode::WorkCellCompileFailed, RuntimeErrorCode::DwcCompileFailed,
        RuntimeErrorCode::NameConflict,          RuntimeErrorCode::BaseWorldInconsistent,
    };
    for (const RuntimeErrorCode e : hardErrors) {
        if (code == registryCode(e)) { return true; }
    }
    return false;
}

/// 规范化排序＋相邻重复检测（集合稳定键——§4.5；重复键＝调用方装配违约）。
template <typename Range, typename KeyFn>
void sortAndRejectDuplicates(Range& items, KeyFn&& keyOf, const std::string& what)
{
    std::sort(items.begin(), items.end(), [&keyOf](const auto& a, const auto& b) {
        return keyOf(a) < keyOf(b);
    });
    for (std::size_t i = 1; i < items.size(); ++i) {
        if (keyOf(items[i - 1]) == keyOf(items[i])) {
            throw structureInvalid(what + "：出现重复 ObjectId（" + keyOf(items[i])
                                   + "）——§4.3 全模型唯一约束");
        }
    }
}

}  // namespace

// =====================================================================
// 能力派生（§9.6——规则细则见 CanonicalModel.hpp 结构体注释）。
// =====================================================================

RuntimeCapability deriveRuntimeCapability(const RobotChain& chain,
                                          const std::vector<CanonicalTool>& tools,
                                          const std::vector<CanonicalSceneObject>& scene,
                                          const CanonicalDrivetrain& drivetrain)
{
    RuntimeCapability cap;  // 默认：hasWorkCell/hasBidirectionalNameMap 恒 true，其余 false

    cap.hasTools = !tools.empty();
    cap.hasScene = !scene.empty();

    // 物性齐备位：连杆全 Provided 前提＋（Full 位另要求）工具全 Provided。
    // 空集合按"全称量化成立"处理（无工具即工具侧不缺）。
    bool linksFull = true;
    bool linksPhysicsFull = true;
    for (const CanonicalLink& l : chain.links) {
        const bool full = l.mass.state() == core::FieldState::Provided
                          && l.centerOfMass.state() == core::FieldState::Provided
                          && l.inertia.state() == core::FieldState::Provided;
        linksFull = linksFull && full;
        // DWC Body 物性＝连杆三元组齐备（§9.6"被消费 Body"——见头文件规则注释）。
        linksPhysicsFull = linksPhysicsFull && full;
    }
    bool toolsFull = true;
    bool toolsCollision = false;
    for (const CanonicalTool& t : tools) {
        const bool full = t.mass.state() == core::FieldState::Provided
                          && t.centerOfMass.state() == core::FieldState::Provided
                          && t.inertia.state() == core::FieldState::Provided;
        toolsFull = toolsFull && full;
        if (t.geometry.has_value()) { toolsCollision = true; }
    }
    cap.hasFullMassInertia = linksFull && toolsFull;
    cap.hasDynamicWorkCell = linksPhysicsFull;

    // 限速位：全关节 maxVelocity Provided（§9.6 字面）。
    bool velocityFull = !chain.joints.empty();
    bool frictionFull = !chain.joints.empty();
    bool anyCollision = toolsCollision;
    for (const CanonicalJoint& j : chain.joints) {
        velocityFull = velocityFull && j.maxVelocity.state() == core::FieldState::Provided;
        frictionFull = frictionFull
                       && j.friction.viscous.state() == core::FieldState::Provided
                       && j.friction.coulomb.state() == core::FieldState::Provided
                       && j.friction.bias.state() == core::FieldState::Provided;
    }
    for (const CanonicalLink& l : chain.links) {
        if (l.collision.has_value()) { anyCollision = true; }
    }
    cap.hasJointVelocityLimits = velocityFull;
    cap.hasFrictionModel = frictionFull;
    // 碰撞几何位：任一连杆 collision / 任一工具 geometry / 场景非空（§9.6）。
    cap.hasCollisionGeometry = anyCollision || !scene.empty();

    cap.hasCouplingMatrix = drivetrain.coupling.has_value();
    cap.hasWorkCell = true;               // 恒 true（§9.6——无 WC 即无快照）
    cap.hasBidirectionalNameMap = true;   // 恒 true（映射随快照必建且双射）

    // 关节类型序列按链序（类型保留事实——V12-01）。
    cap.jointTypesPresent.reserve(chain.joints.size());
    for (const CanonicalJoint& j : chain.joints) { cap.jointTypesPresent.push_back(j.type); }
    return cap;
}

// =====================================================================
// CanonicalModelBuilder::build（校验→规范化→派生→索引→身份）。
// =====================================================================

CanonicalModel CanonicalModelBuilder::build() const
{
    // 部件取副本做校验与规范化——builder 自身保持 set 后原样（build 可重复
    // 调用；副本策略让"排序/单位化"不改写调用方通过 setter 提供的原件）。
    CanonicalModelHeader header = m_header;
    WorldPlacement world = m_world;
    RobotChain chain = m_chain;
    std::vector<CanonicalTool> tools = m_tools;
    std::vector<CanonicalSceneObject> scene = m_scene;
    CanonicalDrivetrain drivetrain = m_drivetrain;
    std::vector<ResourceRef> manifest = m_resourceManifest;
    std::vector<core::DiagnosticRecord> diagnostics = m_diagnostics;

    // ---- 第 1 步：身份与来源块（§4.3.1）----
    // 三个定位 id 非空（保留值纪律：全零＝未设置，非法）；闭包成员判定归
    // S1（RT-T11），此处只拦值级违约。
    if (!header.project.isValid() || !header.branch.isValid() || !header.revision.isValid()) {
        throw inputInvalid("header：project/branch/revision 存在空 id——§4.3.1");
    }
    if (header.descriptionContractVersion < 1 || header.compilerContractVersion < 1) {
        throw inputInvalid("header：descriptionContractVersion/compilerContractVersion 须 ≥1——§4.3.1");
    }
    // builtFrom 非零（Description 摘要——空摘要＝没有编译输入的身份）。
    bool builtFromZero = true;
    for (const std::uint8_t b : header.builtFrom) {
        if (b != 0) { builtFromZero = false; break; }
    }
    if (builtFromZero) {
        throw inputInvalid("header.builtFrom：Description 摘要为全零——§4.3.1 非空 Digest256");
    }
    // objectRefs ≥1 且逐条 id 非空、无重复（CM-0 引用一致性的值层面）。
    if (header.objectRefs.empty()) {
        throw structureInvalid("header.objectRefs：为空（§4.3.1 要求 ≥1）");
    }
    for (const ObjectRefEntry& e : header.objectRefs) {
        if (!e.objectId.isValid()) {
            throw inputInvalid("header.objectRefs：存在空 objectId——§4.3.1");
        }
    }

    // ---- 第 2 步：资源清单（§4.3.5）——先于链/工具/场景，因引用命中检查需要它 ----
    for (const ResourceRef& r : manifest) {
        if (!r.resourceId.isValid()) {
            throw inputInvalid("resourceManifest：存在空 resourceId——§4.3.5");
        }
        bool digestZero = true;
        for (const std::uint8_t b : r.contentDigest) {
            if (b != 0) { digestZero = false; break; }
        }
        if (digestZero) {
            throw inputInvalid("resourceManifest：存在全零内容摘要（§4.3.5\"逐条摘要非零\"）");
        }
    }
    // 稳定键排序（§4.5——resourceId 规范文本字典序）＋重复 id 拒绝。
    sortAndRejectDuplicates(
        manifest, [](const ResourceRef& r) { return r.resourceId.toCanonical(); },
        "resourceManifest");

    /// 引用命中检查：ResourceRef 须指向清单内同（resourceId, contentDigest）
    /// 的条目（§4.3.3"引用清单外的资源"非法；按身份承载面匹配——路径/状态
    /// 差异不影响命中，§8.6 路径不作身份）。
    const auto requireInManifest = [&manifest](const std::optional<ResourceRef>& ref,
                                               const std::string& what) {
        if (!ref.has_value()) { return; }
        for (const ResourceRef& r : manifest) {
            if (resourceIdentityEqual(r, *ref)) { return; }
        }
        throw structureInvalid(what + "：资源引用不在 resourceManifest（按 resourceId+digest"
                                    " 匹配）——§4.3.3/§4.3.4 引用约束");
    };

    // ---- 第 3 步：世界与基座块（§4.3.2）----
    requireValidTransform(world.T_world_base, "world.T_world_base");
    // 重力：有限且非全零（§4.3.2"非法：全零/非有限"）。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(world.gravityWorld(i))) {
            throw inputInvalid("world.gravityWorld：含非有限分量（单位 m/s²）——NFR-COR-03");
        }
    }
    if (world.gravityWorld(0) == 0.0 && world.gravityWorld(1) == 0.0
        && world.gravityWorld(2) == 0.0) {
        throw inputInvalid("world.gravityWorld：全零重力非法（§4.3.2）");
    }
    // 预设一致性（§4.3.2 唯一硬拒绝面）：Custom 而 R≈I——Custom 必须携带
    // 非恒等旋转（编辑侧 customEaa 的存在意义）。Inverted/Wall 的矩阵匹配
    // 与默认推导归 §6/RT-T06，不在此扩大语义。
    if (world.installPreset.state() == core::FieldState::Provided
        && world.installPreset.tryValue() == InstallationPresetToken::Custom) {
        bool identity = true;
        for (int i = 0; i < 3 && identity; ++i) {
            for (int j = 0; j < 3; ++j) {
                const double want = (i == j) ? 1.0 : 0.0;
                if (std::fabs(world.T_world_base.R()(i, j) - want) > kOrthoTolerance) {
                    identity = false;
                    break;
                }
            }
        }
        if (identity) {
            throw inputInvalid(
                "world：preset=Custom 而 T_world_base.R≈I（§4.3.2 一致性校验失败）");
        }
    }

    // ---- 第 4 步：机器人链块（§4.3.3）----
    if (!chain.robotObjectId.isValid()) {
        throw inputInvalid("chain.robotObjectId：空 id——§4.3.3");
    }
    requireValidLocalName(chain.robotLocalName, "chain.robotLocalName");
    requireValidLocalName(chain.deviceName, "chain.deviceName");
    if (chain.joints.empty()) {
        throw structureInvalid("chain.joints：为空（§4.3.3 要求 ≥1）");
    }
    if (chain.links.size() != chain.joints.size() + 1) {
        throw structureInvalid("chain：links.size() != joints.size()+1（§4.3.3 数量失配）");
    }

    // 逐关节校验＋轴向规格化（"编译器规格化"是 §4.3.3 axis 行明文的构造步骤）。
    for (std::size_t i = 0; i < chain.joints.size(); ++i) {
        CanonicalJoint& j = chain.joints.at(i);
        const std::string what = "chain.joints[" + std::to_string(i) + "]";
        if (!j.objectId.isValid()) { throw inputInvalid(what + ".objectId：空 id"); }
        requireValidLocalName(j.localName, what + ".localName");
        // 轴向：有限非零→就地单位化（确定性除法；零向量/非有限拒绝）。
        double normSq = 0.0;
        for (int k = 0; k < 3; ++k) {
            if (!std::isfinite(j.axis(k))) {
                throw inputInvalid(what + ".axis：含非有限分量——NFR-COR-03");
            }
            normSq += j.axis(k) * j.axis(k);
        }
        if (!(normSq > 0.0)) {
            throw inputInvalid(what + ".axis：零向量非法（MDL-11——任意有限非零轴）");
        }
        const double norm = std::sqrt(normSq);
        for (int k = 0; k < 3; ++k) { j.axis(k) = j.axis(k) / norm; }
        // 关节原点变换（T_parent_joint）。
        requireValidTransform(j.origin, what + ".origin");
        // 零位偏置有限（单位 rad 或 m——随 type）。
        if (!std::isfinite(j.zeroOffset)) {
            throw inputInvalid(what + ".zeroOffset：非有限——NFR-COR-03");
        }
        // 限位按类型分派（与 S3 校验器同口径，§4.3.3 bounds 行）：
        // Revolute/Prismatic 必填＋有序；Continuous 必无（工作范围替代）；
        // Fixed 未约束——提供则按有限＋有序复核。
        const bool boundedType =
            j.type == JointType::Revolute || j.type == JointType::Prismatic;
        if (boundedType && !j.bounds.has_value()) {
            throw inputInvalid(what + ".bounds：旋转/移动关节限位必填（§4.3.3）");
        }
        if (j.type == JointType::Continuous && j.bounds.has_value()) {
            throw inputInvalid(what + ".bounds：continuous 关节不携带限位（工作范围替代——MDL-12）");
        }
        if (j.bounds.has_value()) {
            const JointBounds& b = *j.bounds;
            if (!std::isfinite(b.lower) || !std::isfinite(b.upper)) {
                throw inputInvalid(what + ".bounds：限位含非有限值——NFR-COR-03");
            }
            if (!(b.lower < b.upper)) {
                throw inputInvalid(what + ".bounds：qmin≥qmax 非法（§4.3.3）");
            }
        }
        // 工作范围：仅 Continuous（§4.3.3"仅 Continuous"）。
        if (j.workingRange.has_value()) {
            if (j.type != JointType::Continuous) {
                throw inputInvalid(what + ".workingRange：仅 continuous 关节可有（§4.3.3）");
            }
            const WorkingRange& wr = *j.workingRange;
            if (!std::isfinite(wr.lower) || !std::isfinite(wr.upper)) {
                throw inputInvalid(what + ".workingRange：含非有限值（单位 rad）——MDL-12 有限区间");
            }
            if (!(wr.lower < wr.upper)) {
                throw inputInvalid(what + ".workingRange：须 qmin<qmax（MDL-12 有限区间）");
            }
        }
        // 限速/加速度（缺失＝能力缺失放行；Provided 须有限非负）。
        requireProvidedNonNegative(j.maxVelocity, what + ".maxVelocity");
        requireProvidedNonNegative(j.maxAcceleration, what + ".maxAcceleration");
        // 摩擦三元（Provided 须有限正值——§4.3.3 friction 行）。
        requireProvidedPositive(j.friction.viscous, what + ".friction.viscous");
        requireProvidedPositive(j.friction.coulomb, what + ".friction.coulomb");
        requireProvidedPositive(j.friction.bias, what + ".friction.bias");
    }

    // 逐连杆校验（数量已核对；物性 Provided 复核＋资源引用命中）。
    for (std::size_t i = 0; i < chain.links.size(); ++i) {
        CanonicalLink& l = chain.links.at(i);
        const std::string what = "chain.links[" + std::to_string(i) + "]";
        if (!l.objectId.isValid()) { throw inputInvalid(what + ".objectId：空 id"); }
        requireValidLocalName(l.localName, what + ".localName");
        requireProvidedPositive(l.mass, what + ".mass");
        if (l.centerOfMass.state() == core::FieldState::Provided) {
            const auto c = l.centerOfMass.tryValue();
            if (!c.has_value()) { throw inputInvalid(what + ".centerOfMass：状态异常"); }
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite((*c)(k))) {
                    throw inputInvalid(what + ".centerOfMass：含非有限分量（连杆系，单位 m）");
                }
            }
        }
        if (l.inertia.state() == core::FieldState::Provided) {
            const auto im = l.inertia.tryValue();
            if (!im.has_value()) { throw inputInvalid(what + ".inertia：状态异常"); }
            requireValidInertia(*im, what + ".inertia");
        }
        requireInManifest(l.visual, what + ".visual");
        requireInManifest(l.collision, what + ".collision");
    }

    // ---- 第 5 步：工具（§4.3.4）----
    for (std::size_t i = 0; i < tools.size(); ++i) {
        const CanonicalTool& t = tools.at(i);
        const std::string what = "tools[" + std::to_string(i) + "]";
        if (!t.objectId.isValid()) { throw inputInvalid(what + ".objectId：空 id"); }
        requireValidLocalName(t.localName, what + ".localName");
        requireProvidedPositive(t.mass, what + ".mass");
        if (t.centerOfMass.state() == core::FieldState::Provided) {
            const auto c = t.centerOfMass.tryValue();
            if (!c.has_value()) { throw inputInvalid(what + ".centerOfMass：状态异常"); }
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite((*c)(k))) {
                    throw inputInvalid(what + ".centerOfMass：含非有限分量（工具系，单位 m）");
                }
            }
        }
        if (t.inertia.state() == core::FieldState::Provided) {
            const auto im = t.inertia.tryValue();
            if (!im.has_value()) { throw inputInvalid(what + ".inertia：状态异常"); }
            requireValidInertia(*im, what + ".inertia");
        }
        requireValidTransform(t.tcpOffset, what + ".tcpOffset");
        requireInManifest(t.geometry, what + ".geometry");
    }

    // ---- 第 6 步：默认 TCP（§4.3.4 defaultTcp 行——有 tools 时必填且指向
    // tools 内项；无 tools 时必须为 nullopt）----
    if (tools.empty()) {
        if (m_defaultTcpIndex.has_value()) {
            throw inputInvalid("defaultTcpIndex：无工具时不得设置（§4.3.4）");
        }
    } else {
        if (!m_defaultTcpIndex.has_value()) {
            throw inputInvalid("defaultTcpIndex：有工具时必填（§4.3.4/KIN-14）");
        }
        if (*m_defaultTcpIndex >= tools.size()) {
            throw inputInvalid("defaultTcpIndex：越界（须指向 tools 内项——§4.3.4）");
        }
    }

    // ---- 第 7 步：场景对象（§4.3.4）----
    for (std::size_t i = 0; i < scene.size(); ++i) {
        const CanonicalSceneObject& s = scene.at(i);
        const std::string what = "scene[" + std::to_string(i) + "]";
        if (!s.objectId.isValid()) { throw inputInvalid(what + ".objectId：空 id"); }
        requireValidLocalName(s.localName, what + ".localName");
        // 世界系固连位姿（不得预乘安装旋转的检测在 S6/S9——此处拦值级非法）。
        requireValidTransform(s.worldPose, what + ".worldPose");
        requireInManifest(s.geometry, what + ".geometry");
    }

    // ---- 第 8 步：全模型 ObjectId 唯一性＋引用∈objectRefs（CM-0 值层面）----
    {
        std::vector<std::string> allIds;
        allIds.reserve(1 + chain.joints.size() + chain.links.size() + tools.size()
                       + scene.size());
        allIds.push_back(chain.robotObjectId.toCanonical());
        for (const CanonicalJoint& j : chain.joints) { allIds.push_back(j.objectId.toCanonical()); }
        for (const CanonicalLink& l : chain.links) { allIds.push_back(l.objectId.toCanonical()); }
        for (const CanonicalTool& t : tools) { allIds.push_back(t.objectId.toCanonical()); }
        for (const CanonicalSceneObject& s : scene) { allIds.push_back(s.objectId.toCanonical()); }
        std::sort(allIds.begin(), allIds.end());
        for (std::size_t i = 1; i < allIds.size(); ++i) {
            if (allIds[i - 1] == allIds[i]) {
                throw structureInvalid("全模型 ObjectId 重复（" + allIds[i]
                                       + "）——§4.3.3 唯一性约束");
            }
        }
        // 每个模型内对象都必须出现在修订闭包引用清单（objectRefs）中。
        std::vector<std::string> refIds;
        refIds.reserve(header.objectRefs.size());
        for (const ObjectRefEntry& e : header.objectRefs) { refIds.push_back(e.objectId.toCanonical()); }
        std::sort(refIds.begin(), refIds.end());
        for (const std::string& id : allIds) {
            if (!std::binary_search(refIds.begin(), refIds.end(), id)) {
                throw structureInvalid("对象 " + id + " 不在 header.objectRefs（CM-0 引用约束）");
            }
        }
    }

    // ---- 第 9 步：传动块（§4.3.4；病态/奇异判定归 S3——结构自洽复核）----
    if (!drivetrain.ratioPerJoint.empty() && drivetrain.ratioPerJoint.size() != chain.joints.size()) {
        throw inputInvalid("drivetrain.ratioPerJoint：长度须为 0 或 joints.size()（§4.3.4）");
    }
    for (std::size_t i = 0; i < drivetrain.ratioPerJoint.size(); ++i) {
        requireProvidedPositive(drivetrain.ratioPerJoint.at(i),
                                "drivetrain.ratioPerJoint[" + std::to_string(i) + "]");
    }
    if (drivetrain.coupling.has_value()) {
        const CouplingMatrix& c = *drivetrain.coupling;
        if (c.rows < 1 || c.rows != c.cols) {
            throw inputInvalid("drivetrain.coupling：须为方阵且阶 ≥1（MDL-21）");
        }
        if (c.c.size() != static_cast<std::size_t>(c.rows) * c.cols) {
            throw inputInvalid("drivetrain.coupling：扁平数据长度 != rows*cols（行主序）");
        }
        if (c.jointRange.count < 1 || c.jointRange.count != c.rows) {
            throw inputInvalid("drivetrain.coupling：方阵维度须等于适用关节数（MDL-21）");
        }
        if (static_cast<std::size_t>(c.jointRange.firstIndex) + c.jointRange.count
            > chain.joints.size()) {
            throw inputInvalid("drivetrain.coupling：适用关节范围越界（§4.3.4）");
        }
    }

    // ---- 第 10 步：诊断块（§4.3.5——error 级稳定码出现＝编译失败却试图发布）----
    for (const core::DiagnosticRecord& d : diagnostics) {
        if (isHardErrorDiagCode(d.code)) {
            throw inputInvalid("diagnostics：含 error 级稳定码 " + d.code
                               + "（§4.3.5——发布模型仅警告级，构造拒绝）");
        }
    }

    // ---- 第 11 步：确定性规范化（§4.5 集合稳定键）----
    // objectRefs/tools/scene 按 ObjectId 规范文本字典序；关节/连杆保持链序
    // （链序是语义序，不排序）。排序后模型存储序＝编码序——parse(encode(x))==x
    // 的全字段等值由此成立。
    sortAndRejectDuplicates(
        header.objectRefs, [](const ObjectRefEntry& e) { return e.objectId.toCanonical(); },
        "header.objectRefs");
    sortAndRejectDuplicates(
        tools, [](const CanonicalTool& t) { return t.objectId.toCanonical(); }, "tools");
    sortAndRejectDuplicates(
        scene, [](const CanonicalSceneObject& s) { return s.objectId.toCanonical(); }, "scene");

    // ---- 第 12 步：组装模型（friend 写入）＋索引＋能力派生 ----
    CanonicalModel model;
    model.m_header = std::move(header);
    model.m_world = std::move(world);
    model.m_chain = std::move(chain);
    model.m_tools = std::move(tools);
    model.m_defaultTcpIndex = m_defaultTcpIndex;
    model.m_scene = std::move(scene);
    model.m_drivetrain = std::move(drivetrain);
    model.m_resourceManifest = std::move(manifest);
    model.m_diagnostics = std::move(diagnostics);
    model.m_capabilities =
        deriveRuntimeCapability(model.m_chain, model.m_tools, model.m_scene, model.m_drivetrain);

    // 对象索引（§4.6-1）：Robot/Joint/Link/Tool/SceneObject/Resource 全量入
    // 索引——名称映射（RT-T05）与诊断定位的查询基础。
    model.m_objectIndex.emplace(model.m_chain.robotObjectId,
                                CanonicalModel::ObjectLocation{
                                    CanonicalModel::ObjectKind::Robot, 0});
    for (std::size_t i = 0; i < model.m_chain.joints.size(); ++i) {
        model.m_objectIndex.emplace(
            model.m_chain.joints[i].objectId,
            CanonicalModel::ObjectLocation{CanonicalModel::ObjectKind::Joint,
                                           static_cast<std::uint32_t>(i)});
    }
    for (std::size_t i = 0; i < model.m_chain.links.size(); ++i) {
        model.m_objectIndex.emplace(
            model.m_chain.links[i].objectId,
            CanonicalModel::ObjectLocation{CanonicalModel::ObjectKind::Link,
                                           static_cast<std::uint32_t>(i)});
    }
    for (std::size_t i = 0; i < model.m_tools.size(); ++i) {
        model.m_objectIndex.emplace(
            model.m_tools[i].objectId,
            CanonicalModel::ObjectLocation{CanonicalModel::ObjectKind::Tool,
                                           static_cast<std::uint32_t>(i)});
    }
    for (std::size_t i = 0; i < model.m_scene.size(); ++i) {
        model.m_objectIndex.emplace(
            model.m_scene[i].objectId,
            CanonicalModel::ObjectLocation{CanonicalModel::ObjectKind::SceneObject,
                                           static_cast<std::uint32_t>(i)});
    }
    // 资源索引（§4.6-3）：resourceId→清单下标；同时资源对象进入对象索引。
    for (std::size_t i = 0; i < model.m_resourceManifest.size(); ++i) {
        const ResourceRef& r = model.m_resourceManifest[i];
        model.m_resourceIndex.emplace(r.resourceId, static_cast<std::uint32_t>(i));
        model.m_objectIndex.emplace(
            r.resourceId,
            CanonicalModel::ObjectLocation{CanonicalModel::ObjectKind::Resource,
                                           static_cast<std::uint32_t>(i)});
    }

    // ---- 第 13 步：内容身份（§4.3.5——builder 计算非调用方申报）----
    // 摘要只经 core::ContentDigester（CR-02）；对什么字节做摘要（身份域编码、
    // 排除字段清单）声明在 Codec.hpp——本处只消费其纯函数。
    model.m_contentIdentity = rtcodec::computeContentIdentity(model);
    return model;
}

// =====================================================================
// 位模式等值（§4.3.6——身份域字段位模式级；诊断块 core 精确等值）。
// =====================================================================

bool CanonicalModel::operator==(const CanonicalModel& o) const noexcept
{
    // 身份与来源块：revisionSeq 不入身份域但属于模型字段——全字段等值包含
    // 它（两个仅修订序号不同的模型 operator== 不等、身份相等——RT-ID-3 钉住
    // 的正是这一区分）。
    const CanonicalModelHeader& a = m_header;
    const CanonicalModelHeader& b = o.m_header;
    if (!(a.project == b.project && a.branch == b.branch && a.revision == b.revision)) {
        return false;
    }
    if (a.revisionSeq != b.revisionSeq) { return false; }
    if (a.objectRefs.size() != b.objectRefs.size()) { return false; }
    for (std::size_t i = 0; i < a.objectRefs.size(); ++i) {
        const ObjectRefEntry& x = a.objectRefs[i];
        const ObjectRefEntry& y = b.objectRefs[i];
        if (!(x.objectId == y.objectId && x.contentVersion == y.contentVersion
              && x.objectTypeToken == y.objectTypeToken && x.digest == y.digest)) {
            return false;
        }
    }
    if (a.descriptionContractVersion != b.descriptionContractVersion
        || a.compilerContractVersion != b.compilerContractVersion
        || a.builtFrom != b.builtFrom) {
        return false;
    }

    // 世界块：T/gravity 位模式；installPreset 不入身份域但参与全字段等值
    // （token＋来源）。
    if (!transformEqual(m_world.T_world_base, o.m_world.T_world_base)) { return false; }
    if (!(m_world.installPreset == o.m_world.installPreset)) { return false; }
    if (!vecEqual(m_world.gravityWorld, o.m_world.gravityWorld)) { return false; }

    // 链块。
    if (!(m_chain.robotObjectId == o.m_chain.robotObjectId
          && m_chain.robotLocalName == o.m_chain.robotLocalName
          && m_chain.deviceName == o.m_chain.deviceName
          && m_chain.joints.size() == o.m_chain.joints.size()
          && m_chain.links.size() == o.m_chain.links.size())) {
        return false;
    }
    for (std::size_t i = 0; i < m_chain.joints.size(); ++i) {
        const CanonicalJoint& x = m_chain.joints[i];
        const CanonicalJoint& y = o.m_chain.joints[i];
        if (!(x.objectId == y.objectId && x.localName == y.localName && x.type == y.type
              && vecEqual(x.axis, y.axis) && transformEqual(x.origin, y.origin)
              && bitEqual(x.zeroOffset, y.zeroOffset))) {
            return false;
        }
        if (x.bounds.has_value() != y.bounds.has_value()) { return false; }
        if (x.bounds
            && !(bitEqual(x.bounds->lower, y.bounds->lower)
                 && bitEqual(x.bounds->upper, y.bounds->upper))) {
            return false;
        }
        if (x.workingRange.has_value() != y.workingRange.has_value()) { return false; }
        if (x.workingRange
            && !(bitEqual(x.workingRange->lower, y.workingRange->lower)
                 && bitEqual(x.workingRange->upper, y.workingRange->upper))) {
            return false;
        }
        if (!svDoubleEqual(x.maxVelocity, y.maxVelocity)
            || !svDoubleEqual(x.maxAcceleration, y.maxAcceleration)) {
            return false;
        }
        if (!svDoubleEqual(x.friction.viscous, y.friction.viscous)
            || !svDoubleEqual(x.friction.coulomb, y.friction.coulomb)
            || !svDoubleEqual(x.friction.bias, y.friction.bias)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < m_chain.links.size(); ++i) {
        const CanonicalLink& x = m_chain.links[i];
        const CanonicalLink& y = o.m_chain.links[i];
        if (!(x.objectId == y.objectId && x.localName == y.localName)) { return false; }
        if (x.visual.has_value() != y.visual.has_value()) { return false; }
        if (x.visual && !resourceIdentityEqual(*x.visual, *y.visual)) { return false; }
        if (x.collision.has_value() != y.collision.has_value()) { return false; }
        if (x.collision && !resourceIdentityEqual(*x.collision, *y.collision)) { return false; }
        if (!svDoubleEqual(x.mass, y.mass) || !svVecEqual(x.centerOfMass, y.centerOfMass)
            || !svInertiaEqual(x.inertia, y.inertia)) {
            return false;
        }
    }

    // 工具/默认 TCP/场景/传动/资源清单。
    if (m_tools.size() != o.m_tools.size()) { return false; }
    for (std::size_t i = 0; i < m_tools.size(); ++i) {
        const CanonicalTool& x = m_tools[i];
        const CanonicalTool& y = o.m_tools[i];
        if (!(x.objectId == y.objectId && x.localName == y.localName)) { return false; }
        if (x.geometry.has_value() != y.geometry.has_value()) { return false; }
        if (x.geometry && !resourceIdentityEqual(*x.geometry, *y.geometry)) { return false; }
        if (!svDoubleEqual(x.mass, y.mass) || !svVecEqual(x.centerOfMass, y.centerOfMass)
            || !svInertiaEqual(x.inertia, y.inertia)
            || !transformEqual(x.tcpOffset, y.tcpOffset)) {
            return false;
        }
    }
    if (!(m_defaultTcpIndex == o.m_defaultTcpIndex)) { return false; }
    if (m_scene.size() != o.m_scene.size()) { return false; }
    for (std::size_t i = 0; i < m_scene.size(); ++i) {
        const CanonicalSceneObject& x = m_scene[i];
        const CanonicalSceneObject& y = o.m_scene[i];
        if (!(x.objectId == y.objectId && x.localName == y.localName
              && transformEqual(x.worldPose, y.worldPose)
              && resourceIdentityEqual(x.geometry, y.geometry))) {
            return false;
        }
    }
    if (m_drivetrain.ratioPerJoint.size() != o.m_drivetrain.ratioPerJoint.size()) { return false; }
    for (std::size_t i = 0; i < m_drivetrain.ratioPerJoint.size(); ++i) {
        if (!svDoubleEqual(m_drivetrain.ratioPerJoint[i], o.m_drivetrain.ratioPerJoint[i])) {
            return false;
        }
    }
    if (m_drivetrain.coupling.has_value() != o.m_drivetrain.coupling.has_value()) { return false; }
    if (m_drivetrain.coupling) {
        const CouplingMatrix& x = *m_drivetrain.coupling;
        const CouplingMatrix& y = *o.m_drivetrain.coupling;
        if (x.rows != y.rows || x.cols != y.cols || x.c.size() != y.c.size()) { return false; }
        for (std::size_t i = 0; i < x.c.size(); ++i) {
            if (!bitEqual(x.c[i], y.c[i])) { return false; }
        }
        if (x.jointRange.firstIndex != y.jointRange.firstIndex
            || x.jointRange.count != y.jointRange.count) {
            return false;
        }
        // 条件数申报值：optional 状态＋位模式（申报值不入合法性判定——S3 重算）。
        if (x.conditionNumber.has_value() != y.conditionNumber.has_value()) { return false; }
        if (x.conditionNumber && !bitEqual(*x.conditionNumber, *y.conditionNumber)) {
            return false;
        }
    }
    if (m_resourceManifest.size() != o.m_resourceManifest.size()) { return false; }
    for (std::size_t i = 0; i < m_resourceManifest.size(); ++i) {
        const ResourceRef& x = m_resourceManifest[i];
        const ResourceRef& y = o.m_resourceManifest[i];
        // 清单条目按全字段比较（清单本身是承载面——state/accessVersion 参与）。
        if (!(x.resourceId == y.resourceId && x.contentDigest == y.contentDigest
              && x.sourcePathHint == y.sourcePathHint && x.state == y.state
              && x.accessVersion == y.accessVersion)) {
            return false;
        }
    }

    // 诊断块（不入身份域——core 精确等值即可；位级差异无身份影响，D-12）。
    if (!(m_diagnostics == o.m_diagnostics)) { return false; }

    // 能力块（派生投影——逐字段等值）。
    const RuntimeCapability& ca = m_capabilities;
    const RuntimeCapability& cb = o.m_capabilities;
    if (ca.hasWorkCell != cb.hasWorkCell || ca.hasDynamicWorkCell != cb.hasDynamicWorkCell
        || ca.hasFullMassInertia != cb.hasFullMassInertia
        || ca.hasJointVelocityLimits != cb.hasJointVelocityLimits
        || ca.hasCollisionGeometry != cb.hasCollisionGeometry || ca.hasTools != cb.hasTools
        || ca.hasScene != cb.hasScene || ca.hasFrictionModel != cb.hasFrictionModel
        || ca.hasCouplingMatrix != cb.hasCouplingMatrix
        || ca.hasBidirectionalNameMap != cb.hasBidirectionalNameMap
        || ca.jointTypesPresent != cb.jointTypesPresent) {
        return false;
    }

    // 内容身份（派生自身份域——位模式等值）。
    return m_contentIdentity == o.m_contentIdentity;
}

}  // namespace sdurws::ird::runtime
