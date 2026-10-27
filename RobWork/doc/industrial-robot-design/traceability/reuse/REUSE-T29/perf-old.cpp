#include <sdurws/ird/runtime/BaseWorldTransform.hpp>
#include <rw/math/LinearAlgebra.hpp>
#include <Eigen/Geometry>
#include <cmath>
#include <string>
namespace t29old {
using namespace sdurws::ird::runtime;
constexpr double kOrthoTolerance=1e-12;
rw::math::Rotation3D<double> makeRotation(double r00, double r01, double r02,
                                          double r10, double r11, double r12,
                                          double r20, double r21, double r22)
{
    return rw::math::Rotation3D<double>(r00, r01, r02, r10, r11, r12,
                                        r20, r21, r22);
}
rw::math::Rotation3D<double> identityRotation()
{
    return makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
}
rw::math::Rotation3D<double> multiplyRotation(const rw::math::Rotation3D<double>& a,
                                              const rw::math::Rotation3D<double>& b)
{
    rw::math::Rotation3D<double> r(1, 0, 0, 0, 1, 0, 0, 0, 1);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += a(i, k) * b(k, j);
            }
            r(i, j) = sum;
        }
    }
    return r;
}
rw::math::Vector3D<double> rotateVector(const rw::math::Rotation3D<double>& r,
                                        const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v(0) + r(0, 1) * v(1) + r(0, 2) * v(2),
        r(1, 0) * v(0) + r(1, 1) * v(1) + r(1, 2) * v(2),
        r(2, 0) * v(0) + r(2, 1) * v(1) + r(2, 2) * v(2));
}
rw::math::Vector3D<double> rotateTransposed(const rw::math::Rotation3D<double>& r,
                                            const rw::math::Vector3D<double>& v)
{
    return rw::math::Vector3D<double>(
        r(0, 0) * v(0) + r(1, 0) * v(1) + r(2, 0) * v(2),
        r(0, 1) * v(0) + r(1, 1) * v(1) + r(2, 1) * v(2),
        r(0, 2) * v(0) + r(1, 2) * v(1) + r(2, 2) * v(2));
}
double determinant(const rw::math::Rotation3D<double>& r)
{
    return r(0, 0) * (r(1, 1) * r(2, 2) - r(1, 2) * r(2, 1))
         - r(0, 1) * (r(1, 0) * r(2, 2) - r(1, 2) * r(2, 0))
         + r(0, 2) * (r(1, 0) * r(2, 1) - r(1, 1) * r(2, 0));
}
double orthogonalityDeviation(const rw::math::Rotation3D<double>& r)
{
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // (RᵀR)(i,j)＝R 的第 i 列与第 j 列的点积。
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) {
                dot += r(k, i) * r(k, j);
            }
            const double dev = std::fabs(dot - (i == j ? 1.0 : 0.0));
            if (dev > worst) { worst = dev; }
        }
    }
    return worst;
}
std::string num(double v)
{
    return std::to_string(v);
}
rw::math::Rotation3D<double> rotationFromCustomEaa(const rw::math::Vector3D<double>& eaa)
{
    // 第一步：非有限拦截（NFR-COR-03——NaN/Inf 一经算术传播会污染整个
    // 矩阵且比较恒假，必须在入口拒绝）。
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(eaa(i))) {
            throw RuntimeError(RuntimeErrorCode::InputInvalid,
                               "base.customEaa：含非有限分量（index "
                               + std::to_string(i) + "）——单位 rad 的旋转"
                               "矢量须全部有限（NFR-COR-03）");
        }
    }

    // 第二步：角度 θ＝‖eaa‖（单位 rad）。零矢量＝零旋转（数学合法极限，
    // 恒等返回 I；其是否可入模型由 checkPresetConsistency 的"Custom 而
    // R＝I"一致性面判定——本函数只做纯换算，不做语义层拒绝）。
    const double norm = std::sqrt(eaa(0) * eaa(0) + eaa(1) * eaa(1)
                                  + eaa(2) * eaa(2));
    if (norm == 0.0) {
        return identityRotation();
    }

    // 第三步：Rodrigues 公式（轴 k＝eaa/θ，角 θ）：
    //   R＝I＋sin(θ)·[k]×＋(1−cos(θ))·[k]×²
    // 展开为逐元素标准形（[k]× 为反对称叉乘矩阵）——三角函数只作用于
    // 标量角度，矩阵组装为纯算术，同输入逐位同输出（确定性）。
    const double kx = eaa(0) / norm;
    const double ky = eaa(1) / norm;
    const double kz = eaa(2) / norm;
    const double c = std::cos(norm);
    const double s = std::sin(norm);
    const double bigC = 1.0 - c;

    return makeRotation(
        c + bigC * kx * kx,     bigC * kx * ky - s * kz, bigC * kx * kz + s * ky,
        bigC * kx * ky + s * kz, c + bigC * ky * ky,     bigC * ky * kz - s * kx,
        bigC * kx * kz - s * ky, bigC * ky * kz + s * kx, c + bigC * kz * kz);
}
rw::math::Transform3D<double> invertWorldBase(
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 前置防御：正交性是对合可逆前提（转置≠逆当 R 非正交）——调用方
    // 契约违约 fail-fast（§3.4 错误语义：编译链 S5 已拦截的输入再以
    // 非法形态到达消费函数属违约），不产出错误值静默吞错。
    const double orthoDev = orthogonalityDeviation(tWorldBase.R());
    if (orthoDev > kOrthoTolerance) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           "invertWorldBase：输入 R 非正交（max|RᵀR−I| 实测 "
                           + num(orthoDev) + "）——调用方须先经 "
                           "checkWorldBaseTransform（§6.6 容差 1e-12）");
    }

    // R 正交 ⇒ 逆旋转＝转置（原地 inverse()——rw 头内 inline 实现，
    // 两模式语义一致）；逆平移＝−Rᵀ·t（§6.5②：R_x(π)、t=(0,0,2) 时
    // Rᵀt=(0,0,−2)、t'＝−Rᵀt＝(0,0,+2)——卡文原 "(0,0,−2)" 为笔误，
    // 修正登记见 units/runtime.md §15.4 v0.7）。
    rw::math::Rotation3D<double> rInv = tWorldBase.R();
    rInv.inverse();
    const rw::math::Vector3D<double> tInv = rotateTransposed(tWorldBase.R(),
                                                             tWorldBase.P())
                                             * (-1.0);
    return rw::math::Transform3D<double>(tInv, rInv);
}
rw::math::Vector3D<double> transformBasePointToWorld(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pBase)
{
    // p_world＝R·p_base＋t（§6.5①——正向点变换；§6.5 手算例：
    // p_base=(0.3,0.2,0.5) 经倒挂 T 得 (0.3,−0.2,1.5)）。
    return rotateVector(tWorldBase.R(), pBase) + tWorldBase.P();
}
rw::math::Vector3D<double> transformWorldPointToBase(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pWorld)
{
    // p_base＝Rᵀ·(p_world−t)——T_base_world 作用于 p_world 的直接展开
    // （§6.5②：数学上等价于先 invertWorldBase 再复合，但按逆变换公式
    // 一次遍历完成，不构造中间变换对象；§6.5 手算例：p_world 回代得
    // 与输入逐位一致的 p_base）。向量的减法为 rw 头内 Eigen 实现
    // （header-only，两模式一致）。
    return rotateTransposed(tWorldBase.R(), pWorld - tWorldBase.P());
}
rw::math::Vector3D<double> gravityToBase(
    const rw::math::Rotation3D<double>& rWorldBase,
    const rw::math::Vector3D<double>& gWorld)
{
    // g_base＝Rᵀ·g_W（§6.1 乘法表——DYN-01 唯一公式；禁止清单 2 禁止
    // "倒挂就地取反"式的基座系参数化，必须经本投影。§6.5③ 手算：倒挂
    // 时 (0,0,−9.81)→(0,0,+9.81)——重力沿 +Z_base）。
    return rotateTransposed(rWorldBase, gWorld);
}
rw::math::Transform3D<double> composeWorldBaseTcp(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Transform3D<double>& tBaseTcp)
{
    // T_world_tcp＝T_world_base·T_base_tcp（§6.1；core §4.6 复合读法）：
    // 旋转 R_wc＝R_wb·R_bc，平移 t_wc＝R_wb·t_bc＋t_wb——逐元素实现，
    // 不经 rw Transform3D::operator*（其内部调 Rotation3D::multiply 外联
    // 符号，冒烟不可达；数学一致，见文件头实现要点）。
    const rw::math::Rotation3D<double> rWc = multiplyRotation(tWorldBase.R(),
                                                              tBaseTcp.R());
    const rw::math::Vector3D<double> tWc
        = rotateVector(tWorldBase.R(), tBaseTcp.P()) + tWorldBase.P();
    return rw::math::Transform3D<double>(tWc, rWc);
}
std::optional<RuntimeError> checkWorldBaseTransform(
    const rw::math::Transform3D<double>& tWorldBase)
{
    // 检查 1：有限性（先于正交性——NaN 参与的任何比较恒假，正交性
    // 检查对非有限矩阵无意义；§4.3.2"含非有限分量→InputInvalid"）。
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            if (!std::isfinite(tWorldBase.R()(i, j))) {
                return RuntimeError(
                    RuntimeErrorCode::InputInvalid,
                    "T_world_base.R：元素(" + std::to_string(i) + ","
                    + std::to_string(j) + ") 非有限——§4.3.2 合法域"
                    "（NFR-COR-03 非有限不静默）");
            }
        }
        if (!std::isfinite(tWorldBase.P()(i))) {
            return RuntimeError(
                RuntimeErrorCode::InputInvalid,
                "T_world_base.P：分量(" + std::to_string(i)
                + ") 非有限，单位 m——§4.3.2 合法域");
        }
    }

    // 检查 2：正交性——max|RᵀR−I| ≤ 1×10⁻¹²（§6.6 容差；detail 携带
    // 实测最大偏差＝比较型诊断素材，§6.6"实测偏差值的比较型诊断"）。
    const double orthoDev = orthogonalityDeviation(tWorldBase.R());
    if (orthoDev > kOrthoTolerance) {
        return RuntimeError(
            RuntimeErrorCode::InputInvalid,
            "T_world_base.R：非正交——max|RᵀR−I| 实测 "
            + num(orthoDev) + " 超容差 1e-12（§6.6；非法旋转/单位错误"
            "在 reader 侧拒绝后此处为编译链复核面）");
    }

    // 检查 3：非反射——正交矩阵 det＝±1，det＜0 即反射变换（行列式
    // −1 的"旋转"会把手性翻转，§4.3.2"det＝+1"的判定面；RT-BW-6
    // 反射矩阵反例的拦截点）。
    const double det = determinant(tWorldBase.R());
    if (det < 0.0) {
        return RuntimeError(
            RuntimeErrorCode::InputInvalid,
            "T_world_base.R：反射矩阵（det 实测 " + num(det)
            + "＜0）——§4.3.2 合法域 det=+1（RT-BW-6 反例面）");
    }

    return std::nullopt;
}
}

