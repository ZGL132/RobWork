/**
 * @file   GoldenDatasetsContractTest.cpp
 * @brief  requirements 黄金数据集契约套件（WP-14-T09）——四套黄金数据集
 *         （req-transport-task/req-partial-pose/req-region-sampling/
 *         req-payload-events）经 testkit GoldenFixture（TK-T08）全链装载，
 *         按卡 §10.2 故障注入矩阵逐行落位 requirements 侧断言，并承载
 *         NFR-COR-01/02/03 确定性负样例（任务契约 acceptance 1/2/5 的
 *         具名自证面）。
 *
 * 设计依据：
 *   - units/requirements.md §10.1（黄金数据集四清单＋容差档案＋边角样例）、
 *     §10.2（V-01~V-22 观测点——requirements 侧输入/输出面；联合观测行
 *     只断言本单元侧，不把联合行整体写为通过）、§13（NFR-COR-01/02/03
 *     T09 行：确定性断言＋附录 D 容差口径）、§6.2（必验冻结 schema）、
 *     §5.2/D-REQ-2（采样规范化）、§5.1/I-REQ-7（顺序拓扑）、§4.7
 *     （I-REQ-5 负样例）、§7.2（镜像黄金闭式）
 *   - units/testkit.md §4.2/§4.3/§4.5/§4.6（manifest/档案/完整性/数据根）、
 *     §6.1（GoldenFixture 六步生命周期）、§10.2（各域黄金数据集域自负——
 *     本套件为 requirements 域消费端）
 *   - 任务契约 tasks/foundation/WP-14-T09.json acceptance 1（V 行落位执
 *     行＋留痕）、2（四套黄金数据集＋lint＋GoldenFixture 随 CI 跑通）、
 *     5（确定性断言：同输入同字节/ObjectId 字典序/非有限与非法不静默）
 *
 * 双实现互证口径（pol/ev 夹具先例同款）：各数据集 expected/ 由 generate/
 * 下 Node 脚本按设计文档规则**独立重算**产出（§6.2 冻结解析、D-REQ-2
 * floor 规范化、I-REQ-7 Kahn 拓扑、π/180 独立计算）——本套件装载后以
 * 产品实现（resolveRequiredCases/buildPlan/checkSequence 等）对照
 * expected 逐项核对，任一侧漂移即本套件显性失败。
 *
 * 值模型重建口径：黄金身份须固定，而服务 createXxx 分配随机临时句柄
 * ——故按 CommandHandlersTest::makeHealthyPoint 同款夹具先例直构值模型
 * ＋validate* 全链证明构造合法性（构造边界的校验单点——NFR-MNT-04）。
 *
 * 模式约束：纯 core＋requirements＋testkit＋io/project/evidence 值类型
 * 消费（零 Qt）——冒烟＋集成两模式编译运行（数据集随 CI 跑通的双模式
 * 通道）；数据根两级解析由 testkit 库自带，两模式同源。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/io/Csv.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/requirements/CommandHandlers.hpp>
#include <sdurws/ird/requirements/Codec.hpp>
#include <sdurws/ird/requirements/DiagCodes.hpp>
#include <sdurws/ird/requirements/Editor.hpp>
#include <sdurws/ird/requirements/Errors.hpp>
#include <sdurws/ird/requirements/Import.hpp>
#include <sdurws/ird/requirements/ObjectTypes.hpp>
#include <sdurws/ird/requirements/OrientationResolution.hpp>
#include <sdurws/ird/requirements/Readiness.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/requirements/Sampling.hpp>
#include <sdurws/ird/requirements/Services.hpp>
#include <sdurws/ird/requirements/TemplateArray.hpp>
#include <sdurws/ird/testkit/Dataset.hpp>
#include <sdurws/ird/testkit/JsonLite.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/gtest/GoldenFixture.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <rw/math/Vector3D.hpp>

namespace tk = sdurws::ird::testkit;
namespace req = sdurws::ird::requirements;
namespace io = sdurws::ird::io;

namespace {

// =====================================================================
// 数据集常量（datasetId/版本与目录结构一致——testkit §4.2.2 关联约束）
// =====================================================================

constexpr char kDsTransport[] = "req-transport-task";
constexpr char kDsPartialPose[] = "req-partial-pose";
constexpr char kDsRegionSampling[] = "req-region-sampling";
constexpr char kDsPayloadEvents[] = "req-payload-events";
constexpr char kDsVersion[] = "1.0.0";
constexpr char kProfileId[] = "req-golden";

// 黄金外部锚（跨聚合浅引用目标——token 匹配表 §4.1/§4.3；仅元数据浅校验）。
constexpr char kAnchorRobot[] = "obj-3e000000000000000000000000000001";
constexpr char kAnchorSceneA[] = "obj-3f000000000000000000000000000001";
constexpr char kAnchorSceneB[] = "obj-3f000000000000000000000000000002";
constexpr char kAnchorTool[] = "obj-3d000000000000000000000000000001";

// =====================================================================
// 读取与小工具
// =====================================================================

/// 读文本文件（expected/inputs 的读取入口——二进制安全整读；读失败显性失败）。
std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(static_cast<bool>(in)) << "无法读取: " << path.string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// 解析 JSON（数据集文件非法＝数据资产缺陷——gtest 失败而非异常穿越）。
tk::JsonValue parseDatasetJson(const std::filesystem::path& path)
{
    const std::string text = readTextFile(path);
    tk::JsonValue v;
    EXPECT_NO_THROW(v = tk::parseJson(text)) << "JSON 解析失败: " << path.string();
    return v;
}

/// 固定 ObjectId 反序列化（数据集内全部身份为固定十六进制——确定性前提；
/// 规范文本非法＝数据资产缺陷，显性失败不猜）。
sdurws::ird::core::ObjectId oidOf(const std::string& canonical)
{
    auto id = sdurws::ird::core::ObjectId::tryFromCanonical(canonical);
    EXPECT_TRUE(id.has_value()) << "ObjectId 规范文本非法: " << canonical;
    return id.value_or(sdurws::ird::core::ObjectId{});
}

/// 三维向量读取（JSON 数组 [x,y,z]；单位见 manifest units——m/rad SI）。
rw::math::Vector3D<double> vec3Of(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    if (v == nullptr) { return {}; }
    EXPECT_TRUE(v->isArray()) << "向量字段须为三元素数组";
    EXPECT_EQ(v->items.size(), 3U);
    return {v->items[0].number, v->items[1].number, v->items[2].number};
}

/// 来源标记（数据集统一 user-provided＋methodTag=golden-fixture——§4.3
/// source 行；ValueProvenance::make 校验 methodTag 语法）。
sdurws::ird::core::ValueProvenance goldenProvenance()
{
    return sdurws::ird::core::ValueProvenance::make(
        sdurws::ird::core::ProvenanceKind::UserProvided, std::nullopt,
        std::nullopt, std::string("golden-fixture"));
}

/// SourcedValue<double> 读取（四态词表——provided/not-provided/not-
/// applicable/invalid；invalid 携 raw 原串——"缺失≠0、非法不清洗"）。
sdurws::ird::core::SourcedValue<double> scalarOf(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    if (v == nullptr) { return {}; }
    const std::string state = v->find("state")->text;
    if (state == "provided") {
        return sdurws::ird::core::SourcedValue<double>::provided(
            v->find("value")->number, goldenProvenance());
    }
    if (state == "not-provided") {
        return sdurws::ird::core::SourcedValue<double>::notProvided();
    }
    if (state == "not-applicable") {
        return sdurws::ird::core::SourcedValue<double>::notApplicable();
    }
    EXPECT_EQ(state, "invalid") << "未知四态 token: " << state;
    return sdurws::ird::core::SourcedValue<double>::invalid(v->find("raw")->text);
}

/// SourcedValue<Vector3D> 读取（com 单位 m，工具 TCP 系——§4.5 payloads 行）。
sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>> sourcedVecOf(
    const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    if (v == nullptr) { return {}; }
    const std::string state = v->find("state")->text;
    if (state == "provided") {
        return sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            vec3Of(v->find("value")), goldenProvenance());
    }
    if (state == "not-provided") {
        return sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
    }
    if (state == "not-applicable") {
        return sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::notApplicable();
    }
    EXPECT_EQ(state, "invalid");
    return sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::invalid(
        v->find("raw")->text);
}

/// SourcedValue 四态 → manifest 词表 token（provided/not-provided/
/// not-applicable/invalid——core FieldState 的黄金词表投影）。
const char* fieldStateToken(sdurws::ird::core::FieldState state) noexcept
{
    switch (state) {
    case sdurws::ird::core::FieldState::Provided: return "provided";
    case sdurws::ird::core::FieldState::NotProvided: return "not-provided";
    case sdurws::ird::core::FieldState::NotApplicable: return "not-applicable";
    case sdurws::ird::core::FieldState::Invalid: return "invalid";
    }
    return "unknown";
}

/// 需求等级（"Must"/"Should"——tryRequirementLevel 词表外拒绝不猜测）。
req::RequirementLevel levelOf(const std::string& token)
{
    auto level = req::tryRequirementLevel(token);
    EXPECT_TRUE(level.has_value()) << "等级词表外: " << token;
    return level.value_or(req::RequirementLevel::Must);
}

/// 引用读取（kind 词表＝枚举 token 原文；载荷匹配性由 wellFormed 校验）。
req::RequirementReference refOf(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    if (v == nullptr) { return {}; }
    req::RequirementReference ref;
    const std::string kind = v->find("kind")->text;
    if (kind == "World") { ref.kind = req::RequirementRefKind::World; }
    else if (kind == "ModelFrame") { ref.kind = req::RequirementRefKind::ModelFrame; }
    else if (kind == "SceneObject") { ref.kind = req::RequirementRefKind::SceneObject; }
    else if (kind == "Tool") { ref.kind = req::RequirementRefKind::Tool; }
    else if (kind == "DefaultTcp") { ref.kind = req::RequirementRefKind::DefaultTcp; }
    else { ADD_FAILURE() << "引用种类词表外: " << kind; }
    if (const auto* oid = v->find("objectId"); oid != nullptr && !oid->isNull()) {
        ref.objectId = oidOf(oid->text);
    }
    if (const auto* tcp = v->find("tcpKey"); tcp != nullptr && !tcp->isNull()) {
        ref.tcpKey = tcp->text;
    }
    return ref;
}

/// 姿态规则读取（§5.3 五规则载荷按 kind 分支——其余字段保持缺省值纪律）。
req::OrientationRule ruleOf(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    req::OrientationRule rule;
    if (v == nullptr) { return rule; }
    const std::string kind = v->find("kind")->text;
    if (kind == "Fixed") {
        rule.kind = req::OrientationRuleKind::Fixed;
        rule.fixedRpy = vec3Of(v->find("fixedRpy"));  // rad（Z-Y-X 欧拉序）
    } else if (kind == "AlignFrame") {
        rule.kind = req::OrientationRuleKind::AlignFrame;
        rule.targetFrame = refOf(v->find("targetFrame"));
    } else if (kind == "AlignGeometryNormal") {
        rule.kind = req::OrientationRuleKind::AlignGeometryNormal;
        rule.targetSceneObject = oidOf(v->find("targetSceneObject")->text);
        if (const auto* f = v->find("feature"); f != nullptr && !f->isNull()) {
            auto feature = req::tryOrientationFeature(f->text);
            EXPECT_TRUE(feature.has_value()) << "特征词表外: " << f->text;
            rule.feature = feature;
        }
        if (const auto* inv = v->find("invertNormal"); inv != nullptr && inv->isBool()) {
            rule.invertNormal = inv->boolean;
        }
    } else if (kind == "PointAtTarget") {
        rule.kind = req::OrientationRuleKind::PointAtTarget;
        rule.targetPoint = vec3Of(v->find("targetPoint"));  // m
    } else if (kind == "ToolRollFree") {
        rule.kind = req::OrientationRuleKind::ToolRollFree;
        const auto* rr = v->find("rollRange");
        rule.rollRange.min = rr->find("min")->number;  // rad
        rule.rollRange.max = rr->find("max")->number;  // rad
    } else {
        ADD_FAILURE() << "姿态规则词表外: " << kind;
    }
    return rule;
}

/// 位姿约束读取（position 四态＋六分量掩码＋姿态规则——§4.3）。
req::PoseConstraint poseOf(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    req::PoseConstraint pose;
    if (v == nullptr) { return pose; }
    pose.position = sourcedVecOf(v->find("position"));
    const auto* dof = v->find("constrainedDof");
    pose.constrainedDof.x = dof->find("x")->boolean;
    pose.constrainedDof.y = dof->find("y")->boolean;
    pose.constrainedDof.z = dof->find("z")->boolean;
    pose.constrainedDof.roll = dof->find("roll")->boolean;
    pose.constrainedDof.pitch = dof->find("pitch")->boolean;
    pose.constrainedDof.yaw = dof->find("yaw")->boolean;
    pose.orientation = ruleOf(v->find("orientation"));
    return pose;
}

/// 三段读取（approach/work/retract——距离单位 m，启用时须 >0）。
req::TaskSegment segmentOf(const tk::JsonValue* v)
{
    EXPECT_NE(v, nullptr);
    req::TaskSegment seg;
    if (v == nullptr) { return seg; }
    seg.enabled = v->find("enabled")->boolean;
    auto axis = req::trySegmentAxis(v->find("axis")->text);
    EXPECT_TRUE(axis.has_value()) << "轴词表外: " << v->find("axis")->text;
    seg.axis = axis.value_or(req::SegmentAxis::ToolZ);
    seg.distanceM = v->find("distanceM")->number;  // m
    return seg;
}

// ---- 领域对象重建（输入 JSON → 值模型；合法性经 validate* 单点证明）----

/// 任务点重建（字段序＝§4.3 字段表；固定 id——黄金前提）。
req::TaskPoint pointOf(const tk::JsonValue& j)
{
    req::TaskPoint p;
    p.objectId = oidOf(j.find("id")->text);
    p.name = j.find("name")->text;
    p.level = levelOf(j.find("level")->text);
    p.enabled = j.find("enabled")->boolean;
    auto tag = req::tryProcessTag(j.find("processTag")->text);
    EXPECT_TRUE(tag.has_value()) << "工艺标签词表外";
    p.processTag = tag.value_or(req::ProcessTag::Generic);
    p.source = goldenProvenance();
    p.refFrame = refOf(j.find("refFrame"));
    p.pose = poseOf(j.find("pose"));
    const auto* tol = j.find("tolerance");
    p.tolerance.positionTolerance = tol->find("positionTolerance")->number;       // m
    p.tolerance.orientationTolerance = tol->find("orientationTolerance")->number; // rad
    const auto* segs = j.find("segments");
    p.approach = segmentOf(segs->find("approach"));
    p.work = segmentOf(segs->find("work"));
    p.retract = segmentOf(segs->find("retract"));
    p.demands.collisionFreeRequired =
        j.find("demands")->find("collisionFreeRequired")->boolean;
    if (const auto* sk = j.find("sequenceKey"); sk != nullptr && !sk->isNull()) {
        p.sequenceKey = sk->text;
    }
    p.note = j.find("note")->text;
    return p;
}

/// 区域重建（§4.4 字段表；采样定义为编辑态——GridBySpacing 合法，D-REQ-2）。
req::WorkRegion regionOf(const tk::JsonValue& j)
{
    req::WorkRegion r;
    r.objectId = oidOf(j.find("id")->text);
    r.name = j.find("name")->text;
    r.level = levelOf(j.find("level")->text);
    r.enabled = j.find("enabled")->boolean;
    r.source = goldenProvenance();
    r.refFrame = refOf(j.find("refFrame"));
    const auto* box = j.find("box");
    r.box.center = vec3Of(box->find("center"));  // m
    r.box.size = vec3Of(box->find("size"));      // m
    const auto* ps = j.find("positionSampling");
    const std::string method = ps->find("method")->text;
    if (method == "Grid") {
        r.positionSampling.method = req::PositionSamplingMethod::Grid;
        const auto& counts = ps->find("counts")->items;
        EXPECT_EQ(counts.size(), 3U);
        for (std::size_t i = 0; i < 3 && i < counts.size(); ++i) {
            r.positionSampling.counts[i] = static_cast<std::uint32_t>(counts[i].number);
        }
    } else {
        EXPECT_EQ(method, "GridBySpacing");
        r.positionSampling.method = req::PositionSamplingMethod::GridBySpacing;
        const auto& sp = ps->find("spacing")->items;
        EXPECT_EQ(sp.size(), 3U);
        for (std::size_t i = 0; i < 3 && i < sp.size(); ++i) {
            r.positionSampling.spacing[i] = sp[i].number;  // m
        }
    }
    const auto* os = j.find("orientationSampling");
    r.orientationSampling.directionSamples =
        static_cast<std::uint32_t>(os->find("directionSamples")->number);
    r.orientationSampling.rollSamples =
        static_cast<std::uint32_t>(os->find("rollSamples")->number);
    r.orientationSampling.methodToken = os->find("methodToken")->text;
    r.coverageTargets.minPositionCoverage =
        j.find("coverageTargets")->find("minPositionCoverage")->number;
    if (const auto* sk = j.find("sequenceKey"); sk != nullptr && !sk->isNull()) {
        r.sequenceKey = sk->text;
    }
    r.note = j.find("note")->text;
    return r;
}

/// 工况重建（§4.5 字段表——负载四态/事件/适用范围/要求值）。
req::OperatingCondition conditionOf(const tk::JsonValue& j)
{
    req::OperatingCondition c;
    c.objectId = oidOf(j.find("id")->text);
    c.name = j.find("name")->text;
    c.level = levelOf(j.find("level")->text);
    c.enabled = j.find("enabled")->boolean;
    for (const auto& e : j.find("environmentRefs")->items) {
        c.environmentRefs.push_back(oidOf(e.text));
    }
    for (const auto& t : j.find("toolRefs")->items) {
        c.toolRefs.push_back(refOf(&t));
    }
    for (const auto& pl : j.find("payloads")->items) {
        req::ConditionPayload payload;
        payload.toolRef = refOf(pl.find("toolRef"));
        payload.mass = scalarOf(pl.find("mass"));        // kg
        payload.com = sourcedVecOf(pl.find("com"));      // m（工具 TCP 系）
        payload.inertia = scalarOf(pl.find("inertia"));  // kg·m²
        c.payloads.push_back(std::move(payload));
    }
    for (const auto& ev : j.find("events")->items) {
        req::ConditionEvent event;
        auto type = req::tryConditionEventType(ev.find("type")->text);
        EXPECT_TRUE(type.has_value()) << "事件类型词表外";
        event.type = type.value_or(req::ConditionEventType::Grasp);
        event.stationRef = oidOf(ev.find("stationRef")->text);
        if (const auto* d = ev.find("durationS"); d != nullptr && !d->isNull()) {
            event.durationS = d->number;  // s
        }
        c.events.push_back(std::move(event));
    }
    if (const auto* t = j.find("targetCycleTimeS"); t != nullptr && !t->isNull()) {
        c.targetCycleTimeS = t->number;  // s
    }
    c.demands.collisionFreeRequired =
        j.find("demands")->find("collisionFreeRequired")->boolean;
    if (const auto* m = j.find("demands")->find("minimumJointMargin");
        m != nullptr && !m->isNull()) {
        c.demands.minimumJointMargin = m->number;  // rad/m 依关节类型
    }
    if (const auto* h = j.find("verificationOrderHint"); h != nullptr && !h->isNull()) {
        c.verificationOrderHint = static_cast<std::uint32_t>(h->number);
    }
    const auto* at = j.find("appliesTo");
    const std::string scope = at->find("scope")->text;
    if (scope == "AllStations") { c.appliesTo.scope = req::AppliesToScope::AllStations; }
    else if (scope == "Stations") { c.appliesTo.scope = req::AppliesToScope::Stations; }
    else { EXPECT_EQ(scope, "None"); c.appliesTo.scope = req::AppliesToScope::None; }
    for (const auto& s : at->find("stations")->items) {
        c.appliesTo.stations.push_back(oidOf(s.text));
    }
    c.note = j.find("note")->text;
    return c;
}

// =====================================================================
// 编码/闭包/就绪上下文小工具
// =====================================================================

/// canonical 编码（ok 断言封装——失败消息携 detail 定位）。
req::RequirementBytes encodeOk(const req::RequirementCodec& codec,
                               const req::RequirementObjectVariant& object)
{
    auto encoded = codec.encode(object, req::kCurrentRequirementFormatVersion);
    EXPECT_TRUE(encoded.ok()) << "canonical 编码失败: " << encoded.error().detail;
    return encoded.ok() ? encoded.get() : req::RequirementBytes{};
}

/// 诊断清单中是否含指定稳定码（CommandHandlersTest 同款机器判别面）。
bool hasDiagCode(const std::vector<sdurws::ird::core::DiagnosticRecord>& diags,
                 const char* code)
{
    for (const auto& d : diags) {
        if (d.code == code) { return true; }
    }
    return false;
}

/// 闭包引用条目（project::ObjectRef 元数据——浅校验仅消费 oid/token 两字段，
/// §8.1 浅引用边界：不解码 modeling 对象字节）。
sdurws::ird::project::ObjectRef makeRef(const sdurws::ird::core::ObjectId& oid,
                                        std::string token)
{
    sdurws::ird::project::ObjectRef ref;
    ref.objectId = oid;
    ref.contentVersion = sdurws::ird::core::ContentVersion{};
    ref.contentVersion.bytes[0] = 1;  // 非全零（全零＝保留值语义）
    ref.objectTypeToken = std::move(token);
    ref.digest256 = std::string(64, '0');
    return ref;
}

/// 黄金闭包上下文（外部 modeling 锚固定登记——R1/R8 浅核对数据面）。
req::CheckContext goldenContext()
{
    req::CheckContext ctx;
    ctx.closureRefs.push_back(makeRef(oidOf(kAnchorRobot), "robot-design"));
    ctx.closureRefs.push_back(makeRef(oidOf(kAnchorSceneA), "scene-object"));
    ctx.closureRefs.push_back(makeRef(oidOf(kAnchorSceneB), "scene-object"));
    ctx.closureRefs.push_back(makeRef(oidOf(kAnchorTool), "tool-definition"));
    return ctx;
}

// =====================================================================
// 黄金夹具（GoldenFixture 派生——TK-T08 六步生命周期；每数据集一子类，
// SetUp 自动完成：数据根解析→数据集装载（含完整性）→档案就绪→确定性
// 环境→TempDir——任一步失败按 §7.2 分类跳过，不伪装通过）
// =====================================================================

/// 夹具基座：期望/输入文件路径的域扩展访问面。
class ReqGoldenBase : public tk::GoldenFixture {
protected:
    /// 版本目录内期望文件路径（resolveExpected 校验 relPath 在清单内）。
    std::filesystem::path expectedPath(const char* rel) const
    {
        EXPECT_NE(dataset, std::nullopt) << "数据集未装载（GoldenFixture 步骤②）";
        return dataset->resolveExpected(rel);
    }

    /// 输入文件路径（resolveInput 校验 relPath 在 manifest inputs 清单内）。
    std::filesystem::path inputPath(const char* rel) const
    {
        EXPECT_NE(dataset, std::nullopt);
        return dataset->resolveInput(rel);
    }
};

/// 数据集 1/4：搬运任务（五对象全量）。
class TransportGoldenTest : public ReqGoldenBase {
protected:
    tk::DatasetRef datasetRef() const override { return {kDsTransport, kDsVersion}; }
};

/// 数据集 2/4：部分位姿约束。
class PartialPoseGoldenTest : public ReqGoldenBase {
protected:
    tk::DatasetRef datasetRef() const override { return {kDsPartialPose, kDsVersion}; }
};

/// 数据集 3/4：区域与采样定义。
class RegionSamplingGoldenTest : public ReqGoldenBase {
protected:
    tk::DatasetRef datasetRef() const override { return {kDsRegionSampling, kDsVersion}; }
};

/// 数据集 4/4：负载事件。
class PayloadEventsGoldenTest : public ReqGoldenBase {
protected:
    tk::DatasetRef datasetRef() const override { return {kDsPayloadEvents, kDsVersion}; }
};

// =====================================================================
// 容差档案面（acceptance 2——附录 D 锚定不放宽＋消费路径前置可解析）
// =====================================================================

/**
 * req-golden 档案装载全链＋锚定核对（acceptance 2——"容差档案……锚
 * REQUIREMENTS 附录 D 通用比较公式……档案不得放宽附录 D 固定值"）：
 * basis 锚定附录 D 项号；全部条目零宽容差且 tolerance==allowedMax（字节
 * 级等值的档案化表达——不存在任何放宽通道，§4.3.2 固定类"只准更严"
 * 同判据）；套件消费的全部 fieldPath 前置可解析（C4"报错不默认"——消费
 * 点 ToleranceUndefined 前移到 CI；modeling MdlGoldenDatasets 同款）。
 */
TEST(ReqGoldenTolerance, ProfileAnchoredAndNotRelaxed_WP14T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-03"},
                  std::vector<std::string>{"AT-02"});

    const auto profile = tk::ToleranceProfile::load(
        tk::toleranceProfileDir(kProfileId) / "v1.0.0.json");
    EXPECT_EQ(profile.profileId, kProfileId);
    EXPECT_EQ(profile.version, kDsVersion);
    EXPECT_NE(profile.basis.find("appendixD#12"), std::string::npos)
        << "basis 须锚定附录 D 第 12 项（身份/字面精确等值——无容差）";
    EXPECT_NE(profile.basis.find("appendixD#4"), std::string::npos)
        << "basis 须锚定附录 D 第 4 项（零参考退化 ε_abs 口径）";
    ASSERT_FALSE(profile.entries.empty());

    // "不得放宽"的档案面执行：零宽容差条目且 tolerance==allowedMax。
    for (const tk::ToleranceEntry& e : profile.entries) {
        ASSERT_TRUE(e.allowedMax.has_value())
            << "条目 " << e.fieldPath << " 缺 allowedMax";
        EXPECT_DOUBLE_EQ(e.tolerance.relative, e.allowedMax->relative)
            << "条目 " << e.fieldPath << " 相对分量被放宽";
        EXPECT_DOUBLE_EQ(e.tolerance.absolute, e.allowedMax->absolute)
            << "条目 " << e.fieldPath << " 绝对分量被放宽";
        EXPECT_DOUBLE_EQ(e.tolerance.relative, 0.0);
        EXPECT_DOUBLE_EQ(e.tolerance.absolute, 0.0);
    }
    // 套件消费的全部具体 fieldPath 前置可解析（C4——缺失即消费点抛错）。
    for (const std::string& p :
         {"req.tolerance.position", "req.tolerance.orientation",
          "req.sampling.counts[*]", "req.plan.position-samples",
          "req.plan.orientation-samples", "req.payload.mass",
          "req.coverage.min-position"}) {
        EXPECT_NO_THROW((void)profile.resolve(p)) << "档案缺条目: " << p;
    }
}

