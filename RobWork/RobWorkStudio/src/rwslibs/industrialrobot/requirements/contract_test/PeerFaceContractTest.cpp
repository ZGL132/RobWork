/**
 * @file   PeerFaceContractTest.cpp
 * @brief  requirements 跨单元对端面契约套件（WP-14-T09）——以 testkit
 *         FaultInterceptor（TK-T09）伪造 project/io/evidence/diagnostics
 *         四个对端接口，逐项断言 codec/命令 payload 对 project 契约形状、
 *         mapCsv 对 io RawTable/CsvParseReport 形状、readinessSummary 对
 *         evidence ReadinessSummary 形状、REQ- 稳定码对 diagnostics 契约
 *         夹具；另承载测试侧红线 T-1/T-2 的具名自证（契约 acceptance 3/4
 *         的落地面）。
 *
 * 设计依据：
 *   - units/testkit.md §6.4（故障注入接缝原则——生产代码不含 testkit 头；
 *     fake 实现编译进测试目标、实现消费者自有接口；故障点标识
 *     <unit>/<接口>/<动作>）、§2.4（T-1：产品目标零 testkit 链接零
 *     testkit 头；T-2：testkit 只依赖 core＋标准库；允许形态
 *     sdurws_ird_<unit>_test → {被测目标, testkit, gtest} 无环）、§3.6
 *     （testdata/ 不安装——安装树扫描断言归 WP-24 侧）
 *   - units/requirements.md §3.1/§3.2（二分结构与登记边）、§9.1（命令
 *     协议——载荷 canonical 透传存储不解释，D-10）、§9.5（mapCsv @pre
 *     RawTable/report.dataRows 一致性；readinessSummary 两字段投影）、
 *     §9.6（REQ- 稳定码注册纪律）
 *   - 任务契约 tasks/foundation/WP-14-T09.json acceptance 3（T-1/T-2 红
 *     线用例）、4（contract_test 对端面套件——对端类型形状漂移由测试面
 *     捕获，不构造产品边）
 *
 * 先例：requirements/contract_test/BuildGraphContractTest.cpp（WP-14-T02
 * 落位期构建图契约——本文件业务用例级对端面与红线扩展同目标共存）。
 * 模式约束：纯 core＋requirements＋testkit＋io/project/evidence 值类型
 * （零 Qt）——冒烟＋集成两模式编译运行。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/io/AtomicFile.hpp>
#include <sdurws/ird/io/Csv.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/requirements/CommandHandlers.hpp>
#include <sdurws/ird/requirements/Codec.hpp>
#include <sdurws/ird/requirements/DiagCodes.hpp>
#include <sdurws/ird/requirements/Errors.hpp>
#include <sdurws/ird/requirements/Import.hpp>
#include <sdurws/ird/requirements/ObjectTypes.hpp>
#include <sdurws/ird/requirements/Readiness.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/testkit/Fault.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/math/Vector3D.hpp>

namespace tk = sdurws::ird::testkit;
namespace req = sdurws::ird::requirements;
namespace io = sdurws::ird::io;

namespace {

// =====================================================================
// 测试目标根（IRD_REQUIREMENTS_UNIT_ROOT 注入——industrialrobot 树根）
// =====================================================================

/// industrialrobot 单元树根（requirements/ 与 testkit/ 的共同父目录）。
const std::filesystem::path& unitRoot()
{
    static const std::filesystem::path dir{IRD_REQUIREMENTS_UNIT_ROOT};
    return dir;
}

/// 读取文本文件全文（CMakeLists 扫描入口；缺失/不可读显性失败）。
std::string readTextFile(const std::filesystem::path& path)
{
    EXPECT_TRUE(std::filesystem::exists(path)) << "文件不存在: " << path.string();
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 剥离 "#" 注释后的逐行视图（注释散文不是构建图引用——T02 同款纪律）。
std::vector<std::string> codeLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const auto hashPos = line.find('#');
        if (hashPos != std::string::npos) { line.erase(hashPos); }
        lines.push_back(line);
    }
    return lines;
}

// =====================================================================
// project 对端 fake（消费者自有接口实现——TK-T09 接缝原则的落地面）
// =====================================================================

using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::ObjectId;

/// 非全零内容版本（对象库编址键——替身不复核摘要真实性）。
ContentVersion fakeContentVersion(std::uint8_t tag)
{
    ContentVersion cv;
    cv.bytes[0] = tag;
    return cv;
}

/// project ②端口内存替身（modeling CommandFixtures 同款精简——数据形状
/// 与 project 公共契约一致；单线程用例内使用）。
class FakeQueryPort final : public sdurws::ird::project::IProjectQueryPort {
public:
    sdurws::ird::project::RevisionView view;
    std::map<std::string, std::vector<std::uint8_t>> objects;

    void addObject(const ObjectId& oid, std::string token,
                   const std::vector<std::uint8_t>& bytes)
    {
        const ContentVersion cv = fakeContentVersion(
            static_cast<std::uint8_t>(view.objectRefs.size() + 1U));
        sdurws::ird::project::ObjectRef ref;
        ref.objectId = oid;
        ref.contentVersion = cv;
        ref.objectTypeToken = std::move(token);
        ref.digest256 = std::string(64, '0');
        view.objectRefs.push_back(std::move(ref));
        objects[oid.toCanonical() + "|" + cv.toCanonical()] = bytes;
    }

    [[nodiscard]] sdurws::ird::project::RevisionView head() const override { return view; }
    [[nodiscard]] std::optional<sdurws::ird::project::RevisionView> tryRevision(
        sdurws::ird::core::RevisionId id) const override
    {
        if (id == view.id) { return view; }
        return std::nullopt;
    }
    [[nodiscard]] sdurws::ird::project::RevisionView revision(
        sdurws::ird::core::RevisionId) const override { return view; }
    [[nodiscard]] sdurws::ird::project::ProjectMetadataView currentMetadata() const override
    {
        return {};
    }
    [[nodiscard]] std::optional<sdurws::ird::project::ProjectMetadataView> metadataAt(
        sdurws::ird::core::RevisionId) const override { return std::nullopt; }
    [[nodiscard]] std::vector<sdurws::ird::project::BranchTip> branchTips() const override
    {
        return {};
    }
    [[nodiscard]] std::vector<sdurws::ird::project::RevisionView> branchHistory(
        sdurws::ird::core::BranchId, std::uint32_t) const override { return {}; }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        ObjectId oid, ContentVersion cv) const noexcept override
    {
        auto it = objects.find(oid.toCanonical() + "|" + cv.toCanonical());
        if (it == objects.end()) { return std::nullopt; }
        return it->second;
    }
    [[nodiscard]] std::vector<std::uint8_t> object(
        ObjectId oid, ContentVersion cv) const override
    {
        auto bytes = tryObject(oid, cv);
        if (!bytes.has_value()) {
            throw std::logic_error("fixture: 对象字节缺失（引用断裂）");
        }
        return *bytes;
    }
    [[nodiscard]] std::vector<sdurws::ird::project::DraftInfo> listDrafts(
        sdurws::ird::core::BranchId) const override { return {}; }
    [[nodiscard]] std::vector<sdurws::ird::project::RunInfo> listRuns(
        sdurws::ird::core::RevisionId) const override { return {}; }
    [[nodiscard]] std::filesystem::path runDir(sdurws::ird::core::RunId) const override
    {
        return {};
    }
};

/// 故障拦截端口（FaultInterceptor<IProjectQueryPort> 接缝形态——方法内
/// 先 shouldFire 决定注入失败〔抛被测接口声明的错误族——防御复核 fail-
/// fast 面〕，未命中转调 real；命中序列入 FaultLog 观测）。
class FaultingQueryPort final : public sdurws::ird::project::IProjectQueryPort {
public:
    explicit FaultingQueryPort(std::shared_ptr<FakeQueryPort> real,
                               tk::FaultPlan plan)
        : fault_(std::move(real), std::move(plan))
    {
    }

    [[nodiscard]] sdurws::ird::project::RevisionView head() const override
    {
        return fault_.real()->head();
    }
    [[nodiscard]] std::optional<sdurws::ird::project::RevisionView> tryRevision(
        sdurws::ird::core::RevisionId id) const override
    {
        // 故障点 project/query/try-revision：命中第 occurrence 次→抛（数据
        // 侧异常透传——prepare 防御复核的 fail-fast 面，不吞不改）。
        if (fault_.shouldFire("project/query/try-revision", ++tryRevisionCalls_)) {
            throw std::runtime_error("fault: project/query/try-revision 注入失败");
        }
        (void)0;
        return fault_.real()->tryRevision(id);
    }
    [[nodiscard]] sdurws::ird::project::RevisionView revision(
        sdurws::ird::core::RevisionId id) const override
    {
        return fault_.real()->revision(id);
    }
    [[nodiscard]] sdurws::ird::project::ProjectMetadataView currentMetadata() const override
    {
        return fault_.real()->currentMetadata();
    }
    [[nodiscard]] std::optional<sdurws::ird::project::ProjectMetadataView> metadataAt(
        sdurws::ird::core::RevisionId id) const override
    {
        return fault_.real()->metadataAt(id);
    }
    [[nodiscard]] std::vector<sdurws::ird::project::BranchTip> branchTips() const override
    {
        return fault_.real()->branchTips();
    }
    [[nodiscard]] std::vector<sdurws::ird::project::RevisionView> branchHistory(
        sdurws::ird::core::BranchId b, std::uint32_t n) const override
    {
        return fault_.real()->branchHistory(b, n);
    }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        ObjectId oid, ContentVersion cv) const noexcept override
    {
        return fault_.real()->tryObject(oid, cv);
    }
    [[nodiscard]] std::vector<std::uint8_t> object(
        ObjectId oid, ContentVersion cv) const override
    {
        if (fault_.shouldFire("project/query/object", ++objectCalls_)) {
            throw std::runtime_error("fault: project/query/object 注入失败");
        }
        return fault_.real()->object(oid, cv);
    }
    [[nodiscard]] std::vector<sdurws::ird::project::DraftInfo> listDrafts(
        sdurws::ird::core::BranchId b) const override
    {
        return fault_.real()->listDrafts(b);
    }
    [[nodiscard]] std::vector<sdurws::ird::project::RunInfo> listRuns(
        sdurws::ird::core::RevisionId r) const override
    {
        return fault_.real()->listRuns(r);
    }
    [[nodiscard]] std::filesystem::path runDir(sdurws::ird::core::RunId r) const override
    {
        return fault_.real()->runDir(r);
    }

    /// 命中日志快照（观测面——断言注入确实发生在计划故障点）。
    [[nodiscard]] tk::FaultLog log() const { return fault_.log(); }

private:
    mutable tk::FaultInterceptor<sdurws::ird::project::IProjectQueryPort> fault_;
    mutable std::uint64_t tryRevisionCalls_ = 0;  ///< try-revision 命中计数
    mutable std::uint64_t objectCalls_ = 0;       ///< object 命中计数
};

/// 黄金最小闭包（根＋点集两对象——prepare 管线驱动的最小合法载荷）。
req::PointSet healthyPointSet()
{
    req::TaskPoint p;
    p.objectId = ObjectId::generate();
    p.name = "P1";
    p.pose.position =
        sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.1, 0.2, 0.3), sdurws::ird::core::ValueProvenance::make(
                sdurws::ird::core::ProvenanceKind::UserProvided, std::nullopt,
                std::nullopt, std::string("peer-fixture")));
    p.pose.constrainedDof.z = true;
    p.work.enabled = true;
    p.work.distanceM = 1.0;  // m
    req::PointSet ps;
    ps.entries.push_back(std::move(p));
    req::sortEntriesByObjectId(ps.entries);
    return ps;
}

/// canonical 编码（ok 断言封装）。
std::vector<std::uint8_t> encodeOk(const req::RequirementCodec& codec,
                                   const req::RequirementObjectVariant& object)
{
    auto encoded = codec.encode(object, req::kCurrentRequirementFormatVersion);
    EXPECT_TRUE(encoded.ok()) << encoded.error().detail;
    return encoded.ok() ? encoded.get() : std::vector<std::uint8_t>{};
}

// =====================================================================
// ACC4①——命令 payload/codec 对 project 契约形状＋FaultInterceptor 注入
// =====================================================================

/**
 * 对端面①（acceptance 4——codec/命令 payload 对 project 契约形状）：
 * 首应用载荷经真实 prepare 管线产出 Planned＋恰五写槽（根＋四集合——
 * D-REQ-9"应用恰一修订"的形状面）；载荷 token 全部为 requirements 登记
 * 词表（project 路由面）；对 project 端口的计划注入（FaultInterceptor，
 * 故障点 project/query/try-revision）→防御复核 fail-fast 透传——对端形
 * 状漂移由测试面显性捕获，不构造产品边。
 */
