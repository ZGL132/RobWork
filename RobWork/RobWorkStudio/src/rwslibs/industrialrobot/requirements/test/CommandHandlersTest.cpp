/**
 * @file   CommandHandlersTest.cpp
 * @brief  需求命令处理器族用例组（ReqReqCommands）——O-35 无点 token、
 *         载荷编解码往返、prepare 管线（首次应用/增量替换/快照逆放）、
 *         就绪现场重估（Blocking 拒绝＋逐项定位＋汇总诊断/Warning 随
 *         计划留痕）、导入溯源完整性断言与防御性复核（任务契约
 *         WP-14-T05 acceptance 6 的逐条具名自证）。
 *
 * 设计依据：
 *   - units/requirements.md §9.1（命令清单表/prepare 管线图——对错基准）、
 *     §9.5（IRequirementCommandHandler 契约）、§4.8（I-REQ-8）、§4.1
 *     （恰一根/每集合至多一份）
 *   - units/project.md §5.3（HandlerContext 冻结签名——测试直接构造；
 *     HandlerRegistry 行为冻结表）、§6.9（快照式逆命令）
 *   - modeling/test/CommandFixtures.hpp（TestQueryPort 内存替身先例——
 *     project ②端口的数据形状一致；本文件内嵌同款精简替身，不入公共面）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 6（O-35 处置）
 *
 * 断言纪律：prepare 经 HandlerContext 真实调用（桩端口仅替换取数面）；
 * inverse 往返以"逆载荷解码→Restore 槽字节==基线原字节"机械比对。
 */

#include <sdurws/ird/requirements/CommandHandlers.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/requirements/Codec.hpp>
#include <sdurws/ird/requirements/ObjectTypes.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <rw/math/Vector3D.hpp>

using namespace sdurws::ird;

namespace {

using requirements::ApplyRequirementImportHandler;
using requirements::ApplyRequirementSetHandler;
using requirements::RequirementCommandPayload;
using requirements::RequirementPayloadSlot;
using requirements::RequirementWorkingSet;
using requirements::TaskPoint;
using requirements::WorkRegion;

// ---------------------------------------------------------------------
// project ②端口内存替身（modeling CommandFixtures 同款精简——数据形状
// 与 project 公共契约一致；单线程用例内使用）
// ---------------------------------------------------------------------

/// 非全零内容版本（对象库编址键——替身不复核摘要真实性）。
core::ContentVersion testContentVersion(std::uint8_t tag)
{
    core::ContentVersion cv;
    cv.bytes[0] = tag;
    return cv;
}

class TestQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view;  ///< head()/revision()/tryRevision 应答值
    /// 对象字节表（键＝"<oid 规范文本>|<cv 规范文本>"）。
    std::map<std::string, std::vector<std::uint8_t>> objects;

    /// 登记一个闭包对象（引用进 view.objectRefs＋字节入表——一次装配）。
    void addObject(const core::ObjectId& oid, std::string token,
                   const std::vector<std::uint8_t>& bytes)
    {
        const core::ContentVersion cv = testContentVersion(
            static_cast<std::uint8_t>(view.objectRefs.size() + 1U));
        project::ObjectRef ref;
        ref.objectId = oid;
        ref.contentVersion = cv;
        ref.objectTypeToken = std::move(token);
        ref.digest256 = std::string(64, '0');
        view.objectRefs.push_back(std::move(ref));
        objects[oid.toCanonical() + "|" + cv.toCanonical()] = bytes;
    }

    [[nodiscard]] project::RevisionView head() const override { return view; }
    [[nodiscard]] std::optional<project::RevisionView> tryRevision(
        core::RevisionId id) const override
    {
        if (id == view.id) { return view; }
        return std::nullopt;
    }
    [[nodiscard]] project::RevisionView revision(core::RevisionId) const override
    {
        return view;
    }
    [[nodiscard]] project::ProjectMetadataView currentMetadata() const override
    {
        return {};
    }
    [[nodiscard]] std::optional<project::ProjectMetadataView> metadataAt(
        core::RevisionId) const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<project::BranchTip> branchTips() const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RevisionView> branchHistory(
        core::BranchId, std::uint32_t) const override
    {
        return {};
    }
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> tryObject(
        core::ObjectId oid, core::ContentVersion cv) const noexcept override
    {
        auto it = objects.find(oid.toCanonical() + "|" + cv.toCanonical());
        if (it == objects.end()) { return std::nullopt; }
        return it->second;
    }
    [[nodiscard]] std::vector<std::uint8_t> object(core::ObjectId oid,
                                                   core::ContentVersion cv) const override
    {
        auto bytes = tryObject(oid, cv);
        if (!bytes.has_value()) {
            throw std::logic_error("fixture: 对象字节缺失（引用断裂）");
        }
        return *bytes;
    }
    [[nodiscard]] std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        return {};
    }
    [[nodiscard]] std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        return {};
    }
    [[nodiscard]] std::filesystem::path runDir(core::RunId) const override
    {
        return {};
    }
};

// ---------------------------------------------------------------------
// 值模型构造辅助（与 ReadinessTest 同款——合法实例，过 Codec 解码门）
// ---------------------------------------------------------------------

core::ObjectId makeOid()
{
    return core::ObjectId::generate();
}

core::ValueProvenance userProvenance()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 合法任务点（World 系/约束 Z/有限位置/默认容差）。
TaskPoint makeHealthyPoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = makeOid();
    p.name = name;
    p.level = requirements::RequirementLevel::Must;
    p.enabled = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.1, 0.2, 0.3), userProvenance());
    p.pose.constrainedDof.z = true;
    p.work.enabled = true;
    p.work.distanceM = 1.0;  // m
    return p;
}

/// 导入溯源（摘要非全零＋行号 1——I-REQ-8 完整面）。
requirements::ImportProvenance makeImportProvenance(std::uint8_t tag)
{
    requirements::ImportProvenance ip;
    ip.sourceDigest[0] = tag;
    ip.sourceDigest[31] = tag;
    ip.recordNumber = 1;
    return ip;
}

/// 健康点集对象（单条任务点——canonical 编码就绪）。
requirements::PointSet makeHealthyPointSet()
{
    requirements::PointSet ps;
    ps.entries.push_back(makeHealthyPoint("P1"));
    requirements::sortEntriesByObjectId(ps.entries);
    return ps;
}

/// 健康工况集对象（单条 enabled Must 工况——必验集合非空）。
requirements::ConditionSet makeHealthyConditionSet()
{
    requirements::ConditionSet cs;
    requirements::OperatingCondition c;
    c.objectId = makeOid();
    c.name = "C1";
    c.level = requirements::RequirementLevel::Must;
    c.enabled = true;
    cs.entries.push_back(c);
    requirements::sortEntriesByObjectId(cs.entries);
    return cs;
}

/// 五对象 canonical 字节（RequirementCodec 同源编码——载荷槽/基线登记）。
std::vector<std::uint8_t> encodeObject(const requirements::RequirementObjectVariant& v)
{
    const requirements::RequirementCodec codec;
    auto encoded = codec.encode(v, requirements::kCurrentRequirementFormatVersion);
    if (!encoded.ok()) {
        throw std::logic_error("fixture: 健康对象编码失败: " + encoded.error().detail);
    }
    return encoded.get();
}

/// 首应用载荷（根＋点集＋工况集全 allocateNew——根字节引用槽留空，挂载
/// 增量由 prepare 落入候选根重编码）。
RequirementCommandPayload makeFirstApplyPayload()
{
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    requirements::RequirementSet root;
    root.name = "T05 需求集";
    payload.objects.push_back(RequirementPayloadSlot{true, core::ObjectId{},
                                                     std::string(requirements::kReqSetObjectType),
                                                     encodeObject(requirements::RequirementObjectVariant(root))});
    payload.objects.push_back(RequirementPayloadSlot{true, core::ObjectId{},
                                                     std::string(requirements::kReqPointSetObjectType),
                                                     encodeObject(requirements::RequirementObjectVariant(makeHealthyPointSet()))});
    payload.objects.push_back(RequirementPayloadSlot{true, core::ObjectId{},
                                                     std::string(requirements::kReqConditionSetObjectType),
                                                     encodeObject(requirements::RequirementObjectVariant(makeHealthyConditionSet()))});
    return payload;
}

