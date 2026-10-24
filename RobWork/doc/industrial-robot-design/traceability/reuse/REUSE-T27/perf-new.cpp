#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <rw/math/LinearAlgebra.hpp>
#include <array>
#include <cmath>
#include <algorithm>
namespace t27new {
using sdurws::ird::modeling::InertiaTensor;
__declspec(noinline) std::array<double, 3> symmetricEigenvalues3x3(const InertiaTensor& t)
{
    // 六分量是同一连杆参考系、同一参考点的 kg·m²；镜像填入两三角，
    // 不改变惯量积符号、不做平行轴迁移或额外归一化。上游返回值已升序。
    Eigen::MatrixXd matrix(3, 3);
    matrix << t.ixx, t.ixy, t.ixz,
              t.ixy, t.iyy, t.iyz,
              t.ixz, t.iyz, t.izz;
    const auto decomposition = rw::math::LinearAlgebra::eigenDecompositionSymmetric(matrix);
    return {decomposition.second[0], decomposition.second[1], decomposition.second[2]};
}
}
