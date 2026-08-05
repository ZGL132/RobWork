#include "RequirementCompiler.hpp"

#include "RequirementSetJson.hpp"

#include <QCryptographicHash>

#include <cmath>
#include <set>

namespace rws {
namespace {

/**
 * @brief 辅助函数：校验三维浮点数组（位置/姿态/尺寸等）中的每个元素是否均为有限数值
 * @param values 待校验的三维 double 数组
 * @return true 均非 NaN 且非 Inf；false 包含非法浮点数
 */
bool finiteArray(const std::array<double, 3>& values)
{
    return std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]);
}

/**
 * @brief 辅助函数：向诊断日志集合中构造并追加一条新的诊断条目
 * 
 * 核心逻辑：若需求等级为 RequirementLevel::Must（强约束），则标记 blocking = true，
 * 这意味着该诊断条目会在后续编译时触发阻断，阻止需求集被编译冻结。
 * 
 * @param diagnostics 诊断日志列表引用
 * @param requirementId 出错的需求/工位 ID
 * @param level 需求等级（Must / Should / Info）
 * @param message 详细诊断描述信息
 */
void addDiagnostic(std::vector<RequirementDiagnostic>& diagnostics, const std::string& requirementId,
                   RequirementLevel level, const std::string& message,
                   const std::string& code = "REQ_INVALID")
{
    RequirementDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.requirementId = requirementId;
    diagnostic.level = level;
    diagnostic.message = message;
    diagnostic.blocking = (level == RequirementLevel::Must); // 仅 Must 级别的错误才会阻断编译
    diagnostics.push_back(diagnostic);
}

/**
 * @brief 辅助函数：检查指定 ID 的需求项是否存在相关的诊断日志（警告或错误）
 * @param diagnostics 全局诊断日志列表
 * @param requirementId 待查询的需求 ID
 * @return true 存在诊断记录；false 无诊断记录
 */
bool hasDiagnosticFor(const std::vector<RequirementDiagnostic>& diagnostics, const std::string& requirementId)
{
    for (const RequirementDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.requirementId == requirementId)
            return true;
    }
    return false;
}

std::string diagnosticReasonFor(const std::vector<RequirementDiagnostic>& diagnostics,
                                const std::string& requirementId)
{
    for (const RequirementDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.requirementId == requirementId)
            return diagnostic.message;
    }
    return std::string();
}

} // namespace 匿名空间

/**
 * @brief 全量静态语法与语义校验器
 * 
 * 对输入的 RequirementSet 进行全方位的合法性扫描，包括：
 * 1. 模型绑定检查：确保机器人模型指纹非空；
 * 2. 关键工位（PoseTask）校验：
 *    - 标识符与命名：ID/Name 非空且全局唯一；
 *    - 坐标系引用：参考系与 TCP Frame 是否配置；
 *    - 特殊来源与模式约束：GeometryFeature 帧名称、AlignFrame 目标系、PointAtTarget 目标等；
 *    - 数值有效性：位姿、容差、滚转角范围 [rollMin, rollMax]、接近/撤离距离 $\ge 0$、关节/可操作度裕度 $\ge 0$、置信度 $\in [0, 1]$ 等。
 * 3. 工作空间包络盒（BoxRegion）校验：
 *    - ID/参考系非空且唯一；
 *    - 包络盒尺寸必须严格大于 0（size > 0），覆盖率 $\in [0, 1]$，单轴离散采样点数 $\ge 2$。
 */
std::vector<RequirementDiagnostic> RequirementCompiler::validateDetailed(const RequirementSet& requirements)
{
    std::vector<RequirementDiagnostic> diagnostics;

    // 1. 检查机器人模型绑定信息
    if (requirements.modelBinding.robotModelFingerprint.empty())
        addDiagnostic(diagnostics, std::string(), RequirementLevel::Must, "A robot model fingerprint is required.");

    std::set<std::string> ids; // 用于跨工位和工作区检测全局 ID 冲突

    // 2. 逐项校验关键工位 (KeyStation / PoseTask)
    for (const PoseTask& task : requirements.poseTasks) {
        // ID 及其唯一性检查
        if (task.id.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station id is required.",
                          "REQ_REQUIRED_FIELD_MISSING");
        else if (!ids.insert(task.id).second) 
            addDiagnostic(diagnostics, task.id, task.level, "Duplicate requirement id: " + task.id,
                          "REQ_DUPLICATE_ID");

        // 必填基础文本项检查
        if (task.name.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station name is required: " + task.id,
                          "REQ_REQUIRED_FIELD_MISSING");
        if (task.refFrame.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station reference frame is required: " + task.id,
                          "REQ_REQUIRED_FIELD_MISSING");
        if (task.tcpFrame.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station TCP frame is required: " + task.id,
                          "REQ_REQUIRED_FIELD_MISSING");

        // 绑定几何特征时的特定依赖检查
        if (task.source == PoseTaskSource::GeometryFeature &&
            (task.geometryFeature.type == GeometryFeatureType::None || task.geometryFeature.frameName.empty()))
            addDiagnostic(diagnostics, task.id, task.level, "Key station geometry feature frame is required: " + task.id,
                          "REQ_GEOMETRY_FEATURE_INVALID");

        // 姿态模式特定依赖检查：坐标系对齐模式必须提供目标 Frame
        if (task.orientation.mode == OrientationMode::AlignFrame && task.orientation.targetFrame.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station alignment target frame is required: " + task.id,
                          "REQ_ORIENTATION_TARGET_MISSING");

        // 姿态模式特定依赖检查：指向模式必须提供目标 Frame 或目标 Point
        if (task.orientation.mode == OrientationMode::PointAtTarget && task.orientation.targetFrame.empty() && task.orientation.targetPoint.empty())
            addDiagnostic(diagnostics, task.id, task.level, "Key station pointing target is required: " + task.id,
                          "REQ_ORIENTATION_TARGET_MISSING");

        // 位姿与容差数值有效性校验（必须为有限数且容差非负）
        if (!finiteArray(task.position) || !finiteArray(task.rpyDeg) ||
            !std::isfinite(task.tolerance.positionMeters) || !std::isfinite(task.tolerance.orientationDeg) ||
            task.tolerance.positionMeters < 0.0 || task.tolerance.orientationDeg < 0.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station contains invalid pose or tolerance values: " + task.id,
                          "REQ_POSE_INVALID");

        // 姿态滚转角极限范围校验 (rollMin <= rollMax)
        if (!std::isfinite(task.orientation.rollMinimumDeg) || !std::isfinite(task.orientation.rollMaximumDeg) ||
            task.orientation.rollMinimumDeg > task.orientation.rollMaximumDeg)
            addDiagnostic(diagnostics, task.id, task.level, "Key station contains invalid roll limits: " + task.id,
                          "REQ_ORIENTATION_RULE_INVALID");

        // 接近与撤离距离校验（启用时距离必须非负）
        if ((task.approach.enabled && (!std::isfinite(task.approach.distanceMeters) || task.approach.distanceMeters < 0.0)) ||
            (task.retract.enabled && (!std::isfinite(task.retract.distanceMeters) || task.retract.distanceMeters < 0.0)))
            addDiagnostic(diagnostics, task.id, task.level, "Key station approach or retract distance must be non-negative: " + task.id,
                          "REQ_PATH_POLICY_INVALID");

        // 可行性校验策略中的关节裕度与可操作度指标校验（必须非负）
        if (!std::isfinite(task.validation.minimumJointMargin) || !std::isfinite(task.validation.minimumManipulability) ||
            task.validation.minimumJointMargin < 0.0 || task.validation.minimumManipulability < 0.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station validation policy contains invalid values: " + task.id,
                          "REQ_VALIDATION_POLICY_INVALID");

        // 工位可信度/权重校验 [0.0, 1.0]
        if (!std::isfinite(task.confidence) || task.confidence < 0.0 || task.confidence > 1.0)
            addDiagnostic(diagnostics, task.id, task.level, "Key station confidence must be within [0, 1]: " + task.id,
                          "REQ_CONFIDENCE_INVALID");
    }

    // 3. 逐项校验工作空间包络盒 (BoxRegion)
    for (const BoxRegion& region : requirements.boxRegions) {
        if (region.id.empty()) 
            addDiagnostic(diagnostics, region.id, region.level, "Box region id is required.",
                          "REQ_REQUIRED_FIELD_MISSING");
        else if (!ids.insert(region.id).second) 
            addDiagnostic(diagnostics, region.id, region.level, "Duplicate requirement id: " + region.id,
                          "REQ_DUPLICATE_ID");

        if (region.refFrame.empty()) 
            addDiagnostic(diagnostics, region.id, region.level, "Box region reference frame is required: " + region.id,
                          "REQ_REQUIRED_FIELD_MISSING");
        if (region.tcpFrame.empty())
            addDiagnostic(diagnostics, region.id, region.level, "Box region TCP frame is required: " + region.id,
                          "REQ_REQUIRED_FIELD_MISSING");

        // 几何参数与采样密度校验：包络盒三维尺寸必须严格大于 0，覆盖率 $\in [0, 1]$，离散采样点数 $\ge 2$
        if (!finiteArray(region.center) || !finiteArray(region.size) ||
            region.size[0] <= 0.0 || region.size[1] <= 0.0 || region.size[2] <= 0.0)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region contains invalid geometry values: " + region.id,
                          "REQ_WORKSPACE_GEOMETRY_INVALID");
        if (!std::isfinite(region.minimumCoverage) || region.minimumCoverage < 0.0 ||
            region.minimumCoverage > 1.0)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region minimumCoverage must be within [0, 1]: " + region.id,
                          "REQ_WORKSPACE_COVERAGE_INVALID");
        const int minimumGridSamples =
            region.minimumVerificationStage == RequirementVerificationStage::Verified ? 2 : 1;
        if (region.samplesPerAxis < minimumGridSamples)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region samplesPerAxis is too coarse for the requested verification stage: " + region.id,
                          "REQ_WORKSPACE_GRID_TOO_COARSE");
        if (region.directionSamples < 1)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region directionSamples must be at least 1: " + region.id,
                          "REQ_WORKSPACE_DIRECTION_SAMPLES_INVALID");
        if (region.rollSamples < 1)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region rollSamples must be at least 1: " + region.id,
                          "REQ_WORKSPACE_ROLL_SAMPLES_INVALID");
        // 采样密度安全上限校验：逐轴网格数、方向样本数与翻滚样本数任一超过
        // 定义的上限即生成 REQ_WORKSPACE_SAMPLE_LIMIT_EXCEEDED 诊断，防止畸形的
        // 覆盖盒请求在下游采样分析中产生无界计算量。
        if (region.samplesPerAxis > MaxWorkspaceSamplesPerAxis ||
            region.directionSamples > MaxWorkspaceDirectionSamples ||
            region.rollSamples > MaxWorkspaceRollSamples)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region sampling density exceeds the supported safety limit: " + region.id,
                          "REQ_WORKSPACE_SAMPLE_LIMIT_EXCEEDED");
        if (!finiteArray(region.fixedRpyDeg) ||
            !std::isfinite(region.minimumOrientationCoverage) ||
            region.minimumOrientationCoverage < 0.0 || region.minimumOrientationCoverage > 1.0)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region orientation coverage or fixed RPY is invalid: " + region.id,
                          "REQ_WORKSPACE_ORIENTATION_POLICY_INVALID");
        if (!std::isfinite(region.positionToleranceMeters) || region.positionToleranceMeters < 0.0 ||
            !std::isfinite(region.orientationToleranceDeg) || region.orientationToleranceDeg < 0.0 ||
            !std::isfinite(region.minimumJointMargin) || region.minimumJointMargin < 0.0 ||
            !std::isfinite(region.minimumManipulability) || region.minimumManipulability < 0.0)
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region validation tolerances and margins must be finite and non-negative: " + region.id,
                          "REQ_WORKSPACE_VALIDATION_POLICY_INVALID");
        if (region.orientationMode == OrientationMode::AlignFrame && region.orientationTargetFrame.empty())
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region alignment target frame is required: " + region.id,
                          "REQ_WORKSPACE_ORIENTATION_TARGET_MISSING");
        if (region.orientationMode == OrientationMode::AlignGeometryNormal &&
            (region.orientationTargetGeometry.empty() || region.orientationTargetFrame.empty()))
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region geometry normal requires target frame and geometry: " + region.id,
                          "REQ_WORKSPACE_GEOMETRY_TARGET_MISSING");
        if (region.orientationMode == OrientationMode::PointAtTarget &&
            region.orientationTargetFrame.empty() && region.orientationTargetPoint.empty())
            addDiagnostic(diagnostics, region.id, region.level,
                          "Box region pointing target is required: " + region.id,
                          "REQ_WORKSPACE_POINTING_TARGET_MISSING");
    }

    return diagnostics;
}

/**
 * @brief 简化的文本校验接口实现
 */
std::vector<std::string> RequirementCompiler::validate(const RequirementSet& requirements)
{
    std::vector<std::string> messages;
    for (const RequirementDiagnostic& diagnostic : validateDetailed(requirements))
        messages.push_back(diagnostic.message);
    return messages;
}

/**
 * @brief 计算规范化需求集指纹 (SHA-256)
 * 
 * 为了保证不同编辑状态下计算出的摘要一致性：
 * 强制将副本的 frozen 标志归零（因为编辑态与冻结态内容本质相同，不能因状态标志改变指纹），
 * 随后序列化为 Compact JSON 格式并计算 SHA-256 哈希。
 */
std::string RequirementCompiler::fingerprint(const RequirementSet& requirements)
{
    RequirementSet canonical = requirements;
    canonical.frozen = false; // 排除内部状态干扰，仅针对数据内容计算哈希
    // 模型内容由 robotModelFingerprint 单独校验。sourcePath 只是项目内定位信息，
    // 保存时会相对化且随项目移动改变，不能使同一份工程需求证据失效。
    canonical.modelBinding.sourcePath.clear();
    const QByteArray bytes = QByteArray::fromStdString(RequirementSetJson::toJson(canonical));
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

/**
 * @brief 需求集核心编译函数
 * 
 * 编译处理工作流：
 * 1. 调用 validateDetailed() 进行全量扫描，若发现任何阻断性错误（blocking == true），直接熔断退出并返回错误；
 * 2. 实例化 CompiledRequirementSet，标记 frozen = true，保存模型绑定并写入唯一的 requirementFingerprint；
 * 3. 过滤并编译 KeyStation：
 *    - 过滤条件 1：跳过 RequirementLevel::Info 级别的辅助说明项；
 *    - 过滤条件 2：跳过带有诊断警告（如 Should 级未对齐）的工位，确保编译出的集合纯净；
 *    - 标记 pathValidationPending：若工位使能了接近/撤离动作，标记后续（P3 阶段）需进一步进行连续路径碰撞与 IK 解算校验；
 * 4. 过滤并编译 BoxRegion（逻辑同上，过滤 Info 项和非阻断异常项）；
 * 5. 返回编译产物。
 */
bool RequirementCompiler::compile(const RequirementSet& requirements, CompiledRequirementSet& compiled,
                                  std::string* error)
{
    // 步骤 1：执行详细校验并检查阻断性错误
    const std::vector<RequirementDiagnostic> diagnostics = validateDetailed(requirements);
    for (const RequirementDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.blocking) { // 遇到阻断性错误（Must 级别违规）
            if (error != nullptr) *error = diagnostic.message;
            return false;
        }
    }

    // 步骤 2：初始化冻结态产物及元数据
    CompiledRequirementSet result;
    result.frozen = true;
    result.modelBinding = requirements.modelBinding;
    result.requirementFingerprint = fingerprint(requirements);
    result.diagnostics = diagnostics; // 保留 Should 等级的非阻断性警告日志

    // 步骤 3：过滤并转换关键工位 (PoseTask -> CompiledPoseTask)
    for (const PoseTask& task : requirements.poseTasks) {
        CompiledPoseTask item;
        item.id = task.id; 
        item.name = task.name; 
        item.level = task.level;
        item.refFrame = task.refFrame; 
        item.tcpFrame = task.tcpFrame;
        item.position = task.position; 
        item.rpyDeg = task.rpyDeg; 
        item.tolerance = task.tolerance;
        item.processType = task.processType;
        item.geometryFeature = task.geometryFeature;
        item.orientation = task.orientation;
        item.validation = task.validation;
        item.approach = task.approach;
        item.retract = task.retract;
        item.compileState = RequirementCompileState::Included;
        
        // 若使能了接近或撤离段，需在 P3 运动学阶段进一步做轨迹连续性与碰撞验证
        item.pathValidationPending = task.approach.enabled || task.retract.enabled;

        if (task.level == RequirementLevel::Info) {
            item.compileState = RequirementCompileState::Excluded;
            item.excludedReason = "Info requirement is retained for audit only.";
        }
        else if (hasDiagnosticFor(diagnostics, task.id)) {
            item.compileState = RequirementCompileState::Excluded;
            item.excludedReason = diagnosticReasonFor(diagnostics, task.id);
        }
        
        result.poseTasks.push_back(item);
    }

    // 步骤 4：过滤并转换工作空间包络盒 (BoxRegion -> WorkspaceDemandRegion)
    for (const BoxRegion& region : requirements.boxRegions) {
        WorkspaceDemandRegion item;
        item.id = region.id; 
        item.name = region.name; 
        item.level = region.level;
        item.refFrame = region.refFrame; 
        item.center = region.center; 
        item.size = region.size;
        item.minimumCoverage = region.minimumCoverage; 
        item.samplesPerAxis = region.samplesPerAxis;
        item.tcpFrame = region.tcpFrame;
        item.orientationMode = region.orientationMode;
        item.orientationTargetFrame = region.orientationTargetFrame;
        item.orientationTargetGeometry = region.orientationTargetGeometry;
        item.orientationTargetPoint = region.orientationTargetPoint;
        item.fixedRpyDeg = region.fixedRpyDeg;
        item.directionSamples = region.directionSamples;
        item.rollSamples = region.rollSamples;
        item.minimumOrientationCoverage = region.minimumOrientationCoverage;
        item.minimumVerificationStage = region.minimumVerificationStage;
        item.collisionFreeRequired = region.collisionFreeRequired;
        item.positionToleranceMeters = region.positionToleranceMeters;
        item.orientationToleranceDeg = region.orientationToleranceDeg;
        item.minimumJointMargin = region.minimumJointMargin;
        item.minimumManipulability = region.minimumManipulability;
        item.compileState = RequirementCompileState::Included;
        if (region.level == RequirementLevel::Info) {
            item.compileState = RequirementCompileState::Excluded;
            item.excludedReason = "Info requirement is retained for audit only.";
        }
        else if (hasDiagnosticFor(diagnostics, region.id)) {
            item.compileState = RequirementCompileState::Excluded;
            item.excludedReason = diagnosticReasonFor(diagnostics, region.id);
        }
        
        result.workspaceRegions.push_back(item);
    }

    // 步骤 5：完成编译输出
    compiled = result;
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
