/**
 * @file   InertiaMath.hpp
 * @brief  modeling 单元私有实现头——惯量张量到上游对称特征值分解的薄适配、
 *         物性断言②③（SPD＋三角不等式）核查（I-MDL-5 单一实现点）与
 *         物性估算输出自检（§9.4.6 @post——WP-13-T04）。
 *
 * 设计依据：units/modeling.md §4.10 I-MDL-5（"惯量对称（相对 1×10⁻¹²）
 * ＋SPD（严格>0）＋三角不等式"，附录 D 第 6/7 项）；§4.4"断言①②③同
 * 连杆"（工具与连杆共用同一判定）；§9.4.6 @post（估算输出自检——WP-13-T04，
 * synthesisSelfCheck 单一实现）。
 *
 * ★ 为什么是私有头（src/ 内、不入公共 include/）：R-2 红线——实现细节
 * 不跨单元暴露；本头只被本单元 RobotDesign.cpp／Parts.cpp 包含。单一
 * 实现的动机：连杆与工具的物性判定必须逐字同源（两份实现会在容差/
 * 判定式上漂移），故收敛到本头。
 *
 * 线程安全：全部纯函数，分解状态由调用栈及上游局部对象持有。
 * REUSE-T27：数值核由 RobWork 维护；同输入/工具链重复确定性由回归核对，
 * 不将解析公式或跨平台位级一致性作为未经验证的保证（NFR-COR-02）。
 */

#ifndef IRD_MODELING_SRC_INERTIAMATH_HPP
#define IRD_MODELING_SRC_INERTIAMATH_HPP

#include <array>
#include <cmath>

#include <rw/math/LinearAlgebra.hpp>  // REUSE-T27：复用原有 sdurw_math，不自持特征值核心。

#include <sdurws/ird/modeling/RobotDesign.hpp>  // InertiaTensor/InvariantViolation/InvariantId

namespace sdurws::ird::modeling::inertiamath {

/**
 * @brief 将惯量六分量投影到 RobWork 对称分解，返回升序主惯量（REUSE-T27）。
 *
 * 这里仅连接产品张量和上游矩阵接口：不计算第二份行列式/三角解析根，
 * 不缓存分解状态。旧解析公式的平方在极小/极大有限尺度会下溢/溢出，
 * 直接复用上游分解可消除该重复核心；SPD 与三角不等式仍由下方业务函数判定。
 *
 * @param t [in] 六分量对称张量（ixx,iyy,izz 主项；ixy,ixz,iyz 惯量积），
 *          单位 kg·m²；分量须有限（非有限由 I-MDL-3 层先报，本函数行为
 *          仅对有限输入有定义）
 * @return 升序特征值 (λmin, λmid, λmax)，单位 kg·m²
 * @throws std::bad_alloc 上游动态矩阵分配失败时向调用方传播；本私有函数
 *         不再声明 noexcept，以免把资源失败转换成进程终止，不吞错或另建诊断轨。
 *
 * 纯函数；线程安全；确定性。
 */
inline std::array<double, 3> symmetricEigenvalues3x3(const InertiaTensor& t)
{
    // 六分量是同一连杆参考系、同一参考点的 kg·m²；镜像填入两三角，
    // 不改变惯量积符号、不做平行轴迁移或额外归一化。上游返回值已升序。
    Eigen::MatrixXd matrix(3, 3);
    matrix << t.ixx, t.ixy, t.ixz,
              t.ixy, t.iyy, t.iyz,
              t.ixz, t.iyz, t.izz;
    const auto decomposition = rw::math::LinearAlgebra::eigenDecompositionSymmetric(matrix);
    return {decomposition.second[0], decomposition.second[1], decomposition.second[2]};
}

/**
 * @brief 惯量断言②③核查（SPD＋三角不等式）——违例即追加。
 *
 * @param out     [out] 违例接收容器（追加，不清空）
 * @param inertia [in] 待查惯量（SourcedValue——非 Provided 态直接跳过：
 *               缺失走 DataInsufficient 降级，MDL-06，不触发断言）
 * @param subject [in] 定位路径前缀（如 "links[0]"——本函数追加
 *               "<subject>.inertia.spd"/"<subject>.inertia.triangle"）
 *
 * 判定口径：
 *   - 对称性：六分量表示天然对称（附录 D 第 6 项由表示层结构性满足）；
 *   - SPD：最小特征值严格 > 0（第 7 项）；
 *   - 三角不等式：λmax ≤ λmid + λmin（严格比较——卡面未给容差，特征值
 *     来自统一的上游分解，不在本层增加第二容差）。
 *
 * 纯函数；线程安全；确定性。
 */
inline void checkInertiaAssertions(std::vector<InvariantViolation>& out,
                                   const core::SourcedValue<InertiaTensor>& inertia,
                                   const std::string& subject)
{
    const auto tensor = inertia.tryValue();
    if (!tensor.has_value()) { return; }

    const std::array<double, 3> eig = symmetricEigenvalues3x3(*tensor);
    if (!(eig[0] > 0.0)) {
        // SPD 违例：不再做三角不等式（非正定张量的主矩无椭球语义——避免
        // 一个张量报两条无关违例干扰修正回路）
        out.push_back(InvariantViolation{InvariantId::IMdl5, subject + ".inertia.spd"});
        return;
    }
    if (eig[2] > eig[1] + eig[0]) {
        out.push_back(InvariantViolation{InvariantId::IMdl5, subject + ".inertia.triangle"});
    }
}

// =====================================================================
// 估算输出自检（WP-13-T04 新增；卡 §9.4.6 @post"输出自检：对称＋SPD＋
// 三角不等式（自检失败=内部错误码，不输出非法张量）"的单一实现点）
// =====================================================================

/// 对称性容差：|M−Mᵀ| 逐元素上限 1×10⁻¹²（卡 acceptance 2 原文"对称相对
/// 1×10⁻¹²"——双精度旋转/平移合成引入的浮点噪声远低于此量级，超限即
/// 实现错误而非正常舍入）。附录 D 第 6 项同值的建模侧承载。
inline constexpr double kSynthesisSymmetryTolerance = 1e-12;

/**
 * @brief 估算合成结果自检（§9.4.6 @post——WP-13-T04 acceptance 2 的
 *        "反例拒收"守卫）。
 *
 * 为什么在提取六分量前对全 3×3 矩阵自检：合成累加 R·I·Rᵀ＋平行轴项在
 * 全矩阵上进行，(i,j) 与 (j,i) 两次浮点路径不同，理论上可产生不对称
 * 噪声；六分量表示只存一个三角，提取前若不核查，不对称会被表示层
 * "结构性对称"掩盖（I-MDL-5 的对称检查因此永远通过——自检空转）。
 * 本函数在提取前封住该通道。
 *
 * 核查序（失败即短路返回原因，NFR-COR-03 不修复不截断）：
 *   ①有限性：九元素全有限（NaN/Inf——如量纲溢出到 inf——立即拒收）；
 *   ②对称性：|M(i,j)−M(j,i)| ≤ 1×10⁻¹²（kSynthesisSymmetryTolerance）；
 *   ③SPD＋三角不等式：经六分量投影（容差内的对称化取平均——两三角
 *     差已由②保证 ≤1×10⁻¹²，投影不引入超容差改动）复用
 *     symmetricEigenvalues3x3 判定 λmin>0 且 λmax ≤ λmid+λmin。
 *
 * 物理背景（为什么合法输入下此自检应恒过）：SPD 张量经正交相似变换
 * （R·I·Rᵀ）保持 SPD，加平行轴项 m((d·d)E−ddᵀ)（半正定秩 2）仍 SPD，
 * 凸组合保持 SPD——故 SynthesisFailed 触发即内部实现错误（§9.4.6 把它
 * 归为"内部错误码"的原因）。
 *
 * @param m [in] 合成累加完成的全 3×3 矩阵（kg·m²；元素须已按同一合成
 *          次序计算——本函数不重算，只核查）
 * @return 空串＝自检通过；非空＝失败原因（英文稳定短语，进
 *         EstimateError.detail 的内部定位串——不直接呈现给用户）
 *
 * 纯函数；线程安全；相同输入/工具链的重复确定性由特征值及消费者回归验证。
 */
inline std::string synthesisSelfCheck(const std::array<std::array<double, 3>, 3>& m)
{
    // ①有限性：任何 NaN/Inf（含大尺寸溢出场景）都不能作为"物性"输出。
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(m[r][c])) {
                return "non-finite-entry";
            }
        }
    }
    // ②对称性（相对 1×10⁻¹²）：逐元素核查两三角的偏差。
    for (int r = 0; r < 3; ++r) {
        for (int c = r + 1; c < 3; ++c) {
            const double diff = m[r][c] - m[c][r];
            if (!(diff <= kSynthesisSymmetryTolerance
                  && diff >= -kSynthesisSymmetryTolerance)) {
                return "asymmetric-beyond-tolerance";
            }
        }
    }
    // ③六分量投影（容差内对称化平均——见函数注）＋SPD/三角不等式。
    InertiaTensor t;
    t.ixx = m[0][0];
    t.iyy = m[1][1];
    t.izz = m[2][2];
    t.ixy = 0.5 * (m[0][1] + m[1][0]);
    t.ixz = 0.5 * (m[0][2] + m[2][0]);
    t.iyz = 0.5 * (m[1][2] + m[2][1]);
    const std::array<double, 3> eig = symmetricEigenvalues3x3(t);  // 升序
    if (!(eig[0] > 0.0)) {
        return "not-positive-definite";  // SPD 严格>0（附录 D 第 7 项口径）
    }
    if (eig[2] > eig[1] + eig[0]) {
        return "triangle-inequality-violated";  // 惯性椭球三角不等式
    }
    return {};
}

}  // namespace sdurws::ird::modeling::inertiamath

#endif  // IRD_MODELING_SRC_INERTIAMATH_HPP
