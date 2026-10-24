#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <rw/math/LinearAlgebra.hpp>
#include <array>
#include <cmath>
#include <algorithm>
namespace t27old {
using sdurws::ird::modeling::InertiaTensor;
__declspec(noinline) std::array<double, 3> symmetricEigenvalues3x3(const InertiaTensor& t) noexcept
{
    // 先按分量判断真正的零矩阵，而不是用平方和 p1==0 判断：极小但
    // 合法的惯量（例如 1e-300 kg·m²）平方后会下溢为 0，不能因此被
    // 错当成零矩阵。零矩阵分支必须优先于 p2 边界处理，SPD 判定仍会
    // 正确拒绝其最小特征值不严格大于 0（I-MDL-5）。
    const bool isZeroMatrix = t.ixx == 0.0 && t.iyy == 0.0 && t.izz == 0.0
                           && t.ixy == 0.0 && t.ixz == 0.0 && t.iyz == 0.0;
    if (isZeroMatrix) {
        // 零矩阵：特征值全 0（SPD 判定将正确拒绝——零惯量非正定）
        return {0.0, 0.0, 0.0};
    }
    const double trThird = (t.ixx + t.iyy + t.izz) / 3.0;
    const double p2 = (t.ixx - trThird) * (t.ixx - trThird)
                    + (t.iyy - trThird) * (t.iyy - trThird)
                    + (t.izz - trThird) * (t.izz - trThird)
                    + 2.0 * (t.ixy * t.ixy + t.ixz * t.ixz + t.iyz * t.iyz);
    if (p2 == 0.0) {
        // 各向同性张量的三个对角项相等且积项为零，此时 pNorm=0；
        // 直接返回三重平均值，避免后续 B=(A−qI)/pNorm 形成 0/0。
        // 该分支也覆盖大尺度输入的 p1 溢出场景，因为 p2 只描述偏离
        // 各向同性部分的能量，仍可保持为零（WP-13-T23）。
        return {trThird, trThird, trThird};
    }
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
}
