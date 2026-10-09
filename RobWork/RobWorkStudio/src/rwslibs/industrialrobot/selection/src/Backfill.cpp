/**
 * @file   Backfill.cpp
 * @brief  器件回填实现（SEL-10）——物性合成内核（§12.4）、命令/记录
 *         载荷 canonical 编解码、回填数据组装、DeviceBackfillCommandHandler。
 *
 * 设计依据：
 *   - units/selection.md §12（回填时序 S1～S6／纪律九条／命令 token／
 *     物性合成规则——本文件全部函数的语义唯一权威）、§14.8（处理器
 *     契约——prepare 判定在处理器，P-PR-3）、§14.0（错误二分与通用
 *     约定）；公共面契约见 Backfill.hpp 文件头（依赖形态登记＋回填记录
 *     对象与 modeling robot-drivetrain 的关系登记——P-SEL-7）。
 *   - 需求 SEL-10/MDL-16（合成规则：按明确参考系合成质量/质心/惯量；
 *     壳体质量与转子等效惯性区分、禁止重复计入）、MDL-05（平行轴规则
 *     同源）、MDL-06（断言①～③：m>0、SPD、三角不等式）、AT-30（新
 *     修订＋复算提示；复核前不沿用原通过结论）。
 *   - units/project.md §5.3/§6.3（ICommandHandler 三态语义——Planned/
 *     RejectedHardAssert/RejectedInvalidInput 的映射职责在处理器；稳定
 *     码 PRJ-STALE-REVISION-REJECTED 等由命令服务产出，selection 不复述）。
 *
 * 确定性（NFR-COR-02）：全部函数纯计算——零环境量/时钟/地址依赖；同
 * 输入恒同输出。浮点按 IEEE754 位模式进入 canonical 字节（非文本——
 * project.md §4.8 canonical 规则同语义）。
 *
 * 线程安全：全部函数无共享可变状态、可重入；处理器实例无状态。
 */

#include <sdurws/ird/selection/Backfill.hpp>

#include <sdurws/ird/selection/DiagCodes.hpp>  // SEL-BACKFILL-* 码常量（唯一书写点）

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sdurws::ird::selection {

namespace {

// =====================================================================
// 局部小工具（文件内——零公共面污染）
// =====================================================================

/// 非有限判定（NFR-COR-03：NaN/±Inf 一律拒绝，不静默置零）。
bool isFiniteNumber(double v) noexcept
{
    return std::isfinite(v);
}

/// 三分量位置矢量非有限检查。
bool isFiniteVec3(const BackfillVec3& v) noexcept
{
    return isFiniteNumber(v.x) && isFiniteNumber(v.y) && isFiniteNumber(v.z);
}

/// 六分量惯量非有限检查。
bool isInertiaFinite(const BackfillInertiaTensor& t) noexcept
{
    return isFiniteNumber(t.ixx) && isFiniteNumber(t.iyy) && isFiniteNumber(t.izz)
        && isFiniteNumber(t.ixy) && isFiniteNumber(t.ixz) && isFiniteNumber(t.iyz);
}

/**
 * @brief 3×3 对称矩阵三特征值（解析闭式——数值稳定的主值法）。
 *
 * 数学依据：对称三阶矩阵的特征多项式为三次式，经移位 q=trace/3 后化为
 * 退化三次 t³−pt−r=0，三角解 t_k＝2√(p/3)·cos(φ−2πk/3)。本实现采用
 * Wikipedia "Eigenvalue algorithm#3×3 symmetric matrices" 的标准封闭式
 * （与 robotics 文献常用形式一致）；r 经 clamp(−1,1) 限幅抵抗舍入漂移。
 *
 * 精度口径：断言②③的容差语义（附录 D 第 6/7 项——相对 1×10⁻¹²）下
 * 解析式精度充分（黄金用例以解析可验证的各向同性/对角阵钉扎）。
 *
 * @param m [in] 行主序对称矩阵 {m00,m01,m02, m11,m12, m22}（六分量——
 *          对称性由存储形态保证）
 * @return 三特征值（升序 λ[0] ≤ λ[1] ≤ λ[2]）
 */
std::array<double, 3> symmetricEigenvalues3x3(
    double m00, double m01, double m02, double m11, double m12, double m22) noexcept
{
    // 移位量 q＝迹的三分之一（把谱心移到原点，退化三次式系数变小）。
    const double q = (m00 + m11 + m22) / 3.0;
    // p1＝非对角元平方和；p2＝对角偏移平方和＋2·p1（＝‖A−qE‖_F²/2 的等价形）。
    const double p1 = m01 * m01 + m02 * m02 + m12 * m12;
    const double p2 = (m00 - q) * (m00 - q) + (m11 - q) * (m11 - q)
                    + (m22 - q) * (m22 - q) + 2.0 * p1;
    // p＝0 ⇔ A 为纯量阵 qE（三特征值全等）——避免除零分支。
    if (p2 <= 0.0) {
        return {q, q, q};
    }
    const double p = std::sqrt(p2 / 6.0);
    // B＝(A−qE)/p；r＝det(B)/2（∈[−1,1]——限幅抵抗浮点舍入越界）。
    const double b00 = (m00 - q) / p, b01 = m01 / p, b02 = m02 / p;
    const double b11 = (m11 - q) / p, b12 = m12 / p;
    const double b22 = (m22 - q) / p;
    double r = (b00 * b11 * b22 + 2.0 * b01 * b02 * b12
                - b01 * b01 * b22 - b02 * b02 * b11 - b12 * b12 * b00) / 2.0;
    r = std::clamp(r, -1.0, 1.0);
    const double phi = std::acos(r) / 3.0;
    const double l1 = q + 2.0 * p * std::cos(phi);            // 最大特征值
    const double l3 = q + 2.0 * p * std::cos(phi + 2.0943951023931953); // +2π/3——最小
    const double l2 = 3.0 * q - l1 - l3;                      // 迹守恒求中间值
    return {l3, l2, l1};  // 升序返回（l3 ≤ l2 ≤ l1——三角解的性质）
}

// =====================================================================
// canonical 字节编解码原语（小端定宽——CombinationCheck.cpp 同款协议
// 风格；本文件复制而非共享：单元内不设第二实现头，协议面各自冻结）
// =====================================================================

/// 字节写出器（编码侧——追加式；容量自管理）。
class ByteWriter {
public:
    /// 追加原始字节。
    void raw(const void* data, std::size_t n)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        m_bytes.insert(m_bytes.end(), p, p + n);
    }
    /// u32 定宽（小端——协议字序）。
    void u32(std::uint32_t v) { raw(&v, sizeof(v)); }
    /// u8（枚举底值/有无标志）。
    void u8(std::uint8_t v) { raw(&v, sizeof(v)); }
    /// f64 定宽（小端 IEEE754 位模式；调用方保证有限——编码入口已校验）。
    void f64(double v) { raw(&v, sizeof(v)); }
    /// string＝u32 字节长＋UTF-8 字节。
    void str(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    /// 16 字节 ID 原始字节。
    void id128(const core::ObjectId& id) { raw(id.bytes.data(), id.bytes.size()); }
    /// 32 字节摘要原始字节（ContentVersion/ContentIdentity 同构 Digest256）。
    void digest32(const core::ContentVersion& c) { raw(c.bytes.data(), c.bytes.size()); }

    /// 惯量六分量（声明序——协议冻结面）。
    void inertia(const BackfillInertiaTensor& t)
    {
        f64(t.ixx);
        f64(t.iyy);
        f64(t.izz);
        f64(t.ixy);
        f64(t.ixz);
        f64(t.iyz);
    }
    /// 三维位置（声明序）。
    void vec3(const BackfillVec3& v)
    {
        f64(v.x);
        f64(v.y);
        f64(v.z);
    }

    std::vector<std::uint8_t> m_bytes; ///< 累积字节（编码产物）
};

/**
 * @brief 字节读出器（解码侧）。
 *
 * 与 CombinationCheck.cpp 解码器的差异（诚实登记）：回填载荷来自①端口
 * 外部通道（可能截断/篡改/异版），解码失败必须走**查询轨**（返回
 * Status 而非抛异常——§14.0 数据侧错误走返回值；且本文件解码失败要
 * 逐项定位 SEL-BACKFILL-* 码，异常文本无法承载码值）。因此本类以
 * fail(reason) 记录首个失败原因并置 failFlag，读取器各方法在失败态
 * 返回零值——调用方在协议尾检查 failFlag 统一归类。
 */
class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

    /// 头部校验：8 字节 magic＋u32 codec 版本。版本不符＝版本轨（升级
    /// 指引面），形态不符＝结构轨。
    void expectHeader(std::string_view magic, std::uint32_t version)
    {
        if (m_bytes.size() < 8 + sizeof(std::uint32_t)
            || std::memcmp(m_bytes.data(), magic.data(), 8) != 0) {
            fail("magic 头不符（期望 " + std::string(magic) + "——字节形态违约）");
            return;
        }
        const std::uint32_t v = u32At(8);
        if (v != version) {
            fail("codec 版本不符（实际 " + std::to_string(v) + "，期望 "
                 + std::to_string(version) + "）");
            m_versionMismatch = true;
            return;
        }
        m_pos = 8 + sizeof(std::uint32_t);
    }

    void raw(void* out, std::size_t n)
    {
        if (m_failFlag || !require(n)) { return; }
        std::memcpy(out, m_bytes.data() + m_pos, n);
        m_pos += n;
    }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        raw(&v, sizeof(v));
        return v;
    }
    std::uint8_t u8()
    {
        std::uint8_t v = 0;
        raw(&v, sizeof(v));
        return v;
    }
    /// f64 读取＋非有限拒绝（对称于编码入口——防旁路字节注入）。
    double f64()
    {
        double v = 0;
        raw(&v, sizeof(v));
        if (!m_failFlag && !isFiniteNumber(v)) {
            fail("非有限 double（NFR-COR-03）");
        }
        return v;
    }
    std::string str()
    {
        const std::uint32_t n = u32();
        if (m_failFlag) { return {}; }
        if (!require(n)) { return {}; }
        // 长度上限防御（1 MiB）：协议字段均为短文本，超长＝畸形字节——
        // 防失控分配（解码不可信输入的资源纪律）。
        constexpr std::uint32_t kMaxStringBytes = 1024u * 1024u;
        if (n > kMaxStringBytes) {
            fail("字符串字段超长（" + std::to_string(n) + " 字节——畸形载荷）");
            return {};
        }
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_pos), n);
        m_pos += n;
        return s;
    }
    core::ObjectId id128()
    {
        core::ObjectId id;
        raw(id.bytes.data(), id.bytes.size());
        return id;
    }
    core::ContentVersion digest32()
    {
        core::ContentVersion c;
        raw(c.bytes.data(), c.bytes.size());
        return c;
    }
    BackfillInertiaTensor inertia()
    {
        BackfillInertiaTensor t;
        t.ixx = f64();
        t.iyy = f64();
        t.izz = f64();
        t.ixy = f64();
        t.ixz = f64();
        t.iyz = f64();
        return t;
    }
    BackfillVec3 vec3()
    {
        BackfillVec3 v;
        v.x = f64();
        v.y = f64();
        v.z = f64();
        return v;
    }

    /// 尾部残余检查（残余字节＝编码/解码面不对齐——协议违约）。
    void expectEnd()
    {
        if (!m_failFlag && m_pos != m_bytes.size()) {
            fail("字节残余 " + std::to_string(m_bytes.size() - m_pos)
                 + "（编码形态不对齐——协议违约）");
        }
    }

    /// 失败态与原因（调用方在解码完成后统一检查）。
    [[nodiscard]] bool failed() const noexcept { return m_failFlag; }
    [[nodiscard]] bool versionMismatch() const noexcept { return m_versionMismatch; }
    [[nodiscard]] const std::string& reason() const noexcept { return m_reason; }

