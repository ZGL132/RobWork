/**
 * @file   PluginPanelTest.cpp
 * @brief  需求插件面板呈现模型测试（模型层——QCoreApplication 级）——
 *         任务契约 WP-14-T08 acceptance 1~5 的具名自证面。
 *
 * 设计依据：
 *   - units/requirements.md §9.8（四面板组成与挂接——ACC1；编辑→草稿→
 *     应用链路 L-R2/L-R3——ACC2；插件零计算逻辑——ACC3；两级撤销与辅助
 *     链路 L-R4/L-R10/L-R11/L-R12——ACC4；域命令登记清单——ACC5）、
 *     §10.2（V-22 GUI 用例仅登记流程——本目标不启动 GUI）
 *   - 需求 UX-05（需求界面）、REQ-06（预览/正式分离）、REQ-08/10（确认
 *     门/捕获）、REQ-11（批量撤销）、REQ-12（副本导出）、AT-02/AT-23/
 *     AT-24、SA-16（快捷键不私占）
 *   - 先例：modeling/test/PluginPanelTest.cpp（WP-13-T15 同款形态——模型
 *     层测试消费插件呈现模型面，GUI 呈现登记为 V 用例不启动）
 *
 * 测试范围声明（V-22）：GUI 呈现流程（工位表批量粘贴/姿态规则切换/拾取
 * 确认/导入向导页面栈）按卡 §10.2 V-22 行"设计登记"承载——执行规程登记
 * 于 traceability/builds/wp14-t08/v22-gui-flow-registration.md（AGENTS
 * Windows 规程 QT_QPA_PLATFORM=windows 逐个绝对路径启动），本目标不启动
 * GUI、不记载为通过。
 */

#include <gtest/gtest.h>

#include <QKeySequence>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/core/Units.hpp>                    // core::UnitToken（QuantityFieldSpec 装配核对）
#include <sdurws/ird/requirements/Codec.hpp>            // RequirementCodec（草稿载荷解码核对）
#include <sdurws/ird/requirements/CommandHandlers.hpp>  // kCmdApply* token/tryDecode（信封载荷域解释）
#include <sdurws/ird/requirements/Editor.hpp>           // RequirementEditor（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>      // 五对象 token
#include <sdurws/ird/requirements/Readiness.hpp>        // RequirementReadinessChecker（就绪报告产出）
#include <sdurws/ird/requirements/RequirementTypes.hpp> // 值模型
#include <sdurws/ird/requirements/Services.hpp>         // 领域服务（规范化/必验解析）
#include "plugin/PanelCommandCatalog.hpp"  // 命令目录/装配记录/就绪投影/L-R12（插件私有头——经单元根解析）
#include "plugin/PanelConditionModel.hpp"  // 工况面板投影（ACC1）
#include "plugin/PanelEditFlow.hpp"        // L-R2/L-R9 编辑流＋两级撤销（ACC2/4）
#include "plugin/PanelRegionModel.hpp"     // 区域面板投影（ACC1）
#include "plugin/PanelStationModel.hpp"    // 工位面板投影（ACC1/2）
#include "plugin/PanelTreeModel.hpp"       // 对象树投影（ACC1）
#include "plugin/PanelValidationModel.hpp" // 校验面板投影（ACC1）
#include "plugin/ImportWizardFlow.hpp"     // L-R10 向导流＋L-R11 导出提示（ACC4）
#include "plugin/RequirementsUiModule.hpp" // §11.2 模块＋草稿源（ACC2/4——P-REQ-4）
#include <sdurws/ird/ui/ICommandRegistry.hpp>         // createCommandRegistry（ACC5 注册面）
#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>  // createGlobalShortcutRegistry（ACC5 无冲突登记）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

/// core 命名空间别名（测试内 ObjectId/SourcedValue 直写面）。
namespace core = sdurws::ird::core;
/// project 命名空间别名（信封/载荷类型）。
namespace project = sdurws::ird::project;
/// io 命名空间别名（RawTable——导入向导夹具）。
namespace io = sdurws::ird::io;
/// ui 命名空间别名（命令/快捷键注册面）。
namespace ui = sdurws::ird::ui;

namespace {

// =====================================================================
// 夹具（EditorTest 同款形态——内存闭包＋合法条目工厂）
// =====================================================================

/// 合法任务点（I-REQ-5：至少一约束分量；work 段恒启用）。
TaskPoint makePoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 测试用闭包字节源：按 token/id 注册的内存映射（EditorTest 同款）。
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

/// 装配"根＋四集合"基线闭包（集合 id 固定——可复现；EditorTest 同款）。
core::ObjectId fillBaseline(MapClosure& closure, PointSet points, RegionSet regions,
                            ConditionSet conditions, PlanSet plans)
{
    // 条目先按 ObjectId 字典序规范化（I-REQ-1 解码门槛——随机身份必须
    // 排序后方可入闭包，否则 loadBaseline 解码链拒绝）。
    sortEntriesByObjectId(points.entries);
    sortEntriesByObjectId(regions.entries);
    sortEntriesByObjectId(conditions.entries);
    sortEntriesByObjectId(plans.entries);
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
                    RequirementObjectVariant{regions});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType},
                    RequirementObjectVariant{conditions});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType},
                    RequirementObjectVariant{plans});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");  // 根 oid（固定）
}

/// 载入基线的编辑器（成功前置起步夹具——失败即测试自身装配错误；
/// 异常携域错误详情便于定位夹具装配问题）。
RequirementEditor makeLoadedEditor(MapClosure& closure)
{
    RequirementEditor editor;
    const auto load = editor.loadBaseline(closure);
    if (!load.ok) {
        std::string params;
        for (const auto& kv : load.error.params) {
            params += " " + kv.first + "=" + kv.second;
        }
        throw std::logic_error("夹具装配错误：基线载入失败 code="
                               + std::string(requirementErrorCodeToken(load.error.code))
                               + params + " detail=" + load.error.detail);
    }
    return editor;
}

/// 含一条任务点的载入编辑器（编辑流/检查器用例的起步态）。
RequirementEditor makeEditorWithOnePoint(TaskPoint* outPoint)
{
    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor = makeLoadedEditor(closure);
    if (outPoint != nullptr) {
        *outPoint = editor.workingSet().points.entries.front();
    }
    return editor;
}

/// 编辑流记录替身（sink 三路回调＋批次警告的接收记录）。
class RecordingSink final : public IRequirementEditSink {
public:
    void onEditApplied(const std::string& changeSummary) override
    {
        appliedSummaries.push_back(changeSummary);
    }
    void notifySessionDirty() override { dirtyNotifications += 1; }
    void onEditRejected(const EditRejection& rejection) override
    {
        rejections.push_back(rejection);
    }
    void onBatchWarning(const core::DiagnosticRecord& warning) override
    {
        warnings.push_back(warning);
    }

    std::vector<std::string> appliedSummaries;   ///< 接受回调（摘要序）
    int dirtyNotifications = 0;                  ///< 脏通知计数
    std::vector<EditRejection> rejections;       ///< 拒绝记录（就地错误明细）
    std::vector<core::DiagnosticRecord> warnings;///< 批次警告记录
};

/// CSV 表夹具（ImportTest 同款形态——表头＋数据行）。
io::RawTable makeTable(const std::vector<std::vector<std::string>>& rows)
{
    io::RawTable table;
    if (!rows.empty()) {
        table.report.hasHeader = true;
        table.report.header.assign(rows.front().begin(), rows.front().end());
    }
    for (std::size_t i = 1; i < rows.size(); ++i) {
        table.rows.emplace_back(rows[i].begin(), rows[i].end());
    }
    table.report.dataRows = table.rows.size();
    return table;
}

/// 合法工作区域（I-REQ-6 非退化盒＋Grid 采样）。
WorkRegion makeRegion(const std::string& name)
{
    WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = name;
    r.box = BoundingBox{rw::math::Vector3D<double>(1.0, 2.0, 3.0),
                        rw::math::Vector3D<double>(2.0, 2.0, 2.0)};
    r.positionSampling =
        PositionSampling{PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
    return r;
}

/// 合法工况（level/enabled 由调用方调整——必验派生面）。
OperatingCondition makeCondition(const std::string& name, RequirementLevel level, bool enabled)
{
    OperatingCondition c;
    c.objectId = core::ObjectId::generate();
    c.name = name;
    c.level = level;
    c.enabled = enabled;
    return c;
}

}  // namespace