// =====================================================================
// 数据集 1/4：搬运任务
// =====================================================================

/**
 * 数据集装载与黄金判据面（acceptance 2——GoldenFixture 全链装载：解析→
 * schema→integrity SHA-256＋size→交叉校验；任一失败即"数据集非法级"
 * 显性失败——损坏数据不得伪装成算法回归，§4.5）。期望文件独立重算面
 * 随装载可读（§6.2 必验表黄金判据）。
 */
TEST_F(TransportGoldenTest, DatasetLoadsAndExpectedFacesReadable_WP14T09_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "REQ-02", "REQ-03", "REQ-04"},
                  std::vector<std::string>{"AT-02", "AT-23", "AT-24"});

    ASSERT_NE(dataset, std::nullopt);
    EXPECT_EQ(dataset->manifest().datasetId, kDsTransport);
    EXPECT_EQ(dataset->manifest().kind, tk::DatasetKind::ContractFixture);
    ASSERT_NE(profile, std::nullopt);
    EXPECT_EQ(profile->profileId, kProfileId);

    // 期望文件独立重算面可读（§6.2 冻结解析黄金判据：2 条目/1 必验）。
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/transport-task-expected.json"));
    const auto* frozen = expected.find("requiredCasesFrozenSchema");
    ASSERT_NE(frozen, nullptr);
    EXPECT_EQ(frozen->find("entries")->items.size(), 2U);
    EXPECT_EQ(frozen->find("requiredCount")->number, 1.0);

    // 步骤⑤ TempDir 可用（V-10 副本导出的写盘落点）。
    ASSERT_NE(workDir, nullptr);
    EXPECT_TRUE(std::filesystem::is_directory(workDir->path()));
}

/**
 * V-01（acceptance 1——任务点创建/修改/删除与引用保护）：编辑器闭包载
 * 入黄金对象→增点（草稿产生＋编辑计数 +1）→改名（接受）→删除被工况
 * 事件引用的点（§5.1 删除保护：拒绝＋字节不变）。"应用恰一修订"的
 * requirements 侧输入面＝整批草稿装配为**恰一条命令载荷**（根＋四集合
 * 槽，D-REQ-9）；修订计数行属联合观测（project 侧），不在此断言。
 */
TEST_F(TransportGoldenTest, V01EditGuardAndSingleApplyEnvelope_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "REQ-06"},
                  std::vector<std::string>{"AT-02"});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);

    // 黄金输入重建五对象（固定 id；逐条经 validate* 证明合法）。
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    RequirementWorkingSet ws;
    ws.root.name = input.find("root")->find("name")->text;
    // 根引用表（§4.2——四集合对象 id；闭包解引用的路由面）。
    const auto* refs = input.find("root")->find("refs");
    ws.root.pointSetRef = oidOf(refs->find("pointSet")->text);
    ws.root.regionSetRef = oidOf(refs->find("regionSet")->text);
    ws.root.conditionSetRef = oidOf(refs->find("conditionSet")->text);
    ws.root.planSetRef = oidOf(refs->find("planSet")->text);
    for (const auto& j : input.find("points")->items) {
        TaskPoint p = pointOf(j);
        EXPECT_FALSE(validateTaskPoint(p).has_value()) << p.name << " 应合法";
        ws.points.entries.push_back(std::move(p));
    }
    sortEntriesByObjectId(ws.points.entries);
    for (const auto& j : input.find("regions")->items) {
        WorkRegion r = regionOf(j);
        EXPECT_FALSE(validateWorkRegion(r).has_value());
        ws.regions.entries.push_back(std::move(r));
    }
    sortEntriesByObjectId(ws.regions.entries);
    for (const auto& j : input.find("conditions")->items) {
        OperatingCondition c = conditionOf(j);
        EXPECT_FALSE(validateOperatingCondition(c).has_value());
        ws.conditions.entries.push_back(std::move(c));
    }
    sortEntriesByObjectId(ws.conditions.entries);

    // 闭包域内存字节源（五对象 canonical 字节——decode(encode(x))==x 面）。
    const RequirementCodec codec;
    std::map<std::string, RequirementBytes> byToken;
    byToken[std::string(kReqSetObjectType)] = encodeOk(codec, RequirementObjectVariant(ws.root));
    byToken[std::string(kReqPointSetObjectType)] = encodeOk(codec, RequirementObjectVariant(ws.points));
    byToken[std::string(kReqRegionSetObjectType)] = encodeOk(codec, RequirementObjectVariant(ws.regions));
    byToken[std::string(kReqConditionSetObjectType)] = encodeOk(codec, RequirementObjectVariant(ws.conditions));
    byToken[std::string(kReqPlanSetObjectType)] = encodeOk(codec, RequirementObjectVariant(ws.plans));
    std::map<std::string, std::pair<std::string, RequirementBytes>> byOid;
    byOid[oidOf("obj-2a000000000000000000000000000001").toCanonical()]
        = {std::string(kReqPointSetObjectType), byToken[std::string(kReqPointSetObjectType)]};
    byOid[oidOf("obj-2a000000000000000000000000000002").toCanonical()]
        = {std::string(kReqRegionSetObjectType), byToken[std::string(kReqRegionSetObjectType)]};
    byOid[oidOf("obj-2a000000000000000000000000000003").toCanonical()]
        = {std::string(kReqConditionSetObjectType), byToken[std::string(kReqConditionSetObjectType)]};
    byOid[oidOf("obj-2a000000000000000000000000000004").toCanonical()]
        = {std::string(kReqPlanSetObjectType), byToken[std::string(kReqPlanSetObjectType)]};

    class MemClosure final : public RequirementObjectClosureView {
    public:
        const std::map<std::string, RequirementBytes>* byToken{};
        const std::map<std::string, std::pair<std::string, RequirementBytes>>* byOid{};
        std::optional<RequirementClosureObject> tryObjectByToken(
            std::string_view token) const override
        {
            auto it = byToken->find(std::string{token});
            if (it == byToken->end()) { return std::nullopt; }
            return RequirementClosureObject{std::string{token}, it->second};
        }
        std::optional<RequirementClosureObject> tryObject(
            const sdurws::ird::core::ObjectId& oid) const override
        {
            auto it = byOid->find(oid.toCanonical());
            if (it == byOid->end()) { return std::nullopt; }
            return RequirementClosureObject{it->second.first, it->second.second};
        }
    } closure;
    closure.byToken = &byToken;
    closure.byOid = &byOid;

    // 载入基线（编辑态建立；基线解码经 decode 校验链——V-13 恢复面同源）。
    RequirementEditor editor;
    auto loaded = editor.loadBaseline(closure);
    ASSERT_TRUE(loaded.ok) << loaded.error.detail;
    EXPECT_FALSE(editor.draftStatus().dirty);
    EXPECT_EQ(editor.draftStatus().edits, 0U);

    // 增点：接受（草稿产生——编辑计数 +1；预览面零修订归 V-20）。
    TaskPoint fresh;
    fresh.objectId = oidOf("obj-1a0000000000000000000000000000aa");
    fresh.name = "P-extra";
    fresh.pose.position =
        sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.1, 0.1, 0.1), goldenProvenance());
    fresh.pose.constrainedDof.z = true;
    fresh.work.enabled = true;
    fresh.work.distanceM = 1.0;  // m
    auto addOutcome = editor.applyEdit(RequirementEdit{fresh});
    ASSERT_TRUE(addOutcome.accepted) << addOutcome.error.detail;
    EXPECT_EQ(editor.draftStatus().edits, 1U);
    EXPECT_TRUE(editor.draftStatus().dirty);

    // 改名：接受（D-REQ-6——名称是语义字段；改名点的前驱引用先行解除，
    // 避免 R7 悬空前驱混入本用例的关注面）。
    TaskPoint renamed = editor.workingSet().points.entries[0];
    renamed.name = "P-pick-renamed";
    renamed.sequenceKey = std::nullopt;
    TaskPoint placeFixed = editor.workingSet().points.entries[1];
    placeFixed.sequenceKey = std::nullopt;
    ASSERT_TRUE(editor.applyEdit(RequirementEdit{renamed}).accepted);
    ASSERT_TRUE(editor.applyEdit(RequirementEdit{placeFixed}).accepted);
    EXPECT_EQ(editor.draftStatus().edits, 3U);

    // 删除被引用点：拒绝（P-place 被 Release/Dwell 事件 stationRef 引用
    // ——§5.1 删除保护：编辑边界拒绝＋字节不变）。
    const auto before = editor.workingSet().points;
    auto removeOutcome =
        editor.applyEdit(removeEdit(placeFixed.objectId, WorkingSetMember::Points));
    EXPECT_FALSE(removeOutcome.accepted) << "被引用点删除必须被拒绝（§5.1）";
    EXPECT_EQ(editor.workingSet().points, before) << "拒绝态工作集字节不变";
    EXPECT_EQ(editor.draftStatus().edits, 3U) << "拒绝不产生编辑计数";

    // "应用恰一修订"输入面：整批装配为恰一条 apply-requirement-set 载荷
    // （根＋四集合槽；载荷编解码往返逐字节——D-REQ-9 命令族仅两条）。
    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    const auto slotOf = [](bool allocateNew, std::string_view token,
                           const RequirementBytes& bytes) {
        return RequirementPayloadSlot{allocateNew, sdurws::ird::core::ObjectId{},
                                      std::string(token), bytes};
    };
    payload.objects.push_back(slotOf(true, kReqSetObjectType, byToken[std::string(kReqSetObjectType)]));
    payload.objects.push_back(slotOf(true, kReqPointSetObjectType, byToken[std::string(kReqPointSetObjectType)]));
    payload.objects.push_back(slotOf(true, kReqRegionSetObjectType, byToken[std::string(kReqRegionSetObjectType)]));
    payload.objects.push_back(slotOf(true, kReqConditionSetObjectType, byToken[std::string(kReqConditionSetObjectType)]));
    payload.objects.push_back(slotOf(true, kReqPlanSetObjectType, byToken[std::string(kReqPlanSetObjectType)]));
    const auto bytes = encodeRequirementCommandPayload(payload);
    auto decoded = tryDecodeRequirementCommandPayload(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->objects.size(), 5U) << "根＋四集合＝恰一命令五槽（D-REQ-9）";
    EXPECT_TRUE(encodeRequirementCommandPayload(*decoded) == bytes) << "载荷往返逐字节一致";
}

/**
 * V-02 requirements 侧（acceptance 1——区域边界与零样本）：size 含 0 的
 * 区域构造拒绝（DegenerateRegion——I-REQ-6）；counts 乘积=0 计划**合法
 * 存储**（buildPlan ok＋canonical 可编码——"planContentIdentity=0 计划
 * 可编码"观测点；零样本判定归评估 KIN-04，本单元不判）；区域已定义而
 * 计划集为空 → R6 Warning（REQ-READY-PLAN-MISSING——就绪报告分级面）。
 */