private:
    /// 越界保护（读取前调用——失败记录后停止推进，不静默截断）。
    bool require(std::size_t n)
    {
        if (m_failFlag) { return false; }
        if (m_pos + n > m_bytes.size()) {
            fail("字节越界（需 " + std::to_string(n) + "，剩余 "
                 + std::to_string(m_bytes.size() - m_pos) + "）");
            return false;
        }
        return true;
    }
    /// 记录首个失败原因（后续读取短路——不覆盖首个定位）。
    void fail(std::string reason)
    {
        if (!m_failFlag) {
            m_failFlag = true;
            m_reason = std::move(reason);
        }
    }
    std::uint32_t u32At(std::size_t pos) const
    {
        std::uint32_t v = 0;
        std::memcpy(&v, m_bytes.data() + pos, sizeof(v));
        return v;
    }

    const std::vector<std::uint8_t>& m_bytes; ///< 源字节（调用方持有）
    std::size_t m_pos = 0;                    ///< 读游标
    bool m_failFlag = false;                  ///< 首失败后置位
    bool m_versionMismatch = false;           ///< 版本轨失败（区别于结构轨）
    std::string m_reason;                     ///< 首失败原因
};

// ---- 逐轴条目编码/解码（命令载荷与记录对象共用同一字段序——协议
//      一致性：两协议的轴段字段序相同，仅外层包装不同）----

/// 逐轴条目编码（声明序——身份/目录引用/安装关系/传动比/连杆原值/
/// 电机壳体/减速器壳体/转子惯量）。
void writeAxisEntry(ByteWriter& w, const AxisBackfillEntry& a)
{
    w.id128(a.jointId);
    w.str(a.catalogId);
    w.str(a.catalogVersion);
    w.str(a.motorModelId);
    w.str(a.gearboxModelId);
    w.str(a.mountKind);
    w.id128(a.catalogLockObject);
    w.digest32(a.catalogLockVersion);
    w.f64(a.appliedRatio);
    w.f64(a.linkMassKg);
    w.vec3(a.linkComM);
    w.inertia(a.linkInertia);
    w.vec3(a.motorComAnchorM);
    w.f64(a.motorHousingMassKg);
    w.inertia(a.motorHousingInertia);
    w.vec3(a.gearboxComAnchorM);
    w.f64(a.gearboxHousingMassKg);
    w.inertia(a.gearboxHousingInertia);
    w.f64(a.rotorInertiaKgM2);
}

