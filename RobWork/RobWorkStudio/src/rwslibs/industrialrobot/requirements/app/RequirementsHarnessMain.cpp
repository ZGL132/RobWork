/**
 * @file   RequirementsHarnessMain.cpp
 * @brief  需求域面板＋宿主迁移三接入面的开发验证 harness（GUI 手动验证
 *         通道——DTB §5.1 约定；WP-14-T10 迁移链任务的 GUI 冒烟留痕面）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/WP-14-T10.json acceptance 1/2/4（三接入面
 *     落位、迁移链路"项目树导航→检查器→复杂编辑页"端到端可演示、GUI
 *     冒烟留痕 traceability/builds/wp14-t10/）；
 *   - B1-SPEC §5.1（三接入面职责）、§5.2（迁移期双形态并存——自持导航
 *     deprecated 标记保留可用）、§4.2（联动 L1~L3）；PIPE §6.2/§6.3
 *     （共享 UI 装配面零改动——本 harness 只消费 UI-T21/T22 冻结协议）；
 *   - modeling 先例：WP-13-T20 app/HarnessMain.cpp（同型迁移演示——本
 *     文件为其 requirements 侧同构，域语义替换）。
 *
 * 真值边界（诚实声明——防止误当作产品宿主形态）：
 *   - 名称解析/运行时名映射的权威归 runtime RuntimeNameMap（R-4/SA-05）
 *     ——本 harness 的解析器/映射是端口缝隙的演示绑定（任务点"P1→TP-P1"
 *     演示映射），不复制解析语义；真实绑定随 UI-T20 宿主运行时发布桥在
 *     装配后的宿主中进行；
 *   - 三维高亮以状态行＋控制台回显（harness 无三维视图——L2 动作出线
 *     的可观测演示，不虚构三维呈现）；真实呈现归宿主 RWStudioView3D；
 *   - 域命令按钮经面板提交出口回显（L-3 提交/确认流待产品装配——不虚构
 *     提交成功）；
 *   - 自持四区面板标题旁的 deprecated 标记＝B1-SPEC §5.2 双形态并存
 *     （保留可用，删除归 WP-24-T09）。
 *
 * 自动化冒烟（--auto 参数）：启动后自动驱动一轮迁移演示（树重建→树选
 *   联动→检查器应答→D6 激活→L3 反解→L2 反例），逐步控制台输出
 *   [migration-smoke] 标记行，完成后自动退出（退出码 0＝全链无异常）；
 *   不带参数＝常驻窗口供手动检视（app.exec 直至用户关闭）。
 *
 * 线程模型：单线程——QApplication/编辑器/面板全部 main 线程（§3.4）。
 */

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <sdurws/ird/core/Provenance.hpp>           // ValueProvenance（位置来源标记）
#include <sdurws/ird/core/Units.hpp>                // core::UnitToken（比较型三要素演示值）
#include <sdurws/ird/requirements/Editor.hpp>       // RequirementEditor（域裁决唯一入口）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // 五对象 token
#include <sdurws/ird/requirements/Readiness.hpp>    // RequirementReadinessChecker（真实校验报告）
#include <sdurws/ird/requirements/RequirementTypes.hpp>  // 值模型（演示工作集）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>  // 共享项目树（UI-T21 冻结协议——迁移演示消费面）
#include <sdurws/ird/ui/PropertyInspector.hpp>      // 共享属性检查器（UI-T22 冻结协议——迁移演示消费面）
#include <sdurws/ird/ui/SelectionService.hpp>       // 选择服务（UI-T21 冻结协议——联动演示消费面）
#include <sdurws/ird/ui/UiPorts.hpp>                // ui::IUiNameResolver（显示名解析端口——UX-02）
#include <sdurws/ird/ui/UiTypes.hpp>                // ui::CommandId（点分小写命令 id 词表）

#include "plugin/HostMigrationProviders.hpp"        // 迁移三接入面（WP-14-T10 被验证面）
#include "plugin/RequirementsPanelWidget.hpp"       // 被验证的四区面板（本单元插件私有头——同单元可含，R-2 不跨单元）

using sdurws::ird::requirements::RequirementsPanelWidget;
using sdurws::ird::requirements::RequirementsSharedSurfaceDeps;
using sdurws::ird::requirements::RequirementsSelectionAdapter;
using sdurws::ird::requirements::RequirementsTreeNodesProvider;
using sdurws::ird::requirements::RequirementsPropertyPagesProvider;
using sdurws::ird::requirements::RequirementWorkingSet;
using sdurws::ird::requirements::RequirementEditor;
namespace ui = sdurws::ird::ui;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 闭包/会话夹具（EditorTest 同款形态——内存闭包＋合法条目工厂）
// =====================================================================

/// 测试/演示用闭包字节源：按 token/id 注册的内存映射（EditorTest 同款）。
class MapClosure final : public sdurws::ird::requirements::RequirementObjectClosureView {
public:
    void put(const std::string& token,
             const sdurws::ird::requirements::RequirementObjectVariant& object)
    {
        const sdurws::ird::requirements::RequirementCodec codec;
        auto bytes = codec.encode(object, sdurws::ird::requirements::kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byToken_[token] = sdurws::ird::requirements::RequirementClosureObject{token, bytes.get()};
        }
    }

    void putById(const core::ObjectId& id, const std::string& token,
                 const sdurws::ird::requirements::RequirementObjectVariant& object)
    {
        const sdurws::ird::requirements::RequirementCodec codec;
        auto bytes = codec.encode(object, sdurws::ird::requirements::kCurrentRequirementFormatVersion);
        if (bytes.ok()) {
            byId_[id] = sdurws::ird::requirements::RequirementClosureObject{token, bytes.get()};
        }
    }

    std::optional<sdurws::ird::requirements::RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        const auto it = byToken_.find(std::string{objectTypeToken});
        return it != byToken_.end() ? std::optional{it->second} : std::nullopt;
    }

    std::optional<sdurws::ird::requirements::RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
    {
        const auto it = byId_.find(objectId);
        return it != byId_.end() ? std::optional{it->second} : std::nullopt;
    }

private:
    std::map<std::string, sdurws::ird::requirements::RequirementClosureObject> byToken_;
    std::map<core::ObjectId, sdurws::ird::requirements::RequirementClosureObject> byId_;
};

/// 合法任务点（位置已提供——常用字段页五字段形态的演示起步）。
sdurws::ird::requirements::TaskPoint makePoint(const std::string& name,
                                               double x, double y, double z)
{
    sdurws::ird::requirements::TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.work = sdurws::ird::requirements::TaskSegment{true,
                                                   sdurws::ird::requirements::SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 合法工作区域（I-REQ-6 非退化盒＋Grid 采样）。
sdurws::ird::requirements::WorkRegion makeRegion(const std::string& name)
{
    sdurws::ird::requirements::WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = name;
    r.box = sdurws::ird::requirements::BoundingBox{
        rw::math::Vector3D<double>(1.0, 2.0, 3.0), rw::math::Vector3D<double>(2.0, 2.0, 2.0)};
    r.positionSampling = sdurws::ird::requirements::PositionSampling{
        sdurws::ird::requirements::PositionSamplingMethod::Grid, {2, 2, 2}, {0, 0, 0}, 0};
    return r;
}

/// 合法工况（演示起步态）。
sdurws::ird::requirements::OperatingCondition makeCondition(const std::string& name)
{
    sdurws::ird::requirements::OperatingCondition c;
    c.objectId = core::ObjectId::generate();
    c.name = name;
    return c;
}

/// 装配演示基线闭包（根＋四集合；返回根 oid——固定值可复现）。
core::ObjectId fillBaseline(MapClosure& closure,
                            sdurws::ird::requirements::PointSet points,
                            sdurws::ird::requirements::RegionSet regions,
                            sdurws::ird::requirements::ConditionSet conditions)
{
    using namespace sdurws::ird::requirements;
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
    closure.putById(pointSetId, std::string{kReqPointSetObjectType}, RequirementObjectVariant{points});
    closure.putById(regionSetId, std::string{kReqRegionSetObjectType}, RequirementObjectVariant{regions});
    closure.putById(condSetId, std::string{kReqConditionSetObjectType}, RequirementObjectVariant{conditions});
    closure.putById(planSetId, std::string{kReqPlanSetObjectType}, RequirementObjectVariant{PlanSet{}});
    return core::ObjectId::fromCanonical("obj-50000000000000000000000000000005");  // 根 oid（固定）
}

// =====================================================================
// 迁移演示替身族（WP-14-T10 GUI 冒烟——L5 端口的最小演示实现；真值边界
// 见文件头：解析权威归 runtime，本族是端口缝隙的演示绑定）
// =====================================================================

/// 显示名解析（IUiNameResolver）：树/检查器渲染显示名——演示映射为条目
/// name（UX-02 呈现口径；解析失败占位由面板处理）。
class HarnessNameResolver final : public ui::IUiNameResolver {
public:
    std::map<std::string, std::string> byId;  ///< ObjectId 规范文本→显示名

    std::optional<std::string> resolveObjectId(core::ObjectId id) const override
    {
        const auto it = byId.find(id.toCanonical());
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/// 运行时名映射（IUiRuntimeNameMapPort）：SA-05 双射的演示绑定——仅任务
/// 点 P1 演示映射 "TP-P1"（已应用对象；区域/工况为未应用草稿——L2 反例）。
class HarnessNameMap final : public ui::IUiRuntimeNameMapPort {
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

/// 三维高亮出口（IUiHighlightOutlet）：harness 无三维视图——高亮动作以
/// 状态行＋控制台回显（L2 动作出线的可观测演示，不虚构三维呈现）。
class HarnessHighlightOutlet final : public ui::IUiHighlightOutlet {
public:
    QStatusBar* status = nullptr;  ///< 回显落点（窗口构造后接线；可空＝仅控制台）

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        std::cout << "[migration-smoke] L2 highlight: " << runtimeName << std::endl;
        if (status != nullptr) {
            status->showMessage(
                QString("三维高亮：%1（演示出口——真实呈现归宿主 RWStudioView3D）")
                    .arg(QString::fromStdString(runtimeName)));
        }
    }
    void clearHighlight() override
    {
        std::cout << "[migration-smoke] L2 highlight cleared" << std::endl;
        if (status != nullptr) {
            status->showMessage(QStringLiteral("三维高亮：清除"));
        }
    }
};

}  // namespace

/// @brief harness 入口。
/// @return 0 正常退出；1 装配失败（控制台输出原因——环境/构建问题）
int main(int argc, char* argv[])
{
    try {
        const bool autoSmoke = argc > 1 && std::string(argv[1]) == "--auto";

        QApplication app(argc, argv);
        QApplication::setOrganizationName("sdurws");
        QApplication::setApplicationName("ird-requirements-harness");

        // ①会话权威编辑器：main 栈上自持（产品中归装配层——同一"UI 线程
        //   单例权威"语义，§3.4）。面板/三接入面经提供器现取，零副本。
        using namespace sdurws::ird::requirements;
        MapClosure closure;
        PointSet points;
        points.entries.push_back(makePoint("P1", 1.0, 2.0, 3.0));
        RegionSet regions;
        regions.entries.push_back(makeRegion("R1"));
        ConditionSet conditions;
        conditions.entries.push_back(makeCondition("C1"));
        const core::ObjectId rootOid = fillBaseline(closure, points, regions, conditions);
        RequirementEditor editor;
        const auto load = editor.loadBaseline(closure);
        if (!load.ok) {
            std::cerr << "[harness] 基线载入失败: " << load.error.detail << std::endl;
            return 1;
        }

        // ②四区面板（初始可写——L-R12 门控初值）。
        RequirementsPanelWidget panel(true);

        // ③编辑目标提供器：每次编辑提交现取权威编辑器（L-R2 的数据前提；
        //   捕获引用——editor 存活期覆盖 app.exec() 全程）。
        panel.setEditTargetProvider([&editor]() -> IRequirementEditor* { return &editor; });

        // ③b 命令提交出口：产品装配层此处绑定 ui CommandRegistry.submit；
        //   装配前的 harness 以回显代替（诚实边界——不虚构提交成功）。
        panel.setCommandSubmit([](const ui::CommandId& id) {
            std::cout << "[command] " << id << std::endl;
        });

        // ④首刷：校验报告取真实校验器产出（真值边界——判定面归域校验链，
        //   面板只呈现）；树/检查器/区域/工况/校验页自真实工作集投影。
        //   诚实边界：需求面板无 post-edit 外部钩子（编辑后刷新是面板内部
        //   既有流——onEditApplied 全面板重投影），共享面的编辑后同步属
        //   产品装配层编排（harness 由下方演示序列显式驱动，不虚构接线）。
        const RequirementReadinessChecker checker;
        const RequirementReadinessReport report =
            checker.check(editor.workingSet(), CheckContext{});
        panel.refreshPanel(editor.workingSet(), report);

        // ⑤主窗口承载＋迁移演示装配（WP-14-T10——B1-SPEC §5.1 三接入面
        //   的最小宿主形态）：共享工业项目树＋共享属性检查器＋选择服务。
        QMainWindow window;
        window.setWindowTitle(
            QStringLiteral("需求面板开发验证 harness（sdurws_ird_requirements_app）"));
        window.setCentralWidget(&panel);

        // ⑤a 域三接入面（Deps 绑定 harness 会话与面板——面板即编辑分流
        //   出口，focusObject 即高亮/激活执行器）。
        RequirementsSharedSurfaceDeps migrationDeps;
        migrationDeps.editor = [&editor]() -> IRequirementEditor* { return &editor; };
        migrationDeps.rootObjectId = [&rootOid]() { return std::optional<core::ObjectId>(rootOid); };
        migrationDeps.editSink = &panel;
        migrationDeps.panelHighlight = [&panel](
            const std::optional<core::ObjectId>& oid) {
            panel.focusObject(oid);  // 树选→域面板高亮/定位（下行联动）
        };
        migrationDeps.complexPageActivator = [&panel](const core::ObjectId& oid) {
            panel.focusObject(oid);  // D6 域自持打开＝定位目标对象
        };
        auto treeProvider = std::make_shared<RequirementsTreeNodesProvider>(migrationDeps);
        auto pagesProvider = std::make_shared<RequirementsPropertyPagesProvider>(migrationDeps);

        // ⑤b 共享模型＋选择服务（UI-T21/T22 冻结面；演示映射填充）。
        auto resolver = std::make_shared<HarnessNameResolver>();
        auto nameMap = std::make_shared<HarnessNameMap>();
        auto highlightOutlet = std::make_shared<HarnessHighlightOutlet>();
        const TaskPoint& p1 = editor.workingSet().points.entries.front();
        resolver->byId[p1.objectId.toCanonical()] = "P1";
        resolver->byId[rootOid.toCanonical()] = "演示需求集";
        nameMap->byId[p1.objectId] = "TP-P1";   // 已应用演示对象（L2 正向）
        nameMap->byName["TP-P1"] = p1.objectId;

        ui::ProjectTreeModel treeModel;
        treeModel.addProvider(treeProvider);
        ui::PropertyInspectorModel inspectorModel;
        inspectorModel.addProvider(pagesProvider);

        // 树定位延迟接线（L3 反解成功的落点＝共享树面板定位选中）。
        std::function<bool(const core::ObjectId&)> locateFn =
            [](const core::ObjectId&) { return false; };  // 面板创建后重绑
        ui::SelectionService selection(ui::SelectionService::Deps{
            nameMap, [&locateFn](const core::ObjectId& oid) { return locateFn(oid); },
            highlightOutlet, nullptr});

        // 共享树/检查器面板（工厂——R-2 封闭实现；deps shared 非 owning）。
        ui::IndustrialProjectTreePanelDeps treePanelDeps;
        treePanelDeps.model = std::shared_ptr<ui::ProjectTreeModel>(
            &treeModel, [](ui::ProjectTreeModel*) {});
        treePanelDeps.selection = std::shared_ptr<ui::SelectionService>(
            &selection, [](ui::SelectionService*) {});
        treePanelDeps.nameResolver = resolver;
        auto treePanel = ui::createIndustrialProjectTreePanel(treePanelDeps, nullptr);

        ui::PropertyInspectorPanelDeps inspectorPanelDeps;
        inspectorPanelDeps.model = std::shared_ptr<ui::PropertyInspectorModel>(
            &inspectorModel, [](ui::PropertyInspectorModel*) {});
        inspectorPanelDeps.nameResolver = resolver;
        auto inspectorPanel = ui::createPropertyInspectorPanel(inspectorPanelDeps, nullptr);

        // 树定位重绑（L3 落点＝共享树定位选中）；检查器订阅服务（L1——
        // UI-T22 模型即 IUiSelectionObserver）；适配器订阅服务（下行联动）。
        locateFn = [&treePanel](const core::ObjectId& oid) {
            return treePanel->locateAndHighlight(oid);
        };
        RequirementsSelectionAdapter adapter(migrationDeps);
        adapter.attach(selection);
        auto inspectorSub = selection.subscribe(inspectorModel);

        // 共享面刷新编排（装配层编排形：重建→双面板刷新→检查器按当前
        // 选中重询问）。
        const auto refreshSharedSurfaces = [&]() {
            const ui::TreeRebuildReport r = treeModel.rebuild();
            if (!r.ok) {
                std::cout << "[migration-smoke] tree rebuild rejected: " << r.reason
                          << std::endl;
            }
            treePanel->refresh();
            ui::SelectionChange current;
            current.selectedObjectIds = selection.selectedObjectIds();
            current.source = selection.selectionSource().value_or(
                ui::SelectionSource::ProjectTree);
            inspectorModel.onSelectionChanged(current);
            inspectorPanel->refresh();
        };
        refreshSharedSurfaces();

        // ⑥Dock 挂位（演示形态——产品宿主挂位归 UI-T23/WP-24-T08，O-43）。
        QDockWidget* treeDock = new QDockWidget(
            QStringLiteral("工业项目树（共享·WP-14-T10 演示）"), &window);
        treeDock->setWidget(treePanel->widget());
        window.addDockWidget(Qt::LeftDockWidgetArea, treeDock);
        QDockWidget* inspectorDock = new QDockWidget(
            QStringLiteral("属性检查器（共享·WP-14-T10 演示）"), &window);
        inspectorDock->setWidget(inspectorPanel->widget());
        inspectorDock->setMinimumWidth(320);  // 字段呈现宽度下限（防 Dock 挤压）
        window.addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

        // L3 反解演示按钮：模拟官方 TreeView Select Frame 事件（TP-P1）——
        // 反解→树定位选中→检查器刷新全链；World.UnknownFrame 触发反解失败
        // 分支（树不动＋runtimeOnly 暂态）。
        QToolBar* toolbar = window.addToolBar(QStringLiteral("迁移演示"));
        toolbar->setMovable(false);
        QPushButton* l3Button =
            new QPushButton(QStringLiteral("模拟 TreeView 选中 P1（L3 反解）"), &window);
        toolbar->addWidget(l3Button);
        QObject::connect(l3Button, &QPushButton::clicked, &window,
                         [&selection]() { selection.handleTreeViewFrameSelected("TP-P1"); });
        QPushButton* l3MissButton =
            new QPushButton(QStringLiteral("模拟反解失败"), &window);
        toolbar->addWidget(l3MissButton);
        QObject::connect(l3MissButton, &QPushButton::clicked, &window, [&selection]() {
            selection.handleTreeViewFrameSelected("World.UnknownFrame");
        });

        window.statusBar()->showMessage(QStringLiteral(
            "开发期 harness：命令仅回显（提交/确认流待装配）；名称映射/高亮为演示绑定"
            "（解析权威归 runtime——R-4）；自持导航已标 deprecated（B1-SPEC §5.2）"));
        window.resize(1280, 860);
        highlightOutlet->status = window.statusBar();  // L2 回显落点接线
        window.show();

        // ⑦自动化冒烟（--auto）：迁移链路全步演示＋控制台标记＋自动退出。
        if (autoSmoke) {
            QTimer::singleShot(200, &window, [&]() {
                // 步 1：deprecated 标记存在性（B1-SPEC §5.2 双形态并存）。
                const QLabel* deprecation =
                    panel.findChild<QLabel*>("requirementsNavDeprecationLabel");
                std::cout << "[migration-smoke] deprecated-label="
                          << (deprecation != nullptr ? "present" : "MISSING") << std::endl;

                // 步 2：树重建规模（根＋P1＋R1＋C1＝4 节点）。
                std::cout << "[migration-smoke] tree-nodes="
                          << treeModel.nodeCount() << std::endl;

                // 步 3：树选 P1（L1——树选→服务广播→检查器应答＋面板高亮）。
                selection.selectBusiness({p1.objectId}, ui::SelectionSource::ProjectTree);
                std::cout << "[migration-smoke] inspector-kind="
                          << static_cast<int>(inspectorModel.view().kind)
                          << " fields=" << inspectorModel.view().fields.size() << std::endl;

                // 步 4：D6 激活（任务点复杂页＝域自持打开）。
                const auto activation = inspectorModel.activateComplexPage(
                    p1.objectId, "point-editor", nullptr);
                std::cout << "[migration-smoke] d6-activate="
                          << (activation.ok ? "ok" : activation.reason) << std::endl;

                // 步 5：L3 反解（TreeView Select Frame 事件→反解→树定位）。
                selection.handleTreeViewFrameSelected("TP-P1");
                std::cout << "[migration-smoke] l3-selected="
                          << selection.selectedObjectIds().size() << std::endl;

                // 步 6：L2 反例（未应用草稿 R1 不高亮——无三维动作）。
                const WorkRegion& r1 = editor.workingSet().regions.entries.front();
                selection.selectBusiness({r1.objectId}, ui::SelectionSource::ProjectTree);
                std::cout << "[migration-smoke] l2-draft-no-highlight=expected" << std::endl;

                std::cout << "[migration-smoke] DONE" << std::endl;
                app.exit(0);  // 冒烟完成自动退出（退出码 0＝全链无异常）
            });
        }

        return app.exec();
    } catch (const std::exception& err) {
        // 装配失败属环境/构建问题——fail-fast 带原因退出，不带病进入半
        // 初始化界面。
        std::cerr << "[harness] 装配失败: " << err.what() << std::endl;
        return 1;
    }
}
