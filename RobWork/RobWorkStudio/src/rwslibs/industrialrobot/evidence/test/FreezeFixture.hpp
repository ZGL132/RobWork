/**
 * @file FreezeFixture.hpp
 * @brief EV-T13：测试变异后重冻结输入；不提供工程判定或独立 oracle。
 * 仅在测试中保存给定闭包事实；线程私有，不参与生产修订查询。
 */
#ifndef SDURWS_IRD_EVIDENCE_FREEZEFIXTURE_HPP
#define SDURWS_IRD_EVIDENCE_FREEZEFIXTURE_HPP
#include <sdurws/ird/evidence/Snapshot.hpp>
namespace sdurws::ird::evidence::testdoubles
{
/** @brief 按给定测试闭包重建摘要。输入为测试值，非法内容仍由 builder 拒绝。 */
inline AnalysisSnapshot refreezeSnapshot(const AnalysisSnapshot& s)
{
    class Closure final : public IRevisionClosureSource
    {
        const AnalysisSnapshot& snapshot;

       public:
        explicit Closure(const AnalysisSnapshot& value) : snapshot(value) {}
        bool objectInRevision(core::RevisionId r, core::ObjectId id,
                              core::ContentVersion cv) const override
        {
            if (r != snapshot.revision) return false;
            for (const auto& ref : snapshot.objectClosure)
                if (ref.objectId == id && ref.contentVersion == cv) return true;
            return false;
        }
    } source{s};
    SnapshotBuilder b;
    b.setIdentity(s.project, s.branch, s.revision, s.revisionSeq);
    for (const auto& e : s.objectClosure) b.addObjectRef(e);
    for (const auto& e : s.configurationRefs) b.addConfiguration(e);
    for (const auto& e : s.caseSet.entries) b.addCase(e);
    for (const auto& e : s.samplingPlans) b.addSamplingPlan(e);
    for (const auto& e : s.externalResources) b.addExternalResource(e);
    b.setPolicyRef(s.policyRef).setNameMapRef(s.nameMapRef).setReproduction(s.reproduction);
    return b.build(source);
}
}  // namespace sdurws::ird::evidence::testdoubles
#endif
