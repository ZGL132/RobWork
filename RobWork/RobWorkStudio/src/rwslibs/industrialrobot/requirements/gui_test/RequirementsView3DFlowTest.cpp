/**
 * @file   RequirementsView3DFlowTest.cpp
 * @brief  需求域三维缝命令流测试（UI-T33——capture-tcp 解除降级后的
 *         真数据源命令流；pick-feature 未拾取指引面）。
 *
 * 设计依据：
 *   - 方案 spec docs/superpowers/specs/2026-10-03-view3d-stage-b-design.md
 *     §2.5（TCP 会话态数据源）；REQ-08（捕获写回确认门）；I-REQ-3/5（域侧
 *     唯一性/容差校验——流程零判定）；
 *   - 先例：PluginPanelTest（MapClosure/makeLoadedEditor/RecordingSink
 *     夹具——本文件同源复制最小集）。
 *
 * 断言纪律：替身缝（listDevices/tcpWorldPose 预置）驱动；草稿断言经
 * editor.workingSet() 现取；QCoreApplication 级（testkit §6.7）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <optional>
#include <string>
#include <vector>

#include <rw/math/Transform3D.hpp>
#include <rw/math/RPY.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/requirements/Capture.hpp>       // 捕获服务/请求模型（写回断言锚）
#include <sdurws/ird/requirements/Editor.hpp>        // RequirementEditor（草稿权威）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // RequirementsView3DSeams
#include "plugin/RequirementsCommandFlows.hpp"       // executeRequirementCommand（被测流）
#include "plugin/PanelEditFlow.hpp"                   // IRequirementEditSink/EditRejection
#include "plugin/RequirementsPanelWidget.hpp"         // 面板（flows 装配端）

using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

namespace {

/// 名称映射替身（PluginPanelTest 同源最小集——闭包字节源）。
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

    std::optional<RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
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

TaskPoint makePoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

core::ObjectId fillBaseline(MapClosure& closure, PointSet points)
{
    for (auto& p : points.entries) { (void)p; }
    RequirementSet root;
    const core::ObjectId pointSetId =
        core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    const core::ObjectId regionSetId =
        core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    const core::ObjectId condSetId =
        core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    const core::ObjectId planSetId =
        core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.name = "基线需求集";
    root.pointSetRef = pointSetId;
    root.regionSetRef = regionSetId;
    root.conditionSetRef = condSetId;
    root.planSetRef = planSetId;
    closure.put(std::string{kReqSetObjectType}, RequirementObjectVariant{root});
    closure.putById(pointSetId, std::string{kReqPointSetObjectType},
                    RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{kReqRegionSetObjectType},
                    RequirementObjectVariant{RegionSet{}});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType},
                    RequirementObjectVariant{ConditionSet{}});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType},
                    RequirementObjectVariant{PlanSet{}});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
}

/// 构造载入单点基线的编辑器（PluginPanelTest 同款——成功前置起步）。
RequirementEditor makeEditorWithNoPoints()
{
    MapClosure closure;
    fillBaseline(closure, PointSet{});
    RequirementEditor editor;
    const auto load = editor.loadBaseline(closure);
    if (!load.ok) {
        throw std::logic_error("夹具装配错误：基线载入失败");
    }
    return editor;
}

/// 命令对话框宿主替身（预置应答——确认文本留痕断言面）。
struct FakeHost final : CommandDialogHost {
    std::optional<int> chooseItemAnswer;       ///< 设备选择应答（nullopt＝取消）
    bool confirmAnswer = true;                 ///< 摘要确认应答
    QString lastConfirmText;                   ///< 确认文本留痕

    std::optional<QString> saveFilePath(const QString&, const QString&,
                                        const QString&) override
    { return std::nullopt; }
    std::optional<QString> openFilePath(const QString&, const QString&) override
    { return std::nullopt; }
    std::optional<int> chooseItem(const QString&, const QString&,
                                  const QStringList& items) override
    {
        lastChooseItems = items;
        return chooseItemAnswer;
    }
    bool editTemplateParams(TemplateParams&) override { return false; }
    bool editArrayParams(int&, double&) override { return false; }
    bool confirmImport(const QString& summaryText) override
    {
        lastConfirmText = summaryText;
        return confirmAnswer;
    }
    RegenerateAction chooseRegenerateAction() override
    { return RegenerateAction::Cancel; }

    QStringList lastChooseItems;  ///< 下拉候选留痕（设备清单断言面）
};

/// 编辑通知收集替身（IRequirementEditSink 最小实现——PluginPanelTest 同款）。
struct RecordingSink final : IRequirementEditSink {
    void onEditApplied(const std::string&) override {}
    void notifySessionDirty() override {}
    void onEditRejected(const EditRejection&) override {}
    void onBatchWarning(const core::DiagnosticRecord&) override {}
};

/// 三维缝替身（预置设备与位姿——断言面即缝值直投）。
struct StubSeams {
    std::vector<std::string> devices;
    std::optional<rw::math::Transform3D<>> pose;
    std::optional<core::ObjectId> lastPicked;  ///< 网关分发落点（UI-T33——拾取流输入面）
    std::vector<project::ObjectRef> closureRefs;  ///< 会话闭包引用（拾取浅核对元数据——UI-T33）
    RequirementsView3DSeams seams() const
    {
        RequirementsView3DSeams s;
        s.listDevices = [this]() { return devices; };
        s.tcpWorldPose = [this](const std::string&) { return pose; };
        s.resolveFrameObjectId = [](const std::string&) {
            return std::nullopt;
        };
        s.lastPickedObjectId = [this]() { return lastPicked; };
        s.sessionClosureRefs = [this]() { return closureRefs; };
        s.sessionRevisionId;  // 空＝无基线（STALE 对账诚实缺省）
        return s;
    }
};

}  // namespace

/// 缝在位＋确认接受 → 新建固定任务点（位置＝替身位姿直投；pointName
/// 确定性命名）——capture-tcp 解除降级的主证面。
TEST(RequirementsView3DFlow, CaptureTcp_SeamsPresent_CreatesFixedPoint_UI_T33)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08", "KIN-06"},
                  std::vector<std::string>{});

    RequirementEditor editor = makeEditorWithNoPoints();
    RequirementsPanelWidget panel(true);

    StubSeams stub;
    stub.devices = {"Robot"};
    stub.pose = rw::math::Transform3D<>(
        rw::math::Vector3D<>(0.4, 0.0, 0.3),
        rw::math::Rotation3D<>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    RequirementsView3DSeams seams = stub.seams();

    FakeHost host;
    host.confirmAnswer = true;
    RecordingSink sink;

    const bool executed = executeRequirementCommand(
        "requirements.capture-tcp", panel, editor, sink, host, &seams);

    EXPECT_TRUE(executed) << "缝在位应解除降级";
    ASSERT_EQ(editor.workingSet().points.entries.size(), std::size_t{1})
        << "捕获未落草稿（新建固定点语义缺失）";
    const TaskPoint& created = editor.workingSet().points.entries.front();
    EXPECT_EQ(created.name, "TcpCapture") << "确定性命名失实";
    ASSERT_TRUE(created.pose.position.tryValue().has_value())
        << "捕获位置未写入";
    const rw::math::Vector3D<double> written =
        created.pose.position.tryValue().value();
    EXPECT_DOUBLE_EQ(written[0], 0.4) << "捕获位置 X 失实（替身位姿直投）";
    EXPECT_DOUBLE_EQ(written[1], 0.0) << "捕获位置 Y 失实";
    EXPECT_DOUBLE_EQ(written[2], 0.3) << "捕获位置 Z 失实";
    EXPECT_NE(host.lastConfirmText.indexOf(QString::fromUtf8("捕获 TCP")), -1)
        << "确认摘要未含语义标题（REQ-08 确认门呈现面）";
}

/// 网关拾取登记＋目标点选中＋确认接受 → 姿态规则写回（AlignFrame——
/// applyPickToOrientation 域裁决；拾取只动姿态规则其余字段原样保留）
/// ——pick-feature 解除引导的主证面（UI-T33 acceptance 4）。
TEST(RequirementsView3DFlow, PickFeature_PickedAndSelected_WritesOrientationRule_UI_T33)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-08", "REQ-10"},
                  std::vector<std::string>{});

    // ①预置：带一个已启用任务点的基线（refFrame=World 缺省）＋拾取目标
    // 登记（ModelFrame 承载对象 token "robot-design"——浅核对的 closure 面）。
    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("tp1"));
    fillBaseline(closure, points);
    const core::ObjectId pickedId =
        core::ObjectId::fromCanonical("obj-60000000000000000000000000000006");
    closure.putById(pickedId, "robot-design",
                    RequirementObjectVariant{RequirementSet{}});

    RequirementEditor editor;
    ASSERT_TRUE(editor.loadBaseline(closure).ok);
    ASSERT_EQ(editor.workingSet().points.entries.size(), std::size_t{1});
    const core::ObjectId pointId =
        editor.workingSet().points.entries.front().objectId;

    // ②缝替身：lastPicked 预置（网关分发落点——Ctrl+双击的等价输入）＋
    // 会话闭包引用（picked 目标的浅核对元数据——ObjectRef token 与
    // expectedTargetToken 字面同源 "robot-design"）。
    StubSeams stub;
    stub.lastPicked = pickedId;
    stub.closureRefs.push_back(project::ObjectRef{
        pickedId, core::ContentVersion{}, std::string("robot-design"), {}});
    RequirementsView3DSeams seams = stub.seams();

    // ③面板重载＋树选中目标点（选中锚＝拾取写回目标——L-R1 选中联动）。
    RequirementsPanelWidget panel(true);
    panel.setEditTargetProvider([&editor]() -> IRequirementEditor* {
        return &editor;  // 选中槽经编辑目标现取工作集——注入缺失＝锚不落
    });
    panel.refreshPanel(editor.workingSet(), RequirementReadinessReport{});
    const QString pointAnchor =
        QString::fromStdString(pointId.toCanonical());
    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr) << "需求树未构建";
    QTreeWidgetItemIterator it(tree);
    while (*it != nullptr && (*it)->text(1) != pointAnchor) { ++it; }
    ASSERT_NE(*it, nullptr) << "任务点树行未投影（选中锚无落点）";
    tree->setCurrentItem(*it);
    ASSERT_TRUE(panel.selectedPointId().has_value())
        << "树选中未落工位锚（写回目标解析输入缺席）";

    // ④确认接受＋执行拾取命令（确认门 REQ-08——写回前确认）。
    FakeHost host;
    host.confirmAnswer = true;
    RecordingSink sink;
    const bool executed = executeRequirementCommand(
        "requirements.pick-feature", panel, editor, sink, host, &seams);

    EXPECT_TRUE(executed) << "缝与选中在位应解除引导";
    // ⑤写回断言：姿态规则＝AlignFrame{picked}；其余字段原样（拾取只动
    // 姿态规则——NFR-COR-03 不顺带改写）。
    const TaskPoint& updated = editor.workingSet().points.entries.front();
    EXPECT_EQ(updated.objectId, pointId) << "写回目标漂移";
    EXPECT_EQ(updated.pose.orientation.kind, OrientationRuleKind::AlignFrame)
        << "姿态规则未写回（拾取收口语义缺失）";
    ASSERT_TRUE(updated.pose.orientation.targetFrame.objectId.has_value());
    EXPECT_TRUE(updated.pose.orientation.targetFrame.objectId.value() == pickedId)
        << "AlignFrame 目标失实（picked 直投语义缺失）";
    EXPECT_NE(host.lastConfirmText.indexOf(QString::fromUtf8("拾取")), -1)
        << "确认摘要未含拾取语义（REQ-08 确认门呈现面）";
}
