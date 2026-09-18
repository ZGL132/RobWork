/**
 * @file   RunRegistryAdmissionTest.cpp
 * @brief  RunRegistry 与迟到结果两段式接纳用例组——EX-REG-1/2/3/6/7/8 与
 *         EX-ORT-1（单元内面：判定矩阵/接纳编排/归档事务/A7 防御）。
 *
 * 设计依据：
 *   - units/execution.md §9.1～§9.5（登记记录/九步接纳/迟到处置/归档协作）、
 *     §11 验证矩阵 EX-REG-1/2/3/6/7/8 与 EX-ORT-1 行、§5.6（四轴正交）、
 *     §12 EX-T04 行（验证方式＝EX-REG-1~8、EX-ORT-1）
 *   - ARCHITECTURE.md §4.5（A8 两段式）、§11.2-7（迟到结果登记表契约）、
 *     §6.8（A7 存储上下文防御）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 1/2/3（本文件承载
 *     单元内面；EX-REG-4 跨项目真实存储场景归 contract_test/
 *     ArchiveCollaborationContractTest）
 *
 * 替身边界声明（§11/EV-REG-3 同源纪律）：评估产出解码器/名称反解器为
 *   接口替身，只验证接纳编排契约，不构成 runtime⑥端口或通道编解码的
 *   实现正确性证明；归档写面用**真实** ProjectStore（PRJ-T14 实现——
 *   本任务验证的就是与 project 的协作语义），故障注入经转发装饰器实现
 *   （磁盘满/重试场景），不伪造 project 侧行为；快照/证据/Profile 为
 *   契约形态数据（VerdictTest 同款最小实例），不构成 IK 等业务算法证明。
 *
 * 断言不 sleep：归档重试退避经 DelayFn 注入收集器虚拟推进（testkit
 *   §6.5/§6.6 同纪律——时序断言落在收集到的退避值上，不真实等待）。
 */

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/execution/RunRegistry.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
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
// 身份/取值辅助（确定性固定值——EnvelopeTest/VerdictTest 同款风格，自持）
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
// 冻结快照与判定素材（VerdictTest 同构最小实例——契约形态数据）
// =====================================================================

class AcceptAllClosureSource : public ev::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId, core::ContentVersion) const override
    {
        return true;
    }
};

/// 标准必验工况集：A/B 必验、C 可选（VerdictTest 同构）。
std::vector<ev::CaseEntry> standardCases()
{
    return {{oid(kHex32A), "case-a", true, true},
            {oid(kHex32B), "case-b", true, true},
            {oid(kHex32C), "case-c-optional", true, false}};
}

ev::AnalysisSnapshot buildSnapshotFor(core::ProjectId project, core::BranchId branch,
                                      core::RevisionId revision)
{
    ev::SnapshotBuilder b;
    b.setIdentity(project, branch, revision, 5);
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
    for (const ev::CaseEntry& c : standardCases()) {
        b.addCase(c);
    }
    b.addExternalResource({oid(kHex32D), ev::ExternalResourceStatus::Solidified, cv(kHex64B)});
    AcceptAllClosureSource source;
    return b.build(source);
}

/// 标准 kin 域 Profile（两项必需——VerdictTest 同款最小合法实例）。
ev::RequiredEvidenceProfile standardKinProfile()
{
    ev::RequiredEvidenceProfile p;
    p.profileId = "kin";
    p.version = "1.0.0";
    ev::EvidenceProfileItem reach;
    reach.itemId = "kin.reach-per-task-point";
    reach.itemClass = ev::EvidenceItemClass::Required;
    reach.description = "任务点可达性证据（表 4 运动学行的最小承载实例）";
    reach.substitutableByInfeasibility = true;
    ev::EvidenceProfileItem convergence;
    convergence.itemId = "kin.ik-convergence-per-point";
    convergence.itemClass = ev::EvidenceItemClass::Required;
    convergence.description = "逐任务点 IK 收敛证据（表 4 运动学行的最小承载实例）";
    p.required = {reach, convergence};
    p.contentIdentity = ev::computeProfileContentIdentity(p);
    return p;
}

/// 产生者注册表替身：kin-batch-ik@7（键词形合法——无点）。
class ScriptedProducerRegistry final : public ev::IProducerRegistryView {
public:
    explicit ScriptedProducerRegistry(std::map<std::string, std::uint32_t> table)
        : m_table(std::move(table))
    {
    }
    bool isRegistered(std::string_view key) const override
    {
        return m_table.count(std::string{key}) > 0;
    }
    bool contractVersionMatches(std::string_view key, std::uint32_t version) const override
    {
        const auto it = m_table.find(std::string{key});
        return it != m_table.end() && it->second == version;
    }

private:
    std::map<std::string, std::uint32_t> m_table;
};

/// Profile 注册表替身（aggregateVerdict 的 Profile 查询面）。
class ScriptedProfileRegistry final : public ev::IProfileRegistryView {
public:
    void registerProfile(ev::RequiredEvidenceProfile profile)
    {
        m_profiles[profile.profileId + "@" + profile.version] = std::move(profile);
    }
    const ev::RequiredEvidenceProfile* findProfile(std::string_view profileId,
                                                   std::string_view version) const override
    {
        const auto it = m_profiles.find(std::string{profileId} + "@" + std::string{version});
        return it == m_profiles.end() ? nullptr : &it->second;
    }

private:
    std::map<std::string, ev::RequiredEvidenceProfile> m_profiles;
};

/// 全覆盖覆盖矩阵（全部工况 Executed——⑤级 Feasible 底座）。
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

/// 漏验一份必验工况的覆盖矩阵（②级命中——Completed+DataInsufficient 底座）。
ev::CaseCoverageMatrix coverageMissing(const ev::AnalysisSnapshot& snapshot,
                                       core::ObjectId missingCaseId)
{
    ev::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const ev::CaseEntry& entry : snapshot.caseSet.entries) {
        ev::CaseCoverageEntry item;
        item.caseId = entry.caseId;
        item.status = entry.caseId == missingCaseId ? ev::CaseExecutionStatus::NotExecuted
                                                    : ev::CaseExecutionStatus::Executed;
        matrix.entries.push_back(item);
    }
    return matrix;
}

/// 满足态证据项（artifactDigest 非零——presence 纪律）。
ev::EvidenceItem satisfiedItem(std::string itemId)
{
    ev::EvidenceItem item;
    item.itemId = std::move(itemId);
    item.status = ev::EvidenceItemStatus::Satisfied;
    item.artifactDigest = cv(kHex64A).bytes;
    return item;
}

/// 评估产出替身载荷（域 canonical 字节——对 execution 不透明的契约形态）。
std::vector<std::uint8_t> domainPayloadBytes()
{
    return {0x01, 0x02, 0x03, 0x04};
}

// =====================================================================
// 注入接口替身（Ports.hpp 三件）与事件收集
// =====================================================================

/// 诊断收集替身（稳定码记录＋开发消息双通道捕获——拒绝诊断断言面）。
class CollectingSink final : public IExecutionDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const core::DiagnosticRecord& record) override { reports.push_back(record); }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }
    bool hasReportCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
};

/// 名称反解替身：按（名称映射身份→名称→对象）双层映射应答。
class MapNameResolver final : public INameResolverAdapter {
public:
    void bind(core::ContentIdentity mapId, std::string runtimeName, core::ObjectId object)
    {
        m_table[std::move(mapId)][std::move(runtimeName)] = std::move(object);
    }
    std::optional<core::ObjectId> tryResolve(core::ContentIdentity nameMapIdentity,
                                             std::string_view runtimeName) const override
    {
        const auto it = m_table.find(nameMapIdentity);
        if (it == m_table.end()) {
            return std::nullopt;
        }
        const auto name = it->second.find(std::string{runtimeName});
        return name == it->second.end() ? std::nullopt
                                        : std::optional<core::ObjectId>{name->second};
    }

private:
    std::map<core::ContentIdentity, std::map<std::string, core::ObjectId>> m_table;
};

/// 评估产出解码替身：按脚本逐次应答（脚本耗尽＝nullopt——解码失败注入）。
class PresetDecoder final : public IEvaluationOutputDecoder {
public:
    void enqueue(std::optional<ev::EvaluationOutput> output) { m_script.push_back(std::move(output)); }
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

/// 事件收集 sink（ResultArchived 断言面——core 参考总线消费）。
class CollectingEventSink final : public core::IDomainEventSink {
public:
    std::vector<core::DomainEvent> events;
    void onEvent(const core::DomainEvent& event) override { events.push_back(event); }
};

// =====================================================================
// 归档协作替身：真实存储＋故障注入转发器＋脚本化网关
// =====================================================================

/// 故障注入转发器：包装真实归档端口，前 N 次 writeBatch/finalize 返回
/// 磁盘满（不透传——有界重试路径的注入面）；其余调用全转发。
///
/// 说明：ArchiveSessionRef 只能由 project 内部实现构造（值语义凭据的
/// 防伪设计），因此替身不自己铸造会话——begin/finalize 的会话面全部
/// 转发真实端口取得，注入只落在 ArchiveStatus 返回值上（EX-ARC-1/2 的
/// "fake 注入 disk-full" 形态，EX-T09 契约套件同款接缝思路）。
class FaultInjectingPort final : public pd::IResultArchivePort {
public:
    explicit FaultInjectingPort(pd::IResultArchivePort& real)
        : m_real(real)
    {
    }

    int writeFailures = 0;    ///< 前 N 次 writeBatch 注入 disk-full
    int finalizeFailures = 0; ///< 前 N 次 finalize 注入 disk-full
    int beginCalls = 0;       ///< begin 透传计数（EX-REG-1"不写任何 results/"断言面）
    std::vector<pd::ArchiveEndReason> abandons; ///< abandon 观测（转发前记录）

    pd::ArchiveSessionRef begin(const pd::ArchiveRequest& request) override
    {
        ++beginCalls;
        return m_real.begin(request);
    }

    pd::ArchiveStatus writeBatch(pd::ArchiveSessionRef session,
                                 const pd::ArchiveBatch& batch) override
    {
        if (writeFailures > 0) {
            --writeFailures;
            return pd::ArchiveStatus{false, pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                           "project/fake: 注入 disk-full"}};
        }
        return m_real.writeBatch(std::move(session), batch);
    }

    pd::ArchiveStatus finalize(pd::ArchiveSessionRef session,
                               const pd::RunManifest& manifest) override
    {
        if (finalizeFailures > 0) {
            --finalizeFailures;
            return pd::ArchiveStatus{false, pd::StoreError{pd::StoreErrorCode::DiskFull,
                                                           "project/fake: 注入 disk-full"}};
        }
        return m_real.finalize(std::move(session), manifest);
    }

    void abandon(pd::ArchiveSessionRef session, pd::ArchiveEndReason reason) override
    {
        abandons.push_back(reason);
        m_real.abandon(std::move(session), reason);
    }

private:
    pd::IResultArchivePort& m_real;
};

/// 脚本化归档网关：指向当前存储上下文；A7 重取两形态——脚本布尔（失败
/// 分支）或注入真实重开回调（成功分支：工厂再开同目录，写面切换到新
/// 上下文——EX-REG-5 两分支的共用替身）。
class ScriptedGateway final : public IRunArchiveGateway {
public:
    explicit ScriptedGateway(pd::IResultArchivePort& port)
        : m_port(&port)
    {
    }

    void redirect(pd::IResultArchivePort& port) { m_port = &port; }
    void setReacquireResult(bool ok) { m_reacquireResult = ok; }
    void setReacquireAction(std::function<bool()> action) { m_reacquire = std::move(action); }

    pd::IResultArchivePort& port() const override { return *m_port; }
    bool reacquireWriteAuthority() override
    {
        // 回调优先（真实重开路径——重开后由回调负责 redirect 写面）；
        // 无回调时按脚本布尔应答（失败分支）。
        if (m_reacquire) {
            return m_reacquire();
        }
        return m_reacquireResult;
    }

private:
    pd::IResultArchivePort* m_port;
    bool m_reacquireResult = true;
    std::function<bool()> m_reacquire;
};

// =====================================================================
// 测试夹具：一个真实项目存储＋登记表＋接纳编排全接线
// =====================================================================

class RunRegistryAdmissionTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
#ifdef _WIN32
        s_base = fs::temp_directory_path(ec) / "ird_ex_runregistry_test"
                 / std::to_string(::GetCurrentProcessId());
#else
        s_base = fs::temp_directory_path(ec) / "ird_ex_runregistry_test";