/// 便捷封装：以桩端口为基线执行 prepare。
project::PrepareOutcome runPrepare(project::ICommandHandler& handler,
                                   TestQueryPort& port,
                                   const RequirementCommandPayload& payload,
                                   project::CommandPlan& out,
                                   std::vector<core::DiagnosticRecord>& diags)
{
    project::CommandEnvelope envelope;
    envelope.commandType = handler.commandType();
    envelope.payloadFormatVersion = requirements::kRequirementCommandPayloadVersion;
    envelope.payloadCanonical = requirements::encodeRequirementCommandPayload(payload);
    project::HandlerContext ctx(port, nullptr, nullptr);
    return handler.prepare(ctx, envelope, port.view, out, diags);
}

/// prepare 拒绝态的 diags 中是否含指定稳定码。
bool hasDiagCode(const std::vector<core::DiagnosticRecord>& diags, const char* code)
{
    for (const auto& d : diags) {
        if (d.code == code) { return true; }
    }
    return false;
}

}  // namespace

// =====================================================================
// acceptance 6——命令族（O-35 处置：无点 token＝服从 §4.4.4 冻结语法）
// =====================================================================

/**
 * 命令 token 无点形态（ACC6/O-35）：两处理器 commandType 与 §9.1 表行
 * 原文逐字一致（无点）；服从 project §4.4.4 冻结语法 ^[a-z0-9-]{3,64}；
 * 真实 HandlerRegistry 注册后可 find 命中且清单含两 token（L5 装配面）。
 */
TEST(ReqReqCommands, CommandTokensArePointFreeAndRegisterable_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"},
                  std::vector<std::string>{});

    const ApplyRequirementSetHandler setHandler;
    const ApplyRequirementImportHandler importHandler;
    EXPECT_EQ(setHandler.commandType(), "apply-requirement-set");
    EXPECT_EQ(importHandler.commandType(), "apply-requirement-import");
    EXPECT_EQ(setHandler.currentPayloadVersion(),
              requirements::kRequirementCommandPayloadVersion);

    // token 语法（project §4.4.4 ^[a-z0-9-]{3,64}——无点形态的机器面）。
    for (const std::string& token :
         {setHandler.commandType(), importHandler.commandType()}) {
        EXPECT_GE(token.size(), 3U) << token;
        EXPECT_LE(token.size(), 64U) << token;
        for (const char ch : token) {
            ASSERT_TRUE((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
                        || ch == '-')
                << "token 含冻结语法外字符: " << token;
        }
    }

    // 真实注册表注册＋find 命中（L5 装配行为面）。
    project::HandlerRegistry registry;
    requirements::registerRequirementCommandHandlers(registry);
    EXPECT_NE(registry.find("apply-requirement-set"), nullptr);
    EXPECT_NE(registry.find("apply-requirement-import"), nullptr);
    const auto tokens = registry.registeredCommandTypes();
    EXPECT_EQ(tokens.size(), 2U);
    // 重复注册边界拒绝（project §5.3.5——装配错误 fail-fast）。
    EXPECT_THROW(requirements::registerRequirementCommandHandlers(registry),
                 std::invalid_argument);
}

/**
 * 载荷编解码往返与破损拒绝（ACC6——decode 段机器面）：encode→tryDecode
 * 恒等往返；magic 破损/截断/版本不受理/尾随字节/词表外 token→nullopt
 * 不猜测（NFR-DEP-04/COR-02）。
 */