TEST_F(TransportGoldenTest, V02DegenerateRefusedAndZeroPlanLegal_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-04"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    const WorkRegion zone = regionOf(*input.find("regions")->items.begin());

    // 区域退化拒绝（构造边界——size 含 0）。
    WorkRegionSpec degenerate;
    degenerate.name = "R-degenerate";
    degenerate.box = zone.box;
    degenerate.box.size = rw::math::Vector3D<double>(0.2, 0.0, 0.2);  // m（0 非法）
    WorkRegionService regionService;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    auto refused = regionService.createRegion(degenerate, diags);
    ASSERT_FALSE(refused.ok);
    EXPECT_EQ(refused.error.code, RequirementErrorCode::DegenerateRegion);
    EXPECT_TRUE(diags.empty()) << "构造边界走值面不产诊断（§9.4 契约预留位）";

    // 零样本计划合法（counts [0,2,2]——分母 0 存储面；判定归评估）。
    WorkRegion zeroRegion = zone;
    zeroRegion.objectId = oidOf("obj-2c000000000000000000000000000004");
    zeroRegion.name = "R-zero-transport";
    zeroRegion.positionSampling.method = PositionSamplingMethod::Grid;
    zeroRegion.positionSampling.counts = {0U, 2U, 2U};
    SamplingPlanBuilder builder;
    SamplingPlanSpec zeroSpec;
    zeroSpec.planObjectId = oidOf("obj-1d0000000000000000000000000000aa");
    zeroSpec.positionSampling = zeroRegion.positionSampling;
    zeroSpec.orientationSampling = zone.orientationSampling;
    auto zeroPlan = builder.buildPlan(zeroRegion, zeroSpec, diags);
    ASSERT_TRUE(zeroPlan.ok) << zeroPlan.error.detail;
    auto zeroDigest = builder.digest(zeroPlan.plan, diags);
    ASSERT_TRUE(zeroDigest.ok());
    EXPECT_EQ(zeroDigest.get().positionSamples, 0U) << "分母 0＝零样本（V-02）";

    // "planContentIdentity=0 计划可编码"：含零样本计划的计划集 canonical
    // 编码成功（字节面无"零计划即不可编码"的隐藏拒绝——NFR-COR-3）。
    PlanSet plans;
    plans.entries.push_back(zeroPlan.plan);
    const RequirementCodec codec;
    EXPECT_NO_THROW((void)encodeOk(codec, RequirementObjectVariant(plans)));

    // 区域已定义而计划集为空 → R6 Warning（零计划——不阻断应用级）。
    RequirementWorkingSet ws;
    ws.root.name = "零计划观测集";
    ws.regions.entries.push_back(zone);
    auto report = RequirementReadinessChecker{}.check(ws, goldenContext());
    EXPECT_FALSE(report.hasBlocking()) << "零计划是 Warning 不是 Blocking";
    bool r6Warning = false;
    for (const auto& item : report.items) {
        if (item.layer == ReadinessCheckLayer::R6
            && item.level == ReadinessFindingLevel::Warning
            && item.diag.code == std::string_view(kReqReadyPlanMissing)) {
            r6Warning = true;
        }
    }
    EXPECT_TRUE(r6Warning) << "R6 零计划应产出 REQ-READY-PLAN-MISSING Warning";
}

/**
 * V-03（acceptance 1——姿态规则解析与非法定位）：黄金五规则样例解析成功
 * 且 resolution 留痕三要素（kind＋目标/解析值——PointAtTarget 参考方向
 * ＝targetPoint 归一化的 IEEE754 确定运算）；非法参数（零向量目标）解析
 * 失败可定位（REQ-READY-POSE-ILLEGAL＋subject/localName/cause 三要素）。
 */
TEST_F(TransportGoldenTest, V03OrientationResolutionAndIllegalLocalized_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09", "AT-23"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));

    const CheckContext ctx = goldenContext();
    for (const auto& j : input.find("points")->items) {
        TaskPoint p = pointOf(j);
        OrientationRuleAnchor anchor{p.objectId, p.name};
        auto resolution = resolveOrientationRule(p.pose.orientation, anchor, ctx);
        ASSERT_TRUE(resolution.ok) << p.name << " 规则应解析成功";
        EXPECT_EQ(resolution.kind, p.pose.orientation.kind);
        if (p.pose.orientation.kind == OrientationRuleKind::Fixed) {
            // Fixed＝参数字面留痕（rad——NFR-COR-3 不做等效角归一，D-REQ-3）。
            ASSERT_TRUE(resolution.referenceRpy.has_value());
            // rw::math::Vector3D 组件访问＝operator[]（无 .x() 访问器）。
            EXPECT_DOUBLE_EQ((*resolution.referenceRpy)[0], p.pose.orientation.fixedRpy[0]);
            EXPECT_DOUBLE_EQ((*resolution.referenceRpy)[1], p.pose.orientation.fixedRpy[1]);
            EXPECT_DOUBLE_EQ((*resolution.referenceRpy)[2], p.pose.orientation.fixedRpy[2]);
        }
        if (p.pose.orientation.kind == OrientationRuleKind::PointAtTarget) {
            // 参考方向＝targetPoint/‖targetPoint‖（(0.2,0.1,0.4) 归一化——
            // IEEE754 确定运算，解析值留痕）。
            ASSERT_TRUE(resolution.referenceDirection.has_value());
            const auto& d = *resolution.referenceDirection;
            const double n = std::sqrt(0.2 * 0.2 + 0.1 * 0.1 + 0.4 * 0.4);
            EXPECT_DOUBLE_EQ(d[0], 0.2 / n);
            EXPECT_DOUBLE_EQ(d[1], 0.1 / n);
            EXPECT_DOUBLE_EQ(d[2], 0.4 / n);
        }
    }

    // 非法可定位：零向量目标（构造边界拒绝码＋就绪层定位诊断三要素）。
    OrientationRule zeroRule;
    zeroRule.kind = OrientationRuleKind::PointAtTarget;
    zeroRule.targetPoint = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    const tk::JsonValue& anchorJson = input.find("points")->items.front();
    const OrientationRuleAnchor anchor{oidOf(anchorJson.find("id")->text),
                                       anchorJson.find("name")->text};
    auto failed = resolveOrientationRule(zeroRule, anchor, ctx);
    ASSERT_FALSE(failed.ok);
    EXPECT_EQ(failed.diag.code, std::string_view(kReqReadyPoseIllegal));
    ASSERT_TRUE(failed.diag.subject.has_value());
    EXPECT_EQ(failed.diag.subject->toCanonical(), anchor.entryId.toCanonical())
        << "subject 回指需求条目（O-36 锚）";
    ASSERT_TRUE(failed.diag.localName.has_value());
    EXPECT_EQ(*failed.diag.localName, anchor.entryName);
    EXPECT_FALSE(failed.diag.cause.empty()) << "cause 三要素非空（可定位）";
}

/**
 * V-04 requirements 侧＋联合登记（acceptance 1——工况重复/错误引用）：
 * 重复 name 构造拒绝（I-REQ-3——不静默加后缀）；悬空场景/工位引用→
 * 就绪 R4 Blocking（可定位）。"漏一个必验工况评估拦截"属联合观测
 * （evidence EV-COV 覆盖矩阵）——本套件仅登记，不把联合行整体写为通过。
 */
TEST_F(TransportGoldenTest, V04DuplicateAndDanglingBlocked_ReqSideJointRegistered_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "EVI-02"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    const OperatingCondition transport = conditionOf(*input.find("conditions")->items.begin());

    // 重复 name（siblingNames 含同名——构造边界 DuplicateName）。
    OperatingConditionService service;
    OperatingConditionSpec dup;
    dup.name = transport.name;
    dup.siblingNames = {transport.name};
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    auto refused = service.createCondition(dup, diags);
    ASSERT_FALSE(refused.ok);
    EXPECT_EQ(refused.error.code, RequirementErrorCode::DuplicateName);

    // 悬空工位引用：appliesTo 指向不存在条目（有效 ObjectId 但闭包外）
    // → R4 Blocking＋REQ-READY-REF-MISSING（V-04"悬空场景引用→Blocking"）。
    RequirementWorkingSet ws;
    OperatingCondition dangling = transport;
    dangling.appliesTo.scope = AppliesToScope::Stations;
    dangling.appliesTo.stations = {oidOf("obj-9900000000000000000000000000ff01")};
    ws.conditions.entries.push_back(dangling);
    auto report = RequirementReadinessChecker{}.check(ws, goldenContext());
    EXPECT_TRUE(report.hasBlocking());
    bool r4Blocking = false;
    for (const auto& item : report.items) {
        if (item.layer == ReadinessCheckLayer::R4
            && item.level == ReadinessFindingLevel::Blocking
            && item.diag.code == std::string_view(kReqReadyRefMissing)) {
            r4Blocking = true;
        }
    }
    EXPECT_TRUE(r4Blocking) << "R4 悬空绑定应 Blocking＋REF-MISSING 定位";
}

/**
 * V-05（acceptance 1——必验工况冻结）：resolveRequiredCases（P-EV-9 唯一
 * 实现点）对照 expected 独立重算表逐字段核对（mandatory≡level==Must、
 * entries 按 caseId 规范序、必验集合=enabled∧mandatory）；需求档派生
 * （deriveRequirementProfile）计数与覆盖汇总同表核对。
 */
TEST_F(TransportGoldenTest, V05RequiredCasesFrozenSchema_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "EVI-01", "EVI-02"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/transport-task-expected.json"));

    std::vector<OperatingCondition> conditions;
    std::vector<TaskPoint> points;
    std::vector<WorkRegion> regions;
    for (const auto& j : input.find("points")->items) { points.push_back(pointOf(j)); }
    for (const auto& j : input.find("regions")->items) { regions.push_back(regionOf(j)); }
    for (const auto& j : input.find("conditions")->items) { conditions.push_back(conditionOf(j)); }

    // 冻结解析对照黄金表（双实现互证——Node 侧按 §6.2 独立重算）。
    OperatingConditionService service;
    const auto resolution = service.resolveRequiredCases(conditions);
    const auto& frozenEntries = expected.find("requiredCasesFrozenSchema")->find("entries")->items;
    ASSERT_EQ(resolution.entries.size(), frozenEntries.size());
    for (std::size_t i = 0; i < frozenEntries.size(); ++i) {
        const auto& e = frozenEntries[i];
        EXPECT_EQ(resolution.entries[i].caseId.toCanonical(), e.find("caseId")->text)
            << "entries 序＝caseId 规范文本字典序（§6.2 确定性）";
        EXPECT_EQ(resolution.entries[i].label, e.find("label")->text);
        EXPECT_EQ(resolution.entries[i].enabled, e.find("enabled")->boolean);
        EXPECT_EQ(resolution.entries[i].mandatory, e.find("mandatory")->boolean);
        // I-REQ-9：mandatory ≡ (level==Must)——与输入工况 level 机械复核。
        const OperatingCondition* source = nullptr;
        for (const auto& c : conditions) {
            if (c.objectId == resolution.entries[i].caseId) { source = &c; }
        }
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(resolution.entries[i].mandatory,
                  source->level == RequirementLevel::Must);
    }
    const double requiredCount =
        expected.find("requiredCasesFrozenSchema")->find("requiredCount")->number;
    EXPECT_EQ(resolution.requiredCount, static_cast<std::size_t>(requiredCount));

    // 需求档派生（§4.8）对照黄金表计数面。
    const auto& profileJson = *expected.find("profile");
    const auto profile = deriveRequirementProfile(points, regions, conditions, service);
    EXPECT_EQ(profile.mustCount, static_cast<std::size_t>(profileJson.find("mustCount")->number));
    EXPECT_EQ(profile.shouldCount, static_cast<std::size_t>(profileJson.find("shouldCount")->number));
    EXPECT_DOUBLE_EQ(profile.minPositionCoverage,
                     profileJson.find("minPositionCoverage")->number);
    EXPECT_EQ(profile.requiredCases.size(),
              static_cast<std::size_t>(profileJson.find("requiredCasesCount")->number));
}

/**
 * V-06 requirements 侧（acceptance 1——镜像黄金断言）：黄金点过参考系
 * 镜像面（法向 Y）反射——位置分量取反（p'=p−2(n·p)n）＋Fixed 姿态黄金
 * 闭式（法向 Y→(−roll,+pitch,−yaw)，§14.6 v0.7 闭式表）；派生条目独立
 * ObjectId＋溯源完整＋sequenceKey 不继承（§7.2）＋源条目零回写
 * （I-REQ-10）。
 */
