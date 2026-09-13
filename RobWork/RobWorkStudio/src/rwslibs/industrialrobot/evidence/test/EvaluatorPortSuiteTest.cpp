/**
 * @file   EvaluatorPortSuiteTest.cpp
 * @brief  评估器端口套件用例组（EV-T11 产物）——§11 验证矩阵中点名
 *         ScriptedEvaluator 替身的用例行（EV-VER-1~5/8、EV-VER-6 双例）
 *         经**端口路径**（注册表→create→evaluate→汇总）端到端驱动；
 *         EV-INV 失效矩阵的显示单位子例；EV-REG-3 替身边界的机检面。
 *
 * 设计依据：
 *   - units/evidence.md §11（测试设施：ScriptedEvaluator 按脚本返回预设
 *     证据/证明/搜索未果/取消抛出，注册于测试内 registry；EV-VER-1~8、
 *     EV-INV、EV-REG-3 行的输入/操作/预期/观测点）、§12 EV-T11 行
 *     （产物＝ScriptedEvaluator 及 EV-* 全部用例体）
 *   - §9.3 调用约定（IEngineeringEvaluator/IEvaluationContext——替身逐条
 *     兑现；取消查询/进度/对象读取三通道演练）、§9.4（注册路径——
 *     EvaluatorRegistry 真实注册期校验）、§6.4（aggregateVerdict 决策表）
 *   - 需求 EVI-01（表 2 五级优先级）、C5/C8（搜索未果≠不可行/碰撞作用
 *     域）、CON-05（失效矩阵）、KIN-12（显示单位纯显示投影——§5.3 行）、
 *     任务约束§八（替身不冒充真实证据——EV-REG-3）
 *   - 任务契约 tasks/foundation/EV-T11.json（≙WP-05-T11）acceptance 1～3
 *
 * 范围说明（与既有用例的分工——§12 依赖序）：EV-VER-1~8/EV-COV-1/4 的
 * **决策表正反例**已在 EV-T06（VerdictTest.cpp，直接装配 VerdictInput）
 * 落地并留痕；本文件补齐其"经评估器端口"的组合面——产出素材来自
 * ScriptedEvaluator 的 evaluate() 调用（§11 测试设施的字面要求），再经
 * 调用侧装配进 aggregateVerdict：验证评估器产出→汇总输入的承接链不丢
 * 素材、不变形（proof/searchRecord/verdictInputs/evidence 逐项透传）。
 * 取消抛出与对象读取通道（§9.3 上下文契约）亦在本文件演练。
 *
 * 装配模式说明：脚本身份绑定（证明的 snapshotId/sliceId 字段）需要先于
 * 夹具构造的快照/切片事实——本文件各用例先以确定性构造函数独立重建场景
 * 事实（固定标识/固定内容——EV-ID-1 确定性的同源口径），再以该事实构造
 * 脚本、构造夹具；夹具自身的重建结果与先行事实逐字节一致由
 * ASSERT_EQ(port.snapshot.snapshotId, …) 显式钉住（隐式依赖显性化）。
 *
 * ★ 替身边界声明（EV-REG-3）：
 *   本文件全部"证据/证明/搜索未果/违例"均为 ScriptedEvaluator 脚本产出
 *   的**契约形态数据**（testdoubles 命名空间内的测试设施），仅验证
 *   evidence 的评估器端口、汇总与构造边界契约；**不构成任何 IK/动力学/
 *   碰撞检测等业务算法正确性证明**，也不得被任何产品路径持久化为真实
 *   证据（替身只存活于测试进程内存）。全文声明见本目录 README.md；
 *   机检面见本文件 ScriptedDoubleBoundary 用例组。
 */

#include "EvidenceTestDoubles.hpp"  // §11 规范替身（ScriptedEvaluator/上下文）

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Currentness.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO（需求/AT 追溯登记）

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird::evidence;
namespace core = sdurws::ird::core;
namespace doubles = sdurws::ird::evidence::testdoubles;

// =====================================================================
// 身份/取值辅助（确定性固定值——与 VerdictTest 同款风格，自持不共享）
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

core::ProjectId fixedProject()
{
    return core::ProjectId::fromCanonical(std::string{"prj-"} + kHex32A);
}

core::BranchId fixedBranch()
{
    return core::BranchId::fromCanonical(std::string{"brn-"} + kHex32B);
}

core::RevisionId fixedRevision()
{
    return core::RevisionId::fromCanonical(std::string{"rev-"} + kHex32C);
}

/// 合法任务五元组（execution 分配形态——五字段全部 isValid）。
core::TaskIdentity validTask()
{
    core::TaskIdentity t;
    t.project = fixedProject();
    t.branch = fixedBranch();
    t.revision = fixedRevision();
    t.run = core::RunId::fromCanonical(std::string{"run-"} + kHex32D);
    t.attempt = core::AttemptId{1};
    return t;
}

/// 合法复现块（必填标量非空——builder 通过的最小形态）。
ReproductionBlock validReproduction()
{
    ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    return r;
}

/// 合法对象引用条目（digest 与 contentVersion 同源——builder 一致性闸门）。
ObjectRefEntry validObjectRef(core::ObjectId id, std::string typeToken)
{
    ObjectRefEntry e;
    e.objectId = id;
    e.contentVersion = cv(kHex64A);
    e.objectTypeToken = std::move(typeToken);
    e.digest = e.contentVersion.bytes;
    return e;
}

/// 标准必验工况集：A/B 双双 enabled∧mandatory（覆盖分母）、C enabled 非
/// 必验（O-14 字面对照——与 VerdictTest 同构）。
std::vector<CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true},
            {oid(kHex32B), "case-b", true, true},
            {oid(kHex32C), "case-c-optional", true, false}};
}

/// 修订闭包事实来源替身（EV-REG-3 声明见文件头）：对一切 (oid,cv) 回答
/// true——快照组装协议归 EV-T03 用例，非本文件被测面。
class AcceptAllClosureSource : public IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/// 组装标准冻结快照（端口场景事实——确定性：同调用恒同身份，EV-ID-1）。
AnalysisSnapshot buildStandardSnapshot()
{
    SnapshotBuilder b;
    b.setIdentity(fixedProject(), fixedBranch(), fixedRevision(), 5);
    b.setPolicyRef(PolicyRef{cid(kHex64A)});
    b.setNameMapRef(NameMapRef{cid(kHex64B)});
    b.setReproduction(validReproduction());
    b.addObjectRef(validObjectRef(oid(kHex32A), "robot-design"));
    for (const CaseEntry& c : standardCases()) {
        b.addCase(c);
    }
    b.addExternalResource({oid(kHex32D), ExternalResourceStatus::Solidified, cv(kHex64B)});
    AcceptAllClosureSource source;
    return b.build(source);
}

/// 标准 kin 域 Profile（两项必需＋一项建议——与 VerdictTest 同构；内容
/// 身份经 computeProfileContentIdentity 计算，注册表与清单共用同一值）。
RequiredEvidenceProfile standardKinProfile()
{
    RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1.0.0";
    EvidenceProfileItem reach;
    reach.itemId = "kin.reach-per-task-point";
    reach.itemClass = EvidenceItemClass::Required;
    reach.description = "任务点可达性证据（表 4 运动学行的最小承载实例）";
    reach.substitutableByInfeasibility = true;  // 成功产物类——③级豁免观测面
    EvidenceProfileItem convergence;
    convergence.itemId = "kin.ik-convergence-per-point";
    convergence.itemClass = EvidenceItemClass::Required;
    convergence.description = "逐任务点 IK 收敛证据（表 4 运动学行的最小承载实例）";
    EvidenceProfileItem hint;
    hint.itemId = "kin.cycle-time-hint";
    hint.itemClass = EvidenceItemClass::Suggested;
    hint.description = "节拍建议项（缺失不阻断）";
    p.required = {reach, convergence};
    p.suggested = {hint};
    p.contentIdentity = computeProfileContentIdentity(p);
    return p;
}

/// 满足态证据项（artifactDigest 非零——presence 纪律）。
EvidenceItem satisfiedItem(std::string itemId)
{
    EvidenceItem item;
    item.itemId = std::move(itemId);
    item.status = EvidenceItemStatus::Satisfied;
    item.artifactDigest = cv(kHex64C).bytes;
    return item;
}

/// 满足态证据的标准两项（Profile 两必需项——脚本产出的常用素材集）。
std::vector<EvidenceItem> standardSatisfiedItems()
{
    return {satisfiedItem("kin.reach-per-task-point"),
            satisfiedItem("kin.ik-convergence-per-point")};
}

/// 最小合法切片：单条 Configuration 条目（求解配置——进 sliceId 不进
/// inputBaselineId，D-04）；@p configHex64 为配置身份的十六进制体。
InputSlice buildMinimalSlice(const AnalysisSnapshot& snapshot, const char* configHex64)
{
    SliceBuilder b;
    b.setEvaluation("kin-batch-ik", 7);
    DependencyEntry entry;
    entry.key = "solve.ik-config";
    entry.kind = DependencyKind::Configuration;
    ConfigurationDependencyPayload payload;
    payload.configKindToken = "ik-solve";
    payload.canonicalBytes = {std::uint8_t(0x01), std::uint8_t(0x02)};
    payload.contentIdentity = cid(configHex64);
    entry.payload = payload;
    entry.applied = true;
    b.addEntry(entry);
    return b.build(snapshot);
}

/// 合法必经状态碰撞证明基底（绑定一致＋产生者已注册——③级正例形态；
/// @param omitBasis true 时抹去必经性依据——EV-VER-6 反例的"同款证明缺
/// mandatoryState"形态）。
DeterministicInfeasibilityProof mandatoryStateProof(const AnalysisSnapshot& snapshot,
                                                    core::ContentIdentity sliceId,
                                                    bool omitBasis = false)
{
    DeterministicInfeasibilityProof proof;
    proof.category = ProofCategory::MandatoryStateCollision;
    proof.claimToken = "kin.task-point-config-collision";
    proof.subject = oid(kHex32A);
    proof.mandatoryState.stateKind = "TaskPointConfig";
    proof.mandatoryState.nonSelectabilityBasis =
        omitBasis ? std::string{}
                  : std::string{"任务点构型是任务定义强制要求的末端状态，无替代选择（必经性依据）"};
    proof.mandatoryState.objectId = oid(kHex32A);
    proof.collisionPairs = {{oid(kHex32A), oid(kHex32B), true}};
    proof.preconditions = "需求任务定义 v3；策略启用碰撞检测（绑定输入身份）";
    proof.coverageClaim = kCoverageClaimMandatoryState;
    proof.snapshotId = snapshot.snapshotId;
    proof.sliceId = sliceId;
    proof.producer = "kin-batch-ik";
    proof.producerContractVersion = 7;
    return proof;
}

/// 全覆盖覆盖矩阵（全部必验工况 Executed——⑤级底座的覆盖面）。
CaseCoverageMatrix fullCoverage(const AnalysisSnapshot& snapshot)
{
    CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const CaseEntry& entry : snapshot.caseSet.entries) {
        CaseCoverageEntry item;
        item.caseId = entry.caseId;
        item.status = CaseExecutionStatus::Executed;
        item.runId = core::RunId::fromCanonical(std::string{"run-"} + kHex32A);
        item.resultSliceId = cid(kHex64C);
        matrix.entries.push_back(item);
    }
    return matrix;
}