/// 逐轴条目解码（与 writeAxisEntry 字段序严格镜像——协议一致性）。
AxisBackfillEntry readAxisEntry(ByteReader& r)
{
    AxisBackfillEntry a;
    a.jointId = r.id128();
    a.catalogId = r.str();
    a.catalogVersion = r.str();
    a.motorModelId = r.str();
    a.gearboxModelId = r.str();
    a.mountKind = r.str();
    a.catalogLockObject = r.id128();
    a.catalogLockVersion = r.digest32();
    a.appliedRatio = r.f64();
    a.linkMassKg = r.f64();
    a.linkComM = r.vec3();
    a.linkInertia = r.inertia();
    a.motorComAnchorM = r.vec3();
    a.motorHousingMassKg = r.f64();
    a.motorHousingInertia = r.inertia();
    a.gearboxComAnchorM = r.vec3();
    a.gearboxHousingMassKg = r.f64();
    a.gearboxHousingInertia = r.inertia();
    a.rotorInertiaKgM2 = r.f64();
    return a;
}

// =====================================================================
// 合成断言（MDL-06①～③同语义——§12.4 断言行；黄金用例钉扎的判定面）
// =====================================================================

/**
 * @brief 合成断言检查（第 5 步——三断言按序判定，首个失败即返回）。
 *
 * @param massKg [in] 合成质量，kg
 * @param t      [in] 合成惯量（绕合成质心、连杆系姿态），kg·m²
 * @return ok=true＝三断言全过；否则 failure/detail 携带分类与数值定位
 */
SynthesisOutcome checkSynthesisAssertions(double massKg, const BackfillInertiaTensor& t)
{
    SynthesisOutcome o;
    // 断言①：合成质量严格为正（kg）——m>0（MDL-06①；质量守恒下求和
    // 恒正，此处为防线纵深——输入侧异常穿透时就地拦截）。
    if (!(massKg > 0.0)) {
        o.failure = SynthesisFailure::MassNonPositive;
        o.detail = "合成质量非正（m=" + std::to_string(massKg)
                 + " kg——MDL-06 断言①违约）";
        return o;
    }
    // 断言②：SPD——绕合成质心的三主惯量（特征值）严格>0（MDL-06②；
    // 附录 D 第 7 项"SPD 特征值严格>0"）。特征值经对称解析式求得。
    const auto eig = symmetricEigenvalues3x3(t.ixx, t.ixy, t.ixz, t.iyy, t.iyz, t.izz);
    if (!(eig[0] > 0.0)) {
        o.failure = SynthesisFailure::NotPositiveDefinite;
        o.detail = "合成惯量非正定（λmin=" + std::to_string(eig[0])
                 + " kg·m²——MDL-06 断言②违约）";
        return o;
    }
    // 断言③：三角不等式——升序特征值满足 λ1+λ2 ≥ λ3（允许等号＝平面
    // 薄板极限；相对容差 1×10⁻¹²——附录 D 第 6 项对称性同源的舍入余量：
    // 差值低于 max|λ|×1×10⁻¹² 视为数值零）。
    const double sumTol = std::max({std::abs(eig[0]), std::abs(eig[1]), std::abs(eig[2])})
                        * 1e-12;
    if (eig[0] + eig[1] - eig[2] < -sumTol) {
        o.failure = SynthesisFailure::TriangleInequality;
        o.detail = "合成惯量三角不等式违约（λ1+λ2−λ3=" + std::to_string(eig[0] + eig[1] - eig[2])
                 + " kg·m²——MDL-06 断言③违约）";
        return o;
    }
    o.ok = true;
    return o;
}

/// 平行轴迁移的秩一修正项 m·(|d|²E₃ − d·dᵀ)（d＝质心位移，m；m＝质量 kg
/// ——返回对六分量的增量；MDL-05 平行轴规则同源）。
BackfillInertiaTensor parallelAxisContribution(double massKg, const BackfillVec3& d)
{
    // |d|²（m²）——标量距离平方。
    const double d2 = d.x * d.x + d.y * d.y + d.z * d.z;
    BackfillInertiaTensor t;
    t.ixx = massKg * (d2 - d.x * d.x);   // m(dy²+dz²)
    t.iyy = massKg * (d2 - d.y * d.y);   // m(dx²+dz²)
    t.izz = massKg * (d2 - d.z * d.z);   // m(dx²+dy²)
    t.ixy = -massKg * d.x * d.y;         // −m·dx·dy（惯性积负积分约定）
    t.ixz = -massKg * d.x * d.z;
    t.iyz = -massKg * d.y * d.z;
    return t;
}

/// 六分量张量加法。
BackfillInertiaTensor addInertia(const BackfillInertiaTensor& a,
                                 const BackfillInertiaTensor& b)
{
    BackfillInertiaTensor t;
    t.ixx = a.ixx + b.ixx;
    t.iyy = a.iyy + b.iyy;
    t.izz = a.izz + b.izz;
    t.ixy = a.ixy + b.ixy;
    t.ixz = a.ixz + b.ixz;
    t.iyz = a.iyz + b.iyz;
    return t;
}

}  // namespace

// =====================================================================
// 复算提示（AT-30）
// =====================================================================

std::string_view recalcDomainToken(RecalcDomain domain) noexcept
{
    switch (domain) {
    case RecalcDomain::Kinematics:   return "kinematics";
    case RecalcDomain::Dynamics:     return "dynamics";
    case RecalcDomain::Selection:    return "selection";
    case RecalcDomain::Optimization: return "optimization";
    }
    return {};  // 词表外值（防御分支——switch 全覆盖后不可达）
}

// =====================================================================
// 物性合成内核（§12.4——唯一实现点；P-SEL-6 裁决前自持＋黄金钉住）
// =====================================================================

