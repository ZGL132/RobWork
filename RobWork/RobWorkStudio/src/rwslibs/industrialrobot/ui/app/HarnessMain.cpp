/**
 * @file   HarnessMain.cpp
 * @brief  工作台验证 harness（sdurws_ird_ui_app）的装配序列——开发期 L5
 *         应用壳雏形：真实诊断栈＋真实工作台壳＋真实项目打开协议，交互式
 *         验证平台逻辑（建模 WP-13+ 开发期间的可视化验证手段，任务 UI-T15）。
 *
 * 设计依据：
 *   - units/ui.md §10.1（IWorkbenchShell 装配门面：ShellWiring 注入、
 *     initialize 恰好一次、mainWindow 唯一 Widget 出口）、§10.5
 *     （UiSessionControllerDeps）、§5.2（打开状态机——NoProject→Opening→
 *     Open*）、§12.2（Windows GUI 执行纪律：QT_QPA_PLATFORM=windows）；
 *   - UiPorts.hpp 头注（O-31：L5 装配器同时看见两边，写单行适配器——
 *     本文件的装配序列即该角色的开发期载体）；
 *   - diagnostics §4.5/§9.2（码表装配序：registerCode 全部完成后 seal；
 *     DiagnosticsFactory 同款两段式）、§6.2（Dev 码唯一出线＝日志管线）、
 *     §7.7（目录导出挂接脱敏双保险）。
 *
 * 本 harness 与产品装配层（插件装配任务）的关系：
 *   本文件是**开发工具**，不是产品交付路径——它验证的是"平台链路本身"
 *   （布局记忆/命令门控/打开协议/诊断出线），装配形状按产品契约逐行实现；
 *   UI-T16 起：壳拆「内容装配层＋顶层窗口宿主层」——本入口经
 *   IWorkbenchShell 门面消费的内容装配面与宿主插件（sdurws_ird_ui_plugin）
 *   完全同源（O-38 裁决②）；诊断栈装配序列亦与插件共用
 *   （DiagnosticsAssembly.*——单一装配序列，防两份漂移）。产品装配层
 *   落位时按同一契约替换本入口，ui 库产品面零改动。
 *
 * 用法（开发期交互验证）：
 *   sdurws_ird_ui_app                       无项目首页态（PM-10：三入口/
 *                                           命令门控/命令面板 Ctrl+Shift+P）
 *   sdurws_ird_ui_app --open <项目目录>      真实打开协议→项目态（PM-11
 *                                           状态栏；双开同一目录可验 PM-07
 *                                           锁竞争降级横幅）
 *   sdurws_ird_ui_app --new <目录> <显示名>  创建空项目→经标准打开协议进入
 *                                           会话（创建后释放锁再打开——
 *                                           同一协议路径，锁干净）
 *   可选项：--readonly（显式只读打开）、--devlog-dir <目录>（开发日志落盘
 *   目录，缺省 <cwd>/ird-harness-logs）。
 *
 * 线程模型：单线程——QApplication/壳/控制器全部 main 线程（§3.4 M-1 的
 *   装配侧同款纪律；壳的布局落盘已在库内走后台写线程，本文件不感知）。
 */

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QWidget>

#include <sdurws/ird/project/ProjectStore.hpp>    // ProjectStoreFactory（--new 创建协议）
#include <sdurws/ird/project/StoreTypes.hpp>      // StoreError（创建失败折叠）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>      // createWorkbenchShell/ShellWiring（装配门面）
#include <sdurws/ird/ui/UiSessionController.hpp>  // UiSessionController（§5 会话状态机）
#include <sdurws/ird/ui/UiTypes.hpp>              // UiOpenMode（打开模式词表）

#include "DiagnosticsAssembly.hpp"                // 诊断栈一步式装配（UI-T16 起与插件共用——单一装配序列）
#include "PortAdapters.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace sdurws::ird;

// ---------------------------------------------------------------------
// CLI 解析（开发工具从简——不用 QCommandLineParser，保持装配序列可见性）
// ---------------------------------------------------------------------

/// 解析后的启动请求（全部可选——缺省即无项目首页态）。
struct HarnessOptions {
    bool openRequested = false;          ///< --open：打开既有项目
    bool newRequested = false;           ///< --new：新建空项目
    bool readOnly = false;               ///< --readonly：显式只读打开
    std::string projectDir;              ///< 项目目录（--open/--new 共用；UTF-8）
    std::string displayName;             ///< 项目显示名（--new 专用；UTF-8）
    fs::path devlogDir;                  ///< 开发日志目录（缺省 <cwd>/ird-harness-logs）
    bool helpRequested = false;          ///< --help：打印用法
};

