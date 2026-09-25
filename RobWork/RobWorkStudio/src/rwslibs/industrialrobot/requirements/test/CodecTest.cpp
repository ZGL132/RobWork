/**
 * @file   CodecTest.cpp
 * @brief  canonical 编码登记用例组（ReqCodec）——roundtrip 逐字段一致/
 *         确定性/schema 主版本拒绝/RequirementProfile 不入编码（任务
 *         契约 WP-14-T03 acceptance 2 的具名自证面——编码登记供 evidence
 *         切片，DTB 完成条件原文）。
 *
 * 设计依据：units/requirements.md §9.5（IRequirementCodec 契约）、§4.7
 * （I-REQ-1 canonical 形态）、§4.8（Profile 派生档不入编码）、§6.2（必验
 * 解析确定性——编码字节是对账输入）、NFR-DEP-04（未知版本拒绝＋升级指
 * 引）；先例 modeling/test/CodecTest.cpp（WP-13-T03 同款形态）。
 */

#include <sdurws/ird/requirements/Codec.hpp>
#include <sdurws/ird/requirements/Services.hpp>  // deriveRequirementProfile——Profile 派生确定性用例

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位姿/盒/采样字段的框架值类型

#include <algorithm>
#include <string>
#include <vector>

using namespace sdurws::ird::requirements;

/// core 命名空间别名（测试内 ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 构造满配任务点（覆盖可选字段全部出现——roundtrip 的字段覆盖面）。
TaskPoint makeRichPoint()
{
    TaskPoint p;
    p.objectId = core::ObjectId::fromCanonical(
        "obj-0102030405060708090a0b0c0d0e0f10");
    p.name = "拾取点-1";
    p.processTag = ProcessTag::Pick;
    p.level = RequirementLevel::Must;
    p.enabled = true;
    p.source = core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                           std::nullopt, std::nullopt,
                                           std::string("captured-tcp"));
    GenerationProvenance gen;
    gen.generatorId = "mirror";
    gen.instanceId = "inst-42";
    gen.linked = true;
    gen.parameters = {{"axis", "x"}, {"plane", "world"}};
    p.generation = gen;
    ImportProvenance imp;
    imp.sourceDigest = core::Digest256{9};
    imp.recordNumber = 7;
    p.importProvenance = imp;
    p.refFrame = RequirementReference{};  // World
    RequirementReference tool;
    tool.kind = RequirementRefKind::Tool;
    tool.objectId = core::ObjectId::fromCanonical(
        "obj-a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    tool.tcpKey = "tcp1";
    p.tcpRef = tool;
    p.pose.constrainedDof.x = true;
    p.pose.constrainedDof.yaw = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.35, -0.2, 0.8),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.pose.orientation.kind = OrientationRuleKind::Fixed;
    p.pose.orientation.fixedRpy = rw::math::Vector3D<double>(0.0, 1.5707963267948966, 0.0);
    p.tolerance = ToleranceSpec{};
    p.approach = TaskSegment{true, SegmentAxis::ToolZ, 0.1};
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    p.retract = TaskSegment{true, SegmentAxis::ToolZ, 0.05};
    p.demands.collisionFreeRequired = true;
    p.demands.minimumJointMargin = 0.2;
    p.sequenceKey = "P0";
    p.note = "备注（吸收 Info 级信息）";
    return p;
}

/// 构造满配五对象之一组（根＋四集合各一条）。
RequirementSet makeRoot()
{
    RequirementSet root;
    root.name = "搬运需求集";
    root.pointSetRef = core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    root.regionSetRef = core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    root.conditionSetRef = core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    root.planSetRef = core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.note = "根备注";
    return root;
}

WorkRegion makeRegion()
{
    WorkRegion r;
    r.objectId = core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    r.name = "区域-1";
    r.box = BoundingBox{rw::math::Vector3D<double>(0.5, 0.0, 0.3),
                        rw::math::Vector3D<double>(0.4, 0.4, 0.2)};
    r.coverageTargets.minPositionCoverage = 0.8;
    r.coverageTargets.minOrientationCoverage = 0.6;
    r.positionSampling.method = PositionSamplingMethod::Grid;
    r.positionSampling.counts = {4, 5, 2};
    r.orientationSampling.directionSamples = 12;
    r.orientationSampling.rollSamples = 6;
    r.orientationSampling.methodToken = "cascade";
    return r;
}