SynthesisOutcome synthesizeAxisBodyProperties(const AxisBackfillEntry& axis)
{
    SynthesisOutcome o;

    // ---- 第 1 步：输入卫生检查（NFR-COR-03——非有限/越界即拒绝）----
    // 质量组：连杆/两壳体质量必须>0 且有限（kg）；转子惯量独立字段同样
    // 校验（>0 kg·m²——虽不进合成，但作为登记值同样不得非法）。
    if (!isFiniteNumber(axis.linkMassKg) || !isFiniteNumber(axis.motorHousingMassKg)
        || !isFiniteNumber(axis.gearboxHousingMassKg)
        || !isFiniteNumber(axis.rotorInertiaKgM2)
        || !isFiniteNumber(axis.appliedRatio)) {
        o.failure = SynthesisFailure::NonFiniteInput;
        o.detail = "回填输入含非有限标量（质量/传动比/转子惯量——NFR-COR-03）";
        return o;
    }
    if (!isFiniteVec3(axis.linkComM) || !isFiniteVec3(axis.motorComAnchorM)
        || !isFiniteVec3(axis.gearboxComAnchorM)) {
        o.failure = SynthesisFailure::NonFiniteInput;
        o.detail = "回填输入含非有限位置矢量（质心/锚点——NFR-COR-03）";
        return o;
    }
    if (!isInertiaFinite(axis.linkInertia) || !isInertiaFinite(axis.motorHousingInertia)
        || !isInertiaFinite(axis.gearboxHousingInertia)) {
        o.failure = SynthesisFailure::NonFiniteInput;
        o.detail = "回填输入含非有限惯量分量（NFR-COR-03）";
        return o;
    }
    if (axis.linkMassKg <= 0.0 || axis.motorHousingMassKg <= 0.0
        || axis.gearboxHousingMassKg <= 0.0) {
        o.failure = SynthesisFailure::RangeInvalid;
        o.detail = "部件质量必须>0（连杆=" + std::to_string(axis.linkMassKg)
                 + "，电机壳体=" + std::to_string(axis.motorHousingMassKg)
                 + "，减速器壳体=" + std::to_string(axis.gearboxHousingMassKg)
                 + " kg）";
        return o;
    }
    if (axis.rotorInertiaKgM2 <= 0.0) {
        o.failure = SynthesisFailure::RangeInvalid;
        o.detail = "转子等效惯量必须>0（" + std::to_string(axis.rotorInertiaKgM2)
                 + " kg·m²——独立登记字段同样不得非法）";
        return o;
    }

    // ---- 第 2 步：各部件惯量向连杆坐标系原点平行轴迁移 ----
    // I_i^O ＝ I_i ＋ m_i·(|c_i|²E₃ − c_i·c_iᵀ)（c_i 为部件质心在连杆系
    // 下的位置；连杆原值自身惯量已是质心基准——BodyData 同语义）。
    const BackfillInertiaTensor linkShifted =
        addInertia(axis.linkInertia,
                   parallelAxisContribution(axis.linkMassKg, axis.linkComM));
    const BackfillInertiaTensor motorShifted =
        addInertia(axis.motorHousingInertia,
                   parallelAxisContribution(axis.motorHousingMassKg, axis.motorComAnchorM));
    const BackfillInertiaTensor gearboxShifted =
        addInertia(axis.gearboxHousingInertia,
                   parallelAxisContribution(axis.gearboxHousingMassKg, axis.gearboxComAnchorM));

    // ---- 第 3 步：求和（质量/质心/原点系惯量）----
    // 质心按质量加权（§12.4"质心按质量加权（平行轴迁移）"）。
    const double totalMass = axis.linkMassKg + axis.motorHousingMassKg
                           + axis.gearboxHousingMassKg;
    BackfillVec3 com;
    com.x = (axis.linkMassKg * axis.linkComM.x
             + axis.motorHousingMassKg * axis.motorComAnchorM.x
             + axis.gearboxHousingMassKg * axis.gearboxComAnchorM.x) / totalMass;
    com.y = (axis.linkMassKg * axis.linkComM.y
             + axis.motorHousingMassKg * axis.motorComAnchorM.y
             + axis.gearboxHousingMassKg * axis.gearboxComAnchorM.y) / totalMass;
    com.z = (axis.linkMassKg * axis.linkComM.z
             + axis.motorHousingMassKg * axis.motorComAnchorM.z
             + axis.gearboxHousingMassKg * axis.gearboxComAnchorM.z) / totalMass;
    const BackfillInertiaTensor originInertia =
        addInertia(addInertia(linkShifted, motorShifted), gearboxShifted);

    // ---- 第 4 步：向合成质心回迁 ----
    // I_syn ＝ I^O − M·(|c|²E₃ − c·cᵀ)——结果即"绕合成质心、连杆系姿态"
    // 的惯量张量（BodyData 消费语义）。
    const BackfillInertiaTensor backShift =
        parallelAxisContribution(totalMass, com);
    AxisSynthesis syn;
    syn.massKg = totalMass;
    syn.comM = com;
    syn.inertia.ixx = originInertia.ixx - backShift.ixx;
    syn.inertia.iyy = originInertia.iyy - backShift.iyy;
    syn.inertia.izz = originInertia.izz - backShift.izz;
    syn.inertia.ixy = originInertia.ixy - backShift.ixy;
    syn.inertia.ixz = originInertia.ixz - backShift.ixz;
    syn.inertia.iyz = originInertia.iyz - backShift.iyz;
    // 转子等效惯量独立登记（§12.2 纪律 5——不进上面任何合成分量；此处
    // 仅作记录面透传，映射侧显式计入的唯一位置在 drivetrain）。
    syn.rotorInertiaKgM2 = axis.rotorInertiaKgM2;

    // ---- 第 5 步：MDL-06 断言①～③同语义检查 ----
    const SynthesisOutcome check = checkSynthesisAssertions(totalMass, syn.inertia);
    if (!check.ok) {
        return check;  // 断言失败即整体拒绝（回填失败零修订——§12.4 断言行）
    }

    o.ok = true;
    o.failure = SynthesisFailure::NonFiniteInput;  // ok 态下无语义（默认值占位）
    o.synthesis = syn;
    o.detail = "合成完成：m=" + std::to_string(totalMass)
             + " kg（连杆＋电机壳体＋减速器壳体；转子惯量独立登记未计入）";
    return o;
}

// =====================================================================
// 命令载荷 canonical 编解码（IRDSBFP1）
// =====================================================================

std::vector<std::uint8_t> encodeBackfillPayload(const DeviceBackfillRequest& request)
{
    // 编码入口校验（调用方错误 fail-fast——§14.0）：空轴表/重复轴/非法
    // 身份/参考系词表外/非有限值在此快速失败。
    if (request.axes.empty()) {
        throw std::invalid_argument("回填载荷编码：轴表为空（回填命令至少一条轴）");
    }
    if (request.referenceFrameToken != kBackfillFrameLink) {
        throw std::invalid_argument("回填载荷编码：参考系词表外（期望 "
                                    + std::string(kBackfillFrameLink) + "）");
    }
    for (std::size_t i = 0; i < request.axes.size(); ++i) {
        const AxisBackfillEntry& a = request.axes[i];
        if (!a.jointId.isValid()) {
            throw std::invalid_argument("回填载荷编码：第 " + std::to_string(i)
                                        + " 轴 jointId 为保留值（全零）");
        }
        for (std::size_t j = i + 1; j < request.axes.size(); ++j) {
            if (request.axes[j].jointId == a.jointId) {
                throw std::invalid_argument("回填载荷编码：轴重复（index "
                                            + std::to_string(i) + " 与 "
                                            + std::to_string(j) + " 同 jointId）");
            }
        }
    }
    // 非有限值卫生检查（与合成内核同一口径——编码面不落非法字节）。
    for (const AxisBackfillEntry& a : request.axes) {
        const SynthesisOutcome sanity = synthesizeAxisBodyProperties(a);
        if (sanity.ok) { continue; }
        // 合成未过≠编码必然失败（断言失败是数据语义问题，编码只挡"非
        // 有限/范围"类）——此处仅复核非有限类与范围类，断言类留给 prepare。
        const bool nonFiniteOrRange =
            sanity.failure == SynthesisFailure::NonFiniteInput
            || sanity.failure == SynthesisFailure::RangeInvalid;
        if (nonFiniteOrRange) {
            throw std::invalid_argument("回填载荷编码：" + sanity.detail);
        }
        // 断言类失败（质量非正已由范围挡住；SPD/三角由合成数据决定）——
        // 编码继续，由 prepare 硬断言轨统一拒绝（单一拒绝面）。
    }

    ByteWriter w;
    w.raw(kBackfillPayloadMagic.data(), 8);
    w.u32(kBackfillPayloadCodecVersion);
    w.str(request.referenceFrameToken);
    w.u32(static_cast<std::uint32_t>(request.axes.size()));
    for (const AxisBackfillEntry& a : request.axes) {
        writeAxisEntry(w, a);  // 声明序编码（不重排——提交序即记录序）
    }
    return std::move(w.m_bytes);
}

