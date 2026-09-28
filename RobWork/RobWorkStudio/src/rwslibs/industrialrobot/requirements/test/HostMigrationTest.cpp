/**
 * @file   HostMigrationTest.cpp
 * @brief  宿主迁移三接入面测试（模型层——QCoreApplication 级）——任务契约
 *         WP-14-T10 acceptance 1/2/3 的具名自证面。
 *
 * 设计依据：
 *   - B1-SPEC §5.1（TreeNodes/PropertyPages/SelectionAdapter 三接入面）、
 *     §5.2（迁移期双形态并存——deprecated 标记保留可用）、§5.3（共享 UI
 *     文件互斥——本测试只消费 ui 冻结协议公共头，零共享面改动）、§4.2
 *     （联动 L1~L3——L2 正向存在性→三维高亮；L3 反解→树定位）；
 *   - ui 注册协议（UI-T21/T22 冻结形状）：IndustrialProjectTree.hpp
 *     （ProjectTreeModel 重建/校验/定位）、PropertyInspector.hpp
 *     （PropertyInspectorModel first-wins 询问/分野哨兵/D6 激活编排）、
 *     SelectionService.hpp（selectBusiness 唯一写入口/L2 高亮判定/
 *     handleTreeViewFrameSelected 反解编排）、FormEditCommon.hpp
 *     （IFormEditOutlet 编辑移交面）；
 *   - 需求 REQ-01（任务点对象呈现迁移）/UX-05（参数表消费）；任务契约
 *     acceptance 2 迁移链路（树导航→检查器常用字段→复杂编辑页→L2/L3
 *     演示）——L2/L3 在需求对象上的可演示性由端到端用例承载（GUI 呈现
 *     面按 V-22 惯例由 harness 冒烟留痕承载，本目标不启动完整 GUI）；
 *   - modeling 先例：WP-13-T20 HostMigrationTest（同型测试——本文件为
 *     requirements 侧同构，域语义替换）。
 *
 * 测试范围声明：三接入面为模型层（零 Qt 控件）——树面板/检查器面板的
 *   Qt 渲染半区由 ui 单元自身 GUI 测试覆盖（UI-T21/T22 交付面）；本文件
 *   验证"域供给值 → 共享模型 → 联动编排"的全链语义＋既有方案 A 面板投
 *   影零回退（acceptance 3 增量面——无整体重写的回归自证）。AC 4 的构
 *   建/门禁/留痕项在任务验证流程执行（ird_gates＋双模式构建＋
 *   traceability/builds/wp14-t10/），不在本测试文件内。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sdurws/ird/requirements/Editor.hpp>       // RequirementEditor（域裁决唯一入口——真编辑器夹具）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // 五对象 token
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型（条目工厂）
#include "plugin/HostMigrationProviders.hpp"        // 被测三接入面（插件私有头——同单元可含）
#include "plugin/PanelEditFlow.hpp"                 // IRequirementEditSink（编辑分流替身接口）
#include "plugin/PanelTreeModel.hpp"                // buildRequirementTree（方案 A 投影回归面）
#include <sdurws/ird/ui/FormEditCommon.hpp>         // ui::ParamEditSet/IFormEditOutlet（编辑移交面）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // ui::ProjectTreeModel（共享树模型）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // ui::PropertyInspectorModel（共享检查器模型）
#include <sdurws/ird/ui/SelectionService.hpp>       // ui::SelectionService（选择唯一汇聚点）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::requirements;

namespace core = sdurws::ird::core;
namespace ui = sdurws::ird::ui;

namespace {

// =====================================================================
// 夹具（PluginPanelTest/EditorTest 同款形态——内存闭包＋合法条目工厂）
// =====================================================================

/// 合法任务点（I-REQ-5：至少一约束分量；work 段恒启用；位置未提供态）。
TaskPoint makePoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 位置已提供的任务点（常用字段页五字段形态的起步夹具——UserProvided 源）。
TaskPoint makePointWithPosition(const std::string& name, double x, double y, double z)
{
    TaskPoint p = makePoint(name);
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z), core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    return p;
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

/// 合法工况（level/enabled 由调用方调整）。
OperatingCondition makeCondition(const std::string& name)
{
    OperatingCondition c;
    c.objectId = core::ObjectId::generate();
    c.name = name;
    return c;
}

/// 合法采样计划（regionRef 指向给定区域条目）。
SamplingPlan makePlan(const core::ObjectId& regionId)
{
    SamplingPlan plan;
    plan.objectId = core::ObjectId::generate();
    plan.regionRef = regionId;
    plan.positionSampling =
        PositionSampling{PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
    return plan;
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

/// 装配"根＋四集合"基线闭包（返回根 oid——固定值可复现；EditorTest 同款）。
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

/// 载入基线的编辑器（成功前置起步夹具——失败即测试自身装配错误）。
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

/// 编辑流记录替身（sink 三路回调的接收记录——测试判别面）。
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

    std::vector<std::string> appliedSummaries;    ///< 接受回调（摘要序）
    int dirtyNotifications = 0;                   ///< 脏通知计数
    std::vector<EditRejection> rejections;        ///< 拒绝记录（就地错误明细）
    std::vector<core::DiagnosticRecord> warnings; ///< 批次警告记录
};

/// 高亮执行器记录替身（panelHighlight——下行联动的观测面）。
class HighlightSpy {
public:
    std::vector<std::optional<core::ObjectId>> calls;  ///< 调用序（nullopt＝清除）
    void operator()(const std::optional<core::ObjectId>& oid) { calls.push_back(oid); }
};

/// D6 激活执行器记录替身（complexPageActivator——自持打开的观测面）。
class ActivatorSpy {
public:
    std::vector<core::ObjectId> calls;  ///< 激活目标序
    void operator()(const core::ObjectId& oid) { calls.push_back(oid); }
};

/// 三接入面装配辅助：deps 组装（执行器绑定 spy——非 owning 存活期由
/// 用例作用域保证）。
RequirementsSharedSurfaceDeps makeDeps(IRequirementEditor* editor,
                                       std::function<std::optional<core::ObjectId>()> rootFn,
                                       HighlightSpy* highlight, ActivatorSpy* activator,
                                       IRequirementEditSink* editSink)
{
    RequirementsSharedSurfaceDeps deps;
    deps.editor = [editor]() -> IRequirementEditor* { return editor; };
    deps.rootObjectId = std::move(rootFn);
    deps.editSink = editSink;
    if (highlight != nullptr) {
        deps.panelHighlight = [highlight](const std::optional<core::ObjectId>& oid) {
            (*highlight)(oid);
        };
    }
    if (activator != nullptr) {
        deps.complexPageActivator = [activator](const core::ObjectId& oid) {
            (*activator)(oid);
        };
    }
    return deps;
}

// ---- 端到端替身族（L2/L3 演示——L5 端口缝隙的最小实现；真值边界归
// runtime RuntimeNameMap，R-4/SA-05——本族不复制解析语义）---------------

/// 运行时名映射替身（IUiRuntimeNameMapPort）——反解/正向同表（同源纪律）。
class FakeNameMap final : public ui::IUiRuntimeNameMapPort {
public:
    std::map<std::string, core::ObjectId> byName;  ///< 反解向
    std::map<core::ObjectId, std::string> byId;    ///< 正向向

    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it != byName.end() ? std::optional<core::ObjectId>{it->second}
                                  : std::nullopt;
    }
    std::optional<std::string> resolveRuntimeName(const core::ObjectId& id) const override
    {
        const auto it = byId.find(id);
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/// 三维高亮出口替身（IUiHighlightOutlet）——记录动作（L2 判定的观测面）。
class FakeHighlightOutlet final : public ui::IUiHighlightOutlet {
public:
    std::vector<std::string> highlighted;  ///< 高亮运行时名序
    int clearCalls = 0;                    ///< 清除次数

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        highlighted.push_back(runtimeName);
    }
    void clearHighlight() override { clearCalls += 1; }
};

/// L3 树定位回调替身（SelectionService Deps.treeLocator——反解落点观测）。
///
/// 协议语义对齐（UI-T21 面板把手）：树定位成功后选中事件经面板 Qt 信号
/// **回流** selectBusiness——服务侧不重复写状态（单一写入路径，见
/// SelectionService::handleTreeViewFrameSelected 分支①注释原文）。本替身
/// 经 reflow 通道模拟该回流（用例在 service 构造后绑定），否则反解成功
/// 分支只定位不选中——协议行为与替身语义必须同构。
class FakeTreeLocator {
public:
    std::vector<core::ObjectId> calls;  ///< 定位目标序
    std::function<bool(const core::ObjectId&)> reflow =
        [](const core::ObjectId&) { return false; };  ///< 面板选中回流（构造后绑定）

    bool locate(const core::ObjectId& oid)
    {
        calls.push_back(oid);
        return reflow(oid);  // 演示树恒命中（内容漂移分支归 UI-T21 交付面测试）
    }
};

/// 单一需求会话夹具：编辑器＋根身份＋一次性装配（各用例自持——隔离）。
struct SessionFixture {
    MapClosure closure;
    RequirementEditor editor;
    core::ObjectId rootOid;

    /// 装载指定四集合的会话（返回自身引用——链式组装）。
    static SessionFixture load(PointSet points, RegionSet regions,
                               ConditionSet conditions, PlanSet plans)
    {
        SessionFixture f;
        f.rootOid = fillBaseline(f.closure, points, regions, conditions, plans);
        f.editor = makeLoadedEditor(f.closure);
        return f;
    }
};

}  // namespace

// =====================================================================
// ACC1——树接入面（TreeNodesProvider：需求集根＋四集合条目入工业项目树）
// =====================================================================

/// 树供给：根在首位＋四集合条目按工作集序、全部 RequirementObjects 分组、
/// 子引用闭合（rebuild 整体成功）、共享树定位可命中（契约 acceptance 1）。
TEST(HostMigration, TreeSupply_RootAndEntriesIntoSharedTree_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    // 补条目：基线空集后经编辑器追加一条任务点（真编辑流——同域裁决）。
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);

    HighlightSpy highlight;
    ActivatorSpy activator;
    auto provider = std::make_shared<RequirementsTreeNodesProvider>(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    ui::ProjectTreeModel model;
    model.addProvider(provider);
    const ui::TreeRebuildReport report = model.rebuild();
    EXPECT_TRUE(report.ok) << report.reason;
    // 根＋1 条目；分组正确；子引用闭合（rebuild ok 已含 dangling 检查）。
    EXPECT_EQ(report.nodeCount, 2u);
    EXPECT_EQ(model.nodesInGroup(ui::ProjectTreeGroup::RequirementObjects).size(), 2u);
    const auto located = model.locate(point.objectId);
    ASSERT_TRUE(located.has_value());
    EXPECT_EQ(located->group, ui::ProjectTreeGroup::RequirementObjects);
    ASSERT_TRUE(located->parentId.has_value());  // 条目是根的子节点（optional 语义显式核对）
    EXPECT_EQ(*located->parentId, f.rootOid);
}

/// 树供给：根身份未回填（首应用前）＝条目组直属顶层（depth 0）——重建
/// 仍成功（类注会话边界的合法常态）。
TEST(HostMigration, TreeSupply_NoRootIdentityEntriesTopLevel_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);

    HighlightSpy highlight;
    ActivatorSpy activator;
    auto provider = std::make_shared<RequirementsTreeNodesProvider>(
        makeDeps(&f.editor, []() { return std::nullopt; },  // 根未回填
                 &highlight, &activator, &sink));

    ui::ProjectTreeModel model;
    model.addProvider(provider);
    const ui::TreeRebuildReport report = model.rebuild();
    EXPECT_TRUE(report.ok) << report.reason;
    EXPECT_EQ(report.nodeCount, 1u);
    const auto located = model.locate(point.objectId);
    ASSERT_TRUE(located.has_value());
    EXPECT_FALSE(located->parentId.has_value());  // 组直属顶层
}

/// 树供给：无会话（编辑器未注入）＝空集——空集不产生组内占位节点
/// （协议注释的合法常态；不崩溃不伪造）。
TEST(HostMigration, TreeSupply_NoSessionEmptySet_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsTreeNodesProvider provider(
        makeDeps(nullptr, []() { return std::nullopt; }, &highlight, &activator, nullptr));
    EXPECT_TRUE(provider.treeNodes().empty());
    EXPECT_EQ(provider.domainKey(), "requirements");
}

// =====================================================================
// ACC1——页面接入面（PropertyPagesProvider：常用字段＋D6 分野）
// =====================================================================

/// 任务点常用字段页（位置已提供）：恰五字段（位置三分量＋双容差）、基线
/// 值键闭合、不超分野哨兵、可编辑（出口随 editSink 装配提供）。
TEST(HostMigration, CommonPage_PointFiveFieldsWithBaseline_ACC1)
{
    IRD_TEST_INFO("REQ-01/UX-05", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePointWithPosition("P1", 1.0, 2.0, 3.0);
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    const auto page = provider.commonFieldsPage(pointOid);
    ASSERT_TRUE(page.has_value());
    EXPECT_EQ(page->title, "任务点常用参数");
    EXPECT_FALSE(page->readOnly);
    ASSERT_EQ(page->fields.size(), 5u);  // 位置三分量＋双容差
    EXPECT_LE(page->fields.size(), ui::kMaxCommonFieldsPerObject);  // 分野哨兵内
    // 基线键闭合（values 键 ⊆ fields 键——检查器三查之一）。
    for (const auto& v : page->values) {
        bool found = false;
        for (const auto& spec : page->fields) {
            if (spec.key == v.key) { found = true; break; }
        }
        EXPECT_TRUE(found) << v.key;
    }
    EXPECT_EQ(page->values.size(), 5u);  // 五字段全部有基线值
    EXPECT_NE(page->editOutlet, nullptr);  // 编辑出口已装配
}

/// 任务点常用字段页（位置未提供）：不供位置字段——双容差保底非空
/// （"缺失不转零"〔MDL-06 同源〕：检查器呈现占位，不伪造 0）。
TEST(HostMigration, CommonPage_PointWithoutPositionKeepsTwoTolerances_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");  // 位置未提供
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    const auto page = provider.commonFieldsPage(pointOid);
    ASSERT_TRUE(page.has_value());
    ASSERT_EQ(page->fields.size(), 2u);  // 双容差
    for (const auto& spec : page->fields) {
        EXPECT_NE(spec.key, "point-x");
    }
    ASSERT_EQ(page->values.size(), 2u);
}

/// 域判定在域：区域/工况/计划/根＝无常用字段页（v1 诚实边界——不虚构）；
/// 域外对象（伪造身份）＝nullopt（协议二态——检查器顺延询问下一域）。
TEST(HostMigration, CommonPage_NonPointObjectsAnswerNothing_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const WorkRegion region = makeRegion("R1");
    const OperatingCondition condition = makeCondition("C1");
    const SamplingPlan plan = makePlan(region.objectId);
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, region), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, condition), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, plan), EditSubmitOutcome::Applied);

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    EXPECT_FALSE(provider.commonFieldsPage(f.rootOid).has_value());  // 根
    EXPECT_FALSE(provider.commonFieldsPage(region.objectId).has_value());
    EXPECT_FALSE(provider.commonFieldsPage(condition.objectId).has_value());
    EXPECT_FALSE(provider.commonFieldsPage(plan.objectId).has_value());
    EXPECT_FALSE(provider.commonFieldsPage(core::ObjectId::generate()).has_value());  // 域外
}

// =====================================================================
// ACC1——编辑移交出口（IFormEditOutlet：键→域编辑流转译）
// =====================================================================

/// 出口编辑：位置 Z 分量单分量替换→域接受→工作集真变化＋sink 接受回调
/// ＋来源标记保源（L-R2 域裁决唯一——出口零判定）。
TEST(HostMigration, EditOutlet_PositionComponentAppliedThroughDomainFlow_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePointWithPosition("P1", 1.0, 2.0, 3.0);
    RecordingSink setupSink;  // 夹具提交记录（独立 sink——出口交互计数不被夹具污染）
    ASSERT_EQ(submitEntryEdit(f.editor, setupSink, point), EditSubmitOutcome::Applied);

    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;
    const auto sourceBefore = f.editor.workingSet().points.entries.front().pose.position.provenance();

    RecordingSink sink;  // 出口交互记录（applyEdits 分流的判别面）
    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));
    // 出口绑定供给时刻的目标（页供给路径刷新——测试直供页达成绑定）。
    const auto page = provider.commonFieldsPage(pointOid);
    ASSERT_TRUE(page.has_value());
    ASSERT_NE(page->editOutlet, nullptr);

    ui::ParamEditSet edits;
    ui::ParamChange change;
    change.key = "point-z";
    change.newSi = 9.5;
    edits.changes.push_back(change);
    page->editOutlet->applyEdits(edits);

    // 域接受：工作集值已更新（Z=9.5，X/Y 不变）＋接受/脏回调已发。
    ASSERT_EQ(sink.appliedSummaries.size(), 1u);
    EXPECT_EQ(sink.dirtyNotifications, 1);
    ASSERT_EQ(f.editor.workingSet().points.entries.size(), 1u);
    const auto pos = f.editor.workingSet().points.entries.front().pose.position.tryValue();
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ((*pos)[0], 1.0);
    EXPECT_DOUBLE_EQ((*pos)[1], 2.0);
    EXPECT_DOUBLE_EQ((*pos)[2], 9.5);
    // 改值保源：来源标记不因就地编辑改变。
    EXPECT_EQ(f.editor.workingSet().points.entries.front().pose.position.provenance(),
              sourceBefore);
    EXPECT_TRUE(sink.rejections.empty());
}

/// 出口编辑：位置未提供时编辑位置分量＝出口拒绝（position-not-provided）
/// ——不虚构另外两分量，工作集字节不变（拒绝优于伪造）。
TEST(HostMigration, EditOutlet_PositionNotProvidedRejectedAndUntouched_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");  // 位置未提供
    RecordingSink setupSink;  // 夹具提交记录（独立 sink——出口交互计数不被夹具污染）
    ASSERT_EQ(submitEntryEdit(f.editor, setupSink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;
    const auto before = f.editor.workingSet().points.entries.front();

    RecordingSink sink;  // 出口交互记录（applyEdits 分流的判别面）
    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));
    const auto page = provider.commonFieldsPage(pointOid);
    ASSERT_TRUE(page.has_value());

    ui::ParamEditSet edits;
    ui::ParamChange change;
    change.key = "point-x";
    change.newSi = 1.0;
    edits.changes.push_back(change);
    page->editOutlet->applyEdits(edits);

    // 出口拒绝（无域提交——编辑器未收到编辑）＋工作集不变。
    ASSERT_EQ(sink.rejections.size(), 1u);
    EXPECT_EQ(sink.rejections.front().codeToken, "position-not-provided");
    EXPECT_TRUE(sink.appliedSummaries.empty());
    EXPECT_EQ(f.editor.workingSet().points.entries.front(), before);
}

/// 出口编辑：容差单值提交经域接受；未知键＝防御拒绝（unknown-field）。
TEST(HostMigration, EditOutlet_ToleranceAppliedAndUnknownKeyRejected_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink setupSink;  // 夹具提交记录（独立 sink——出口交互计数不被夹具污染）
    ASSERT_EQ(submitEntryEdit(f.editor, setupSink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;

    RecordingSink sink;  // 出口交互记录（applyEdits 分流的判别面）
    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));
    const auto page = provider.commonFieldsPage(pointOid);
    ASSERT_TRUE(page.has_value());

    // 容差编辑（>0 有限——域裁决链受理）。
    ui::ParamEditSet edits;
    ui::ParamChange change;
    change.key = "position-tolerance";
    change.newSi = 2.0e-3;
    edits.changes.push_back(change);
    page->editOutlet->applyEdits(edits);
    ASSERT_EQ(sink.appliedSummaries.size(), 1u);
    EXPECT_DOUBLE_EQ(
        f.editor.workingSet().points.entries.front().tolerance.positionTolerance, 2.0e-3);

    // 未知键＝防御拒绝（检查器表单只回传本页键——防御面）。
    ui::ParamEditSet bad;
    ui::ParamChange badChange;
    badChange.key = "no-such-key";
    badChange.newSi = 1.0;
    bad.changes.push_back(badChange);
    page->editOutlet->applyEdits(bad);
    ASSERT_EQ(sink.rejections.size(), 1u);
    EXPECT_EQ(sink.rejections.front().codeToken, "unknown-field");
}

// =====================================================================
// ACC1——复杂编辑页（D6：入口声明＋域自持激活）
// =====================================================================

/// 复杂页：五类对象各一入口、hosted=false（D6"域面板内"形态）；激活转发
/// 执行器并 ok；未知页/无执行器＝诚实拒绝（封闭词表 reason）。
TEST(HostMigration, ComplexPages_EntriesHostedFalseAndActivation_ACC1)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC1"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    const WorkRegion region = makeRegion("R1");
    const OperatingCondition condition = makeCondition("C1");
    const SamplingPlan plan = makePlan(region.objectId);
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, region), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, condition), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, plan), EditSubmitOutcome::Applied);

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsPropertyPagesProvider provider(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    // 入口声明（各一入口、hosted=false；D6 点名的区域定义/导入向导在位）。
    const auto pointEntries = provider.complexPageEntries(point.objectId);
    ASSERT_EQ(pointEntries.size(), 1u);
    EXPECT_EQ(pointEntries.front().pageKey, "point-editor");
    EXPECT_FALSE(pointEntries.front().hosted);
    const auto regionEntries = provider.complexPageEntries(region.objectId);
    ASSERT_EQ(regionEntries.size(), 1u);
    EXPECT_EQ(regionEntries.front().pageKey, "region-definition");
    const auto rootEntries = provider.complexPageEntries(f.rootOid);
    ASSERT_EQ(rootEntries.size(), 1u);
    EXPECT_EQ(rootEntries.front().pageKey, "import-wizard");
    ASSERT_EQ(provider.complexPageEntries(condition.objectId).size(), 1u);
    ASSERT_EQ(provider.complexPageEntries(plan.objectId).size(), 1u);
    EXPECT_TRUE(provider.complexPageEntries(core::ObjectId::generate()).empty());  // 域外空集

    // 激活：转发执行器（域自持打开）＋自持页恒空。
    const auto report = provider.activateComplexPage(region.objectId, "region-definition", nullptr);
    EXPECT_TRUE(report.ok);
    EXPECT_EQ(report.hostedWidget, nullptr);
    ASSERT_EQ(activator.calls.size(), 1u);
    EXPECT_EQ(activator.calls.front(), region.objectId);

    // 未知页＝寻址拒绝（closed 词表）；无执行器＝诚实拒绝。
    const auto unknown = provider.activateComplexPage(point.objectId, "no-such-page", nullptr);
    EXPECT_FALSE(unknown.ok);
    EXPECT_EQ(unknown.reason, "activation-unknown-page");

    ActivatorSpy unused;
    RequirementsPropertyPagesProvider noActivator(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, nullptr, &sink));
    const auto blocked =
        noActivator.activateComplexPage(point.objectId, "point-editor", nullptr);
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(blocked.reason, "domain-surface-unavailable");
}

// =====================================================================
// ACC2——选择联动（SelectionAdapter：下行高亮＋上行拾取）
// =====================================================================

/// 下行联动：单选需求对象→面板高亮执行器收到；他域对象→对称清除；
/// runtimeOnly（L3 反解失败暂态）→零触碰（不误清既有高亮）。
TEST(HostMigration, Selection_DownstreamHighlightCrossDomainSilenceRuntimeOnly_ACC2)
{
    IRD_TEST_INFO("UX-05", {"WP-14-T10/AC2"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsSelectionAdapter adapter(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    // 单选本域对象→高亮。
    adapter.onSelectionChanged(ui::SelectionChange{{point.objectId},
                                                   ui::SelectionSource::ProjectTree, false, ""});
    ASSERT_EQ(highlight.calls.size(), 1u);
    EXPECT_EQ(highlight.calls.front(), std::optional<core::ObjectId>(point.objectId));

    // runtimeOnly 暂态→零触碰（不追加不清除）。
    adapter.onSelectionChanged(ui::SelectionChange{{}, ui::SelectionSource::ProjectTree,
                                                   true, "FrameX"});
    EXPECT_EQ(highlight.calls.size(), 1u);

    // 他域对象（闭包外）→对称清除。
    adapter.onSelectionChanged(ui::SelectionChange{{core::ObjectId::generate()},
                                                   ui::SelectionSource::ProjectTree, false, ""});
    ASSERT_EQ(highlight.calls.size(), 2u);
    EXPECT_EQ(highlight.calls.back(), std::nullopt);
}

/// 上行拾取：闭包内对象经服务唯一写入口广播（View3DPick 来源）；闭包外/
/// 无效身份/未接线＝拒绝上报 false（零伪造——selectBusiness 契约的域侧
/// 前置过滤）。
TEST(HostMigration, Selection_UpstreamPickOnlyClosure_ACC2)
{
    IRD_TEST_INFO("UX-05", {"WP-14-T10/AC2"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);

    FakeNameMap nameMap;  // 演示映射（L2 判定输入——空表＝不高亮，不涉本例断言）
    FakeTreeLocator locator;
    auto service = std::make_unique<ui::SelectionService>(ui::SelectionService::Deps{
        std::shared_ptr<ui::IUiRuntimeNameMapPort>(&nameMap, [](ui::IUiRuntimeNameMapPort*) {}),
        [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        nullptr, nullptr});

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsSelectionAdapter adapter(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));

    // 未接线＝false；接线后闭包内＝true 且服务选中生效。
    EXPECT_FALSE(adapter.reportView3DPick(point.objectId));
    adapter.attach(*service);
    EXPECT_FALSE(adapter.reportView3DPick(core::ObjectId::generate()));  // 闭包外
    EXPECT_TRUE(adapter.reportView3DPick(point.objectId));
    EXPECT_EQ(service->selectedObjectIds().size(), 1u);
    EXPECT_EQ(service->selectedObjectIds().front(), point.objectId);
    EXPECT_EQ(service->selectionSource(), ui::SelectionSource::View3DPick);
}

// =====================================================================
// ACC2——迁移链路端到端（共享树→检查器→复杂页；L2/L3 在需求对象上演示）
// =====================================================================

/// 端到端：共享树注册重建→树选（服务写入口）→检查器 first-wins 应答
/// 任务点页→复杂页激活转发执行器；建模对象（他域）选中＝检查器无应答
/// （本域 Provider 不越界应答——域判定在域的跨域面）。
TEST(HostMigration, EndToEnd_TreeToInspectorToComplexPage_ACC2)
{
    IRD_TEST_INFO("REQ-01/UX-05", {"WP-14-T10/AC2"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePointWithPosition("P1", 1.0, 2.0, 3.0);
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;

    HighlightSpy highlight;
    ActivatorSpy activator;
    auto deps = makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                         &highlight, &activator, &sink);
    auto treeProvider = std::make_shared<RequirementsTreeNodesProvider>(deps);
    auto pageProvider = std::make_shared<RequirementsPropertyPagesProvider>(deps);

    // 共享树注册＋重建（UI-T21 模型面）。
    ui::ProjectTreeModel tree;
    tree.addProvider(treeProvider);
    ASSERT_TRUE(tree.rebuild().ok);

    // 共享检查器注册（UI-T22 模型面）。
    ui::PropertyInspectorModel inspector;
    inspector.addProvider(pageProvider);

    // 树选经服务广播（L1 基线）→检查器应答任务点页。
    inspector.onSelectionChanged(ui::SelectionChange{{pointOid},
                                                     ui::SelectionSource::ProjectTree, false, ""});
    EXPECT_EQ(inspector.view().kind, ui::InspectorContentKind::ObjectFields);
    EXPECT_EQ(inspector.view().domainKey, "requirements");
    EXPECT_EQ(inspector.view().fields.size(), 5u);
    ASSERT_EQ(inspector.view().complexEntries.size(), 1u);
    EXPECT_EQ(inspector.view().complexEntries.front().pageKey, "point-editor");

    // D6 激活编排（检查器→Provider→执行器）。
    const auto report = inspector.activateComplexPage(pointOid, "point-editor", nullptr);
    EXPECT_TRUE(report.ok);
    ASSERT_EQ(activator.calls.size(), 1u);
    EXPECT_EQ(activator.calls.front(), pointOid);

    // 他域对象（建模对象模拟）＝无应答占位（迁移期诚实呈现——不虚构）。
    inspector.onSelectionChanged(ui::SelectionChange{{core::ObjectId::generate()},
                                                     ui::SelectionSource::ProjectTree, false, ""});
    EXPECT_EQ(inspector.view().kind, ui::InspectorContentKind::NoProviderAnswer);
}

/// L2 演示：单选已应用对象（NameMap 正向存在）→三维高亮出口收到运行时名
/// ；L2 反例：单选未应用草稿对象（NameMap 无对应）→不高亮（无动作不报
/// 错——B1-SPEC §4.2 L2 的承诺面与反例面）。
TEST(HostMigration, EndToEnd_L2HighlightAppliedAndDraftNotHighlighted_ACC2)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC2"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint applied = makePoint("P-applied");
    const TaskPoint draft = makePoint("P-draft");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, applied), EditSubmitOutcome::Applied);
    ASSERT_EQ(submitEntryEdit(f.editor, sink, draft), EditSubmitOutcome::Applied);
    const core::ObjectId appliedOid = f.editor.workingSet().points.entries.front().objectId;
    // draft 与 applied 字典序确定先后——按下标区分（I-REQ-1 稳定序）。
    const core::ObjectId draftOid = f.editor.workingSet().points.entries.back().objectId;

    // 演示 NameMap：仅"已应用"对象有运行时对应物（草稿未进编译链）。
    auto nameMap = std::make_shared<FakeNameMap>();
    nameMap->byId[appliedOid] = "TP-applied";
    nameMap->byName["TP-applied"] = appliedOid;
    auto outlet = std::make_shared<FakeHighlightOutlet>();
    FakeTreeLocator locator;
    ui::SelectionService service(ui::SelectionService::Deps{
        nameMap, [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        outlet, nullptr});

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsSelectionAdapter adapter(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));
    adapter.attach(service);

    // L2 正向：已应用对象→高亮出口收到运行时名。
    service.selectBusiness({appliedOid}, ui::SelectionSource::ProjectTree);
    ASSERT_EQ(outlet->highlighted.size(), 1u);
    EXPECT_EQ(outlet->highlighted.front(), "TP-applied");

    // L2 反例：未应用草稿对象→无三维动作（不高亮、不报错）。
    service.selectBusiness({draftOid}, ui::SelectionSource::ProjectTree);
    EXPECT_EQ(outlet->highlighted.size(), 1u);  // 无新增高亮
}

/// L3 演示：TreeView Select Frame 事件经反解命中需求对象→树定位回调触发
/// ＋以 ProjectTree 来源广播业务选中（反解成功的编排链——B1-SPEC §4.2 L3）。
TEST(HostMigration, EndToEnd_L3ResolveLocatesProjectTree_ACC2)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC2"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);
    const core::ObjectId pointOid = f.editor.workingSet().points.entries.front().objectId;

    auto nameMap = std::make_shared<FakeNameMap>();
    nameMap->byName["TP-1"] = pointOid;
    nameMap->byId[pointOid] = "TP-1";
    auto outlet = std::make_shared<FakeHighlightOutlet>();
    FakeTreeLocator locator;
    ui::SelectionService service(ui::SelectionService::Deps{
        nameMap, [&locator](const core::ObjectId& oid) { return locator.locate(oid); },
        outlet, nullptr});
    // 面板选中回流绑定（service 构造后——UI-T21 面板把手语义：定位选中
    // 事件经 Qt 信号回流 selectBusiness，服务侧单一写入路径）。
    locator.reflow = [&service](const core::ObjectId& oid) {
        service.selectBusiness({oid}, ui::SelectionSource::ProjectTree);
        return true;
    };

    HighlightSpy highlight;
    ActivatorSpy activator;
    RequirementsSelectionAdapter adapter(
        makeDeps(&f.editor, [&f]() { return std::optional<core::ObjectId>(f.rootOid); },
                 &highlight, &activator, &sink));
    adapter.attach(service);

    // 反解成功：树定位回调触发＋业务选中广播（来源收敛 ProjectTree）。
    service.handleTreeViewFrameSelected("TP-1");
    ASSERT_EQ(locator.calls.size(), 1u);
    EXPECT_EQ(locator.calls.front(), pointOid);
    EXPECT_EQ(service.selectedObjectIds().size(), 1u);
    EXPECT_EQ(service.selectedObjectIds().front(), pointOid);
    EXPECT_EQ(service.selectionSource(), ui::SelectionSource::ProjectTree);
    // 下行联动随广播到达适配器（树选→面板高亮）。
    ASSERT_EQ(highlight.calls.size(), 1u);
    EXPECT_EQ(highlight.calls.front(), std::optional<core::ObjectId>(pointOid));
}

// =====================================================================
// ACC3——Provider 增量面（无整体重写：方案 A 面板投影零回退）
// =====================================================================

/// 增量面回归：迁移接入后方案 A 自持树投影（buildRequirementTree）行为
/// 不变——根＋四分组＋条目结构照旧（deprecated 保留可用；GUI 标记呈现
/// 由 harness 冒烟承载，本用例钉模型层零回退）。
TEST(HostMigration, IncrementalFace_ExistingPanelProjectionUnchanged_ACC3)
{
    IRD_TEST_INFO("REQ-01", {"WP-14-T10/AC3"});
    SessionFixture f = SessionFixture::load(
        PointSet{}, RegionSet{}, ConditionSet{}, PlanSet{});
    const TaskPoint point = makePoint("P1");
    RecordingSink sink;
    ASSERT_EQ(submitEntryEdit(f.editor, sink, point), EditSubmitOutcome::Applied);

    const std::vector<RequirementNode> nodes = buildRequirementTree(f.editor.workingSet());
    ASSERT_FALSE(nodes.empty());
    EXPECT_EQ(nodes.front().kind, RequirementNodeKind::RequirementRoot);  // 根在首
    bool hasPointsGroup = false;
    bool hasPointEntry = false;
    for (const RequirementNode& n : nodes) {
        hasPointsGroup = hasPointsGroup || n.kind == RequirementNodeKind::PointsGroup;
        hasPointEntry = hasPointEntry || n.kind == RequirementNodeKind::Point;
    }
    EXPECT_TRUE(hasPointsGroup);  // 四分组结构照旧
    EXPECT_TRUE(hasPointEntry);   // 条目行照旧
}