// =====================================================================
// 端口夹具：真实注册表对＋脚本评估器实例＋宿主上下文替身
// =====================================================================

/**
 * @brief 端口场景夹具（§11"注册于测试内 registry"的承载——真实
 *        EvidenceProfileRegistry/EvaluatorRegistry，注册路径过 §9.4/§9.5
 *        全部注册期校验；评估器实例经 registry.create() 取得）。
 *
 * 成员声明序即构造序（Profile 注册表先于评估器注册表——§13 接入顺序；
 * EvaluatorRegistry 持前者 const 引用，构造注入）。
 */
struct PortFixture {
    AnalysisSnapshot snapshot;             ///< 冻结快照（builder 产出）
    RequiredEvidenceProfile profile;       ///< 标准 kin Profile
    EvidenceProfileRegistry profileRegistry;    ///< Profile 注册表（真实）
    EvaluatorRegistry evaluatorRegistry;        ///< 评估器注册表（真实）
    std::unique_ptr<IEngineeringEvaluator> evaluator; ///< registry.create() 产物
    doubles::ScriptedEvaluationContext context;       ///< 宿主上下文替身
    InputSlice slice;                      ///< 标准切片（config=kHex64C）

    /// @param script 脚本步骤序列（工坊模板——评估器实例按序回放；身份
    ///        绑定见本文件头"装配模式说明"——调用方以确定性重建事实构造）
    explicit PortFixture(std::vector<doubles::ScriptedStep> script)
        : snapshot(buildStandardSnapshot())
        , profile(standardKinProfile())
        , evaluatorRegistry(profileRegistry)
        , slice(buildMinimalSlice(snapshot, kHex64C))
    {
        // 接入顺序（§13）：Profile 先注册、评估器后注册（注册期验证解析
        // Profile；本 Profile 无条件项，快照事实键可传空表）。
        profileRegistry.registerProfile(profile);
        doubles::registerScriptedEvaluator(evaluatorRegistry, profileRegistry,
                                           std::move(script));
        evaluator = evaluatorRegistry.create("kin-batch-ik");
    }

    /// 脚本评估器的具型视图（回放进度/描述符断言用——工厂为本文件自持，
    /// dynamic_cast 必然命中；失败即测试装配缺陷）。
    doubles::ScriptedEvaluator& scripted()
    {
        auto* p = dynamic_cast<doubles::ScriptedEvaluator*>(evaluator.get());
        EXPECT_NE(p, nullptr) << "registry.create() 未产出 ScriptedEvaluator（装配缺陷）";
        return *p;
    }

    /// 清单装配（调用侧职责——评估器只产出素材，§9.3；清单身份三元组由
    /// 调用侧绑定快照/切片/Profile，items 取自脚本产出）。
    EvidenceManifest makeManifest(const InputSlice& forSlice,
                                  std::vector<EvidenceItem> items) const
    {
        EvidenceManifest m;
        m.snapshotId = snapshot.snapshotId;
        m.sliceId = forSlice.sliceId;
        m.profileId = profile.profileId;
        m.profileVersion = profile.version;
        m.profileContentIdentity = profile.contentIdentity;
        m.items = std::move(items);
        return m;
    }

    /// 评估请求装配（§9.3 EvaluationRequest——调用方〔execution/域〕组装面）。
    EvaluationRequest makeRequest(const InputSlice& forSlice,
                                  std::vector<CaseId> caseSubset = {}) const
    {
        EvaluationRequest r;
        r.task = validTask();
        r.mode = core::EvaluationMode::Verified;
        r.snapshot = snapshot;
        r.slice = forSlice;
        r.caseSubset = std::move(caseSubset);
        return r;
    }

    /// 汇总输入装配（评估产出→VerdictInput 的承接链——本文件被测核心：
    /// 素材逐项透传不丢不变形；门禁/覆盖由调用侧按其事实装配）。
    VerdictInput verdictInputFrom(const InputSlice& forSlice,
                                  const EvaluationOutput& out) const
    {
        VerdictInput input;
        input.outcome = core::TaskOutcome::Completed;
        input.mode = core::EvaluationMode::Verified;
        input.readiness.valid = true;
        input.snapshotGate.complete = true;
        input.coverage = fullCoverage(snapshot);
        input.evidence = makeManifest(forSlice, out.evidence);
        input.proof = out.proof;
        input.searchRecord = out.searchRecord;
        input.domain = out.verdictInputs;
        return input;
    }
};

// =====================================================================
// 断言辅助（与 VerdictTest 同款风格）
// =====================================================================

/// trace 六槽中取指定级次的记录（下标＝级次——枚举值即槽位）。
const VerdictLevelRecord& levelRecord(const VerdictResult& result, VerdictLevel level)
{
    return result.trace.records.at(static_cast<std::size_t>(level));
}

/// 诊断清单中是否含指定建议码。
bool hasDiagCode(const std::vector<core::DiagnosticRecord>& diagnostics,
                 std::string_view code)
{
    for (const auto& record : diagnostics) {
        if (record.code == code) {
            return true;
        }
    }
    return false;
}