DecodedBackfillPayload decodeBackfillPayload(const std::vector<std::uint8_t>& bytes)
{
    DecodedBackfillPayload out;
    ByteReader r(bytes);
    r.expectHeader(kBackfillPayloadMagic, kBackfillPayloadCodecVersion);
    if (r.failed()) {
        out.status = r.versionMismatch() ? DecodedBackfillPayload::Status::UnsupportedVersion
                                         : DecodedBackfillPayload::Status::Malformed;
        out.detail = r.reason();
        return out;
    }
    out.request.referenceFrameToken = r.str();
    const std::uint32_t axisCount = r.u32();
    // 轴数上限防御：协议轴段定宽（约 250 字节/轴），1 MiB 载荷对应 ~4200
    // 轴——超上限＝畸形载荷（防失控分配；真实机械臂轴数 ≤10 量级）。
    constexpr std::uint32_t kMaxAxes = 4096;
    if (axisCount > kMaxAxes) {
        out.status = DecodedBackfillPayload::Status::Malformed;
        out.detail = "轴数超上限（" + std::to_string(axisCount) + "）";
        return out;
    }
    for (std::uint32_t i = 0; i < axisCount; ++i) {
        out.request.axes.push_back(readAxisEntry(r));
    }
    r.expectEnd();
    if (r.failed()) {
        out.status = DecodedBackfillPayload::Status::Malformed;
        out.detail = r.reason();
        return out;
    }
    // 解码面词表/身份复核（与编码入口对称——防旁路字节注入）。
    if (out.request.referenceFrameToken != kBackfillFrameLink) {
        out.status = DecodedBackfillPayload::Status::Malformed;
        out.detail = "参考系词表外：" + out.request.referenceFrameToken;
        return out;
    }
    for (std::size_t i = 0; i < out.request.axes.size(); ++i) {
        if (!out.request.axes[i].jointId.isValid()) {
            out.status = DecodedBackfillPayload::Status::Malformed;
            out.detail = "第 " + std::to_string(i) + " 轴 jointId 为保留值";
            return out;
        }
        for (std::size_t j = i + 1; j < out.request.axes.size(); ++j) {
            if (out.request.axes[j].jointId == out.request.axes[i].jointId) {
                out.status = DecodedBackfillPayload::Status::Malformed;
                out.detail = "轴重复（index " + std::to_string(i) + " 与 "
                           + std::to_string(j) + "）";
                return out;
            }
        }
    }
    out.status = DecodedBackfillPayload::Status::Ok;
    return out;
}

// =====================================================================
// 回填记录对象 canonical 编解码（IRDSBFV1）
// =====================================================================

std::vector<std::uint8_t> encodeBackfillRecordObject(const BackfillRecordObject& record)
{
    // 编码入口校验（调用方错误 fail-fast）：记录必须非空且两表等长——
    // 合成结果与条目一一对应是记录语义的结构前提。
    if (record.axes.empty() || record.synthesis.size() != record.axes.size()) {
        throw std::invalid_argument(
            "回填记录编码：synthesis 与 axes 必须非空且等长（实际 "
            + std::to_string(record.synthesis.size()) + "/"
            + std::to_string(record.axes.size()) + "）");
    }
    if (record.referenceFrameToken != kBackfillFrameLink) {
        throw std::invalid_argument("回填记录编码：参考系词表外（期望 "
                                    + std::string(kBackfillFrameLink) + "）");
    }
    for (std::size_t i = 0; i < record.axes.size(); ++i) {
        if (!record.axes[i].jointId.isValid()) {
            throw std::invalid_argument("回填记录编码：第 " + std::to_string(i)
                                        + " 轴 jointId 为保留值");
        }
        for (std::size_t j = i + 1; j < record.axes.size(); ++j) {
            if (record.axes[j].jointId == record.axes[i].jointId) {
                throw std::invalid_argument("回填记录编码：轴重复（index "
                                            + std::to_string(i) + " 与 "
                                            + std::to_string(j) + "）");
            }
        }
    }

    ByteWriter w;
    w.raw(kBackfillRecordMagic.data(), 8);
    w.u32(kBackfillPayloadCodecVersion);  // 记录与命令同版本演进（同批冻结 v1）
    w.str(record.referenceFrameToken);
    // 复算提示：四域位图（bit0..3＝Kinematics..Optimization）＋不沿用标志。
    std::uint8_t domainBits = 0;
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        if (record.recalc.domains[i]) {
            domainBits |= static_cast<std::uint8_t>(1u << i);
        }
    }
    w.u8(domainBits);
    w.u8(record.recalc.retainPriorConclusion ? 1 : 0);
    w.u32(static_cast<std::uint32_t>(record.axes.size()));
    for (std::size_t i = 0; i < record.axes.size(); ++i) {
        writeAxisEntry(w, record.axes[i]);       // 条目全字段（追溯面）
        const AxisSynthesis& s = record.synthesis[i];
        w.f64(s.massKg);                          // 合成结果（记录面）
        w.vec3(s.comM);
        w.inertia(s.inertia);
        w.f64(s.rotorInertiaKgM2);
    }
    return std::move(w.m_bytes);
}

DecodedBackfillRecord decodeBackfillRecordObject(const std::vector<std::uint8_t>& bytes)
{
    DecodedBackfillRecord out;
    ByteReader r(bytes);
    r.expectHeader(kBackfillRecordMagic, kBackfillPayloadCodecVersion);
    if (r.failed()) {
        out.status = r.versionMismatch() ? DecodedBackfillRecord::Status::UnsupportedVersion
                                         : DecodedBackfillRecord::Status::Malformed;
        out.detail = r.reason();
        return out;
    }
    out.record.referenceFrameToken = r.str();
    const std::uint8_t domainBits = r.u8();
    // 位图词表校验：只允许低 4 位（高位置 1＝未知域——协议演进越界）。
    if ((domainBits & 0xF0u) != 0) {
        out.status = DecodedBackfillRecord::Status::Malformed;
        out.detail = "复算域位图含未知位（0x" + std::to_string(domainBits) + "）";
        return out;
    }
    for (std::size_t i = 0; i < kRecalcDomainCount; ++i) {
        out.record.recalc.domains[i] = (domainBits & (1u << i)) != 0;
    }
    const std::uint8_t retain = r.u8();
    if (retain > 1) {
        out.status = DecodedBackfillRecord::Status::Malformed;
        out.detail = "不沿用标志非法（期望 0/1）";
        return out;
    }
    out.record.recalc.retainPriorConclusion = (retain != 0);
    const std::uint32_t axisCount = r.u32();
    constexpr std::uint32_t kMaxAxes = 4096;  // 同命令载荷上限口径
    if (axisCount > kMaxAxes) {
        out.status = DecodedBackfillRecord::Status::Malformed;
        out.detail = "轴数超上限（" + std::to_string(axisCount) + "）";
        return out;
    }
    for (std::uint32_t i = 0; i < axisCount; ++i) {
        out.record.axes.push_back(readAxisEntry(r));
        AxisSynthesis s;
        s.massKg = r.f64();
        s.comM = r.vec3();
        s.inertia = r.inertia();
        s.rotorInertiaKgM2 = r.f64();
        out.record.synthesis.push_back(s);
    }
    r.expectEnd();
    if (r.failed()) {
        out.status = DecodedBackfillRecord::Status::Malformed;
        out.detail = r.reason();
        return out;
    }
    if (out.record.referenceFrameToken != kBackfillFrameLink) {
        out.status = DecodedBackfillRecord::Status::Malformed;
        out.detail = "参考系词表外：" + out.record.referenceFrameToken;
        return out;
    }
    out.status = DecodedBackfillRecord::Status::Ok;
    return out;
}

