#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP

#include "StructureOptimizationTypes.hpp"

#include <string>
#include <vector>

namespace rws {

struct StructurePreflightFinding
{
    AnalysisStatus severity = AnalysisStatus::Unknown;
    std::string code;
    std::string message;
    std::string remediation;
};

/** 预览许可判定结果（M16：候选预览必须绑定运行时快照）。 */
struct StructurePreviewPermission
{
    bool allowed = false;
    std::string reason;
};

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

    static std::vector<StructurePreflightFinding> preflight(
        const StructureOptimizationProblem& problem);

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

    /**
     * @brief 计算可编辑任务点与约束的稳定指纹（冻结契约一致性检测用）。
     *
     * 指纹只由 tasks/constraints 内容决定（复用问题级规范化 JSON 编码器，
     * 键排序、跨会话确定）。冻结需求载入时记录参考指纹；此后任何任务/约束
     * 编辑使指纹偏离参考值即判定冻结契约 stale——必须从需求源重新冻结，
     * 绝不允许用当前表格内容重建执行契约。
     */
    static std::string editableContractFingerprint(
        const std::vector<OptimizationTaskPoint>& tasks,
        const std::vector<StructureConstraint>& constraints);

    /**
     * @brief 问题是否携带冻结执行契约（任务或区域非空）。
     */
    static bool hasFrozenRequirementContract(
        const StructureOptimizationProblem& problem);

    /**
     * @brief 冻结契约参考指纹（随契约持久化；旧项目可能为空）。
     */
    static std::string frozenReferenceFingerprint(
        const StructureOptimizationProblem& problem);

    /**
     * @brief 冻结契约是否 stale（C1.1/D1 最终判定，供 Widget/Controller/Preflight 共用）。
     *
     * 无契约 -> false；有契约但缺持久化参考指纹 -> true（未验证，安全默认）；
     * 当前 tasks+constraints 指纹 != 持久化参考 -> true。判定完全基于问题
     * 本身，因此绕过 UI 直接调用 Controller 同样被拦截。
     */
    static bool frozenContractStale(const StructureOptimizationProblem& problem);

    /**
     * @brief M4 存量迁移：抑制与 TcpOffset* 同字段冲突的 JointPosition*。
     *
     * 旧项目可能对同一 ToolFrame 坐标同时携带两套绑定（写同一 pos[axis]），
     * 后应用者覆盖前者。语义明确的 TcpOffset* 保留，JointPosition* 置为
     * disabled；返回抑制数量。
     */
    static int disableShadowedLegacyTcpDuplicates(
        std::vector<StructureDesignVariable>& variables);

    /**
     * @brief M3 存量迁移：按 id 后缀 `_dim_<axis>` 重映射旧维度变量的 kind。
     *
     * 旧建议器把 `_dim_0` 标为 LinkHeight、`_dim_1` 标为 LinkWidth，与实际
     * 写入的 dimensions 轴互相冲突。迁移后统一为 LinkDimensionX/Y/Z；
     * 返回实际改写的数量，id 不匹配后缀模式的遗留变量保持不变。
     */
    static int migrateLegacyDrawableDimensionKinds(
        std::vector<StructureDesignVariable>& variables);

    /**
     * @brief 设计变量"定义模式"指纹（M16）。
     *
     * 只由影响候选几何映射的字段决定（id/kind/targetName/min/max/step/
     * enabled/unit），与 current/preferred 等运行时数值无关——用户微调当前值
     * 不应导致历史候选被拒预览。
     */
    static std::string designVariableSchemaFingerprint(
        const std::vector<StructureDesignVariable>& variables);

    /**
     * @brief 判定是否允许用历史候选做几何预览（M16）。
     *
     * 无运行时快照、或当前变量定义模式与快照不一致时拒绝并给出原因；
     * 允许时预览必须使用快照问题而非当前 collectProblem()。
     */
    static StructurePreviewPermission evaluatePreviewPermission(
        bool hasRuntimeSnapshot,
        const std::string& snapshotSchemaFingerprint,
        const std::string& currentSchemaFingerprint);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREOPTIMIZATIONUILOGIC_HPP
