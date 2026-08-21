#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP

#include "CanonicalKinematicModel.hpp"

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 规范运动学模型的数据指纹计算结果
 * @details 记录针对规范模型（或其子集）进行确定性序列化与哈希计算后的特征摘要。
 * 主要用于缓存命中判定（如逆运动学校验表、工作空间点云、刚度矩阵预计算等）与模型版本一致性比对。
 */
struct KinematicFingerprintResult
{
    /**
     * @brief 指纹计算是否成功
     * @note 若模型数据不完整、序列化失败或存在未定义拓扑，该值为 false。
     */
    bool ok = false;

    /**
     * @brief 使用的哈希算法标识符
     * @note 默认采用 64 位 FNV-1a 非加密快速哈希算法（"fnv1a-64"），兼顾极高的计算吞吐量与低碰撞率。
     */
    std::string algorithmId = "fnv1a-64";

    /**
     * @brief 规范序列化协议版本号
     * @note 当规范模型字段增删或序列化浮点数字节序规则升级时变更，防止旧版缓存发生版本混淆。
     */
    std::string serializationVersion = "canonical-kinematic-model-v1";

    /**
     * @brief 计算得到的指纹哈希字符串（十六进制或固定长度字符串）
     */
    std::string value;

    /**
     * @brief 指纹生成过程中的诊断日志（包含格式异常、浮点数溢出警告等）
     */
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/**
 * @brief 规范运动学指纹与确定性哈希计算引擎
 * @details 负责将规范运动学模型及其子组件按确定性顺序（Canonical Serialization）序列化并生成唯一指纹。
 * 
 * 核心设计特征：
 * 1. **确定性与无歧义**：保证相同拓扑与尺寸参数在任意平台、任意内存布局下均生成完全一致的哈希值。
 * 2. **细粒度解耦**：将本体机构（Model）、场景环境（Environment）与末端工具（Tool）的指纹独立生成，
 *    实现精准的细粒度缓存失效（如仅更换焊枪时，无需清空本体连杆的动力学校验缓存）。
 * 3. **表现层数据正交隔离**：完全排除 UI 材质、渲染颜色等非物理属性，杜绝显示属性变化引发的无效重算。
 */
class KinematicFingerprint
{
public:
    /**
     * @brief 查询显示/渲染颜色是否参与指纹计算
     * @return 编译期常量 false
     * @note 规范模型设计上严格剥离了表现层属性（如 RGB/Alpha）。
     * 此静态断言明确表明：仅改变模型显示颜色绝不会导致模型、工具或环境的数据指纹失效。
     */
    static constexpr bool visualColorAffectsFingerprint() { return false; }

    /**
     * @brief 计算机器人本体运动学模型（Kinematic Model）的特征指纹
     * @param model 规范模型输入
     * @return KinematicFingerprintResult 包含机构拓扑（Frames/Joints/DOFs）、$SE(3)$ 相对位姿、连杆几何尺寸及限位约定的哈希结果
     */
    static KinematicFingerprintResult forModel(const CanonicalKinematicModel& model);

    /**
     * @brief 计算工作站场景与环境配置（Environment）的特征指纹
     * @param model 规范模型输入
     * @return KinematicFingerprintResult 包含环境固定障碍物、工作台基准及全局参考坐标系的哈希结果
     */
    static KinematicFingerprintResult forEnvironment(const CanonicalKinematicModel& model);

    /**
     * @brief 计算末端工具与执行器绑定（Tool Binding）的特征指纹
     * @param model 规范模型输入
     * @return KinematicFingerprintResult 包含法兰至 TCP 的静态变换 $T_{flange \to tcp}$、工具外形包络与工具碰撞体的哈希结果
     */
    static KinematicFingerprintResult forTool(const CanonicalKinematicModel& model);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP