/**
 * @file   HarnessMain.cpp
 * @brief  selection 插件面板开发验证 harness（sdurws_ird_selection_app）——
 *         "插件开发完成即可手动 GUI 验证"的 selection 侧载体（单元卡
 *         §15.3 GUI 测试流程的宿主位；WP-19-T10）。
 *
 * 设计依据：
 *   - units/selection.md §15.3（GUI 范围——目录管理页/候选表/淘汰原因
 *     视图/回填入口的链路用例随插件任务执行；Windows GUI 约定——一次
 *     只启动一个 GUI 测试可执行文件）、§16（WP-19-T10 行）；DTB §5.1
 *     （插件 GUI 手动验证通道——先例＝kinematics_app/modeling_app/
 *     dynamics_app〔WP-17-T09〕）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json（acceptance 1——界面
 *     链路；无人值守门禁不做 GUI 运行验证：本 harness 的**构建**留痕
 *     属本任务交付，**运行**属手动验证流程，未启动如实登记不标注通过）
 *
 * ★ 与产品装配层的关系（dynamics_app 先例同款口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层创建与持有
 *   的产品契约不变（ui.md §10.9）；本 harness 在进程内扮演装配层的最
 *   小角色：经装配门面创建模块与面板，注入演示服务缝（固定演示行集
 *   ——零业务语义），把真实面板与真实装配描述符跑起来供人工验证。
 *
 * 真值边界（诚实声明，防误读验证结论）：
 *   - 真实部分：真实装配门面（描述符＋命令目录＋面板工厂）、真实
 *     面板控件链路（就绪投影/回填提交流/目录表/候选表/原因/缺口分
 *     轨呈现）、真实模型层流（L-S1~L-S5——缝消费与文案守卫）；
 *   - 演示部分：行集缝在进程内返回固定演示行（真实行集组装＝宿主
 *     装配层从选型结果翻译——本 harness 不触计算库结果路径，插件零
 *     计算红线在 harness 侧同样成立）、回填提交出口以回显承载（真实
 *     CommandRegistry 与①命令端口接线归宿主装配批次——不虚构提交
 *     语义）、文案解析器以键名回显（宿主 UiText 资源接线归装配批次）；
 *   - 不覆盖：目录导入/批量计算的后台任务链（NFR-PERF-01——执行归
 *     execution/装配层，本 harness 只呈现现成行集）。
 *
 * 用法（开发期交互验证——单元卡 §15.3 流程）：
 *   sdurws_ird_selection_app
 *     启动即见工作流页（就绪投影＋缺项＋回填入口＋最近提交）＋目录
 *     管理页（版本清单）＋候选表页（候选/原因/缺口分轨明细）。手动
 *     可验：点回填入口看受理记录与复算提示素材；切 Tab 看三页空态/
 *     行集呈现（演示行集＋键名回显）。
 *
 * 线程模型：单线程——QApplication/缝/面板全部 main 线程（卡 §3.4）。
 */

#include <QApplication>
#include <QDebug>
#include <QMainWindow>

#include <iostream>
#include <string>
#include <vector>

#include <sdurws/ird/selection/SelectionPluginAssembly.hpp> // 装配门面（产品入口）
#include "plugin/SelPanelTypes.hpp"           // SelPanelServices/呈现行 DTO
                                              //   （服务缝值——app 目标
                                              //   include 单元根，同单元
                                              //   私有头 plugin/ 直指）

using sdurws::ird::selection::createSelectionPluginAssembly;
using sdurws::ird::selection::SelCandidateRow;
using sdurws::ird::selection::SelCatalogRow;
using sdurws::ird::selection::SelGapRow;
using sdurws::ird::selection::SelPanelServices;
using sdurws::ird::selection::SelRejectionRow;

namespace {

// =====================================================================
// 演示行集（固定演示数据——零业务语义，全部值可直接读出；呈现链路
// 验证用，非产品路径）。
// =====================================================================

/// 演示目录版本行（两条——含当前锁定与未锁定各一）。
std::vector<SelCatalogRow> demoCatalogRows()
{
    std::vector<SelCatalogRow> rows;
    SelCatalogRow locked;
    locked.catalogId = "demo-catalog";      // 演示词面（企业目录 ID 形态）
    locked.version = "1.0.0";
    locked.sourceLabelKey = "plugin.selection.catalog.source.demo";
    locked.selected = true;                 // 当前锁定引用版本
    rows.push_back(locked);
    SelCatalogRow archived;
    archived.catalogId = "demo-catalog";
    archived.version = "0.9.0";
    archived.sourceLabelKey = "plugin.selection.catalog.source.demo";
    archived.selected = false;              // 历史版本（只增不删——§4.2）
    rows.push_back(archived);
    return rows;
}

/// 演示候选行（三行——可行/数据不足〔范围外标注〕/淘汰各一）。
std::vector<SelCandidateRow> demoCandidateRows()
{
    std::vector<SelCandidateRow> rows;
    SelCandidateRow feasible;
    feasible.combinationKeyLabel =
        "plugin.selection.candidate.combo-a";  // 组合呈现标签键
    feasible.verdictKey = "feasible";
    feasible.totalMassKg = 9.0;                // kg（电机 6＋减速器 3 形态）
    feasible.hasMass = true;
    feasible.minMargin = 0.25;                 // 无量纲裕量（演示值）
    feasible.hasMargin = true;
    feasible.reasonCount = 0;
    rows.push_back(feasible);
    SelCandidateRow outOfScope;
    outOfScope.combinationKeyLabel =
        "plugin.selection.candidate.combo-b";
    outOfScope.verdictKey = "data-insufficient";
    outOfScope.hasMass = false;                // 无素材——"不适用"占位呈现
    outOfScope.hasMargin = false;
    outOfScope.reasonCount = 0;
    outOfScope.noteKey = "axis-out-of-scope";  // 范围外格级标注（wp19-t08）
    rows.push_back(outOfScope);
    SelCandidateRow rejected;
    rejected.combinationKeyLabel =
        "plugin.selection.candidate.combo-c";
    rejected.verdictKey = "rejected";
    rejected.totalMassKg = 7.5;                // kg
    rejected.hasMass = true;
    rejected.minMargin = -0.4;                 // 负裕量＝超限事实如实保留
    rejected.hasMargin = true;
    rejected.reasonCount = 1;
    rows.push_back(rejected);
    return rows;
}

/// 演示淘汰原因行（一条——比较型字段全列）。
std::vector<SelRejectionRow> demoRejectionRows()
{
    std::vector<SelRejectionRow> rows;
    SelRejectionRow row;
    row.reasonKey = "gearbox-peak-torque-insufficient"; // 原因 token 词面
    row.axisLabel = "plugin.selection.axis.j2";         // 轴呈现标签键
    row.caseLabel = "plugin.selection.case.emergency";  // 工况呈现标签键
    row.actual = 220.0;                         // 实际值 N·m（演示值）
    row.required = 180.0;                       // 要求值 N·m
    row.unitToken = "N*m";                      // SI 单位词面（core 词形）
    row.thresholdSourceKey =
        "plugin.selection.threshold.catalog-field"; // 阈值来源键
    row.diagCodeText = "SEL-GEARBOX-PEAK-TORQUE-INSUFFICIENT"; // 稳定码词面
    rows.push_back(row);
    return rows;
}

/// 演示数据缺口行（一条——范围外轴缺口与格级标注同词）。
std::vector<SelGapRow> demoGapRows()
{
    std::vector<SelGapRow> rows;
    SelGapRow row;
    row.dimensionKey = "axis-out-of-scope";     // 缺口维键（wp19-t08 同词）
    row.axisLabel = "plugin.selection.axis.j3"; // 轴呈现标签键
    // caseLabel 保持默认空串——缺口与工况无关（T08 语义：轴级边界
    // 事实不绑工况；呈现侧空工况列不伪造）。
    row.diagCodeText = "SEL-INPUT-AXIS-OUT-OF-SCOPE"; // 稳定码词面
    rows.push_back(row);
    return rows;
}

}  // namespace

/**
 * @brief harness 入口（QApplication＋装配门面＋演示缝注入＋主面板挂位）。
 *
 * @param argc [in] Qt 参数计数
 * @param argv [in] Qt 参数向量
 * @return 进程退出码（0＝正常退出）
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QObject::tr("selection 插件验证 harness（WP-19-T10）"));

    // ---- 装配门面（产品入口——真实描述符＋命令目录＋面板工厂）。 ---
    sdurws::ird::selection::SelectionPluginAssembly bundle =
        createSelectionPluginAssembly();

    // ---- 会话事实注入（就绪投影呈现素材——演示态：输入完整/无在途
    //      任务/无判定；真实事实归域就绪校验/execution，装配层注入）。
    bundle.session().epoch = 1;
    bundle.session().writable = true;
    bundle.session().inputComplete = true;
    bundle.session().missingItemKeys = {};  // 演示态无缺项（UX-10 空清单）

    // ---- 演示服务缝（行集三缝＝固定演示行；回填出口＝回显——真实
    //      CommandRegistry 与①命令端口接线归宿主装配批次，不虚构提交
    //      语义；文案解析器＝键名回显——宿主 UiText 资源接线归装配
    //      批次）。 ---------------------------------------------------
    const std::vector<SelCatalogRow> catalogRows = demoCatalogRows();
    const std::vector<SelCandidateRow> candidateRows = demoCandidateRows();
    const std::vector<SelRejectionRow> rejectionRows = demoRejectionRows();
    const std::vector<SelGapRow> gapRows = demoGapRows();
    SelPanelServices services;
    services.catalogRows = [&catalogRows]() { return catalogRows; };
    services.candidateRows = [&candidateRows]() { return candidateRows; };
    services.rejectionRows = [&rejectionRows]() { return rejectionRows; };
    services.gapRows = [&gapRows]() { return gapRows; };
    services.backfillSubmit = [](const std::string& commandToken) {
        qDebug("selection backfill submit: %s", commandToken.c_str());
    };
    services.backfillAvailability = [](const std::string&) {
        return true;  // 演示态可用
    };
    services.textResolver = [](const std::string& titleKey) {
        return titleKey;  // 键名回显（开发态可见缺口——产品装配接宿主）
    };
    bundle.setServices(services);

    // ---- 面板挂位（descriptor.panels 工厂现调——主面板中央位；工厂
    //      产物归调用方接管所有权）。
    QWidget* mainPanel = nullptr;
    for (const auto& panel : bundle.descriptor.panels) {
        mainPanel = panel.factory();  // 主面板（工作流页＋目录管理页＋
                                      //   候选表页合一）
    }
    window.setCentralWidget(mainPanel);
    window.resize(1100, 720);

    std::cout << "[selection-harness] pluginId=" << bundle.descriptor.pluginId
              << " stage=" << bundle.descriptor.stageToken
              << " panels=" << bundle.descriptor.panels.size()
              << " commands=" << bundle.descriptor.commands.size() << std::endl;

    window.show();
    return app.exec();
}
