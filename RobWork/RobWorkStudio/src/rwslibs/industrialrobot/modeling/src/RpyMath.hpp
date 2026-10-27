/**
 * @file RpyMath.hpp
 * @brief modeling 私有旋转适配：保留产品 roll/pitch/yaw 输入顺序，数学由 RobWork 实现。
 *
 * 设计依据：MDL-11/MDL-22、units/modeling.md §4.3/§6.3、REUSE-T11。
 * 导入和 Origin 编辑共用同一顺序边界，避免各调用点独自交换参数。
 * 基座编辑仅产生 EAA 参数，最终安装矩阵仍由 runtime 唯一维护。
 * 纯值、无共享状态/缓存；可并发调用，确定性沿用同构建的上游数学实现。
 */
#ifndef IRD_MODELING_SRC_RPYMATH_HPP
#define IRD_MODELING_SRC_RPYMATH_HPP

#include <rw/math/EAA.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Vector3D.hpp>

namespace sdurws::ird::modeling::rpymath {

/**
 * @brief 产品 RPY 输入转为 ZYX 旋转：Rz(yaw)·Ry(pitch)·Rx(roll)。
 * @param roll [in] 绕 X 的角，rad，须有限；由导入/编辑入口校验。
 * @param pitch [in] 绕 Y 的角，rad，须有限。
 * @param yaw [in] 绕 Z 的角，rad，须有限。
 * @return 同一参考系下的无量纲旋转矩阵；不改变平移或持有模型。
 * @note 无产品异常/诊断分支；调用方负责业务输入的原子拒绝。
 */
inline rw::math::Rotation3D<double> rpyToRotation(double roll, double pitch,
                                                 double yaw)
{
    // RobWork RPY 构造顺序为 yaw/pitch/roll；差异只有字段顺序，不另展开三角公式。
    return rw::math::RPY<double>(yaw, pitch, roll).toRotation3D();
}

/**
 * @brief MDL-22：欧拉编辑值转为既有主值 EAA，不缓存或叠加安装矩阵。
 * @param rpy [in] {roll,pitch,yaw}，rad，各分量须有限，入口负责校验。
 * @return 同一参考系的轴乘角矢量，rad，角度主值 [0,pi]；零旋转返回零矢量。
 * @note pi 处轴符号以所表示的矩阵为准；表示转换容差见需求附录D第13项。
 * 无产品异常/诊断分支；Custom 身份及非法输入仍由既有业务入口判定。
 */
inline rw::math::Vector3D<double> rpyToEaa(const rw::math::Vector3D<double>& rpy)
{
    // 上游承担矩阵到轴角的零角/pi分支；不保留旧四元数核或精度回退。
    return rw::math::EAA<double>(rpyToRotation(rpy[0], rpy[1], rpy[2])).toVector3D();
}

}  // namespace sdurws::ird::modeling::rpymath
#endif  // IRD_MODELING_SRC_RPYMATH_HPP
