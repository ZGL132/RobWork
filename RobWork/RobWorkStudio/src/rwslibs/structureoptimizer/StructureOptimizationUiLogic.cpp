#include "StructureOptimizationUiLogic.hpp"

#include "OptimizationPreflight.hpp"
#include "StructureOptimizationJson.hpp"

#include <cmath>
#include <limits>
#include <sstream>

using namespace rws;

namespace {

std::string remediationFor(const std::string& code)
{
    if (code.find("Context") != std::string::npos)
        return "Load or create a complete robot model snapshot.";
    if (code.find("Variable") != std::string::npos)
        return "Review enabled variables, bounds and search steps.";
    if (code.find("Task") != std::string::npos)
        return "Enable at least one task point or import frozen requirements.";
    if (code.find("Weights") != std::string::npos || code.find("Objective") != std::string::npos)
        return "Choose a template or adjust objective weights and normalization.";
    if (code.find("Workspace") != std::string::npos)
        return "Review workspace regions and their grid resolution.";
    if (code.find("Run") != std::string::npos)
        return "Choose a run preset or reduce the candidate and refinement counts.";
    return "Review the highlighted optimization input.";
}

/**
 * @brief 判断一个浮点数值是否为有效的非零值。
 * 
 * 检查数值是否有限（非 NaN、非 Inf），且绝对值大于 1e-12（防止浮点数精度误差造成的伪非零）。
 * 
 * @param value 待检查的浮点数值
 * @return true 数值有限且不为零
 * @return false 数值为 0、NaN 或无限大
 */
bool nonZero(double value)
{
    return std::isfinite(value) && std::abs(value) > 1e-12;
}

/**
 * @brief 构建一个长度/尺寸类型的结构设计变量实例。
 * 
 * 统一设置长度变量的默认属性：
 *  - 物理单位：米 ("m")
 *  - 寻优范围：[当前值 * 0.7, 当前值 * 1.3] (即上下浮动 30%)
 *  - 默认步长：0.001 m (1 mm)
 *  - 偏好值：默认等于当前初始值
 * 
 * @param id 变量的唯一标识符
 * @param label UI 显示标签
 * @param target 目标关节/坐标系/几何体名称
 * @param kind 变量种类枚举
 * @param currentValue 变量当前的初始物理数值
 * @return StructureDesignVariable 构造完成的设计变量结构体
 */
StructureDesignVariable makeLengthVariable(const std::string& id,
                                           const std::string& label,
                                           const std::string& target,
                                           StructureVariableKind kind,
                                           double currentValue)
{
    StructureDesignVariable variable;
    variable.id = id;
    variable.label = label;
    variable.targetName = target;
    variable.unit = "m";
    variable.kind = kind;
    variable.currentValue = currentValue;
    // 默认寻优下限设为当前值的 70%
    variable.minimum = currentValue * 0.7;
    // 默认寻优上限设为当前值的 130%
    variable.maximum = currentValue * 1.3;
    // 若当前值为负数导致 min > max，则交换保证区间合法
    if (variable.minimum > variable.maximum)
        std::swap(variable.minimum, variable.maximum);
    variable.step = 0.001; // 1 mm 默认搜索步长
    variable.preferredValue = currentValue; // 工程师默认偏好初始构型尺寸
    variable.enabled = true; // 默认启用该变量
    return variable;
}

/**
 * @brief 为关节变换 (JointTransform) 的指定坐标轴添加位置平移设计变量。
 * 
 * 仅当指定轴向的平移位移非零时才会提取并添加为可优化变量。
 * 
 * @param variables [out] 收集设计变量的向量容器
 * @param joint 关节变换规格对象
 * @param axis 坐标轴索引 (0 -> x, 1 -> y, 2 -> z)
 */
void appendTransformPositionVariable(std::vector<StructureDesignVariable>& variables,
                                     const JointTransformSpec& joint,
                                     int axis)
{
    static const char* axisNames[] = {"x", "y", "z"};
    static const StructureVariableKind kinds[] = {
        StructureVariableKind::JointPositionX,
        StructureVariableKind::JointPositionY,
        StructureVariableKind::JointPositionZ
    };
    const double value = joint.pos[static_cast<std::size_t>(axis)];
    // 忽略平移为 0 的坐标轴，避免生成无效变量
    if (!nonZero(value))
        return;

    std::ostringstream id;
    id << joint.name << "_pos_" << axisNames[axis];
    std::ostringstream label;
    label << joint.name << " Pos " << axisNames[axis];
    variables.push_back(makeLengthVariable(
        id.str(), label.str(), joint.name, kinds[axis], value));
}

/**
 * @brief 为工具坐标系 (ToolFrame / TCP) 的指定坐标轴添加偏移量设计变量。
 * 
 * @param variables [out] 收集设计变量的向量容器
 * @param joint 工具关节规格对象
 * @param axis 坐标轴索引 (0 -> x, 1 -> y, 2 -> z)
 */
void appendTcpVariable(std::vector<StructureDesignVariable>& variables,
                       const JointTransformSpec& joint,
                       int axis)
{
    static const char* axisNames[] = {"x", "y", "z"};
    static const StructureVariableKind kinds[] = {
        StructureVariableKind::TcpOffsetX,
        StructureVariableKind::TcpOffsetY,
        StructureVariableKind::TcpOffsetZ
    };
    const double value = joint.pos[static_cast<std::size_t>(axis)];
    if (!nonZero(value))
        return;

    std::ostringstream id;
    id << joint.name << "_tcp_" << axisNames[axis];
    std::ostringstream label;
    label << joint.name << " TCP " << axisNames[axis];
    variables.push_back(makeLengthVariable(
        id.str(), label.str(), joint.name, kinds[axis], value));
}

/**
 * @brief 为连杆可几何化渲染对象 (Drawable) 添加连杆截面尺寸设计变量（半径、宽、高）。
 * 
 * 仅对启用了 autoLinkGeometry（自动生成连杆几何）的连杆生成几何变量。
 * 自动同步标志 syncAssociatedGeometry 会被置为 true，变异时将联动更新碰撞模型[cite: 17, 22]。
 * 
 * @param variables [out] 收集设计变量的向量容器
 * @param drawable 渲染规格对象
 */
void appendDrawableVariables(std::vector<StructureDesignVariable>& variables,
                             const DrawableSpec& drawable)
{
    // 未开启自动连杆几何生成则忽略
    if (!drawable.autoLinkGeometry)
        return;

    // 若存在半径属性（圆柱/圆管连杆），生成 LinkRadius 变量
    if (nonZero(drawable.radius)) {
        StructureDesignVariable variable = makeLengthVariable(
            drawable.name + "_radius",
            drawable.name + " Radius",
            drawable.name,
            StructureVariableKind::LinkRadius,
            drawable.radius);
        variable.syncAssociatedGeometry = true; // 变异时联动同步碰撞网格[cite: 17, 22]
        variables.push_back(variable);
    }

    // 遍历三轴尺寸（方形截面连杆），生成显式轴维度变量（M3：kind 与
    // dimensions[X/Y/Z] 一一对应，不再复用语义冲突的 LinkWidth/LinkHeight）。
    static const char* const axisNames[] = {"X", "Y", "Z"};
    static const StructureVariableKind axisKinds[] = {
        StructureVariableKind::LinkDimensionX,
        StructureVariableKind::LinkDimensionY,
        StructureVariableKind::LinkDimensionZ
    };
    for (int axis = 0; axis < 3; ++axis) {
        const double value = drawable.dimensions[static_cast<std::size_t>(axis)];
        if (!nonZero(value))
            continue;
        StructureDesignVariable variable = makeLengthVariable(
            drawable.name + "_dim_" + std::to_string(axis),
            drawable.name + " Dimension " + axisNames[axis],
            drawable.name,
            axisKinds[axis],
            value);
        variable.syncAssociatedGeometry = true; // 变异时联动同步碰撞网格
        variables.push_back(variable);
    }
}

} // namespace

/**
 * @brief 根据输入的机器人设计上下文，自动分析并生成建议的设计变量列表。
 * 
 * 算法扫描模型的平移关节、TCP 工具、基座安装高度及连杆几何体，提取其中非零的物理尺寸作为推荐变量。
 * 
 * @param context 机器人设计上下文
 * @return std::vector<StructureDesignVariable> 建议生成的结构设计变量集合
 */
std::vector<StructureDesignVariable>
StructureOptimizationUiLogic::suggestVariables(const RobotDesignContext& context)
{
    std::vector<StructureDesignVariable> variables;
    const RobotModelSpec& spec = context.modelSpec;

    // 1. 遍历机器人模型的所有平移关节变换
    for (const JointTransformSpec& joint : spec.transformJoints) {
        const bool toolFrame = typeToKind(joint.type) == JointKind::ToolFrame;
        if (toolFrame) {
            // M4: ToolFrame 只生成 TcpOffset* 一组变量。JointPosition* 与
            // TcpOffset* 会写同一 pos[axis]，两套并存时后者覆盖前者，先者
            // 成为死变量并污染候选解释与灵敏度分析。
            for (int axis = 0; axis < 3; ++axis)
                appendTcpVariable(variables, joint, axis);
        }
        else {
            for (int axis = 0; axis < 3; ++axis)
                appendTransformPositionVariable(variables, joint, axis);
        }
    }

    // 2. 检查基座安装高度 Z 坐标，若非零则提取 BaseHeight 变量
    if (nonZero(spec.robotBaseFrame.pos[2])) {
        variables.push_back(makeLengthVariable(
            "base_height",
            "Base Height",
            spec.robotBaseFrame.name.empty() ? "RobotBase" : spec.robotBaseFrame.name,
            StructureVariableKind::BaseHeight,
            spec.robotBaseFrame.pos[2]));
    }

    // 3. 遍历渲染模型，提取连杆截面半径/宽高尺寸变量
    for (const DrawableSpec& drawable : spec.drawables)
        appendDrawableVariables(variables, drawable);

    return variables;
}

std::vector<StructurePreflightFinding>
StructureOptimizationUiLogic::preflight(const StructureOptimizationProblem& problem)
{
    std::vector<StructurePreflightFinding> findings;
    // UI 只把核心结构化结果投影成旧表格需要的类型，避免 Start/banner 分叉。
    const OptimizationPreflightResult core = OptimizationPreflight::run(problem);
    for (const PreflightFinding& finding : core.findings) {
        const AnalysisStatus severity = finding.severity == OptimizationPreflightSeverity::Error
                                            ? AnalysisStatus::Fail
                                            : finding.severity == OptimizationPreflightSeverity::Warning
                                                  ? AnalysisStatus::Warning
                                                  : AnalysisStatus::Pass;
        findings.push_back({severity, finding.code, finding.message,
                            finding.remediation.empty() ? remediationFor(finding.code)
                                                        : finding.remediation});
    }
    return findings;
}

/**
 * @brief 检查当前结构优化问题的输入配置是否合法且具备可运行条件。
 * 
 * 该函数供 UI 界面在启动优化前进行综合门禁控制。
 * 
 * @param problem 待校验的优化问题定义
 * @param reason [out] 若校验失败，用于写回展示给用户的中文提示字符串
 * @return true 可以启动优化计算
 * @return false 存在阻断性错误或配置缺失，拒绝启动
 */
bool StructureOptimizationUiLogic::hasRunnableInputs(
    const StructureOptimizationProblem& problem, std::string* reason)
{
    // 唯一门禁来源：核心 preflight 组合所有校验（模型完整性、启用变量、
    // 启用任务点、运行数量等），UI 层只做投影，不再重复扫描变量与任务。
    const OptimizationPreflightResult result = OptimizationPreflight::run(problem);
    if (result.canStart) {
        if (reason != nullptr) reason->clear();
        return true;
    }
    if (reason != nullptr) {
        // 取第一条阻断性 (Error) finding，输出稳定的 "code: message"；
        // 中文提示由 Widget 状态栏的 UI 投影层负责。
        for (const PreflightFinding& finding : result.findings) {
            if (finding.severity == OptimizationPreflightSeverity::Error) {
                *reason = finding.code + ": " + finding.message;
                break;
            }
        }
        if (reason->empty())
            *reason = "Optimization preflight blocked the run.";
    }
    return false;
}

std::string StructureOptimizationUiLogic::editableContractFingerprint(
    const std::vector<OptimizationTaskPoint>& tasks,
    const std::vector<StructureConstraint>& constraints)
{
    // 复用问题级规范化 JSON 编码器（键排序、确定性序列化）。临时 problem 只
    // 携带 tasks/constraints，其余字段保持默认值，因此指纹与权重、运行配置等
    // 可编辑无关项完全解耦。
    StructureOptimizationProblem scratch;
    scratch.tasks = tasks;
    scratch.constraints = constraints;
    return StructureOptimizationJson::problemToJson(scratch);
}

int StructureOptimizationUiLogic::disableShadowedLegacyTcpDuplicates(
    std::vector<StructureDesignVariable>& variables)
{    // M4 存量迁移：旧项目可能同时携带同一 ToolFrame 字段的 JointPosition*
    // 与 TcpOffset* 绑定。语义明确的 TcpOffset* 保留，JointPosition* 抑制，
    // 返回抑制数量供调用方告警。
    auto axisOf = [](StructureVariableKind kind) -> int {
        switch (kind) {
            case StructureVariableKind::JointPositionX:
            case StructureVariableKind::TcpOffsetX: return 0;
            case StructureVariableKind::JointPositionY:
            case StructureVariableKind::TcpOffsetY: return 1;
            case StructureVariableKind::JointPositionZ:
            case StructureVariableKind::TcpOffsetZ: return 2;
            default: return -1;
        }
    };
    auto isPosition = [](StructureVariableKind kind) {
        return kind == StructureVariableKind::JointPositionX ||
               kind == StructureVariableKind::JointPositionY ||
               kind == StructureVariableKind::JointPositionZ;
    };
    auto isTcp = [](StructureVariableKind kind) {
        return kind == StructureVariableKind::TcpOffsetX ||
               kind == StructureVariableKind::TcpOffsetY ||
               kind == StructureVariableKind::TcpOffsetZ;
    };

    int disabled = 0;
    for (std::size_t i = 0; i < variables.size(); ++i) {
        if (!isPosition(variables[i].kind) || !variables[i].enabled)
            continue;
        const int axis = axisOf(variables[i].kind);
        for (std::size_t j = 0; j < variables.size(); ++j) {
            if (i == j || !variables[j].enabled || !isTcp(variables[j].kind))
                continue;
            if (variables[j].targetName == variables[i].targetName &&
                axisOf(variables[j].kind) == axis) {
                variables[i].enabled = false;
                ++disabled;
                break;
            }
        }
    }
    return disabled;
}

