/**
 * @file   HarnessMain.cpp
 * @brief  kinematics 插件面板开发验证 harness（sdurws_ird_kinematics_app）
 *         ——"插件开发完成即可手动 GUI 验证"的 kinematics 侧载体
 *         （DTB §5.1 v0.20 插件 GUI 手动验证通道；先例＝ui
 *         sdurws_ird_ui_app〔UI-T15〕、modeling sdurws_ird_modeling_app
 *         〔WP-13-T15 增量〕）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（四面板/十二条数据流/域命令清单）、§3.2
 *     （_plugin 落位）；DTB §5.1（构建约定——插件完成定义含 GUI 手动
 *     验证通道，owner 指示 2026-09-26 增补；DoD 第 5 条）；
 *   - 任务契约 tasks/foundation/WP-15-T12.json acceptance 1（界面链路
 *     ——GUI 手动验证通道与留痕）。
 *
 * ★ 与产品装配层的关系（同 ui_app/modeling_app 先例口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层创建与持有的
 *   产品契约不变（§10.9）；本 harness 在进程内扮演装配层的最小角色：经
 *   装配门面创建模块与面板，注入最小服务缝（脚本化后台缝＋命令提交
 *   回显），把真实面板、真实装配描述符与真实域编排跑起来供人工验证。
 *   真实 execution/results/io 通道归装配收口任务接线；harness 缝以
 *   "本地受理并如实标注"承载（不虚构提交语义）。
 *
 * 真值边界（诚实声明，防误读验证结论）：
 *   - 真实部分：真实装配门面（模块＋描述符＋八条域命令）、L-K4/5/7/8/
 *     11 的完整面板链路、L-K3/6 的提交编排与只读门控、L-K12 的迟到判定
 *     （noteBackgroundResult 真实消费）；
 *   - 演示部分：后台缝同步受理（真实 execution 派发归装配层——任务引用
 *     为 harness 本地编号）；模型视图/求解器未接线（真实 runtime 快照
 *     归装配收口），位姿指标/单点求解以"视图未装配"降级态如实呈现；
 *   - 不覆盖：results 归档投影/ProjectCommandService 真提交（命令出口
 *     以回显承载——不虚构修订语义）。
 *
 * 用法（开发期交互验证）：
 *   sdurws_ird_kinematics_app
 *     启动即见四面板 Tab（中央）＋求解配置高级面板（右 Dock）；任务点
 *     页点"批量验证"验 L-K3（勾"只读"后按钮禁用＝L-K11）；覆盖页点
 *     "运行"验 L-K6；配置面板改数值→确认应用验 L-K7 提示与确认流；
 *     结果页命令行验 L-K9/L-K10 的缝装配降级反馈。
 *
 * 线程模型：单线程——QApplication/会话态/面板全部 main 线程（§3.4）。
 */

#include <QApplication>
#include <QDebug>
#include <QDockWidget>
#include <QMainWindow>
#include <QWidget>

#include <functional>
#include <memory>
#include <string>

#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // 装配门面（产品入口）
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
using sdurws::ird::kinematics::createKinematicsPluginAssembly;
using sdurws::ird::kinematics::noteBackgroundResult;

namespace {

/// 后台提交计数（harness 本地任务编号锚——UI 线程单线程无竞争）。
int g_backgroundSeq = 0;

/**
 * @brief harness 后台提交缝（脚本化受理——真实 execution 派发归装配层）。
 *
 * 受理即回执（本地编号如实标注"非 execution 派发"）；完成通知经投递槽
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

}  // namespace

/**
 * @brief harness 入口（QApplication＋装配门面＋双面板挂位）。
 *
 * @param argc [in] Qt 参数计数
 * @param argv [in] Qt 参数向量
 * @return 进程退出码（0＝正常退出）
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QObject::tr("kinematics 插件验证 harness（WP-15-T12）"));

    // ---- 装配门面（产品入口——真实模块＋真实描述符＋八条域命令）。
    KinematicsPluginAssembly bundle = createKinematicsPluginAssembly();

    // ---- 会话事实注入（快照绑定留空＝无项目语境；配置基线必须合法——
    //      AnalysisConfiguration 默认 seed=0 非法（I-KIN-4），装配纪律
    //      要求会话基线恒过 validateAnalysisConfiguration）。
    KinModuleSessionState& session = bundle.session();
    session.savedConfig.seed = 1;
    session.savedConfig.regionBudget.seed = 1;
    session.writable = true;

    // ---- 服务缝（后台缝＝脚本化受理；完成通知经投递槽——面板创建后
    //      填入；其余缝留空＝降级语义如实呈现，见文件头真值边界）。
    sdurws::ird::kinematics::KinPanelServices services;
    auto resultRoute =
        std::make_shared<std::function<void(const KinBackgroundResultNote&)>>();
    services.backgroundSubmit = makeHarnessBackgroundSink(resultRoute);
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
    //      右 Dock＝UX-04 高级面板位演示；工厂归调用方接管所有权）。
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

    window.resize(1200, 800);
    window.show();
    return app.exec();
}
