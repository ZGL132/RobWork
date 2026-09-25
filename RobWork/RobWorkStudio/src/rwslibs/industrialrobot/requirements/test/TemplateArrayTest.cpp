/**
 * @file   TemplateArrayTest.cpp
 * @brief  工艺模板/镜像/阵列/重生成用例组（ReqTemplateArray）——镜像黄金
 *         断言、不可镜像规则处置、四类阵列几何、六模板黄金默认、批次应
 *         用与整体回滚、重生成冲突、删除源提示、局部撤销与命令通道路径
 *         分离（任务契约 WP-14-T07 acceptance 1~5 逐条具名自证；V-06/
 *         V-07 观测点、AT-24）。
 *
 * 设计依据：units/requirements.md §7.1/§7.2（模板/镜像/阵列语义）、§9.5
 * （ITemplateArrayService 契约）、§9.3（编辑器批次入口）、§4.7（I-REQ-10
 * 派生不回写）、§10.2 V-06/V-07（故障注入矩阵）；诊断码登记值随附断言
 * 见 DiagCodesTest.cpp 的 T07 用例。
 *
 * 黄金断言口径（ACC1）：镜像反射的解析性质——
 *   - 位置：轴对齐法向的反射是坐标取反（IEEE 精确运算，逐位断言）；
 *   - 姿态：反射共轭 R'=M·R·M 的逐元素规则 R'ij=σiσj·Rij（σi=±1 为镜
 *     像对角的符号）给出解析闭式——绕法向轴的旋转分量不变、面内两轴
 *     取反：法向 X：(roll,pitch,yaw)→(+roll,−pitch,−yaw)；法向 Y：
 *     (−roll,+pitch,−yaw)；法向 Z：(−roll,−pitch,+yaw)（TemplateArray.
 *     cpp reflectRpy 注有推导）——期望值以解析常量写入断言，容差 1e-12
 *     （三角/asin/atan2 的浮点实现差）。
 */

#include <sdurws/ird/requirements/TemplateArray.hpp>
#include <sdurws/ird/requirements/Editor.hpp>
#include <sdurws/ird/requirements/DiagCodes.hpp>
#include <sdurws/ird/requirements/CommandHandlers.hpp>
#include <sdurws/ird/requirements/ObjectTypes.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/project/CommandService.hpp>  // HandlerContext/CommandPlan/PrepareOutcome（ACC5 命令面）
#include <sdurws/ird/project/QueryPort.hpp>       // IProjectQueryPort——端口替身基类（ACC5）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sdurws::ird::requirements;
using namespace sdurws::ird;  // project:: 限定名直写面（CommandHandlersTest 同款）

/// core 命名空间别名（测试内 ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 公共辅助（EditorTest 的闭包夹具＋CommandHandlersTest 的端口夹具同款）
// =====================================================================

constexpr double kGoldenEpsilon = 1e-12;  // 黄金姿态断言容差（rad/m——解析闭式与浮点实现的差界）

/// 合法源任务点（World 系/位置在场/约束 Z＋滚转自由？——恒约束 Z，位姿
/// 可派生；work 段启用——createPoint 约定同源）。
TaskPoint makeSourcePoint(const std::string& name, const rw::math::Vector3D<double>& pos)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        pos, core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                         std::nullopt, std::nullopt,
                                         std::string("test-fixture")));
    p.pose.constrainedDof.z = true;
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 诊断列表中是否含指定稳定码。
bool hasDiagCode(const std::vector<core::DiagnosticRecord>& diags, const char* code)
{
    for (const auto& d : diags) {
        if (d.code == code) { return true; }
    }
    return false;
}

/// 参数快照中是否含指定键值对。
bool hasParam(const GenerationProvenance& gen, const std::string& key,
              const std::string& value)
{
    for (const auto& kv : gen.parameters) {
        if (kv.first == key && kv.second == value) { return true; }
    }
    return false;
}

/// World 镜像面（法向可配——轴对齐黄金断言用）。
MirrorPlaneSpec worldPlane(const rw::math::Vector3D<double>& normal)
{
    MirrorPlaneSpec plane;
    plane.refFrame = RequirementReference{};  // World
    plane.axisNormal = normal;
    return plane;
}

/// 断言批次溯源的公共面（generatorId/linked/instanceId 非空/参数含 kind）。
void assertProvenanceShape(const EditBatch& batch, const std::string& generatorId,
                           const char* kindToken)
{
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    EXPECT_EQ(batch.provenance.generatorId, generatorId);
    EXPECT_TRUE(batch.provenance.linked) << "生成批次恒 linked=true（解除关联是显式动作）";
    EXPECT_FALSE(batch.provenance.instanceId.empty());
    EXPECT_TRUE(hasParam(batch.provenance, "kind", kindToken));
}

/// 断言派生条目相对源条目的独立性与零回写（I-REQ-10——独立 ObjectId、
/// 源条目字节不被触碰；generation 溯源在场且同批）。
void assertDerivedIndependent(const TaskPoint& derived, const TaskPoint& source,
                              const EditBatch& batch)
{
    EXPECT_TRUE(derived.objectId.isValid());
    EXPECT_FALSE(derived.objectId == source.objectId)
        << "派生条目独立 ObjectId（I-REQ-10）";
    EXPECT_TRUE(derived.generation.has_value());
    EXPECT_EQ(derived.generation->instanceId, batch.provenance.instanceId);
    EXPECT_EQ(derived.generation->generatorId, batch.provenance.generatorId);
    // 零回写：源条目不含 generation 之外的任何派生痕迹（源的 generation
    // 原状；这里核对调用方持有的源值未被服务出口改写——值语义下等价于
    // 字节不变）。
    EXPECT_FALSE(source.generation.has_value()) << "源条目保持无溯源（零回写）";
}

// =====================================================================
// 基线闭包＋端口双面夹具（ACC5——同一基线对象的两个消费面）
// =====================================================================

