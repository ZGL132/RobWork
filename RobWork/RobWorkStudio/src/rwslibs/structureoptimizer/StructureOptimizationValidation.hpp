#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONVALIDATION_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONVALIDATION_HPP

#include "StructureOptimizationTypes.hpp"

#include <vector>

namespace rws {

//! @brief 对 StructureOptimizationProblem 执行一致性/完整性验证。
//!
//! 验证项目包括:
//!   - 上下文完整 (robotName + transformJoints / dhJoints)
//!   - 至少一个启用变量
//!   - 变量 ID 唯一性
//!   - 变量边界合法
//!   - 变量来源一致性 (不混用 DH 与 Transform)
//!   - 至少一个启用任务点
//!   - 权重合法
//!   - 候选/精英数合法
//!   - 覆盖网格单元格数在合法范围内
class StructureOptimizationValidation
{
public:
    //! @brief 判断模型规格是否具备完整上下文 (robotName 非空且 transformJoints 非空)。
    //!        失败时通过 reason 返回以稳定错误码为前缀的原因；成功时清空 reason。
    //!        Factory/ProjectAdapter/Template/Validation 共用此唯一判断入口。
    static bool hasCompleteModel(const RobotModelSpec& spec,
                                 std::string* reason = nullptr);

    //! @brief 验证问题定义, 返回发现的警告/错误列表。
    //!        空 vector 表示完全通过。
    static std::vector< AnalysisWarning > validateProblem(
        const StructureOptimizationProblem& problem);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONVALIDATION_HPP
