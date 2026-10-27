#include <sdurws/ird/runtime/BaseWorldTransform.hpp>
#include <rw/math/LinearAlgebra.hpp>
#include <Eigen/Geometry>
#include <cmath>
#include <string>
namespace t29new {
using namespace sdurws::ird::runtime;
constexpr double kOrthoTolerance=1e-12;
double orthogonalityDeviation(const rw::math::Rotation3D<double>& r)
{
    // inverse(Rotation3D)按上游值契约取转置；此处检查的是Gram矩阵，
    // 即使待查R尚非正交也须使用转置，不能改成一般矩阵逆而让检查恒为I。
    const auto gram = rw::math::inverse(r) * r;
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // 保留原逐元素最大误差及阈值，不能换成另一个矩阵范数。
            const double dev = std::fabs(gram(i, j) - (i == j ? 1.0 : 0.0));
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
        return rw::math::Rotation3D<double>::identity();
    }

    // GAP-BW-EAA-01方案A：普通和微小非零角共用既有Eigen轴角实现，
    // 数值矩阵只在此投影成RobWork值；保留rad、旋转方向和原有限性契约。
    // 不调用已知会吞掉小于1e-6角度的EAA::axis，不自写小角公式或fallback。
    return rw::math::Rotation3D<double>(
        Eigen::AngleAxisd(norm, eaa.e() / norm).toRotationMatrix());
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

    // 前置保持，刚体逆的旋转/平移定义由RobWork维护，不再手写第二份展开。
    return rw::math::inverse(tWorldBase);
}
rw::math::Vector3D<double> transformBasePointToWorld(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Vector3D<double>& pBase)
{
    // p_world＝R·p_base＋t（§6.5①——正向点变换；§6.5 手算例：
    // p_base=(0.3,0.2,0.5) 经倒挂 T 得 (0.3,−0.2,1.5)）。
    return tWorldBase * pBase;
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
    return rw::math::inverse(tWorldBase.R()) * (pWorld - tWorldBase.P());
}
rw::math::Vector3D<double> gravityToBase(
    const rw::math::Rotation3D<double>& rWorldBase,
    const rw::math::Vector3D<double>& gWorld)
{
    // g_base＝Rᵀ·g_W（§6.1 乘法表——DYN-01 唯一公式；禁止清单 2 禁止
    // "倒挂就地取反"式的基座系参数化，必须经本投影。§6.5③ 手算：倒挂
    // 时 (0,0,−9.81)→(0,0,+9.81)——重力沿 +Z_base）。
    return rw::math::inverse(rWorldBase) * gWorld;
}
rw::math::Transform3D<double> composeWorldBaseTcp(
    const rw::math::Transform3D<double>& tWorldBase,
    const rw::math::Transform3D<double>& tBaseTcp)
{
    // T_world_tcp＝T_world_base·T_base_tcp；只在runtime定义安装来源，
    // 组合计算直接消费上游结果，不保留逐元素乘法helper或中间缓存。
    return tWorldBase * tBaseTcp;
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
    const double det = rw::math::LinearAlgebra::det(tWorldBase.R().e());
    if (det < 0.0) {
        return RuntimeError(
            RuntimeErrorCode::InputInvalid,
            "T_world_base.R：反射矩阵（det 实测 " + num(det)
            + "＜0）——§4.3.2 合法域 det=+1（RT-BW-6 反例面）");
    }

    return std::nullopt;
}
}

extern "C" __declspec(noinline) double new_eval(int scene,double v){
 using namespace rw::math;
 const Rotation3D<> r(1.0/3,2.0/3,2.0/3, 2.0/3,1.0/3,-2.0/3, -2.0/3,2.0/3,-1.0/3);
 const Transform3D<> a(Vector3D<>(3+v,-2,5),r),b(Vector3D<>(1,2,-1),Rotation3D<>(0,-1,0,1,0,0,0,0,1));
 double sum=0;
 if(scene==0 || scene==1){
  const auto out=scene==0?t29new::composeWorldBaseTcp(a,b):t29new::invertWorldBase(a);
  for(int i=0;i<3;++i){sum+=out.P()[i];for(int j=0;j<3;++j)sum+=out.R()(i,j);}
 }else if(scene==2){
  const auto out=t29new::gravityToBase(r,Vector3D<>(v,0,-9.81));for(int i=0;i<3;++i)sum+=out[i];
 }else if(scene==3 || scene==4){
  const auto out=t29new::rotationFromCustomEaa(scene==3?Vector3D<>(0.3+v,-0.7,1.1):Vector3D<>(1e-8+v*1e-7,0,0));
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)sum+=(i*3+j+1)*out(i,j);
 }else{sum=t29new::checkWorldBaseTransform(a).has_value()?1:2;}
 return sum;
}