int StructureOptimizationUiLogic::migrateLegacyDrawableDimensionKinds(
    std::vector<StructureDesignVariable>& variables)
{
    // M3 存量迁移：旧建议器把 `_dim_0` 错标为 LinkHeight（写 dimensions[2]）、
    // `_dim_1` 错标为 LinkWidth（写 dimensions[0]）。按 id 后缀 "_dim_<axis>"
    // 重映射为显式轴 kind；id 不符合该模式的遗留变量保持原语义不动。
    static const std::string marker = "_dim_";
    int migrated = 0;
    for (auto& variable : variables) {
        const bool legacyKind =
            variable.kind == StructureVariableKind::LinkWidth ||
            variable.kind == StructureVariableKind::LinkHeight;
        if (!legacyKind)
            continue;
        const std::size_t pos = variable.id.rfind(marker);
        if (pos == std::string::npos)
            continue;
        const std::string suffix = variable.id.substr(pos + marker.size());
        StructureVariableKind target;
        if (suffix == "0")
            target = StructureVariableKind::LinkDimensionX;
        else if (suffix == "1")
            target = StructureVariableKind::LinkDimensionY;
        else if (suffix == "2")
            target = StructureVariableKind::LinkDimensionZ;
        else
            continue;
        if (variable.kind != target) {
            variable.kind = target;
            ++migrated;
        }
    }
    return migrated;
}

bool StructureOptimizationUiLogic::hasFrozenRequirementContract(
    const StructureOptimizationProblem& problem)
{
    return !problem.requirementExecution.tasks.empty() ||
           !problem.requirementExecution.workspaceRegions.empty();
}

std::string StructureOptimizationUiLogic::frozenReferenceFingerprint(
    const StructureOptimizationProblem& problem)
{
    return problem.requirementExecution.extensions
        .value(QStringLiteral("frozenEditableContractFingerprint"))
        .toString()
        .toStdString();
}

bool StructureOptimizationUiLogic::frozenContractStale(
    const StructureOptimizationProblem& problem)
{
    if (!hasFrozenRequirementContract(problem))
        return false;
    // C1.1: 旧项目缺参考指纹 -> 冻结契约未验证，安全默认要求重新冻结。
    const std::string reference = frozenReferenceFingerprint(problem);
    if (reference.empty())
        return true;
    return editableContractFingerprint(problem.tasks, problem.constraints) !=
           reference;
}

std::string StructureOptimizationUiLogic::designVariableSchemaFingerprint(
    const std::vector<StructureDesignVariable>& variables)
{
    // 只保留影响"候选值 -> 模型字段"映射与合法域的定义字段；运行时数值
    // (current/preferred/weight) 归零，避免用户微调当前值误触发拒绝。
    std::vector<StructureDesignVariable> schema = variables;
    for (StructureDesignVariable& variable : schema) {
        variable.currentValue = 0.0;
        variable.preferredValue = 0.0;
        variable.preferenceWeight = 0.0;
        variable.syncAssociatedGeometry = false;
    }
    StructureOptimizationProblem scratch;
    scratch.variables = std::move(schema);
    return StructureOptimizationJson::problemToJson(scratch);
}

StructurePreviewPermission StructureOptimizationUiLogic::evaluatePreviewPermission(
    bool hasRuntimeSnapshot,
    const std::string& snapshotSchemaFingerprint,
    const std::string& currentSchemaFingerprint)
{
    if (!hasRuntimeSnapshot) {
        return {false,
                "No runtime snapshot is attached to this result. Re-run the "
                "optimization to preview its candidates."};
    }
    if (snapshotSchemaFingerprint != currentSchemaFingerprint) {
        return {false,
                "Preview rejected: variable definitions changed since this run "
                "completed. Restore the variables or re-run the optimization."};
    }
    return {true, std::string()};
}