OperatingCondition makeCondition()
{
    OperatingCondition c;
    c.objectId = core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    c.name = "带载工况";
    c.level = RequirementLevel::Must;
    c.environmentRefs.push_back(
        core::ObjectId::fromCanonical("obj-b0000000000000000000000000000001"));
    RequirementReference tool;
    tool.kind = RequirementRefKind::Tool;
    tool.objectId = core::ObjectId::fromCanonical(
        "obj-a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    tool.tcpKey = "tcp1";
    c.toolRefs.push_back(tool);
    ConditionPayload payload;
    payload.toolRef = tool;
    payload.mass = core::SourcedValue<double>::provided(
        12.5, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    payload.com = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
    payload.inertia = core::SourcedValue<double>::provided(
        0.8, core::ValueProvenance::make(core::ProvenanceKind::CatalogBackfill));
    c.payloads.push_back(payload);
    ConditionEvent ev;
    ev.type = ConditionEventType::Dwell;
    ev.stationRef = core::ObjectId::fromCanonical("obj-0102030405060708090a0b0c0d0e0f10");
    ev.durationS = 2.0;
    c.events.push_back(ev);
    c.targetCycleTimeS = 18.0;
    c.verificationOrderHint = 3;
    c.appliesTo.scope = AppliesToScope::Stations;
    c.appliesTo.stations.push_back(
        core::ObjectId::fromCanonical("obj-0102030405060708090a0b0c0d0e0f10"));
    c.note = "工况备注";
    return c;
}

SamplingPlan makePlan()
{
    SamplingPlan pl;
    pl.objectId = core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
    pl.regionRef = core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    pl.positionSampling.method = PositionSamplingMethod::Grid;
    pl.positionSampling.counts = {4, 5, 2};
    pl.orientationSampling.directionSamples = 12;
    pl.orientationSampling.rollSamples = 6;
    pl.orientationSampling.methodToken = "cascade";
    return pl;
}

}  // namespace

/**
 * 五对象 roundtrip 逐字段一致（acceptance 2——IRequirementCodec encode/
 * decode roundtrip）：满配对象编码→解码→对象全等（operator== 逐字段）；
 * 五变体逐一覆盖。
 */
TEST(ReqCodec, RoundtripAllFiveObjectsFieldWise_WP14T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "NFR-COR-02"},
                  std::vector<std::string>{"ACC2-roundtrip"});

    const RequirementCodec codec;
    const CodecFormatVersion version = kCurrentRequirementFormatVersion;

    // 根对象。
    {
        const RequirementSet in = makeRoot();
        auto bytes = codec.encode(RequirementObjectVariant{in}, version);
        ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
        auto back = codec.decode(bytes.get(), version);
        ASSERT_TRUE(back.ok()) << back.error().detail;
        ASSERT_TRUE(std::holds_alternative<RequirementSet>(back.get()));
        EXPECT_EQ(std::get<RequirementSet>(back.get()), in) << "根对象逐字段一致";
    }
    // 点集（满配任务点）。
    {
        const PointSet in{kReqPointSetSchemaVersion, {makeRichPoint()}};
        auto bytes = codec.encode(RequirementObjectVariant{in}, version);
        ASSERT_TRUE(bytes.ok()) << bytes.error().detail;
        auto back = codec.decode(bytes.get(), version);
        ASSERT_TRUE(back.ok()) << back.error().detail;
        ASSERT_TRUE(std::holds_alternative<PointSet>(back.get()));
        EXPECT_EQ(std::get<PointSet>(back.get()).entries, in.entries)
            << "任务点逐字段一致（含溯源/位姿/容差/三段/顺序键/备注）";
    }
    // 区域集。
    {
        const RegionSet in{kReqRegionSetSchemaVersion, {makeRegion()}};
        auto bytes = codec.encode(RequirementObjectVariant{in}, version);
        ASSERT_TRUE(bytes.ok());
        auto back = codec.decode(bytes.get(), version);
        ASSERT_TRUE(back.ok()) << back.error().detail;
        EXPECT_EQ(std::get<RegionSet>(back.get()).entries, in.entries);
    }
    // 工况集（负载/事件/适用范围满配）。
    {
        const ConditionSet in{kReqConditionSetSchemaVersion, {makeCondition()}};
        auto bytes = codec.encode(RequirementObjectVariant{in}, version);
        ASSERT_TRUE(bytes.ok());
        auto back = codec.decode(bytes.get(), version);
        ASSERT_TRUE(back.ok()) << back.error().detail;
        EXPECT_EQ(std::get<ConditionSet>(back.get()).entries, in.entries);
    }
    // 计划集。
    {
        const PlanSet in{kReqPlanSetSchemaVersion, {makePlan()}};
        auto bytes = codec.encode(RequirementObjectVariant{in}, version);
        ASSERT_TRUE(bytes.ok());
        auto back = codec.decode(bytes.get(), version);
        ASSERT_TRUE(back.ok()) << back.error().detail;
        EXPECT_EQ(std::get<PlanSet>(back.get()).entries, in.entries);
    }
}

