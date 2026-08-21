#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP

#include "KinematicImportResult.hpp"

#include <string>

// 前向声明 RobWork 核心运动学与模型实体
namespace rw {
namespace kinematics {
class Frame;
}
namespace models {
class Device;
class WorkCell;
}
}    // namespace rw

namespace rws {

struct RobotModelSpec;

/**
 * @brief 运动学模型导入请求契约（显式入参结构体）
 * @details 采用显式形式化选择策略：导入器遵循严格的契约设计，
 * 绝不对目标机构（Device）或工具中心点（TCP Frame）进行任何隐式推断或默认猜测，
 * 所有源数据与绑定目标必须由调用方显式指定。
 */
struct KinematicImportRequest
{
    /**
     * @brief RobWork 工作单元场景指针（借用指针，只读）
     * @note 提供整个装配场景树与坐标系拓扑关系的上下文。
     */
    const rw::models::WorkCell* workcell = nullptr;

    /**
     * @brief 待导入的目标机器人机构/设备指针（借用指针，只读）
     * @note 必须显式传入，严禁依赖场景中的“首个设备”等启发式推断。
     */
    const rw::models::Device* device = nullptr;

    /**
     * @brief 显式指定的工具中心点 (TCP) 坐标系指针（借用指针，只读）
     * @note 用于确定末端工具链与 Flange 到 TCP 的变换，若无工具可置空或指向末端法兰。
     */
    const rw::kinematics::Frame* tcpFrame = nullptr;

    /**
     * @brief 机器人模型规格参数快照（可选元数据，只读）
     * @note 包含导入时的结构参数、标称配置或外部规格定义。
     */
    const RobotModelSpec* sourceSnapshot = nullptr;

    /**
     * @brief 生成的规范模型全局唯一标识符 (UUID / 业务 ID)
     */
    std::string modelId;

    /**
     * @brief 原始模型源数据的哈希特征指纹
     * @note 用于校验源文件/设备定义是否发生变更或用于缓存失效判断。
     */
    std::string sourceFingerprint;

    /**
     * @brief 所属场景/环境配置的哈希特征指纹
     * @note 确保当前优化任务与特定的工作站环境上下文强绑定。
     */
    std::string environmentFingerprint;
};

/**
 * @brief 运动学模型导入器（静态无状态转换器）
 * @details 负责将受支持的 RobWork 串联运动链转换为内部规范的 $SE(3)$ 拓扑模型（CanonicalKinematicModel）。
 *
 * 核心设计原则：
 * 1. **只读性 (Read-Only)**：导入过程完全不修改传入的 RobWork 原生对象及场景树状态。
 * 2. **内存完全解耦 (Zero Lifetime Coupling)**：生成的导入结果采用值语义（Value Semantics），
 *    绝不保留、借用或持有任何来自 WorkCell、Device 或 Frame 的裸指针/引用，彻底杜绝悬空指针风险。
 * 3. **变换显式化**：自动提取各关节间的纯 $SE(3)$ 相对位姿矩阵、旋转/移动轴向量、零位偏移及限位约定。
 */
class KinematicModelImporter
{
public:
    /**
     * @brief 执行运动学导入与拓扑转换
     * @param request 显式填写的导入请求参数
     * @return KinematicImportResult 包含转换后的规范模型实体 (CanonicalKinematicModel) 及校验诊断信息
     */
    static KinematicImportResult import(const KinematicImportRequest& request);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICMODELIMPORTER_HPP
