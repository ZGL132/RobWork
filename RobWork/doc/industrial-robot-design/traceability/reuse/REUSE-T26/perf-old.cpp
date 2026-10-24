#include <sdurws/ird/trajectory/CartesianLine.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <rw/math/Quaternion.hpp>
#include <cmath>
namespace sdurws::ird::benchold {
using namespace sdurws::ird::trajectory;
double safeAcos(double x)
{
    if (x > 1.0) {
        return 0.0;
    }
    if (x < -1.0) {
        return std::acos(-1.0);
    }
    return std::acos(x);
}

struct QuatComponents {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

QuatComponents normalizedQuat(const rw::math::Rotation3D<double>& R)
{
    const rw::math::Quaternion<double> q(R);
    QuatComponents c{q.getQx(), q.getQy(), q.getQz(), q.getQw()};
    const double n2 = c.x * c.x + c.y * c.y + c.z * c.z + c.w * c.w;
    if (n2 > 0.0) {
        const double inv = 1.0 / std::sqrt(n2);
        c.x *= inv;
        c.y *= inv;
        c.z *= inv;
        c.w *= inv;
    }
    return c;
}

rw::math::Transform3D<double> interpolateCartesianLine(
    const rw::math::Transform3D<double>& ta,
    const rw::math::Transform3D<double>& tb,
    double s)
{
    // 前置：行程参数 ∈[0,1]（越界外推属几何语义破坏——§8.2 定义域）。
    if (!(s >= 0.0 && s <= 1.0)) {
        throw TrajectoryError("trajectory/cartesian/interpolate-range",
                              "行程参数 s 须 ∈[0,1]，实际 " + std::to_string(s));
    }

    // 端点快速路径：s=0/1 直接返回端点位姿——保证端点**位级**精确还原
    // （V-04 断言依托；通用 slerp 公式在 s=0/1 时 sin(θ)/sinθ 的浮点除
    // 不保证精确等于 1/0，会有 1 ULP 级姿态漂移，违反端点还原义务）。
    if (s == 0.0) {
        return ta;
    }
    if (s == 1.0) {
        return tb;
    }

    // 位置：逐分量凸组合 p(s)=(1-s)·p_a+s·p_b——s=0/1 浮点下精确还原
    // 端点（V-04 端点还原断言依托；与 PTP 插值同式的笛卡尔侧对应）。
    const rw::math::Vector3D<double>& pa = ta.P();
    const rw::math::Vector3D<double>& pb = tb.P();
    const rw::math::Vector3D<double> p((1.0 - s) * pa[0] + s * pb[0],
                                       (1.0 - s) * pa[1] + s * pb[1],
                                       (1.0 - s) * pa[2] + s * pb[2]);

    // 姿态：四元数最短路径 slerp（§8.2"姿态最短弧 slerp（四元数最短
    // 路径）"）。手写显式实现以锁定口径（V-05 与显式四元数对照）：
    // 两端各取单位四元数→点积为负时翻转一端（±q 同旋转成对，取非负
    // 点积一支保证最短弧）→θ=acos(dot)→标准 slerp 公式。
    QuatComponents qa = normalizedQuat(ta.R());
    QuatComponents qb = normalizedQuat(tb.R());
    double dot = qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w;
    if (dot < 0.0) {
        // 最短路径翻转：q 与 -q 表示同一旋转，翻转后插值走短弧（V-05
        // 大弧样例锁定该分支——Rz(350°)→Rz(10°) 经 0° 而非绕行 340°）。
        qb.x = -qb.x;
        qb.y = -qb.y;
        qb.z = -qb.z;
        qb.w = -qb.w;
        dot = -dot;
    }
    const double theta = safeAcos(dot);
    QuatComponents qo = qa;
    if (theta > 1e-12) {
        // 常规 slerp：θ 大于极小角阈值——sin(θ) 远离 0，公式稳定。
        const double sinTheta = std::sin(theta);
        const double wa = std::sin((1.0 - s) * theta) / sinTheta;
        const double wb = std::sin(s * theta) / sinTheta;
        qo.x = wa * qa.x + wb * qb.x;
        qo.y = wa * qa.y + wb * qb.y;
        qo.z = wa * qa.z + wb * qb.z;
        qo.w = wa * qa.w + wb * qb.w;
    } else {
        // 近共线分支：θ<1e-12 rad（约 5.7×10⁻¹¹°）时 sin(θ)≈θ 数值不稳，
        // 回退线性插值后归一化（数学极限一致——θ→0 时 slerp→nlerp）。
        qo.x = (1.0 - s) * qa.x + s * qb.x;
        qo.y = (1.0 - s) * qa.y + s * qb.y;
        qo.z = (1.0 - s) * qa.z + s * qb.z;
        qo.w = (1.0 - s) * qa.w + s * qb.w;
        const double len = std::sqrt(qo.x * qo.x + qo.y * qo.y + qo.z * qo.z + qo.w * qo.w);
        if (len > 0.0) {
            const double inv = 1.0 / len;
            qo.x *= inv;
            qo.y *= inv;
            qo.z *= inv;
            qo.w *= inv;
        }
    }

    rw::math::Transform3D<double> out;
    out.P() = p;
    out.R() = rw::math::Quaternion<double>(qo.x, qo.y, qo.z, qo.w).toRotation3D();
    return out;
}


}
