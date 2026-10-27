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
 * ★ REUSE-T33：反解直接消费上游 rw::math::RPY(R)（sdurw_math 外联符号，
 *   catalog MATH-ROT verified 面），万向锁确定性 roll=0 产品约定保留。
 *   原"逐元素解析、禁调 RPY 构造"纪律（F-480 同族）经 WP-01 冒烟供给
 *   修复证伪后废止；插件经 STATIC 库 $<LINK_ONLY> 传递取得链接面（集成
 *   构建实证——本插件不在独立冒烟树目标内，G4/G6 边界如实登记）。
 *
 * 线程安全：纯函数；确定性：上游 RPY 分解＋本头固定分支判据（NFR-COR-02
 * ——同构建同输入逐位一致）。
 */

#ifndef IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP
#define IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP

#include <array>
#include <algorithm>
#include <cmath>

#include <rw/math/RPY.hpp>  // REUSE-T33：反解直接消费上游 RPY(R) 构造（sdurw_math）
#include <rw/math/Rotation3D.hpp>

namespace sdurws::ird::modeling::rpyview {

/**
 * @brief 旋转矩阵 → ZYX 欧拉角（RPY）反解——位姿行的角度半区呈现用。
 *
 * 正解约定 R＝Rz(yaw)·Ry(pitch)·Rx(roll)。REUSE-T33：解析式手写反解删除，
 * 直接消费上游 `rw::math::RPY(R)` 分解（RPY.cpp 外联符号，catalog
 * MATH-ROT verified 面；probe-r6 实测近奇异与精确 ±π/2 旋转往返正确），
 * 输出按上游 (z角,pitch,x角) → 产品 (roll,pitch,yaw) 反向换序取值——
 * 通支 roll/yaw 与原手写公式同式同值（atan2 同参），pitch 由 asin 路径
 * 换上游 atan2(−R20,√(R00²+R10²))（ULP 级差异，呈现精度 1e-6 不可见）。
 *
 * 万向锁：产品确定性约定 roll=0 保留（工程惯例——矩阵记录"上游奇异支
 * yaw(z角)=0 与产品 roll=0 表示不同，需保持规范化约定"）。pitch 直接取
 * 上游奇异支输出（恰 ±π/2）；yaw 用与原手写实现同式的配角公式
 * atan2(−R01,R11)（对 ±π/2 两支同式——上游奇异支对 x 角的取号在两支
 * 相反，不宜从上游 x 角换算；本式在精确极点与原实现逐位一致）。上游
 * 1e-5 带内（距奇异 ~1e-5 rad 内）呈现规范化 roll=0 形式，属呈现层确
 * 定性约定（零业务判定，重建该旋转仍逐位正确——奇异往返见
 * PluginPanelTest）。
 *
 * @param R [in] 旋转矩阵（无量纲正交阵；参考系语义由调用行负责）
 * @return {roll, pitch, yaw}，单位 rad
 */
inline std::array<double, 3> rotationToRpy(const rw::math::Rotation3D<double>& R)
{
    const rw::math::RPY<double> angles(R);
    // 奇异支判据＝上游 RPY.cpp 内部判据的镜像（cosβ=√(r00²+r10²)<1e-5），
    // 保证本处分支与上游实际所走分支一致（std::cos(angles[1]) 归一化路径
    // 在阈值附近可能与之分叉）。
    if (std::sqrt(R(0, 0) * R(0, 0) + R(1, 0) * R(1, 0)) < 1e-5) {
        // 万向锁：roll/yaw 共线不可分——确定性取 roll=0（产品工程惯例），
        // yaw＝atan2(−R01,R11)（配角公式，±π/2 两支同式）。
        return {0.0, angles[1], std::atan2(-R(0, 1), R(1, 1))};
    }
    // 上游 (z角,pitch,x角) → 产品 (roll,pitch,yaw) 反向换序。
    return {angles[2] * 2.0, angles[1], angles[0]};  // DISCONNECT-MUTATION
}

}  // namespace sdurws::ird::modeling::rpyview

#endif  // IRD_MODELING_PLUGIN_RPYPRESENTATION_HPP