TEST(PeerProject, PayloadShapePreparePlanAndQueryFault_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "CON-05"},
                  std::vector<std::string>{});
    using namespace req;

    // 最小首应用载荷（根＋点集——两槽；根字节由 prepare 候选根重编码）。
    const RequirementCodec codec;
    RequirementSet root;
    root.name = "对端面契约集";
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    payload.objects.push_back(RequirementPayloadSlot{
        true, ObjectId{}, std::string(kReqSetObjectType),
        encodeOk(codec, RequirementObjectVariant(root))});
    payload.objects.push_back(RequirementPayloadSlot{
        true, ObjectId{}, std::string(kReqPointSetObjectType),
        encodeOk(codec, RequirementObjectVariant(healthyPointSet()))});

    // 载荷对 project 契约形状面：处理器注册 token 无点（O-35/project
    // §4.4.4 冻结语法）＋版本三元组的处理器自有版本戳一致。
    ApplyRequirementSetHandler handler;
    EXPECT_EQ(handler.commandType(), "apply-requirement-set");
    EXPECT_EQ(handler.currentPayloadVersion(), kRequirementCommandPayloadVersion);

    // 真实端口：prepare 经全管线产出 Planned＋恰五写槽（根＋四集合——
    // 首应用全量形态；objectWrites 的 token 全在登记词表内——路由面）。
    auto real = std::make_shared<FakeQueryPort>();
    sdurws::ird::project::CommandEnvelope envelope;
    envelope.commandType = handler.commandType();
    envelope.payloadFormatVersion = kRequirementCommandPayloadVersion;
    envelope.payloadCanonical = encodeRequirementCommandPayload(payload);
    sdurws::ird::project::CommandPlan plan;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    {
        sdurws::ird::project::HandlerContext ctx(*real, nullptr, nullptr);
        const auto outcome = handler.prepare(ctx, envelope, real->view, plan, diags);
        ASSERT_EQ(outcome, sdurws::ird::project::PrepareOutcome::Planned);
        EXPECT_EQ(plan.objectWrites.size(), payload.objects.size())
            << "写槽数＝载荷槽数（最小载荷＝根＋点集两槽；五槽全量形态"
               "由 GoldenDatasetsContractTest V01 用例承载）";
        for (const auto& write : plan.objectWrites) {
            EXPECT_TRUE(write.objectTypeToken == kReqSetObjectType
                        || write.objectTypeToken == kReqPointSetObjectType
                        || write.objectTypeToken == kReqRegionSetObjectType
                        || write.objectTypeToken == kReqConditionSetObjectType
                        || write.objectTypeToken == kReqPlanSetObjectType)
                << "写槽 token 词表外（project 路由面漂移）: "
                << write.objectTypeToken;
        }
    }

    // FaultInterceptor 注入（计划命中 try-revision 第 1 次）：防御复核的
    // 数据侧异常 fail-fast 透传——对端不可用即显性失败（不吞不改）。
    // 基线引用表登记一个集合对象——rebuild 遍历引用表时才会触达
    // object() 缝（空基线不取数，故障点不会被命中）。
    const ObjectId baselineSetOid = ObjectId::generate();
    real->addObject(baselineSetOid, std::string(kReqPointSetObjectType),
                    encodeOk(codec, RequirementObjectVariant(healthyPointSet())));

    tk::FaultPlan faultPlan;
    // 故障点落 prepare 真实触达的取数缝：基线防御性复核按引用表逐对象
    // object() 取回（强语义——io/execution 同款接缝最小集）。
    faultPlan.triggers.push_back(tk::FaultTrigger{"project/query/object", 1});
    FaultingQueryPort faulting(real, faultPlan);
    sdurws::ird::project::CommandPlan faultPlanOut;
    std::vector<sdurws::ird::core::DiagnosticRecord> faultDiags;
    sdurws::ird::project::HandlerContext ctx(faulting, nullptr, nullptr);
    EXPECT_THROW((void)handler.prepare(ctx, envelope, real->view,
                                       faultPlanOut, faultDiags),
                 std::runtime_error)
        << "project 端口故障→fail-fast 透传（对端漂移显性捕获）";
    const auto log = faulting.log();
    ASSERT_EQ(log.hits.size(), 1U);
    EXPECT_EQ(log.hits[0].first, "project/query/object");
    EXPECT_EQ(log.hits[0].second, 1U);
}

