#include <sdurws/ird/trajectory/CartesianLine.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <rw/math/Quaternion.hpp>
#include <cmath>
namespace sdurws::ird::benchnew {
using namespace sdurws::ird::trajectory;
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

    // REUSE-T26／TRJ-02：数值插值由 RobWork Quaternion 维护；产品仅
    // 保留端点快速路径、行程范围与位置凸组合，不再维护翻转/三角权重/
    // 极小角分支或另一份四元数分量结构。两个局部值无共享状态或缓存。
    rw::math::Quaternion<double> qa(ta.R()), qb(tb.R());
    qa.normalize();
    qb.normalize();

    rw::math::Transform3D<double> out;
    out.P() = p;
    out.R() = qa.slerp(qb, s).toRotation3D();
    return out;
}


}