extern "C" __declspec(noinline) double old_eval(int scene,double v){
 using namespace rw::math;
 const Rotation3D<> r(1.0/3,2.0/3,2.0/3, 2.0/3,1.0/3,-2.0/3, -2.0/3,2.0/3,-1.0/3);
 const Transform3D<> a(Vector3D<>(3+v,-2,5),r),b(Vector3D<>(1,2,-1),Rotation3D<>(0,-1,0,1,0,0,0,0,1));
 double sum=0;
 if(scene==0 || scene==1){
  const auto out=scene==0?t29old::composeWorldBaseTcp(a,b):t29old::invertWorldBase(a);
  for(int i=0;i<3;++i){sum+=out.P()[i];for(int j=0;j<3;++j)sum+=out.R()(i,j);}
 }else if(scene==2){
  const auto out=t29old::gravityToBase(r,Vector3D<>(v,0,-9.81));for(int i=0;i<3;++i)sum+=out[i];
 }else if(scene==3 || scene==4){
  const auto out=t29old::rotationFromCustomEaa(scene==3?Vector3D<>(0.3+v,-0.7,1.1):Vector3D<>(1e-8+v*1e-7,0,0));
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)sum+=(i*3+j+1)*out(i,j);
 }else{sum=t29old::checkWorldBaseTransform(a).has_value()?1:2;}
 return sum;
}