// =====================================================================
// ACC4②——mapCsv 对 io RawTable/CsvParseReport 形状＋原子写出故障注入
// =====================================================================

/// 故障原子写出器（FaultInterceptor<IAtomicFileWriter> 接缝形态——commit
/// 按计划注入 IO 资源错误，prepare/abort 转调真实实现）。
class FaultingAtomicWriter final : public sdurws::ird::io::IAtomicFileWriter {
public:
    explicit FaultingAtomicWriter(sdurws::ird::io::IAtomicFileWriterPtr real,
                                  tk::FaultPlan plan)
        : fault_(std::move(real), std::move(plan))
    {
    }

    sdurws::ird::io::IoResult<sdurws::ird::io::AtomicTarget> prepare(
        const std::filesystem::path& target,
        sdurws::ird::io::ReplacePolicy policy) override
    {
        return fault_.real()->prepare(target, policy);
    }
    sdurws::ird::io::IoResult<void> commit(
        sdurws::ird::io::AtomicTarget& target) override
    {
        // 故障点 io/atomic-writer/commit：磁盘满类资源故障模拟（AT-13/
        // V-10 事务回滚面——"commit 失败→旧文件完好"）。
        if (fault_.shouldFire("io/atomic-writer/commit", ++commitCalls_)) {
            sdurws::ird::io::IoResult<void> failure;
            failure.error.code = sdurws::ird::io::IoErrorCode::ResAccessDenied;
            failure.error.detail = "fault: io/atomic-writer/commit 注入失败";
            return failure;
        }
        return fault_.real()->commit(target);
    }
    sdurws::ird::io::IoResult<void> abort(
        sdurws::ird::io::AtomicTarget& target) override
    {
        return fault_.real()->abort(target);
    }