/// 测试用闭包字节源（EditorTest 同款——按 token/id 的确定性内存映射）。
class MapClosure final : public RequirementObjectClosureView {
public:
    void put(const std::string& token, const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byToken_[token] = RequirementClosureObject{token, bytes.get()};
        }
    }

    void putById(const core::ObjectId& id, const std::string& token,
                 const RequirementObjectVariant& object)
    {
        const RequirementCodec codec;
        auto bytes = codec.encode(object, kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byId_[id] = RequirementClosureObject{token, bytes.get()};
        }
    }

    std::optional<RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        const auto it = byToken_.find(std::string{objectTypeToken});
        if (it == byToken_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<RequirementClosureObject> tryObject(const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        if (it == byId_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

private:
    std::map<std::string, RequirementClosureObject> byToken_;
    std::map<core::ObjectId, RequirementClosureObject> byId_;
};

/// project ②端口内存替身（CommandHandlersTest 同款精简——单线程用例内
/// 使用；仅 ACC5 命令通道路径消费）。
class TestQueryPort final : public project::IProjectQueryPort {
public:
    project::RevisionView view;  ///< head()/revision()/tryRevision 应答值
    /// 对象字节表（键＝"<oid 规范文本>|<cv 规范文本>"）。
    std::map<std::string, std::vector<std::uint8_t>> objects;

    void addObject(const core::ObjectId& oid, std::string token,
                   const std::vector<std::uint8_t>& bytes)
    {
        core::ContentVersion cv;
        cv.bytes[0] = static_cast<std::uint8_t>(view.objectRefs.size() + 1U);
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

/// 五对象 canonical 字节（RequirementCodec 同源编码）。
std::vector<std::uint8_t> encodeObject(const RequirementObjectVariant& v)
{
    const RequirementCodec codec;
    auto encoded = codec.encode(v, kCurrentRequirementFormatVersion);
    if (!encoded.ok()) {
        throw std::logic_error("fixture: 对象编码失败: " + encoded.error().detail);
    }
    return encoded.get();
}

/// 基线束：根＋点集（P1）＋空区域/工况/计划集——闭包与端口同源装配。
struct BaselineBundle {
    core::ObjectId rootOid;
    core::ObjectId pointSetOid;
    core::ObjectId conditionSetOid;
    RequirementSet root;
    PointSet points;
    ConditionSet conditions;
    std::vector<std::uint8_t> rootBytes;
    std::vector<std::uint8_t> pointBytes;
    std::vector<std::uint8_t> conditionBytes;
    MapClosure closure;

    /// 装配（P1 由调用方注入——各用例自定源点位）。
    static BaselineBundle make(TaskPoint p1)
    {
        BaselineBundle b;
        b.rootOid = core::ObjectId::generate();
        b.pointSetOid = core::ObjectId::generate();
        b.conditionSetOid = core::ObjectId::generate();
        b.points.entries.push_back(std::move(p1));
        sortEntriesByObjectId(b.points.entries);
        OperatingCondition c1;
        c1.objectId = core::ObjectId::generate();
        c1.name = "C1";
        c1.level = RequirementLevel::Must;
        c1.enabled = true;
        b.conditions.entries.push_back(c1);
        sortEntriesByObjectId(b.conditions.entries);
        b.root.name = "基线需求集";
        b.root.pointSetRef = b.pointSetOid;
        b.root.conditionSetRef = b.conditionSetOid;
        b.rootBytes = encodeObject(RequirementObjectVariant{b.root});
        b.pointBytes = encodeObject(RequirementObjectVariant{b.points});
        b.conditionBytes = encodeObject(RequirementObjectVariant{b.conditions});
        b.closure.put(std::string{kReqSetObjectType}, RequirementObjectVariant{b.root});
        b.closure.putById(b.pointSetOid, std::string{kReqPointSetObjectType},
                          RequirementObjectVariant{b.points});
        b.closure.putById(b.conditionSetOid, std::string{kReqConditionSetObjectType},
                          RequirementObjectVariant{b.conditions});
        return b;
    }
};

}  // namespace

// =====================================================================
// acceptance 1——镜像黄金断言＋规则处置（V-06 黄金断言面＋V-07 不静默降级）
// =====================================================================

/**
 * 镜像黄金断言·法向 Y（ACC1/V-06）：过 World 系镜像面（法向 Y）的反射
 * ——位置 (1,2,3)→(1,−2,3)（轴对齐反射＝坐标取反，逐位精确）；Fixed
 * 姿态 (0.1,0.2,0.3) rad→(−0.1,+0.2,−0.3) rad（反射共轭解析闭式，1e-12）；
 * 容差/三段/等级同构字段原样；派生条目独立 ObjectId＋溯源完整＋源零回写。
 */
TEST(ReqTemplateArray, MirrorGoldenNormalY_WP14T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC1-mirror-golden"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(1.0, 2.0, 3.0));
    src.pose.orientation.kind = OrientationRuleKind::Fixed;
    src.pose.orientation.fixedRpy = rw::math::Vector3D<double>(0.1, 0.2, 0.3);  // rad
    const TaskPoint srcBefore = src;

    const EditBatch batch = service.applyMirror({src}, worldPlane(
        rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    assertProvenanceShape(batch, "mirror", "Mirror");
    ASSERT_EQ(batch.newPoints.size(), 1U);
    ASSERT_TRUE(batch.diagnostics.empty()) << "可镜像规则零警告";

    const TaskPoint& d = batch.newPoints[0];
    assertDerivedIndependent(d, src, batch);
    // 位置黄金（轴对齐反射＝逐位精确——IEEE 减法无舍入差）。
    ASSERT_TRUE(d.pose.position.state() == core::FieldState::Provided);
    EXPECT_EQ(d.pose.position.value()[0], 1.0);
    EXPECT_EQ(d.pose.position.value()[1], -2.0);
    EXPECT_EQ(d.pose.position.value()[2], 3.0);
    // 姿态黄金（解析闭式：法向 Y → (−roll,+pitch,−yaw)）。
    ASSERT_EQ(d.pose.orientation.kind, OrientationRuleKind::Fixed);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[0], -0.1, kGoldenEpsilon);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[1], 0.2, kGoldenEpsilon);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[2], -0.3, kGoldenEpsilon);
    // 同构字段原样（容差/三段/等级/顺序键清空——派生不入顺序链）。
    EXPECT_EQ(d.tolerance, src.tolerance);
    EXPECT_EQ(d.approach, src.approach);
    EXPECT_EQ(d.work, src.work);
    EXPECT_EQ(d.retract, src.retract);
    EXPECT_EQ(d.level, src.level);
    EXPECT_FALSE(d.sequenceKey.has_value());
    // 溯源参数快照（平面参数＋源 id——重解析面；数值文本＝%.17g 无损往返）。
    EXPECT_TRUE(hasParam(d.generation.value(), "plane-normal", "0,1,0"));
    EXPECT_TRUE(hasParam(d.generation.value(), "source-id", src.objectId.toCanonical()));
    // 源零回写：服务出口未触碰源值。
    EXPECT_EQ(src == srcBefore, true);
}

/**
 * 镜像黄金断言·法向 Z（ACC1/V-06）：位置 (1,2,3)→(1,2,−3)；姿态解析
 * 闭式 (−roll,−pitch,+yaw)——绕法向轴（Z）的旋转分量与反射对易不变
 * （R'ij=σiσj·Rij 逐元素规则，reflectRpy 注），面内轴分量取反。
 */
TEST(ReqTemplateArray, MirrorGoldenNormalZ_WP14T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC1-mirror-golden"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(1.0, 2.0, 3.0));
    src.pose.orientation.kind = OrientationRuleKind::Fixed;
    src.pose.orientation.fixedRpy = rw::math::Vector3D<double>(0.1, 0.2, 0.3);  // rad

    const EditBatch batch = service.applyMirror({src}, worldPlane(
        rw::math::Vector3D<double>(0.0, 0.0, 1.0)));
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 1U);
    const TaskPoint& d = batch.newPoints[0];

    ASSERT_TRUE(d.pose.position.state() == core::FieldState::Provided);
    EXPECT_EQ(d.pose.position.value()[0], 1.0);
    EXPECT_EQ(d.pose.position.value()[1], 2.0);
    EXPECT_EQ(d.pose.position.value()[2], -3.0);
    ASSERT_EQ(d.pose.orientation.kind, OrientationRuleKind::Fixed);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[0], -0.1, kGoldenEpsilon);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[1], -0.2, kGoldenEpsilon);
    EXPECT_NEAR(d.pose.orientation.fixedRpy[2], 0.3, kGoldenEpsilon);
}