/// 打印用法（控制台 UTF-8；Windows 下 main 已设控制台代码页）。
void printUsage()
{
    std::cout <<
        "用法：sdurws_ird_ui_app [--readonly] (--open <项目目录> | --new <目录> <显示名>)\n"
        "                      [--devlog-dir <日志目录>]\n"
        "  无 --open/--new     ：无项目首页态（PM-10 交互验证）\n"
        "  --open <目录>       ：真实打开协议进入项目会话（PM-11 状态栏/命令门控）\n"
        "  --new <目录> <名称> ：创建空项目后经标准打开协议进入会话\n"
        "  --readonly          ：显式只读打开（写入口全禁——§5.5）\n"
        "  --devlog-dir <目录> ：开发日志目录（缺省 ./ird-harness-logs）\n";
}

/// 解析 argv（Windows 控制台参数经本地代码页读入、统一转 UTF-8 存储——
/// 中文目录/显示名可用；存储口径与 fs::u8path/对端 UTF-8 契约一致）。
HarnessOptions parseOptions(int argc, char** argv, bool& ok)
{
    HarnessOptions opts;
    ok = true;
    std::vector<QString> args;  // argv[1..] 的本地编码读入（索引对齐）
    for (int i = 1; i < argc; ++i) {
        args.push_back(QString::fromLocal8Bit(argv[i]));
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const QString& arg = args[i];
        if (arg == QLatin1String("--help")) {
            opts.helpRequested = true;
        } else if (arg == QLatin1String("--readonly")) {
            opts.readOnly = true;
        } else if (arg == QLatin1String("--devlog-dir")) {
            if (i + 1 >= args.size()) {
                std::cerr << "错误：--devlog-dir 缺少目录参数\n";
                ok = false;
                return opts;
            }
            opts.devlogDir = fs::u8path(args[++i].toStdString());
        } else if (arg == QLatin1String("--open")) {
            if (i + 1 >= args.size()) {
                std::cerr << "错误：--open 缺少项目目录参数\n";
                ok = false;
                return opts;
            }
            opts.openRequested = true;
            opts.projectDir = args[++i].toStdString();
        } else if (arg == QLatin1String("--new")) {
            if (i + 2 >= args.size()) {
                std::cerr << "错误：--new 需要 <目录> <显示名> 两个参数\n";
                ok = false;
                return opts;
            }
            opts.newRequested = true;
            opts.projectDir = args[++i].toStdString();
            opts.displayName = args[++i].toStdString();
        } else {
            std::cerr << "错误：未知参数 " << arg.toStdString() << "（--help 查看用法）\n";
            ok = false;
            return opts;
        }
    }
    if (opts.openRequested && opts.newRequested) {
        std::cerr << "错误：--open 与 --new 互斥\n";
        ok = false;
    }
    if (opts.devlogDir.empty()) {
        opts.devlogDir = fs::current_path() / "ird-harness-logs";
    }
    return opts;
}

// ---------------------------------------------------------------------
// 装配段（每步单一职责；任一步抛异常＝装配失败——fail-fast 退出，不留
// 半装配状态。诊断栈装配自 UI-T16 起为共用实现——DiagnosticsAssembly.*，
// 与宿主插件同序列；栈对象为静态存储期稳定出参，见其头注稳定性契约）
// ---------------------------------------------------------------------

/// 控制台报告（开发工具的可见性面——装配/打开事实直接可读）。
void reportLine(const std::string& text)
{
    std::cout << "[ird-harness] " << text << std::endl;
}

}  // namespace

// =====================================================================
// 装配序列入口
// =====================================================================

