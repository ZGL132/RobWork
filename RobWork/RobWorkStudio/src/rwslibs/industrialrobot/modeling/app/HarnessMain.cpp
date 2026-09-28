/**
 * @file   HarnessMain.cpp
 * @brief  建模插件面板开发验证 harness（sdurws_ird_modeling_app）——
 *         "插件开发完成即可手动 GUI 验证"的建模侧载体（owner 指示
 *         2026-09-26；先例＝ui 单元 sdurws_ird_ui_app，UI-T15）。
 *
 * 设计依据：
 *   - units/modeling.md §9.7（插件界面设计与界面逻辑）、§9.7.1（五区
 *     信息架构）、§9.7.2（L-1 选中联动／L-2 字段编辑流／L-7 只读门控）、
 *     §9.7.4（编辑器仅 UI 线程；面板不缓存权威数据——ACC5）；
 *   - 需求 MDL-07（关节树/参数表联动）、UX-05/07（表单编辑与内联确认）、
 *     PM-04（脏标记）；DTB §5.1（构建约定——插件完成定义含 GUI 手动
 *     验证通道，owner 指示 2026-09-26 增补）；
 *   - 任务背景：WP-13-T15 已交付面板（verdict=pass），但装配点
 *     （registrar 调用）归 L5 装配任务 WP-24-T03，P-MDL-8 期间面板在
 *     宿主中尚不可见——本 harness 补齐"VS 编译→启动→手动点验"通道。
 *
 * ★ 与产品装配层的关系（同 ui_app 先例口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层
 *   （CentralAreaHost）创建与持有的产品契约不变（§9.7.1）；本 harness
 *   在进程内扮演装配层的最小角色：注入编辑目标提供器（会话权威工作集）
 *   与命令提交出口（回显，不进 project 命令管线），把真实面板、真实
 *   呈现模型与真实域编辑裁决（PanelEditFlow→applyEdit）跑起来供人工
 *   验证。产品装配层落位时按同一契约替换本入口，modeling 插件面零改动。
 *
 * 真值边界（诚实声明，防止误读验证结论）：
 *   - 真实部分：六轴模板草稿（RobotDesignTemplateFactory 真实产出）、
 *     L-1 树/属性联动、L-2 编辑流（域裁决＋就地拒绝）、L-7 只读门控、
 *     脏标记、域命令目录呈现；
 *   - 演示部分：就绪条条目为手填样例（读 ReadinessTest/PluginPanelTest
 *     同款构造）——真实就绪校验需要 AssertionSuite 装配（策略评估器＋
 *     名称上下文，L5 注入面），harness 不复制该装配（R-4：名称解析归
 *     runtime；且避免 harness 引入 policy/runtime 链接面）；
 *   - 不覆盖：draft.apply 提交/确认对话（需 project 管线，L-3 的完整
 *     验证在装配后的宿主中进行——命令按钮激活仅回显即此含义）。
 *
 * 用法（开发期交互验证）：
 *   sdurws_ird_modeling_app
 *     启动即见五区面板＋generic-6r 草稿；点树节点验属性过滤（MDL-07），
 *     改属性值验 L-2（非法输入就地报错保留原值），去勾"可写"验 L-7，
 *     点域命令按钮看控制台回显（命令 id 全集见 §9.7.3 目录）。
 *   迁移演示（WP-13-T20——左 Dock 共享工业项目树＋右 Dock 共享属性
 *     检查器）：点共享树"建模对象"组节点验检查器常用字段与自持面板
 *     高亮联动（L1）；单选关节验状态行三维高亮回显（L2 演示出口）；
 *     关节页"复杂编辑"入口"DH 参数"验域面板定位（D6 自持打开）；改
 *     检查器零位值验编辑流经域裁决同步双面板；工具条"模拟 TreeView
 *     选中 J2"验 L3 反解定位、"模拟反解失败"验 runtimeOnly 暂态。
 *     自持五区面板标题旁的 deprecated 标记＝B1-SPEC §5.2 双形态并存
 *     （保留可用，删除归 WP-24-T09）。
 *
 * 线程模型：单线程——QApplication/工作集/面板全部 main 线程（§3.4；
 *   面板内部 PanelUiThreadGuard 对跨线程访问 fail-fast，harness 不触探）。
 */

