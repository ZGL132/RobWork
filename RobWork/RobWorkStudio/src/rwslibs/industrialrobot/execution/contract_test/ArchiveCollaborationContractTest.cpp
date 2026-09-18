/**
 * @file   ArchiveCollaborationContractTest.cpp
 * @brief  接纳编排与协作单元的跨单元契约用例组——与 project 真实存储的
 *         归档协作（EX-REG-4 切换场景/D-13 完整性可见性/D-14 内容冲突）
 *         与 evidence 校验器单点复用（acceptance 3——TASK-02/D-03 接纳
 *         唯一权威点）。
 *
 * 设计依据：
 *   - units/execution.md §3.4（`_contract_test` 分工＝跨单元契约面：与
 *     evidence 校验器、与 project 归档端口）、§9.3/§9.4/§9.5（迟到处置/
 *     S7 流程/归档协作——P-PR-4 冻结）、§12 EX-T04 行（verify 两测试目标）
 *   - ARCHITECTURE.md §4.5（第二段：归档位置取自登记记录、不写入当前
 *     HEAD/新修订；当前性独立判定）、§6.8（A7 存储上下文生命周期）、
 *     §11.2-7（迟到结果登记表契约）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 1（EX-REG-4 归档
 *     位置取登记记录——项目切换后合法迟到结果归档至 A 原 runDir、不进
 *     B 会话；EX-REG-8 内容冲突拒绝）/3（envelope 组装复用 evidence
 *     校验器——aggregateVerdict→make→validateCombination 单点复用不复制）
 *   - 跨单元契约：project D-13（manifest 在＝运行完整、查询只认完整）、
 *     D-14（摘要一致幂等/不一致冲突）、AT-10（跨项目迟到事件绝不写入
 *     新项目 results/）
 *
 * 替身边界声明（§11 同源纪律）：评估产出解码器/名称反解器为接口替身
 *   （只验证接纳编排契约）；归档写面全部使用**真实** ProjectStore
 *   （PRJ-T14 实现——跨单元契约的本体）；快照/证据/Profile 为契约形态
 *   数据（VerdictTest 同款最小实例），不构成业务算法正确性证明。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;
namespace ev = sdurws::ird::evidence;
namespace pd = sdurws::ird::project;
namespace fs = std::filesystem;

// =====================================================================
// 身份/取值辅助（单元测试套件同款确定性固定值——自持不共享）
// =====================================================================

const char* kHex32A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex32B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex32C = "cccccccccccccccccccccccccccccccc";
const char* kHex32D = "dddddddddddddddddddddddddddddddd";
const char* kHex64A = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
const char* kHex64B = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
const char* kHex64C = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

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

// =====================================================================
// 快照/Profile/判定素材（契约形态数据——VerdictTest 同构最小实例）
// =====================================================================

class AcceptAllClosureSource : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

ev::AnalysisSnapshot buildSnapshotFor(core::ProjectId project, core::BranchId branch,
                                      core::RevisionId revision)
{
    ev::SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 1);
    b.setPolicyRef(ev::PolicyRef{cid(kHex64A)});
    b.setNameMapRef(ev::NameMapRef{cid(kHex64B)});
    ev::ReproductionBlock r;
    r.productVersion = "industrialrobot-designer 0.1.0";
    r.evidenceContractVersion = "evidence-contract/1";
    r.codecVersions = {"snapshot-codec/1", "slice-codec/1"};
    b.setReproduction(r);
    ev::ObjectRefEntry obj;
    obj.objectId = oid(kHex32A);
    obj.contentVersion = cv(kHex64A);
    obj.objectTypeToken = "robot-design";
    obj.digest = obj.contentVersion.bytes;
    b.addObjectRef(obj);
    for (const ev::CaseEntry& c : std::vector<ev::CaseEntry>{
             {oid(kHex32A), "case-a", true, true},
             {oid(kHex32B), "case-b", true, true}}) {
        b.addCase(c);
    }
    AcceptAllClosureSource source;
    return b.build(source);
}

ev::RequiredEvidenceProfile standardKinProfile()
{
    ev::RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1.0.0";
    ev::EvidenceProfileItem reach;
    reach.itemId = "kin.reach-per-task-point";
    reach.itemClass = ev::EvidenceItemClass::Required;
    reach.description = "任务点可达性证据（最小承载实例）";
    ev::EvidenceProfileItem convergence;
    convergence.itemId = "kin.ik-convergence-per-point";
    convergence.itemClass = ev::EvidenceItemClass::Required;
    convergence.description = "逐任务点 IK 收敛证据（最小承载实例）";
    p.required = {reach, convergence};
    p.contentIdentity = ev::computeProfileContentIdentity(p);
    return p;
}

class ScriptedProducerRegistry final : public ev::IProducerRegistryView {
public:
    bool isRegistered(std::string_view key) const override
    {
        return key == "kin-batch-ik";
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        return key == "kin-batch-ik" && version == 7;
    }
};

class ScriptedProfileRegistry final : public ev::IProfileRegistryView {
public:
    explicit ScriptedProfileRegistry(ev::RequiredEvidenceProfile profile)
        : m_profile(std::move(profile))
    {
    }
    const ev::RequiredEvidenceProfile* findProfile(std::string_view profileId,
                                                   std::string_view version) const override
    {
        if (profileId == m_profile.profileId && version == m_profile.version) {
            return &m_profile;
        }
        return nullptr;
    }

private:
    ev::RequiredEvidenceProfile m_profile;
};

ev::CaseCoverageMatrix fullCoverage(const ev::AnalysisSnapshot& snapshot)
{
    ev::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const ev::CaseEntry& entry : snapshot.caseSet.entries) {
        ev::CaseCoverageEntry item;
        item.caseId = entry.caseId;
        item.status = ev::CaseExecutionStatus::Executed;
        matrix.entries.push_back(item);
    }
    return matrix;
}

ev::EvidenceItem satisfiedItem(std::string itemId)
{
    ev::EvidenceItem item;
    item.itemId = std::move(itemId);
    item.status = ev::EvidenceItemStatus::Satisfied;
    item.artifactDigest = cv(kHex64A).bytes;
    return item;
}

std::vector<std::uint8_t> domainPayloadBytes()
{
    return {0x01, 0x02, 0x03, 0x04};
}

/// 标准评估产出（两项必需 Satisfied＋域载荷——与执行单元套件同构；
/// EvaluationOutput 只携带条目集，清单绑定面由登记材料承载）。
ev::EvaluationOutput standardOutput(const ev::AnalysisSnapshot& snapshot,
                                    const ev::RequiredEvidenceProfile& profile)
{
    (void)snapshot;
    (void)profile;
    ev::EvaluationOutput out;
    out.evidence = {satisfiedItem("kin.reach-per-task-point"),
                    satisfiedItem("kin.ik-convergence-per-point")};
    ev::DomainPayload payload;
    payload.kindToken = "kin.batch-ik.v1";
    payload.canonicalBytes = domainPayloadBytes();
    core::ContentDigester d;
    d.update(payload.canonicalBytes.data(), payload.canonicalBytes.size());
    payload.digest.bytes = d.finalize();
    out.payload = std::move(payload);
    return out;
}

// =====================================================================
// 注入替身（Ports.hpp 接口——单元测试套件同款最小形态）
// =====================================================================

class CollectingSink final : public IExecutionDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;
    void report(const core::DiagnosticRecord& record) override { reports.push_back(record); }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }
};

class PresetDecoder final : public IEvaluationOutputDecoder {
public:
    void enqueue(ev::EvaluationOutput output) { m_script.push_back(std::move(output)); }
    std::optional<ev::EvaluationOutput> tryDecode(
        const std::vector<std::uint8_t>&) const override
    {
        if (m_script.empty()) {
            return std::nullopt;
        }
        std::optional<ev::EvaluationOutput> out = std::move(m_script.front());
        m_script.pop_front();
        return out;
    }

private:
    mutable std::deque<std::optional<ev::EvaluationOutput>> m_script;
};

class MapNameResolver final : public INameResolverAdapter {
public:
    std::optional<core::ObjectId> tryResolve(core::ContentIdentity,
                                             std::string_view) const override
    {
        return oid(kHex32A);  // 本套件不核对名称面（单元套件已钉）——恒成功
    }
};

/// 真实存储网关（P-PR-4 形态：execution 侧只持本接口——port 转发到
/// 存储上下文归档写面；本套件不注入故障——故障面归单元套件）。
class StoreGateway final : public IRunArchiveGateway {
public:
    explicit StoreGateway(pd::ProjectStore& store)
        : m_store(&store)
    {
    }
    pd::IResultArchivePort& port() const override { return m_store->archive(); }
    bool reacquireWriteAuthority() override { return false; }  // 本套件不覆盖 A7 重开

private:
    pd::ProjectStore* m_store;
};

// =====================================================================
// 夹具：两个真实项目（A＝登记归属；B＝切换后的当前项目）
// =====================================================================

class ArchiveCollaborationContractTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_archive_contract"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_archive_contract";
#endif
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);  // 失败保留现场
    }

    void SetUp() override
    {
        const int n = ++s_caseCounter;
        m_dirA = s_base / ("case" + std::to_string(n)) / "a.rwdesign";
        m_dirB = s_base / ("case" + std::to_string(n)) / "b.rwdesign";
        m_storeA = pd::ProjectStoreFactory::createNew(m_dirA, "项目A", nullptr, nullptr).store;
        m_storeB = pd::ProjectStoreFactory::createNew(m_dirB, "项目B", nullptr, nullptr).store;

        // 快照绑定项目 A（评估归属的锚定三元组——project/branch/revision）。
        m_branch = core::BranchId::generate();
        m_revision = core::RevisionId::generate();
        m_snapshot = std::make_shared<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_storeA->projectId(), m_branch, m_revision));
        m_profile = standardKinProfile();
        m_producers = std::make_shared<ScriptedProducerRegistry>();
        m_profiles = std::make_shared<ScriptedProfileRegistry>(m_profile);
        m_sink = std::make_unique<CollectingSink>();
        m_decoder = std::make_unique<PresetDecoder>();
    }

    /// 在 A 上登记一次正式运行（归档位置＝A 的登记 runDir；网关指向 A
    /// 写面——execution 持有 A 上下文的归档引用，A7）。
    core::TaskIdentity registerRunOnA()
    {
        core::TaskIdentity id;
        id.project = m_storeA->projectId();
        id.branch = m_branch;
        id.revision = m_revision;
        id.run = core::RunId::generate();
        id.attempt = core::AttemptId{1};

        RegistrationInput in;
        in.identity = id;
        in.evaluatorKey = "kin-batch-ik";
        in.contractVersion = 7;
        in.snapshotId = m_snapshot->snapshotId;
        in.sliceId = cid(kHex64C);
        in.inputBaselineId = cid(kHex64B);
        in.policyIdentity = cid(kHex64A);
        in.nameMapIdentity = cid(kHex64B);
        in.mode = core::EvaluationMode::Verified;
        in.runDir = m_storeA->canonicalPath() / "results" / id.run.toCanonical();
        in.runKind = "kin-batch-eval";
        in.allowedResultKinds = {ResultKind::FinalEnvelope, ResultKind::ResultBatch};
        in.currentState = core::TaskState::Running;

        auto materials = std::make_shared<RunEvaluationMaterials>();
        materials->snapshot = m_snapshot;
        materials->producers = m_producers;
        materials->profiles = m_profiles;
        materials->producedIn = ev::ProducerProcess::Worker;
        materials->readiness.valid = true;
        materials->snapshotGate.complete = true;
        materials->coverage = fullCoverage(*m_snapshot);
        materials->manifestProfile.profileId = m_profile.profileId;
        materials->manifestProfile.version = m_profile.version;
        materials->manifestProfile.contentIdentity = m_profile.contentIdentity;
        in.resources.evaluation = std::move(materials);
        m_gateway = std::make_shared<StoreGateway>(*m_storeA);
        in.resources.archive = m_gateway;

        m_registry.registerRun(TaskId::generate(), in);
        return id;
    }

    ResultAdmission makeAdmission()
    {
        ResultAdmission admission(m_registry, *m_sink);
        admission.setOutputDecoder(m_decoder.get());
        admission.setNameResolver(&m_resolver);
        return admission;
    }

    std::string readAll(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return {};
        }
        return std::string{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    }

    static fs::path s_base;
    static int s_caseCounter;

    fs::path m_dirA;
    fs::path m_dirB;
    std::unique_ptr<pd::ProjectStore> m_storeA;
    std::unique_ptr<pd::ProjectStore> m_storeB;
    core::BranchId m_branch;
    core::RevisionId m_revision;
    std::shared_ptr<const ev::AnalysisSnapshot> m_snapshot;
    ev::RequiredEvidenceProfile m_profile;
    std::shared_ptr<ScriptedProducerRegistry> m_producers;
    std::shared_ptr<ScriptedProfileRegistry> m_profiles;
    std::unique_ptr<CollectingSink> m_sink;
    std::unique_ptr<PresetDecoder> m_decoder;
    MapNameResolver m_resolver;
    std::shared_ptr<StoreGateway> m_gateway;
    RunRegistry m_registry;
};

fs::path ArchiveCollaborationContractTest::s_base;
int ArchiveCollaborationContractTest::s_caseCounter = 0;

// =====================================================================
// EX-REG-4：项目切换后合法迟到结果——归档至 A 原 runDir，不进 B
// =====================================================================

TEST_F(ArchiveCollaborationContractTest,
       LateResultArchivesToRegisteredRunDirAfterProjectSwitch_EX_REG_4)
{
    // 排布（§9.4 S7）：项目 A 运行中登记 r1 → 用户切换到项目 B（B 成为
    // 当前项目——B 存储上下文打开存活）→ r1 完成事件迟到到达主进程。
    const core::TaskIdentity id = registerRunOnA();

    // "切换"动作本身：打开 B（界面会话切到 B——admission 的输入面不含
    // B 的任何引用，结构性保证"不按当前会话判失配"）。
    ASSERT_TRUE(m_storeB->writable());

    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(standardOutput(*m_snapshot, m_profile));
    ArrivalEnvelope arrival;
    arrival.identity = id;
    arrival.snapshotId = m_snapshot->snapshotId;
    arrival.sliceId = cid(kHex64C);
    arrival.evaluatorKey = "kin-batch-ik";
    arrival.contractVersion = 7;
    arrival.policyIdentity = cid(kHex64A);
    arrival.nameMapIdentity = cid(kHex64B);
    arrival.kind = ResultKind::FinalEnvelope;
    arrival.payloadCanon = domainPayloadBytes();

    const AdmissionOutcome out = admission.admitResult(std::move(arrival));

    // 接纳→归档至**登记记录**的 A 原 runDir（A8：不重新推导、不写入当前
    // 会话项目）；卡行禁止项"不按当前会话丢弃合法迟到结果"的行为面。
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived);

    const fs::path aManifest = m_storeA->canonicalPath() / "results" / id.run.toCanonical()
                               / "manifest.json";
    EXPECT_TRUE(fs::exists(aManifest)) << "归档落在 A 原 runDir";
    EXPECT_FALSE(fs::exists(m_storeB->canonicalPath() / "results" / id.run.toCanonical()))
        << "绝不写入项目 B 的 results/（AT-10 反例面）";

    // D-13 跨单元半区：manifest 发布后 A 查询端口按原修订可见该运行
    // （"只认 manifest 在的 run"——归档绑定原修订，HEAD/切换无关）。
    const std::vector<pd::RunInfo> runs = m_storeA->query().listRuns(m_revision);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs.front().runId, id.run);
    EXPECT_EQ(runs.front().task, id) << "manifest 五元组＝登记原五元组（原修订归属）";
    EXPECT_EQ(runs.front().runKind, "kin-batch-eval");
    EXPECT_EQ(runs.front().evaluationKey, "kin-batch-ik");

    // A7 引用持有观测：execution 持有 A 归档引用期间，A 上下文保持存活
    // （未被切换动作关闭——关闭由 execution 终结路径驱动，§9.3）。
    EXPECT_FALSE(m_storeA->closed());
}

// =====================================================================
// EX-REG-8：批次内容冲突（D-14 经真实 project 端口的全链路）
// =====================================================================

TEST_F(ArchiveCollaborationContractTest, BatchContentConflictViaProjectPort_EX_REG_8)
{
    const core::TaskIdentity id = registerRunOnA();

    // 第一条批次（独立接纳实例 1——指纹表各自持有，模拟两个接收窗口）。
    {
        ResultAdmission first = makeAdmission();
        ArrivalEnvelope batch;
        batch.identity = id;
        batch.snapshotId = m_snapshot->snapshotId;
        batch.sliceId = cid(kHex64C);
        batch.evaluatorKey = "kin-batch-ik";
        batch.contractVersion = 7;
        batch.policyIdentity = cid(kHex64A);
        batch.nameMapIdentity = cid(kHex64B);
        batch.kind = ResultKind::ResultBatch;
        batch.payloadCanon = {0x11, 0x22, 0x33};
        const AdmissionOutcome out = first.admitResult(std::move(batch));
        EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    }

    const fs::path batchFile =
        m_storeA->canonicalPath() / "results" / id.run.toCanonical() / "result-batch.bin";
    ASSERT_TRUE(fs::exists(batchFile));
    const std::string bytesBefore = readAll(batchFile);
    ASSERT_FALSE(bytesBefore.empty());

    // 第二接收窗口重投递同 relPath 不同内容：接纳侧指纹表为空（窗口独
    // 立）→ 归档事务直达真实 project writeBatch → D-14 摘要比对冲突 →
    // 冲突拒绝＋既有文件不被覆盖（跨单元冲突机制的全链路验证）。
    ResultAdmission second = makeAdmission();
    ArrivalEnvelope conflicting;
    conflicting.identity = id;
    conflicting.snapshotId = m_snapshot->snapshotId;
    conflicting.sliceId = cid(kHex64C);
    conflicting.evaluatorKey = "kin-batch-ik";
    conflicting.contractVersion = 7;
    conflicting.policyIdentity = cid(kHex64A);
    conflicting.nameMapIdentity = cid(kHex64B);
    conflicting.kind = ResultKind::ResultBatch;
    conflicting.payloadCanon = {0x44, 0x55, 0x66};
    const AdmissionOutcome out = second.admitResult(std::move(conflicting));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::ContentConflict);
    EXPECT_NE(readAll(batchFile), std::string("\x44\x55\x66", 3))
        << "冲突内容不落盘";
    EXPECT_EQ(readAll(batchFile), bytesBefore) << "既有批次逐字节不变（不覆盖）";
    // 批次未 finalize → 查询不可见（D-13：无 manifest 即不完整）。
    EXPECT_TRUE(m_storeA->query().listRuns(m_revision).empty());
}

// =====================================================================
// acceptance 3：envelope 组装复用 evidence 校验器（单点复用不复制）
// =====================================================================

TEST_F(ArchiveCollaborationContractTest, EnvelopeAssemblyReusesEvidenceValidators)
{
    // 契约面：接纳编排产出的 envelope 必须**全字段等于**用 evidence 公共
    // API（aggregateVerdict→validateCombination→make）以同一素材直接组装
    // 的 envelope——execution 不存在第二份判定/校验逻辑（TASK-02/D-03：
    // 接纳唯一权威点；任何本地复制都会使本断言失配）。
    const core::TaskIdentity id = registerRunOnA();

    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(standardOutput(*m_snapshot, m_profile));

    ArrivalEnvelope arrival;
    arrival.identity = id;
    arrival.snapshotId = m_snapshot->snapshotId;
    arrival.sliceId = cid(kHex64C);
    arrival.evaluatorKey = "kin-batch-ik";
    arrival.contractVersion = 7;
    arrival.policyIdentity = cid(kHex64A);
    arrival.nameMapIdentity = cid(kHex64B);
    arrival.kind = ResultKind::FinalEnvelope;
    arrival.payloadCanon = domainPayloadBytes();
    const AdmissionOutcome out = admission.admitResult(std::move(arrival));
    ASSERT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    ASSERT_TRUE(out.envelope);

    // 对照组装（纯 evidence 公共 API——与接纳编排同一素材、同一校验链；
    // 清单绑定面同样由登记事实落位——与接纳编排的组装规则逐字一致）。
    ev::EvidenceManifest manifest;
    manifest.snapshotId = m_snapshot->snapshotId;
    manifest.sliceId = cid(kHex64C);
    manifest.profileId = m_profile.profileId;
    manifest.profileVersion = m_profile.version;
    manifest.profileContentIdentity = m_profile.contentIdentity;
    const ev::EvaluationOutput output = standardOutput(*m_snapshot, m_profile);
    manifest.items = output.evidence;

    ev::VerdictInput vi;
    vi.outcome = core::TaskOutcome::Completed;
    vi.mode = core::EvaluationMode::Verified;
    vi.readiness.valid = true;
    vi.snapshotGate.complete = true;
    vi.coverage = fullCoverage(*m_snapshot);
    vi.proof = output.proof;
    vi.evidence = manifest;
    vi.domain = output.verdictInputs;
    vi.searchRecord = output.searchRecord;
    const ev::VerdictResult vr =
        ev::aggregateVerdict(vi, *m_snapshot, *m_producers, *m_profiles);

    ev::ResultEnvelopeDraft draft;
    draft.task = id;
    draft.evaluationKey = "kin-batch-ik";
    draft.evaluatorContractVersion = 7;
    draft.mode = core::EvaluationMode::Verified;
    draft.snapshotId = m_snapshot->snapshotId;
    draft.sliceId = cid(kHex64C);
    draft.inputBaselineId = cid(kHex64B);
    for (const ev::CaseCoverageEntry& entry : fullCoverage(*m_snapshot).entries) {
        if (entry.status == ev::CaseExecutionStatus::Executed) {
            draft.caseScope.caseIds.push_back(entry.caseId);
        }
    }
    draft.profile.profileId = m_profile.profileId;
    draft.profile.version = m_profile.version;
    draft.profile.contentIdentity = m_profile.contentIdentity;
    draft.outcome = core::TaskOutcome::Completed;
    draft.engineeringStatus = vr.status;
    draft.evidence = manifest;
    draft.infeasibilityProof = output.proof;
    draft.searchRecord = vr.searchRecord;
    draft.missingItems = vr.missingItems;
    if (output.payload.has_value()) {
        draft.payload = ev::DomainPayloadDraft{output.payload->kindToken,
                                               output.payload->canonicalBytes};
    }
    draft.diagnostics = output.diagnostics;
    draft.diagnostics.insert(draft.diagnostics.end(), vr.diagnostics.begin(),
                             vr.diagnostics.end());
    draft.producer.producedIn = ev::ProducerProcess::Worker;
    draft.producer.productVersion = m_snapshot->reproduction.productVersion;

    ASSERT_TRUE(ev::validateCombination(draft, *m_snapshot, *m_producers).empty());
    const ev::ResultEnvelope expected = ev::ResultEnvelope::make(std::move(draft));

    EXPECT_EQ(*out.envelope, expected)
        << "接纳产物与 evidence 单点组装全字段一致（无本地复制判定/校验）";
    EXPECT_EQ(out.envelope->engineeringStatus, core::EngineeringStatus::Feasible);
    // 载荷摘要是 make 重算的完整性凭据（CR-02）——两侧一致。
    ASSERT_TRUE(out.envelope->payload.has_value());
    EXPECT_EQ(out.envelope->payload->digest, expected.payload->digest);
}

}  // namespace
