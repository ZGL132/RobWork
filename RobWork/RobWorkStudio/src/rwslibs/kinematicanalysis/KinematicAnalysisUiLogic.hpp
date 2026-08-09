#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP

#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"

#include <vector>
#include <string>

namespace rw { namespace models { class Device; } }

namespace rws {

bool ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked);
// KinematicIkCollisionEvidence 描述某个 IK 候选解的碰撞检查证据。
// 区分"未请求检查"与"请求了但缺少数据"两种边界情况,避免 UI 把"没检查过
// 碰撞"误当成"碰撞通过":
//   - NotEvaluated:用户未请求碰撞检查,无任何碰撞结论;
//   - Unavailable :请求了检查,但该解没有执行碰撞检测;
//   - Clear       :已检测且无碰撞;
//   - Collision   :已检测且处于碰撞状态。
enum class KinematicIkCollisionEvidence {
    NotEvaluated,
    Unavailable,
    Clear,
    Collision
};
// 由 IK 分析结果与单条候选解计算碰撞证据(实现见 .cpp)。
KinematicIkCollisionEvidence ikCollisionEvidence (
    const KinematicIkAnalysisResult& result,
    const KinematicIkSolution& solution);
// 判断某候选解是否可安全 Apply 到 WorkCell(过期/失败/碰撞的解均不可用)。
bool canApplyIkSolution (const KinematicIkAnalysisResult& result,
                         const KinematicIkSolution& solution,
                         bool stale);
// 在候选索引中选出默认 Apply 的首选解(Pass → Warning → 首个有效索引,均无效返回 -1)。
int preferredIkSolutionIndex (const KinematicIkAnalysisResult& result,
                              const std::vector< int >& candidateIndices);
bool visualEnvelopeModeAvailable (int sourceKind, int renderMode);
bool visualEnvelopeDirectionChangeSupersedesRequest (bool envelopeActive, bool requestActive);
bool visualEnvelopeStateChangeRequiresRefresh (bool envelopeActive, bool studioStateChanged);
std::vector< int > taskPointCompactTableColumns ();
std::vector< int > taskPointDetailColumns ();
std::string defaultTcpFrameName (const rw::models::Device* device);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
