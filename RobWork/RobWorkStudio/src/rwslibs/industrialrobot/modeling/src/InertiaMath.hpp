/**
 * @file   InertiaMath.hpp
 * @brief  modeling 单元私有实现头——惯量张量的对称 3×3 特征值解析求解
 *         与物性断言②③（SPD＋三角不等式）核查（I-MDL-5 单一实现点）。
 *
 * 设计依据：units/modeling.md §4.10 I-MDL-5（"惯量对称（相对 1×10⁻¹²）
 * ＋SPD（严格>0）＋三角不等式"，附录 D 第 6/7 项）；§4.4"断言①②③同
 * 连杆"（工具与连杆共用同一判定）。
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

}  // namespace sdurws::ird::modeling::inertiamath

#endif  // IRD_MODELING_SRC_INERTIAMATH_HPP
