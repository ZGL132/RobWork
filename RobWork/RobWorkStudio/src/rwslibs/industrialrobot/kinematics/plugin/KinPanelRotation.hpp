/**
 * @file   KinPanelRotation.hpp
 * @brief  RPY ↔ Rotation3D 的面板私有转换（F-919／WP-15-T19）。
 *
 * 设计依据：
 *   - findings F-919（独立冒烟树链接缺陷）
 *   - units/kinematics.md §11 WP-15-T19 行、§14.6 v0.28（KIN-R2 姿态呈现）
 *   - 任务契约 tasks/foundation/WP-15-T19.json
 *
 * 背景说明（为什么这里自己实现转换）：
 *   `rw::math::RPY::toRotation3D()` 与 `RPY(const Rotation3D&, epsilon)` 都是
 *   **非内联符号**（定义在框架 RPY.cpp）。集成模式由框架库解析；独立冒烟树
 *   （industrialrobot 顶层 CMakeLists 判定 CMAKE_CURRENT_SOURCE_DIR==
 *   CMAKE_SOURCE_DIR）不构建任何框架库，插件一旦引用这两个符号，测试链接
 *   必失败（F-919：LNK2019/LNK1120；显式补 `sdurw_math` 链接也只会得到
 *   LNK1181——冒烟树没有该库）。本头把两个转换收在单一私有实现点：
 *     - 正向：Eigen 头文件轴角组合 Rz(roll)·Ry(pitch)·Rx(yaw)，与 RPY.cpp
 *       的九元矩阵同序（rw 文档命名：roll 绕 z / pitch 绕 y / yaw 绕 x）；
 *     - 反向：逐行复现 RPY.cpp 的闭式解与 epsilon 万向锁分支（1e-5）。
 *   两者都在集成侧由 `IRD_HAVE_SDURW_MATH` 门控的等价钉逐元对照
 *   rw::math::RPY（含万向锁邻域），语义漂移会变红；独立冒烟树不注入该
 *   定义，测试编译为空，不把非内联符号带回链接面。
 *
 * 线程约束：纯函数、无共享状态；UI 线程与测试线程均可调用。
 */

#ifndef IRD_KINEMATICS_PLUGIN_KINPANELROTATION_HPP
#define IRD_KINEMATICS_PLUGIN_KINPANELROTATION_HPP

#include <array>

#include <rw/math/Rotation3D.hpp>

namespace sdurws {
namespace ird {
namespace kinematics {

/**
 * @brief RPY 三个 SI 角度（rad）→ 旋转矩阵（Z-Y-X，与 rw::math::RPY 同序）。
 *
 * @param roll  [in] 绕 z 的角度，单位 rad
 * @param pitch [in] 绕 y 的角度，单位 rad
 * @param yaw   [in] 绕 x 的角度，单位 rad
 * @return 3×3 旋转矩阵
 */
rw::math::Rotation3D<double> rotationFromRpySi(double roll, double pitch, double yaw);

/**
 * @brief 万向锁判定阈值默认值（rad 无关，余弦值域）——全文唯一来源。
 *
 * 取值 1e-5 与 rw::math::RPY(const Rotation3D&, T epsilon = 1e-5) 的框架
 * 默认一致（F-933：此前头文件默认字面量、cpp 常量与注释三处重复，存漂移
 * 温床；现统一为本常量，实现只消费参数、不再自带第二份字面量）。
 */
inline constexpr double kGimbalEpsilonDefault = 1e-5;

/**
 * @brief 旋转矩阵 → RPY 三个 SI 角度（rad，Z-Y-X；与 rw::math::RPY(R) 同式）。
 *
 * 复现 RPY.cpp 的闭式解：beta=asin(-r31)、alpha=atan2(r21,r11)、
 * gamma=atan2(r32,r33)；|cos(beta)| < epsilon 时走万向锁分支（alpha=0，
 * gamma=atan2(r12,r22)，beta=±90°）。
 *
 * @param R       [in] 旋转矩阵（须正交；调用方保证）
 * @param epsilon [in] 万向锁判定阈值（默认 kGimbalEpsilonDefault＝1e-5，
 *                    与 rw::math::RPY 框架默认一致）
 * @return {roll, pitch, yaw}（rad）
 */
std::array<double, 3> rpyFromRotationSi(const rw::math::Rotation3D<double>& R,
                                        double epsilon = kGimbalEpsilonDefault);

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_PLUGIN_KINPANELROTATION_HPP