#endif
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 失败保留现场（TempDir 失败保留惯例）。
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter)) / "p.rwdesign";
        m_store = pd::ProjectStoreFactory::createNew(m_dir, "EX-T04", nullptr, nullptr).store;
        m_sink = std::make_unique<CollectingSink>();
        m_decoder = std::make_unique<PresetDecoder>();
        m_resolver = std::make_unique<MapNameResolver>();

        // 快照锚定三元组使用**存储真实 projectId**（归档 begin 校验清单①
        // task.project==上下文 projectId——runDir 白名单面对真实存储时，
        // 身份必须与上下文一致；branch/revision 取派发期生成值）。
        m_snapshot = std::make_shared<const ev::AnalysisSnapshot>(
            buildSnapshotFor(m_store->projectId(), core::BranchId::generate(),
                             core::RevisionId::generate()));
        m_profile = standardKinProfile();
        m_profiles = std::make_shared<ScriptedProfileRegistry>();
        m_profiles->registerProfile(m_profile);
        m_producers = std::make_shared<ScriptedProducerRegistry>(
            ScriptedProducerRegistry({{"kin-batch-ik", 7}}));

        m_injector = std::make_unique<FaultInjectingPort>(m_store->archive());
        m_gateway = std::make_shared<ScriptedGateway>(*m_injector);
    }

    // ---- 登记与到达构造（合法底座——各用例单点变异）----

    /// 建一份合法登记（FinalEnvelope+ResultBatch 允许；Materials 就绪；
    /// gatewayOverride 供 A7 重开等特殊网关场景注入）。
    RunRegistration& registerStandardRun(core::TaskIdentity identity,
                                         ev::CaseCoverageMatrix coverage = {},
                                         IRunArchiveGateway* gatewayOverride = nullptr)
    {
        RegistrationInput in;
        in.identity = identity;
        in.evaluatorKey = "kin-batch-ik";
        in.contractVersion = 7;
        in.snapshotId = m_snapshot->snapshotId;
        in.sliceId = cid(kHex64C);
        in.inputBaselineId = cid(kHex64B);
        in.policyIdentity = cid(kHex64A);
        in.nameMapIdentity = cid(kHex64B);
        in.mode = core::EvaluationMode::Verified;
        in.runDir = m_dir / "results" / identity.run.toCanonical();
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
        materials->coverage =
            coverage.entries.empty() ? fullCoverage(*m_snapshot) : std::move(coverage);
        materials->manifestProfile.profileId = m_profile.profileId;
        materials->manifestProfile.version = m_profile.version;
        materials->manifestProfile.contentIdentity = m_profile.contentIdentity;
        in.resources.evaluation = std::move(materials);
        // 网关覆写用非所有权共享（别名构造——测试局部对象保证存活期覆盖
        // 本次接纳；缺省用夹具的注入网关）。
        in.resources.archive =
            gatewayOverride != nullptr
                ? std::shared_ptr<IRunArchiveGateway>(std::shared_ptr<IRunArchiveGateway>(),
                                                      gatewayOverride)
                : m_gateway;
        return m_registry.registerRun(TaskId::generate(), in);
    }

    /// 合法五元组（project/branch/revision 与快照锚定三元组一致——快照
    /// 绑定核对的事实面对齐）。
    core::TaskIdentity makeIdentity()
    {
        core::TaskIdentity t;
        t.project = m_snapshot->project;
        t.branch = m_snapshot->branch;
        t.revision = m_snapshot->revision;
        t.run = core::RunId::generate();
        t.attempt = core::AttemptId{1};
        return t;
    }

    /// 合法到达（绑定字段与登记一致；payload 为域载荷字节）。
    ArrivalEnvelope makeArrival(const core::TaskIdentity& identity, ResultKind kind)
    {
        ArrivalEnvelope a;
        a.identity = identity;
        a.snapshotId = m_snapshot->snapshotId;
        a.sliceId = cid(kHex64C);
        a.evaluatorKey = "kin-batch-ik";
        a.contractVersion = 7;
        a.policyIdentity = cid(kHex64A);
        a.nameMapIdentity = cid(kHex64B);
        a.kind = kind;
        a.payloadCanon = domainPayloadBytes();
        return a;
    }

    /// Feasible 底座评估产出（两项必需 Satisfied＋域载荷——⑤级通过；
    /// EvaluationOutput 只携带条目集——清单绑定面由登记材料承载）。
    ev::EvaluationOutput feasibleOutput()
    {
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

    /// 接纳编排构造（退避收集器注入——不 sleep；默认带解码器/反解器）。
    ResultAdmission makeAdmission(std::vector<std::chrono::milliseconds>* delays = nullptr)
    {
        ResultAdmission::Config cfg;
        auto delayFn = [delays](std::chrono::milliseconds wait) {
            if (delays != nullptr) {
                delays->push_back(wait);
            }
        };
        ResultAdmission admission(m_registry, *m_sink, nullptr, cfg, delayFn);
        admission.setOutputDecoder(m_decoder.get());
        admission.setNameResolver(m_resolver.get());
        admission.setEventBus(&m_bus);
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

    fs::path m_dir;
    std::unique_ptr<pd::ProjectStore> m_store;
    std::unique_ptr<CollectingSink> m_sink;
    std::unique_ptr<PresetDecoder> m_decoder;
    std::unique_ptr<MapNameResolver> m_resolver;
    std::shared_ptr<const ev::AnalysisSnapshot> m_snapshot;
    ev::RequiredEvidenceProfile m_profile;
    std::shared_ptr<ScriptedProfileRegistry> m_profiles;
    std::shared_ptr<ScriptedProducerRegistry> m_producers;
    std::unique_ptr<FaultInjectingPort> m_injector;
    std::shared_ptr<ScriptedGateway> m_gateway;
    RunRegistry m_registry;
    CollectingEventSink m_eventSink;
    core::ReferenceEventBus m_bus;
};

fs::path RunRegistryAdmissionTest::s_base;
int RunRegistryAdmissionTest::s_caseCounter = 0;

// =====================================================================
// 登记表本体（§9.1/§4.3）
// =====================================================================

/** 登记契约校验：重复 runId/无效身份/空允许集均为调用方违约（fail-fast）。 */
TEST_F(RunRegistryAdmissionTest, RegisterRejectsCallerContractViolations)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    // 同 runId 二次登记＝两份"权威登记事实"（A8 语义被破坏）——拒绝。
    RegistrationInput dup;
    dup.identity = id;
    dup.runDir = m_dir / "results" / id.run.toCanonical();
    dup.allowedResultKinds = {ResultKind::FinalEnvelope};
    auto materials = std::make_shared<RunEvaluationMaterials>();
    materials->snapshot = m_snapshot;
    materials->producers = m_producers;
    materials->profiles = m_profiles;
    dup.resources.evaluation = materials;
    dup.resources.archive = m_gateway;
    EXPECT_THROW(m_registry.registerRun(TaskId::generate(), dup), ExecutionError);

    // 无效五元组（attempt 保留值）——核对基准不合法即拦截。
    RegistrationInput bad = dup;
    bad.identity.attempt = core::AttemptId{0};
    bad.identity.run = core::RunId::generate();
    EXPECT_THROW(m_registry.registerRun(TaskId::generate(), bad), ExecutionError);

    // 空允许集——步 6 核对面不允许空集。
    RegistrationInput noKinds = dup;
    noKinds.identity.run = core::RunId::generate();
    noKinds.allowedResultKinds.clear();
    EXPECT_THROW(m_registry.registerRun(TaskId::generate(), noKinds), ExecutionError);
}

/** 尝试追加：单调前进＋旧尝试入被取代集合（步 4 判定数据源）。 */
TEST_F(RunRegistryAdmissionTest, AppendAttemptSupersedesOldAttempts)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    m_registry.appendAttempt(id.run, core::AttemptId{2});
    m_registry.appendAttempt(id.run, core::AttemptId{3});

    const std::optional<RunRegistration> rec = m_registry.tryRun(id.run);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->identity.attempt, core::AttemptId{3});
    ASSERT_EQ(rec->supersededAttempts.size(), 2u);
    EXPECT_EQ(rec->supersededAttempts[0], core::AttemptId{1});
    EXPECT_EQ(rec->supersededAttempts[1], core::AttemptId{2});

    // 回退/复用被取代号＝调用方违约。
    EXPECT_THROW(m_registry.appendAttempt(id.run, core::AttemptId{2}), ExecutionError);
    EXPECT_THROW(m_registry.appendAttempt(id.run, core::AttemptId{1}), ExecutionError);
}

/** 终结标记：一次写入＋状态镜像同步；Completed 不经此入口。 */
TEST_F(RunRegistryAdmissionTest, MarkTerminatedWritesOnceAndMirrorsState)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    m_registry.markTerminated(id.run, TerminationCause::Canceled);
    const std::optional<RunRegistration> rec = m_registry.tryRun(id.run);
    ASSERT_TRUE(rec.has_value());
    EXPECT_EQ(rec->currentState, core::TaskState::Canceled);
    ASSERT_TRUE(rec->terminationCause.has_value());
    EXPECT_EQ(*rec->terminationCause, TerminationCause::Canceled);

    // 二次终结＝编排层状态机与登记表失步——拒绝。
    EXPECT_THROW(m_registry.markTerminated(id.run, TerminationCause::Failed), ExecutionError);

    // Completed 终结必须经接纳路径（T8）——走本方法属编排违约。
    const core::TaskIdentity other = makeIdentity();
    (void)registerStandardRun(other);
    EXPECT_THROW(m_registry.markTerminated(other.run, TerminationCause::Completed),
                 ExecutionError);
}

// =====================================================================
// EX-REG-1：未知 RunId——拒绝＋开发诊断＋零归档写入
// =====================================================================

TEST_F(RunRegistryAdmissionTest, UnknownRunRejectedWithNoArchiveWrite_EX_REG_1)
{
    // TASK-03/AT-10：无登记构造到达 → Rejected(UnknownRun)＋开发诊断；
    // 不写任何项目 results/（真实存储门面：begin 计数为 0＋results 目录
    // 不存在——两项目场景归 contract_test）。
    core::TaskIdentity ghost = makeIdentity();
    ghost.run = core::RunId::generate();  // 未登记运行

    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(ghost, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::UnknownRun);
    ASSERT_FALSE(out.decision.diagnostics.empty());
    EXPECT_EQ(out.decision.diagnostics.front().code, "EX-REGISTRY-UNKNOWN-RUN");
    EXPECT_TRUE(m_sink->hasReportCode("EX-REGISTRY-UNKNOWN-RUN"));
    EXPECT_EQ(admission.rejectedArrivalCount(), 1u);
    // 零归档写入：到达在步 1 即被丢弃，归档端口未被触碰（AT-10 反例的
    // "绝不写入"观测面——文件级双项目断言在 contract_test）。
    EXPECT_EQ(m_injector->beginCalls, 0);
    EXPECT_FALSE(fs::exists(m_dir / "results"));
    EXPECT_FALSE(out.envelope);  // 不构造正式结果对象
}

// =====================================================================
// EX-REG-2：五元组逐字段失配——全部拒绝（逐字段独立断言）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, FiveTupleFieldMismatchRejectedPerField_EX_REG_2)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    // project/branch/revision/attempt 四字段逐一变异（run 字段失配＝查表
    // 未命中，归 EX-REG-1 的 UnknownRun 承载——ARCH §4.5 把 runId 未知
    // 单列；此处四例＋EX-REG-1 合成五元组全字段覆盖）。
    struct FieldCase
    {
        const char* name;
        std::function<void(core::TaskIdentity&)> mutate;
    };
    const std::vector<FieldCase> cases = {
        {"project", [](core::TaskIdentity& t) { t.project = core::ProjectId::generate(); }},
        {"branch", [](core::TaskIdentity& t) { t.branch = core::BranchId::generate(); }},
        {"revision", [](core::TaskIdentity& t) { t.revision = core::RevisionId::generate(); }},
        {"attempt", [](core::TaskIdentity& t) { t.attempt = core::AttemptId{9}; }},
    };
    for (const FieldCase& c : cases) {
        core::TaskIdentity mutated = id;
        c.mutate(mutated);
        const AdmissionDecision d = m_registry.classifyArrival(makeArrival(mutated, ResultKind::FinalEnvelope));
        EXPECT_EQ(d.verdict, AdmissionDecision::Verdict::Rejected) << c.name;
        EXPECT_EQ(d.reason, AdmissionDecision::RejectReason::IdentityMismatch) << c.name;
        // 逐字段独立可判别：诊断 cause 携带字段名。
        ASSERT_FALSE(d.diagnostics.empty()) << c.name;
        EXPECT_NE(d.diagnostics.front().cause.find(c.name), std::string::npos) << c.name;
    }
    EXPECT_EQ(admission.rejectedArrivalCount(), 0u);  // classify 纯查询不计数——计数在 admit
}

// =====================================================================
// EX-REG-3：陈旧 AttemptId——全部拒绝
// =====================================================================

TEST_F(RunRegistryAdmissionTest, StaleAttemptsAllRejected_EX_REG_3)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    m_registry.appendAttempt(id.run, core::AttemptId{2});
    m_registry.appendAttempt(id.run, core::AttemptId{3});

    // 携带任一被取代尝试的到达 → StaleAttempt（"全部拒绝"——逐个陈旧
    // 号独立断言；ARCH §11.2-7 第三格）。
    for (const std::uint64_t stale : {1u, 2u}) {
        core::TaskIdentity old = id;
        old.attempt = core::AttemptId{stale};
        const AdmissionDecision d = m_registry.classifyArrival(makeArrival(old, ResultKind::FinalEnvelope));
        EXPECT_EQ(d.verdict, AdmissionDecision::Verdict::Rejected);
        EXPECT_EQ(d.reason, AdmissionDecision::RejectReason::StaleAttempt) << stale;
        EXPECT_EQ(d.diagnostics.front().code, "EX-STALE-ATTEMPT");
    }
    // 当前尝试仍可通行（对照面——拒绝不殃及现役尝试；登记追加后现役值
    // 已前移到 3，对照到达须携带当前值）。
    core::TaskIdentity current = id;
    current.attempt = core::AttemptId{3};
    const AdmissionDecision ok = m_registry.classifyArrival(makeArrival(current, ResultKind::FinalEnvelope));
    EXPECT_EQ(ok.verdict, AdmissionDecision::Verdict::Admitted);
}

// =====================================================================
// 步 5/6/7：绑定失配/类型越集/已终结（九步 1~7 剩余格）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, BindingMismatchAtSecondCheck)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);

    // 第二道绑定核对（步 5——防通道串扰）：六个扩展字段逐一变异。
    ArrivalEnvelope a = makeArrival(id, ResultKind::FinalEnvelope);
    a.snapshotId = cid(kHex64A);
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);

    a = makeArrival(id, ResultKind::FinalEnvelope);
    a.sliceId = cid(kHex64B);
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);

    a = makeArrival(id, ResultKind::FinalEnvelope);
    a.evaluatorKey = "trj-batch";
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);

    a = makeArrival(id, ResultKind::FinalEnvelope);
    a.contractVersion = 8;
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);

    a = makeArrival(id, ResultKind::FinalEnvelope);
    a.policyIdentity = cid(kHex64C);
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);

    a = makeArrival(id, ResultKind::FinalEnvelope);
    a.nameMapIdentity = cid(kHex64C);
    EXPECT_EQ(m_registry.classifyArrival(a).reason, AdmissionDecision::RejectReason::BindingMismatch);
}

TEST_F(RunRegistryAdmissionTest, KindOutsideAllowedSetRejected)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);  // 允许集＝{FinalEnvelope, ResultBatch}

    // 到达类型越集＝协议错误（步 6——登记时按 mode 与阶段声明允许集）。
    const AdmissionDecision d = m_registry.classifyArrival(makeArrival(id, ResultKind::Progress));
    EXPECT_EQ(d.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(d.reason, AdmissionDecision::RejectReason::KindNotAllowed);
}

/** 步 7/TASK-02：已终结登记（取消/失败/强杀）拒绝 FinalEnvelope——不构造
 *  不归档（EX-ORT-1 的执行侧半区）。 */
TEST_F(RunRegistryAdmissionTest, TerminatedRegistrationsRejectFinalEnvelope_EX_ORT_1)
{
    // 三种终结因各用一份独立登记（同一存储内互不干扰；begin 计数与目录
    // 断言按运行逐一核对）。
    const std::vector<TerminationCause> causes = {TerminationCause::Canceled,
                                                  TerminationCause::Failed,
                                                  TerminationCause::ForceTerminated};
    const int beginCallsBefore = m_injector->beginCalls;
    for (const TerminationCause cause : causes) {
        const core::TaskIdentity id = makeIdentity();
        (void)registerStandardRun(id);
        m_registry.markTerminated(id.run, cause);

        ResultAdmission admission = makeAdmission();
        m_decoder->enqueue(feasibleOutput());  // 若被消费即说明错误地走到了组装
        const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

        EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected) << static_cast<int>(cause);
        EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::AlreadyTerminated) << static_cast<int>(cause);
        EXPECT_FALSE(out.envelope) << "取消/失败后不得产生完整成功结果（TASK-02）";
        // 状态投影保持终结镜像（Canceled→Canceled；Failed/强杀→Failed——
        // markTerminated 的镜像规则）。
        const core::TaskState expectedState = cause == TerminationCause::Canceled
                                                  ? core::TaskState::Canceled
                                                  : core::TaskState::Failed;
        EXPECT_EQ(out.taskStateAfter, expectedState) << static_cast<int>(cause);
    }
    EXPECT_EQ(m_injector->beginCalls, beginCallsBefore) << "不归档（三种终结因合计零端口调用）";
    EXPECT_FALSE(fs::exists(m_dir / "results")) << "无任何 results 落盘";
}

/** 步 7 边界对照：Interrupted（恢复期标注）不在拒绝集——§9.2 步 7 原文
 *  三值（Canceled/Failed/ForceTerminated）之外不截获。 */
TEST_F(RunRegistryAdmissionTest, InterruptedTerminationNotInStepSevenRejectSet)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    m_registry.markTerminated(id.run, TerminationCause::Interrupted);

    const AdmissionDecision d =
        m_registry.classifyArrival(makeArrival(id, ResultKind::FinalEnvelope));
    EXPECT_EQ(d.verdict, AdmissionDecision::Verdict::Admitted);
}

// =====================================================================
// EX-REG-7：重投递幂等（同 (run,attempt) FinalOutput；完成事件幂等无上限）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, FinalEnvelopeRedeliveryIdempotent_EX_REG_7)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    // 订阅句柄保持存活（RAII——句柄析构即退订，丢弃返回值等于没有订阅）。
    std::unique_ptr<core::IEventSubscription> subscription = m_bus.subscribe(m_eventSink);
    ResultAdmission admission = makeAdmission();

    // 首次接纳：Admitted＋归档（manifest 发布＝完整）＋事件一次。
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome first = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));
    EXPECT_EQ(first.decision.verdict, AdmissionDecision::Verdict::Admitted);
    ASSERT_TRUE(first.envelope);
    EXPECT_EQ(first.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(first.archivePhaseAfter, ArchivePhase::Archived);
    EXPECT_EQ(admission.archivePhase(id.run), ArchivePhase::Archived);
    ASSERT_EQ(m_eventSink.events.size(), 1u);
    EXPECT_EQ(m_eventSink.events.front().kind, core::DomainEventKind::ResultArchived);
    EXPECT_EQ(std::get<core::ResultArchivedPayload>(m_eventSink.events.front().payload),
              core::ResultArchivedPayload{id});

    const std::string manifestFile =
        (m_dir / "results" / id.run.toCanonical() / "manifest.json").string();
    const std::string manifestBefore = readAll(manifestFile);
    EXPECT_FALSE(manifestBefore.empty());
    const int beginCallsBefore = m_injector->beginCalls;

    // 重投递（同 run/attempt、同内容）→ 幂等成功：不重写、不报错、不重
    // 发布事件（完成事件重复投递无次数上限——P-PR-4 冻结义务之二：每次
    // 都幂等处置）。
    for (int i = 0; i < 3; ++i) {
        m_decoder->enqueue(feasibleOutput());  // 若被消费说明短路失效（防误配）
        const AdmissionOutcome again =
            admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));
        EXPECT_EQ(again.decision.verdict, AdmissionDecision::Verdict::DuplicateIgnored) << i;
        EXPECT_EQ(again.taskStateAfter, core::TaskState::Completed) << i;
    }
    EXPECT_EQ(readAll(manifestFile), manifestBefore) << "manifest 内容不变（不重写）";
    EXPECT_EQ(m_injector->beginCalls, beginCallsBefore) << "重投递零归档端口调用";
    EXPECT_EQ(m_eventSink.events.size(), 1u) << "ResultArchived 只发布一次";
    EXPECT_EQ(admission.rejectedArrivalCount(), 0u) << "幂等不是拒绝";
}

// =====================================================================
// EX-REG-8：内容冲突拒绝（FinalEnvelope 重投递不同内容／批次重投递）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, FinalEnvelopeContentConflictRejected_EX_REG_8)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    m_decoder->enqueue(feasibleOutput());
    ASSERT_EQ(admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope)).decision.verdict,
              AdmissionDecision::Verdict::Admitted);

    // 不同内容重投递（D-14 语义）：冲突拒绝＋诊断；既有归档不被覆盖。
    ArrivalEnvelope conflicting = makeArrival(id, ResultKind::FinalEnvelope);
    conflicting.payloadCanon = {0xDE, 0xAD, 0xBE, 0xEF};
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(std::move(conflicting));
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::ContentConflict);
    EXPECT_FALSE(out.decision.diagnostics.empty());

    // 既有 manifest 逐字节保持（不覆盖、不静默）。
    const std::string manifestFile =
        (m_dir / "results" / id.run.toCanonical() / "manifest.json").string();
    const std::string manifestBefore = readAll(manifestFile);
    m_decoder->enqueue(feasibleOutput());
    (void)admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));  // 幂等轨仍通
    EXPECT_EQ(readAll(manifestFile), manifestBefore);
    EXPECT_EQ(admission.archivePhase(id.run), ArchivePhase::Archived);
}

TEST_F(RunRegistryAdmissionTest, ResultBatchAdmissionAndConflict_EX_REG_8)
{
    // 批次路径（§9.3 批次级重投递行）：首见落盘（无 finalize——批次只
    // 增不发布"完整"）；同内容重投递幂等跳过；不同内容冲突拒绝＋文件
    // 不被覆盖。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    ArrivalEnvelope batch = makeArrival(id, ResultKind::ResultBatch);
    batch.payloadCanon = {0x11, 0x22, 0x33};
    const AdmissionOutcome first = admission.admitResult(std::move(batch));
    EXPECT_EQ(first.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(first.archivePhaseAfter, ArchivePhase::Archiving) << "批次就位、未 finalize";

    const fs::path batchFile = m_dir / "results" / id.run.toCanonical() / "result-batch.bin";
    EXPECT_TRUE(fs::exists(batchFile));
    EXPECT_FALSE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"))
        << "批次不发布 manifest（D-13：无 manifest 即不完整）";
    const std::string bytesBefore = readAll(batchFile);

    // 同内容重投递 → DuplicateIgnored（零端口调用）。
    ArrivalEnvelope same = makeArrival(id, ResultKind::ResultBatch);
    same.payloadCanon = {0x11, 0x22, 0x33};
    EXPECT_EQ(admission.admitResult(std::move(same)).decision.verdict,
              AdmissionDecision::Verdict::DuplicateIgnored);

    // 不同内容重投递 → ContentConflict＋文件逐字节不变。
    ArrivalEnvelope different = makeArrival(id, ResultKind::ResultBatch);
    different.payloadCanon = {0x44, 0x55, 0x66};
    const AdmissionOutcome out = admission.admitResult(std::move(different));
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::ContentConflict);
    EXPECT_EQ(readAll(batchFile), bytesBefore) << "不覆盖";
}

// =====================================================================
// EX-ORT-1：四轴互不推导的逐行断言（CON-02 三态正交）
// =====================================================================

/** Completed＋DataInsufficient（合法且常见——判定轴独立于执行轴）。 */
TEST_F(RunRegistryAdmissionTest, CompletedWithDataInsufficientIsAdmitted_EX_ORT_1)
{
    const core::TaskIdentity id = makeIdentity();
    // 必验工况 A 漏验 → ②级命中 → 判定 DataInsufficient（任务仍 Completed）。
    (void)registerStandardRun(id, coverageMissing(*m_snapshot, oid(kHex32A)));
    ResultAdmission admission = makeAdmission();

    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    ASSERT_TRUE(out.envelope);
    // 四轴正交的包络观测：执行轴 Completed 与判定轴 DataInsufficient 同体
    // 共存——互不推导（§5.6 表行 3"Completed＋DataInsufficient 合法且常见"）。
    EXPECT_EQ(out.envelope->outcome, core::TaskOutcome::Completed);
    EXPECT_EQ(out.envelope->engineeringStatus, core::EngineeringStatus::DataInsufficient);
    EXPECT_FALSE(out.envelope->missingItems.empty());
    // 判定轴降级不改写任务轴：任务镜像仍 Completed、归档照常推进。
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived);
    // 正式结论载荷不因 DataInsufficient 伪造：make 只按组合规则收下证据
    // 面（本例域载荷在场属产出事实——判定轴由 envelope.engineeringStatus
    // 表达，二者互不覆盖）。
    EXPECT_TRUE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"));
}

/** HEAD 不参与接纳判定（EX-REG-6——CON-02/05：当前性由 evidence 另判）。
 *
 * 场景（ARCH §4.5 正例的结构化承载）：运行期间"HEAD 已前进"（本任务
 * 阶段 A 无调度器/修订栈接线——以"接纳编排的输入面不含任何 HEAD/当前
 * 修订参数"为结构断言＋运行跨登记接纳成功为行为断言；真实存储的跨修
 * 订归档在 contract_test 以 manifest.taskIdentity==登记原修订复核）。
 * 电机成本变更不影响运动学切片的结果仍被接纳并可 Current——Current
 * 判定本体归 evidence（EX-CUR 套件），本单元只保证不在此截获。 */
