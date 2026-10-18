/**
 * @file   KinPanelRotation.cpp
 * @brief  KinPanelRotation.hpp 的实现（RPY ↔ Rotation3D，独立冒烟可链接）。
 *
 * 设计依据：KinPanelRotation.hpp 文件头（F-919 根因、为何自持转换、
 * 与 rw::math::RPY 的等价钉门控）。
 */

#include "KinPanelRotation.hpp"

#include <cmath>

#include <Eigen/Geometry>

namespace sdurws {
namespace ird {
namespace kinematics {

namespace {
// 与 rw::math::RPY 的万向锁分支保持同一常量（RPY.cpp 默认 epsilon=1e-5）。
constexpr double kGimbalEpsilon = 1e-5;
// 90° 常量（避免依赖框架 Constants.hpp；值取 IEEE-754 double 的 π/2 最近值）。
constexpr double kHalfPi = 1.5707963267948966192313216916398;
}  // namespace

rw::math::Rotation3D<double> rotationFromRpySi(double roll, double pitch, double yaw)
{
    // RPY.cpp::toRotation3D 的矩阵等价于 Rz(roll)·Ry(pitch)·Rx(yaw)。
    // Eigen 轴角组合是纯头文件实现（不产生框架库符号），两模式同源。
    const Eigen::Matrix3d m =
        (Eigen::AngleAxisd(roll, Eigen::Vector3d::UnitZ()) *
         Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY()) *
         Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitX()))
            .toRotationMatrix();
    // 走 9 参构造（内联），避免 Eigen 矩阵模板构造的重载歧义。
    return rw::math::Rotation3D<double>(m(0, 0), m(0, 1), m(0, 2),
                                        m(1, 0), m(1, 1), m(1, 2),
                                        m(2, 0), m(2, 1), m(2, 2));
}

std::array<double, 3> rpyFromRotationSi(const rw::math::Rotation3D<double>& R,
                                        double epsilon)
{
    // 逐行对应 RPY.cpp 的 RPY(const Rotation3D&, epsilon)：
    // 变量命名与分支顺序刻意保持同构，便于与框架实现对照 review。
    const double r11 = R(0, 0);
    const double r12 = R(0, 1);
    const double r21 = R(1, 0);
    const double r22 = R(1, 1);
    const double r31 = R(2, 0);
    const double r32 = R(2, 1);
    const double r33 = R(2, 2);

    double sum = r11 * r11 + r21 * r21;
    if (sum < 0.0) { sum = 0.0; }  // 舍入误差防护（与 RPY.cpp 同）

    const double cosBeta = std::sqrt(sum);
    const double sinBeta = -r31;

    std::array<double, 3> out{};
    if (std::fabs(cosBeta) < epsilon) {
        // 万向锁：beta=±90°；alpha 固定 0，gamma 由剩余项解出（RPY.cpp 同式）。
        if (sinBeta < 0.0) {
            out[0] = 0.0;
            out[1] = -kHalfPi;
            out[2] = -std::atan2(r12, r22);
        } else {
            out[0] = 0.0;
            out[1] = kHalfPi;
            out[2] = std::atan2(r12, r22);
        }
    } else {
        out[1] = std::atan2(sinBeta, cosBeta);
        out[0] = std::atan2(r21, r11);
        out[2] = std::atan2(r32, r33);
    }
    return out;
}

// 显式引用常量，保证编译器不因未使用而告警（同时把 1e-5 口径写在单一位置）。
[[maybe_unused]] const double kGimbalEpsilonRef = kGimbalEpsilon;

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