/// 诊断清单中是否有记录的 cause 含指定文本（违例详情透传的逐项判据）。
bool hasDiagCauseContaining(const std::vector<core::DiagnosticRecord>& diagnostics,
                            std::string_view text)
{
    for (const auto& record : diagnostics) {
        if (record.cause.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// missingItems 中是否含指定 itemId（"缺失项全量列出"的逐项判据）。
bool hasMissingItem(const std::vector<MissingItem>& items, const std::string& itemId)
{
    for (const auto& item : items) {
        if (item.itemId == itemId) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 用例前奏：确定性重建场景事实并校验夹具一致性（见文件头"装配
 *        模式说明"——脚本身份绑定与夹具重建必须同源）。
 *
 * @param port [in] 已构造夹具（其快照/切片由同一构造函数重建）
 * @param snapshot [in] 用例先行重建的快照（脚本身份绑定的事实面）
 * @param slice [in] 用例先行重建的切片（同上）
 */
void expectFixtureAligned(const PortFixture& port, const AnalysisSnapshot& snapshot,
                          const InputSlice& slice)
{
    // 确定性重建的一致性（EV-ID-1 同源口径：同内容恒同身份——若失配，
    // 说明脚本身份绑定与夹具事实脱钩，后续绑定类断言全部失效）。
    ASSERT_EQ(port.snapshot.snapshotId, snapshot.snapshotId);
    ASSERT_EQ(port.slice.sliceId, slice.sliceId);
    ASSERT_EQ(port.slice.inputBaselineId, slice.inputBaselineId);
}

}  // namespace

// =====================================================================
// EV-VER-1 门禁优先（EVI-01 §8.1 表 2 C6——经端口：脚本证明不豁免门禁）
// =====================================================================

/** EV-VER-1（端口路径）：脚本评估器返回不可行证明＋调用侧装配快照身份
 *  缺失（无策略内容身份）→②级命中 DataInsufficient；证明未进入③。 */
TEST(EvaluatorPortVerdict, GateBlocksScriptedProof_EV_VER_1)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);

    // 场景事实先行（脚本身份绑定面——见文件头"装配模式说明"）。
    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    const InputSlice slice = buildMinimalSlice(snapshot, kHex64C);

    // 脚本：一步返回"满足态证据＋合法必经状态证明"（若进入③会判不可行
    // ——正是被②级拦截的行为）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.proof = mandatoryStateProof(snapshot, slice.sliceId);
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    expectFixtureAligned(port, snapshot, slice);

    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);

    // 端口承接面：请求到达评估器（切片身份/工况子集原样——调用方装配面）。
    ASSERT_EQ(port.scripted().receivedRequests().size(), 1u);
    EXPECT_EQ(port.scripted().receivedRequests()[0].slice.sliceId, port.slice.sliceId);
    EXPECT_TRUE(port.scripted().receivedRequests()[0].caseSubset.empty());

    // 汇总装配：快照身份门禁缺失（EV-VER-1 场景——无策略内容身份，
    // 调用方装配面；即使证明在场也不豁免，C6）。
    VerdictInput input = port.verdictInputFrom(port.slice, out);
    input.snapshotGate.complete = false;
    input.snapshotGate.missingFields = {"policyContentIdentity"};

    const VerdictResult result = aggregateVerdict(
        input, port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // ②级命中→DataInsufficient；missingItems 含快照身份项；证明未进入③。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::CommonEvidenceGate);
    EXPECT_TRUE(hasMissingItem(result.missingItems, "policyContentIdentity"));
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_FALSE(levelRecord(result, VerdictLevel::InfeasibilityProof).evaluated);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagSnapshotIncomplete));
}

// =====================================================================
// EV-VER-2 搜索未果 ≠ 不可行（C5/AT-03——经端口：双步回放＋双身份复评）
// =====================================================================

/** EV-VER-2（端口路径）：第一次评估脚本返回"多初值全发散"（无证明）
 *  →DataInsufficient（搜索记录透传）；扩大初值后第二次评估返回有效解
 *  →Feasible；两次 inputBaselineId 相等、sliceId 不等（配置变，D-04）。 */
