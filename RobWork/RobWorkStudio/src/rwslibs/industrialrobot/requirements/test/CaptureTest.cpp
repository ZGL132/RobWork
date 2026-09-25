/**
 * @file   CaptureTest.cpp
 * @brief  三维拾取/TCP 捕获域侧用例组（ReqCapture）——确认写回与来源
 *         标记（契约 WP-14-T06 acceptance 3——L-R6/L-R7、REQ-10、AT-23）、
 *         未确认/取消零数据变更与接口面不可绕过（acceptance 4——REQ-08）、
 *         STALE 警告与未应用不失效。
 *
 * 设计依据：units/requirements.md §9.8（L-R6/L-R7 数据流）、§9.6（REQ-
 * CAPTURE-STATE-STALE 行）、§5.1（source＝UserProvided＋methodTag=
 * captured-tcp）、§4.6（草稿态——未应用零修订）、ui.md §9.2（确认
 * 凭据只读消费）。
 */

#include <sdurws/ird/requirements/Capture.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/requirements/Codec.hpp>
#include <sdurws/ird/requirements/Editor.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

/// core 命名空间别名（ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 固定身份（规范文本构造——断言可精确比对）。
core::ObjectId oidOf(const char* canonical)
{
    return core::ObjectId::fromCanonical(canonical);
}

/// 闭包元数据登记（ObjectRef 元数据——无 modeling 对象字节，§8.1）。
void addRef(CheckContext& ctx, const core::ObjectId& oid, std::string token)
{
    project::ObjectRef ref;
    ref.objectId = oid;
    ref.contentVersion.bytes[0] = static_cast<std::uint8_t>(ctx.closureRefs.size() + 1U);
    ref.objectTypeToken = std::move(token);
    ref.digest256 = std::string(64, '0');
    ctx.closureRefs.push_back(std::move(ref));
}

/// 测试用闭包字节源（EditorTest 同款内存映射——按 token/id 取回）。
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

/// 装配"根＋四集合"基线闭包（集合 id 固定；points 由调用方给定——
/// 拾取写回目标条目的载体）。
class BaselineFixture {
public:
    BaselineFixture()
    {
        root_.name = "基线需求集";
        root_.pointSetRef = pointSetId_;
        root_.regionSetRef = regionSetId_;
        root_.conditionSetRef = condSetId_;
        root_.planSetRef = planSetId_;
        rebuild();
    }

    /// 以给定点集重建闭包映射（loadBaseline 前置——四集合齐全）。
    void rebuild()
    {
        closure_.put(std::string{kReqSetObjectType}, RequirementObjectVariant{root_});
        closure_.putById(pointSetId_, std::string{kReqPointSetObjectType},
                         RequirementObjectVariant{points_});
        closure_.putById(regionSetId_, std::string{kReqRegionSetObjectType},
                         RequirementObjectVariant{RegionSet{}});
        closure_.putById(condSetId_, std::string{kReqConditionSetObjectType},
                         RequirementObjectVariant{ConditionSet{}});
        closure_.putById(planSetId_, std::string{kReqPlanSetObjectType},
                         RequirementObjectVariant{PlanSet{}});
    }

    MapClosure& closure() { return closure_; }
    PointSet& points() { return points_; }
    const core::ObjectId& pointSetId() const { return pointSetId_; }

    /// 载入编辑器基线（须成功——夹具自检）。
    void load(IRequirementEditor& editor)
    {
        rebuild();
        const RequirementLoadOutcome loaded = editor.loadBaseline(closure_);
        if (!loaded.ok) {
            FAIL() << "夹具基线载入失败（夹具自检）: " << loaded.error.detail;
        }
    }

private:
    core::ObjectId pointSetId_ = oidOf("obj-10000000000000000000000000000001");
    core::ObjectId regionSetId_ = oidOf("obj-20000000000000000000000000000002");
    core::ObjectId condSetId_ = oidOf("obj-30000000000000000000000000000003");
    core::ObjectId planSetId_ = oidOf("obj-40000000000000000000000000000004");
    RequirementSet root_;
    PointSet points_;
    MapClosure closure_;
};

