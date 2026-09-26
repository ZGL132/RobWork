/**
 * @file   Sampling.cpp
 * @brief  区域采样与覆盖率（KIN-04）的实现翻译单元——确定性样本生成
 *         （Grid 体心/Random 种子序列/斐波那契螺旋方向集×roll 均分）、
 *         sampleSetIdentity（evidence §4.1.4 公式）、覆盖率计算唯一
 *         实现点与区域覆盖 canonical 载荷编码。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Sampling.hpp/Coverage.hpp
 *     行——T06）、§7.2（覆盖率图全链语义）、§9.2（IWorkspaceSampler
 *     接口契约）、§3.4（随机性唯一来源＝seed 派生确定性序列）、§8.4
 *     （确定性/并行——同输入同线程数逐位一致）
 *   - REQUIREMENTS KIN-04（R3/R8）、KIN-05；evidence §4.1.4
 *   - 任务契约 tasks/foundation/WP-15-T06.json acceptance 1/2/3/4
 *
 * 实现口径（黄金锁定值逐处登记，修改走设计变更并同步卡面 §7.2/§14.6）：
 *   - Grid 体心规则/Random splitmix64 序列/斐波那契螺旋/roll 体心均分
 *     ——D-KIN-6，常量与序见各函数注；
 *   - sampleSetIdentity canonical 字节布局见 sampleSetIdentity 函数注
 *     （O-38/P-KIN-3 冻结前对账面＝快照 SamplingPlanRef 逐字节比对）；
 *   - 位置样本评估的"orientation 无约束"落值＝姿态残差容差取 π rad
 *     （角度型残差量程 [0,π]——π 容差接受任意姿态，存在性口径的结构
 *     落值），随卡 §14.6 v0.6 登记。
 *
 * 线程安全：全部为纯函数（无共享可变状态）；确定性：位模式直写/无
 * 环境熵（NFR-COR-01/02）。
 */

#include <sdurws/ird/kinematics/Sampling.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "CanonicalCodec.hpp"  // detail::putU32/putU64/putF64（定宽小端原语——私有头）