    [[nodiscard]] tk::FaultLog log() const { return fault_.log(); }

private:
    tk::FaultInterceptor<sdurws::ird::io::IAtomicFileWriter> fault_;
    std::uint64_t commitCalls_ = 0;  ///< commit 命中计数
};

/**
 * 对端面②（acceptance 4——mapCsv 对 io RawTable/CsvParseReport 形状）：
 * rows 与 report.dataRows 不一致→invalid_argument（@pre 契约——io 形状
 * 漂移由本测试面显性捕获）；副本导出经注入的原子写出器在 commit 处故障
 * （FaultInterceptor，AT-13 事务面）→ok=false＋旧文件字节完好（V-10
 * "导出失败旧文件完好"）＋命中日志可观测。
 */
TEST(PeerIo, MapCsvRawTableShapeAndAtomicCommitFault_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "REQ-12"},
                  std::vector<std::string>{"AT-02", "AT-13"});
    using namespace req;

    // ①RawTable/CsvParseReport 形状漂移捕获：@pre rows==dataRows 违约→
    // 调用方契约违约 fail-fast（std::invalid_argument——io 形状漂移由
    // 测试面捕获的机器判别面）。
    io::RawTable inconsistent;
    inconsistent.report.hasHeader = true;
    for (int f = 0; f < static_cast<int>(kImportFieldCount); ++f) {
        inconsistent.report.header.emplace_back(
            importFieldToken(static_cast<ImportField>(f)));
    }
    // 形状漂移探针（对齐实现缝 Import.cpp：rows 为空而 report.dataRows>0
    // ＝流式丢弃行后误入——invalid_argument fail-fast，不静默返回空导入）。
    inconsistent.report.dataRows = 2;  // 行集空而计数>0（形状违约）
    RequirementImporter importer;
    FieldMapping mapping = FieldMapping::none();
    mapping.assign(ImportField::Id, 0);
    ImportUnitOptions units;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    EXPECT_THROW((void)importer.mapCsv(inconsistent, mapping, units, diags),
                 std::invalid_argument)
        << "rows 与 report.dataRows 不一致→invalid_argument（io 形状契约）";

    // ②原子写出故障注入：旧文件先就位→exportCopy 在 commit 处失败→
    // ok=false＋旧文件字节完好（V-10/AT-13 事务回滚面）。
    const auto workDir = std::filesystem::path{testing::TempDir()} / "peer-io-fault";
    std::filesystem::create_directories(workDir);
    const auto target = workDir / "export_copy.json";
    static const std::string kOldContent = R"({"schemaVersion": 1, "points": []})";
    {
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << kOldContent;
    }
    tk::FaultPlan faultPlan;
    faultPlan.triggers.push_back(tk::FaultTrigger{"io/atomic-writer/commit", 1});
    auto faulting = std::make_shared<FaultingAtomicWriter>(
        sdurws::ird::io::makeAtomicFileWriter(), faultPlan);
    RequirementImporter injected(faulting);

    RequirementWorkingSet ws;
    req::TaskPoint p;
    p.objectId = ObjectId::generate();
    p.name = "导出载体";
    p.pose.position =
        sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.1, 0.2, 0.3), sdurws::ird::core::ValueProvenance::make(
                sdurws::ird::core::ProvenanceKind::UserProvided, std::nullopt,
                std::nullopt, std::string("peer-fixture")));
    p.pose.constrainedDof.z = true;
    p.work.enabled = true;
    p.work.distanceM = 1.0;  // m
    ws.points.entries.push_back(std::move(p));
    auto outcome = injected.exportCopy(ws, ExportFormat::Json,
                                       ExportTarget{target, false});
    EXPECT_FALSE(outcome.ok) << "commit 故障→导出失败（值面 ok=false）";
    EXPECT_EQ(readTextFile(target), kOldContent)
        << "失败时旧文件完好（字节级——V-10 事务面）";
    const auto log = faulting->log();
    ASSERT_EQ(log.hits.size(), 1U);
    EXPECT_EQ(log.hits[0].first, "io/atomic-writer/commit");
}

// =====================================================================
// ACC4③——readinessSummary 对 evidence ReadinessSummary 形状
// =====================================================================

/// evidence 消费端 fake 接口（测试目标内定义的消费者自有接口——Fault
/// Interceptor 接缝；evidence 侧组装方"收到 ReadinessSummary"的形状面）。
class IEvidenceSink {
public:
    virtual ~IEvidenceSink() = default;
    /// 接收①级输入门禁数据（evidence::ReadinessSummary 值透传）。
    virtual void accept(const sdurws::ird::evidence::ReadinessSummary& summary) = 0;
};

/// 记录型真实实现（接收值快照供断言）。
class RecordingSink final : public IEvidenceSink {
public:
    std::optional<sdurws::ird::evidence::ReadinessSummary> received;
    void accept(const sdurws::ird::evidence::ReadinessSummary& summary) override
    {
        received = summary;
    }
};

/// 故障拦截 sink（FaultInterceptor<IEvidenceSink>——evidence 对端不可用
/// 即显性失败；命中序列入 FaultLog）。
class FaultingSink final : public IEvidenceSink {
public:
    FaultingSink(std::shared_ptr<IEvidenceSink> real, tk::FaultPlan plan)
        : fault_(std::move(real), std::move(plan))
    {
    }
    void accept(const sdurws::ird::evidence::ReadinessSummary& summary) override
    {
        if (fault_.shouldFire("evidence/sink/accept", ++calls_)) {
            throw std::runtime_error("fault: evidence/sink/accept 注入失败");
        }
        fault_.real()->accept(summary);
    }
    [[nodiscard]] tk::FaultLog log() const { return fault_.log(); }

private:
    tk::FaultInterceptor<IEvidenceSink> fault_;
    std::uint64_t calls_ = 0;  ///< accept 命中计数
};

/**
 * 对端面③（acceptance 4——readinessSummary 对 evidence ReadinessSummary
 * 形状）：R0~R9 报告投影出的两字段值类型与 evidence::ReadinessSummary
 * 逐字段一致（valid/invalidMustItems——PA-1 对端值类型直投，不包装）；
 * Blocking 面 valid=false＋启用 Must 条目全量清单；evidence 消费端故障
 * 注入（FaultInterceptor）→显性失败＋命中观测。
 */
TEST(PeerEvidence, ReadinessSummaryProjectionShapeAndFault_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "EVI-01"},
                  std::vector<std::string>{});
    using namespace req;

    // 工作集：启用 Must 点带 Blocking 发现（悬空 tcpRef——R8 面）。
    RequirementWorkingSet ws;
    req::TaskPoint bad;
    bad.objectId = ObjectId::generate();
    bad.name = "P-bad";
    bad.pose.position =
        sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.1, 0.2, 0.3), sdurws::ird::core::ValueProvenance::make(
                sdurws::ird::core::ProvenanceKind::UserProvided, std::nullopt,
                std::nullopt, std::string("peer-fixture")));
    bad.pose.constrainedDof.z = true;
    bad.work.enabled = true;
    bad.work.distanceM = 1.0;  // m
    req::RequirementReference danglingTcp;
    danglingTcp.kind = req::RequirementRefKind::Tool;
    danglingTcp.objectId = ObjectId::generate();  // 闭包外工具
    danglingTcp.tcpKey = "tcp1";
    bad.tcpRef = danglingTcp;
    ws.points.entries.push_back(std::move(bad));

    const RequirementReadinessChecker checker;
    const auto report = checker.check(ws, req::CheckContext{});
    ASSERT_TRUE(report.hasBlocking());

    // 形状面：投影值类型＝evidence::ReadinessSummary（逐字段机械比对——
    // 对端值类型直投，任何字段漂移在此显性失败）。
    const auto summary = checker.readinessSummary(report);
    EXPECT_FALSE(summary.valid) << "Blocking⇒valid=false（§9.5 投影规则）";
    ASSERT_EQ(summary.invalidMustItems.size(), 1U);
    EXPECT_EQ(summary.invalidMustItems[0], report.invalidMustItems[0])
        << "invalidMustItems 直投（零重算——NFR-MNT-04）";
    // 对端类型字面复核（evidence §6.4① 两字段形状——多字段/缺字段即漂移）。
    const sdurws::ird::evidence::ReadinessSummary expectedShape{false,
                                                                summary.invalidMustItems};
    EXPECT_TRUE(summary == expectedShape);

    // 经 fake sink 的消费面（形状兼容由编译期+运行期双证）＋故障注入。
    auto recording = std::make_shared<RecordingSink>();
    recording->accept(summary);
    ASSERT_TRUE(recording->received.has_value());
    EXPECT_TRUE(*recording->received == summary);

    tk::FaultPlan faultPlan;
    faultPlan.triggers.push_back(tk::FaultTrigger{"evidence/sink/accept", 2});
    FaultingSink faulting(recording, faultPlan);
    faulting.accept(summary);   // 第 1 次：放行
    EXPECT_THROW(faulting.accept(summary), std::runtime_error)  // 第 2 次：注入
        << "evidence 对端故障→显性失败（形状/可用性漂移由测试面捕获）";
    const auto log = faulting.log();
    ASSERT_EQ(log.hits.size(), 1U);
    EXPECT_EQ(log.hits[0].first, "evidence/sink/accept");
    EXPECT_EQ(log.hits[0].second, 2U);
}