TEST_F(TransportGoldenTest, V06MirrorGoldenReflectionAndProvenance_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11", "AT-24"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    TaskPoint place;
    for (const auto& j : input.find("points")->items) {
        TaskPoint p = pointOf(j);
        if (p.name == "P-place") { place = p; }
    }
    ASSERT_FALSE(place.name.empty());

    // 过 World 系镜像面、法向 Y（单位化由服务内部完成）。
    MirrorPlaneSpec plane;
    plane.refFrame = place.refFrame;
    plane.axisNormal = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    const TaskPoint sourceBefore = place;
    TemplateArrayService arrayService;
    auto batch = arrayService.applyMirror({place}, plane);
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 1U);

    // 位置反射黄金断言（refFrame 系下分量级——需求数据变换，零跨系变换）。
    const auto& mirrored = batch.newPoints[0];
    ASSERT_TRUE(mirrored.pose.position.tryValue().has_value());
    const auto& pos = *mirrored.pose.position.tryValue();
    EXPECT_DOUBLE_EQ(pos[0], 0.8);   // x 分量不变
    EXPECT_DOUBLE_EQ(pos[1], 0.5);   // y 分量取反（-0.5→+0.5）
    EXPECT_DOUBLE_EQ(pos[2], 0.3);   // z 分量不变
    // Fixed 姿态反射闭式（法向 Y→(−roll,+pitch,−yaw)——v0.7 黄金闭式表）。
    EXPECT_DOUBLE_EQ(mirrored.pose.orientation.fixedRpy[0], -place.pose.orientation.fixedRpy[0]);
    EXPECT_DOUBLE_EQ(mirrored.pose.orientation.fixedRpy[1], place.pose.orientation.fixedRpy[1]);
    EXPECT_DOUBLE_EQ(mirrored.pose.orientation.fixedRpy[2], -place.pose.orientation.fixedRpy[2]);

    // 派生条目独立性（D-REQ-4/I-REQ-10/§7.2）：独立 ObjectId＋溯源完整
    // ＋顺序键不继承＋导入溯源不继承＋源条目字节不变。
    EXPECT_NE(mirrored.objectId, place.objectId);
    ASSERT_TRUE(mirrored.generation.has_value());
    EXPECT_TRUE(mirrored.generation->linked);
    EXPECT_FALSE(mirrored.sequenceKey.has_value()) << "顺序键不继承（§7.2）";
    EXPECT_FALSE(mirrored.importProvenance.has_value()) << "导入溯源不继承";
    EXPECT_EQ(place, sourceBefore) << "源条目零回写（I-REQ-10）";
}

/**
 * V-08/V-09 requirements 侧（acceptance 1——CSV 导入黄金与部分成功＋单位
 * 与 Frame 错误；AT-02/NFR-COR-03）：RawTable 经 io 契约形状直构（P-REQ-8
 * 口径——CSV 方言归 io，本单元只消费）→字典自动识别表头→行级部分成功
 * （正确行保留/错误行定位）→mm/deg 声明换算归一 SI（预览与落库同源）→
 * 悬空 Frame 警告级保留。
 */
TEST_F(TransportGoldenTest, V08V09CsvFacesPartialSuccessAndUnits_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "AT-02", "NFR-COR-03"},
                  std::vector<std::string>{});
    using namespace req;

    // 直构带表头 RawTable（io::RawTable 契约形状——dataRows 与 rows 一致）。
    const auto canonicalHeader = [] {
        std::vector<std::string> header;
        for (int f = 0; f < static_cast<int>(kImportFieldCount); ++f) {
            header.emplace_back(importFieldToken(static_cast<ImportField>(f)));
        }
        return header;
    }();
    io::RawTable table;
    table.report.hasHeader = true;
    table.report.header.assign(canonicalHeader.begin(), canonicalHeader.end());
    const auto addRow = [&table](const std::vector<std::string>& cells) {
        ASSERT_EQ(cells.size(), kImportFieldCount) << "行格数须与 canonical 表头一致";
        table.rows.emplace_back(cells.begin(), cells.end());
    };
    // canonical 20 列序：id,name,process_tag,level,enabled,ref_frame,tcp,
    // x,y,z,roll,pitch,yaw,pos_tol,ori_tol,approach_axis,approach_dist,
    // retract_dist,min_joint_margin,note。
    addRow({"r1", "R1", "Pick", "Must", "true", "World", "", "650", "0", "300",
            "0", "0", "90", "", "", "", "", "", "", ""});
    addRow({"r2", "R2", "Generic", "Must", "true", "World", "", "abc", "0", "100",
            "0", "0", "0", "", "", "", "", "", "", ""});
    addRow({"r1", "R1-dup", "Generic", "Must", "true", "World", "", "100", "0", "100",
            "0", "0", "0", "", "", "", "", "", "", ""});
    addRow({"r3", "R3", "Generic", "Must", "true", "obj-9900000000000000000000000000ff02",
            "", "100", "0", "100", "0", "0", "0", "", "", "", "", "", "", ""});
    table.report.dataRows = table.rows.size();

    RequirementImporter importer;
    const FieldDictionary& dictionary = importer.fieldDictionary();

    // 表头自动识别（冻结字典 canonical 优先——20 列 canonical 名全命中）。
    auto detected = autoDetectMapping(table, dictionary);
    EXPECT_TRUE(detected.mapping.isMapped(ImportField::X));
    EXPECT_TRUE(detected.mapping.isMapped(ImportField::RefFrame));

    // 单位声明：x 列 mm、yaw 列 deg（换算预览与落库同一声明校验/换算入口
    // ——语义单源 NFR-MNT-04；650 mm→0.65 m、90 deg→π/2 rad 黄金换算）。
    ImportUnitOptions units;
    units.setUnit(ImportField::X, "mm");
    units.setUnit(ImportField::Yaw, "deg");
    auto preview = previewUnitConversion(table, detected.mapping, units, dictionary);
    bool sawMillimeter = false;
    bool sawDegree = false;
    for (const auto& entry : preview) {
        if (entry.field == ImportField::X && entry.declaredUnit == "mm") {
            sawMillimeter = true;
            ASSERT_TRUE(entry.siSample.has_value());
            EXPECT_DOUBLE_EQ(*entry.siSample, 0.65) << "650 mm→0.65 m（SI 归一）";
        }
        if (entry.field == ImportField::Yaw && entry.declaredUnit == "deg") {
            sawDegree = true;
            ASSERT_TRUE(entry.siSample.has_value());
            EXPECT_DOUBLE_EQ(*entry.siSample, 1.5707963267948966) << "90 deg→π/2 rad";
        }
    }
    EXPECT_TRUE(sawMillimeter);
    EXPECT_TRUE(sawDegree);

    // 导入：部分成功（错行 r2 保留正确行；重复 id 行定位；悬空 Frame 警告）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    auto outcome = importer.mapCsv(table, detected.mapping, units, diags);
    EXPECT_EQ(outcome.status, ImportOutcome::Status::Partial);
    ASSERT_EQ(outcome.entries.size(), 2U) << "正确行保留（r1/r3；r2 错行与 r1 重复）";
    EXPECT_TRUE(hasDiagCode(outcome.rowErrors, std::string(kReqImportRowError).c_str()))
        << "错误行定位（REQ-IMPORT-ROW-ERROR）";
    EXPECT_TRUE(hasDiagCode(outcome.rowErrors, std::string(kReqImportDuplicateId).c_str()))
        << "重复 id 行级定位（REQ-IMPORT-DUPLICATE-ID）";
    EXPECT_TRUE(hasDiagCode(diags, std::string(kReqImportFrameUnknown).c_str()))
        << "悬空 Frame 警告级（REQ-IMPORT-FRAME-UNKNOWN——条目保留待解析）";
    // SI 归一落库断言（导入条目 x=0.65 m——与预览同源换算）。
    for (const auto& e : outcome.entries) {
        if (e.name == "R1") {
            ASSERT_TRUE(e.pose.position.tryValue().has_value());
            EXPECT_DOUBLE_EQ((*e.pose.position.tryValue())[0], 0.65)
                << "650 mm 落库 0.65 m";
        }
    }
    EXPECT_EQ(outcome.entries[0].objectId, sdurws::ird::core::ObjectId{})
        << "导入条目 ObjectId 恒全零待命令分配（O-36——确定性解耦）";
}

/**
 * V-10 requirements 侧（acceptance 1——JSON 导入/导出副本；REQ-12/AT-24）：
 * JSON 导入→导出副本→重导入逐字段一致（roundtrip）；导出经原子就位
 * （失败面回滚由契约测试 FaultInterceptor 注入——此处断言成功路径字节
 * 面与零修订零项目触碰的结构面：签名无项目句柄）。
 */
TEST_F(TransportGoldenTest, V10JsonRoundtripAndCopyExport_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12", "AT-24"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(workDir, nullptr);

    // 黄金 JSON（§7.4 文档投影 schema——schemaVersion＋points 记录）。
    const std::string jsonText = R"({
  "schemaVersion": 1,
  "points": [
    { "id": "g1", "name": "G-取件", "process_tag": "Pick", "level": "Must",
      "enabled": "true", "ref_frame": "World",
      "x": 0.65, "y": -0.5, "z": 0.3, "note": "黄金 roundtrip 载体" }
  ]
})";
    const std::vector<std::uint8_t> bytes(jsonText.begin(), jsonText.end());
    RequirementImporter importer;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    auto first = importer.mapJson(bytes, diags);
    ASSERT_EQ(first.status, ImportOutcome::Status::Completed) << "首导全成功";
    ASSERT_EQ(first.entries.size(), 1U);

    // 副本导出（正式形态 draft=false——写 workDir，测试唯一合法落盘位）。
    const auto targetPath = workDir->path() / "roundtrip_copy.json";
    RequirementWorkingSet ws;
    ws.points.entries = first.entries;
    auto exported = importer.exportCopy(ws, ExportFormat::Json,
                                        ExportTarget{targetPath, false});
    ASSERT_TRUE(exported.ok) << exported.error;
    EXPECT_GT(exported.bytesWritten, 0U);

    // 重导入→逐字段一致（域字段全等；importProvenance 为源侧事实各指其源
    // ——按设计归零后比域字段，ImportTest V-10 同款口径）。
    const std::vector<std::uint8_t> exportedBytes = [&] {
        const std::string text = readTextFile(targetPath);
        return std::vector<std::uint8_t>(text.begin(), text.end());
    }();
    std::vector<sdurws::ird::core::DiagnosticRecord> diags2;
    auto second = importer.mapJson(exportedBytes, diags2);
    ASSERT_EQ(second.status, ImportOutcome::Status::Completed);
    ASSERT_EQ(second.entries.size(), 1U);
    auto lhs = first.entries[0];
    auto rhs = second.entries[0];
    lhs.importProvenance.reset();
    rhs.importProvenance.reset();
    EXPECT_TRUE(lhs == rhs) << "roundtrip 逐字段一致（REQ-12/AT-24）";
}

/**
 * V-15/V-16/V-19 requirements 侧＋联合登记（acceptance 1——快照绑定/
 * 切片失效/下游消费的输入输出面）：对象级 cv 变化范围（改工况 note→
 * 仅 ConditionSet 字节变；点/区域/计划集合字节不变——四条独立失效面的
 * 字节级演示）；必验解析与计划分母确定性（requiredCaseSetId 输入面）；
 * 角色键词表语法面（req.* 四键——requirements 交出义务的 evidence 侧
 * 语法判定）。联合行整体（快照组装/迟到结果/切片声明消费）仅登记。
 */
TEST_F(TransportGoldenTest, V15V16V19SliceFacesJointRegistered_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05", "CON-06", "AT-05", "AT-03"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/transport-task-expected.json"));

    PointSet points;
    for (const auto& j : input.find("points")->items) { points.entries.push_back(pointOf(j)); }
    sortEntriesByObjectId(points.entries);
    ConditionSet conditions;
    for (const auto& j : input.find("conditions")->items) { conditions.entries.push_back(conditionOf(j)); }
    sortEntriesByObjectId(conditions.entries);
    PlanSet plans;
    RegionSet regions;
    for (const auto& j : input.find("regions")->items) {
        WorkRegion r = regionOf(j);
        regions.entries.push_back(r);
        // R-zone 计划（buildPlan 规范化——分母核算输入面）。
        SamplingPlanBuilder builder;
        SamplingPlanSpec spec;
        spec.planObjectId = oidOf("obj-1d000000000000000000000000000001");
        spec.positionSampling = r.positionSampling;
        spec.orientationSampling = r.orientationSampling;
        std::vector<sdurws::ird::core::DiagnosticRecord> diags;
        auto plan = builder.buildPlan(r, spec, diags);
        ASSERT_TRUE(plan.ok);
        plans.entries.push_back(plan.plan);
    }
    sortEntriesByObjectId(regions.entries);
    sortEntriesByObjectId(plans.entries);

    const RequirementCodec codec;
    const auto basePoints = encodeOk(codec, RequirementObjectVariant(points));
    const auto baseRegions = encodeOk(codec, RequirementObjectVariant(regions));
    const auto baseConditions = encodeOk(codec, RequirementObjectVariant(conditions));
    const auto basePlans = encodeOk(codec, RequirementObjectVariant(plans));

    // V-16：改工况 note（保守失效面——R-REQ-2）→ 仅 ConditionSet 字节变。
    ConditionSet touched = conditions;
    touched.entries[0].note += "（note 变更）";
    EXPECT_NE(encodeOk(codec, RequirementObjectVariant(touched)), baseConditions);
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(points)), basePoints)
        << "点集字节不变（独立失效面）";
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(regions)), baseRegions)
        << "区域集字节不变";
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(plans)), basePlans)
        << "计划集字节不变";

    // V-15/V-19：requiredCaseSetId 输入面确定性（同输入同解析同字节）＋
    // 计划分母对照黄金表（plannedXxxSamples 核算输入——V-19 联合的
    // requirements 侧）。
    OperatingConditionService service;
    EXPECT_TRUE(service.resolveRequiredCases(conditions.entries)
                == service.resolveRequiredCases(conditions.entries));
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(conditions)), baseConditions)
        << "同输入→同编码字节（NFR-COR-01/02）";
    const auto& denominators = expected.find("planDenominators")->items;
    ASSERT_EQ(denominators.size(), plans.entries.size());
    SamplingPlanBuilder builder;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    for (std::size_t i = 0; i < denominators.size(); ++i) {
        auto digest = builder.digest(plans.entries[i], diags);
        ASSERT_TRUE(digest.ok());
        EXPECT_EQ(digest.get().positionSamples,
                  static_cast<std::uint64_t>(denominators[i].find("positionSamples")->number));
        EXPECT_EQ(digest.get().orientationSamples,
                  static_cast<std::uint64_t>(denominators[i].find("orientationSamples")->number));
    }

    // 角色键词表语法面（§8.4 四键——evidence DependencyKey 语法判定；
    // 联合观测的登记面：声明消费归 evidence 侧）。
    for (const char* key : {"req.points", "req.regions", "req.conditions",
                            "req.sampling-plans"}) {
        EXPECT_TRUE(sdurws::ird::evidence::isValidDependencyKey(key)) << key;
    }
}