TEST(EvaluatorPortVerdict, SearchExhaustedThenFeasibleViaPort_EV_VER_2)
{
    IRD_TEST_INFO("CON-05", {"AT-03"}, std::nullopt);

    // 脚本两步：①多初值全发散（无证明、证据未产出）；②扩大初值后有效解
    // （证据齐备）——同一实例按构造序回放（§11 替身行文的双输入形态）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep exhausted;
    exhausted.output.searchRecord = SearchExhaustedRecord{1000, 8, {}};
    script.push_back(std::move(exhausted));
    doubles::ScriptedStep replanned;
    replanned.output.evidence = standardSatisfiedItems();
    script.push_back(std::move(replanned));

    PortFixture port(std::move(script));

    // 复评场景的输入前提：配置身份 c→b（扩大初值＝改求解配置——新 sliceId、
    // 同 inputBaselineId，D-04 双层身份）。
    const InputSlice firstSlice = port.slice;
    const InputSlice secondSlice = buildMinimalSlice(port.snapshot, kHex64B);
    ASSERT_EQ(secondSlice.inputBaselineId, firstSlice.inputBaselineId);
    ASSERT_NE(secondSlice.sliceId, firstSlice.sliceId);

    // ---- 第一次评估（全发散）→DataInsufficient；记录透传 ----
    const EvaluationRequest firstRequest = port.makeRequest(firstSlice);
    const EvaluationOutput firstOut = port.evaluator->evaluate(firstRequest, port.context);
    ASSERT_TRUE(firstOut.searchRecord.has_value());
    const VerdictResult firstResult = aggregateVerdict(
        port.verdictInputFrom(firstSlice, firstOut),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    EXPECT_EQ(firstResult.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(firstResult.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(firstResult.trace.hitLevel, VerdictLevel::MissingEvidence);
    ASSERT_TRUE(firstResult.searchRecord.has_value());
    EXPECT_EQ(firstResult.searchRecord->searchBudgetUsed, 1000u);
    EXPECT_EQ(firstResult.searchRecord->initialGuessesTried, 8u);
    EXPECT_TRUE(hasDiagCode(firstResult.diagnostics, kDiagSearchExhausted));

    // ---- 第二次评估（有效解）→Feasible；无搜索未果输出 ----
    const EvaluationRequest secondRequest = port.makeRequest(secondSlice);
    const EvaluationOutput secondOut
        = port.evaluator->evaluate(secondRequest, port.context);
    EXPECT_FALSE(secondOut.searchRecord.has_value());
    const VerdictResult secondResult = aggregateVerdict(
        port.verdictInputFrom(secondSlice, secondOut),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    EXPECT_EQ(secondResult.status, core::EngineeringStatus::Feasible);
    EXPECT_FALSE(secondResult.searchRecord.has_value());
    EXPECT_EQ(secondResult.trace.hitLevel, VerdictLevel::EngineeringJudgement);

    // 双步回放与双身份观测（EV-VER-2 观测点：两次 inputBaselineId 相等、
    // sliceId 不等——脚本按构造序恰好消费两步、请求两份各就各位）。
    EXPECT_EQ(port.scripted().consumedSteps(), 2u);
    ASSERT_EQ(port.scripted().receivedRequests().size(), 2u);
    EXPECT_EQ(port.scripted().receivedRequests()[0].slice.sliceId, firstSlice.sliceId);
    EXPECT_EQ(port.scripted().receivedRequests()[1].slice.sliceId, secondSlice.sliceId);
}

// =====================================================================
// EV-VER-3 构型碰撞仅过滤（C8/AT-03——经端口：碰撞解不上升为任务级）
// =====================================================================

/** EV-VER-3（端口路径）：脚本评估器返回两组 IK 解（一组碰撞被过滤、
 *  一组有效）→Feasible；碰撞解仅入域侧 filteredSolutions（含碰撞原因），
 *  无证明产生；聚合输入不含 searchRecord（有有效解不产搜索未果记录）。 */
TEST(EvaluatorPortVerdict, FilteredSolutionDoesNotBlockFeasibleViaPort_EV_VER_3)
{
    IRD_TEST_INFO("CON-05", {"AT-03"}, std::nullopt);

    // 脚本：一步返回有效解证据；域侧对照数据记录被过滤的碰撞解（C8：
    // 构型碰撞＝该解被硬过滤，不进入任务级证明契约——有有效解时评估器
    // 不产出 searchRecord，§6.3 末"全部被过滤"才产出）。对照数据在入队
    // 前留本地副本（脚本 move 后原步不可再读——断言以同值副本承载）。
    SearchExhaustedRecord domainSide;
    domainSide.filteredSolutions = {{"ik-solution-1", SearchFilterReason::Collision}};

    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.domainSideFilterRecord = domainSide;
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);

    // 端口承接面：碰撞解仅存在于域侧记录（含 Collision 原因），未进入
    // 聚合输入（out.searchRecord 为空——"仅过滤"的端口级观测）。
    EXPECT_EQ(out.searchRecord, std::nullopt);
    ASSERT_EQ(domainSide.filteredSolutions.size(), 1u);
    EXPECT_EQ(domainSide.filteredSolutions[0].filterReason, SearchFilterReason::Collision);
    // 无证明产生（构型碰撞不进入证明契约——C8 作用域）。
    EXPECT_EQ(out.proof, std::nullopt);

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // 碰撞解被过滤不影响任务可行——⑤级正常判定。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(result.missingItems.empty());
}

// =====================================================================
// EV-VER-4 全部解被过滤（C8/AT-03——经端口：搜索未果口径）
// =====================================================================

/** EV-VER-4（端口路径）：全部解因碰撞过滤→DataInsufficient（搜索未果
 *  口径）；searchRecord.filteredSolutions 全为 Collision；不产生
 *  MandatoryStateCollision 证明。 */
TEST(EvaluatorPortVerdict, AllSolutionsFilteredIsDataInsufficientViaPort_EV_VER_4)
{
    IRD_TEST_INFO("CON-05", {"AT-03"}, std::nullopt);

    // 脚本：一步返回"全部碰撞过滤"（无有效解——证据未产出＋搜索未果记录）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    SearchExhaustedRecord exhausted;
    exhausted.searchBudgetUsed = 500;
    exhausted.initialGuessesTried = 4;
    exhausted.filteredSolutions = {{"ik-solution-1", SearchFilterReason::Collision},
                                   {"ik-solution-2", SearchFilterReason::Collision}};
    step.output.searchRecord = exhausted;
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);
    ASSERT_TRUE(out.searchRecord.has_value());
    // 无证明产生（"全部碰撞过滤≠任务级不可行"——C8）。
    EXPECT_EQ(out.proof, std::nullopt);

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // 搜索未果口径：DataInsufficient＋记录透传（filteredSolutions 全
    // Collision）；绝不输出不可行。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    ASSERT_TRUE(result.searchRecord.has_value());
    ASSERT_EQ(result.searchRecord->filteredSolutions.size(), 2u);
    for (const FilteredSolution& solution : result.searchRecord->filteredSolutions) {
        EXPECT_EQ(solution.filterReason, SearchFilterReason::Collision);
    }
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagSearchExhausted));
}

// =====================================================================
// EV-VER-5 路径碰撞重规划（C8/AT-06——经端口：路径级淘汰非任务级证明）
// =====================================================================

/** EV-VER-5（端口路径）：脚本评估器返回"初始候选路径碰撞（淘汰诊断）→
 *  重规划成功"的产出→Feasible；路径碰撞记录为淘汰证据（诊断轨），非
 *  任务级证明（out.proof 为空）。 */
