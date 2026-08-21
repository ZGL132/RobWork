#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICCONVENTIONS_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICCONVENTIONS_HPP

#include <rw/math/Rotation3D.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

namespace rws {

/**
 * @brief 规范 $\text{SE}(3)$ 关节运动学方程所使用的运动类型枚举。
 *
 * @details
 * 显式区分关节的物理自由度类型：
 * - Revolute: 旋转关节（绕指定运动轴旋转，单位强制为弧度 rad）
 * - Prismatic: 移动/棱柱关节（沿指定运动轴平移，单位强制为米 m）
 * - Fixed: 固定连接（无内部运动自由度，运动变换恒为单位矩阵 Identity）
 */
enum class CanonicalJointMotion
{
    Revolute,   ///< 旋转关节 (rad)
    Prismatic,  ///< 移动关节 (m)
    Fixed       ///< 固定连接 (无自由度)
};

/**
 * @brief 依附于基准单位轴的稳定切平面正交基底。
 *
 * @details
 * 对于给定的基准单位运动轴 $\mathbf{n}$，在与其正交的切平面上构造两个相互正交的单位切向量 $(e_1, e_2)$，
 * 满足 $e_1 \cdot \mathbf{n} = 0$、$e_2 \cdot \mathbf{n} = 0$ 且 $e_1 \cdot e_2 = 0$。
 * 该基底用于支持无奇异性、无万向节死锁的局部轴偏转参数化（$\alpha, \beta$ 或 $U, V$ 锥面坐标）。
 */
struct TangentBasis
{
    rw::math::Vector3D<> first;  ///< 切平面第一基底向量 $e_1$
    rw::math::Vector3D<> second; ///< 切平面第二基底向量 $e_2$
    bool valid = false;          ///< 标记基底是否成功构造（若输入轴退化或非有限值则为 false）
};

/**
 * @brief 规范运动学模型共享的纯数学约定与几何计算辅助类。
 *
 * @details
 * 【核心架构与数学约定】：
 * 1. 唯一的 $\text{SE}(3)$ 空间关节级联公式：
 *    $$T_{\text{parent\_child}} = T_{\text{parent\_jointZero}} \cdot \text{Motion}(\mathbf{axis}, q_{\text{model}}) \cdot T_{\text{jointMotion\_child}}$$
 * 2. 内部模型坐标约定：
 *    $$q_{\text{model}} = q_{\text{input}} + \text{zeroPositionOffset}$$
 * 3. 严格的国际单位制：长度/平移一律为米（$\text{m}$），角度/旋转一律为弧度（$\text{rad}$）。
 * 4. 纯函数设计：本类为无状态工具类，不包含欧拉角或 DH 状态，不依赖任何 Qt Widget 或可变 WorkCell，可直接用于多线程并行计算。
 */
class KinematicConventions
{
  public:
    /**
     * @brief 计算内部运动学模型实际使用的关节坐标 $q_{\text{model}}$。
     *
     * @param inputCoordinate 外部输入/驱动器视角的关节位置 $q_{\text{input}}$（旋转用 rad，移动用 m）
     * @param zeroPositionOffset 关节零位偏置（Zero Position Offset）
     * @return 实际代入运动学方程的模型坐标 $q_{\text{model}} = q_{\text{input}} + \text{zeroPositionOffset}$
     */
    static double modelCoordinate(double inputCoordinate, double zeroPositionOffset);

    /**
     * @brief 计算仅由关节自身运动产生的局部 $\text{SE}(3)$ 刚体变换 $\text{Motion}$。
     *
     * @param type 关节运动类型（Revolute / Prismatic / Fixed）
     * @param axis 关节运动方向的单位向量（单位：归一化无量纲向量）
     * @param modelCoordinate 经过零位修正后的内部模型坐标 $q_{\text{model}}$
     * @return 局部运动产生的刚体变换：
     *         - Revolute: $\text{Rot}(\mathbf{axis}, q_{\text{model}})$
     *         - Prismatic: $\text{Trans}(\mathbf{axis} \cdot q_{\text{model}})$
     *         - Fixed: $\text{Transform3D::identity}()$
     */
    static rw::math::Transform3D<> jointMotion(CanonicalJointMotion type,
                                               const rw::math::Vector3D<>& axis,
                                               double modelCoordinate);

    /**
     * @brief 按照冻结的标准数学公式合成从父坐标系到子坐标系的完整 $\text{SE}(3)$ 变换。
     *
     * @param parentToJointZero 从父坐标系原点到关节零位安装面的固定安装位姿 $T_{\text{parent\_jointZero}}$
     * @param type 关节运动类型（旋转 / 移动 / 固定）
     * @param axis 关节运动轴单位向量
     * @param inputCoordinate 外部输入坐标 $q_{\text{input}}$
     * @param zeroPositionOffset 零位偏置
     * @param jointMotionToChild 从关节运动输出端到子坐标系的固定安装位姿 $T_{\text{jointMotion\_child}}$
     * @return 合成后的完整父子变换 $T_{\text{parent\_child}}$
     */
    static rw::math::Transform3D<> composeJointTransform(
        const rw::math::Transform3D<>& parentToJointZero,
        CanonicalJointMotion type,
        const rw::math::Vector3D<>& axis,
        double inputCoordinate,
        double zeroPositionOffset,
        const rw::math::Transform3D<>& jointMotionToChild);

    /**
     * @brief 为给定的基准单位轴确定性地构建一组正交切平面基底 $(e_1, e_2)$。
     *
     * @details
     * 采用无随机性的确定性正交化算法（如选取与基准轴夹角最大的全局轴进行施密特正交化），
     * 确保在不同平台、多线程以及重复运行中得到的切向量完全一致。
     *
     * @param referenceAxis 基准运动轴单位向量
     * @return 包含切向量 $(e_1, e_2)$ 及其有效性标志的 TangentBasis 结构体
     */
    static TangentBasis stableTangentBasis(const rw::math::Vector3D<>& referenceAxis);

    /**
     * @brief 使用局部切平面坐标 $(\alpha, \beta)$ 计算偏转后的全新单位运动轴。
     *
     * @details
     * 采用精确的球面指数偏转公式：
     * 令偏转角模长 $\rho = \sqrt{\alpha^2 + \beta^2}$：
     * - 当 $\rho = 0$ 时，直接返回基准轴 $\mathbf{n}$（避免浮点除零）；
     * - 当 $\rho > 0$ 时，$\mathbf{axis} = \cos(\rho)\mathbf{n} + \frac{\sin(\rho)}{\rho}(\alpha e_1 + \beta e_2)$。
     * 得到的向量模长恒为 1，且偏转角严格等于 $\rho$。
     *
     * @param referenceAxis 偏转前的名义基准轴单位向量 $\mathbf{n}$
     * @param alpha 切向基底 $e_1$ 上的偏转分量（rad）
     * @param beta 切向基底 $e_2$ 上的偏转分量（rad）
     * @return 偏转后的全新单位运动轴
     */
    static rw::math::Vector3D<> tiltedAxis(const rw::math::Vector3D<>& referenceAxis,
                                           double alpha,
                                           double beta);

    /**
     * @brief 计算两个三维向量之间的空间夹角（夹角范围 $[0, \pi]$）。
     *
     * @param first 第一个三维向量
     * @param second 第二个三维向量
     * @return 向量夹角（单位：弧度 rad）
     */
    static double angleBetween(const rw::math::Vector3D<>& first,
                               const rw::math::Vector3D<>& second);

    /**
     * @brief 校验给定的 $3 \times 3$ 矩阵是否为合法的真旋转矩阵（属于特殊正交群 $\text{SO}(3)$）。
     *
     * @details
     * 严格检查以下两个几何条件：
     * 1. 矩阵的正交性：$R R^T \approx I$（列/行向量相互正交且模长为 1）；
     * 2. 矩阵的纯旋转性：$\det(R) \approx +1$（排除行列式为 -1 的镜像翻转或缩放退化）。
     *
     * @param rotation 待校验的 Rotation3D 对象
     * @param tolerance 浮点数值容差（默认 $1\times 10^{-10}$）
     * @return true 若严格满足正交性且行列式为 1；false 否则
     */
    static bool isProperRotation(const rw::math::Rotation3D<>& rotation,
                                 double tolerance = 1e-10);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICCONVENTIONS_HPP