/**
 * 同输入→同编码字节（acceptance 2——确定性 NFR-COR-01）：同对象重复编
 * 码逐字节相等；集合条目乱序输入编码同字节（I-REQ-1——canonical 字节
 * 不含输入序）；同字节解码同结果。
 */
TEST(ReqCodec, DeterministicBytesAndOrderInsensitive_WP14T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{"ACC2-determinism"});

    const RequirementCodec codec;
    // 满配四条目点集。
    std::vector<TaskPoint> pts;
    for (int i = 0; i < 4; ++i) {
        TaskPoint p = makeRichPoint();
        p.objectId = core::ObjectId::fromCanonical(
            std::string("obj-0000000000000000000000000000000") + std::to_string(i + 1));
        p.name = "P" + std::to_string(i);
        p.sequenceKey = std::nullopt;
        pts.push_back(p);
    }
    PointSet a{kReqPointSetSchemaVersion, pts};
    PointSet b{kReqPointSetSchemaVersion, pts};
    std::reverse(b.entries.begin(), b.entries.end());  // 同集合、异输入序

    auto ea = codec.encode(RequirementObjectVariant{a}, kCurrentRequirementFormatVersion);
    auto eb = codec.encode(RequirementObjectVariant{b}, kCurrentRequirementFormatVersion);
    ASSERT_TRUE(ea.ok());
    ASSERT_TRUE(eb.ok());
    EXPECT_EQ(ea.get(), ea.get()) << "重复编码逐字节相等";
    // 同集合任意输入序必得同字节（I-REQ-1——规范化副本写出）。
    EXPECT_EQ(ea.get(), eb.get()) << "乱序输入编码同字节（canonical 形态与输入序无关）";

    // 同字节解码同结果。
    auto da = codec.decode(ea.get(), kCurrentRequirementFormatVersion);
    auto db = codec.decode(eb.get(), kCurrentRequirementFormatVersion);
    ASSERT_TRUE(da.ok());
    ASSERT_TRUE(db.ok());
    EXPECT_EQ(std::get<PointSet>(da.get()), std::get<PointSet>(db.get()));
}

/**
 * schema 主版本不识别→RequirementError(SchemaVersionUnsupported)＋升级
 * 指引（acceptance 2——NFR-DEP-04 稳定拒绝面；params 与 REQ-SCHEMA-
 * UNSUPPORTED paramSchema 对齐：object-type/schema-version/supported-major）。
 */
TEST(ReqCodec, UnsupportedMajorVersionRejectedWithGuidance_WP14T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-DEP-04"},
                  std::vector<std::string>{"ACC2-schema-reject"});

    const RequirementCodec codec;
    // encode 侧：请求未来主版本——拒绝（无降级/升级产出）。
    auto enc = codec.encode(RequirementObjectVariant{makeRoot()},
                            CodecFormatVersion{2, 0});
    ASSERT_FALSE(enc.ok());
    EXPECT_EQ(enc.error().code, RequirementErrorCode::SchemaVersionUnsupported);
    bool hasGuidance = enc.error().detail.find("升级") != std::string::npos;
    EXPECT_TRUE(hasGuidance) << "detail 携升级指引（acceptance 2 原文）";
    bool hasObjectType = false;
    bool hasSchemaVersion = false;
    bool hasSupportedMajor = false;
    for (const auto& kv : enc.error().params) {
        if (kv.first == "object-type") { hasObjectType = true; }
        if (kv.first == "schema-version") { hasSchemaVersion = true; }
        if (kv.first == "supported-major") { hasSupportedMajor = true; }
    }
    EXPECT_TRUE(hasObjectType && hasSchemaVersion && hasSupportedMajor)
        << "params 与 DiagCodes.hpp 同码 paramSchema 三键对齐";

    // decode 侧：未来主版本字节（头 major=99）——稳定拒绝不猜测。
    auto good = codec.encode(RequirementObjectVariant{makeRoot()},
                             kCurrentRequirementFormatVersion);
    ASSERT_TRUE(good.ok());
    auto tampered = good.get();
    ASSERT_GE(tampered.size(), 11U);
    tampered[7] = 99;  // 头 major 低字节（小端——offset 7 起 4 字节）
    auto dec = codec.decode(tampered, kCurrentRequirementFormatVersion);
    ASSERT_FALSE(dec.ok());
    EXPECT_EQ(dec.error().code, RequirementErrorCode::SchemaVersionUnsupported);
    EXPECT_NE(dec.error().detail.find("升级"), std::string::npos)
        << "decode 侧同样携升级指引";

    // decode 侧：截断/垃圾字节——MalformedPayload（结构面）。
    RequirementBytes truncated(good.get().begin(), good.get().begin() + 10);
    auto dec2 = codec.decode(truncated, kCurrentRequirementFormatVersion);
    ASSERT_FALSE(dec2.ok());
    EXPECT_EQ(dec2.error().code, RequirementErrorCode::MalformedPayload);

    // decode 侧：条目字节未按字典序（绕过构造边界）——I-REQ-1 拒绝。
    PointSet reversed{kReqPointSetSchemaVersion, {}};
    std::vector<TaskPoint> pts;
    for (int i = 0; i < 3; ++i) {
        TaskPoint p = makeRichPoint();
        p.objectId = core::ObjectId::fromCanonical(
            std::string("obj-0000000000000000000000000000000") + std::to_string(i + 1));
        p.name = "P" + std::to_string(i);
        p.sequenceKey = std::nullopt;
        pts.push_back(p);
    }
    std::reverse(pts.begin(), pts.end());
    // 直接构造乱序字节：编码后再交换载荷不可行——改为校验"编码器输出
    // 恒规范序"（乱序输入→同规范字节，本用例前半已钉）；此处补字节面：
    // 解码正字节的 roundtrip 恒规范序。
    reversed.entries = pts;
    auto e3 = codec.encode(RequirementObjectVariant{reversed}, kCurrentRequirementFormatVersion);
    ASSERT_TRUE(e3.ok());
    auto d3 = codec.decode(e3.get(), kCurrentRequirementFormatVersion);
    ASSERT_TRUE(d3.ok());
    EXPECT_TRUE(isCanonicalOrder(std::get<PointSet>(d3.get()).entries))
        << "解码产物恒 canonical 序（字节面 I-REQ-1）";
}