/**
 * V-17/V-18 requirements 侧（acceptance 1——显示单位变化零失效＋同内容
 * 跨项目身份隔离）：需求对象恒 SI 存储无显示单位态（重复编码逐字节——
 * 显示单位切换在 requirements 面零可观测效果）；同内容不同 ObjectId 的
 * 集合编码不同（身份不跨项目复用——CON-01/ARC-04）、同 id 同内容编码
 * 相同（ContentVersion 可相同面）。
 */
TEST_F(TransportGoldenTest, V17V18UnitAndIdentityFaces_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "CON-01", "ARC-04", "AT-27"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));

    PointSet points;
    for (const auto& j : input.find("points")->items) { points.entries.push_back(pointOf(j)); }
    sortEntriesByObjectId(points.entries);
    const RequirementCodec codec;
    const auto bytes = encodeOk(codec, RequirementObjectVariant(points));

    // V-17：重复编码逐字节（对象无显示单位投影字段——SI 单一权威
    // SA-12/D-REQ-3；KIN-12 显示单位切换的 requirements 面零失效）。
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(points)), bytes);

    // V-18：同内容不同 id → 字节不同（ObjectId 是身份——不跨项目复用）；
    // 同 id 同内容 → 字节相同（同内容 ⇒ ContentVersion 可相同）。
    PointSet other = points;
    for (auto& e : other.entries) {
        auto fresh = sdurws::ird::core::ObjectId::generate();
        e.objectId = fresh;
    }
    sortEntriesByObjectId(other.entries);
    EXPECT_NE(encodeOk(codec, RequirementObjectVariant(other)), bytes)
        << "ObjectId 独立 ⇒ 编码不同（身份隔离）";
    PointSet same = points;
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(same)), bytes);
}

/**
 * V-20/V-21 requirements 侧＋联合登记（acceptance 1——AT-04 预览零修订＋
 * AT-30 回填影响面）：预览/即时校验纯函数（同输入同产出；零编辑器/零
 * 修订/零草稿状态变化——预览输入无内容身份承诺）；需求对象对外部工具
 * 对象变化零响应（tcpRef 浅引用——语义解析归评估，本单元零动作）。
 */
TEST_F(TransportGoldenTest, V20V21PreviewZeroMutationJointRegistered_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01", "SEL-10", "AT-04", "AT-30"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/transport-task.json"));

    PointSet points;
    for (const auto& j : input.find("points")->items) { points.entries.push_back(pointOf(j)); }
    sortEntriesByObjectId(points.entries);

    // V-20：就绪检查纯函数（同输入同报告——预览语义零副作用面）。
    RequirementWorkingSet ws;
    ws.points.entries = points.entries;
    const auto ctx = goldenContext();
    auto r1 = RequirementReadinessChecker{}.check(ws, ctx);
    auto r2 = RequirementReadinessChecker{}.check(ws, ctx);
    EXPECT_TRUE(r1 == r2) << "纯函数——预览/即时校验零状态突变";

    // V-21：外部工具对象变化（本单元零 API 消费其字节——浅引用）时需求
    // 对象字节零变化（结构性：编码只依赖需求值模型自身）。
    const RequirementCodec codec;
    const auto before = encodeOk(codec, RequirementObjectVariant(points));
    EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(points)), before)
        << "外部状态不在编码输入内——回填影响面＝需求侧零动作（SEL-10）";
}

// =====================================================================
// 数据集 2/4：部分位姿约束
// =====================================================================

/**
 * V-03 掩码/四态/规则字面（acceptance 1/2——部分位姿黄金断言）：六分量
 * 掩码部分约束、position not-provided 合法态（缺失≠0）、Fixed rpy 等效
 * 角字面不归一（D-REQ-3）、PointAtTarget 参考方向独立重算对照、
 * ToolRollFree 区间字面、AlignFrame/AlignGeometryNormal 闭包浅核对通过。
 */
TEST_F(PartialPoseGoldenTest, V03MasksStatesAndRuleLiterals_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "REQ-09", "AT-23"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/partial-pose.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/partial-pose-expected.json"));
    const auto& projections = expected.find("pointProjections")->items;
    const auto& inputPoints = input.find("points")->items;
    ASSERT_EQ(projections.size(), inputPoints.size());

    const CheckContext ctx = goldenContext();
    for (std::size_t i = 0; i < inputPoints.size(); ++i) {
        TaskPoint p = pointOf(inputPoints[i]);
        EXPECT_FALSE(validateTaskPoint(p).has_value()) << p.name << " 应合法";
        const auto& proj = projections[i];

        // 掩码六分量（部分约束——x/y/z/roll/pitch/yaw 布尔序）。
        const auto& mask = proj.find("constrainedDof")->items;
        ASSERT_EQ(mask.size(), 6U);
        EXPECT_EQ(p.pose.constrainedDof.x, mask[0].boolean);
        EXPECT_EQ(p.pose.constrainedDof.y, mask[1].boolean);
        EXPECT_EQ(p.pose.constrainedDof.z, mask[2].boolean);
        EXPECT_EQ(p.pose.constrainedDof.roll, mask[3].boolean);
        EXPECT_EQ(p.pose.constrainedDof.pitch, mask[4].boolean);
        EXPECT_EQ(p.pose.constrainedDof.yaw, mask[5].boolean);
        EXPECT_TRUE(p.pose.constrainedDof.any()) << "I-REQ-5 至少一真";

        // position 四态（not-provided 合法——PP-orientation-only 纯姿态）。
        EXPECT_EQ(std::string(proj.find("positionState")->text),
                  fieldStateToken(p.pose.position.state()))
            << p.name << " 四态投影对照黄金表";
        if (std::string(proj.find("positionState")->text) == "provided") {
            ASSERT_TRUE(p.pose.position.tryValue().has_value());
        } else {
            EXPECT_FALSE(p.pose.position.tryValue().has_value())
                << "not-provided 态取值必须被类型面切断（缺失≠0）";
        }

        // 规则字面与解析（Fixed 字面不归一；PointAtTarget 方向独立重算；
        // 引用型规则闭包核对通过留痕目标）。
        OrientationRuleAnchor anchor{p.objectId, p.name};
        auto resolution = resolveOrientationRule(p.pose.orientation, anchor, ctx);
        ASSERT_TRUE(resolution.ok) << p.name;
        const std::string kind = proj.find("orientationKind")->text;
        if (kind == "Fixed") {
            const auto& rpy = proj.find("fixedRpy")->items;
            EXPECT_DOUBLE_EQ(p.pose.orientation.fixedRpy[0], rpy[0].number)
                << "Fixed rpy 字面保真（±π/−π/2 等效角不归一——D-REQ-3）";
        } else if (kind == "PointAtTarget") {
            const auto& dir = proj.find("referenceDirection")->items;
            ASSERT_TRUE(resolution.referenceDirection.has_value());
            EXPECT_DOUBLE_EQ((*resolution.referenceDirection)[0], dir[0].number);
            EXPECT_DOUBLE_EQ((*resolution.referenceDirection)[1], dir[1].number);
            EXPECT_DOUBLE_EQ((*resolution.referenceDirection)[2], dir[2].number);
        } else if (kind == "ToolRollFree") {
            EXPECT_DOUBLE_EQ(p.pose.orientation.rollRange.min,
                             proj.find("rollRange")->find("min")->number);
            EXPECT_DOUBLE_EQ(p.pose.orientation.rollRange.max,
                             proj.find("rollRange")->find("max")->number);
        } else if (kind == "AlignFrame") {
            ASSERT_TRUE(resolution.targetObjectId.has_value());
            EXPECT_EQ(resolution.targetObjectId->toCanonical(),
                      proj.find("targetFrameObjectId")->text);
        } else if (kind == "AlignGeometryNormal") {
            ASSERT_TRUE(resolution.targetObjectId.has_value());
            EXPECT_EQ(resolution.targetObjectId->toCanonical(),
                      proj.find("targetSceneObject")->text);
            EXPECT_EQ(resolution.invertNormal, proj.find("invertNormal")->boolean);
            ASSERT_TRUE(resolution.feature.has_value());
        }
    }
}

/**
 * 负样例逐条（acceptance 5——不静默转 0 或默认通过）：零向量目标/
 * rollRange 逆序/缺 feature/全自由掩码/零容差——构造边界首违例码与
 * expected 独立转写表逐条一致（requirementErrorCodeToken 机械比对）。
 */
TEST_F(PartialPoseGoldenTest, NegativeSamplesFirstErrorCode_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03", "AT-23"},
                  std::vector<std::string>{});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/partial-pose.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/partial-pose-expected.json"));
    const auto& negatives = expected.find("negativeExpectations")->items;
    ASSERT_EQ(negatives.size(), input.find("negativeCases")->items.size());

    for (std::size_t i = 0; i < negatives.size(); ++i) {
        const auto& n = input.find("negativeCases")->items[i];
        std::optional<RequirementError> violation;
        const std::string testCase = n.find("case")->text;
        if (testCase == "zero-vector-target") {
            violation = validateOrientationRule(ruleOf(n.find("orientation")));
        } else if (testCase == "roll-range-inverted") {
            violation = validateOrientationRule(ruleOf(n.find("orientation")));
        } else if (testCase == "missing-feature") {
            violation = validateOrientationRule(ruleOf(n.find("orientation")));
        } else if (testCase == "all-dof-free") {
            violation = validatePoseConstraint(poseOf(n.find("pose")));
        } else if (testCase == "zero-tolerance") {
            const auto* t = n.find("tolerance");
            ToleranceSpec spec;
            spec.positionTolerance = t->find("positionTolerance")->number;
            spec.orientationTolerance = t->find("orientationTolerance")->number;
            violation = validateTolerance(spec);
        }
        ASSERT_TRUE(violation.has_value()) << testCase << " 必须被拒绝（不静默）";
        EXPECT_EQ(std::string(requirementErrorCodeToken(violation->code)),
                  negatives[i].find("expectedErrorCode")->text)
            << testCase << " 首违例码对照黄金表";
    }
}

// =====================================================================
// 数据集 3/4：区域与采样定义
// =====================================================================

/**
 * V-02＋确定性（acceptance 1/2/5——D-REQ-2 规范化黄金断言）：buildPlan
 * 规范化计数对照 expected 独立重算（IEEE754 位级：0.6/0.2→counts=3）；
 * 同义计划对（不同 spacing→同 counts）规范化产出相等（planContentIdentity
 * 同一输入——D-REQ-2）；零样本计划合法存储可编码；changed 标志面。
 */
TEST_F(RegionSamplingGoldenTest, V02NormalizationSynonymAndZeroSample_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-04", "NFR-COR-02"},
                  std::vector<std::string>{"AT-05"});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/region-sampling.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/region-sampling-expected.json"));

    std::map<std::string, WorkRegion> regions;
    for (const auto& j : input.find("regions")->items) {
        WorkRegion r = regionOf(j);
        EXPECT_FALSE(validateWorkRegion(r).has_value()) << r.name;
        regions.emplace(r.name, r);
    }
    const auto& expectedPlans = expected.find("plans")->items;
    const auto& planSpecs = input.find("planSpecs")->items;
    ASSERT_EQ(expectedPlans.size(), planSpecs.size());

    SamplingPlanBuilder builder;
    WorkRegionService regionService;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    std::optional<NormalizedSampling> spacingA;
    std::optional<NormalizedSampling> spacingB;
    for (std::size_t i = 0; i < planSpecs.size(); ++i) {
        const auto& specJson = planSpecs[i];
        const std::string regionName = specJson.find("region")->text;
        const WorkRegion& region = regions.at(regionName);
        SamplingPlanSpec spec;
        spec.planObjectId = oidOf(specJson.find("planId")->text);
        // 位置采样读取（Grid/GridBySpacing 两形态）。
        const auto* ps = specJson.find("positionSampling");
        if (std::string(ps->find("method")->text) == "Grid") {
            spec.positionSampling.method = PositionSamplingMethod::Grid;
            const auto& counts = ps->find("counts")->items;
            for (std::size_t k = 0; k < 3; ++k) {
                spec.positionSampling.counts[k] =
                    static_cast<std::uint32_t>(counts[k].number);
            }
        } else {
            spec.positionSampling.method = PositionSamplingMethod::GridBySpacing;
            const auto& sp = ps->find("spacing")->items;
            for (std::size_t k = 0; k < 3; ++k) {
                spec.positionSampling.spacing[k] = sp[k].number;  // m
            }
        }
        const auto* os = specJson.find("orientationSampling");
        spec.orientationSampling.directionSamples =
            static_cast<std::uint32_t>(os->find("directionSamples")->number);
        spec.orientationSampling.rollSamples =
            static_cast<std::uint32_t>(os->find("rollSamples")->number);
        spec.orientationSampling.methodToken = os->find("methodToken")->text;
        spec.note = specJson.find("note")->text;

        // 规范化面（normalizeSampling——D-REQ-2 单点）。
        auto normalized = regionService.normalizeSampling(spec.positionSampling,
                                                          region.box.size, diags);
        ASSERT_TRUE(normalized.ok()) << regionName;
        EXPECT_EQ(normalized.get().changed,
                  spec.positionSampling.method == PositionSamplingMethod::GridBySpacing)
            << "changed＝是否发生规范化改写";

        // buildPlan 规范化计数对照黄金表（floor 规则独立重算——位级行为
        // 锁定：0.6/0.2=2.9999…→counts=3，整数直觉 4 恰是夹具钉死面）。
        auto plan = builder.buildPlan(region, spec, diags);
        ASSERT_TRUE(plan.ok) << plan.error.detail;
        const auto& proj = expectedPlans[i];
        for (std::size_t k = 0; k < 3; ++k) {
            EXPECT_EQ(plan.plan.positionSampling.counts[k],
                      static_cast<std::uint32_t>(proj.find("counts")->items[k].number))
                << regionName << " counts[" << k << "]";
        }
        if (regionName == "RS-spacing-a") { spacingA = normalized.get(); }
        if (regionName == "RS-spacing-b") { spacingB = normalized.get(); }
        if (regionName == "RS-grid") {
            EXPECT_FALSE(normalized.get().changed) << "Grid 原样承载";
        }

        // 零样本计划：分母 0＋canonical 可编码（planContentIdentity=0 面）。
        if (regionName == "RS-zero") {
            auto digest = builder.digest(plan.plan, diags);
            ASSERT_TRUE(digest.ok());
            EXPECT_EQ(digest.get().positionSamples, 0U);
            PlanSet plans;
            plans.entries.push_back(plan.plan);
            const RequirementCodec codec;
            EXPECT_NO_THROW((void)encodeOk(codec, RequirementObjectVariant(plans)));
        }
    }

    // 同义计划对：不同 spacing 规范化到同 counts（采样内容身份同一输入
    // ——D-REQ-2 同义计划同身份；计划字节因 regionRef 不同而不同）。
    ASSERT_TRUE(spacingA.has_value() && spacingB.has_value());
    EXPECT_EQ(spacingA->normalized, spacingB->normalized)
        << "同义计划规范化产出相等（D-REQ-2）";
    EXPECT_TRUE(spacingA->changed && spacingB->changed);

    // 负样例（acceptance 5——不静默）。
    WorkRegionSpec degenerate;
    degenerate.name = "R-bad";
    degenerate.box.size = rw::math::Vector3D<double>(0.2, 0.0, 0.2);  // m
    auto refused = regionService.createRegion(degenerate, diags);
    EXPECT_FALSE(refused.ok);
    PositionSampling zeroSpacing;
    zeroSpacing.method = PositionSamplingMethod::GridBySpacing;
    zeroSpacing.spacing = {0.2, 0.0, 0.2};  // m（0 非法）
    auto spacingRefused =
        regionService.normalizeSampling(zeroSpacing, regions.at("RS-grid").box.size, diags);
    ASSERT_FALSE(spacingRefused.ok());
    CoverageTargets outOfRange;
    outOfRange.minPositionCoverage = 1.5;
    EXPECT_TRUE(validateCoverageTargets(outOfRange).has_value())
        << "覆盖率 1.5 越界拒绝（不截断不静默）";
}

// =====================================================================
// 数据集 4/4：负载事件
// =====================================================================

/**
 * V-04 面＋四态黄金断言（acceptance 1/2——负载四态全谱/事件绑定/适用
 * 范围）：SourcedValue 四态投影对照黄金表（零值 mass=0.0 provided 合法
 * ——空载非缺失；invalid 保留原串——NFR-COR-3 不清洗；not-provided/
 * not-applicable 态取值被类型面切断）；事件三类型绑定与 duration 字面
 * （0.0 与 null 严格区分）；appliesTo 三范围（D-REQ-5）。
 */
TEST_F(PayloadEventsGoldenTest, V04FourStatesEventsAndAppliesTo_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "EVI-02", "NFR-COR-03"},
                  std::vector<std::string>{"AT-02", "AT-24"});
    using namespace req;
    ASSERT_NE(dataset, std::nullopt);
    const tk::JsonValue input = parseDatasetJson(inputPath("inputs/payload-events.json"));
    const tk::JsonValue expected = parseDatasetJson(
        expectedPath("expected/payload-events-expected.json"));

    std::map<std::string, OperatingCondition> byName;
    for (const auto& j : input.find("conditions")->items) {
        OperatingCondition c = conditionOf(j);
        EXPECT_FALSE(validateOperatingCondition(c).has_value()) << c.name << " 应合法";
        byName.emplace(c.name, c);
    }
    ASSERT_EQ(byName.count("PE-full"), 1U);
    const OperatingCondition& full = byName.at("PE-full");
    ASSERT_EQ(full.payloads.size(), 2U);

    // 四态投影对照（负载一：全 provided 含边角；负载二：零值/不适用/缺失）。
    EXPECT_DOUBLE_EQ(full.payloads[0].mass.tryValue().value_or(-1.0), 12.5);  // kg
    const auto& com0 = *full.payloads[0].com.tryValue();
    EXPECT_DOUBLE_EQ(com0[0], 0.05);   // m（正负抵消对的一侧）
    EXPECT_DOUBLE_EQ(com0[1], -0.05);  // m（x+y=0——正负抵消）
    EXPECT_DOUBLE_EQ(com0[2], 1e-18);  // m（近零位面保真——IEEE754 位模式）
    EXPECT_DOUBLE_EQ(full.payloads[0].inertia.tryValue().value_or(-1.0), 0.125);  // kg·m²
    EXPECT_DOUBLE_EQ(full.payloads[1].mass.tryValue().value_or(-1.0), 0.0)
        << "零值 provided 合法（空载≠缺失）";
    EXPECT_FALSE(full.payloads[1].com.tryValue().has_value())
        << "not-applicable 态取值被切断（ERR-01 显式标记不伪造数值）";
    EXPECT_FALSE(full.payloads[1].inertia.tryValue().has_value());

    // 缺失谱（PE-partial）：mass/com not-provided＋inertia invalid 保留原串。
    ASSERT_EQ(byName.count("PE-partial"), 1U);
    const OperatingCondition& partial = byName.at("PE-partial");
    ASSERT_EQ(partial.payloads.size(), 1U);
    EXPECT_FALSE(partial.payloads[0].mass.tryValue().has_value())
        << "not-provided（DataInsufficient 降级预告——不断言不转 0）";
    EXPECT_EQ(partial.payloads[0].inertia.state(),
              sdurws::ird::core::FieldState::Invalid);
    // invalid 态取值抛错（"缺失＝零/非法＝默认"通道在类型面被切断）。
    EXPECT_THROW((void)partial.payloads[0].inertia.value(), sdurws::ird::core::CoreError);

    // 事件表对照（类型/绑定锚/duration 0.0 与 null 严格区分）。
    const auto& eventTable = expected.find("eventTable")->items;
    std::size_t eventIndex = 0;
    for (const auto& j : input.find("conditions")->items) {
        const OperatingCondition c = conditionOf(j);
        for (const auto& ev : c.events) {
            ASSERT_LT(eventIndex, eventTable.size());
            const auto& proj = eventTable[eventIndex++];
            EXPECT_EQ(std::string(conditionEventTypeToken(ev.type)),
                      proj.find("type")->text);
            EXPECT_EQ(ev.stationRef.toCanonical(), proj.find("stationRef")->text);
            if (ev.durationS.has_value()) {
                EXPECT_TRUE(proj.find("hasDuration")->boolean);
                EXPECT_DOUBLE_EQ(*ev.durationS, proj.find("durationS")->number);
            } else {
                EXPECT_FALSE(proj.find("hasDuration")->boolean) << "null 与 0.0 严格区分";
            }
        }
    }
    EXPECT_EQ(eventIndex, eventTable.size());

    // appliesTo 三范围（D-REQ-5：绑定方向唯一——工况侧声明）。
    EXPECT_EQ(full.appliesTo.scope, AppliesToScope::Stations);
    EXPECT_EQ(byName.at("PE-partial").appliesTo.scope, AppliesToScope::AllStations);
    EXPECT_EQ(byName.at("PE-idle").appliesTo.scope, AppliesToScope::None);
}

/**
 * 负样例＋R4 拦截（acceptance 5——缺失引用不静默通过）：重复名拒绝；
 * Stations 空清单拒绝（§4.7 非法组合）；有效但闭包外工位引用构造收下
 * →就绪 R4 Blocking（§8.1 分层语义——构造查结构半区、跨闭包归就绪层）；
 * invalid 空原串 core 工厂拒绝。
 */
TEST_F(PayloadEventsGoldenTest, NegativeCasesAndR4Blocking_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03", "REQ-04"},
                  std::vector<std::string>{});
    using namespace req;

    OperatingConditionService service;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;

    // 重复名（I-REQ-3——不静默加后缀）。
    OperatingConditionSpec dup;
    dup.name = "PE-full";
    dup.siblingNames = {"PE-full"};
    auto refused = service.createCondition(dup, diags);
    ASSERT_FALSE(refused.ok);
    EXPECT_EQ(refused.error.code, RequirementErrorCode::DuplicateName);

    // Stations 空清单（§4.7 非法组合——构造边界拒绝）。
    OperatingConditionSpec emptyStations;
    emptyStations.name = "PE-bad";
    emptyStations.appliesTo.scope = AppliesToScope::Stations;
    emptyStations.appliesTo.stations = {};
    auto comboRefused = service.createCondition(emptyStations, diags);
    ASSERT_FALSE(comboRefused.ok);

    // 悬空绑定分层：有效 ObjectId 但不在工作集 → 构造收下（结构半区通过）
    // → 就绪 R4 Blocking（REQ-READY-REF-MISSING）。
    OperatingCondition dangling;
    dangling.objectId = oidOf("obj-2e0000000000000000000000000000aa");
    dangling.name = "PE-dangling";
    ConditionEvent event;
    event.type = ConditionEventType::Grasp;
    event.stationRef = oidOf("obj-9900000000000000000000000000ff01");
    dangling.events.push_back(event);
    EXPECT_FALSE(validateOperatingCondition(dangling).has_value())
        << "结构半区通过（stationRef 是有效 ObjectId）";
    RequirementWorkingSet ws;
    ws.conditions.entries.push_back(dangling);
    auto report = RequirementReadinessChecker{}.check(ws, goldenContext());
    EXPECT_TRUE(report.hasBlocking());
    bool r4Blocking = false;
    for (const auto& item : report.items) {
        if (item.layer == ReadinessCheckLayer::R4
            && item.level == ReadinessFindingLevel::Blocking
            && item.diag.code == std::string_view(kReqReadyRefMissing)) {
            r4Blocking = true;
        }
    }
    EXPECT_TRUE(r4Blocking) << "跨闭包存在性归就绪层 R4（§8.1 分层）";

    // invalid 空原串：core 工厂拒绝（非法态的存在意义是保留原串）。
    EXPECT_THROW((void)sdurws::ird::core::SourcedValue<double>::invalid(""),
                 sdurws::ird::core::CoreError);
}

// =====================================================================
// V-11：空需求集合（acceptance 1——空集合法＋R5 Warning 投影）
// =====================================================================

/**
 * V-11（acceptance 1——空需求集合）：空集合法保存（五空对象 canonical
 * 编码/解码往返）；R5 Warning 投影（无启用必验工况——REQ-READY-NO-
 * REQUIRED-CASE，不阻断应用级）；ReadinessSummary 投影（valid=true＋
 * 空清单——Warning 不构成①级输入门禁拒绝；正式评估拦截归 evidence，
 * 联合登记）。
 */
