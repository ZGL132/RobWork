/**
 * @file   InertiaMath.hpp
 * @brief  modeling 单元私有实现头——惯量张量的对称 3×3 特征值解析求解、
 *         物性断言②③（SPD＋三角不等式）核查（I-MDL-5 单一实现点）与
 *         物性估算输出自检（§9.4.6 @post——WP-13-T04）。
 *
 * 设计依据：units/modeling.md §4.10 I-MDL-5（"惯量对称（相对 1×10⁻¹²）
 * ＋SPD（严格>0）＋三角不等式"，附录 D 第 6/7 项）；§4.4"断言①②③同
 * 连杆"（工具与连杆共用同一判定）；§9.4.6 @post（估算输出自检——WP-13-T04，
 * synthesisSelfCheck 单一实现）。
 *
 * ★ 为什么是私有头（src/ 内、不入公共 include/）：R-2 红线——实现细节
 * 不跨单元暴露；本头只被本单元 RobotDesign.cpp／Parts.cpp 包含。单一
 * 实现的动机：连杆与工具的物性判定必须逐字同源（两份实现会在容差/
 * 判定式上漂移），故收敛到本头。
 *
 * 线程安全：全部纯函数；确定性：解析式（无迭代、无收敛判据）——
 * 同输入位级同输出（NFR-COR-02）。
 */

#ifndef IRD_MODELING_SRC_INERTIAMATH_HPP
#define IRD_MODELING_SRC_INERTIAMATH_HPP

#include <array>
#include <cmath>

#include <sdurws/ird/modeling/RobotDesign.hpp>  // InertiaTensor/InvariantViolation/InvariantId

namespace sdurws::ird::modeling::inertiamath {

/**
 * @brief 对称 3×3 矩阵特征值（解析式三角法——Smith 算法，无迭代）。
 *
 * 为什么不用迭代特征值库：I-MDL-5 需要"同输入同特征值"的位级确定性，
 * 迭代法（Jacobi/QR）的收敛路径依赖浮点累积次序，跨平台/跨调用可能差
 * 一位；解析式是固定算式，确定性由构造保证。精度对 3×3 对称阵足够
 * （判定边界由测试用清晰例钉住；边界浮点噪声 ~1e-16 相对量级）。
 *
 * 数学（对称阵 A=[[x,p,q],[p,y,r],[q,r,z]]）：
 *   p1 = x²+y²+z²+2(p²+q²+r²)；p1=0 → 零阵（特征值全 0）；
 *   否则 q_ = tr(A)/3，p2 = Σ(xᵢ-q_)²+2(p²+q²+r²)，p_ = √(p2/6)，
 *   B = (A - q_·I)/p_，r_ = det(B)/2 夹取到 [-1,1]，φ = acos(r_)/3，
 *   λmax = q_ + 2p_·cos(φ)，λmin = q_ + 2p_·cos(φ+2π/3)，
 *   λmid = 3q_-λmax-λmin。
 *
 * @param t [in] 六分量对称张量（ixx,iyy,izz 主项；ixy,ixz,iyz 惯量积），
 *          单位 kg·m²；分量须有限（非有限由 I-MDL-3 层先报，本函数行为
 *          仅对有限输入有定义）
 * @return 升序特征值 (λmin, λmid, λmax)，单位 kg·m²
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<double, 3> symmetricEigenvalues3x3(const InertiaTensor& t) noexcept
{
    const double p1 = t.ixx * t.ixx + t.iyy * t.iyy + t.izz * t.izz
                    + 2.0 * (t.ixy * t.ixy + t.ixz * t.ixz + t.iyz * t.iyz);
    if (p1 == 0.0) {
        // 零矩阵：特征值全 0（SPD 判定将正确拒绝——零惯量非正定）
        return {0.0, 0.0, 0.0};
    }
    const double trThird = (t.ixx + t.iyy + t.izz) / 3.0;
    const double p2 = (t.ixx - trThird) * (t.ixx - trThird)
                    + (t.iyy - trThird) * (t.iyy - trThird)
                    + (t.izz - trThird) * (t.izz - trThird)
                    + 2.0 * (t.ixy * t.ixy + t.ixz * t.ixz + t.iyz * t.iyz);
    const double pNorm = std::sqrt(p2 / 6.0);
    // B = (A - trThird·I) / pNorm（p2>0 蕴含 pNorm>0——非零阵不会除零）
    const double bx = (t.ixx - trThird) / pNorm, bp = t.ixy / pNorm, bq = t.ixz / pNorm;
    const double by = (t.iyy - trThird) / pNorm, br = t.iyz / pNorm, bz = (t.izz - trThird) / pNorm;
    // det(B)：对称 3×3 行列式展开
    const double detB = bx * (by * bz - br * br) - bp * (bp * bz - br * bq)
                      + bq * (bp * br - by * bq);
    const double rr = std::max(-1.0, std::min(1.0, detB / 2.0));
    const double phi = std::acos(rr) / 3.0;
    const double eigMax = trThird + 2.0 * pNorm * std::cos(phi);
    const double eigMin = trThird + 2.0 * pNorm * std::cos(phi + 2.0943951023931953);  // φ+2π/3
    const double eigMid = 3.0 * trThird - eigMax - eigMin;
    return {eigMin, eigMid, eigMax};  // 升序
}

/**
 * @brief 惯量断言②③核查（SPD＋三角不等式）——违例即追加。
 *
 * @param out     [out] 违例接收容器（追加，不清空）
 * @param inertia [in] 待查惯量（SourcedValue——非 Provided 态直接跳过：
 *               缺失走 DataInsufficient 降级，MDL-06，不触发断言）
 * @param subject [in] 定位路径前缀（如 "links[0]"——本函数追加
 *               "<subject>.inertia.spd"/"<subject>.inertia.triangle"）
 *
 * 判定口径：
 *   - 对称性：六分量表示天然对称（附录 D 第 6 项由表示层结构性满足）；
 *   - SPD：最小特征值严格 > 0（第 7 项）；
 *   - 三角不等式：λmax ≤ λmid + λmin（严格比较——卡面未给容差，特征值
 *     已是解析解，不在本层放宽）。
 *
 * 纯函数；线程安全；确定性。
 */
inline void checkInertiaAssertions(std::vector<InvariantViolation>& out,
                                   const core::SourcedValue<InertiaTensor>& inertia,
                                   const std::string& subject)
{
    const auto tensor = inertia.tryValue();
    if (!tensor.has_value()) { return; }

    const std::array<double, 3> eig = symmetricEigenvalues3x3(*tensor);
    if (!(eig[0] > 0.0)) {
        // SPD 违例：不再做三角不等式（非正定张量的主矩无椭球语义——避免
        // 一个张量报两条无关违例干扰修正回路）
        out.push_back(InvariantViolation{InvariantId::IMdl5, subject + ".inertia.spd"});
        return;
    }
    if (eig[2] > eig[1] + eig[0]) {
        out.push_back(InvariantViolation{InvariantId::IMdl5, subject + ".inertia.triangle"});
    }
}

// =====================================================================
// 估算输出自检（WP-13-T04 新增；卡 §9.4.6 @post"输出自检：对称＋SPD＋
// 三角不等式（自检失败=内部错误码，不输出非法张量）"的单一实现点）
// =====================================================================

/// 对称性容差：|M−Mᵀ| 逐元素上限 1×10⁻¹²（卡 acceptance 2 原文"对称相对
/// 1×10⁻¹²"——双精度旋转/平移合成引入的浮点噪声远低于此量级，超限即
/// 实现错误而非正常舍入）。附录 D 第 6 项同值的建模侧承载。
inline constexpr double kSynthesisSymmetryTolerance = 1e-12;

/**
 * @brief 估算合成结果自检（§9.4.6 @post——WP-13-T04 acceptance 2 的
 *        "反例拒收"守卫）。
 *
 * 为什么在提取六分量前对全 3×3 矩阵自检：合成累加 R·I·Rᵀ＋平行轴项在
 * 全矩阵上进行，(i,j) 与 (j,i) 两次浮点路径不同，理论上可产生不对称
 * 噪声；六分量表示只存一个三角，提取前若不核查，不对称会被表示层
 * "结构性对称"掩盖（I-MDL-5 的对称检查因此永远通过——自检空转）。
 * 本函数在提取前封住该通道。
 *
 * 核查序（失败即短路返回原因，NFR-COR-03 不修复不截断）：
 *   ①有限性：九元素全有限（NaN/Inf——如量纲溢出到 inf——立即拒收）；
 *   ②对称性：|M(i,j)−M(j,i)| ≤ 1×10⁻¹²（kSynthesisSymmetryTolerance）；
 *   ③SPD＋三角不等式：经六分量投影（容差内的对称化取平均——两三角
 *     差已由②保证 ≤1×10⁻¹²，投影不引入超容差改动）复用
 *     symmetricEigenvalues3x3 判定 λmin>0 且 λmax ≤ λmid+λmin。
 *
 * 物理背景（为什么合法输入下此自检应恒过）：SPD 张量经正交相似变换
 * （R·I·Rᵀ）保持 SPD，加平行轴项 m((d·d)E−ddᵀ)（半正定秩 2）仍 SPD，
 * 凸组合保持 SPD——故 SynthesisFailed 触发即内部实现错误（§9.4.6 把它
 * 归为"内部错误码"的原因）。
 *
 * @param m [in] 合成累加完成的全 3×3 矩阵（kg·m²；元素须已按同一合成
 *          次序计算——本函数不重算，只核查）
 * @return 空串＝自检通过；非空＝失败原因（英文稳定短语，进
 *         EstimateError.detail 的内部定位串——不直接呈现给用户）
 *
 * 纯函数；线程安全；确定性（无迭代收敛判据——解析特征值，NFR-COR-02）。
 */
inline std::string synthesisSelfCheck(const std::array<std::array<double, 3>, 3>& m)
{
    // ①有限性：任何 NaN/Inf（含大尺寸溢出场景）都不能作为"物性"输出。
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(m[r][c])) {
                return "non-finite-entry";
            }
        }
    }
    // ②对称性（相对 1×10⁻¹²）：逐元素核查两三角的偏差。
    for (int r = 0; r < 3; ++r) {
        for (int c = r + 1; c < 3; ++c) {
            const double diff = m[r][c] - m[c][r];
            if (!(diff <= kSynthesisSymmetryTolerance
                  && diff >= -kSynthesisSymmetryTolerance)) {
                return "asymmetric-beyond-tolerance";
            }
        }
    }
    // ③六分量投影（容差内对称化平均——见函数注）＋SPD/三角不等式。
    InertiaTensor t;
    t.ixx = m[0][0];
    t.iyy = m[1][1];
    t.izz = m[2][2];
    t.ixy = 0.5 * (m[0][1] + m[1][0]);
    t.ixz = 0.5 * (m[0][2] + m[2][0]);
    t.iyz = 0.5 * (m[1][2] + m[2][1]);
    const std::array<double, 3> eig = symmetricEigenvalues3x3(t);  // 升序
    if (!(eig[0] > 0.0)) {
        return "not-positive-definite";  // SPD 严格>0（附录 D 第 7 项口径）
    }
    if (eig[2] > eig[1] + eig[0]) {
        return "triangle-inequality-violated";  // 惯性椭球三角不等式
    }
    return {};
}

}  // namespace sdurws::ird::modeling::inertiamath

#endif  // IRD_MODELING_SRC_INERTIAMATH_HPP