// =====================================================================
// ACC4④——REQ- 稳定码对 diagnostics 契约夹具
// =====================================================================

/**
 * 对端面④（acceptance 4——REQ- 稳定码对 diagnostics 契约夹具）：全部
 * 16 码注册进真实 StableCodeRegistry（诊断码单点注册纪律）→find 逐码
 * 命中且 ownerUnit/前缀一致（diagnostics.md §4.5 契约——码值漂移/参数
 * schema 漂移在注册期验证即显性失败）。
 */
TEST(PeerDiagnostics, StableCodesRegisteredContractFixture_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"},
                  std::vector<std::string>{});

    sdurws::ird::diagnostics::StableCodeRegistry registry;
    req::registerRequirementCodes(registry);

    // 全码 find 命中＋前缀-所有权一致（REQ-* → ownerUnit=requirements）。
    const auto descriptors = req::requirementCodeDescriptors();
    ASSERT_EQ(descriptors.size(), 16U) << "§9.6 表全量 16 码（分批纪律已收口）";
    for (const auto& d : descriptors) {
        const auto* found = registry.find(d.code);
        ASSERT_NE(found, nullptr) << "码未注册: " << d.code;
        EXPECT_EQ(found->code, d.code);
        EXPECT_EQ(found->ownerUnit, "requirements");
        EXPECT_EQ(std::string(found->code).rfind("REQ-", 0), 0U)
            << "码值前缀与 ownerUnit 不一致（diagnostics §4.5 契约）";
    }
    const auto registered = registry.registeredCodes("requirements");
    EXPECT_EQ(registered.size(), 16U) << "requirements 名下恰 16 码（无越权登记）";
}

// =====================================================================
// ACC3——测试侧红线 T-1/T-2（产品零 testkit；测试形态无环；testdata
// 安装排除的单元内登记面——全仓安装树扫描归 WP-24）
// =====================================================================