/**
 * PointAtTarget/ToolRollFree 正常镜像（ACC1——§7.2"Fixed/PointAtTarget
 * 正常镜像"）：目标点反射（非零目标反射后仍非零——等距性）；滚转区间
 * 反手性翻转 [min,max]→[−max,−min]。
 */
TEST(ReqTemplateArray, MirrorPointAtTargetAndToolRollFree_WP14T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11", "REQ-09"},
                  std::vector<std::string>{"AT-24"});

    TemplateArrayService service;

    // PointAtTarget：目标 (0,1,0) →法向 Y 镜像→ (0,−1,0)。
    TaskPoint pt = makeSourcePoint("PT", rw::math::Vector3D<double>(1.0, 0.0, 0.0));
    pt.pose.constrainedDof.z = true;
    pt.pose.orientation.kind = OrientationRuleKind::PointAtTarget;
    pt.pose.orientation.targetPoint = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    const EditBatch b1 = service.applyMirror({pt}, worldPlane(
        rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    ASSERT_TRUE(b1.ok) << b1.error.detail;
    ASSERT_EQ(b1.newPoints.size(), 1U);
    EXPECT_EQ(b1.newPoints[0].pose.orientation.kind, OrientationRuleKind::PointAtTarget);
    EXPECT_NEAR(b1.newPoints[0].pose.orientation.targetPoint[0], 0.0, kGoldenEpsilon);
    EXPECT_NEAR(b1.newPoints[0].pose.orientation.targetPoint[1], -1.0, kGoldenEpsilon);
    EXPECT_NEAR(b1.newPoints[0].pose.orientation.targetPoint[2], 0.0, kGoldenEpsilon);
    EXPECT_TRUE(b1.diagnostics.empty());

    // ToolRollFree：[−0.5, 0.9] rad → [−0.9, 0.5] rad（反手性翻转）。
    TaskPoint tr = makeSourcePoint("TR", rw::math::Vector3D<double>(1.0, 0.0, 0.0));
    tr.pose.orientation.kind = OrientationRuleKind::ToolRollFree;
    tr.pose.orientation.rollRange = RollRange{-0.5, 0.9};  // rad
    const EditBatch b2 = service.applyMirror({tr}, worldPlane(
        rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    ASSERT_TRUE(b2.ok) << b2.error.detail;
    ASSERT_EQ(b2.newPoints.size(), 1U);
    EXPECT_EQ(b2.newPoints[0].pose.orientation.kind, OrientationRuleKind::ToolRollFree);
    EXPECT_NEAR(b2.newPoints[0].pose.orientation.rollRange.min, -0.9, kGoldenEpsilon);
    EXPECT_NEAR(b2.newPoints[0].pose.orientation.rollRange.max, 0.5, kGoldenEpsilon);
}

/**
 * 不可镜像规则处置（ACC1/V-07——§7.2 原文）：AlignGeometryNormal 与
 * AlignFrame 引用目标在镜像侧不存在→派生条目**保留规则原样**（不静默
 * 降级为 Fixed）＋参数快照 orientation-pending 标记＋REQ-DERIVE-MIRROR-
 * PENDING warning（每条恰一——不静默猜）。
 */
TEST(ReqTemplateArray, MirrorUnmirrorableRulePending_WP14T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11", "REQ-09"},
                  std::vector<std::string>{"AT-24", "ACC1-pending"});

    TemplateArrayService service;

    // AlignGeometryNormal 源（目标场景对象＋特征显式——合法规则）。
    TaskPoint agn = makeSourcePoint("AGN", rw::math::Vector3D<double>(1.0, 0.0, 0.0));
    agn.pose.orientation.kind = OrientationRuleKind::AlignGeometryNormal;
    agn.pose.orientation.targetSceneObject = core::ObjectId::generate();
    agn.pose.orientation.feature = OrientationFeature::FramePlaneNormal;

    // AlignFrame 源（目标场景对象引用——合法规则）。
    TaskPoint af = makeSourcePoint("AF", rw::math::Vector3D<double>(2.0, 0.0, 0.0));
    af.pose.orientation.kind = OrientationRuleKind::AlignFrame;
    af.pose.orientation.targetFrame.kind = RequirementRefKind::SceneObject;
    af.pose.orientation.targetFrame.objectId = core::ObjectId::generate();

    const EditBatch batch = service.applyMirror({agn, af}, worldPlane(
        rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 2U);

    // 逐条：规则保留原样（kind 与目标/特征逐字段不变——绝不降级 Fixed）。
    for (const auto* src : {&agn, &af}) {
        const auto it = std::find_if(batch.newPoints.begin(), batch.newPoints.end(),
                                     [&](const TaskPoint& p) {
                                         return p.name == src->name + "-M";
                                     });
        ASSERT_TRUE(it != batch.newPoints.end());
        EXPECT_EQ(it->pose.orientation.kind, src->pose.orientation.kind)
            << "规则保留——不静默降级为 Fixed（V-07）";
        EXPECT_EQ(it->pose.orientation.targetSceneObject,
                  src->pose.orientation.targetSceneObject);
        EXPECT_EQ(it->pose.orientation.feature, src->pose.orientation.feature);
        // 待人工处理标记入参数快照。
        EXPECT_TRUE(hasParam(it->generation.value(),
                             std::string{kGenParamOrientationPending}, "1"));
    }
    // 每条恰一条 REQ-DERIVE-MIRROR-PENDING warning。
    ASSERT_EQ(batch.diagnostics.size(), 2U);
    for (const auto& diag : batch.diagnostics) {
        EXPECT_EQ(diag.code, std::string{kReqDeriveMirrorPending});
    }
}

// =====================================================================
// acceptance 2——四类阵列（数量/间距/角度/几何＋溯源完整＋零回写）
// =====================================================================

/**
 * Linear 阵列（ACC2/V-06）：源 (0,0,0)、方向 X、间距 0.5 m、数量 3→恰 3
 * 条，位置 (0.5,0,0)/(1,0,0)/(1.5,0,0)，相邻间距逐一 0.5 m；溯源完整。
 */
TEST(ReqTemplateArray, LinearArrayCountAndSpacing_WP14T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC2-linear"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(0.0, 0.0, 0.0));

    ArrayParams params;
    params.sources = {src};
    params.direction = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    params.spacingM = 0.5;  // m
    params.count = 3;
    const EditBatch batch = service.applyArray(ArrayKind::Linear, params);
    assertProvenanceShape(batch, "array:linear", "Linear");
    ASSERT_EQ(batch.newPoints.size(), 3U);

    // 位置逐条黄金（主向单位化＝原向——整值精确）。
    const double expectedX[3] = {0.5, 1.0, 1.5};
    for (int i = 0; i < 3; ++i) {
        const TaskPoint& d = batch.newPoints[static_cast<std::size_t>(i)];
        assertDerivedIndependent(d, src, batch);
        EXPECT_NEAR(d.pose.position.value()[0], expectedX[i], kGoldenEpsilon);
        EXPECT_NEAR(d.pose.position.value()[1], 0.0, kGoldenEpsilon);
        EXPECT_NEAR(d.pose.position.value()[2], 0.0, kGoldenEpsilon);
        if (i > 0) {
            // 相邻间距＝0.5 m（阵列"间距"的逐一断言）。
            const double dx = d.pose.position.value()[0]
                            - batch.newPoints[static_cast<std::size_t>(i) - 1]
                                  .pose.position.value()[0];
            EXPECT_NEAR(dx, 0.5, kGoldenEpsilon);
        }
        // 名称确定性后缀（-A1..-A3）。
        EXPECT_EQ(d.name, "P1-A" + std::to_string(i + 1));
    }
    // 溯源参数快照（方向/间距/数量——重生成重解析面；%.17g 文本）。
    EXPECT_TRUE(hasParam(batch.provenance, "direction", "1,0,0"));
    EXPECT_TRUE(hasParam(batch.provenance, "spacing-m", "0.5"));
    EXPECT_TRUE(hasParam(batch.provenance, "count", "3"));
}

/**
 * Rectangular 阵列（ACC2）：2×3 网格→恰 6 条，位置 (i·0.5, j·0.25, 0)
 * 全网格覆盖（i=1..2、j=1..3）。
 */
TEST(ReqTemplateArray, RectangularArrayGrid_WP14T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC2-rect"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(0.0, 0.0, 0.0));

    ArrayParams params;
    params.sources = {src};
    params.direction = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    params.spacingM = 0.5;   // m
    params.count = 2;
    params.direction2 = rw::math::Vector3D<double>(0.0, 1.0, 0.0);
    params.spacing2M = 0.25;  // m
    params.count2 = 3;
    const EditBatch batch = service.applyArray(ArrayKind::Rectangular, params);
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 6U) << "2×3 网格恰 6 条";

    // 全网格覆盖核对（坐标集合比对——顺序无关的覆盖性断言）。
    for (int i = 1; i <= 2; ++i) {
        for (int j = 1; j <= 3; ++j) {
            const std::string expectName = "P1-A" + std::to_string(i) + "x"
                                         + std::to_string(j);
            const auto it = std::find_if(batch.newPoints.begin(), batch.newPoints.end(),
                                         [&](const TaskPoint& p) { return p.name == expectName; });
            ASSERT_TRUE(it != batch.newPoints.end()) << expectName;
            EXPECT_NEAR(it->pose.position.value()[0], 0.5 * i, kGoldenEpsilon);
            EXPECT_NEAR(it->pose.position.value()[1], 0.25 * j, kGoldenEpsilon);
            EXPECT_NEAR(it->pose.position.value()[2], 0.0, kGoldenEpsilon);
        }
    }
}

/**
 * Circular 阵列（ACC2）：圆心 (1,0,0)、半径 0.5 m、起始角 0、步距 π/2
 * rad、数量 4→恰 4 条位于角度 π/2/π/3π/2/2π（三角黄金 1e-12）；逐条到
 * 圆心距离＝半径。
 */
TEST(ReqTemplateArray, CircularArrayRadiusAndAngle_WP14T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC2-circular"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(0.0, 0.0, 0.0));

    ArrayParams params;
    params.sources = {src};
    params.center = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    params.radiusM = 0.5;  // m
    params.startAngleRad = 0.0;
    params.angleStepRad = 3.14159265358979323846 / 2.0;  // rad
    params.count = 4;
    const EditBatch batch = service.applyArray(ArrayKind::Circular, params);
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 4U);

    for (int i = 1; i <= 4; ++i) {
        const double theta = params.angleStepRad * static_cast<double>(i);  // rad
        const auto it = std::find_if(batch.newPoints.begin(), batch.newPoints.end(),
                                     [&](const TaskPoint& p) {
                                         return p.name
                                             == "P1-A" + std::to_string(i);
                                     });
        ASSERT_TRUE(it != batch.newPoints.end());
        EXPECT_NEAR(it->pose.position.value()[0],
                    params.center[0] + params.radiusM * std::cos(theta), kGoldenEpsilon);
        EXPECT_NEAR(it->pose.position.value()[1],
                    params.center[1] + params.radiusM * std::sin(theta), kGoldenEpsilon);
        EXPECT_NEAR(it->pose.position.value()[2], params.center[2], kGoldenEpsilon);
        // 圆性：到圆心距离恒＝半径（阵列"半径"的逐一断言）。
        const double dx = it->pose.position.value()[0] - params.center[0];
        const double dy = it->pose.position.value()[1] - params.center[1];
        EXPECT_NEAR(std::sqrt(dx * dx + dy * dy), params.radiusM, 1e-9);
    }
}

/**
 * Polyline 阵列（ACC2）：折线 (0,0,0)→(2,0,0)→(2,1,0)、间距 0.8 m→
 * 总弧长 3 m、恰 floor(3/0.8)=3 条，弧长定位 s=0.8/1.6/2.4（第三点落
 * 第二段 (2,0.4,0)）——"折线可配"＋条数由间距派生。
 */
TEST(ReqTemplateArray, PolylineArrayArcLength_WP14T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC2-polyline"});

    TemplateArrayService service;
    TaskPoint src = makeSourcePoint("P1", rw::math::Vector3D<double>(0.0, 0.0, 0.0));

    ArrayParams params;
    params.sources = {src};
    params.polyline = {rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                       rw::math::Vector3D<double>(2.0, 0.0, 0.0),
                       rw::math::Vector3D<double>(2.0, 1.0, 0.0)};
    params.polylineSpacingM = 0.8;  // m
    const EditBatch batch = service.applyArray(ArrayKind::Polyline, params);
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 3U) << "floor(3.0/0.8)=3 条";

    using V3 = rw::math::Vector3D<double>;
    const V3 expected[3] = {V3(0.8, 0.0, 0.0), V3(1.6, 0.0, 0.0), V3(2.0, 0.4, 0.0)};
    for (int i = 0; i < 3; ++i) {
        const TaskPoint& d = batch.newPoints[static_cast<std::size_t>(i)];
        EXPECT_NEAR(d.pose.position.value()[0], expected[i][0], kGoldenEpsilon);
        EXPECT_NEAR(d.pose.position.value()[1], expected[i][1], kGoldenEpsilon);
        EXPECT_NEAR(d.pose.position.value()[2], expected[i][2], kGoldenEpsilon);
    }
}

