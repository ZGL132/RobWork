/**
 * @file   HarnessMain.cpp
 * @brief  kinematics 插件面板开发验证 harness（sdurws_ird_kinematics_app）
 *         ——“插件开发完成即可手动 GUI 验证”的 kinematics 侧载体
 *         （DTB §5.1 v0.20 插件 GUI 手动验证通道；先例＝ui
 *         sdurws_ird_ui_app〔UI-T15〕、modeling sdurws_ird_modeling_app
 *         〔WP-13-T15 增量〕、requirements sdurws_ird_requirements_app
 *         〔WP-14-T10 增量〕）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（四面板/十二条数据流/域命令清单）、§3.2
 *     （_plugin 落位）；DTB §5.1（构建约定——插件完成定义含 GUI 手动
 *     验证通道，owner 指示 2026-09-26 增补；DoD 第 5 条）；
 *   - 任务契约 tasks/foundation/WP-15-T12.json acceptance 1（界面链路）；
 *     tasks/foundation/WP-15-T18.json acceptance 1/4（v1.2——宿主迁移三
 *     接入面接入＋迁移链路 v1 口径）、acceptance 2/3（Jog/Playback 会话
 *     姿态承接——D8/D9）；
 *   - DTB §4.2 O-44（接入面 v1 诚实边界——本域树面恒空集/页面无应答，
 *     结构接缝注册即落位证明）。
 *
 * ★ 与产品装配层的关系（同 ui_app/modeling_app 先例口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层创建与持有的
 *   产品契约不变（§10.9）；本 harness 在进程内扮演装配层的最小角色：经
 *   装配门面创建模块与面板，注入最小服务缝（脚本化后台缝＋命令提交
 *   回显），把真实面板、真实装配描述符与真实域编排跑起来供人工验证；
 *   WP-15-T18 起另接入共享工业项目树/属性检查器/选择服务（UI-T21/T22
 *   冻结面——requirements 先例同款迁移演示装配），产品宿主挂位归
 *   UI-T23/WP-24-T08。
 *
 * 真值边界（诚实声明，防误读验证结论）：
 *   - 真实部分：真实装配门面（模块＋描述符＋八条域命令）、L-K4/5/7/8/
 *     11 的完整面板链路、L-K3/6 的提交编排与只读门控、L-K12 的迟到判定
 *     （noteBackgroundResult 真实消费）、三接入面（真实 Provider/Adapter
 *     ＋真实共享模型）、Jog/Playback 承接缝（真实 KinSessionPose 写点）；
 *   - 演示部分：后台缝同步受理（真实 execution 派发归装配层）；模型视图
 *     /求解器未接线（位姿指标/单点求解以“视图未装配”降级态如实呈现）；
 *     任务点行为 harness 演示投影（真值归 requirements/装配层——零写回）；
 *   - 不覆盖：results 归档投影/ProjectCommandService 真提交（命令出口
 *     以回显承载——不虚构修订语义）；本域树面对象（v1 恒空集——O-44，
 *     共享树不出现 kinematics 节点是诚实形态而非缺陷）。
 *
 * 用法（开发期交互验证）：
 *   sdurws_ird_kinematics_app
 *     启动即见四面板 Tab（中央）＋求解配置高级面板（右 Dock）＋共享工业
 *     项目树/属性检查器（左/右 Dock，迁移演示挂位）；启动序列已演示：
 *     树重建（本域零节点——O-44 诚实边界）、任务点选择→任务点表高亮
 *     （选择服务下行联动）、Jog 承接（会话姿态写入零修订）。手动可验：
 *     树选中演示点看面板联动；配置面板改数值验 L-K7 提示流。
 *
 * 线程模型：单线程——QApplication/会话态/面板全部 main 线程（§3.4）。
 */

#include <QApplication>
#include <QDebug>
#include <QDockWidget>
#include <QMainWindow>
#include <QWidget>

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>            // core::ObjectId（演示点身份）
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // 装配门面（产品入口）
#include <sdurws/ird/ui/IndustrialProjectTree.hpp> // 共享工业项目树（UI-T21 冻结面）
#include <sdurws/ird/ui/PropertyInspector.hpp>     // 共享属性检查器（UI-T22 冻结面）
#include <sdurws/ird/ui/SelectionService.hpp>      // 选择服务（UI-T21 冻结面）
#include <sdurws/ird/ui/UiPorts.hpp>               // ui::IUiNameResolver（显示名解析端口）
#include <sdurws/ird/ui/UiTypes.hpp>               // ui::CommandId/PanelRegistration

#include "plugin/KinPanelFlows.hpp"                // noteBackgroundResult（L-K12 真实消费）
#include "plugin/KinPanelTypes.hpp"                // 会话态/服务缝值（同单元私有头）

using sdurws::ird::kinematics::KinBackgroundAck;
using sdurws::ird::kinematics::KinBackgroundKind;
using sdurws::ird::kinematics::KinBackgroundRequest;
using sdurws::ird::kinematics::KinBackgroundResultNote;
using sdurws::ird::kinematics::KinBackgroundSubmitFn;
using sdurws::ird::kinematics::KinematicsPluginAssembly;
using sdurws::ird::kinematics::KinModuleSessionState;
using sdurws::ird::kinematics::KinTaskPointRow;
using sdurws::ird::kinematics::createKinematicsPluginAssembly;
using sdurws::ird::kinematics::noteBackgroundResult;

namespace {

/// 后台提交计数（harness 本地任务编号锚——UI 线程单线程无竞争）。
int g_backgroundSeq = 0;

/**
 * @brief harness 后台提交缝（脚本化受理——真实 execution 派发归装配层）。
 *
 * 受理即回执（本地编号如实标注“非 execution 派发”）；完成通知经投递槽
 * 转发（槽由面板创建后填入——受理先于面板场景下的通知安全降级为丢弃）。
 *
 * @param resultRoute [in] 完成通知投递槽（共享持有——面板创建后填入）
 * @return 提交缝（恒受理——演示通道；拒绝路径由只读门控/空缝承载）
 */
KinBackgroundSubmitFn makeHarnessBackgroundSink(
    const std::shared_ptr<std::function<void(const KinBackgroundResultNote&)>>& resultRoute)
{
    return [resultRoute](const KinBackgroundRequest& request) {
        KinBackgroundAck ack;
        ack.accepted = true;
        ack.taskRef = "harness-local-" + std::to_string(++g_backgroundSeq);
        ack.reason = "harness 本地受理（非 execution 派发）";
        if (resultRoute != nullptr && *resultRoute != nullptr) {
            // 同步完成回执（演示语义——真实通道经 results 投影异步刷新；
            // 摘要不含素材计数：正式素材归 results 归档，此处零虚构）。
            KinBackgroundResultNote note;
            note.acceptedEpoch = request.epoch;
            note.kind = request.kind;
            note.interrupted = false;
            note.summaryText = "后台任务完成：" + ack.taskRef
                               + "（正式素材经 results 归档——本 harness 不产）";
            (*resultRoute)(note);
        }
        return ack;
    };
}

/// 显示名解析替身（IUiNameResolver——树/检查器渲染名称现取的演示映射）。
class HarnessNameResolver final : public sdurws::ird::ui::IUiNameResolver {
public:
    std::map<std::string, std::string> byId;  ///< ObjectId 规范文本→显示名

    std::optional<std::string> resolveObjectId(
        sdurws::ird::core::ObjectId id) const override
    {
        const auto it = byId.find(id.toCanonical());
        return it != byId.end() ? std::optional<std::string>{it->second}
                                : std::nullopt;
    }
};

/// 运行时名映射替身（IUiRuntimeNameMapPort——恒空：harness 无已应用
/// WorkCell，L2/L3 反例分支是诚实形态，不伪造已应用对象）。
class HarnessNameMap final : public sdurws::ird::ui::IUiRuntimeNameMapPort {
public:
    std::optional<sdurws::ird::core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string&) const override
    {
        return std::nullopt;
    }

    std::optional<std::string> resolveRuntimeName(
        const sdurws::ird::core::ObjectId&) const override
    {
        return std::nullopt;
    }
};

}  // namespace

/**
 * @brief harness 入口（QApplication＋装配门面＋双面板挂位＋迁移演示装配）。
 *
 * @param argc [in] Qt 参数计数
 * @param argv [in] Qt 参数向量
 * @return 进程退出码（0＝正常退出）
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QObject::tr("kinematics 插件验证 harness（WP-15-T12/T18）"));

    // ---- 装配门面（产品入口——真实模块＋真实描述符＋八条域命令）。
    KinematicsPluginAssembly bundle = createKinematicsPluginAssembly();

    // ---- 会话事实注入（快照绑定留空＝无项目语境；配置基线必须合法——
    //      AnalysisConfiguration 默认 seed=0 非法（I-KIN-4），装配纪律
    //      要求会话基线恒过 validateAnalysisConfiguration）。
    KinModuleSessionState& session = bundle.session();
    session.savedConfig.seed = 1;
    session.savedConfig.regionBudget.seed = 1;
    session.writable = true;

    // ---- 会话姿态容器（Jog/Playback 承接缝的宿主侧载体——D8/D9：宿主
    //      State 变化经 applyHostJointState 写入，零修订/零失效/零缓存）。
    sdurws::ird::kinematics::KinSessionPose sessionPose;

    // ---- 演示任务点投影（真值归 requirements/装配层——harness 以演示
    //      行承载“任务点选择→结果面板高亮”链路的可视面；零写回入口）。
    const sdurws::ird::core::ObjectId demoPointA = sdurws::ird::core::ObjectId::generate();
    const sdurws::ird::core::ObjectId demoPointB = sdurws::ird::core::ObjectId::generate();
    const std::vector<KinTaskPointRow> demoPoints = [demoPointA, demoPointB]() {
        KinTaskPointRow a;
        a.pointOid = demoPointA;
        a.label = "演示任务点 P1";
        a.enabled = true;
        a.outcomeText = "3 解/残差 0.2 mm（演示投影）";
        a.requiredCoverageDone = true;
        KinTaskPointRow b;
        b.pointOid = demoPointB;
        b.label = "演示任务点 P2";
        b.enabled = true;
        b.outcomeText = "未计算（演示投影）";
        return std::vector<KinTaskPointRow>{a, b};
    }();

    // ---- 服务缝（后台缝＝脚本化受理；任务点＝演示投影；会话姿态＝宿主
    //      承接载体；完成通知经投递槽——面板创建后填入；其余缝留空＝
    //      降级语义如实呈现，见文件头真值边界）。
    sdurws::ird::kinematics::KinPanelServices services;
    auto resultRoute =
        std::make_shared<std::function<void(const KinBackgroundResultNote&)>>();
    services.backgroundSubmit = makeHarnessBackgroundSink(resultRoute);
    services.taskPoints = [demoPoints]() { return demoPoints; };
    services.sessionPose = &sessionPose;
    // 服务缝注入（面板创建前的装配接线点——setServices 转发模块内部）。
    bundle.setServices(services);

    // ---- 绑定（宿主装配层标准用法——assembly 公共头消费序①②）。
    bundle.bindCommandSubmit([](const sdurws::ird::ui::CommandId& id) {
        // 命令提交出口（状态回显——真实 registry 接线归装配收口，不虚构
        // 提交语义）。
        qDebug("kinematics command submit: %s", id.c_str());
    });
    bundle.bindTextResolver(
        [](const std::string& titleKey) { return QString::fromStdString(titleKey); });

    // ---- 面板挂位（descriptor.panels 工厂现调——主面板中央位＋高级面板
    //      右 Dock＝UX-04 高级面板位演示；工厂归调用方接管所有权；高级
    //      面板即 D6 求解配置收口位——acceptance 4 v1“经面板直达”落点）。
    QWidget* mainPanel = nullptr;
    for (const sdurws::ird::ui::PanelRegistration& p : bundle.descriptor.panels) {
        QWidget* w = p.factory();
        if (!p.advanced) {
            mainPanel = w;  // 主面板（四面板合一 Tab）→中央
        } else {
            auto* dock = new QDockWidget(QObject::tr("求解配置（高级）"), &window);
            dock->setWidget(w);
            window.addDockWidget(Qt::RightDockWidgetArea, dock);
        }
    }
    if (mainPanel != nullptr) {
        window.setCentralWidget(mainPanel);
    }

    // ---- 完成通知投递槽填入（面板就绪后——L-K12 noteBackgroundResult
    //      真实消费：迟到判定/中断如实/状态行呈现）。
    KinModuleSessionState* sessionPtr = &session;
    QWidget* statusTarget = mainPanel;
    *resultRoute = [sessionPtr, statusTarget](const KinBackgroundResultNote& note) {
        const std::string text = noteBackgroundResult(*sessionPtr, note);
        if (statusTarget != nullptr) {
            statusTarget->setProperty("statusText", QString::fromStdString(text));
        }
        qDebug("kinematics harness: %s", text.c_str());
    };

    // ---- 迁移演示装配（WP-15-T18——B1-SPEC §5.1 三接入面的最小宿主形态）：
    //      共享工业项目树＋共享属性检查器＋选择服务（requirements 先例
    //      RequirementsHarnessMain 同款；产品宿主挂位归 UI-T23/WP-24-T08）。
    auto handles = bundle.sharedSurfaceProviders();
    const auto domainKey = handles.treeNodes->domainKey();
    auto resolver = std::make_shared<HarnessNameResolver>();
    auto nameMap = std::make_shared<HarnessNameMap>();
    resolver->byId[demoPointA.toCanonical()] = "演示任务点 P1";

    sdurws::ird::ui::ProjectTreeModel treeModel;
    treeModel.addProvider(handles.treeNodes);
    sdurws::ird::ui::PropertyInspectorModel inspectorModel;
    inspectorModel.addProvider(handles.propertyPages);

    // 树定位延迟接线（L3 反解成功的落点＝共享树面板定位选中）。
    std::function<bool(const sdurws::ird::core::ObjectId&)> locateFn =
        [](const sdurws::ird::core::ObjectId&) { return false; };  // 面板创建后重绑
    sdurws::ird::ui::SelectionService selection(sdurws::ird::ui::SelectionService::Deps{
        nameMap, [&locateFn](const sdurws::ird::core::ObjectId& oid) { return locateFn(oid); },
        nullptr, nullptr});

    // 共享树/检查器面板（工厂——R-2 封闭实现；deps shared 非 owning）。
    sdurws::ird::ui::IndustrialProjectTreePanelDeps treePanelDeps;
    treePanelDeps.model = std::shared_ptr<sdurws::ird::ui::ProjectTreeModel>(
        &treeModel, [](sdurws::ird::ui::ProjectTreeModel*) {});
    treePanelDeps.selection = std::shared_ptr<sdurws::ird::ui::SelectionService>(
        &selection, [](sdurws::ird::ui::SelectionService*) {});
    treePanelDeps.nameResolver = resolver;
    auto treePanel = sdurws::ird::ui::createIndustrialProjectTreePanel(treePanelDeps, nullptr);

    sdurws::ird::ui::PropertyInspectorPanelDeps inspectorPanelDeps;
    inspectorPanelDeps.model = std::shared_ptr<sdurws::ird::ui::PropertyInspectorModel>(
        &inspectorModel, [](sdurws::ird::ui::PropertyInspectorModel*) {});
    inspectorPanelDeps.nameResolver = resolver;
    auto inspectorPanel = sdurws::ird::ui::createPropertyInspectorPanel(inspectorPanelDeps, nullptr);

    // 树定位重绑（L3 落点）；检查器订阅服务（L1）；适配器经门面接线
    // （下行联动——“树选任务点→结果面板高亮”）。
    locateFn = [&treePanel](const sdurws::ird::core::ObjectId& oid) {
        return treePanel->locateAndHighlight(oid);
    };
    bundle.attachSelectionService(selection);
    auto inspectorSub = selection.subscribe(inspectorModel);

    // 共享面刷新编排（装配层编排形：重建→双面板刷新→检查器按当前选中
    // 重询问）。
    const auto refreshSharedSurfaces = [&]() {
        const sdurws::ird::ui::TreeRebuildReport r = treeModel.rebuild();
        std::cout << "[migration-smoke] tree rebuild ok=" << (r.ok ? 1 : 0)
                  << " nodeCount=" << r.nodeCount
                  << "（kinematics v1 恒 0——O-44 诚实边界，非缺陷）" << std::endl;
        treePanel->refresh();
        sdurws::ird::ui::SelectionChange current;
        current.selectedObjectIds = selection.selectedObjectIds();
        current.source = selection.selectionSource().value_or(
            sdurws::ird::ui::SelectionSource::ProjectTree);
        inspectorModel.onSelectionChanged(current);
        inspectorPanel->refresh();
    };

    // ---- Dock 挂位（演示形态——产品宿主挂位归 UI-T23/WP-24-T08，O-43）。
    QDockWidget* treeDock = new QDockWidget(
        QStringLiteral("工业项目树（共享·WP-15-T18 演示）"), &window);
    treeDock->setWidget(treePanel->widget());
    window.addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    QDockWidget* inspectorDock = new QDockWidget(
        QStringLiteral("属性检查器（共享·WP-15-T18 演示）"), &window);
    inspectorDock->setWidget(inspectorPanel->widget());
    window.addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    window.resize(1400, 860);

    // ---- 迁移冒烟序列（启动演示——GUI 呈现后人工可复演）。
    refreshSharedSurfaces();
    std::cout << "[migration-smoke] domainKey=" << domainKey
              << "（三接入面注册落位证明）" << std::endl;
    // 下行联动演示：选择服务选中演示任务点 P1 → 适配器 → 面板任务点表
    // 定位/高亮（切到任务点页——acceptance 4 v1 链路第二棒可视呈现）。
    selection.selectBusiness({demoPointA}, sdurws::ird::ui::SelectionSource::Command);
    std::cout << "[migration-smoke] 下行联动：任务点 P1 选中→面板高亮（任务点页）"
              << std::endl;
    // Jog 承接演示（D8——关节点动经宿主 State 桥的域侧入口写入会话姿态；
    // 零修订/零失效/零缓存——KIN-06 结构性保证，命令出口零触发）。
    const bool jogApplied = bundle.applyHostJointState({0.1, -0.2, 0.3, 0.0});
    std::cout << "[migration-smoke] Jog 承接：applyHostJointState ok="
              << (jogApplied ? 1 : 0) << "（会话姿态已写入——零修订）" << std::endl;

    window.show();
    return app.exec();
}