TEST(EmptySetFaces, V11EmptySetLegalAndR5WarningProjection_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "EVI-01"},
                  std::vector<std::string>{});
    using namespace req;

    // 空集合法保存：五空对象编码/解码往返逐字节（§4.7——空集合法实例）。
    const RequirementCodec codec;
    RequirementWorkingSet empty;
    empty.root.name = "空需求集";
    const auto rootBytes = encodeOk(codec, RequirementObjectVariant(empty.root));
    const auto pointBytes = encodeOk(codec, RequirementObjectVariant(empty.points));
    auto decoded = codec.decode(pointBytes, kCurrentRequirementFormatVersion);
    ASSERT_TRUE(decoded.ok());
    const auto* decodedPoints = std::get_if<PointSet>(&decoded.get());
    ASSERT_NE(decodedPoints, nullptr);
    EXPECT_TRUE(decodedPoints->entries.empty());
    EXPECT_EQ(encodeOk(codec, decoded.get()), pointBytes) << "空集 roundtrip 逐字节";

    // R5 Warning 投影：无启用必验工况（不阻断——正式拦截归 evidence 联合）。
    auto report = RequirementReadinessChecker{}.check(empty, goldenContext());
    EXPECT_FALSE(report.hasBlocking());
    bool r5Warning = false;
    for (const auto& item : report.items) {
        if (item.layer == ReadinessCheckLayer::R5
            && item.level == ReadinessFindingLevel::Warning
            && item.diag.code == std::string_view(kReqReadyNoRequiredCase)) {
            r5Warning = true;
        }
    }
    EXPECT_TRUE(r5Warning) << "R5 空必验集 → REQ-READY-NO-REQUIRED-CASE Warning";

    // ReadinessSummary 投影（evidence §6.4① 数据形状——两字段直投）。
    auto summary = RequirementReadinessChecker{}.readinessSummary(report);
    EXPECT_TRUE(summary.valid) << "Warning 不构成①级门禁拒绝（保守门禁仅 Blocking）";
    EXPECT_TRUE(summary.invalidMustItems.empty());
}

// =====================================================================
// 确定性断言（acceptance 5——NFR-COR-01/02/03 黄金锁定）
// =====================================================================

/**
 * 同输入→同编码字节＋ObjectId 字典序稳定排序复验（acceptance 5）：四套
 * 黄金数据集全部对象——重复编码逐字节相等；乱序集合被编码门拒绝（
 * canonical 形态前提）；经 sortEntriesByObjectId 规范化后与原序字节全等
 * （I-REQ-1 稳定排序复验）＋存储序严格升序机械核对。
 */
TEST(DeterminismFaces, SameInputSameBytesAndCanonicalSort_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{});
    using namespace req;

    // 四套数据集装载（lint 语义——GoldenDataset::load 全链含完整性）。
    auto transport = tk::GoldenDataset::load({kDsTransport, kDsVersion});
    auto partial = tk::GoldenDataset::load({kDsPartialPose, kDsVersion});
    auto region = tk::GoldenDataset::load({kDsRegionSampling, kDsVersion});
    auto payload = tk::GoldenDataset::load({kDsPayloadEvents, kDsVersion});

    const RequirementCodec codec;
    std::size_t checkedSets = 0;
    const auto checkSet = [&codec, &checkedSets](const auto& set) {
        const auto bytes = encodeOk(codec, RequirementObjectVariant(set));
        // 同输入→同编码字节（NFR-COR-01/02）。
        EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(set)), bytes);

        using SetType = std::remove_cv_t<std::remove_reference_t<decltype(set)>>;
        SetType shuffled = set;
        if constexpr (!std::is_same_v<SetType, RequirementSet>) {
            // 乱序：编码门拒绝（编码器只接受规范化集合——不静默重排）。
            std::reverse(shuffled.entries.begin(), shuffled.entries.end());
            // 规范化写出复验（I-REQ-1——编码器按"规范化副本排序写出"：
            // 任意输入序必得同字节；decode 校验链第③步再核字节规范序，
            // 双向闭环钉死 canonical 形态）。
            EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(shuffled)), bytes)
                << "乱序输入编码后与原序逐字节一致（稳定排序写出）";
            sortEntriesByObjectId(shuffled.entries);
            EXPECT_EQ(encodeOk(codec, RequirementObjectVariant(shuffled)), bytes)
                << "显式规范化后与原序逐字节一致";
            // 存储序严格升序机械核对（ObjectId 规范文本字典序）。
            for (std::size_t i = 1; i < set.entries.size(); ++i) {
                EXPECT_LT(set.entries[i - 1].objectId.toCanonical(),
                          set.entries[i].objectId.toCanonical())
                    << "集合存储序违反严格升序（I-REQ-1）";
            }
        }
        ++checkedSets;
    };

    // 搬运任务：五对象（根＋四集合）。
    const tk::JsonValue tIn = parseDatasetJson(
        transport.resolveInput("inputs/transport-task.json"));
    RequirementWorkingSet tWs;
    tWs.root.name = tIn.find("root")->find("name")->text;
    for (const auto& j : tIn.find("points")->items) { tWs.points.entries.push_back(pointOf(j)); }
    sortEntriesByObjectId(tWs.points.entries);
    for (const auto& j : tIn.find("regions")->items) { tWs.regions.entries.push_back(regionOf(j)); }
    sortEntriesByObjectId(tWs.regions.entries);
    for (const auto& j : tIn.find("conditions")->items) { tWs.conditions.entries.push_back(conditionOf(j)); }
    sortEntriesByObjectId(tWs.conditions.entries);
    checkSet(tWs.root);
    checkSet(tWs.points);
    checkSet(tWs.regions);
    checkSet(tWs.conditions);
    checkSet(tWs.plans);

    // 部分位姿：点集。
    const tk::JsonValue pIn = parseDatasetJson(partial.resolveInput("inputs/partial-pose.json"));
    PointSet pSet;
    for (const auto& j : pIn.find("points")->items) { pSet.entries.push_back(pointOf(j)); }
    sortEntriesByObjectId(pSet.entries);
    checkSet(pSet);

    // 区域采样：区域集。
    const tk::JsonValue rIn = parseDatasetJson(region.resolveInput("inputs/region-sampling.json"));
    RegionSet rSet;
    for (const auto& j : rIn.find("regions")->items) { rSet.entries.push_back(regionOf(j)); }
    sortEntriesByObjectId(rSet.entries);
    checkSet(rSet);

    // 负载事件：工况集。
    const tk::JsonValue eIn = parseDatasetJson(payload.resolveInput("inputs/payload-events.json"));
    ConditionSet eSet;
    for (const auto& j : eIn.find("conditions")->items) { eSet.entries.push_back(conditionOf(j)); }
    sortEntriesByObjectId(eSet.entries);
    checkSet(eSet);

    EXPECT_EQ(checkedSets, 8U) << "四套数据集合计覆盖 8 个集合/根对象";
}

/**
 * 非有限/非法/缺失不静默（acceptance 5——负样例逐条）：非有限 double 经
 * 字节面注入（NaN 位模式替换）→ decode MalformedPayload（不产半成品）；
 * 非有限容差构造拒绝；非法单位声明→REQ-IMPORT-UNIT-ILLEGAL（预览
 * unitUsable=false，不静默转 0）；缺失引用→REQ-READY-REF-MISSING（不
 * 默认通过）。
 */
TEST(DeterminismFaces, NonFiniteAndIllegalNotSilent_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{});
    using namespace req;

    // ①非有限数：合法点编码后把 position 0.5 的 IEEE754 位模式（小端）
    // 替换为 quiet NaN→decode 结构校验拒绝（MalformedPayload——不产半
    // 成品；0.5=0x3FE0000000000000→NaN=0x7FF8000000000000）。
    TaskPoint p;
    p.objectId = sdurws::ird::core::ObjectId::generate();
    p.name = "NaN 注入载体";
    p.pose.position =
        sdurws::ird::core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(0.5, -0.5, 0.3), goldenProvenance());
    p.pose.constrainedDof.z = true;
    p.work.enabled = true;
    p.work.distanceM = 1.0;  // m
    const RequirementCodec codec;
    PointSet nanCarrier;
    nanCarrier.entries.push_back(p);
    auto bytes = encodeOk(codec, RequirementObjectVariant(nanCarrier));
    static const std::uint8_t kDoubleOneHalf[8] = {0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0xE0, 0x3F};
    static const std::uint8_t kDoubleNaN[8] = {0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0xF8, 0x7F};
    bool patched = false;
    for (std::size_t i = 0; i + 8 <= bytes.size(); ++i) {
        if (std::equal(kDoubleOneHalf, kDoubleOneHalf + 8, bytes.begin() + i)) {
            std::copy(kDoubleNaN, kDoubleNaN + 8, bytes.begin() + i);
            patched = true;
            break;
        }
    }
    ASSERT_TRUE(patched) << "未找到 0.5 位模式（编码布局漂移？）——注入失败";
    auto poisoned = codec.decode(bytes, kCurrentRequirementFormatVersion);
    ASSERT_FALSE(poisoned.ok()) << "NaN 必须被 decode 拒绝（非有限不静默）";
    EXPECT_EQ(poisoned.error().code, RequirementErrorCode::MalformedPayload);

    // ②非有限容差：构造边界拒绝（I-REQ-5 两分量 >0 且有限）。
    ToleranceSpec nanTolerance;
    nanTolerance.positionTolerance = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(validateTolerance(nanTolerance).has_value());

    // ③非法单位：声明表外 token→REQ-IMPORT-UNIT-ILLEGAL（预览如实呈现
    // unitUsable=false——不猜测不换算）。
    io::RawTable table;
    table.report.hasHeader = true;
    for (int f = 0; f < static_cast<int>(kImportFieldCount); ++f) {
        table.report.header.emplace_back(
            importFieldToken(static_cast<ImportField>(f)));
    }
    table.report.dataRows = 1;
    std::vector<std::string> row;
    for (int f = 0; f < static_cast<int>(kImportFieldCount); ++f) {
        row.push_back(f == static_cast<int>(ImportField::X) ? "650" : "");
    }
    table.rows.emplace_back(row.begin(), row.end());
    RequirementImporter importer;
    FieldMapping mapping = FieldMapping::none();
    mapping.assign(ImportField::X, 7);  // canonical 表序 X＝第 7 列（0 起）
    ImportUnitOptions badUnits;
    badUnits.setUnit(ImportField::X, "furlong");
    auto preview = previewUnitConversion(table, mapping, badUnits, importer.fieldDictionary());
    ASSERT_EQ(preview.size(), 1U);
    EXPECT_FALSE(preview[0].unitUsable) << "非法声明如实呈现（不静默转默认）";
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    auto outcome = importer.mapCsv(table, mapping, badUnits, diags);
    EXPECT_TRUE(hasDiagCode(diags, std::string(kReqImportUnitIllegal).c_str()))
        << "非法单位声明 → REQ-IMPORT-UNIT-ILLEGAL";
    EXPECT_TRUE(outcome.entries.empty()) << "声明非法不得产出换算条目（不静默）";

    // ④缺失引用：AlignFrame 目标不在闭包→REQ-READY-REF-MISSING（不默认
    // 通过——可定位稳定码）。
    OrientationRule danglingRule;
    danglingRule.kind = OrientationRuleKind::AlignFrame;
    danglingRule.targetFrame.kind = RequirementRefKind::ModelFrame;
    danglingRule.targetFrame.objectId = oidOf("obj-9900000000000000000000000000ff03");
    OrientationRuleAnchor anchor{p.objectId, p.name};
    CheckContext emptyCtx;
    auto resolution = resolveOrientationRule(danglingRule, anchor, emptyCtx);
    ASSERT_FALSE(resolution.ok);
    EXPECT_EQ(resolution.diag.code, std::string_view(kReqReadyRefMissing));
}

// =====================================================================
// V-22：GUI 主流程（登记不执行——envUnavailable 如实登记不得绿灯）
// =====================================================================

/**
 * V-22（卡 §10.2"GUI 用例仅登记流程（AGENTS Windows 规程），本次不启
 * 动"）：本用例以 GTEST_SKIP＋报告面 setOutcome(EnvUnavailable) 登记——
 * ird-test-report.json 中该记录的 outcome 为 env-unavailable（非通过），
 * 与"未执行的测试不得标注通过"（AGENTS §4.2）一致。GUI 流程的执行归
 * Windows GUI 环境的规程化会话（testkit §6.7），不在无界面测试目标内。
 */
TEST(GuiRegistration, V22RegisteredNotExecuted_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08", "UX-05", "UX-06"},
                  std::vector<std::string>{});
    tk::report::setOutcome(tk::report::Outcome::EnvUnavailable,
                           "V-22 GUI 主流程仅登记不执行（卡 §10.2——GUI 用例不"
                           "启动；Windows GUI 环境规程归 testkit §6.7，无界面"
                           "测试目标不含 GUI 平台插件）");
    GTEST_SKIP() << "V-22 GUI 流程仅登记（envUnavailable 如实登记）";
}

}  // namespace