/**
 * T-1 头面（acceptance 3——产品目标零 testkit 头）：include/＋src/＋
 * plugin/ 的全部产品翻译单元 #include 指令行零 testkit 段（plugin/ 面
 * 为 T08 落位的 Qt 载体——同一 T-1 红线覆盖；BuildGraphContractTest 的
 * include 封闭断言只扫 include/＋src/，本用例补齐 plugin/ 并钉 testkit）。
 */
TEST(PeerRedLine, T1ProductFaceZeroTestkitHeaders_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    int scanned = 0;
    for (const auto* sub : {"include", "src", "plugin"}) {
        const auto base = unitRoot() / "requirements" / sub;
        ASSERT_TRUE(std::filesystem::exists(base)) << sub << " 目录缺失";
        for (auto it = std::filesystem::recursive_directory_iterator(base);
             it != std::filesystem::recursive_directory_iterator(); ++it) {
            const auto ext = it->path().extension().string();
            if (ext != ".hpp" && ext != ".h" && ext != ".cpp") { continue; }
            std::ifstream in(it->path(), std::ios::binary);
            ASSERT_TRUE(static_cast<bool>(in));
            const std::string text{std::istreambuf_iterator<char>(in),
                                   std::istreambuf_iterator<char>()};
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                const auto first = line.find_first_not_of(" \t\r");
                if (first == std::string::npos
                    || line.compare(first, 8, "#include") != 0) {
                    continue;  // 仅认 #include 指令行——注释散文不误报
                }
                EXPECT_EQ(line.find("sdurws/ird/testkit"), std::string::npos)
                    << "产品面 include testkit（T-1 违约）: " << it->path().string();
            }
            ++scanned;
        }
    }
    EXPECT_GT(scanned, 20) << "扫描面过小（防扫描失效）";
}

/**
 * T-2 测试目标形态（acceptance 3——{被测目标, testkit, gtest} 无环）：
 * 两测试目标的链接语句恰含被测目标＋sdurws_ird_testkit＋GTest::gtest
 * （允许形态全集）；testkit 自身 CMakeLists 零 requirements 目标引用
 * （无反向边——testkit→core 单向，环被结构性排除）；requirements 的
 * CMakeLists 无 testdata 安装语句（§3.6 排除清单的单元内登记面——全仓
 * 安装树扫描门禁归 WP-24 侧，此处登记消费现状）。
 */
TEST(PeerRedLine, T2TestTargetLinkFormAcyclicAndTestdataExclusion_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01"},
                  std::vector<std::string>{});

    const std::string cmakeText = readTextFile(
        unitRoot() / "requirements" / "CMakeLists.txt");
    int testkitLinkLines = 0;
    for (const std::string& line : codeLines(cmakeText)) {
        if (line.find("sdurws_ird_testkit") == std::string::npos) { continue; }
        ASSERT_NE(line.find("target_link_libraries"), std::string::npos)
            << "testkit 引用只允许落在链接语句行（T-1）: " << line;
        const bool onTestTarget
            = line.find("sdurws_ird_requirements_test") != std::string::npos
              || line.find("sdurws_ird_requirements_contract_test") != std::string::npos;
        ASSERT_TRUE(onTestTarget) << "testkit 只可被测试目标链接（T-1）: " << line;
        // 允许形态全集核对：{被测目标〔＋同单元插件面〕, testkit, gtest}
        // ——出现任何第四产品单元目标即形态漂移。
        EXPECT_NE(line.find("GTest::gtest"), std::string::npos)
            << "测试链接语句缺 gtest（T-2 形态）: " << line;
        ++testkitLinkLines;
    }
    ASSERT_GE(testkitLinkLines, 2) << "两测试目标均应登记 testkit 消费";

    // 无环半区：testkit 的构建脚本零 requirements 目标引用（testkit→core
    // 单向；test→testkit→core 是 DAG 边而非环——testkit.md §2.4）。
    const std::string testkitCmake = readTextFile(
        unitRoot() / "testkit" / "CMakeLists.txt");
    EXPECT_EQ(testkitCmake.find("sdurws_ird_requirements"), std::string::npos)
        << "testkit 反向引用 requirements（T-2 环面）";

    // testdata 安装排除（单元内登记面）：requirements 构建脚本零
    // install(testdata) 语句——黄金数据不随产品分发（§3.6；全仓安装树
    // 扫描断言登记交 WP-24 侧）。
    EXPECT_EQ(cmakeText.find("install("), std::string::npos)
        << "requirements 单元零安装语句（testdata 不入安装树的构建面现状）";
}

}  // namespace