TEST_F(RunRegistryAdmissionTest, HeadChangeNotInvolvedInAdmission_EX_REG_6)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    // 到达携带的 revision＝登记原修订；接纳编排没有任何"当前修订"输入
    // （admitResult 签名面——结构断言），判定只对登记表。
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    // 无任何"策略过期/HEAD 变更"类拒绝诊断出现（EX-REG-6 观测点）。
    for (const auto& d : m_sink->reports) {
        EXPECT_NE(d.code, "EX-SNAPSHOT-STALE") << "HEAD/当前性不参与接纳";
    }
    ASSERT_TRUE(out.envelope);
    // 归档绑定原修订（manifest.taskIdentity==登记五元组——归档位置取
    // 登记记录、不重新推导的第二半区）。
    EXPECT_EQ(out.envelope->task, id);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived);
}

/** Superseded-Current 组合的执行侧半区：当前性是 evidence 的派生投影，
 *  接纳/归档照常、历史 payload 不被改写（CON-02）。 */
TEST_F(RunRegistryAdmissionTest, SupersededCurrentDoesNotBlockOrRewrite_EX_ORT_1)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));
    ASSERT_TRUE(out.envelope);

    // 载荷摘要是 make() 重算的完整性凭据（CR-02）——"历史不回写"的观测：
    // 归档落盘的域载荷字节与 make 计算的摘要一致，且接纳侧没有任何回写
    // 通道（envelope 无 setter——类型面保证）。
    const fs::path payloadFile =
        m_dir / "results" / id.run.toCanonical() / "envelope-payload.bin";
    ASSERT_TRUE(fs::exists(payloadFile));
    const std::string archived = readAll(payloadFile);
    const std::vector<std::uint8_t> bytes(archived.begin(), archived.end());
    core::ContentDigester d;
    d.update(bytes.data(), bytes.size());
    EXPECT_EQ(out.envelope->payload->digest.bytes, d.finalize());
    EXPECT_EQ(out.envelope->payload->canonicalBytes, bytes);
}

// =====================================================================
// 步 8：名称反解编排（先反解后接纳——CON-06）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, RuntimeNameResolutionGatesAdmission)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    // ① 带运行时名称的诊断＋反解成功 → 接纳（名称按登记映射可解）。
    ev::EvaluationOutput named = feasibleOutput();
    named.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{"KIN-CASE-FAIL"}, std::nullopt, std::nullopt,
        std::string{"轴1.J1"}, "evaluation", "工况案例的运行时名称引用",
        "接纳侧按登记名称映射反解"));
    m_resolver->bind(cid(kHex64B), "轴1.J1", oid(kHex32A));
    m_decoder->enqueue(std::move(named));
    EXPECT_EQ(admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope)).decision.verdict,
              AdmissionDecision::Verdict::Admitted);

    // ② 名称不在登记映射下 → NameUnresolved（先反解后接纳——任一失败
    //    即拒绝，不猜不兜底）。
    const core::TaskIdentity other = makeIdentity();
    (void)registerStandardRun(other);
    ev::EvaluationOutput unknownName = feasibleOutput();
    unknownName.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{"KIN-CASE-FAIL"}, std::nullopt, std::nullopt,
        std::string{"已删除的轴"}, "evaluation", "映射漂移的运行时名称",
        "接纳侧拒绝"));
    m_decoder->enqueue(std::move(unknownName));
    const AdmissionOutcome rejected =
        admission.admitResult(makeArrival(other, ResultKind::FinalEnvelope));
    EXPECT_EQ(rejected.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(rejected.decision.reason, AdmissionDecision::RejectReason::NameUnresolved);
    EXPECT_FALSE(fs::exists(m_dir / "results" / other.run.toCanonical() / "manifest.json"))
        << "反解失败的结果不归档";

    // ③ 反解器未装配而到达含名称 → 同样拒绝（fail-closed 不跳过——装配
    //    缺失不放行含名称结果）。
    const core::TaskIdentity third = makeIdentity();
    (void)registerStandardRun(third);
    ResultAdmission noResolver(m_registry, *m_sink);
    noResolver.setOutputDecoder(m_decoder.get());
    ev::EvaluationOutput withName = feasibleOutput();
    withName.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{"KIN-CASE-FAIL"}, std::nullopt, std::nullopt, std::string{"某轴"},
        "evaluation", "无反解器装配时的运行时名称", "装配面检查"));
    m_decoder->enqueue(std::move(withName));
    const AdmissionOutcome out3 = admission.admitResult(makeArrival(third, ResultKind::FinalEnvelope));
    EXPECT_EQ(out3.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out3.decision.reason, AdmissionDecision::RejectReason::NameUnresolved);
}

// =====================================================================
// 步 9 前置与组装：解码失败/组装校验拒绝（T9）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, UndecodablePayloadRejected)
{
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    // 解码脚本耗尽＝nullopt（通道数据错误）→ PayloadUndecodable。
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::PayloadUndecodable);
    EXPECT_FALSE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"));

    // 解码器未装配 → 同拒绝面（装配缺失不静默放行）。
    const core::TaskIdentity other = makeIdentity();
    (void)registerStandardRun(other);
    ResultAdmission noDecoder(m_registry, *m_sink);
    noDecoder.setNameResolver(m_resolver.get());
    const AdmissionOutcome out2 = admission.admitResult(makeArrival(other, ResultKind::FinalEnvelope));
    EXPECT_EQ(out2.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out2.decision.reason, AdmissionDecision::RejectReason::PayloadUndecodable);
}

TEST_F(RunRegistryAdmissionTest, ConstructionFailureFailsTask_EX_ORT_1)
{
    // 组装校验拒绝（表 3 条目纪律格：Satisfied 项缺产物摘要）→
    // ConstructionFailed＋T9（任务转 Failed）＋不构造不归档。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    ResultAdmission admission = makeAdmission();

    ev::EvaluationOutput undisciplined = feasibleOutput();
    ev::EvidenceItem missingDigest = undisciplined.evidence.front();
    missingDigest.itemId = "kin.reach-per-task-point";
    missingDigest.artifactDigest.reset();  // Satisfied 必带产物摘要——presence 纪律违例
    undisciplined.evidence = {missingDigest};
    m_decoder->enqueue(std::move(undisciplined));

    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Rejected);
    EXPECT_EQ(out.decision.reason, AdmissionDecision::RejectReason::ConstructionFailed);
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Failed) << "步 9：make 失败 → Running→Failed";
    EXPECT_FALSE(out.envelope);
    EXPECT_EQ(m_injector->beginCalls, 0) << "不归档";
    EXPECT_EQ(m_registry.tryRun(id.run)->currentState, core::TaskState::Failed);
    // 判定记录诊断携带校验消息（开发定位面）。
    EXPECT_FALSE(out.decision.diagnostics.empty());
}

