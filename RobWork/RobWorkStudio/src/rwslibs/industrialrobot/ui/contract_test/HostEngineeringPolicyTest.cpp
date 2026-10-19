/**
 * @file HostEngineeringPolicyTest.cpp
 * @brief A-23/UX-08：真实项目修订策略解析回归；替身仅用于写入外部坏对象。
 */
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>
#include <sdurws/ird/policy/SystemDefaultPolicy.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include "plugin/HostEngineeringPolicy.hpp"

namespace sdurws::ird::ui::test {
namespace {
/** @brief 测试存储边界注入器；模拟已存在的异常项目，不替代策略解析。 */
class PolicySeedHandler final : public project::ICommandHandler {
public:
    std::vector<project::ObjectWrite> writes;
    std::optional<project::MetadataChange> metadata;
    std::string commandType() const override { return "seed-policy-test"; }
    std::uint32_t currentPayloadVersion() const override { return 1; }
    project::PrepareOutcome prepare(project::HandlerContext&, const project::CommandEnvelope&,
        const project::RevisionView&, project::CommandPlan& out,
        std::vector<core::DiagnosticRecord>&) override
    {
        out.objectWrites = writes;
        out.metadataChange = metadata;
        out.summary = "测试外部项目输入";
        return project::PrepareOutcome::Planned;
    }
};
/** @brief 新建真实磁盘项目；临时目录与存储按逆序析构，避免锁残留。 */
class HostPolicyTest : public ::testing::Test {
protected:
    QTemporaryDir directory;
    std::unique_ptr<project::ProjectStore> store;
    PolicySeedHandler* seed = nullptr;
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        auto opened = project::ProjectStoreFactory::createNew(
            std::filesystem::path(directory.path().toStdWString()) / "policy.rwdesign", "A23");
        store = std::move(opened.store);
        auto handler = std::make_unique<PolicySeedHandler>(); seed = handler.get();
        store->handlerRegistry().registerHandler(std::move(handler));
        HostEngineeringPolicy::registerCommandHandler(store->handlerRegistry());
    }
    /** @brief 以project唯一命令入口发布测试输入；返回真实不可变修订。 */
    project::RevisionView commit()
    {
        const auto base = store->query().head();
        project::CommandEnvelope envelope;
        envelope.branch = base.branch; envelope.expectedRevision = base.id;
        envelope.commandType = "seed-policy-test"; envelope.payloadFormatVersion = 1;
        const auto result = store->commands().submit(envelope);
        EXPECT_TRUE(result.committed());
        return store->query().head();
    }
    /** @brief 策略字节由唯一PolicyCodec生成；行程单位rad，不拼接二进制布局。 */
    policy::RawPolicyInput input(double travel)
    {
        policy::RawPolicyInput raw; raw.policyObject = core::ObjectId::generate();
        raw.collision.enabled = false;
        raw.jointThresholds.finiteRotationTravelLimit = policy::RawThresholdInput{travel, "rad"};
        return raw;
    }
};
}
TEST_F(HostPolicyTest, AmbiguousRevisionMustNotFallBackToDefault_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}), (std::vector<std::string>{"AT-01"}));
    const auto a = input(20.0), b = input(40.0);
    seed->writes = {{a.policyObject,"EngineeringPolicy",policy::PolicyCodec::encode(a)},
                    {b.policyObject,"EngineeringPolicy",policy::PolicyCodec::encode(b)}};
    const auto revision = commit(); HostEngineeringPolicy host; host.bindProjectStore(store.get());
    const auto result = host.resolvePolicyForRevision(revision);
    // 多候选的输入事实不能伪装为系统缺省或任取首项。
    EXPECT_FALSE(result.policy.has_value()); EXPECT_FALSE(result.diagnostics.empty());
    EXPECT_FALSE(host.summary().available);
}
TEST_F(HostPolicyTest, CorruptPolicyMustStayUnavailable_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}), (std::vector<std::string>{"AT-01"}));
    seed->writes = {{core::ObjectId::generate(),"EngineeringPolicy",{1,2,3}}};
    const auto revision = commit(); HostEngineeringPolicy host; host.bindProjectStore(store.get());
    const auto result = host.resolvePolicyForRevision(revision);
    EXPECT_FALSE(result.policy.has_value()); EXPECT_FALSE(result.diagnostics.empty());
    // 卡片同样拒绝显示默认阈值；坏对象必须保持可见失败。
    EXPECT_FALSE(host.summary().available);
}
TEST_F(HostPolicyTest, OldRevisionRemainsExactAfterHeadAdvances_MDL06_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"MDL-06","ARC-05"}), (std::vector<std::string>{"AT-01"}));
    auto raw = input(20.0); seed->writes = {{raw.policyObject,"EngineeringPolicy",policy::PolicyCodec::encode(raw)}};
    const auto first = commit(); raw.jointThresholds.finiteRotationTravelLimit->value = 40.0;
    seed->writes = {{raw.policyObject,"EngineeringPolicy",policy::PolicyCodec::encode(raw)}};
    const auto second = commit(); HostEngineeringPolicy host; host.bindProjectStore(store.get());
    const auto a = host.resolvePolicyForRevision(first), b = host.resolvePolicyForRevision(second);
    ASSERT_TRUE(a.policy); ASSERT_TRUE(b.policy);
    EXPECT_DOUBLE_EQ(a.policy->jointThresholds.finiteRotationTravelLimit.siValue(),20.0);
    EXPECT_DOUBLE_EQ(b.policy->jointThresholds.finiteRotationTravelLimit.siValue(),40.0);
    EXPECT_NE(a.policy->contentIdentity,b.policy->contentIdentity);
    EXPECT_DOUBLE_EQ(host.summary().travelLimit.siValue,40.0);
}
TEST_F(HostPolicyTest, FirstCreateUndoRedoPreservesIdentityAndHistory_ARC05_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05","PM-12"}), (std::vector<std::string>{"AT-01"}));
    HostEngineeringPolicy host; host.bindProjectStore(store.get());
    const auto initial=store->query().head(); auto raw=input(30.0);
    ASSERT_TRUE(HostEngineeringPolicy::submit(*store,initial,raw,true).committed());
    const auto first=store->query().head(); auto parsed=host.resolvePolicyForRevision(first);
    ASSERT_TRUE(parsed.policy); const auto oid=parsed.policy->policyObject;
    EXPECT_NE(oid,raw.policyObject); // 首次由project分配，运输身份不能成为项目身份。
    ASSERT_TRUE(store->undoRedo().undo(first.branch).committed());
    const auto undone=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(undone.policy);
    EXPECT_EQ(undone.policy->contentIdentity,policy::makeSystemDefaultPolicySet().contentIdentity);
    EXPECT_EQ(undone.policy->policyObject,oid);
    ASSERT_TRUE(store->undoRedo().redo(first.branch).committed());
    const auto redone=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(redone.policy);
    EXPECT_EQ(redone.policy->policyObject,oid); EXPECT_DOUBLE_EQ(redone.policy->jointThresholds.finiteRotationTravelLimit.siValue(),30.0);
    // 旧修订仍按原始字节/阈值解析，撤销重做没有覆盖历史。
    EXPECT_EQ(host.resolvePolicyForRevision(first).policy->contentIdentity,parsed.policy->contentIdentity);
    EXPECT_EQ(store->query().tryRevision(initial.id)->objectRefs,initial.objectRefs);
}
TEST_F(HostPolicyTest, InvalidStaleAndFullFieldsRoundTrip_UX08_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","ARC-05"}), (std::vector<std::string>{"AT-01"}));
    HostEngineeringPolicy host;host.bindProjectStore(store.get());auto raw=input(720.0);
    raw.jointThresholds.finiteRotationTravelLimit->unitToken="deg";
    raw.jointThresholds.nearLimitRatio=policy::RawThresholdInput{0.1,"1"};
    raw.jointThresholds.conditionNumberWarning=policy::RawThresholdInput{100.0,"1"};
    raw.collision.enabled=true;raw.collision.enabledDomains={policy::CollisionDomain::Self,policy::CollisionDomain::Tool};
    raw.collision.safetyClearance=policy::RawThresholdInput{20.0,"mm"};
    raw.collision.excludeAdjacentLinksByDefault=false;
    raw.origin.note="来源保留";raw.compatibilityNotes="兼容字段保留";
    const auto base=store->query().head();
    auto invalid=raw;invalid.collision.safetyClearance.reset();
    EXPECT_FALSE(HostEngineeringPolicy::submit(*store,base,invalid,true).committed());
    EXPECT_EQ(store->query().head().id,base.id);
    ASSERT_TRUE(HostEngineeringPolicy::submit(*store,base,raw,true).committed());
    const auto changed=store->query().head();
    EXPECT_FALSE(HostEngineeringPolicy::submit(*store,base,raw,true).committed());
    EXPECT_EQ(store->query().head().id,changed.id);
    const auto resolved=host.resolvePolicyForRevision(changed);ASSERT_TRUE(resolved.policy);
    EXPECT_DOUBLE_EQ(resolved.policy->collision.safetyClearance->siValue(),0.02);
    EXPECT_EQ(resolved.policy->origin.note,raw.origin.note);
    const auto ref=std::find_if(changed.objectRefs.begin(),changed.objectRefs.end(),[](const project::ObjectRef& ref){return ref.objectTypeToken=="EngineeringPolicy";});
    ASSERT_NE(ref,changed.objectRefs.end());
    const auto decoded=policy::PolicyCodec::decode(*store->query().tryObject(ref->objectId,ref->contentVersion));
    EXPECT_EQ(decoded.compatibilityNotes,raw.compatibilityNotes);
    EXPECT_EQ(decoded.collision,raw.collision);EXPECT_EQ(decoded.jointThresholds,raw.jointThresholds);
}
TEST_F(HostPolicyTest, ReopenReadOnlyAndCrossProjectIsolation_PM02_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-08","PM-02","ARC-05"}), (std::vector<std::string>{"AT-01"}));
    auto raw=input(25.0);ASSERT_TRUE(HostEngineeringPolicy::submit(*store,store->query().head(),raw,true).committed());
    HostEngineeringPolicy host;host.bindProjectStore(store.get());const auto revision=store->query().head();
    const auto saved=host.resolvePolicyForRevision(revision);ASSERT_TRUE(saved.policy);
    const auto path=store->canonicalPath();host.bindProjectStore(static_cast<project::ProjectStore*>(nullptr));
    EXPECT_EQ(store->requestClose(),0u);store.reset();
    project::OpenStoreRequest request;request.path=path;request.mode=project::OpenMode::ReadOnly;
    auto reopened=project::ProjectStoreFactory::open(request);store=std::move(reopened.store);
    HostEngineeringPolicy::registerCommandHandler(store->handlerRegistry());host.bindProjectStore(store.get());
    const auto restored=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(restored.policy);
    EXPECT_EQ(saved.policy->contentIdentity,restored.policy->contentIdentity);EXPECT_EQ(saved.policy->policyObject,restored.policy->policyObject);
    raw.policyObject=restored.policy->policyObject;raw.jointThresholds.finiteRotationTravelLimit->value=50;
    EXPECT_FALSE(HostEngineeringPolicy::submit(*store,store->query().head(),raw,false).committed());
    EXPECT_EQ(store->query().head().id,revision.id);
    auto another=project::ProjectStoreFactory::createNew(std::filesystem::path(directory.path().toStdWString())/"second.rwdesign","second");
    host.bindProjectStore(another.store.get());
    const auto other=host.resolvePolicyForRevision(another.store->query().head());ASSERT_TRUE(other.policy);
    EXPECT_EQ(other.policy->contentIdentity,policy::makeSystemDefaultPolicySet().contentIdentity);
    EXPECT_NE(other.policy->policyObject,restored.policy->policyObject);
    host.bindProjectStore(static_cast<project::ProjectStore*>(nullptr));
}
TEST_F(HostPolicyTest, BranchPolicyChangesKeepOtherBranchAndOldRevisionExact_PM12_A23)
{
    IRD_TEST_INFO((std::vector<std::string>{"PM-12","ARC-05","MDL-06"}), (std::vector<std::string>{"AT-01"}));
    auto raw=input(20.0);ASSERT_TRUE(HostEngineeringPolicy::submit(*store,store->query().head(),raw,true).committed());
    const auto first=store->query().head();HostEngineeringPolicy host;host.bindProjectStore(store.get());
    const auto original=host.resolvePolicyForRevision(first);ASSERT_TRUE(original.policy);
    seed->writes.clear();seed->metadata=project::MetadataChange{first.branch,"策略分支",std::nullopt};
    commit();const auto tips=store->query().branchTips();ASSERT_EQ(tips.size(),2u);
    const auto branch=std::find_if(tips.begin(),tips.end(),[&](const project::BranchTip& tip){return tip.id!=first.branch;});
    ASSERT_NE(branch,tips.end());auto base=first;base.branch=branch->id;
    raw.policyObject=original.policy->policyObject;raw.jointThresholds.finiteRotationTravelLimit->value=40.0;
    ASSERT_TRUE(HostEngineeringPolicy::submit(*store,base,raw,false).committed());
    const auto updated=host.resolvePolicyForRevision(store->query().head());ASSERT_TRUE(updated.policy);
    EXPECT_DOUBLE_EQ(updated.policy->jointThresholds.finiteRotationTravelLimit.siValue(),40.0);
    const auto old=host.resolvePolicyForRevision(first);ASSERT_TRUE(old.policy);
    EXPECT_DOUBLE_EQ(old.policy->jointThresholds.finiteRotationTravelLimit.siValue(),20.0);
    const auto after=store->query().branchTips();
    const auto untouched=std::find_if(after.begin(),after.end(),[&](const project::BranchTip& tip){return tip.id==first.branch;});
    ASSERT_NE(untouched,after.end());EXPECT_EQ(untouched->tip,first.id);
}
} // namespace sdurws::ird::ui::test