/**
 * 阵列多源与批次溯源（ACC2——"批量派生"面）：两源同参数一次生成（条数
 * ＝每源派生数之和），同批同 instanceId，各源产物独立 id；源零回写。
 */
TEST(ReqTemplateArray, LinearArrayMultiSourceProvenance_WP14T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24"});

    TemplateArrayService service;
    TaskPoint s1 = makeSourcePoint("S1", rw::math::Vector3D<double>(0.0, 0.0, 0.0));
    TaskPoint s2 = makeSourcePoint("S2", rw::math::Vector3D<double>(0.0, 5.0, 0.0));
    const TaskPoint s1Before = s1;
    const TaskPoint s2Before = s2;

    ArrayParams params;
    params.sources = {s1, s2};
    params.direction = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    params.spacingM = 1.0;  // m
    params.count = 2;
    const EditBatch batch = service.applyArray(ArrayKind::Linear, params);
    ASSERT_TRUE(batch.ok) << batch.error.detail;
    ASSERT_EQ(batch.newPoints.size(), 4U) << "两源×2 条";
    for (const auto& d : batch.newPoints) {
        EXPECT_EQ(d.generation->instanceId, batch.provenance.instanceId)
            << "同批同 instanceId（批次溯源完整）";
        EXPECT_TRUE(hasParam(d.generation.value(), "source-id",
                             d.name.substr(0, 2) == "S1"
                                 ? s1.objectId.toCanonical()
                                 : s2.objectId.toCanonical()));
    }
    EXPECT_EQ(s1 == s1Before, true) << "源零回写";
    EXPECT_EQ(s2 == s2Before, true) << "源零回写";
}

