#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

/**
 * @brief 结构优化 UI 业务逻辑与智能辅助工具类。
 *
 * 该类提供纯静态方法，用于在 UI 界面层完成“根据机器人模型自动推荐设计变量”
 * 以及“检查当前优化问题配置是否满足启动运行条件”等业务逻辑。
 */
class StructureOptimizationUiLogic
{
public:
    /**
     * @brief 根据输入的机器人设计上下文，自动分析并生成建议的设计变量列表。
     *
     * 该函数会遍历 RobotDesignContext 中包含的模型关节变换 (transformJoints)、
     * 工具坐标系 (ToolFrame)、基座安装高度 (BaseHeight) 以及连杆几何体 (drawables)，
     * 自动提取非零的位移与尺寸参数，为其生成默认的取值范围 [0.7*v, 1.3*v] 和步长，
     * 帮助用户快速初始化设计变量配置。
     *
     * @param context 机器人设计上下文 (包含 RobotModelSpec 数据结构)
     * @return std::vector<StructureDesignVariable> 自动推荐生成的结构设计变量列表
     */
    static std::vector<StructureDesignVariable> suggestVariables(
        const RobotDesignContext& context);

    /**
     * @brief 检查当前结构优化问题的输入配置是否合法且具备可运行条件。
     *
     * 在 UI 界面点击“开始优化”前调用此函数校验。主要检查项包括：
     *  1. 问题的基本完整性与模型校验 (通过 StructureOptimizationValidation)；
     *  2. 是否包含至少一个处于启用状态 (enabled == true) 的设计变量；
     *  3. 是否包含至少一个处于启用状态 (point.enabled == true) 的任务点。
     *
     * @param problem 待检查的完整结构优化问题定义对象
     * @param reason  [out] 可选的输出参数。若检查不通过，用于接收不可运行的具体原因描述字符串
     * @return true  问题配置完整无误，可以启动优化
     * @return false 问题配置存在缺陷，无法启动优化
     */
    static bool hasRunnableInputs(const StructureOptimizationProblem& problem,
                                  std::string* reason = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP