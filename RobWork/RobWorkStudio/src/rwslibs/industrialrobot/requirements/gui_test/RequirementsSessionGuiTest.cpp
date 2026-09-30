/**
 * @file   RequirementsSessionGuiTest.cpp
 * @brief  需求面板会话接线 Widgets 级契约测试（sdurws_ird_requirements_
 *         gui_test 载体）——UI-T29 会话接线＋最小校验的具名自证面。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T29.json acceptance 1/2/5 的具名用例
 *     （会话数据面：基线闭包载入→四集合入面板；编辑生效：宿主编辑后
 *     动作随 L-2 接受触发；最小校验：就绪判定权威＝域侧 checker——
 *     gate 真值源双态）；
 *   - units/requirements.md §9.8（面板组成/L-R2 编辑流）、§4.6（编辑器
 *     基线/工作集）、§8（就绪校验分层——Blocking 语义）；
 *   - 先例：requirements/app/RequirementsHarnessMain.cpp（MapClosure/
 *     fillBaseline 夹具——同源复用）；modeling/gui_test（QApplication
 *     main＋findChild 定位形态）。
 *
 * 断言纪律：组合子编排（宿主重估→模块以最新报告 refreshPanel）经模块
 * 公共面装配后由 onEditApplied 直呼触发（IPanelEditSink 公共覆盖——
 * UI 线程同线程直调）；视觉呈现细节归验收截图，本组断言数据面。
 */

#include <gtest/gtest.h>

#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/requirements/Editor.hpp>       // RequirementEditor＋闭包视图（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // kReqSetObjectType 等 token（公共头常量）
#include <sdurws/ird/requirements/Readiness.hpp>    // RequirementReadinessChecker（判定权威）

#include "plugin/RequirementsPanelWidget.hpp"  // 被测面板（同单元 PRIVATE include 面）
#include "plugin/RequirementsUiModule.hpp"     // 模块组合子（宿主接线同款形态）

using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

namespace {

/// 闭包域字节源的测试实现（harness MapClosure 同款——内存映射形态）。
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
        return it != byToken_.end() ? std::optional{it->second} : std::nullopt;
    }

    std::optional<RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        return it != byId_.end() ? std::optional{it->second} : std::nullopt;
    }

private:
    std::map<std::string, RequirementClosureObject> byToken_;
    std::map<core::ObjectId, RequirementClosureObject> byId_;
};

/// 合法任务点（位置已提供——harness makePoint 同款）。
TaskPoint makePoint(const std::string& name, double x, double y, double z)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 合法工作区域（I-REQ-6 非退化盒＋Grid 采样——harness makeRegion 同款）。
WorkRegion makeRegion(const std::string& name)
{
    WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = name;
    r.box = BoundingBox{rw::math::Vector3D<double>(1.0, 2.0, 3.0),
                        rw::math::Vector3D<double>(2.0, 2.0, 2.0)};
    r.positionSampling = PositionSampling{PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
    return r;
}

/// 根＋四集合闭包填充（harness fillBaseline 同款——固定 set oid＋根 oid）。
core::ObjectId fillBaseline(MapClosure& closure,
                            PointSet points,
                            RegionSet regions,
                            ConditionSet conditions)
{
    sortEntriesByObjectId(points.entries);
    sortEntriesByObjectId(regions.entries);
    sortEntriesByObjectId(conditions.entries);
    RequirementSet root;
    const core::ObjectId pointSetId =
        core::ObjectId::fromCanonical("obj-10000000000000000000000000000001");
    const core::ObjectId regionSetId =
        core::ObjectId::fromCanonical("obj-20000000000000000000000000000002");
    const core::ObjectId condSetId =
        core::ObjectId::fromCanonical("obj-30000000000000000000000000000003");
    const core::ObjectId planSetId =
        core::ObjectId::fromCanonical("obj-40000000000000000000000000000004");
    root.name = "演示需求集";
    root.pointSetRef = pointSetId;
    root.regionSetRef = regionSetId;
    root.conditionSetRef = condSetId;
    root.planSetRef = planSetId;
    closure.put(std::string{kReqSetObjectType}, RequirementObjectVariant{root});
    closure.putById(pointSetId, std::string{kReqPointSetObjectType},
                    RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{kReqRegionSetObjectType},
                    RequirementObjectVariant{regions});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType},
                    RequirementObjectVariant{conditions});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType},
                    RequirementObjectVariant{PlanSet{}});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
}

}  // namespace

// =====================================================================
// 夹具：基线载入＋面板＋就绪校验器（每用例独立——零跨用例状态）
// =====================================================================

class RequirementsSessionGuiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        MapClosure closure;
        PointSet points;
        points.entries.push_back(makePoint("P1", 1.0, 2.0, 3.0));
        RegionSet regions;
        regions.entries.push_back(makeRegion("R1"));
        ConditionSet conditions;
        const core::ObjectId rootOid = fillBaseline(closure, points, regions, conditions);
        m_rootOid = rootOid;
        const auto load = m_editor.loadBaseline(closure);
        ASSERT_TRUE(load.ok) << "基线载入失败（夹具装配错误）";
        m_report = m_checker.check(m_editor.workingSet(), CheckContext{});
        m_panel = std::make_unique<RequirementsPanelWidget>(true);
        // 编辑目标提供器（L-R2 数据前提——检查器/编辑流经提供器现取权威
        // 编辑器；harness ③同款装配）。
        m_panel->setEditTargetProvider([this]() -> IRequirementEditor* {
            return &m_editor;
        });
    }

    core::ObjectId m_rootOid{};                       ///< 根对象身份（定位跳转用）
    RequirementEditor m_editor;                       ///< 会话权威编辑器（宿主持有形态）
    RequirementReadinessChecker m_checker;            ///< 判定权威（直投值面）
    RequirementReadinessReport m_report{};            ///< 首刷就绪报告
    std::unique_ptr<RequirementsPanelWidget> m_panel; ///< 被测面板
};

// =====================================================================
// acceptance 1 会话数据面：基线闭包载入→四集合入面板（树投影＋定位）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, SessionDataPlane_FourSetsIntoPanel_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 首刷（装配层 refreshPanel 编排的测试面直呼——harness ④同款）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 树投影：自持结构树出现需求条目（根＋任务点——非空即"四集合入面板"
    // 的观测面；空会话形态下树为空组，本断言区分二态）。
    const auto trees = m_panel->findChildren<QTreeWidget*>();
    ASSERT_FALSE(trees.isEmpty()) << "面板无树控件（五区构建缺陷）";
    int totalItems = 0;
    for (const QTreeWidget* tree : trees) {
        totalItems += tree->topLevelItemCount();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            totalItems += tree->topLevelItem(i)->childCount();
        }
    }
    EXPECT_GT(totalItems, 0) << "基线载入后树投影为空（会话数据面断链）";

    // 定位：任务点聚焦→检查器编辑行出现（L-R2 编辑面的数据前提）。
    const core::ObjectId pointOid = m_editor.workingSet().points.entries.front().objectId;
    m_panel->focusObject(pointOid);
    EXPECT_FALSE(m_panel->findChildren<QLineEdit*>().isEmpty())
        << "任务点聚焦后检查器无编辑行（L-R2 数据前提断链）";
}

// =====================================================================
// acceptance 2/5 编辑生效＋最小校验：L-2 接受→宿主编辑后动作触发→
// 模块组合子以会话最新报告 refreshPanel（校验页实时化的编排面）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, PostEditComposition_HostActionThenRefresh_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 宿主接线同款装配（DomainAssembly 需求段的产品形态）：模块挂编辑器
    // →宿主编辑后动作（重估＋bindReadiness 由宿主承载，测试以计数替身）
    // →面板创建后 attachPanel（组合子补挂）。
    RequirementsUiModule module;
    module.attachEditor(&m_editor);
    int hostActionCount = 0;
    module.setPostEditAction([&hostActionCount] { ++hostActionCount; });
    module.attachPanel(m_panel.get());
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // L-2 接受触发（IPanelEditSink 公共覆盖——UI 线程同线程直调）：
    // 组合子先行宿主动作（恰一次），随后以会话报告 refreshPanel（重入
    // 面板刷新——可观测面＝刷新后树仍完整，即重投影未断链）。
    m_panel->onEditApplied("测试接受（组合子触发面）");
    EXPECT_EQ(hostActionCount, 1) << "宿主编辑后动作未触发或重复触发（组合子编排失守）";

    const auto trees = m_panel->findChildren<QTreeWidget*>();
    int totalItems = 0;
    for (const QTreeWidget* tree : trees) {
        totalItems += tree->topLevelItemCount();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            totalItems += tree->topLevelItem(i)->childCount();
        }
    }
    EXPECT_GT(totalItems, 0) << "组合子 refreshPanel 后树投影为空（重投影断链）";
}

// =====================================================================
// acceptance 4/5 gate 真值源双态：就绪判定权威＝域侧 checker——干净基线
// 无阻断／违约基线（Must 任务点位置未提供）有阻断（draft.apply 门控的
// 真值源；宿主谓词只消费 hasBlocking 直投值——P-REQ-6）。
// =====================================================================

TEST_F(RequirementsSessionGuiTest, GateTruthSource_BlockingDoubleState_UI_T29)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 接通态：违约基线（Must 任务点位置未提供——R4 位姿层阻断的构造面，
    // checker 判定，本测试只构造输入）→hasBlocking＝true（draft.apply
    // 门控接通的真值面）。
    MapClosure closure;
    PointSet points;
    TaskPoint broken = makePoint("P-断链", 0.0, 0.0, 0.0);
    broken.pose.position = core::SourcedValue<rw::math::Vector3D<double>>{};
    points.entries.push_back(broken);
    RegionSet regions;
    regions.entries.push_back(makeRegion("R1"));
    ConditionSet conditions;
    fillBaseline(closure, points, regions, conditions);
    RequirementEditor brokenEditor;
    const auto load = brokenEditor.loadBaseline(closure);
    ASSERT_TRUE(load.ok) << "违约基线载入失败（夹具装配错误）";
    const RequirementReadinessReport brokenReport =
        m_checker.check(brokenEditor.workingSet(), CheckContext{});
    EXPECT_TRUE(brokenReport.hasBlocking()) << "违约基线未判阻断（gate 真值源失真）";

    // 确定性（gate 快照可复现的前提）：同一工作集两次 check→hasBlocking
    // 结论一致。释放态（干净基线→hasBlocking＝false）由同套件
    // requirements_test 的 ReadinessTest.HealthySetPassesAllLayers_ACC1
    // 钉住——健康夹具链为该用例既有面，本组不重复构造。
    const RequirementReadinessReport again =
        m_checker.check(brokenEditor.workingSet(), CheckContext{});
    EXPECT_EQ(again.hasBlocking(), brokenReport.hasBlocking())
        << "同输入两次判定结论漂移（gate 快照不可复现）";
}