TEST(EvaluatorPortVerdict, ReplannedPathKeepsFeasibleViaPort_EV_VER_5)
{
    IRD_TEST_INFO("CON-05", {"AT-06"}, std::nullopt);

    // 脚本：一步返回"重规划成功"的证据面（清单两项 Satisfied）＋候选路径
    // 淘汰记录（诊断轨——code 为测试域自持 token，码值权威归 diagnostics
    // StableCodeRegistry，本替身域仅为演练载体，不冒充产品登记码）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.diagnostics = {core::DiagnosticRecord::make(
        std::string{"EVI-TRJ-PATH-CANDIDATE-ELIMINATED"}, std::nullopt, std::nullopt,
        std::nullopt, "path-replan", "初始候选路径与夹具碰撞，该路径被淘汰并重规划",
        "查看重规划记录确认替换路径")};
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);

    // 端口承接面：淘汰记录在诊断轨（非证明——out.proof 为空）。
    EXPECT_EQ(out.proof, std::nullopt);
    ASSERT_EQ(out.diagnostics.size(), 1u);

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // 重规划成功→任务不受路径淘汰影响（Feasible；无任务级证明输出）。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(result.missingItems.empty());
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
}

// =====================================================================
// EV-VER-6 必经状态证明（C8/表 2③——经端口：字段级校验正/反例）
// =====================================================================

/** EV-VER-6 正例（端口路径）：脚本评估器返回 mandatoryState＋
 *  collisionPairs 齐备的碰撞证明→validateProof 通过→③命中
 *  EngineeringInfeasible。 */
TEST(EvaluatorPortVerdict, ValidMandatoryProofInfeasibleViaPort_EV_VER_6)
{
    IRD_TEST_INFO("CON-05", {"AT-03"}, std::nullopt);

    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    const InputSlice slice = buildMinimalSlice(snapshot, kHex64C);

    // 脚本：一步返回字段齐备的必经状态碰撞证明（绑定快照/切片/产生者）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.proof = mandatoryStateProof(snapshot, slice.sliceId);
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    expectFixtureAligned(port, snapshot, slice);

    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);
    ASSERT_TRUE(out.proof.has_value());

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // ③命中→EngineeringInfeasible（证明字段级校验通过方计入——D-09）。
    EXPECT_EQ(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::InfeasibilityProof);
    EXPECT_TRUE(result.missingItems.empty());
}

/** EV-VER-6 反例（端口路径）：缺 mandatoryState 的同款证明→validateProof
 *  字段级拒绝（必经性依据缺失）→Invalid→④DataInsufficient——证明字段级
 *  校验逐项观测（不凭存在性采信，D-09）。 */
TEST(EvaluatorPortVerdict, MandatoryProofMissingStateFallsToDataInsufficient_EV_VER_6)
{
    IRD_TEST_INFO("CON-05", {"AT-03"}, std::nullopt);

    const AnalysisSnapshot snapshot = buildStandardSnapshot();
    const InputSlice slice = buildMinimalSlice(snapshot, kHex64C);

    // 脚本：一步返回"同款证明但缺必经性依据"（mandatoryState 三要素缺一
    // ——validateProof 的 MandatoryStateBasisMissing 拒绝面）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.proof = mandatoryStateProof(snapshot, slice.sliceId,
                                            /*omitBasis=*/true);
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    expectFixtureAligned(port, snapshot, slice);

    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);
    ASSERT_TRUE(out.proof.has_value());

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // 证明无效→不输出不可行，落④DataInsufficient（无效证明＝Invalid 语义）。
    EXPECT_EQ(result.status, core::EngineeringStatus::DataInsufficient);
    EXPECT_NE(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::MissingEvidence);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagProofInvalid));
    // 字段级观测：缺失项清单指明证明对象（MissingItem.itemId＝claimToken
    // ——证明类缺失项的可定位稳定指称，Verdict.hpp MissingItem 注释）。
    EXPECT_TRUE(hasMissingItem(result.missingItems, "kin.task-point-config-collision"));
}

// =====================================================================
// EV-VER-8 Must/Should（REQ-06——经端口：verdictInputs 透传与定级）
// =====================================================================

/** EV-VER-8（端口路径）：脚本评估器返回 Must 违例→EngineeringInfeasible
 *  （⑤级）；verdictInputs 透传（违例详情出现在判定诊断 cause）。 */
TEST(EvaluatorPortVerdict, MustViolationInfeasibleViaPort_EV_VER_8)
{
    IRD_TEST_INFO("REQ-06", {}, std::nullopt);

    // 脚本：一步返回 Must 违例判定输入（域知识——哪条 Must 被违例是域
    // 语义，evidence 只汇总定级与透传，PA-1）。
    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.verdictInputs.mustViolations = {
        {"kin.payload-limit", "载荷超限 12%（Must——REQ-06 判定输入）"}};
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // ⑤级命中→EngineeringInfeasible；VerdictTrace 级次=⑤；透传观测：
    // 违例详情文本出现在判定诊断（domain.verdictInputs → diagnostics）。
    EXPECT_EQ(result.status, core::EngineeringStatus::EngineeringInfeasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagMustViolation));
    EXPECT_TRUE(hasDiagCauseContaining(result.diagnostics, "载荷超限 12%"));
}

/** EV-VER-8（端口路径）：脚本评估器返回 Should 违例→Feasible＋警告诊断
 *  （不阻断，REQ-06）。 */
