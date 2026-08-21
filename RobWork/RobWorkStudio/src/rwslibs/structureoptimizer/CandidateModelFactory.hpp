#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP

#include "StructureOptimizationTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <memory>
#include <QTemporaryDir>
#include <string>
#include <vector>

// 前置声明 RobWork 的核心类，避免引入过重的头文件，提高编译速度
namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; } }

namespace rws {

/**
 * @brief 候选解模型仿真构件结构体 (工厂生产出来的“全家桶”产品)。
 * 
 * 当工厂成功将算法参数构建为 3D 仿真模型后，会将 RobWork 引擎计算所需的
 * 所有核心对象打包放进该结构体中，供后续的运动学求解器和碰撞检测器直接使用。
 */
struct CandidateModelArtifact {
    rw::core::Ptr<rw::models::WorkCell> workcell;          //!< RobWork 的 3D 场景工作单元 (包含机器人和周围环境)
    rw::core::Ptr<rw::models::Device> device;              //!< 机器人运动学设备对象 (如 6 轴机械臂运动链)
    rw::kinematics::State state;                            //!< 机器人的默认关节状态与树状坐标系状态向量
    rw::core::Ptr<const rw::kinematics::Frame> tcpFrame;   //!< 末端工具中心点 (TCP) 的坐标系指针
    rw::core::Ptr<rw::proximity::CollisionDetector> collisionDetector; //!< 基于当前 WorkCell 构建的碰撞检测器引擎
    
    /**
     * @brief 托管的临时磁盘目录指针。
     * 
     * 实例化时工厂会在系统的 Temp 目录创建临时文件夹，将模型编译为临时 XML/WC 文件。
     * 当该 shared_ptr 的引用计数归零析构时，QTemporaryDir 会自动清空并删除磁盘上的临时文件，
     * 从而完美解决多候选解频繁编译产生的磁盘垃圾问题。
     */
    std::shared_ptr<QTemporaryDir> temporaryDirectory;
};

/**
 * @brief 候选解模型构建请求结构体 (给工厂下达的“订单”)。
 */
struct CandidateModelBuildRequest {
    RobotModelSpec spec;                 //!< 经过参数变异后的机器人几何与运动学规格描述
    std::string deviceName;              //!< 目标机器人在场景中的名称 (如 "UR10" 或 "Robot")
    std::string tcpFrame;                //!< 目标 TCP 坐标系的名称 (如 "Tool0" 或 "TCP")
    bool checkCollision = true;          //!< 构建时是否同步初始化碰撞检测引擎 (若仅算简单 Kinematics 可关闭以提速)
    
    /**
     * @brief 场景快照指针 (只读借用)。
     * 
     * 注意：此指针仅在 build() 调用期间被临时借用。场景快照属于优化问题上下文，
     * 工厂绝不会保存该指针，从而确保多线程或并行评估候选解时，不会共享和篡改同一份场景数据。
     */
    const StructureOptimizationScenarioSnapshot* scenarioSnapshot = nullptr;
    std::string scenarioBaseDirectory;   //!< 场景外部资源文件 (如 3D 牙模 CAD/STL 文件) 的基准加载路径
};

/**
 * @brief 候选解模型构建结果结构体。
 */
struct CandidateModelBuildResult {
    bool ok = false;                     //!< 模型构建是否成功 (true: 成功生成 WorkCell 和碰撞引擎; false: XML 解析或加载失败)
    CandidateModelArtifact artifact;     //!< 构建成功时生成的仿真构件产品
    std::vector<AnalysisWarning> warnings; //!< 构建过程产生的警告或解析错误信息列表
};

/**
 * @brief 候选解模型构建工厂类。
 * 
 * 负责将数据层面的 RobotModelSpec（包含变异后的连杆长度、关节限位、CAD 引用等）
 * 编译并解析为 RobWork 物理仿真引擎可以立刻运行的内存模型 (WorkCell / Device / CollisionDetector)。
 */
class CandidateModelFactory {
  public:
    /**
     * @brief 在修改保存目录前，解析并补全模型引用的外部几何文件 (CAD/STL) 的相对路径。
     * @param spec [in, out] 待处理的机器人模型规格
     */
    static void resolveExternalAssetPaths(RobotModelSpec& spec);

    /**
     * @brief 根据指定的基准路径，解析模型中引用的外部 3D 几何文件路径。
     * @param spec [in, out] 待处理的机器人模型规格
     * @param baseDirectory 外部 CAD 资源所在的根目录路径
     */
    static void resolveExternalAssetPaths(RobotModelSpec& spec,
                                          const std::string& baseDirectory);

    /**
     * @brief 将冻结需求中的工装/工件场景合并入候选机器人的规格描述中。
     * 
     * 核心设计哲学：
     * 候选解评价、UI 3D 预览和最终 XML 模型导出**必须**调用同一个合并入口！
     * 方法会仅保留候选机器人经过设计变量变异后的本体，同时从场景快照 (snapshot) 中复制
     * 外部的工装 Frame、环境 CAD 几何以及对应的碰撞模型。
     * 这样可以彻底避免“评估时用套场景，导出模型时用另一套场景”的不可审计偏差。
     * 
     * @param spec [in, out] 待插入环境的机器人规格描述
     * @param snapshot 车间工装/环境的场景快照
     * @param baseDirectory 环境资源文件的基准路径
     */
    static void applyScenarioSnapshot(
        RobotModelSpec& spec,
        const StructureOptimizationScenarioSnapshot& snapshot,
        const std::string& baseDirectory = {});

    /**
     * @brief 生产构建核心函数：执行从数据规格到 RobWork 内存模型的编译过程。
     * 
     * 内部流程：
     *  1. 合并场景快照 (applyScenarioSnapshot)；
     *  2. 创建独立的 QTemporaryDir 临时文件夹；
     *  3. 将 RobotModelSpec 写入为临时的 RobWork XML 文件；
     *  4. 调用 RobWork 的 XMLRWLoader 加载该文件生成 WorkCell 指针；
     *  5. 检索 Target Device 和 TCP Frame；
     *  6. 初始化 CollisionDetector 并打包为 CandidateModelArtifact 返回。
     * 
     * @param request 构建请求参数
     * @return CandidateModelBuildResult 包含构建状态及成果构件的结果对象
     */
    CandidateModelBuildResult build(const CandidateModelBuildRequest& request);
};

} // namespace rws
#endif // RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP
