// =============================================================================
//  RegionCoverageEvaluator.cpp —— 工作空间区域覆盖率评估实现
// =============================================================================
//
// 实现三步流水线:
//   1. generateGrid:把长方体区域按冻结的 XYZ 网格点数切分为网格单元,
//      每个单元位置经 baseTReference 变换到设备 base 坐标系;
//   2. generateTargets:按区域的朝向策略为单元构造任务点 ——
//      Fixed(固定 RPY)、AlignFrame(对齐目标帧的姿态)、
//      PointAtTarget(工具指向某个点,按 rollSamples 滚动)、
//      AlignGeometryNormal(对齐某帧 Z 轴,语法 frame:<frameName>);
//   3. evaluate:逐单元逐任务点调用 TargetEvaluator 求解,统计位置覆盖率
//      与姿态覆盖率,并按最小覆盖率阈值聚合整体可行性。
//
// 关键保护:对总姿态采样数做上限(MaxCompositeOrientationTargets)防止
// 高分辨率方向 × 滚动在大量单元上组合爆炸导致长时间卡死。
#include "RegionCoverageEvaluator.hpp"

#include "TargetEvaluator.hpp"

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <cctype>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace rws {
namespace {

// 区域评估允许的"方向 × 滚动 × 单元"组合姿态总数上限。
// 超过则直接拒绝评估(DataInsufficient),避免意外参数把一次评估放大成
// 数十万个 IK 求解,阻塞主线程过久。
constexpr std::size_t MaxCompositeOrientationTargets = 100000;

// -----------------------------------------------------------------------------
// 内部辅助函数(匿名命名空间,仅本翻译单元可见)
// -----------------------------------------------------------------------------
//
// iequals:不区分大小写的字符串比较。用于宽容匹配参考帧名
// ("world" / "WORLD" / "World" 均视为世界帧),降低用户配置错误的概率。
bool iequals(const std::string& lhs, const std::string& rhs)
{
    if (lhs.size () != rhs.size ())
        return false;
    for (std::size_t i = 0; i < lhs.size (); ++i) {
        if (std::tolower (static_cast< unsigned char > (lhs[i])) !=
            std::tolower (static_cast< unsigned char > (rhs[i])))
            return false;
    }
    return true;
}

// failGrid:以 DataInsufficient + Critical 标记整个区域结果并附一条 Fail 告警。
// 用于前置校验失败(区域参数非法、参考帧缺失、检测器缺失等),
// 表示"没有足够数据可评估",而非"区域真的不可达"。
void failGrid(RegionCoverageResult& result,
              const char* code,
              const std::string& message)
{
    result.feasibility = Feasibility::DataInsufficient;
    result.quality = Quality::Critical;
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "RegionCoverageEvaluator";
    warning.severity = AnalysisStatus::Fail;
    result.warnings.push_back (warning);
}

// failTargets:标记"目标生成"阶段失败(单元级),仅影响当前单元,
// 不会使整个区域结果失效,但 evaluate 会把该单元记为 DataInsufficient。
void failTargets(RegionTargetGenerationResult& result,
                 const char* code,
                 const std::string& message)
{
    result.feasibility = Feasibility::DataInsufficient;
    AnalysisWarning warning;
    warning.code = code;
    warning.message = message;
    warning.source = "RegionCoverageEvaluator";
    warning.severity = AnalysisStatus::Fail;
    result.warnings.push_back (warning);
}

// resolveBaseTReference:把参考帧名解析为"设备 base -> 参考帧"变换。
// 规则:参考帧为设备 base 时返回单位阵;为空或 "WORLD"(忽略大小写)时取世界帧;
// 否则在 WorkCell 中按名查找。任何解析失败(缺 WorkCell / 缺 Device /
// 帧不存在 / frameTframe 异常)都返回 false,由调用方决定如何报错。
bool resolveBaseTReference(const AnalysisContext& context,
                           const std::string& referenceName,
                           rw::math::Transform3D<>& baseTReference)
{
    if (context.workcell == nullptr || context.device == nullptr)
        return false;

    const std::string baseName = context.device->getBase ()->getName ();
    if (iequals (referenceName, baseName)) {
        baseTReference = rw::math::Transform3D<>::identity ();
        return true;
    }

    const rw::kinematics::Frame* reference = nullptr;
    if (referenceName.empty () || iequals (referenceName, "WORLD"))
        reference = context.workcell->getWorldFrame ();
    else
        reference = context.workcell->findFrame (referenceName);
    if (reference == nullptr)
        return false;

    try {
        baseTReference = rw::kinematics::Kinematics::frameTframe (
            context.device->getBase (), reference, context.baseState);
    }
    catch (const std::exception&) {
        return false;
    }
    return true;
}

// toolZToRotation:由任意方向向量构造一个以该方向为 Z 轴的旋转矩阵。
// 参考"up"向量选取规则:与 +Z 夹角 > 0.99 时改用 +Y,避免两向量近平行时
// 叉乘退化(结果接近零向量),这是正交基构造的数值稳定性要点。
rw::math::Rotation3D<> toolZToRotation(const rw::math::Vector3D<>& direction)
{
    using rw::math::Vector3D;
    Vector3D<> z = direction;
    if (z.norm2 () < 1e-12)
        z = Vector3D<>::z ();
    z = normalize (z);
    Vector3D<> up = std::fabs (dot (Vector3D<>::z (), z)) > 0.99 ?
                        Vector3D<>::y () : Vector3D<>::z ();
    Vector3D<> x = normalize (cross (up, z));
    const Vector3D<> y = normalize (cross (z, x));
    return rw::math::Rotation3D<> (x, y, z);
}

// parsePoint:严格解析 "x,y,z" 形式的指向目标点文本。
// 要求恰好三个有限数值且以逗号分隔、后面无多余字符;解析失败返回 false。
// 采用严格解析是为了让拼写错误(如 "1;2;3")在评估前就被明确拒绝,
// 而不是被静默解释成错误坐标。
bool parsePoint(const std::string& text, rw::math::Vector3D<>& point)
{
    std::istringstream stream (text);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    char comma1 = '\0';
    char comma2 = '\0';
    if (!(stream >> x >> comma1 >> y >> comma2 >> z) || comma1 != ',' || comma2 != ',' ||
        !std::isfinite (x) || !std::isfinite (y) || !std::isfinite (z))
        return false;
    stream >> std::ws;
    if (!stream.eof ())
        return false;
    point = rw::math::Vector3D<> (x, y, z);
    return true;
}

// appendUniqueFailures:把 source 中的失败原因去重合并进 destination。
// 用于把单元内多个任务点的失败原因汇总到单元 / 区域级别,只保留根因集合,
// 避免同一原因(如 JointLimit)被几十个任务点重复列出。
void appendUniqueFailures(std::vector< KinematicFailureReason >& destination,
                          const std::vector< KinematicFailureReason >& source)
{
    for (const KinematicFailureReason reason : source) {
        if (std::find (destination.begin (), destination.end (), reason) == destination.end ())
            destination.push_back (reason);
    }
}

} // namespace

// -----------------------------------------------------------------------------
// RegionCoverageEvaluator::generateGrid —— 区域体素化
// -----------------------------------------------------------------------------
//
// 前置校验(编译状态 / WorkCell / 区域尺寸 / 采样数)全部通过后,按
// 冻结的 XYZ 网格点数在每个维度等距切分,把单元中心位置从区域参考系变换到
// 设备 base 坐标系。本阶段只做几何,结果可行性保持 NotEvaluated。
RegionCoverageResult RegionCoverageEvaluator::generateGrid(
    const AnalysisContext& context,
    const RequirementExecutionRegion& region) const
{
    RegionCoverageResult result;
    result.stage = AnalysisEvidenceStage::Verified;
    result.regionId = region.id;
    result.itemProvenance = region.provenance;

    if (region.compileState != RequirementExecutionCompileState::Included) {
        result.feasibility = Feasibility::NotEvaluated;
        return result;
    }
    if (context.workcell == nullptr || context.device == nullptr) {
        failGrid (result, "KIN_REGION_CONTEXT_UNAVAILABLE",
                  "Verified region generation requires a WorkCell and Device.");
        return result;
    }
    for (const double size : region.size) {
        if (!std::isfinite (size) || size <= 0.0) {
            failGrid (result, "KIN_REGION_INVALID_SIZE",
                      "Verified region size must be finite and positive on every axis.");
            return result;
        }
    }
    for (const double center : region.center) {
        if (!std::isfinite (center)) {
            failGrid (result, "KIN_REGION_INVALID_CENTER",
                      "Verified region center must be finite on every axis.");
            return result;
        }
    }
    for (const int samples : region.sampleCounts) {
        if (samples < 2 || samples > MaxExecutionWorkspaceSamplesPerAxis) {
            failGrid (result, "KIN_REGION_INVALID_SAMPLES",
                      "Verified region sampleCounts is outside the execution limits.");
            return result;
        }
    }

    rw::math::Transform3D<> baseTReference;
    if (!resolveBaseTReference (context, region.refFrame, baseTReference)) {
        failGrid (result, "KIN_REGION_FRAME_NOT_FOUND",
                  "Verified region reference frame could not be resolved.");
        return result;
    }

    // 单元位置计算:局部坐标 = center - 0.5*size + 比例*size(每轴 sampleCounts-1 份),
    // 再把局部坐标经 baseTReference 旋转 + 平移到设备 base 系。
    const int samplesX = region.sampleCounts[0];
    const int samplesY = region.sampleCounts[1];
    const int samplesZ = region.sampleCounts[2];
    const std::size_t total = static_cast< std::size_t > (samplesX) *
                              static_cast< std::size_t > (samplesY) *
                              static_cast< std::size_t > (samplesZ);
    result.cells.reserve (total);
    for (int x = 0; x < samplesX; ++x) {
        for (int y = 0; y < samplesY; ++y) {
            for (int z = 0; z < samplesZ; ++z) {
                const double xRatio = static_cast< double > (x) /
                                      static_cast< double > (samplesX - 1);
                const double yRatio = static_cast< double > (y) /
                                      static_cast< double > (samplesY - 1);
                const double zRatio = static_cast< double > (z) /
                                      static_cast< double > (samplesZ - 1);
                const rw::math::Vector3D<> local (
                    region.center[0] - 0.5 * region.size[0] + xRatio * region.size[0],
                    region.center[1] - 0.5 * region.size[1] + yRatio * region.size[1],
                    region.center[2] - 0.5 * region.size[2] + zRatio * region.size[2]);
                const rw::math::Vector3D<> inBase =
                    baseTReference.P () + baseTReference.R () * local;
                RegionCellResult cell;
                cell.index = {{x, y, z}};
                cell.position = {{inBase[0], inBase[1], inBase[2]}};
                result.cells.push_back (cell);
            }
        }
    }
    result.totalCells = static_cast< int > (result.cells.size ());
    result.feasibility = Feasibility::NotEvaluated;
    result.quality = Quality::Unknown;
    return result;
}

// -----------------------------------------------------------------------------
// RegionCoverageEvaluator::generateTargets —— 单元任务点生成
// -----------------------------------------------------------------------------
//
// 依据区域朝向策略为单元构造 TaskPoint 列表。策略分支:
//   - AlignFrame:对齐到指定帧的姿态(单任务点);
//   - PointAtTarget:工具指向目标点,并按 rollSamples 滚动生成多个任务点;
//   - AlignGeometryNormal:对齐到 frame:<name> 帧的 Z 轴法向(单任务点);
//   - Fixed:使用 region.fixedRpyDeg 固定姿态(单任务点)。
// 生成的 TaskPoint 继承区域的位置 / 姿态容差与权重(Must=1.0,其余=0.5)。
RegionTargetGenerationResult RegionCoverageEvaluator::generateTargets(
    const AnalysisContext& context,
    const RequirementExecutionRegion& region,
    const RegionCellResult& cell) const
{
    RegionTargetGenerationResult result;
    if (context.device == nullptr) {
        failTargets (result, "KIN_REGION_CONTEXT_UNAVAILABLE",
                     "Verified region target generation requires a Device.");
        return result;
    }
    for (const double value : cell.position) {
        if (!std::isfinite (value)) {
            failTargets (result, "KIN_REGION_INVALID_CELL",
                         "Verified region cell position must be finite.");
            return result;
        }
    }
    const auto appendTarget = [&] (const std::array< double, 3 >& rpyDeg,
                                   int rollIndex = -1) {
        TaskPoint target;
        target.id = region.id + "_" + std::to_string (cell.index[0]) + "_" +
                    std::to_string (cell.index[1]) + "_" + std::to_string (cell.index[2]);
        if (rollIndex >= 0)
            target.id += "_roll_" + std::to_string (rollIndex);
        target.name = region.name;
        target.refFrame = context.device->getBase ()->getName ();
        target.tcpFrame = region.tcpFrame;
        target.position = cell.position;
        target.rpyDeg = rpyDeg;
        target.tolerance.positionMeters = region.positionToleranceMeters;
        target.tolerance.orientationDeg = region.orientationToleranceDeg;
        target.weight = region.level == RequirementExecutionLevel::Must ? 1.0 : 0.5;
        target.enabled = true;
        result.targets.push_back (target);
    };
    // ---- 按朝向策略确定目标姿态 --------------------------------------------
    // 默认取区域固定姿态;各策略分支改写 targetRpyDeg 或直接产出多任务点。
    std::array< double, 3 > targetRpyDeg = region.fixedRpyDeg;
    if (region.orientationMode == RequirementExecutionOrientationMode::AlignFrame) {
        rw::math::Transform3D<> baseTFrame;
        if (region.orientationTargetFrame.empty () ||
            !resolveBaseTReference (context, region.orientationTargetFrame, baseTFrame)) {
            failTargets (result, "KIN_REGION_ORIENTATION_FRAME_NOT_FOUND",
                         "Verified region orientation Frame could not be resolved.");
            return result;
        }
        const rw::math::RPY<> rpy (baseTFrame.R ());
        const double toDeg = 180.0 / rw::math::Pi;
        targetRpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    }
    else if (region.orientationMode ==
             RequirementExecutionOrientationMode::PointAtTarget) {
        rw::math::Vector3D<> targetPointInBase;
        if (!region.orientationTargetFrame.empty ()) {
            rw::math::Transform3D<> baseTTargetFrame;
            if (!resolveBaseTReference (
                    context, region.orientationTargetFrame, baseTTargetFrame)) {
                failTargets (result, "KIN_REGION_POINT_FRAME_NOT_FOUND",
                             "Verified region pointing target Frame could not be resolved.");
                return result;
            }
            targetPointInBase = baseTTargetFrame.P ();
        }
        else {
            rw::math::Vector3D<> localTargetPoint;
            rw::math::Transform3D<> baseTReference;
            if (!parsePoint (region.orientationTargetPoint, localTargetPoint)) {
                failTargets (result, "KIN_REGION_POINT_TARGET_INVALID",
                             "Verified region pointing target must contain x, y, z.");
                return result;
            }
            if (!resolveBaseTReference (context, region.refFrame, baseTReference)) {
                failTargets (result, "KIN_REGION_FRAME_NOT_FOUND",
                             "Verified region pointing reference Frame could not be resolved.");
                return result;
            }
            targetPointInBase = baseTReference.P () + baseTReference.R () * localTargetPoint;
        }
        const rw::math::Vector3D<> source (
            cell.position[0], cell.position[1], cell.position[2]);
        const rw::math::Vector3D<> direction = targetPointInBase - source;
        if (direction.norm2 () < 1e-12) {
            failTargets (result, "KIN_REGION_POINT_TARGET_COINCIDENT",
                         "Verified region pointing target coincides with the region cell.");
            return result;
        }
        if (region.rollSamples < 1 ||
            region.rollSamples > MaxExecutionWorkspaceRollSamples) {
            failTargets (result, "KIN_REGION_INVALID_ROLL_SAMPLES",
                         "Verified region roll sample count is outside execution limits.");
            return result;
        }
        const rw::math::Rotation3D<> baseRotation = toolZToRotation (direction);
        const double toDeg = 180.0 / rw::math::Pi;
        for (int rollIndex = 0; rollIndex < region.rollSamples; ++rollIndex) {
            const double roll = 2.0 * rw::math::Pi * static_cast< double > (rollIndex) /
                                static_cast< double > (region.rollSamples);
            const rw::math::Rotation3D<> rotation =
                baseRotation * rw::math::EAA<> (rw::math::Vector3D<>::z (), roll)
                                   .toRotation3D ();
            const rw::math::RPY<> rpy (rotation);
            appendTarget ({{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}},
                          rollIndex);
        }
        return result;
    }
    else if (region.orientationMode ==
             RequirementExecutionOrientationMode::AlignGeometryNormal) {
        static const std::string prefix = "frame:";
        if (region.orientationTargetGeometry.rfind (prefix, 0) != 0 ||
            region.orientationTargetGeometry.size () == prefix.size ()) {
            failTargets (result, "KIN_REGION_GEOMETRY_REFERENCE_INVALID",
                         "Verified region geometry orientation requires frame:<frameName>.");
            return result;
        }
        rw::math::Transform3D<> baseTFrame;
        if (!resolveBaseTReference (
                context, region.orientationTargetGeometry.substr (prefix.size ()), baseTFrame)) {
            failTargets (result, "KIN_REGION_GEOMETRY_FRAME_NOT_FOUND",
                         "Verified region geometry Frame could not be resolved.");
            return result;
        }
        const rw::math::RPY<> rpy (toolZToRotation (baseTFrame.R ().getCol (2)));
        const double toDeg = 180.0 / rw::math::Pi;
        targetRpyDeg = {{rpy (0) * toDeg, rpy (1) * toDeg, rpy (2) * toDeg}};
    }
    else if (region.orientationMode != RequirementExecutionOrientationMode::Fixed) {
        failTargets (result, "KIN_REGION_ORIENTATION_UNSUPPORTED",
                     "Verified region orientation strategy is not implemented.");
        return result;
    }
    for (const double value : targetRpyDeg) {
        if (!std::isfinite (value)) {
            failTargets (result, "KIN_REGION_INVALID_ORIENTATION",
                         "Verified region fixed orientation must be finite.");
            return result;
        }
    }

    appendTarget (targetRpyDeg);
    return result;
}

// -----------------------------------------------------------------------------
// RegionCoverageEvaluator::evaluate —— 区域覆盖率评估(组合入口)
// -----------------------------------------------------------------------------
//
// 流程:generateGrid 得到网格 -> 校验覆盖率阈值与采样上限 -> 逐单元
// generateTargets + TargetEvaluator.evaluate -> 汇总位置 / 姿态覆盖率 ->
// 按 minimumCoverage / minimumOrientationCoverage 聚合整体可行性。
// 取消令牌(cancellation)在单元之间与单元内部检查,保证长任务可中断。
RegionCoverageResult RegionCoverageEvaluator::evaluate(
    const AnalysisContext& context,
    const RequirementExecutionRegion& region,
    const CancellationToken& cancellation) const
{
    RegionCoverageResult result = generateGrid (context, region);
    if (result.feasibility == Feasibility::DataInsufficient ||
        region.compileState != RequirementExecutionCompileState::Included)
        return result;
    if (!std::isfinite (region.minimumCoverage) || region.minimumCoverage < 0.0 ||
        region.minimumCoverage > 1.0 ||
        !std::isfinite (region.minimumOrientationCoverage) ||
        region.minimumOrientationCoverage < 0.0 ||
        region.minimumOrientationCoverage > 1.0) {
        failGrid (result, "KIN_REGION_INVALID_COVERAGE_THRESHOLD",
                  "Verified region coverage thresholds must be finite values from zero to one.");
        return result;
    }
    if (region.collisionFreeRequired && context.collisionDetector == nullptr) {
        failGrid (result, "KIN_REGION_COLLISION_DETECTOR_UNAVAILABLE",
                  "Verified region requires collision checking, but no detector is available.");
        return result;
    }
    const std::size_t compositeTargets =
        region.directionSamples > 0 && region.rollSamples > 0
            ? result.cells.size () * static_cast< std::size_t > (region.directionSamples) *
                  static_cast< std::size_t > (region.rollSamples)
            : 0;
    if (compositeTargets > MaxCompositeOrientationTargets) {
        failGrid (result, "KIN_REGION_SAMPLING_LIMIT",
                  "Verified region orientation sampling exceeds the configured limit.");
        return result;
    }

    TargetEvaluator targetEvaluator;
    TargetEvaluationOptions targetOptions;
    targetOptions.evidenceStage = AnalysisEvidenceStage::Verified;
    targetOptions.checkCollision = region.collisionFreeRequired;
    targetOptions.requireCollisionFree = region.collisionFreeRequired;

    // ---- 主循环:逐单元生成任务点并求解 ------------------------------------
    // dataInsufficient 记录是否有单元 / 任务点证据不足(优先于不可达);
    // cancelled 记录用户是否中断,中断后只保留已完成单元的结果。
    bool dataInsufficient = false;
    bool cancelled = false;
    for (RegionCellResult& cell : result.cells) {
        const RegionTargetGenerationResult generated =
            generateTargets (context, region, cell);
        result.warnings.insert (result.warnings.end (), generated.warnings.begin (),
                                generated.warnings.end ());
        if (generated.feasibility == Feasibility::DataInsufficient) {
            cell.feasibility = Feasibility::DataInsufficient;
            cell.quality = Quality::Critical;
            dataInsufficient = true;
            continue;
        }

        // 单元内逐任务点求解:统计可达 / 采样次数,收集最佳可操作度与关节裕度。
        bool cellDataInsufficient = false;
        for (const TaskPoint& target : generated.targets) {
            if (cancellation.cancellationRequested ()) {
                cancelled = true;
                dataInsufficient = true;
                break;
            }
            const TargetEvaluation targetResult =
                targetEvaluator.evaluate (context, target, targetOptions);
            ++cell.sampledOrientationCount;
            ++result.sampledOrientations;
            appendUniqueFailures (cell.failureReasons, targetResult.failureReasons);
            result.warnings.insert (result.warnings.end (), targetResult.warnings.begin (),
                                    targetResult.warnings.end ());
            if (targetResult.feasibility == Feasibility::DataInsufficient) {
                cellDataInsufficient = true;
                dataInsufficient = true;
                continue;
            }
            if (targetResult.feasibility != Feasibility::Feasible)
                continue;

            ++cell.reachableOrientationCount;
            ++result.reachableOrientations;
            for (const TargetCandidate& candidate : targetResult.candidates) {
                cell.bestManipulability =
                    std::max (cell.bestManipulability, candidate.configuration.manipulability);
                cell.bestJointMargin =
                    std::max (cell.bestJointMargin, candidate.configuration.minimumJointMargin);
            }
        }

        if (cancelled)
            break;

        if (cellDataInsufficient) {
            cell.feasibility = Feasibility::DataInsufficient;
            cell.quality = Quality::Critical;
        }
        else if (cell.reachableOrientationCount > 0) {
            cell.feasibility = Feasibility::Feasible;
            cell.quality = Quality::Good;
            ++result.reachableCells;
        }
        else {
            cell.feasibility = Feasibility::Infeasible;
            cell.quality = Quality::Critical;
        }
    }

    if (cancelled) {
        AnalysisWarning warning;
        warning.code = "KIN_REGION_CANCELLED";
        warning.message = "Verified region evaluation was cancelled before all targets completed.";
        warning.source = "RegionCoverageEvaluator";
        warning.severity = AnalysisStatus::Warning;
        result.warnings.push_back (warning);
    }

    // ---- 覆盖率统计与整体状态聚合 ------------------------------------------
    if (result.totalCells > 0) {
        result.positionCoverage = static_cast< double > (result.reachableCells) /
                                  static_cast< double > (result.totalCells);
    }
    if (result.sampledOrientations > 0) {
        result.orientationCoverage =
            static_cast< double > (result.reachableOrientations) /
            static_cast< double > (result.sampledOrientations);
    }
    if (dataInsufficient) {
        result.feasibility = Feasibility::DataInsufficient;
        result.quality = Quality::Critical;
    }
    else if (result.positionCoverage >= region.minimumCoverage &&
             result.orientationCoverage >= region.minimumOrientationCoverage) {
        result.feasibility = Feasibility::Feasible;
        result.quality = result.positionCoverage >= 1.0 && result.orientationCoverage >= 1.0 ?
                             Quality::Good : Quality::Degraded;
    }
    else {
        result.feasibility = Feasibility::Infeasible;
        result.quality = Quality::Critical;
    }
    return result;
}

} // namespace rws