TEST(ReqReqCommands, PayloadRoundtripAndDamageRejection_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "NFR-DEP-04"},
                  std::vector<std::string>{});

    const RequirementCommandPayload payload = makeFirstApplyPayload();
    const std::vector<std::uint8_t> bytes =
        requirements::encodeRequirementCommandPayload(payload);
    const auto decoded = requirements::tryDecodeRequirementCommandPayload(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(*decoded == payload) << "encode→tryDecode 恒等往返";

    // 确定性：同载荷重复编码逐字节相等。
    EXPECT_EQ(requirements::encodeRequirementCommandPayload(payload), bytes);

    // 破损面：magic 首字节破坏。
    auto broken = bytes;
    broken[0] = 'X';
    EXPECT_FALSE(requirements::tryDecodeRequirementCommandPayload(broken).has_value());
    // 截断。
    EXPECT_FALSE(requirements::tryDecodeRequirementCommandPayload(
                     std::vector<std::uint8_t>(bytes.begin(), bytes.end() - 1))
                     .has_value());
    // 尾随字节。
    auto trailing = bytes;
    trailing.push_back(0xAB);
    EXPECT_FALSE(requirements::tryDecodeRequirementCommandPayload(trailing).has_value());

    // 版本不受理（NFR-DEP-04）：帧内版本字段改写为 99。
    auto wrongVersion = bytes;
    const std::size_t versionOffset = 9;  // magic(8) 后第一个 u32
    wrongVersion[versionOffset] = 99;
    EXPECT_FALSE(
        requirements::tryDecodeRequirementCommandPayload(wrongVersion).has_value());
}

/**
 * 首次应用（ACC6——prepare 管线正例）：空基线闭包＋根/点集/工况集全
 * allocateNew→Planned；写入恰三对象；confirmableFindings 恒空（SA-15
 * 不私设）；requiresDualCompile=false（零编译影响面）；首次应用无前版
 * ＝inverse 不可逆声明 nullopt（§6.9 空历史语义）；摘要为非空中文。
 */
TEST(ReqReqCommands, FirstApplicationPlanned_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "ARC-01"},
                  std::vector<std::string>{});

    TestQueryPort port;  // 空闭包（首应用）
    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    const project::PrepareOutcome outcome =
        runPrepare(handler, port, makeFirstApplyPayload(), plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.objectWrites.size(), 3U) << "根＋点集＋工况集恰三对象";
    // token 路由面：恰一根。
    std::size_t roots = 0;
    for (const auto& w : plan.objectWrites) {
        if (w.objectTypeToken == std::string(requirements::kReqSetObjectType)) {
            ++roots;
            ASSERT_TRUE(w.objectId.has_value());
            EXPECT_TRUE(w.objectId->isValid()) << "新根已经 ctx 取号（PA-1）";
        }
    }
    EXPECT_EQ(roots, 1U);
    EXPECT_TRUE(plan.confirmableFindings.empty())
        << "confirmableFindings 恒空（§9.1——SA-15 流不私设）";
    EXPECT_FALSE(plan.requiresDualCompile)
        << "需求对象不进 WorkCell 描述——零编译影响面";
    EXPECT_FALSE(plan.inverseCommandType.has_value())
        << "首次应用无前版＝不可逆声明（§6.9 空历史）";
    EXPECT_FALSE(plan.summary.empty());
    EXPECT_TRUE(plan.summary.find("应用需求集") != std::string::npos)
        << "命令摘要为中文（PM-04 人读面）";
}

/**
 * 增量替换＋快照逆命令（ACC6）：基线（根＋点集＋工况集）之上显式替换
 * 点集字节→Planned；inverse 在场且逆载荷解码为 Restore 模式、槽字节与
 * 基线原字节逐字节一致（受影响对象前版字节——D-MDL-9 同源口径）。
 */