namespace sdurws::ird::kinematics {

namespace {

// =====================================================================
// 确定性随机原语（与 Ik.cpp 初值生成同算法同形态——§3.4 随机性唯一来源
// 的两个消费点；算法公开定版、跨平台位级一致）
// =====================================================================

/// splitmix64（确定性伪随机序列源——无时钟/环境熵）。
std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// u64 → [0,1) 均匀归一化（53 位有效——双精度无偏下界；与 Ik.cpp 同式）。
double unitFromU64(std::uint64_t v)
{
    return static_cast<double>(v >> 11) * (1.0 / 9007199254740992.0);  // 2^53
}

/// 黄金角 φ₀＝π(3−√5)（rad——斐波那契螺旋的球面均匀分离角；常量字面
/// 锁定，不用运行期表达式——跨平台位级一致的确定性承载面，D-KIN-6）。
constexpr double kFibonacciGoldenAngle = 2.39996322972865332223;

/// π（rad——roll 均分的全周端点；字面锁定同上）。
constexpr double kPi = 3.14159265358979323846;

/// 位置样本评估的"姿态无约束"残差容差落值（rad——角度型残差量程 [0,π]，
/// π 容差接受任意姿态＝存在性口径；见文件头"实现口径"注）。
constexpr double kOrientationFreeTolerance = kPi;

// =====================================================================
// 载荷编码小工具（T04/T05 编码器的同构形态——绑定块共享布局）
// =====================================================================

/// 追加定长 32 字节身份（ContentIdentity/Digest256 的原始字节——小端
/// 语义对字节序不敏感，直接按序拷贝）。
void putIdentity(std::vector<std::uint8_t>& out, const core::ContentIdentity& id)
{
    detail::putBytes(out, id.bytes.data(), id.bytes.size());
}

/// 追加定长 16 字节强类型 id（ObjectId 等 Id128 家族——逐字节拷贝，
/// 不跨类型构造 ContentIdentity：16/32 字节类型语义不同、强类型纪律
/// 禁互转——core §4.1）。
template <typename IdT>
void putId128(std::vector<std::uint8_t>& out, const IdT& id)
{
    detail::putBytes(out, id.bytes.data(), id.bytes.size());
}

/// 追加长度前缀字符串（u32 长度＋原始字节——禁 NUL 纪律由调用方保证）。
void putString(std::vector<std::uint8_t>& out, const std::string& s)
{
    detail::putU32(out, static_cast<std::uint32_t>(s.size()));
    detail::putBytes(out, s.data(), s.size());
}

/// 追加 f64 向量（u32 计数＋逐元素位模式——全精度，无舍入）。
void putF64Vector(std::vector<std::uint8_t>& out, const std::vector<double>& v)
{
    detail::putU32(out, static_cast<std::uint32_t>(v.size()));
    for (const double x : v) {
        detail::putF64(out, x);
    }
}

/// 追加任务五元组（4×16B 强类型 id＋attempt u64——T04/T05 绑定块同布局；
/// attempt 为 u64 序号非 Id128——core TaskIdentity 字段形态）。
void putTaskIdentity(std::vector<std::uint8_t>& out, const core::TaskIdentity& t)
{
    detail::putBytes(out, t.project.bytes.data(), t.project.bytes.size());
    detail::putBytes(out, t.branch.bytes.data(), t.branch.bytes.size());
    detail::putBytes(out, t.revision.bytes.data(), t.revision.bytes.size());
    detail::putBytes(out, t.run.bytes.data(), t.run.bytes.size());
    detail::putU64(out, t.attempt.value);
}

/// 追加单轴六计数（planned/reached/unreachable/dataInsufficient/notRun/
/// notApplicable——CoverageTotals 字段序即编码序）。
void putCoverageTotals(std::vector<std::uint8_t>& out, const CoverageTotals& t)
{
    detail::putU64(out, t.planned);
    detail::putU64(out, t.reached);
    detail::putU64(out, t.unreachable);
    detail::putU64(out, t.dataInsufficient);
    detail::putU64(out, t.notRun);
    detail::putU64(out, t.notApplicable);
}

}  // namespace

// =====================================================================
// sampleSetIdentity——SHA-256 over (planContentIdentity ‖ 预算/种子 canonical)
// =====================================================================

core::ContentIdentity sampleSetIdentity(const core::ContentIdentity& planContentIdentity,
                                        const RegionSamplingBudget& budget)
{
    // canonical 字节布局（codec 版本 1——布局全文见头文件函数注；登记随
    // 卡 §14.6 v0.6，evidence 冻结时以此为准对齐——P-KIN-3 三方一致义务；
    // 冻结前对账面＝快照 SamplingPlanRef 逐字节比对，布局漂移即对账失败
    // 显性化，设计上安全）。
    std::vector<std::uint8_t> canonical;
    const char magic[] = "IRDSSID1";
    canonical.insert(canonical.end(), magic, magic + 7);
    detail::putU32(canonical, 1U);  // codecVersion（演进即新版本，不回退）
    // 计划身份取规范文本字节（"cid-<64hex>" ASCII——与 core 规范文本面
    // 同源；长度前缀定界，禁裸拼接的歧义）。
    const std::string planText = planContentIdentity.toCanonical();
    putString(canonical, planText);
    detail::putU64(canonical, budget.seed);  // 预算/种子参数（threadCount 不入身份）

    // SHA-256（core ContentDigester——CR-02 摘要算法唯一面）。
    core::ContentDigester digester;
    digester.update(canonical.data(), canonical.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

// =====================================================================
// 确定性样本生成（D-KIN-6 黄金锁定面）
// =====================================================================

std::vector<rw::math::Vector3D<double>>
generateGridPositions(const RegionBox& box, const std::array<std::uint32_t, 3>& counts)
{
    std::vector<rw::math::Vector3D<double>> out;
    // 零样本判定前置：任一轴计数 0 → 乘积 0（I-REQ-6 零样本场景——空集
    // 合法，评估面判 DataInsufficient；此处不拒）。
    const std::uint64_t product = static_cast<std::uint64_t>(counts[0])
        * static_cast<std::uint64_t>(counts[1]) * static_cast<std::uint64_t>(counts[2]);
    if (product == 0) {
        return out;
    }
    out.reserve(static_cast<std::size_t>(product));

    // 盒端点（基座系 {B}，单位 m）。
    const double minX = box.center[0] - box.size[0] / 2.0;
    const double minY = box.center[1] - box.size[1] / 2.0;
    const double minZ = box.center[2] - box.size[2] / 2.0;

    // 体心规则（黄金锁定）：格心＝min＋(k＋0.5)·格宽；枚举序＝x 最慢、
    // z 最快（index＝(ix·counts[1]＋iy)·counts[2]＋iz——登记序）。
    for (std::uint32_t ix = 0; ix < counts[0]; ++ix) {
        const double x = minX + (static_cast<double>(ix) + 0.5)
            * (box.size[0] / static_cast<double>(counts[0]));
        for (std::uint32_t iy = 0; iy < counts[1]; ++iy) {
            const double y = minY + (static_cast<double>(iy) + 0.5)
                * (box.size[1] / static_cast<double>(counts[1]));
            for (std::uint32_t iz = 0; iz < counts[2]; ++iz) {
                const double z = minZ + (static_cast<double>(iz) + 0.5)
                    * (box.size[2] / static_cast<double>(counts[2]));
                out.emplace_back(x, y, z);
            }
        }
    }
    return out;
}

std::vector<rw::math::Vector3D<double>>
generateRandomPositions(const RegionBox& box, std::uint32_t count, std::uint64_t seed)
{
    std::vector<rw::math::Vector3D<double>> out;
    out.reserve(count);
    // 种子派生确定性序列（黄金锁定）：第 k 个样本 state＝seed·2³²＋k
    // （混入样本序——与 Ik.cpp 初值生成同构），逐轴连续派生 x→y→z。
    for (std::uint32_t k = 0; k < count; ++k) {
        std::uint64_t state = seed * 0x100000000ull + k;
        const double u0 = unitFromU64(splitmix64(state));
        const double u1 = unitFromU64(splitmix64(state));
        const double u2 = unitFromU64(splitmix64(state));
        out.emplace_back(box.center[0] - box.size[0] / 2.0 + u0 * box.size[0],
                         box.center[1] - box.size[1] / 2.0 + u1 * box.size[1],
                         box.center[2] - box.size[2] / 2.0 + u2 * box.size[2]);
    }
    return out;
}

std::vector<rw::math::Vector3D<double>> generateDirections(std::uint32_t count)
{
    std::vector<rw::math::Vector3D<double>> out;
    out.reserve(count);
    // 斐波那契螺旋（黄金锁定）：z_j＝1−(2j＋1)/N、r_j＝√(1−z_j²)、
    // φ_j＝j·黄金角——球面近似均匀方向集；N=1 退化 z=0、r=1、φ=0＝+X。
    for (std::uint32_t j = 0; j < count; ++j) {
        const double n = static_cast<double>(count);
        const double z = 1.0 - (2.0 * static_cast<double>(j) + 1.0) / n;
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));  // 下截断防浮点负
        const double phi = static_cast<double>(j) * kFibonacciGoldenAngle;
        out.emplace_back(r * std::cos(phi), r * std::sin(phi), z);
    }
    return out;
}

std::vector<double> generateRolls(std::uint32_t count)
{
    std::vector<double> out;
    out.reserve(count);
    // roll 体心均分（黄金锁定）：[−π,π) 全周 M 等分的格心——与位置 Grid
    // 体心口径一致；count=1 → 0.5·(2π)−π＝0（单一 roll＝0 rad）。
    for (std::uint32_t r = 0; r < count; ++r) {
        out.push_back(-kPi
                      + (static_cast<double>(r) + 0.5) * (2.0 * kPi / static_cast<double>(count)));
    }
    return out;
}

rw::math::Transform3D<double> poseFromDirectionRoll(
    const rw::math::Vector3D<double>& direction, double roll,
    const rw::math::Vector3D<double>& position)
{
    // 第一步：工具 z 轴（+Z）到方向 d 的最小旋转的九个矩阵元（Rodrigues
    // 轴角公式直写——轴＝ẑ×d、角＝atan2(|ẑ×d|, ẑ·d)；双退化分支：d≈+ẑ
    // 恒等、d≈−ẑ 绕 x 半周。与产品 Fk 路径无共享代码的独立小实现，仅
    // 姿态组装用；rw 旋转类型的外联符号（默认构造/operator*）全程规避
    // ——冒烟模式不链 rw 库，Ik.hpp identityTransform3D 同款纪律）。
    const double dx = direction[0];
    const double dy = direction[1];
    const double dz = direction[2];
    const double cx = -dy;             // ẑ×d 的 x 分量（ẑ＝(0,0,1)）
    const double cy = dx;              // ẑ×d 的 y 分量
    const double cz = 0.0;             // ẑ×d 的 z 分量
    const double crossNorm = std::sqrt(cx * cx + cy * cy + cz * cz);
    const double dot = dz;             // ẑ·d＝d 的 z 分量

    double m00 = 0.0; double m01 = 0.0; double m02 = 0.0;
    double m10 = 0.0; double m11 = 0.0; double m12 = 0.0;
    double m20 = 0.0; double m21 = 0.0; double m22 = 0.0;
    if (crossNorm > 1e-12) {
        // 常规分支：Rodrigues 公式展开（R＝I＋sinθ·[axis]×＋(1−cosθ)·
        // [axis]×²——按分量直写）。
        const double ax = cx / crossNorm;
        const double ay = cy / crossNorm;
        const double az = cz / crossNorm;
        const double angle = std::atan2(crossNorm, dot);  // atan2 比 acos 数值稳定
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double t = 1.0 - c;
        m00 = t * ax * ax + c;       m01 = t * ax * ay - s * az; m02 = t * ax * az + s * ay;
        m10 = t * ax * ay + s * az;  m11 = t * ay * ay + c;       m12 = t * ay * az - s * ax;
        m20 = t * ax * az - s * ay;  m21 = t * ay * az + s * ax;  m22 = t * az * az + c;
    } else if (dot > 0.0) {
        // 退化分支 1：d≈+ẑ（夹角 0）——恒等旋转。
        m00 = 1.0; m01 = 0.0; m02 = 0.0;
        m10 = 0.0; m11 = 1.0; m12 = 0.0;
        m20 = 0.0; m21 = 0.0; m22 = 1.0;
    } else {
        // 退化分支 2：d≈−ẑ（夹角 π）——绕 x 轴半周（任一正交半周皆可，
        // 黄金锁定取 x 轴分支）。
        m00 = 1.0; m01 = 0.0; m02 = 0.0;
        m10 = 0.0; m11 = -1.0; m12 = 0.0;
        m20 = 0.0; m21 = 0.0; m22 = -1.0;
    }

    // 第二步：绕局部 z 轴转 roll（工具滚转）——R＝R_dir·Rz(roll)。
    // 手写三阶矩阵积：rw 的 Rotation3D::operator*（multiply）是外联符号，
    // 冒烟模式（不链 rw 库）不可解析。列组合展开：R·Rz 的第 0 列＝cr·
    // R_dir 列 0＋sr·R_dir 列 1、第 1 列＝cr·R_dir 列 1−sr·R_dir 列 0、
    // 第 2 列＝R_dir 列 2（Rz 右乘只混前两列）。
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const rw::math::Rotation3D<double> rRoll(
        m00 * cr + m01 * sr, m01 * cr - m00 * sr, m02,
        m10 * cr + m11 * sr, m11 * cr - m10 * sr, m12,
        m20 * cr + m21 * sr, m21 * cr - m20 * sr, m22);
    return rw::math::Transform3D<double>(position, rRoll);
}

std::uint64_t plannedPositionSampleCount(const SamplingPlan& plan)
{
    // Grid→逐轴乘积（含 0 因子→0——零样本）；Random→count。
    if (plan.position.method == PositionSamplingDefinition::Method::Grid) {
        return static_cast<std::uint64_t>(plan.position.gridCounts[0])
            * static_cast<std::uint64_t>(plan.position.gridCounts[1])
            * static_cast<std::uint64_t>(plan.position.gridCounts[2]);
    }
    return static_cast<std::uint64_t>(plan.position.randomCount);
}

std::uint64_t plannedPoseSampleCount(const SamplingPlan& plan)
{
    // (位置×姿态) 组合计数＝位置样本数×方向数×roll 数（全局口径分母——
    // 方向/roll 计数 ≥1 由装配校验保证，故位姿分母为 0 ⇔ 位置分母为 0）。
    return plannedPositionSampleCount(plan)
        * static_cast<std::uint64_t>(plan.orientation.directionSamples)
        * static_cast<std::uint64_t>(plan.orientation.rollSamples);
}

SampleSet generateSampleSet(const std::vector<SamplingPlan>& plans,
                            const RegionSamplingBudget& budget)
{
    SampleSet out;
    std::uint64_t nextIndex = 0;  // 全局 sampleIndex 游标（跨计划连续累加）

    // 逐计划生成（注入序＝生成序——黄金锁定；计划内先位置后位姿）。
    for (const SamplingPlan& plan : plans) {
        const std::uint64_t positionCount = plannedPositionSampleCount(plan);
        const std::uint32_t dirCount = plan.orientation.directionSamples;
        const std::uint32_t rollCount = plan.orientation.rollSamples;

        // 位置样本（Grid 枚举序/Random k 序——生成函数内的黄金序）。
        std::vector<rw::math::Vector3D<double>> positions;
        if (plan.position.method == PositionSamplingDefinition::Method::Grid) {
            positions = generateGridPositions(plan.box, plan.position.gridCounts);
        } else {
            positions = generateRandomPositions(plan.box, plan.position.randomCount,
                                                budget.seed);
        }

        // 方向集与 roll 序（计划级一次生成——位姿样本按 (pi,j,r) 组合）。
        const std::vector<rw::math::Vector3D<double>> directions = generateDirections(dirCount);
        const std::vector<double> rolls = generateRolls(rollCount);

        out.samples.reserve(out.samples.size() + positions.size()
                            + positions.size() * dirCount * rollCount);

        for (const rw::math::Vector3D<double>& p : positions) {
            SampleRecord rec;
            rec.sampleIndex = nextIndex++;
            rec.regionObjectId = plan.regionObjectId;
            rec.planContentIdentity = plan.planContentIdentity;
            rec.kind = SampleKind::Position;
            rec.position = p;
            out.samples.push_back(std::move(rec));
        }
        // 位姿样本：位置序×方向 j 序×roll r 序（index＝(pi·D＋j)·R＋r 的
        // 展开序——黄金锁定，登记随卡 §14.6 v0.6）。
        for (const rw::math::Vector3D<double>& p : positions) {
            for (const rw::math::Vector3D<double>& d : directions) {
                for (const double roll : rolls) {
                    SampleRecord rec;
                    rec.sampleIndex = nextIndex++;
                    rec.regionObjectId = plan.regionObjectId;
                    rec.planContentIdentity = plan.planContentIdentity;
                    rec.kind = SampleKind::Pose;
                    rec.position = p;
                    rec.pose = poseFromDirectionRoll(d, roll, p);
                    out.samples.push_back(std::move(rec));
                }
            }
        }

        out.plannedPositionSamples += positionCount;
        out.plannedPoseSamples += plannedPoseSampleCount(plan);
    }

    // 生成不变式自检（样本数＝两分母之和——实现缺陷 logic_error 不静默；
    // 分母完整性核查的结构前提）。
    const std::uint64_t total = static_cast<std::uint64_t>(out.samples.size());
    if (total != out.plannedPositionSamples + out.plannedPoseSamples) {
        throw std::logic_error(
            "kin.region-coverage：样本生成自检失败（样本总数≠位置分母＋位姿"
            "分母——内部缺陷，fail-fast）");
    }
    return out;
}

// =====================================================================
// computeCoverage——覆盖率计算唯一实现点（§7.2 覆盖率图）
// =====================================================================

CoverageResult computeCoverage(const SampleSet& set, const SampleResultSet& results)
{
    // 双射核查（分母完整性核查键——acceptance 4）：results 的 sampleIndex
    // 集合与 set.samples 完全一致（缺项/重复/越界即调用方组装缺陷）。
    if (results.results.size() != set.samples.size()) {
        throw std::logic_error(
            "kin.region-coverage：覆盖率计算双射核查失败（结果数≠样本数——"
            "分母完整性，fail-fast）");
    }
    std::vector<const SampleResultRecord*> byIndex(set.samples.size(), nullptr);
    for (const SampleResultRecord& r : results.results) {
        if (r.sampleIndex >= set.samples.size() || byIndex[static_cast<std::size_t>(r.sampleIndex)] != nullptr) {
            throw std::logic_error(
                "kin.region-coverage：覆盖率计算双射核查失败（sampleIndex 越"
                "界或重复——分母完整性，fail-fast）");
        }
        byIndex[static_cast<std::size_t>(r.sampleIndex)] = &r;
    }

    // 双口径计数（五值归轴累计——分母恒取计划样本总数，禁止按评估结果
    // 剔除样本，KIN-04 R8）。
    CoverageResult out;
    out.position.planned = set.plannedPositionSamples;
    out.orientation.planned = set.plannedPoseSamples;
    for (std::size_t i = 0; i < set.samples.size(); ++i) {
        const SampleKind kind = set.samples[i].kind;
        CoverageTotals& axis = (kind == SampleKind::Position) ? out.position
                                                              : out.orientation;
        switch (byIndex[i]->state) {
        case SampleState::Reached:
            ++axis.reached;
            break;
        case SampleState::Unreachable:
            ++axis.unreachable;
            break;
        case SampleState::DataInsufficient:
            ++axis.dataInsufficient;  // 保留分母不计分子——单独计数
            break;
        case SampleState::NotRun:
            ++axis.notRun;
            break;
        case SampleState::NotApplicable:
            ++axis.notApplicable;
            break;
        }
    }

    // 守恒式自检（Coverage.hpp 结构注——违例即实现缺陷）。
    if (out.position.planned
            != out.position.reached + out.position.unreachable
                + out.position.dataInsufficient + out.position.notRun
                + out.position.notApplicable
        || out.orientation.planned
            != out.orientation.reached + out.orientation.unreachable
                + out.orientation.dataInsufficient + out.orientation.notRun
                + out.orientation.notApplicable) {
        throw std::logic_error(
            "kin.region-coverage：覆盖率守恒式自检失败（分母≠五态之和——"
            "内部缺陷，fail-fast）");
    }

    // defined/降级/不完整标记（KIN-04 R3/R8 语义面——结构体注）。
    out.positionDefined = out.position.planned > 0;
    out.orientationDefined = out.orientation.planned > 0;
    out.downgraded = out.position.dataInsufficient > 0
        || out.orientation.dataInsufficient > 0 || !out.positionDefined
        || !out.orientationDefined;
    out.incomplete = out.position.notRun > 0 || out.orientation.notRun > 0;
    return out;
}

// =====================================================================
// encodeRegionCoveragePayloadCanonical——载荷编码（布局见 Coverage.hpp 注）
// =====================================================================

std::vector<std::uint8_t> encodeRegionCoveragePayloadCanonical(
    const RegionCoverageComputation& computation, const core::TaskIdentity& task)
{
    std::vector<std::uint8_t> out;
    const char magic[] = "IRDCV01";
    out.insert(out.end(), magic, magic + 7);
    detail::putU32(out, 1U);  // codecVersion

    // 标记块（四标记——覆盖率语义面的字节承载；u8 布尔，T05 完整性
    // 标记同款宽度）。
    out.push_back(computation.coverage.incomplete ? 1U : 0U);
    out.push_back(computation.coverage.downgraded ? 1U : 0U);
    out.push_back(computation.coverage.positionDefined ? 1U : 0U);
    out.push_back(computation.coverage.orientationDefined ? 1U : 0U);

    // 绑定块（§5.6 六要素——T04/T05 布局同构；mode u8 词表值）。
    putIdentity(out, computation.snapshotId);
    putIdentity(out, computation.sliceId);
    putIdentity(out, computation.configDigest);
    out.push_back(static_cast<std::uint8_t>(computation.mode));
    detail::putU64(out, computation.seed);
    putF64Vector(out, computation.referenceQ);
    putTaskIdentity(out, task);
    putString(out, kRegionCoverageEvaluationKey);
    detail::putU32(out, kRegionCoverageContractVersion);

    // 计划块（逐计划对账＋分母两值——分母来源逐计划核对面；布尔标记
    // u8。两轴六计数为全集合计，独立成块跟随其后，不在计划块内重复）。
    detail::putU32(out, static_cast<std::uint32_t>(computation.identityChecks.size()));
    for (const PlanIdentityCheck& check : computation.identityChecks) {
        putId128(out, check.regionObjectId);
        putIdentity(out, check.planContentIdentity);
        putIdentity(out, check.computedIdentity);
        out.push_back(check.matched ? 1U : 0U);
        detail::putU64(out, check.plannedPositionSamples);
        detail::putU64(out, check.plannedPoseSamples);
    }

    // 覆盖计数块（双口径各六计数——V-14"分子分母计数"的字节面）。
    putCoverageTotals(out, computation.coverage.position);
    putCoverageTotals(out, computation.coverage.orientation);

    // 逐样本状态表（覆盖证据核心面——样本几何不入载荷，见头文件注）。
    detail::putU32(out, static_cast<std::uint32_t>(computation.results.results.size()));
    for (const SampleResultRecord& r : computation.results.results) {
        const SampleRecord* sample = nullptr;
        // 对齐回查（状态表按 sampleIndex 携带归属——samples 双射保证命中）。
        for (const SampleRecord& s : computation.samples.samples) {
            if (s.sampleIndex == r.sampleIndex) {
                sample = &s;
                break;
            }
        }
        if (sample == nullptr) {
            throw std::logic_error(
                "kin.region-coverage：载荷编码对齐失败（结果 sampleIndex 无"
                "对应样本——双射破坏，fail-fast）");
        }
        detail::putU64(out, r.sampleIndex);
        putId128(out, sample->regionObjectId);
        out.push_back(static_cast<std::uint8_t>(sample->kind));
        out.push_back(static_cast<std::uint8_t>(r.state));
        if (r.outcomeKind.has_value()) {
            out.push_back(1U);
            out.push_back(static_cast<std::uint8_t>(*r.outcomeKind));
        } else {
            out.push_back(0U);
        }
        out.push_back(r.collisionNotEvaluated ? 1U : 0U);
        putString(out, r.reason);
    }
    return out;
}

}  // namespace sdurws::ird::kinematics
