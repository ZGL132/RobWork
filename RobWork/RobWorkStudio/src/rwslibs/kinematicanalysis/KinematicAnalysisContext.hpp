// =============================================================================
//  KinematicAnalysisContext.hpp —— 运动学分析上下文(声明)
// =============================================================================
//
// 本组件定义所有运动学评估器共享的"上下文"数据结构:
//   - CancellationToken:轻量取消令牌,长任务(如区域覆盖率扫描)据此响应中断;
//   - AnalysisContextInput:调用方提供的原始输入;
//   - AnalysisContext:由 makeAnalysisContext 校验并归一化后的评估上下文,
//     所有评估器(ConfigurationEvaluator / TargetEvaluator / RegionCoverageEvaluator)
//     都以它作为第一参数,保证阈值、TCP 帧、碰撞检测器等配置在插件内口径一致;
//   - makeAnalysisContext:入口函数,负责必填项校验、默认名回填与能力告警注入。
//
// 设计要点:
//   - AnalysisContext 中的 capabilityWarnings 是"能力缺失但可继续"的告警集合
//     (例如要求无碰撞却没有碰撞检测器),评估器会把它们透传到每个结果,统一呈现;
//   - 评估器不直接读取 AnalysisContextInput,统一经 makeAnalysisContext 归一化,
//     避免"同一份输入在不同评估器里被解释成不同语义"。
#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISCONTEXT_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISCONTEXT_HPP

#include "KinematicAnalysisTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <string>
#include <vector>

namespace rws {

// =============================================================================
//  CancellationToken —— 轻量取消令牌
// =============================================================================
//
// 用函数指针而非虚基类实现,便于从任意 UI 环境(回调 / 控件状态)接入:
// 调用方把"查询函数"与透传数据封装进来即可。cancellationRequested() 在
// isCancellationRequested 为空时恒返回 false,即"从不取消",保证无 UI 场景安全。
struct CancellationToken
{
    // 查询是否已请求取消的函数指针;为 nullptr 表示永不取消。
    bool (*isCancellationRequested) (void* userData) = nullptr;
    // 随查询函数透传的用户数据(通常为 UI 状态指针)。
    void* userData = nullptr;

    // 便捷查询:仅当函数指针非空时才调用,返回是否应中止当前长任务。
    bool cancellationRequested () const
    {
        return isCancellationRequested != nullptr && isCancellationRequested (userData);
    }
};

// =============================================================================
//  AnalysisContextInput —— 评估上下文的原始输入
// =============================================================================
//
// 调用方(插件入口 / UI)在此填入原始数据;由 makeAnalysisContext 校验并归一化
// 为 AnalysisContext。字段尽量保持"可空",校验规则集中在工厂函数中,
// 使调用方能以最小负担传入可能不完整的上下文。
struct AnalysisContextInput
{
    // 环境 WorkCell;为空时多数评估器无法运行。
    rw::core::Ptr< rw::models::WorkCell > workcell;
    // 被评估的设备;为空时评估器拒绝(NoDevice)。
    rw::core::Ptr< rw::models::Device > device;
    // 工具坐标系帧(TCP);为空时可回退到设备默认末端帧。
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame;
    // 评估基准状态(含设备当前关节值),IK / FK 均从该状态派生。
    rw::kinematics::State baseState;
    // 碰撞检测器;仅当需要碰撞检查时必填。
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector;
    // 设备显示名;为空时回退为 device->getName()。
    std::string deviceName;
    // TCP 帧显示名;为空时回退为 tcpFrame->getName()。
    std::string tcpFrameName;
    // 模型指纹;为空则上下文构造失败(缓存与溯源依赖它)。
    std::string modelFingerprint;
    // 环境指纹;为空则上下文构造失败(缓存与溯源依赖它)。
    std::string environmentFingerprint;
    // 所有"近限位 / 近奇异"判定阈值;由调用方统一提供。
    KinematicThresholds thresholds;
    // 是否要求无碰撞;为真但缺少碰撞检测器时产生能力告警而非硬失败。
    bool collisionRequired = false;
};

// =============================================================================
//  AnalysisContext —— 归一化后的评估上下文(评估器的唯一数据来源)
// =============================================================================
//
// 由 makeAnalysisContext 从 AnalysisContextInput 校验生成:必填项缺失直接构造
// 失败,可回填项(名称 / 能力告警)在此归一化。所有评估器都以它为准,
// 因此评估结果对"输入如何被解释"是确定的。
struct AnalysisContext
{
    // 环境 WorkCell(经校验非空)。
    rw::core::Ptr< rw::models::WorkCell > workcell;
    // 被评估设备(经校验非空)。
    rw::core::Ptr< rw::models::Device > device;
    // TCP 帧(经校验非空)。
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame;
    // 评估基准状态(含设备当前关节值)。
    rw::kinematics::State baseState;
    // 碰撞检测器(可为空,仅碰撞检查时使用)。
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector;
    // 设备名(已回填默认值)。
    std::string deviceName;
    // TCP 帧名(已回填默认值)。
    std::string tcpFrameName;
    // 模型指纹(已校验非空)。
    std::string modelFingerprint;
    // 环境指纹(已校验非空)。
    std::string environmentFingerprint;
    // 判定阈值集合。
    KinematicThresholds thresholds;
    // 是否要求无碰撞。
    bool collisionRequired = false;
    // 能力缺失告警(如"要求无碰撞但无检测器"):评估器透传到每个结果统一呈现。
    std::vector< AnalysisWarning > capabilityWarnings;
};

// -----------------------------------------------------------------------------
// makeAnalysisContext —— 输入 -> 上下文的工厂
// -----------------------------------------------------------------------------
//
// 校验规则:workcell / device / tcpFrame 与两个指纹任一为空即失败(经 error
// 返回诊断码,如 KIN_CONTEXT_NO_WORKCELL);deviceName / tcpFrameName 为空时
// 回填实际对象名;collisionRequired 为真但缺检测器时,不阻断构造,而是注入
// capabilityWarnings 告警 —— 由下游评估器决定如何降级处理。
inline bool makeAnalysisContext(const AnalysisContextInput& input,
                                AnalysisContext& output,
                                std::string* error = nullptr)
{
    output = AnalysisContext ();
    const auto fail = [error] (const char* code) {
        if (error != nullptr)
            *error = code;
        return false;
    };

    if (input.workcell == nullptr)
        return fail ("KIN_CONTEXT_NO_WORKCELL");
    if (input.device == nullptr)
        return fail ("KIN_CONTEXT_NO_DEVICE");
    if (input.tcpFrame == nullptr)
        return fail ("KIN_CONTEXT_NO_TCP");
    if (input.modelFingerprint.empty ())
        return fail ("KIN_CONTEXT_NO_MODEL_FINGERPRINT");
    if (input.environmentFingerprint.empty ())
        return fail ("KIN_CONTEXT_NO_ENVIRONMENT_FINGERPRINT");

    output.workcell = input.workcell;
    output.device = input.device;
    output.tcpFrame = input.tcpFrame;
    output.baseState = input.baseState;
    output.collisionDetector = input.collisionDetector;
    output.deviceName = input.deviceName.empty () ? input.device->getName () : input.deviceName;
    output.tcpFrameName =
        input.tcpFrameName.empty () ? input.tcpFrame->getName () : input.tcpFrameName;
    output.modelFingerprint = input.modelFingerprint;
    output.environmentFingerprint = input.environmentFingerprint;
    output.thresholds = input.thresholds;
    output.collisionRequired = input.collisionRequired;

    if (input.collisionRequired && input.collisionDetector == nullptr) {
        AnalysisWarning warning;
        warning.code = "KIN_COLLISION_DETECTOR_UNAVAILABLE";
        warning.message = "Collision-free evaluation is required, but no collision detector is available.";
        warning.source = "KinematicAnalysisContext";
        warning.severity = AnalysisStatus::Warning;
        output.capabilityWarnings.push_back (warning);
    }

    if (error != nullptr)
        error->clear ();
    return true;
}

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_KINEMATICANALYSISCONTEXT_HPP