TEST(ReqReqCommands, IncrementalReplaceProducesSnapshotInverse_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "PA-2"},
                  std::vector<std::string>{});

    // ---- 基线装配：根＋点集＋工况集（健康）登记进桩端口 ----
    TestQueryPort port;
    const requirements::PointSet baselinePoints = makeHealthyPointSet();
    const requirements::ConditionSet baselineConditions = makeHealthyConditionSet();
    const std::vector<std::uint8_t> pointBytes =
        encodeObject(requirements::RequirementObjectVariant(baselinePoints));
    const std::vector<std::uint8_t> conditionBytes =
        encodeObject(requirements::RequirementObjectVariant(baselineConditions));
    const core::ObjectId rootOid = makeOid();
    const core::ObjectId pointSetOid = makeOid();
    const core::ObjectId conditionSetOid = makeOid();
    requirements::RequirementSet root;
    root.name = "基线需求集";
    root.pointSetRef = pointSetOid;
    root.conditionSetRef = conditionSetOid;
    const std::vector<std::uint8_t> rootBytes =
        encodeObject(requirements::RequirementObjectVariant(root));
    port.addObject(rootOid, std::string(requirements::kReqSetObjectType), rootBytes);
    port.addObject(pointSetOid, std::string(requirements::kReqPointSetObjectType),
                   pointBytes);
    port.addObject(conditionSetOid,
                   std::string(requirements::kReqConditionSetObjectType), conditionBytes);

    // ---- 载荷：根显式（引用不变）＋点集显式替换（增一条任务点）----
    requirements::PointSet updated = baselinePoints;
    TaskPoint p2 = makeHealthyPoint("P2");
    updated.entries.push_back(p2);
    requirements::sortEntriesByObjectId(updated.entries);
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    payload.objects.push_back(RequirementPayloadSlot{
        false, rootOid, std::string(requirements::kReqSetObjectType),
        encodeObject(requirements::RequirementObjectVariant(root))});
    payload.objects.push_back(RequirementPayloadSlot{
        false, pointSetOid, std::string(requirements::kReqPointSetObjectType),
        encodeObject(requirements::RequirementObjectVariant(updated))});

    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);

    // 写入面：根＋点集两对象（工况集未涉及不写）。
    ASSERT_EQ(plan.objectWrites.size(), 2U);

    // 逆命令：在场、同 token、Restore 模式、槽字节==基线原字节。
    ASSERT_TRUE(plan.inverseCommandType.has_value());
    EXPECT_EQ(*plan.inverseCommandType, "apply-requirement-set");
    ASSERT_TRUE(plan.inversePayloadCanonical.has_value());
    const auto inverse = requirements::tryDecodeRequirementCommandPayload(
        *plan.inversePayloadCanonical);
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(inverse->mode, RequirementCommandPayload::Mode::Restore);
    ASSERT_EQ(inverse->objects.size(), 2U);
    std::map<std::string, std::vector<std::uint8_t>> inverseBytes;
    for (const auto& slot : inverse->objects) {
        EXPECT_FALSE(slot.allocateNew);
        inverseBytes[slot.objectTypeToken] = slot.objectBytes;
    }
    EXPECT_EQ(inverseBytes[std::string(requirements::kReqPointSetObjectType)], pointBytes)
        << "逆槽字节＝受影响对象前一版本 canonical 字节（位级保真）";
    EXPECT_EQ(inverseBytes[std::string(requirements::kReqSetObjectType)], rootBytes)
        << "根逆槽字节＝基线根字节";
}

/**
 * 就绪 Blocking 拒绝（ACC6——§9.1 断言列）：候选带悬空工况绑定→
 * RejectedHardAssert＋逐项定位诊断（REQ-READY-REF-MISSING）＋汇总诊断
 * （REQ-READY-INPUT-INCOMPLETE）；拒绝态计划被清空（零修订——ARC-01）。
 */
TEST(ReqReqCommands, ReadinessBlockingRejectsPrepare_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "ARC-01"},
                  std::vector<std::string>{});

    TestQueryPort port;  // 空闭包（首应用路径——候选整体进入就绪重估）
    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    // 载荷：工况集带悬空 appliesTo 绑定（指向不存在的任务点条目）。
    RequirementCommandPayload payload = makeFirstApplyPayload();
    requirements::ConditionSet bad;
    requirements::OperatingCondition c;
    c.objectId = makeOid();
    c.name = "C-BAD";
    c.enabled = true;
    c.appliesTo.scope = requirements::AppliesToScope::Stations;
    c.appliesTo.stations = {makeOid()};  // 悬空
    bad.entries.push_back(c);
    requirements::sortEntriesByObjectId(bad.entries);
    for (auto& slot : payload.objects) {
        if (slot.objectTypeToken == std::string(requirements::kReqConditionSetObjectType)) {
            slot.objectBytes =
                encodeObject(requirements::RequirementObjectVariant(bad));
        }
    }

    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
    EXPECT_TRUE(plan.objectWrites.empty()) << "拒绝态计划不可消费（零修订）";
    EXPECT_TRUE(hasDiagCode(diags, "REQ-READY-REF-MISSING"))
        << "逐项定位诊断（R4 悬空绑定）";
    EXPECT_TRUE(hasDiagCode(diags, "REQ-READY-INPUT-INCOMPLETE"))
        << "汇总诊断（ReadinessSummary.valid=false 投影面）";
}


