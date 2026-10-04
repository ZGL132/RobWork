/**
 * @file   RpyMath.hpp
 * @brief  modeling 单元私有实现头——RPY（roll/pitch/yaw）→ 旋转矩阵的
 *         ZYX 正解唯一实现点（UI-T53 从 Import.cpp 提升共享；NFR-MNT-04
 *         单一实现纪律）。
 *
 * 设计依据：units/modeling.md §4.3-A（JointEntry.origin 参考系语义）、
 * §5.2 字段级编辑流（applyJointFieldEdit Origin 分支——UI-T53）、§6.3
 * （URDF 导入 origin/rpy 映射——既有消费点）。
 *
 * ★ 为什么是私有头（src/ 内、不入公共 include/）：R-2 红线——实现细节
 *   不跨单元暴露；本头只被本单元 Import.cpp／Template.cpp 包含。单一
 *   实现的动机：导入映射与字段编辑的旋转组合必须逐字同源（两份实现会
 *   在公式/约定上漂移——InertiaMath.hpp 同款动机）。
 *
 * ★ 为什么逐元素展开、不调用 rw::math::RPY 构造（F-480 同族外联符号——
 *   冒烟 header-only 纪律，HostMigrationProviders 侧已实测链接失败）：
 *   正解元素式与呈现反解（PanelModel rotationToRpy／HostMigrationProviders
 *   同名辅助）互为正逆，公式一致性由各自单元测试钉住。
 *
 * 线程安全：纯函数；确定性：libm 三角函数固定算式——同输入位级同输出
 * （NFR-COR-02）。
 */

#ifndef IRD_MODELING_SRC_RPYMATH_HPP
#define IRD_MODELING_SRC_RPYMATH_HPP

#include <cmath>

#include <rw/math/Rotation3D.hpp>

namespace sdurws::ird::modeling::rpymath {

/**
 * @brief RPY（roll/pitch/yaw，单位 rad）→ 旋转矩阵的 ZYX 正解。
 *
 * 约定：R ＝ Rz(yaw) · Ry(pitch) · Rx(roll)（与 rw::math::RPY 构造约定
 * 逐元素一致；与呈现侧反解 pitch＝-asin(R20)、roll＝atan2(R21,R22)、
 * yaw＝atan2(R10,R00) 互为正逆）。
 *
 * @param roll  [in] 绕 X 角，单位 rad
 * @param pitch [in] 绕 Y 角，单位 rad
 * @param yaw   [in] 绕 Z 角，单位 rad
 * @return 3×3 正交旋转矩阵（无量纲）
 */
inline rw::math::Rotation3D<double> rpyToRotation(double roll, double pitch,
                                                  double yaw)
{
    const double cr = std::cos(roll), sr = std::sin(roll);
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    // Rz·Ry·Rx 乘积的逐元素展开（行主序——手推矩阵积，不依赖框架 RPY 符号）。
    return rw::math::Rotation3D<double>(
        cy * cp,
        cy * sp * sr - sy * cr,
        cy * sp * cr + sy * sr,
        sy * cp,
        sy * sp * sr + cy * cr,
        sy * sp * cr - cy * sr,
        -sp,
        cp * sr,
        cp * cr);
}

}  // namespace sdurws::ird::modeling::rpymath

#endif  // IRD_MODELING_SRC_RPYMATH_HPP