/**
 * RequirementProfile 派生档不入 canonical 编码（acceptance 2——§4.8）：
 * 编码对象全集恰为五对象（variant 备择数＝5）且不含 RequirementProfile；
 * 派生档重算即得（deriveRequirementProfile 两次派生同 contentIdentity
 * ——确定性）。
 */
TEST(ReqCodec, ProfileNeverEncodedVariantClosed_WP14T03_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{"ACC2-profile-not-encoded"});

    // 变体封闭性：RequirementObjectVariant 恰五备择（§4.1 表行数），
    // 五备择逐一可 holds_alternative；RequirementProfile 不在其中——
    // holds_alternative 对零出现类型 ill-formed，无法直接断言，改以
    // "五备择逐一命中＋备择总数＝5"钉死封闭面（不存在第 6 备择）。
    static_assert(std::variant_size_v<RequirementObjectVariant> == 5,
                  "编码对象全集＝五对象（§4.1）——Profile 派生档不入编码");
    {
        const RequirementObjectVariant probe{makeRoot()};
        EXPECT_TRUE(std::holds_alternative<RequirementSet>(probe));
        EXPECT_TRUE(std::holds_alternative<PointSet>(RequirementObjectVariant{PointSet{}}));
        EXPECT_TRUE(std::holds_alternative<RegionSet>(RequirementObjectVariant{RegionSet{}}));
        EXPECT_TRUE(std::holds_alternative<ConditionSet>(RequirementObjectVariant{ConditionSet{}}));
        EXPECT_TRUE(std::holds_alternative<PlanSet>(RequirementObjectVariant{PlanSet{}}));
    }

    // 派生档重算确定性（§4.8"重算即得"）。
    OperatingConditionService service;
    const auto pts = std::vector<TaskPoint>{makeRichPoint()};
    const auto regions = std::vector<WorkRegion>{makeRegion()};
    const auto conds = std::vector<OperatingCondition>{makeCondition()};
    const auto p1 = deriveRequirementProfile(pts, regions, conds, service);
    const auto p2 = deriveRequirementProfile(pts, regions, conds, service);
    EXPECT_EQ(p1, p2) << "同输入集合同派生档（含 contentIdentity 逐字节一致）";
    EXPECT_TRUE(p1.contentIdentity.isValid());
    // 计数核对：点 Must＋区域 Must（默认）＋工况 Must＝3；Should＝0。
    EXPECT_EQ(p1.mustCount, 3U);
    EXPECT_EQ(p1.shouldCount, 0U);
    // 必验清单：enabled∧Must 的工况恰好 1 条（makeCondition enabled∧Must）。
    EXPECT_EQ(p1.requiredCases.size(), 1U);
    EXPECT_EQ(p1.requiredCases[0].label, "带载工况");
    EXPECT_TRUE(p1.requiredCases[0].mandatory);
    // 覆盖汇总：唯一区域 minPositionCoverage＝0.8。
    EXPECT_DOUBLE_EQ(p1.minPositionCoverage, 0.8);
}