/**
 * Warning 不阻断（ACC6/§8.1 级别语义）：候选必验集合为空（唯一工况
 * 停用）→R5 Warning 随 diags 留痕、计划照常产出（Planned）。
 */
TEST(ReqReqCommands, ReadinessWarningDoesNotBlock_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"},
                  std::vector<std::string>{});

    TestQueryPort port;
    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    // 载荷：工况集唯一工况停用（必验集合空——R5 Warning）。
    RequirementCommandPayload payload = makeFirstApplyPayload();
    requirements::ConditionSet disabled;
    requirements::OperatingCondition c;
    c.objectId = makeOid();
    c.name = "C-OFF";
    c.enabled = false;  // 停用→必验集合空
    disabled.entries.push_back(c);
    requirements::sortEntriesByObjectId(disabled.entries);
    for (auto& slot : payload.objects) {
        if (slot.objectTypeToken == std::string(requirements::kReqConditionSetObjectType)) {
            slot.objectBytes =
                encodeObject(requirements::RequirementObjectVariant(disabled));
        }
    }

    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::Planned) << "Warning 不阻断应用";
    EXPECT_TRUE(hasDiagCode(diags, "REQ-READY-NO-REQUIRED-CASE"))
        << "Warning 随计划留痕";
    EXPECT_FALSE(plan.objectWrites.empty());
}

/**
 * 导入溯源完整性断言（ACC6——§9.1 表行 2"同上＋导入溯源完整性"）：
 * import 处理器对无溯源条目拒绝（RejectedInvalidInput）；溯源完整
 * （摘要非全零＋行号≥1）则放行。
 */
TEST(ReqReqCommands, ImportRequiresProvenanceCompleteness_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "I-REQ-08"},
                  std::vector<std::string>{});

    TestQueryPort port;
    ApplyRequirementImportHandler handler;

    // 反例：点集条目无导入溯源。
    RequirementCommandPayload bad = makeFirstApplyPayload();
    for (auto& slot : bad.objects) {
        if (slot.objectTypeToken == std::string(requirements::kReqPointSetObjectType)) {
            slot.objectBytes =
                encodeObject(requirements::RequirementObjectVariant(makeHealthyPointSet()));
        }
    }
    {
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const project::PrepareOutcome outcome = runPrepare(handler, port, bad, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput)
            << "导入批次条目须携带完整溯源（I-REQ-8）";
    }

    // 正例：溯源完整（点集条目携摘要非全零＋recordNumber=1；工况条目
    // 无溯源字段——§4.5，不在断言面）。
    requirements::PointSet imported;
    TaskPoint p = makeHealthyPoint("P-IMPORT");
    p.importProvenance = makeImportProvenance(7);
    imported.entries.push_back(p);
    requirements::sortEntriesByObjectId(imported.entries);
    RequirementCommandPayload good = makeFirstApplyPayload();
    for (auto& slot : good.objects) {
        if (slot.objectTypeToken == std::string(requirements::kReqPointSetObjectType)) {
            slot.objectBytes =
                encodeObject(requirements::RequirementObjectVariant(imported));
        }
    }
    {
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const project::PrepareOutcome outcome = runPrepare(handler, port, good, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::Planned);
    }
}

/**
 * 快照逆放路径（ACC6——§6.9 同源）：Restore 模式载荷（全部显式身份）
 * →Planned；写入字节与载荷槽字节逐字节一致（位级保真——不重编码）。
 */