// =====================================================================
// 回填数据组装（§3.1 Backfill 行"回填数据组装"）
// =====================================================================

namespace {

/// 快照内按型号查找电机条目（快照电机表装配后 modelId 升序——二分）。
const MotorCatalogEntry* findMotor(const CatalogPackageSnapshot& snapshot,
                                   const ModelId& id)
{
    const auto it = std::lower_bound(
        snapshot.motors.begin(), snapshot.motors.end(), id,
        [](const MotorCatalogEntry& e, const ModelId& key) { return e.modelId < key; });
    if (it != snapshot.motors.end() && it->modelId == id) {
        return &*it;
    }
    return nullptr;
}

/// 快照内按型号查找减速器条目（同上）。
const GearboxCatalogEntry* findGearbox(const CatalogPackageSnapshot& snapshot,
                                       const ModelId& id)
{
    const auto it = std::lower_bound(
        snapshot.gearboxes.begin(), snapshot.gearboxes.end(), id,
        [](const GearboxCatalogEntry& e, const ModelId& key) { return e.modelId < key; });
    if (it != snapshot.gearboxes.end() && it->modelId == id) {
        return &*it;
    }
    return nullptr;
}

/// 兼容表存在 (motor, gearbox, mountKind) 记录判定（§12.2 纪律 4——
/// 记录的安装关系必须与目录兼容记录一致；快照兼容表升序——线性扫描
/// 足够〔组装面非热点路径〕，确定性依赖容器序）。
bool mountRecordExists(const CatalogPackageSnapshot& snapshot,
                       const ModelId& motorId, const ModelId& gearboxId,
                       const std::string& mountKind)
{
    for (const CompatibilityRecord& rec : snapshot.compatibility) {
        if (rec.motorId == motorId && rec.gearboxId == gearboxId
            && rec.mountKind == mountKind) {
            return true;
        }
    }
    return false;
}

}  // namespace

BackfillAssemblyOutcome assembleDeviceBackfill(const CatalogPackageSnapshot& snapshot,
                                               const CatalogVersion& lock,
                                               const std::vector<AxisBackfillSource>& axes)
{
    BackfillAssemblyOutcome out;

    // ---- 第 1 步：输入卫生（空轴表/重复轴/锁定引用完整性）----
    if (axes.empty()) {
        out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
        out.detail = "组装轴表为空（回填命令至少一条轴）";
        return out;
    }
    if (lock.lockObjectId.isValid() && !(lock.identity == snapshot.manifest.identity)) {
        // 锁定引用与快照身份失配＝引用完整性破坏（调用方错误轨——组装
        // 期即拒绝，不留到 prepare）。
        out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
        out.detail = "锁定引用的目录身份与快照不一致（catalogId="
                   + lock.identity.catalogId + " version=" + lock.identity.version
                   + "）";
        return out;
    }
    for (std::size_t i = 0; i < axes.size(); ++i) {
        if (!axes[i].jointId.isValid()) {
            out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
            out.detail = "第 " + std::to_string(i) + " 轴 jointId 为保留值（全零）";
            return out;
        }
        for (std::size_t j = i + 1; j < axes.size(); ++j) {
            if (axes[j].jointId == axes[i].jointId) {
                out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
                out.detail = "轴重复（index " + std::to_string(i) + " 与 "
                           + std::to_string(j) + " 同 jointId）";
                return out;
            }
        }
        if (axes[i].mountKind.empty()) {
            out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
            out.detail = "第 " + std::to_string(i) + " 轴安装关系为空（§12.2 纪律 4"
                       "——回填必须记录安装关系）";
            return out;
        }
    }

    // ---- 第 2～4 步：逐轴目录取值/安装核对/合成预演 ----
    DeviceBackfillRequest request;
    request.referenceFrameToken = std::string(kBackfillFrameLink);  // v1 冻结参考系
    for (const AxisBackfillSource& src : axes) {
        AxisBackfillEntry entry;
        entry.jointId = src.jointId;

        // 第 2a 步：目录取值——电机条目（壳体质量/转子惯量）。
        const MotorCatalogEntry* motor = findMotor(snapshot, src.motorModelId);
        if (motor == nullptr) {
            out.kind = BackfillAssemblyOutcome::Kind::UnknownDevice;
            out.detail = "电机型号不在快照主表：" + src.motorModelId;
            return out;
        }
        // 第 2b 步：减速器条目（壳体质量/壳体惯量——目录可缺失）。
        const GearboxCatalogEntry* gearbox = findGearbox(snapshot, src.gearboxModelId);
        if (gearbox == nullptr) {
            out.kind = BackfillAssemblyOutcome::Kind::UnknownDevice;
            out.detail = "减速器型号不在快照主表：" + src.gearboxModelId;
            return out;
        }
        // 第 3 步：安装关系核对——兼容表必须存在该 (motor, gearbox,
        // mountKind) 记录（§12.2 纪律 4：回填记录的安装关系＝目录兼容
        // 记录的回执，不一致即组装拒绝）。
        if (!mountRecordExists(snapshot, src.motorModelId, src.gearboxModelId,
                               src.mountKind)) {
            out.kind = BackfillAssemblyOutcome::Kind::MountIncompatible;
            out.detail = "安装关系与兼容表不一致（" + src.motorModelId + "×"
                       + src.gearboxModelId + " mountKind=" + src.mountKind + "）";
            return out;
        }
        // 第 2c 步：壳体物性齐备性（§12.4 缺失处理——合成不可得即整体
        // 失败，P-SEL-6 保守口径：不允许部分回填）：
        //   - 电机壳体质量：目录必填列（mass>0 由导入校验保证）；
        //   - 电机壳体惯量：目录 v1 无字段——必须由调用方补充（P-SEL-7）；
        //   - 减速器壳体惯量：目录 optional——缺失即数据不足（不伪造）。
        if (!src.motorHousingInertiaSupplement.has_value()) {
            out.kind = BackfillAssemblyOutcome::Kind::DataInsufficient;
            out.detail = "电机壳体惯量缺失且无补充（目录 v1 无该字段——P-SEL-7 数据面，"
                       "§12.4 整体失败口径）：jointId=" + src.jointId.toCanonical();
            return out;
        }
        if (!gearbox->housingInertia.has_value()) {
            out.kind = BackfillAssemblyOutcome::Kind::DataInsufficient;
            out.detail = "减速器壳体惯量目录缺失（GearboxCatalogEntry.housingInertia "
                       "为空——§12.4 整体失败口径）：jointId="
                       + src.jointId.toCanonical();
            return out;
        }

        // ---- 组装单轴条目（目录字段＋调用方物理输入）----
        entry.catalogId = lock.identity.catalogId;
        entry.catalogVersion = lock.identity.version;
        entry.motorModelId = src.motorModelId;
        entry.gearboxModelId = src.gearboxModelId;
        entry.mountKind = src.mountKind;
        entry.catalogLockObject = lock.lockObjectId;
        entry.catalogLockVersion = core::ContentVersion{};  // 锁定对象版本：
        // project 锁定对象的内容版本不随本命令携带（组装面只见锁定对象
        // ID——CatalogVersion 结构仅承载 oid；prepare 侧对锁定引用的闭包
        // 存在性检查按 (oid, cv=零) 的存在性半区执行——登记于单元卡
        // §19.3 T09 落位细化：cv 面核对随 L5 装配（锁定对象读取器）补齐）。

        // 连杆原值（权威模型值传递）。
        entry.linkMassKg = src.linkMassKg;
        entry.linkComM = src.linkComM;
        entry.linkInertia = src.linkInertia;

        // 电机壳体（目录质量＋补充惯量＋安装锚点）。
        entry.motorComAnchorM = src.motorComAnchorM;
        entry.motorHousingMassKg = motor->mass;
        entry.motorHousingInertia = *src.motorHousingInertiaSupplement;

        // 减速器壳体（目录质量/惯量＋安装锚点）。目录壳体惯量为标量列
        // （kg·m²——目录 v1 无姿态信息），承载口径＝对角各向同性近似
        // {v,v,v,0,0,0}（登记于单元卡 §19.3 T09 落位细化：标量→对角的
        // 单点换算，无第二种约定）。
        entry.gearboxComAnchorM = src.gearboxComAnchorM;
        entry.gearboxHousingMassKg = gearbox->mass;
        const double gearboxInertiaScalar = *gearbox->housingInertia;
        entry.gearboxHousingInertia =
            BackfillInertiaTensor{gearboxInertiaScalar, gearboxInertiaScalar,
                                  gearboxInertiaScalar, 0.0, 0.0, 0.0};

        // 转子等效惯量（目录 rotorInertia——独立字段，不进合成）。
        entry.rotorInertiaKgM2 = motor->rotorInertia;

        // 回填传动比（组装源透传——取值口径归候选传动参数构造的唯一实
        // 现点，本处不重算）；组装期范围预检（I-MDL-11 同口径：有限>0），
        // 违例即组装拒绝（synthesis 合成不消费 ratio，但记录面契约要求
        // 合法值——不留非法值进载荷）。
        if (!isFiniteNumber(src.appliedRatio) || src.appliedRatio <= 0.0) {
            out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
            out.detail = "回填传动比非法（appliedRatio=" + std::to_string(src.appliedRatio)
                       + "，要求有限>0——I-MDL-11 同口径）：jointId="
                       + src.jointId.toCanonical();
            return out;
        }
        entry.appliedRatio = src.appliedRatio;

        // ---- 第 4 步：合成预演（§12.4——组装期即执行合成与断言）----
        const SynthesisOutcome syn = synthesizeAxisBodyProperties(entry);
        if (!syn.ok) {
            out.kind = BackfillAssemblyOutcome::Kind::InvalidInput;
            out.detail = "合成预演失败（jointId=" + src.jointId.toCanonical()
                       + "）：" + syn.detail;
            return out;
        }
        out.synthesis.push_back(syn.synthesis);
        request.axes.push_back(entry);
    }

    out.kind = BackfillAssemblyOutcome::Kind::Assembled;
    out.request = std::move(request);
    out.detail = "组装完成：" + std::to_string(out.request.axes.size())
               + " 轴（目录 " + lock.identity.catalogId + " "
               + lock.identity.version + "）";
    return out;
}

