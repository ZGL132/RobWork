#include "KinematicAnalysisUiLogic.hpp"

#include "TaskPointTableModel.hpp"

#include <rw/models/Device.hpp>
#include <rw/kinematics/Frame.hpp>

bool rws::ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked)
{
    return !checkboxAvailable || checkboxChecked;
}

// ikCollisionEvidence 把"用户是否请求碰撞检查"与"该解是否真的执行了碰撞检测"
// 浓缩为四态证据,供 UI 判断某解能否安全 Apply 以及碰撞列如何着色:
//   - NotEvaluated:用户未请求碰撞检查,不产生任何碰撞结论;
//   - Unavailable :请求了检查,但该解没有碰撞检测数据(数据缺失);
//   - Clear       :检测过且无碰撞;
//   - Collision   :检测过且处于碰撞状态。
rws::KinematicIkCollisionEvidence rws::ikCollisionEvidence (
    const KinematicIkAnalysisResult& result, const KinematicIkSolution& solution)
{
    if (!result.collisionCheckRequested)
        return KinematicIkCollisionEvidence::NotEvaluated;
    if (!solution.collisionChecked)
        return KinematicIkCollisionEvidence::Unavailable;
    return solution.inCollision ? KinematicIkCollisionEvidence::Collision :
                                  KinematicIkCollisionEvidence::Clear;
}

// canApplyIkSolution 判定某个 IK 候选解是否可以安全 Apply 到当前 WorkCell:
//   - stale(WorkCell 已变化、结果已过期)、状态为 Fail、或解处于碰撞状态时直接拒绝;
//   - 未请求碰撞检查时仅要求状态非 Fail;请求了检查时还要求碰撞证据为 Clear。
// 返回 false 时 UI 应禁用 Apply 按钮,避免把过期或不可达的解写回机器人。
bool rws::canApplyIkSolution (const KinematicIkAnalysisResult& result,
                              const KinematicIkSolution& solution,
                              bool stale)
{
    if (stale || solution.status == AnalysisStatus::Fail || solution.inCollision)
        return false;
    const KinematicIkCollisionEvidence evidence = ikCollisionEvidence (result, solution);
    return !result.collisionCheckRequested || evidence == KinematicIkCollisionEvidence::Clear;
}

// preferredIkSolutionIndex 从候选索引集合中挑选"默认 Apply"的首选解:
//   - 优先选择第一个无碰撞且状态为 Pass 的解;
//   - 其次选择无碰撞且状态为 Warning 的解;
//   - 都没有时退回第一个落在有效范围内的索引(即使状态不佳也给用户一个可选项);
//   - 全部无效返回 -1,调用方应禁用 Apply。
// 偏好与 sortIkSolutionsForDisplay 的排序保持一致:可用性优先于完美性。
int rws::preferredIkSolutionIndex (const KinematicIkAnalysisResult& result,
                                   const std::vector< int >& candidateIndices)
{
    const auto firstWithStatus = [&result, &candidateIndices] (AnalysisStatus status) {
        for (const int index : candidateIndices) {
            if (index < 0 || index >= static_cast< int > (result.solutions.size ()))
                continue;
            const KinematicIkSolution& solution =
                result.solutions[static_cast< std::size_t > (index)];
            if (!solution.inCollision && solution.status == status)
                return index;
        }
        return -1;
    };
    const int pass = firstWithStatus (AnalysisStatus::Pass);
    if (pass >= 0)
        return pass;
    const int warning = firstWithStatus (AnalysisStatus::Warning);
    if (warning >= 0)
        return warning;
    for (const int index : candidateIndices) {
        if (index >= 0 && index < static_cast< int > (result.solutions.size ()))
            return index;
    }
    return -1;
}

std::vector< int > rws::taskPointCompactTableColumns ()
{
    return {
        ColEnabled,
        ColName,
        ColRefFrame,
        ColTcpFrame,
        ColStatus
    };
}

std::vector< int > rws::taskPointDetailColumns ()
{
    return {
        ColId,
        ColType,
        ColPosTol,
        ColOriTol,
        ColFreeRoll,
        ColWeight,
        ColNote,
        ColRawCandidates,
        ColPositionError,
        ColOrientationError,
        ColMinMargin,
        ColCondition,
        ColCollision
    };
}

std::string rws::defaultTcpFrameName (const rw::models::Device* device)
{
    if (device == nullptr || device->getEnd () == nullptr)
        return std::string ();
    return device->getEnd ()->getName ();
}

bool rws::visualEnvelopeModeAvailable (int sourceKind, int renderMode)
{
    return sourceKind == 1 &&
        renderMode == static_cast< int > (VisualRenderMode::Envelope);
}

bool rws::visualEnvelopeDirectionChangeSupersedesRequest (
    bool envelopeActive, bool requestActive)
{
    return envelopeActive && requestActive;
}

bool rws::visualEnvelopeStateChangeRequiresRefresh (
    bool envelopeActive, bool studioStateChanged)
{
    return envelopeActive && studioStateChanged;
}