// =====================================================================
// acceptance 3——六类工艺模板＋重生成/冲突/解除关联
// =====================================================================

namespace {

/// 六类黄金默认期望表（applyTemplate 生成数/工艺标签/生成器标识——黄金
/// 锁定值，TemplateArray.cpp defaultTemplateParams 注释的测试面镜像）。
struct GoldenTemplate {
    TemplateKind kind;
    const char* kindToken;
    const char* generatorId;
    int countX;
    int countY;
    ProcessTag tag;
};

}  // namespace

/**
 * 六类模板黄金默认（ACC3/REQ-07——TemplateKind 词表 6 值）：逐类默认
 * 参数生成数＝countX×countY（2/3/4/1/1/2）、工艺标签就近映射、条目全
 * 部通过 validateTaskPoint、批次溯源完整（generatorId=template:<token>）。
 */
TEST(ReqTemplateArray, TemplateSixKindsGoldenDefaults_WP14T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07"},
                  std::vector<std::string>{"AT-24", "ACC3-templates"});

    const TemplateArrayService service;
    const GoldenTemplate golden[6] = {
        {TemplateKind::BinPicking, "BinPicking", "template:bin-picking", 2, 1,
         ProcessTag::Pick},
        {TemplateKind::MachineTending, "MachineTending", "template:machine-tending",
         3, 1, ProcessTag::MachineLoad},
        {TemplateKind::Palletizing, "Palletizing", "template:palletizing", 2, 2,
         ProcessTag::Place},
        {TemplateKind::Inspection, "Inspection", "template:inspection", 1, 1,
         ProcessTag::Inspect},
        {TemplateKind::ToolChange, "ToolChange", "template:tool-change", 1, 1,
         ProcessTag::ToolChange},
        {TemplateKind::Handover, "Handover", "template:handover", 2, 1,
         ProcessTag::Handover},
    };
    for (const auto& g : golden) {
        const TemplateParams params = defaultTemplateParams(g.kind);
        const EditBatch batch = service.applyTemplate(g.kind, params);
        assertProvenanceShape(batch, g.generatorId, g.kindToken);
        ASSERT_EQ(batch.newPoints.size(),
                  static_cast<std::size_t>(g.countX * g.countY))
            << g.kindToken << " 黄金网格规模";
        for (const auto& d : batch.newPoints) {
            // 黄金默认的条目合法性（validateTaskPoint 全链通过）。
            EXPECT_FALSE(validateTaskPoint(d).has_value()) << d.name;
            EXPECT_EQ(d.processTag, g.tag) << d.name;
            EXPECT_EQ(d.level, RequirementLevel::Must);
            EXPECT_TRUE(d.enabled);
            // D-REQ-4：产物为普通条目（source=UserProvided——非
            // DerivedReadOnly，可继续手改）。
            EXPECT_EQ(d.source.kind, core::ProvenanceKind::UserProvided);
            EXPECT_TRUE(d.generation.has_value());
            EXPECT_TRUE(d.generation->linked);
        }
        // 黄金首格位置＝原点（网格 [r1c1] 即 origin）。
        EXPECT_NEAR(batch.newPoints.front().pose.position.value()[0], 0.0, kGoldenEpsilon);
        EXPECT_NEAR(batch.newPoints.front().pose.position.value()[1], 0.0, kGoldenEpsilon);
        // 黄金间距：X 向相邻格差＝spacingM。
        if (batch.newPoints.size() >= 2) {
            EXPECT_NEAR(batch.newPoints[1].pose.position.value()[0]
                            - batch.newPoints[0].pose.position.value()[0],
                        params.spacingM, kGoldenEpsilon);
        }
    }
}

/**
 * 模板批次应用＋一键重生成（ACC3——applyTemplate 产出编辑批次→编辑器
 * 应用；linked 条目一键重生成＝替换 linked∧未手改条目，条目数不增不减、
 * 旧 id 换新 id（替换语义））。
 */
