#ifndef RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP
#define RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP

#include <QString>

#include <string>

// 前置声明类型，避免包含不必要的头文件，加快编译速度
namespace rws {

struct FrozenRequirementValidationResult;
struct StructureOptimizationProblem;

} // namespace rws

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

/**
 * @brief 冻结工程需求项目导入服务类。
 * 
 * 该服务是 StructureOptimizer 模块在文件/磁盘层面对接上游 EngineeringRequirements 插件的边界通道。
 * 负责从磁盘读取已经“冻结”且持有完整审计指纹的 `frozenArtifact` 格式 JSON 文件，
 * 解析并加载其绑定的机器人模型规格 (RobotModelSpec)，并委托适配器生成 P2 阶段可执行的运动学优化问题结构体。
 * 
 * 核心安全规则：
 * 编辑态的需求即使包含任务点，也绝对无法通过本服务导入优化器！
 */
class FrozenRequirementProjectImportService
{
  public:
    //! @brief 计算两个绝对路径的公共目录，并保留 Windows UNC 根前缀。
    static QString commonDirectoryPath(const QString& first, const QString& second);

    /**
     * @brief 读取需求 JSON 文件，创建包含完整需求来源审计记录的结构优化问题。
     * 
     * 相对路径处理机制：
     * 模型绑定路径支持相对路径，且相对路径的基准根目录固定为需求 JSON 文件所在的目录。
     * 这确保了将整个工程文件夹（包含需求和模型）在不同电脑间整体移动后，仍能被一致地解析。
     * 
     * 事务安全机制：
     * 函数在内部使用局部临时副本完成全部的磁盘读取、模型解析与校验流程。
     * 只有在所有校验 100% 成功后，才会一次性更新传出参数 `problem`，
     * 彻底避免因导入中途失败而在 UI 上留下“半成品/脏数据”项目。
     * 
     * @param requirementPath 磁盘上冻结需求 JSON 文件的绝对路径
     * @param workcell 当前场景的 WorkCell 指针引用
     * @param state 当前场景的基准关节 State 引用
     * @param problem [out] 导入成功时用于接收构建完成的优化问题结构体（强事务安全）
     * @param validation [out] 可选的输出参数，接收需求冻结合规性校验的详细结果
     * @param error [out] 可选的输出错误描述信息指针
     * @return true 导入并创建项目成功；false 文件损坏、未冻结或模型加载失败
     */
    static bool createProblem(const QString& requirementPath,
                              const rw::models::WorkCell& workcell,
                              const rw::kinematics::State& state,
                              StructureOptimizationProblem& problem,
                              FrozenRequirementValidationResult* validation = nullptr,
                              std::string* error = nullptr);

    /**
     * @brief 读取需求 JSON 文件，并在显式指定的基准根目录下解析模型与工件。
     * 
     * 重载版本的 createProblem 函数，允许显式传入 `artifactBaseDirectory`。
     * 当需求 JSON 文件的实际位置与内部引用的 3D 几何/模型文件的根目录不一致时（如托管工程架构），
     * 使用此重载接口指定外部资源搜索基准。
     * 
     * @param requirementPath 磁盘上冻结需求 JSON 文件的绝对路径
     * @param workcell 当前场景的 WorkCell 指针引用
     * @param state 当前场景的基准关节 State 引用
     * @param problem [out] 导入成功时用于接收构建完成的优化问题结构体
     * @param validation [out] 可选的输出参数，接收需求校验结果
     * @param error [out] 可选的输出错误描述信息指针
     * @param artifactBaseDirectory 显式指定的外部 CAD/模型资源解析基准根目录
     * @return true 导入并创建项目成功；false 导入失败
     */
    static bool createProblem(const QString& requirementPath,
                              const rw::models::WorkCell& workcell,
                              const rw::kinematics::State& state,
                              StructureOptimizationProblem& problem,
                              FrozenRequirementValidationResult* validation,
                              std::string* error,
                              const QString& artifactBaseDirectory);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZER_FROZENREQUIREMENTPROJECTIMPORTSERVICE_HPP