/// 基线内的既有任务点（拾取写回目标——World 系/Z 约束/work 启用）。
TaskPoint makeTargetPoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = oidOf("obj-00000000000000000000000000000a01");
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.5, 0.5, 0.5),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 已确认凭据（固定主体/时刻——确定性断言；真实流程中由 ui/project 采集）。
CaptureConfirmation confirmedReceipt()
{
    std::chrono::system_clock::time_point t{};  // epoch——测试固定值
    return CaptureConfirmation::confirmed("tester", t);
}

/// 健康捕获请求（会话基线为空串＝编辑器同口径——无 STALE；位置/姿态
/// 有限正值——构造边界必过）。
CaptureTcpRequest makeCaptureRequest(const std::string& name)
{
    CaptureTcpRequest req;
    req.captured.position = rw::math::Vector3D<double>(1.0, 2.0, 3.0);  // m
    req.captured.rpy = rw::math::Vector3D<double>(0.1, 0.2, 0.3);       // rad
    req.captured.refFrame = RequirementReference{};                     // World 缺省
    req.captured.sessionRevisionId = "";                                // 空基线口径
    req.pointName = name;
    req.confirmation = confirmedReceipt();
    return req;
}

}  // namespace

/// 接口形状探针（编译期——acc4 接口面审查的编译期半边）：T 是否内嵌
/// 可访问的 CaptureConfirmation confirmation 成员（SFINAE 探测——成员
/// 缺失/改名/类型漂移即 false，静态断言随即编译失败）。
template <class T, class = void>
struct RequiresCaptureConfirmationMember : std::false_type {
};

template <class T>
struct RequiresCaptureConfirmationMember<
    T, std::void_t<decltype(std::declval<T&>().confirmation)>>
    : std::is_same<decltype(std::declval<T&>().confirmation), CaptureConfirmation> {
};

// =====================================================================
// acceptance 3——L-R7 TCP 捕获（确认写回/来源标记/STALE/未应用不失效）
// =====================================================================

/**
 * 确认后捕获写草稿（ACC3——L-R7 正例：REQ-10"捕获 TCP 位姿进入任务点
 * 草稿"）：产出条目为 Fixed 规则（参数字面＝捕获 rpy）、六分量全约束、
 * 位置＝捕获值；来源标记 methodTag=captured-tcp（UserProvided——§5.1
 * 来源标记行）；编辑器工作集收录＋脏标记＋编辑计数 +1（草稿态——零
 * 修订，§4.6）。
 */
TEST(ReqCapture, ConfirmedCaptureWritesFixedPointWithCapturedTag_WP14T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-10"}, std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;

    const CaptureTcpRequest req = makeCaptureRequest("捕获工位");
    const CaptureOutcome out = service.captureTcpAsFixedPoint(
        editor, req, CheckContext{});

    ASSERT_TRUE(out.accepted) << "确认后捕获必须写回草稿: " << out.error.detail;
    EXPECT_FALSE(out.rejectedUnconfirmed);
    // 条目值：Fixed 规则＋捕获 rpy 字面＋全约束掩码＋位置 Provided。
    EXPECT_EQ(out.draftEntry.pose.orientation.kind, OrientationRuleKind::Fixed);
    EXPECT_DOUBLE_EQ(out.draftEntry.pose.orientation.fixedRpy[0], 0.1);
    EXPECT_DOUBLE_EQ(out.draftEntry.pose.orientation.fixedRpy[1], 0.2);
    EXPECT_DOUBLE_EQ(out.draftEntry.pose.orientation.fixedRpy[2], 0.3);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.x);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.y);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.z);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.roll);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.pitch);
    EXPECT_TRUE(out.draftEntry.pose.constrainedDof.yaw);
    ASSERT_TRUE(out.draftEntry.pose.position.tryValue().has_value());
    EXPECT_DOUBLE_EQ(out.draftEntry.pose.position.tryValue()->operator[](0), 1.0);
    // 来源标记（R-REQ-5 缓解第二半——条目自证捕获来源，可追溯重捕）。
    EXPECT_EQ(out.draftEntry.source.kind, core::ProvenanceKind::UserProvided);
    ASSERT_TRUE(out.draftEntry.source.methodTag.has_value());
    EXPECT_EQ(*out.draftEntry.source.methodTag, "captured-tcp");
    // 草稿态：工作集收录＋脏标记＋编辑计数（零修订——未走命令族）。
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_EQ(editor.workingSet().points.entries.front().name, "捕获工位");
    const RequirementDraftStatus st = editor.draftStatus();
    EXPECT_TRUE(st.dirty);
    EXPECT_EQ(st.edits, 1U);
    // 对账诊断：空基线口径下（会话基线同为空串）无 STALE。
    EXPECT_TRUE(out.diags.empty());
}

/**
 * 未确认＝零数据变更（ACC4——REQ-08 负向用例"取消/关闭拾取态＝零数据
 * 变更"）：Rejected/Pending/凭据违约（Confirmed 空主体）三种未确认形态
 * 全部 rejectedUnconfirmed，工作集/撤销栈/编辑计数零变化（确认门先于
 * 一切写路径——接口面审查的运行期半边）。
 */
TEST(ReqCapture, UnconfirmedCaptureZeroDataChange_WP14T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08", "REQ-10"},
                  std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;
    const RequirementWorkingSet before = editor.workingSet();
    const RequirementDraftStatus stBefore = editor.draftStatus();

    // 形态①：用户拒绝（取消拾取写回）。
    {
        CaptureTcpRequest req = makeCaptureRequest("拒绝工位");
        req.confirmation = CaptureConfirmation::rejected();
        const CaptureOutcome out = service.captureTcpAsFixedPoint(editor, req, CheckContext{});
        EXPECT_FALSE(out.accepted);
        EXPECT_TRUE(out.rejectedUnconfirmed);
        EXPECT_TRUE(out.diags.empty()) << "取消是正常流——零诊断（Canceled 轴）";
    }
    // 形态②：对话未决（Pending——到达域服务即未决态，同走零变更）。
    {
        CaptureTcpRequest req = makeCaptureRequest("未决工位");
        req.confirmation.state = core::ConfirmationState::Pending;
        const CaptureOutcome out = service.captureTcpAsFixedPoint(editor, req, CheckContext{});
        EXPECT_TRUE(out.rejectedUnconfirmed);
    }
    // 形态③：凭据违约（Confirmed 态空主体——wellFormed 拒绝）。
    {
        CaptureTcpRequest req = makeCaptureRequest("违约工位");
        // 构造"Confirmed 但主体为空"违约：清空工厂填充的主体（工厂数据
        // 本身合法——违约形态由调用方注入，正是本形态要钉的防御面）。
        req.confirmation.credential.principal.clear();
        req.confirmation.state = core::ConfirmationState::Confirmed;
        const CaptureOutcome out = service.captureTcpAsFixedPoint(editor, req, CheckContext{});
        EXPECT_TRUE(out.rejectedUnconfirmed);
    }

    // 零数据变更：三次拒绝后工作集/状态与初始全等。
    EXPECT_TRUE(editor.workingSet() == before);
    const RequirementDraftStatus stAfter = editor.draftStatus();
    EXPECT_EQ(stAfter.dirty, stBefore.dirty);
    EXPECT_EQ(stAfter.edits, stBefore.edits);
}

/**
 * 会话基线错位→REQ-CAPTURE-STATE-STALE（ACC3——R-REQ-5 缓解：warning
 * 登记不阻断，知情写回）：会话基线非空而草稿基线未标注（空串口径）→
 * 诊断码命中且 context 携两侧值对照；捕获值仍经确认门写草稿（accepted）。
 */
TEST(ReqCapture, StaleSessionEmitsWarningButWrites_WP14T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-10", "ERR-01"},
                  std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;

    CaptureTcpRequest req = makeCaptureRequest("错位工位");
    req.captured.sessionRevisionId = "rev-0001";  // 会话基线≠草稿基线（空串）
    const CaptureOutcome out = service.captureTcpAsFixedPoint(editor, req, CheckContext{});

    // 警告登记（warning——不阻断）。
    ASSERT_EQ(out.diags.size(), 1U);
    EXPECT_EQ(out.diags.front().code, "REQ-CAPTURE-STATE-STALE");
    EXPECT_NE(out.diags.front().context.find("session-revision=rev-0001"),
              std::string::npos);
    EXPECT_NE(out.diags.front().context.find("draft-baseline=<none>"),
              std::string::npos);
    // 知情写回：确认门已过——草稿照常更新（R-REQ-5 缓解语义）。
    EXPECT_TRUE(out.accepted);
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
}

/**
 * 未应用不失效（ACC3——DTB 完成条件原文）：确认捕获仅写编辑器草稿；
 * 丢弃草稿（重载同一基线闭包）后工作集回到基线、基线对象字节零变化、
 * 修订闭包元数据零变化——源数据与修订在捕获全流程中零触（捕获服务
 * 接口面无任何修订/命令端口——结构保证）。
 */
TEST(ReqCapture, NotAppliedLeavesBaselineAndRevisionsUntouched_WP14T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-10"}, std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;

    // 基线点集对象字节快照（CON-05——内容寻址：字节不变＝身份不变）。
    const RequirementCodec codec;
    const auto baselineBytes =
        codec.encode(RequirementObjectVariant{fx.points()}, kCurrentRequirementFormatVersion);
    ASSERT_TRUE(baselineBytes.ok());

    // 确认捕获入草稿。
    const CaptureOutcome out =
        service.captureTcpAsFixedPoint(editor, makeCaptureRequest("丢弃工位"), CheckContext{});
    ASSERT_TRUE(out.accepted);
    ASSERT_EQ(editor.workingSet().points.entries.size(), 1U);  // 草稿已含捕获条目

    // 丢弃＝重载同一基线（ui 关闭/放弃草稿的域侧等价动作）：工作集回到
    // 基线态，草稿脏标记清零。
    fx.load(editor);
    EXPECT_TRUE(editor.workingSet().points.entries.empty());
    const RequirementDraftStatus st = editor.draftStatus();
    EXPECT_FALSE(st.dirty);
    EXPECT_EQ(st.edits, 0U);

    // 基线对象字节零变化（修订零变化的字节面——重编码逐字节一致；
    // RequirementBytes＝vector<uint8_t>，直接逐字节比较）。
    const auto afterBytes =
        codec.encode(RequirementObjectVariant{fx.points()}, kCurrentRequirementFormatVersion);
    ASSERT_TRUE(afterBytes.ok());
    EXPECT_EQ(afterBytes.get(), baselineBytes.get());
}

// =====================================================================
// acceptance 3/4——L-R6 几何特征拾取（确认写回/悬空拦截/零变更）
// =====================================================================

/**
 * Frame 拾取确认后更新姿态规则（ACC3——L-R6 正例：拾取结果（Frame
 * ObjectId）→确认→入草稿）：目标任务点的姿态规则替换为 AlignFrame
 * （目标＝拾取对象），其余字段原样保留（NFR-COR-03 不顺带改写）。
 */
TEST(ReqCapture, ConfirmedFramePickUpdatesOrientationRule_WP14T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-10", "REQ-08"},
                  std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    fx.points().entries.push_back(makeTargetPoint("拾取目标点"));
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;

    const core::ObjectId frameOid = oidOf("obj-00000000000000000000000000000b01");
    CheckContext closure;
    addRef(closure, frameOid, "robot-design");  // Frame 拾取目标的闭包元数据

    ApplyPickRequest req;
    req.picked.target.kind = RequirementRefKind::ModelFrame;
    req.picked.target.objectId = frameOid;
    req.targetPointId = oidOf("obj-00000000000000000000000000000a01");
    req.confirmation = confirmedReceipt();

    const CaptureOutcome out = service.applyPickToOrientation(editor, req, closure);
    ASSERT_TRUE(out.accepted) << "确认后拾取必须写回草稿: " << out.error.detail;
    // 姿态规则替换为 AlignFrame（目标＝拾取对象）。
    EXPECT_EQ(out.draftEntry.pose.orientation.kind, OrientationRuleKind::AlignFrame);
    ASSERT_TRUE(out.draftEntry.pose.orientation.targetFrame.objectId.has_value());
    EXPECT_EQ(*out.draftEntry.pose.orientation.targetFrame.objectId, frameOid);
    // 其余字段原样保留（拾取只动姿态规则）。
    EXPECT_EQ(out.draftEntry.name, "拾取目标点");
    EXPECT_DOUBLE_EQ(out.draftEntry.tolerance.positionTolerance, 1.0e-3);
    ASSERT_TRUE(out.draftEntry.pose.position.tryValue().has_value());
    EXPECT_DOUBLE_EQ(out.draftEntry.pose.position.tryValue()->operator[](0), 0.5);
    // 草稿态：同 id 更新（条目数不增——upsert）。
    EXPECT_EQ(editor.workingSet().points.entries.size(), 1U);
    EXPECT_TRUE(editor.draftStatus().dirty);
}

/**
 * 几何特征拾取构造 AlignGeometryNormal（ACC3——L-R6 特征分支：场景对象
 * ＋特征＋取反标志装配为特征法向规则）。
 */
TEST(ReqCapture, ConfirmedFeaturePickBuildsAlignGeometryNormal_WP14T06_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-10"}, std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    fx.points().entries.push_back(makeTargetPoint("特征目标点"));
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;

    const core::ObjectId sceneOid = oidOf("obj-00000000000000000000000000000b02");
    CheckContext closure;
    addRef(closure, sceneOid, "scene-object");

    ApplyPickRequest req;
    req.picked.target.kind = RequirementRefKind::SceneObject;
    req.picked.target.objectId = sceneOid;
    req.picked.feature = OrientationFeature::FramePlaneNormal;
    req.picked.invertNormal = true;
    req.targetPointId = oidOf("obj-00000000000000000000000000000a01");
    req.confirmation = confirmedReceipt();

    const CaptureOutcome out = service.applyPickToOrientation(editor, req, closure);
    ASSERT_TRUE(out.accepted);
    const OrientationRule& rule = out.draftEntry.pose.orientation;
    EXPECT_EQ(rule.kind, OrientationRuleKind::AlignGeometryNormal);
    ASSERT_TRUE(rule.targetSceneObject.has_value());
    EXPECT_EQ(*rule.targetSceneObject, sceneOid);
    ASSERT_TRUE(rule.feature.has_value());
    EXPECT_EQ(*rule.feature, OrientationFeature::FramePlaneNormal);
    EXPECT_TRUE(rule.invertNormal);
}

/**
 * 拾取未确认/悬空目标＝零数据变更＋可定位诊断（ACC4 负向——取消拾取
 * 态零变更；ACC2——过期场景的悬空拾取结果不写回且诊断回指目标任务点
 * （objectId＋name））。
 */
TEST(ReqCapture, PickUnconfirmedOrDanglingZeroChangeWithDiag_WP14T06_ACC2_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08", "REQ-10", "REQ-09"},
                  std::vector<std::string>{"AT-23"});

    BaselineFixture fx;
    fx.points().entries.push_back(makeTargetPoint("拾取目标点"));
    RequirementEditor editor;
    fx.load(editor);
    const RequirementCaptureService service;
    const RequirementWorkingSet before = editor.workingSet();

    const core::ObjectId danglingOid = oidOf("obj-00000000000000000000000000000b09");

    // 形态①：未确认（取消拾取态）——零数据变更、零诊断。
    {
        ApplyPickRequest req;
        req.picked.target.kind = RequirementRefKind::ModelFrame;
        req.picked.target.objectId = danglingOid;
        req.targetPointId = oidOf("obj-00000000000000000000000000000a01");
        req.confirmation = CaptureConfirmation::rejected();
        const CaptureOutcome out = service.applyPickToOrientation(editor, req, CheckContext{});
        EXPECT_TRUE(out.rejectedUnconfirmed);
        EXPECT_TRUE(out.diags.empty());
    }
    // 形态②：悬空拾取目标（确认已过、目标不在闭包）——拒绝＋REF-MISSING
    // 回指目标任务点（subject=条目 id/localName=条目名）。
    {
        CheckContext closure;  // 空闭包——拾取目标必悬空
        ApplyPickRequest req;
        req.picked.target.kind = RequirementRefKind::ModelFrame;
        req.picked.target.objectId = danglingOid;
        req.targetPointId = oidOf("obj-00000000000000000000000000000a01");
        req.confirmation = confirmedReceipt();
        const CaptureOutcome out = service.applyPickToOrientation(editor, req, closure);
        EXPECT_FALSE(out.accepted);
        EXPECT_FALSE(out.rejectedUnconfirmed);
        ASSERT_EQ(out.diags.size(), 1U);
        EXPECT_EQ(out.diags.front().code, "REQ-READY-REF-MISSING");
        ASSERT_TRUE(out.diags.front().subject.has_value());
        EXPECT_EQ(*out.diags.front().subject, oidOf("obj-00000000000000000000000000000a01"));
        ASSERT_TRUE(out.diags.front().localName.has_value());
        EXPECT_EQ(*out.diags.front().localName, "拾取目标点");
    }
    // 形态③：目标任务点不存在（引用违约）——值面拒绝（MalformedPayload）。
    {
        const core::ObjectId sceneOid = oidOf("obj-00000000000000000000000000000b02");
        CheckContext closure;
        addRef(closure, sceneOid, "scene-object");
        ApplyPickRequest req;
        req.picked.target.kind = RequirementRefKind::SceneObject;
        req.picked.target.objectId = sceneOid;
        req.picked.feature = OrientationFeature::FrameOrigin;
        req.targetPointId = oidOf("obj-00000000000000000000000000000e0f");  // 不存在
        req.confirmation = confirmedReceipt();
        const CaptureOutcome out = service.applyPickToOrientation(editor, req, closure);
        EXPECT_FALSE(out.accepted);
        EXPECT_EQ(out.error.code, RequirementErrorCode::MalformedPayload);
    }

    // 全部形态零数据变更：工作集与初始全等。
    EXPECT_TRUE(editor.workingSet() == before);
}

/**
 * 接口面不可绕过（ACC4——接口面审查的编译期半边自证）：写回入口仅
 * captureTcpAsFixedPoint/applyPickToOrientation 两个，均以
 * CaptureConfirmation 必填参数承接确认结果——本用例以编译期静态断言
 * 钉住"请求结构体必含确认成员"（成员缺失即编译失败＝不存在绕过确认
 * 的请求装配形态），与 Unconfirmed 用例的运行期半边互补。
 */
TEST(ReqCapture, WriteEntriesRequireConfirmationMemberByShape_WP14T06_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08"}, std::vector<std::string>{});

    // 请求结构体形状钉：确认成员存在且非 trivially 绕过（编译期断言
    // ——成员删除/改名即刻编译失败，接口漂移零容忍）。
    static_assert(RequiresCaptureConfirmationMember<CaptureTcpRequest>::value,
                  "CaptureTcpRequest 必须内嵌 CaptureConfirmation confirmation 成员"
                  "（REQ-08——写回前必须确认的接口面形状钉）");
    static_assert(RequiresCaptureConfirmationMember<ApplyPickRequest>::value,
                  "ApplyPickRequest 必须内嵌 CaptureConfirmation confirmation 成员"
                  "（REQ-08——写回前必须确认的接口面形状钉）");
    SUCCEED() << "两写入口请求形状均内嵌确认成员（编译期核查通过）";
}