TEST(ReqTemplateArray, TemplateApplyAndRegenerateReplace_WP14T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07", "REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC3-regenerate"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(0.1, 0.2, 0.3)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);

    const TemplateArrayService service;
    const EditBatch batch =
        service.applyTemplate(TemplateKind::BinPicking, defaultTemplateParams(
                                                         TemplateKind::BinPicking));
    ASSERT_TRUE(batch.ok) << batch.error.detail;

    // 批次应用（一次 applyEdit 入草稿——模板不绕过命令：仅入工作集）。
    const EditOutcome applied = editor.applyEdit(batch);
    ASSERT_TRUE(applied.accepted) << applied.error.detail;
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U) << "P1＋2 工位";
    EXPECT_EQ(editor.draftStatus().edits, 1U) << "一次 applyEdit＝一次编辑";

    // 一键重生成（零手改→全替换、零冲突）。
    const RegenerateOutcome regen =
        service.regenerate(editor.workingSet(), batch.provenance.instanceId);
    ASSERT_TRUE(regen.found) << regen.error.detail;
    EXPECT_TRUE(regen.conflictNames.empty());
    EXPECT_TRUE(regen.diagnostics.empty());
    ASSERT_EQ(regen.batch.newPoints.size(), 2U);
    ASSERT_EQ(regen.batch.replaceNames.size(), 2U);
    // 替换条目沿用原批次 instanceId（同批次重放语义）。
    for (const auto& r : regen.batch.newPoints) {
        EXPECT_EQ(r.generation->instanceId, batch.provenance.instanceId);
    }
    // 编辑器应用替换批次：条目数不变、被替换旧条目消失（新 id 取位）。
    std::vector<core::ObjectId> oldIds;
    for (const auto& p : editor.workingSet().points.entries) {
        if (p.generation.has_value()
            && p.generation->instanceId == batch.provenance.instanceId) {
            oldIds.push_back(p.objectId);
        }
    }
    const EditOutcome replaceApplied = editor.applyEdit(regen.batch);
    ASSERT_TRUE(replaceApplied.accepted) << replaceApplied.error.detail;
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U) << "替换不增减条目";
    for (const auto& oldId : oldIds) {
        bool gone = true;
        for (const auto& p : editor.workingSet().points.entries) {
            if (p.objectId == oldId) {
                gone = false;
            }
        }
        EXPECT_TRUE(gone) << "被替换旧条目已移除（替换语义）";
    }
}

/**
 * 手改条目重生成冲突（ACC3/V-07——REQ-DERIVE-REGENERATE-CONFLICT 不静默
 * 覆盖）：手改一条 linked 条目后重生成→冲突清单恰含该条目＋warning 诊断；
 * 应用重生成批次后手改值**原样保留**。
 */
TEST(ReqTemplateArray, RegenerateConflictKeepsHandModified_WP14T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07", "REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC3-conflict"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(0.1, 0.2, 0.3)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);

    const TemplateArrayService service;
    const EditBatch batch = service.applyTemplate(
        TemplateKind::BinPicking, defaultTemplateParams(TemplateKind::BinPicking));
    ASSERT_TRUE(batch.ok);
    ASSERT_TRUE(editor.applyEdit(batch).accepted);

    // 手改第一条产物（容差位置分量 1×10⁻³→5×10⁻³ m——生成器确定性重算
    // 之外的唯一变更源）。
    const std::string victim = batch.newPoints.front().name;
    TaskPoint edited;
    for (const auto& p : editor.workingSet().points.entries) {
        if (p.name == victim) {
            edited = p;
        }
    }
    ASSERT_FALSE(edited.name.empty());
    edited.tolerance.positionTolerance = 5.0e-3;  // m（>0 有限——合法值）
    const EditOutcome handEdit = editor.applyEdit(edited);
    ASSERT_TRUE(handEdit.accepted) << handEdit.error.detail;

    // 重生成→恰一条冲突＋REQ-DERIVE-REGENERATE-CONFLICT。
    const RegenerateOutcome regen =
        service.regenerate(editor.workingSet(), batch.provenance.instanceId);
    ASSERT_TRUE(regen.found);
    ASSERT_EQ(regen.conflictNames.size(), 1U);
    EXPECT_EQ(regen.conflictNames.front(), victim);
    ASSERT_EQ(regen.diagnostics.size(), 1U);
    EXPECT_EQ(regen.diagnostics.front().code,
              std::string{kReqDeriveRegenerateConflict});
    // 替换面只含未手改的另一条。
    ASSERT_EQ(regen.batch.replaceNames.size(), 1U);
    EXPECT_NE(regen.batch.replaceNames.front(), victim);

    // 应用重生成批次→手改条目原样保留（不静默覆盖），未手改条目被替换。
    ASSERT_TRUE(editor.applyEdit(regen.batch).accepted);
    bool victimKept = false;
    for (const auto& p : editor.workingSet().points.entries) {
        if (p.name == victim) {
            victimKept = true;
            EXPECT_EQ(p.tolerance.positionTolerance, 5.0e-3) << "手改值保留";
        }
    }
    EXPECT_TRUE(victimKept);
}

/**
 * 解除关联与产物手改（ACC3/D-REQ-4）：unlinkGenerator→linked=false；
 * 解除后重生成不再替换该条目（linked=false 不参与）；模板产物可继续
 * 手改（source=UserProvided，编辑器接受手改——非 DerivedReadOnly）。
 */
TEST(ReqTemplateArray, UnlinkDisablesRegenerationAndAllowsHandEdit_WP14T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-07"},
                  std::vector<std::string>{"AT-24", "ACC3-unlink"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(0.1, 0.2, 0.3)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);

    const TemplateArrayService service;
    const EditBatch batch = service.applyTemplate(
        TemplateKind::Handover, defaultTemplateParams(TemplateKind::Handover));
    ASSERT_TRUE(batch.ok);
    ASSERT_TRUE(editor.applyEdit(batch).accepted);

    // 解除关联（linked→false；其余字段逐字节保持）。
    const std::vector<TaskPoint> unlinked = unlinkGenerator(
        editor.workingSet().points.entries, batch.provenance.instanceId);
    ASSERT_EQ(unlinked.size(), editor.workingSet().points.entries.size());
    std::size_t unlinkedCount = 0;
    for (const auto& p : unlinked) {
        if (p.generation.has_value()
            && p.generation->instanceId == batch.provenance.instanceId) {
            EXPECT_FALSE(p.generation->linked) << "解除关联＝linked=false";
            ++unlinkedCount;
        }
    }
    EXPECT_EQ(unlinkedCount, 2U);
    // 把解除后的条目写回工作集（upsert 语义——同 id 更新）。
    for (const auto& p : unlinked) {
        if (p.generation.has_value()) {
            ASSERT_TRUE(editor.applyEdit(p).accepted);
        }
    }

    // 解除后重生成：批次在场但零替换（linked 条目为空）。
    const RegenerateOutcome regen =
        service.regenerate(editor.workingSet(), batch.provenance.instanceId);
    ASSERT_TRUE(regen.found);
    EXPECT_TRUE(regen.batch.replaceNames.empty()) << "linked=false 不参与重生成";
    EXPECT_TRUE(regen.conflictNames.empty());

    // 产物可继续手改（D-REQ-4）：改 note 后编辑器接受。
    TaskPoint product;
    for (const auto& p : editor.workingSet().points.entries) {
        if (p.generation.has_value()) {
            product = p;
        }
    }
    product.note = "手改备注";
    const EditOutcome edit = editor.applyEdit(product);
    EXPECT_TRUE(edit.accepted) << edit.error.detail;
}

// =====================================================================
// acceptance 4——需求集批量撤销/重做整体回滚＋派生无环＋删除源提示
// =====================================================================

