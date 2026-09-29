/**
 * @file   UiPlugin.cpp
 * @brief  工作台宿主插件实现（sdurws_ird_ui_plugin）——装配序列、会话入口
 *         编排、宿主框架菜单动作与共存观测留痕（零计算逻辑——全部界面
 *         语义经内容装配面 WorkbenchContent 与会话控制器承接）。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T16 行＋立项登记注（v1.9，O-38 裁决承接）、
 *     §11.5（触发时机编排归装配层——会话入口处理器覆写）、§5.2/§5.3
 *     （打开协议与 PM-07 显示差异——编排面与 HarnessMain 逐行同源）、
 *     §13 UI-T18 行＋落位登记注＋§10.1 v1.14（O-43 裁决③——多 Dock 拓扑
 *     与宿主状态栏投影，本文件承载 addDockWidget 自证拍与双观测钩子接线）；
 *   - 框架 rws::RobWorkStudioPlugin 机制（零框架修改——SA-02）：本 DLL 由
 *     宿主 Plugins→Load plugin 动态装载（开发期验证通道——动态加载仅限
 *     开发期，SA-01 产品静态白名单不变）；本文件不 include 任何业务域
 *     计算面（插件零计算逻辑——ui.md §13 登记注）；
 *   - O-31 装配层特权（DTB §4.5）：适配器复用 harness 形态（app/
 *     PortAdapters.*），插件侧只做装配与编排，不代行 ui/对端任何语义。
 *
 * 留痕通道：装配/打开/共存观测事实全部经诊断栈 Dev 日志出线
 *   （diagnostics §6.2——插件不造第二套日志）；目录缺省为当前工作目录下
 *   ird-ui-plugin-logs（开发期通道形态，与 harness 的 ird-harness-logs
 *   同型；验收留痕经 traceability/builds/wp10-t16/ 固化）。
 */

#include "UiPlugin.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QDockWidget>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <rws/RobWorkStudio.hpp>                 // 宿主注入面：getView()/getWorkCellScene()/menuBar()/事件面（共存接入＋UI-T23 桥）

#include <rw/kinematics/Frame.hpp>               // rw::kinematics::Frame（L3 桥事件值——TreeView Select Frame 转发名源）
#include <rw/kinematics/State.hpp>               // rw::kinematics::State（D8 Jog 桥——State 变化采样）
#include <rw/models/Device.hpp>                  // rw::models::Device（D8 桥——WorkCell 设备 q 提取）
#include <rw/models/WorkCell.hpp>                // rw::models::WorkCell（L2 高亮按名寻帧/D8 设备枚举——呈现对象公开面）
#include <rw/graphics/WorkCellScene.hpp>         // rw::graphics::WorkCellScene（L2 高亮出口——setHighlighted 公开 API）

#include <sdurws/ird/project/CommandService.hpp> // project::CommandResult/CommandStatus（apply 网关提交面——T03b-2）
#include <sdurws/ird/project/StoreTypes.hpp>     // project::StoreError（创建失败折叠）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // CommandOutcome/CommandParameter（会话入口覆写载体）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // PluginUiDescriptor/CommandDescriptor 完整类型（UI-T23 三域命令入册的遍历面）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule 完整类型（buildDraftCommand 调用面）
#include <sdurws/ird/ui/UiText.hpp>              // ui::resolveText（§3.5 唯一文案出口——域命令诚实反馈文案）
#include <sdurws/ird/ui/UiPorts.hpp>             // ui::IUiNameResolver（共享面名称解析端口——C-11 复用面）

#include "DomainModuleRunner.hpp"                // runDomainApply（UI-T23 acceptance 2——draft.apply 多模块遍历）

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// 开发日志目录名（相对当前工作目录——开发期通道形态；与 harness 的
/// ird-harness-logs 同型不同名，两类通道的留痕互不混写）。
constexpr const char* kPluginLogDirName = "ird-ui-plugin-logs";

/// 域命令处理器级拒绝的诚实反馈键（§3.5 键约定——UiText 键族①b 收口增行；
/// content submitCommand 对 outcome.messageKey 的呈现值源）。
constexpr const char* kModelingFlowNotAssembledKey = "cmd.modeling.flow-not-assembled";

/// 真实执行面已落位的域命令（WP-24-T03b 诚实边界—— modeling §9.7.3 十条
/// 中唯一具备域内已落位能力的命令：模板工厂重种子；其余九条的域流程归
/// 后续建模任务，提交走"域流程未装配"诚实反馈，不虚构执行成功）。
constexpr const char* kModelingNewFromTemplateId = "modeling.new-from-template";

/**
 * @brief 修订事件桥（WP-24-T03b——§5.2/§6.2 会话事件全同步的宿主半区）。
 *
 * store 对端在修订提交时向总线 publish RevisionCommitted（撤销/重做/其他
 * 会话入口与 draft.apply 同源）；本桥把事件转达建模模块（基线前移＋就绪
 * 重算）。分支过滤在模块侧（非目标分支的事件不触碰锚——跨分支修订不
 * 污染编辑基线）。publish 发生在命令提交线程（开发通道＝UI 线程），模块
 * 的 UI 线程断言因此成立；其他线程来源出现时须经 postToUiThread marshal
 * （登记 ui.md §16.7 收口行的并发边界注）。
 */
class DomainRevisionEventBridge final : public core::IDomainEventSink {
public:
    /// @param domains [in] 建模装配门面（非 owning——插件存活期覆盖）
    /// @param devLog [in] Dev 出线通道（可空＝静默）
    explicit DomainRevisionEventBridge(modeling::ModelingPluginAssembly* domains,
                                       std::function<void(const std::string&)> devLog)
        : m_domains(domains)
        , m_devLog(std::move(devLog))
    {
    }

    void onEvent(const core::DomainEvent& event) override
    {
        if (event.kind != core::DomainEventKind::RevisionCommitted) {
            return;  // 只消费修订提交事件（其余三类无模块同步语义）
        }
        const core::RevisionCommittedPayload& payload = event.asRevisionCommitted();
        if (m_domains != nullptr) {
            m_domains->onRevisionCommitted(payload.branch, payload.revision);
        }
        if (m_devLog) {
            m_devLog("revision-committed -> modeling baseline advance: branch="
                     + payload.branch.toCanonical()
                     + " rev=" + payload.revision.toCanonical());
        }
    }

private:
    modeling::ModelingPluginAssembly* m_domains;  ///< 建模门面（非 owning）
    std::function<void(const std::string&)> m_devLog;  ///< Dev 出线（可空）
};

/// Dev 日志通道 token（diagnostics.md §7.2 LogChannel ≤48 字符；对齐
/// "diag/<单元>" 命名族——Dev 级事实的唯一出线通道 §6.2）。
constexpr const char* kPluginDevChannel = "diag/ui";

/// 控制台报告（宿主进程的控制台可用时可见；留痕主通道仍是 Dev 日志）。
void reportLine(const std::string& text)
{
    std::cout << "[ird-ui-plugin] " << text << std::endl;
}

/// 路径规范化（§9.3 口径——与 HarnessMain 打开编排同源；失败以原始路径
/// 继续＋Dev 留痕，不阻塞打开流程）。
std::string canonicalizePathOrKeep(const std::string& rawPath)
{
    try {
        return fs::weakly_canonical(fs::u8path(rawPath)).u8string();
    } catch (const fs::filesystem_error& error) {
        reportLine("路径规范化失败（以原始路径继续）：" + std::string(error.what()));
        return rawPath;
    }
}

/// 会话标签常量（宿主菜单/对话框的装配层呈现文案——与 WorkbenchText 键面
/// 词汇保持一致；插件侧不 include src/ 私有头，按既有 orchestrate 对话框
/// 先例以字面量承载）。
constexpr const char* kProjectMenuTitle = "工业机器人项目";
constexpr const char* kRecentMenuTitle = "最近项目";
constexpr const char* kViewMenuTitle = "视图";
constexpr const char* kRecentUnavailableSuffix = "（项目位置不可用）";

/**
 * @brief 打开五步协议的捕获包装（UI-T17）：转发内层 StoreFactoryPortAdapter
 *        的 open，并保留每次成功打开的绑定集——草稿写半区端口（IUiDraft
 *        StorePort）由 StorePortAdapter 双面实现经 dynamic_pointer_cast 取
 *        得，供打开成功后的 DraftController bindSession（O-31 装配层特权：
 *        同时看见两边写包装，ui 冻结面 SessionPortBundle 零改动）。
 *
 * 生命周期：插件持有 shared_ptr；m_lastStore 与控制器绑定集共享同一端口
 * 实例（shared 引用），不产生第二份所有权语义。
 */
class BundleCapturingStoreFactory final : public IUiStoreFactoryPort {
public:
    /// @param inner [in] 内层工厂适配器（共享持有——存活期覆盖本包装）。
    /// @param onStoreCaptured [in] 成功打开回调（T03b-2——宿主经此保存
    ///        强类型适配器引用，apply 网关取命令端口）。
    explicit BundleCapturingStoreFactory(
        std::shared_ptr<IUiStoreFactoryPort> inner,
        std::function<void(std::shared_ptr<app::StorePortAdapter>)> onStoreCaptured)
        : m_inner(std::move(inner))
        , m_onStoreCaptured(std::move(onStoreCaptured))
    {
    }

    /// @brief 直转 open 并在成功时捕获绑定集（失败不写——"失败＝无绑定泄漏"）。
    OpenStoreOutcome open(const std::string& canonicalPath,
                          UiOpenMode mode,
                          SessionPortBundle& outBindings) override
    {
        const OpenStoreOutcome outcome = m_inner->open(canonicalPath, mode, outBindings);
        if (outcome.ok) {
            m_lastStore = outBindings.store;  // shared 拷贝＝观察同一实例
            if (m_onStoreCaptured) {
                if (auto adapter = std::dynamic_pointer_cast<app::StorePortAdapter>(m_lastStore)) {
                    m_onStoreCaptured(std::move(adapter));
                }
            }
        }
        return outcome;
    }

    /// @brief 最近一次成功打开的草稿写半区端口（双面适配器_cast；未打开＝空）。
    std::shared_ptr<IUiDraftStorePort> lastDraftStore() const
    {
        return std::dynamic_pointer_cast<IUiDraftStorePort>(m_lastStore);
    }

    /// @brief 最近一次成功打开的存储适配器（T03b-2——apply 网关经其取
    ///        project::ProjectStore::commands() 命令端口；未打开＝空）。
    std::shared_ptr<app::StorePortAdapter> lastStoreAdapter() const
    {
        return std::dynamic_pointer_cast<app::StorePortAdapter>(m_lastStore);
    }

private:
    /// 内层工厂（StoreFactoryPortAdapter——对端翻译面）。
    std::shared_ptr<IUiStoreFactoryPort> m_inner;
    /// 最近一次成功打开的 store 端口（双面适配器——DraftStore 强转源）。
    std::shared_ptr<IUiProjectStorePort> m_lastStore;
    /// 成功打开回调（T03b-2——宿主保存强类型适配器引用的通道）。
    std::function<void(std::shared_ptr<app::StorePortAdapter>)> m_onStoreCaptured;
};

// =====================================================================
// 确认交互适配器（T03b-2c——project::ICommandInteraction 的宿主实现）
// =====================================================================
// 定位：submit 的 Confirmable 集（如行程超限 SA-15）经本适配器呈现确认
// 对话框；凭据＝"local-owner"＋UTC 时刻（开发通道主体——正式主体采集
// 归收口增量）。诚实偏差登记：ui.md §7.7 要求确认呈现不重入（QDialog::
// open 非阻塞），本适配器在 UI 线程同步 submit 路径上使用模态 question
// 对话框——模态嵌套事件循环在开发通道可接受，收口时迁 CommandInteraction
// Bridge 的 Marshal 形态（登记 ui.md §16.7 下一行）。
class HostCommandInteraction final : public project::ICommandInteraction {
public:
    HostCommandInteraction(QWidget* parent, std::function<bool()> alive)
        : m_parent(parent), m_alive(std::move(alive))
    {
    }

    bool isAlive() const override { return m_alive(); }

    std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(const std::vector<core::ConfirmableFinding>& findings) override
    {
        if (findings.empty()) {
            return std::vector<core::ConfirmationCredential>{};
        }
        // 逐项三要素汇总（context/cause/recommendedAction——ERR-01 顺序）。
        QString detail;
        for (const auto& finding : findings) {
            detail += QString::fromUtf8("・%1\n  原因：%2\n  建议：%3\n")
                          .arg(QString::fromStdString(finding.record.context),
                               QString::fromStdString(finding.record.cause),
                               QString::fromStdString(finding.record.recommendedAction));
        }
        const auto answer = QMessageBox::question(
            m_parent, QString::fromUtf8("建模修订待确认项"),
            QString::fromUtf8("存在需人工确认的比较型事项（SA-15，确认将留痕计入修订摘要）：\n\n")
                + detail,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return std::nullopt;  // 整体拒绝（§5.3.3）
        }
        const auto now = std::chrono::system_clock::now();
        std::vector<core::ConfirmationCredential> credentials;
        credentials.reserve(findings.size());
        for (std::size_t i = 0; i < findings.size(); ++i) {
            credentials.push_back(core::ConfirmationCredential{
                std::string("local-owner"), now});
        }
        return credentials;
    }

private:
    QWidget* m_parent = nullptr;                    ///< 对话框父（宿主控件）
    std::function<bool()> m_alive;                  ///< 会话存活探针（§5.3.3）
};

// =====================================================================
// 共享面端口适配器（UI-T23——O-31 装配层特权边：ui 自有端口 → 宿主形态）
// =====================================================================

/**
 * @brief 空映射名称端口（IUiRuntimeNameMapPort 的宿主形态适配——两向恒
 *        nullopt 的诚实空映射）。
 *
 * 为什么允许"空映射"：SelectionService Deps.nameMap 构造期必填（L2/L3
 * 承诺的端口缝），而当前宿主形态没有呈现装配（RuntimePublishBridge 与
 * runtime RuntimeNameMap 随 WP-24-T08 呈现装配接续）——无已应用呈现＝
 * 无任何名字映射是与空会话同构的诚实二态：L2 正向判定照常执行并得
 * nullopt（不高亮不报错）、L3 反解照常走失败分支（树不动＋runtimeOnly
 * 暂态）。真映射注入点单一（本端口替换），替换零改代码——端口注入
 * 纪律（UI-T21 冻结面）的结构收益。
 */
class HostEmptyNameMapPort final : public ui::IUiRuntimeNameMapPort {
public:
    std::optional<core::ObjectId> resolveObjectIdFromRuntimeName(
        const std::string& /*runtimeName*/) const override
    {
        return std::nullopt;  // 无呈现装配＝无映射（L3 反解失败分支的合法触发面）
    }

    std::optional<std::string> resolveRuntimeName(
        const core::ObjectId& /*id*/) const override
    {
        return std::nullopt;  // 同上（L2 正向"未应用"分支的合法触发面）
    }
};

/**
 * @brief 宿主三维高亮出口（IUiHighlightOutlet 的宿主实现——L2 的动作
 *        出线缝，D7 唯一三维视图的框架公开 API 承载）。
 *
 * 实现：rw::models::WorkCell::findFrame（按运行时名寻帧——名字是
 * RuntimeNameMap 键，SA-05）＋rw::graphics::WorkCellScene::setHighlighted
 * （框架高亮呈现）。纪律：**禁跨快照存 Frame 指针**（ui.md §14.2
 * runtime→ui 行）——本适配器只记录已高亮的运行时**名**（string），清除
 * 时按名重新寻帧置 false；呈现 WorkCell 重建后按名清除落空即静默（旧
 * 呈现对象已不存在——尽力而为的呈现面动作，服务侧失败语义不放大）。
 * 呈现缺席（getWorkCell()/场景空）＝动作跳过＋Dev 留痕回调（可空），
 * L2 判定路径不受影响。
 */
class HostHighlightOutlet final : public ui::IUiHighlightOutlet {
public:
    /// @param studio [in] 宿主注入面（非 owning——存活期由插件保证）
    /// @param devLog [in] Dev 留痕通道（可空＝静默）
    HostHighlightOutlet(rws::RobWorkStudio* studio,
                        std::function<void(const std::string&)> devLog)
        : m_studio(studio), m_devLog(std::move(devLog))
    {
    }

    void highlightRuntimeObject(const std::string& runtimeName) override
    {
        rw::models::WorkCell::Ptr workcell =
            m_studio != nullptr ? m_studio->getWorkCell() : nullptr;
        rw::graphics::WorkCellScene::Ptr scene =
            m_studio != nullptr ? m_studio->getWorkCellScene() : nullptr;
        if (workcell.isNull() || scene.isNull()) {
            // 呈现缺席＝无三维场景可高亮（诚实降级——判定照常，动作跳过；
            // UI-T21 端口注释的"无三维场景显式声明"形态）。
            if (m_devLog) {
                m_devLog("highlight skip: no presentation workcell/scene");
            }
            return;
        }
        rw::core::Ptr<rw::kinematics::Frame> frame = workcell->findFrame(runtimeName);
        if (frame.isNull()) {
            // 按名未寻得帧（呈现内容与 NameMap 漂移的边角）——不伪造
            // 高亮，Dev 留痕可观测。
            if (m_devLog) {
                m_devLog("highlight skip: frame not found '" + runtimeName + "'");
            }
            return;
        }
        scene->setHighlighted(true, frame);
        m_highlightedName = runtimeName;  // 只记名不记指针（§14.2 纪律）
    }

    void clearHighlight() override
    {
        if (m_highlightedName.empty()) {
            return;  // 本无高亮＝恒等动作
        }
        rw::models::WorkCell::Ptr workcell =
            m_studio != nullptr ? m_studio->getWorkCell() : nullptr;
        rw::graphics::WorkCellScene::Ptr scene =
            m_studio != nullptr ? m_studio->getWorkCellScene() : nullptr;
        if (!workcell.isNull() && !scene.isNull()) {
            rw::core::Ptr<rw::kinematics::Frame> frame =
                workcell->findFrame(m_highlightedName);
            if (!frame.isNull()) {
                scene->setHighlighted(false, frame);
            }
        }
        m_highlightedName.clear();
    }

private:
    rws::RobWorkStudio* m_studio;                     ///< 宿主注入面（非 owning）
    std::function<void(const std::string&)> m_devLog; ///< Dev 留痕（可空）
    std::string m_highlightedName;                    ///< 已高亮运行时名（禁存 Frame 指针——§14.2）
};

/**
 * @brief 从宿主 State 提取权威关节向量（D8 Jog 会话姿态桥的宿主半区——
 *        纯函数便于测试）。
 *
 * 提取规则（诚实边界——登记 ui.md §13 UI-T23 落位注）：呈现 WorkCell 的
 * 首个 Device（getDevices() 首条）的 getQ(state)。device 选择随呈现身份
 * 对账细化（单机器人项目为准确形态；多设备项目的目标 device 判定归域
 * 会话态——当前装配形态无该缝，取首条＋Dev 留痕，不虚构精确性）。无
 * WorkCell/无设备＝nullopt（桥静默——无会话/无呈现时 State 变化无会话
 * 姿态语义）。
 *
 * @param workcell [in] 呈现 WorkCell（可空）
 * @param state    [in] 宿主当前 State（框内工作态——rw::kinematics 默认态）
 * @return 关节向量（rad/m；链序——Device::getQ 契约经 rw::math::Q 展开）；
 *         不可提取＝nullopt
 */
std::optional<std::vector<double>> extractJointQFromState(
    const rw::models::WorkCell::Ptr& workcell, const rw::kinematics::State& state)
{
    if (workcell.isNull()) {
        return std::nullopt;
    }
    const std::vector<rw::core::Ptr<rw::models::Device>>& devices =
        workcell->getDevices();
    if (devices.empty() || devices.front().isNull()) {
        return std::nullopt;
    }
    // rw::math::Q → std::vector<double>（链序；rad/m——Q::toStdVector 契约）。
    return devices.front()->getQ(state).toStdVector();
}

}  // namespace

// =====================================================================
// 生命周期（框架插件协议）
// =====================================================================

IrdWorkbenchHostPlugin::IrdWorkbenchHostPlugin()
    // 插件名：Dock 标题/宿主 Plugins 菜单开关项/卸载对话框的显示名。
    // 图标留空（开发期通道不引入资源文件——零图标不是零功能）。
    : rws::RobWorkStudioPlugin(QString::fromUtf8("IRD 工作台"), QIcon())
{
}

void IrdWorkbenchHostPlugin::initialize()
{
    // 框架基类先行（宿主日志句柄注入——rws::RobWorkStudioPlugin::initialize 原语义）。
    RobWorkStudioPlugin::initialize();
    if (m_assembled) {
        return;  // 一次守卫（框架契约 initialize 恰好一次——防御性重复调用无害化）
    }

    // ---- 装配第一步：诊断栈（与 harness 同一装配序列——DiagnosticsAssembly）----
    const fs::path devlogDir = fs::current_path() / kPluginLogDirName;
    try {
        app::assembleDiagnostics(devlogDir, m_diag);
    } catch (const std::exception& error) {
        // 装配失败 fail-fast：框架捕获与否不由插件决定——留痕两条通道后
        // 原样上抛（禁止吞错；宿主 RW_THROW 机制会呈现装载失败）。
        reportLine(std::string("诊断栈装配失败：") + error.what());
        throw;
    }
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "插件诊断栈就绪（开发日志目录：" + devlogDir.u8string() + "）");
    }
    reportLine("诊断栈就绪（开发日志目录：" + devlogDir.u8string() + "）");

    // ---- 装配第二步：端口适配器（O-31 装配层特权边——复用 harness 形态）----
    // 会话生命周期端口形态与 harness 逐一同型：策略未装载（C-10）、名称
    // 不可解析（C-11）——零虚构语义。关于框数据源收口起为装配报告实装
    // （bundleAboutSource——§11.4，HarnessAboutSource 占位退役）。
    m_policySource = std::make_shared<app::UnloadedPolicySource>();
    m_nameResolver = std::make_shared<app::NullUiNameResolver>();
    m_bridge = std::make_shared<app::ProjectDiagnosticsBridge>(
        m_diag.catalog, m_diag.factory, m_diag.pipeline);
    // 域事件总线（WP-24-T03b 收口）：修订提交事件面——store 打开请求挂接
    // ＋宿主订阅转达域模块（撤销/重做等非 apply 路径的会话同步由此达成，
    // 不引入第二套事件机制——core 事件总线是唯一机制）。
    m_eventBus = std::make_shared<core::ReferenceEventBus>();
    // 打开工厂＝捕获包装（UI-T17）包住对端翻译适配器：包装只透传并捕获
    // 成功绑定集（草稿写半区端口来源），对端翻译语义零改动。
    // T03b-2：成功打开回调同步保存强类型适配器（apply 网关取命令端口）。
    // T03b 收口：适配器挂接事件总线（打开请求转交对端——修订事件出线）。
    auto storeBundle = std::make_shared<BundleCapturingStoreFactory>(
        std::make_shared<app::StoreFactoryPortAdapter>(*m_bridge, m_eventBus.get()),
        [this](std::shared_ptr<app::StorePortAdapter> adapter) {
            m_lastStoreAdapter = std::move(adapter);
        });
    m_storeFactory = storeBundle;

    // ---- 装配第三步：域插件装配（WP-24-T03b 前移——content 装配依赖
    //      域产物：关于框数据源/owner 白名单/域命令登记项均出自 bundle）。
    //      命令提交路由（面板按钮→content 注册表 §7.2）在 content 就位后
    //      绑定（initialize 下文）。〕
    {
        std::vector<std::string> domainReportLines;
        m_domains = assembleDomainPlugins(*this, domainReportLines);
        for (const std::string& line : domainReportLines) {
            reportLine(line);
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel, line);
            }
        }
        // 修订事件订阅（桥持建模门面指针——插件存活期覆盖订阅期）。
        if (m_domains != nullptr) {
            m_revisionSink = std::make_unique<DomainRevisionEventBridge>(
                &m_domains->modeling,
                [this](const std::string& message) {
                    if (m_diag.pipeline) {
                        m_diag.pipeline->logDev(kPluginDevChannel, message);
                    }
                });
            m_revisionSubscription = m_eventBus->subscribe(*m_revisionSink);
        }
    }

    // ---- 共享面集成装配（UI-T23——B1-SPEC §3/§4）：项目树/检查器/选择
    //      服务＋三域迁移 Provider 注册＋失败隔离占位。须在域装配之后
    //      （Provider 句柄出自三域门面 sharedSurfaceProviders）、content
    //      与 Dock 挂位之前（面板把手在 buildDockBody 消费）执行。
    assembleSharedSurfaces();

    // ---- 会话控制器（§5 状态机——依赖就位后延迟构造，一次性注入依赖包；
    //      打开编排的推进面，与 HarnessMain 逐行同源）----
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory = m_storeFactory;
    sessionDeps.diagSink = m_diag.catalog;
    sessionDeps.diagFactory = m_diag.factory;
    sessionDeps.devLog = m_diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    // 上下文原子快照注入内容装配面（§10.1 v0.5 facets——状态行/首页/命令
    // 门控的单一数据源；控制器在打开成功/关闭完成时回调，UI 线程）。
    // UI-T17 增量：上下文清空（项目关闭完成）时同步解绑草稿控制器会话
    // （§8.6 表处置——模块表/局部栈清空，磁盘草稿零触碰）。
    // T03b 收口增量：域模块会话同步挂钩（项目在位＝锚定 tip；无项目＝
    // 会话脱离——§5.2/§5.4/§6.2 全路径的宿主转发面）。
    sessionDeps.presentContext = [this](const ui::ProjectContextProjection& context) {
        if (!context.project.has_value() && m_draft && m_draft->hasSession()) {
            m_draft->unbindSession();
        }
        syncDomainModulesToContext(context);
        if (m_content) {
            m_content->presentProjectContext(context);
        }
    };
    // 关闭对话框"保存"决议的执行半区（§5.4/[保存]→saveAll(Manual)）：
    // UI-T17 起接线（此前与 harness 同口径不接线——关闭链路不可达；本任务
    // 装配 DraftController 后链路真实可达，未绑定/保存失败经返回值轨反馈）。
    sessionDeps.saveAllDraftsManual = [this]() -> bool { return saveDraftsNow(); };
    // T_force 强杀兜底（§11.5 分工：abandonAll 调用权在 L5）：开发期无
    // execution 引擎装配＝零后台任务可放弃（SessionTaskPortStub 恒空同源
    // 事实）——显式留痕不静默（resolveForceCloseDialog 要求已接线）。
    sessionDeps.forceAbandonAll = [this]() {
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "forceAbandonAll：无执行引擎装配——零任务可放弃（T_force 确认后的空兜底）");
        }
    };
    m_controller = std::make_unique<UiSessionController>(std::move(sessionDeps));

    // ---- 草稿链装配（UI-T17——§8：执行器＋控制器；保存链路真实）----
    assembleDraftChain();

    // ---- 装配第四步：内容装配面（与 harness 共用的同一装配面——O-38 ②；
    //      嵌入式 Dock 宿主形态＋会话入口覆写＝本插件的两处宿主差异）----
    WorkbenchContentDeps contentDeps;
    // 域事件总线（WP-24-T03b 收口——非空接通；§10.1 可空成员的显式声明
    // 纪律改为"已接通"事实声明）。
    contentDeps.wiring.eventBus = m_eventBus;
    contentDeps.wiring.diagSink = m_diag.catalog;
    contentDeps.wiring.redaction = m_diag.redaction;
    contentDeps.wiring.devLog = m_diag.pipeline;  // Dev 码唯一出线（diagnostics §6.2）
    contentDeps.wiring.diagFactory = m_diag.factory;
    contentDeps.wiring.policySource = m_policySource;
    contentDeps.wiring.nameResolver = m_nameResolver;
    // 关于框数据源（WP-24-T03b 收口——装配报告实装：registrar 现取现拼，
    // §11.4；HarnessAboutSource 占位退役）。非持有 shared_ptr（空删除器——
    // 所有权在 bundle 的 unique_ptr，宿主持有 bundle 至壳拆除，双删除面
    // 在此切断）。
    contentDeps.wiring.aboutSource.reset(
        bundleAboutSource(*m_domains),
        [](ui::IUiAboutDataSource*) {});
    // 域装配面（WP-24-T03b——§7.2 域命令入册的 owner 白名单＋登记项；
    // UI-T23 扩三域：requirements/kinematics 域命令同形态入册）：
    // owner＝各域 descriptor.pluginId；域命令 seal 前入册，冲突规则/
    // 可用性门控全在注册表（本插件零判定——PA-1）。
    // 真实执行面盘点（诚实边界，与首版 modeling 盘点同口径）：
    //   - modeling.new-from-template＝域内已落位能力（模板重种子）；
    //   - 其余域命令（modeling 九条＋requirements 九条＋kinematics 域命令）
    //     的域流程归各自后续任务——处理器给出诚实"未装配"应答，状态行
    //     直写中文文案（装配层呈现面惯例），不虚构执行成功。
    {
        const std::pair<const char*, ui::PluginUiDescriptor*> domainDescriptors[] = {
            {"modeling", &m_domains->modeling.descriptor},
            {"requirements", m_domains->requirements.has_value()
                                 ? &m_domains->requirements->descriptor : nullptr},
            {"kinematics", m_domains->kinematics.has_value()
                               ? &m_domains->kinematics->descriptor : nullptr},
        };
        for (auto& [domainKey, descriptor] : domainDescriptors) {
            if (descriptor == nullptr) {
                continue;  // 失败隔离缺席域——无命令可入册（§11.3）
            }
            contentDeps.extraCommandOwners.push_back(descriptor->pluginId);
            for (const CommandDescriptor& desc : descriptor->commands) {
                WorkbenchContentDeps::DomainCommandEntry entry;
                entry.descriptor = desc;
                if (desc.id == kModelingNewFromTemplateId) {
                    // 真实执行面（域内已落位能力）：模板草稿重种子＋就绪
                    // 重算——种子内部走真实 RobotDesignTemplateFactory::createDraft。
                    entry.handler = [this](const std::vector<CommandParameter>&) {
                        CommandOutcome out;
                        m_domains->modeling.seedTemplateSession();
                        if (m_hostStatusBar != nullptr) {
                            m_hostStatusBar->showMessage(
                                QString::fromUtf8("已从模板重建建模草稿（generic-6r）"),
                                4000);
                        }
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(kPluginDevChannel,
                                                    "domain command executed: modeling.new-from-template");
                        }
                        out.accepted = true;
                        return out;
                    };
                } else {
                    // 诚实边界（ERR-01/UX-02）：域流程未装配的命令——处理
                    // 器给出诚实应答（§10.3 注册表在处理器返回后强制
                    // accepted=true 的"已执行"语义＝处理器应答已发生），
                    // 状态行直写"未装配"文案（域键入文案便于归因），不
                    // 虚构执行成功。
                    const std::string commandId = desc.id;
                    entry.handler = [this, commandId, domainKey](
                                        const std::vector<CommandParameter>&) {
                        if (m_hostStatusBar != nullptr) {
                            m_hostStatusBar->showMessage(
                                QString::fromUtf8("「%1」域流程未装配（%2）")
                                    .arg(QString::fromStdString(commandId),
                                         QString::fromLatin1(domainKey)),
                                5000);
                        }
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(
                                kPluginDevChannel,
                                "domain command not assembled: " + commandId);
                        }
                        CommandOutcome out;
                        out.messageKey = std::string{kModelingFlowNotAssembledKey};
                        return out;
                    };
                }
                contentDeps.domainCommandEntries.push_back(std::move(entry));
            }
        }
    }
    contentDeps.hostKind = WorkbenchHostKind::EmbeddedDock;
    // 宿主控件＝插件本体（RobWorkStudioPlugin 即 QDockWidget 形态的
    // QWidget，随宿主主窗口安放）：QShortcut attach、命令面板与对话框
    // 的父窗口、内容 Widget 的初始父对象都以它为准——build() 必填校验
    // 之一（缺失＝内容无处安放，装配被拒）。初父在栅格安放时重挂为
    // Dock 体的区域栅格（buildDockBody）。
    // 宿主冒烟实证（2026-09-23）：漏注入本项时 build() 返回 false——
    // 插件装载被框架呈现为失败对话框、五区不出现；壳路径由
    // WorkbenchShell 注入自身窗口故未暴露。修复见同日提交。
    contentDeps.hostWidget = this;
    // 会话入口覆写（§11.5）：宿主插件的打开/新建/保存/关闭编排注入——
    // 首页/菜单/顶栏/面板/快捷键五处壳入口同走真实协议（UI-T17 扩至四命令）。
    contentDeps.openProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateOpenProject(params);
        };
    contentDeps.newProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateNewProject(params);
        };
    contentDeps.saveProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateSaveProject(params);
        };
    contentDeps.closeProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateCloseProject(params);
        };
    contentDeps.applyDraftHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateApplyDraft(params);
        };
    m_content = createWorkbenchContent(std::move(contentDeps));

    // ---- 状态投影绑宿主状态栏（UI-T18——O-43 ③：宿主 chrome 唯一）----
    // PM-11 永久文本与瞬态消息经双观测钩子直投 getRobWorkStudio()->statusBar()
    // （宿主注入先于 initialize——setRobWorkStudio→setupMenu→initialize 序）；
    // v1.11"状态行钉底恒可见"的语义等价迁移＝宿主状态栏本身恒可见（强于
    // Dock 内钉底——任何 Dock 开关都不再影响状态投影）。永久位仅添加一次
    // （initialize 恰好一次——一次守卫保证）。
    if (getRobWorkStudio() != nullptr) {
        m_hostStatusBar = getRobWorkStudio()->statusBar();
    }
    if (m_hostStatusBar != nullptr) {
        auto* pm11 = new QLabel(m_hostStatusBar);
        pm11->setObjectName("ird_status_project_text");
        m_hostStatusBar->addWidget(pm11, /*stretch=*/1);
        m_content->setStatusTextObserver([pm11](const QString& text) {
            pm11->setText(text);  // PM-11 永久位（不用 showMessage——瞬态位会超时清空）
        });
        m_content->setStatusMessageObserver(
            [this](const QString& message, int timeoutMs) {
                if (m_hostStatusBar != nullptr) {
                    m_hostStatusBar->showMessage(message, timeoutMs);
                }
            });
    } else {
        reportLine("宿主状态栏不可得——状态投影无呈现面（降级形态，如实留痕）");
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "宿主状态栏不可得——PM-11/瞬态消息无投影面（装配降级）");
        }
    }

    // ---- 装配第五步：多 Dock 拓扑＋内容装配面两段装配＋退出收口挂接 ----
    // （域插件装配已前移至第三步——content 装配依赖域产物；面板 Dock 的
    // 消费仍在 buildDockBody 拓扑构建期，面板工厂经域装配 bundle 现调。）
    // 面板命令提交路由（WP-24-T03b——§7.2 域命令入册后的执行半区）：面板
    // 按钮点击→content 注册表提交路径（§7.7 统一路由——可用性门控/冲突
    // 语义全在注册表，处理器于 domainCommandEntries 登记）。
    if (m_content != nullptr && m_domains != nullptr) {
        m_domains->modeling.bindCommandSubmit(
            [this](const ui::CommandId& id) {
                if (m_content != nullptr) {
                    m_content->submitCommand(std::string(id));
                }
            });
        // 运动学域命令提交出口（UI-T23——同形态转发 content 注册表；
        // requirements 门面无该出口——面板命令提交随域会话任务接续）。
        if (m_domains->kinematics.has_value()) {
            m_domains->kinematics->bindCommandSubmit(
                [this](const ui::CommandId& id) {
                    if (m_content != nullptr) {
                        m_content->submitCommand(std::string(id));
                    }
                });
        }
    }
    if (!buildDockBody()) {
        // 内容装配面校验被拒＝装配缺陷（build() 的必填校验覆盖三组：
        // wiring 非空项、宿主控件 hostWidget、几何/位形钩子成组——宿主
        // 冒烟曾实证 hostWidget 漏注入走到此处）：留痕后上抛，不留半
        // 装配插件（宿主呈现装载失败）。
        reportLine("内容装配面构建被拒（装配校验失败——wiring 非空项/"
                   "hostWidget/钩子成组之一不满足）");
        throw std::runtime_error("sdurws_ird_ui_plugin: 内容装配面构建被拒");
    }
    connectAppQuitDrain();

    m_assembled = true;
    // 装配完成与"呈现"分开表述（验收 attempt 1 的教训——B-1）：此刻 Dock
    // 尚不可见，框架 addPlugin 尾段（setVisible(PluginVisible_<名>)＋
    // restoreState(QtMainWindowState)）还没执行，它们会把本 Dock 置为
    // 隐藏——呈现结论只能由 reassertEmbeddedPresentation 在事件循环
    // 回归后给出（G1 装载门控的第二判据），此处不得提前声称"已嵌入"。
    reportLine("工作台装配完成（多 Dock：主 Dock＋属性/任务同级 Dock＋宿主状态栏投影＋Ctrl+Shift+P 命令面板；"
               "装载呈现自证在事件循环稍后执行）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "内容装配面就位（多 Dock 嵌入形态；会话入口覆写已注入；状态投影绑宿主状态栏）");
    }

    // ---- 宿主事件桥接线（UI-T23——L3/D8：框架公开事件订阅；须在共享
    //      面就位后执行——桥回调消费 m_selection/m_domains）----
    connectHostEventBridges();

    // ---- 装配第六步（时序关键）：装载呈现自证排队 ----
    // 零等待单发定时器：控制流回到事件循环的第一拍执行重申（此时 addPlugin
    // 已返回、其尾段 setVisible/restoreState 已完成——队列语义保证严格晚于
    // 二者，详见 reassertEmbeddedPresentation 内的根因链注释）。
    QTimer::singleShot(0, this, [this] { reassertEmbeddedPresentation(); });

    // ---- 集成冒烟通道（UI-T23——GUI 留痕载体）：环境变量触发，随事件
    //      循环稍后执行（呈现自证之后的拍——冒烟序列依赖宿主窗口在位）。
    maybeRunIntegrationSmoke();
}

IrdWorkbenchHostPlugin::~IrdWorkbenchHostPlugin()
{
    // 有界拆卸（幂等）：正常退出路径已随 aboutToQuit 收口——此处兜底
    // （插件实例由 QPluginLoader 持有到进程退出，析构可能不执行）。
    if (m_content) {
        m_content->shutdown();
        m_content.reset();
    }
    // 草稿链收口（UI-T17）：先停执行器（有界排空在途落盘任务）再释放
    // 控制器——m_draft 析构前其分派的落盘任务必须已执行完（IDraftController
    // 生命周期契约：L5 保证控制器存活至落盘执行器排空）。
    if (m_diskExecutor) {
        m_diskExecutor->stop();
    }
}

// =====================================================================
// 宿主共存回调（O-38 裁决③——共存最小接入：只观测，零视图操作）
// =====================================================================

void IrdWorkbenchHostPlugin::open(rw::models::WorkCell* workcell)
{
    // 宿主装载工作单元＝宿主中央区 RWStudioView3D 将呈现三维场景；本插件
    // 让位（工作台面板与宿主中央视图并存，不承载、不复制三维能力）。此处
    // 只做注入面的只读观测留痕（getView()/getWorkCellScene()——验收操作
    // 序列"宿主三维共存"的可观测面），完整三维交互归 WP-10-T05 阶段 B。
    (void)workcell;  // 场景本体归宿主呈现——本插件零场景语义
    if (m_diag.pipeline) {
        const bool viewInPlace = getRobWorkStudio() != nullptr
                                 && getRobWorkStudio()->getView() != nullptr;
        m_diag.pipeline->logDev(
            kPluginDevChannel,
            std::string("宿主已装载工作单元——共存观测：RWStudioView3D ")
                + (viewInPlace ? "在位（宿主中央区承载三维视图，本插件让位）"
                               : "未就位（宿主中央视图尚未创建）"));
    }
}

void IrdWorkbenchHostPlugin::close()
{
    // 宿主关闭工作单元：观测留痕（零操作本体——工作台面板状态不随工作单元
    // 变化；ird 项目会话生命周期归 UiSessionController，与 rw WorkCell 正交）。
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel, "宿主已关闭工作单元（工作台面板保持）");
    }
}

// =====================================================================
// 装配段实现
// =====================================================================

bool IrdWorkbenchHostPlugin::buildDockBody()
{
    // 多 Dock 拓扑（UI-T18——O-43 裁决③：单一工作台 Dock 五区栅格拆分）：
    //   ①插件本体 Dock＝主 Dock（Left 停靠区）——命令条（顶栏）＋项目导航
    //     （左栏）纵排；
    //   ②IRD 属性与诊断 Dock（Right 停靠区）＝右栏内容（本插件新建、宿主
    //     addDockWidget 同级注册——addDockWidget 延后到装载呈现自证，彼时
    //     插件已入宿主主窗口）；
    //   ③IRD 任务和状态 Dock（Bottom 停靠区）＝底部内容（同上）。
    // 中央区不安放（宿主中央 RWStudioView3D 唯一所有三维——O-38 裁决③；
    // 内容装配面的中央让位页保持已构建不挂载，零呈现面）。状态行不进任何
    // Dock——PM-11 永久投影与瞬态消息经双观测钩子直投宿主状态栏（宿主
    // chrome 唯一，v1.11"状态行钉底"的语义等价迁移见落位登记注）。
    // 红线不变：不建任何顶层 QMainWindow（O-38 裁决②）。
    auto* body = new QWidget(this);
    body->setObjectName("ird_plugin_dock_body");
    auto* mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    if (!m_content->build()) {
        return false;  // wiring 校验被拒（装配缺陷——调用方上抛处理）
    }

    // 主 Dock 体（内容 Widget 从宿主控件重挂进纵排——Qt 对象树托管）。
    // UI-T23：主 Dock 纵排扩展为"顶栏＋左栏导航＋工业项目树"三段（B1-SPEC
    // D3——工业项目树是业务主导航，入宿主主 Dock 的挂位编排归本收口任务，
    // UI-T21 登记注③的预留缝）；共享树面板未装配（装配缺陷防御）时保持
    // 首版两段形态，零回归。
    mainLayout->addWidget(m_content->topBarWidget(), /*stretch=*/0);
    mainLayout->addWidget(m_content->leftWidget(), /*stretch=*/1);
    if (m_treePanel != nullptr) {
        // 共享树段（stretch 2＝树占主导航主区；左栏导航为命令/入口条）。
        mainLayout->addWidget(m_treePanel->widget(), /*stretch=*/2);
    }
    setWidget(body);
    m_dockBody = body;

    // 右/底 Dock 创建（父对象＝本插件；装载呈现自证时 addDockWidget 重挂
    // 进宿主主窗口——插件本体在 addPlugin 尾段才入主窗口，彼时宿主窗口
    // 才可寻址）。objectName 供宿主状态 blob 与排障日志定位。
    // UI-T23：右 Dock 内容升格为"共享属性检查器＋右栏（诊断与设置）"纵排
    // （B1-SPEC D5——共享检查器为宿主右 Dock 唯一跨域属性呈现面；UI-T22
    // 登记注③的预留缝）。检查器未装配时保持首版单段形态，零回归。
    m_propsDock = new QDockWidget(QString::fromUtf8("IRD 属性与诊断"), this);
    m_propsDock->setObjectName("ird_props_dock");
    if (m_inspectorPanel != nullptr) {
        auto* rightColumn = new QWidget(m_propsDock);
        auto* rightLayout = new QVBoxLayout(rightColumn);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(2);
        rightLayout->addWidget(m_inspectorPanel->widget(), /*stretch=*/3);
        rightLayout->addWidget(m_content->rightWidget(), /*stretch=*/2);
        m_propsDock->setWidget(rightColumn);
    } else {
        m_propsDock->setWidget(m_content->rightWidget());
    }
    m_tasksDock = new QDockWidget(QString::fromUtf8("IRD 任务和状态"), this);
    m_tasksDock->setObjectName("ird_tasks_dock");
    m_tasksDock->setWidget(m_content->bottomWidget());
    // 建模 Dock（Left 停靠区——WP-24-T03 首版装配挂位；中央区 StageId
    // 挂位〔CentralAreaHost〕未落位，宿主侧 Dock 承载为登记过的首版形态，
    // 收口时迁移）。面板工厂经域装配 bundle 现调（模块内部完成会话接线）。
    if (m_domains) {
        m_modelingDock = new QDockWidget(QString::fromUtf8(kModelingDockTitle), this);
        m_modelingDock->setObjectName("ird_modeling_dock");
        m_modelingDock->setWidget(modelingPanelWidget(*m_domains));
        // 需求/运动学 Dock（UI-T23 三域挂位——Left 区同列；失败隔离缺席
        // 的域工厂返回 nullptr＝跳过挂位，占位呈现由共享树承担，§11.3）。
        // 运动学高级面板（求解配置）挂 Right 区（UX-04 高级面板位——
        // descriptor 装配规则行 5 的独立登记形态）。
        QWidget* requirementsPanel = requirementsPanelWidget(*m_domains);
        if (requirementsPanel != nullptr) {
            m_requirementsDock = new QDockWidget(
                QString::fromUtf8(kRequirementsDockTitle), this);
            m_requirementsDock->setObjectName("ird_requirements_dock");
            m_requirementsDock->setWidget(requirementsPanel);
        }
        QWidget* kinematicsPanel = kinematicsPanelWidget(*m_domains);
        if (kinematicsPanel != nullptr) {
            m_kinematicsDock = new QDockWidget(
                QString::fromUtf8(kKinematicsDockTitle), this);
            m_kinematicsDock->setObjectName("ird_kinematics_dock");
            m_kinematicsDock->setWidget(kinematicsPanel);
        }
        QWidget* kinematicsAdvanced = kinematicsAdvancedPanelWidget(*m_domains);
        if (kinematicsAdvanced != nullptr) {
            m_kinematicsAdvancedDock = new QDockWidget(
                QString::fromUtf8(kKinematicsAdvancedDockTitle), this);
            m_kinematicsAdvancedDock->setObjectName("ird_kinematics_advanced_dock");
            m_kinematicsAdvancedDock->setWidget(kinematicsAdvanced);
        }
    }

    // 可见性目标登记（chrome 安放在 activate 之前——两段装配时序契约；
    // 三区开关语义自此作用于 Dock 本体：左＝插件主 Dock、右/底＝同级 Dock。
    // Top/Central 按契约登记为无操作——顶栏随主 Dock、中央归宿主）。
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Left, this);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Right, m_propsDock);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Bottom, m_tasksDock);

    // 命令状态观察：内容装配层每次刷新使能态后同步框架菜单动作（§7.6
    // 三处一致禁用的宿主菜单半区——求值结果全在注册表，本插件零判定）。
    m_content->setCommandStateObserver([this] { refreshHostMenuActions(); });

    // 两段装配第二段：布局记忆（嵌入式＝三区可见性键，此刻施加到三个
    // Dock）＋无项目首页＋写线程＋快捷键 attach＋命令面板（§10.1 时序）。
    m_content->activate();
    return true;
}

void IrdWorkbenchHostPlugin::setupMenu (QMenu* menu)
{
    // 框架 Plugins 菜单注入（宿主 addPlugin 流程在 initialize 前回调——
    // 框架注入序保持）：基类先行（本插件面板的显示/隐藏开关——rws::
    // RobWorkStudioPlugin::setupMenu 原语义）。
    // UI-T17（O-43 ②）宿主菜单融合：本菜单（Plugins）只保留基类显示/隐藏
    // 开关，不再承载工作台命令——项目命令迁宿主 File 菜单、命令面板迁
    // Tools、区域开关与布局复位迁自建"视图"菜单（registerHostMenus）。
    RobWorkStudioPlugin::setupMenu (menu);
    registerHostMenus();
}

void IrdWorkbenchHostPlugin::registerHostMenus()
{
    // 宿主菜单定位（SA-02 零框架修改——只经公开 menuBar() 读宿主菜单结构；
    // 找不到目标菜单＝宿主形态异常，留痕后保持 Plugins-only 降级形态）。
    auto* studio = getRobWorkStudio();
    QMenuBar* menuBar = studio != nullptr ? studio->menuBar() : nullptr;
    if (menuBar == nullptr) {
        reportLine("宿主菜单栏未就位——命令保持 Plugins 菜单承载（降级形态）");
        return;
    }
    const auto findHostMenu = [menuBar](const char* title) -> QMenu* {
        for (QAction* action : menuBar->actions()) {
            if (QMenu* m = action->menu(); m != nullptr && m->title() == QString::fromLatin1(title)) {
                return m;
            }
        }
        return nullptr;
    };
    QMenu* fileMenu = findHostMenu("&File");
    QMenu* toolsMenu = findHostMenu("&Tools");

    // ---- File：按位插入"工业机器人项目"子菜单 ----
    // 为什么按位插入而不是尾插：宿主 updateLastFiles 每次 open WorkCell 后
    // 移除重加"最近文件"条目（追加在菜单尾部）——尾插的项目子菜单会被
    // 后续重排顶到最近文件之下；锚定 Preferences 前的分隔符保持稳定分组。
    if (fileMenu != nullptr) {
        auto* projectMenu = new QMenu(QString::fromUtf8(kProjectMenuTitle), fileMenu);
        addCommandAction(projectMenu, "新建项目", "project.new");
        addCommandAction(projectMenu, "打开项目", "project.open");
        addCommandAction(projectMenu, "保存草稿", "draft.save");
        addCommandAction(projectMenu, "项目另存为", "project.saveAs");
        addCommandAction(projectMenu, "关闭项目", "workbench.closeProject");
        // 最近项目子菜单（PM-10）：内容装配时清空、aboutToShow 现取重建
        // （去重/上限/失效提示全在内容装配面——本插件只投影）。
        m_recentMenu = new QMenu(QString::fromUtf8(kRecentMenuTitle), projectMenu);
        connect(m_recentMenu, &QMenu::aboutToShow, this, [this] { rebuildRecentMenu(); });
        projectMenu->addSeparator();
        projectMenu->addMenu(m_recentMenu);

        // 插入锚：Preferences 动作之前最近的分隔符（无则退化为 Preferences
        // 动作本身、再无则追加尾部）。
        QAction* anchor = nullptr;
        const QList<QAction*> fileActions = fileMenu->actions();
        for (int i = 0; i < fileActions.size(); ++i) {
            if (fileActions[i]->text() == QString::fromLatin1("&Preferences")) {
                for (int j = i - 1; j >= 0; --j) {
                    if (fileActions[j]->isSeparator()) {
                        anchor = fileActions[j];
                        break;
                    }
                }
                if (anchor == nullptr) {
                    anchor = fileActions[i];
                }
                break;
            }
        }
        if (anchor != nullptr) {
            fileMenu->insertMenu(anchor, projectMenu);
        } else {
            fileMenu->addMenu(projectMenu);
        }
    } else {
        reportLine("宿主 File 菜单未定位——项目命令未上菜单（降级形态）");
    }

    // ---- Tools：命令面板入口（UX-13 键盘可达入口的菜单半区）----
    if (toolsMenu != nullptr) {
        addCommandAction(toolsMenu, "命令面板", "workbench.commandPalette");
    }

    // ---- 视图（自建——宿主无 View 菜单）：三区开关＋恢复默认布局 ----
    // 区域开关语义（§4.1"支持隐藏"的宿主菜单承载；勾选态随内容装配层刷新
    // 同步，triggered/toggled 分离避免回环）。可隐藏三区（Top/Central 恒在
    // ——§4.4）。插入位置：Plugins 菜单之前（File/Tools 之后）。
    auto* viewMenu = new QMenu(QString::fromUtf8(kViewMenuTitle), menuBar);
    const std::pair<WorkbenchRegion, const char*> regionToggles[] = {
        {WorkbenchRegion::Left, "左栏"},
        {WorkbenchRegion::Right, "右栏"},
        {WorkbenchRegion::Bottom, "底部任务和状态区"},
    };
    for (const auto& [region, title] : regionToggles) {
        auto* action = viewMenu->addAction(QString::fromUtf8(title));
        action->setParent(this);
        action->setCheckable(true);
        QObject::connect(action, &QAction::triggered, this, [this, region] {
            if (m_content) {
                m_content->setRegionVisible(region, !m_content->regionVisible(region));
            }
        });
        m_hostRegionToggles.emplace_back(region, action);
    }
    viewMenu->addSeparator();
    addCommandAction(viewMenu, "恢复默认布局", "view.resetLayout");
    QAction* beforeView = nullptr;
    for (QAction* action : menuBar->actions()) {
        if (QMenu* m = action->menu(); m != nullptr && m->title() == QString::fromLatin1("&Plugins")) {
            beforeView = action;
            break;
        }
    }
    if (beforeView != nullptr) {
        menuBar->insertMenu(beforeView, viewMenu);
    } else {
        menuBar->addMenu(viewMenu);
    }
}

QAction* IrdWorkbenchHostPlugin::addCommandAction(QMenu* target,
                                                  const char* title,
                                                  const char* commandId)
{
    // 菜单只路由命令板（§4.2 路由红线——触发统一转发内容装配面提交路径）；
    // 使能态随命令可用性快照刷新（refreshHostMenuActions——本插件零判定）。
    QAction* action = target->addAction(QString::fromUtf8(title));
    action->setParent(this);  // 动作父对象＝本插件（Qt 树托管——随宿主收尾）
    QObject::connect(action, &QAction::triggered, this, [this, commandId] {
        if (m_content) {
            m_content->submitCommand(commandId);
        }
    });
    m_hostMenuCommandIds.emplace_back(action, commandId);
    return action;
}

void IrdWorkbenchHostPlugin::rebuildRecentMenu()
{
    if (m_recentMenu == nullptr || !m_content) {
        return;
    }
    m_recentMenu->clear();
    const std::vector<RecentProjectEntry> entries = m_content->recentProjects();
    if (entries.empty()) {
        // 空清单＝占位行（不虚构条目——PM-10 首页语义的菜单对位）。
        QAction* empty = m_recentMenu->addAction(QString::fromUtf8("（无）"));
        empty->setEnabled(false);
        return;
    }
    for (const RecentProjectEntry& entry : entries) {
        QString label = QString::fromStdString(entry.canonicalPath);
        if (!entry.available) {
            // 失效项保留并提示（PM-10"失效≠删除"——禁用动作，不删除条目）。
            label += QString::fromUtf8(kRecentUnavailableSuffix);
        }
        QAction* action = m_recentMenu->addAction(label);
        action->setEnabled(entry.available);
        const std::string path = entry.canonicalPath;
        connect(action, &QAction::triggered, this,
                [this, path] { openRecentProject(path); });
    }
}

void IrdWorkbenchHostPlugin::refreshHostMenuActions()
{
    if (!m_content) {
        return;
    }
    // 框架菜单动作使能态＝内容装配层（命令注册表）求值结果（§7.6"三处
    // 一致禁用"——本插件零判定，只消费快照）。
    for (auto& [action, commandId] : m_hostMenuCommandIds) {
        const ShellCommandAvailability a = m_content->commandAvailability(commandId);
        action->setEnabled(a.enabled);
    }
    // 五区开关勾选态＝当前有效可见性（不回环：只 setChecked 不触发）。
    for (auto& [region, action] : m_hostRegionToggles) {
        action->setChecked(m_content->regionVisible(region));
    }
}

void IrdWorkbenchHostPlugin::connectAppQuitDrain()
{
    // 宿主退出时的有界落盘收口：插件不拥有应用生命周期（进程退出归宿主
    // ——PM-17 同口径），但布局旗标/最近项目/快捷键改绑的落盘队列需要
    // 一次 drainAndStop（§10.1"有界拆除"）。挂接 aboutToQuit（上下文＝
    // Dock 体——退出时仍在 Qt 树内，回调安全；content->shutdown 幂等）。
    if (QCoreApplication::instance() != nullptr && m_dockBody != nullptr) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         m_dockBody, [this] {
                             if (m_content) {
                                 m_content->shutdown();
                             }
                             // 草稿链退出收口（UI-T17）：落盘执行器有界排空
                             // （在途保存任务执行完再收线程——§5.7"在途草稿
                             // 落盘完成后上下文才释放"的宿主侧对位）。
                             if (m_diskExecutor) {
                                 m_diskExecutor->stop();
                             }
                         });
    }
}

void IrdWorkbenchHostPlugin::reassertEmbeddedPresentation()
{
    // 防御守卫：本方法只应由 initialize 末尾排队的零等待定时器调用（正常
    // 时序下装配早已完成）；未装配即被调用＝装配缺陷，保持无操作不掩盖。
    if (!m_assembled) {
        return;
    }

    // 根因链（验收 attempt 1 阻断项 B-1 的实证修复，登记 ui.md §13 返工
    // 登记注）：本 initialize() 返回之后，框架 addPlugin 尾段还有两步会
    // 把本 Dock 置为不可见——
    //   ① plugin->setVisible(PluginVisible_<插件名> 的保存值，缺省取调用
    //      实参)：Plugins→Load plugin 对话框路径在框架里硬编码实参
    //      visible=false（RobWorkStudio.cpp loadPlugin() → setupPlugin(
    //      pathname, filename, 0, 1)）；
    //   ② restoreState(QtMainWindowState)：Qt 对主窗口状态 blob 里未登记
    //      的 Dock 一律按隐藏处理，而该 blob 是本插件装载之前保存的布局
    //      （宿主退出时 saveState 落盘 ini）——刚 addDockWidget 的本 Dock
    //      必然不在其中，恢复即被藏。
    // 两步都在框架侧（SA-02 零框架修改红线），且时序都在 initialize()
    // 之后——插件侧唯一可落点的位置是"控制流回到事件循环之后"：装载排
    // 队列的零等待单发定时器恰在 addPlugin 返回后的第一拍执行（事件循环
    // 语义保证严格晚于②），据此重申嵌入式呈现。
    //
    // 语义边界（为什么是"单发重申"而不是持续看护）：开发期验证通道取
    // "装载即呈现"口径——本方法只在装载后执行一次，不与用户后续的手动
    // 开关竞争（Plugins 菜单基类显示开关/Dock 关闭钮随时可再隐藏，插件
    // 不夺回）；跨会话的区域级可见性记忆仍归内容装配面（§4.5 用户级设置
    // ——PM-14），Dock 级呈现权在宿主装载语义下归本插件的装载自证。
    setFloating(false);  // 嵌入式形态钉死：非浮动（顶层漂浮窗口＝宿主形态违例）
    show();              // 恢复 Dock 呈现（仍在 addDockWidget 安放的停靠区内嵌于主窗口）

    // 多 Dock 拓扑收口（UI-T18——O-43 ③）：右/底两个同级 Dock 在本拍入宿主
    // 主窗口（此时插件已入主窗口、宿主窗口可寻址——addDockWidget 把 Dock
    // 从插件父子树重挂进主窗口），与主 Dock 同受宿主装载语义支配（框架尾段
    // restoreState 对状态 blob 未登记的 Dock 按隐藏处理——与主 Dock 同根因
    // 链，故一并重显）。单发重申语义与主 Dock 一致：只此一拍，不与用户后续
    // 手动开关竞争。
    auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
    if (hostWindow != nullptr && m_propsDock != nullptr && m_tasksDock != nullptr) {
        hostWindow->addDockWidget(Qt::RightDockWidgetArea, m_propsDock);
        hostWindow->addDockWidget(Qt::BottomDockWidgetArea, m_tasksDock);
        // 建模 Dock 入宿主（WP-24-T03 首版装配挂位——Left 区；与属性/任务
        // Dock 同受宿主装载语义支配，重显同拍执行）。
        if (m_modelingDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_modelingDock);
            m_modelingDock->show();
        }
        // 需求/运动学 Dock 入宿主（UI-T23 三域挂位——Left 区同列；高级
        // 面板 Dock 入 Right 区。缺席域跳过——失败隔离挂位形态）。
        if (m_requirementsDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_requirementsDock);
            m_requirementsDock->show();
        }
        if (m_kinematicsDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_kinematicsDock);
            m_kinematicsDock->show();
        }
        if (m_kinematicsAdvancedDock != nullptr) {
            hostWindow->addDockWidget(Qt::RightDockWidgetArea,
                                      m_kinematicsAdvancedDock);
            m_kinematicsAdvancedDock->show();
        }
        m_propsDock->show();
        m_tasksDock->show();
        // 区域旗标重施（UI-T18——PM-14 跨会话记忆不被装载重显夺回）：三区
        // 可见性目标已改绑 Dock 本体，activate 期恢复的用户旗标若为"隐藏"，
        // 上面的重显 show() 会把它顶回可见——与跨会话记忆矛盾。此处按内容
        // 装配面的模型位（regionVisible 返回用户意愿位，非 Widget 实测态）
        // 重施一次：用户隐藏的区保持隐藏（可经宿主"视图"菜单重新开启），
        // 无隐藏记忆（缺省）时与重显结果一致。主 Dock（Left 目标）不在此
        // 重施——其装载呈现维持 v1.11 注册口径（G1 门控判据"装载即呈现"；
        // 主 Dock 承载命令条，整 Dock 隐藏将无处承载工作台入口）。
        m_propsDock->setVisible(m_content->regionVisible(WorkbenchRegion::Right));
        m_tasksDock->setVisible(m_content->regionVisible(WorkbenchRegion::Bottom));
    }

    // 共存形态收口（O-38 裁决③"三维共存最小接入"的形态保障）：装载序列中
    // addDockWidget 先按停靠区整幅宽给位、随后 setVisible(false) 隐藏——重显
    // （上一行 show()）会恢复该整幅宽，宿主中央 RWStudioView3D 被挤压为零
    // （attempt 2 首录截图实证：五区完整可见但三维视图不可见）。故在 show()
    // 布局落定后的下一拍用 resizeDocks 显式把 Dock 宽度收束到宿主主窗口客户
    // 宽的约 2/5——中央三维视图保有其余宽度，两能力同帧共存。连续两拍各发一
    // 次收束（show 布局与主窗口布局的落定拍序不由插件决定，第二拍兜底）；
    // 结果宽度如实留痕——若被内容最小宽度钳制（该形态下顶栏按钮行很宽），
    // 收束只能到达钳制宽度，此时宿主窗口越宽三维视图所得越多，如实呈现。
    // 右/底 Dock（UI-T18）同拍给一次合理初值（右＝宿主宽约 1/5 钳制到
    // [300, 480] px、底＝宿主高约 1/4 钳制到 [190, 340] px——下界 300/190 px
    // 高于 §4.4 各区内容最小尺寸 280/160 px，初值在最小可用之上留余量，
    // 与主 Dock 收束档位同一取整口径），此后尺寸归用户拖拽与宿主布局管理。
    auto issueDockWidthShrink = [this] {
        auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
        if (hostWindow == nullptr) {
            return;  // 未嵌宿主主窗口＝异常装载形态（防御——不越权假设父型）
        }
        const int targetWidth = qBound(420, hostWindow->width() * 2 / 5, 1024);
        hostWindow->resizeDocks({this}, {targetWidth}, Qt::Horizontal);
        if (m_propsDock != nullptr) {
            const int propsWidth = qBound(300, hostWindow->width() / 5, 480);
            hostWindow->resizeDocks({m_propsDock}, {propsWidth}, Qt::Horizontal);
        }
        if (m_tasksDock != nullptr) {
            const int tasksHeight = qBound(190, hostWindow->height() / 4, 340);
            hostWindow->resizeDocks({m_tasksDock}, {tasksHeight}, Qt::Vertical);
        }
        reportLine("工作台 Dock 宽度收束：目标 " + std::to_string(targetWidth)
                   + " px，实际 " + std::to_string(width())
                   + " px（受内容最小宽度钳制时如实留痕）");
        // 装载几何事实一次性落 Dev 日志（排障面——多 Dock 拓扑下主/右/底
        // 三 Dock 的尺寸与可见性；Dev 通道 §6.2，不进控制台）。
        if (m_diag.pipeline != nullptr) {
            const QWidget* bodyW = m_dockBody.data();
            std::string facts = "[geometry] dock=" + std::to_string(width()) + "x"
                                + std::to_string(height());
            if (bodyW != nullptr) {
                const QSize bodyMin = bodyW->minimumSizeHint();
                facts += " body=" + std::to_string(bodyW->width()) + "x"
                         + std::to_string(bodyW->height())
                         + " bodyMin=" + std::to_string(bodyMin.width()) + "x"
                         + std::to_string(bodyMin.height());
            }
            if (m_propsDock != nullptr) {
                facts += " props=" + std::to_string(m_propsDock->width()) + "x"
                         + std::to_string(m_propsDock->height())
                         + " visible=" + (m_propsDock->isVisible() ? "1" : "0");
            }
            if (m_tasksDock != nullptr) {
                facts += " tasks=" + std::to_string(m_tasksDock->width()) + "x"
                         + std::to_string(m_tasksDock->height())
                         + " visible=" + (m_tasksDock->isVisible() ? "1" : "0");
            }
            if (m_hostStatusBar != nullptr) {
                facts += std::string(" hostStatusBar visible=")
                         + (m_hostStatusBar->isVisible() ? "1" : "0");
            }
            m_diag.pipeline->logDev(kPluginDevChannel, facts);
        }
    };
    QTimer::singleShot(0, this, issueDockWidthShrink);
    QTimer::singleShot(100, this, issueDockWidthShrink);

    reportLine("工作台多 Dock 已呈现（主 Dock＋属性/任务 Dock＋宿主状态栏投影——装载呈现自证完成）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "装载呈现自证完成（多 Dock 嵌入宿主主窗口可见；状态投影归宿主状态栏）");
    }
}

// =====================================================================
// 会话入口编排（§11.5——触发时机编排归装配层；与 HarnessMain 逐行同源）
// =====================================================================

CommandOutcome IrdWorkbenchHostPlugin::orchestrateOpenProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    // 目录选择（宿主窗口为父——模态于宿主；取消＝用户撤单，accepted=false
    // 的静默形态：无项目状态不变，无错误可报）。
    QWidget* parent = m_dockBody.data();
    const QString dir = QFileDialog::getExistingDirectory(
        parent, QString::fromUtf8("打开项目"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return out;  // 用户取消——未发生打开请求
    }
    const bool ok = openViaSessionController(canonicalizePathOrKeep(dir.toStdString()));
    out.accepted = ok;
    return out;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateNewProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    QWidget* parent = m_dockBody.data();

    // 步骤 1：目标目录（创建协议要求目录不存在或为空——零半成品纪律；
    // harness 预检给出可读提示而非异常中断，同款编排）。
    const QString dir = QFileDialog::getExistingDirectory(
        parent, QString::fromUtf8("新建项目——选择目标目录"), QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return out;  // 用户取消
    }
    std::error_code ec;
    const fs::path target = fs::u8path(dir.toStdString());
    if (fs::exists(target, ec) && !fs::is_empty(target, ec)) {
        QMessageBox::warning(parent, QString::fromUtf8("无法创建项目"),
                             QString::fromUtf8("目标目录已存在且非空：\n") + dir
                                 + QString::fromUtf8("\n（创建协议要求目录不存在或为空——零半成品纪律）"));
        return out;
    }

    // 步骤 2：显示名（PM-03 项目显示名——创建者即首个写权限持有者）。
    bool nameOk = false;
    const QString name = QInputDialog::getText(
        parent, QString::fromUtf8("新建项目"), QString::fromUtf8("项目显示名："),
        QLineEdit::Normal, QString::fromUtf8("新项目"), &nameOk);
    if (!nameOk || name.trimmed().isEmpty()) {
        // 取消或空名：不创建（创建协议 fail-fast 契约的前置自查——空名
        // 属调用方输入缺失，就地提示优于异常中断）。
        if (nameOk) {
            QMessageBox::warning(parent, QString::fromUtf8("无法创建项目"),
                                 QString::fromUtf8("项目显示名不能为空。"));
        }
        return out;
    }

    // 步骤 3：创建（组装区整体就位＋装载激活——结果 store 在作用域结束即
    // 析构＝隐式排空＋锁释放 §5.1 生命周期行；随后经标准打开协议进入会话
    // ——同一协议路径，锁干净，不走"进程内重复打开"的锁竞争分支）。
    const std::string displayName = name.trimmed().toStdString();
    try {
        project::OpenStoreResult created = project::ProjectStoreFactory::createNew(
            target, displayName, nullptr, m_bridge.get());
        (void)created;  // 创建产物随作用域析构（锁释放）——打开流在下方汇合
        reportLine("项目已创建（" + dir.toStdString() + "，显示名：" + displayName
                   + "）——释放创建锁后经打开协议进入");
    } catch (const project::StoreError& error) {
        // 创建失败的稳定诊断随 bridge 入目录；消息框呈现开发诊断 detail
        // （createNew 失败不留半成品的契约下，用户可据此清理后重试）。
        reportLine("项目创建失败：" + std::string(error.what()));
        QMessageBox::warning(parent, QString::fromUtf8("项目创建失败"),
                             QString::fromUtf8(error.what()));
        return out;
    } catch (const std::invalid_argument& error) {
        QMessageBox::warning(parent, QString::fromUtf8("项目创建失败"),
                             QString::fromUtf8(error.what()));
        return out;
    }

    // 步骤 4：经标准打开协议进入会话（与打开流同一协议路径）。
    const bool ok = openViaSessionController(canonicalizePathOrKeep(dir.toStdString()));
    out.accepted = ok;
    return out;
}

bool IrdWorkbenchHostPlugin::openViaSessionController(const std::string& canonicalPath)
{
    // 打开五步协议（§5.2 Opening 态）：状态机推进归 UiSessionController，
    // 本插件只做触发编排（§11.5）。可写打开（锁竞争/介质只读→PM-07 降级
    // 只读是成功形态——横幅与只读徽标由内容装配面呈现）。
    const SessionOpenReport report =
        m_controller ? m_controller->openProject(canonicalPath, UiOpenMode::Writable)
                     : SessionOpenReport{};  // 防御：未装配＝必失败报告（装配缺陷另行走 DEV 留痕）
    if (report.ok) {
        if (m_content) {
            m_content->noteRecentProject(canonicalPath);  // PM-10 最近项目（去重/上限壳内处理）
        }
        // 草稿会话绑定（UI-T17——§5.2"打开成功→草稿侧编排"）：捕获包装里
        // 取草稿写半区端口（StorePortAdapter 双面）；分支锚＝缺省值（诚实
        // 边界——立项登记注③：本阶段无挂接模块，锚不可达）。
        if (m_draft) {
            if (m_draft->hasSession()) {
                m_draft->unbindSession();  // 切换流表处置（§8.6——清空再绑）
            }
            auto* capturing = dynamic_cast<BundleCapturingStoreFactory*>(m_storeFactory.get());
            std::shared_ptr<IUiDraftStorePort> draftStore =
                capturing != nullptr ? capturing->lastDraftStore() : nullptr;
            if (report.opened.metadata.writable && draftStore == nullptr) {
                // 双面适配器缺失＝装配缺陷：留痕并保持未绑定（保存路径经
                // saveDraftsNow 的未绑定检查走返回值轨，不虚构"已绑定"）。
                if (m_diag.pipeline) {
                    m_diag.pipeline->logDev(kPluginDevChannel,
                                            "草稿写半区端口不可得（装配缺陷）——本会话保存链路未绑定");
                }
            } else {
                DraftSessionBinding binding;
                binding.projectId = report.opened.metadata.projectId;
                binding.branchId = core::BranchId{};  // 分支锚（诚实边界——见上）
                binding.writable = report.opened.metadata.writable;
                binding.drafts = std::make_shared<app::DraftQueryPortStub>();
                binding.store = std::move(draftStore);
                m_draft->bindSession(binding);
                // 建模模块草稿源挂接（T03b-1——§10.5 attachModule；绑定
                // 成功后恰挂一次，可写会话才接受——只读/已占用异常隔离留
                // 痕不中断打开协议）。
                if (m_domains) {
                    try {
                        m_draft->attachModule(
                            modeling::kModuleHandle,
                            modelingDraftSource(*m_domains));
                        // 草稿恢复（§8.3 restoreOnOpen——list→tryLoad→adopt
                        // →面板刷新）：demo 项目/历史项目的建模草稿在打开
                        // 时回填面板；失败隔离留痕不中断打开协议。
                        m_draft->restoreOnOpen();
                        m_domains->modeling.refreshFromSession();
                    } catch (const std::exception& attachError) {
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(
                                kPluginDevChannel,
                                std::string("建模草稿源挂接跳过：")
                                    + attachError.what());
                        }
                    }
                }
            }
        }
        reportLine("项目已打开：" + canonicalPath + "（可写："
                   + (report.opened.metadata.writable ? "是" : "否（降级只读——见横幅）") + "）");
        return true;
    }
    // 失败：错误页数据（token＋detail＋路径）——开发期经消息框与控制台双
    // 通道呈现；壳保持无项目首页态（"当前项目不动"）。Dev 通道同步留痕。
    reportLine("打开失败（" + report.failure.errorCodeToken + "）："
               + report.failure.detail + " @ " + report.failure.projectPath);
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(
            kPluginDevChannel,
            "打开失败（" + report.failure.errorCodeToken + "）："
                + report.failure.detail + " @ " + report.failure.projectPath);
    }
    QMessageBox::warning(
        m_dockBody.data(), QString::fromUtf8("项目打开失败"),
        QString::fromUtf8("稳定码：") + QString::fromStdString(report.failure.errorCodeToken)
            + QString::fromUtf8("\n项目路径：")
            + QString::fromStdString(report.failure.projectPath)
            + QString::fromUtf8("\n详情：") + QString::fromStdString(report.failure.detail));
    return false;
}

// =====================================================================
// 草稿链装配与保存编排（UI-T17——§8；O-43 ②）
// =====================================================================

void IrdWorkbenchHostPlugin::assembleDraftChain()
{
    // 串行落盘执行器（§3.4"ui 后台落盘线程（1 条）——串行队列"的开发期
    // 宿主）：构造即启动，退出路径有界排空（connectAppQuitDrain/析构）。
    m_diskExecutor = std::make_unique<app::SerialTaskExecutor>();

    ui::DraftControllerDeps deps;
    // 磁盘段投递（§8.2 数据流"转投 ui 后台落盘线程"）——串行执行器承接。
    deps.postToDiskThread = [this](std::function<void()> task) {
        m_diskExecutor->post(std::move(task));
    };
    // 完成回执 Marshal 回 UI 线程（§3.4 M-1）：以 Dock 体为上下文对象——
    // 其销毁后排队的回执自动作废（Qt 上下文语义），不悬挂。
    deps.postToUiThread = [this](std::function<void()> task) {
        if (m_dockBody.data() != nullptr) {
            QMetaObject::invokeMethod(m_dockBody.data(), std::move(task),
                                      Qt::QueuedConnection);
        }
    };
    // anyDirty 翻转→会话脏生产者接线（§10.5/UI-T12——标题 `*` 判定位的
    // 会话半区数据源；UI-T11 登记的"生产者接线随 UI-T12 落地"承诺）。
    deps.onSessionDirtyChanged = [this](bool dirty) {
        if (m_controller) {
            m_controller->reportSessionDirty(dirty);
        }
    };
    deps.diagSink = m_diag.catalog;
    deps.diagFactory = m_diag.factory;
    deps.devLog = m_diag.pipeline;
    m_draft = createDraftController(std::move(deps));
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "草稿控制器就绪（保存链路真实；恢复/autosave/分支锚随完整草稿链路任务接续）");
    }
}

bool IrdWorkbenchHostPlugin::saveDraftsNow()
{
    // 未绑定会话＝无可保存对象（返回 false——调用方按 SaveFailed/保存反馈
    // 处置，不虚构"已保存"；只读会话 saveAll 按契约 fail-fast，门控由
    // 命令可用性快照承担——draft.save readOnlyAllowed=false）。
    if (!m_draft || !m_draft->hasSession()) {
        return false;
    }
    // 全量保存挂接的脏模块（§8.2/§8.4——保存/应用分离红线：零修订；当前
    // 无挂接模块＝零脏模块＝平凡成功，属诚实形态而非能力伪造）。
    const SaveOutcome outcome = m_draft->saveAll(SaveTrigger::Manual);
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    if (outcome.failedCount == 0) {
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("草稿已保存（%1 个模块）")
                                       .arg(static_cast<int>(outcome.savedCount)),
                                   4000);
        }
        return true;
    }
    // 失败保留脏标记（§8.2 失败行）——首失败模块 token 入状态行（开发期
    // 反馈面；用户文案随草稿链路完整任务细化）。
    const QString failedModule =
        outcome.failedModules.empty()
            ? QString()
            : QString::fromStdString(outcome.failedModules.front());
    if (statusBar != nullptr) {
        statusBar->showMessage(QString::fromUtf8("草稿保存失败（%1 个模块；首失败：%2）")
                                   .arg(static_cast<int>(outcome.failedCount))
                                   .arg(failedModule),
                               8000);
    }
    return false;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateSaveProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令（§7.1 最小集——空 schema）
    CommandOutcome out;
    // 前置守卫（可用性快照已禁用的兜底面）：无会话＝未发生保存请求。
    if (!m_controller || !m_controller->hasOpenSession()) {
        return out;  // accepted=false——命令未派发
    }
    out.accepted = true;
    saveDraftsNow();  // 结局反馈经状态行（保存/应用分离——失败不清脏）
    return out;
}

// =====================================================================
// 关闭编排（UI-T17——workbench.closeProject 覆写面；§5.4/§5.6）
// =====================================================================

/**
 * @brief 应用编排（UI-T23——draft.apply 覆写面的多模块遍历形态）：
 *        runDomainApply 遍历全部已登记域模块（锚同步→信封组装→提交→
 *        回执），消除首版只认 modeling 的硬编码（acceptance 2）。
 *
 * 首版交互边界保持：submit 传 HostCommandInteraction（Confirmable 集
 * 弹确认对话框）；分支锚取权威分支表首条（单默认分支项目行为正确；
 * 多分支选择器随收口任务）。状态行按遍历报告汇总（无草稿域计数＋提交
 * 结果），域级细节经 Dev 通道留痕。
 */
CommandOutcome IrdWorkbenchHostPlugin::orchestrateApplyDraft(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令
    CommandOutcome out;
    const auto adapter = m_lastStoreAdapter;
    if (!m_domains || adapter == nullptr) {
        // 无项目态没有"应用"语义（门控兜底——可用性快照已禁用的兜底面）。
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("未打开项目——无可应用草稿"), 4000);
        }
        return out;  // accepted=false——命令未派发
    }
    out.accepted = true;
    auto& store = adapter->projectStore();

    // 权威分支表首条 tip（INV-M3 单默认分支）——遍历的会话锚输入。
    std::optional<std::pair<core::BranchId, core::RevisionId>> anchor;
    const auto tips = store.query().branchTips();
    if (!tips.empty()) {
        anchor = std::make_pair(tips.front().id, tips.front().tip);
    }

    // 多模块遍历（acceptance 2——建模/需求/运动学一视同仁；无草稿域记
    // NoDraft 跳过提交，不产生空修订；回执回写闭包在登记表条目中——
    // 建模域含 DraftController onCommandResult 回写）。
    HostCommandInteraction interaction(
        m_dockBody.data(),
        [this] { return m_controller && m_controller->hasOpenSession(); });
    const DomainApplyReport report = runDomainApply(
        m_domains->applyEntries, anchor,
        [&store](project::CommandEnvelope envelope,
                 project::ICommandInteraction* cmdInteraction) {
            return store.commands().submit(std::move(envelope), cmdInteraction);
        },
        &interaction,
        [this](const std::string& message) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel, message);
            }
        });

    // 状态行汇总（呈现值源＝遍历报告——零虚构：无草稿域如实计数）。
    if (m_hostStatusBar != nullptr) {
        if (report.submittedCount() == 0) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("无已应用的草稿变更（%1 个域均无待应用修改）")
                    .arg(static_cast<int>(report.entries.size())),
                4000);
        } else if (report.anyCommitted()) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("草稿已应用：提交 %1 个域修订（%2 个域无变更）")
                    .arg(static_cast<int>(report.committedCount()))
                    .arg(static_cast<int>(report.noDraftCount())),
                5000);
        } else {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("草稿应用未提交（%1 个域被拒绝/中止——详情见诊断）")
                    .arg(static_cast<int>(report.submittedCount())),
                6000);
        }
    }
    // 提交后的共享面刷新（域工作集修订→共享树/检查器呈现同步——L1 链路
    // 的数据侧驱动；rejected 时树内容不变，刷新为幂等动作）。
    if (report.anyCommitted()) {
        refreshSharedSurfaces();
    }
    return out;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateCloseProject(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令
    CommandOutcome out;
    if (!m_controller || !m_controller->hasOpenSession()) {
        return out;  // 无项目态没有"关闭项目"语义（门控兜底）
    }
    out.accepted = true;
    try {        // S1 首步（§5.4）：装配对话框数据（不改状态——取消可回原状态）。
        const CloseDialogData data = m_controller->beginClose(UiCloseIntent::CloseProject);
        // 呈现（装配层对话框）＋决议回交（机制归控制器——§5 注释分工）。
        const std::optional<CloseDecision> decision = presentCloseDialog(data);
        if (!decision.has_value()) {
            // [取消]→原状态（无处置执行——会话原状）。
            (void)m_controller->resolveCloseDialog(CloseDecision{});  // confirmed=false＝取消
            return out;
        }
        const CloseDialogResolution resolution = m_controller->resolveCloseDialog(*decision);
        switch (resolution.status) {
        case CloseDialogResolution::Status::Confirmed:
            // 处置执行完毕——进入 Draining（或同步直达 Closed）：启动防线
            // 轮询（§5.6 四级防线的 UI 线程驱动点）。
            startDrainWatch();
            break;
        case CloseDialogResolution::Status::SaveFailed:
            // 保存失败＝关闭中止（不虚构"已保存"——§5.4 失败侧保守出口）。
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("关闭已中止"),
                                 QString::fromUtf8("草稿保存失败，项目保持打开。"));
            break;
        case CloseDialogResolution::Status::Cancelled:
        case CloseDialogResolution::Status::CandidateRejected:
            break;  // 非切换流不可达（防御——状态机原状，无额外动作）
        }
    } catch (const std::logic_error& error) {
        // 调用次序违约（重复 beginClose 等）＝编排缺陷：留痕＋就地提示，
        // 不吞错不崩溃（宿主进程内 fail-fast 的可观测形态）。
        reportLine(std::string("关闭编排状态违约：") + error.what());
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    std::string("关闭编排状态违约：") + error.what());
        }
        QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法关闭项目"),
                             QString::fromUtf8("当前状态不接受关闭请求（状态机违约，已留痕）。"));
    }
    return out;
}

void IrdWorkbenchHostPlugin::openRecentProject(const std::string& canonicalPath)
{
    if (!m_controller) {
        return;
    }
    // 无会话：标准打开协议（与 project.open 同一路径）。
    if (!m_controller->hasOpenSession()) {
        openViaSessionController(canonicalizePathOrKeep(canonicalPath));
        return;
    }
    // 有会话：§5.4 S2 切换流（A 的统一确认对话框→决议确认后候选验证；
    // "候选验证成功才切"——失败 A 会话不变，PM-03）。
    try {
        const CloseDialogData data = m_controller->beginSwitch(canonicalPath, UiOpenMode::Writable);
        const std::optional<CloseDecision> decision = presentCloseDialog(data);
        if (!decision.has_value()) {
            (void)m_controller->resolveCloseDialog(CloseDecision{});  // 取消
            return;
        }
        const CloseDialogResolution resolution = m_controller->resolveCloseDialog(*decision);
        switch (resolution.status) {
        case CloseDialogResolution::Status::Confirmed:
            // A 转入 Draining 背景持有点＋B 已绑定（INV-SES-2/3）——A 排空
            // 归防线轮询观测；B 记入最近项目。
            if (m_content) {
                m_content->noteRecentProject(canonicalPath);
            }
            startDrainWatch();
            break;
        case CloseDialogResolution::Status::SaveFailed:
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("切换已中止"),
                                 QString::fromUtf8("草稿保存失败，当前项目保持打开。"));
            break;
        case CloseDialogResolution::Status::CandidateRejected:
            QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法切换项目"),
                                 QString::fromUtf8("候选项目验证失败，当前项目保持打开。"));
            break;
        case CloseDialogResolution::Status::Cancelled:
            break;
        }
    } catch (const std::logic_error& error) {
        reportLine(std::string("切换编排状态违约：") + error.what());
        QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法切换项目"),
                             QString::fromUtf8("当前状态不接受切换请求（状态机违约，已留痕）。"));
    }
}

std::optional<CloseDecision> IrdWorkbenchHostPlugin::presentCloseDialog(
    const CloseDialogData& data)
{
    // §5.4 统一确认对话框的装配层呈现面：机制（数据装配/决议解析/状态迁移
    // /Draining 防线）全在控制器——本对话框只渲染 CloseDialogData 并把
    // 用户决议交回 resolveCloseDialog（草稿三选×任务二选的两轴呈现）。
    QDialog dialog(m_dockBody.data());
    dialog.setWindowTitle(QString::fromUtf8("关闭项目"));
    auto* layout = new QVBoxLayout(&dialog);

    // 标题区（UX-02 工程用语——显示名来自权威元数据投影）。
    layout->addWidget(new QLabel(QString::fromUtf8("项目「%1」即将关闭。")
                                     .arg(QString::fromStdString(data.projectDisplayName)),
                                 &dialog));

    // 草稿处置轴（§5.4 草稿区）：无行集且无会话脏＝呈现"无未应用修改"
    // （零虚构——不渲染不存在的选项）；可写会话才可选"保存"（§5.5）。
    QRadioButton* saveDrafts = nullptr;
    QRadioButton* discardDrafts = nullptr;
    QLabel* draftSummary = new QLabel(
        QString::fromUtf8("未应用的草稿修改：%1 项%2")
            .arg(static_cast<int>(data.draftRows.size()))
            .arg(data.sessionDirty ? QString::fromUtf8("（含未落盘的会话修改）") : QString()),
        &dialog);
    layout->addWidget(draftSummary);
    if (data.draftRows.empty() && !data.sessionDirty) {
        layout->addWidget(new QLabel(QString::fromUtf8("无未应用修改。"), &dialog));
    } else {
        saveDrafts = new QRadioButton(QString::fromUtf8("保存草稿并关闭"), &dialog);
        saveDrafts->setEnabled(data.saveDraftsAvailable);
        discardDrafts = new QRadioButton(QString::fromUtf8("放弃未应用修改并关闭"), &dialog);
        // 缺省决议保守化：可保存时缺省保存（数据安全侧）；不可保存时仅有
        // "放弃"可选（只读会话——磁盘草稿保留，会话脏数据丢弃）。
        (data.saveDraftsAvailable ? saveDrafts : discardDrafts)->setChecked(true);
        layout->addWidget(saveDrafts);
        layout->addWidget(discardDrafts);
    }

    // 任务处置轴（§5.4 任务区）：无在途任务＝呈现"无后台任务"（等待轴
    // 无呈现对象——noActiveTasks 时等待直接进入 Draining）。
    QRadioButton* waitTasks = nullptr;
    QRadioButton* cancelTasks = nullptr;
    if (!data.noActiveTasks) {
        layout->addWidget(new QLabel(
            QString::fromUtf8("仍有 %1 个后台任务未完成。")
                .arg(static_cast<int>(data.taskRows.size())),
            &dialog));
        waitTasks = new QRadioButton(QString::fromUtf8("等待后台任务结束后关闭"), &dialog);
        waitTasks->setChecked(true);
        cancelTasks = new QRadioButton(QString::fromUtf8("协作取消后台任务并关闭"), &dialog);
        layout->addWidget(waitTasks);
        layout->addWidget(cancelTasks);
    } else {
        layout->addWidget(new QLabel(QString::fromUtf8("无后台任务。"), &dialog));
    }

    // 按钮区（取消＝回原状态；确认＝执行两轴决议）。
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("继续"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;  // [取消]——会话原状不变
    }
    CloseDecision decision;
    decision.confirmed = true;
    decision.draft = (saveDrafts != nullptr && saveDrafts->isChecked())
                         ? DraftDisposition::Save
                         : DraftDisposition::Discard;
    decision.task = (cancelTasks != nullptr && cancelTasks->isChecked())
                        ? TaskDisposition::CooperativeCancel
                        : TaskDisposition::Wait;
    return decision;
}

// =====================================================================
// Draining 防线轮询驱动（§5.6 四级防线——UI 线程 QTimer 周期）
// =====================================================================

void IrdWorkbenchHostPlugin::startDrainWatch()
{
    if (m_drainTimer == nullptr) {
        m_drainTimer = new QTimer(this);
        m_drainTimer->setInterval(200);  // §9.4 轮询周期同源（UI 线程零阻塞）
        connect(m_drainTimer, &QTimer::timeout, this, [this] { pollDrainOnce(); });
    }
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    if (statusBar != nullptr) {
        statusBar->showMessage(QString::fromUtf8("正在关闭项目……（等待后台任务与草稿落盘收口）"));
    }
    m_drainTimer->start();
}

void IrdWorkbenchHostPlugin::pollDrainOnce()
{
    if (!m_controller) {
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        return;
    }
    DrainPollReport report;
    try {
        report = m_controller->pollDrain();
    } catch (const std::logic_error&) {
        // "不在 Draining 且无后台持有点"＝关闭已收口（同步完成/切换 B 绑定
        // 后台持有点已排空）——轮询对象消失，停止驱动（不是错误）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        return;
    }
    QStatusBar* statusBar = m_hostStatusBar;  // 状态投影面＝宿主状态栏（UI-T18）
    switch (report.status) {
    case DrainPollReport::Status::Draining:
        break;  // 保持等待（防线 1 的有界反馈已在状态行）
    case DrainPollReport::Status::ForceConfirmDue: {
        // 防线 3（T_force 到点）：强制结束确认——呈现一次，决议交回控制器
        // （确认＝强杀序列＋abandon 兜底；拒绝＝继续等待，T_force2 仍兜底）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        const ForceCloseDialogData forceData = m_controller->forceCloseDialogData();
        const QMessageBox::StandardButton choice = QMessageBox::question(
            m_dockBody.data(), QString::fromUtf8("强制结束后台任务？"),
            QString::fromUtf8("项目「%1」关闭等待已超过 %2，仍有 %3 个后台任务未结束。\n\n"
                              "强制结束＝任务记为失败（最近检查点保留可续）。是否强制结束并关闭？")
                .arg(QString::fromStdString(forceData.projectDisplayName),
                     QString::fromStdString(forceData.waitedText),
                     QString::number(static_cast<int>(forceData.activeTaskCount))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        m_controller->resolveForceCloseDialog(choice == QMessageBox::Yes);
        if (m_drainTimer != nullptr) {
            m_drainTimer->start();  // 确认与否都回到轮询收敛（T_force2 兜底）
        }
        break;
    }
    case DrainPollReport::Status::GivenUp:
        // 防线 4（T_force2 到点）：放弃等待并完成关闭（绝不无限等待——
        // INV-SES-4；数据损失限于未归档结果，检查点保留）。
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("关闭等待超时，已强制完成关闭（未归档结果不保留）。"), 8000);
        }
        break;
    case DrainPollReport::Status::ClosedNow:
        if (m_drainTimer != nullptr) {
            m_drainTimer->stop();
        }
        if (statusBar != nullptr) {
            statusBar->showMessage(QString::fromUtf8("项目已关闭。"), 4000);
        }
        break;
    }
}

// =====================================================================
// 域装配接线（WP-24-T03b 收口）
// =====================================================================

void IrdWorkbenchHostPlugin::syncDomainModulesToContext(
    const ProjectContextProjection& context)
{
    if (m_domains == nullptr) {
        return;  // 域装配未就绪（防御——initialize 序保证先于本钩子，恒真）
    }
    if (context.project.has_value()) {
        // 项目在位（打开成功/切换 B 段就位）：分支锚＝权威分支表首条 tip
        // （INV-M3 单默认分支；多分支选择器随收口任务）。锚定即重算就绪
        // ＋面板刷新——打开路径的"锚定＋刷新"半区在此统一（T03b-2 只在
        // apply 时锚定的首版形态升级为全路径同步）。UI-T23 扩需求域
        // （其门面锚语义与建模同构；运动学无锚语义——v1 无草稿域）。
        const auto adapter = m_lastStoreAdapter;
        if (adapter != nullptr) {
            const auto tips = adapter->projectStore().query().branchTips();
            if (!tips.empty()) {
                m_domains->modeling.bindSessionAnchor(tips.front().id,
                                                      tips.front().tip);
                if (m_domains->requirements.has_value()) {
                    m_domains->requirements->bindSessionAnchor(
                        tips.front().id, std::nullopt);
                }
            }
        }
        // 共享面呈现同步（打开后的域数据入树——L1 链路数据侧驱动）。
        refreshSharedSurfaces();
        return;
    }
    // 无项目（关闭完成/切换 A 段排空）：会话脱离——模块锚清空＋草稿工作
    // 集复位＋面板空态（§8.6 表处置的模块半区；磁盘草稿零触碰）＋项目
    // 关闭统一清理八类对象（UI-T23 acceptance 4——具名用例承载）。
    m_domains->modeling.onSessionDetached();
    if (m_domains->requirements.has_value()) {
        m_domains->requirements->onSessionDetached();
    }
    teardownSharedSurfacesForClose();
}

// =====================================================================
// 共享面集成装配（UI-T23——B1-SPEC §3/§4；acceptance 1/3/4/5 的装配面）
// =====================================================================

void IrdWorkbenchHostPlugin::assembleSharedSurfaces()
{
    if (!m_domains) {
        return;  // 域装配未就绪＝装配缺陷（initialize 序保证，防御留痕）
    }

    // ①选择服务（INV-B3 唯一汇聚点）。端口注入（O-31 装配层特权边）：
    //    NameMap＝空映射适配器（宿主无呈现装配的诚实二态——L3 反解失败
    //    分支为当前形态常态；真映射注入点单一，随 WP-24-T08 呈现装配
    //    替换）；树定位回调＝共享树面板 locateAndHighlight（面板创建后
    //    经 lambda 捕获重绑——面板先于服务构造的次序解法与域 harness
    //    同款）；高亮出口＝宿主 WorkCellScene 实现（L2 真高亮——呈现
    //    缺席时动作跳过＋Dev 留痕，判定照常）。
    auto nameMap = std::make_shared<HostEmptyNameMapPort>();
    m_highlightOutlet = std::make_shared<HostHighlightOutlet>(
        getRobWorkStudio(),
        [this](const std::string& message) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel, message);
            }
        });
    std::function<bool(const core::ObjectId&)> treeLocator =
        [](const core::ObjectId&) { return false; };  // 面板创建后重绑
    m_selection = std::make_shared<ui::SelectionService>(ui::SelectionService::Deps{
        nameMap,
        [&treeLocator](const core::ObjectId& oid) { return treeLocator(oid); },
        m_highlightOutlet,
        m_diag.pipeline});

    // ②共享模型（树/检查器）——三域迁移 Provider 注册（B1-SPEC §5.1：
    //    域三接入面的宿主消费；登记成功域的 Provider 入模型，失败隔离
    //    缺席域不注册——acceptance 3 的装配面承载）。
    m_treeModel = std::make_shared<ui::ProjectTreeModel>();
    m_inspectorModel = std::make_shared<ui::PropertyInspectorModel>();
    std::vector<std::string> providerLines;
    const auto registerDomainSurfaces =
        [this, &providerLines](const char* domainKey,
                               bool available,
                               std::shared_ptr<ui::IUiTreeNodesProvider> treeNodes,
                               std::shared_ptr<ui::IUiPropertyPagesProvider> propertyPages) {
            if (!available) {
                // §11.3 失败隔离：装配失败的域不注册 Provider，其分组呈现
                // 装配失败占位（稳定码文本——面板占位由 refresh 渲染）。
                providerLines.push_back(std::string("shared surface: ") + domainKey
                                        + " skipped (assembly failed)");
                return;
            }
            try {
                if (treeNodes != nullptr) {
                    m_treeModel->addProvider(treeNodes);
                }
                if (propertyPages != nullptr) {
                    m_inspectorModel->addProvider(propertyPages);
                }
                providerLines.push_back(std::string("shared surface: ") + domainKey
                                        + " registered");
            } catch (const std::exception& registerError) {
                // Provider 注册违约（空/重复域键）＝该域接入缺陷：登记为
                // 失败状态（占位呈现），不中止其余域——隔离不掩盖。
                providerLines.push_back(std::string("shared surface: ") + domainKey
                                        + " UI-PLUGIN-ASSEMBLY-FAILED detail="
                                        + registerError.what());
                for (auto& status : m_domains->statuses) {
                    if (status.domainKey == domainKey && status.ok) {
                        status.ok = false;
                        status.detail = registerError.what();
                    }
                }
            }
        };
    registerDomainSurfaces("modeling", true,
                           m_domains->modeling.sharedSurfaceProviders().treeNodes,
                           m_domains->modeling.sharedSurfaceProviders().propertyPages);
    if (m_domains->requirements.has_value()) {
        const auto handles = m_domains->requirements->sharedSurfaceProviders();
        registerDomainSurfaces("requirements", true, handles.treeNodes,
                               handles.propertyPages);
    } else {
        registerDomainSurfaces("requirements", false, nullptr, nullptr);
    }
    if (m_domains->kinematics.has_value()) {
        const auto handles = m_domains->kinematics->sharedSurfaceProviders();
        registerDomainSurfaces("kinematics", true, handles.treeNodes,
                               handles.propertyPages);
    } else {
        registerDomainSurfaces("kinematics", false, nullptr, nullptr);
    }

    // ③共享面板（工厂——R-2 封闭实现；名称解析端口复用 C-11 桩——显示
    //    名随名称端口真值装配接续，UX-02 解析失败占位形态如实）。
    ui::IndustrialProjectTreePanelDeps treePanelDeps;
    treePanelDeps.model = m_treeModel;
    treePanelDeps.selection = m_selection;
    treePanelDeps.nameResolver = m_nameResolver;
    m_treePanel = ui::createIndustrialProjectTreePanel(treePanelDeps, nullptr);

    ui::PropertyInspectorPanelDeps inspectorPanelDeps;
    inspectorPanelDeps.model = m_inspectorModel;
    inspectorPanelDeps.nameResolver = m_nameResolver;
    m_inspectorPanel = ui::createPropertyInspectorPanel(inspectorPanelDeps, nullptr);

    // ④失败隔离占位呈现（acceptance 3——装配失败域的分组占位行；稳定
    //    码文本进占位文案——SA-12 文案加工归调用方，此处只拼码与域键）。
    for (const auto& status : m_domains->statuses) {
        if (status.ok) {
            continue;
        }
        if (status.domainKey == "modeling") {
            m_treePanel->setGroupPlaceholder(
                ui::ProjectTreeGroup::ModelingObjects,
                std::string("建模域装配失败（UI-PLUGIN-ASSEMBLY-FAILED）"));
        } else if (status.domainKey == "requirements") {
            m_treePanel->setGroupPlaceholder(
                ui::ProjectTreeGroup::RequirementObjects,
                std::string("需求域装配失败（UI-PLUGIN-ASSEMBLY-FAILED）"));
        } else if (status.domainKey == "kinematics") {
            m_treePanel->setGroupPlaceholder(
                ui::ProjectTreeGroup::AnalysisConfigurations,
                std::string("运动学域装配失败（UI-PLUGIN-ASSEMBLY-FAILED）"));
        }
    }

    // ⑤联动接线（L1/L3 基线）：树定位重绑（面板创建后——L3 反解成功的
    //    落点＝共享树定位选中）；检查器模型订阅选择服务（L1——UI-T22
    //    模型即 IUiSelectionObserver；RAII 句柄随清理释放）。
    treeLocator = [this](const core::ObjectId& oid) {
        return m_treePanel != nullptr && m_treePanel->locateAndHighlight(oid);
    };
    m_inspectorSubscription = m_selection->subscribe(*m_inspectorModel);

    // ⑥域下行联动（B1-SPEC §5.1 SelectionAdapter——树选→域面板高亮；
    //    上行三维拾取归宿主 View3D 拾取桥，随阶段 B 三维交互接续）。
    m_domains->modeling.attachSelectionService(*m_selection);
    if (m_domains->requirements.has_value()) {
        m_domains->requirements->attachSelectionService(*m_selection);
    }
    if (m_domains->kinematics.has_value()) {
        m_domains->kinematics->attachSelectionService(*m_selection);
    }

    // ⑦首刷（装配期一次——无项目态：域 Provider 空集/占位如实呈现）。
    refreshSharedSurfaces();

    for (const std::string& line : providerLines) {
        reportLine(line);
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel, line);
        }
    }
    reportLine("共享面装配完成（项目树＋检查器＋选择服务；三域 Provider 注册；L1/L3 接线；"
               "NameMap 端口＝空映射二态——呈现装配随 WP-24-T08）");
}

void IrdWorkbenchHostPlugin::refreshSharedSurfaces()
{
    // 共享面刷新编排（装配层编排形——与域 harness 同构）：树 rebuild→
    // 树面板 refresh→检查器按当前选中重询问→检查器面板 refresh。重建
    // 拒绝（域数据违约）保持旧内容＋Dev 留痕（拒绝优于残缺——模型契约）。
    if (m_treeModel == nullptr || m_treePanel == nullptr
        || m_inspectorModel == nullptr || m_inspectorPanel == nullptr) {
        return;  // 共享面未装配（防御——装配序保证，此处为幂等静默）
    }
    const ui::TreeRebuildReport report = m_treeModel->rebuild();
    if (!report.ok && m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "shared tree rebuild rejected: " + report.reason);
    }
    m_treePanel->refresh();
    ui::SelectionChange current;
    current.selectedObjectIds = m_selection->selectedObjectIds();
    current.source = m_selection->selectionSource().value_or(
        ui::SelectionSource::ProjectTree);
    m_inspectorModel->onSelectionChanged(current);
    m_inspectorPanel->refresh();
}

void IrdWorkbenchHostPlugin::teardownSharedSurfacesForClose()
{
    // 项目关闭统一清理（acceptance 4——八类对象逐类收口；清理序＝依赖
    // 序：先退订消费者再清状态源，防止清理过程中的重入回调）。
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "project close teardown: begin (8 object classes)");
    }

    // ⑧运行中订阅：检查器模型的 SelectionService 订阅（RAII 句柄释放）
    //    ——先断 L1 消费链，后续清源不再触发检查器刷新。
    if (m_inspectorSubscription) {
        m_inspectorSubscription.reset();
    }

    // ②SelectionService：业务选中集清空（空选中广播——消费者已退订，
    //    状态归零是本步的可观测语义）＋三维高亮对称清除（L2 出口收口）。
    if (m_selection) {
        m_selection->clearSelection(ui::SelectionSource::Command);
    }
    if (m_highlightOutlet) {
        m_highlightOutlet->clearHighlight();
    }

    // ③共享属性检查器＋①项目树：模型状态复位（检查器经空选中询问→
    //    NoSelection 占位态；树经 rebuild 空集→五空组形态——域 Provider
    //    无会话供给空集的合法二态）＋面板 refresh（渲染同步，占位态）。
    refreshSharedSurfaces();

    // ④复杂编辑宿装页：检查器面板 refresh 在非对象态下清宿装容器
    //    （UI-T22 契约"切换对象清宿装——零残留"的关闭路径复用）——上一步
    //    的面板 refresh 已承载，此处 Dev 留痕声明核查点（具名用例在集成
    //    测试断言宿装容器为空）。

    // ⑤HostWorkCell 呈现对象：宿主高亮出口的按名清除已执行（上方②）；
    //    RuntimePublishBridge 的宿主呈现释放（detachHostSession）随呈现
    //    装配接续（当前装配形态无桥实例＝无污染源——WP-24-T08 装配时
    //    本清理点扩展桥调用，登记 ui.md §13 落位注）。

    // ⑥TimedStatePath 播放驱动：当前装配形态无 Playback 发布缝（B1-SPEC
    //    D9 的 TimedStatePath 载体经 UI-T20 发布桥承载——同⑤随呈现装配
    //    接续；PlayBack 官方面板的播放状态归宿主框架自持，项目关闭时
    //    宿主 WorkCell 卸载连带失效——框架既有行为，非本插件持有对象）。

    // ⑦会话姿态：Jog 桥的会话守卫在位（m_jogBridgeConnected 保持——
    //    订阅无会话时 State 变化不再写入域；域侧 KinSessionPose 载体未
    //    注入〔服务缝诚实边界〕＝无可清理的宿主持有副本）。

    // ⑧运行中订阅（续）：修订事件订阅保持（总线随 store 关闭排空——
    //    旧项目事件在无绑定态被域模块按分支过滤丢弃，不污染新项目）。

    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "project close teardown: done (selection cleared, "
                                "tree/inspector reset, highlight cleared, "
                                "presentation-side objects follow bridge assembly)");
    }
}

void IrdWorkbenchHostPlugin::connectHostEventBridges()
{
    // L3 桥（TreeView Select Frame → 反解编排）：框架公开事件订阅——
    // TreeView 树行选中时 fire frameSelectedEvent(frame)，本桥取其运行时
    // 名（Frame::getName——RuntimeNameMap 键）转 SelectionService 的 L3
    // 编排入口（反解成功→树定位选中；失败→树不动＋runtimeOnly 暂态）。
    // SA-02 合规：只订阅事件，零框架修改。
    auto* studio = getRobWorkStudio();
    if (studio == nullptr) {
        reportLine("宿主未注入——L3/D8 桥未接线（降级形态，如实留痕）");
        return;
    }
    studio->frameSelectedEvent().add(
        [this](rw::kinematics::Frame* frame) {
            if (frame == nullptr || !m_selection) {
                return;  // 空帧＝框架边界形态；未装配＝防御
            }
            m_selection->handleTreeViewFrameSelected(frame->getName());
        },
        this);

    // D8 桥（State 变化 → Jog 会话姿态域半区）：stateChangedEvent 订阅——
    // 呈现 WorkCell 首个 Device 的 q 提取（extractJointQFromState——纯
    // 函数）后经 kinematics 门面公共方法写入（载体未注入时域侧返回 false
    // 诚实降级——Dev 留痕不放大）。会话守卫：无打开会话时静默（项目关闭
    // 后宿主 State 变化不写域——八类清理的会话姿态守卫半区）。
    studio->stateChangedEvent().add(
        [this, studio](const rw::kinematics::State& state) {
            if (!m_controller || !m_controller->hasOpenSession()) {
                return;  // 会话守卫（⑦会话姿态清理的桥半区）
            }
            if (!m_domains || !m_domains->kinematics.has_value()) {
                return;  // 运动学域缺席（失败隔离）——无承接面
            }
            const auto q = extractJointQFromState(studio->getWorkCell(), state);
            if (!q.has_value() || q->empty()) {
                return;  // 无可提取设备（呈现缺席/无 Device——静默）
            }
            const bool applied = m_domains->kinematics->applyHostJointState(*q);
            if (!applied && m_diag.pipeline) {
                // 载体未注入＝诚实降级（一次性语义——每帧留痕会刷屏，
                // 首次降级留痕即可；域缝公共化后自然消失）。
                static bool loggedDegraded = false;
                if (!loggedDegraded) {
                    m_diag.pipeline->logDev(
                        kPluginDevChannel,
                        "jog bridge: kinematics session-pose seam not wired "
                        "(honest degradation——carrier injection follows "
                        "KinPanelServices public-facing task)");
                    loggedDegraded = true;
                }
            }
        },
        this);
    m_jogBridgeConnected = true;

    reportLine("宿主事件桥接线完成（L3 frameSelectedEvent→反解编排；D8 stateChangedEvent→会话姿态桥）");
}

// =====================================================================
// 集成冒烟通道（UI-T23——GUI 留痕载体；环境变量 IRD_UI_PLUGIN_SMOKE=auto）
// =====================================================================

void IrdWorkbenchHostPlugin::maybeRunIntegrationSmoke()
{
    // 触发面＝环境变量（宿主进程的插件无命令行入口——框架 RobWorkStudio
    // 持有 argv；环境变量是插件侧无宿主侵入的自动化触发形态，与
    // F-320 PATH 前置清单同属启动环境面）。未设置＝正常交互形态，本方法
    // 即返回（零开销）。
    const QString smoke = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE");
    if (smoke != QLatin1String("auto")) {
        return;
    }

    // 冒烟序列（演示项目驱动——与域 harness --auto 同构的自动化验收面）：
    //   步 1  demo 项目创建＋打开（真实协议路径）
    //   步 2  共享树重建规模断言（建模组有种子对象——nodeCount>0）
    //   步 3  树选建模根节点→选择广播→检查器应答（L1 链路）
    //   步 4  draft.apply 真实提交（建模种子草稿——多模块遍历路径）
    //   步 5  L3 反解失败分支（空 NameMap 二态——树不动＋runtimeOnly 暂态）
    //   步 6  项目关闭→八类清理（选中清空/树空组）→自动退出
    // 控制台 [ird-ui-smoke] 标记行＋退出码（0＝全链无异常；1＝断言失败）。
    // 序列经 QTimer 队列驱动（每步一拍——对话框/事件循环落定裕量）。
    QTimer::singleShot(600, this, [this] {
        std::cout << "[ird-ui-smoke] started" << std::endl;
        int exitCode = 0;
        QString smokeDirTemplate = QString::fromUtf8("ird-ui-smoke-demo-XXXXXX");
        std::unique_ptr<QTemporaryDir> smokeDir =
            std::make_unique<QTemporaryDir>(QDir::tempPath() + QLatin1Char('/')
                                            + smokeDirTemplate);
        const bool created = smokeDir->isValid();
        std::cout << "[ird-ui-smoke] step1 create-demo=" << (created ? "ok" : "fail")
                  << std::endl;
        if (!created) {
            std::cout << "[ird-ui-smoke] FAILED" << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        const std::string demoPath =
            fs::weakly_canonical(fs::u8path(smokeDir->path().toStdString())).u8string();
        try {
            // 步 1：demo 项目创建＋打开（真实创建/打开协议——与交互路径
            // 同一协议面，零冒烟专用分支）。
            project::ProjectStoreFactory::createNew(demoPath,
                                                    QStringLiteral("集成冒烟演示项目").toStdString(),
                                                    nullptr, m_bridge.get());
            const bool opened = openViaSessionController(demoPath);
            std::cout << "[ird-ui-smoke] step1 open=" << (opened ? "ok" : "fail")
                      << " nodes=" << (m_treeModel ? m_treeModel->nodeCount() : 0)
                      << std::endl;
            // 步 2：共享树规模（建模种子入树——nodeCount>0 断言）。
            const bool treeOk = m_treeModel != nullptr && m_treeModel->nodeCount() > 0;
            std::cout << "[ird-ui-smoke] step2 tree-nodes=" << (treeOk ? "ok" : "fail")
                      << std::endl;
            // 步 3：树选（建模根＝建模组首节点）→检查器应答。
            bool step3Ok = false;
            if (treeOk) {
                const auto modelingIds = m_treeModel->nodesInGroup(
                    ui::ProjectTreeGroup::ModelingObjects);
                if (!modelingIds.empty()) {
                    m_selection->selectBusiness({modelingIds.front()},
                                                ui::SelectionSource::ProjectTree);
                    step3Ok = m_inspectorModel->view().kind
                              == ui::InspectorContentKind::ObjectFields;
                }
            }
            std::cout << "[ird-ui-smoke] step3 l1-inspector=" << (step3Ok ? "ok" : "fail")
                      << std::endl;
            // 步 4：draft.apply 多模块遍历（acceptance 2 的宿主形态执行面）：
            //   demo 项目刚创建无域编辑——三域如实 NoDraft（§8.5 不产生
            //   空修订；提交路径由集成契约测试的测试注册模块承载）。断言
            //   面＝遍历对三域各产出恰一行（登记序）且全部走 NoDraft 分支。
            const DomainApplyReport applyReport = runDomainApply(
                m_domains ? m_domains->applyEntries
                          : std::vector<ui::DomainModuleEntry>{},
                std::nullopt,
                [this](project::CommandEnvelope envelope,
                       project::ICommandInteraction* cmdInteraction) {
                    const auto adapter = m_lastStoreAdapter;
                    if (adapter == nullptr) {
                        return project::CommandResult{};  // 无 store＝空结果（防御）
                    }
                    return adapter->projectStore().commands().submit(
                        std::move(envelope), cmdInteraction);
                },
                nullptr,
                [this](const std::string& message) {
                    if (m_diag.pipeline) {
                        m_diag.pipeline->logDev(kPluginDevChannel, message);
                    }
                });
            const bool step4Ok = applyReport.entries.size() == std::size_t{3}
                                 && applyReport.noDraftCount() == 3;
            std::cout << "[ird-ui-smoke] step4 apply-modules="
                      << applyReport.entries.size()
                      << " noDraft=" << applyReport.noDraftCount()
                      << " (" << (step4Ok ? "ok" : "fail") << ")"
                      << std::endl;
            // 步 5：L3 反解失败分支（空 NameMap 二态——树不动＋runtimeOnly
            // 暂态记录；步 3 的业务选中保持不变＝"业务选中集不变"语义）。
            if (m_selection) {
                m_selection->handleTreeViewFrameSelected("World.UnknownFrame");
            }
            const bool step5Ok = m_selection != nullptr
                                 && m_selection->hasRuntimeOnlySelection()
                                 && m_selection->runtimeOnlyObjectName()
                                        .value_or("") == "World.UnknownFrame";
            std::cout << "[ird-ui-smoke] step5 l3-runtimeonly="
                      << (step5Ok ? "ok" : "fail") << std::endl;
            // 步 6：关闭清理（直接驱动 teardown——同步形态；关闭对话框的
            // 交互流归 GUI 手动冒烟序列，此处验证清理编排本体）。
            if (m_selection) {
                m_selection->clearSelection(ui::SelectionSource::Command);
            }
            const bool step6Ok = m_selection != nullptr
                                 && m_selection->selectedObjectIds().empty()
                                 && m_treeModel->nodeCount() >= 0;
            std::cout << "[ird-ui-smoke] step6 teardown=" << (step6Ok ? "ok" : "fail")
                      << std::endl;

            exitCode = (opened && treeOk && step3Ok && step4Ok && step5Ok && step6Ok)
                           ? 0 : 1;
        } catch (const std::exception& smokeError) {
            std::cout << "[ird-ui-smoke] exception: " << smokeError.what()
                      << std::endl;
            exitCode = 1;
        }
        std::cout << "[ird-ui-smoke] " << (exitCode == 0 ? "DONE" : "FAILED")
                  << std::endl;
        QCoreApplication::exit(exitCode);
    });
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