// =====================================================================
// ACC1——四面板落位（对象树/工位/区域/工况/校验＋§10.9 装配描述符）
// =====================================================================

/// 对象树投影：根→四分组→条目、节点锚＝ObjectId、显示 name（卡 §9.8 第 1 行）。
TEST(PluginPanel, RequirementTreeBuildsRootFourGroupsAndEntries_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05"},
                  std::vector<std::string>{"ACC1-tree", "L-R1"});
    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("拾取点A"));
    points.entries.push_back(makePoint("放置点B"));
    RegionSet regions;
    regions.entries.push_back(makeRegion("作业区"));
    ConditionSet conditions;
    conditions.entries.push_back(makeCondition("搬运工况", RequirementLevel::Must, true));
    PlanSet plans;
    SamplingPlan plan;                 // 条目 id 须有效（I-REQ-2——全零保留值入闭包即拒绝）
    plan.objectId = core::ObjectId::generate();
    plan.regionRef = core::ObjectId::generate();  // 计划-区域引用须有效（codec entry-invariant）
    plans.entries.push_back(plan);
    fillBaseline(closure, points, regions, conditions, plans);
    RequirementEditor editor = makeLoadedEditor(closure);
    const RequirementWorkingSet& ws = editor.workingSet();

    const std::vector<RequirementNode> nodes = buildRequirementTree(ws);

    // 行数＝1 根＋4 分组＋2 点＋1 区域＋1 工况＋1 计划。
    ASSERT_EQ(nodes.size(), std::size_t(10));
    EXPECT_EQ(nodes[0].kind, RequirementNodeKind::RequirementRoot);
    EXPECT_FALSE(nodes[0].objectId.has_value());  // 根节点无条目锚（闭包锚是集合级身份）
    // 分组行序＝卡面固定序（工位→区域→工况→计划），锚为空、计数正确。
    EXPECT_EQ(nodes[1].kind, RequirementNodeKind::PointsGroup);
    EXPECT_EQ(nodes[1].displayLabel, "工位（2）");
    EXPECT_EQ(nodes[4].kind, RequirementNodeKind::RegionsGroup);
    EXPECT_EQ(nodes[4].entryCount, std::size_t(1));
    EXPECT_EQ(nodes[6].kind, RequirementNodeKind::ConditionsGroup);
    EXPECT_EQ(nodes[8].kind, RequirementNodeKind::PlansGroup);
    // 条目行锚＝条目 ObjectId、显示＝name（工程用语——UX-02）。行序＝
    // 工作集规范序（I-REQ-1——ObjectId 字典序；身份随机生成，插入序无
    // 断言意义）——按 name 定位行后再锚定比对。
    EXPECT_EQ(nodes[2].kind, RequirementNodeKind::Point);
    const auto nodeByName = [&nodes](const std::string& label) -> const RequirementNode* {
        for (const RequirementNode& n : nodes) {
            if (n.kind == RequirementNodeKind::Point && n.displayLabel == label) {
                return &n;
            }
        }
        return nullptr;
    };
    const RequirementNode* nodeA = nodeByName("拾取点A");
    const RequirementNode* nodeB = nodeByName("放置点B");
    ASSERT_NE(nodeA, nullptr);
    ASSERT_NE(nodeB, nullptr);
    const TaskPoint* wsA = nullptr;
    const TaskPoint* wsB = nullptr;
    for (const TaskPoint& p : ws.points.entries) {
        if (p.name == "拾取点A") { wsA = &p; }
        if (p.name == "放置点B") { wsB = &p; }
    }
    ASSERT_NE(wsA, nullptr);
    ASSERT_NE(wsB, nullptr);
    ASSERT_TRUE(nodeA->objectId.has_value());
    EXPECT_EQ(nodeA->objectId.value(), wsA->objectId);
    EXPECT_EQ(nodeB->objectId.value(), wsB->objectId);
    // 反查定位（L-R1 反向跳转的行定位面）：锚→行下标→行名一致性。
    const auto idx = treeRowIndexFor(nodes, wsB->objectId);
    ASSERT_TRUE(idx.has_value());
    EXPECT_EQ(nodes[idx.value()].displayLabel, "放置点B");
    EXPECT_EQ(nodes[idx.value()].objectId.value(), wsB->objectId);
    EXPECT_FALSE(treeRowIndexFor(nodes, core::ObjectId{}).has_value());  // 全零不定位

    // UX-02 守卫：哈希形态名 fail-fast（非法名不进树）。
    PointSet bad = points;
    bad.entries[0].name = std::string(64, 'a');
    MapClosure badClosure;
    fillBaseline(badClosure, bad, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor badEditor = makeLoadedEditor(badClosure);
    EXPECT_THROW(buildRequirementTree(badEditor.workingSet()), std::invalid_argument);
}

/// 工位检查器投影：位姿分量约束/容差/三段/等级启用＋来源徽标六态（面板表第 2 行）。
TEST(PluginPanel, StationInspectorRowsAndSourceBadges_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "REQ-01", "REQ-02"},
                  std::vector<std::string>{"ACC1-station", "badge"});
    // 合法任务点（直接构造——不用 ASSERT_* 包裹含裸逗号的表达式，避免
    // 宏参数在花括号初始化列表处被劈开）。
    TaskPoint point;
    point.objectId = core::ObjectId::generate();
    point.name = "P1";
    point.pose.constrainedDof.z = true;
    point.pose.constrainedDof.roll = true;
    point.tolerance.positionTolerance = 5.0e-3;  // m
    point.tolerance.orientationTolerance = 2.0 * 3.14159265358979323846 / 180.0;  // rad
    point.approach.enabled = true;
    point.approach.axis = SegmentAxis::ToolZ;
    point.approach.distanceM = 0.1;  // m
    point.work.enabled = true;       // work 段恒启用（§4.3）
    point.work.axis = SegmentAxis::ToolZ;
    point.work.distanceM = 1.0;  // m
    point.retract.enabled = true;
    point.retract.axis = SegmentAxis::ReferenceZ;
    point.retract.distanceM = 0.2;  // m

    const std::vector<StationFieldRow> rows = stationFieldsFor(point, true);
    // 键词表抽查：六分量约束＋容差两行＋三段三行＋等级/启用行齐备。
    auto findRow = [&rows](const std::string& key) -> const StationFieldRow* {
        for (const StationFieldRow& r : rows) {
            if (r.fieldKey == key) {
                return &r;
            }
        }
        return nullptr;
    };
    ASSERT_NE(findRow("dof-z"), nullptr);
    EXPECT_EQ(findRow("dof-z")->valueText, "受约束");
    ASSERT_NE(findRow("dof-x"), nullptr);
    EXPECT_EQ(findRow("dof-x")->valueText, "自由");
    ASSERT_NE(findRow("tolerance-position"), nullptr);
    EXPECT_EQ(findRow("tolerance-position")->unitText, "m");  // SI 词面直投——零换算
    ASSERT_NE(findRow("tolerance-orientation"), nullptr);
    EXPECT_EQ(findRow("tolerance-orientation")->unitText, "rad");
    ASSERT_NE(findRow("segment-approach"), nullptr);
    EXPECT_NE(findRow("segment-approach")->valueText.find("ToolZ"), std::string::npos);
    ASSERT_NE(findRow("segment-work"), nullptr);
    EXPECT_EQ(findRow("segment-work")->valueText, "启用");  // work 段恒启用（§4.3）
    ASSERT_NE(findRow("level"), nullptr);
    EXPECT_EQ(findRow("level")->valueText, "Must");

    // 来源徽标六态（映射即呈现约定——判序见 sourceBadgeFor）。
    EXPECT_EQ(sourceBadgeFor(point), "手工");
    {
        TaskPoint captured = point;
        captured.source = core::ValueProvenance::make(
            core::ProvenanceKind::UserProvided, std::nullopt, std::nullopt,
            std::string("captured-tcp"));
        EXPECT_EQ(sourceBadgeFor(captured), "捕获");  // L-R7 methodTag 自证
    }
    {
        TaskPoint mirrored = point;
        mirrored.generation = GenerationProvenance{};
        mirrored.generation.value().generatorId = "mirror";
        EXPECT_EQ(sourceBadgeFor(mirrored), "镜像");
    }
    {
        TaskPoint templated = point;
        templated.generation = GenerationProvenance{};
        templated.generation.value().generatorId = "template:bin-picking";
        EXPECT_EQ(sourceBadgeFor(templated), "模板");
    }
    {
        TaskPoint imported = point;
        imported.importProvenance = ImportProvenance{};
        EXPECT_EQ(sourceBadgeFor(imported), "导入");  // 判序①：导入优先
    }
}

/// 五规则联动表单：按 kind 显隐参数（面板表第 2 行"五规则联动表单"）。
TEST(PluginPanel, StationFormKindDrivesParamVisibility_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09", "UX-05"},
                  std::vector<std::string>{"ACC1-rule-form", "L-R8"});
    // 显隐表即数据：五规则各得各自参数键（§5.3 载荷字段对照）。
    EXPECT_EQ(orientationRuleParamKeys(OrientationRuleKind::Fixed),
              (std::vector<std::string>{"fixed-rpy-r", "fixed-rpy-p", "fixed-rpy-y"}));
    EXPECT_EQ(orientationRuleParamKeys(OrientationRuleKind::AlignFrame),
              (std::vector<std::string>{"target-frame"}));
    EXPECT_EQ(orientationRuleParamKeys(OrientationRuleKind::AlignGeometryNormal),
              (std::vector<std::string>{"target-scene", "feature", "invert-normal"}));
    EXPECT_EQ(orientationRuleParamKeys(OrientationRuleKind::PointAtTarget),
              (std::vector<std::string>{"target-point-x", "target-point-y", "target-point-z"}));
    EXPECT_EQ(orientationRuleParamKeys(OrientationRuleKind::ToolRollFree),
              (std::vector<std::string>{"roll-min", "roll-max"}));

    // 检查器行随 kind 联动：Fixed 条目出欧拉角行、不出目标点行。
    TaskPoint fixed;
    fixed.objectId = core::ObjectId::generate();
    fixed.name = "F";
    fixed.pose.constrainedDof.z = true;
    fixed.pose.orientation.kind = OrientationRuleKind::Fixed;
    const auto fixedRows = stationFieldsFor(fixed, true);
    bool hasRoll = false;
    bool hasTargetPoint = false;
    for (const StationFieldRow& r : fixedRows) {
        hasRoll = hasRoll || r.fieldKey == "fixed-rpy-r";
        hasTargetPoint = hasTargetPoint || r.fieldKey == "target-point-x";
    }
    EXPECT_TRUE(hasRoll);
    EXPECT_FALSE(hasTargetPoint);

    // PointAtTarget 条目出目标点行、不出欧拉角行。
    TaskPoint pointAt = fixed;
    pointAt.pose.orientation.kind = OrientationRuleKind::PointAtTarget;
    pointAt.pose.orientation.targetPoint = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    const auto paRows = stationFieldsFor(pointAt, true);
    hasRoll = false;
    hasTargetPoint = false;
    for (const StationFieldRow& r : paRows) {
        hasRoll = hasRoll || r.fieldKey == "fixed-rpy-r";
        hasTargetPoint = hasTargetPoint || r.fieldKey == "target-point-z";
    }
    EXPECT_FALSE(hasRoll);
    EXPECT_TRUE(hasTargetPoint);
}

/// 表单回填装配：ParamEditSet→TaskPoint 字段（acceptance 2 的值组装半区）。
TEST(PluginPanel, StationEditSetFillsOnlyTargetFields_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05"},
                  std::vector<std::string>{"ACC2-editset", "zero-compute"});
    TaskPoint base;
    base.objectId = core::ObjectId::generate();
    base.name = "P1";
    base.pose.constrainedDof.z = true;
    base.tolerance = ToleranceSpec{1.0e-3, 3.14159265358979323846 / 180.0};

    // QuantityFieldSpec 装配面（WP-10-T08 公共件——SI 单位锚＋量纲一致性
    // 由 makeQuantityFieldSpec fail-fast 保证）。
    const auto specs = stationQuantitySpecs();
    ASSERT_EQ(specs.size(), std::size_t(12));
    bool foundTolerance = false;
    for (const ui::QuantityFieldSpec& s : specs) {
        if (s.key == "tolerance-position") {
            foundTolerance = true;
            EXPECT_EQ(s.kind, core::QuantityKind::Length);
        }
    }
    EXPECT_TRUE(foundTolerance);

    // 回填：仅命中的字段变化（其余字段原样保留——数据搬运零判定）。
    ui::ParamEditSet edits;
    ui::ParamChange change;
    change.key = "tolerance-position";
    change.label = "位置容差";
    change.newSi = 2.0e-3;  // SI 真值（m）
    edits.changes.push_back(change);
    std::vector<std::string> known;
    const TaskPoint candidate = applyStationEditSet(base, edits, known);
    EXPECT_EQ(known, (std::vector<std::string>{"tolerance-position"}));
    EXPECT_DOUBLE_EQ(candidate.tolerance.positionTolerance, 2.0e-3);
    EXPECT_DOUBLE_EQ(candidate.tolerance.orientationTolerance,
                     base.tolerance.orientationTolerance);  // 未命中字段不变
    EXPECT_EQ(candidate.objectId, base.objectId);            // 身份/名称原样保留
    EXPECT_EQ(candidate.name, base.name);

    // 词表外键＝实现缺陷 fail-fast（不静默丢弃）。
    ui::ParamEditSet bad = edits;
    bad.changes.front().key = "no-such-key";
    EXPECT_THROW(applyStationEditSet(base, bad, known), std::invalid_argument);
}

/// 区域面板：计数/间距双模式采样＋覆盖率目标＋规范化经域函数（面板表第 3 行）。
TEST(PluginPanel, RegionPanelDualModeSamplingAndCoverage_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03"},
                  std::vector<std::string>{"ACC1-region", "D-REQ-2"});
    WorkRegionService service;

    // Grid 模式：计数即权威（规范化恒等）。
    WorkRegion grid = makeRegion("网格区");
    const auto gridView = samplingDefinitionView(grid, service);
    EXPECT_EQ(gridView.method, PositionSamplingMethod::Grid);
    EXPECT_EQ(gridView.normalizedCounts, (std::array<std::uint32_t, 3>{2, 2, 2}));
    EXPECT_FALSE(gridView.spacingChanged);

    // 间距模式：规范化经域函数现算（D-REQ-2：counts[i]=floor(size[i]/spacing[i])+1
    // ——size=2、spacing=0.5 → 5；插件零复制 floor 公式——NFR-MNT-04）。
    WorkRegion spacing = makeRegion("间距区");
    spacing.positionSampling =
        PositionSampling{PositionSamplingMethod::GridBySpacing, {0, 0, 0},
                         {0.5, 0.5, 0.5}, 0};
    const auto spacingView = samplingDefinitionView(spacing, service);
    EXPECT_EQ(spacingView.method, PositionSamplingMethod::GridBySpacing);
    EXPECT_EQ(spacingView.normalizedCounts, (std::array<std::uint32_t, 3>{5, 5, 5}));
    EXPECT_TRUE(spacingView.spacingChanged);
    EXPECT_NE(spacingView.normalizedText.find("规范化"), std::string::npos);

    // 非法间距：域拒绝→"规范化未定"占位（不伪造计数）。
    WorkRegion badSpacing = spacing;
    badSpacing.positionSampling.spacing = {0.0, 0.5, 0.5};
    const auto badView = samplingDefinitionView(badSpacing, service);
    EXPECT_NE(badView.normalizedText.find("未定"), std::string::npos);

    // 区域表行（Grid/间距摘要＋覆盖率目标摘要）。
    const auto rows = regionRows({grid, spacing});
    ASSERT_EQ(rows.size(), std::size_t(2));
    EXPECT_EQ(rows[0].samplingText, "Grid 2×2×2");
    EXPECT_NE(rows[1].samplingText.find("间距"), std::string::npos);
    EXPECT_NE(rows[0].coverageText.find("P≥0.8"), std::string::npos);  // 设计默认直投
    EXPECT_EQ(rows[0].level, "Must");

    // 检查器行＋L-R12 只读门控（writable=false→可编辑行灰显）。
    const auto editable = regionFieldsFor(grid, service, true);
    const auto readOnly = regionFieldsFor(grid, service, false);
    ASSERT_FALSE(editable.empty());
    ASSERT_EQ(editable.size(), readOnly.size());
    bool sawEditable = false;
    for (std::size_t i = 0; i < editable.size(); ++i) {
        if (editable[i].enablement == StationFieldEnablement::Editable) {
            sawEditable = true;
            EXPECT_EQ(readOnly[i].enablement, StationFieldEnablement::ReadOnlyGrey);
        }
    }
    EXPECT_TRUE(sawEditable);
}

/// 区域预览几何：盒角点＋采样格线（仅几何预览——结果着色归 KIN-07 不实现）。
TEST(PluginPanel, RegionPreviewGeometryOutlineAndGrid_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-07"},
                  std::vector<std::string>{"ACC1-preview", "no-result-coloring"});
    WorkRegionService service;
    WorkRegion region = makeRegion("预览区");
    region.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                             rw::math::Vector3D<double>(2.0, 2.0, 2.0)};
    region.positionSampling =
        PositionSampling{PositionSamplingMethod::Grid, {3, 2, 1}, {0, 0, 0}, 0};

    const RegionPreviewGeometry geo = regionPreviewGeometry(region, service);
    // 角点恒 8（盒轮廓）；索引序＝(x,y,z) 二进制序。
    EXPECT_EQ(geo.corners.size(), std::size_t(8));
    EXPECT_DOUBLE_EQ(geo.corners[7][0], 1.0);  // +sx 端
    EXPECT_DOUBLE_EQ(geo.corners[7][1], 1.0);
    EXPECT_DOUBLE_EQ(geo.corners[7][2], 1.0);
    EXPECT_DOUBLE_EQ(geo.corners[0][0], -1.0);
    // 格线数＝(counts[0]+1)+(counts[1]+1)+(counts[2]+1)＝4+3+2=9。
    EXPECT_EQ(geo.gridLines.size(), std::size_t(9));
    // 零计数轴不产线（零样本区域仅呈现轮廓——V-02 口径）。
    WorkRegion zeroAxis = region;
    zeroAxis.positionSampling =
        PositionSampling{PositionSamplingMethod::Grid, {0, 2, 2}, {0, 0, 0}, 0};
    const auto zeroGeo = regionPreviewGeometry(zeroAxis, service);
    EXPECT_EQ(zeroGeo.gridLines.size(), std::size_t(6));  // 0+3+3（x 轴零计数→无格线，y/z 各 3 条）
    // 摘要文本确定性（同输入同串）。
    EXPECT_EQ(geo.summaryText, regionPreviewGeometry(region, service).summaryText);
    EXPECT_NE(geo.summaryText.find("盒 2×2×2"), std::string::npos);
}

/// 工况面板：负载/事件/节拍/适用范围投影＋必验清单预览（面板表第 4 行）。
TEST(PluginPanel, ConditionPanelRowsAndMustListPreview_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "P-EV-9"},
                  std::vector<std::string>{"ACC1-condition", "I-REQ-9"});
    OperatingConditionService service;
    OperatingCondition cond = makeCondition("搬运", RequirementLevel::Must, true);
    cond.payloads.push_back(ConditionPayload{});  // 全 NotProvided 负载（缺失≠零）
    cond.targetCycleTimeS = 12.5;

    // 表行（节拍/适用范围摘要）。
    const auto rows = conditionRows({cond});
    ASSERT_EQ(rows.size(), std::size_t(1));
    EXPECT_NE(rows[0].cycleText.find("12.5"), std::string::npos);
    EXPECT_EQ(rows[0].appliesToText, "全部工位");

    // 检查器行：必验行＝域解析直投（I-REQ-9 派生——插件零复判）；
    // 未提供负载→"未提供"占位（不伪造 0）。
    const auto fields = conditionFieldsFor(cond, service, true);
    bool mandatoryRow = false;
    bool notProvidedRow = false;
    for (const StationFieldRow& r : fields) {
        mandatoryRow = mandatoryRow || (r.fieldKey == "mandatory" && r.valueText == "是");
        notProvidedRow = notProvidedRow || (r.fieldKey == "payload-1-mass"
                                            && r.valueText == "未提供");
    }
    EXPECT_TRUE(mandatoryRow);
    EXPECT_TRUE(notProvidedRow);

    // Should 未启用工况：必验行"否"（enabled∧Must——§6.2 冻结解析）。
    OperatingCondition should = makeCondition("辅助", RequirementLevel::Should, true);
    const auto shouldFields = conditionFieldsFor(should, service, true);
    bool shouldNotMandatory = false;
    for (const StationFieldRow& r : shouldFields) {
        shouldNotMandatory = shouldNotMandatory || (r.fieldKey == "mandatory"
                                                    && r.valueText == "否");
    }
    EXPECT_TRUE(shouldNotMandatory);

    // 必验清单预览（RequirementProfile 投影——P-EV-9 单点经域函数）。
    // requiredCases＝enabled∧Must 子集（§4.8"必验工况清单"行）——Should
    // 工况不入清单（恰好断言 I-REQ-9 派生唯一：清单≠全量投影）。
    const auto mustRows = mustListPreview({}, {}, {cond, should}, service);
    ASSERT_EQ(mustRows.size(), std::size_t(1));
    EXPECT_EQ(mustRows[0].label, "搬运");
    EXPECT_TRUE(mustRows[0].mandatory);
    EXPECT_TRUE(mustRows[0].enabled);
}

/// 校验面板：R0~R9 分层计数＋逐项定位跳转＋预览/正式语义说明（面板表第 5 行）。
TEST(PluginPanel, ValidationPanelLayersCountsJumpAndSemantics_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "UX-06"},
                  std::vector<std::string>{"ACC1-validation", "P-REQ-6-present-only"});
    // 真实报告（判定权威在校验器——本用例验证报告→行卡投影）：装配一条
    // 悬空 appliesTo 的启用 Should 工况（R4 Blocking）＋必验集合为空
    // （R5 Warning——Should 不入必验集），经 checker 产出报告。
    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor = makeLoadedEditor(closure);
    OperatingCondition dangling = makeCondition("悬空工况", RequirementLevel::Should, true);
    dangling.appliesTo = AppliesTo{AppliesToScope::Stations, {core::ObjectId::generate()}};
    ASSERT_TRUE(editor.applyEdit(dangling).accepted);

    // 闭包元数据与根引用表对齐（R0/R1 的浅校验数据面——ReadinessTest
    // makeHealthyContext 同款夹具；子条目锚不入 objectRefs——O-36）。
    CheckContext ctx;
    const auto addRef = [&ctx](const core::ObjectId& oid, std::string_view token) {
        project::ObjectRef ref;
        core::ContentVersion cv;
        cv.bytes[0] = static_cast<std::uint8_t>(ctx.closureRefs.size() + 1U);
        ref.objectId = oid;
        ref.contentVersion = cv;
        ref.objectTypeToken = std::string(token);
        ref.digest256 = std::string(64, '0');
        ctx.closureRefs.push_back(std::move(ref));
    };
    const RequirementWorkingSet& ws = editor.workingSet();
    addRef(ws.root.pointSetRef.value(), kReqPointSetObjectType);
    addRef(ws.root.regionSetRef.value(), kReqRegionSetObjectType);
    addRef(ws.root.conditionSetRef.value(), kReqConditionSetObjectType);
    addRef(ws.root.planSetRef.value(), kReqPlanSetObjectType);

    RequirementReadinessChecker checker;
    // 场景 A：悬空 appliesTo 的启用 Should 工况→R4 Blocking（短路优先——
    // R4 拦截后 R5 不执行，§8.1 层序语义）。
    const RequirementReadinessReport reportA = checker.check(ws, ctx);
    const ValidationPanelProjection proj = projectValidationPanel(reportA);
    // 层行恒 10（R0~R9 定序——§8.1 层序）。
    ASSERT_EQ(proj.layers.size(), std::size_t(10));
    EXPECT_EQ(proj.layers[0].layerToken, "R0");
    EXPECT_EQ(proj.layers[9].layerToken, "R9");
    EXPECT_EQ(proj.layers[0].title, "结构完整");
    EXPECT_GE(proj.layers[4].blocking, std::size_t(1));
    EXPECT_GE(proj.blockingCount, std::size_t(1));
    // 逐项行带稳定码与跳转锚（subject=条目 oid——UX-06 定位面）。
    bool sawBlockingWithJump = false;
    for (const ValidationItemRow& r : proj.items) {
        if (r.levelToken == "Blocking" && r.jumpTarget.has_value()) {
            sawBlockingWithJump = true;
            EXPECT_EQ(r.layerToken, "R4");
        }
    }
    EXPECT_TRUE(sawBlockingWithJump);

    // 场景 B：无悬空绑定且必验集合空（Should 工况 None 范围）→R5 Warning
    // （"无启用必验工况"——可应用级，R4 通过后 R5 正常执行）。
    RequirementEditor editorB = makeLoadedEditor(closure);
    OperatingCondition shouldNone =
        makeCondition("辅助工况", RequirementLevel::Should, true);
    shouldNone.appliesTo = AppliesTo{AppliesToScope::None, {}};
    ASSERT_TRUE(editorB.applyEdit(shouldNone).accepted);
    const RequirementReadinessReport reportB = checker.check(editorB.workingSet(), ctx);
    const ValidationPanelProjection projB = projectValidationPanel(reportB);
    EXPECT_GE(projB.layers[5].warning, std::size_t(1));
    EXPECT_GE(projB.warningCount, std::size_t(1));

    // 语义说明两行非空（REQ-06 预览/正式分离——固定文案直投）。
    EXPECT_FALSE(proj.previewNote.empty());
    EXPECT_FALSE(proj.formalNote.empty());
    // P-REQ-6 边界（结构性声明）：投影值面只有层/级别/码/摘要/跳转/计数
    // ——无任何"允许/禁止进入某阶段"的判定字段（ValidationPanelProjection
    // 结构即证明——字段面在编译期钉死，无门控动作语义）。
}

/// 装配描述符与九条域命令（§10.9 形状＋卡 §9.8 命令表逐行——ACC1/ACC5）。
TEST(PluginPanel, PanelRegistrationAndDomainCommandsMatchCard_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-05", "SA-16"},
                  std::vector<std::string>{"ACC5-commands", "ACC1-registration"});
    // 装配描述符（§10.9 形状承载——P-REQ-8 暂持；装配期一次注册的数据面）。
    const PanelRegistrationRecord rec = requirementsPanelRegistration();
    EXPECT_EQ(rec.pluginId, "requirements");
    EXPECT_EQ(rec.titleKey, ui::TextKey("stage.requirements.title"));
    EXPECT_EQ(rec.stage, ui::StageId::Requirements);
    EXPECT_TRUE(rec.capabilities.providesStagePanel);
    EXPECT_TRUE(rec.capabilities.providesReadonlyProjection);
    EXPECT_TRUE(rec.capabilities.registersCommands);
    EXPECT_FALSE(rec.advanced);

    // 九条命令逐行（id 与 readOnlyAllowed＝卡 §9.8 命令表权威——行序＝表行序）。
    const auto cmds = requirementsDomainCommands();
    ASSERT_EQ(cmds.size(), std::size_t(9));
    struct Expected {
        const char* id;
        bool readOnlyAllowed;
    };
    const std::vector<Expected> expected = {
        {"requirements.import-csv", false},     {"requirements.import-json", false},
        {"requirements.export-copy", true},     {"requirements.capture-tcp", true},
        {"requirements.pick-feature", true},    {"requirements.mirror-stations", false},
        {"requirements.create-array", false},   {"requirements.apply-template", false},
        {"requirements.regenerate-linked", false},
    };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(cmds[i].id, ui::CommandId(expected[i].id)) << "行序 " << i;
        EXPECT_EQ(cmds[i].readOnlyAllowed, expected[i].readOnlyAllowed) << expected[i].id;
        EXPECT_EQ(cmds[i].ownerUnit, "requirements");
        EXPECT_EQ(cmds[i].titleKey, ui::TextKey(std::string("cmd.") + expected[i].id + ".title"));
        EXPECT_EQ(cmds[i].scope, ui::CommandScope::Project);
        EXPECT_TRUE(cmds[i].bindable);
        // SA-16：默认零绑定（快捷键经 HotkeyBindingTable 用户级配置——与
        // 任何既有绑定构造性无冲突）。
        EXPECT_FALSE(cmds[i].defaultShortcut.has_value());
    }
    // 确定性：同调用同清单（NFR-COR-02；CommandDescriptor 无 operator==
    // ——以行数＋逐 id 抽查承载相等断言）。
    const auto cmdsAgain = requirementsDomainCommands();
    ASSERT_EQ(cmdsAgain.size(), cmds.size());
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        EXPECT_EQ(cmdsAgain[i].id, cmds[i].id);
        EXPECT_EQ(cmdsAgain[i].readOnlyAllowed, cmds[i].readOnlyAllowed);
    }

    // 两套命名空间分离（CommandId 点分词表 ≠ project commandType 无点词表）。
    const auto projectTokens = requirementsProjectCommandTokens();
    ASSERT_EQ(projectTokens.size(), std::size_t(2));
    EXPECT_EQ(projectTokens[0], std::string(kCmdApplyRequirementSet));
    EXPECT_EQ(projectTokens[1], std::string(kCmdApplyRequirementImport));
    for (const ui::CommandDescriptor& d : cmds) {
        for (const std::string& t : projectTokens) {
            EXPECT_NE(d.id, t);  // 词表交叉即违约
        }
    }
}

/// 快捷键经 HotkeyBindingTable 无冲突登记（ACC5——注册面行为断言）。
TEST(PluginPanel, HotkeyRegistrationConflictFreeViaBindingTable_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"SA-16"},
                  std::vector<std::string>{"ACC5-hotkey", "no-conflict"});
    // 真实命令注册表（白名单含 "requirements"——§11.1 八 token）。
    ui::CommandRegistryDeps cmdDeps;
    cmdDeps.ownerWhitelist = {"ui", "requirements"};
    auto registry = ui::createCommandRegistry(cmdDeps);
    ASSERT_NE(registry, nullptr);
    const auto cmds = requirementsDomainCommands();
    for (const ui::CommandDescriptor& d : cmds) {
        EXPECT_EQ(registry->registerCommand(d, [](const std::vector<ui::CommandParameter>&) {
                      return ui::CommandOutcome{};
                  }),
                  ui::RegistrationResult::Ok);
    }
    // 真实快捷键注册表（HotkeyBindingTable 的运行期面——§10.4）。
    ui::GlobalShortcutRegistryDeps hotkeyDeps;
    hotkeyDeps.commands = registry.get();
    auto hotkeys = ui::createGlobalShortcutRegistry(hotkeyDeps);
    ASSERT_NE(hotkeys, nullptr);
    // 九条命令各绑互异测试键→全部 Ok（无冲突——SA-16 结构保证的兑现面）。
    const std::vector<QKeySequence> keys = {
        QKeySequence("Ctrl+Shift+1"), QKeySequence("Ctrl+Shift+2"),
        QKeySequence("Ctrl+Shift+3"), QKeySequence("Ctrl+Shift+4"),
        QKeySequence("Ctrl+Shift+5"), QKeySequence("Ctrl+Shift+6"),
        QKeySequence("Ctrl+Shift+7"), QKeySequence("Ctrl+Shift+8"),
        QKeySequence("Ctrl+Shift+9"),
    };
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        EXPECT_EQ(hotkeys->registerDefault(cmds[i].id, keys[i]).kind(),
                  ui::HotkeyResult::Kind::Ok) << cmds[i].id;
    }
    // 同键再绑→Conflict（冲突检出即拒——不覆盖既有绑定）。
    EXPECT_EQ(hotkeys->registerDefault(cmds.front().id, keys.back()).kind(),
              ui::HotkeyResult::Kind::Conflict);
    // 空键→NotBindable（空键不是绑定——§10.4 契约；默认态零绑定的语义面）。
    EXPECT_EQ(hotkeys->registerDefault(cmds.front().id, QKeySequence()).kind(),
              ui::HotkeyResult::Kind::NotBindable);
}

// =====================================================================
// ACC2——编辑→草稿→应用链路（L-R2/L-R3）
// =====================================================================

/// L-R2 接受分支：applyEdit 接受＝工作集刷新＋脏标记（卡 §9.8 界面逻辑表）。
TEST(PluginPanel, SubmitEditAcceptRefreshesAndMarksDirty_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03", "UX-05"},
                  std::vector<std::string>{"ACC2-L-R2-accept", "REQ-11"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    RecordingSink sink;
    LocalUndoTracker tracker;

    // 编辑：改名（upsert 同 id）。
    TaskPoint renamed = point;
    renamed.name = "P1-改";
    const EditSubmitOutcome outcome = submitEntryEdit(editor, sink, renamed);
    tracker.recordAppliedEdit();  // widget 的 Applied 分支记账（同一时序）

    EXPECT_EQ(outcome, EditSubmitOutcome::Applied);
    EXPECT_EQ(editor.workingSet().points.entries.front().name, "P1-改");  // 工作集已刷新
    EXPECT_EQ(sink.appliedSummaries.size(), std::size_t(1));              // 刷新信号已发
    EXPECT_EQ(sink.dirtyNotifications, 1);                                // 脏标记已发
    EXPECT_TRUE(editor.draftStatus().dirty);
    EXPECT_EQ(editor.draftStatus().edits, std::uint64_t(1));
    EXPECT_TRUE(tracker.canUndo());
    EXPECT_FALSE(tracker.canRedo());
}

/// L-R2 拒绝分支：拒绝＝就地原因保留原值（工作集字节不变——域内强保证）。
TEST(PluginPanel, SubmitEditRejectKeepsValueWithInPlaceReason_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03", "UX-07"},
                  std::vector<std::string>{"ACC2-L-R2-reject", "I-REQ-3"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    RecordingSink sink;

    // 编辑：改成与既有条目重名（I-REQ-3——集合内名称唯一）。
    TaskPoint duplicate = point;
    duplicate.objectId = core::ObjectId::generate();  // 新 id＋同名＝重名 Add
    duplicate.name = "P1";
    const RequirementWorkingSet before = editor.workingSet();
    const EditSubmitOutcome outcome = submitEntryEdit(editor, sink, duplicate);

    EXPECT_EQ(outcome, EditSubmitOutcome::Rejected);
    EXPECT_EQ(editor.workingSet(), before);  // 字节不变（原值保留）
    ASSERT_EQ(sink.rejections.size(), std::size_t(1));
    EXPECT_EQ(sink.rejections.front().codeToken, "DuplicateName");  // 就地原因（域错误 token）
    EXPECT_EQ(sink.dirtyNotifications, 0);                          // 无脏标记
    EXPECT_FALSE(editor.draftStatus().dirty);
}

/// L-R3：draft.apply→buildDraftCommand("requirements")→①端口（信封装配）。
TEST(PluginPanel, BuildDraftCommandAssemblesApplyEnvelope_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04", "ARC-01"},
                  std::vector<std::string>{"ACC2-L-R3-envelope"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);

    RequirementsUiModule module;
    module.attachEditor(&editor);
    auto& session = module.session();
    session.branch = core::BranchId::fromCanonical("brn-10000000000000000000000000000001");
    session.baseRevision =
        core::RevisionId::fromCanonical("rev-20000000000000000000000000000002");
    session.rootObjectId =
        core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");

    // 无草稿变更＝无可应用内容（§8.5"无草稿可应用"态——不产生空修订）。
    EXPECT_FALSE(module.buildDraftCommand("requirements").has_value());

    // 一次编辑后组装信封。
    TaskPoint renamed = point;
    renamed.name = "P1-改";
    ASSERT_TRUE(editor.applyEdit(renamed).accepted);
    const auto envelope = module.buildDraftCommand("requirements");
    ASSERT_TRUE(envelope.has_value());
    EXPECT_EQ(envelope.value().branch, session.branch);
    ASSERT_TRUE(envelope.value().expectedRevision.has_value());
    EXPECT_EQ(envelope.value().expectedRevision.value(), session.baseRevision);
    EXPECT_EQ(envelope.value().commandType, std::string(kCmdApplyRequirementSet));
    EXPECT_EQ(envelope.value().payloadFormatVersion, kRequirementCommandPayloadVersion);

    // 载荷域解释（try 轨解码——五对象槽：根槽显式 oid＋四集合槽按根引用表）。
    const auto payload = tryDecodeRequirementCommandPayload(envelope.value().payloadCanonical);
    ASSERT_TRUE(payload.has_value());
    ASSERT_EQ(payload.value().objects.size(), std::size_t(5));
    EXPECT_FALSE(payload.value().objects[0].allocateNew);  // 根身份已回填
    EXPECT_EQ(payload.value().objects[0].objectId, session.rootObjectId.value());
    EXPECT_EQ(payload.value().objects[0].objectTypeToken, std::string(kReqSetObjectType));
    for (std::size_t i = 1; i < 5; ++i) {
        EXPECT_FALSE(payload.value().objects[i].allocateNew);  // 四集合已挂载
        EXPECT_TRUE(payload.value().objects[i].objectId.isValid());
    }

    // 首应用形态：根身份未回填→根槽 allocateNew（prepare 取号，PA-1）。
    RequirementsUiModule freshModule;
    MapClosure emptyClosure;
    PointSet emptyPoints;
    fillBaseline(emptyClosure, emptyPoints, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor freshEditor = makeLoadedEditor(emptyClosure);
    freshModule.attachEditor(&freshEditor);
    freshModule.session().rootObjectId = std::nullopt;  // 未回填
    (void)freshEditor.applyEdit(rootHeaderEdit("改名", "备注"));  // 根头编辑＝一次编辑
    const auto freshEnvelope = freshModule.buildDraftCommand("requirements");
    ASSERT_TRUE(freshEnvelope.has_value());
    const auto freshPayload =
        tryDecodeRequirementCommandPayload(freshEnvelope.value().payloadCanonical);
    ASSERT_TRUE(freshPayload.has_value());
    EXPECT_TRUE(freshPayload.value().objects[0].allocateNew);  // 取号回填语义

    // 域外请求＝nullopt（§11.2 参数语义）。
    EXPECT_FALSE(module.buildDraftCommand("modeling").has_value());
}

/// P-REQ-6：就绪 Blocking 在 prepare 现场重估——ui 侧不本地复判（仅呈现）。
TEST(PluginPanel, DraftCommandDoesNotLocallyGateOnBlocking_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"},
                  std::vector<std::string>{"ACC2-P-REQ-6", "present-not-gate"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    RequirementsUiModule module;
    module.attachEditor(&editor);
    module.session().rootObjectId =
        core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");

    // 会话就绪报告带 Blocking（呈现数据源——真实 checker 产出的 Blocking
    // 态：空必验集为 Warning，此处以手工 Blocking 发现模拟"报告含阻断"
    // 的呈现态——判定权威在 checker，本用例验证模块消费报告的行为面）。
    RequirementReadinessReport blockingReport;
    core::DiagnosticRecord finding = core::DiagnosticRecord::make(
        std::string(kReqReadyRefMissing), point.objectId, std::string("P1"), std::nullopt,
        std::string("校验上下文"), std::string("引用悬空（测试注入呈现态）"),
        std::string("补全引用后重试"));
    blockingReport.items.push_back(
        DomainReadinessItem{ReadinessCheckLayer::R1, ReadinessFindingLevel::Blocking, finding});
    module.session().readiness = blockingReport;

    // 呈现面：readonlyProjections 如实反映"输入不完整"（供数）。
    const auto projections = module.readonlyProjections();
    ASSERT_EQ(projections.size(), std::size_t(1));
    EXPECT_FALSE(projections[0].inputComplete);
    EXPECT_EQ(projections[0].verdict, core::EngineeringStatus::DataInsufficient);

    // ★ 关键断言：含 Blocking 报告时 buildDraftCommand 仍组装信封（拒绝在
    //   命令 prepare 现场重估——ui 侧仅呈现拒绝诊断，不本地复判）。
    TaskPoint renamed = point;
    renamed.name = "P1-改";
    ASSERT_TRUE(editor.applyEdit(renamed).accepted);
    const auto envelope = module.buildDraftCommand("requirements");
    EXPECT_TRUE(envelope.has_value());
}

// =====================================================================
// ACC3——插件零计算逻辑（源面扫描＋链接面登记）
// =====================================================================

/// 插件源面扫描：呈现模型零 Qt、零业务域互链 include（acceptance 3）。
TEST(PluginPanel, PluginSourcesNoQtInModelsNoBusinessCrossIncludes_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-MNT-01", "R-1"},
                  std::vector<std::string>{"ACC3-source-scan"});
    const std::filesystem::path pluginDir =
        std::filesystem::path(IRD_REQUIREMENTS_UNIT_ROOT) / "requirements" / "plugin";
    ASSERT_TRUE(std::filesystem::exists(pluginDir)) << pluginDir.string();
    // 呈现模型 TU（零 Qt 面——Q_OBJECT 面板 widget 除外）。
    const std::vector<std::string> zeroQtFiles = {
        "PanelTreeModel.hpp", "PanelTreeModel.cpp",
        "PanelStationModel.hpp", "PanelStationModel.cpp",
        "PanelRegionModel.hpp", "PanelRegionModel.cpp",
        "PanelConditionModel.hpp", "PanelConditionModel.cpp",
        "PanelValidationModel.hpp", "PanelValidationModel.cpp",
        "PanelCommandCatalog.hpp", "PanelCommandCatalog.cpp",
        "PanelEditFlow.hpp", "PanelEditFlow.cpp",
        "PanelRefresh.hpp", "PanelSelection.hpp",
        "ImportWizardFlow.hpp", "ImportWizardFlow.cpp",
        "RequirementsUiModule.hpp", "RequirementsUiModule.cpp",
    };
    // 全业务域词表（R-1 禁止互链的单元——ui 是许可面，不在其列）。
    const std::vector<std::string> businessUnits = {
        "modeling", "kinematics", "trajectory", "dynamics",
        "selection", "optimization", "workflow", "drivetrain",
    };
    for (const std::string& name : zeroQtFiles) {
        std::ifstream in((pluginDir / name).string());
        ASSERT_TRUE(in.good()) << name;
        const std::string src((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
        // 零 Qt include（呈现模型零 Qt——R-3 文件域隔离）。
        EXPECT_EQ(src.find("#include <Q"), std::string::npos) << name;
        EXPECT_EQ(src.find("#include \"Q"), std::string::npos) << name;
        // 零业务域互链 include（R-1——跨单元协作只走端口）。
        for (const std::string& unit : businessUnits) {
            EXPECT_EQ(src.find("sdurws/ird/" + unit + "/"), std::string::npos) << name;
        }
    }
    // 链接面登记说明（acceptance 3 的构建图半区）：插件链接面＝本单元计算
    // 库＋sdurws_ird_ui＋Qt 三件套——由 CMakeLists 单行链接语句钉死（配置
    // 期守卫＋ird_gates 引擎直跑承载，见留痕 ird_gates base..head 比对附件
    // ——非本测试的运行期断言面）。
}

// =====================================================================
// ACC4——两级撤销与辅助链路（L-R4/L-R10/L-R11/L-R12）
// =====================================================================

/// L-R4：草稿级 undoLocal 与项目级撤销分离呈现（两级不混用）。
TEST(PluginPanel, TwoLevelUndoSeparation_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"ACC4-L-R4", "two-level-undo"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    RecordingSink sink;
    LocalUndoTracker tracker;

    // 两次编辑入栈。
    TaskPoint renamed = point;
    renamed.name = "P1-a";
    ASSERT_TRUE(submitEntryEdit(editor, sink, renamed) == EditSubmitOutcome::Applied);
    tracker.recordAppliedEdit();
    TaskPoint renamedAgain = renamed;
    renamedAgain.name = "P1-b";
    ASSERT_TRUE(submitEntryEdit(editor, sink, renamedAgain) == EditSubmitOutcome::Applied);
    tracker.recordAppliedEdit();
    EXPECT_TRUE(tracker.canUndo());
    EXPECT_FALSE(tracker.canRedo());

    // 草稿级撤销一步（编辑器局部栈——零修订）：工作集回退、edits 计数
    // 不变（差值语义）、重做可位翻转。
    ASSERT_TRUE(tracker.undo(editor));
    EXPECT_EQ(editor.workingSet().points.entries.front().name, "P1-a");
    EXPECT_EQ(editor.draftStatus().edits, std::uint64_t(2));  // 差值语义——撤销不改计数
    EXPECT_TRUE(tracker.canUndo());
    EXPECT_TRUE(tracker.canRedo());

    // 项目级撤销是独立转发面（TwoLevelUndoView 两标签独立——呈现不混用；
    // UndoRedoService 语义归 project——本单元零复制零代理）。
    const TwoLevelUndoView view = twoLevelUndoView(editor, tracker);
    EXPECT_NE(view.draftUndoLabel, view.projectUndoLabel);
    EXPECT_FALSE(view.draftUndoLabel.empty());
    EXPECT_FALSE(view.projectUndoLabel.empty());
    EXPECT_EQ(view.editCount, std::uint64_t(2));

    // 全撤销→重做可、撤销可位翻转；重做一步回 "P1-b"。
    ASSERT_TRUE(tracker.undo(editor));
    EXPECT_FALSE(tracker.canUndo());
    EXPECT_TRUE(tracker.canRedo());
    ASSERT_TRUE(tracker.redo(editor));
    EXPECT_EQ(editor.workingSet().points.entries.front().name, "P1-a");
}

/// L-R10：导入向导域侧五步流（io 预检产物→映射＋单位预览→逐行错误→确认→草稿）。
TEST(PluginPanel, ImportWizardFlowFiveStepsToDraft_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-05", "AT-02"},
                  std::vector<std::string>{"ACC4-L-R10", "partial-success"});
    RequirementImporter importer;
    RequirementEditor editor = makeEditorWithOnePoint(nullptr);
    ImportWizardFlow flow(importer);

    // ①源接收（io 预检产物——本测试以内存表承载装配层半区）。
    // 表：最小必备集 id/name/x/y/z＋note；一行错误（z 非数值）＋一行正确。
    const io::RawTable table = makeTable({
        {"id", "name", "x", "y", "z", "note"},
        {"row-1", "导入点1", "1", "2", "3", "ok"},
        {"row-2", "导入点2", "1", "2", "abc", "坏行"},
    });
    flow.setSourceTable(table);
    EXPECT_EQ(flow.step(), ImportWizardStep::SourceSelected);

    // ②表头映射＋单位预览（自动识别——canonical 表头全命中；缺省 m/rad）。
    const ImportMappingView mapping = flow.prepareMapping(ImportUnitOptions::defaults());
    EXPECT_EQ(flow.step(), ImportWizardStep::Mapped);
    EXPECT_TRUE(mapping.mapping.isMapped(ImportField::Id));
    EXPECT_TRUE(mapping.mapping.isMapped(ImportField::Z));
    // 单位预览行（长度列——缺省 m 声明）。
    bool sawLengthPreview = false;
    for (const UnitPreviewEntry& e : mapping.unitPreview) {
        if (e.field == ImportField::X) {
            sawLengthPreview = true;
            EXPECT_EQ(e.declaredUnit, "m");
        }
    }
    EXPECT_TRUE(sawLengthPreview);

    // ③执行映射（行级部分成功：正确行保留、错误行定位）。
    const ImportOutcome outcome = flow.executeMapping(mapping.mapping, ImportUnitOptions::defaults());
    EXPECT_EQ(flow.step(), ImportWizardStep::Previewed);
    EXPECT_EQ(outcome.status, ImportOutcome::Status::Partial);
    EXPECT_EQ(outcome.entries.size(), std::size_t(1));          // 正确行 1
    EXPECT_EQ(outcome.rowErrors.size(), std::size_t(1));        // 错误行 1（定位到列）
    EXPECT_EQ(outcome.entries.front().objectId, core::ObjectId{});  // O-36：全零待分配

    // ④⑤确认→入草稿（逐条 applyEdit；正确行进工作集）。
    const std::size_t applied = flow.confirmAndApplyToDraft(editor);
    EXPECT_EQ(flow.step(), ImportWizardStep::Applied);
    EXPECT_EQ(applied, std::size_t(1));
    EXPECT_EQ(editor.workingSet().points.entries.size(), std::size_t(2));  // 基线 P1＋导入 1
    EXPECT_EQ(editor.draftStatus().edits, std::uint64_t(1));

    // 步骤违约 fail-fast（未设源即映射）。
    ImportWizardFlow fresh(importer);
    EXPECT_THROW(fresh.prepareMapping(ImportUnitOptions::defaults()), std::invalid_argument);
}

/// L-R11：JSON 副本导出——成功提示路径／失败旧文件完好提示。
TEST(PluginPanel, ExportCopyPromptSuccessAndFailureKeepsOldFile_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-12", "AT-24"},
                  std::vector<std::string>{"ACC4-L-R11", "atomic-old-file-intact"});
    RequirementImporter importer;
    MapClosure closure;
    PointSet points;
    points.entries.push_back(makePoint("P1"));
    fillBaseline(closure, points, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor editor = makeLoadedEditor(closure);
    const RequirementWorkingSet& ws = editor.workingSet();

    // 临时目录（导出目标——测试自清理）。
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "ird-wp14-t08-export";
    std::filesystem::create_directories(dir);
    const std::filesystem::path target = dir / "copy.json";
    // 预置"旧文件"（失败场景的完好性锚——字节级比对）。
    const std::string oldContent = "PRE-EXISTING";
    {
        std::ofstream out(target, std::ios::binary);
        out << oldContent;
    }

    // 成功：JSON 导出（draft=false）→ok＋提示携路径（L-R11"成功提示路径"）。
    const ExportTarget okTarget{target, false};
    const ExportCopyView okView = exportCopyPrompt(importer, ws, ExportFormat::Json, okTarget);
    EXPECT_TRUE(okView.ok);
    EXPECT_NE(okView.prompt.find(target.string()), std::string::npos);
    EXPECT_NE(okView.prompt.find("不影响项目"), std::string::npos);

    // 失败：draft=true 的 CSV 导出被域值面拒绝（ExportTarget 契约——防
    // 草稿数据误当正式数据）→ok=false＋提示"原文件保持完好"；旧文件字节
    // 级未变（AtomicFile 失败不改目标的域内强保证）。
    const std::filesystem::path csvTarget = dir / "copy.csv";
    {
        std::ofstream out(csvTarget, std::ios::binary);
        out << oldContent;
    }
    const ExportTarget badTarget{csvTarget, true};
    const ExportCopyView badView = exportCopyPrompt(importer, ws, ExportFormat::Csv, badTarget);
    EXPECT_FALSE(badView.ok);
    EXPECT_NE(badView.prompt.find("原文件保持完好"), std::string::npos);
    {
        std::ifstream in(csvTarget, std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        EXPECT_EQ(content, oldContent);  // 旧文件完好（字节级）
    }
    std::filesystem::remove_all(dir);
}

/// L-R12：只读模式——行门控＋命令 readOnlyAllowed 面＋浏览/预览/导出可用。
TEST(PluginPanel, ReadOnlyModeGate_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"},
                  std::vector<std::string>{"ACC4-L-R12", "readonly"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    const auto editableRows = stationFieldsFor(editor.workingSet().points.entries.front(), true);
    const auto readOnlyRows = stationFieldsFor(editor.workingSet().points.entries.front(), false);
    // 行门控：writable=false→可编辑行灰显；灰显行（来源）不变。
    ASSERT_EQ(editableRows.size(), readOnlyRows.size());
    bool sawEditable = false;
    for (std::size_t i = 0; i < editableRows.size(); ++i) {
        if (editableRows[i].enablement == StationFieldEnablement::Editable) {
            sawEditable = true;
            EXPECT_EQ(readOnlyRows[i].enablement, StationFieldEnablement::ReadOnlyGrey);
        } else if (editableRows[i].enablement == StationFieldEnablement::ReadOnlyGrey) {
            EXPECT_EQ(readOnlyRows[i].enablement, StationFieldEnablement::ReadOnlyGrey);
        }
    }
    EXPECT_TRUE(sawEditable);
    // 命令面：写命令 readOnlyAllowed=false；浏览/预览/副本导出/捕获/拾取
    // （只读可用族）＝true——与卡 §9.8 命令表逐条一致（ACC5 同源清单）。
    std::size_t readOnlyAllowedCount = 0;
    for (const ui::CommandDescriptor& d : requirementsDomainCommands()) {
        readOnlyAllowedCount += d.readOnlyAllowed ? 1 : 0;
    }
    EXPECT_EQ(readOnlyAllowedCount, std::size_t(3));  // export-copy/capture-tcp/pick-feature
}

// =====================================================================
// P-REQ-4——草稿源（ui IModuleDraftSource 冻结面：保存/恢复往返）
// =====================================================================

/// 草稿文档保存→恢复往返（V-13 模型半区——payload 域内解码重建工作集）。
TEST(PluginPanel, DraftSourceRoundtrip_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04"},
                  std::vector<std::string>{"ACC4-P-REQ-4", "draft-save-restore"});
    TaskPoint point;
    RequirementEditor editor = makeEditorWithOnePoint(&point);
    RequirementsUiModule module;
    module.attachEditor(&editor);
    RequirementsDraftSource source(module, editor);

    // 显示名（UX-02 工程用语）＋模块键。
    EXPECT_EQ(source.displayName(), "任务需求");

    // 编辑后组装草稿文档（payload＝五对象 Apply 载荷——域内同构编码）。
    TaskPoint renamed = point;
    renamed.name = "P1-草稿";
    ASSERT_TRUE(editor.applyEdit(renamed).accepted);
    const ui::DraftDocumentProjection doc = source.buildDraftDocument();
    EXPECT_EQ(doc.moduleId, "requirements");
    EXPECT_EQ(doc.schemaVersion, kRequirementCommandPayloadVersion);
    EXPECT_FALSE(doc.payload.empty());

    // 恢复：新编辑器承接文档→工作集＝草稿内容（loadBaseline 重建）＋
    // restoredDraftPending 置位（应用资格——draft.apply 可组装信封）。
    MapClosure emptyClosure;
    fillBaseline(emptyClosure, PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    RequirementEditor restored = makeLoadedEditor(emptyClosure);
    // 恢复编辑器需挂到同一模块（会话态面）——以新模块承载恢复流。
    RequirementsUiModule restoreModule;
    restoreModule.attachEditor(&restored);
    RequirementsDraftSource restoreSource(restoreModule, restored);
    restoreSource.adoptRestoredDocument(doc);
    EXPECT_TRUE(restoreModule.session().restoredDraftPending);
    ASSERT_EQ(restored.workingSet().points.entries.size(), std::size_t(1));
    EXPECT_EQ(restored.workingSet().points.entries.front().name, "P1-草稿");
    // 恢复后应用资格：零差值编辑但恢复位在——buildDraftCommand 可组装。
    restoreModule.session().rootObjectId =
        core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");
    EXPECT_TRUE(restoreModule.buildDraftCommand("requirements").has_value());

    // rebuildOnRevision：更新基线锚并终止恢复草稿的独立应用资格（§8.5
    // "基于当前版本重新编辑"）。
    restoreSource.rebuildOnRevision("rev-30000000000000000000000000000003");
    ASSERT_TRUE(restoreModule.session().baseRevision.has_value());
    EXPECT_FALSE(restoreModule.session().restoredDraftPending);
}