TEST(EvaluatorPortVerdict, ShouldViolationFeasibleWithWarningViaPort_EV_VER_8)
{
    IRD_TEST_INFO("REQ-06", {}, std::nullopt);

    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.output.verdictInputs.shouldViolations = {
        {"kin.cycle-time-hint", "节拍超出建议值 5%（Should——不阻断）"}};
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    const EvaluationRequest request = port.makeRequest(port.slice);
    const EvaluationOutput out = port.evaluator->evaluate(request, port.context);

    const VerdictResult result = aggregateVerdict(
        port.verdictInputFrom(port.slice, out),
        port.snapshot, port.evaluatorRegistry, port.profileRegistry);

    // Should 违例→Feasible＋逐条警告诊断（不阻断）；透传同 Must 面。
    EXPECT_EQ(result.status, core::EngineeringStatus::Feasible);
    EXPECT_EQ(result.trace.hitLevel, VerdictLevel::EngineeringJudgement);
    EXPECT_TRUE(hasDiagCode(result.diagnostics, kDiagShouldViolation));
    EXPECT_TRUE(hasDiagCauseContaining(result.diagnostics, "节拍超出建议值 5%"));
}

// =====================================================================
// §9.3 上下文契约演练（取消抛出/进度/对象读取——ScriptedEvaluator 行为面）
// =====================================================================

/** 取消抛出（§9.3）：宿主置位取消→脚本"取消抛出"步抛 EvidenceError→
 *  evidence 不捕获域异常（D-15 进程内抛传）——无产出物化。 */
TEST(EvaluatorPortContext, CancelledEvaluationThrowsThroughPort)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);

    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep cancelStep;
    cancelStep.throwIfCancellationRequested = true;
    cancelStep.cancelCode = EvidenceErrorCode::EvidenceMissing;
    cancelStep.cancelDetail = "评估器观测到取消，选择抛出退出（域自选轨）";
    script.push_back(std::move(cancelStep));

    PortFixture port(std::move(script));
    port.context.setCancelRequested(true);   // 宿主已请求取消

    const EvaluationRequest request = port.makeRequest(port.slice);
    bool thrown = false;
    try {
        (void)port.evaluator->evaluate(request, port.context);
    } catch (const EvidenceError& e) {
        thrown = true;
        EXPECT_EQ(e.code(), EvidenceErrorCode::EvidenceMissing);
        EXPECT_NE(std::string(e.what()).find("取消"), std::string::npos);
    }
    ASSERT_TRUE(thrown) << "取消抛出步未抛出——§9.3 退出路径未被演练";
    // 脚本步已消费（行为分发发生在取消查询之后——回放进度一致）。
    EXPECT_EQ(port.scripted().consumedSteps(), 1u);
}

/** 对象读取与进度上报（§9.3 三通道）：评估器经 context.tryObjectBytes
 *  读取物化对象字节、上报进度——上下文替身逐项记录（worker 物化场景的
 *  契约面；表外对象＝nullopt"不可得"支路）。 */
TEST(EvaluatorPortContext, ObjectReadAndProgressChannels)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);

    std::vector<doubles::ScriptedStep> script;
    doubles::ScriptedStep step;
    step.output.evidence = standardSatisfiedItems();
    step.readObjectBytes = true;
    step.readObjectId = oid(kHex32A);
    step.readContentVersion = cv(kHex64A);
    script.push_back(std::move(step));

    PortFixture port(std::move(script));
    // 宿主替身预置物化对象字节（worker 场景——execution 物化后供给）。
    port.context.provideObject(oid(kHex32A), cv(kHex64A), {0x0A, 0x0B, 0x0C});

    const EvaluationRequest request = port.makeRequest(port.slice);
    (void)port.evaluator->evaluate(request, port.context);

    // 上下文通道观测：对象读取恰好一次（命中预置表）；进度上报一次。
    EXPECT_EQ(port.context.objectLookupCount(), 1);
    ASSERT_EQ(port.context.progressLog().size(), 1u);
    EXPECT_EQ(port.context.progressLog()[0].first, 50u);
    EXPECT_EQ(port.context.progressLog()[0].second, "scripted-solve");
}

// =====================================================================
// EV-INV 失效矩阵子例（CON-05/AT-05——显示单位切换→无任何 reason）
// =====================================================================

/**
 * @brief 依赖事实源替身（EV-REG-3 声明见文件头）：按（声明键→预置条目）
 *        表回答——当前性重建的解析面（与 CurrentnessTest 的 MapFactSource
 *        同构，本文件自持）。
 */
class MapFactSource : public ICurrentnessFactSource {
public:
    std::map<std::string, DependencyEntry> table; ///< 预置解析结果（键＝声明键）

    std::optional<DependencyEntry>
    tryCurrentEntry(const DependencyDeclaration& declaration) const override
    {
        const auto it = table.find(declaration.key);
        return it == table.end() ? std::nullopt : std::optional<DependencyEntry>(it->second);
    }
};

/** EV-INV 子例（§5.3 显示单位行 KIN-12：纯显示投影——不在任何切片条目）：
 *  显示单位切换不产生任何切片条目变化→重建目标切片身份不变→
 *  computeCurrentness 判 Current 且 **reasons 为空**（"显示单位切换→
 *  无任何 reason"的逐字观测）。 */
TEST(EvaluatorPortCurrentness, DisplayUnitSwitchProducesNoInvalidationReason_EV_INV)
{
    IRD_TEST_INFO("CON-05", {"AT-05", "AT-27"}, std::nullopt);

    // 本用例为当前性投影面（与评估器端口无关）——直接用确定性快照构造。
    const AnalysisSnapshot snapshot = buildStandardSnapshot();

    // 归档时刻：运动学切片只消费机器人对象（显示单位根本不进切片——
    // §5.3 显示单位行全 ✗ 的实现基础）。
    SliceBuilder b;
    b.setEvaluation("kin-batch-ik", 7);
    DependencyEntry robot;
    robot.key = "model.robot-design";
    robot.kind = DependencyKind::Object;
    ObjectDependencyPayload payload;
    payload.objectId = oid(kHex32A);
    payload.contentVersion = cv(kHex64A);
    payload.objectTypeToken = "robot-design";
    robot.payload = payload;
    robot.applied = true;
    b.addEntry(robot);
    const InputSlice archived = b.build(snapshot);

    // "显示单位切换"：单位制展示切换是纯显示投影（ARCH §7.7），不产生
    // 修订、不产生对象内容版本、不进任何切片条目——目标重建的声明解析
    // 结果与归档时刻逐字节相同（事实源返回同一条目）。
    MapFactSource facts;
    facts.table[robot.key] = robot;

    // 目标上下文（同项目/分支/评估键/契约版本——HEAD 修订推进不参与，
    // EV-CUR-1 已另测；本用例钉住"显示单位变化"这一无事实变化的形态）。
    CurrentnessTarget target;
    target.projectId = fixedProject();
    target.branchId = fixedBranch();
    target.evaluationKey = "kin-batch-ik";
    target.currentConfigIdentity = cid(kHex64C);

    CurrentnessSources sources;
    sources.targetSnapshot = &snapshot;
    sources.facts = &facts;
    sources.currentContractVersion = 7;
    sources.declarations = {[] {
        DependencyDeclaration d;
        d.key = "model.robot-design";
        d.kind = DependencyKind::Object;
        d.requiredness = DependencyRequiredness::Required;
        return d;
    }()};
    sources.resultSlice = &archived;

    ResultRef ref;
    ref.task = validTask();
    ref.evaluationKey = archived.evaluationKey;
    ref.evaluatorContractVersion = archived.evaluatorContractVersion;
    ref.sliceId = archived.sliceId;
    ref.inputBaselineId = archived.inputBaselineId;

    const CurrentnessResult out = computeCurrentness(ref, target, sources);

    // Current 且 reasons 为空——"无任何 reason 条目产生"（§5.3 显示单位
    // 行；sliceId 逐字节不变的同义观测：重建目标切片身份与归档一致）。
    ASSERT_TRUE(out.status.has_value());
    EXPECT_EQ(*out.status, CurrentnessStatus::Current);
    EXPECT_TRUE(out.reasons.empty()) << "显示单位切换不得产生失效原因条目";
    EXPECT_TRUE(out.diagnostics.empty());
}

// =====================================================================
// EV-REG-3 替身边界机检（任务约束§八——替身不冒充真实证据的机制面）
// =====================================================================

/**
 * @brief 源码树递归收集（BuildRedLineTest 同款——IRD_EVIDENCE_UNIT_ROOT
 *        注入的源码树内按扩展名过滤）。
 */
std::vector<std::filesystem::path> collectSourceFiles(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext == ".hpp" || ext == ".cpp") {
            files.push_back(entry.path());
        }
    }
    return files;
}

/// 文件全文读取（读失败＝环境错误显性失败）。
std::string readFileText(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << p.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/** EV-REG-3 机检①：ScriptedEvaluator 替身只存在于测试源码面——产品库
 *  源码（src/＋include/）零命中（替身数据不得进入产品形态，任务约束§八；
 *  与 test/README.md 的文字声明互为机检/文书两面）。 */
TEST(ScriptedDoubleBoundary, DoubleLivesOnlyInTestSources_EV_REG_3)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);
    const std::filesystem::path unitRoot{IRD_EVIDENCE_UNIT_ROOT};

    // 产品源码面（src＋include）扫描：替身类型/命名空间不得出现——替身
    // 若混入产品库，"真实证据"与"契约形态数据"的边界即被破坏。
    std::size_t hits = 0;
    for (const std::filesystem::path& dir :
         {unitRoot / "evidence" / "src", unitRoot / "evidence" / "include"}) {
        for (const auto& file : collectSourceFiles(dir)) {
            const std::string text = readFileText(file);
            if (text.find("ScriptedEvaluator") != std::string::npos
                || text.find("evidence::testdoubles") != std::string::npos) {
                ADD_FAILURE() << "产品源码出现替身符号: " << file.string();
                ++hits;
            }
        }
    }
    EXPECT_EQ(hits, 0u);

    // 反向自证：替身头确实在测试源码面（扫描不是空转——工具无效防护的
    // 显性排除）。
    const std::string doubleHeader =
        readFileText(unitRoot / "evidence" / "test" / "EvidenceTestDoubles.hpp");
    EXPECT_NE(doubleHeader.find("class ScriptedEvaluator final"), std::string::npos);
}

/** EV-REG-3 机检②：替身边界声明文书在案（EV-REG-3 观测点"测试文档显式
 *  声明：替身输出仅验证契约，不构成 IK/动力学等算法正确性证明"——
 *  test/README.md 含该声明文本；文档缺位即本用例失败）。 */
TEST(ScriptedDoubleBoundary, BoundaryDeclarationDocumentedInReadme_EV_REG_3)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);
    const std::filesystem::path readme =
        std::filesystem::path{IRD_EVIDENCE_UNIT_ROOT} / "evidence" / "test" / "README.md";
    const std::string text = readFileText(readme);

    // 关键声明句的机检锚点（README 的 EV-REG-3 节——三要素逐项在案）。
    EXPECT_NE(text.find("仅验证"), std::string::npos)
        << "README 缺少'替身输出仅验证契约'声明";
    EXPECT_NE(text.find("不构成"), std::string::npos)
        << "README 缺少'不构成算法正确性证明'声明";
    EXPECT_NE(text.find("EV-REG-3"), std::string::npos)
        << "README 缺少 EV-REG-3 追溯锚点";
}