TEST(ReqReqCommands, RestorePathWritesBytesVerbatim_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "PA-2"},
                  std::vector<std::string>{});

    // ---- 基线装配：根＋点集 ----
    TestQueryPort port;
    const requirements::PointSet baselinePoints = makeHealthyPointSet();
    const std::vector<std::uint8_t> pointBytes =
        encodeObject(requirements::RequirementObjectVariant(baselinePoints));
    const core::ObjectId rootOid = makeOid();
    const core::ObjectId pointSetOid = makeOid();
    requirements::RequirementSet root;
    root.name = "基线需求集";
    root.pointSetRef = pointSetOid;
    const std::vector<std::uint8_t> rootBytes =
        encodeObject(requirements::RequirementObjectVariant(root));
    port.addObject(rootOid, std::string(requirements::kReqSetObjectType), rootBytes);
    port.addObject(pointSetOid, std::string(requirements::kReqPointSetObjectType),
                   pointBytes);

    // ---- Restore 载荷：根＋点集显式回放 ----
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Restore;
    payload.objects.push_back(RequirementPayloadSlot{
        false, rootOid, std::string(requirements::kReqSetObjectType), rootBytes});
    payload.objects.push_back(RequirementPayloadSlot{
        false, pointSetOid, std::string(requirements::kReqPointSetObjectType), pointBytes});

    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    ASSERT_EQ(plan.objectWrites.size(), 2U);
    for (const auto& w : plan.objectWrites) {
        if (w.objectTypeToken == std::string(requirements::kReqPointSetObjectType)) {
            EXPECT_EQ(w.payloadCanonical, pointBytes) << "逆放字节位级保真";
        }
    }
}

/**
 * 防御性复核与形状违约拒绝（ACC6 防线纵深）：expectedRevision 与基线
 * 不一致＝调用方契约违约 fail-fast；根 allocateNew 而闭包已有根＝无效
 * 载荷；显式集合槽挂载失配＝无效载荷。
 */
TEST(ReqReqCommands, DefensiveAndShapeRejections_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "ARC-01"},
                  std::vector<std::string>{});

    // ---- ① 防御性复核：expectedRevision≠基线（S2 已拦——纵深断言）----
    TestQueryPort port;
    ApplyRequirementSetHandler handler;
    {
        project::CommandEnvelope envelope;
        envelope.commandType = handler.commandType();
        envelope.payloadFormatVersion =
            requirements::kRequirementCommandPayloadVersion;
        envelope.payloadCanonical =
            requirements::encodeRequirementCommandPayload(makeFirstApplyPayload());
        envelope.expectedRevision = core::RevisionId::generate();  // 与桩视图 id（缺省值）不同
        project::HandlerContext ctx(port, nullptr, nullptr);
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        EXPECT_THROW(handler.prepare(ctx, envelope, port.view, plan, diags),
                     std::invalid_argument);
    }

    // ---- ② 闭包已有根而载荷申请建根（恰一根违约）＝无效载荷 ----
    const requirements::PointSet points = makeHealthyPointSet();
    const std::vector<std::uint8_t> pointBytes =
        encodeObject(requirements::RequirementObjectVariant(points));
    const core::ObjectId rootOid = makeOid();
    const core::ObjectId pointSetOid = makeOid();
    requirements::RequirementSet root;
    root.name = "基线需求集";
    root.pointSetRef = pointSetOid;
    port.addObject(rootOid, std::string(requirements::kReqSetObjectType),
                   encodeObject(requirements::RequirementObjectVariant(root)));
    port.addObject(pointSetOid, std::string(requirements::kReqPointSetObjectType),
                   pointBytes);
    {
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const project::PrepareOutcome outcome =
            runPrepare(handler, port, makeFirstApplyPayload(), plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput)
            << "闭包含根时根槽 allocateNew＝恰一根违约";
    }

    // ---- ③ 显式集合槽挂载失配（基线挂载的是别的对象）＝无效载荷 ----
    RequirementCommandPayload mismatch;
    mismatch.mode = RequirementCommandPayload::Mode::Apply;
    mismatch.objects.push_back(RequirementPayloadSlot{
        false, rootOid, std::string(requirements::kReqSetObjectType),
        encodeObject(requirements::RequirementObjectVariant(root))});
    mismatch.objects.push_back(RequirementPayloadSlot{
        false, makeOid(),  // 未挂载的对象身份
        std::string(requirements::kReqPointSetObjectType), pointBytes});
    {
        project::CommandPlan plan;
        std::vector<core::DiagnosticRecord> diags;
        const project::PrepareOutcome outcome =
            runPrepare(handler, port, mismatch, plan, diags);
        EXPECT_EQ(outcome, project::PrepareOutcome::RejectedInvalidInput)
            << "显式集合槽须恰为基线根引用表已挂载对象（引用稳定）";
    }
}
/**
 * 就绪现场重估（ACC6——"就绪 R0~R9 现场重估（防基线漂移）"）：基线
 * 健康，载荷替换的工况集绑定悬空（指向点集中不存在的条目）——以**提
 * 交时刻候选**整体重估拦截（RejectedHardAssert＋REQ-READY-REF-MISSING），
 * 而非以基线就绪态放行（O-39 同源语义）。
 *
 * 注：候选值级违例（如退化盒）由载荷解码门先拒（decode 校验链强制
 * I-REQ 不变量——非法条目不可经字节进入候选）；现场重估拦截的是解码
 * 门之外**跨对象/跨集合事实**（引用悬空/绑定失配/必验空集等）——两道
 * 防线分工见 §9.1/§8.1。
 */