project::CommandEnvelope makeBackfillEnvelope(core::BranchId branch,
                                              std::optional<core::RevisionId> expectedRevision,
                                              const DeviceBackfillRequest& request)
{
    if (request.axes.empty()) {
        // 组装成功面不可能到达（axes≥1 由组装器保证）——防御性快失败。
        throw std::invalid_argument("回填信封构造：请求轴表为空（组装产物违约）");
    }
    project::CommandEnvelope env;
    env.branch = branch;
    env.expectedRevision = expectedRevision;
    env.commandType = std::string(kBackfillCommandToken);
    env.payloadFormatVersion = kBackfillPayloadFormatVersion;
    env.payloadCanonical = encodeBackfillPayload(request);
    return env;
}

// =====================================================================
// 命令处理器（§14.8）
// =====================================================================

std::string DeviceBackfillCommandHandler::commandType() const
{
    return std::string(kBackfillCommandToken);
}

std::uint32_t DeviceBackfillCommandHandler::currentPayloadVersion() const
{
    return kBackfillPayloadFormatVersion;
}

BackfillPlanOutcome DeviceBackfillCommandHandler::planFromEnvelope(
    const project::CommandEnvelope& envelope,
    const project::RevisionView& baseSnapshot,
    project::CommandPlan& out,
    std::vector<core::DiagnosticRecord>& diags) const
{
    BackfillPlanOutcome result;

    // ---- 判定 1：载荷版本受理核对（NFR-DEP-04——不受理即拒绝）----
    if (envelope.payloadFormatVersion != currentPayloadVersion()) {
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kSelBackfillPayloadVersionUnsupported),
            std::nullopt, std::nullopt, std::nullopt,
            "回填命令载荷版本核对",
            "载荷格式版本 " + std::to_string(envelope.payloadFormatVersion)
                + " 不在受理集合（当前受理 " + std::to_string(currentPayloadVersion())
                + "）",
            "按当前 payloadFormatVersion 重新组装回填命令后提交"));
        result.kind = BackfillPlanOutcome::Kind::RejectedInvalidInput;
        result.detail = "载荷版本不受理";
        return result;
    }

    // ---- 判定 2：载荷解码（结构轨——逐项定位）----
    const DecodedBackfillPayload decoded = decodeBackfillPayload(envelope.payloadCanonical);
    if (decoded.status != DecodedBackfillPayload::Status::Ok) {
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kSelBackfillPayloadMalformed),
            std::nullopt, std::nullopt, std::nullopt,
            "回填命令载荷解码",
            decoded.detail,
            "按 kBackfillPayloadCodecVersion 协议重新编码载荷"));
        result.kind = BackfillPlanOutcome::Kind::RejectedInvalidInput;
        result.detail = "载荷结构非法";
        return result;
    }
    const DeviceBackfillRequest& request = decoded.request;

    // ---- 判定 3：域校验（参考系词表/锁定引用与基线闭包一致性）----
    if (request.referenceFrameToken != kBackfillFrameLink) {
        diags.push_back(core::DiagnosticRecord::make(
            std::string(kSelBackfillInputInvalid),
            std::nullopt, std::nullopt, std::nullopt,
            "回填域输入校验",
            "参考系词表外：" + request.referenceFrameToken
                + "（v1 冻结 " + std::string(kBackfillFrameLink) + "）",
            "以 kBackfillFrameLink 参考系重组装回填命令"));
        result.kind = BackfillPlanOutcome::Kind::RejectedInvalidInput;
        result.detail = "参考系词表外";
        return result;
    }
    // 目录锁定引用的基线侧存在性检查（§12.1 S3"目录版本存在"——锁定
    // 对象 (oid, cv) 必须出现在基线修订闭包 objectRefs 中；值一致＝
    // 引用完整性。cv 的携带口径见组装面注释：v1 携带零值——存在性按
    // oid 半区核对＋cv 一致半区仅在载荷携带非零 cv 时核对，cv 面完整
    // 核对随 L5 装配（锁定对象读取器）补齐——单元卡 §19.3 T09 登记）。
    for (const AxisBackfillEntry& axis : request.axes) {
        const bool lockFound = std::any_of(
            baseSnapshot.objectRefs.begin(), baseSnapshot.objectRefs.end(),
            [&](const project::ObjectRef& ref) {
                if (ref.objectId != axis.catalogLockObject) { return false; }
                // cv 半区：载荷携带零值（组装面口径）时仅核对存在性；
                // 携带非零值时必须逐字节一致。
                if (axis.catalogLockVersion.bytes == core::Digest256{}) { return true; }
                return ref.contentVersion == axis.catalogLockVersion;
            });
        if (!lockFound) {
            diags.push_back(core::DiagnosticRecord::make(
                std::string(kSelBackfillLockRefMismatch),
                axis.jointId, std::nullopt, std::nullopt,
                "目录锁定引用基线核对（jointId=" + axis.jointId.toCanonical() + "）",
                "锁定对象不在基线修订闭包或版本失配（catalogId=" + axis.catalogId
                    + " version=" + axis.catalogVersion + "）",
                "以当前分支 tip 重新组装回填命令（基线前进后目录锁定引用随之更新）"));
            result.kind = BackfillPlanOutcome::Kind::RejectedInvalidInput;
            result.detail = "目录锁定引用与基线闭包不一致";
            return result;
        }
    }

    // ---- 判定 4：合成断言（逐轴复核——载荷可能经不可信通道改写，
    //      断言以解码值为准；任一轴失败＝整体失败零修订，§12.2 纪律 3）----
    BackfillRecordObject record;
    record.referenceFrameToken = request.referenceFrameToken;
    record.recalc = BackfillRecalcNotice{};  // 四域全量＋不沿用（AT-30 默认）
    for (const AxisBackfillEntry& axis : request.axes) {
        const SynthesisOutcome syn = synthesizeAxisBodyProperties(axis);
        if (!syn.ok) {
            // 失败分类映射：SPD/三角/质量非正＝MDL-06 断言轨（硬断言，
            // 就地阻止）；非有限/范围＝域输入轨（invalid-payload）——
            // 解码面已挡非有限，此处范围类仍可能（质量≤0 以外的字段面
            // 组合），按码面分轨。
            const bool hardAssert =
                syn.failure == SynthesisFailure::MassNonPositive
                || syn.failure == SynthesisFailure::NotPositiveDefinite
                || syn.failure == SynthesisFailure::TriangleInequality;
            diags.push_back(core::DiagnosticRecord::make(
                std::string(hardAssert ? kSelBackfillSynthesisAssertFailed
                                       : kSelBackfillRangeInvalid),
                axis.jointId, std::nullopt, std::nullopt,
                "合成物性断言（jointId=" + axis.jointId.toCanonical() + "）",
                syn.detail,
                "修正该轴回填物理输入（连杆原值/壳体锚点/目录物性）后重试"));
            result.kind = hardAssert ? BackfillPlanOutcome::Kind::RejectedHardAssert
                                     : BackfillPlanOutcome::Kind::RejectedInvalidInput;
            result.detail = "合成断言失败（多轴整体原子——任一轴失败整体失败）";
            return result;
        }
        record.synthesis.push_back(syn.synthesis);
    }
    record.axes = request.axes;

    // ---- 判定 5：计划组装（恰一对象写——多轴整体原子的记录面载体）----
    // 基线闭包已存在 sel-device-backfill 对象 → 继承其 oid 改版（PA-2：
    // 旧版本字节随历史修订闭包永久保留）；否则 objectId 空＝project S6
    // 装配点取号新建（公共头 ObjectWrite 空值语义）。
    project::ObjectWrite write;
    for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
        if (ref.objectTypeToken == kBackfillRecordObjectToken) {
            write.objectId = ref.objectId;
            break;  // 恰一记录对象——多写即基线违约（保守取首个并登记诊断）
        }
    }
    write.objectTypeToken = std::string(kBackfillRecordObjectToken);
    write.payloadCanonical = encodeBackfillRecordObject(record);
    out.objectWrites.push_back(std::move(write));

    // 命令摘要（随修订持久化——人读；PM-12-S1 历史浏览展示面）：轴数/
    // 目录版本/复算提示（AT-30 的摘要留痕面）。
    std::string summary = "应用选型回填：";
    summary += std::to_string(request.axes.size());
    summary += " 轴（目录 ";
    summary += request.axes.front().catalogId;
    summary += " ";
    summary += request.axes.front().catalogVersion;
    summary += "）；合成质量/质心/惯量已按 link-frame 参考系登记（壳体计入、";
    summary += "转子惯量独立登记未重复计入）；运动学/动力学/选型/优化结果";
    summary += "需复算，复核完成前不沿用原通过结论";
    out.summary = std::move(summary);
    // 本命令不声明双编译（回填记录对象非权威建模对象——权威驱动链的
    // 双编译由 modeling apply-drivetrain-design 命令承载，P-SEL-7）；
    // 不产出待确认集（§12.1 S4——当前回填无策略校验类可确认诊断，SA-15
    // 通道预留）；不可逆命令不声明逆命令（撤销经 project 修订语义的
    // 恢复形态，PM-18——§12.2 纪律 8 的处理器侧声明面）。
    out.requiresDualCompile = false;
    out.inverseCommandType = std::nullopt;
    out.inversePayloadCanonical = std::nullopt;

    result.kind = BackfillPlanOutcome::Kind::Planned;
    result.detail = "计划产出：恰一 sel-device-backfill 对象写（"
                  + std::to_string(request.axes.size()) + " 轴）";
    return result;
}

project::PrepareOutcome DeviceBackfillCommandHandler::prepare(
    project::HandlerContext& /*ctx*/,
    const project::CommandEnvelope& envelope,
    const project::RevisionView& baseSnapshot,
    project::CommandPlan& out,
    std::vector<core::DiagnosticRecord>& diags)
{
    // 直通转发计划内核（ctx 零调用——本处理器不需要对象取号〔空值语义
    // 由 project S6 分配〕、不需要补充查询〔基线判定全部基于 baseSnapshot
    // 值快照〕；文件头"依赖形态登记"①——零 project 实现符号引用）。
    const BackfillPlanOutcome result = planFromEnvelope(envelope, baseSnapshot, out, diags);
    switch (result.kind) {
    case BackfillPlanOutcome::Kind::Planned:
        return project::PrepareOutcome::Planned;
    case BackfillPlanOutcome::Kind::RejectedHardAssert:
        return project::PrepareOutcome::RejectedHardAssert;
    case BackfillPlanOutcome::Kind::RejectedInvalidInput:
        return project::PrepareOutcome::RejectedInvalidInput;
    }
    return project::PrepareOutcome::RejectedInvalidInput;  // 防御分支（不可达）
}

}  // namespace sdurws::ird::selection