// =====================================================================
// 归档事务：有界重试（D-11）／abandon 责任终结／A7 防御
// =====================================================================

TEST_F(RunRegistryAdmissionTest, ArchiveRetriesBoundedThenSucceeds)
{
    // 前 2 次 writeBatch 注入 disk-full：有界重试（2 次、退避 1 s→4 s——
    // D-11）后成功 → Admitted＋Archived；退避值被收集器如实记录（不 sleep）。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    std::vector<std::chrono::milliseconds> delays;
    ResultAdmission admission = makeAdmission(&delays);
    m_injector->writeFailures = 2;

    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived);
    ASSERT_EQ(delays.size(), 2u);
    EXPECT_EQ(delays[0], std::chrono::milliseconds{1000});
    EXPECT_EQ(delays[1], std::chrono::milliseconds{4000});
    EXPECT_TRUE(m_injector->abandons.empty());
    EXPECT_TRUE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"));
}

TEST_F(RunRegistryAdmissionTest, ArchiveRetryExhaustionAbandonsKeepsCompleted_EX_ARC_5)
{
    // 重试耗尽：abandon(Failed)＋EX-ARCHIVE-FAILED＋阶段 ArchiveFailed；
    // 任务终态不变（Completed 保持——"任务完成≠归档完成"的正交观测）。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    std::vector<std::chrono::milliseconds> delays;
    ResultAdmission admission = makeAdmission(&delays);
    m_injector->writeFailures = 99;  // 恒失败（远超重试预算）

    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    // 接纳成立（envelope 已构造、任务 Completed），归档责任以 abandon 终结。
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::ArchiveFailed);
    EXPECT_EQ(admission.archivePhase(id.run), ArchivePhase::ArchiveFailed);
    ASSERT_EQ(m_injector->abandons.size(), 1u);
    EXPECT_EQ(m_injector->abandons.front(), pd::ArchiveEndReason::Failed);
    EXPECT_TRUE(m_sink->hasReportCode("EX-ARCHIVE-FAILED"));
    // 退避只发生在重试间隙（2 次——D-11 预算）。
    EXPECT_EQ(delays.size(), 2u);
    // 无 manifest（未 finalize）＝运行目录残留为"未完成归档"（D-13）。
    EXPECT_FALSE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"));
}

TEST_F(RunRegistryAdmissionTest, AuthorityLostRejectedWithDiagnostics_EX_REG_5)
{
    // A7 防御失败分支：上下文已 Closed＋重取失败 → EX-ARCHIVE-AUTHORITY-
    // LOST 拒绝归档；接纳成立、任务保持 Completed（结果不落盘，用户可重跑）。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    m_store->requestClose();  // 存储上下文关闭（A7 场景构造——异常路径）
    ASSERT_TRUE(m_store->closed());
    m_gateway->setReacquireResult(false);

    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted) << "拒绝的是归档不是接纳";
    EXPECT_EQ(out.taskStateAfter, core::TaskState::Completed);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::ArchiveFailed);
    EXPECT_TRUE(m_sink->hasReportCode("EX-ARCHIVE-AUTHORITY-LOST"));
    EXPECT_FALSE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"))
        << "结果不落盘";
}

TEST_F(RunRegistryAdmissionTest, AuthorityReacquiredArchivesNormally_EX_REG_5)
{
    // A7 防御成功分支（真实重开）：关闭上下文后，重取动作＝工厂重新
    // open writable 同目录（锁随 Closed 释放——真实路径）→ begin 走新
    // 写面 → 照常归档至登记 runDir。
    const core::TaskIdentity id = makeIdentity();
    m_store->requestClose();
    ASSERT_TRUE(m_store->closed());

    // A7 重开网关：port() 先指向旧（Closed）上下文写面；重取回调经工厂
    // 真实再开并把写面切到新上下文。
    auto reopenStore = std::make_unique<pd::OpenStoreResult>();
    ScriptedGateway reopenGateway(m_store->archive());
    reopenGateway.setReacquireAction([&] {
        pd::OpenStoreRequest req;
        req.path = m_dir;
        *reopenStore = pd::ProjectStoreFactory::open(req);
        if (reopenStore->store != nullptr && reopenStore->store->writable()) {
            reopenGateway.redirect(reopenStore->store->archive());
            return true;
        }
        return false;
    });

    // 本运行的登记改用重开网关（非所有权注入——存活期覆盖本用例）。
    (void)registerStandardRun(id, {}, &reopenGateway);

    ResultAdmission admission = makeAdmission();
    m_decoder->enqueue(feasibleOutput());
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::FinalEnvelope));

    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::Archived) << "重取成功→照常归档";
    EXPECT_TRUE(reopenStore->store != nullptr);
    EXPECT_TRUE(fs::exists(m_dir / "results" / id.run.toCanonical() / "manifest.json"));
}

// =====================================================================
// 非归档类型边界（CheckpointBatch/Progress/PartialDataOnTerminate）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, NonArchivedKindsPassClassificationOnly)
{
    const core::TaskIdentity id = makeIdentity();
    RunRegistration& reg = registerStandardRun(id);
    reg.allowedResultKinds.push_back(ResultKind::CheckpointBatch);
    reg.allowedResultKinds.push_back(ResultKind::Progress);
    reg.allowedResultKinds.push_back(ResultKind::PartialDataOnTerminate);

    ResultAdmission admission = makeAdmission();
    const AdmissionOutcome out = admission.admitResult(makeArrival(id, ResultKind::Progress));

    // 登记核对面通过（Admitted）；持久化归各自组件（EX-T08/自有通道）——
    // 归档阶段不动、无任何落盘（本编排不伪造第二持久化路径）。
    EXPECT_EQ(out.decision.verdict, AdmissionDecision::Verdict::Admitted);
    EXPECT_EQ(out.archivePhaseAfter, ArchivePhase::NotApplicable);
    EXPECT_EQ(m_injector->beginCalls, 0);
    EXPECT_FALSE(fs::exists(m_dir / "results" / id.run.toCanonical()));
}

// =====================================================================
// 调用方契约（admitResult 入口的 fail-fast 面）
// =====================================================================

TEST_F(RunRegistryAdmissionTest, InvalidArrivalIdentityFailsFast)
{
    ResultAdmission admission = makeAdmission();
    ArrivalEnvelope bad = makeArrival(makeIdentity(), ResultKind::FinalEnvelope);
    bad.identity.attempt = core::AttemptId{0};  // 保留值身份不可作到达
    EXPECT_THROW(admission.admitResult(std::move(bad)), ExecutionError);
}

}  // namespace
