/**
 * @file   RpyPresentation.hpp
 * @brief  modeling 插件私有头——旋转矩阵 → RPY（roll/pitch/yaw）反解的
 *         呈现层唯一实现（UI-T53 从 HostMigrationProviders.cpp 私有副本
 *         提升共享；检查器位姿六值呈现与编辑页 origin 基线回填共用）。
 *
 * 设计依据：units/modeling.md §9.7.1（位姿行"m, rad"六值口径）；UI-T53
 * 关节详细编辑页。与域内核正解 src/RpyMath.hpp（rpymath::rpyToRotation，
 * R＝Rz·Ry·Rx）互为正逆——约定单一权威＝正解头注，本反解由消费面单元
 * 测试钉住（UI-T50 检查器测试/UI-T53 编辑页测试）。
 *
 * ★ 逐元素解析实现——不调用 rw::math::RPY 构造（冒烟 header-only 纪律：
 *   RPY 构造是框架外联符号，F-480 同族——requirements 侧已实测链接失败，
 *   本插件禁重蹈）。
 *
 * 线程安全：纯函数；确定性：std::clamp/asin/atan2 固定算式（NFR-COR-02）。
 */

#ifndef IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP
#define IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP

#include <array>
#include <algorithm>
#include <cmath>

#include <rw/math/Rotation3D.hpp>

namespace sdurws::ird::modeling::rpyview {

/**
 * @brief 旋转矩阵 → ZYX 欧拉角（RPY）反解——位姿行的角度半区呈现用。
 *
 * 正解约定 R＝Rz(yaw)·Ry(pitch)·Rx(roll)；反解：pitch＝-asin(R20)，
 * roll＝atan2(R21,R22)，yaw＝atan2(R10,R00)；|R20|≈1 奇异（万向锁）时
 * roll＝0、yaw 改由 atan2(-R01,R11) 确定（确定性特例——工程惯例）。
 *
 * @param R [in] 旋转矩阵（无量纲正交阵；参考系语义由调用行负责）
 * @return {roll, pitch, yaw}，单位 rad
 */
inline std::array<double, 3> rotationToRpy(const rw::math::Rotation3D<double>& R)
{
    const double pitch = -std::asin(std::clamp(R(2, 0), -1.0, 1.0));
    const double cp = std::cos(pitch);
    if (std::abs(cp) > 1e-12) {
        return {std::atan2(R(2, 1), R(2, 2)), pitch, std::atan2(R(1, 0), R(0, 0))};
    }
    // 万向锁：roll/yaw 共线不可分——确定性取 roll=0（工程惯例）。
    return {0.0, pitch, std::atan2(-R(0, 1), R(1, 1))};
}

}  // namespace sdurws::ird::modeling::rpyview

#endif  // IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP
