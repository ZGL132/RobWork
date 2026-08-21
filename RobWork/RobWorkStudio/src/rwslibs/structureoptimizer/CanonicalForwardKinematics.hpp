#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP

#include "CanonicalKinematicModel.hpp"

#include <map>

namespace rws {

/**
 * @brief 正向运动学 (FK) 求解结果结构体
 * @details 具备不可变属性的纯数据输出对象。保存所有在规范模型中已注册的坐标系
 * 相对于场景根坐标系（Root Frame）或基座标系（Base Frame）的全局 $SE(3)$ 位姿变换。
 */
struct CanonicalForwardKinematicsResult
{
    /**
     * @brief 正运动学计算是否成功有效
     * @note 若输入的关节向量维度不匹配、存在断链或未定义坐标系，该值将被置为 false。
     */
    bool valid = false;

    /**
     * @brief 各坐标系的全局位姿映射表
     * @details 键（Key）为规范坐标系的唯一标识符 `frameId`，
     * 值（Value）为从根坐标系（Root/World）到该坐标系的全局变换矩阵 $T_{root \to frame} \in SE(3)$。
     */
    std::map< std::string, rw::math::Transform3D<> > frameTransforms;

    /**
     * @brief 正运动学求解过程中的诊断与异常日志列表
     * @note 记录如关节越界告警、未映射自由度或奇异配置等诊断信息。
     */
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/**
 * @brief 规范正向运动学计算器（无状态纯计算引擎）
 *
 * @details 负责遍历规范运动学模型中的 $SE(3)$ 链，求解每个坐标系在给定关节配置 $Q$ 下的精确空间位姿。
 * 
 * 核心设计哲学与约束：
 * 1. **无副作用 (Side-Effect Free)**：计算过程为纯只读操作，不修改模型拓扑结构，也不变动输入的 $Q$ 向量。
 * 2. **独立关节运动学委托**：单关节的局部运动变换求解公式统一委托给 `KinematicConventions` 模块计算。
 * 3. **严禁隐式截断 (No Silent Clamping)**：本求解器**特意不对关节运行限位做强制截断截流**。
 *    限位策略由上层校验/评估层全权管理；若传入的 $Q$ 超出物理或软限位，求解器必须忠实计算其实际位姿并通过
 *    `diagnostics` 上报越界，绝不能在后台静默将 $Q$ 截断到限位区间内，以保证数值梯度的连续性与物理真实性。
 */
class CanonicalForwardKinematics
{
public:
    /**
     * @brief 执行全模型正向运动学求解
     * @param model 规范运动学拓扑模型（包含坐标系树、关节边及约束）
     * @param q 机械臂当前关节状态向量 $Q = [q_0, q_1, \dots, q_{n-1}]^T$（弧度/米）
     * @return CanonicalForwardKinematicsResult 包含所有坐标系全局位姿的映射表与计算诊断
     * 
     * @details 计算流程通过对机构链进行前向级联乘法完成：
     * $$T_{root \to child} = T_{root \to parent} \cdot T_{parent \to jointZero} \cdot T_{motion}(q_i) \cdot T_{jointMotion \to child}$$
     */
    static CanonicalForwardKinematicsResult evaluate(const CanonicalKinematicModel& model,
                                                     const std::vector< double >& q);

    /**
     * @brief 安全查询指定坐标系的全局位姿变换
     * @param[in] result 预先求解出的正运动学结果对象
     * @param[in] frameId 待查询的目标坐标系 ID（如 "TCP", "Link_4", "Flange"）
     * @param[out] transform 若查询成功，将该坐标系的全局变换 $T_{root \to frame}$ 写入该引用
     * @param[out] diagnostic 可选参数。若查找失败且传入了有效指针，则写入详细的错误诊断信息
     * @return bool 若找到该坐标系且 result 有效则返回 true，否则返回 false
     */
    static bool frameTransform(const CanonicalForwardKinematicsResult& result,
                               const std::string& frameId,
                               rw::math::Transform3D<>& transform,
                               StructureOptimizationDiagnostic* diagnostic = nullptr);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP