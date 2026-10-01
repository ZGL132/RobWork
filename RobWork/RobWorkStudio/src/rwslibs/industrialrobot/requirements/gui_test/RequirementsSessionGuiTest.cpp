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
        // 工况起步条目（UI-T30 生命周期的工况集合操作载体——T29 版夹具
        // 此处为空，B1 用例需要非空集合）。
        OperatingCondition c1;
        c1.objectId = core::ObjectId::generate();
        c1.name = "C1";
        conditions.entries.push_back(c1);
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

// =====================================================================
// UI-T30 B1 对象生命周期：三集合新增/复制/删除＋锚回落＋撤销＋无会话门控
// （触发面＝objectName 锚按钮 click——与用户点击同路径；断言面＝集合
// 规模＋页签状态行呈现〔选中锚的呈现投影〕＋检查器编辑行）。
// =====================================================================

/// 生命周期按钮现取（objectName 锚——makeLifecycleBar 的定位面）。
QPushButton* lifecycleButton(const RequirementsPanelWidget& panel, const char* action,
                             const char* key)
{
    return const_cast<RequirementsPanelWidget&>(panel)
        .findChild<QPushButton*>(
            QStringLiteral("ird_req_%1_%2")
                .arg(QString::fromLatin1(action), QString::fromLatin1(key)));
}

/// 页签状态行文本现取（选中锚的呈现投影——工位/区域/工况页头）。
QString tabHeaderText(const RequirementsPanelWidget& panel, const char* objectName)
{
    const QLabel* label = const_cast<RequirementsPanelWidget&>(panel)
                              .findChild<QLabel*>(QString::fromLatin1(objectName));
    return label != nullptr ? label->text() : QString();
}

TEST_F(RequirementsSessionGuiTest, LifecycleAdd_ThreeCollections_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 工位新增：树投影出现自动名新条目＋选中锚落新条目（页签状态行呈现）。
    const std::size_t pointsBefore = m_editor.workingSet().points.entries.size();
    ASSERT_NE(lifecycleButton(*m_panel, "add", "points"), nullptr);
    lifecycleButton(*m_panel, "add", "points")->click();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), pointsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_station_header")
                    .contains(QStringLiteral("工位")))
        << "新增后选中锚未落新条目（页签状态行应显示自动名）";

    // 区域新增＋工况新增（同轨断言——集合规模＋状态行）。
    const std::size_t regionsBefore = m_editor.workingSet().regions.entries.size();
    lifecycleButton(*m_panel, "add", "regions")->click();
    ASSERT_EQ(m_editor.workingSet().regions.entries.size(), regionsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_region_header")
                    .contains(QStringLiteral("区域")));

    const std::size_t conditionsBefore =
        m_editor.workingSet().conditions.entries.size();
    lifecycleButton(*m_panel, "add", "conditions")->click();
    ASSERT_EQ(m_editor.workingSet().conditions.entries.size(), conditionsBefore + 1);
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_condition_header")
                    .contains(QStringLiteral("工况")));

    // 自动名防撞（I-REQ-3 前置）：连续新增两个工位——名字互异。
    lifecycleButton(*m_panel, "add", "points")->click();
    const auto& entries = m_editor.workingSet().points.entries;
    std::set<std::string> names;
    for (const auto& e : entries) { names.insert(e.name); }
    EXPECT_EQ(names.size(), entries.size()) << "自动名撞名（uniqueEntryName 失守）";
}

TEST_F(RequirementsSessionGuiTest, LifecycleDuplicate_CopyFieldsAndSelect_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中 P1→复制：规模＋1＋新名『P1 副本』＋选中锚落副本＋字段等值
    //（位置值与源一致——深拷贝；溯源字段保持源值——非伪造模板物）。
    const core::ObjectId srcId = m_editor.workingSet().points.entries.front().objectId;
    const double srcZ =
        m_editor.workingSet().points.entries.front().pose.position.value()[2];
    m_panel->focusObject(srcId);
    const std::size_t before = m_editor.workingSet().points.entries.size();
    lifecycleButton(*m_panel, "duplicate", "points")->click();
    ASSERT_EQ(m_editor.workingSet().points.entries.size(), before + 1);

    const TaskPoint* copy = nullptr;
    for (const TaskPoint& e : m_editor.workingSet().points.entries) {
        if (e.objectId != srcId && e.name == "P1 副本") { copy = &e; }
    }
    ASSERT_NE(copy, nullptr) << "副本条目未按『源名 副本』命名";
    EXPECT_DOUBLE_EQ(copy->pose.position.value()[2], srcZ)
        << "副本字段与源不等值";
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_station_header")
                    .contains(QStringLiteral("P1 副本")))
        << "复制后选中锚未落副本";
}

TEST_F(RequirementsSessionGuiTest, LifecycleRemoveAnchorFallbackAndUndo_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 删除唯一区域条目：集合空＋锚清空（状态行回落『未选择对象』）。
    const core::ObjectId regionId =
        m_editor.workingSet().regions.entries.front().objectId;
    m_panel->focusObject(regionId);
    lifecycleButton(*m_panel, "remove", "regions")->click();
    EXPECT_TRUE(m_editor.workingSet().regions.entries.empty())
        << "删除未生效（集合应空）";
    EXPECT_TRUE(tabHeaderText(*m_panel, "ird_req_tab_region_header")
                    .contains(QStringLiteral("未选择对象")))
        << "空集合锚回落未清空";

    // 撤销（域轨 undoLocal——面板结构操作与字段编辑同栈）→ 区域恢复。
    EXPECT_TRUE(m_editor.undoLocal());
    ASSERT_EQ(m_editor.workingSet().regions.entries.size(), std::size_t{1});
    // 重做→再删除。
    EXPECT_TRUE(m_editor.redoLocal());
    EXPECT_TRUE(m_editor.workingSet().regions.entries.empty());

    // 工况锚回落（非空集合）：选中唯一工况删除→集合空清空；再 undo 恢复
    // ＋新增第二个工况后删除首个→锚回落到次条（同位次钳制）。
    const core::ObjectId condId =
        m_editor.workingSet().conditions.entries.front().objectId;
    m_panel->focusObject(condId);
    lifecycleButton(*m_panel, "remove", "conditions")->click();
    EXPECT_TRUE(m_editor.workingSet().conditions.entries.empty());
    EXPECT_TRUE(m_editor.undoLocal());  // 工况恢复（撤销栈跨集合序贯）
    lifecycleButton(*m_panel, "add", "conditions")->click();
    ASSERT_EQ(m_editor.workingSet().conditions.entries.size(), std::size_t{2});
    const core::ObjectId firstId =
        m_editor.workingSet().conditions.entries.front().objectId;
    m_panel->focusObject(firstId);
    lifecycleButton(*m_panel, "remove", "conditions")->click();
    EXPECT_FALSE(tabHeaderText(*m_panel, "ird_req_tab_condition_header")
                     .contains(QStringLiteral("未选择对象")))
        << "非空集合删除后锚被清空（应回落到同位次条目）";
}

TEST_F(RequirementsSessionGuiTest, LifecycleButtons_DisabledWithoutSession_UI_T30)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 独立面板（无编辑目标提供器——无会话形态）：refreshPanel 后生命周期
    // 三键全禁用（不虚构可编辑）。
    RequirementsPanelWidget noSessionPanel(true);
    noSessionPanel.refreshPanel(RequirementWorkingSet{},
                                RequirementReadinessReport{});
    for (const char* key : {"points", "regions", "conditions"}) {
        for (const char* action : {"add", "duplicate", "remove"}) {
            QPushButton* btn =
                lifecycleButton(noSessionPanel, action, key);
            ASSERT_NE(btn, nullptr);
            EXPECT_FALSE(btn->isEnabled())
                << "无会话下生命周期按钮可用（" << action << "/" << key << "）";
        }
    }
}

// =====================================================================
// UI-T31 B2 区域/工况字段编辑四态（接受/拒绝·解析面/回退/未修改不脏化）
// ＋未设态设值＋只读门控。触发面＝行编辑器 setText＋editingFinished 直发
//（与失焦同信号路径）；断言面＝工作集权威值＋编辑器回退＋编辑计数。
// =====================================================================

/// 按 objectName 锚定位面板内表格（区域表/工况表——B2 构建期锚）。
QTreeWidget* tableByName(const RequirementsPanelWidget& panel, const char* objectName)
{
    return const_cast<RequirementsPanelWidget&>(panel).findChild<QTreeWidget*>(
        QString::fromLatin1(objectName));
}

/// 按 irdFieldKey 属性定位检查器行编辑器（区域/工况表单共用形态）。
QLineEdit* fieldEditor(const RequirementsPanelWidget& panel, const char* fieldKey)
{
    const auto edits =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QLineEdit*>();
    for (QLineEdit* e : edits) {
        if (e->property("irdFieldKey").toString() == QString::fromLatin1(fieldKey)) {
            return e;
        }
    }
    return nullptr;
}

/// 面板状态行文本（就地错误/接受摘要的呈现面）。
QString statusText(const RequirementsPanelWidget& panel)
{
    const auto labels =
        const_cast<RequirementsPanelWidget&>(panel).findChildren<QLabel*>();
    for (QLabel* l : labels) {
        if (l->wordWrap()) { return l->text(); }  // 状态行＝唯一 wrap 行
    }
    return QString();
}

TEST_F(RequirementsSessionGuiTest, RegionFieldEditing_FourStates_UI_T31)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中区域表首行→检查器投影（B2 可编辑行构建）。
    QTreeWidget* regionTable = tableByName(*m_panel, "ird_req_region_table");
    ASSERT_NE(regionTable, nullptr);
    ASSERT_GT(regionTable->topLevelItemCount(), 0);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    const core::ObjectId rid = m_editor.workingSet().regions.entries.front().objectId;
    const double sizeXBefore = m_editor.workingSet().regions.entries.front().box.size[0];
    const std::uint64_t editsBefore = m_editor.draftStatus().edits;

    // ① 未修改短路：直发 editingFinished（文本未变）——零提交（编辑计数
    // 不变＝幻影脏化消除的四态之四）。
    QLineEdit* sizeEdit = fieldEditor(*m_panel, "box-size-x");
    ASSERT_NE(sizeEdit, nullptr);
    ASSERT_FALSE(sizeEdit->isReadOnly()) << "B2 未解除数值行只读（诚实降级未消除）";
    Q_EMIT sizeEdit->editingFinished();
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore)
        << "未修改触发产生了编辑（幻影脏化回归）";

    // ② 接受：尺寸 1.0→2.5——工作集权威值变＋编辑计数＋1。
    sizeEdit->setText(QStringLiteral("2.5"));
    Q_EMIT sizeEdit->editingFinished();
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], 2.5) << "接受后权威值未变";
        }
    }
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore + 1);

    // ③ 拒绝·解析面：非数值输入——就地错误＋权威回退（编辑器文本回滚）。
    QLineEdit* centerEdit = fieldEditor(*m_panel, "box-center-x");
    ASSERT_NE(centerEdit, nullptr);
    const QString authoritativeBefore = centerEdit->property("irdAuthoritativeValue").toString();
    centerEdit->setText(QStringLiteral("abc"));
    Q_EMIT centerEdit->editingFinished();
    EXPECT_EQ(m_editor.draftStatus().edits, editsBefore + 1) << "解析拒绝产生了提交";
    QLineEdit* centerAfter = fieldEditor(*m_panel, "box-center-x");
    ASSERT_NE(centerAfter, nullptr);
    EXPECT_EQ(centerAfter->text(), authoritativeBefore)
        << "拒绝后编辑器未回退权威值";
    EXPECT_FALSE(statusText(*m_panel).isEmpty()) << "就地错误未呈现";

    // ④ 拒绝·范围面：尺寸负值（bounds 正数）——同解析轨拒绝。
    QLineEdit* sizeEdit2 = fieldEditor(*m_panel, "box-size-x");
    sizeEdit2->setText(QStringLiteral("-3"));
    Q_EMIT sizeEdit2->editingFinished();
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], 2.5) << "范围外输入被写入（bounds 失守）";
        }
    }

    // ⑤ 撤销同栈：undoLocal 回滚 2.5→1.0（B1 结构/B2 字段同一局部栈）。
    EXPECT_TRUE(m_editor.undoLocal());
    for (const WorkRegion& r : m_editor.workingSet().regions.entries) {
        if (r.objectId == rid) {
            EXPECT_DOUBLE_EQ(r.box.size[0], sizeXBefore);
        }
    }
}

TEST_F(RequirementsSessionGuiTest, ConditionCycleTime_SetFromUnset_UI_T31)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    // 选中工况表首行→检查器投影；未设节拍行（空文本权威基准）输入即设值。
    QTreeWidget* condTable = tableByName(*m_panel, "ird_req_condition_table");
    ASSERT_NE(condTable, nullptr);
    ASSERT_GT(condTable->topLevelItemCount(), 0);
    condTable->setCurrentItem(condTable->topLevelItem(0));
    m_panel->refreshPanel(m_editor.workingSet(), m_report);

    const core::ObjectId cid = m_editor.workingSet().conditions.entries.front().objectId;
    ASSERT_FALSE(m_editor.workingSet().conditions.entries.front().targetCycleTimeS
                     .has_value())
        << "夹具工况应无节拍（未设态设值路径的前提）";
    QLineEdit* cycleEdit = fieldEditor(*m_panel, "cycle-time");
    ASSERT_NE(cycleEdit, nullptr);
    ASSERT_FALSE(cycleEdit->isReadOnly());

    cycleEdit->setText(QStringLiteral("12.5"));
    Q_EMIT cycleEdit->editingFinished();
    for (const OperatingCondition& c : m_editor.workingSet().conditions.entries) {
        if (c.objectId == cid) {
            ASSERT_TRUE(c.targetCycleTimeS.has_value());
            EXPECT_DOUBLE_EQ(*c.targetCycleTimeS, 12.5) << "未设→设值未生效";
        }
    }
}

TEST_F(RequirementsSessionGuiTest, FieldEditing_ReadOnlyGate_UI_T31)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 只读门控（L-R12 行半区）：setWritable(false)→刷新后区域/工况数值
    // 行全部只读（B2 门控联动——浏览不受影响）。
    m_panel->refreshPanel(m_editor.workingSet(), m_report);  // 首建表行
    QTreeWidget* regionTable = tableByName(*m_panel, "ird_req_region_table");
    ASSERT_NE(regionTable, nullptr);
    QTreeWidget* condTable = tableByName(*m_panel, "ird_req_condition_table");
    ASSERT_NE(condTable, nullptr);
    regionTable->setCurrentItem(regionTable->topLevelItem(0));
    condTable->setCurrentItem(condTable->topLevelItem(0));
    m_panel->setWritable(false);
    m_panel->refreshPanel(m_editor.workingSet(), m_report);
    for (const char* key : {"box-size-x", "box-center-y", "coverage-position",
                            "cycle-time"}) {
        QLineEdit* e = fieldEditor(*m_panel, key);
        ASSERT_NE(e, nullptr) << key;
        EXPECT_TRUE(e->isReadOnly()) << key << " 只读态未生效";
    }
    m_panel->setWritable(true);  // 夹具复位
}