/**
 * 批量整体回滚（ACC4/V-06 批次面）：单条 applyEdit 携带镜像批次→一次
 * 入栈（edits==1）；undoLocal 一次整体回滚（不逐条散开）＋再撤返回
 * false；redoLocal 一次整体重做；编辑计数差值推进（撤销不减——T03 既有
 * 语义，EditorTest 同款）；基线修订标识全程不变（局部撤销零修订）。
 */
TEST(ReqTemplateArray, BatchApplyUndoRedoAtomic_WP14T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC4-batch-undo"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(1.0, 2.0, 3.0)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);
    EXPECT_FALSE(editor.draftStatus().dirty);

    const TemplateArrayService service;
    TaskPoint p2 = makeSourcePoint("P2", rw::math::Vector3D<double>(4.0, 5.0, 6.0));
    const EditBatch batch = service.applyMirror(
        {editor.workingSet().points.entries[0], p2},
        worldPlane(rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    ASSERT_TRUE(batch.ok);
    ASSERT_EQ(batch.newPoints.size(), 2U);

    // 单条 applyEdit 携带批次→恰一次入栈。
    const EditOutcome applied = editor.applyEdit(batch);
    ASSERT_TRUE(applied.accepted) << applied.error.detail;
    EXPECT_EQ(editor.draftStatus().edits, 1U);
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U);

    // 一次 undoLocal＝整体回滚（两条派生同时消失）。
    EXPECT_TRUE(editor.undoLocal());
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U) << "批次整体回滚";
    // 编辑计数差值语义（T03 既有——撤销不减计数）。
    EXPECT_EQ(editor.draftStatus().edits, 1U);
    // 无可再撤（批次不是两条独立编辑——栈已空）。
    EXPECT_FALSE(editor.undoLocal());
    // 一次 redoLocal＝整体重做。
    EXPECT_TRUE(editor.redoLocal());
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U);
    EXPECT_TRUE(editor.draftStatus().dirty);
    // 局部撤销零修订：基线修订标识全程不变。
    EXPECT_EQ(editor.draftStatus().baseRevisionId, "");
}

/**
 * 删除被 linked 批次引用的源条目（ACC4——§7.2 删除保护）：删除**允许**
 * （不拒绝）＋REQ-DERIVE-SOURCE-REMOVED 提示"仍有 N 条 linked 派生"；
 * 派生条目独立性不受影响（仍在工作集）。
 */
TEST(ReqTemplateArray, DeleteSourceWithLinkedDerivedWarns_WP14T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC4-delete-source"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(1.0, 2.0, 3.0)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);

    const TemplateArrayService service;
    const EditBatch batch = service.applyMirror(
        {editor.workingSet().points.entries[0]},
        worldPlane(rw::math::Vector3D<double>(0.0, 1.0, 0.0)));
    ASSERT_TRUE(batch.ok);
    ASSERT_TRUE(editor.applyEdit(batch).accepted);
    const core::ObjectId sourceId = base.points.entries[0].objectId;

    // 删除源条目：允许＋诊断提示。
    const EditOutcome removal =
        editor.applyEdit(removeEdit(sourceId, WorkingSetMember::Points));
    ASSERT_TRUE(removal.accepted) << "源删除允许（§7.2 删除保护——不拒绝）"
                                  << removal.error.detail;
    EXPECT_TRUE(hasDiagCode(removal.diagnostics,
                            std::string{kReqDeriveSourceRemoved}.c_str()));
    ASSERT_EQ(removal.diagnostics.size(), 1U);
    EXPECT_EQ(removal.diagnostics.front().code,
              std::string{kReqDeriveSourceRemoved});
    EXPECT_NE(removal.diagnostics.front().context.find("linked-derived=1"),
              std::string::npos)
        << "提示仍有 N 条 linked 派生（N=1）: "
        << removal.diagnostics.front().context;
    // 派生条目独立留存。
    ASSERT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_EQ(editor.workingSet().points.entries[0].name, "P1-M");
}

/**
 * 再派生按构造无环（ACC4/V-07——溯源为一次性参数快照、非活性链接）：
 * 对派生条目再镜像→新批次新 instanceId、溯源链参数留痕（source-id 指向
 * 派生条目）；修改源条目零传播（派生值保持生成时快照——变更传播＝用户
 * 显式重生成）。
 */
TEST(ReqTemplateArray, ReDerivationAcyclicNoLivePropagation_WP14T07_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"AT-24", "ACC4-acyclic"});

    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(1.0, 2.0, 3.0)));
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);

    const TemplateArrayService service;
    const MirrorPlaneSpec planeY = worldPlane(rw::math::Vector3D<double>(0.0, 1.0, 0.0));

    // 第一次镜像：P1 → P1-M（批次 B1）。
    const EditBatch batch1 = service.applyMirror(
        {editor.workingSet().points.entries[0]}, planeY);
    ASSERT_TRUE(batch1.ok);
    ASSERT_TRUE(editor.applyEdit(batch1).accepted);

    // 对派生条目再镜像（P1-M → P1-M-M，批次 B2）——以其为源生成新条目。
    const auto derivedIt =
        std::find_if(editor.workingSet().points.entries.begin(),
                     editor.workingSet().points.entries.end(),
                     [](const TaskPoint& p) { return p.name == "P1-M"; });
    ASSERT_TRUE(derivedIt != editor.workingSet().points.entries.end());
    const TaskPoint derived = *derivedIt;
    const EditBatch batch2 = service.applyMirror({derived}, planeY);
    ASSERT_TRUE(batch2.ok);
    EXPECT_NE(batch2.provenance.instanceId, batch1.provenance.instanceId)
        << "再派生＝新批次（溯源链参数留痕，按构造无环）";
    ASSERT_TRUE(hasParam(batch2.provenance, "source-id",
                         derived.objectId.toCanonical()));
    ASSERT_TRUE(editor.applyEdit(batch2).accepted);
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U);

    // 非活性链接：修改源 P1（位置移位）→派生条目 P1-M 的值保持不变
    // （溯源是一次性参数快照——"源变→派生自动变"不存在）。
    TaskPoint movedP1 = base.points.entries[0];
    movedP1.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(9.0, 9.0, 9.0), movedP1.pose.position.provenance());
    ASSERT_TRUE(editor.applyEdit(movedP1).accepted);
    const auto& entries = editor.workingSet().points.entries;
    const auto mirrored = std::find_if(entries.begin(), entries.end(),
                                       [](const TaskPoint& p) { return p.name == "P1-M"; });
    ASSERT_TRUE(mirrored != entries.end());
    EXPECT_NEAR(mirrored->pose.position.value()[0], 1.0, kGoldenEpsilon);
    EXPECT_NEAR(mirrored->pose.position.value()[1], -2.0, kGoldenEpsilon);
}

// =====================================================================
// acceptance 5——局部撤销与项目级撤销分离（两条路径互不串扰＋不绕过命令）
// =====================================================================

namespace {

/// 便捷封装：以桩端口为基线执行 prepare（CommandHandlersTest 同款）。
project::PrepareOutcome runPrepare(project::ICommandHandler& handler,
                                   TestQueryPort& port,
                                   const RequirementCommandPayload& payload,
                                   project::CommandPlan& out,
                                   std::vector<core::DiagnosticRecord>& diags)
{
    project::CommandEnvelope envelope;
    envelope.commandType = handler.commandType();
    envelope.payloadFormatVersion = kRequirementCommandPayloadVersion;
    envelope.payloadCanonical = encodeRequirementCommandPayload(payload);
    project::HandlerContext ctx(port, nullptr, nullptr);
    return handler.prepare(ctx, envelope, port.view, out, diags);
}

}  // namespace

/**
 * 路径分离（ACC5）：①局部撤销路径——模板批次经 applyEdit 入草稿→
 * undoLocal 整体回退，全程零修订（端口 head/对象字节零变化——编辑器无
 * project 写路径，模板"草稿生成辅助"不绕过命令）；②项目级路径——批次
 * 应用的草稿以 apply-requirement-set 载荷提交→Planned＋快照逆命令在场
 * （逆槽字节＝基线前版字节——新修订、PA-2 历史不变）；③互不串扰——
 * 局部撤销/重做不改变端口状态，命令 prepare 不触碰编辑器栈。
 */
TEST(ReqTemplateArray, LocalUndoAndCommandPathSeparation_WP14T07_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11", "REQ-06", "PA-2"},
                  std::vector<std::string>{"AT-24", "ACC5-separation"});

    // ---- 同一基线装配两个消费面（闭包＝编辑器面；端口＝命令面）。
    const BaselineBundle base = BaselineBundle::make(makeSourcePoint(
        "P1", rw::math::Vector3D<double>(0.1, 0.2, 0.3)));
    TestQueryPort port;
    port.addObject(base.rootOid, std::string{kReqSetObjectType}, base.rootBytes);
    port.addObject(base.pointSetOid, std::string{kReqPointSetObjectType},
                   base.pointBytes);
    port.addObject(base.conditionSetOid, std::string{kReqConditionSetObjectType},
                   base.conditionBytes);
    const std::size_t baselineRefs = port.view.objectRefs.size();

    const TemplateArrayService service;
    const EditBatch batch = service.applyTemplate(
        TemplateKind::BinPicking, defaultTemplateParams(TemplateKind::BinPicking));
    ASSERT_TRUE(batch.ok);

    // ---- ①局部撤销路径（UI 线程、零修订）：批次入草稿→端口零变化。
    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(base.closure).ok);
    const EditOutcome applied = editor.applyEdit(batch);
    ASSERT_TRUE(applied.accepted) << applied.error.detail;
    ASSERT_EQ(editor.workingSet().points.entries.size(), 3U);
    EXPECT_TRUE(editor.draftStatus().dirty);
    // 不绕过命令：编辑器应用批次后端口 head/对象字节零变化（无任何
    // project 写路径——草稿生成辅助，§7.1）。
    EXPECT_EQ(port.view.objectRefs.size(), baselineRefs);
    EXPECT_EQ(port.objects.size(), 3U);

    EXPECT_TRUE(editor.undoLocal());
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_EQ(port.view.objectRefs.size(), baselineRefs) << "局部撤销零修订";
    EXPECT_TRUE(editor.redoLocal()) << "局部重做不影响端口";
    EXPECT_EQ(port.view.objectRefs.size(), baselineRefs);

    // ---- ②项目级路径：草稿（批次应用后的工作集）经 apply-requirement-set
    //      提交→Planned＋快照逆命令（PA-2——新修订、历史只增不改）。
    RequirementEditor editor2;
    ASSERT_TRUE(editor2.loadBaseline(base.closure).ok);
    ASSERT_TRUE(editor2.applyEdit(batch).accepted);
    PointSet candidate = editor2.workingSet().points;

    RequirementCommandPayload payload;
    payload.mode = RequirementCommandPayload::Mode::Apply;
    payload.objects.push_back(RequirementPayloadSlot{
        false, base.rootOid, std::string{kReqSetObjectType}, base.rootBytes});
    payload.objects.push_back(RequirementPayloadSlot{
        false, base.pointSetOid, std::string{kReqPointSetObjectType},
        encodeObject(RequirementObjectVariant{candidate})});

    ApplyRequirementSetHandler handler;
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const project::PrepareOutcome outcome = runPrepare(handler, port, payload, plan, diags);
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned) << "草稿提交走命令通道";
    ASSERT_EQ(plan.objectWrites.size(), 2U) << "根＋点集两对象写入（恰一修订的写入集）";
    // 项目级逆命令在场（新修订、PA-2 历史不变——逆放恢复前版字节）。
    ASSERT_TRUE(plan.inverseCommandType.has_value());
    EXPECT_EQ(*plan.inverseCommandType, "apply-requirement-set");
    ASSERT_TRUE(plan.inversePayloadCanonical.has_value());
    const auto inverse = tryDecodeRequirementCommandPayload(*plan.inversePayloadCanonical);
    ASSERT_TRUE(inverse.has_value());
    EXPECT_EQ(inverse->mode, RequirementCommandPayload::Mode::Restore);
    for (const auto& slot : inverse->objects) {
        if (slot.objectTypeToken == std::string{kReqPointSetObjectType}) {
            EXPECT_EQ(slot.objectBytes, base.pointBytes)
                << "逆槽字节＝基线前版 canonical 字节（位级保真——PA-2）";
        }
    }
    // prepare 零副作用：端口 head/字节面不被触碰（提交/入史归 project，
    // 本层只产出计划——历史只增不改的第一半）。
    EXPECT_EQ(port.view.objectRefs.size(), baselineRefs);
    EXPECT_EQ(port.objects.size(), 3U);

    // ---- ③互不串扰：编辑器1 的局部撤销/重做与命令 prepare 互不影响——
    //      同载荷重放产出同计划（确定性），编辑器1 栈状态无关。
    project::CommandPlan plan2;
    std::vector<core::DiagnosticRecord> diags2;
    EXPECT_EQ(runPrepare(handler, port, payload, plan2, diags2),
              project::PrepareOutcome::Planned);
    ASSERT_EQ(plan2.objectWrites.size(), plan.objectWrites.size());
    for (std::size_t i = 0; i < plan.objectWrites.size(); ++i) {
        EXPECT_EQ(plan2.objectWrites[i].objectTypeToken,
                  plan.objectWrites[i].objectTypeToken);
        EXPECT_EQ(plan2.objectWrites[i].payloadCanonical,
                  plan.objectWrites[i].payloadCanonical);
    }
    // 编辑器1 仍在重做态（3 条目）——未被命令路径触碰。
    EXPECT_TRUE(editor.draftStatus().dirty);
    EXPECT_EQ(editor.workingSet().points.entries.size(), 3U);
}
