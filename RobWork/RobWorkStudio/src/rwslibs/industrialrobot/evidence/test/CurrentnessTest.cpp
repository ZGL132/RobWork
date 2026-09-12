/**
 * @file   CurrentnessTest.cpp
 * @brief  当前性纯投影用例组——内容身份当前性（EV-CUR-1）、不可解析不默认
 *         Current（EV-CUR-2）、历史不可改写（EV-CUR-3）、跨上下文
 *         （EV-CUR-4）、AT-05 载体：依赖提示驱动的重算判定（§4.2.4 修改
 *         求解预算后的复评不冒充原运行）、契约版本短路、条件翻转与条目
 *         消失原因、CurrentnessIndex 会话缓存行为与调用方违约 fail-fast。
 *
 * 设计依据：
 *   - units/evidence.md §8.1（computeCurrentness 五步算法＋规则冻结表）、
 *     §4.2.4（修改求解预算后的复评——新 sliceId 旧 inputBaseline）、
 *     §10.4（S7 切面——内容身份判定不按修订号）、§11 矩阵（EV-CUR-1~4
 *     行的观测点）、§12 EV-T08 行（验证方式＝EV-CUR-1~4；任务契约
 *     tasks/foundation/EV-T08.json ≙WP-05-T08 acceptance 1～3）
 *   - 需求 CON-02（当前性是派生投影、历史证据不可变）、CON-05（切片内容
 *     身份＝失效判据）、TASK-03（跨上下文拦截）
 *
 * 替身边界声明（EV-REG-3 同源纪律）：本文件的 MapFactSource（依赖事实源
 * 替身）与 AcceptAllClosureSource（修订闭包替身）仅验证 evidence 当前性
 * 投影契约（声明→条目解析面、闭包放行），其返回内容不构成任何 project/
 * policy/runtime 侧实现正确性证明，也不构成 IK/动力学等业务算法正确性
 * 证明；用例中的"对象/配置/策略"均为契约形态数据（§11：可控测试替身只
 * 验证 evidence 契约）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 EnvelopeTest 同款风格，自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
const char* kHex32D = "dddddddddddddddddddddddddddddddd";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
const char* kHex64D = "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";

core::ContentIdentity cid(const char* hex64)
{
    return core::ContentIdentity::fromCanonical(std::string{"cid-"} + hex64);
}

core::ContentVersion cv(const char* hex64)
{
    return core::ContentVersion::fromCanonical(std::string{"cv-"} + hex64);
}

core::ObjectId oid(const char* hex32)
{
    return core::ObjectId::fromCanonical(std::string{"obj-"} + hex32);
}

core::ProjectId projectA()
{
    return core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32A);
}

core::ProjectId projectB()
{
    return core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32B);
}

core::BranchId branchA()
{
    return core::BranchId::fromCanonical(std::string{"brn-"} + kHex32A);
}

core::BranchId branchB()
{
    return core::BranchId::fromCanonical(std::string{"brn-"} + kHex32B);
}

/// 两个不同的锚定修订（rev1＝归档时刻、rev2＝HEAD 前进后——EV-CUR-1 的
/// "HEAD 修订号不参与判定"观测面）。
core::RevisionId revision1()
{
    return core::RevisionId::fromCanonical(std::string{"rev-"} + kHex32C);
}

core::RevisionId revision2()
{
    return core::RevisionId::fromCanonical(std::string{"rev-"} + kHex32D);
}

/// 场景对象：模型机器人对象（运动学评估的消费对象——进切片）。
core::ObjectId robotObject() { return oid(kHex32A); }

/// 场景对象：电机成本对象（S7 正例——不进运动学切片的无关变化源）。
core::ObjectId motorCostObject() { return oid(kHex32B); }

/// 合法任务五元组（execution 分配形态——五字段全部 isValid）。
core::TaskIdentity validTask()
{
    core::TaskIdentity t;
    t.project = projectA();
    t.branch = branchA();
    t.revision = revision1();
    t.run = core::RunId::fromCanonical(std::string{"run-"} + kHex32D);
    t.attempt = core::AttemptId{1};
    return t;
}

/// 修订闭包事实来源替身：对一切 (oid,cv) 回答 true（builder 的防混入校验
/// 不是本文件被测面——快照组装协议归 EV-T03 用例）。
class AcceptAllClosureSource : public IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/**
 * @brief 组装冻结快照（各用例共用底座——对象闭包/策略/名称映射/复现块
 *        齐备，SnapshotBuilder 产出）。
 *
 * @param project  [in] 项目身份（跨上下文用例以此为变量）
 * @param branch   [in] 方案分支身份（同上）
 * @param revision [in] 锚定修订（HEAD 前进观测面以此为变量）
 * @param robotCv  [in] 机器人对象的内容版本（消费对象变化观测面）
 * @param costCv   [in] 电机成本对象的内容版本（无关变化源——不进切片）
 */
