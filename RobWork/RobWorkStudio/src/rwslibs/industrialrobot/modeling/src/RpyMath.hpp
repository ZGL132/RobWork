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
#include <rw/math/Vector3D.hpp>

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

/**
 * @brief MDL-22：ZYX欧拉编辑值换算为既有主值EAA，不产生第二份安装矩阵。
 * @param rpy [in] {roll,pitch,yaw}，rad，分量必须有限（入口已校验）。
 * @return 轴乘角的旋转矢量，rad，角度主值[0,pi]；零旋转返回零矢量。
 *
 * 纯函数、无共享状态；半角四元数按qz*qy*qx组合，与上方RzRyRx同约定。
 * 采用atan2求角而不除sin(theta)，使pi附近及欧拉万向锁处仍有稳定轴角。
 */
inline rw::math::Vector3D<double> rpyToEaa(const rw::math::Vector3D<double>& rpy)
{
    const double cr=std::cos(rpy[0]/2),sr=std::sin(rpy[0]/2);
    const double cp=std::cos(rpy[1]/2),sp=std::sin(rpy[1]/2);
    const double cy=std::cos(rpy[2]/2),sy=std::sin(rpy[2]/2);
    double w=cr*cp*cy+sr*sp*sy;
    double x=sr*cp*cy-cr*sp*sy;
    double y=cr*sp*cy+sr*cp*sy;
    double z=cr*cp*sy-sr*sp*cy;
    // q与-q表示同一旋转。固定非负标量部以选主值轴角；归一化仅消除
    // 三角运算舍入，不修改用户姿态，也不缓存矩阵或叠加安装变换。
    const double norm=std::hypot(std::hypot(w,x),std::hypot(y,z));
    const double scale=(w<0?-1.0:1.0)/norm;
    w*=scale;x*=scale;y*=scale;z*=scale;
    const double sineHalf=std::hypot(x,y,z);
    // 仅精确零向量无可定义轴；小角保持atan2极限，不截断有效微小姿态。
    if(sineHalf==0){return {0,0,0};}
    const double angleScale=2*std::atan2(sineHalf,w)/sineHalf;
    return {x*angleScale,y*angleScale,z*angleScale};
}

}  // namespace sdurws::ird::modeling::rpymath

#endif  // IRD_MODELING_SRC_RPYMATH_HPP