TEST(ReqReqCommands, CandidateReevaluatedAgainstBaselineDrift_ACC6)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "NFR-MNT-04"},
                  std::vector<std::string>{});

    // ---- 基线装配：根＋点集＋工况集（全部健康——基线就绪）----
    TestQueryPort port;
    const requirements::PointSet baselinePoints = makeHealthyPointSet();
    const requirements::ConditionSet baselineConditions = makeHealthyConditionSet();
    const std::vector<std::uint8_t> pointBytes =
        encodeObject(requirements::RequirementObjectVariant(baselinePoints));
    const core::ObjectId rootOid = makeOid();
    const core::ObjectId pointSetOid = makeOid();
    const core::ObjectId conditionSetOid = makeOid();
    requirements::RequirementSet root;
    root.name = "基线需求集";
    root.pointSetRef = pointSetOid;
    root.conditionSetRef = conditionSetOid;
    port.addObject(rootOid, std::string(requirements::kReqSetObjectType),
                   encodeObject(requirements::RequirementObjectVariant(root)));
    port.addObject(pointSetOid, std::string(requirements::kReqPointSetObjectType),
                   pointBytes);
    port.addObject(conditionSetOid,
                   std::string(requirements::kReqConditionSetObjectType),
                   encodeObject(requirements::RequirementObjectVariant(baselineConditions)));

    // ---- 载荷：根显式（引用不变）＋工况集显式替换为悬空绑定版本 ----
    requirements::ConditionSet drifted;
    requirements::OperatingCondition c;
    c.objectId = makeOid();
    c.name = "C-DRIFT";
    c.enabled = true;
    c.appliesTo.scope = requirements::AppliesToScope::Stations;
    c.appliesTo.stations = {makeOid()};  // 基线点集中不存在的条目——候选态悬空
    drifted.entries.push_back(c);
    requirements::sortEntriesByObjectId(drifted.entries);
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    payload.objects.push_back(RequirementPayloadSlot{
        false, rootOid, std::string(requirements::kReqSetObjectType),
        encodeObject(requirements::RequirementObjectVariant(root))});
    payload.objects.push_back(RequirementPayloadSlot{
        false, conditionSetOid, std::string(requirements::kReqConditionSetObjectType),
        encodeObject(requirements::RequirementObjectVariant(drifted))});

    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert)
        << "基线健康而候选悬空——现场重估以候选整体拦截（防基线漂移）";
    EXPECT_TRUE(hasDiagCode(diags, "REQ-READY-REF-MISSING"));
}