AnalysisSnapshot makeSnapshot(core::ProjectId project, core::BranchId branch,
                              core::RevisionId revision,
                              core::ContentVersion robotCv, core::ContentVersion costCv)
{
    SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 7);
    b.setPolicyRef(PolicyRef{cid(kHex64A)});
    b.setNameMapRef(NameMapRef{cid(kHex64B)});
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    // 机器人对象进闭包（切片 Object 条目子集校验的事实面）。
    ObjectRefEntry robot;
    robot.objectId = robotObject();
    robot.contentVersion = robotCv;
    robot.objectTypeToken = "robot-design";
    robot.digest = robot.contentVersion.bytes;
    b.addObjectRef(robot);
    // 电机成本对象同样进闭包（它是项目对象——但不被运动学评估声明消费，
    // S7 正例：它变化不改变运动学切片身份）。
    ObjectRefEntry cost;
    cost.objectId = motorCostObject();
    cost.contentVersion = costCv;
    cost.objectTypeToken = "catalog.motor-cost";
    cost.digest = cost.contentVersion.bytes;
    b.addObjectRef(cost);
    AcceptAllClosureSource source;
    return b.build(source);
}

/**
 * @brief 依赖事实源替身：按（声明键→预置条目）表回答；表外键＝无法解析
 *        （nullopt——EV-CUR-2 触发面）。调用计数供契约短路用例断言
 *        （步骤 2 命中时事实源不得被调用）。
 */
class MapFactSource : public ICurrentnessFactSource {
public:
    std::map<std::string, DependencyEntry> table; ///< 预置解析结果（键＝声明键）
    mutable int calls = 0;                        ///< 调用计数（mutable——接口为 const）

    std::optional<DependencyEntry>
    tryCurrentEntry(const DependencyDeclaration& declaration) const override
    {
        ++calls;
        const auto it = table.find(declaration.key);
        if (it == table.end()) {
            return std::nullopt;   // 表外键＝对象缺失/资源缺失（不可解析）
        }
        return it->second;
    }
};

/// Object 条目构造辅助（载荷三元组齐备；key 与 typeToken 语义角色分离）。
DependencyEntry objectEntry(const std::string& key, core::ObjectId id,
                            core::ContentVersion v, const std::string& typeToken)
{
    DependencyEntry e;
    e.key = key;
    e.kind = DependencyKind::Object;
    ObjectDependencyPayload p;
    p.objectId = id;
    p.contentVersion = v;
    p.objectTypeToken = typeToken;
    e.payload = p;
    e.applied = true;
    return e;
}

/// Configuration 条目构造辅助（求解配置子集——canonical 字节＋内容身份；
/// §4.2.4 复评场景的"预算条目"载体）。
DependencyEntry configEntry(const std::string& key, std::vector<std::uint8_t> bytes,
                            core::ContentIdentity identity)
{
    DependencyEntry e;
    e.key = key;
    e.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload p;
    p.configKindToken = "ik-solver-budget";
    p.canonicalBytes = std::move(bytes);
    p.contentIdentity = identity;
    e.payload = p;
    e.applied = true;
    return e;
}

/// Policy 条目构造辅助（条件依赖的载荷形态——碰撞启用属策略语义）。
DependencyEntry policyEntry(const std::string& key, core::ContentIdentity identity,
                            bool applied, const char* notAppliedReason)
{
    DependencyEntry e;
    e.key = key;
    e.kind = DependencyKind::Policy;
    PolicyDependencyPayload p;
    p.policyContentIdentity = identity;
    e.payload = p;
    e.applied = applied;
    if (!applied) {
        e.notAppliedReason = notAppliedReason;
    }
    return e;
}

/// 标准依赖声明集：模型对象（必需）＋求解预算（必需）——AT-05 场景底座。
std::vector<DependencyDeclaration> standardDeclarations()
{
    DependencyDeclaration robot;
    robot.key = "model.robot-design";
    robot.kind = DependencyKind::Object;
    robot.requiredness = DependencyRequiredness::Required;

    DependencyDeclaration budget;
    budget.key = "solve.budget";
    budget.kind = DependencyKind::Configuration;
    budget.requiredness = DependencyRequiredness::Required;

    return {robot, budget};
}

/// 仅模型对象的声明集（EV-CUR-1 的运动学切片——不消费电机成本/预算）。
std::vector<DependencyDeclaration> robotOnlyDeclarations()
{
    DependencyDeclaration robot;
    robot.key = "model.robot-design";
    robot.kind = DependencyKind::Object;
    robot.requiredness = DependencyRequiredness::Required;
    return {robot};
}

/**
 * @brief 冻结一个切片（SliceBuilder 直通——各用例在其上做单点变异）。
 *
 * 条目 (oid,cv) 必须落在传入快照的 objectClosure 内（子集校验——调用方
 * 保证事实一致）。
 */
InputSlice freezeSlice(const AnalysisSnapshot& snapshot,
                       const std::vector<DependencyDeclaration>& declarations,
                       const std::vector<DependencyEntry>& entries,
                       std::uint32_t contractVersion = 7)
{
    SliceBuilder b;
    b.setEvaluation("kin-batch-ik", contractVersion);
    for (const DependencyEntry& e : entries) {
        b.addEntry(e);
    }
    (void)declarations;   // 声明集仅作文档语义（条目由声明解析而来——测试
                          // 直接给解析结果，与运行时 SliceBuilder 用法一致）
    return b.build(snapshot);
}

/// 组装目标上下文（EV-CUR 场景底座——项目/分支/评估键/当前配置身份）。
CurrentnessTarget makeTarget(core::ProjectId project, core::BranchId branch)
{
    CurrentnessTarget t;
    t.projectId = project;
    t.branchId = branch;
    t.evaluationKey = "kin-batch-ik";
    t.currentConfigIdentity = cid(kHex64C);
    return t;
}

/// 组装依赖重建源（快照＋事实源＋声明集＋当前契约版本的值传递承载）。
CurrentnessSources makeSources(const AnalysisSnapshot& targetSnapshot,
                               const MapFactSource& facts,
                               std::vector<DependencyDeclaration> declarations,
                               const InputSlice* resultSlice = nullptr)
{
    CurrentnessSources s;
    s.targetSnapshot = &targetSnapshot;
    s.facts = &facts;
    s.currentContractVersion = 7;
    s.declarations = std::move(declarations);
    s.resultSlice = resultSlice;
    return s;
}

/// 组装结果引用（身份摘要形态——与原切片身份一致由调用方保证）。
ResultRef makeResultRef(const InputSlice& resultSlice, core::TaskIdentity task)
{
    ResultRef r;
    r.task = task;
    r.evaluationKey = resultSlice.evaluationKey;
    r.evaluatorContractVersion = resultSlice.evaluatorContractVersion;
    r.sliceId = resultSlice.sliceId;
    r.inputBaselineId = resultSlice.inputBaselineId;
    return r;
}

// =====================================================================
// EV-CUR-1 内容身份当前性（CON-02/05、S7——acceptance 1）
// =====================================================================

/** HEAD 前进（仅电机成本变化）→ 运动学结果仍 Current：reasons 空、修订号
 *  不参与判定（目标快照锚定 rev2≠rev1）。 */
TEST(EvidenceCurrentness, CurrentOnIrrelevantHeadAdvance_EV_CUR_1)
{
    // 归档时刻：rev1 锚定 robot@cvA＋电机成本@cvM；原切片只消费机器人。
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // HEAD 前进：rev2 锚定 robot@cvA（未变）＋电机成本@cvM2（变了——但
    // 不被运动学切片声明消费，S7 正例）。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));

    // Current：内容身份相等；无关对象的变化不产生任何失效原因。
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Current);
    EXPECT_TRUE(out.reasons.empty()) << "无关对象变化不得产生失效原因";
    EXPECT_FALSE(out.unevaluableCause.has_value());
    EXPECT_TRUE(out.diagnostics.empty());
    // "HEAD 修订号不参与判定"的正面观测：目标快照锚定 rev2（≠归档 rev1）
    // 仍判 Current——若实现误比修订号，本用例即翻红。
    EXPECT_EQ(out.evaluatedAgainst.projectId, projectA());
}

/** 被消费对象内容变化 → Superseded＋ObjectContentChanged，detail 携带旧→
 *  新两版规范文本（KIN-13 重算提示的数据源）。 */
TEST(EvidenceCurrentness, SupersededOnConsumedObjectChange_EV_CUR_1)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // HEAD 前进且机器人对象内容变化（cvA→cvC——被消费条目，§8.1 步骤 3
    // 体现为切片身份变化）。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64C), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64C), "robot-design");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));

    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Superseded);
    ASSERT_EQ(out.reasons.size(), 1u);
    EXPECT_EQ(out.reasons[0].dependencyKey, "model.robot-design");
    EXPECT_EQ(out.reasons[0].kind, InvalidationKind::ObjectContentChanged);
    // detail 携带旧→新 contentVersion 规范文本（失效原因输出的具体性——
    // §8.1 步骤 4"ObjectContentChanged(旧cv→新cv)"）。
    EXPECT_NE(out.reasons[0].detail.find(cv(kHex64A).toCanonical()), std::string::npos);
    EXPECT_NE(out.reasons[0].detail.find(cv(kHex64C).toCanonical()), std::string::npos);
}

/** 新会话同内容（EV-ID-3 的当前性面：重开"项目"后同对象内容重建）→
 *  Current，无任何失效原因条目产生。 */
TEST(EvidenceCurrentness, NewSessionSameContentStaysCurrent_EV_CUR_1)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // 新会话：新修订 id（rev2）＋同样的对象内容（cvA）——内容身份不依赖
    // 会话/修订载体（CON-05）。
    const AnalysisSnapshot reopenSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64B));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(reopenSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Current);
    EXPECT_TRUE(out.reasons.empty());
}

// =====================================================================
// EV-CUR-2 不可解析不默认 Current（§8.1 步骤 3——acceptance 1）
// =====================================================================

/** 目标上下文某依赖对象缺失 → status==nullopt＋unresolved-dependency 诊断
 *  （无持久第三状态——P-EV-4 计算形态）。 */
TEST(EvidenceCurrentness, UnresolvedDependencyNeverDefaultsToCurrent_EV_CUR_2)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    // 事实源表为空＝机器人对象在目标上下文无法解析。
    MapFactSource facts;

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));

    // 不可判定形态：status 空＋诊断非空（EV-CUR-2 观测点），**绝不**产出
    // Current（任务约束§五.8 无默认 Current）。
    EXPECT_FALSE(out.status.has_value());
    ASSERT_TRUE(out.unevaluableCause.has_value());
    EXPECT_EQ(*out.unevaluableCause, UnevaluableCause::UnresolvedDependency);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_TRUE(out.reasons.empty());
    // 诊断建议码与涉事键可定位（消费者据此提示补齐依赖）。
    EXPECT_EQ(out.diagnostics[0].code, std::string{kDiagCurrentnessUnresolvedDependency});
    EXPECT_NE(out.diagnostics[0].cause.find("model.robot-design"), std::string::npos);
}

/** 多个依赖同时缺失 → 逐键全量列出诊断（声明序——缺失全量列出，
 *  NFR-COR-03 同口径；不在首错处短路）。 */
TEST(EvidenceCurrentness, UnresolvedListsAllMissingKeys_EV_CUR_2)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, standardDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design"),
         configEntry("solve.budget", {0x01}, cid(kHex64B))});
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;   // 双键全部缺失

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, standardDeclarations(),
                                         &archived));
    EXPECT_FALSE(out.status.has_value());
    // 双键各一条诊断（全量、声明序——model.robot-design 先于 solve.budget）。
    ASSERT_EQ(out.diagnostics.size(), 2u);
    EXPECT_NE(out.diagnostics[0].cause.find("model.robot-design"), std::string::npos);
    EXPECT_NE(out.diagnostics[1].cause.find("solve.budget"), std::string::npos);
}

/** 事实源与目标快照事实面不一致（Object 条目不在目标闭包——SliceBuilder
 *  子集校验拒绝）→ 转译为不可判定＋诊断，不抛出、不默认 Current。 */
TEST(EvidenceCurrentness, TargetRefreezeFailureIsUnevaluable_EV_CUR_2)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // 目标快照闭包含 robot@cvA，但事实源谎报 robot@cvC（非同一事实面——
    // 组装与查询之间上下文演进的形态化）。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64C), "robot-design");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));
    EXPECT_FALSE(out.status.has_value());
    ASSERT_TRUE(out.unevaluableCause.has_value());
    EXPECT_EQ(*out.unevaluableCause, UnevaluableCause::UnresolvedDependency);
    ASSERT_FALSE(out.diagnostics.empty());
    // 诊断透出原冻结拒绝细节（不静默吞错——NFR-COR-03）。
    EXPECT_FALSE(out.diagnostics[0].cause.empty());
}

/** 事实源返回与声明键/Kind 不符的条目 → 调用方契约违约 fail-fast
 *  （DeclarationInvalid——静默纠正会掩盖事实源 bug）。 */
TEST(EvidenceCurrentness, FactSourceEntryMismatchFailsFast_EV_CUR_2)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    // 谎报 Kind：声明为 Object，返回 Policy（键相同）。
    facts.table["model.robot-design"]
        = policyEntry("model.robot-design", cid(kHex64B), true, nullptr);

    const CurrentnessSources sources
        = makeSources(headSnapshot, facts, robotOnlyDeclarations(), &archived);
    const CurrentnessTarget target = makeTarget(projectA(), branchA());
    bool thrown = false;
    try {
        (void)computeCurrentness(makeResultRef(archived, validTask()), target, sources);
    } catch (const EvidenceError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), EvidenceErrorCode::DeclarationInvalid);
    }
    EXPECT_TRUE(thrown) << "事实源与声明不符必须 fail-fast";
}

// =====================================================================
// EV-CUR-3 历史不可改写（CON-02/AT-05——acceptance 1＋2）
// =====================================================================

/** 归档包络构造辅助（表 3 行 1 合法组合——Completed×Feasible；EV-CUR-3
 *  的"归档结果"载体；与 EnvelopeTest 同构但自持）。 */
ResultEnvelope makeArchivedEnvelope(const InputSlice& archivedSlice,
                                    const AnalysisSnapshot& archiveSnapshot)
{
    ResultEnvelopeDraft d;
    d.task = validTask();
    d.evaluationKey = "kin-batch-ik";
    d.evaluatorContractVersion = 7;
    d.mode = core::EvaluationMode::Verified;
    d.snapshotId = archiveSnapshot.snapshotId;
    d.sliceId = archivedSlice.sliceId;
    d.inputBaselineId = archivedSlice.inputBaselineId;
    d.caseScope.caseIds = {oid(kHex32C)};
    d.profile.profileId = "kin";
    d.profile.version = "1.0.0";
    d.profile.contentIdentity = cid(kHex64B);
    d.outcome = core::TaskOutcome::Completed;
    d.engineeringStatus = core::EngineeringStatus::Feasible;
    // 证据清单：与包络同名绑定一致＋一条 Satisfied 项（presence 纪律）。
    EvidenceManifest m;
    m.snapshotId = archiveSnapshot.snapshotId;
    m.sliceId = archivedSlice.sliceId;
    m.profileId = "kin";
    m.profileVersion = "1.0.0";
    m.profileContentIdentity = cid(kHex64B);
    EvidenceItem ok;
    ok.itemId = "kin.reach-per-task-point";
    ok.status = EvidenceItemStatus::Satisfied;
    ok.artifactDigest = cv(kHex64A).bytes;
    m.items = {ok};
    d.evidence = m;
    d.payload = DomainPayloadDraft{"kin.batch-ik.v1", {0x01, 0x02, 0x03}};
    d.producer.producedIn = ProducerProcess::MainProcess;
    d.producer.productVersion = "industrialrobot-designer 0.1.0";
    return ResultEnvelope::make(std::move(d));
}

/** Superseded 判定前后归档包络逐字段不变（payload/engineeringStatus/摘要
 *  全等）——当前性纯投影、无持久化副作用（acceptance 2 的断言面）。 */
TEST(EvidenceCurrentness, SupersededDoesNotTouchArchivedEnvelope_EV_CUR_3)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultEnvelope archivedEnvelope = makeArchivedEnvelope(archived, archiveSnapshot);
    const ResultRef result = ResultRef::fromEnvelope(archivedEnvelope);

    // 判定前快照包络全量字节（== 为全字段比较—— envelope 结构注释）。
    const ResultEnvelope before = archivedEnvelope;

    // 目标上下文：机器人对象已变化 → Superseded。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64C), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64C), "robot-design");
    const CurrentnessResult first
        = computeCurrentness(ResultRef::fromEnvelope(archivedEnvelope),
                             makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));
    ASSERT_TRUE(first.status.has_value());
    EXPECT_EQ(*first.status, CurrentnessStatus::Superseded);

    // 归档包络逐字节不变：payload 与 engineeringStatus 不变、全字段等值
    // （EV-CUR-3"重读归档 envelope 比对字节/摘要"）。
    EXPECT_EQ(archivedEnvelope, before);
    ASSERT_TRUE(archivedEnvelope.payload.has_value());
    EXPECT_EQ(archivedEnvelope.payload->digest, before.payload->digest);
    EXPECT_EQ(archivedEnvelope.engineeringStatus, before.engineeringStatus);

    // 重复判定结果相同（纯函数——同输入必同输出，NFR-COR-02）。
    const CurrentnessResult second
        = computeCurrentness(ResultRef::fromEnvelope(archivedEnvelope),
                             makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));
    EXPECT_EQ(first, second);
}

/** 当前性只存在于投影索引：判定本身不写缓存（find 未命中），store 后才
 *  命中；store 的是拷贝——修改原 result 不影响缓存内容。 */
TEST(EvidenceCurrentness, CurrentnessLivesOnlyInProjectionIndex_EV_CUR_3)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    // 判定（Current）不产生任何缓存痕迹——写缓存是调用方显式动作。
    CurrentnessIndex index;
    const CurrentnessResult computed
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, robotOnlyDeclarations(),
                                         &archived));
    EXPECT_FALSE(index.find(result.task.run, "ctx-a").has_value());

    // 调用方显式 store 后命中；store 的是拷贝（修改源对象不影响缓存）。
    index.store(result.task.run, "ctx-a", computed);
    const std::optional<CurrentnessResult> cached = index.find(result.task.run, "ctx-a");
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(*cached, computed);
    // 缓存值与归档切片/包络无关联面（投影值独立——EV-CUR-3"currentness
    // 仅存在于投影索引"）：修改调用方副本不回染缓存。
    CurrentnessResult mutated = computed;
    mutated.status = CurrentnessStatus::Superseded;
    EXPECT_EQ(*index.find(result.task.run, "ctx-a"), computed);
    (void)mutated;
}

// =====================================================================
// EV-CUR-4 跨上下文（TASK-03/AT-10——acceptance 1）
// =====================================================================

/** 项目 A 结果对项目 B 上下文查询 → 不可判定{cross-context}＋诊断；不产生
 *  项目 B 侧任何持久状态（索引无痕迹——判定是纯计算）。 */
TEST(EvidenceCurrentness, CrossProjectQueryIsUnevaluable_EV_CUR_4)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // 项目 B 的目标上下文（快照也组装在项目 B 之下——S7 迟到结果切面）。
    const AnalysisSnapshot projectBSnapshot
        = makeSnapshot(projectB(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    CurrentnessIndex projectBIndex;   // 项目 B 侧会话态（判定前为空）
    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectB(), branchA()),
                             makeSources(projectBSnapshot, facts,
                                         robotOnlyDeclarations(), &archived));

    // 不可判定{cross-context}：status 空＋CrossContext＋诊断非空。
    EXPECT_FALSE(out.status.has_value());
    ASSERT_TRUE(out.unevaluableCause.has_value());
    EXPECT_EQ(*out.unevaluableCause, UnevaluableCause::CrossContext);
    ASSERT_FALSE(out.diagnostics.empty());
    EXPECT_EQ(out.diagnostics[0].code, std::string{kDiagCurrentnessCrossContext});
    EXPECT_TRUE(out.reasons.empty());
    // 不产生项目 B 侧持久状态：判定后索引仍空（computeCurrentness 无处
    // 可写——纯投影承诺的结构性保证）。
    EXPECT_FALSE(projectBIndex.find(result.task.run, "ctx-b").has_value());
}

/** 同项目跨分支查询 → 同为跨上下文（分支是上下文一部分——§8.1 规则表
 *  行 1"分支/方案匹配"）。 */
TEST(EvidenceCurrentness, CrossBranchQueryIsUnevaluable_EV_CUR_4)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot branchBSnapshot
        = makeSnapshot(projectA(), branchB(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchB()),
                             makeSources(branchBSnapshot, facts,
                                         robotOnlyDeclarations(), &archived));
    EXPECT_FALSE(out.status.has_value());
    ASSERT_TRUE(out.unevaluableCause.has_value());
    EXPECT_EQ(*out.unevaluableCause, UnevaluableCause::CrossContext);
}

/** 评估键失配（结果为 A 键、查询目标为 B 键——上下文四要素之一，I-7）→
 *  不可判定{cross-context}＋区分维度的诊断。 */
TEST(EvidenceCurrentness, EvaluationKeyMismatchIsCrossContext_I_7)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    // 目标上下文的评估键与结果不一致（拿运动学结果查覆盖评估的当前性）。
    CurrentnessTarget target = makeTarget(projectA(), branchA());
    target.evaluationKey = "kin-region-coverage";
    const CurrentnessResult out
        = computeCurrentness(result, target,
                             makeSources(headSnapshot, facts,
                                         robotOnlyDeclarations(), &archived));
    EXPECT_FALSE(out.status.has_value());
    ASSERT_TRUE(out.unevaluableCause.has_value());
    EXPECT_EQ(*out.unevaluableCause, UnevaluableCause::CrossContext);
    ASSERT_FALSE(out.diagnostics.empty());
    // 诊断区分失配维度（评估键而非项目/分支）。
    EXPECT_NE(out.diagnostics[0].cause.find("评估键"), std::string::npos);
}

/** 身份摘要形态降级（I-8）：结果切片条目不可得时，sliceId 全值比较——
 *  跨修订（快照锚差异）保守判 Superseded＋SliceContentChanged，绝不误判
 *  Current。 */
TEST(EvidenceCurrentness, IdentityOnlyFormDegradesConservatively_I_8)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());

    // HEAD 前进：目标快照锚定 rev2——条目内容与归档一致（robot 未变），
    // 但身份摘要形态无法做条目级对比，sliceId 全值（含快照锚）不等。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");

    // resultSlice 不注入（nullptr——身份摘要形态）。
    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts,
                                         robotOnlyDeclarations(), nullptr));
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Superseded);
    ASSERT_EQ(out.reasons.size(), 1u);
    EXPECT_EQ(out.reasons[0].kind, InvalidationKind::SliceContentChanged);
    // detail 注明降级原因与补救路径（提供切片可执行条目级判定）。
    EXPECT_NE(out.reasons[0].detail.find("身份摘要形态"), std::string::npos);
}

// =====================================================================
// AT-05 载体：依赖提示驱动的重算判定（§4.2.4——acceptance 3）
// =====================================================================

/** 修改求解预算后的复评：预算条目变化 → 新 sliceId（缓存不命中→重算→
 *  新运行），inputBaselineId 不变（同一次冻结输入研究——复评不冒充原运
 *  行）；判定输出 Superseded＋ConfigurationChanged，detail 携带旧→新配置
 *  身份（KIN-13"新旧结果不可直接比较"提示的数据源）。 */
TEST(EvidenceCurrentness, BudgetChangeSupersedesWithBaselineUnchanged_AT_05)
{
    // 归档时刻：预算 v1（canonical 字节 0x01——求解预算的域 canonical 承载）。
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, standardDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design"),
         configEntry("solve.budget", {0x01}, cid(kHex64A))});
    const ResultRef result = makeResultRef(archived, validTask());

    // 复评时刻：仅扩大求解预算（v2）——模型对象未变。
    const AnalysisSnapshot revisitSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64B));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");
    facts.table["solve.budget"] = configEntry("solve.budget", {0x05}, cid(kHex64B));

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(revisitSnapshot, facts, standardDeclarations(),
                                         &archived));

    // Superseded＋ConfigurationChanged（依赖提示驱动的重算判定——重算
    // 提示来自失效原因清单而非时钟/修订号）。
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Superseded);
    ASSERT_EQ(out.reasons.size(), 1u);
    EXPECT_EQ(out.reasons[0].dependencyKey, "solve.budget");
    EXPECT_EQ(out.reasons[0].kind, InvalidationKind::ConfigurationChanged);
    EXPECT_NE(out.reasons[0].detail.find(cid(kHex64A).toCanonical()), std::string::npos);
    EXPECT_NE(out.reasons[0].detail.find(cid(kHex64B).toCanonical()), std::string::npos);

    // §4.2.4 的双层身份断言：求解类 Configuration 不进基准身份（§5.1
    // 排除面/D-1）——在同一快照上仅改预算冻结的两个切片：sliceId 不等
    // （缓存不命中→重算→新运行）而 inputBaselineId 相等（同一次冻结输入
    // 研究——复评不冒充原运行，旧运行结果保留为原快照历史证据）。切片按
    // 同一 builder 协议冻结（与 computeCurrentness 步骤 3 同一事实面）。
    InputSlice baselineUnchanged;
    {
        SliceBuilder b;
        b.setEvaluation("kin-batch-ik", 7);
        b.addEntry(objectEntry("model.robot-design", robotObject(), cv(kHex64A),
                               "robot-design"));
        b.addEntry(configEntry("solve.budget", {0x05}, cid(kHex64B)));
        baselineUnchanged = b.build(archiveSnapshot);
    }
    EXPECT_FALSE(archived.sliceId == baselineUnchanged.sliceId) << "预算变化必须改变 sliceId";
    EXPECT_TRUE(archived.inputBaselineId == baselineUnchanged.inputBaselineId)
        << "求解预算不进基准身份（§5.1 排除求解类 Configuration）——新旧同基准";
}

// =====================================================================
// 契约版本短路（§8.1 步骤 2——evaluator-contract-changed）
// =====================================================================

/** 契约版本不一致 → Superseded＋EvaluatorContractChanged，且**不进入**条
 *  目解析/切片重建（事实源调用数为 0——契约升级后条目级比较失去前提）。 */
TEST(EvidenceCurrentness, ContractVersionChangeSupersedesBeforeRebuild_EV_CUR_1)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    // 归档切片以契约版本 6 产生；当前 descriptor 已升到 7。
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")},
        6);
    const ResultRef result = makeResultRef(archived, validTask());

    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;   // 表为空——若被调用即产诊断，可一并暴露短路失效

    CurrentnessSources sources
        = makeSources(headSnapshot, facts, robotOnlyDeclarations(), &archived);
    sources.currentContractVersion = 7;

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()), sources);
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Superseded);
    ASSERT_EQ(out.reasons.size(), 1u);
    EXPECT_EQ(out.reasons[0].kind, InvalidationKind::EvaluatorContractChanged);
    EXPECT_NE(out.reasons[0].detail.find("6"), std::string::npos);
    EXPECT_NE(out.reasons[0].detail.find("7"), std::string::npos);
    // 短路观测：契约失配即返回，事实源一次都不被调用。
    EXPECT_EQ(facts.calls, 0);
}

// =====================================================================
// 条目级原因的其他形态（ConditionFlipped/条目消失——§8.1 步骤 4"…"扩展）
// =====================================================================

/** 条件依赖适用性翻转（applied true→false）→ ConditionFlipped；历史切片
 *  有而目标声明集无的条目 → SampleBaselineChanged＋"已消失"detail。 */
TEST(EvidenceCurrentness, ConditionFlipAndVanishedEntryReasons_EV_CUR_1)
{
    // 归档时刻声明：模型对象＋条件策略（碰撞守卫，已适用）＋样本区域。
    DependencyDeclaration robotDecl;
    robotDecl.key = "model.robot-design";
    robotDecl.kind = DependencyKind::Object;
    DependencyDeclaration guardDecl;
    guardDecl.key = "policy.collision-guard";
    guardDecl.kind = DependencyKind::Policy;
    guardDecl.requiredness = DependencyRequiredness::Conditional;
    guardDecl.applicability = ApplicabilityCondition{"策略启用碰撞", {"model.robot-design"}};
    DependencyDeclaration sampleDecl;
    sampleDecl.key = "sample.region";
    sampleDecl.kind = DependencyKind::SampleSet;

    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    DependencyEntry samplePayload;
    samplePayload.key = "sample.region";
    samplePayload.kind = DependencyKind::SampleSet;
    SampleSetDependencyPayload sp;
    sp.regionObjectId = oid(kHex32C);
    sp.sampleSetIdentity = cid(kHex64C);
    samplePayload.payload = sp;
    const InputSlice archived = freezeSlice(
        archiveSnapshot, {robotDecl, guardDecl, sampleDecl},
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design"),
         policyEntry("policy.collision-guard", cid(kHex64D), true, nullptr),
         samplePayload});
    const ResultRef result = makeResultRef(archived, validTask());

    // 目标时刻声明集收窄（样本区域不再消费）且条件策略翻转为未适用
    // （策略停用碰撞——载荷不变、applied 变）。
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    facts.table["model.robot-design"]
        = objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design");
    facts.table["policy.collision-guard"]
        = policyEntry("policy.collision-guard", cid(kHex64D), false, "策略停用碰撞");

    const CurrentnessResult out
        = computeCurrentness(result, makeTarget(projectA(), branchA()),
                             makeSources(headSnapshot, facts, {robotDecl, guardDecl},
                                         &archived));
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Superseded);
    ASSERT_EQ(out.reasons.size(), 2u);
    // 条件翻转优先于载荷比对（载荷相同、applied 不同）。
    EXPECT_EQ(out.reasons[0].dependencyKey, "policy.collision-guard");
    EXPECT_EQ(out.reasons[0].kind, InvalidationKind::ConditionFlipped);
    // 目标声明集无样本区域 → 历史条目"消失"原因（SampleBaselineChanged）。
    EXPECT_EQ(out.reasons[1].dependencyKey, "sample.region");
    EXPECT_EQ(out.reasons[1].kind, InvalidationKind::SampleBaselineChanged);
    EXPECT_NE(out.reasons[1].detail.find("消失"), std::string::npos);
}

// =====================================================================
// CurrentnessIndex 行为（§8.1 规则表行 3——会话态缓存）
// =====================================================================

/** store→find 命中等值；invalidateRun 后未命中（修订事件按需重算的触发
 *  点）；不同 targetContextId 互不影响；clear 全清；空串键拒绝。 */
TEST(EvidenceCurrentness, IndexStoreFindInvalidateClear)
{
    CurrentnessIndex index;
    const core::RunId run = core::RunId::fromCanonical(std::string{"run-"} + kHex32D);
    const CurrentnessTarget target = makeTarget(projectA(), branchA());

    CurrentnessResult r;
    r.status = CurrentnessStatus::Current;
    r.evaluatedAgainst = target;

    // 空串上下文标识＝键契约违约（presence 噪声——store 契约）。
    bool thrown = false;
    try {
        index.store(run, "", r);
    } catch (const EvidenceError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), EvidenceErrorCode::CacheIncompatible);
    }
    EXPECT_TRUE(thrown);

    // 非保留 runId＋非空上下文：store→find 命中且等值。
    index.store(run, "ctx-a", r);
    const std::optional<CurrentnessResult> hit = index.find(run, "ctx-a");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(*hit, r);

    // 另一目标上下文的投影独立存放（同一运行可对多上下文各有一份）。
    EXPECT_FALSE(index.find(run, "ctx-b").has_value());

    // 修订事件到达：按运行粒度整体失效（幂等——重复失效不抛）。
    index.invalidateRun(run);
    EXPECT_FALSE(index.find(run, "ctx-a").has_value());
    index.invalidateRun(run);   // 幂等

    // clear 全清：store 后 clear，两键均未命中。
    index.store(run, "ctx-c", r);
    index.clear();
    EXPECT_FALSE(index.find(run, "ctx-c").has_value());

    // 保留值 runId 拒绝（缓存键契约——fail-fast）。
    thrown = false;
    try {
        index.store(core::RunId{}, "ctx-d", r);
    } catch (const EvidenceError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), EvidenceErrorCode::CacheIncompatible);
    }
    EXPECT_TRUE(thrown);
}

// =====================================================================
// 调用方契约违约 fail-fast（重建事实面未注入/声明集为空）
// =====================================================================

/** 事实源/目标快照缺位、声明集为空 → EvidenceError（fail-fast——不可判
 *  定是目标上下文的状态，不是调用方忘给输入的兜底）。 */
TEST(EvidenceCurrentness, MissingRebuildSourcesFailFast)
{
    const AnalysisSnapshot archiveSnapshot
        = makeSnapshot(projectA(), branchA(), revision1(), cv(kHex64A), cv(kHex64B));
    const InputSlice archived = freezeSlice(
        archiveSnapshot, robotOnlyDeclarations(),
        {objectEntry("model.robot-design", robotObject(), cv(kHex64A), "robot-design")});
    const ResultRef result = makeResultRef(archived, validTask());
    const AnalysisSnapshot headSnapshot
        = makeSnapshot(projectA(), branchA(), revision2(), cv(kHex64A), cv(kHex64D));
    MapFactSource facts;
    const CurrentnessTarget target = makeTarget(projectA(), branchA());

    // 事实源未注入（null 指针）。
    {
        CurrentnessSources s = makeSources(headSnapshot, facts, robotOnlyDeclarations());
        s.facts = nullptr;
        bool thrown = false;
        try {
            (void)computeCurrentness(result, target, s);
        } catch (const EvidenceError& e) {
            thrown = true;
            EXPECT_EQ(e.code(), EvidenceErrorCode::SnapshotIncomplete);
        }
        EXPECT_TRUE(thrown);
    }
    // 目标快照未注入。
    {
        CurrentnessSources s = makeSources(headSnapshot, facts, robotOnlyDeclarations());
        s.targetSnapshot = nullptr;
        bool thrown = false;
        try {
            (void)computeCurrentness(result, target, s);
        } catch (const EvidenceError& e) {
            thrown = true;
            EXPECT_EQ(e.code(), EvidenceErrorCode::SnapshotIncomplete);
        }
        EXPECT_TRUE(thrown);
    }
    // 声明集为空（无依赖的评估没有失效语义）。
    {
        const CurrentnessSources s
            = makeSources(headSnapshot, facts, {}, &archived);
        bool thrown = false;
        try {
            (void)computeCurrentness(result, target, s);
        } catch (const EvidenceError& e) {
            thrown = true;
            EXPECT_EQ(e.code(), EvidenceErrorCode::DeclarationInvalid);
        }
        EXPECT_TRUE(thrown);
    }
}

}  // namespace
