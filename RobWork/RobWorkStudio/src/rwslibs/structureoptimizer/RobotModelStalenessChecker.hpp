#ifndef RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP
#define RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP

#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>

#include <QString>

namespace rws {

/**
 * @brief 机器人源模型文件状态枚举。
 * 
 * 用于标识优化项目内部保存的冻结模型快照与磁盘外部源模型文件之间的数据一致性状态。
 */
enum class RobotModelSourceStatus
{
    Untracked,     //!< 未跟踪：未关联外部源模型文件，或项目独立运行
    Current,       //!< 最新/一致：磁盘源模型文件与项目内的冻结快照内容完全吻合
    Stale,         //!< 陈旧/过期：磁盘外部源模型已被修改，项目内的快照已过期
    SourceMissing, //!< 源文件丢失：引用的外部源模型文件在磁盘路径上不存在
    SourceInvalid  //!< 源文件无效：外部源文件存在但格式损坏、无法解析
};

/**
 * @brief 机器人模型陈旧度校验结果结构体。
 */
struct RobotModelStalenessResult
{
    RobotModelSourceStatus status = RobotModelSourceStatus::Untracked; //!< 源模型校验状态
    QString resolvedSourcePath;                                         //!< 解析出的磁盘源模型文件绝对路径
    QString message;                                                    //!< 详细的诊断/状态描述信息（用于 UI 状态栏或日志显示）
};

/**
 * @brief 机器人模型陈旧度检查器类。
 * 
 * 专门用于将优化项目文件中保存的“冻结参数快照 (RobotModelSpec)”与磁盘上真实的“参数化源模型文件”进行对比校验。
 * 能够自动侦测外部模型修改、丢失或破坏情况，协助 UI 决定是提示用户更新模型，还是强制降级使用内置快照。
 */
class RobotModelStalenessChecker
{
  public:
    /**
     * @brief 检查指定上下文中的模型快照是否与磁盘上的源模型文件保持一致。
     * 
     * 内部流程：
     *  1. 从 context 中提取源模型相对路径，结合 projectPath 解析出绝对路径 resolvedSourcePath；
     *  2. 检查源文件是否存在（不存在则返回 SourceMissing）；
     *  3. 读取并解析源文件（解析失败则返回 SourceInvalid）；
     *  4. 对比源文件内容与 context 内的 RobotModelSpec（内容/哈希不一致则返回 Stale）；
     *  5. 完全匹配则返回 Current。
     * 
     * @param context 机器人设计上下文（包含保存的冻结模型快照和关联的源文件路径）
     * @param projectPath 当前优化项目文件所在的磁盘路径（用作相对路径解析基准）
     * @return RobotModelStalenessResult 包含状态枚举、解析路径及描述信息的结果结构体
     */
    static RobotModelStalenessResult check(const RobotDesignContext& context,
                                           const QString& projectPath);

    /**
     * @brief 在托管工程（Managed Project）根目录下执行模型陈旧度检查。
     * 
     * 用于基于统一工程架构（RobWorkStudio Project Registry）的环境，
     * 允许显式传入 `managedProjectRoot` 作为托管工程的全局基准根目录来解析资源路径。
     * 
     * @param context 机器人设计上下文
     * @param projectPath 优化项目文件路径
     * @param managedProjectRoot 托管工程的全局根目录路径
     * @return RobotModelStalenessResult 校验结果结构体
     */
    static RobotModelStalenessResult checkManaged(const RobotDesignContext& context,
                                                  const QString& projectPath,
                                                  const QString& managedProjectRoot);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP