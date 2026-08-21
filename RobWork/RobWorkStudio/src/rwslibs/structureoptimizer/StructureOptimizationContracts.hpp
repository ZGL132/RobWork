#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTRACTS_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTRACTS_HPP

#include "StructureOptimizationTypes.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 候选模型的执行生命周期状态（独立于工程可行性判断）。
 *
 * @details
 * 【架构设计原则与解耦意图】
 * 本枚举仅表达优化调度器在运行时处理该候选模型时的“计算流程状态”，完全独立于物理/运动学层面的可行性（Feasibility）。
 * - 例如：被用户取消（Canceled）的候选方案绝不等于不可行（Infeasible），它仅代表计算被提前终止、缺少完整的验证证据链。
 * - 将执行生命周期与 Feasibility（可行性）、EvidenceStage（证据阶段）、Quality（质量）正交拆分，
 *   可以避免 UI 展现、数据持久化及优化器排序算法将“系统运行状态”与“机械结构工程结论”混淆。
 */
enum class CandidateLifecycle
{
    Pending,    ///< 待处理：已生成设计向量（DesignVector），处于待编译/待评估队列中
    Compiling,  ///< 编译中：正在由 CandidateCompiler 解析参数、执行 Patch 合并并生成规范模型副本与工件
    Evaluating, ///< 评估中：正在多阶段流水线（EvaluationPipeline）中执行运动学、碰撞、覆盖率等评估
    Completed,  ///< 已完成：所有已规划的评估阶段均已正常执行完毕（无论工程约束是否满足）
    Failed,     ///< 异常失败：由于系统异常、内部求解器崩溃或非法模型拓扑导致编译/评估流程意外中断
    Canceled    ///< 已取消：执行过程被用户或上层调度器协作式中断（Cooperative Cancellation）
};

/**
 * @brief 采样/评估阶段的事实完成度记录（POD 结构体）。
 *
 * @details
 * 用于跟踪需要多次采样或批处理任务的阶段（如网格区域覆盖率、工作空间采样、多任务点 IK 求解）的执行进度与完整性。
 * - 即使任务中途被取消，已完成的样本计数（completedCount）仍会被完整保留，以供审计和部分结果分析。
 * - 【评估规则】：若某一必需的工程需求（Must Requirement）因为未全部完成而导致样本不足，
 *   上层调用者必须将其映射为 Feasibility::DataInsufficient，严禁静默视为 Feasible 或 Infeasible。
 */
struct EvaluationCompletion
{
    std::size_t requestedCount = 0; ///< 计划请求评估的总样本数/任务点数
    std::size_t completedCount = 0; ///< 实际成功完成评估的样本数/任务点数
    bool canceled = false;          ///< 标记该阶段在执行过程中是否收到了取消信号
    std::string partialReason;      ///< 若仅部分完成，记录导致中断或未全部完成的具体原因

    /**
     * @brief 判断该评估阶段是否按计划 100% 完整执行。
     * @return true 当且仅当未被取消且实际完成数等于请求数。
     */
    bool complete() const;

    /**
     * @brief 判断该评估阶段是否仅获取到了部分有效结果。
     * @return true 若完成数大于 0 但小于请求数，或在执行过程中被标记为取消。
     */
    bool partial() const;
};

/**
 * @brief 结构优化核心层使用的标准化、机器可读的诊断信息载体（POD 结构体）。
 *
 * @details
 * 统一承载从设计空间编译（DesignSpaceCompiler）、参数适配（Adapter）、
 * 候选编译（CandidateCompiler）到多阶段评估（EvaluationPipeline）全链路中产生的所有诊断、警告与阻断错误。
 * 诊断码（code）与对象标识（objectId/fieldPath）严格保持稳定，便于自动化测试、UI 定位与报告追溯。
 */
struct StructureOptimizationDiagnostic
{
    std::string code;         ///< 稳定的机器可读诊断代码（如 "PARAMETER_WRITE_CONFLICT", "REQ_V3_REQUIRES_REFREEZE"）
    std::string severity;     ///< 严重级别：通常为 "Info"、"Warning" 或 "Error"（Error 会阻断编译/启动）
    std::string subsystem;    ///< 产生诊断的子系统名称（如 "DesignSpace", "CandidateCompiler", "Kinematics"）
    std::string stage;        ///< 具体的执行阶段或管道标识（如 "Compilation", "TaskEvaluation", "VerifiedRegion"）
    std::string objectId;     ///< 关联的目标对象唯一 ID（如 Frame ID、Joint ID、Requirement ID）
    std::string fieldPath;    ///< 关联的具体属性路径（如 "parentToJointZero.translation.x", "physicalLimits.lower"）
    std::string candidateId;  ///< 发生诊断的候选模型唯一标识（若处于候选评估期）
    std::string message;      ///< 面向开发/工程人员的详细诊断描述信息
    std::string suggestion;   ///< 针对该问题的修复建议或补救措施（Remediation）
    std::vector< std::string > evidenceIds; ///< 关联的证据、样本或约束违反项 ID 列表
};

/**
 * @brief 针对旧版复合状态（StructureCandidateStatus）的正交化兼容投影结构体。
 *
 * @details
 * 【重构过渡层设计】
 * 旧版系统中将执行状态、可行性与评估阶段混杂在单一枚举（StructureCandidateStatus）中。
 * 本结构体作为单向兼容投影，将旧版单一状态解构为 4 个正交的核心维度：
 * 1. lifecycle: 任务生命周期（Pending / Compiling / Evaluating / Completed / Failed / Canceled）
 * 2. feasibility: 物理/工程可行性（NotEvaluated / Feasible / Infeasible / DataInsufficient）
 * 3. evidenceStage: 证据等级（Estimated / Quick / Verified / Final）
 * 4. quality: 方案质量等级（Good / Degraded / Critical / Unknown）
 *
 * 新增业务代码必须直接操作这 4 个独立维度，禁止反向组合或在 UI 层重新实现状态推导。
 */
struct CandidateStateProjection
{
    CandidateLifecycle lifecycle = CandidateLifecycle::Pending;         ///< 执行生命周期
    Feasibility feasibility = Feasibility::NotEvaluated;                ///< 工程可行性判定
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;  ///< 证据置信度等级
    Quality quality = Quality::Unknown;                                 ///< 方案综合质量
};

/**
 * @brief 将 CandidateLifecycle 枚举转换为稳定的文本字符串（用于 JSON 序列化、日志与调试）。
 * @param lifecycle 候选生命周期枚举值
 * @return 对应的常量字符串指针（如 "Pending", "Evaluating", "Completed" 等）
 */
const char* toString(CandidateLifecycle lifecycle);

/**
 * @brief 从字符串解析 CandidateLifecycle 枚举值（用于 JSON 反序列化与文本解析）。
 * @param[in] text 输入的枚举字符串
 * @param[out] value 解析成功后写入的目标枚举引用
 * @param[out] error 可选的错误信息接收指针；若解析失败且指针非空，将写入详细错误原因
 * @return true 解析成功；false 遇到未知字符串并拒绝解析
 */
bool candidateLifecycleFromString(const std::string& text,
                                  CandidateLifecycle& value,
                                  std::string* error = nullptr);

/**
 * @brief 显式兼容转换函数：将旧版单一候选状态投影为四维正交状态。
 * @param legacyStatus 旧版 StructureCandidateStatus 枚举值
 * @return 包含独立 lifecycle, feasibility, evidenceStage, quality 的投影结构体
 */
CandidateStateProjection projectLegacyCandidateStatus(StructureCandidateStatus legacyStatus);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONCONTRACTS_HPP