#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <sdurws/ird/core/Units.hpp>               // core::UnitToken::find（比较型三要素单位）
#include <sdurws/ird/modeling/DiagCodes.hpp>       // kMdl06TravelLimit 等（已注册码字面——不臆造码）
#include <sdurws/ird/modeling/Readiness.hpp>       // ModelReadinessReport/ReadinessNote（就绪条数据面）
#include <sdurws/ird/modeling/Template.hpp>        // RobotDesignTemplateFactory/createDraft（六轴草稿真源）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp> // 共享项目树（UI-T21 冻结协议——迁移演示消费面）
#include <sdurws/ird/ui/PropertyInspector.hpp>     // 共享属性检查器（UI-T22 冻结协议——迁移演示消费面）
#include <sdurws/ird/ui/SelectionService.hpp>      // 选择服务（UI-T21 冻结协议——联动演示消费面）
#include <sdurws/ird/ui/UiPorts.hpp>               // ui::IUiNameResolver（显示名解析端口——UX-02）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CommandId（点分小写命令 id——§9.7.3 词表）

#include "plugin/HostMigrationProviders.hpp"       // 迁移三接入面（WP-13-T20 被验证面）
#include "plugin/ModelingPanelWidget.hpp"          // 被验证的五区面板（本单元插件私有头——同单元可含，R-2 不跨单元）

#include <exception>
#include <iostream>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

//using namespace sdurws::ird;
using sdurws::ird::modeling::ModelingPanelWidget;
using sdurws::ird::modeling::ModelingSharedSurfaceDeps;
using sdurws::ird::modeling::ModelingWorkingSet;
using sdurws::ird::modeling::ModelReadinessReport;
using sdurws::ird::modeling::ReadinessNote;
using sdurws::ird::modeling::RobotDesignTemplateFactory;
using sdurws::ird::modeling::TemplateOutcome;
using sdurws::ird::modeling::TemplateId;
using sdurws::ird::modeling::kTemplateIdGeneric6R;

namespace {

/// 确定性占位锚（演示条目的 subject 定位用——非全零、字节可控；
/// PluginPanelTest::fixedOid 同款，仅用于"定位跳转"呈现验证）。
sdurws::ird::core::ObjectId fixedOid(std::uint8_t tag)
{
    sdurws::ird::core::ObjectId oid;
    oid.bytes[0] = tag;
    return oid;
}

/**
 * @brief 创建 generic-6r 六轴模板草稿（真实域路径——非夹具克隆）。
 *
 * 走产品创建入口 createDraft（§9.4.2）：纯函数、仅内存、不触达 project、
 * 不产生修订（PA-1——草稿工作集在 harness 进程内自持，正是 L-2 编辑
 * 流的合法性前提）。安装预设取 Ground（默认地面——MDL-22 未配置路径
 * 的同一缺省语义）。
 *
 * @return 初始工作集（成功前置——失败属 harness 装配错误，fail-fast）
 *
 * @throws std::logic_error 若模板创建意外失败（runtime::Expected 错误侧
 *         取值违约——模板目录静态登记 generic-6r 恒在，理论不可达）
 */
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        sdurws::ird::runtime::InstallationPresetToken::Ground, "demo", diags);
    // 成功前置：generic-6r 在静态模板清单内恒可创建（TemplateTest 同款
    // 前置起步）；诊断列表仅携带提示级信息，不影响本路径。
    return outcome.get();
}

/**
 * @brief 构造就绪条演示报告（三组各一条＋note 一条）。
 *
 * ★ 为什么手填而不跑真实校验：ModelReadinessChecker 需要 AssertionSuite
 * （行程评估器＋名称上下文——L5 装配注入面，见 Readiness.hpp 类注）。
 * harness 链接面与插件目标保持同源（modeling＋ui＋Qt，零 policy/runtime）
 * 以免扩散业务边；就绪判定的正确性由 ReadinessTest 真实覆盖，本报告只
 * 验证**呈现面**（分组计数/三要素/定位行）。码面取 DiagCodes 已注册码
 * 字面（不臆造码——T08 纪律）。
 *
 * 确定性：全部字段字面常量（NFR-COR-02——每次启动呈现一致，便于对照）。
 */
ModelReadinessReport makeSampleReport()
{
    ModelReadinessReport report;

    // blocker 一条：行程上限比较（MDL-06）——实际 42 N·m 超 40 N·m 阈值。
    // comparison 三要素齐全（实际/阈值/单位 N·m——UX-03），subject 锚在
    // 关节对象上供"定位跳转"演示。
    sdurws::ird::core::ComparativeFields cmp;
    cmp.actual.quantity = sdurws::ird::core::SourcedValue<double>::provided(
        42.0, sdurws::ird::core::ValueProvenance::make(
                  sdurws::ird::core::ProvenanceKind::UserProvided));
    cmp.actual.unit = sdurws::ird::core::UnitToken::find("N*m").value();
    cmp.expected.quantity = sdurws::ird::core::SourcedValue<double>::provided(
        40.0, sdurws::ird::core::ValueProvenance::make(
                  sdurws::ird::core::ProvenanceKind::UserProvided));
    cmp.expected.unit = sdurws::ird::core::UnitToken::find("N*m").value();
    report.blockers.push_back(sdurws::ird::core::DiagnosticRecord::make(
        sdurws::ird::core::DiagCode(sdurws::ird::modeling::kMdl06TravelLimit),
        fixedOid(1), std::string("j0"), std::nullopt,
        std::string("行程上限"), std::string("实际行程超过阈值"),
        std::string("确认或修正"), cmp));

    // warning 一条（无 subject——验证"不可定位行"的呈现降级）。
    report.warnings.push_back(sdurws::ird::core::DiagnosticRecord::make(
        sdurws::ird::core::DiagCode(
            sdurws::ird::modeling::kMdlReadinessResourceState),
        std::nullopt, std::nullopt, std::nullopt,
        std::string("资源状态"), std::string("网格资源未固化"),
        std::string("执行固化或生成占位几何"), std::nullopt));

    // confirmable 一条（C-1 契约：comparison 必在——ConfirmableFinding
    // 工厂会校验；内容与 blocker 同源＝"待用户裁决"语义）。
    report.confirmables.push_back(sdurws::ird::core::ConfirmableFinding::make(
        report.blockers.front()));

    // note 一条：L5 层非阻断预告（DataInsufficient 语义的呈现样例）。
    ReadinessNote note;
    note.layer = sdurws::ird::modeling::ReadinessLayer::L5InertiaPhysical;
    note.subjectPath = "links[1].body";
    note.summary = "连杆 2 物性未提供（DataInsufficient 预告）";
    note.blocking = false;
    report.notes.push_back(note);

    return report;
}

// ---- 迁移演示替身族（WP-13-T20 GUI 冒烟——L5 端口的最小演示实现）------
// 真值边界：名称解析/运行时名映射的权威归 runtime RuntimeNameMap（R-4/
// SA-05）——本族是端口缝隙的演示绑定（"J1../L1.."演示映射），不复制解析
// 语义；真实绑定随 UI-T20 宿主运行时发布桥在装配后的宿主中进行。

/// 显示名解析（IUiNameResolver）：树/检查器渲染显示名——演示映射为
/// 工作集 localName（UX-02 呈现口径；解析失败占位由面板处理）。
class HarnessNameResolver final : public sdurws::ird::ui::IUiNameResolver {
public:
    std::map<std::string, std::string> byId;  ///< ObjectId 规范文本→localName

    std::optional<std::string> resolveObjectId(sdurws::ird::core::ObjectId id) const override
    {
        const auto it = byId.find(id.toCanonical());
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/// 运行时名映射（IUiRuntimeNameMapPort）：SA-05 双射的演示绑定——
/// 关节 "J1..J6"、连杆 "L1..L7"（反解/正向同表——同源纪律）。
class HarnessNameMap final : public sdurws::ird::ui::IUiRuntimeNameMapPort {
public:
    std::map<std::string, sdurws::ird::core::ObjectId> byName;  ///< 反解向
    std::map<sdurws::ird::core::ObjectId, std::string> byId;    ///< 正向向

    std::optional<sdurws::ird::core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it != byName.end()
                   ? std::optional<sdurws::ird::core::ObjectId>{it->second}
                   : std::nullopt;
    }
    std::optional<std::string> resolveRuntimeName(
        const sdurws::ird::core::ObjectId& id) const override
    {
        const auto it = byId.find(id);
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/// 三维高亮出口（IUiHighlightOutlet）：harness 无三维视图——高亮动作
/// 以状态行＋控制台回显（L2 动作出线的可观测演示，不虚构三维呈现）。
class HarnessHighlightOutlet final : public sdurws::ird::ui::IUiHighlightOutlet {
public:
    QStatusBar* status = nullptr;  ///< 回显落点（窗口构造后接线；可空＝仅控制台）

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        std::cout << "[highlight] " << runtimeName << std::endl;
        if (status != nullptr) {
            status->showMessage(
                QString("三维高亮：%1（演示出口——真实呈现归宿主 RWStudioView3D）")
                    .arg(QString::fromStdString(runtimeName)));
        }
    }
    void clearHighlight() override
    {
        std::cout << "[highlight] clear" << std::endl;
        if (status != nullptr) {
            status->showMessage(QStringLiteral("三维高亮：清除"));
        }
    }
};

/**
 * @brief 选中回声观察者（harness 刷新编排面）：检查器协议约定"模型刷新
 *        后由装配层调用 refresh()（不自动监听——刷新时机归装配层编排）"
 *        ——本观察者即该装配半区：每次选中变更后驱动检查器面板重渲染。
 */
class PanelRefreshEcho final : public sdurws::ird::ui::IUiSelectionObserver {
public:
    std::function<void()> refresh;  ///< 面板重渲染动作（面板创建后接线）

    void onSelectionChanged(const sdurws::ird::ui::SelectionChange&) override
    {
        if (refresh) { refresh(); }
    }
};

}  // namespace

/**
 * @brief harness 入口——装配最小会话并进入 GUI 事件循环。
 *
 * 装配序列（对应产品装配层 CentralAreaHost 的最小化形）：
 *   ①创建真实六轴草稿（会话权威工作集）→②构造面板（可写初值 true）→
 *   ③注入编辑目标提供器（现取零缓存——ACC5）与命令提交出口（回显）→
 *   ④全面板首刷（事件驱动的刷新出口同款入口）→⑤主窗口承载＋事件循环。
 *
 * @return 0 正常退出；1 装配失败（控制台输出原因——环境/构建问题）
 */
int main(int argc, char* argv[])
{
    try {
        // Qt Widgets 应用基座；org/app 名仅影响 QSettings 缺省位置（本
        // harness 不落任何设置——布局记忆等产品语义归 ui 壳，不在本面）。
        QApplication app(argc, argv);
        QApplication::setOrganizationName("sdurws");
        QApplication::setApplicationName("ird-modeling-harness");

        // ①会话权威工作集：main 栈上自持（产品中归 ModuleSessionState/
        // 装配层——同一"UI 线程单例权威"语义，§3.4）。面板经提供器现取
        // 该指针，绝不持有副本（ACC5：面板零缓存纪律的装配侧保障）。
        ModelingWorkingSet ws = makeSixAxisDraft();

        // ②五区面板（初始可写——L-7 门控初值；只读切换见下方工具条）。
        ModelingPanelWidget panel(true);

        // ③a 编辑目标提供器：每次编辑提交现取权威工作集指针（L-2 的
        // 数据前提）。捕获引用——ws 存活期覆盖 app.exec() 全程（main
        // 栈序保证：panel 先于 ws 析构之前不再接收事件）。
        panel.setEditTargetProvider([&ws]() -> ModelingWorkingSet* {
            return &ws;
        });

        // ③b 命令提交出口：产品装配层此处绑定 ui CommandRegistry.submit
        // （最终进 project 命令管线——§9.7.2 L-3）。装配前的 harness 以
        // 回显代替：命令 id 打到控制台＋窗口状态行，验证"目录→激活→
        // 转发"链路连通；draft.apply 等提交语义的完整验证在装配后进行
        // （诚实边界——不虚构提交成功）。
        QLabel* lastCommand = nullptr;  // 状态行标签（窗口构造后接线）
        panel.setCommandSubmit(
            [&lastCommand](const sdurws::ird::ui::CommandId& id) {
                std::cout << "[command] " << id << std::endl;
                if (lastCommand != nullptr) {
                    lastCommand->setText(
                        QString("最近命令：%1（harness 回显，未提交管线）")
                            .arg(QString::fromStdString(id)));
                }
            });

        // ④首刷：就绪条取演示报告（真值边界见文件头）；树/属性区自真实
        // 工作集投影。此后刷新由面板编辑流内驱动（L-2 接受分支增量刷新）。
        const ModelReadinessReport report = makeSampleReport();
        panel.refreshPanel(ws, report);

        // ⑤主窗口承载：工具条提供两个手动验证开关——可写性（L-7）与
        // 最近命令回显；状态行登记 harness 的诚实边界，防止误当作产品
        // 宿主形态。
        QMainWindow window;
        window.setWindowTitle(
            QStringLiteral("建模面板开发验证 harness（sdurws_ird_modeling_app）"));
        window.setCentralWidget(&panel);

        QToolBar* toolbar = window.addToolBar(QStringLiteral("验证开关"));
        toolbar->setMovable(false);  // 固定位形——harness 无布局记忆语义
        QCheckBox* writableBox = new QCheckBox(QStringLiteral("可写"), &window);
        writableBox->setChecked(true);  // 与面板初值 true 同源
        toolbar->addWidget(writableBox);
        // L-7 只读门控：去勾后编辑控件禁用、域命令 readOnlyAllowed=false
        // （面板内 applyReadOnlyGate 同一事实源——§9.7.2 L-7 行）。
        QObject::connect(writableBox, &QCheckBox::toggled,
                         &panel, &ModelingPanelWidget::setWritable);

        lastCommand = new QLabel(
            QStringLiteral("最近命令：（尚未激活）"), &window);
        toolbar->addWidget(lastCommand);

        // ⑥宿主迁移演示装配（WP-13-T20——B1-SPEC §5.1 三接入面的最小
        // 宿主形态）：共享工业项目树＋共享属性检查器＋选择服务。真值
        // 边界见上方替身族注释（名称映射为演示绑定；L2 高亮以状态行
        // 回显；本演示不触 Dock 拓扑的产品装配——宿主挂位归 UI-T23/
        // WP-24-T08，O-43）。
        namespace ui = sdurws::ird::ui;

        // ⑥a 域三接入面（Deps 绑定 harness 会话与面板——面板即编辑
        // 分流出口，focusObject 即高亮/激活执行器）。
        ModelingSharedSurfaceDeps migrationDeps;
        migrationDeps.workingSet = [&ws]() -> ModelingWorkingSet* { return &ws; };
        migrationDeps.editSink = &panel;
        migrationDeps.panelHighlight = [&panel](
            const std::optional<sdurws::ird::core::ObjectId>& oid) {
            panel.focusObject(oid);  // 树选→域面板高亮/定位（下行联动）
        };
        migrationDeps.complexPageActivator = [&panel](
            const sdurws::ird::core::ObjectId& oid) {
            panel.focusObject(oid);  // D6 域自持打开＝定位目标对象
        };
        auto treeProvider =
            std::make_shared<sdurws::ird::modeling::ModelingTreeNodesProvider>(
                migrationDeps);
        auto pagesProvider =
            std::make_shared<sdurws::ird::modeling::ModelingPropertyPagesProvider>(
                migrationDeps);

        // ⑥b 共享模型＋选择服务（UI-T21/T22 冻结面；演示映射填充）。
        ui::ProjectTreeModel treeModel;
        treeModel.addProvider(treeProvider);

        auto resolver = std::make_shared<HarnessNameResolver>();
        auto nameMap = std::make_shared<HarnessNameMap>();
        for (std::size_t i = 0; i < ws.design.joints.size(); ++i) {
            const std::string runtime = "J" + std::to_string(i + 1);
            nameMap->byName[runtime] = ws.design.joints[i].objectId;
            nameMap->byId[ws.design.joints[i].objectId] = runtime;
            resolver->byId[ws.design.joints[i].objectId.toCanonical()]
                = ws.design.joints[i].localName;
        }
        for (std::size_t i = 0; i < ws.design.links.size(); ++i) {
            const std::string runtime = "L" + std::to_string(i + 1);
            nameMap->byName[runtime] = ws.design.links[i].objectId;
            nameMap->byId[ws.design.links[i].objectId] = runtime;
            resolver->byId[ws.design.links[i].objectId.toCanonical()]
                = ws.design.links[i].localName;
        }

        HarnessHighlightOutlet highlightOutlet;  // 状态行回显（窗口构造后接线）
        std::function<bool(const sdurws::ird::core::ObjectId&)> locateFn;  // 树定位延迟接线
        ui::SelectionService::Deps selectionDeps;
        selectionDeps.nameMap = nameMap;
        selectionDeps.treeLocator =
            [&locateFn](const sdurws::ird::core::ObjectId& oid) {
                return locateFn ? locateFn(oid) : false;  // 面板创建后接线
            };
        selectionDeps.highlightOutlet = std::shared_ptr<ui::IUiHighlightOutlet>(
            &highlightOutlet, [](ui::IUiHighlightOutlet*) {});  // 非 owning 共享
        ui::SelectionService selection(selectionDeps);

        ui::PropertyInspectorModel inspectorModel;
        inspectorModel.addProvider(pagesProvider);
        auto inspectorSubscription = selection.subscribe(inspectorModel);  // L1 刷新
        // 检查器面板刷新编排半区（订阅序在模型之后——模型先更新、面板
        // 后重渲染；面板创建后接线 refresh 动作）。
        PanelRefreshEcho inspectorEcho;
        auto echoSubscription = selection.subscribe(inspectorEcho);

        sdurws::ird::modeling::ModelingSelectionAdapter selectionAdapter(
            migrationDeps);
        selectionAdapter.attach(selection);

        // ⑥c 共享面板（Qt 渲染半区——UI-T21/T22 工厂；挂宿主 Dock）。
        ui::IndustrialProjectTreePanelDeps treePanelDeps;
        treePanelDeps.model =
            std::shared_ptr<ui::ProjectTreeModel>(&treeModel, [](ui::ProjectTreeModel*) {});
        treePanelDeps.selection = std::shared_ptr<ui::SelectionService>(
            &selection, [](ui::SelectionService*) {});
        treePanelDeps.nameResolver = resolver;
        auto treePanel = ui::createIndustrialProjectTreePanel(treePanelDeps, nullptr);

        ui::PropertyInspectorPanelDeps inspectorPanelDeps;
        inspectorPanelDeps.model = std::shared_ptr<ui::PropertyInspectorModel>(
            &inspectorModel, [](ui::PropertyInspectorModel*) {});
        inspectorPanelDeps.nameResolver = resolver;
        auto inspectorPanel = ui::createPropertyInspectorPanel(inspectorPanelDeps, nullptr);

        // 树定位延迟接线（L3 反解成功的落点＝共享树面板定位选中）。
        locateFn = [&treePanel](const sdurws::ird::core::ObjectId& oid) {
            return treePanel->locateAndHighlight(oid);
        };

        // 检查器刷新编排第二段接线（面板已创建——每次选中变更后重渲染）。
        inspectorEcho.refresh = [&inspectorPanel]() { inspectorPanel->refresh(); };

        // 共享树重建＋双面板首刷（编辑后刷新同款入口——装配层编排形）。
        const auto refreshSharedSurfaces = [&]() {
            const ui::TreeRebuildReport r = treeModel.rebuild();
            if (!r.ok) { std::cout << "[tree] rebuild rejected: " << r.reason << std::endl; }
            treePanel->refresh();
            // 检查器重组装（选中未变而内容已变——按当前选中重询问，
            // harness 刷新编排面；生产路径随域修订事件驱动）。
            ui::SelectionChange current;
            current.selectedObjectIds = selection.selectedObjectIds();
            current.source = selection.selectionSource().value_or(
                ui::SelectionSource::ProjectTree);
            inspectorModel.onSelectionChanged(current);
            inspectorPanel->refresh();
        };
        refreshSharedSurfaces();

        // 编辑后动作：共享面同步（L-2 接受→树/检查器即见新值——迁移
        // 双形态并存的刷新闭环）。singleShot(0) 队列化——编辑回调栈内
        // 不重入检查器面板的确认流（回调返回后再刷新，零重入风险）。
        panel.setPostEditAction([&panel, &ws, &report, &refreshSharedSurfaces]() {
            QTimer::singleShot(0, &panel, [&panel, &ws, &report,
                                            &refreshSharedSurfaces]() {
                refreshSharedSurfaces();
                panel.refreshPanel(ws, report);  // 自持面板同步（演示报告不变——判定面归 ReadinessTest）
            });
        });

        QDockWidget* treeDock = new QDockWidget(QStringLiteral("工业项目树（共享·WP-13-T20 演示）"), &window);
        treeDock->setWidget(treePanel->widget());
        window.addDockWidget(Qt::LeftDockWidgetArea, treeDock);
        QDockWidget* inspectorDock = new QDockWidget(QStringLiteral("属性检查器（共享·WP-13-T20 演示）"), &window);
        inspectorDock->setWidget(inspectorPanel->widget());
        inspectorDock->setMinimumWidth(320);  // 字段呈现宽度下限（防 Dock 挤压成不可读）
        window.addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

        // L3 反解演示按钮：模拟官方 TreeView Select Frame 事件（J2）——
        // 反解→树定位选中→检查器刷新全链； World.UnknownFrame 触发反解
        // 失败分支（树不动＋状态行提示）。
        QPushButton* l3Button =
            new QPushButton(QStringLiteral("模拟 TreeView 选中 J2（L3 反解）"), &window);
        toolbar->addWidget(l3Button);
        QObject::connect(l3Button, &QPushButton::clicked, &window, [&selection]() {
            selection.handleTreeViewFrameSelected("J2");
        });
        QPushButton* l3MissButton =
            new QPushButton(QStringLiteral("模拟反解失败"), &window);
        toolbar->addWidget(l3MissButton);
        QObject::connect(l3MissButton, &QPushButton::clicked, &window, [&selection]() {
            selection.handleTreeViewFrameSelected("World.UnknownFrame");
        });

        window.statusBar()->showMessage(QStringLiteral(
            "开发期 harness：命令仅回显（L-3 提交/确认流待装配）；就绪条为演示条目；"
            "迁移演示的名称映射/高亮为演示绑定（解析权威归 runtime——R-4）"));
        window.resize(1280, 860);
        highlightOutlet.status = window.statusBar();  // L2 回显落点接线
        window.show();

        return app.exec();
    } catch (const std::exception& err) {
        // 装配失败属环境/构建问题（模板创建不可达失败、Qt 平台插件缺失
        // 等）——fail-fast 带原因退出，不带病进入半初始化界面。
        std::cerr << "[harness] 装配失败: " << err.what() << std::endl;
        return 1;
    }
}