int main(int argc, char** argv)
{
#ifdef _WIN32
    // 控制台 UTF-8 输出（中文报告行不乱码；仅本进程控制台，不改全局）。
    SetConsoleOutputCP(CP_UTF8);
#endif

    bool parseOk = true;
    const HarnessOptions opts = parseOptions(argc, argv, parseOk);
    if (opts.helpRequested) {
        printUsage();
        return 0;
    }
    if (!parseOk) {
        printUsage();
        return 2;
    }

    // QApplication 先于一切 Widget/壳装配（Qt 要求 app 对象先于窗口存活）。
    QApplication app(argc, argv);

    // ---- 装配第一步：诊断栈（后续每步的协作面都从这里取依赖）----
    ui::app::DiagnosticsStack diag;       // 共用装配序列的产物集合（UI-T16 起与宿主插件同源）
    try {
        ui::app::assembleDiagnostics(opts.devlogDir, diag);
    } catch (const std::exception& error) {
        std::cerr << "装配失败：诊断栈（" << error.what() << "）\n";
        return 3;
    }
    reportLine("诊断栈就绪（开发日志目录：" + opts.devlogDir.u8string() + "）");

    // ---- 装配第二步：工作台壳（§10.1 ShellWiring——必填四端口非空；
    //      可空成员全部"显式声明"赋值，空即无该场景的契约纪律）----
    const std::unique_ptr<ui::IWorkbenchShell> shell = ui::createWorkbenchShell();
    ui::ShellWiring wiring;
    wiring.eventBus = nullptr;  // 显式声明：v0.1 无事件消费场景（投影管线随
                                // UI-T04+ 事件驱动装配；产品事件总线归
                                // execution·ui，测试参考总线不入装配面）。
    wiring.diagSink = diag.catalog;
    wiring.redaction = diag.redaction;
    wiring.devLog = diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    wiring.diagFactory = diag.factory;
    wiring.policySource = std::make_shared<ui::app::UnloadedPolicySource>();
    wiring.nameResolver = std::make_shared<ui::app::NullUiNameResolver>();
    wiring.aboutSource = std::make_shared<ui::app::HarnessAboutSource>();
    if (!shell->initialize(wiring)) {
        std::cerr << "装配失败：壳 initialize 被拒（重复装配或必填端口为空）\n";
        return 3;
    }
    shell->mainWindow()->show();
    reportLine("工作台壳已就位（五区布局＋命令门控；Ctrl+Shift+P 命令面板）");

    // ---- 装配第三步：会话控制器（§5 状态机；打开编排的触发面）----
    // 诊断桥/工厂端口全部经适配器接入（O-31：装配层同时看见两边——
    // 适配器实现见 PortAdapters.*，翻译规则逐行锚定对端契约）。
    const auto bridge = std::make_shared<ui::app::ProjectDiagnosticsBridge>(
        diag.catalog, diag.factory, wiring.devLog);
    const auto storeFactory =
        std::make_shared<ui::app::StoreFactoryPortAdapter>(*bridge);

    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory = storeFactory;
    sessionDeps.diagSink = diag.catalog;
    sessionDeps.diagFactory = diag.factory;
    sessionDeps.devLog = wiring.devLog;
    sessionDeps.presentContext = [&shell](const ui::ProjectContextProjection& context) {
        // 上下文原子快照注入壳（§10.1 v0.5 facets——状态栏/首页/命令门控
        // 的单一数据源）；控制器在打开成功/关闭完成时回调（UI 线程）。
        shell->presentProjectContext(context);
    };
    // saveAllDraftsManual/forceAbandonAll 不接线（v0.1 关闭链路不可达——
    // 壳层 workbench.closeProject 为阶段 A 占位处理器；若未来误触发"保存"
    // 决议，控制器按契约 fail-fast 而非静默降级——装配缺失必须是响错误）。

    ui::UiSessionController controller(std::move(sessionDeps));

    // ---- 打开编排（--new/--open；§5.2 打开五步的触发——触发时机编排
    //      归装配层〔§11.5 分工表〕，状态机推进归控制器）----
    // 编排段兜底（WP-10-T15 验收 attempt 1 阻断项 B-1 的第二道防线）：段内
    // 残存异常（典型＝诊断桥上抛的装配类缺陷 CodeUnknown/Usage——对端记录
    // 的契约缺口已在桥内具名降级，装配缺陷按纪律继续上抛）在此转换为消息框
    // ＋控制台双通道留痕＋非零退出码（5），绝不让异常穿透 main 触发
    // std::terminate（0xC0000409 fast-fail＝不可观测的进程死亡）；也绝不
    // 静默吞——退出码与消息框都是失败事实的承载（AGENTS"禁止吞错"）。
    if (opts.openRequested || opts.newRequested) {
      try {
        // 路径规范化（§9.3 口径——与适配器内打开请求同源；最近项目登记
        // 也用此规范形态去重）。
        std::string canonical;
        try {
            canonical = fs::weakly_canonical(fs::u8path(opts.projectDir)).u8string();
        } catch (const fs::filesystem_error& error) {
            canonical = opts.projectDir;
            reportLine("路径规范化失败（以原始路径继续）：" + std::string(error.what()));
        }

        if (opts.newRequested) {
            // 新建流：createNew 前置校验（目录不存在或为空目录——对端
            // fail-fast 契约；harness 预检给出可读提示而非异常中断）。
            std::error_code ec;
            if (fs::exists(fs::u8path(opts.projectDir), ec)
                && !fs::is_empty(fs::u8path(opts.projectDir), ec)) {
                QMessageBox::warning(
                    shell->mainWindow(), QString::fromUtf8("无法创建项目"),
                    QString::fromUtf8("目标目录已存在且非空：\n")
                        + QString::fromStdString(opts.projectDir)
                        + QString::fromUtf8("\n（创建协议要求目录不存在或为空——零半成品纪律）"));
                return 4;
            }
            // 创建（组装区整体就位＋装载激活——创建者即首个写权限持有者）。
            // 结果 store 在作用域结束即析构＝隐式排空＋锁释放（§5.1 生命
            // 周期行）——随后经标准打开协议进入会话（同一协议路径，锁干净，
            // 不走"进程内重复打开"的锁竞争分支）。
            try {
                project::OpenStoreResult created = project::ProjectStoreFactory::createNew(
                    fs::u8path(opts.projectDir), opts.displayName, nullptr,
                    bridge.get());
                reportLine("项目已创建（" + canonical + "，显示名："
                           + opts.displayName + "）——释放创建锁后经打开协议进入");
            } catch (const project::StoreError& error) {
                // 创建失败的稳定诊断随 bridge 入目录；消息框呈现开发诊断
                // detail（what()——createNew 失败不留半成品的契约下，用户
                // 可据此清理目标目录后重试）。
                reportLine("项目创建失败：" + std::string(error.what()));
                QMessageBox::warning(shell->mainWindow(),
                                     QString::fromUtf8("项目创建失败"),
                                     QString::fromUtf8(error.what()));
                return 4;
            } catch (const std::invalid_argument& error) {
                QMessageBox::warning(shell->mainWindow(),
                                     QString::fromUtf8("项目创建失败"),
                                     QString::fromUtf8(error.what()));
                return 4;
            }
        }

        // 打开流（新建流在此汇合——同一协议路径）：模式随 --readonly。
        const ui::UiOpenMode mode =
            opts.readOnly ? ui::UiOpenMode::ReadOnly : ui::UiOpenMode::Writable;
        const ui::SessionOpenReport report = controller.openProject(canonical, mode);
        if (report.ok) {
            shell->noteRecentProject(canonical);  // PM-10 最近项目（去重/上限壳内处理）
            reportLine("项目已打开：" + canonical + "（可写："
                       + (report.opened.metadata.writable ? "是" : "否（降级只读——见横幅）")
                       + "）");
        } else {
            // 失败：错误页数据（token＋detail＋路径）——开发期经消息框与
            // 控制台双通道呈现；壳保持无项目首页态（"当前项目不动"）。
            reportLine("打开失败（" + report.failure.errorCodeToken + "）："
                       + report.failure.detail + " @ " + report.failure.projectPath);
            QMessageBox::warning(
                shell->mainWindow(), QString::fromUtf8("项目打开失败"),
                QString::fromUtf8("稳定码：")
                    + QString::fromStdString(report.failure.errorCodeToken)
                    + QString::fromUtf8("\n项目路径：")
                    + QString::fromStdString(report.failure.projectPath)
                    + QString::fromUtf8("\n详情：")
                    + QString::fromStdString(report.failure.detail));
        }
      } catch (const std::exception& error) {
          // 兜底错误页：业务失败（StoreError/打开失败投影）已在上方按各自
          // 契约处理并 return——走到这里的是装配类缺陷或未预期异常，属
          // harness 自身错误（对端记录契约缺口不会到达——桥内已降级）。
          // 可观测失败＋受控退出（exit 5），留给开发者可读的失败事实。
          reportLine(std::string("打开编排异常终止（harness 装配类缺陷）：")
                     + error.what());
          QMessageBox::critical(
              shell->mainWindow(), QString::fromUtf8("工作台打开失败"),
              QString::fromUtf8("打开编排遇到未预期异常（harness 装配类缺陷，"
                                "非对端业务失败）：\n")
                  + QString::fromUtf8(error.what())
                  + QString::fromUtf8("\n（对端记录的契约缺口由诊断桥具名落 Dev "
                                      "日志，不会走到这里）"));
          return 5;
      }
    }

    // ---- 事件循环（交互验证主体；关窗即退出）----
    const int exitCode = app.exec();

    // ---- 有界拆卸（§10.1：布局落盘＋窗口销毁；幂等）----
    const ui::ShellTeardownReport teardown = shell->shutdown();
    reportLine("拆卸完成（布局落盘：" + std::string(teardown.layoutPersisted ? "成功" : "未写")
               + "；会话纪元：" + std::to_string(controller.epoch()) + "）");
    return exitCode;
}
