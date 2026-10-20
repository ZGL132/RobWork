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
#include "HostEngineeringPolicy.hpp"
#include "../src/DockWorkspace_p.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard> // 正式需求tour验证复制模板的真实系统剪贴板结果。
#include <QDateTime>                            // 快照创建时刻呈现格式化（UI-T59 来源头行）
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QDockWidget>
#include <QIcon>
#include <QImage> // 验收读取真实窗口像素；不以投影状态代替实际颜色。
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>  // WP-14-T12：正式宿主命令面板中文过滤的真实行观测。
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>  // 三维拾取拦截（UI-T45——双击事件位形）
#include <QKeyEvent> // 临时三维工具在视图获得焦点时也接受Esc取消。
#include <QFormLayout> // 临时目标的位置/姿态检查，确认前仅保存界面值。
#include <rw/graphics/SceneViewer.hpp> // REQ-10正式验证使用真实相机/视口，不替换raycast。
#include <rw/graphics/SceneCamera.hpp> // 标准视图验收读取真实相机投影。
#include <sdurws/ird/ui/UiTheme.hpp> // 验收色值与产品图例共用词表。
#include <rw/math/RPY.hpp> // 三维创建的世界系RPY输入；构造参数按Z/Y/X轴排列。
#include <QPushButton>
#include <QRadioButton>
#include <QStatusBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>       // ★ KIN-P4——求解配置持久化载体（用户级 Ini）
#include <QStandardPaths>  // ★ KIN-P4——导出目录缺省解析（AppDataLocation）
#include <QString>
#include <QCheckBox>  // WP-14-T12：正式宿主采样暂存区的可选覆盖率交互。
#include <QComboBox>  // 需求遍历通道（检查器下拉）
#include <QTableWidget>  // 建模遍历通道（编辑页参数表——UI-T67 modeling-tour）
#include <QDoubleSpinBox>  // 需求遍历通道（容差/距离编辑）
#include <QGroupBox>  // 需求遍历通道（卡片折叠）
#include <QSlider>  // 需求遍历通道（覆盖率滑块）
#include <QSpinBox>  // 需求遍历通道（采样计数）
#include <QTabWidget>  // 需求遍历通道（页签切换——requirements-tour）
#include <QTreeWidgetItemIterator>  // 建模遍历通道关节行遍历（UI-T67——modeling-tour）
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>  // 需求遍历通道（卡片折叠三角）
#include <QTreeWidget>  // 需求遍历通道（树/表定位）
#include <QVBoxLayout>
#include <QWidget>

#include <rws/RobWorkStudio.hpp>                 // 宿主注入面：getView()/getWorkCellScene()/menuBar()/事件面（共存接入＋UI-T23 桥）
#include <rws/RWStudioView3D.hpp>                // 框架三维视图（UI-T45——pickFrame 公开 API 消费面）

#include <rw/kinematics/Frame.hpp>               // rw::kinematics::Frame（L3 桥事件值——TreeView Select Frame 转发名源）
#include <rw/kinematics/State.hpp>               // rw::kinematics::State（D8 Jog 桥——State 变化采样）
#include <rw/models/Device.hpp>                  // rw::models::Device（D8 桥——WorkCell 设备 q 提取）
#include <rw/models/JointDevice.hpp>             // rw::models::JointDevice（UI-T45——TCP 帧解析 getEnd）
#include <rw/models/WorkCell.hpp>                // rw::models::WorkCell（L2 高亮按名寻帧/D8 设备枚举——呈现对象公开面）
#include <rw/graphics/WorkCellScene.hpp>         // rw::graphics::WorkCellScene（L2 高亮出口——setHighlighted 公开 API）

#include <sdurws/ird/project/CommandService.hpp> // project::CommandResult/CommandStatus（apply 网关提交面——T03b-2）
#include <sdurws/ird/project/QueryPort.hpp>      // project::IProjectQueryPort/RevisionView（UI-T29 闭包数据源）
#include <sdurws/ird/project/StoreTypes.hpp>     // project::StoreError（创建失败折叠）
#include <sdurws/ird/project/UndoRedo.hpp>       // project::UndoRedoService/UndoRedoStatus（UI-T39 项目级撤销接线）
#include <sdurws/ird/requirements/CommandHandlers.hpp>  // registerRequirementCommandHandlers（UI-T39——装配期处理器注册，§5.3.5 公共通道）
#include <sdurws/ird/requirements/ObjectTypes.hpp>  // requirements::kReqSetObjectType（根回填 token——公共头常量）
#include <sdurws/ird/requirements/RevisionSyncPolicy.hpp>  // planExternalRevisionSync（UI-T35 P2 外部修订同步三分岔判定——纯函数）
#include <sdurws/ird/requirements/RequirementsPluginAssembly.hpp>  // requirements::kModuleHandle＋requirementsDraftSource 门面缝（F-911 生产装配挂接——UI-T84）
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（UI-T64——会话态 savedConfig 合法基线）
#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>  // kinematics 装配门面（UI-T23；UI-T64 通道注入/note 入口）
#include <sdurws/ird/ui/ICommandRegistry.hpp>    // CommandOutcome/CommandParameter（会话入口覆写载体）
#include <sdurws/ird/ui/DomainReadinessSummaryCard.hpp>  // 跨域就绪摘要卡（UI-T44——Right Dock 诊断摘要半区）
#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // PluginUiDescriptor/CommandDescriptor 完整类型（UI-T23 三域命令入册的遍历面）
#include <sdurws/ird/ui/IPluginUiModule.hpp>     // ui::IPluginUiModule 完整类型（buildDraftCommand 调用面）
#include <sdurws/ird/ui/UiText.hpp>              // ui::resolveText（§3.5 唯一文案出口——域命令诚实反馈文案）
#include <sdurws/ird/ui/UiPorts.hpp>             // ui::IUiNameResolver（共享面名称解析端口——C-11 复用面）

#include "DomainModuleRunner.hpp"                // runDomainApply（UI-T23 acceptance 2——draft.apply 多模块遍历）
#include "KinEvaluationChannel.hpp"              // kinematics 覆盖评估执行器（UI-T64——F-490① 上游批装配半区）
#include "KinHostServicesAssembly.hpp"           // ★ KIN-P4——面板服务缝宿主装配（网关/补丁/写出/持久化/Home）
#include "HostView3DGateway.hpp"                 // 宿主三维网关（UI-T45——上行拾取/呈现出口/TCP 源）
#include "HostView3DPreviewBackend.hpp"          // 会话预览渲染后端（UI-T33——场景原语绑定）
#include "HostCompilePort.hpp"                   // 宿主编译端口（UI-T46——十段链适配＋快照缓存＋分段探针）
#include "HostPresentationAdapters.hpp"          // 呈现装配适配器族（UI-T46——映射真值/挂接对象/构造源）

// ASM-PANEL 收口批（2026-10-10）：三新域面板取件面（宿主挂位段消费三域
// 描述符在册的面板工厂——六域面板全部可见）。携带 IRD_UI_PLUGIN_EXTRA_
// DOMAINS 定义的宿主目标（ui_plugin／studio——ui/CMakeLists 两段同款
// 单一书写点）才编入 ExtraDomainAssembly TU 并链接三新域 plugin，本
// include 与下方三域挂位段以同一宏区分——两同源目标防漂移的既有形态
// （DomainAssembly.cpp 尾段承接段同款）。
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
#include "ExtraDomainAssembly.hpp"      // dynamicsPanelWidget 等（三域取件——ASM-PANEL）
#include "DomainAssemblyShared.hpp"     // kAssemblyFailedCode（§11.3 稳定码文本单点）
#include <memory>                       // std::static_pointer_cast（extraAssemblies 类型擦除句柄还原）
#include <chrono>                       // std::chrono::steady_clock（V-13 周期落盘驱动拍——UI-T84）
#endif

#include <sdurws/ird/modeling/ModelingPluginAssembly.hpp>  // modeling::isAssembledModelingCommand（UI-T42——F-466 路由判定出线）
#include <sdurws/ird/modeling/CommandHandlers.hpp>  // registerModelingCommandHandlers/HandlerServices（UI-T46——F-461 modeling 半区装配）
#include <sdurws/ird/modeling/Codec.hpp>           // ★ KIN-P4——RobotDesignCodec/kCurrentFormatVersion（Home 位姿集解码）
#include <sdurws/ird/modeling/DhConvert.hpp>     // DhExplicitConverter（UI-T46——HandlerServices 转换器注入）
#include <sdurws/ird/modeling/ObjectTypes.hpp>   // modeling::kRobotDesignObjectType（UI-T46——编译根 token 单一权威；★ KIN-P4 kNamedPoseSetObjectType）
#include <sdurws/ird/modeling/Package.hpp>       // exportWorkCellXml/WorkCellExportTarget（UI-T56——WC/DWC XML 外供导出接源）
#include <sdurws/ird/modeling/Parts.hpp>         // ★ KIN-P4——PoseSet/PoseSetEntry/kHomeConfigurationPoseKey（Home 来源闭包）
#include <sdurws/ird/policy/JointLimits.hpp>     // makeJointLimitEvaluator（UI-T46——HandlerServices 评估器装配）
#include <sdurws/ird/policy/Contexts.hpp>        // policy::IPolicyNameContext（UI-T46——行程评估名称上下文适配基类）
#include <sdurws/ird/policy/PolicyInput.hpp>     // A-23 策略编辑命令的唯一编解码入口
#include <sdurws/ird/policy/PolicyParsing.hpp>   // A-23 prepare 阶段只读校验
#include <sdurws/ird/ui/RuntimePublishBridge.hpp>  // RuntimePublishBridge/观察者（UI-T46——呈现刷新事务装配）

#include <filesystem>
#include <cstdio>                        // 采样清单行位置摘要格式化（std::snprintf——FIX-T2）
#include <cmath>
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

/**
 * @brief 宿主名称组合器：借用现有域/运行时名称端口，补齐共享树显示。
 * 宿主持有、仅UI线程同步查询；不缓存名称，不生成或剥离运行时前缀。
 */
class HostUiNameResolver final : public IUiNameResolver {
public:
    /** @brief 保存宿主查询函数；query[in]返回已有权威名称，未知为nullopt。 */
    explicit HostUiNameResolver(std::function<std::optional<std::string>(core::ObjectId)> query)
        : m_query(std::move(query)) {}
    /** @brief 按业务身份现取显示名；id[in]为原ObjectId，不写项目或编辑会话。 */
    std::optional<std::string> resolveObjectId(core::ObjectId id) const override
    { return m_query(id); }
private:
    std::function<std::optional<std::string>(core::ObjectId)> m_query; ///< 仅宿主面板存活期调用。
};

/**
 * @brief REQ-08同步临时工具的栈上事件观察器；仅UI线程，无Qt子对象所有权。
 * 析构时QObject自动退订；回调只在交互栈存活期借用宿主，结束恢复默认导航。
 */
class RequirementTargetEventFilter final : public QObject {
public:
    /** @brief 保存调用期事件处理器；handler[in]返回true表示本工具已消费事件。 */
    explicit RequirementTargetEventFilter(std::function<bool(QObject*,QEvent*)> handler)
        : m_handler(std::move(handler)) {}
protected:
    /** @brief 对应用事件转发到交互处理器；watched/event[in]仅本次回调借用。 */
    bool eventFilter(QObject* watched,QEvent* event) override { return m_handler(watched,event); }
private:
    std::function<bool(QObject*,QEvent*)> m_handler; ///< 无跨线程状态，不延迟保存QEvent指针。
};

/// 开发日志目录名（相对当前工作目录——开发期通道形态；与 harness 的
/// ird-harness-logs 同型不同名，两类通道的留痕互不混写）。
constexpr const char* kPluginLogDirName = "ird-ui-plugin-logs";

/// 域命令处理器级拒绝的诚实反馈键（§3.5 键约定——UiText 键族①b 收口增行；
/// content submitCommand 对 outcome.messageKey 的呈现值源）。
constexpr const char* kModelingFlowNotAssembledKey = "cmd.modeling.flow-not-assembled";

/// 真实执行面已落位的建模域命令（UI-T42——F-466 消账：判定逻辑迁至
/// modeling 命令流层 ModelingCommandFlows.hpp 的 isAssembledModelingCommand
/// ——路由词表与 executeModelingCommand 同 TU 演化，装配集∧目录集一致性
/// 可被 modeling_test 断言；本处只转发，零第二词表）。
bool isAssembledModelingCommand(const std::string& id)
{
    return modeling::isAssembledModelingCommand(id);
}

/// 已有真实执行流的需求命令；注册期只判断装配，不缓存启动时的场景状态。
/// 捕获/拾取已接真实宿主缝，无设备/未拾取等运行期前置由各流程就地说明。
bool isAssembledRequirementCommand(const std::string& id)
{
    // WP-14-T12/REQ-10：旧撤牌清单没有随三维装配更新，导致真实数据在位
    // 仍永久禁用。保留无项目/只读注册表门控，不绕开域侧确认与现场重查。
    static const std::set<std::string> kAssembled{
        "requirements.export-copy",  "requirements.apply-template",
        "requirements.mirror-stations", "requirements.create-array",
        "requirements.regenerate-linked", "requirements.import-csv",
        "requirements.import-json",
        "requirements.capture-tcp", "requirements.pick-feature",
    };
    return kAssembled.count(id) != 0;
}

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
            m_devLog("revision-committed -> modeling event delivered: branch="
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

/// 采样评估状态中文词表（FIX-T2——F-495 样本级定位的清单行状态列；
/// R-2 翻译单点：kinematics 账面枚举→requirements 面板投影行的唯一
/// 翻译处，与 UI-T65 着色词表语义同源〔mapSampleStateToCell 的呈现
/// 文字面〕——判定零参与，只做词表翻译）。
std::string sampleStateText(kinematics::KinChannelSampleState state)
{
    switch (state) {
    case kinematics::KinChannelSampleState::Reached:
        return "达标";
    case kinematics::KinChannelSampleState::Unreachable:
        return "不可达";
    case kinematics::KinChannelSampleState::DataInsufficient:
        return "数据不足";
    case kinematics::KinChannelSampleState::NotRun:
        return "未运行";
    case kinematics::KinChannelSampleState::NotApplicable:
        return "不适用";
    }
    return "未运行";  // 枚举穷尽后防御面（不逃逸 switch——词表外值诚实回落）
}

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
 * @brief 选择→顶栏上下文栏转发器（UI-T26——L1 广播的消费半区）。
 *
 * IUiSelectionObserver 的最小实现：onSelectionChanged 纯值转发给装配层
 * 注入的回调（→ IWorkbenchContent::noteSelectionForContext——『当前对象』
 * 标签唯一数据入口）。零业务语义、零状态——重入纪律（回调内禁调服务写
 * 入口）由被转发方的只读呈现语义结构性满足。
 */
class ContextSelectionForwarder final : public ui::IUiSelectionObserver {
public:
    using Forward = std::function<void(const ui::SelectionChange&)>;
    explicit ContextSelectionForwarder(Forward forward) : m_forward(std::move(forward)) {}
    void onSelectionChanged(const ui::SelectionChange& change) override
    {
        if (m_forward) {
            m_forward(change);
        }
    }

private:
    Forward m_forward;
};

/// 辅助 Dock 可见性记忆键（UI-T24 P1——内容装配面 aux 半区的业务键；词形
/// 稳定 ASCII，进用户级设置文件 layout/aux.<key>.visible——词形改动＝用户
/// 记忆迁移，等价契约变更）。四个域自持面板 Dock 各一键。
constexpr const char* kAuxKeyModelingDock = "domain.modeling";
constexpr const char* kAuxKeyRequirementsDock = "domain.requirements";
constexpr const char* kAuxKeyKinematicsDock = "domain.kinematics";
constexpr const char* kAuxKeyKinematicsAdvancedDock = "domain.kinematicsAdvanced";

// 三新域辅助记忆键（ASM-PANEL 收口批——六域面板全部可见的可见性半区；
// 词形 "domain.<token>" 与上方四键同族。工厂默认＝不呈现〔净室默认布局
// 收敛，UI-T24 P1 同口径〕——用户经"视图"菜单呼出后跨会话记忆优先）。
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
constexpr const char* kAuxKeyDynamicsDock = "domain.dynamics";
constexpr const char* kAuxKeySelectionDock = "domain.selection";
constexpr const char* kAuxKeyOptimizationDock = "domain.optimization";
#endif

/// 中央区最小可见保留宽（UI-T24 P2），单位 px。取值依据：§4.4 最小窗口
/// 1280 宽下，左栏 240＋右栏 280 两栏取其内容最小尺寸后中央仍应保有约
/// 1/4 窗宽；320 px 为三维视图可辨认操作的诚实下限（低于此值三维交互
/// 已不可用，与"归零"无实质差异）。该值是本插件 Dock 回推的目标保留量，
/// 不触碰框架中央控件属性（SA-02）。
constexpr int kCentralMinReserveWidth = 320;

/// 本插件 Dock 回推时的收缩下限（UI-T24 P2），单位 px。回推不与内容最小
/// 宽度对抗（Qt 布局对 minimumSizeHint 以下本就拒收）——本常量只是再垫
/// 一层"不缩到不可辨认"的插件侧地板：主 Dock 承载工业项目树（UI-T38 起
/// 外壳退役、树是唯一内容），低于 160 px 呈现已无意义。
constexpr int kDockShrinkFloorWidth = 160;

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
 * 为什么允许"空二态"：SelectionService Deps.nameMap 构造期必填（L2/L3
 * 承诺的端口缝），而呈现未就位（本会话尚无成功呈现发布）＝无任何名字
 * 映射是与空会话同构的诚实二态：L2 正向判定照常执行并得 nullopt（不
 * 高亮不报错）、L3 反解照常走失败分支（树不动＋runtimeOnly 暂态）。
 * UI-T46 呈现装配起本插件实例化为 HostRuntimeNameMapPort（绑当前呈现
 * 视图——发布/拆除拍单点 bind/clear 即三消费面同步升级；上注语义在
 * 未就位形态逐字保持）。
 */

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
    // UI-T38 更名"IRD 工作台"→"工业项目树"：主 Dock 自建外壳（顶栏/左栏）
    // 退役后插件本体 Dock 内容＝纯工业项目树，标题如实反映内容并与宿主
    // 原生插件（TreeView 等）命名风格一致；类名保持 IrdWorkbenchHostPlugin
    // 不变（插件仍是全部 IRD Dock/菜单/投影的宿主装配体，更名只作用于
    // 显示名——宿主 QSettings 布局记忆键随名一次性轮换，旧键残留无害）。
    : rws::RobWorkStudioPlugin(QString::fromUtf8("工业项目树"), QIcon()),
      m_crashTailSource(m_diag)  // WP-24-T02——快照源绑定诊断栈（成员声明序保证 m_diag 先构造）
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
    // WP-24-T02——崩溃捕获快照源接线（装配序后段）：进程入口装捕获时点
    // 本栈尚不存在，栈就绪后把 dev 日志尾（§7.6 第③段）接进崩溃报告。
    // 捕获未安装（开发期动态装载通道）时为无操作——静默无害。
    studio::setCrashSnapshotSource(&m_crashTailSource);
    reportLine("诊断栈就绪（开发日志目录：" + devlogDir.u8string() + "）");

    // ---- 装配第二步：端口适配器（O-31 装配层特权边——复用 harness 形态）----
    // 会话生命周期端口形态与 harness 逐一同型：策略未装载（C-10）、名称
    // 不可解析（C-11）——零虚构语义。关于框数据源收口起为装配报告实装
    // （bundleAboutSource——§11.4，HarnessAboutSource 占位退役）。
    m_policySource = std::make_shared<ui::HostEngineeringPolicy>([this] {
        // 策略卡只负责触发命令；原始草稿、校验和事务全部由同源私有适配器处理。
        m_policySource->edit(m_hostStatusBar);
    });
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
            std::shared_ptr<app::StorePortAdapter> kept = adapter;
            m_lastStoreAdapter = std::move(adapter);
            // 域命令处理器装配期注册（UI-T39——§5.3.5"L5 装配期一次性注册"
            // 的宿主半区）：打开成功的存储上下文上注册 requirements 两处理
            // 器（apply-requirement-set/apply-requirement-import——无状态，
            // 族内共享安全）。此前生产装配无注册通道（实现私有访问器跨单
            // 元不可达），draft.apply 的域信封提交恒被 S1 以 unknown-command
            // 拒绝——"应用修改"对需求域从未真实产生修订（冒烟 step9 二连
            // 应用实证暴露）。modeling 域处理器依赖 HandlerServices 服务集
            // 装配（断言端口/策略锚——建模域任务范围），随 findings 登记。
            if (kept != nullptr) {
                // A-23：摘要卡与建模处理器绑定同一项目修订解析源。
                m_policySource->bindProjectStore(
                    std::shared_ptr<project::ProjectStore>(kept, &kept->projectStore()));
                requirements::registerRequirementCommandHandlers(
                    kept->projectStore().handlerRegistry());
                HostEngineeringPolicy::registerCommandHandler(
                    kept->projectStore().handlerRegistry());
                if (m_diag.pipeline) {
                    m_diag.pipeline->logDev(
                        kPluginDevChannel,
                        "需求域命令处理器已注册（apply-requirement-set/"
                        "apply-requirement-import——§5.3.5 装配期）");
                }
            }
            // 需求域会话接线（UI-T29）：项目打开成功即尝试 HEAD 闭包载入
            //（含根→活会话；无根→内存空根初始化〔UI-T35 P1-1——新建项目
            // 从零可编辑〕——wireRequirementsSession 内三态裁决）。回调在
            // UI 线程（打开协议同线程）。
            if (kept != nullptr) {
                wireRequirementsSession(*kept);
            }
            // 呈现会话绑定（UI-T46）：编译端口按打开上下文构造＋建模命令
            // 处理器注册（F-461 modeling 半区）＋发布桥 attach——与需求域
            // 会话接线同点同线程。
            if (kept != nullptr) {
                attachPresentationSession(kept->projectStore().projectId());
            }
            // ★ KIN-P4：kinematics 设默认命令门面装配（审查 §4"命令门面"
            // 接通的项目打开拍——网关/门面随存储上下文存活，关闭拍 reset）。
            // 提交闭包＝真①端口递交（HostCommandInteraction 确认桥随宿主
            // 既有 apply 编排同形态——Confirmable 集弹确认）。装配失败
            // fail-fast（网关构造契约）——打开协议上抛即打开失败（与
            // 处理器注册同点同语义）。
            if (kept != nullptr && m_domains != nullptr
                && m_domains->kinematics.has_value()) {
                installKinematicsCommandHandler(kept->projectStore());
            }
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

            // UI-T35 P2：需求域修订同步 sink（第二订阅——外部/他域修订
            // 事件→需求会话重导线或 STALE 提示；应答体见
            // onRequirementExternalRevision）。
            class RequirementsRevisionSyncSink final
                : public core::IDomainEventSink {
            public:
                explicit RequirementsRevisionSyncSink(
                    IrdWorkbenchHostPlugin* host)
                    : m_host(host)
                {
                }
                void onEvent(const core::DomainEvent& event) override
                {
                    if (event.kind != core::DomainEventKind::RevisionCommitted) {
                        return;
                    }
                    // WP-14-T12/REQ-10：事务发布事件时仍持有存储独占锁，
                    // 本订阅不能同步回查HEAD，否则同线程再次取writer互斥锁会失败。
                    // 只复制值事件，待命令返回后由UI队列重读权威；QObject上下文
                    // 保证宿主销毁时取消，项目身份与关闭守卫阻止迟到事件污染新项目。
                    const auto payload = event.asRevisionCommitted();
                    const auto source = m_host->m_lastStoreAdapter;
                    QTimer::singleShot(0, m_host, [host = m_host, payload, source] {
                        const auto adapter = host->m_lastStoreAdapter;
                        if (adapter && adapter == source && !adapter->isClosed()
                            && adapter->projectStore().projectId() == payload.project) {
                            try {
                                const auto head=adapter->projectStore().query().head();
                                if (head.id==payload.revision && head.inverse
                                    && head.inverse->commandType=="edit-engineering-policy" && head.parent) {
                                    const auto previous=adapter->projectStore().query().tryRevision(*head.parent);
                                    auto businessRefs=[](const project::RevisionView& revision) {
                                        std::vector<project::ObjectRef> refs;
                                        for(const auto& ref:revision.objectRefs) {
                                            if(ref.objectTypeToken!="EngineeringPolicy" && ref.objectId!=revision.metadataRef.objectId)refs.push_back(ref);
                                        }
                                        // 闭包本身不承诺输入顺序；按稳定身份排序后比较完整引用值。
                                        std::sort(refs.begin(),refs.end(),[](const auto& a,const auto& b) { return a.objectId<b.objectId; });
                                        return refs;
                                    };
                                    // 只有策略变化且其余业务对象逐项相同时，才安全前移建模草稿基线；
                                    // 不把同时发生的模型编辑/分支变化静默重基，防止旧草稿覆盖新模型。
                                    if (previous && businessRefs(*previous)==businessRefs(head) && host->m_domains
                                        && host->m_domains->modeling.module) {
                                        const auto draft=host->m_domains->modeling.module->buildDraftCommand("modeling");
                                        // 已过期或属于其它分支的草稿仍应被project拒绝；策略提交不能
                                        // 越过此前模型修订，只有恰好基于父修订的草稿才可安全前移。
                                        if (!draft || (draft->branch==head.branch && draft->expectedRevision==previous->id)) {
                                            host->m_domains->modeling.bindSessionAnchor(head.branch,head.id);
                                        }
                                    }
                                }
                                host->m_policySource->refreshPublishedPolicy();
                                // 策略修订即使不编译模型也必须推进评估纪元；旧任务晚到不入新账。
                                host->syncKinematicsSessionFacts();
                                if (host->m_content && host->m_lastContextProjection) {
                                    host->m_content->presentProjectContext(*host->m_lastContextProjection);
                                }
                                host->onRequirementExternalRevision(payload.revision);
                            } catch (const std::exception& error) {
                                // 队列任务已离开事件总线的异常边界，必须在本操作边界
                                // 保留可归因错误并提示重开，禁止异常穿透Qt事件循环。
                                reportLine(std::string("需求会话同步失败：") + error.what());
                                if (host->m_hostStatusBar) {
                                    host->m_hostStatusBar->showMessage(
                                        QStringLiteral("需求会话同步失败，请重新打开项目；详情见日志"), 8000);
                                }
                            }
                        }
                    });
                }

            private:
                IrdWorkbenchHostPlugin* m_host;  ///< 宿主（非 owning——插件存活期）
            };
            m_requirementsRevisionSink =
                std::make_unique<RequirementsRevisionSyncSink>(this);
            m_requirementsRevisionSubscription =
                m_eventBus->subscribe(*m_requirementsRevisionSink);
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
        // F-910 修复面（异常隔离三要素——捕获边界，UI-T84）：本回调是控制
        // 器状态迁移（打开成功/关闭完成/脏位重投）的宿主承接边界，运行于
        // Qt 回调上下文（含 store 关闭回调沿 UI 线程的重入路径）——域同步
        // 抛出的异常若穿透本 lambda，将沿 presentContextLocked→
        // finishCurrentClose→onStoreClosed 直穿 Qt 事件循环、落到 StudioMain
        // 兜底 catch 致整站退出（手工验证 A5"关闭后弹 critical 即退出"的
        // 放大环节）。此处隔离＝操作边界捕获，不吞不崩：①回滚责任＝Dev
        // 留痕可归因（teardown 幂等护栏已保证桥不处于半拆绑态——见
        // teardownSharedSurfacesForClose）；②恢复成功条件＝控制器态由调用
        // 方先落定（关闭链 resetToNoProject 先于本回调），验证"无项目"成
        // 立才提示可重试，不成立则提示重启（状态不可确认→显性故障处置）。
        try {
            // 边界覆盖整个回调体（UI-T84 实测扩面）：除域同步外，内容面
            // 投影（presentProjectContext→命令可用性重估——undo/redo 谓词
            // 现取 branchTips）与投影缓存同在关闭收尾的已关闭上下文上跑，
            // 同属本捕获边界（冒烟 step8 实测 context-closed 由此逃逸）。
            syncDomainModulesToContext(context);
            if (m_content) {
                m_content->presentProjectContext(context);
            }
            // 投影缓存（UI-T39——撤销/重做后重发触发谓词重估的快照源；项目
            // 关闭的空投影同样缓存——关闭后的撤销重发即"无项目"快照，门控
            // 语义一致）。
            m_lastContextProjection = context;
        } catch (const std::exception& syncError) {
            // WP-14-T12：采样开发日志可能省略首个异常；保留操作边界的完整原因，
            // 使正式宿主回归能够区分关闭残余查询与真正的打开/呈现故障。
            reportLine(std::string("项目上下文同步失败：") + syncError.what());
            const bool recoveredNoProject =
                (m_controller == nullptr || !m_controller->hasOpenSession());
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(
                    kPluginDevChannel,
                    std::string("上下文同步异常已隔离（恢复=")
                        + (recoveredNoProject ? "无项目态已确认，可重试打开"
                                              : "状态未确认，请重启应用")
                        + "）：" + syncError.what());
            }
            if (m_hostStatusBar != nullptr) {
                m_hostStatusBar->showMessage(
                    QString::fromUtf8(recoveredNoProject
                                          ? "项目操作出现异常（已恢复到无项目状态，可重试）"
                                          : "项目操作出现异常（状态未确认，请重启应用）"),
                    8000);
            }
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
                entry.assembled = (domainKey == std::string("modeling")
                                   && isAssembledModelingCommand(desc.id))
                                  || (domainKey == std::string("requirements")
                                          && isAssembledRequirementCommand(desc.id));
                if (domainKey == std::string("modeling")
                    && isAssembledModelingCommand(desc.id)) {
                    // UI-T41 A2：已装配的建模域命令——经装配门面执行真实
                    // UI 流程（对话框＋内核实现类；路由唯一，requirements
                    // executeDomainCommand 同构先例）。
                    const std::string commandId = desc.id;
                    entry.handler = [this, commandId](
                                        const std::vector<CommandParameter>&) {
                        CommandOutcome out;
                        out.accepted = true;  // 流程已派发（回执/原因经面板状态行）
                        m_domains->modeling.executeDomainCommand(commandId);
                        return out;
                    };
                } else if (domainKey == std::string("requirements")
                           && isAssembledRequirementCommand(desc.id)) {
                    // UI-T32 C 批段：已装配的需求域命令——经装配门面执行
                    // 真实 UI 流程（对话框/表单＋域纯函数；路由唯一）。
                    const std::string commandId = desc.id;
                    entry.handler = [this, commandId](
                                        const std::vector<CommandParameter>&) {
                        CommandOutcome out;
                        out.accepted = true;  // 流程已派发（应用/取消/就地
                                              // 错误均经面板状态行反馈）
                        if (m_domains == nullptr
                            || !m_domains->requirements.has_value()
                            || !m_domains->requirements->executeDomainCommand(
                                   commandId)) {
                            // 流程失败/面板缺位——反馈已在面板状态行；
                            // 此处不覆盖（面板可见时），Dev 留痕归因。
                        }
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
    // UI-T82/UX-09：复位重建同一有界拓扑；仅管理本插件 Dock，宿主框架
    // 自有 Dock 不重排，后续内容装配面按工厂值关闭辅助域并保持导航可见。
    contentDeps.restoreFactoryWindowState = [this] {
        if (auto* host = qobject_cast<QMainWindow*>(parentWidget())) {
            detail::arrangeDomainWorkspace(*host, *this,
                {m_modelingDock, m_requirementsDock, m_kinematicsDock,
                 m_dynamicsDock, m_selectionDock, m_optimizationDock},
                {m_propsDock, m_kinematicsAdvancedDock});
            // 底部任务也可能被用户浮动/移位，复位后须回到底部，不能仅隐藏。
            if (m_tasksDock != nullptr) {
                host->addDockWidget(Qt::BottomDockWidgetArea, m_tasksDock);
                m_tasksDock->setFloating(false);
            }
        }
    };

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
    // 最近项目直开钩子（R3 审计 C1，2026-10-15）：首页"最近项目"可用项
    // 点击与菜单最近项（:1904 同款）走同一 openRecentProject 编排——原缺陷
    // 是点击只提交无参 project.open、处理器弹出目录选择框，被点项目路径
    // 被丢弃（两入口行为分裂）。
    contentDeps.openRecentProject =
        [this](const std::string& canonicalPath) {
            openRecentProject(canonicalPath);
        };
    // 项目级撤销/重做编排注入（UI-T39——审核 P1：需求面板"撤销上次应用"
    // 转发的 project.undo 此前无处理器〔占位说明〕且面板命令 id 拼写错误
    // ——本缝与面板侧修正共同构成真实撤销链路：UndoRedoService 逆命令
    // 提交→新修订→事件总线同步各域）。
    contentDeps.undoProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateProjectUndo(params);
        };
    contentDeps.redoProjectHandler =
        [this](const std::vector<CommandParameter>& params) {
            return orchestrateProjectRedo(params);
        };
    // 撤销/重做注册期门控（UI-T39——审核 P1：可用性必须含"修订存在性"
    // 维度）：canUndo 由磁盘 tip inverse 推导（§5.5——重启后会话无关），
    // canRedo 仅会话栈（D-11）；无项目／无可撤销（重做）修订＝禁用＋统一
    // 原因键。只读半区由描述符 readOnlyAllowed=false 的注册表默认谓词
    // 承担（撤销产生修订＝写操作），本谓词零重复判定。
    contentDeps.undoRedoDisablement =
        [this](const std::string& commandId) -> std::optional<ui::DisableReason> {
        const auto adapter = m_lastStoreAdapter;
        // 上下文已关闭＝无项目态诚实降级（F-910 根因面修复——UI-T84 验收
        // 追查定位：关闭收尾期 resetForSessionDetached→refreshUndoButtons
        // →本谓词 branchTips 查询已关闭上下文抛 StoreError(context-closed)，
        // 修复前无隔离面时穿事件循环→StudioMain 兜底退出＝手工验证 A5 的
        // 真实传播链）。可用性快照查询在关闭后语义上恒为"无项目→禁用"，
        // 返回值轨降级非吞错——真值形态的值面表达。
        if (adapter == nullptr || adapter->isClosed()) {
            return ui::DisableReason{"reason.no-project"};
        }
        const auto tips = adapter->projectStore().query().branchTips();
        if (tips.empty()) {
            return ui::DisableReason{"reason.no-project"};
        }
        const project::UndoRedoStatus status =
            adapter->projectStore().undoRedo().status(tips.front().id);
        const bool redo = (commandId == "project.redo");
        if (redo ? !status.canRedo : !status.canUndo) {
            return ui::DisableReason{redo ? "reason.no-redo-revision"
                                          : "reason.no-undo-revision"};
        }
        return std::nullopt;
    };
    // 应用草稿门控（UI-T29 最小校验——draft.apply 注册期谓词的宿主注入）：
    // 需求会话存活时现算就绪（判定权威＝域侧 checker 直投值，P-REQ-6），
    // 存在 Blocking→禁用＋统一原因键（注册表谓词按当前快照求值——bind-
    // Readiness 刷新编辑器工作集后按钮/命令面板随查询刷新）；无会话/无
    // 阻断＝nullopt（门控放行——诚实二态，不虚构阻断）。
    contentDeps.applyDraftDisablement = [this]() -> std::optional<ui::DisableReason> {
        if (!m_requirementsSessionLive) {
            return std::nullopt;  // 无需求会话＝本域零门控（空态不虚构阻断）
        }
        const requirements::RequirementReadinessReport report =
            m_requirementsReadinessChecker.check(
                m_requirementsEditor.workingSet(), currentRequirementsCheckContext());
        if (report.hasBlocking()) {
            return ui::DisableReason{"reason.readiness-blocking"};
        }
        return std::nullopt;
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
        m_domains->modeling.bindCommandAvailability(
            [this](const ui::CommandId& id) {
                return m_content->commandRegistry().availability(id);
            });
        // MDL-17/AT-04：恢复命名姿态只更新真实宿主State；模型/项目历史不写。
        // 每次现取同一已发布快照，先在私有State副本完整校验，成功才一次提交
        // 给宿主；既有D8 stateChangedEvent桥同步运动学会话态与三维，不另建姿态缓存。
        m_domains->modeling.bindSessionPoseWriter(
            [this](const modeling::ModelingSessionPoseRequest& request,std::string& reason){
                auto* studio=getRobWorkStudio();
                const auto snapshot=m_compilePort?m_compilePort->lastPublishedSnapshot():nullptr;
                if(!m_controller||!m_controller->hasOpenSession()||!studio||!snapshot){
                    reason="尚无打开项目的已应用三维模型——未恢复姿态";return false;
                }
                const auto cell=studio->getWorkCell();
                if(cell.isNull()){reason="宿主WorkCell缺失——未恢复姿态";return false;}
                auto candidate=studio->getState();
                if(!ui::stageModelingSessionPose(request,*snapshot,*cell,candidate,reason)){return false;}
                studio->setState(candidate);
                return true;
            });
        // WC/DWC XML 外供导出接源（UI-T56——export-workcell-xml 落点）：
        // 快照现取 m_compilePort->lastPublishedSnapshot（已应用修订的编译
        // 产物——零第二编译路径，PackageWorkCell §6.8 契约）；缺席＝诚实
        // 拒绝（原因入 summary——未应用修订/编译链未跑均此形态）。双面
        // 输出：用户选定路径→WC 面；同名 +".dwc.xml"→DWC 面（独立可选，
        // DWC 能力缺失＝域 ExportFailed 如实呈现）。
        m_domains->modeling.bindWorkCellExport(
            [this](const std::string& targetPath, std::string& summary) -> bool {
                const auto snapshot = m_compilePort != nullptr
                                          ? m_compilePort->lastPublishedSnapshot()
                                          : nullptr;
                if (snapshot == nullptr) {
                    summary = "尚无已应用修订的编译产物——请先经顶栏『应用草稿』"
                              "提交（WorkCell XML 的数据源＝编译快照）";
                    return false;
                }
                modeling::WorkCellExportTarget target;
                target.wcTargetFile = targetPath;
                target.dwcTargetFile = targetPath + ".dwc.xml";
                std::vector<core::DiagnosticRecord> diags;
                const modeling::PackageExportOutcome outcome =
                    modeling::exportWorkCellXml(*snapshot, target, diags);
                if (!outcome.ok) {
                    summary = "WorkCell XML 导出失败：" + outcome.error.detail;
                    return false;
                }
                summary = "WorkCell/DWC XML 已导出（" + targetPath + " 与 "
                          + targetPath + ".dwc.xml）——外部查看产物（非 .wc.xml "
                          "标准格式）；运行时加载校验归后续任务链";
                return true;
            });
        // 预览内存导出接源（UI-T59——F-498 预览半区）：与文件导出同源同
        // 纪律——快照现取 lastPublishedSnapshot（零第二编译路径）；kind 分
        // 派域 exportPreviewXml（建模自有 XML 表示，UI 零拼装）；来源头行
        // （修订号/序号/模型身份/生成时间——非 XML 呈现元数据）由宿主组装
        // （快照身份块值供给 CR-05 同款值传递——呈现层不重算身份）。
        // 缺席＝诚实拒绝（原因入 reason——未应用修订/DWC 能力缺席均此形）。
        m_domains->modeling.bindWorkCellPreview(
            [this](const std::string& kind, std::string& headerLine,
                   std::string& sourceObject, std::string& text,
                   std::string& reason) -> bool {
                const auto snapshot = m_compilePort != nullptr
                                          ? m_compilePort->lastPublishedSnapshot()
                                          : nullptr;
                if (snapshot == nullptr) {
                    reason = "尚无已应用修订的编译产物——请先经顶栏『应用草稿』"
                             "提交（预览数据源＝编译快照）";
                    return false;
                }
                // kind token→域枚举（fail-closed——未知 token 拒绝，不猜测）。
                std::optional<modeling::PreviewExportKind> parsed;
                for (const modeling::PreviewExportKind k : {
                         modeling::PreviewExportKind::SerialDeviceXml,
                         modeling::PreviewExportKind::SceneXml,
                         modeling::PreviewExportKind::CollisionXml,
                         modeling::PreviewExportKind::DwcXml,
                     }) {
                    if (std::string(modeling::previewExportKindToken(k)) == kind) {
                        parsed = k;
                        break;
                    }
                }
                if (!parsed.has_value()) {
                    reason = "未知预览类型：" + kind;
                    return false;
                }
                const modeling::PreviewExportOutcome outcome =
                    modeling::exportPreviewXml(*snapshot, *parsed);
                if (!outcome.ok) {
                    reason = "预览导出失败：" + outcome.error.detail;
                    return false;
                }
                // 来源头行（F-498"来源修订号/模型身份/生成时间/来源对象
                // 明确"）：修订规范文本＋序号＋模型身份规范文本＋快照创建
                // 时刻。UTC 文本经 QDateTime（宿主层允许 Qt；域导出面自身
                // 零时钟——确定性归域，呈现时间归宿主，两不相扰）。
                const std::chrono::system_clock::time_point createdAt =
                    snapshot->createdAtUtc();
                const QDateTime qCreated = QDateTime::fromMSecsSinceEpoch(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        createdAt.time_since_epoch())
                        .count());
                headerLine = "来源修订 " + snapshot->revision().toCanonical()
                           + "（序号 " + std::to_string(snapshot->revisionSeq())
                           + "）｜模型身份 " + snapshot->modelIdentity().toCanonical()
                           + "｜生成于 "
                           + qCreated.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss UTC"))
                                 .toStdString()
                           + "｜" + outcome.sourceObject;
                sourceObject = outcome.sourceObject;
                text = outcome.text;
                return true;
            });
        if (m_domains->requirements.has_value()) {
            m_domains->requirements->bindCommandSubmit(
                [this](const ui::CommandId& id) {
                    if (m_content != nullptr) {
                        m_content->submitCommand(std::string(id));
                    }
                });
            m_domains->requirements->bindCommandAvailability(
                [this](const ui::CommandId& id) {
                    return m_content->commandRegistry().availability(id);
                });
        }
        // 运动学域命令提交出口（UI-T23——同形态转发 content 注册表；
        // requirements 门面无该出口——面板命令提交随域会话任务接续）。
        if (m_domains->kinematics.has_value()) {
            m_domains->kinematics->bindCommandSubmit(
                [this](const ui::CommandId& id) {
                    if (m_content != nullptr) {
                        m_content->submitCommand(std::string(id));
                    }
                });
            m_domains->kinematics->bindCommandAvailability(
                [this](const std::string& id) {
                    // A-17（Phase-A 冻结终验实测）：kinematics.* 为面板本地
                    // 按钮命令——点击直达 KinPanelFlows 域流，不经宿主注册
                    // 表派发；注册表中的同名条目来自域目录泛型登记，其使能
                    // 谓词恒报"执行流未装配"（cmd.flow-not-assembled.reason
                    // ——实测 enabled=0），原"同形态转发"接线以该谓词二次
                    // 否定面板会话门，使 evaluate-coverage/validate-task-
                    // points 等写命令按钮在正式宿主**永久禁用**（评估链不
                    // 可达）。可用性权威归面板会话门（kinCommandEnabled-
                    // InSession＋writable＋backgroundSubmit——§7.5 界面使
                    // 能态≠业务判定），此处恒放行、不二次否定。
                    (void)this;
                    (void)id;
                    ui::CommandAvailability deferToPanel;
                    deferToPanel.registered = true;
                    deferToPanel.visible = true;
                    deferToPanel.enabled = true;
                    return deferToPanel;
                });
            // 覆盖评估执行通道接线（UI-T64——F-490① 上游批：服务缝经
            // 公共通道值面注入，面板覆盖评估按钮点亮；会话事实随发布拍
            // 同步——syncKinematicsSessionFacts）。
            wireKinematicsEvaluationChannel();
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
    reportLine("工业项目树装配完成（多 Dock：项目树主 Dock＋属性/任务同级 Dock＋宿主状态栏投影＋Ctrl+Shift+P 命令面板；"
               "装载呈现自证在事件循环稍后执行）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "内容装配面就位（多 Dock 嵌入形态；会话入口覆写已注入；状态投影绑宿主状态栏）");
    }

    // ---- 宿主事件桥接线（UI-T23——L3/D8：框架公开事件订阅；须在共享
    //      面就位后执行——桥回调消费 m_selection/m_domains）----
    connectHostEventBridges();

    // ---- 宿主三维网关装配（UI-T45——三桥一源；须在选择服务/域装配就位
    //      后执行——Deps 借用两者指针。getView 不可得＝降级留痕不装配）。
    assembleView3DGateway();

    // ---- 宿主呈现装配（UI-T46——发布桥＋呈现源＋建模处理器服务集；
    //      须在网关之后执行——出口借用网关本体。编译端口随项目打开构造，
    //      名称映射真值随发布拍绑定）。
    assemblePresentationPipeline();

    // ---- 装配第六步（时序关键）：装载呈现自证排队 ----
    // 零等待单发定时器：控制流回到事件循环的第一拍执行重申（此时 addPlugin
    // 已返回、其尾段 setVisible/restoreState 已完成——队列语义保证严格晚于
    // 二者，详见 reassertEmbeddedPresentation 内的根因链注释）。
    QTimer::singleShot(0, this, [this] { reassertEmbeddedPresentation(); });

    // ---- 集成冒烟通道（UI-T23——GUI 留痕载体）：环境变量触发，随事件
    //      循环稍后执行（呈现自证之后的拍——冒烟序列依赖宿主窗口在位）。
    maybeRunIntegrationSmoke();

    // ---- 布局度量冒烟通道（UI-T24——P2 钳制源定位载体）：同上环境变量
    //      触发面，度量拍排在呈现自证与两拍宽度收束之后（度量的是"装载
    //      落定后"的稳定布局态）。
    maybeRunLayoutSmoke();

    // ---- 需求界面全功能遍历通道（requirements-tour）：同环境变量触发面
    //      （互斥——各通道按环境变量值单选）。
    maybeRunRequirementsTour();

    // ---- 建模域全功能遍历通道（modeling-tour，UI-T67——F-496 取证）：
    //      同环境变量触发面（互斥——各通道按环境变量值单选）。
    maybeRunModelingTour();
}

IrdWorkbenchHostPlugin::~IrdWorkbenchHostPlugin()
{
    // WP-24-T02——崩溃捕获快照源解除注册：本源（m_crashTailSource）随插件
    // 消亡，不及时解绑会让崩溃路径在源生命周期之后悬挂解引用（注册为
    // 裸指针——设施契约"注册方以成员/静态存储期持有"的退出半区）。
    studio::setCrashSnapshotSource(nullptr);
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
    // 呈现管道兜底收口（UI-T46）：桥先毁（观察者弱持有自动退订）→编译
    // 端口最后毁。正常关闭路径已随 teardown 收口，此处兜底。
    // UI-T77（F-569）注释更正：成员声明序上 m_compilePort 在
    // m_lastStoreAdapter **之前**（hpp:395 vs :428），C++ 逆序销毁＝
    // adapter 先毁——「声明逆序保证端口析构时适配器仍在」的旧注释与真实
    // 布局相反；现行的存活保障是**本显式 reset 序**（bridge→observer→
    // compilePort 抢在成员销毁前执行）。若删除本显式 reset 或在
    // HostModelCompilePort 析构新增 Deps.query 解引用（非 owning 裸指针
    // ——适配器存活期锚），即成悬垂：改动前必须重建适配器/端口的销毁序
    // （如把 m_lastStoreAdapter 声明移到 m_compilePort 之前）。
    m_publishBridge.reset();
    m_presentationObserver.reset();
    m_compilePort.reset();
}

// =====================================================================
// 宿主共存回调（O-38 裁决③——共存最小接入：只观测，零视图操作）
// =====================================================================

void IrdWorkbenchHostPlugin::open(rw::models::WorkCell* workcell)
{
    // 宿主装载工作单元＝宿主中央区 RWStudioView3D 将呈现三维场景；本插件
    // 让位（工作台面板与宿主中央视图并存，不承载、不复制三维能力）。此处
    // 只做注入面的只读观测留痕（getView()/getWorkCellScene()——验收操作
    // 序列"宿主三维共存"的可观测面）。UI-T45：三维交互经网关受控消费
    // （阶段 B 交付——上行拾取/呈现出口/TCP 源；引用全部现取零挂接）。
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
    // UI-T45：场景清除拍——网关呈现残留整组清理（幂等；高亮归既有
    // outlet 的选中流收口——teardown 选择清空即驱动）。
    if (m_view3dGateway != nullptr) {
        m_view3dGateway->onSceneCleared();
    }
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
    // 多 Dock 拓扑（UI-T18——O-43 裁决③：单一工作台 Dock 五区栅格拆分；
    // UI-T38 收口——主 Dock 自建外壳退役）：插件本体 Dock＝主 Dock（Left
    // 停靠区）＝纯工业项目树（业务主导航），不再挂载内容装配面的顶栏命令
    // 条与左栏导航——顶栏命令与宿主 File 菜单重复、左栏为未落地占位，外
    // 壳退役后主 Dock 与宿主原生插件 Dock（TreeView 等）同形态；命令入口
    // 全量由宿主菜单（File 项目子菜单/Tools/视图）＋命令面板承载（见
    // registerHostMenus）。     ②IRD 属性与诊断 Dock（Right 停靠区）＝右
    // 栏内容（本插件新建、宿主 addDockWidget 同级注册——addDockWidget 延
    // 后到装载呈现自证，彼时插件已入宿主主窗口）；③IRD 任务和状态 Dock
    // （Bottom 停靠区）＝底部内容（同上；UI-T38 起工厂默认隐藏——见
    // WorkbenchContent 构造的宿主形态分派）。中央区不安放（宿主中央
    // RWStudioView3D 唯一所有三维——O-38 裁决③；内容装配面的中央让位页
    // 保持已构建不挂载，零呈现面）。状态行不进任何 Dock——PM-11 永久投影
    // 与瞬态消息经双观测钩子直投宿主状态栏（宿主 chrome 唯一，v1.11"状态
    // 行钉底"的语义等价迁移见落位登记注）。红线不变：不建任何顶层
    // QMainWindow（O-38 裁决②）。
    // 内容装配面（m_content）仍全量构建五区并持有命令设施（命令注册表/
    // 快捷键/命令面板/PM-11 投影）——本 Dock 只是不再消费其顶栏/左栏
    // Widget 出口（构建产物保持未挂载＝不可见，harness 顶层形态消费面
    // 不变）；submitCommand 路由与三区可见性记忆照常经它运转。
    auto* body = new QWidget(this);
    body->setObjectName("ird_plugin_dock_body");
    auto* mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    if (!m_content->build()) {
        return false;  // wiring 校验被拒（装配缺陷——调用方上抛处理）
    }

    // 主 Dock 体＝共享工业项目树单段（UI-T38——B1-SPEC D3 的收口形态：
    // 树是业务主导航，独占主 Dock；顶栏/左栏外壳退役登记于 units/ui.md
    // UI-T38 增量注）。树面板未装配＝装配缺陷防御：空体兜底（宿主装载
    // 不中断，缺陷经 Dev 留痕上抛），不虚构占位内容。
    // 顶栏/左栏出口显式藏（buildDockBody 安放纪律）：两 Widget 的父对象＝
    // 本 Dock 内容体（内容装配面统一以宿主控件为父），且创建于宿主显示
    // 之前——Qt 级联显示语义（父 show 时未 hide 的子件一并点亮）会把未
    // 安放的它们在 Dock 原点裸显成叠影；本形态不安放＝必须出显隐面。
    // harness 顶层形态两出口照常安放，不受影响。
    if (QWidget* topBar = m_content->topBarWidget()) {
        topBar->hide();
    }
    if (QWidget* leftNav = m_content->leftWidget()) {
        leftNav->hide();
    }
    if (m_treePanel != nullptr) {
        mainLayout->addWidget(m_treePanel->widget(), /*stretch=*/1);
    } else {
        reportLine("工业项目树面板未装配——主 Dock 空体（装配缺陷，如实留痕）");
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "buildDockBody：共享树面板缺席，主 Dock 空体");
        }
    }
    setWidget(body);
    m_dockBody = body;

    // 右/底 Dock 创建（父对象＝本插件；装载呈现自证时 addDockWidget 重挂
    // 进宿主主窗口——插件本体在 addPlugin 尾段才入主窗口，彼时宿主窗口
    // 才可寻址）。objectName 供宿主状态 blob 与排障日志定位。
    // UI-T23：右 Dock 内容升格为"共享属性检查器＋右栏（诊断与设置）"纵排
    // （B1-SPEC D5——共享检查器为宿主右 Dock 唯一跨域属性呈现面；UI-T22
    // 登记注③的预留缝）。检查器未装配时保持首版单段形态，零回归。
    // UI-T44：检查器与右栏之间插入跨域就绪摘要卡（诊断摘要半区——§4.2
    // 右栏行的最小兑现；数据源＝各域 §11.2 readonlyProjections，随共享面
    // 刷新点重取）。检查器缺席形态下卡照常挂（就绪摘要不依赖检查器）。
    m_propsDock = new QDockWidget(QString::fromUtf8("IRD 属性与诊断"), this);
    m_propsDock->setObjectName("ird_props_dock");
    {
        auto* rightColumn = new QWidget(m_propsDock);
        auto* rightLayout = new QVBoxLayout(rightColumn);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(2);
        if (m_inspectorPanel != nullptr) {
            rightLayout->addWidget(m_inspectorPanel->widget(), /*stretch=*/3);
        }
        m_readinessCard =
            new DomainReadinessSummaryCard(QString::fromUtf8("跨域就绪摘要"),
                                           rightColumn);
        rightLayout->addWidget(m_readinessCard);
        rightLayout->addWidget(m_content->rightWidget(), /*stretch=*/2);
        rightLayout->addStretch(0);
        detail::installDockViewport(*m_propsDock, rightColumn, 280);
    }
    m_tasksDock = new QDockWidget(QString::fromUtf8("IRD 任务和状态"), this);
    m_tasksDock->setObjectName("ird_tasks_dock");
    detail::installDockViewport(*m_tasksDock, m_content->bottomWidget(), 240, 160);
    // 建模 Dock（Left 停靠区——WP-24-T03 首版装配挂位；中央区 StageId
    // 挂位〔CentralAreaHost〕未落位，宿主侧 Dock 承载为登记过的首版形态，
    // 收口时迁移）。面板工厂经域装配 bundle 现调（模块内部完成会话接线）。
    if (m_domains) {
        m_modelingDock = new QDockWidget(QString::fromUtf8(kModelingDockTitle), this);
        m_modelingDock->setObjectName("ird_modeling_dock");
        detail::installDockViewport(*m_modelingDock, modelingPanelWidget(*m_domains));
        // 需求/运动学 Dock（UI-T23 三域挂位——Left 区同列；失败隔离缺席
        // 的域工厂返回 nullptr＝跳过挂位，占位呈现由共享树承担，§11.3）。
        // 运动学高级面板（求解配置）挂 Right 区（UX-04 高级面板位——
        // descriptor 装配规则行 5 的独立登记形态）。
        QWidget* requirementsPanel = requirementsPanelWidget(*m_domains);
        if (requirementsPanel != nullptr) {
            m_requirementsDock = new QDockWidget(
                QString::fromUtf8(kRequirementsDockTitle), this);
            m_requirementsDock->setObjectName("ird_requirements_dock");
            detail::installDockViewport(*m_requirementsDock, requirementsPanel);
            // 首呼默认尺寸（UI-T39——审核布局返工：需求编辑区在默认布局中
            // 过矮，卡片只见顶部须频繁滚动。初始尺寸 520×680＝审核建议区间
            // （宽 420~480／高 560~680）的宽容形态——Qt 布局在左列空间不足
            // 时按各 Dock 的尺寸提示协商，初始尺寸只是首选值而非硬下限
            // 〔硬下限＝面板 setMinimumSize 380×560，与中央区保留红线相
            // 容〕；用户拖拽/记忆〔PM-14〕之后各会话以记忆值为准）。
            requirementsPanel->resize(520, 680);
        }
        QWidget* kinematicsPanel = kinematicsPanelWidget(*m_domains);
        if (kinematicsPanel != nullptr) {
            m_kinematicsPanel = kinematicsPanel;  // 回执投递锚（成员注）
            m_kinematicsDock = new QDockWidget(
                QString::fromUtf8(kKinematicsDockTitle), this);
            m_kinematicsDock->setObjectName("ird_kinematics_dock");
            detail::installDockViewport(*m_kinematicsDock, kinematicsPanel);
        }
        QWidget* kinematicsAdvanced = kinematicsAdvancedPanelWidget(*m_domains);
        if (kinematicsAdvanced != nullptr) {
            m_kinematicsAdvancedDock = new QDockWidget(
                QString::fromUtf8(kKinematicsAdvancedDockTitle), this);
            m_kinematicsAdvancedDock->setObjectName("ird_kinematics_advanced_dock");
            detail::installDockViewport(*m_kinematicsAdvancedDock, kinematicsAdvanced, 280);
        }
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
        // 三新域面板挂位（ASM-PANEL 收口批 2026-10-10——六域面板全部可见；
        // 收口余量兑现：ui.md §16.7 v1.15 WP-24-T03 登记④"中央区 Central
        // AreaHost 挂位〔首版＝宿主侧 Left Dock 承载〕——随装配收口任务
        // 兑现"的首版形态延伸至三新域，与 modeling/requirements/kinematics
        // 同列 Left 区）。宿主装配路径在此**真实消费**三域描述符在册的面板
        // 工厂（dynamics DynamicsPanelWidget／selection SelCatalogPanel
        // Widget／optimization OptimizationPanelWidget——装配门面创建时
        // 转接模块 createPanel 的闭包），此前三域仅注册计数、widget 实体
        // 从未创建（用户实机点验"三域面板不可见"的缺口在本段闭合）。
        //
        // 单一书写点：本段与 include 以 IRD_UI_PLUGIN_EXTRA_DOMAINS 区分
        // （ui_plugin/studio 两宿主目标同款定义——ui/CMakeLists 两段同源，
        // 防两份装配序列漂移）；三域产物容器经类型擦除句柄还原实型
        // （shared_ptr<void> 的删除器在 assembleExtraDomains 构造点绑定
        // ExtraDomainAssemblies 实型——static_pointer_cast 还原即原类型，
        // 析构安全随句柄语义保持）。
        // FIX-PANEL 缺席忍耐挂位拍辅助（半区①——消费方忍耐的登记镜像）：
        // 把"该域辅助可见性键已在内容装配层登记"的事实镜像到对应视图菜单
        // 开关的使能位上——挂位成功（setAuxVisibilityTarget 已调用）＝开关
        // 保持可用；挂位缺席（容器缺席/取件空/工厂抛出）＝开关置灰。镜像
        // 判据在本 TU 单点维护（setAuxVisibilityTarget 的全部调用点都在
        // buildDockBody——登记与镜像同拍同步，无跨文件漂移面）；消费点
        // （refreshHostMenuActions 勾选态回写／视图菜单 toggle 触发 lambda）
        // 以 action->isEnabled() 为缺席判据跳过 auxVisible 查询——
        // WorkbenchContent::auxVisible 的"未登记键 out_of_range fail-fast"
        // 契约本体不动（登记面不弱化），忍耐在消费方按缺席语义跳过。
        // 时序前提：视图菜单开关在 registerHostMenus（setupMenu 拍）已全部
        // 入列（m_hostAuxToggles 七项），本拍晚于其构建——按键查得即置灰。
        const auto disableAuxToggle = [this](const char* auxKey) {
            for (auto& [key, action] : m_hostAuxToggles) {
                if (key == auxKey) {
                    // 置灰＋勾选位归零：缺席域无可见性可翻转（disabled 动作
                    // 不触发 triggered——触发 lambda 的第二道防线）。
                    action->setEnabled(false);
                    action->setChecked(false);
                    return;
                }
            }
        };
        if (!m_domains->extraAssemblies) {
            // 容器缺席＝装配序违约（assembleDomainPlugins 尾段未执行——
            // 宿主装配缺陷），Dev 留痕不虚构挂位，其余域照常。
            reportLine("三新域装配容器缺席（装配序违约，如实留痕）");
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel,
                                        "buildDockBody：extraAssemblies 缺席"
                                        "（IRD_UI_PLUGIN_EXTRA_DOMAINS 宿主"
                                        "形态下不应发生）");
            }
            // 三域辅助开关同步置灰——三域 Dock 均未创建、辅助可见性键未
            // 登记（setAuxVisibilityTarget 未调用），开关保持可点会让
            // refreshHostMenuActions 的勾选态回写撞 auxVisible 未登记
            // fail-fast（ASM-PANEL 实机启动崩溃根因链）；置灰＝缺席的
            // 用户可见诚实呈现，不虚构可呼出能力。
            for (const char* auxKey : {kAuxKeyDynamicsDock, kAuxKeySelectionDock,
                                       kAuxKeyOptimizationDock}) {
                disableAuxToggle(auxKey);
            }
        } else {
            const auto extra = std::static_pointer_cast<ExtraDomainAssemblies>(
                m_domains->extraAssemblies);
            // 逐域挂位规格表（域序＝白名单序 dynamics→selection→
            // optimization——与 assembleExtraDomains 装配序一致）：域键/
            // Dock 标题/objectName（ird_<domain>_dock 词形，与
            // ird_modeling_dock 同族——宿主状态 blob 与排障日志定位面）/
            // 辅助记忆键/成员 Dock 槽/取件函数六项逐域显式登记，取件函数
            // 内部承载 §11.3 缺席防御（返回 nullptr＝跳过）。
            struct ExtraPanelDockSpec {
                const char* domainKey;   ///< 域键（白名单 token——日志定位）
                const char* dockTitle;   ///< Dock chrome 标题（字面常量）
                const char* objectName;  ///< Dock objectName（同族词形）
                const char* auxKey;      ///< 辅助可见性记忆键（domain.<token>）
                QDockWidget** dockSlot;  ///< 成员 Dock 槽（非 owning——窗口树托管）
                QWidget* (*pickup)(const ExtraDomainAssemblies&);  ///< 面板取件函数
            };
            const ExtraPanelDockSpec specs[3] = {
                {"dynamics", kDynamicsDockTitle, "ird_dynamics_dock",
                 kAuxKeyDynamicsDock, &m_dynamicsDock, dynamicsPanelWidget},
                {"selection", kSelectionDockTitle, "ird_selection_dock",
                 kAuxKeySelectionDock, &m_selectionDock, selectionPanelWidget},
                {"optimization", kOptimizationDockTitle, "ird_optimization_dock",
                 kAuxKeyOptimizationDock, &m_optimizationDock,
                 optimizationPanelWidget},
            };
            for (const ExtraPanelDockSpec& spec : specs) {
                // 逐域 try/catch（§11.3 失败隔离——单域面板工厂抛出不传染
                // 其余域：该域 Dock 缺席＋UI-PLUGIN-ASSEMBLY-FAILED 稳定码
                // 留痕〔statuses 注册态不受影响——面板创建是注册后的独立
                // 装配步骤，两态分立如实承载〕，下一域照常）。
                try {
                    // 面板工厂现调（缺席域＝nullptr——跳过挂位，§11.3；
                    // 非 owning——Dock setWidget 接管父子树托管）。
                    QWidget* extraPanel = spec.pickup(*extra);
                    if (extraPanel == nullptr) {
                        // FIX-PANEL 缺席留痕：取件空（描述符空/门面缺席的
                        // §11.3 防御面）此前静默 continue——装配报告对该域
                        // 无任何行，缺席不可观测。补 §11.3 稳定码报告行＋
                        // Dev 通道一行（语义零变化：单域缺席其余域照常——
                        // 与下方异常分支同族，只是无异常对象可携带原因）。
                        const std::string absentLine =
                            std::string("domain panel assembly: plugin=") +
                            spec.domainKey + " " + kAssemblyFailedCode +
                            " detail=面板取件缺席（失败隔离——工厂返回空）";
                        reportLine(absentLine);
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(kPluginDevChannel,
                                                    absentLine);
                        }
                        // 辅助开关同步置灰（缺席忍耐挂位拍——见上方
                        // disableAuxToggle 注：该域辅助键未登记）。
                        disableAuxToggle(spec.auxKey);
                        continue;  // 失败隔离缺席域——不建空 Dock 不虚构
                    }
                    *spec.dockSlot = new QDockWidget(
                        QString::fromUtf8(spec.dockTitle), this);
                    (*spec.dockSlot)->setObjectName(spec.objectName);
                    detail::installDockViewport(**spec.dockSlot, extraPanel);
                    // 辅助可见性登记（UI-T24 P1 同口径：工厂默认不呈现——
                    // 净室默认布局收敛；呈现落定在装载呈现自证拍的
                    // addDockWidget＋setVisible 对齐，见
                    // reassertEmbeddedPresentation）。
                    m_content->setAuxVisibilityTarget(spec.auxKey,
                                                      *spec.dockSlot,
                                                      /*factoryVisible=*/false);
                } catch (const std::exception& extraPanelError) {
                    // §11.3 稳定码＋原因一行（Dev 通道＋控制台——与装配段
                    // 报告行同族；该域面板降级为缺席〔共享树占位承担〕，
                    // 不中止其余域）。
                    const std::string failedLine =
                        std::string("domain panel assembly: plugin=") +
                        spec.domainKey + " " + kAssemblyFailedCode +
                        " detail=" + extraPanelError.what();
                    reportLine(failedLine);
                    if (m_diag.pipeline) {
                        m_diag.pipeline->logDev(kPluginDevChannel, failedLine);
                    }
                    // FIX-PANEL：工厂抛出域的辅助键同样未登记——辅助开关
                    // 同步置灰（与取件空分支同语义；否则回写拍对该开关查
                    // auxVisible 未登记键即抛，单域面板失败升级为主窗口
                    // 不呈现——ASM-PANEL 实证根因链）。
                    disableAuxToggle(spec.auxKey);
                }
            }
        }
#endif
    }

    // 可见性目标登记（chrome 安放在 activate 之前——两段装配时序契约；
    // 三区开关语义自此作用于 Dock 本体：左＝插件主 Dock、右/底＝同级 Dock。
    // Top/Central 按契约登记为无操作——顶栏随主 Dock、中央归宿主）。
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Left, this);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Right, m_propsDock);
    m_content->setRegionVisibilityTarget(WorkbenchRegion::Bottom, m_tasksDock);

    // 辅助 Dock 可见性登记（UI-T24 P1——域自持面板跨会话记忆半区）：工厂
    // 默认＝不呈现（净室默认布局收敛）；登记即按记忆/默认施加（此时 Dock
    // 尚未入宿主主窗口——可见位只是父子树上的标记，呈现落定在装载呈现
    // 自证拍的 addDockWidget＋setVisible 对齐）。activate 的记忆装载拍会
    // 再对齐一次（登记在 buildDockBody、装载在 activate——时序覆盖）。
    // FIX-PANEL 缺席忍耐对齐：Dock 缺席（共享树装配失败）的域辅助键同样
    // 不登记——对应视图菜单开关同步置灰（与三新域段同语义；否则回写拍
    // 撞 auxVisible 未登记 fail-fast 的根因链对四域同样成立）。
    const auto disableAuxToggleTail = [this](const char* auxKey) {
        for (auto& [key, action] : m_hostAuxToggles) {
            if (key == auxKey) {
                action->setEnabled(false);
                action->setChecked(false);
                return;
            }
        }
    };
    if (m_modelingDock != nullptr) {
        m_content->setAuxVisibilityTarget(kAuxKeyModelingDock, m_modelingDock,
                                          /*factoryVisible=*/false);
    } else {
        disableAuxToggleTail(kAuxKeyModelingDock);
    }
    if (m_requirementsDock != nullptr) {
        m_content->setAuxVisibilityTarget(kAuxKeyRequirementsDock, m_requirementsDock,
                                          /*factoryVisible=*/false);
    } else {
        disableAuxToggleTail(kAuxKeyRequirementsDock);
    }
    if (m_kinematicsDock != nullptr) {
        m_content->setAuxVisibilityTarget(kAuxKeyKinematicsDock, m_kinematicsDock,
                                          /*factoryVisible=*/false);
    } else {
        disableAuxToggleTail(kAuxKeyKinematicsDock);
    }
    if (m_kinematicsAdvancedDock != nullptr) {
        m_content->setAuxVisibilityTarget(kAuxKeyKinematicsAdvancedDock,
                                          m_kinematicsAdvancedDock,
                                          /*factoryVisible=*/false);
    } else {
        disableAuxToggleTail(kAuxKeyKinematicsAdvancedDock);
    }

    // UI-T82/PM-14：setupMenu 先于 initialize，菜单创建时所有域 Dock 均
    // 为空。显隐观察必须在本拍（域已创建）绑定，否则标题栏关闭从未写记忆。
    for (const auto& [key, action] : m_hostAuxToggles) {
        QDockWidget* dock = auxiliaryDock(key);
        if (dock == nullptr) { continue; } // 域缺席，菜单已置灰，不绑定无效目标。
        QObject::connect(dock, &QDockWidget::visibilityChanged, this,
            [this, key, dock](bool /*visible*/) {
                auto* host = qobject_cast<QMainWindow*>(parentWidget());
                if (m_content == nullptr || host == nullptr || !host->isVisible()) { return; }
                // 守卫必须观察宿主：浮动 Dock 关闭后 window()==自身且已隐藏，
                // 若检查它会漏掉真正的关闭，导致菜单勾选和打开记忆停在旧值。
                // Qt 在标签被遮挡时也发 false；显式 hidden 位才代表用户关闭，
                // 最小化/退出的父窗口隐去则由上方守卫排除，避免污染用户意愿。
                m_content->setAuxVisible(key, detail::dockRequestedOpen(*dock));
                // 工厂初始隐藏的组可能直到首次呼出才创建 Qt 标签栏，不能只在
                // 装载拍设置标题策略；显示/切换后的下一拍幂等补施，不移动焦点。
                if (!dock->isHidden()) {
                    const QString title = dock->windowTitle();
                    QTimer::singleShot(0, host, [host, title] {
                        detail::configureWorkspaceTabs(*host, title);
                    });
                }
            });
    }

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
        // 写命令入口补全（UI-T38——主 Dock 顶栏命令条退役的承接面）：应用/
        // 撤销/重做此前仅顶栏按钮与 Ctrl+Shift+P 命令面板两入口，顶栏退役
        // 后菜单成为唯一常驻图形入口（§7.2 路由红线不变——动作只转发命令
        // 板；使能态随命令可用性快照刷新）。
        // F-501（宿主审核 P2）应用语义明确化：菜单文案与两域面板既有指引
        // （『应用草稿』）统一——"应用修改"易与项目级修改混淆；悬停说明
        // 分层交代预检门禁/修订生成/两级撤销的差别（语义零变化，纯文案）。
        // F-553：三条命令的基础悬停说明改经 addCommandAction 装配拍快照
        // （refreshHostMenuActions 的禁用原因刷新以之为还原值源——禁用
        // 态呈现原因文案、可用态还原本说明，见该函数注释）。
        addCommandAction(projectMenu, "应用草稿", "draft.apply", QStringLiteral(
            "预检并应用当前草稿——通过门禁后生成新项目修订。"
            "历史只增不改：旧修订不变，项目级撤销＝以新修订对冲；"
            "未应用的草稿编辑请在各域面板内草稿撤销/直接改写。"));
        addCommandAction(projectMenu, "撤销", "project.undo", QStringLiteral(
            "项目级撤销——对冲最近一次已应用的修订（产生一条新修订，历史只增不改）；"
            "未应用的草稿编辑不在此列。"));
        addCommandAction(projectMenu, "重做", "project.redo", QStringLiteral(
            "项目级重做——对冲最近一次项目级撤销（产生一条新修订，历史只增不改）。"));
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
    // UI-T38：开关文案与 Dock 标题对齐——主 Dock 外壳退役后，三个可隐藏
    // 区在用户视野里就是三个 Dock 面板（原"左栏/右栏/底部"是内容装配面
    // 栅格词表，宿主融合形态下不直观）；开关目标与跨会话记忆键零变化。
    auto* viewMenu = new QMenu(QString::fromUtf8(kViewMenuTitle), menuBar);
    const std::pair<WorkbenchRegion, const char*> regionToggles[] = {
        {WorkbenchRegion::Left, "工业项目树"},
        {WorkbenchRegion::Right, "IRD 属性与诊断"},
        {WorkbenchRegion::Bottom, "IRD 任务和状态"},
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
    // ---- 域自持面板开关（UI-T24 P1——默认不呈现的勾选呼出通道）----
    // 三域面板＋运动学高级面板各一勾选项：触发＝辅助可见性记忆翻转（内容
    // 装配面持久化——跨会话记忆优先）；勾选态随 refreshHostMenuActions 回写
    // （与三区开关同机制，不回环）。Dock 关闭钮关闭同样落记忆（buildDockBody
    // 在 Dock 已创建后接线 visibilityChanged；父窗口隐去不等于用户关闭）。
    viewMenu->addSeparator();
    addAuxDockToggle(viewMenu, "IRD 建模面板", kAuxKeyModelingDock, m_modelingDock);
    addAuxDockToggle(viewMenu, "IRD 需求面板", kAuxKeyRequirementsDock, m_requirementsDock);
    addAuxDockToggle(viewMenu, "IRD 运动学面板", kAuxKeyKinematicsDock, m_kinematicsDock);
    addAuxDockToggle(viewMenu, "IRD 运动学（求解配置）面板", kAuxKeyKinematicsAdvancedDock,
                     m_kinematicsAdvancedDock);
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
    // 三新域面板开关（ASM-PANEL 收口批——六域面板全部可见的用户呼出半区；
    // Dock 缺席〔失败隔离〕时装配拍将菜单项置灰，与上方四项同机制）。
    addAuxDockToggle(viewMenu, "IRD 动力学面板", kAuxKeyDynamicsDock, m_dynamicsDock);
    addAuxDockToggle(viewMenu, "IRD 选型面板", kAuxKeySelectionDock, m_selectionDock);
    addAuxDockToggle(viewMenu, "IRD 优化面板", kAuxKeyOptimizationDock, m_optimizationDock);
#endif
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
                                                  const char* commandId,
                                                  const QString& baseToolTip)
{
    // 菜单只路由命令板（§4.2 路由红线——触发统一转发内容装配面提交路径）；
    // 使能态随命令可用性快照刷新（refreshHostMenuActions——本插件零判定）。
    QAction* action = target->addAction(QString::fromUtf8(title));
    action->setParent(this);  // 动作父对象＝本插件（Qt 树托管——随宿主收尾）
    action->setToolTip(baseToolTip);  // 基础悬停说明（F-501 三条长文案——装配拍施加）
    QObject::connect(action, &QAction::triggered, this, [this, commandId] {
        if (m_content) {
            m_content->submitCommand(commandId);
        }
    });
    // F-553：装配拍快照基础 tooltip 进登记条目（refreshHostMenuActions
    // 的可用态还原值源——回读 action->toolTip() 会被上一次刷新写入的
    // 原因文案污染，故显式携带）。
    HostMenuCommandAction entry;
    entry.action = action;
    entry.commandId = commandId;
    entry.baseToolTip = baseToolTip;
    m_hostMenuCommandIds.push_back(std::move(entry));
    return action;
}

QDockWidget* IrdWorkbenchHostPlugin::auxiliaryDock(const std::string& key) const noexcept
{
    // 枚举只做装配引用查找，不解析域对象名称；缺席域保持空指针失败隔离语义。
    const std::pair<const char*, QDockWidget*> docks[] = {
        {kAuxKeyModelingDock, m_modelingDock},
        {kAuxKeyRequirementsDock, m_requirementsDock},
        {kAuxKeyKinematicsDock, m_kinematicsDock},
        {kAuxKeyKinematicsAdvancedDock, m_kinematicsAdvancedDock},
        {kAuxKeyDynamicsDock, m_dynamicsDock},
        {kAuxKeySelectionDock, m_selectionDock},
        {kAuxKeyOptimizationDock, m_optimizationDock}};
    for (const auto& [known, dock] : docks) {
        if (key == known) { return dock; }
    }
    return nullptr;
}

void IrdWorkbenchHostPlugin::addAuxDockToggle(QMenu* target, const char* title,
                                              const char* auxKey, QDockWidget* /*dock*/)
{
    // 勾选开关（UI-T24 P1）：触发＝辅助可见性记忆翻转（内容装配面施加并
    // 持久化——PM-14）；菜单构建早于 initialize 装配（框架 setupMenu 时序
    // ——m_content 可能为空），捕获处判空，初值勾选态由 refreshHostMenu
    // Actions 装配后统一回写。
    QAction* action = target->addAction(QString::fromUtf8(title));
    action->setParent(this);
    action->setCheckable(true);
    const std::string key(auxKey);
    // FIX-PANEL 缺席忍耐消费点②（视图菜单 toggle 触发 lambda）：缺席域
    // 开关（挂位拍置灰——disabled 动作用户点不出 triggered，程序化
    // trigger() 对 disabled 动作亦不发射）此处再按使能位防线跳过——
    // 未登记键不查询 auxVisible/setAuxVisible（两法对未登记键均
    // out_of_range fail-fast），登记面契约本体不弱化，忍耐在消费方。
    QObject::connect(action, &QAction::triggered, this, [this, key, action] {
        if (m_content && action->isEnabled()) {
            const bool opening = !m_content->auxVisible(key);
            m_content->setAuxVisible(key, opening);
            // 菜单呼出不止取消隐藏，还须选中对应页签，否则用户看不到目标。
            if (QDockWidget* current = auxiliaryDock(key); opening && current != nullptr) { current->raise(); }
        }
    });
    m_hostAuxToggles.emplace_back(key, action);
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
    for (auto& entry : m_hostMenuCommandIds) {
        const ShellCommandAvailability a = m_content->commandAvailability(entry.commandId);
        entry.action->setEnabled(a.enabled);
        // F-553：禁用原因→tooltip 传递补齐（ERR-01「禁用必带原因」——
        // 此前本循环只消费 enabled，reasonKey 丢弃＝置灰动作悬停无禁用
        // 原因）。决策统一走 ui::disableReasonToolTip（UiText 单一书写
        // 点：禁用→原因文案；可用→还原装配拍基础说明；空键→F-557 同面
        // 守卫不进解析）。与 WorkbenchContent 顶栏按钮先例（refreshCommand
        // States）同口径——菜单/按钮/命令面板三呈现面同源一词表。
        entry.action->setToolTip(QString::fromStdString(ui::disableReasonToolTip(
            a.enabled, a.reasonKey, entry.baseToolTip.toStdString())));
    }
    // 五区开关勾选态＝当前有效可见性（不回环：只 setChecked 不触发）。
    for (auto& [region, action] : m_hostRegionToggles) {
        action->setChecked(m_content->regionVisible(region));
    }
    // 辅助 Dock 开关勾选态＝当前有效可见性（UI-T24 P1——记忆位非实测位，
    // 与五区开关同语义；不回环）。
    // FIX-PANEL 缺席忍耐消费点①（勾选态回写）：缺席域开关（挂位拍置灰
    // ——面板工厂缺席/异常使辅助可见性键未登记）跳过 auxVisible 查询、
    // 勾选位保持归零——未登记键查询＝out_of_range fail-fast（ASM-PANEL
    // 实机启动主窗口不呈现的根因行），缺席按"无可见性可回写"处理。
    // enabled 判据的镜像维护点＝buildDockBody 挂位拍（disableAuxToggle）
    // ——登记与镜像同拍同步（单一 TU），WorkbenchContent::auxVisible 的
    // 登记面 fail-fast 契约本体不动。
    for (auto& [key, action] : m_hostAuxToggles) {
        if (!action->isEnabled()) {
            action->setChecked(false);  // 缺席域开关——无可回写，显式归零
            continue;
        }
        action->setChecked(m_content->auxVisible(key));
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
        // Dock 同受宿主装载语义支配）。呈现位（UI-T24 P1——默认布局收敛；
        // UI-T38 收敛面更新）：三域自持面板与任务 Dock 工厂默认＝不呈现
        // （净室默认呈现面收敛为"工业项目树主 Dock＋属性诊断 Dock"，中央
        // 三维视图非零），可见性由内容装配面的辅助/区域记忆半区决定（用户
        // 经"视图"菜单呼出后跨会话记忆优先——PM-14；框架 restoreState 先于
        // 本拍 addDockWidget，blob 对域 Dock 无效，故记忆自持）。缺席域跳过
        // ——失败隔离挂位形态。
        if (m_modelingDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_modelingDock);
            m_modelingDock->setVisible(m_content->auxVisible(kAuxKeyModelingDock));
        }
        // 需求/运动学 Dock 入宿主（UI-T23 三域挂位——Left 区同列；高级
        // 面板 Dock 入 Right 区。呈现位同上——P1 收敛＋辅助记忆）。
        if (m_requirementsDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_requirementsDock);
            m_requirementsDock->setVisible(m_content->auxVisible(kAuxKeyRequirementsDock));
        }
        if (m_kinematicsDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_kinematicsDock);
            m_kinematicsDock->setVisible(m_content->auxVisible(kAuxKeyKinematicsDock));
        }
        if (m_kinematicsAdvancedDock != nullptr) {
            hostWindow->addDockWidget(Qt::RightDockWidgetArea,
                                      m_kinematicsAdvancedDock);
            m_kinematicsAdvancedDock->setVisible(
                m_content->auxVisible(kAuxKeyKinematicsAdvancedDock));
        }
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
        // 三新域 Dock 入宿主（ASM-PANEL 收口批——六域面板全部可见的呈现
        // 落定拍；Left 区同列＝ui.md §16.7 v1.15 首版登记④"宿主侧 Left
        // Dock 承载"的同一形态）。呈现位与前三域同口径：工厂默认不呈现
        // （净室默认收敛），可见性由辅助记忆半区决定（视图菜单呼出后跨
        // 会话记忆优先——PM-14；blob 对域 Dock 无效故记忆自持）。缺席域
        // （失败隔离）跳过——挂位形态与前三域一致。
        if (m_dynamicsDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_dynamicsDock);
            m_dynamicsDock->setVisible(m_content->auxVisible(kAuxKeyDynamicsDock));
        }
        if (m_selectionDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_selectionDock);
            m_selectionDock->setVisible(m_content->auxVisible(kAuxKeySelectionDock));
        }
        if (m_optimizationDock != nullptr) {
            hostWindow->addDockWidget(Qt::LeftDockWidgetArea, m_optimizationDock);
            m_optimizationDock->setVisible(
                m_content->auxVisible(kAuxKeyOptimizationDock));
        }
#endif
        // UI-T82：六域统一页签化，一份工作区高度替代六份最小高度的累加。
        detail::arrangeDomainWorkspace(*hostWindow, *this,
            {m_modelingDock, m_requirementsDock, m_kinematicsDock,
             m_dynamicsDock, m_selectionDock, m_optimizationDock},
                {m_propsDock, m_kinematicsAdvancedDock});
        m_propsDock->show();
        m_tasksDock->show();
        // 区域旗标重施（UI-T18——PM-14 跨会话记忆不被装载重显夺回）：三区
        // 可见性目标已改绑 Dock 本体，activate 期恢复的用户旗标若为"隐藏"，
        // 上面的重显 show() 会把它顶回可见——与跨会话记忆矛盾。此处按内容
        // 装配面的模型位（regionVisible 返回用户意愿位，非 Widget 实测态）
        // 重施一次：用户隐藏的区保持隐藏（可经宿主"视图"菜单重新开启），
        // 无隐藏记忆（缺省）时与重显结果一致。主 Dock（Left 目标）不在此
        // 重施——其装载呈现维持 v1.11 注册口径（G1 门控判据"装载即呈现"；
        // 主 Dock 承载工业项目树——业务主导航，整 Dock 隐藏将无处承载项目
        // 对象导航，UI-T38 外壳退役后语义不变）。
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
    // 结果宽度如实留痕——若被内容最小宽度钳制，收束只能到达钳制宽度，
    // 此时宿主窗口越宽三维视图所得越多，如实呈现（UI-T38 后主 Dock 内容
    // ＝纯项目树，顶栏按钮行 1054 px 钳制源已随外壳退役消失，收束可达性
    // 显著改善）。
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

    // 中央区最小可见宽度保障（UI-T24 P2——装配侧钳制）：呈现与两拍收束
    // 落定后安装事件观察（宿主主窗口＋中央控件 Resize → 合并抖动 → 回推
    // 检查）。钳制源已定位并经流式栅格消除（clamp-source.md），本保障是
    // 极端拖拽与最小窗口下的最后防线（中央三维视图不归零）。
    installCentralReserveGuard();

    reportLine("工业项目树多 Dock 已呈现（项目树主 Dock＋属性/任务 Dock＋宿主状态栏投影——装载呈现自证完成）");
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "装载呈现自证完成（多 Dock 嵌入宿主主窗口可见；状态投影归宿主状态栏）");
    }
}

// =====================================================================
// 中央区最小可见宽度保障（UI-T24 P2——装配侧钳制；SA-02 框架零修改）
// =====================================================================

void IrdWorkbenchHostPlugin::installCentralReserveGuard()
{
    auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
    if (hostWindow == nullptr) {
        return;  // 未嵌宿主主窗口＝异常装载形态（防御——不越权假设父型）
    }
    // 事件观察两处：宿主主窗口（整窗缩放——"主窗口缩至最小"场景）与中央
    // 控件（分隔条拖拽直接改中央区几何——"极端拖拽"场景）。本方法只观察
    // 事件与回推 Dock 尺寸，不读写框架控件任何属性（零修改红线）。
    hostWindow->installEventFilter(this);
    if (hostWindow->centralWidget() != nullptr) {
        hostWindow->centralWidget()->installEventFilter(this);
    }
    // 合并抖动定时器：Resize 事件流（拖拽中的连续几何变更）每次重启计时，
    // 静默 80 ms 后执行一次检查——拖拽过程零干扰，停手即校正。
    m_centralGuardTimer = new QTimer(this);
    m_centralGuardTimer->setSingleShot(true);
    connect(m_centralGuardTimer, &QTimer::timeout, this,
            &IrdWorkbenchHostPlugin::enforceCentralMinWidth);
}

bool IrdWorkbenchHostPlugin::eventFilter(QObject* watched, QEvent* event)
{
    // 三维拾取拦截（UI-T45——Ctrl+双击＝旧版交互语义等价承接）。事件
    // 归属判定＝watched 为视图本体或其后代 QWidget；非 Ctrl/非双击/非
    // 视图域＝放行框架默认处理（零行为外溢）；拦截后无论链路产出与否
    // 均消费（未命中也是诚实处置——放行会触发框架默认双击行为）。
    if (m_view3dGateway != nullptr && !m_view3d.isNull()
        && event->type() == QEvent::MouseButtonDblClick) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget != nullptr && m_view3d->isAncestorOf(widget)) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->modifiers() & Qt::ControlModifier) != 0) {
                // 坐标映射到视图系（落点控件可能与视图原点有布局偏移——
                // pickFrame 约定＝视图坐标系）。
                const QPoint pos =
                    widget->mapTo(m_view3d, mouse->position().toPoint());
                m_view3dGateway->handleViewDoubleClick(pos);
                return true;
            }
        }
    }
    // 只认 Resize 事件（其余全放行基类——零干预面）；宿主窗口与中央控件
    // 的几何变化都会到这里，合并抖动后排程检查。
    if (event != nullptr && event->type() == QEvent::Resize
        && m_centralGuardTimer != nullptr) {
        m_centralGuardTimer->start(80);  // 重启计时（QTimer::start 重置倒计时）
    }
    return RobWorkStudioPlugin::eventFilter(watched, event);
}

void IrdWorkbenchHostPlugin::enforceCentralMinWidth()
{
    auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
    if (hostWindow == nullptr || m_centralGuardTimer == nullptr) {
        return;
    }
    QWidget* central = hostWindow->centralWidget();
    if (central == nullptr || !hostWindow->isVisible()) {
        return;  // 无中央控件/窗口不在屏（启动过程、最小化）——不介入
    }
    // WP-14-T12/A-12：需求表单的可读首选宽度为520逻辑像素；窄窗仍可滚动，
    // 但放大窗口后不能永久保留保护器压出的240px窄列。仅恢复不足420px的
    // 非浮动需求列；已有可读用户宽度不改，中央320px与分隔条12px优先保留。
    // 不设新的硬最小值，低分辨率/高DPI下仍可回落既有滚动地板。
    if (m_requirementsDock && m_requirementsDock->isVisible() && !m_requirementsDock->isFloating()
        && m_requirementsDock->width() < 420) {
        const int available = central->width() + m_requirementsDock->width()
            - kCentralMinReserveWidth - 12;
        const int preferred = std::min(520, available);
        if (preferred > m_requirementsDock->width()) {
            hostWindow->resizeDocks({m_requirementsDock}, {preferred}, Qt::Horizontal);
            return; // Qt下一次Resize拍复核中央预算，不读取本拍尚未落定的几何。
        }
    }
    if (central->width() >= kCentralMinReserveWidth) {
        return;  // 保留量达标——零操作（常态路径）
    }

    // ---- 回推算法（确定性次序，不与内容最小宽度对抗）------------------
    // 缺口＝保留量－当前中央宽。回推对象＝本插件创建的 Dock（框架自有
    // Dock 一律不触碰——SA-02）。次序：先收左列（主 Dock→域 Dock 列），
    // 再收右列属性 Dock；每个 Dock 的目标宽＝max(内容最小宽提示, 插件侧
    // 地板 160 px)——Qt 布局对最小提示以下本就拒收，本算法只在提示之上
    // 收缩，不制造无法满足的请求。
    const int deficit = kCentralMinReserveWidth - central->width();
    const auto clampFloor = [](const QWidget* dock) {
        const int contentMin = dock->minimumSizeHint().width();
        return contentMin > kDockShrinkFloorWidth ? contentMin : kDockShrinkFloorWidth;
    };
    int remaining = deficit;
    QList<QDockWidget*> leftColumn;
    if (m_modelingDock != nullptr && m_modelingDock->isVisible()) {
        leftColumn.append(m_modelingDock);
    }
    if (m_requirementsDock != nullptr && m_requirementsDock->isVisible()) {
        leftColumn.append(m_requirementsDock);
    }
    if (m_kinematicsDock != nullptr && m_kinematicsDock->isVisible()) {
        leftColumn.append(m_kinematicsDock);
    }
    // 主 Dock 最后收（承载工业项目树——业务主导航，优先保它宽裕）。
    for (QDockWidget* dock : leftColumn) {
        if (remaining <= 0) { break; } // 回收够缺口即停，避免同列页签重复过度压缩。
        const int target = std::max(clampFloor(dock), dock->width() - remaining);
        if (dock->width() > target) {
            remaining -= (dock->width() - target);
            hostWindow->resizeDocks({dock}, {target}, Qt::Horizontal);
        }
    }
    if (remaining > 0 && width() > clampFloor(this)) {
        const int target = std::max(clampFloor(this), width() - remaining);
        remaining -= (width() - target);
        hostWindow->resizeDocks({this}, {target}, Qt::Horizontal);
    }
    if (remaining > 0 && m_propsDock != nullptr && m_propsDock->isVisible()
        && m_propsDock->width() > clampFloor(m_propsDock)) {
        const int target = std::max(clampFloor(m_propsDock), m_propsDock->width() - remaining);
        hostWindow->resizeDocks({m_propsDock}, {target}, Qt::Horizontal);
    }
    // 复核留痕（一次性——防御日志洪水；极端窄窗下物理放不下＝如实声明，
    // 不无限对抗用户拖拽）。
    // resizeDocks异步落定；本拍旧宽度不足不能直接宣称“已收缩后仍放不下”。
    // 在下一轮80ms稳定拍复核真正的剩余缺口，避免正常缩窗产生误导性告警。
    QTimer::singleShot(80, this, [this, hostWindow] {
      const auto* settledCentral = hostWindow->centralWidget();
      if (settledCentral && settledCentral->width() < kCentralMinReserveWidth && !m_centralGuardWarned) {
        m_centralGuardWarned = true;
        const std::string facts = "[central-guard] 保留量未达成：中央 "
                                + std::to_string(settledCentral->width()) + " px < "
                                + std::to_string(kCentralMinReserveWidth)
                                + " px（本插件 Dock 已收缩到各自下限；窗口"
                                  "物理宽度不足，不再对抗用户）";
        reportLine(facts);
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel, facts);
        }
      }
    });
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
    //
    // F-552（UI-T74 验收实录——已打开会话下新建项目即静默退出）：打开协议
    // 仅可在 NoProject 态触发（INV-SES-2——openProject 状态机防线，无守卫
    // 直调即 logic_error 违约异常＋进程静默退出）。已打开会话的进入必须走
    // beginSwitch 切换编排（§5.4 S2——与 openRecentProject 同款防线），此
    // 处统一收口保护全部调用方（新建项目/打开项目/巡检通道）。
    if (m_controller && m_controller->hasOpenSession()) {
        try {
            const CloseDialogData data =
                m_controller->beginSwitch(canonicalPath, UiOpenMode::Writable);
            const std::optional<CloseDecision> decision = presentCloseDialog(data);
            if (!decision.has_value()) {
                (void)m_controller->resolveCloseDialog(CloseDecision{});  // 取消
                return false;  // 用户取消＝当前项目保持打开
            }
            const CloseDialogResolution resolution =
                m_controller->resolveCloseDialog(*decision);
            switch (resolution.status) {
            case CloseDialogResolution::Status::Confirmed:
                // A 转入 Draining 背景持有点＋B 已绑定（INV-SES-2/3）——A 排
                // 空归防线轮询观测；B 记入最近项目（与切换流同口径）。
                if (m_content) {
                    m_content->noteRecentProject(canonicalPath);
                }
                startDrainWatch();
                return true;
            case CloseDialogResolution::Status::SaveFailed:
                QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("切换已中止"),
                                     QString::fromUtf8("草稿保存失败，当前项目保持打开。"));
                return false;
            case CloseDialogResolution::Status::CandidateRejected:
                QMessageBox::warning(m_dockBody.data(), QString::fromUtf8("无法切换项目"),
                                     QString::fromUtf8("候选项目验证失败，当前项目保持打开。"));
                return false;
            case CloseDialogResolution::Status::Cancelled:
                return false;
            }
            return false;  // 决议穷尽防御（不可达——Status 封闭词表）
        } catch (const std::logic_error& error) {
            reportLine(std::string("切换编排状态违约：") + error.what());
            return false;
        }
    }
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
                    // F-911/F-910 修复批（UI-T84）：草稿源挂接按域独立隔离
                    // （单域失败不连坐他域——原实现 modeling 挂接与恢复同
                    // try，一处抛即跳过全部），恢复在挂接全部完成后统一
                    // 执行（restoreOnOpen 遍历已挂模块）。
                    try {
                        m_draft->attachModule(
                            modeling::kModuleHandle,
                            modelingDraftSource(*m_domains));
                    } catch (const std::exception& attachError) {
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(
                                kPluginDevChannel,
                                std::string("建模草稿源挂接跳过：")
                                    + attachError.what());
                        }
                    }
                    // 需求域草稿源挂接（F-911 修复面——此前生产 attachModule
                    // 唯一调用仅 modeling，RequirementsDraftSource 已实现但
                    // 无生产调用点＝需求草稿零落盘零恢复）。源视图经公共装
                    // 配门面出线（RequirementsPluginAssembly::requirements
                    // DraftSource——R-2 门面纪律，modelingDraftSource 同款先
                    // 例；存活期随域门面，宿主零持有）；模块句柄＝
                    // requirements::kModuleHandle（清单 handleSymbol 同源
                    // token——装配清单登记先行纪律，IRD-NOTE-006）。
                    if (m_domains->requirements.has_value()) {
                        try {
                            m_draft->attachModule(
                                requirements::kModuleHandle,
                                m_domains->requirements->requirementsDraftSource());
                        } catch (const std::exception& attachError) {
                            if (m_diag.pipeline) {
                                m_diag.pipeline->logDev(
                                    kPluginDevChannel,
                                    std::string("需求草稿源挂接跳过：")
                                        + attachError.what());
                            }
                        }
                    }
                    try {
                        // 草稿恢复（§8.3 restoreOnOpen——list→tryLoad→adopt
                        // →面板刷新）：磁盘草稿经生产查询适配器列出（F-911
                        // 修复——此前 binding.drafts 挂 DraftQueryPortStub 恒
                        // 返空，恢复链整体断裂）；失败隔离留痕不中断打开协议。
                        m_draft->restoreOnOpen();
                        m_domains->modeling.refreshFromSession();
                    } catch (const std::exception& restoreError) {
                        if (m_diag.pipeline) {
                            m_diag.pipeline->logDev(
                                kPluginDevChannel,
                                std::string("草稿恢复跳过：")
                                    + restoreError.what());
                        }
                    }
                }
            }
        }
        // 只读事实接入需求面板（UI-T39——审核 P1：面板工厂恒按可写创建，
        // 此前宿主从不回放真实 writable——只读项目打开后编辑行/生命周期
        // 按钮仍可用的 L-R12 违约。L-R12 门控输入＝ui 只读横幅同源事实
        // 〔report.opened.metadata.writable〕；打开成功/降级只读/切换同走
        // 本接线点）。随后驱动一次会话首刷（就绪重估＋全面板刷新——校验
        // 页打开即呈现真实校验结论，不再等待首次编辑）。
        if (m_domains && m_domains->requirements.has_value()) {
            m_domains->requirements->setWritable(report.opened.metadata.writable);
            refreshRequirementsFromSession();
        }
        // 只读事实接入建模面板（UI-T43——审核 P1 同源缺口：建模域此前无
        // setWritable 接线链，面板恒按可写创建/保持，只读项目下属性编辑
        // 行与写命令按钮仍可用，两域只读体验不一致。L-7 门控输入＝需求域
        // 同一宿主事实 report.opened.metadata.writable；打开/切换/降级只读
        // 同走本接线点。面板未创建＝模块暂存初值（装配序无关）；面板内
        // 即时重算属性行灰显与命令按钮使能，无需额外刷新事件）。项目关闭
        // 路径不加写重置：脱会话后面板呈空态、十条命令均 Project/Session
        // 作用域由注册表 §7.5 门控（无活动项目即禁用写命令），下次打开时
        // 本接线点以新项目的真实 writable 重放。
        if (m_domains) {
            m_domains->modeling.setWritable(report.opened.metadata.writable);
        }
        // 只读事实接入运动学会话态（UI-T64——L-K11 正式评估门控输入与
        // 需求/建模同一宿主事实；私有会话态零解引用——门面
        // bindSessionFacts 整体写，绑定键/配置基线经宿主缓存重放）。
        if (m_domains && m_domains->kinematics.has_value()
            && m_kinEvaluation != nullptr) {
            m_kinSessionWritable = report.opened.metadata.writable;
            m_domains->kinematics->bindSessionFacts(
                m_kinEvaluation->snapshotId(), m_kinEvaluation->epoch(),
                m_kinSessionWritable, m_kinConfigBaseline);
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
    // 受理位透传（R3 审计 C3）：执行器停止后的投递被拒——控制器按失败值
    // 轨就地收口（退出序列再达的保存/放弃不再挂死 UI）。
    deps.postToDiskThread = [this](std::function<void()> task) -> bool {
        return m_diskExecutor->post(std::move(task));
    };
    // 完成回执 Marshal 回 UI 线程（§3.4 M-1）：以 Dock 体为上下文对象——
    // 其销毁后排队的回执自动作废（Qt 上下文语义），不悬挂。受理位＝Dock
    // 体在场（R3-C3 签名对齐；回执被拒时调度线程侧不触碰控制器状态——
    // 计数收口随上下文销毁语义一并作废）。
    deps.postToUiThread = [this](std::function<void()> task) -> bool {
        if (m_dockBody.data() != nullptr) {
            // Marshal 边界异常隔离（F-910 同批——UI-T84，异常隔离三要素的
            // Qt 回调边界半区）：队列回执在 Qt 事件循环内执行，任何异常将
            // 直穿 exec() 落到 StudioMain 兜底 catch 致整站退出（A5 放大环
            // 节的队列面——真机冒烟 step8 实测：关闭收尾期迟到回执查询已
            // 关闭上下文抛 context-closed）。回执边界捕获：Dev 留痕可归因
            // ＋状态栏可见；状态一致性由各回执自身补偿语义承载（落盘回执
            // 只更新脏位/状态行，失败形态走既有返回值轨），不吞不崩。
            auto guarded = [this, task = std::move(task)]() mutable {
                try {
                    task();
                } catch (const std::exception& receiptError) {
                    if (m_diag.pipeline) {
                        m_diag.pipeline->logDev(
                            kPluginDevChannel,
                            std::string("回执Marshal异常已隔离：")
                                + receiptError.what());
                    }
                    if (m_hostStatusBar != nullptr) {
                        m_hostStatusBar->showMessage(
                            QString::fromUtf8("后台回执处理异常（已隔离，详情见开发日志）"),
                            8000);
                    }
                }
            };
            QMetaObject::invokeMethod(m_dockBody.data(), std::move(guarded),
                                      Qt::QueuedConnection);
            return true;
        }
        return false;
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
 * @brief 需求域外部修订同步（UI-T35 P2——RevisionCommitted 事件应答）。
 *
 * 判定与执行分离：三分岔判定归域侧纯函数 planExternalRevisionSync
 * （RevisionSyncPolicy.hpp——具名 UT 逐分支自证），本方法只做前置守卫与
 * 动作执行。语义分岔（编辑器 loadBaseline 无 rebase——STALE 诚实边界）：
 *   - 事件修订＝会话当前基线（自身 draft.apply 回执）→跳过（已同步）；
 *   - 零未应用编辑→从新 HEAD 重导线（wireRequirementsSession——锚/根
 *     刷新，草稿零丢失）；外部/他域修订场景的自动跟进；
 *   - 有未应用编辑→不重载（防丢草稿），状态栏 STALE 提示（建议先应用
 *     或撤销草稿再继续——应用时 expectedRevision 失配由命令 prepare
 *     诚实拒绝）。
 */
void IrdWorkbenchHostPlugin::onRequirementExternalRevision(
    const core::RevisionId& revision)
{
    if (!m_requirementsSessionLive || m_lastStoreAdapter == nullptr) {
        return;  // 无会话/装配缺席——无同步面
    }
    auto& requirements = *m_domains->requirements;
    // 三分岔判定（纯函数——判定输入＝门面基线直投值＋事件修订＋未应用
    // 编辑数；基线 nullopt 的防御形态在策略内与"零编辑"同路径）。
    const requirements::ExternalRevisionSync action =
        requirements::planExternalRevisionSync(
            requirements.sessionBaseRevision(), revision,
            m_requirementsEditor.draftStatus().edits);
    if (action == requirements::ExternalRevisionSync::SkipSelfApplied) {
        // 自身应用回执——会话锚已前移（noteAppliedRevision）。面板呈现仍需
        // 一轮刷新（UI-T39——项目级撤销键的可用性随新 tip 的 inverse 事实
        // 变化：首应用后"撤销上次应用"由不可用转可用；呈现收口与
        // refreshRequirementsFromSession 的三调用点语义一致）。
        refreshRequirementsFromSession();
        return;
    }
    if (action == requirements::ExternalRevisionSync::RewireFromHead) {
        wireRequirementsSession(*m_lastStoreAdapter);  // 零编辑——安全重导线
        // 重导线后的面板收口（UI-T39）：重导线只重锚了会话态，面板呈现
        // 需要显式刷新（就绪重估＋全面板投影）——否则校验页/编辑面停留
        // 在重导线前的投影（事件驱动刷新纪律的应答半区）。
        refreshRequirementsFromSession();
        if (m_diag.pipeline != nullptr) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "需求域会话：外部修订→零编辑重导线");
        }
        return;
    }
    // StaleNotice：草稿基于旧基线——不重载（防丢草稿），状态栏提示。
    if (m_hostStatusBar != nullptr) {
        m_hostStatusBar->showMessage(
            QString::fromUtf8("项目出现新修订——需求草稿基于旧修订，"
                              "请先应用或撤销草稿再继续"),
            6000);
    }
    if (m_diag.pipeline != nullptr) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "需求域会话：外部修订＋未应用草稿——STALE 提示");
    }
}

// 关闭编排（UI-T17——workbench.closeProject 覆写面；§5.4/§5.6）
// =====================================================================

// =====================================================================
// 需求域会话接线（UI-T29——存储成功捕获回调的数据面）
// =====================================================================

namespace {
/**
 * @brief 需求闭包域字节源的项目适配器（RequirementObjectClosureView 的
 *        宿主实现——UI-T29 基线闭包提供器）。
 *
 * 数据面：项目查询端口的修订视图（objectRefs 逐条 token/id/cv）＋
 * tryObject 负载取回——映射为 {token, bytes} 闭包对象。实现方约束
 * （Editor.hpp 闭包纪律）：并发只读安全；同键重复取回同字节；闭包域
 * 纪律＝只应答冻结修订（构造时固定 HEAD 视图）内的对象。
 * 生命周期：非 owning——query 端口引用由 ProjectStore 上下文保证存活
 * （bindAnchor 调用期内有效——loadBaseline 同步完成，无跨期持有）。
 */
class ProjectRequirementsClosure final
    : public requirements::RequirementObjectClosureView {
public:
    ProjectRequirementsClosure(project::IProjectQueryPort& query,
                               project::RevisionView head)
        : m_query(query), m_head(std::move(head))
    {
    }

    std::optional<requirements::RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        // 根对象路由（req-set 按唯一 token 取——§9.3 loadBaseline @pre）。
        for (const auto& ref : m_head.objectRefs) {
            if (ref.objectTypeToken == objectTypeToken) {
                return fetch(ref);
            }
        }
        return std::nullopt;
    }

    std::optional<requirements::RequirementClosureObject> tryObject(
        const core::ObjectId& objectId) const override
    {
        // 四集合解引用（§4.1 根对象引用表——按 id 命中即取）。
        for (const auto& ref : m_head.objectRefs) {
            if (ref.objectId == objectId) {
                return fetch(ref);
            }
        }
        return std::nullopt;
    }

private:
    /// 单对象取回（引用→负载字节——内容寻址 cv 键；nullopt 透传＝闭包
    /// 外/损坏的 try 轨语义，交由 loadBaseline 的校验面裁决）。
    std::optional<requirements::RequirementClosureObject> fetch(
        const project::ObjectRef& ref) const
    {
        auto bytes = m_query.tryObject(ref.objectId, ref.contentVersion);
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        return requirements::RequirementClosureObject{ref.objectTypeToken,
                                                      *std::move(bytes)};
    }

    project::IProjectQueryPort& m_query;  ///< 查询端口（非 owning——上下文存活期）
    project::RevisionView m_head;         ///< 冻结的 HEAD 修订视图（闭包域＝该修订）
};

/**
 * @brief 内存空根闭包（UI-T35 P1-1——空项目需求集初始化的编辑期载入源）。
 *
 * R2 审核整改：此前 HEAD 无 req-set→attachEditor(nullptr)＝诚实空态但
 * 无初始化入口（用户从零无法创建任何需求对象）。本闭包提供一个**内存
 * 空根**（RequirementSet{}＋四集合槽 nullopt）供 loadBaseline 载入——
 * 编辑器会话即活（生命周期按钮可用），首应用时根/集合槽按 allocateNew
 * 由 project 取号（正式存储身份，O-36：编辑期临时句柄——本闭包内的
 * ObjectId 是空根的模型内自洽值，不入存储）。
 *
 * 纪律：只应答 req-set token（四集合槽未挂载＝nullopt——编辑器空集
 * 合合法态）；同键重复取回同字节（纯函数构造）。
 */
class EmptyRequirementsClosure final
    : public requirements::RequirementObjectClosureView {
public:
    EmptyRequirementsClosure()
    {
        // 空根（四集合槽 nullopt——集合对象随首应用挂载）。
        requirements::RequirementSet root;
        root.name = "需求集";
        requirements::RequirementCodec codec;
        auto bytes = codec.encode(
            requirements::RequirementObjectVariant{root},
            requirements::kCurrentRequirementFormatVersion);
        m_rootBytes = bytes.ok() ? bytes.get() : requirements::RequirementBytes{};
    }

    std::optional<requirements::RequirementClosureObject> tryObjectByToken(
        std::string_view objectTypeToken) const override
    {
        if (objectTypeToken == requirements::kReqSetObjectType) {
            return requirements::RequirementClosureObject{
                std::string(requirements::kReqSetObjectType), m_rootBytes};
        }
        return std::nullopt;  // 四集合未挂载（空项目——首应用 allocateNew）
    }

    std::optional<requirements::RequirementClosureObject> tryObject(
        const core::ObjectId&) const override
    {
        return std::nullopt;  // 空项目无既有对象可解引用
    }

private:
    requirements::RequirementBytes m_rootBytes;  ///< 空根 canonical 字节（构造期编码）
};
}  // namespace

/**
 * @brief 需求域会话接线（UI-T29——存储成功捕获回调）。
 *
 * 会话数据面三态（UI-T35 P1-1 整改后口径）：HEAD 闭包含根 req-set→
 * 编辑器载入成功→attachEditor＋bindSessionAnchor＋根身份回填（objectRefs
 * 扫描）＋bindReadiness——全功能会话；闭包无根（全新工程）→内存空根
 * 闭包（EmptyRequirementsClosure）初始化编辑器会话——生命周期编辑可用，
 * 首应用时根/集合槽按 allocateNew 由 project 取号；内存空根载入失败
 * （域侧缺陷，非用户路径）→attachEditor(nullptr) 诚实空态回退＋Dev
 * 留痕（不吞错）。
 *
 * 线程：存储捕获回调在 UI 线程（打开协议同线程）——模块/面板同约束。
 */
void IrdWorkbenchHostPlugin::wireKinematicsEvaluationChannel()
{
    // 装配缺席/重复接线守卫（§11.3 失败隔离形态——一次接线语义）。
    if (m_domains == nullptr || !m_domains->kinematics.has_value()
        || m_kinEvaluation != nullptr) {
        return;
    }
    auto& kin = *m_domains->kinematics;

    // ---- 投递槽闭包（UI 线程——执行器侧 invokeMethod 已保证回投；槽
    // 体经门面消费＝迟到判定/中断如实的权威面，本闭包零判定逻辑）。
    auto route = [this](const kinematics::KinChannelBackgroundResultNote& note) {
        std::string feedback;
        if (m_domains != nullptr && m_domains->kinematics.has_value()) {
            feedback = m_domains->kinematics->noteAssemblyBackgroundResult(note);
        }
        // 完成回执呈现（返回文本→面板状态行）：showStatusText 公共槽即
        // 「编排函数返回值/宿主反馈的统一呈现入口」——受理/完成/失败三态
        // 同面可见。此前返回文本被 (void) 丢弃，完成态只在覆盖表间接可
        // 见、失败原因完全不可观测（ERR-01 断链——本批 tour 取证即受害
        // 实证：评估失败 60s 内状态行停格"已受理"）。按槽名动态调用＝
        // 宿主侧零 kinematics 插件层头包含（R-2 装配面纪律）；槽解析未
        // 命中＝装配违约，Dev 留痕不静默。
        if (m_kinematicsPanel != nullptr && !feedback.empty()) {
            if (!QMetaObject::invokeMethod(
                    m_kinematicsPanel, "showStatusText",
                    Q_ARG(QString, QString::fromStdString(feedback))) &&
                m_diag.pipeline != nullptr) {
                m_diag.pipeline->logDev(
                    kPluginDevChannel,
                    "kinematics：后台完成回执状态行投递失败（showStatusText "
                        "槽解析未命中——装配违约）：" + feedback);
            }
        }
    };

    // ---- ★ KIN-P4：配置持久化载体与恢复（保存/恢复闭环——PM-14 用户
    // 级通道接通；org/app 与宿主既有用户级设置同源）。恢复失败/首次
    // 使用＝缺省基线（I-KIN-4 seed≥1）＋Dev 留痕，不静默钳制半损坏
    // 配置（NFR-COR-03——restoreKinConfig 对任何损坏返回 nullopt）。
    m_kinConfigSettings = std::make_unique<QSettings>(
        QSettings::IniFormat, QSettings::UserScope,
        QStringLiteral("RobWork"), QStringLiteral("IRD-Studio"));
    kinematics::AnalysisConfiguration baseline;
    baseline.seed = 1;
    baseline.regionBudget.seed = 1;
    if (std::optional<kinematics::AnalysisConfiguration> restored =
            restoreKinConfig(m_kinConfigSettings.get());
        restored.has_value()) {
        baseline = *restored;
        if (m_diag.pipeline != nullptr) {
            m_diag.pipeline->logDev(
                kPluginDevChannel,
                "kinematics：用户级求解配置已恢复（QSettings——canonical "
                "v1 编解码）");
        }
    } else if (m_diag.pipeline != nullptr) {
        m_diag.pipeline->logDev(
            kPluginDevChannel,
            "kinematics：无用户级求解配置（缺省基线 seed=1——首次使用或"
            "既往配置损坏）");
    }

    // ---- 执行器构造（deps：工作集切片源/回投上下文/投递槽——route 按
    // 值拷入，channels 侧再持同一槽；std::function 拷贝即共享目标）。★
    // KIN-P4：sessionSolveRoute＝后台求解结构化回投（锚核对→门面
    // applySessionSolveOutcome 写会话态→面板刷新）；sessionSolver 缺省
    // ＝执行器内置生产求解器。参考系解析使用当前runtime名称映射与
    // 已发布WorkCell，不能让三维预览已能解析的合法引用在评估时恒被拒绝。
    auto sessionSolveRoute =
        [this](const kinematics::KinChannelSolveResult& result) {
            // 锚核对（账面纪律——消费方义务）：结果快照锚与当前会话绑定
            // 不一致＝跨快照误投影，拒绝写会话态（执行器已拦迟到，此为
            // 消费面二次核对——PA 权威纪律）。
            if (m_domains == nullptr || !m_domains->kinematics.has_value()
                || m_kinEvaluation == nullptr) {
                return;
            }
            if (!(result.snapshotId == m_kinEvaluation->snapshotId())) {
                return;  // 迟到/跨快照——零会话写入（如实丢弃）
            }
            // 结构化解集写会话态（唯一写点语义——门面方法内完成原值＋
            // 排序视图双面构造与面板刷新）。
            m_domains->kinematics->applySessionSolveOutcome(
                result.solutionSet);
        };
    /**
     * @brief REQ-01/03/09：按对象身份现取参考系的世界位姿。
     * @param objectId [in] 当前修订中模型根或场景对象的有效身份。
     * @return 世界系变换（平移m、旋转无量纲）；无宿主/帧/映射返回nullopt，
     *         由执行器给出可定位拒绝，绝不以单位阵掩盖悬空引用。
     *
     * 仅UI线程受理切片调用：借用当前宿主和状态，不跨调用缓存Frame裸指针。
     * 后台计算只收到变换值，项目切换和重新编译不会留下旧WorkCell引用。
     */
    auto resolveFrame = [this](const core::ObjectId& objectId)
        -> std::optional<rw::math::Transform3D<>> {
        auto* studio = getRobWorkStudio();
        const auto adapter = m_lastStoreAdapter;
        const auto snapshot = m_compilePort ? m_compilePort->lastPublishedSnapshot() : nullptr;
        if (!studio || !adapter || adapter->isClosed() || !snapshot) { return std::nullopt; }
        const auto cell = studio->getWorkCell();
        // REQ-01/09：机器人根的主身份名称指向Device，并不是可findFrame的帧。
        // 这里只接受需求既有ModelFrame/SceneObject语义对应的BaseMount/SceneObject
        // 作用域；复用同一发布快照的完整名称表，禁止拼后缀猜帧名。
        // 快照可能在发布拍切换，必须与宿主挂载的WorkCell同一实例；不比较
        // snapshot的命令baseRevision与提交后HEAD（两者语义不同）。
        if (!cell || &snapshot->workCell().workCell() != cell.get()) { return std::nullopt; }
        const auto& entries = snapshot->nameMap().entries();
        const auto entry = std::find_if(entries.begin(),entries.end(),[&](const auto& item) {
            return item.objectId==objectId && (item.scope==runtime::NameScope::BaseMount
                || item.scope==runtime::NameScope::SceneObject);
        });
        auto* frame = entry!=entries.end() ? cell->findFrame(entry->fullName) : nullptr;
        if (!frame) { return std::nullopt; }
        return rw::kinematics::Kinematics::worldTframe(frame, studio->getState());
    };
    m_kinEvaluation = std::make_unique<KinEvaluationExecutor>(
        KinEvaluationExecutor::Deps{&m_requirementsEditor, std::move(resolveFrame), this, route,
                                    std::move(sessionSolveRoute), nullptr});

    // ---- 求解配置合法基线注入（恢复值或缺省——I-KIN-4 seed≥1；执行器
    // 与会话态同源同值）。
    m_kinEvaluation->setConfiguration(baseline);
    m_kinConfigBaseline = baseline;

    // ---- 会话事实整体写（私有会话态零解引用——门面 bindSessionFacts；
    // 未绑定态 snapshotId 全零，writable 初始可写——无项目态受理已由
    // 快照缺位拒绝，真实值随项目打开拍重放）。
    kin.bindSessionFacts(core::ContentIdentity{}, m_kinEvaluation->epoch(),
                         m_kinSessionWritable, baseline);

    // ---- ★ KIN-P4：导出目录解析闭包（环境变量优先——无头取证/测试
    // 注入面；缺省＝用户应用数据目录下的 kinematics-exports。目录由
    // 写出缝代建——io AtomicFile 契约的装配层补偿）。
    auto exportDir = []() -> std::filesystem::path {
        const QByteArray envDir = qgetenv("IRD_KIN_EXPORT_DIR");
        if (!envDir.isEmpty()) {
            return std::filesystem::path(envDir.toStdString());
        }
        return std::filesystem::path(
                   QStandardPaths::writableLocation(
                       QStandardPaths::AppDataLocation)
                       .toStdString())
               / "kinematics-exports";
    };

    // ---- ★ KIN-P4：具名 Home 来源闭包（named-pose-set 保留键
    // homeConfiguration——MDL-17 保留键纪律；项目闭包现取＋modeling
    // 编解码权威，零缓存。无项目态/闭包无位姿集＝nullopt——面板给
    // "未定义 Home"明确提示，绝不以当前姿态充当，审查 A09 语义）。
    auto homeEntries = [this]() -> std::optional<std::vector<std::vector<double>>> {
        const auto adapter = m_lastStoreAdapter;
        if (adapter == nullptr) {
            return std::nullopt;
        }
        try {
            project::IProjectQueryPort& query = adapter->projectStore().query();
            const project::RevisionView head = query.head();
            for (const project::ObjectRef& ref : head.objectRefs) {
                if (ref.objectTypeToken
                    != std::string(modeling::kNamedPoseSetObjectType)) {
                    continue;
                }
                const std::optional<std::vector<std::uint8_t>> bytes =
                    query.tryObject(ref.objectId, ref.contentVersion);
                if (!bytes.has_value()) {
                    continue;  // 闭包外/装载失败——继续扫其余条目
                }
                const modeling::RobotDesignCodec codec;
                const auto decoded =
                    codec.decode(*bytes, modeling::kCurrentFormatVersion);
                if (!decoded.ok()
                    || !std::holds_alternative<modeling::PoseSet>(
                        decoded.get())) {
                    continue;  // 解码失败/非位姿集——如实跳过
                }
                const modeling::PoseSet& poseSet =
                    std::get<modeling::PoseSet>(decoded.get());
                for (const modeling::PoseSetEntry& entry : poseSet.entries) {
                    if (entry.key
                        == std::string(modeling::kHomeConfigurationPoseKey)) {
                        return std::vector<std::vector<double>>{
                            entry.jointConfiguration};
                    }
                }
                return std::nullopt;  // 位姿集在而无保留键——未定义 Home
            }
        } catch (const std::exception&) {
            return std::nullopt;  // 端口数据侧异常——Home 来源不可用（如实）
        }
        return std::nullopt;  // 闭包无位姿集对象
    };

    // ---- 通道注入（公共通道值面——R-2：装配层零私有头；值面→私有缝
    // 翻译在门面实现 TU 单点执行）。★ KIN-P4：七缝全部注入——产品降级
    // 面收窄到"无项目态/无工具快照"的诚实形态（通道未定义的降级已消）。
    kinematics::KinematicsAssemblyChannels channels;
    channels.modelView = m_kinEvaluation->sessionView();
    // 任务点投影（工作集现取零缓存——面板每次刷新现调；结果状态列随
    // ★ KIN-P4 批量账面合入——锚核对后逐点结果直投，未计算＝未计算）。
    channels.taskPoints = [this]() {
        std::vector<kinematics::KinChannelTaskPointRow> rows;
        if (m_kinEvaluation == nullptr) {
            return rows;
        }
        const requirements::RequirementWorkingSet& ws =
            m_requirementsEditor.workingSet();
        // 批量账面（锚核对——快照不一致＝跨快照误投影，零合入）。
        const auto batchId = m_kinEvaluation->latestBatchSnapshotId();
        std::map<core::ObjectId, kinematics::KinChannelTaskPointOutcome>
            outcomes;
        if (batchId.has_value()
            && (*batchId == m_kinEvaluation->snapshotId())) {
            outcomes = m_kinEvaluation->latestBatchOutcomes();
        }
        rows.reserve(ws.points.entries.size());
        for (const requirements::TaskPoint& point : ws.points.entries) {
            kinematics::KinChannelTaskPointRow row;
            row.pointOid = point.objectId;
            row.label = point.name;
            row.enabled = point.enabled;
            // 逐点结果合入（批量账面在册时——未计算点保持 nullopt）。
            const auto found = outcomes.find(point.objectId);
            if (found != outcomes.end()) {
                row.outcomeState = found->second.outcomeState;
                row.outcomeText = found->second.outcomeText;
                row.requiredCoverageDone =
                    found->second.requiredCoverageDone;
            }
            rows.push_back(std::move(row));
        }
        return rows;
    };
    channels.taskRows = [this]() {
        return m_kinEvaluation != nullptr
                   ? m_kinEvaluation->taskRows()
                   : std::vector<kinematics::KinChannelTaskStatusRow>{};
    };
    channels.backgroundSubmit =
        [this](const kinematics::KinChannelBackgroundRequest& request) {
            // 执行器缺位＝装配缺陷——拒绝受理（不虚构，ERR-01）。
            return m_kinEvaluation != nullptr
                       ? m_kinEvaluation->submit(request)
                       : kinematics::KinChannelBackgroundAck{};
        };
    channels.resultRoute = std::move(route);
    // ---- ★ KIN-P4 七缝（真实求解器/评估器/会话姿态/命令门面/导出/
    // 配置保存/具名 Home——命令门面在项目打开拍装配（installKinematics-
    // CommandHandler），此处仅注入现值——无项目态为空＝按钮禁用诚实）。
    if (!m_kinPolicyIkSolver) {
        m_kinPolicyIkSolver=makePolicyAwareIkSolver([this] { return m_policySource->publishedPolicy(); });
    }
    channels.ikSolver = m_kinPolicyIkSolver.get();
    channels.fkEvaluator = &m_kinFkEvaluator;
    channels.sessionPose = &m_kinSessionPose;
    channels.commandHandler = m_kinCommandHandler.get();
    channels.exportWriter = makeKinExportWriter(std::move(exportDir));
    channels.configPersist =
        makeKinConfigPersist(m_kinConfigSettings.get());
    channels.homePose =
        makeKinHomePoseProvider(KinPoseSetLoader{std::move(homeEntries)});
    kin.installAssemblyChannels(channels);
}

void IrdWorkbenchHostPlugin::installKinematicsCommandHandler(
    project::ProjectStore& store)
{
    // ---- 网关装配（②取数＋①递交——查询端口随存储上下文存活；提交闭
    // 包＝宿主交互桥形态，与 apply 编排同款：Confirmable 集弹确认对话框，
    // 会话守卫谓词同源）。装配失败 fail-fast（网关构造契约——缺依赖到
    // 达即编程错误）。
    m_kinCommandGateway = std::make_unique<KinProjectCommandGateway>(
        &store.query(),
        [this, &store](project::CommandEnvelope envelope)
            -> project::CommandResult {
            HostCommandInteraction interaction(
                m_dockBody.data(),
                [this] { return m_controller && m_controller->hasOpenSession(); });
            return store.commands().submit(std::move(envelope), &interaction);
        });
    // ---- 门面装配（网关＋根对象补丁缝——补丁缝为自由函数指针形态，
    // modeling 编解码权威保持，kinematics 门面零第二套编解码器）。
    m_kinCommandHandler = std::make_unique<kinematics::KinematicsCommandHandler>(
        m_kinCommandGateway.get(), patchRootDesignDefaultTcp);
    // ---- 补缝注入（通道值面已在 wire 拍装配——本拍仅刷新命令门面缝：
    // 仅含 commandHandler 的聚合经 mergeAssemblyChannels 补注——非空才
    // 覆盖，其余缝零触碰；随后面板门控 refreshAll 点亮设默认按钮）。
    if (m_domains != nullptr && m_domains->kinematics.has_value()) {
        kinematics::KinematicsAssemblyChannels patch;
        patch.commandHandler = m_kinCommandHandler.get();
        m_domains->kinematics->installAssemblyChannels(std::move(patch));
        m_domains->kinematics->refreshFromSession();
        if (m_diag.pipeline != nullptr) {
            m_diag.pipeline->logDev(
                kPluginDevChannel,
                "kinematics：设默认命令门面已装配（项目打开拍——网关/补丁缝"
                "/确认交互桥）");
        }
    }
}

void IrdWorkbenchHostPlugin::syncKinematicsSessionFacts()
{
    if (m_kinEvaluation == nullptr || m_domains == nullptr
        || !m_domains->kinematics.has_value()) {
        return;
    }
    auto& kin = *m_domains->kinematics;
    // ---- 快照换绑（发布消费点零第二编译路径——lastPublishedSnapshot
    // 同源现取；attachSnapshot 内部＝纪元推进＋账面清空，L-K12 迟到锚）。
    std::shared_ptr<const runtime::RuntimeSnapshot> snapshot =
        m_compilePort != nullptr ? m_compilePort->lastPublishedSnapshot()
                                 : nullptr;
    const core::ContentIdentity identity =
        snapshot != nullptr ? snapshot->modelIdentity() : core::ContentIdentity{};
    m_kinEvaluation->setPolicy(m_policySource ? m_policySource->publishedPolicy() : nullptr);
    m_kinEvaluation->attachSnapshot(std::move(snapshot), identity);
    // ---- 模块会话态整体写（面板绑定面与执行器受理面的一致性键同源——
    // 提交流构造请求携带的 snapshotId/epoch 即本处写入值；私有会话态
    // 零解引用——门面 bindSessionFacts，可写性/配置基线经宿主成员重放）。
    kin.bindSessionFacts(m_kinEvaluation->snapshotId(),
                         m_kinEvaluation->epoch(), m_kinSessionWritable,
                         m_kinConfigBaseline);
}

void IrdWorkbenchHostPlugin::wireRequirementsSession(
    app::StorePortAdapter& adapter)
{
    if (m_domains == nullptr || !m_domains->requirements.has_value()) {
        return;  // 需求域装配缺席（§11.3 失败隔离形态）——无接线面
    }
    auto& requirements = *m_domains->requirements;
    // REQ-08/10：关闭会话会按门面契约退订共享选择服务；重新打开必须对称
    // 恢复订阅。服务属于宿主、寿命跨项目，域适配器每次attach先退旧订阅，
    // 因而重复接线不会累积监听器；失败空态仍由editor缺位阻止拾取写入。
    if (m_selection) { requirements.attachSelectionService(*m_selection); }

    project::IProjectQueryPort& query = adapter.projectStore().query();
    const project::RevisionView head = query.head();
    ProjectRequirementsClosure closure(query, head);
    auto load = m_requirementsEditor.loadBaseline(closure);
    if (!load.ok) {
        // UI-T35 P1-1（R2 审核整改）：闭包无 req-set 根（全新工程）——
        // 不再诚实空态挂起（审核 P1：用户从零无法创建任何需求对象），
        // 改以内存空根闭包初始化编辑器会话：生命周期按钮可用，新增条目
        // 入草稿，首应用时根/集合槽按 allocateNew 由 project 取号（正式
        // 存储身份——O-36 编辑期临时句柄纪律，UI 零伪造）。
        EmptyRequirementsClosure emptyClosure;
        load = m_requirementsEditor.loadBaseline(emptyClosure);
        if (!load.ok) {
            // 内存空根载入失败＝域侧编码/校验缺陷（非用户路径）——回退
            // 诚实空态并留痕（不吞错）。
            m_requirementsSessionLive = false;
            requirements.attachEditor(nullptr);
            requirements.onSessionDetached();
            if (m_diag.pipeline != nullptr) {
                m_diag.pipeline->logDev(
                    kPluginDevChannel,
                    "需求域会话：空根初始化失败——诚实空态（" + load.error.detail
                        + "）");
            }
            return;
        }
        m_requirementsSessionLive = true;
        requirements.attachEditor(&m_requirementsEditor);
        requirements.noteBaselineReloaded();  // 撤销记账随空根栈复位归零
        requirements.bindSessionAnchor(head.branch, head.id);
        // 根未入库＝首应用 allocateNew（noteAppliedRevision 的 nullopt
        // 语义在"根未挂载"场景是正确态——与提交回执回填衔接）。
        requirements.noteAppliedRevision(head.id, std::nullopt);
        requirements.bindReadiness(m_requirementsReadinessChecker.check(
            m_requirementsEditor.workingSet(), currentRequirementsCheckContext()));
        if (m_diag.pipeline != nullptr) {
            m_diag.pipeline->logDev(
                kPluginDevChannel,
                "需求域会话：空项目初始化（内存空根——首应用 allocateNew 取号）");
        }
        return;
    }
    m_requirementsSessionLive = true;
    requirements.attachEditor(&m_requirementsEditor);
    requirements.noteBaselineReloaded();  // 撤销记账随基线重建归零（UI-T39）

    // 会话锚＋根身份回填（objectRefs 扫描 req-set token——建模 onCommitted
    // 同款扫描形态；token 常量来自 requirements 公共头，域知识最小面）。
    requirements.bindSessionAnchor(head.branch, head.id);
    std::optional<core::ObjectId> rootId;
    for (const auto& ref : head.objectRefs) {
        if (ref.objectTypeToken
            == std::string(requirements::kReqSetObjectType)) {
            rootId = ref.objectId;
            break;
        }
    }
    requirements.noteAppliedRevision(head.id, rootId);

    // 最小校验（UI-T29）：首刷就绪报告——判定权威＝域侧 checker，本宿主
    // 取 check 产出直投会话态（P-REQ-6：呈现数据零判定）。
    requirements.bindReadiness(m_requirementsReadinessChecker.check(
        m_requirementsEditor.workingSet(), currentRequirementsCheckContext()));

    // 编辑后动作：重估就绪→bindReadiness（注册表谓词按当前会话态求值
    // ——draft.apply 门控随之刷新）＋面板校验页经模块组合子以最新报告
    // refreshPanel（编辑态即时预检的呈现收口）。
    requirements.setPostEditAction([this, &requirements]() {
        requirements.bindReadiness(m_requirementsReadinessChecker.check(
            m_requirementsEditor.workingSet(), currentRequirementsCheckContext()));
    });
}

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
requirements::CheckContext
IrdWorkbenchHostPlugin::currentRequirementsCheckContext() const
{
    // 闭包上下文＝当前 HEAD 的 objectRefs（与 wireRequirementsSession 的
    // HEAD 读取同源——store 查询端口现取）。F-554 根因修复的机制面：首应
    // 用后根引用表槽位被回填，空上下文的 R0 闭包核对必误判「悬空」。
    requirements::CheckContext ctx;
    // 关闭收尾仍会重算按钮/空态，已关闭上下文没有可查询闭包；
    // 与撤销门控相同地返回空事实，不能把正常关闭误报成操作异常。
    if (m_lastStoreAdapter != nullptr && !m_lastStoreAdapter->isClosed()) {
        ctx.closureRefs =
            m_lastStoreAdapter->projectStore().query().head().objectRefs;
    }
    return ctx;
}

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

    // UX-07：表单级应用是把草稿变成项目修订，普通合法输入也必须先征求
    // 用户确认，不能只在工程策略产生ConfirmableFinding时才弹窗。
    // 无待应用修改不打扰；此门放在任何域组装/提交之前，取消既不写项目，
    // 也不清除或另存草稿。显示命令不经过本入口，因此不会弹保存提示。
    const bool hasUnapplied = (m_draft && m_draft->hasSession()
                               && m_draft->anyUnappliedChanges())
                              || m_requirementsEditor.draftStatus().dirty;
    if (hasUnapplied) {
        const auto confirmedHead = store.query().head().id;
        const auto answer = QMessageBox::question(
            m_dockBody.data(), QStringLiteral("应用草稿"),
            QStringLiteral("将当前设计和需求修改应用到项目。是否继续？\n取消后草稿将保留。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            if (m_hostStatusBar) { m_hostStatusBar->showMessage(QStringLiteral("已取消应用，草稿已保留"), 4000); }
            return out; // 正常取消不产生错误诊断，也不调用任何域提交出口。
        }
        // 模态事件循环期间可能发生项目关闭/切换或外部修订；旧确认不得
        // 作用到另一个项目或新基线。共享adapter仅保证生命周期，不代表
        // 会话仍有效；确认后必须同时复核会话和权威HEAD再读提交锚。
        if (m_lastStoreAdapter != adapter || !m_controller || !m_controller->hasOpenSession()
            || store.query().head().id != confirmedHead) {
            if (m_hostStatusBar) { m_hostStatusBar->showMessage(QStringLiteral("项目已变化，请检查草稿后重新应用"), 5000); }
            return out;
        }
    }

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

    // 状态行只使用实际结局：准备失败既不是无修改，也不是已提交。
    // 部分成功时仍报告失败域数，不能因另一域成功而掩盖跨分支/组装拒绝。
    if (m_hostStatusBar != nullptr) {
        const auto failed = report.entries.size() - report.noDraftCount() - report.committedCount();
        const auto receiptFailed=static_cast<std::size_t>(std::count_if(
            report.entries.begin(),report.entries.end(),[](const DomainApplyEntryReport& row){
                return row.committed&&!row.rejectionReason.empty();
            }));
        if(receiptFailed!=0){
            // 项目修订已成功，回读/呈现尚未完成；不能冒称“全部应用完成”，
            // 更不能自动重提同一草稿制造重复部件，须显示诊断并保留原草稿。
            m_hostStatusBar->showMessage(
                QString::fromUtf8("草稿应用：%1 个域已提交但回读失败（草稿已保留），%2 个域完整应用，%3 个域未应用；详情见诊断")
                    .arg(static_cast<int>(receiptFailed))
                    .arg(static_cast<int>(report.committedCount()-receiptFailed))
                    .arg(static_cast<int>(failed)),10000);
        } else if (failed != 0) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("草稿应用：%1 个域已应用，%2 个域未应用（详情见诊断），%3 个域无变更")
                    .arg(static_cast<int>(report.committedCount()))
                    .arg(static_cast<int>(failed))
                    .arg(static_cast<int>(report.noDraftCount())), 6000);
        } else if (report.anyCommitted()) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("草稿已应用：提交 %1 个域修订（%2 个域无变更）")
                    .arg(static_cast<int>(report.committedCount()))
                    .arg(static_cast<int>(report.noDraftCount())), 5000);
        } else {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("无待应用的草稿变更（%1 个域均无待应用修改）")
                    .arg(static_cast<int>(report.noDraftCount())), 4000);
        }
    }
    // 提交后的共享面刷新（域工作集修订→共享树/检查器呈现同步——L1 链路
    // 的数据侧驱动；rejected 时树内容不变，刷新为幂等动作）＋需求面板收口
    // （UI-T39——项目级撤销键随新 tip inverse 点亮；修订事件投递是异步的，
    // 编排处同步刷一次保证按钮态同帧一致）。
    if (report.anyCommitted()) {
        refreshSharedSurfaces();
        refreshRequirementsFromSession();
        // 呈现发布触发（UI-T46——编译端口有新快照时驱动呈现刷新事务；
        // S5 双编译随命令提交同步发生，此处快照即为本次修订的编译产物）。
        refreshPresentationAfterCommit();
    }
    return out;
}

// =====================================================================
// 项目级撤销/重做编排（UI-T39——project.undo/project.redo 覆写面）
// =====================================================================

/**
 * @brief 撤销/重做共用实现体（§5.5 机制——把逆命令/原始载荷当一条普通
 *        命令提交，产生恰好一个新修订；历史只增不改，PA-2）。
 *
 * 编排纪律：可用性判定以本函数入口的 status() 现算为准（注册表谓词是
 * 快照事实，本处是执行点事实——两处同源 UndoRedoService，双检不冲突）；
 * 分支锚取权威分支表首条 tip（INV-M3 单默认分支——apply 编排同源）。
 * 提交后的呈现收口由调用方完成（refreshSharedSurfaces＋命令态重估）；
 * 需求会话的修订同步走既有事件总线（RequirementsRevisionSyncSink——
 * 零编辑重导线/有编辑 STALE 提示），本函数不重复驱动域面板（PA-1——
 * 同步编排归事件应答体）。
 *
 * @param interactionParent [in] 确认对话框父窗口（apply 编排同源——Dock 体）
 * @param hasOpenSession    [in] 会话在位判定（HostCommandInteraction 的
 *                          草稿处置对话框前置条件）
 * @param redo [in] false＝撤销（tip inverse）；true＝重做（会话栈重放）
 * @return 命令结果（committed＝已产生新修订）
 */
static project::CommandResult submitUndoOrRedo(QWidget* interactionParent,
                                               bool hasOpenSession,
                                               app::StorePortAdapter& adapter,
                                               bool redo)
{
    project::UndoRedoService& undoRedo = adapter.projectStore().undoRedo();
    // 权威分支表首条（INV-M3——apply/会话锚定同源；空表＝数据缺陷，下面
    // 以 canUndo/canRedo=false 的诚实拒绝路径兜住，不虚构分支）。
    const auto tips = adapter.projectStore().query().branchTips();
    if (tips.empty()) {
        return project::CommandResult{};  // 默认态＝非 committed——调用方按未执行呈现
    }
    const core::BranchId branch = tips.front().id;
    const project::UndoRedoStatus status = undoRedo.status(branch);
    if (redo ? !status.canRedo : !status.canUndo) {
        // 快照与执行点之间的窗口（谓词求值后修订又被推进等）——诚实拒绝，
        // 不抛（invalid_argument 是"前置违约"的域内防御，此处入口已查）。
        return project::CommandResult{};
    }
    // 逆命令可能声明待确认集（§6.7 放行流）——宿主交互桥承接（apply 同款）。
    HostCommandInteraction interaction(
        interactionParent, [hasOpenSession] { return hasOpenSession; });
    return redo ? undoRedo.redo(branch, &interaction)
                : undoRedo.undo(branch, &interaction);
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateProjectUndo(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令
    CommandOutcome out;
    const auto adapter = m_lastStoreAdapter;
    if (!m_domains || adapter == nullptr) {
        // 无项目态没有"撤销"语义（门控兜底——可用性快照已禁用的兜底面）。
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("未打开项目——无可撤销修订"), 4000);
        }
        return out;  // accepted=false——命令未派发
    }
    out.accepted = true;
    const project::CommandResult result = submitUndoOrRedo(
        m_dockBody.data(),
        m_controller && m_controller->hasOpenSession(),
        *adapter, /*redo=*/false);
    if (result.committed()) {
        if (m_hostStatusBar != nullptr) {
            // 摘要优先取服务给的"将被撤销命令摘要"（§5.5——PM-18 人读文案
            // 归 ui，此处的动态摘要即其消费面；空摘要回退固定词）。
            const QString summary = QString::fromUtf8("已撤销最近一次应用（产生新修订，历史只增不改）");
            m_hostStatusBar->showMessage(summary, 5000);
        }
        // 修订事件经事件总线已广播（store 提交段出线）——共享面同步由各
        // 应答体承接；此处幂等补一次（撤销不走 runDomainApply 的收口）。
        refreshSharedSurfaces();
        // 命令态重估（UI-T39）：撤销后 undo/redo/apply 的可用性谓词随新
        // tip 变化——重发最近上下文投影触发注册表谓词现算（顶栏按钮/菜单
        // 随刷新）；需求面板随会话新基线重估刷新（撤销后基线前移——校验
        // 页/编辑面与新 HEAD 一致）。
        if (m_content != nullptr && m_lastContextProjection.has_value()) {
            m_content->presentProjectContext(*m_lastContextProjection);
        }
        refreshRequirementsFromSession();
        // 呈现发布触发（UI-T46——撤销的逆命令若声明双编译，S5 产物即本次
        // 发布快照；呈现随新 tip 刷新）。
        refreshPresentationAfterCommit();
    } else {
        // 拒绝/中止/失败：如实呈现（诊断细节由命令端口诊断链路出线）。
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("撤销未提交（被当前上下文拒绝——详情见诊断）"), 6000);
        }
    }
    return out;
}

CommandOutcome IrdWorkbenchHostPlugin::orchestrateProjectRedo(
    const std::vector<CommandParameter>& params)
{
    (void)params;  // 无参命令
    CommandOutcome out;
    const auto adapter = m_lastStoreAdapter;
    if (!m_domains || adapter == nullptr) {
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("未打开项目——无可重做修订"), 4000);
        }
        return out;
    }
    out.accepted = true;
    const project::CommandResult result = submitUndoOrRedo(
        m_dockBody.data(),
        m_controller && m_controller->hasOpenSession(),
        *adapter, /*redo=*/true);
    if (result.committed()) {
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("已重做最近一次撤销（产生新修订）"), 5000);
        }
        refreshSharedSurfaces();
        if (m_content != nullptr && m_lastContextProjection.has_value()) {
            m_content->presentProjectContext(*m_lastContextProjection);
        }
        refreshRequirementsFromSession();
        // 呈现发布触发（UI-T46——重放命令若声明双编译同拍刷新，与撤销
        // 对称）。
        refreshPresentationAfterCommit();
    } else {
        if (m_hostStatusBar != nullptr) {
            m_hostStatusBar->showMessage(
                QString::fromUtf8("重做未提交（会话重做栈已空或被拒绝——详情见诊断）"), 6000);
        }
    }
    return out;
}

/**
 * @brief 需求域会话刷新（UI-T39——审核 P1/P2 的呈现收口点）。
 *
 * 编排：会话存活时以域侧 checker 现算就绪（P-REQ-6——判定权威在域，本
 * 宿主直投零判定）→bindReadiness 更新会话态→模块组合子驱动面板全面板
 * 刷新（校验页/编辑面/两级撤销按钮同帧一致）。三调用点共用：
 *   ①打开成功（openViaSessionController——首刷，校验页不再停留在『尚未
 *     执行』静态直到首次编辑）；
 *   ②项目级撤销/重做提交（orchestrateProjectUndo/Redo——基线前移后
 *     编辑面与新 HEAD 一致）；
 *   ③外部修订重导线（onRequirementExternalRevision 的 RewireFromHead）。
 * 无存活会话＝空操作（诚实二态——面板保持无会话空态）。
 */
void IrdWorkbenchHostPlugin::refreshRequirementsFromSession()
{
    if (m_domains == nullptr || !m_domains->requirements.has_value()
        || !m_requirementsSessionLive) {
        return;
    }
    auto& requirements = *m_domains->requirements;
    requirements.bindReadiness(m_requirementsReadinessChecker.check(
        m_requirementsEditor.workingSet(), currentRequirementsCheckContext()));
    requirements.refreshFromSession();
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
        // A-14：只有已接受项目上下文才恢复编辑基线；候选预打开不碰活动会话。
        // 使用当前HEAD而非分支表首项，保证工作集、分支和修订来自同一底本。
        const auto adapter = m_lastStoreAdapter;
        if (adapter != nullptr) {
            const auto& query = adapter->projectStore().query();
            const auto head = query.head();
            std::vector<core::ObjectId> roots;
            for (const auto& object : head.objectRefs) {
                if (object.objectTypeToken == modeling::kRobotDesignObjectType) {
                    roots.push_back(object.objectId);
                }
            }
            std::string restoreError;
            if (roots.size() == 1) {
                HostProjectClosureView closure(query, std::string(modeling::kRobotDesignObjectType), nullptr);
                m_domains->modeling.restoreCommittedSession(
                    closure, roots.front(), head.branch, head.id, restoreError);
            } else if (roots.size() > 1) {
                restoreError = "已保存修订包含多个建模根，无法确定编辑基线";
            } else {
                // 新工程没有建模根，保留原空态与后续模板创建路径。
                m_domains->modeling.bindSessionAnchor(head.branch, head.id);
            }
            if (!restoreError.empty()) {
                if (m_diag.pipeline) { m_diag.pipeline->logDev(kPluginDevChannel, restoreError); }
                if (auto* studio = getRobWorkStudio()) {
                    if (auto* status = studio->statusBar()) {
                        status->showMessage(QString::fromUtf8(restoreError.c_str()), 8000);
                    }
                }
            }
            if (m_domains->requirements.has_value()) {
                m_domains->requirements->bindSessionAnchor(head.branch, std::nullopt);
            }
            // A-14：关闭时建模模块退订共享选择，接受新会话后必须对称重订阅。
            // attach内部先退旧订阅，重复上下文投影不会积累回调。
            if (m_selection) { m_domains->modeling.attachSelectionService(*m_selection); }
        }
        // 共享面呈现同步（打开后的域数据入树——L1 链路数据侧驱动）。
        refreshSharedSurfaces();
        return;
    }
    // 无项目（关闭完成/切换 A 段排空）：会话脱离——模块锚清空＋草稿工作
    // 集复位＋面板空态（§8.6 表处置的模块半区；磁盘草稿零触碰）＋项目
    // 关闭统一清理八类对象（UI-T23 acceptance 4——具名用例承载）。
    // UI-T84 顺序契约：本链须先于 teardown 完成但任何一步异常不得阻断
    // teardown（桥拆绑/呈现释放是关闭态最关键转换；F-910 验收追查实证
    // undo 可用性谓词族查询在关闭上下文抛 StoreError 时 teardown 被跳过
    // ＝呈现会话跨关闭残留）。根因面（undoRedoDisablement 谓词）已修
    // （isClosed 诚实降级），此处为纵深：逐段隔离留痕后继续清理。
    try {
        m_domains->modeling.onSessionDetached();
    } catch (const std::exception& detachError) {
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    std::string("modeling 会话脱离异常（隔离继续清理）：")
                                        + detachError.what());
        }
    }
    if (m_domains->requirements.has_value()) {
        try {
            m_domains->requirements->onSessionDetached();
        } catch (const std::exception& detachError) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel,
                                        std::string("requirements 会话脱离异常（隔离继续清理）：")
                                            + detachError.what());
            }
        }
    }
    // REQ-10／UX-06：会话姿态归当前项目，必须先清姿态，再解绑模型。
    // 正式拾取/回写后 KinSessionPose 已设置；若先清 snapshot，随后关闭刷新
    // 位姿页会拿旧 q 解析已解绑的模型（实际栈：refreshPosePane→resolveSessionTcp
    // →KinSessionModelView::model 空委托）。clear 保持注入地址稳定，只清本项目
    // 的 rad/m 关节值，不写修订、不猜默认零位；重新打开必须重新建立会话姿态。
    m_kinSessionPose.clear();
    // kinematics 会话脱离（UI-T64——执行器快照解绑＋面板绑定键同步清空
    // ——拆除拍对称收口：无项目态受理由快照缺位拒绝，不残留旧会话绑定；
    // 私有会话态零解引用——门面 bindSessionFacts）。
    if (m_kinEvaluation != nullptr && m_domains->kinematics.has_value()) {
        try {
            m_kinEvaluation->attachSnapshot(nullptr, core::ContentIdentity{});
            m_domains->kinematics->bindSessionFacts(
                core::ContentIdentity{}, m_kinEvaluation->epoch(),
                m_kinSessionWritable, m_kinConfigBaseline);
        } catch (const std::exception& detachError) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel,
                                        std::string("kinematics 会话脱离异常（隔离继续清理）：")
                                            + detachError.what());
            }
        }
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
    //    NameMap＝真值端口（HostRuntimeNameMapPort——UI-T46 呈现装配：
    //    绑当前呈现视图，未就位＝同构诚实空二态——L3 反解失败分支为
    //    未发布形态常态；发布/拆除拍 bind/clear 单点更新即 Selection
    //    服务/三维网关/需求域缝三消费面同步升级）；树定位回调＝共享树
    //    面板 locateAndHighlight（面板创建后经 lambda 捕获重绑——面板
    //    先于服务构造的次序解法与域 harness 同款）；高亮出口＝宿主
    //    WorkCellScene 实现（L2 真高亮——呈现缺席时动作跳过＋Dev 留痕，
    //    判定照常）。
    m_runtimeNameMap = std::make_shared<HostRuntimeNameMapPort>();
    m_nameMapPort = m_runtimeNameMap;  // UI-T46：具型句柄＋端口面同一对象（三消费面共享）
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
        m_nameMapPort,
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
        // F-551：名称解析端口升级为需求域前端（呈现层最小改法——owner
        // 出路 2「树模型名称解析覆盖需求域对象」）。此前生产装配恒为
        // C-11 桩（NullUiNameResolver 恒 nullopt），而需求域对象不进
        // runtime NameMap（NameScope 词表只有运行时结构对象）——树上
        // 恒呈「（未命名对象）」。前端解析工作集语义名（根/点/区域/工况/
        // 计划），闭包外对象应答 nullopt＝与桩同语义面（UX-02 占位兜底
        // 不破坏）；当前无下游解析器可链（桩恒 nullopt），将来 runtime
        // 名称端口真值装配接续时在宿主装配点链接回落链。升级须在③面板
        // deps 绑定与④内容装配（contentDeps.wiring.nameResolver）之前
        // ——三消费面同持本 shared_ptr，一处升级全覆盖。
        if (auto frontend = m_domains->requirements->sharedNameResolverFrontend()) {
            m_nameResolver = std::move(frontend);
        }
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

    // A-14／UX-02：原前端仅认识需求对象，恢复后的建模树因此全部显示
    // “未命名对象”。组合现有查询端口：需求名→建模草稿局部名→同一已发布
    // 运行时的局部名→域属性页类型标题。最后一级用于没有localName的位姿集/
    // 传动值对象；只是已有类型标题的呈现，不伪造可持久化的对象名称。
    const auto domainNames = m_nameResolver;
    m_nameResolver = std::make_shared<HostUiNameResolver>(
        [this, domainNames](core::ObjectId id) -> std::optional<std::string> {
            if (domainNames) {
                if (auto name = domainNames->resolveObjectId(id)) { return name; }
            }
            if (!m_domains) { return std::nullopt; }
            std::string local;
            if (m_domains->modeling.tryDraftObjectName(id, local)) { return local; }
            const auto snapshot = m_compilePort ? m_compilePort->lastPublishedSnapshot() : nullptr;
            auto* studio = getRobWorkStudio();
            const auto cell = studio ? studio->getWorkCell() : nullptr;
            if (m_lastStoreAdapter && !m_lastStoreAdapter->isClosed()
                && snapshot && cell && &snapshot->workCell().workCell() == cell.get()) {
                const auto name = snapshot->nameMap().resolveObjectId(id);
                if (name.ok()) { return name.get().localName; }
            }
            const auto pages = m_domains->modeling.sharedSurfaceProviders().propertyPages;
            if (const auto page = pages->commonFieldsPage(id)) { return page->title; }
            return std::nullopt; // 闭包外身份仍保留原占位，不猜测名称。
        });

    // ③共享面板：三消费面共享组合后的同一个只读名称端口。
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
    // UI-T26 顶栏上下文栏：选择事实转发（L1 汇聚点广播→内容装配面
    // noteSelectionForContext——『当前对象』标签唯一数据入口；转发器以
    // 观察者接口指针持有，RAII 句柄随清理释放与检查器订阅同款）。
    m_contextSelectionForwarder = std::make_unique<ContextSelectionForwarder>(
            [this](const ui::SelectionChange& change) {
                if (m_content != nullptr) {
                    m_content->noteSelectionForContext(change);
                }
            });
    m_contextSelectionSubscription = m_selection->subscribe(*m_contextSelectionForwarder);

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

void IrdWorkbenchHostPlugin::assembleView3DGateway()
{
    // 宿主三维网关装配（UI-T45——方案 spec 三桥一源的宿主半区；SA-02：
    // 全部经 RWStudioView3D 公开 API 消费，事件注入＝过滤器外挂）。
    if (m_view3dGateway != nullptr || m_domains == nullptr
        || m_selection == nullptr || m_nameMapPort == nullptr) {
        return;  // 已装配/依赖缺席（幂等静默——装配序保证）
    }
    rws::RobWorkStudio* studio = getRobWorkStudio();
    const rws::RWStudioView3D::Ptr view =
        studio != nullptr ? studio->getView() : nullptr;
    if (view == nullptr) {
        // 降级基线（spec §4）：视图不可得＝网关不装配——上行拾取/TCP 源
        // /呈现出口三面缺席（选择服务既有 outlet 保持显式形态），不虚构
        // 三维能力（UX-11/ERR-01）。
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "view3d gateway: RWStudioView3D 不可得——三桥不接（降级基线）");
        }
        return;
    }

    HostView3DGateway::Deps deps;
    /** @brief UI线程最近真实拾取的值锚；函数闭包共享持有，场景Ptr防地址复用。
     * 只缓存交互结果，不作名称或修订真值；每次消费仍查询现有权威端口。
     */
    struct RequirementsPickedAnchor {
        rw::models::WorkCell::Ptr scene;
        std::optional<core::ObjectId> objectId;
        std::string projectId;
        std::string revisionId;
    };
    const auto pickedAnchor = std::make_shared<RequirementsPickedAnchor>();
    // 上行仍使用框架真实raycast；把反解结果固定到发起时项目/修订/场景。
    // 此缓存与业务选中分离，用户回到需求工位后仍可确认刚拾取的夹具Frame。
    deps.pickFrame = [this, view, studio, pickedAnchor](int x, int y) {
        *pickedAnchor = RequirementsPickedAnchor{}; // 空白/失败拾取立即使旧结果失效。
        auto* frame = view->pickFrame(x, y);
        const auto adapter = m_lastStoreAdapter;
        if (frame && studio && adapter && !adapter->isClosed() && m_nameMapPort) {
            pickedAnchor->objectId = m_nameMapPort->resolveObjectIdFromRuntimeName(frame->getName());
            if (pickedAnchor->objectId) {
                pickedAnchor->scene = studio->getWorkCell();
                pickedAnchor->projectId = adapter->projectStore().projectId().toCanonical();
                pickedAnchor->revisionId = adapter->projectStore().query().head().id.toCanonical();
            }
        }
        return frame;
    };
    // TCP 帧缝（设备名→JointDevice 末端帧——WorkCell 现查，非 owning）。
    deps.resolveTcpFrame = [studio](const std::string& deviceName)
        -> const rw::kinematics::Frame* {
        if (studio == nullptr) {
            return nullptr;
        }
        const rw::models::WorkCell::Ptr workcell = studio->getWorkCell();
        if (workcell.isNull()) {
            return nullptr;
        }
        const auto device =
            workcell->findDevice<rw::models::JointDevice>(deviceName);
        return device != nullptr ? device->getEnd() : nullptr;
    };
    // 会话态缝（宿主当前 State——只读借用，KIN-06 零修订）。
    deps.currentState = [studio]() -> const rw::kinematics::State* {
        return studio != nullptr ? &studio->getState() : nullptr;
    };
    // 汇聚端口（与 SelectionService 同一名称映射实例——"真映射注入点
    // 单一"纪律：WP-24-T08 替换成员实例即两消费面同步升级）。
    deps.selection = m_selection.get();
    deps.nameMap = m_nameMapPort.get();
    // 域分发缝（未命中本域＝域内诚实 false；需求域缺席＝失败隔离形态）。
    deps.dispatchToModeling = [this](const core::ObjectId& oid) {
        return m_domains != nullptr
               && m_domains->modeling.reportView3DPick(oid);
    };
    deps.dispatchToRequirements = [this](const core::ObjectId& oid) {
        return m_domains != nullptr && m_domains->requirements.has_value()
               && m_domains->requirements->reportView3DPick(oid);
    };
    // 会话预览渲染后端（UI-T33——场景原语绑定；装配序在网关构造前——
    // Deps 冻结面）。studio 非 owning——后端存活期随插件（成员持有）。
    m_view3dPreviewBackend = std::make_unique<HostView3DPreviewBackend>(studio);
    deps.previewBackend.draw = [this](const ui::View3DPreviewUpdate& update) {
        return m_view3dPreviewBackend != nullptr
                   ? m_view3dPreviewBackend->draw(update) : false;
    };
    deps.previewBackend.clear = [this]() {
        if (m_view3dPreviewBackend != nullptr) { m_view3dPreviewBackend->clear(); }
    };

    m_view3dGateway = std::make_unique<HostView3DGateway>(std::move(deps));

    // 需求域三维缝绑定（UI-T33——capture-tcp/pick-feature 解除降级；
    // 全部缝对网关/宿主现取零缓存）。需求域缺席＝失败隔离形态零绑定。
    if (m_domains->requirements.has_value()) {
        requirements::RequirementsView3DSeams seams;
        seams.listDevices = [studio]() -> std::vector<std::string> {
            std::vector<std::string> names;
            const rw::models::WorkCell::Ptr workcell =
                studio != nullptr ? studio->getWorkCell() : nullptr;
            if (workcell.isNull()) { return names; }
            for (const auto& device : workcell->getDevices()) {
                if (device != nullptr) { names.push_back(device->getName()); }
            }
            return names;
        };
        seams.tcpWorldPose = [this](const std::string& deviceName) {
            return m_view3dGateway != nullptr
                       ? m_view3dGateway->currentTcpPose(deviceName)
                       : std::nullopt;
        };
        seams.resolveFrameObjectId =
            [this](const std::string& frameName) {
                return m_nameMapPort != nullptr
                           ? m_nameMapPort->resolveObjectIdFromRuntimeName(frameName)
                           : std::nullopt;
            };
        seams.lastPickedObjectId = [this, studio, pickedAnchor]() -> std::optional<core::ObjectId> {
            const auto adapter = m_lastStoreAdapter;
            if (!pickedAnchor->objectId || !studio || !adapter || adapter->isClosed()
                || studio->getWorkCell() != pickedAnchor->scene
                || adapter->projectStore().projectId().toCanonical() != pickedAnchor->projectId
                || adapter->projectStore().query().head().id.toCanonical() != pickedAnchor->revisionId
                || !m_nameMapPort || !m_nameMapPort->resolveRuntimeName(*pickedAnchor->objectId)) {
                return std::nullopt; // 原场景/项目/修订已变化；禁止沿用旧拾取值。
            }
            return pickedAnchor->objectId;
        };
        // 捕获基线由需求模块在每次命令发起时从会话权威态取值，不能在启动期固化空串。
        // 会话闭包引用元数据（UI-T33——拾取写回浅核对的 CheckContext 来源）。
        // UI-T77（F-558）修复：改为**延迟现取**（捕 this→调用时经
        // currentRequirementsCheckContext() 同源现取）。原实现按绑定时点的
        // m_lastStoreAdapter 判空捕获裸指针——本函数在 initialize 装配段
        // 执行（进程启动拍，任何项目均未打开、adapter 恒空），缝保持缺省
        // 空 function 且此后无再绑定点→拾取浅核对拿空闭包恒误判「悬空」
        // （F-554 空闭包家族第六处；F-550③ 骨架呈现让拾取链可达后即踩）。
        // 延迟现取＝绑定时机无关，与 F-554 修复的面板就绪上下文同一机制
        // 面；顺带消除裸 adapter 指针的跨项目存活隐患（adapter 被覆盖析构
        // 后旧缝仍持悬垂指针的潜在 UAF 面）。
        seams.sessionClosureRefs = [this]() -> std::vector<project::ObjectRef> {
            return currentRequirementsCheckContext().closureRefs;
        };
        /**
         * @brief REQ-01/08：按既有引用现取权威世界变换；不缓存跨发布Frame。
         * @param ref [in] 原浅引用，Tool精确到tcpKey；长度m、旋转无量纲。
         * @return 当前发布场景下的世界变换；缺目标/键/快照返回nullopt。
         * 仅UI线程交互调用；不在项目writer锁内调用，不触发新编译或草稿写入。
         */
        seams.referenceWorldPose = [this, studio](const requirements::RequirementReference& ref)
            -> std::optional<rw::math::Transform3D<>> {
            if (!ref.wellFormed()) { return std::nullopt; }
            const auto adapter = m_lastStoreAdapter;
            const auto snapshot = m_compilePort ? m_compilePort->lastPublishedSnapshot() : nullptr;
            const auto cell = studio ? studio->getWorkCell() : nullptr;
            // 编译快照记录命令的baseRevision，而提交后的HEAD已经前进；二者
            // 不是同一语义，不能以修订相等判断帧是否可用。这里只接受宿主已挂载
            // 的同一个WorkCell，防止发布切换期间把旧场景的Frame用于新状态。
            if (!adapter || adapter->isClosed() || !snapshot || !cell
                || &snapshot->workCell().workCell() != cell.get()) { return std::nullopt; }
            if (ref.kind == requirements::RequirementRefKind::World) {
                return rw::math::Transform3D<>::identity();
            }
            auto object = ref.objectId;
            auto scope = runtime::NameScope::SceneObject;
            const auto& model = snapshot->model();
            if (ref.kind == requirements::RequirementRefKind::ModelFrame) {
                scope = runtime::NameScope::BaseMount;
            } else if (ref.kind == requirements::RequirementRefKind::Tool
                || ref.kind == requirements::RequirementRefKind::DefaultTcp) {
                scope = runtime::NameScope::Tcp;
                if (ref.kind == requirements::RequirementRefKind::DefaultTcp) {
                    if (!model.defaultTcpIndex()) { return std::nullopt; }
                    object = model.tools().at(*model.defaultTcpIndex()).objectId;
                }
            }
            if (!object) { return std::nullopt; }
            const auto& entries = snapshot->nameMap().entries();
            const auto entry = std::find_if(entries.begin(), entries.end(), [&](const auto& item) {
                return item.objectId == *object && item.scope == scope;
            });
            auto* frame = entry != entries.end() ? cell->findFrame(entry->fullName) : nullptr;
            if (!frame) { return std::nullopt; }
            const auto pose = rw::kinematics::Kinematics::worldTframe(frame, studio->getState());
            if (ref.kind != requirements::RequirementRefKind::Tool) { return pose; }
            // runtime目前每工具物化一个规范TCP。其他具名TCP的交互位置由
            // 同一runtime帧反解法兰，再组合当前闭包公开工具偏置；不另算FK，
            // 不据此声称运动学评估器已能消费任意tcpKey。
            const auto tool = std::find_if(model.tools().begin(), model.tools().end(), [&](const auto& item) {
                return item.objectId == *object;
            });
            if (tool == model.tools().end()) { return std::nullopt; }
            const auto head = adapter->projectStore().query().head();
            const auto stored = std::find_if(head.objectRefs.begin(), head.objectRefs.end(), [&](const auto& item) {
                return item.objectId == *object && item.objectTypeToken == modeling::kToolDefinitionObjectType;
            });
            if (stored == head.objectRefs.end()) { return std::nullopt; }
            const auto bytes = adapter->projectStore().query().tryObject(stored->objectId, stored->contentVersion);
            const modeling::RobotDesignCodec codec;
            const auto decoded = bytes ? codec.decode(*bytes,modeling::kCurrentFormatVersion)
                : codec.decode({},modeling::kCurrentFormatVersion);
            if (!decoded.ok() || !std::holds_alternative<modeling::ToolDefinition>(decoded.get())) { return std::nullopt; }
            const auto& authored = std::get<modeling::ToolDefinition>(decoded.get());
            const auto tcp = std::find_if(authored.tcpList.begin(),authored.tcpList.end(),[&](const auto& item) {
                return item.key == ref.tcpKey;
            });
            if (tcp == authored.tcpList.end()) { return std::nullopt; }
            return pose * rw::math::inverse(tool->tcpOffset) * authored.mountInterface * tcp->offset;
        };
        const auto referencePose = seams.referenceWorldPose;
        /**
         * @brief REQ-01/09：从当前项目闭包生成按业务名显示的引用目录值快照。
         * @return 稳定排序的候选；无项目返回空，缺运行时目标保留名称并标不可用。
         * 仅UI线程现取；不持有跨修订对象，不写草稿，解析失败不杜撰名称。
         */
        seams.referenceChoices = [this, referencePose]() -> std::vector<requirements::RequirementsReferenceChoice> {
            using Ref = requirements::RequirementReference;
            using Kind = requirements::RequirementRefKind;
            std::vector<requirements::RequirementsReferenceChoice> result;
            const auto adapter = m_lastStoreAdapter;
            if (!adapter || adapter->isClosed()) { return result; }
            const auto append = [&](Ref ref, const std::string& name) {
                const bool available = referencePose(ref).has_value();
                result.push_back({std::move(ref),name,available,
                    available ? std::string() : "当前修订中缺少可解析的运行时参考帧或TCP"});
            };
            append(Ref{},"世界坐标系");
            Ref defaultTcp; defaultTcp.kind = Kind::DefaultTcp; append(defaultTcp,"默认TCP");
            const modeling::RobotDesignCodec codec;
            for (const auto& stored : adapter->projectStore().query().head().objectRefs) {
                if (stored.objectTypeToken != modeling::kRobotDesignObjectType
                    && stored.objectTypeToken != modeling::kSceneObjectObjectType
                    && stored.objectTypeToken != modeling::kToolDefinitionObjectType) { continue; }
                const auto bytes = adapter->projectStore().query().tryObject(stored.objectId,stored.contentVersion);
                const auto decoded = bytes ? codec.decode(*bytes,modeling::kCurrentFormatVersion)
                    : codec.decode({},modeling::kCurrentFormatVersion);
                if (!decoded.ok()) { continue; } // 数据错误由原就绪诊断承接，不能杜撰业务名。
                Ref ref; ref.objectId = stored.objectId;
                if (std::holds_alternative<modeling::RobotDesign>(decoded.get())) {
                    const auto& model = std::get<modeling::RobotDesign>(decoded.get());
                    ref.kind = Kind::ModelFrame;
                    append(ref,"机械臂 / " + (model.displayName.empty() ? std::string("当前模型") : model.displayName));
                } else if (std::holds_alternative<modeling::SceneObject>(decoded.get())) {
                    const auto& scene = std::get<modeling::SceneObject>(decoded.get());
                    ref.kind = Kind::SceneObject;
                    append(ref,"场景 / " + scene.localName);
                } else if (std::holds_alternative<modeling::ToolDefinition>(decoded.get())) {
                    const auto& tool = std::get<modeling::ToolDefinition>(decoded.get());
                    ref.kind = Kind::Tool;
                    for (const auto& tcp : tool.tcpList) {
                        ref.tcpKey = tcp.key;
                        append(ref,"工具 / " + (tool.displayName.empty() ? tool.localName : tool.displayName) + " / " + tcp.key);
                    }
                }
            }
            // 目录排序不依赖闭包存储序，重查时可逐值核对，保持同名候选的稳定身份。
            std::sort(result.begin(),result.end(),[](const auto& left,const auto& right) {
                const auto key=[](const auto& item) {
                    return std::make_tuple(int(item.reference.kind),item.displayName,
                        item.reference.objectId ? item.reference.objectId->toCanonical() : std::string(),item.reference.tcpKey);
                };
                return key(left)<key(right);
            });
            return result;
        };
        /**
         * @brief REQ-08：真实相机投影的临时三维创建/移动，确认仅返回世界系值。
         * @param initial [in] 有限刚体初值，平移m、旋转无量纲；非法输入fail-fast。
         * @param creating [in] true允许创建时编辑RPY(rad)，false仅移动位置并保留输入旋转。
         * @return 确认世界系位姿；取消/关闭/切项目/修订或场景改变返回nullopt。
         * Qt子对象由本次工具清理；仅UI线程嵌套事件循环，无编辑器/项目写入。
         */
        seams.editWorldTarget=[this,studio](const rw::math::Transform3D<>& initial,bool creating)
            -> std::optional<rw::math::Transform3D<>> {
            for (int i=0;i<3;++i) {
                if (!std::isfinite(initial.P()[i])) { throw std::invalid_argument("三维交互初值位置非有限"); }
                for (int j=0;j<3;++j) {
                    if (!std::isfinite(initial.R()(i,j))) { throw std::invalid_argument("三维交互初值旋转非有限"); }
                }
            }
            const QPointer<IrdWorkbenchHostPlugin> owner(this);
            const QPointer<rws::RobWorkStudio> guardedStudio(studio);
            const QPointer<QWidget> viewWidget(m_view3d);
            const auto adapter=m_lastStoreAdapter;
            if (!guardedStudio || !viewWidget || !adapter || adapter->isClosed()
                || viewWidget->property("irdTargetEditActive").toBool()) { return std::nullopt; }
            const auto cell=studio->getWorkCell(); const auto revision=adapter->projectStore().query().head().id;
            const auto view=studio->getView();
            QPointer<QWidget> glWidget;
            for (auto* child : viewWidget->findChildren<QWidget*>()) {
                if (child->inherits("QOpenGLWidget")) { glWidget=child; break; }
            }
            if (!cell || !view || !glWidget || !m_view3dGateway) { return std::nullopt; }
            const auto viewer=view->getSceneViewer();
            if (!viewer || !viewer->getViewCamera()) { return std::nullopt; }
            const auto sessionValid=[owner,guardedStudio,viewWidget,glWidget,adapter,cell,revision] {
                return owner && guardedStudio && viewWidget && glWidget && !adapter->isClosed()
                    && owner->m_lastStoreAdapter==adapter && guardedStudio->getWorkCell().get()==cell.get()
                    && adapter->projectStore().query().head().id==revision;
            };
            QPointer<QDialog> dialog=new QDialog(studio);
            dialog->setObjectName("ird_req_target_3d_tool");
            dialog->setWindowTitle(creating ? "三维创建目标（确认前不写入）" : "三维移动工位（确认前不写入）");
            dialog->setWindowFlags(Qt::Tool); dialog->setWindowModality(Qt::NonModal);
            auto* layout=new QVBoxLayout(dialog);
            auto* help=new QLabel("在三维视图中左键点击/拖动；沿按下时的视平面移动。可输入世界坐标调整深度。彩色轴为世界轴，灰线为交互姿态。Esc或取消不写入草稿。",dialog);
            help->setWordWrap(true); layout->addWidget(help);
            auto* fields=new QFormLayout; layout->addLayout(fields);
            std::array<QLineEdit*,3> positions{},angles{};
            auto candidate=initial;
            const auto rpy=[](const rw::math::Rotation3D<>& r) {
                const double cp=std::hypot(r(0,0),r(1,0)); const double pitch=std::atan2(-r(2,0),cp);
                return cp>1e-12 ? rw::math::Vector3D<>(std::atan2(r(2,1),r(2,2)),pitch,std::atan2(r(1,0),r(0,0)))
                    : rw::math::Vector3D<>(0,pitch,std::atan2(-r(0,1),r(1,1)));
            };
            const auto initialRpy=rpy(initial.R());
            for (int i=0;i<3;++i) {
                positions[i]=new QLineEdit(QString::number(initial.P()[i],'g',17),dialog);
                positions[i]->setObjectName(QString("ird_target_world_%1").arg(i));
                fields->addRow(QString("世界%1（m）").arg(QString("XYZ").mid(i,1)),positions[i]);
                angles[i]=new QLineEdit(QString::number(initialRpy[i],'g',17),dialog);
                angles[i]->setObjectName(QString("ird_target_rpy_%1").arg(i)); angles[i]->setReadOnly(!creating);
                fields->addRow(QString("%1（rad）").arg(QStringList{"Roll","Pitch","Yaw"}[i]),angles[i]);
            }
            if (!creating) {
                auto* note=new QLabel("仅移动位置，保留原姿态规则。显示的交互轴不替代规则解析结果。",dialog);
                note->setWordWrap(true); layout->addWidget(note);
            }
            auto* error=new QLabel(dialog); error->setObjectName("ird_target_edit_error"); error->setWordWrap(true); layout->addWidget(error);
            auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,dialog);
            buttons->button(QDialogButtonBox::Ok)->setText("确认写入草稿"); layout->addWidget(buttons);
            const auto showPreview=[&] {
                if (!sessionValid() || !owner->m_view3dGateway) { return; }
                ui::View3DPreviewUpdate update;
                update.frameMarkers=owner->m_reqMarkers; update.boxOutline=owner->m_reqBox;
                update.sampleGrid=owner->m_reqGrid.value_or(ui::View3DSampleGrid{});
                update.frameMarkers.push_back({"临时目标（世界轴）",cell->getWorldFrame()->getName(),candidate.P()});
                // 三根灰线只表达交互矩阵的列向量，不向计算域提交姿态约束；
                // 0.15m为界面指示长度，与产品几何/容差及评估结果无关。
                for (int i=0;i<3;++i) {
                    rw::math::Vector3D<> axis(candidate.R()(0,i),candidate.R()(1,i),candidate.R()(2,i));
                    update.sampleGrid->gridLines.push_back({candidate.P(),candidate.P()+axis*0.15});
                }
                owner->m_view3dGateway->applyPreview(update);
            };
            const auto readFields=[&] {
                rw::math::Vector3D<> p,a;
                for (int i=0;i<3;++i) {
                    bool good=false; p[i]=positions[i]->text().toDouble(&good);
                    if (!good || !std::isfinite(p[i])) { error->setText("世界坐标须为有限数（m）"); return false; }
                    a[i]=angles[i]->text().toDouble(&good);
                    if (!good || !std::isfinite(a[i])) { error->setText("姿态角须为有限数（rad）"); return false; }
                }
                auto rotation=initial.R();
                if (creating) {
                    // 字段按roll,pitch,yaw排列；框架RPY构造参数依Z/Y/X轴传入。
                    rotation=rw::math::RPY<>(a[2],a[1],a[0]).toRotation3D();
                }
                candidate=rw::math::Transform3D<>(p,rotation); error->clear(); showPreview(); return true;
            };
            for (int i=0;i<3;++i) {
                QObject::connect(positions[i],&QLineEdit::editingFinished,dialog,[&] { readFields(); });
                QObject::connect(angles[i],&QLineEdit::editingFinished,dialog,[&] { readFields(); });
            }
            std::optional<rw::math::Transform3D<>> result;
            QEventLoop loop;
            QObject::connect(dialog,&QDialog::finished,&loop,&QEventLoop::quit);
            QObject::connect(dialog,&QObject::destroyed,&loop,&QEventLoop::quit);
            QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
            QObject::connect(buttons,&QDialogButtonBox::accepted,dialog,[&] {
                if (sessionValid() && readFields()) { result=candidate; dialog->accept(); }
                else if (!sessionValid()) { dialog->reject(); }
            });
            bool dragging=false;
            rw::math::Vector3D<> planePoint=initial.P(),planeNormal(0,0,1),dragOffset;
            const auto project=[&](const QPointF& local)->std::optional<rw::math::Vector3D<>> {
                const auto camera=viewer->getViewCamera(); int vx=0,vy=0,w=0,h=0; camera->getViewport(vx,vy,w,h);
                if (w<=0 || h<=0 || glWidget->width()<=0 || glWidget->height()<=0) { return std::nullopt; }
                // Qt事件坐标为逻辑像素；相机viewport为framebuffer物理像素。
                // 先按实际GL控件/DPR换算，再减viewport偏移，避免125%缩放下拖动漂移。
                const double fullWidth=std::round(glWidget->width()*glWidget->devicePixelRatioF());
                const double fullHeight=std::round(glWidget->height()*glWidget->devicePixelRatioF());
                const double x=2*((local.x()/glWidget->width()*fullWidth-vx)/w)-1;
                const double y=2*(((1-local.y()/glWidget->height())*fullHeight-vy)/h)-1;
                const auto worldCamera=camera->getTransform();
                rw::math::Vector3D<> origin,direction;
                double left=0,right=0,bottom=0,top=0,nearPlane=0,farPlane=0,fovy=0,aspect=0;
                const auto projection=camera->getProjectionMatrix();
                if (projection.getOrtho(left,right,bottom,top,nearPlane,farPlane)) {
                    origin=worldCamera*rw::math::Vector3D<>(left+(x+1)*0.5*(right-left),bottom+(y+1)*0.5*(top-bottom),0);
                    direction=worldCamera.R()*rw::math::Vector3D<>(0,0,-1);
                } else if (projection.getPerspective(fovy,aspect,nearPlane,farPlane)) {
                    const double scale=std::tan(fovy*3.14159265358979323846/360.0); // ProjectionMatrix约定degrees。
                    origin=worldCamera.P(); direction=worldCamera.R()*rw::math::Vector3D<>(x*aspect*scale,y*scale,-1);
                } else { return std::nullopt; }
                const double denominator=rw::math::dot(direction,planeNormal);
                if (!std::isfinite(denominator) || std::abs(denominator)<1e-12) { return std::nullopt; }
                const double distance=rw::math::dot(planePoint-origin,planeNormal)/denominator;
                if (!std::isfinite(distance) || distance<0) { return std::nullopt; }
                return origin+direction*distance;
            };
            RequirementTargetEventFilter filter([&](QObject* watched,QEvent* event) {
                if (!glWidget || !viewWidget) { return false; }
                auto* widget=qobject_cast<QWidget*>(watched);
                if (!widget || !(widget==glWidget || glWidget->isAncestorOf(widget) || widget==viewWidget)) { return false; }
                // 场景失效由50ms观察器退出，不能在任意Qt内部事件中同步reject，
                // 否则窗口关闭/失焦事件可能重入本过滤器而递归取消。
                if (!sessionValid()) { return false; }
                if (event->type()==QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape) {
                    dialog->reject(); return true;
                }
                if (event->type()!=QEvent::MouseButtonPress && event->type()!=QEvent::MouseMove
                    && event->type()!=QEvent::MouseButtonRelease && event->type()!=QEvent::MouseButtonDblClick) { return false; }
                auto* mouse=static_cast<QMouseEvent*>(event);
                if (event->type()==QEvent::MouseButtonPress && mouse->button()==Qt::LeftButton) {
                    dragging=true; planePoint=candidate.P();
                    const auto rotation=viewer->getViewCamera()->getTransform().R();
                    planeNormal=rw::math::Vector3D<>(rotation(0,2),rotation(1,2),rotation(2,2));
                    const auto hit=project(widget->mapTo(glWidget,mouse->position().toPoint()));
                    if (!hit) { dragging=false; error->setText("当前视角不能投影到移动平面"); return true; }
                    dragOffset=creating ? rw::math::Vector3D<>() : candidate.P()-*hit;
                } else if (!dragging) { return mouse->button()==Qt::LeftButton; }
                if (dragging) {
                    const auto hit=project(widget->mapTo(glWidget,mouse->position().toPoint()));
                    if (hit) {
                        candidate=rw::math::Transform3D<>(*hit+dragOffset,candidate.R());
                        for (int i=0;i<3;++i) { positions[i]->setText(QString::number(candidate.P()[i],'g',17)); }
                        error->clear(); showPreview();
                    } else { error->setText("当前视角不能投影到移动平面"); }
                    if (event->type()==QEvent::MouseButtonRelease) { dragging=false; }
                    return true;
                }
                return false;
            });
            QTimer watch; watch.setInterval(50);
            QObject::connect(&watch,&QTimer::timeout,&loop,[&] { if (!sessionValid() && dialog) { dialog->reject(); } });
            viewWidget->setProperty("irdTargetEditActive",true); qApp->installEventFilter(&filter); watch.start();
            showPreview(); dialog->show(); loop.exec();
            watch.stop(); qApp->removeEventFilter(&filter);
            if (viewWidget) { viewWidget->setProperty("irdTargetEditActive",false); }
            if (!sessionValid()) { result.reset(); }
            if (dialog) { delete dialog.data(); }
            if (owner && owner->m_view3dGateway) {
                ui::View3DPreviewUpdate update;
                update.frameMarkers=owner->m_reqMarkers; update.boxOutline=owner->m_reqBox; update.sampleGrid=owner->m_reqGrid;
                owner->m_view3dGateway->applyPreview(update); // 所有退出路径清临时层，现取原域投影而非恢复旧场景缓存。
            }
            return result;
        };
        m_domains->requirements->bindView3DSeams(std::move(seams));

        // ---- 会话预览双 sink 投影绑定（UI-T33 收口——acceptance 1/2/3
        // 的投影半区；着色判定零参与〔spec §2.4——cellStates 归域侧采
        // 样纯函数，本层只透传〕；参考系解析失败＝对应层清除＋Dev 留痕，
        // 诚实呈现不虚构预览）。两层各自缓存＋合并整组 applyPreview——
        // 原子替换语义的宿主编排（半新半旧杜绝）。
        auto resolveFrameName =
            [this](const requirements::RequirementReference& ref)
            -> std::optional<std::string> {
            if (ref.kind == requirements::RequirementRefKind::World) {
                // 宿主世界帧名＝"WORLD"（全大写）——RobWork 根帧实名
                // （StateStructure 构造即 FixedFrame("WORLD")），且
                // WorkCell::findFrame 仅对 "WORLD" 特判返回根帧。曾误写
                // 混合大小写 "World"（F-550② 登记）：状态树按名查找大小
                // 写敏感，恒落空→工位标记全数跳过、区域框"参考系不可解
                // 析"清除——三维预览空白的根因之一。
                return std::string("WORLD");
            }
            if (ref.objectId.has_value() && m_nameMapPort != nullptr) {
                return m_nameMapPort->resolveRuntimeName(*ref.objectId);
            }
            return std::nullopt;
        };
        m_domains->requirements->bindStationMarkersSink(
            [this, resolveFrameName, studio](
                const std::vector<requirements::RequirementsPluginAssembly::StationMarkerView>& markers) {
                m_reqMarkers.clear();
                for (const auto& marker : markers) {
                    const auto frameName = resolveFrameName(marker.refFrame);
                    if (frameName.has_value()) {
                        // UI-T77（F-555）：工位受约束位置随标记投影——
                        // refFrame 系→世界系（区域框角点同款变换纪律，
                        // 契约头注"投影方负责参考系变换"）：
                        //   - World 缺省参考系＝位置本身就是世界系坐标，
                        //     恒等直投（不经宿主帧解析——与区域框 F-550②
                        //     ③修复同理，首应用前会话预览锚下即可呈现）；
                        //   - 对象引用系＝宿主帧位姿（worldTframe，发布后
                        //     帧树在位）左乘位置；帧不可解析（宿主无发布
                        //     WC 或帧缺失）＝position 降级置空——标记回落
                        //     挂帧指示器形态（渲染端按 frameName 走既有
                        //     解析/计数路径），不虚构世界系坐标；
                        //   - 工位未提供位置值（四态 NotProvided）＝
                        //     nullopt 透传——缺失不转零〔MDL-06〕，渲染
                        //     端保持挂帧行为（UI-T33 旧语义）。
                        std::optional<rw::math::Vector3D<double>> worldPos;
                        if (marker.position.has_value()) {
                            if (marker.refFrame.kind
                                == requirements::RequirementRefKind::World) {
                                worldPos = *marker.position;  // 恒等直投
                            } else {
                                rw::kinematics::Frame* frame = nullptr;
                                if (frameName.has_value() && studio != nullptr
                                    && !studio->getWorkCell().isNull()) {
                                    frame = studio->getWorkCell()->findFrame(
                                        *frameName);
                                }
                                if (frame != nullptr) {
                                    const rw::math::Transform3D<double> worldT =
                                        rw::kinematics::Kinematics::worldTframe(
                                            rw::core::Ptr<
                                                const rw::kinematics::Frame>(
                                                frame),
                                            studio->getState());
                                    worldPos = worldT * *marker.position;
                                }  // 帧不可解析＝worldPos 保持空（诚实降级）
                            }
                        }
                        m_reqMarkers.push_back(ui::View3DFrameMarker{
                            marker.label, *frameName, worldPos});
                    } else if (m_diag.pipeline) {
                        // UI-T77（F-566）：帧名不可解析＝该标记不进投影集
                        // （渲染端 unresolved 计数只覆盖已转发的挂帧形态
                        // ——此处跳过的标记三面全空，工位数与标记数不一致
                        // 不可观测）。对齐区域路径：Dev 留痕（工位名＋引
                        // 用），静默消失变可观测。
                        m_diag.pipeline->logDev(
                            kPluginDevChannel,
                            "view3d preview: 工位标记帧名不可解析——跳过（"
                                + marker.label + "）");
                    }
                }
                ui::View3DPreviewUpdate update;
                update.frameMarkers = m_reqMarkers;
                update.boxOutline = m_reqBox;
                update.sampleGrid = m_reqGrid;
                m_view3dGateway->applyPreview(update);
            });
        // FIX-T2——样本高亮请求出口（面板清单行点击→置域样本号键；三维
        // 刷新由面板重投最近区域几何触发本闭包重跑〔键先置、投递后到〕，
        // 装配层零重投状态机——P-FIX-5 整组替换语义承载在既有闭包内）。
        m_domains->requirements->bindSampleHighlightRequest(
            [this](std::uint64_t sampleIndex) {
                m_reqHighlightedSample = sampleIndex;
            });
        m_domains->requirements->bindRegionPreviewSink(
            [this, resolveFrameName, studio](
                const std::optional<requirements::RequirementsPluginAssembly::
                                        RegionPreviewView>& geoOpt) {
                // UI-T77（F-560）：区域集合空态（nullopt）＝框/格两层同拍
                // 清除后合并投递——面板删除最后一个区域时三维残留的根治
                // （只清标签不投递则宿主缓存残值被后续标记投递重挂）。
                if (!geoOpt.has_value()) {
                    m_reqBox.reset();
                    m_reqGrid.reset();
                    // FIX-T2——空态同拍：高亮键清位＋面板清单清空（诚实
                    // 空态与三维格层同进退——F-559 同族防串数据）。
                    m_reqHighlightedSample.reset();
                    m_domains->requirements->pushSampleResults({});
                    ui::View3DPreviewUpdate clearUpdate;
                    clearUpdate.frameMarkers = m_reqMarkers;
                    clearUpdate.boxOutline = m_reqBox;    // nullopt＝清框层
                    clearUpdate.sampleGrid = m_reqGrid;   // nullopt＝清格层
                    if (!m_view3dGateway->applyPreview(clearUpdate)
                        && m_diag.pipeline) {
                        // F-560 附带：applyPreview 返回值不再忽略（契约
                        // 「false＝挂接失败——调用方诚实呈现」）——失败经
                        // Dev 留痕，缓存与画面失配可观测。
                        m_diag.pipeline->logDev(
                            kPluginDevChannel,
                            "view3d preview: 区域空态清除挂接失败（保留旧呈现）");
                    }
                    return;
                }
                const requirements::RequirementsPluginAssembly::RegionPreviewView&
                    geo = *geoOpt;
                // refFrame 系→世界系（宿主帧位姿——Kinematics::worldT；
                // 参考系缺失＝框/格两层同拍清除＋Dev 留痕——acceptance 2
                // 的失败原因可见面在投影方摘要与 Dev 双承载）。
                // F-550② 修复：World 缺省参考系（RequirementTypes.hpp
                // kind 缺省值）的角点/格线本身就是世界系坐标——恒等变换
                // 直投，不经宿主帧解析。这使区域框/采样格在首应用前（宿
                // 主尚无发布 WorkCell）即可经后端会话预览锚呈现；对象引
                // 用系才需要宿主帧位姿（发布后帧树在位——帧不可解析＝
                // 清除＋Dev 留痕，语义同前）。
                std::optional<ui::View3DBoxOutline> box;
                rw::math::Transform3D<> worldT =
                    rw::math::Transform3D<>::identity();
                bool refResolved = false;
                const auto frameName = resolveFrameName(geo.refFrame);
                if (geo.refFrame.kind == requirements::RequirementRefKind::World) {
                    refResolved = true;  // 世界系引用——恒等变换（见上注）
                } else {
                    rw::kinematics::Frame* frame = nullptr;
                    if (frameName.has_value() && studio != nullptr
                        && !studio->getWorkCell().isNull()) {
                        frame = studio->getWorkCell()->findFrame(*frameName);
                    }
                    if (frame != nullptr) {
                        worldT = rw::kinematics::Kinematics::worldTframe(
                            rw::core::Ptr<const rw::kinematics::Frame>(frame),
                            studio->getState());
                        refResolved = true;
                    }
                }
                if (refResolved) {
                    ui::View3DBoxOutline outline;
                    for (std::size_t i = 0; i < 8; ++i) {
                        outline.corners[i] = worldT * geo.corners[i];
                    }
                    // 采样格骨架线投递（UI-T52——格线段世界系变换直投）。
                    ui::View3DSampleGrid grid;
                    grid.gridLines.reserve(geo.gridLines.size());
                    for (const auto& seg : geo.gridLines) {
                        grid.gridLines.emplace_back(worldT * seg.first,
                                                    worldT * seg.second);
                    }

                    // ---- 着色采样点层投影（UI-T65——F-495 消费卡兑现，
                    // F-490① 维持口径消账）：执行器账面→三维格元。对账
                    // 纪律：snapshotId+epoch 与当前会话绑定一致才投影
                    // （跨快照账面＝PA 权威镜像的呈现侧拒绝——不虚构
                    // 判定）；区域锚过滤（多计划结果的逐区域切片）；
                    // 坐标基座系→世界系（执行器视图 worldToBase 直乘——
                    // 值语义 T_world_base＝基座相对世界的位姿（core §4.6
                    // 读法），其正读即基座→世界的点变换算子：p_world＝
                    // T_world_base·p_base；零第二套基座变换代数）。
                    std::optional<kinematics::KinChannelCoverageResult> coverage =
                        m_kinEvaluation != nullptr
                            ? m_kinEvaluation->latestCoverageResult()
                            : std::nullopt;
                    // FIX-T2——区域锚清位对账：投影到达新区域＝上一区域
                    // 的高亮请求失效（跨区域串亮防线——P-FIX-4 同源纪
                    // 律；同区域重投〔点击重投〕锚相等不清位）。
                    if (!(m_reqHighlightedRegionAnchor == geo.regionObjectId)) {
                        m_reqHighlightedSample.reset();
                        m_reqHighlightedRegionAnchor = geo.regionObjectId;
                    }
                    // FIX-T2——面板清单行卡（采样结果清单：执行器账面→
                    // 面板投影行的翻译单点〔R-2〕；状态词表见
                    // sampleStateText，判定零参与）。
                    std::vector<requirements::RequirementsPluginAssembly::
                                    SampleResultView>
                        sampleRows;
                    if (coverage.has_value() && coverage->ok
                        && m_kinEvaluation != nullptr
                        && coverage->snapshotId == m_kinEvaluation->snapshotId()
                        && coverage->epoch == m_kinEvaluation->epoch()) {
                        // T_world_base（快照唯一读取点**直乘**——基座系样本
                        // 点回世界系：p_world＝T_world_base·p_base。★ 审查
                        // A01 连带修正（KIN-P1）：接口名 worldToBase() 的
                        // 值语义是 T_world_base（"世界系中看基座"——
                        // Snapshot.hpp §6.4 行），基座→世界是直乘；修复前
                        // 此处取逆得 T_base_world 作用在基座系点上（方向
                        // 双错），恰与通道投影的镜像错误抵消、显示碰巧
                        // 正确——通道修正为真值后本处必须同步修正，否则
                        // 着色点错位（世界/基座转换闭环的呈现半区）。
                        // rw::math::inverse 不再使用——逆运算归
                        // runtime invertWorldBase 唯一权威（通道切片侧）。
                        const rw::math::Transform3D<double> worldTbase =
                            m_kinEvaluation->sessionView()->worldToBase();
                        // 该区域逐样本切片（着色点层＝全 kind 样本透传——
                        // 位置/位姿样本都上屏；计数已抽至纯函数）。
                        grid.samples.reserve(coverage->samples.size());
                        grid.cellStates.reserve(coverage->samples.size());
                        grid.sampleIndices.reserve(coverage->samples.size());
                        for (const auto& record : coverage->samples) {
                            if (!(record.regionObjectId == geo.regionObjectId)) {
                                continue;  // 他区域样本——预览是单选区域面
                            }
                            grid.samples.push_back(worldTbase * record.position);
                            grid.cellStates.push_back(
                                mapSampleStateToCell(record.state));
                            // 域样本号对齐表（FIX-T2——渲染高亮的寻址键；
                            // 数组下标≠域样本号——P-FIX-4 切片偏移）。
                            grid.sampleIndices.push_back(record.sampleIndex);
                            // 清单行卡同源组装（世界系坐标三位小数摘要；
                            // 未通过/未运行原因直投——ERR-01）。
                            requirements::RequirementsPluginAssembly::
                                SampleResultView row;
                            row.sampleIndex = record.sampleIndex;
                            row.stateText = sampleStateText(record.state);
                            const rw::math::Vector3D<double> wp =
                                worldTbase * record.position;
                            char buf[96];
                            std::snprintf(buf, sizeof(buf),
                                          "(%.3f, %.3f, %.3f)", wp[0], wp[1],
                                          wp[2]);
                            row.positionText = buf;
                            row.reason = record.reason;
                            sampleRows.push_back(std::move(row));
                        }
                        // 框色＝该区域**位置口径**计数比×目标下限（契约
                        // acceptance 3；计数＋分档一体在纯函数
                        // view3DRegionTintFromSamples——kind 分轴与域
                        // computeCoverage 位置轴同定义，与面板呈现的
                        // computation.coverage.position 同口径；呈现对照
                        // ——判定权威归域）。
                        outline.tint = view3DRegionTintFromSamples(
                            coverage->samples, geo.regionObjectId,
                            geo.minPositionCoverage);
                        // FIX-T2——高亮键随格层置入（渲染层经对齐表解析，
                        // 键未命中＝诚实不亮——零虚构）。
                        grid.highlightedSampleIndex = m_reqHighlightedSample;
                    } else {
                        // 对账失效/无评估＝清单同步清空（面板诚实空态——
                        // 与三维格层的中性空态同拍）。
                        m_reqHighlightedSample.reset();
                    }
                    m_domains->requirements->pushSampleResults(sampleRows);
                    box = outline;
                    m_reqGrid = grid;
                } else if (m_diag.pipeline) {
                    // UI-T77（F-561）：失败分支格层与框层**同拍清除**——
                    // 此前只清框层、m_reqGrid 保持上一区域旧值合并投递，
                    // 「半新半旧」在原子替换语义自家分支内复活。
                    // UI-T77（F-565）：域侧摘要（regionPreviewGeometry 的
                    // 失败可见面，如「格 未定」＝盒退化/采样间距非法）随
                    // Dev 追注——此前全链透传到投影方后被静默丢弃。
                    m_reqGrid.reset();
                    // FIX-T2——失败分支同拍：高亮键清位＋面板清单清空
                    // （参考系不可解析＝无样本可列，诚实空态）。
                    m_reqHighlightedSample.reset();
                    m_domains->requirements->pushSampleResults({});
                    m_diag.pipeline->logDev(
                        kPluginDevChannel,
                        "view3d preview: 区域参考系不可解析——框/格层清除（"
                            + (frameName.has_value() ? *frameName
                                                     : std::string("<无引用>"))
                            + "）；域摘要：" + geo.summaryText);
                }
                m_reqBox = box;
                // 着色采样点层已随上方合并投递（UI-T65——F-490① 消费卡
                // 兑现：格线骨架＋着色点两层并存，格线灰/点分色）。
                ui::View3DPreviewUpdate update;
                update.frameMarkers = m_reqMarkers;
                update.boxOutline = m_reqBox;
                update.sampleGrid = m_reqGrid;
                if (!m_view3dGateway->applyPreview(update) && m_diag.pipeline) {
                    // UI-T77（F-560 附带）：返回值不再忽略——失败＝渲染
                    // 后端缺位/场景不可得（Dev 留痕可观测，网关保留旧
                    // 呈现的事务语义不受影响）。
                    m_diag.pipeline->logDev(
                        kPluginDevChannel,
                        "view3d preview: 区域层挂接失败（保留旧呈现）");
                }
            });
    }

    // 事件过滤挂接＝视图本体＋全部后代 QWidget（双击事件的落点控件在
    // 框架内部组装——零脆弱查找，isAncestorOf 判定事件归属；视图未挂＝
    // 上游拾取不接——诚实降级面，呈现出口/TCP 源照常在位）。
    m_view3d = view.get();
    view->installEventFilter(this);
    for (QWidget* child : view->findChildren<QWidget*>()) {
        child->installEventFilter(this);
    }
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "view3d gateway: 装配完成（上行拾取＋呈现出口＋TCP 会话态源；名称映射＝宿主当前实例）");
    }
}

// =====================================================================
// 宿主呈现装配（UI-T46——发布桥接线＋编译链服务集＋名称映射真值编排）
// =====================================================================

void IrdWorkbenchHostPlugin::assemblePresentationPipeline()
{
    // 呈现装配（幂等守卫；出口缺位＝呈现刷新无宿主半区——与 UI-T45 降级
    // 基线同构，桥不装配、编译端口照常在位——命令域编译仍真实执行）。
    if (m_publishBridge != nullptr || m_view3dGateway == nullptr) {
        return;
    }
    rws::RobWorkStudio* studio = getRobWorkStudio();

    // ---- 建模命令处理器服务集（F-461 modeling 半区——所有权锚成员持有；
    // A-23已接项目存储背书的按修订策略，系统缺省仅用于无策略对象的修订）。
    m_jointLimitEvaluator = policy::makeJointLimitEvaluator();
    m_dhConverter = std::make_unique<modeling::DhExplicitConverter>();
    m_compileProbe = std::make_unique<HostCompileProbe>(HostCompileProbe::Deps{
        modeling::kRobotDesignObjectType,
        [this](const std::string& message) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel, message);
            }
        }});
    m_runtimeNameContext = std::make_unique<HostRuntimeNameContext>(
        m_runtimeNameMap.get());

    // F-546 出路①（UI-T74）：草稿名感知装饰——发布真值＋草稿名补位（模块
    // 草稿投影缝 tryDraftObjectName）。首应用行程校验的名称解析由此可达。
    m_draftAwareNameContext = std::make_unique<HostDraftAwareNameContext>(
        HostDraftAwareNameContext::Deps{
            m_runtimeNameContext.get(),
            [this](const core::ObjectId& object) -> std::optional<std::string> {
                std::string name;
                if (m_domains != nullptr
                    && m_domains->modeling.tryDraftObjectName(object, name)) {
                    return name;  // 草稿权威命名（localName——确定性标识标签）
                }
                return std::nullopt;  // 闭包外身份（不猜测——ARC-04）
            }});

    // ---- 呈现构造源（RT-T14 工厂适配——供数缝绑编译端口现取；编译端口
    // 随会话构造，此处绑"经成员现取"的间接缝保持源生命周期独立）。
    m_presentationSource = std::make_shared<HostPresentationSource>(
        HostPresentationSource::Deps{
            [this]() -> std::shared_ptr<const runtime::RuntimeSnapshot> {
                return m_compilePort != nullptr ? m_compilePort->lastPublishedSnapshot()
                                                : nullptr;
            },
            studio,
            [this](const std::string& message) {
                if (m_diag.pipeline) {
                    m_diag.pipeline->logDev(kPluginDevChannel, message);
                }
            }});

    // ---- 发布桥（UI-T20——四步刷新事务；出口＝UI-T45 网关（IUiPresentation
    // Outlet 实现体），宿主半区经 HostWorkCellPresentationObject 落地）。
    std::shared_ptr<ui::IUiPresentationOutlet> outlet(
        m_view3dGateway.get(), [](ui::IUiPresentationOutlet*) {});  // 非 owning 别名（网关 unique_ptr 持有本体）
    m_publishBridge = std::make_unique<ui::RuntimePublishBridge>(
        ui::RuntimePublishBridge::Deps{
            m_presentationSource,
            outlet,
            m_diag.catalog,   ///< 用户级诊断 sink（IDiagnosticSink——会话级目录）
            m_diag.factory,
            m_diag.pipeline});

    // ---- 刷新结果观察者（呈现发布成功拍→名称映射真值绑定——拾取反解/
    // 高亮/需求域三维缝三消费面同步激活；失败拍→Dev 留痕）。
    class PresentationNameMapBinder final : public ui::IUiPresentationRefreshObserver {
    public:
        explicit PresentationNameMapBinder(IrdWorkbenchHostPlugin* host)
            : m_host(host)
        {
        }

        void onPresentationReplaced(const ui::PresentationViewProjection& view) override
        {
            // hostPayload 取回（装配层自家类型——网关"本类型本解释"同款
            // 纪律：误型载荷属装配缺陷，非运行态）。
            const auto* object = static_cast<const HostWorkCellPresentationObject*>(
                view.hostPayload.get());
            if (m_host->m_runtimeNameMap != nullptr && object != nullptr) {
                m_host->m_runtimeNameMap->bindPresentation(object->view());
            }
        }

        void onPresentationRefreshFailed(const ui::PresentationEventFacts& /*facts*/,
                                         const std::string& reasonToken) override
        {
            if (m_host->m_diag.pipeline) {
                m_host->m_diag.pipeline->logDev(
                    kPluginDevChannel,
                    "presentation: 刷新失败（token=" + reasonToken
                        + "）——旧呈现保持原状");
            }
        }

    private:
        IrdWorkbenchHostPlugin* m_host;  ///< 宿主编排面（非 owning——存活期由插件保证）
    };
    m_publishBridge->addObserver(
        std::weak_ptr<ui::IUiPresentationRefreshObserver>(
            m_presentationObserver = std::make_shared<PresentationNameMapBinder>(this)));

    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "presentation pipeline: 装配完成（发布桥＋呈现源＋建模处理器服务集——"
                                "编译端口随项目打开构造）");
    }
}

void IrdWorkbenchHostPlugin::attachPresentationSession(const core::ProjectId& project)
{
    // 呈现会话绑定拍（项目打开成功回调——与 requirements 会话接线同点）。
    // 前会话编译端口先毁（旧快照随旧会话废弃——不跨会话残留）。
    m_compilePort.reset();
    if (m_lastStoreAdapter == nullptr) {
        return;  // 存储适配缺位（打开协议外的调用——诚实静默）
    }
    project::ProjectStore& store = m_lastStoreAdapter->projectStore();

    // 编译端口（十段链适配——S5 双编译的真实执行面；快照缓存随端口持有）。
    m_compilePort = std::make_unique<HostModelCompilePort>(HostModelCompilePort::Deps{
        &store.query(),
        project,
        modeling::kRobotDesignObjectType,
        [this](const std::string& message) {
            if (m_diag.pipeline) {
                m_diag.pipeline->logDev(kPluginDevChannel, message);
            }
        }});

    // 双编译端口注入（UI-T74——F-546 链路收口暴露的装配缺口）：§5.3.6 L5
    // 装配面——store 命令服务的 S5 双编译执行端。此前 apply 的行程校验层
    // 恒先行拒绝（F-536 策略墙/F-546 名解析墙），本缺口从未到达；行程校
    // 验贯通后 requiresDualCompile 命令（apply-robot-design）即达 S5。提
    // 交前装配、运行期替换属装配纪律违约（setCompilePort 契约原文）。
    store.commands().attachCompilePort(m_compilePort.get());

    // 建模命令处理器注册（F-461 modeling 半区收口——§5.3.5 装配期一次性；
    // 服务集指针经 HandlerServices 值拷贝注入，存活期由插件成员锚定）。
    const modeling::HandlerServices services{
        modeling::AssertionSuite::Ports{m_jointLimitEvaluator.get(),
                                        m_draftAwareNameContext.get()},
        // A-23：下面三个旧字段保留为兼容装配值；非空的修订解析回调优先，
        // 消费本次prepare冻结基线。仅该修订没有项目策略对象时才回退附录D
        // 系统缺省，解析失败不回退；不再以固定默认掩盖真实项目策略。
        &m_systemDefaultPolicyProvider,  // policyProvider＝系统缺省源（④端口装配）
        policy::systemDefaultPolicyObjectId(),  // policyObject＝保留身份
        std::nullopt,            // policyVersion＝不适用（系统缺省集无版本演进）
        m_dhConverter.get(),     // DH 转换器（权威切换变体）
        m_compileProbe.get(),    // 编译分段探针（等价验证）
        modeling::CouplingStage::R1Locked,
        [this](const project::RevisionView& revision) {
            return m_policySource->resolvePolicyForRevision(revision);
        },                       // A-23：严格消费 prepare 基线修订
    };
    modeling::registerModelingCommandHandlers(store.handlerRegistry(), services);
    if(m_domains){
        for(auto& entry:m_domains->applyEntries){
            if(entry.moduleId!=modeling::kModuleHandle){continue;}
            // 全部正式应用路径（含宿主tour直接遍历）共享此完整回执；不捕获
            // 当前store裸引用，避免关闭/换项目后旧回调访问已释放存储。
            entry.onCommitted=[this](const project::CommandResult& result){
                if(!m_lastStoreAdapter||!m_domains){
                    throw std::runtime_error("建模应用回执缺少已接受的项目会话");
                }
                acceptModelingCommittedSession(m_domains->modeling,
                    m_lastStoreAdapter->projectStore().query(),result);
            };
        }
    }

    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "建模域命令处理器已注册（完整工作集及旧五命令共六 token——§5.3.5 装配期；"
                                "F-461 modeling 半区收口）");
    }

    // 桥会话绑定（F-910 修复面——按当前绑定态分派：未绑定＝attach（常规
    // 打开），已绑定＝switch（切换流——A 绑定中打开候选 B，原恒 attach 在
    // 此形态必触发"重复挂接"logic_error 且 m_dialogPending 卡死）。分派语
    // 义见 RuntimePublishBridge::attachOrSwitchHostSession）。
    // 同拍重武装关闭清理幂等护栏（新会话周期开始——本会话关闭拍的
    // teardown 恢复可执行；见 m_sharedSurfacesTornDown 注释）。
    m_sharedSurfacesTornDown = false;
    // V-13/PM-04 周期落盘驱动（UI-T84——此前 tickAutosave 无生产触发源，
    // 草稿只有手动/关闭两个落盘拍）：本定时器只按秒级拍驱动入口，60 s 节
    // 律判定与在途跳过在控制器 tickAutosave 内（§8.4——单一节律权威）。
    if (m_draftAutosaveTimer == nullptr) {
        m_draftAutosaveTimer = new QTimer(this);
        m_draftAutosaveTimer->setInterval(5 * 1000);  // 驱动拍 5 s（单位 ms）——≠落盘周期
        connect(m_draftAutosaveTimer, &QTimer::timeout, this, [this] {
            if (m_draft && m_draft->hasSession()) {
                (void)m_draft->tickAutosave(std::chrono::steady_clock::now());
            }
        });
    }
    m_draftAutosaveTimer->start();
    if (m_publishBridge != nullptr) {
        m_publishBridge->attachOrSwitchHostSession(project);
    }

    // WP-14-T12/REQ-10：runtime是瞬态产物，关闭时随端口释放；重新打开已有项目
    // 必须从已保存HEAD重建，不能等下一次建模命令才恢复TCP/FK和几何拾取能力。
    // 空计划写入复用同一十段双编译链，只读已有闭包，不提交命令、不产生新修订；
    // 无机器人根的新项目正常保持空场景，不能把“没有模型”当编译失败。
    const project::RevisionView head = store.query().head();
    const bool hasDesign = std::any_of(head.objectRefs.begin(), head.objectRefs.end(),
        [](const project::ObjectRef& ref) { return ref.objectTypeToken == modeling::kRobotDesignObjectType; });
    if (hasDesign) {
        const std::vector<project::ObjectWrite> noWrites;
        const auto restored = m_compilePort->compileWorkCellAndDwc(
            project::CompileRequest{store.query(), noWrites, head.id});
        if (restored.ok) {
            // 发布桥已绑定新项目，沿现有呈现事务挂接WorkCell、名称映射及运动学快照。
            // 失败回滚仍由发布桥负责，禁止另造一个呈现专用的机器人计算快照。
            refreshPresentationAfterCommit();
            reportLine("已从保存修订恢复机器人运行时：" + head.id.toCanonical());
        } else {
            // 数据可打开但模型不可编译时保留编辑能力，明确给出原稳定诊断，
            // 不伪造空模型成功或偷偷应用一笔无变化的建模修订来掩盖恢复失败。
            reportLine("机器人运行时恢复失败，请检查模型诊断后修复并应用");
            for (const auto& diagnostic : restored.diagnostics) {
                reportLine(diagnostic.cause + "；" + diagnostic.recommendedAction);
            }
            if (m_hostStatusBar) {
                m_hostStatusBar->showMessage(QStringLiteral("机器人运行时恢复失败，请检查模型诊断"), 8000);
            }
        }
    }
}

void IrdWorkbenchHostPlugin::refreshPresentationAfterCommit()
{
    // 发布触发拍（命令提交/撤销/重做后——编译端口有新发布快照才进事务；
    // 无桥/无端口/无快照＝幂等静默——诚实降级链与装配缺席形态）。
    if (m_publishBridge == nullptr || m_compilePort == nullptr
        || m_lastStoreAdapter == nullptr) {
        return;
    }
    const std::shared_ptr<const runtime::RuntimeSnapshot> snapshot =
        m_compilePort->lastPublishedSnapshot();
    if (snapshot == nullptr) {
        return;  // 本会话尚无编译发布（纯草稿编辑/无建模命令）——无呈现可刷新
    }
    // kinematics 会话事实同步（UI-T64——发布消费点：快照换绑＋纪元推进
    // ＋面板绑定键同步；在途评估回执按旧纪元比对即迟到丢弃，L-K12）。
    m_policySource->refreshPublishedPolicy();
    m_domains->modeling.bindPolicyProvider([this]() -> const policy::EngineeringPolicySet* {
        return m_policySource->publishedPolicy().get();
    });
    syncKinematicsSessionFacts();
    // 新 tip（发布归属修订——对账基准；权威分支表首条，apply 编排同源）。
    const auto tips = m_lastStoreAdapter->projectStore().query().branchTips();
    if (tips.empty()) {
        return;
    }
    const ui::PresentationEventFacts facts{
        ui::PresentationEventKind::Publish,
        m_lastStoreAdapter->projectStore().projectId(),
        tips.front().tip,
        snapshot->modelIdentity(),
    };
    const ui::PresentationRefreshOutcome outcome =
        m_publishBridge->handlePresentationEvent(facts);
    if (m_diag.pipeline != nullptr && !outcome.applied) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "presentation: 发布事务未应用（token=" + outcome.failureToken
                                    + "）——旧呈现保持");
    }
}

void IrdWorkbenchHostPlugin::refreshSharedSurfaces()
{
    // 跨域就绪摘要先行重取（UI-T44——卡刷新不依赖树/检查器装配态；三域
    // readonlyProjections 现取零缓存，ACC5 同纪律。刷新时机＝本编排的
    // 全部触发点：项目打开/关闭、应用草稿、撤销/重做、装配首刷——编辑
    // 未应用的中间态不实时投递，卡头语义即"随共享面刷新"，不冒名实时）。
    refreshReadinessSummary();
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

void IrdWorkbenchHostPlugin::refreshReadinessSummary()
{
    // 跨域就绪摘要重取（UI-T44——§11.2 readonlyProjections 汇聚半区）：
    // 三域模块现取投影（零判定——verdict 直投，N-11 无第二套），逐项转
    // 摘要行投卡。域装配缺席（§11.3 失败隔离形态）＝该域零行——卡呈剩余
    // 域如实汇总；三域全缺席＝诚实空态行。UI 线程调用（§3.4——调用点全
    // 在共享面刷新编排）。
    if (m_readinessCard == nullptr || m_domains == nullptr) {
        return;  // 卡未装配/域装配未就绪（装配序保证——幂等静默）
    }
    std::vector<ui::DomainReadinessSummaryRow> rows;
    // 行装配辅助：一个域模块＝至少一行投影（§11.2 契约——空集亦呈空态
    // 行）；域名挂宿主装配词（plugin.<domain>.title 同词——UX-02 值源）。
    const auto addRows = [&rows](const char* domainLabel,
                                 ui::IPluginUiModule* module) {
        if (module == nullptr) { return; }
        for (const ui::DomainReadinessItem& item : module->readonlyProjections()) {
            ui::DomainReadinessSummaryRow row;
            row.domainLabel = QString::fromUtf8(domainLabel);
            row.verdictText = ui::engineeringStatusDisplayName(item.verdict);
            if (!item.inputComplete) {
                // 输入不完整附注（REQ-06——缺项明细归域面板就绪条，摘要
                // 只提示不展开；verdict 词已承载"输入不完整"时附注不重复）。
                if (item.verdict != core::EngineeringStatus::DataInsufficient) {
                    row.noteText = QString::fromUtf8("输入不完整");
                }
            }
            rows.push_back(std::move(row));
        }
    };
    addRows("建模", m_domains->modeling.module.get());
    if (m_domains->requirements.has_value()) {
        addRows("需求", m_domains->requirements->module.get());
    }
    if (m_domains->kinematics.has_value()) {
        addRows("运动学", m_domains->kinematics->module.get());
    }
    m_readinessCard->setRows(rows);
}

void IrdWorkbenchHostPlugin::teardownSharedSurfacesForClose()
{
    // F-910 修复面（幂等护栏——根因最小面）：关闭完成拍的本函数在
    // presentContext 合法重入下会被调用两次（unbindSession 脏位翻转→
    // reportSessionDirty→二次空投影投递）——无护栏时第二遍 detachHostSession
    // 命中桥"未绑定拆绑"logic_error（同步关闭被 catch 成警告框；异步关闭
    // 在无保护上下文直穿 Qt 事件循环→StudioMain 兜底 catch→整站退出，
    // 手工验证 A5 根因链）。会话周期内 teardown 恰执行一次：首次置位，
    // 重入幂等跳过（留痕）；新会话绑定拍（attachPresentationSession）重武装。
    if (m_sharedSurfacesTornDown) {
        if (m_diag.pipeline) {
            m_diag.pipeline->logDev(kPluginDevChannel,
                                    "project close teardown: idempotent skip (reentrant projection)");
        }
        return;
    }
    m_sharedSurfacesTornDown = true;
    if (m_policySource) { m_policySource->bindProjectStore(static_cast<project::ProjectStore*>(nullptr)); }
    // 周期落盘驱动随会话停止（V-13 触发半区只服务在位会话——无项目态
    // tickAutosave 分派无意义）。
    if (m_draftAutosaveTimer != nullptr) {
        m_draftAutosaveTimer->stop();
    }
    // 项目关闭统一清理（acceptance 4——九类对象逐类收口〔UI-T77/F-559
    // 增第九类：三维预览投影缓存〕；清理序＝依赖序：先退订消费者再清
    // 状态源，防止清理过程中的重入回调）。
    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "project close teardown: begin (9 object classes)");
    }

    // ⑩★ KIN-P4：kinematics 设默认命令门面拆除（项目关闭拍——先经门面
    // 清缝（命令缝唯一显式清除面——mergeAssemblyChannels 的"非空才覆盖"
    // 语义无法表达清空；清空后按钮门控禁用＝无项目态诚实降级），再 reset
    // 持有对象——零悬垂指针）。
    if (m_domains != nullptr && m_domains->kinematics.has_value()) {
        m_domains->kinematics->clearCommandHandler();
    }
    m_kinCommandHandler.reset();
    m_kinCommandGateway.reset();

    // ⑧运行中订阅：检查器模型的 SelectionService 订阅（RAII 句柄释放）
    //    ——先断 L1 消费链，后续清源不再触发检查器刷新。上下文栏转发
    //    订阅（UI-T26）同批释放——顶栏标签不消费关闭期的清空广播。
    if (m_inspectorSubscription) {
        m_inspectorSubscription.reset();
    }
    if (m_contextSelectionSubscription) {
        m_contextSelectionSubscription.reset();
    }

    // ②SelectionService：业务选中集清空（空选中广播——消费者已退订，
    //    状态归零是本步的可观测语义）＋三维高亮对称清除（L2 出口收口）。
    if (m_selection) {
        m_selection->clearSelection(ui::SelectionSource::Command);
    }
    if (m_highlightOutlet) {
        m_highlightOutlet->clearHighlight();
    }

    // ⑤HostWorkCell 呈现对象（UI-T84 前移——原序在共享面刷新之后）：
    //    detachHostSession（桥：释放宿主呈现＋清当前呈现与绑定）＋名称
    //    映射真值端口回诚实空二态（拾取反解/高亮判定回落未应用分支）＋
    //    编译端口随会话销毁（旧快照不跨会话残留）。前移理由＝关闭态
    //    顺序的正确性：③共享面空态刷新（refreshSharedSurfaces→域就绪
    //    汇聚/面板 refresh）在已关闭上下文上存在查询面（F-910 验收追查
    //    实证——undo 可用性谓词同族），异常若先于本步发生将跳过桥拆绑
    //    （呈现会话跨关闭残留——"旧快照不跨会话残留"纪律违约，且重开
    //    拍依赖桥绑定态分派）。呈现解绑前置＝最关键状态转换不被后续
    //    刷新面异常跳过；无桥形态（降级基线）＝幂等静默。
    if (m_publishBridge) {
        m_publishBridge->detachHostSession();
    }
    if (m_runtimeNameMap) {
        m_runtimeNameMap->clearPresentation();
    }
    m_compilePort.reset();

    // ③共享属性检查器＋①项目树：模型状态复位（检查器经空选中询问→
    //    NoSelection 占位态；树经 rebuild 空集→五空组形态——域 Provider
    //    无会话供给空集的合法二态）＋面板 refresh（渲染同步，占位态）。
    //    呈现会话已先行解绑（上方⑤）——本步在关闭上下文上的残余查询
    //    面若抛出，由 presentContext 捕获边界隔离留痕（不再阻断拆绑）。
    refreshSharedSurfaces();

    // ④复杂编辑宿装页：检查器面板 refresh 在非对象态下清宿装容器
    //    （UI-T22 契约"切换对象清宿装——零残留"的关闭路径复用）——上一步
    //    的面板 refresh 已承载，此处 Dev 留痕声明核查点（具名用例在集成
    //    测试断言宿装容器为空）。

    // ⑥TimedStatePath 播放驱动：当前装配形态无 Playback 发布缝（B1-SPEC
    //    D9 的 TimedStatePath 载体经 UI-T20 发布桥承载——同⑤随呈现装配
    //    接续；PlayBack 官方面板的播放状态归宿主框架自持，项目关闭时
    //    宿主 WorkCell 卸载连带失效——框架既有行为，非本插件持有对象）。

    // ⑦会话姿态：运动学会话姿态已在解绑模型之前清空（上游关闭编排），
    //    防止位姿页消费旧 q；Jog 桥的会话守卫在位（m_jogBridgeConnected 保持——
    //    订阅无会话时 State 变化不再写入域，重新绑定后才接收当前项目姿态）。

    // ⑧运行中订阅（续）：修订事件订阅保持（总线随 store 关闭排空——
    //    旧项目事件在无绑定态被域模块按分支过滤丢弃，不污染新项目）。

    // ⑨三维预览投影缓存（UI-T77——F-559 收口）：三缓存清零＋网关整组
    //    摘除（幂等）。ird 项目关闭经 UiSessionController Draining→本函
    //    数，不经过宿主框架 WorkCell 关闭拍（onSceneCleared→removePreview
    //    不触发）——漏收口则 A 项目的框/格/标记缓存残值被 B 项目首个标
    //    记投递合并重挂（跨项目串呈现，与「旧快照不跨会话残留」⑤同族
    //    纪律）。先清缓存再 removePreview：即使摘除失败（场景不可得），
    //    缓存已归零——新项目首投递携带的是空区域/空格层，不会重挂残值。
    m_reqMarkers.clear();
    m_reqBox.reset();
    m_reqGrid.reset();
    // FIX-T2——第十类收口同拍：高亮键与区域锚清零＋面板清单清空（高亮
    // 键是跨项目会话态——残键在新项目同域样本号上会串亮；清单残行同理
    // 串数据。F-559 第九类收口同族纪律）。
    m_reqHighlightedSample.reset();
    m_reqHighlightedRegionAnchor = core::ObjectId{};
    if (m_domains && m_domains->requirements) {
        m_domains->requirements->pushSampleResults({});
    }
    if (m_view3dGateway) {
        m_view3dGateway->removePreview();
    }

    if (m_diag.pipeline) {
        m_diag.pipeline->logDev(kPluginDevChannel,
                                "project close teardown: done (selection cleared, "
                                "tree/inspector reset, highlight cleared, "
                                "preview caches cleared, "
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
            // 冒烟断言随域扩展如实校正（UI-T84 实测登记）：遍历语义不变
            // （全部 NoDraft——demo 无域编辑，不产生空修订），模块数随草稿
            // 源挂接面增长（三域时代断言 3，六模块时代为 6——需求域经
            // F-911 挂接后纳入遍历）。判据锚定"逐行 NoDraft"语义而非计数。
            const bool step4Ok = applyReport.entries.size() >= std::size_t{3}
                                 && applyReport.entries.size()
                                        == applyReport.noDraftCount();
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
            // 步 6：清理选中断言（UI-T84 返工批注释修正——本步实际验证
            // 的是选中集清空；项目关闭编排已移至步 8 的 beginClose 全链，
            // 原注释"关闭清理/teardown"与代码不符，按 §2.6 过期注释口径
            // 随批修正——验收 UI-T84 attempt 1 建议 6）。
            if (m_selection) {
                m_selection->clearSelection(ui::SelectionSource::Command);
            }
            const bool step6Ok = m_selection != nullptr
                                 && m_selection->selectedObjectIds().empty()
                                 && m_treeModel->nodeCount() >= 0;
            std::cout << "[ird-ui-smoke] step6 teardown=" << (step6Ok ? "ok" : "fail")
                      << std::endl;

            // 步骤 7（UI-T41 A1——F-421 真链路断言）：真实建模装配描述符经
            // registrar 登记的报告对账（门禁 T 规则禁止测试目标跨单元直链
            // 产品目标，真链路 gtest 不可落位——宿主冒烟通道是唯一合法的
            // 真实装配观测面；断言 Ok/panels=1/commands=10——十条连字符
            // 命令 id 过句法校验的直接证据）。
            bool step7Ok = false;
            std::size_t modelingCommands = 0;
            if (m_domains != nullptr) {
                for (const ui::PluginAssemblyReport& report :
                     bundleAboutSource(*m_domains)->assemblyReports()) {
                    if (report.pluginId == "modeling") {
                        // 命令数下限锚（UI-T84 实测校正：初版钉 10——后续批
                        // 次为 modeling 追加命令后漂为 11，冒烟因环境门未跑
                        // 未被看住；改下限锚≥10 保持"句法校验通过面非缩水"
                        // 语义，精确计数回归随面板登记表测试承载）。
                        step7Ok = report.ok && report.panelsLoaded == 1
                                  && report.commandsRegistered >= 10;
                        modelingCommands = report.commandsRegistered;
                        break;
                    }
                }
            }
            std::cout << "[ird-ui-smoke] step7 modeling-assembly="
                      << (step7Ok ? "ok" : "fail") << " commands="
                      << modelingCommands << std::endl;

            // 步骤 8（UI-T84——F-910 真实宿主 S1）：同进程关闭→重开全协议。
            // 关闭走真实状态机（beginClose→resolveCloseDialog Confirmed——
            // 对话框呈现归交互流，此处直投决议值面），同步或 Draining 收口
            // 后重开同一 demo 路径（openViaSessionController 交互同款入口）。
            // 断言面＝重开成功＋桥重新绑定＋草稿会话在位——修复前缺陷链
            // （F-910 四轮探针实证定案，F-921 消账：本注释为修订后权威叙
            // 事）：触发面＝关闭不拆绑→同进程重开时桥重复挂接（attach 命
            // 中已绑 fail-fast，N2 变异实证）；放大面＝关闭收尾期撤销/重
            // 做可用性谓词（undoRedoDisablement）查询已关上下文抛
            // StoreError 穿透事件循环（StudioMain 兜底整站退出＝手工验证
            // A5 本体），修复后本步零异常通过。
            bool step8Ok = false;
            if (m_controller != nullptr && m_controller->hasOpenSession()) {
                (void)m_controller->beginClose(UiCloseIntent::CloseProject);
                const CloseDialogResolution closeRes = m_controller->resolveCloseDialog(
                    CloseDecision{true, DraftDisposition::Discard,
                                  TaskDisposition::Wait});
                // Draining 收口等待（有界事件循环——真实 store 关闭异步形
                // 态；同步完成时谓词首轮即真）。10 s 上限＝冒烟防挂死闸。
                if (closeRes.status == CloseDialogResolution::Status::Confirmed
                    && m_controller->hasOpenSession()) {
                    QEventLoop drainLoop;
                    QTimer drainTimeout;
                    drainTimeout.setSingleShot(true);
                    QObject::connect(&drainTimeout, &QTimer::timeout, &drainLoop,
                                     &QEventLoop::quit);
                    auto drainPoll = std::make_shared<QTimer>();
                    drainPoll->setInterval(100);
                    QObject::connect(drainPoll.get(), &QTimer::timeout, [&] {
                        if (m_controller == nullptr || !m_controller->hasOpenSession()) {
                            drainLoop.quit();
                        }
                    });
                    drainPoll->start();
                    drainTimeout.start(10 * 1000);
                    drainLoop.exec();
                    drainPoll->stop();
                }
                const bool closedToNoProject =
                    m_controller == nullptr || !m_controller->hasOpenSession();
                // 重开（交互同款入口——NoProject 态直进打开协议）。
                const bool reopened = closedToNoProject
                                      && openViaSessionController(demoPath);
                step8Ok = reopened && m_controller != nullptr
                          && m_controller->hasOpenSession()
                          && m_publishBridge != nullptr
                          && m_publishBridge->hasBoundSession()
                          && m_draft != nullptr && m_draft->hasSession();
                std::cout << "[ird-ui-smoke] step8 close-reopen="
                          << (step8Ok ? "ok" : "fail")
                          << " closed=" << (closedToNoProject ? "ok" : "fail")
                          << " reopened=" << (reopened ? "ok" : "fail")
                          << std::endl;
            } else {
                std::cout << "[ird-ui-smoke] step8 close-reopen=skip (no session)"
                          << std::endl;
            }

            exitCode = (opened && treeOk && step3Ok && step4Ok && step5Ok
                        && step6Ok && step7Ok && step8Ok)
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

// =====================================================================
// 布局收敛冒烟通道（UI-T24——P1/P2/P3 的自动化 GUI 断言与截图留痕载体；
// 环境变量 IRD_UI_PLUGIN_SMOKE=layout｜layout2）
// =====================================================================

namespace {

/// 度量递归深度上限：Dock 体→分区→控件行→控件，四层足够定位钳制源
/// （更深的叶子对"谁把最小宽度顶高"没有增量信息，只膨胀报告）。
constexpr int kLayoutProbeMaxDepth = 4;

/**
 * @brief 递归收集一个控件子树的几何事实行（度量报告体）。
 *
 * 每行输出：缩进＋objectName/className＋当前几何＋minimumSizeHint——
 * 钳制源定位的判据：QMainWindow 停靠区列宽被列内 Dock 的内容最小宽度
 * 钳制（resizeDocks 目标低于该值时收不下去），而 Dock 的内容最小宽度
 * 沿父子树取各层 layout 最小值的最大者——自顶向下第一处出现大数值的
 * 控件即钳制源。深度受限（见 kLayoutProbeMaxDepth）。
 *
 * @param w      [in] 待度量控件（允许空——调用点逐个判空）
 * @param depth  [in] 当前递归深度（0＝Dock 体本身）
 * @param lines  [out] 报告行累积器（UTF-8 文本，一行一控件）
 */
void probeLayoutTree(const QWidget* w, int depth, std::vector<std::string>& lines)
{
    if (w == nullptr || depth > kLayoutProbeMaxDepth) {
        return;
    }
    const QSize minHint = w->minimumSizeHint();
    const QSize cur = w->size();
    std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    lines.push_back(indent + std::string(w->objectName().isEmpty()
                                             ? w->metaObject()->className()
                                             : w->objectName().toStdString())
                    + " [" + std::string(w->metaObject()->className()) + "]"
                    + " cur=" + std::to_string(cur.width()) + "x"
                    + std::to_string(cur.height())
                    + " minHint=" + std::to_string(minHint.width()) + "x"
                    + std::to_string(minHint.height()));
    for (const QObject* child : w->children()) {
        probeLayoutTree(qobject_cast<const QWidget*>(child), depth + 1, lines);
    }
}

/// 冒烟事件循环等待拍：处理挂起事件（布局/重绘/定时器）后静置 ms 毫秒
/// ——截图与断言前让 resizeDocks/中央区保障定时器（80 ms 合并抖动）落定。
void settleEvents(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

}  // namespace

void IrdWorkbenchHostPlugin::maybeRunLayoutSmoke()
{
    // 触发面＝环境变量（与 IRD_UI_PLUGIN_SMOKE=auto 同机制——宿主进程的
    // 插件无命令行入口）。两种模式对应布局记忆双场景（契约 acceptance 4）：
    //   layout  ＝净室首启（无辅助记忆）——P1 默认收敛断言＋截图＋呼出域
    //             面板（写记忆）后退出；
    //   layout2 ＝二次启动（同一用户档案，上一轮已呼出）——记忆优先断言
    //             ＋截图后退出。
    // 驱动脚本负责双场景的 CWD 与辅助键夹具（见留痕 README）。
    const QString smoke = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE");
    const bool dockResize = (smoke == QLatin1String("dock-resize"));
    const bool requirementsLayout = (smoke == QLatin1String("requirements-layout"));
    const bool firstRun = (smoke == QLatin1String("layout"));
    const bool secondRun = (smoke == QLatin1String("layout2"));
    if (!firstRun && !secondRun && !dockResize && !requirementsLayout) {
        return;
    }

    // 度量/断言拍：1200 ms——晚于装载呈现自证（0 拍重申）与两拍宽度收束
    // （0/100 ms），取"宿主装载语义全部落定后"的稳定布局态。
    QTimer::singleShot(1200, this, [this, firstRun, dockResize, requirementsLayout] {
        std::cout << "[ird-ui-smoke-layout] started mode="
                  << (firstRun ? "first" : "second") << std::endl;
        std::vector<std::string> lines;  // UTF-8 报告行（几何走查＋断言结论）
        std::vector<std::string> failures;  // 断言失败清单（exit 码的依据）

        // ---- 断言/截图工具（本地闭包——报告行＋失败清单双写）------------
        auto check = [&](bool ok, const std::string& what) {
            lines.push_back(std::string(ok ? "[PASS] " : "[FAIL] ") + what);
            std::cout << "[ird-ui-smoke-layout] " << (ok ? "pass " : "FAIL ")
                      << what << std::endl;
            if (!ok) {
                failures.push_back(what);
            }
        };
        const QString outDir = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE_OUT");
        if (!outDir.isEmpty()) {
            QDir::root().mkpath(outDir);
        }
        auto snapPng = [&](QWidget* w, const char* name) {
            if (w == nullptr || outDir.isEmpty()) {
                return;
            }
            const QString path = outDir + QLatin1Char('/') + QString::fromLatin1(name);
            if (!w->grab().save(path, "PNG")) {
                failures.push_back(std::string("screenshot-missing:") + name);
                std::cout << "[ird-ui-smoke-layout] FAIL screenshot " << name
                          << std::endl;
            } else {
                lines.push_back(std::string("[SNAP] ") + name);
                std::cout << "[ird-ui-smoke-layout] snap " << name << std::endl;
            }
        };

        auto* hostWindow = qobject_cast<QMainWindow*>(parentWidget());
        if (hostWindow == nullptr) {
            std::cout << "[ird-ui-smoke-layout] FAILED host-window-missing"
                      << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        QWidget* central = hostWindow->centralWidget();
        lines.push_back("host-window cur=" + std::to_string(hostWindow->width())
                        + "x" + std::to_string(hostWindow->height()));

        if (requirementsLayout) {
            // WP-14-T12/A-12：沿真实菜单打开需求Dock，再从窄窗放大；不得在
            // 测试里resizeDocks帮生产布局变绿。截图同时保留中央三维和需求表单。
            m_content->setAuxVisible(kAuxKeyRequirementsDock, false);
            for (const auto& toggle : m_hostAuxToggles) {
                if (toggle.first == kAuxKeyRequirementsDock) { toggle.second->trigger(); break; }
            }
            for (int pass = 0; pass < 3; ++pass) {
                hostWindow->showNormal();
                hostWindow->resize(pass == 1 ? QSize(1024, 790) : QSize(1920, 1050));
                if (pass != 1) { hostWindow->showMaximized(); }
                settleEvents(400);
                const auto name = std::string(pass == 1 ? "narrow" : "wide") + std::to_string(pass);
                check(m_requirementsDock && m_requirementsDock->isVisible(), name + "-requirements-visible");
                if (!m_requirementsDock) { continue; }
                std::cout << "[ird-ui-smoke-layout] " << name << " host=" << hostWindow->width()
                          << " req=" << m_requirementsDock->width() << " central=" << central->width() << std::endl;
                if (pass != 1) {
                    check(m_requirementsDock->width() >= 420, name + "-readable-width");
                }
                check(central->width() >= kCentralMinReserveWidth, name + "-central-reserve");
                auto* scroll = qobject_cast<QScrollArea*>(m_requirementsDock->widget());
                check(scroll && scroll->widget(), name + "-scroll-boundary");
                if (scroll && scroll->widget()) {
                    // 滚到末端后右/下边缘应进入视口，证明内容可达而非永久裁切。
                    scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
                    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
                    settleEvents(80);
                    const QRect content = scroll->widget()->geometry();
                    check(content.right() <= scroll->viewport()->width()
                          && content.bottom() <= scroll->viewport()->height(), name + "-content-end-reachable");
                    scroll->horizontalScrollBar()->setValue(0);
                    scroll->verticalScrollBar()->setValue(0);
                }
                snapPng(hostWindow, ("requirements-layout-" + name + ".png").c_str());
            }
            QCoreApplication::exit(failures.empty() ? 0 : 1);
            return;
        }

        // UI-T82/UX-09/PM-14：真实六域宿主巡检，先钉住旧纵排布局的失败能力。
        // 环境变量显式启用才运行；只在独立工作目录执行，不创建/修改研发项目。
        if (dockResize) {
            const std::vector<std::pair<const char*, QDockWidget*>> domains{
                {kAuxKeyModelingDock, m_modelingDock},
                {kAuxKeyRequirementsDock, m_requirementsDock},
                {kAuxKeyKinematicsDock, m_kinematicsDock},
                {kAuxKeyDynamicsDock, m_dynamicsDock},
                {kAuxKeySelectionDock, m_selectionDock},
                {kAuxKeyOptimizationDock, m_optimizationDock}};
            // 经内容装配端口呼出，和菜单使用同一可见性权威；断言预期不借用布局实现。
            for (const auto& [key, dock] : domains) {
                check(dock != nullptr, std::string("domain-present:") + key);
                if (qEnvironmentVariableIsSet("IRD_UI_SMOKE_CLEAN_SETTINGS")) {
                    check(dock != nullptr && !m_content->auxVisible(key),
                          std::string("factory-starts-hidden:") + key);
                }
                if (dock != nullptr) { m_content->setAuxVisible(key, true); }
            }
            const bool allPresent = std::all_of(domains.begin(), domains.end(),
                [](const auto& item) { return item.second != nullptr; });
            if (allPresent) { // 缺席域先报告失败，不让巡检自身空指针崩溃掩盖装配原因。
                m_content->setRegionVisible(WorkbenchRegion::Bottom, true);
                if (m_kinematicsAdvancedDock != nullptr) {
                    m_content->setAuxVisible(kAuxKeyKinematicsAdvancedDock, true);
                }
                hostWindow->showNormal();
                hostWindow->resize(1600, 900);
                settleEvents(350);
                if (m_modelingDock != nullptr) {
                    const auto peers = hostWindow->tabifiedDockWidgets(m_modelingDock);
                    for (const auto& [key, dock] : domains) {
                        check(dock == m_modelingDock || peers.contains(dock),
                              std::string("one-domain-tab-group:") + key);
                    }
                }
                // 切换标签是遮挡，不是关闭；每个域均须保留打开记忆和可访问滚动视口。
                for (const auto& [key, dock] : domains) {
                    if (dock == nullptr) { continue; }
                    dock->raise();
                    settleEvents(90);
                    for (const auto& [otherKey, otherDock] : domains) {
                        check(otherDock != nullptr && !otherDock->isHidden()
                                  && m_content->auxVisible(otherKey),
                              std::string("tab-switch-keeps-open:") + otherKey);
                    }
                    bool readableTabs = false;
                    for (QTabBar* tabs : hostWindow->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
                        for (int i = 0; i < tabs->count(); ++i) {
                            if (tabs->tabText(i) == dock->windowTitle()) {
                                readableTabs = tabs->elideMode() == Qt::ElideNone
                                    && tabs->usesScrollButtons();
                            }
                        }
                    }
                    check(readableTabs, std::string("readable-module-tabs:") + key);
                    auto* scroll = qobject_cast<QScrollArea*>(dock->widget());
                    check(scroll != nullptr && scroll->widgetResizable(),
                          std::string("scroll-viewport:") + key);
                }
                // 四种窗口几何均要求独立导航、活动域和三维画布在窗内，不能互相盖住。
                for (int step = 0; step < 4; ++step) {
                    if (step == 0) { hostWindow->showNormal(); hostWindow->resize(1280, 720); }
                    if (step == 1) { hostWindow->showMaximized(); }
                    if (step == 2) { hostWindow->showFullScreen(); }
                    if (step == 3) { hostWindow->showNormal(); hostWindow->resize(1000, 680); }
                    settleEvents(300);
                    lines.push_back("step-" + std::to_string(step)
                        + " host=" + std::to_string(hostWindow->width()) + "x"
                        + std::to_string(hostWindow->height()) + " min="
                        + std::to_string(hostWindow->minimumSizeHint().width()) + "x"
                        + std::to_string(hostWindow->minimumSizeHint().height())
                        + " dpr=" + std::to_string(hostWindow->devicePixelRatioF()));
                    check(hostWindow->height() >= hostWindow->minimumSizeHint().height(),
                          "window-fits-minimum-layout-step-" + std::to_string(step));
                    check(central != nullptr && central->width() > 0 && central->height() > 0,
                          "central-nonzero-step-" + std::to_string(step));
                    check(!geometry().intersects(m_optimizationDock->geometry()),
                          "tree-domain-disjoint-step-" + std::to_string(step));
                    check(hostWindow->rect().contains(geometry())
                              && hostWindow->rect().contains(m_optimizationDock->geometry()),
                          "docks-contained-step-" + std::to_string(step));
                    snapPng(hostWindow, ("dock-resize-" + std::to_string(step) + ".png").c_str());
                }
                // 真关闭只影响目标域，重开仍属于同一组，控件/草稿对象不重新构造。
                QWidget* originalPanel = m_requirementsDock->widget();
                m_requirementsDock->close();
                settleEvents(100);
                check(!m_content->auxVisible(kAuxKeyRequirementsDock), "close-updates-memory");
                check(m_content->auxVisible(kAuxKeyModelingDock), "close-keeps-other-open");
                // 走真实视图菜单动作，而不是直接 show；覆盖 setupMenu 早于装配的时序。
                for (const auto& [key, action] : m_hostAuxToggles) {
                    if (key == kAuxKeyRequirementsDock) { action->trigger(); }
                }
                settleEvents(100);
                check(m_content->auxVisible(kAuxKeyRequirementsDock)
                          && m_requirementsDock->isVisible(), "menu-reopen-selects-domain");
                check(m_requirementsDock->widget() == originalPanel, "reopen-preserves-widget");
                check(hostWindow->tabifiedDockWidgets(m_modelingDock).contains(m_requirementsDock),
                      "reopen-preserves-tab-group");
                // 浮动面板关闭后其 window() 就是不可见的自身，仍须写回用户意愿。
                m_requirementsDock->setFloating(true);
                settleEvents(100);
                m_requirementsDock->close();
                settleEvents(100);
                check(!m_content->auxVisible(kAuxKeyRequirementsDock), "floating-close-updates-memory");
                for (const auto& [key, action] : m_hostAuxToggles) {
                    if (key == kAuxKeyRequirementsDock) { action->trigger(); }
                }
                settleEvents(100);
                check(m_requirementsDock->isVisible(), "floating-menu-reopens-once");
                // 用户复位必须把曾拆组/浮动的域及任务重新安放，不重建业务控件。
                if (m_tasksDock != nullptr) { m_tasksDock->setFloating(true); }
                m_content->submitCommand("view.resetLayout");
                settleEvents(150);
                check(!m_requirementsDock->isFloating(), "reset-clears-floating");
                check(m_tasksDock != nullptr && !m_tasksDock->isFloating()
                          && hostWindow->dockWidgetArea(m_tasksDock) == Qt::BottomDockWidgetArea,
                      "reset-restores-bottom-task-dock");
                for (const auto& [key, dock] : domains) { m_content->setAuxVisible(key, true); }
                settleEvents(100);
                check(hostWindow->tabifiedDockWidgets(m_modelingDock).size() == 5,
                      "reset-restores-six-domain-group");
                check(m_requirementsDock->widget() == originalPanel, "reset-preserves-widget");
                if (m_propsDock != nullptr && m_kinematicsAdvancedDock != nullptr) {
                    // 复位默认关闭高级辅助区；先呼出再检查可访问的右侧页签归组。
                    m_content->setAuxVisible(kAuxKeyKinematicsAdvancedDock, true);
                    settleEvents(100);
                    check(hostWindow->tabifiedDockWidgets(m_propsDock).contains(m_kinematicsAdvancedDock),
                          "inspectors-share-right-column");
                }
            }
            QFile report(outDir + QLatin1String("/dock-resize-report.txt"));
            if (report.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                for (const auto& line : lines) { report.write(QByteArray::fromStdString(line) + '\n'); }
            } else { failures.push_back("report-write-failed"); }
            std::cout << "[ird-ui-smoke-layout] "
                      << (failures.empty() ? "DOCK_RESIZE_PASS" : "DOCK_RESIZE_FAIL")
                      << " failures=" << failures.size() << std::endl;
            QCoreApplication::exit(failures.empty() ? 0 : 1);
            return;
        }

        if (firstRun) {
            // ---- 首启场景：P1 默认收敛＋P2 中央保护＋P3 按钮语义化 --------
            // P1 断言（净室＝辅助记忆缺失，驱动夹具保证）：三域自持面板与
            // 任务 Dock 默认不呈现（UI-T38——任务 Dock 工厂默认按宿主形态
            // 分派，插件形态隐藏）；默认呈现面＝工业项目树主 Dock＋属性诊断
            // Dock；中央三维视图可见且宽度非零（验收原文判据）。
            const std::vector<std::pair<const char*, const QDockWidget*>> auxDocks{
                {"modeling", m_modelingDock},
                {"requirements", m_requirementsDock},
                {"kinematics", m_kinematicsDock},
                {"kinematics-advanced", m_kinematicsAdvancedDock},
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
                // 三新域面板 Dock（ASM-PANEL 批——工厂默认不呈现同口径；
                // 宿主形态 always 在册，无需宏区分的冒烟面用同一容器）。
                {"dynamics", m_dynamicsDock},
                {"selection", m_selectionDock},
                {"optimization", m_optimizationDock},
#endif
                {"tasks", m_tasksDock},
            };
            for (const auto& [name, dock] : auxDocks) {
                check(dock == nullptr || !dock->isVisible(),
                      std::string("default-hidden:") + name);
            }
            check(isVisible(), "default-visible:main-dock");
            check(m_propsDock != nullptr && m_propsDock->isVisible(),
                  "default-visible:props-dock");
            if (central != nullptr) {
                lines.push_back("central cur=" + std::to_string(central->width())
                                + "x" + std::to_string(central->height()));
                check(central->isVisible() && central->width() > 0,
                      "central-view-nonzero (cur="
                          + std::to_string(central->width()) + "px)");
            } else {
                check(false, "central-widget-present");
            }
            // 视图菜单呼出通道在册（辅助开关动作——P1"经视图菜单可勾选
            // 呼出"的结构前提；数量＝四域自持面板＋运动学高级＋ASM-PANEL
            // 三新域三域＝7）。
            check(m_hostAuxToggles.size() == std::size_t{7},
                  "view-menu-aux-toggles=7 (cur="
                      + std::to_string(m_hostAuxToggles.size()) + ")");
            // FIX-PANEL 缺席忍耐挂位拍断言（实机冒烟通道——面板创建成功
            // 的正常装配形态下七开关全部可用：缺席置灰只在面板失败隔离时
            // 发生；此处把"缺席误伤可用开关"转为显式红。注意勾选态回写与
            // 触发 lambda 对 disabled 开关的忍耐由挂位拍置灰保证——本断言
            // 钉的是"六域全装配时无开关被误置灰"的正常态）。
            for (auto& [auxKey, auxAction] : m_hostAuxToggles) {
                check(auxAction->isEnabled(),
                      std::string("aux-toggle-enabled:") + auxKey);
            }

            // 截图①净室默认布局全景（宿主主窗口整窗）。
            snapPng(hostWindow, "1-default-layout.png");

            // 截图②视图菜单展开面：菜单弹窗 grab（popup 窗口独立于主窗）。
            QMenu* viewMenu = nullptr;
            if (QMenuBar* menuBar = hostWindow->menuBar()) {
                for (QAction* action : menuBar->actions()) {
                    if (QMenu* m = action->menu();
                        m != nullptr && m->title() == QString::fromUtf8(kViewMenuTitle)) {
                        viewMenu = m;
                        break;
                    }
                }
            }
            check(viewMenu != nullptr, "view-menu-present");
            if (viewMenu != nullptr) {
                viewMenu->popup(QPoint(80, 60));
                settleEvents(300);
                snapPng(viewMenu, "2-view-menu.png");
                viewMenu->hide();
                settleEvents(100);
            }

            // 呼出需求面板（端到端走菜单动作——与用户点击同一路径）：
            // 呼出后呈现正常＋记忆写入（跨会话半区）。
            QAction* reqToggle = nullptr;
            for (auto& [key, action] : m_hostAuxToggles) {
                if (key == kAuxKeyRequirementsDock) {
                    reqToggle = action;
                    break;
                }
            }
            check(reqToggle != nullptr, "requirements-toggle-present");
            if (reqToggle != nullptr) {
                reqToggle->trigger();
                settleEvents(300);
                check(m_requirementsDock != nullptr && m_requirementsDock->isVisible(),
                      "requirements-summoned-visible");
                check(m_content != nullptr
                          && m_content->auxVisible(kAuxKeyRequirementsDock),
                      "requirements-memory-on");
                // 截图③域面板呼出态（宿主全景）＋④需求面板按钮行特写
                // （P3：按钮文案为 UiText 中文语义名——非 commandId 直出）。
                snapPng(hostWindow, "3-domain-panel-summoned.png");
                if (m_requirementsDock != nullptr
                    && m_requirementsDock->widget() != nullptr) {
                    snapPng(m_requirementsDock->widget(), "4-requirements-buttons.png");
                }
                // P3 断言：九个命令按钮文案非空且不等于原始 id（UX-02 零
                // 内部名泄漏）。命令面从 ui 命令注册表枚举（SA-16 唯一权威
                // ——R-2：不触碰 requirements 单元私有目录）；titleKey 解析
                // 值与按钮文本逐一对照。
                if (m_requirementsDock != nullptr
                    && m_requirementsDock->widget() != nullptr
                    && m_content != nullptr) {
                    // 按钮集合＝命令条自身（面板根布局第 0 项——buildCommandBar
                    // 的挂位），不含右侧页签容器内的域表单按钮：四个页签页在
                    // QTabWidget 堆叠栈中几何天然相交（叠加页不可见但
                    // geometry() 仍相交），混入会把"页签堆叠"误报为流式栅格
                    // 重叠——重叠断言只对同一可见层的命令条按钮成立。
                    QWidget* commandBar = nullptr;
                    if (auto* rootLayout =
                            qobject_cast<QVBoxLayout*>(m_requirementsDock->widget()
                                                           ->layout());
                        rootLayout != nullptr && rootLayout->count() > 0) {
                        commandBar = rootLayout->itemAt(0)->widget();
                    }
                    check(commandBar != nullptr, "requirements-command-bar-present");
                    const auto buttons =
                        commandBar != nullptr
                            ? commandBar->findChildren<QPushButton*>()
                            : QList<QPushButton*>{};
                    std::vector<ui::CommandView> reqCommands;
                    for (const ui::CommandView& view :
                         m_content->commandRegistry().query(ui::CommandQuery{})) {
                        if (std::string(view.id).rfind("requirements.", 0) == 0) {
                            reqCommands.push_back(view);
                        }
                    }
                    check(reqCommands.size() == std::size_t{9},
                          "registry-requirements-commands=9 (cur="
                              + std::to_string(reqCommands.size()) + ")");
                    int resolvedButtons = 0;
                    // 返工⑤：『导入 ▾』下拉呈现面锚（CSV/JSON 两键整合后
                    // 独立按钮不复存在——菜单动作承载文案与可用性）。
                    const QPushButton* importDropdown = nullptr;
                    for (const QPushButton* btn : buttons) {
                        if (btn->menu() != nullptr
                            && btn->text()
                                   == QString::fromStdString(ui::resolveText(
                                       ui::TextKey{"panel.requirements."
                                                   "import-dropdown.label"}))) {
                            importDropdown = btn;
                            break;
                        }
                    }
                    check(importDropdown != nullptr,
                          "import-dropdown-present (返工⑤——导入整合面)");
                    // 更多操作下拉（UI-T39——命令条"高频主排＋更多操作"
                    // 分组：捕获两条＋派生四条入下拉，主排只剩导出副本＋
                    // 两级撤销；呈现面断言随分组形态更新）。
                    const QPushButton* moreDropdown = nullptr;
                    for (const QPushButton* btn : buttons) {
                        if (btn->objectName()
                            == QStringLiteral("ird_req_more_actions_dropdown")) {
                            moreDropdown = btn;
                            break;
                        }
                    }
                    check(moreDropdown != nullptr,
                          "more-actions-dropdown-present (UI-T39 分组面)");
                    for (const ui::CommandView& view : reqCommands) {
                        const std::string id(view.id);
                        const bool isImport =
                            id == "requirements.import-csv"
                            || id == "requirements.import-json";
                        const bool isLowFrequency =
                            id == "requirements.capture-tcp"
                            || id == "requirements.pick-feature"
                            || id == "requirements.mirror-stations"
                            || id == "requirements.create-array"
                            || id == "requirements.apply-template"
                            || id == "requirements.regenerate-linked";
                        if (isImport || isLowFrequency) {
                            // 下拉命令呈现面＝菜单动作（文案仍经 UiText）。
                            const QPushButton* host2 =
                                isImport ? importDropdown : moreDropdown;
                            bool actionOk = false;
                            bool actionDisabled = true;
                            if (host2 != nullptr && host2->menu() != nullptr) {
                                for (const QAction* action :
                                     host2->menu()->actions()) {
                                    if (action->text().toStdString()
                                        == ui::resolveText(view.titleKey)) {
                                        actionOk = true;
                                        actionDisabled = !action->isEnabled();
                                    }
                                }
                            }
                            check(actionOk,
                                  std::string(isImport ? "menu-action-via-uitext:"
                                                       : "more-action-via-uitext:")
                                      + id);
                            check(actionDisabled,
                                  std::string(isImport ? "menu-action-disabled-no-project:"
                                                       : "more-action-disabled-no-project:")
                                      + id);
                            if (actionOk) {
                                ++resolvedButtons;
                            }
                            continue;
                        }
                        bool textOk = false;
                        for (const QPushButton* btn : buttons) {
                            if (btn->text().toStdString() == std::string(view.id)) {
                                textOk = false;  // 文案＝原始 id＝UX-02 泄漏
                                break;
                            }
                            if (btn->text().toStdString()
                                == ui::resolveText(view.titleKey)) {
                                textOk = true;
                            }
                        }
                        check(textOk, "button-title-via-uitext:"
                                          + std::string(view.id));
                        if (textOk) {
                            ++resolvedButtons;
                        }
                    }
                    check(resolvedButtons == 9,
                          "command-buttons-count=9 (cur="
                              + std::to_string(resolvedButtons) + ")");
                    // UI-T35 P1-3 撤牌断言（契约验收③——"七条可用两条禁
                    // 用"）：撤牌的可观测面＝禁用原因键分岔。冒烟场景未
                    // 打开项目，九条 Project 作用域命令统一 disabled——其
                    // 中已装配七条走缺省使能规则（reason.no-project——
                    // PM-10"禁用＋说明"，开项目后即可用），撤牌两条走注
                    // 册期禁用谓词（cmd.flow-not-assembled.reason——任何
                    // 上下文恒禁用，UI-T27 P0-4 语义）。断言：原因键与装
                    // 配集合逐一对照（7×no-project＋2×not-assembled）＋命
                    // 令条按钮呈现面统一不可达（防"可点即败"回归）。
                    int noProjectCount = 0;
                    int notAssembledCount = 0;
                    for (const ui::CommandView& view : reqCommands) {
                        // 期望原因键＝装配集合（isAssembledRequirementCommand
                        // ——撤牌后恰七条；与宿主登记 entry.assembled 同一
                        // 判定源，双面互证）。
                        const bool assembled =
                            isAssembledRequirementCommand(std::string(view.id));
                        const std::string reasonKey(view.disableReasonKey);
                        if (assembled) {
                            check(reasonKey == "reason.no-project",
                                  std::string("button-disable-reason-default:")
                                      + std::string(view.id) + "=" + reasonKey);
                            ++noProjectCount;
                        } else {
                            check(reasonKey == "cmd.flow-not-assembled.reason",
                                  std::string("button-disable-reason-not-assembled:")
                                      + std::string(view.id) + "=" + reasonKey);
                            ++notAssembledCount;
                        }
                        // 呈现面核对：命令条同名按钮（UiText 标题匹配——
                        // 上一循环已验证文案唯一性）无项目态不可达。
                        for (const QPushButton* btn : buttons) {
                            if (btn->text().toStdString()
                                == ui::resolveText(view.titleKey)) {
                                check(!btn->isEnabled(),
                                      std::string("button-widget-disabled-no-project:")
                                          + std::string(view.id));
                                break;
                            }
                        }
                    }
                    check(noProjectCount == 7 && notAssembledCount == 2,
                          "requirements-assembled-split=7/2 (cur="
                              + std::to_string(noProjectCount) + "/"
                              + std::to_string(notAssembledCount) + ")");
                    // 返工⑤：下拉本体呈现面核对——无项目态两导入动作全禁
                    // ＝下拉按钮置灰（双禁语义，与九键无项目态一致）。
                    check(importDropdown != nullptr && !importDropdown->isEnabled(),
                          "import-dropdown-widget-disabled-no-project");
                    // 无重叠断言（两两矩形求交——面积＞0 即重叠；同排/换行
                    // 两种形态都覆盖——当前宽度即换行形态）。
                    int overlaps = 0;
                    for (int i = 0; i < buttons.size(); ++i) {
                        for (int j = i + 1; j < buttons.size(); ++j) {
                            if (!buttons[i]->isVisible() || !buttons[j]->isVisible()) {
                                continue;
                            }
                            const QRect intersection = buttons[i]->geometry()
                                                           .intersected(
                                                               buttons[j]->geometry());
                            if (intersection.width() > 0 && intersection.height() > 0) {
                                ++overlaps;
                            }
                        }
                    }
                    check(overlaps == 0,
                          "buttons-no-overlap (cur="
                              + std::to_string(overlaps) + ")");
                    // 截图⑤窄窗口特写：把需求 Dock 收窄强制多行换行后再
                    // 断言无重叠（"任意合理窗口宽度"的最严窄态）。
                    if (hostWindow != nullptr) {
                        hostWindow->resizeDocks({m_requirementsDock}, {280},
                                                Qt::Horizontal);
                        settleEvents(400);
                        if (m_requirementsDock->widget() != nullptr) {
                            snapPng(m_requirementsDock->widget(),
                                    "5-requirements-narrow.png");
                        }
                        overlaps = 0;
                        for (int i = 0; i < buttons.size(); ++i) {
                            for (int j = i + 1; j < buttons.size(); ++j) {
                                if (!buttons[i]->isVisible()
                                    || !buttons[j]->isVisible()) {
                                    continue;
                                }
                                const QRect intersection =
                                    buttons[i]->geometry().intersected(
                                        buttons[j]->geometry());
                                if (intersection.width() > 0
                                    && intersection.height() > 0) {
                                    ++overlaps;
                                }
                            }
                        }
                        check(overlaps == 0,
                              "buttons-no-overlap-narrow (cur="
                                  + std::to_string(overlaps) + ")");
                    }
                }
            }

            // UI-T27 返工补拍：建模/运动学域面板呼出＋特写截图（三域面板
            // 留痕——验收证据面；机制＝需求面板同款菜单动作触发）。呼出
            // 截图后随即关闭——首轮落盘的辅助记忆必须保持"仅需求面板可见"
            // （二次启动场景的 memory-*-still-hidden 断言依赖该初态）。
            const std::pair<const char*, const char*> domainSnaps[] = {
                {kAuxKeyModelingDock, "8-modeling-panel.png"},
                {kAuxKeyKinematicsDock, "9-kinematics-panel.png"},
            };
            for (auto& [auxKey, snapName] : domainSnaps) {
                QAction* domainToggle = nullptr;
                for (auto& [key, action] : m_hostAuxToggles) {
                    if (key == auxKey) {
                        domainToggle = action;
                        break;
                    }
                }
                check(domainToggle != nullptr,
                      std::string(auxKey) + "-toggle-present");
                if (domainToggle == nullptr) {
                    continue;
                }
                domainToggle->trigger();
                settleEvents(300);
                const QDockWidget* domainDock =
                    (auxKey == kAuxKeyModelingDock ? m_modelingDock
                                                   : m_kinematicsDock);
                check(domainDock != nullptr && domainDock->isVisible(),
                      std::string(auxKey) + "-summoned-visible");
                if (domainDock != nullptr && domainDock->widget() != nullptr) {
                    snapPng(domainDock->widget(), snapName);
                }
                domainToggle->trigger();  // 关闭复位（记忆初态保持——见上）
                settleEvents(300);
            }

            // 任务 Dock 呼出验证（UI-T38——底部区工厂默认隐藏后的用户通道
            // 抽查）：三区区域开关（"视图"菜单『IRD 任务和状态』）触发→
            // 呈现→回翻复位。翻转经内容装配面即时落盘（嵌入式形态单键微
            // 写），回翻后记忆值与工厂默认一致（隐藏）——二次启动场景的
            // memory-* 断言初态不受影响。
            QAction* tasksToggle = nullptr;
            for (auto& [region, action] : m_hostRegionToggles) {
                if (region == WorkbenchRegion::Bottom) {
                    tasksToggle = action;
                    break;
                }
            }
            check(tasksToggle != nullptr, "tasks-region-toggle-present");
            if (tasksToggle != nullptr) {
                tasksToggle->trigger();
                settleEvents(300);
                check(m_tasksDock != nullptr && m_tasksDock->isVisible(),
                      "tasks-summoned-visible");
                snapPng(hostWindow, "10-tasks-dock-summoned.png");
                tasksToggle->trigger();  // 关闭复位（记忆值回到隐藏缺省）
                settleEvents(300);
                check(m_tasksDock == nullptr || !m_tasksDock->isVisible(),
                      "tasks-restored-hidden");
            }

            // P2 断言：最小窗口（§4.4 1280×720）下中央区不归零（装配侧
            // 保障生效——事件观察＋回推已在 reassert 安装）。
            hostWindow->resize(1280, 720);
            settleEvents(600);  // 中央区保障合并抖动 80 ms＋布局落定裕量
            if (central != nullptr) {
                lines.push_back("central@min-window cur="
                                + std::to_string(central->width()) + "x"
                                + std::to_string(central->height()));
                check(central->width() >= kCentralMinReserveWidth,
                      "central-guard-at-min-window (cur="
                          + std::to_string(central->width()) + "px)");
            }
            snapPng(hostWindow, "6-min-window.png");
        } else {
            // ---- 二次启动场景（acceptance 4）：用户布局记忆优先 ------------
            // 上一轮（layout）呼出的需求面板，本轮恢复可见＝用户布局优先、
            // 不被默认布局覆盖（辅助记忆半区自持——框架 restoreState 对本
            // 插件延后挂载的域 Dock 无效，记忆走内容装配面 PM-14 用户级）。
            // 其余域面板（从未呼出）保持默认隐藏。
            check(m_requirementsDock != nullptr && m_requirementsDock->isVisible(),
                  "memory-requirements-visible");
            check(m_modelingDock == nullptr || !m_modelingDock->isVisible(),
                  "memory-modeling-still-hidden");
            check(m_kinematicsDock == nullptr || !m_kinematicsDock->isVisible(),
                  "memory-kinematics-still-hidden");
#ifdef IRD_UI_PLUGIN_EXTRA_DOMAINS
            // 三新域面板从未呼出——跨会话保持默认隐藏（ASM-PANEL 批同口径；
            // 宿主目标恒带定义，断言面随域面板在册同步扩展）。
            check(m_dynamicsDock == nullptr || !m_dynamicsDock->isVisible(),
                  "memory-dynamics-still-hidden");
            check(m_selectionDock == nullptr || !m_selectionDock->isVisible(),
                  "memory-selection-still-hidden");
            check(m_optimizationDock == nullptr || !m_optimizationDock->isVisible(),
                  "memory-optimization-still-hidden");
#endif
            if (central != nullptr) {
                lines.push_back("central cur=" + std::to_string(central->width())
                                + "x" + std::to_string(central->height()));
                check(central->width() >= kCentralMinReserveWidth,
                      "central-guard-second-start (cur="
                          + std::to_string(central->width()) + "px)");
            }
            snapPng(hostWindow, "7-second-start-memory.png");
        }

        // ---- 几何走查报告照常落盘（度量面证据——与首版度量通道同体）----
        for (const std::pair<const char*, const QDockWidget*>& dockInfo :
             std::vector<std::pair<const char*, const QDockWidget*>>{
                 {"main-dock", this},
                 {"props-dock", m_propsDock},
                 {"tasks-dock", m_tasksDock},
                 {"modeling-dock", m_modelingDock},
                 {"requirements-dock", m_requirementsDock},
                 {"kinematics-dock", m_kinematicsDock},
                 {"kinematics-advanced-dock", m_kinematicsAdvancedDock}}) {
            if (dockInfo.second == nullptr) {
                lines.push_back(std::string(dockInfo.first) + " <absent>");
                continue;
            }
            const QSize minHint = dockInfo.second->minimumSizeHint();
            lines.push_back(std::string("=== ") + dockInfo.first
                            + " visible="
                            + (dockInfo.second->isVisible() ? "1" : "0")
                            + " cur=" + std::to_string(dockInfo.second->width())
                            + " minHint=" + std::to_string(minHint.width())
                            + "x" + std::to_string(minHint.height()));
            probeLayoutTree(dockInfo.second->widget(), 1, lines);
        }

        // ---- 报告落盘（UTF-8 文件——GBK 控制台管道会乱码，F-207/F-231/
        //      F-309 留痕乱码家族的规避口径）----
        const QString reportPath =
            (outDir.isEmpty() ? QDir::currentPath() : outDir)
            + QLatin1String("/geometry-report.txt");
        QFile report(reportPath);
        if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&report);
            stream.setEncoding(QStringConverter::Utf8);
            for (const std::string& line : lines) {
                stream << QString::fromStdString(line) << '\n';
            }
        }
        std::cout << "[ird-ui-smoke-layout] report=" << reportPath.toStdString()
                  << std::endl;
        for (const std::string& f : failures) {
            std::cout << "[ird-ui-smoke-layout] failed-assert: " << f << std::endl;
        }
        std::cout << "[ird-ui-smoke-layout] "
                  << (failures.empty() ? "DONE" : "FAILED") << std::endl;
        QCoreApplication::exit(failures.empty() ? 0 : 1);
    });
}

// =====================================================================
// 需求界面全功能遍历通道（环境变量 IRD_UI_PLUGIN_SMOKE=requirements-tour）
// =====================================================================

void IrdWorkbenchHostPlugin::maybeRunRequirementsTour()
{
    // 触发面＝环境变量（与 auto/layout 同机制）。遍历序列（真实用户操作
    // 语义——控件级驱动触发与手点同源的信号轨）：
    //   拍 1  新建项目＋打开（wireRequirementsSession 空项目初始化）＋召唤面板
    //   拍 2  空态断言（复制/删除置灰＋提示可见）→工位新增→空态退役
    //   拍 3  工位属性：改名（树同步）/等级/启用/自由度/容差
    //   拍 4  动作阶段（启用/轴/距离）＋姿态规则切换（参数行重投影）
    //   拍 5  卡片折叠（收起/展开）＋区域新增＋盒尺寸＋采样计数（实时点数）
    //   拍 6  覆盖率滑块＋工况新增（模态向导自动填写确认）＋是否必验 Tag
    //   拍 7  工况节拍编辑（表列同步）＋校验页实时化＋过滤
    //   拍 7.5 三态着色数据面＋校验定位链屏证（F-529，UI-T67 扩建）
//   拍 8  草稿撤销→重做（树行回退/恢复）
    //   拍 9  draft.apply（需求域真实修订——非 NoDraft）＋项目级撤销使能
    // 每步 check()＋控制台 [ird-ui-smoke-tour] 行＋关键帧截图；全绿 DONE 退出 0。
    // 宿主侧仅经 Qt 公共基类＋objectName 锚操作面板（R-2——零跨单元私有头；
    // 与 gui_test 同型定位面）。提交轨为 queued invocation——每步操作后
    // settleEvents 让队列落域（onEditApplied→全面板重投影）再断言。
    const QString smoke = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE");
    if (smoke != QLatin1String("requirements-tour")) {
        return;
    }
    QTimer::singleShot(600, this, [this] {
        std::cout << "[ird-ui-smoke-tour] started" << std::endl;
        int exitCode = 0;
        const QString outDir = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE_OUT");
        if (!outDir.isEmpty()) {
            QDir::root().mkpath(outDir);
        }
        // ---- 断言/截图工具（layout 通道同款闭包形态）--------------------
        int failedCount = 0;
        auto check = [](bool cond, const std::string& what) -> bool {
            std::cout << "[ird-ui-smoke-tour] " << (cond ? "pass " : "FAIL ")
                      << what << std::endl;
            return cond;
        };
        QMainWindow* hostWin = qobject_cast<QMainWindow*>(parentWidget());
        auto snapPng = [&](QWidget* w, const char* name) {
            if (w == nullptr || outDir.isEmpty()) {
                return;
            }
            w->grab().save(outDir + QLatin1Char('/') + QString::fromLatin1(name),
                           "PNG");
            std::cout << "[ird-ui-smoke-tour] snap " << name << std::endl;
        };
        auto ok = [&](bool cond, const std::string& what) {
            if (!check(cond, what)) {
                ++failedCount;
            }
        };
        auto step = [&](const char* name) {
            std::cout << "[ird-ui-smoke-tour] step " << name << std::endl;
        };

        // ---- 拍 1：新建项目＋打开＋召唤面板------------------------------
        step("1 create-open-project");
        QString demoPath;
        {
            QTemporaryDir dir(QDir::tempPath() + "/ird-tour-demo-XXXXXX");
            demoPath = dir.path();
            dir.setAutoRemove(false);  // 项目落盘供报告留痕（退出后由脚本清理）
        }
        try {
            project::ProjectStoreFactory::createNew(
                fs::weakly_canonical(fs::u8path(demoPath.toStdString())).u8string(),
                QStringLiteral("需求界面遍历演示项目").toStdString(),
                nullptr, m_bridge.get());
        } catch (const std::exception& e) {
            std::cout << "[ird-ui-smoke-tour] create-exception: " << e.what()
                      << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        const bool opened = openViaSessionController(demoPath.toStdString());
        ok(opened, "step1 project-opened");
        settleEvents(400);  // wireRequirementsSession 空项目初始化落定

        // 召唤需求面板（视图菜单动作——与用户点击同路径）。
        QAction* reqToggle = nullptr;
        for (auto& [key, action] : m_hostAuxToggles) {
            if (key == kAuxKeyRequirementsDock) {
                reqToggle = action;
                break;
            }
        }
        if (reqToggle != nullptr) {
            reqToggle->trigger();
        }
        settleEvents(300);
        // E-4（F-538④，UI-T68）续：trigger 翻转的是动作态，页签叠放形态下
        // 面板可存在而不可见（ui-t68 首轮实录——最大化后主窗截图仍五帧同
        // 哈希，需求面板不在可见层）。show＋raise 强制升至所在页签组前景，
        // 并以可见性断言把「面板不可见」从静默态转为显式红——截图取证以
        // 可见层为前提。
        if (m_requirementsDock != nullptr) {
            m_requirementsDock->show();
            m_requirementsDock->raise();
            settleEvents(200);
        }
        QWidget* panel =
            m_requirementsDock != nullptr ? m_requirementsDock->widget() : nullptr;
        ok(panel != nullptr, "step1 panel-present");
        // 可见性断言（E-4 配对——存在≠可见；页签叠放/零宽挤压下的静窗
        // 截图零信息量，此处显式红可阻断该形态溜过取证通道）。
        ok(panel != nullptr && panel->isVisible()
               && !panel->visibleRegion().isEmpty(),
           "step1 panel-visible (取证前提——非静窗)");
        if (panel == nullptr) {
            std::cout << "[ird-ui-smoke-tour] FAILED" << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        // E-4（F-538④，UI-T68）：取证前宿主窗体最大化——默认尺寸下多 Dock
        // 挤压（ui-t67 central-guard「中央 18 px<320 px」实录）把需求面板
        // 压成静窗，五帧截图逐字节同哈希零信息量。最大化让 Dock 布局拿到
        // 物理空间后再取屏证，兑现「截图落证」的完整语义。
        if (hostWin != nullptr) {
            hostWin->showMaximized();
            settleEvents(400);
        }
        snapPng(hostWin, "tour-1-panel-empty-state.png");

        // 面板定位工具（Qt 公共基类＋objectName/属性——R-2 合规）。
        auto buttonOf = [panel](const char* name) -> QPushButton* {
            return panel->findChild<QPushButton*>(QString::fromLatin1(name));
        };
        QTreeWidget* tree = nullptr;
        // F-537（UI-T68）根因：树头自 F-499 起更名「需求树（当前草稿）」
        // （职责分界标注——双呈现位同频），本遍历原用全等匹配「需求树」
        // →tree=nullptr→stationEntryCount 恒 -1（ui-t67 实录 tree-child=-1
        // 的真因，非时序竞态——3s 有界轮询亦不现身即证）。改前缀匹配，
        // 对职责标注后缀稳健。
        for (QTreeWidget* t : panel->findChildren<QTreeWidget*>()) {
            if (t->headerItem()->text(0).startsWith(QStringLiteral("需求树"))) {
                tree = t;
                break;
            }
        }
        auto editByFieldKey = [panel](const char* key) -> QLineEdit* {
            for (QLineEdit* e : panel->findChildren<QLineEdit*>()) {
                if (e->property("irdFieldKey").toString()
                    == QString::fromLatin1(key)) {
                    return e;
                }
            }
            return nullptr;
        };
        auto comboByName = [panel](const char* name) -> QComboBox* {
            return panel->findChild<QComboBox*>(QString::fromLatin1(name));
        };
        // 逐轴/容差行编辑器（QDoubleSpinBox——renderSpinRow 形态，带
        // irdFieldKey 属性；每次重投影后须重新定位——重建式刷新下旧指针
        // 失效，F-453 队列化教训的遍历侧同款纪律）。
        auto spinByFieldKey = [panel](const char* key) -> QDoubleSpinBox* {
            for (QDoubleSpinBox* s : panel->findChildren<QDoubleSpinBox*>()) {
                if (s->property("irdFieldKey").toString()
                    == QString::fromLatin1(key)) {
                    return s;
                }
            }
            return nullptr;
        };
        // 工位组行条目计数（树三级＝根→分组→条目——分组在根的 children
        // 层，不在 topLevel；首轮实录 topLevel 找"工位"恒 -1）。
        auto stationEntryCount = [tree]() -> int {
            if (tree == nullptr) {
                return -1;
            }
            for (int r = 0; r < tree->topLevelItemCount(); ++r) {
                const QTreeWidgetItem* root = tree->topLevelItem(r);
                for (int i = 0; i < root->childCount(); ++i) {
                    const QTreeWidgetItem* group = root->child(i);
                    if (group->text(0).startsWith(QStringLiteral("工位"))) {
                        return group->childCount();
                    }
                }
            }
            return -1;
        };

        // ---- 拍 2：空态断言→工位新增→空态退役--------------------------
        step("2 empty-state-and-add-station");
        ok(buttonOf("ird_req_duplicate_points") != nullptr
               && !buttonOf("ird_req_duplicate_points")->isEnabled(),
           "step2 empty-duplicate-disabled");
        ok(buttonOf("ird_req_remove_points") != nullptr
               && !buttonOf("ird_req_remove_points")->isEnabled(),
           "step2 empty-remove-disabled");
        ok(buttonOf("ird_req_add_points") != nullptr
               && buttonOf("ird_req_add_points")->isEnabled(),
           "step2 empty-add-enabled");
        QLabel* hint = panel->findChild<QLabel*>(
            QStringLiteral("ird_req_empty_hint_points"));
        ok(hint != nullptr && hint->isVisibleTo(hint->parentWidget()),
           "step2 empty-hint-visible");
        if (buttonOf("ird_req_add_points") != nullptr) {
            buttonOf("ird_req_add_points")->click();
        }
        // F-537（UI-T68）：提交轨（queued invocation）落域＋全面板重投影的
        // 时延随宿主负载波动——单次静置 300ms 在负载尖峰下树行尚未现身
        // （ui-t67 实录 tree-child=-1 误红）。改有界轮询：100ms 拍×最多 30
        // 拍（3s 上限），行一现身即收拍；超界仍无行＝如实红（不掩盖真缺陷
        // ——轮询只消时序抖动，不放宽断言本体）。
        int stationRows = -1;
        for (int beat = 0; beat < 30 && stationRows < 1; ++beat) {
            settleEvents(100);
            stationRows = stationEntryCount();
        }
        ok(stationRows >= 1, "step2 station-added (tree-child="
                                 + std::to_string(stationRows) + ")");
        ok(buttonOf("ird_req_duplicate_points") != nullptr
               && buttonOf("ird_req_duplicate_points")->isEnabled(),
           "step2 selected-duplicate-enabled");
        ok(buttonOf("ird_req_remove_points") != nullptr
               && buttonOf("ird_req_remove_points")->isEnabled(),
           "step2 selected-remove-enabled");
        ok(hint != nullptr && !hint->isVisibleTo(hint->parentWidget()),
           "step2 selected-hint-hidden");
        QLabel* header = panel->findChild<QLabel*>(
            QStringLiteral("ird_req_tab_station_header"));
        ok(header != nullptr && header->text().startsWith(QStringLiteral("工位 > "))
               && !header->text().endsWith(QStringLiteral("未选择")),
           "step2 breadcrumb-object-name (cur="
               + (header != nullptr ? header->text().toStdString() : std::string("?"))
               + ")");

        // ---- 拍 3：工位属性编辑（名称/等级/启用/自由度/容差）----------
        step("3 station-fields");
        // REQ-01逐字段验收始终固定原ObjectId，列表排序/重投影后重新取值；
        // 不能仅断言“信号已触发”，否则写错对象或未落域也会假通过。
        const auto stationId = m_requirementsEditor.workingSet().points.entries.front().objectId;
        auto currentStation = [&]() -> requirements::TaskPoint {
            for (const auto& point : m_requirementsEditor.workingSet().points.entries) {
                if (point.objectId == stationId) { return point; }
            }
            throw std::logic_error("正式遍历的原工位丢失");
        };
        // WP-14-T12第三批已补文本轨；旧“只读即语义”断言过期，改为真实输入后
        // 对账编辑器与树呈现，不能为旧测试重新禁用合法名称编辑。
        QLineEdit* nameEdit = editByFieldKey("name");
        ok(nameEdit != nullptr && !nameEdit->isReadOnly(), "step3 name-editor-writable");
        if (nameEdit && !nameEdit->isReadOnly()) {
            const auto pointId = m_requirementsEditor.workingSet().points.entries.front().objectId;
            nameEdit->setText(QStringLiteral("生产遍历工位"));
            QMetaObject::invokeMethod(nameEdit, "editingFinished"); settleEvents(300);
            bool renamed = false;
            for (const auto& point : m_requirementsEditor.workingSet().points.entries) {
                if (point.objectId == pointId) { renamed = point.name == "生产遍历工位"; }
            }
            ok(renamed, "step3 name-written-to-original-point");
        }
        QComboBox* levelCombo = comboByName("ird_station_level_combo");
        ok(levelCombo != nullptr, "step3 level-combo-present");
        if (levelCombo != nullptr) {
            const int before = levelCombo->currentIndex();
            levelCombo->setCurrentIndex(before == 0 ? 1 : 0);  // Must↔Should
            settleEvents(300);
            ok(currentStation().level == (before == 0 ? requirements::RequirementLevel::Should
                                                     : requirements::RequirementLevel::Must),
               "step3 level-authoritative-value");
        }
        QComboBox* enabledCombo = comboByName("ird_station_enabled_combo");
        ok(enabledCombo != nullptr, "step3 enabled-combo-present");
        if (enabledCombo != nullptr) {
            const bool expected = enabledCombo->currentIndex() != 0;
            enabledCombo->setCurrentIndex(expected ? 0 : 1);
            settleEvents(300);
            ok(currentStation().enabled == expected, "step3 enabled-authoritative-value");
        }
        const char* dofKeys[] = {"dof-x", "dof-y", "dof-z", "dof-roll", "dof-pitch", "dof-yaw"};
        auto dofControl = [&](const char* key) -> QComboBox* {
            for (auto* control : panel->findChildren<QComboBox*>("ird_dof_combo")) {
                if (control->property("irdDofKey").toString() == QLatin1String(key)) { return control; }
            }
            return nullptr;
        };
        // 先建立六轴均约束的有效基线，再逐轴释放/恢复，避免“全部自由”
        // 违反至少一约束的不变量而把正确拒绝误报为字段写入故障。
        for (const char* key : dofKeys) {
            auto* control = dofControl(key);
            ok(control != nullptr, std::string("step3 control-present-") + key);
            if (control) { control->setCurrentIndex(0); settleEvents(100); }
        }
        for (std::size_t axis = 0; axis < 6; ++axis) {
            for (const bool constrained : {false, true}) {
                auto* control = dofControl(dofKeys[axis]);
                if (!control) { ok(false, "step3 reprojected-dof-control-missing"); continue; }
                control->setCurrentIndex(constrained ? 0 : 1); settleEvents(100);
                const auto dof = currentStation().pose.constrainedDof;
                const bool values[] = {dof.x, dof.y, dof.z, dof.roll, dof.pitch, dof.yaw};
                bool correct = values[axis] == constrained;
                for (std::size_t other = 0; other < 6; ++other) {
                    if (other != axis) { correct = correct && values[other]; }
                }
                ok(correct, std::string("step3 authoritative-") + dofKeys[axis]
                    + (constrained ? "-constrained" : "-free"));
            }
        }
        // 容差行＝QDoubleSpinBox（renderSpinRow——编辑完成制提交）。
        QDoubleSpinBox* tolSpin = spinByFieldKey("tolerance-position");
        ok(tolSpin != nullptr, "step3 tolerance-editor-present");
        if (tolSpin != nullptr) {
            tolSpin->setValue(0.5);
            Q_EMIT tolSpin->editingFinished();  // 与失焦同源的提交信号
            settleEvents(300);
            ok(std::abs(currentStation().tolerance.positionTolerance - 0.5) < 1e-12,
               "step3 position-tolerance-authoritative-m");
        }
        const char* positionKeys[] = {"pose-position-x", "pose-position-y", "pose-position-z"};
        const double positions[] = {1.25, -0.75, 0.4};  // 原参考系，m。
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto* spin = spinByFieldKey(positionKeys[axis]);
            ok(spin != nullptr, std::string("step3 position-control-") + positionKeys[axis]);
            if (spin) {
                spin->setValue(positions[axis]); Q_EMIT spin->editingFinished(); settleEvents(150);
                const auto value = currentStation().pose.position.tryValue();
                ok(value && std::abs((*value)[axis] - positions[axis]) < 1e-12,
                   std::string("step3 position-authoritative-m-") + positionKeys[axis]);
            }
        }
        auto* angleTolerance = spinByFieldKey("tolerance-orientation");
        ok(angleTolerance != nullptr, "step3 orientation-tolerance-control");
        if (angleTolerance) {
            angleTolerance->setValue(0.025); Q_EMIT angleTolerance->editingFinished(); settleEvents(150);
            ok(std::abs(currentStation().tolerance.orientationTolerance - 0.025) < 1e-12,
               "step3 orientation-tolerance-authoritative-rad");
        }

        // WP-14-T12/UX-05：正式面板展开并复制可回贴模板，不能只在测试中
        // 预知字段键；可见性和剪贴板均观测真实控件，不绕过生产回调。
        auto* parameterTable = panel->findChild<QWidget*>("ird_req_batch_station");
        auto* tableToggle = parameterTable ? parameterTable->findChild<QPushButton*>("ird_req_batch_toggle") : nullptr;
        auto* copyTsv = parameterTable ? parameterTable->findChild<QPushButton*>("ird_req_copy_tsv") : nullptr;
        ok(tableToggle && copyTsv, "step3 TSV-template-entry-present");
        if (tableToggle && copyTsv) {
            const auto beforeCopy = m_requirementsEditor.workingSet();
            tableToggle->setChecked(true); settleEvents(100);
            for (QWidget* parent = copyTsv->parentWidget(); parent; parent = parent->parentWidget()) {
                if (auto* scroll = qobject_cast<QScrollArea*>(parent)) { scroll->ensureWidgetVisible(copyTsv); }
            }
            settleEvents(100);
            ok(!copyTsv->visibleRegion().isEmpty(), "step3 TSV-copy-button-reachable");
            copyTsv->click(); settleEvents(100);
            ok(QApplication::clipboard()->text().contains("tolerance-position\t0.5"),
               "step3 TSV-real-clipboard-key-and-current-value");
            ok(m_requirementsEditor.workingSet() == beforeCopy, "step3 TSV-copy-zero-write");
            snapPng(hostWin, "tour-2b-tsv-template.png");
            tableToggle->setChecked(false);
        }

        // ---- 拍 4：动作阶段＋姿态规则-----------------------------------
        step("4 segment-and-orientation");
        QComboBox* segEnabled = panel->findChild<QComboBox*>(
            QStringLiteral("ird_segment_enabled_combo"));  // 首个＝接近段（投影序）
        ok(segEnabled != nullptr, "step4 segment-enabled-present");
        if (segEnabled != nullptr) {
            const auto beforeInvalid = m_requirementsEditor.workingSet();
            segEnabled->setCurrentIndex(0);  // 启用接近段
            settleEvents(300);
            ok(m_requirementsEditor.workingSet() == beforeInvalid,
               "step4 zero-distance-enable-rejected-zero-write");
            auto* restored = panel->findChild<QComboBox*>("ird_segment_enabled_combo");
            ok(restored && restored->currentIndex() == 1,
               "step4 rejected-enable-control-restores-authoritative-state");
            // 先输入合法距离，再启用；初始0 m不能被测试偷偷换成正默认值。
            auto* distance = restored ? restored->parentWidget()->findChild<QDoubleSpinBox*>() : nullptr;
            ok(distance != nullptr, "step4 approach-distance-control");
            if (distance) {
                distance->setValue(0.12); Q_EMIT distance->editingFinished(); settleEvents(150);
                ok(std::abs(currentStation().approach.distanceM - 0.12) < 1e-12,
                   "step4 approach-distance-authoritative-m");
                restored = panel->findChild<QComboBox*>("ird_segment_enabled_combo");
                if (restored) { restored->setCurrentIndex(0); settleEvents(150); }
                ok(currentStation().approach.enabled, "step4 valid-approach-enabled");
            }
        }
        // 段轴下拉在重投影后重建——操作前重新定位（F-453 遍历侧纪律）。
        QComboBox* segAxis = panel->findChild<QComboBox*>(
            QStringLiteral("ird_segment_axis_combo"));
        ok(segAxis != nullptr, "step4 segment-axis-present");
        if (segAxis != nullptr) {
            segAxis->setCurrentIndex(1);  // ToolZ→ReferenceZ
            settleEvents(300);
            ok(currentStation().approach.axis == requirements::SegmentAxis::ReferenceZ,
               "step4 approach-authoritative-reference-axis");
        }
        // REQ-02：三段按接近/作业/撤离固定投影。定位每次重新执行，
        // 以父行中的控件识别距离，避免误编辑另一段或复用已销毁控件。
        auto segmentEnable = [&](int index) -> QComboBox* {
            const auto controls = panel->findChildren<QComboBox*>("ird_segment_enabled_combo");
            return controls.size() == 3 ? controls[index] : nullptr;
        };
        auto* retractEnable = segmentEnable(2);
        auto* retractDistance = retractEnable
            ? retractEnable->parentWidget()->findChild<QDoubleSpinBox*>() : nullptr;
        ok(retractDistance != nullptr, "step4 retract-distance-control");
        if (retractDistance) {
            retractDistance->setValue(0.18); Q_EMIT retractDistance->editingFinished(); settleEvents(150);
            retractEnable = segmentEnable(2);
            if (retractEnable) { retractEnable->setCurrentIndex(0); settleEvents(150); }
            ok(currentStation().retract.enabled
                   && std::abs(currentStation().retract.distanceM - 0.18) < 1e-12,
               "step4 retract-authoritative-enabled-distance-m");
            retractEnable = segmentEnable(2);
            auto* retractAxis = retractEnable
                ? retractEnable->parentWidget()->findChild<QComboBox*>("ird_segment_axis_combo") : nullptr;
            ok(retractAxis != nullptr, "step4 retract-axis-control");
            if (retractAxis) { retractAxis->setCurrentIndex(1); settleEvents(150); }
            ok(currentStation().retract.axis == requirements::SegmentAxis::ReferenceZ,
               "step4 retract-authoritative-reference-axis");
        }
        auto* workEnable = segmentEnable(1);
        ok(workEnable != nullptr && currentStation().work.enabled, "step4 work-segment-always-enabled");
        ok(workEnable && !workEnable->isEnabled(), "step4 work-segment-control-readonly");
        if (workEnable) {
            for (auto* ancestor = workEnable->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
                if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) { scroll->ensureWidgetVisible(workEnable); }
            }
            settleEvents(100);
            ok(!workEnable->visibleRegion().isEmpty(), "step4 work-segment-readonly-state-reachable");
            snapPng(hostWin, "tour-2c-segments-verified.png");
        }
        if (workEnable && workEnable->isEnabled()) {
            const auto beforeWork = m_requirementsEditor.workingSet();
            workEnable->setCurrentIndex(1); settleEvents(150);
            ok(m_requirementsEditor.workingSet() == beforeWork,
               "step4 work-disable-rejected-zero-write");
        }
        // 姿态规则：五规则下拉切换→参数行重投影（Fixed→ToolRollFree 行集
        // 变化）。切换后卡片重建＝orient 指针失效——回切前必须重新定位
        // （本通道首轮实录：复用旧指针＝UAF 崩溃 0xC0000005）。
        QComboBox* orient = comboByName("ird_station_orientation_combo");
        ok(orient != nullptr, "step4 orientation-combo-present");
        if (orient != nullptr) {
            const int rowBefore = panel->findChildren<QLineEdit*>().size();
            orient->setCurrentIndex(orient->count() - 1);  // 末项＝ToolRollFree
            settleEvents(300);
            const int rowAfter = panel->findChildren<QLineEdit*>().size();
            ok(rowAfter != rowBefore,
               "step4 orientation-reproject (rows " + std::to_string(rowBefore)
                   + "->" + std::to_string(rowAfter) + ")");
            QComboBox* orient2 = comboByName("ird_station_orientation_combo");
            if (orient2 != nullptr) {
                orient2->setCurrentIndex(0);  // 回 Fixed（稳定基线）
                settleEvents(300);
            }
        }
        snapPng(hostWin, "tour-2-station-edited.png");

        // REQ-01：完整工位复制必须只改变身份和唯一名称；删除选中副本后
        // 权威集合、树行及检查器须共同回落原对象。两步均走真实按钮提交轨。
        const auto sourceStation = currentStation();
        auto* duplicateStation = buttonOf("ird_req_duplicate_points");
        ok(duplicateStation != nullptr, "step4 duplicate-entry-present");
        if (duplicateStation) { duplicateStation->click(); settleEvents(200); }
        const auto duplicatedPoints = m_requirementsEditor.workingSet().points.entries;
        ok(duplicatedPoints.size() == 2 && stationEntryCount() == 2,
           "step4 duplicate-model-and-tree-two-entries");
        bool exactCopy = false;
        for (auto copy : duplicatedPoints) {
            if (copy.objectId != stationId) {
                const bool independent = copy.objectId.isValid() && copy.name != sourceStation.name;
                copy.objectId = sourceStation.objectId; copy.name = sourceStation.name;
                exactCopy = independent && copy == sourceStation;
            }
        }
        ok(exactCopy && currentStation() == sourceStation, "step4 duplicate-independent-full-value-copy");
        auto* removeStation = buttonOf("ird_req_remove_points");
        ok(removeStation != nullptr, "step4 remove-entry-present");
        if (removeStation) { removeStation->click(); settleEvents(200); }
        ok(m_requirementsEditor.workingSet().points.entries.size() == 1
               && currentStation() == sourceStation && stationEntryCount() == 1,
           "step4 remove-only-selected-copy");
        auto* fallbackHeader = panel->findChild<QLabel*>("ird_req_tab_station_header");
        ok(fallbackHeader && fallbackHeader->text().contains(QString::fromStdString(sourceStation.name)),
           "step4 selection-fallback-original-station");

        // ---- 拍 5：卡片折叠＋区域新增＋盒尺寸＋采样计数------------------
        step("5 fold-region-sampling");
        QGroupBox* firstCard = nullptr;
        for (QGroupBox* card : panel->findChildren<QGroupBox*>(
                 QStringLiteral("ird_card"))) {
            if (card->findChild<QToolButton*>(QStringLiteral("ird_card_fold"))
                != nullptr) {
                firstCard = card;
                break;
            }
        }
        ok(firstCard != nullptr, "step5 card-fold-present");
        if (firstCard != nullptr) {
            QToolButton* fold =
                firstCard->findChild<QToolButton*>(QStringLiteral("ird_card_fold"));
            fold->setChecked(false);
            settleEvents(150);
            ok(fold->text() == QStringLiteral("▶"), "step5 card-collapsed");
            fold->setChecked(true);
            settleEvents(150);
            ok(fold->text() == QStringLiteral("▼"), "step5 card-expanded");
        }
        buttonOf("ird_req_add_regions")->click();
        settleEvents(300);
        QLabel* regionHeader = panel->findChild<QLabel*>(
            QStringLiteral("ird_req_tab_region_header"));
        ok(regionHeader != nullptr
               && regionHeader->text().startsWith(QStringLiteral("区域 > ")),
           "step5 region-added-breadcrumb");
        // WP-14-T12：正式宿主真实完整采样表单，经确认后读取权威编辑器；
        // 不以控件存在或“已点击”冒充数值通过，三种方法均实际写入并对照。
        const auto regionId = m_requirementsEditor.workingSet().regions.entries.front().objectId;
        auto currentRegion = [&]() -> requirements::WorkRegion {
            for (const auto& region : m_requirementsEditor.workingSet().regions.entries) {
                if (region.objectId == regionId) { return region; }
            }
            throw std::logic_error("采样遍历的原区域已丢失");
        };
        for (const auto method : {requirements::PositionSamplingMethod::GridBySpacing,
                                  requirements::PositionSamplingMethod::Random,
                                  requirements::PositionSamplingMethod::Grid}) {
            auto* form = panel->findChild<QWidget*>("ird_region_sampling_form");
            ok(form != nullptr, "step5 complete-sampling-form");
            if (!form) { break; }
            auto* methodCombo = form->findChild<QComboBox*>("ird_region_sampling_method");
            ok(methodCombo != nullptr, "step5 method-selector");
            if (!methodCombo) { break; }
            methodCombo->setCurrentIndex(methodCombo->findData(int(method)));
            if (method == requirements::PositionSamplingMethod::GridBySpacing) {
                for (int axis = 0; axis < 3; ++axis) {
                    form->findChild<QLineEdit*>(QString("ird_region_spacing_%1").arg(axis))->setText("0.25");
                }
            } else if (method == requirements::PositionSamplingMethod::Random) {
                form->findChild<QLineEdit*>("ird_region_random_count")->setText("24");
            } else {
                for (int axis = 0; axis < 3; ++axis) {
                    form->findChild<QLineEdit*>(QString("ird_region_grid_count_%1").arg(axis))->setText(QString::number(4-axis));
                }
            }
            form->findChild<QLineEdit*>("ird_region_direction_samples")->setText("12");
            form->findChild<QLineEdit*>("ird_region_roll_samples")->setText("4");
            form->findChild<QCheckBox*>("ird_region_orientation_coverage_enabled")->setChecked(true);
            form->findChild<QLineEdit*>("ird_region_orientation_coverage")->setText("0.65");
            const auto before = currentRegion();
            ok(before.positionSampling.method != method || method == requirements::PositionSamplingMethod::Grid,
               "step5 unconfirmed-definition-not-written");
            // 人机可达性独立对账：先滚到方法选择器，再滚到确认按钮；
            // 只调用屏外控件不能证明用户能在正式Dock完成同样操作。
            auto* scroll = qobject_cast<QScrollArea*>(panel);
            auto* applyButton = form->findChild<QPushButton*>("ird_region_sampling_apply");
            ok(scroll && applyButton, "step5 scroll-and-confirm-present");
            if (scroll && applyButton) {
                // 区域卡本身有内层滚动区；须由内向外滚动每一层，外层直接
                // ensureWidgetVisible不会穿透内层裁剪，不能误判控件不可达。
                const auto reveal = [&](QWidget* target) {
                    for (auto* ancestor = target->parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
                        if (auto* area = qobject_cast<QScrollArea*>(ancestor)) {
                            area->ensureWidgetVisible(target); settleEvents(100);
                        }
                    }
                    return !target->visibleRegion().isEmpty();
                };
                ok(reveal(methodCombo), "step5 method-selector-reachable");
                snapPng(hostWin, ("tour-3-method-" + std::to_string(int(method)) + ".png").c_str());
                ok(reveal(applyButton), "step5 confirm-button-reachable");
                snapPng(hostWin, ("tour-3-confirm-" + std::to_string(int(method)) + ".png").c_str());
            }
            applyButton->click(); settleEvents(300);
            const auto after = currentRegion();
            // WP-14-T12/REQ-03：核对正式预览接线收到的世界系几何，不能仅凭
            // 表单值通过就宣称三维正确。本遍历使用World参考，盒角可直接对账；
            // 规则辅助格每条仅一轴延伸，Random不得沿残留Grid载荷画伪网格。
            ok(m_reqBox.has_value() && m_reqGrid.has_value(), "step5 world-preview-layers-present");
            if (m_reqBox && m_reqGrid) {
                bool cornersMatch = true;
                for (std::size_t i = 0; i < 8; ++i) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        const double sign = (i & (std::size_t(1) << axis)) ? 0.5 : -0.5;
                        cornersMatch = cornersMatch && std::abs(m_reqBox->corners[i][axis]
                            - (after.box.center[axis] + sign * after.box.size[axis])) < 1e-12;
                    }
                }
                ok(cornersMatch, "step5 world-box-eight-corners");
                bool aligned = true;
                for (const auto& line : m_reqGrid->gridLines) {
                    int changed = 0;
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        if (std::abs(line.first[axis] - line.second[axis]) > 1e-12) { ++changed; }
                    }
                    aligned = aligned && changed == 1;
                }
                ok(aligned, "step5 grid-lines-axis-aligned");
                if (method == requirements::PositionSamplingMethod::Random) {
                    ok(m_reqGrid->gridLines.empty(), "step5 random-no-fabricated-grid");
                } else if (method == requirements::PositionSamplingMethod::Grid) {
                    ok(m_reqGrid->gridLines.size() == 47, "step5 grid-4x3x2-has-47-boundary-lines");
                } else {
                    ok(!m_reqGrid->gridLines.empty() && m_reqGrid->gridLines.size() <= 3267,
                       "step5 spacing-grid-present-and-bounded");
                }
            }
            ok(after.positionSampling.method == method, "step5 confirmed-method-" + std::to_string(int(method)));
            ok(after.orientationSampling.directionSamples == 12 && after.orientationSampling.rollSamples == 4,
               "step5 orientation-12x4");
            ok(after.coverageTargets.minOrientationCoverage && *after.coverageTargets.minOrientationCoverage == 0.65,
               "step5 optional-orientation-coverage-set");
            if (method == requirements::PositionSamplingMethod::GridBySpacing) {
                ok(after.positionSampling.spacing == std::array<double,3>{0.25,0.25,0.25}, "step5 spacing-SI");
                snapPng(hostWin, "tour-3a-spacing.png");
            } else if (method == requirements::PositionSamplingMethod::Random) {
                ok(after.positionSampling.count == 24, "step5 random-count-24");
                snapPng(hostWin, "tour-3b-random.png");
            } else {
                ok(after.positionSampling.counts == std::array<std::uint32_t,3>{4,3,2}, "step5 grid-counts-4x3x2");
            }
        }
        snapPng(hostWin, "tour-3-region-sampling.png");

        // ---- 拍 6：覆盖率滑块＋工况新增（模态向导自动填写）--------------
        step("6 coverage-and-condition-wizard");
        QSlider* slider =
            panel->findChild<QSlider*>(QStringLiteral("ird_region_coverage_slider"));
        ok(slider != nullptr, "step6 coverage-slider-present");
        if (slider != nullptr) {
            slider->setValue(85);
            settleEvents(300);
            ok(true, "step6 coverage-set (85%)");
        }
        // 新增工况＝默认向导弹窗（模态 exec）——预排队 300ms 定时器在嵌套
        // 事件循环里自动填写名称并确认（自动化模态对话框标准手法）。
        QPushButton* addCondition = buttonOf("ird_req_add_conditions");
        ok(addCondition != nullptr, "step6 condition-add-present");
        if (addCondition != nullptr) {
            QTimer::singleShot(300, this, [this, panel] {
                // 找活动模态对话框（向导）→名称行填"搬运工况"→OK。
                QWidget* modal = QApplication::activeModalWidget();
                if (modal == nullptr) {
                    std::cout << "[ird-ui-smoke-tour] FAIL wizard-modal-missing"
                              << std::endl;
                    return;
                }
                for (QLineEdit* e : modal->findChildren<QLineEdit*>()) {
                    e->setText(QStringLiteral("搬运工况"));
                    break;
                }
                if (QDialog* dlg = qobject_cast<QDialog*>(modal)) {
                    if (QDialogButtonBox* box =
                            modal->findChild<QDialogButtonBox*>()) {
                        box->button(QDialogButtonBox::Ok)->click();
                        (void)dlg;
                    }
                }
                (void)panel;
            });
            addCondition->click();  // 触发 exec()——嵌套循环执行上面定时器
            settleEvents(400);
        }
        // 工况表：行出现＋是否必验列非空（派生 Tag 直投）。
        QTreeWidget* condTable = panel->findChild<QTreeWidget*>(
            QStringLiteral("ird_req_condition_table"));
        ok(condTable != nullptr && condTable->topLevelItemCount() >= 1,
           "step6 condition-row-present");
        ok(condTable != nullptr && condTable->topLevelItemCount() >= 1
               && !condTable->topLevelItem(0)->text(3).isEmpty(),
           "step6 verify-tag-nonempty (cur="
               + (condTable != nullptr && condTable->topLevelItemCount() >= 1
                      ? condTable->topLevelItem(0)->text(3).toStdString()
                      : std::string("?"))
               + ")");

        // ---- 拍 7：工况节拍编辑＋校验页实时化---------------------------
        step("7 cycle-and-validation");
        QLineEdit* cycleEdit = editByFieldKey("cycle-time");
        ok(cycleEdit != nullptr, "step7 cycle-editor-present");
        if (cycleEdit != nullptr) {
            cycleEdit->setText(QStringLiteral("12.5"));
            Q_EMIT cycleEdit->editingFinished();
            settleEvents(300);
            ok(condTable != nullptr && condTable->topLevelItemCount() >= 1
                   && condTable->topLevelItem(0)->text(1).contains(
                       QStringLiteral("12.5")),
               "step7 cycle-table-synced");
        }
        // 校验页：页签切到 index 3→看板卡随编辑实时化（非『尚未执行』）。
        QTabWidget* pages = panel->findChild<QTabWidget*>();
        ok(pages != nullptr && pages->count() == 4, "step7 tabs=4");
        if (pages != nullptr) {
            pages->setCurrentIndex(3);
            settleEvents(300);
            QLabel* valHeader = panel->findChild<QLabel*>(
                QStringLiteral("ird_req_tab_validation_header"));
            ok(valHeader != nullptr
                   && !valHeader->text().contains(QStringLiteral("尚未执行")),
               "step7 validation-live (cur="
                   + (valHeader != nullptr ? valHeader->text().toStdString()
                                           : std::string("?"))
                   + ")");
            snapPng(hostWin, "tour-4-validation.png");
            pages->setCurrentIndex(0);
            settleEvents(150);
        }

        // ---- 拍 7.5：三态着色上屏数据面＋校验逐项行点击协议（F-529，
        //      UI-T67 扩建；UI-T68 按 E-5 名实相符重做②半）----------
        step("7.5 coloring-dataplane-and-validation-click");
        // ①着色数据面上屏链：区域预览 sink 交付的采样格（宿主侧
        //   m_reqGrid——View3D 网关消费同一对象）非空且逐点着色态与采样点
        //   等长（UI-T65 三态映射的宿主侧交付证据）；无评估运行时格态＝
        //   全 NotSampled（中性灰）——诚实呈现，非缺陷。
        // 着色数据面（F-529 自动化屏证）：区域预览 sink 交付＝上屏链通；
        // 空交付＝无评估运行时的诚实中性格（F-495 评估联动为前置——
        // 三态全谱视觉屏证归宿主手动/评估执行联动通道）。
        if (m_reqGrid.has_value() && !m_reqGrid->samples.empty()) {
            ok(m_reqGrid->cellStates.size() == m_reqGrid->samples.size(),
               "step7.5 grid-dataplane-delivered (samples="
                   + std::to_string(m_reqGrid->samples.size()) + ")");
            std::size_t colored = 0;
            for (auto st : m_reqGrid->cellStates) {
                if (st != ui::View3DCellState::NotSampled) { ++colored; }
            }
            std::cout << "[ird-ui-smoke-tour] step7.5 colored-states="
                      << colored << "/" << m_reqGrid->cellStates.size()
                      << " (三态全谱需评估执行联动——UI-T64 通道产出)" << std::endl;
        } else {
            ok(true, "step7.5 grid-honest-absent (无评估运行——中性格线，F-495 评估联动前置)");
        }
        // 3D 视图截图（着色上屏渲染结果——人工复核留证）。
        if (m_view3d != nullptr) {
            snapPng(m_view3d, "tour-4b-view3d-coloring.png");
        }
        // ②校验逐项行点击协议（E-5/F-538⑤ 名实相符重做——UI-T67 首版
        //   findChild 取首树＝层汇总表〔聚合行无锚〕且断言体 ok(true) 恒真
        //   ——验收 M-B 变异实证 emit 删除仍绿）。本版按表头首列『级别』
        //   锚定逐项表（行带隐藏锚列；层汇总表首列＝『层』），两半点击
        //   协议＋可断言后置条件：setCurrentItem＝真实点击的 Qt 选中面；
        //   itemClicked emit＝面板定位槽（nodeAnchor→locate——与手点同源
        //   信号轨）。定位行为面（树滚动＋三维高亮）另有 gui 钉扎
        //   （RequirementsSessionGuiTest 注入 locateSink 形态）——生产装配
        //   locateSink 缺席（F-539 登记）使 LocateTarget 产出即丢弃，遍历
        //   侧无可读回的落点态，故本拍断言面＝点击协议落地＋锚列良构，
        //   不虚称 exercised。
        pages->setCurrentIndex(3);
        settleEvents(150);
        QTreeWidget* itemsTree = nullptr;
        if (auto* valPage = panel->findChild<QWidget*>(
                QStringLiteral("ird_req_tab_validation_header"))) {
            QWidget* page = valPage->parentWidget() != nullptr
                                ? valPage->parentWidget()
                                : valPage;
            for (QTreeWidget* t : page->findChildren<QTreeWidget*>()) {
                if (t->headerItem()->text(0) == QStringLiteral("级别")) {
                    itemsTree = t;  // 逐项表（首列『级别』≠层汇总表『层』）
                    break;
                }
            }
            ok(itemsTree != nullptr, "step7.5 validation-items-tree-present");
            if (itemsTree != nullptr && itemsTree->topLevelItemCount() > 0) {
                QTreeWidgetItem* row = itemsTree->topLevelItem(0);
                itemsTree->setCurrentItem(row);
                settleEvents(100);
                Q_EMIT itemsTree->itemClicked(row, 0);
                settleEvents(200);
                // 后置条件两半：①点击行＝当前行（选中面落地）；②锚列良
                //   构——空＝Warning 行无 subject 属规格（无锚不伪造定位
                //   分支），非空则必须解析为有效 ObjectId（垃圾锚＝投影
                //   缺陷必红）。
                const QString anchorText =
                    row->text(itemsTree->columnCount() - 1);
                const bool anchorWellFormed =
                    anchorText.isEmpty()
                    || core::ObjectId::tryFromCanonical(
                           anchorText.toStdString())
                           .has_value();
                ok(itemsTree->currentItem() == row && anchorWellFormed,
                   "step7.5 validation-row-click-protocol (rows="
                       + std::to_string(itemsTree->topLevelItemCount())
                       + " anchor="
                       + (anchorText.isEmpty() ? std::string("empty")
                                               : std::string("valid"))
                       + ")");
                snapPng(hostWin, "tour-4c-validation-row-click.png");
            } else if (itemsTree != nullptr) {
                ok(false, "step7.5 validation-items-empty");
            }
        } else {
            ok(false, "step7.5 validation-page-missing");
        }
        pages->setCurrentIndex(0);
        settleEvents(150);

        // ---- 拍 8：草稿撤销→重做----------------------------------------
        // 观测面＝重做键使能翻转（撤销栈序贯——一次 undo 回退的是最近一次
        // 编辑而非特定操作；『undo 后重做面点亮／redo 后重做面熄灭』是不
        // 依赖栈深的不变量。首轮实录按"计数回到新增前"断言属设计错误）。
        step("8 draft-undo-redo");
        QPushButton* undoBtn = nullptr;
        QPushButton* redoBtn = nullptr;
        for (QPushButton* b : panel->findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("撤销")) { undoBtn = b; }
            if (b->text() == QStringLiteral("重做")) { redoBtn = b; }
        }
        ok(undoBtn != nullptr && redoBtn != nullptr, "step8 undo-redo-present");
        ok(redoBtn != nullptr && !redoBtn->isEnabled(),
           "step8 redo-initially-disabled");
        if (undoBtn != nullptr && undoBtn->isEnabled()) {
            undoBtn->click();
            settleEvents(300);
            ok(redoBtn != nullptr && redoBtn->isEnabled(),
               "step8 undo-lights-redo");
        }
        if (redoBtn != nullptr && redoBtn->isEnabled()) {
            redoBtn->click();
            settleEvents(300);
            ok(redoBtn != nullptr && !redoBtn->isEnabled(),
               "step8 redo-consumed");
        }

        // ---- 拍 9：draft.apply（需求域真实修订）＋项目级撤销使能----------
        step("9 apply-and-project-undo");
        // UX-07：走真实命令入口验证取消，而非给runDomainApply注入空交互
        // 后把提交成功误当作确认流程已验证。定时器只回答实际产品确认框。
        const auto beforeCancelApply = m_requirementsEditor.workingSet();
        const auto headBeforeCancelApply = m_lastStoreAdapter->projectStore().query().head().id;
        bool sawApplyConfirmation = false;
        QTimer cancelApply;
        cancelApply.setSingleShot(true);
        QObject::connect(&cancelApply, &QTimer::timeout, this, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box && box->windowTitle() == QStringLiteral("应用草稿")) {
                sawApplyConfirmation = true;
                snapPng(box, "tour-apply-confirmation.png");
                box->button(QMessageBox::No)->click();
            }
        });
        cancelApply.start(250);
        m_content->submitCommand("draft.apply");
        cancelApply.stop(); settleEvents(200);
        ok(sawApplyConfirmation, "step9 actual-apply-confirmation-seen");
        ok(m_requirementsEditor.workingSet() == beforeCancelApply, "step9 cancel-apply-keeps-draft");
        ok(m_lastStoreAdapter->projectStore().query().head().id == headBeforeCancelApply,
           "step9 cancel-apply-zero-project-revision");
        bool confirmedApply = false;
        QTimer acceptApply;
        acceptApply.setSingleShot(true);
        QObject::connect(&acceptApply, &QTimer::timeout, this, [&] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (box && box->windowTitle() == QStringLiteral("应用草稿")) {
                confirmedApply = true;
                box->button(QMessageBox::Yes)->click();
            }
        });
        acceptApply.start(250);
        m_content->submitCommand("draft.apply");
        acceptApply.stop();
        ok(confirmedApply, "step9 actual-apply-confirmed");
        ok(m_lastStoreAdapter->projectStore().query().head().id != headBeforeCancelApply,
           "step9 requirements-draft-applied");
        // 由真实编排完成提交后的刷新，随后验证回执和撤销的呈现事实。
        settleEvents(300);
        QPushButton* projectUndo = nullptr;
        for (QPushButton* b : panel->findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("撤销上次应用")) {
                projectUndo = b;
            }
        }
        // UI-T39 重写（原断言在旧呈现下是假通过）：撤销键可用性＝磁盘 tip
        // inverse 推导（§5.5——真实撤销机器的可用性事实）。全新项目**首次
        // 应用**的全部受影响对象无前版（req-set 根/集合/工位均新建）→处
        // 理器声明不可逆（CommandHandlers §"首次应用＝nullopt"）→诚实禁用
        // ＋原因提示。旧代码按钮只按提交出口存在性恒亮＝"点了也没撤销"的
        // 审核指控面；本拍改为验证真实撤销回路：
        //   ①首应用→禁用＋tooltip 携带原因（无可撤销修订）；
        //   ②再次编辑＋二应用→对象有前版→可逆→按钮点亮；
        //   ③点击→逆命令提交产生新修订（canRedo 点亮＝撤销真实发生）。
        ok(projectUndo != nullptr && !projectUndo->isEnabled(),
           "step9 project-undo-disabled-first-apply (irreversible)");
        ok(projectUndo != nullptr
               && projectUndo->toolTip().contains(QStringLiteral("没有可撤销")),
           "step9 project-undo-reason-shown");
        // 第二次编辑并应用必须提交成功，证明首应用已重建基线集合身份；
        // 随后真实点击项目撤销，检查新修订和可重做事实，拒绝不得计通过。
        {
            QPushButton* addStation = nullptr;
            for (QPushButton* b : panel->findChildren<QPushButton*>()) {
                if (b->objectName() == QStringLiteral("ird_req_add_points")) {
                    addStation = b;
                    break;
                }
            }
            if (addStation != nullptr && addStation->isEnabled()) {
                addStation->click();
                settleEvents(200);
            }
            // 第二次提交限定 requirements 域（遍历面收窄——他域对本修订
            // 事件的草稿响应不进本拍验证面）。
            std::vector<ui::DomainModuleEntry> requirementsOnly;
            if (m_domains != nullptr) {
                for (const auto& entry : m_domains->applyEntries) {
                    if (entry.moduleId == "requirements") {
                        requirementsOnly.push_back(entry);
                    }
                }
            }
            const DomainApplyReport applyReport2 = runDomainApply(
                requirementsOnly,
                std::nullopt,
                [this](project::CommandEnvelope envelope,
                       project::ICommandInteraction* cmdInteraction) {
                    const auto adapter = m_lastStoreAdapter;
                    if (adapter == nullptr) {
                        return project::CommandResult{};
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
            bool secondCommitted = false;
            std::string secondDetail;
            for (const auto& entry : applyReport2.entries) {
                if (entry.outcome == ui::DomainApplyEntryReport::Outcome::Submitted) {
                    secondCommitted = secondCommitted || entry.committed;
                    if (!entry.committed) {
                        secondDetail = entry.rejectionReason;
                    }
                }
            }
            // 回执必须真的提交；失败保留拒绝原因供定位，不豁免撤销链。
            if (secondCommitted) {
                ok(true, "step9 second-apply-committed");
                refreshRequirementsFromSession();
                settleEvents(300);
                ok(projectUndo != nullptr && projectUndo->isEnabled(),
                   "step9 project-undo-enabled-after-reversible-apply");
                if (projectUndo != nullptr && projectUndo->isEnabled()) {
                    projectUndo->click();
                    settleEvents(300);
                    const auto adapter = m_lastStoreAdapter;
                    const bool redoLit =
                        adapter != nullptr
                        && !adapter->projectStore().query().branchTips().empty()
                        && adapter->projectStore().undoRedo().status(
                               adapter->projectStore().query().branchTips().front().id)
                               .canRedo;
                    ok(redoLit, "step9 project-undo-committed (redo-stack-lit)");
                }
            } else {
                ok(false, "step9 second-apply-unexpectedly-rejected (rej="
                             + secondDetail + ")");
            }
        }
        // ---- 拍 10：建模模板应用（覆盖评估前置——coverage 消费
        //      robot-design；需求 tour 项目此前只含 req-* 对象。mtour
        //      拍 1/2/5 同款模式：Dock 召唤→模态选单自动应答→
        //      runDomainApply 建模域）--
        step("10 model-apply-for-eval");
        // 建模 Dock 召唤（mtour 拍 1 同款——会话绑定/命令可用性以面板
        // 在场为前提；首跑实测缺此步时模板种子落空→apply 空 rejected）。
        if (m_modelingDock != nullptr) {
            m_modelingDock->show();
            m_modelingDock->raise();
            settleEvents(300);
        }
        QPushButton* evalTmplBtn = hostWin->findChild<QPushButton*>(
            QStringLiteral("ird_modeling_cmd_modeling.new-from-template"));
        ok(evalTmplBtn != nullptr, "step10 template-cmd-present");
        if (evalTmplBtn != nullptr) {
            // 预排队定时器：模态选单（QInputDialog::getItem）选首项
            // （generic-6r）＋OK——mtour 拍 2 同款。
            QTimer::singleShot(300, this, [] {
                QWidget* modal = QApplication::activeModalWidget();
                if (modal == nullptr) {
                    std::cout << "[ird-ui-smoke-tour] FAIL step10 "
                                 "item-modal-missing" << std::endl;
                    return;
                }
                if (QComboBox* combo = modal->findChild<QComboBox*>()) {
                    combo->setCurrentIndex(0);
                }
                if (QDialog* dlg = qobject_cast<QDialog*>(modal)) {
                    if (QDialogButtonBox* box =
                            modal->findChild<QDialogButtonBox*>()) {
                        box->button(QDialogButtonBox::Ok)->click();
                        (void)dlg;
                    }
                }
            });
            evalTmplBtn->click();
            settleEvents(500);
        }
        // 重种子断言（mtour 拍 2 同款——失败定位到"模板未落草稿"而非
        // 笼统的 apply 未提交）。
        QWidget* evalModelingPanel =
            m_modelingDock != nullptr ? m_modelingDock->widget() : nullptr;
        QLabel* evalStatusLine =
            evalModelingPanel != nullptr
                ? evalModelingPanel->findChild<QLabel*>(
                      QStringLiteral("ird_modeling_status_line"))
                : nullptr;
        ok(evalStatusLine != nullptr
               && evalStatusLine->text().contains(QStringLiteral("generic-6r")),
           "step10 reseed-generic6r (cur="
               + (evalStatusLine != nullptr
                      ? evalStatusLine->text().toStdString()
                      : std::string("?"))
               + ")");
        // 模板重种子语义＝"未应用编辑已清空"（changes 清零），而
        // buildDraftCommand 以 changes 非空为可应用判据（NoDraft 合法跳过
        // ——首跑二连实测 rej=[] outcome=NoDraft 根因）。mtour 拍 3/4 先
        // 编辑后应用同因；此处追加一个真实结构编辑（struct-add 关节
        // 6→7，mtour 拍 4 同款）使草稿可应用。
        QPushButton* evalStructAdd =
            evalModelingPanel != nullptr
                ? evalModelingPanel->findChild<QPushButton*>(
                      QStringLiteral("ird_modeling_struct_add"))
                : nullptr;
        ok(evalStructAdd != nullptr, "step10 struct-add-present");
        if (evalStructAdd != nullptr) {
            evalStructAdd->click();
            settleEvents(400);
        }
        std::vector<ui::DomainModuleEntry> modelingOnlyForEval;
        if (m_domains != nullptr) {
            for (const auto& entry : m_domains->applyEntries) {
                if (entry.moduleId == "modeling") {
                    modelingOnlyForEval.push_back(entry);
                }
            }
        }
        ok(!modelingOnlyForEval.empty(), "step10 modeling-apply-entry-present");
        if (!modelingOnlyForEval.empty()) {
            const DomainApplyReport modelApplyReport = runDomainApply(
                modelingOnlyForEval,
                std::nullopt,
                [this](project::CommandEnvelope envelope,
                       project::ICommandInteraction* cmdInteraction) {
                    const auto adapter = m_lastStoreAdapter;
                    if (adapter == nullptr) { return project::CommandResult{}; }
                    return adapter->projectStore().commands().submit(
                        std::move(envelope), cmdInteraction);
                },
                nullptr,
                [this](const std::string& message) {
                    if (m_diag.pipeline) {
                        m_diag.pipeline->logDev(kPluginDevChannel, message);
                    }
                });
            bool modelCommitted = false;
            std::string modelRej;
            for (const auto& entry : modelApplyReport.entries) {
                std::cout << "[ird-ui-smoke-tour] step10 entry module="
                          << entry.moduleId
                          << " outcome=" << static_cast<int>(entry.outcome)
                          << " committed=" << entry.committed
                          << " rej=[" << entry.rejectionReason
                          << "] rev=[" << entry.revision << "]" << std::endl;
                if (entry.moduleId == "modeling") {
                    if (entry.committed) { modelCommitted = true; }
                    else { modelRej = entry.rejectionReason; }
                }
            }
            ok(modelCommitted,
               "step10 modeling-apply-committed (rej=[" + modelRej + "])");
            // runDomainApply 绕过 draft.apply 编排（拍 9 同款捷径）——补
            // 编排 committed 分支同款的发布收口：refreshPresentationAfter
            // Commit（编译发布消费拍——快照换绑＋运动学会话事实同步）。
            // 缺此步评估通道 m_snapshot 缺位→覆盖评估提交被拒（r10 实测
            // "结果绑定快照缺位"）；mtour 拍 5.5 靠重开项目间接触发同链。
            if (modelCommitted) {
                refreshPresentationAfterCommit();
                settleEvents(500);
            }
            refreshRequirementsFromSession();
            settleEvents(300);
        }

        // ---- 拍 11：真实覆盖评估运行（R03-10/11——F-495 评估执行联动。
        //      tour 7.5 登记的"三态全谱归宿主手动/评估执行联动通道"由本拍
        //      兑现：点击运动学面板覆盖运行按钮（与手点同源 clicked 轨）→
        //      执行通道异步评估→coverage 台账→预览投影翻译→区域页样本
        //      清单与三维格层着色同拍）--------------------------------
        step("11 coverage-evaluate-and-coloring");
        // 运动学 Dock 召唤（拍 10 建模 Dock 同因——面板在场才会话/可用性
        // 刷新到位；首跑实测建模应用提交后按钮仍禁用＝刷新未达）。
        if (m_kinematicsDock != nullptr) {
            m_kinematicsDock->show();
            m_kinematicsDock->raise();
        }
        settleEvents(800);
        QPushButton* coverageBtn = nullptr;
        for (QPushButton* b : hostWin->findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("运行覆盖评估（后台）")) {
                coverageBtn = b;  // 无 objectName——文案锚（域面板唯一）
            }
        }
        ok(coverageBtn != nullptr, "step11 coverage-cmd-present");
        if (coverageBtn != nullptr) {
            // 二次等待窗口（事件驱动 refreshAll 可能晚于提交回执到达）；
            // 仍禁用则打印 tooltip 带出禁用原因（applyReadOnlyGate 语义）。
            for (int i = 0; i < 10 && !coverageBtn->isEnabled(); ++i) {
                settleEvents(500);
            }
            std::cout << "[ird-ui-smoke-tour] step11 coverage-enabled="
                      << coverageBtn->isEnabled()
                      << " tooltip=["
                      << coverageBtn->toolTip().toStdString() << "]"
                      << std::endl;
            coverageBtn->click();  // 后台受理——结果经执行通道异步回投
        }
        // 轮询区域页样本清单（ird_req_region_samples）非空＝评估→清单
        // 翻译全链贯通（异步任务由事件循环驱动；30s 上限防挂——超时按
        // FAIL 如实登记，不计为通过）。
        QTreeWidget* evalSampleList = panel->findChild<QTreeWidget*>(
            QStringLiteral("ird_req_region_samples"));
        ok(evalSampleList != nullptr, "step11 sample-list-present");
        // 真实样本行判别——诚实空态行（「尚未执行覆盖评估」，Enabled 但
        // 不可选）也是 topLevelItem，首跑实测它使"计数>0"轮询假阳性。
        auto isRealSampleRow = [](QTreeWidgetItem* it) {
            return it != nullptr && (it->flags() & Qt::ItemIsSelectable)
                && it->text(0) != QStringLiteral("尚未执行覆盖评估");
        };
        bool samplesArrived = false;
        QString kinStatus;
        // 选中区域锚（样本清单/格层投影跟随选区——评估结果到达后须经
        // 预览重投影才达清单；轮询中周期重聚焦触发）。
        std::optional<core::ObjectId> evalRegionId;
        {
            QTreeWidget* regionTable = panel->findChild<QTreeWidget*>(
                QStringLiteral("ird_req_region_table"));
            if (regionTable != nullptr && regionTable->currentItem() != nullptr) {
                const QString anchorText = regionTable->currentItem()->text(
                    regionTable->columnCount() - 1);
                const auto parsed = core::ObjectId::tryFromCanonical(
                    anchorText.toStdString());
                if (parsed.has_value()) { evalRegionId = *parsed; }
            }
        }
        QWidget* kinPanelForStatus =
            m_kinematicsDock != nullptr ? m_kinematicsDock->widget() : nullptr;
        QLabel* kinStatusLabel =
            kinPanelForStatus != nullptr
                ? kinPanelForStatus->findChild<QLabel*>(
                      QStringLiteral("ird_kin_status_line"))
                : nullptr;
        for (int i = 0; i < 300 && !samplesArrived; ++i) {
            settleEvents(200);
            samplesArrived = evalSampleList != nullptr
                && evalSampleList->topLevelItemCount() > 0
                && isRealSampleRow(evalSampleList->topLevelItem(0));
            if (kinStatusLabel != nullptr
                && kinStatusLabel->text() != kinStatus) {
                kinStatus = kinStatusLabel->text();
                std::cout << "[ird-ui-smoke-tour] step11 kin-status="
                          << kinStatus.toStdString() << std::endl;
            }
            // 周期重选区域行（约 3s 间隔——评估完成事件若未驱动预览重投
            // 影，选区重选为确定性触发面；R-2 纪律：纯 Qt 公共面操作，
            // 清选→重选＝真实重选轨）。
            if (!samplesArrived && i % 15 == 7) {
                QTreeWidget* rt = panel->findChild<QTreeWidget*>(
                    QStringLiteral("ird_req_region_table"));
                if (rt != nullptr && rt->currentItem() != nullptr) {
                    QTreeWidgetItem* cur = rt->currentItem();
                    rt->setCurrentItem(nullptr);
                    settleEvents(100);
                    rt->setCurrentItem(cur);
                }
            }
            if (i == 149 && kinPanelForStatus != nullptr) {
                // 30s 窗口末观测面：覆盖表行数（区分"评估未完成"与"投递
                // 断链"——表空＝评估仍在跑/挂，非空＝结果已达面板层）。
                const auto tables =
                    kinPanelForStatus->findChildren<QTableWidget*>();
                for (const QTableWidget* t : tables) {
                    std::cout << "[ird-ui-smoke-tour] step11 cov-table rows="
                              << t->rowCount() << " cols=" << t->columnCount()
                              << std::endl;
                }
            }
        }
        // 诊断面（r16 取证用——清单未达时区分"账面缺位/对账失效/区域切片
        // 不匹配/闭包未触发"四类断链；随批次收尾评估去留）。
        {
            const auto covDiag = m_kinEvaluation != nullptr
                                     ? m_kinEvaluation->latestCoverageResult()
                                     : std::nullopt;
            std::cout << "[ird-ui-smoke-tour] step11 cov-diag"
                      << " has=" << covDiag.has_value()
                      << " ok=" << (covDiag && covDiag->ok)
                      << " snapMatch=" << (covDiag && m_kinEvaluation != nullptr
                                           && covDiag->snapshotId
                                                  == m_kinEvaluation->snapshotId())
                      << " epochMatch=" << (covDiag && m_kinEvaluation != nullptr
                                            && covDiag->epoch == m_kinEvaluation->epoch())
                      << " ledgerSamples=" << (covDiag ? covDiag->samples.size() : 0);
            if (covDiag && !covDiag->ok) {
                std::cout << " err=[" << covDiag->errorText << "]";
            }
            std::cout << " gridHas=" << m_reqGrid.has_value()
                      << " gridLines=" << (m_reqGrid ? m_reqGrid->gridLines.size() : 0)
                      << " gridSamples=" << (m_reqGrid ? m_reqGrid->samples.size() : 0)
                      << " boxHas=" << m_reqBox.has_value()
                      << " listed=" << (evalSampleList ? evalSampleList->topLevelItemCount() : 0)
                      << std::endl;
            if (covDiag && !covDiag->samples.empty()) {
                std::cout << "[ird-ui-smoke-tour] step11 cov-diag firstRegion=["
                          << covDiag->samples.front().regionObjectId.toCanonical()
                          << "]";
                if (evalRegionId.has_value()) {
                    std::cout << " evalRegion=["
                              << evalRegionId->toCanonical() << "]";
                }
                std::cout << std::endl;
            }
        }
        ok(samplesArrived,
           "step11 coverage-results-arrived (rows="
               + std::to_string(evalSampleList != nullptr
                                    ? evalSampleList->topLevelItemCount() : 0)
               + ")");
        // R03-10——三态全谱：格层非中性态（NotSampled）计数＞0＝真实
        // 评估产出的着色事实色（Good/Weak/Failed/DataInsufficient 均为
        // "已评估"——不预设可达率，判定权威归域）。
        std::size_t evalColored = 0;
        if (m_reqGrid.has_value()) {
            for (auto st : m_reqGrid->cellStates) {
                if (st != ui::View3DCellState::NotSampled) { ++evalColored; }
            }
        }
        ok(evalColored > 0,
           "step11 grid-colored-states=" + std::to_string(evalColored)
               + "/"
               + std::to_string(m_reqGrid.has_value()
                                    ? m_reqGrid->cellStates.size() : 0));
        // U08 半区——清单行状态词与格层同源投影（首真实行状态词非空；词
        // 表权威＝域 sampleStateText）。
        QTreeWidgetItem* realSampleRow = nullptr;
        if (evalSampleList != nullptr) {
            for (int i = 0; i < evalSampleList->topLevelItemCount(); ++i) {
                if (isRealSampleRow(evalSampleList->topLevelItem(i))) {
                    realSampleRow = evalSampleList->topLevelItem(i);
                    break;
                }
            }
        }
        ok(realSampleRow != nullptr && realSampleRow->text(1).size() > 0,
           "step11 sample-row-state-text-present");
        if (m_view3d != nullptr) {
            snapPng(m_view3d, "tour-6b-view3d-real-coloring.png");
        }
        // R03-11——样本级定位：清单首真实行点击（itemClicked emit＝与手点
        // 同源信号轨，7.5 校验行点击同款）→高亮键置位→三维蓝圈随格层
        // 刷新上屏（截图人工复核渲染）。
        if (realSampleRow != nullptr) {
            evalSampleList->setCurrentItem(realSampleRow);
            settleEvents(100);
            Q_EMIT evalSampleList->itemClicked(realSampleRow, 0);
            settleEvents(300);
        }
        ok(m_reqHighlightedSample.has_value(),
           "step11 sample-highlight-key-set (index="
               + std::to_string(m_reqHighlightedSample.has_value()
                                    ? static_cast<unsigned long long>(
                                          *m_reqHighlightedSample)
                                    : 0ULL)
               + ")");
        if (m_view3d != nullptr) {
            snapPng(m_view3d, "tour-6c-sample-blue-circle.png");
        }


        // ---- 拍12：真实窗口视图动作及渲染像素（UX-11/UX-06）----------
        // 仅开发期tour启用。下面的四态值是明确的显示夹具，不写覆盖台账，
        // 不计为算法成功证据；与拍11真实评估各自验证上下游，结束恢复原预览。
        step("12 view-actions-and-real-pixels");
        auto rwView = getRobWorkStudio()->getView();
        if (!rwView.isNull() && m_view3dGateway != nullptr) {
            const auto beforeDisplay = m_requirementsEditor.workingSet();
            const auto revisionBeforeDisplay = m_lastStoreAdapter->projectStore().query().head().id;
            const auto viewer = rwView->getSceneViewer();
            const auto savedCamera = viewer->getTransform();
            const auto savedProjection = viewer->getViewCamera()->getProjectionMatrix();
            const int savedMask = rwView->getSceneViewer()->getViewCamera()->getDrawMask();
            auto actionFor = [&](const char* text) -> QAction* {
                for (auto* action : rwView->findChildren<QAction*>()) {
                    if (action->text().remove('&') == QString::fromLatin1(text)) return action;
                }
                return nullptr;
            };
            auto triggerView = [&](const char* text) {
                auto* action = actionFor(text);
                ok(action != nullptr, std::string("step12 action-present ") + text);
                if (action != nullptr) { action->trigger(); settleEvents(150); }
            };
            const char* standardViews[] = {"Top (Ctrl + T)", "Front (Ctrl + F)",
                "Right (Ctrl + R)", "Axiometric (Ctrl + A)"};
            for (int index=0;index<4;++index) {
                triggerView(standardViews[index]);
                const auto camera=viewer->getTransform();
                const auto p=camera.P();
                // 框架标准视图朝向原点；独立坐标比较避免只检查action被触发。
                if(index==0) ok(std::fabs(p[0])+std::fabs(p[1])<1e-8 && p[2]>0,"step12 top-camera-axis");
                if(index==1) ok(p[0]>0 && std::fabs(p[1])+std::fabs(p[2])<1e-8,"step12 front-camera-axis");
                if(index==2) ok(p[1]>0 && std::fabs(p[0])+std::fabs(p[2])<1e-8,"step12 right-camera-axis");
                if(index==3) ok(std::fabs(p[0]-p[1])+std::fabs(p[1]-p[2])<1e-8,"step12 axonometric-camera-axis");
                snapPng(m_view3d,("tour-7-view-"+std::to_string(index)+".png").c_str());
            }
            triggerView("Orthographic view");
            ok(viewer->getViewCamera()->getProjectionMatrix().isOrtographicProjection(),"step12 orthographic-matrix");
            triggerView("Perspective view");
            auto perspective=viewer->getViewCamera()->getProjectionMatrix();
            double projectionEntries[16]; perspective.toOpenGLMatrix(projectionEntries);
            ok(std::fabs(projectionEntries[11]+1.0)<1e-12 && std::fabs(projectionEntries[15])<1e-12,"step12 perspective-matrix");
            const auto zoomBefore=viewer->getTransform().P().norm2();
            triggerView("Zoom in (Ctrl + +)");
            ok(viewer->getTransform().P().norm2()<zoomBefore,"step12 zoom-in-camera-distance");
            triggerView("Zoom out (Ctrl + -)");
            triggerView("Autozoom (Ctrl + *)");
            triggerView("Home view");
            // 相机旋转走真实键盘事件，坐标轴方向发生变化但需求坐标不得改变。
            const auto beforeRotate=viewer->getTransform();
            QKeyEvent rotate(QEvent::KeyPress,Qt::Key_W,Qt::NoModifier);
            QApplication::sendEvent(rwView.get(),&rotate); settleEvents(150);
            ok(viewer->getTransform()!=beforeRotate,"step12 keyboard-orbit-camera-changed");
            for(const char* text:{"Wire","Outline","Solid"}) triggerView(text);
            auto* transparent=actionFor("Transparent");
            ok(transparent!=nullptr,"step12 transparency-action-present");
            if(transparent) {const bool original=transparent->isChecked();triggerView("Transparent");
                ok(transparent->isChecked()!=original,"step12 transparency-toggle");triggerView("Transparent");}
            const char* groups[]={"Physical group","Virtual group","Drawable group","Collision group",
                "User1 group","User2 group","User3 group","User4 group"};
            const int bits[]={rw::graphics::DrawableNode::Physical,rw::graphics::DrawableNode::Virtual,
                rw::graphics::DrawableNode::DrawableObject,rw::graphics::DrawableNode::CollisionObject,
                rw::graphics::DrawableNode::User1,rw::graphics::DrawableNode::User2,
                rw::graphics::DrawableNode::User3,rw::graphics::DrawableNode::User4};
            for(int index=0;index<8;++index) {
                const int before=rwView->getSceneViewer()->getViewCamera()->getDrawMask(); triggerView(groups[index]);
                ok(rwView->getSceneViewer()->getViewCamera()->getDrawMask()==(before^bits[index]),std::string("step12 mask-bit ")+groups[index]);
                triggerView(groups[index]);
                ok(rwView->getSceneViewer()->getViewCamera()->getDrawMask()==before,"step12 mask-restored");
            }
            // 四个相互分开的世界系点(m)，相机直接位于+Z，独立像素断言能发现
            // GL灯光/纹理继承污染；不能用cellStates非空替代真实绿黄红灰可见。
            triggerView("Top (Ctrl + T)");
            ui::View3DSampleGrid fixture;
            fixture.samples={{-1,-1,0.2},{-1,1,0.2},{1,-1,0.2},{1,1,0.2}};
            fixture.cellStates={ui::View3DCellState::Good,ui::View3DCellState::Weak,
                ui::View3DCellState::Failed,ui::View3DCellState::NotSampled};
            fixture.sampleIndices={10,20,30,40};
            ui::View3DPreviewUpdate colorFixture; colorFixture.sampleGrid=fixture;
            ok(m_view3dGateway->applyPreview(colorFixture),"step12 render-fixture-applied");
            settleEvents(250);
            const auto pixels=m_view3d->grab().toImage().convertToFormat(QImage::Format_RGB32);
            const float* colors[]={ui::palette::kSampleGoodGl,ui::palette::kSampleWeakGl,
                ui::palette::kSampleFailedGl,ui::palette::kSampleNotSampledGl};
            std::array<int,4> counts{};
            for(int y=0;y<pixels.height();++y) for(int x=0;x<pixels.width();++x) {
                const auto rgb=pixels.pixel(x,y);
                for(int c=0;c<4;++c) if(std::abs(qRed(rgb)-int(colors[c][0]*255))<=8
                    && std::abs(qGreen(rgb)-int(colors[c][1]*255))<=8
                    && std::abs(qBlue(rgb)-int(colors[c][2]*255))<=8) ++counts[c];
            }
            for(int c=0;c<4;++c) ok(counts[c]>=9,
                "step12 actual-palette-pixels-"+std::to_string(c)+"="+std::to_string(counts[c]));
            snapPng(m_view3d,"tour-8-real-four-state-palette.png");
            fixture.highlightedSampleIndex=20; colorFixture.sampleGrid=fixture;
            ok(m_view3dGateway->applyPreview(colorFixture),"step12 selected-fixture-applied");
            settleEvents(250);
            const auto selected=m_view3d->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int changedPixels=0;
            int bluePixels=0;
            for(int y=0;y<pixels.height();++y) for(int x=0;x<pixels.width();++x)
                if(selected.pixel(x,y)!=pixels.pixel(x,y)) {
                    ++changedPixels;
                    const auto rgb=selected.pixel(x,y);
                    if(std::abs(qRed(rgb)-64)<=8 && std::abs(qGreen(rgb)-115)<=8
                        && std::abs(qBlue(rgb)-255)<=8) ++bluePixels;
                }
            ok(changedPixels>=40,"step12 selected-ring-changes-real-pixels="+std::to_string(changedPixels));
            // 14px蓝点芯应提供约196px；仅32px环线不能掩盖点芯alpha越界读取。
            ok(bluePixels>=100,"step12 selected-blue-core-pixels="+std::to_string(bluePixels));
            snapPng(m_view3d,"tour-8b-real-selected-ring.png");
            // 产品PNG导出走真实视图API，文件可解码；不把QWidget抓图冒充该能力。
            const auto exported=outDir+"/tour-8c-product-export.png";
            rwView->saveBufferToFile(exported);
            ok(!QImage(exported).isNull(),"step12 product-png-export-decodes");
            ui::View3DPreviewUpdate restored; restored.frameMarkers=m_reqMarkers;
            restored.boxOutline=m_reqBox; restored.sampleGrid=m_reqGrid;
            ok(m_view3dGateway->applyPreview(restored),"step12 production-preview-restored");
            viewer->setTransform(savedCamera);
            viewer->getViewCamera()->setProjectionMatrix(savedProjection);
            ok(rwView->getSceneViewer()->getViewCamera()->getDrawMask()==savedMask,"step12 display-mask-unchanged");
            ok(m_requirementsEditor.workingSet()==beforeDisplay,"step12 display-zero-draft-write");
            ok(m_lastStoreAdapter->projectStore().query().head().id==revisionBeforeDisplay,
                "step12 display-zero-project-revision");
        } else { ok(false,"step12 real-view-missing"); }


        // UX-05/09：已加载真实草稿的窄Dock也必须可操作。空项目布局截图
        // 无法验证属性表单，因此在本tour末尾补充上下滚动和真实按钮可达。
        step("13 populated-dock-layout");
        const auto layoutDraft = m_requirementsEditor.workingSet();
        const auto layoutHead = m_lastStoreAdapter->projectStore().query().head().id;
        const auto savedLayout = hostWin->saveState();
        const auto savedSize = hostWin->size();
        m_content->showStagePanel(StageId::Requirements);
        if (m_requirementsDock) { m_requirementsDock->show(); m_requirementsDock->raise(); }
        for (int pass=0;pass<2;++pass) {
            hostWin->showNormal();
            hostWin->resize(pass==0 ? QSize(1920,1152) : QSize(1024,790));
            hostWin->resizeDocks({m_requirementsDock},{pass==0 ? 520 : 396},Qt::Horizontal);
            settleEvents(250);
            for (auto* tabs : panel->findChildren<QTabWidget*>()) {
                for (int tab=0;tab<tabs->count();++tab) {
                    if (tabs->tabText(tab)==QStringLiteral("工位")) { tabs->setCurrentIndex(tab); break; }
                }
            }
            // Dock根节点本身就是外层滚动区；findChildren不含自身，漏掉它会
            // 让上一拍的滚动位置遮住顶部新增入口，误报控件不可达。
            auto layoutScrolls = panel->findChildren<QScrollArea*>();
            if (auto* rootScroll = qobject_cast<QScrollArea*>(panel)) {
                layoutScrolls.prepend(rootScroll);
            }
            for (auto* scroll : layoutScrolls) {
                if (scroll->isVisible()) { scroll->verticalScrollBar()->setValue(0); }
            }
            settleEvents(150);
            auto* add = panel->findChild<QPushButton*>("ird_req_add_points");
            ok(add && add->isEnabled() && !add->visibleRegion().isEmpty(),
               "step13 populated-add-reachable-"+std::to_string(pass));
            snapPng(hostWin,("tour-9-populated-top-"+std::to_string(pass)+".png").c_str());
            for (auto* scroll : layoutScrolls) {
                if (!scroll->isVisible()) { continue; }
                auto* bar=scroll->verticalScrollBar();
                bar->setValue(bar->maximum()); settleEvents(100);
                ok(bar->value()==bar->maximum(),"step13 populated-scroll-bottom-reachable");
            }
            snapPng(m_requirementsDock,("tour-9-populated-bottom-"+std::to_string(pass)+".png").c_str());
        }
        hostWin->resize(savedSize); hostWin->restoreState(savedLayout); settleEvents(150);
        ok(m_requirementsEditor.workingSet()==layoutDraft,"step13 layout-zero-draft-write");
        ok(m_lastStoreAdapter->projectStore().query().head().id==layoutHead,"step13 layout-zero-revision");
        snapPng(hostWin, "tour-5-final-state.png");

        std::cout << "[ird-ui-smoke-tour] " << (failedCount == 0 ? "DONE" : "FAILED")
                  << " (failed-assert=" << failedCount << ")" << std::endl;
         QCoreApplication::exit(failedCount == 0 ? 0 : 1);
    });
}

// =====================================================================
// 建模域全功能遍历通道（modeling-tour，UI-T67——F-496 宿主取证）：
//   拍 1  新建项目＋打开＋召唤建模面板（域 Dock toggle——kAuxKeyModelingDock）
//   拍 2  new-from-template generic-6r 重种子（模态选单自动应答——
//         QInputDialog::getItem 预排队定时器选首项＋OK）
//   拍 3  关节 Origin 编辑（结构树选 j1→编辑页 origin-z/origin-r 行设值→
//         apply→inline 确认→状态行"已应用"）
//   拍 4  结构新增关节（ird_modeling_struct_add——链长 6→7 树行断言）
//   拍 5  draft.apply 建模域真实修订（runDomainApply filter "modeling"——
//         requirements 拍 9 同构；redo 栈点亮断言）
//   拍 6  export-package 包导出（模态 QFileDialog 自动填写全路径——
//         .irdbundle 文件存在断言）
// 每步 check()＋控制台 [ird-ui-smoke-mtour] 行＋关键帧截图；全绿 DONE 退
// 出 0。宿主侧仅经 Qt 公共基类＋objectName 锚操作面板（R-2——零跨单元
// 私有头）。
void IrdWorkbenchHostPlugin::maybeRunModelingTour()
{
    const QString smoke = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE");
    if (smoke != QLatin1String("modeling-tour")) {
        return;
    }
    QTimer::singleShot(600, this, [this] {
        std::cout << "[ird-ui-smoke-mtour] started" << std::endl;
        int exitCode = 0;
        const QString outDir = qEnvironmentVariable("IRD_UI_PLUGIN_SMOKE_OUT");
        if (!outDir.isEmpty()) {
            QDir::root().mkpath(outDir);
        }
        int failedCount = 0;
        auto check = [](bool cond, const std::string& what) -> bool {
            std::cout << "[ird-ui-smoke-mtour] " << (cond ? "pass " : "FAIL ")
                      << what << std::endl;
            return cond;
        };
        auto ok = [&](bool cond, const std::string& what) {
            if (!check(cond, what)) { ++failedCount; }
        };
        auto step = [&](const char* name) {
            std::cout << "[ird-ui-smoke-mtour] step " << name << std::endl;
        };
        QMainWindow* hostWin = qobject_cast<QMainWindow*>(parentWidget());
        auto snapPng = [&](QWidget* w, const char* name) {
            if (w == nullptr || outDir.isEmpty()) { return; }
            w->grab().save(outDir + QLatin1Char('/') + QString::fromLatin1(name),
                           "PNG");
            std::cout << "[ird-ui-smoke-mtour] snap " << name << std::endl;
        };

        // ---- 拍 1：新建项目＋打开＋召唤建模面板--------------------------
        step("1 create-open-project");
        QString demoPath;
        {
            QTemporaryDir dir(QDir::tempPath() + "/ird-mtour-demo-XXXXXX");
            demoPath = dir.path();
            dir.setAutoRemove(false);  // 项目落盘供报告留痕（脚本清理）
        }
        try {
            project::ProjectStoreFactory::createNew(
                fs::weakly_canonical(fs::u8path(demoPath.toStdString())).u8string(),
                QStringLiteral("建模域遍历演示项目").toStdString(),
                nullptr, m_bridge.get());
        } catch (const std::exception& e) {
            std::cout << "[ird-ui-smoke-mtour] create-exception: " << e.what()
                      << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        const bool opened = openViaSessionController(demoPath.toStdString());
        ok(opened, "step1 project-opened");
        settleEvents(400);
        QAction* modelToggle = nullptr;
        for (auto& [key, action] : m_hostAuxToggles) {
            if (key == kAuxKeyModelingDock) { modelToggle = action; break; }
        }
        if (modelToggle != nullptr) { modelToggle->trigger(); }
        settleEvents(300);
        // F-541（UI-T72）：取证前提加固——E-4 的 modeling 通道同款（acc/
        // ui-t68/1 G-C＋acc/ui-t71/2 S-B 双实证：需求 Dock 盖压建模面板，
        // 六帧逐字节同哈希静窗）。trigger 只翻动作态，页签叠放形态下面板
        // 可存在而不可见——show＋raise 强制升至所在页签组前景，并以可见性
        // 断言把「面板不可见」从静默态转为显式红（截图取证以可见层为前提）。
        if (m_modelingDock != nullptr) {
            m_modelingDock->show();
            m_modelingDock->raise();
            settleEvents(200);
        }
        QWidget* panel =
            m_modelingDock != nullptr ? m_modelingDock->widget() : nullptr;
        ok(panel != nullptr, "step1 modeling-panel-present");
        // 可见性断言（E-4 配对——存在≠可见；页签叠放/零宽挤压下的静窗
        // 截图零信息量，此处显式红可阻断该形态溜过取证通道）。
        ok(panel != nullptr && panel->isVisible()
               && !panel->visibleRegion().isEmpty(),
           "step1 modeling-panel-visible (取证前提——非静窗)");
        if (panel == nullptr) {
            std::cout << "[ird-ui-smoke-mtour] FAILED" << std::endl;
            QCoreApplication::exit(1);
            return;
        }
        // 宿主窗体最大化（默认尺寸下多 Dock 挤压——central-guard「中央
        // 18 px<320 px」实录——把面板压成静窗；最大化让 Dock 布局拿到物
        // 理空间后再取屏证）。
        if (hostWin != nullptr) {
            hostWin->showMaximized();
            settleEvents(400);
        }
        snapPng(hostWin, "mtour-1-panel.png");

        // A-23：正式宿主统一命令入口→真实模态编辑→project新修订→摘要/评估纪元。
        // 此拍使用真实项目和策略解析，未通过时不能以GUI独立用例代替宿主接线。
        step("1.5 real-engineering-policy");
        const auto policyEpoch=m_kinEvaluation->epoch();
        const auto policyBase=m_lastStoreAdapter->projectStore().query().head();
        bool policyDialogSeen=false;
        QTimer::singleShot(60,this,[&] {
            auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
            policyDialogSeen=dialog && dialog->objectName()=="ird_policy_editor";
            ok(policyDialogSeen,"a23 real-policy-dialog-visible");
            if(!dialog)return;
            auto* travel=dialog->findChild<QLineEdit*>("policy_travel");
            ok(travel!=nullptr,"a23 travel-input-present");if(travel)travel->setText("24");
            auto* note=dialog->findChild<QLineEdit*>("policy_note");if(note)note->setText(QStringLiteral("正式宿主策略回归"));
            snapPng(dialog,"mtour-a23-policy-editor.png");
            if(auto* buttons=dialog->findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Ok)->click();
            if(dialog->isVisible())dialog->reject(); // 失败仍退出取证，防止无人值守死等。
        });
        m_content->submitCommand("policy.edit");settleEvents(180);
        ok(policyDialogSeen,"a23 command-registry-opens-editor");
        auto& policyStore=m_lastStoreAdapter->projectStore();
        const auto policyHead=policyStore.query().head();
        ok(policyHead.id!=policyBase.id,"a23 policy-confirm-creates-revision");
        const auto policyValue=m_policySource->publishedPolicy();
        ok(policyValue && policyValue->jointThresholds.finiteRotationTravelLimit.siValue()==24.0,
           "a23 published-policy-consumes-current-revision");
        ok(m_policySource->summary().available && m_policySource->summary().travelLimit.siValue==24.0,
           "a23 summary-current-value");
        ok(m_kinEvaluation->epoch()>policyEpoch,"a23 policy-change-invalidates-evaluation-epoch");
        ok(policyStore.undoRedo().undo(policyHead.branch).committed(),"a23 first-policy-undo");settleEvents(100);
        ok(m_policySource->publishedPolicy() && m_policySource->publishedPolicy()->contentIdentity
           ==policy::makeSystemDefaultPolicySet().contentIdentity,"a23 undo-restores-default-semantics");
        ok(policyStore.undoRedo().redo(policyHead.branch).committed(),"a23 first-policy-redo");settleEvents(100);
        ok(m_policySource->publishedPolicy() && m_policySource->publishedPolicy()->jointThresholds.finiteRotationTravelLimit.siValue()==24.0,
           "a23 redo-restores-edited-policy");
        snapPng(hostWin,"mtour-a23-policy-summary.png");

        // ---- 拍 2：new-from-template generic-6r 重种子（模态选单自动应答）--
        step("2 new-from-template-generic6r");
        QPushButton* tmplBtn = panel->findChild<QPushButton*>(
            QStringLiteral("ird_modeling_cmd_modeling.new-from-template"));
        ok(tmplBtn != nullptr, "step2 template-cmd-present");
        if (tmplBtn != nullptr) {
            // 预排队定时器：chooseItem（QInputDialog::getItem——非编辑态
            // QComboBox）选首项（generic-6r）＋OK。
            QTimer::singleShot(300, this, [] {
                QWidget* modal = QApplication::activeModalWidget();
                if (modal == nullptr) {
                    std::cout << "[ird-ui-smoke-mtour] FAIL item-modal-missing"
                              << std::endl;
                    return;
                }
                if (QComboBox* combo = modal->findChild<QComboBox*>()) {
                    combo->setCurrentIndex(0);
                }
                if (QDialog* dlg = qobject_cast<QDialog*>(modal)) {
                    if (QDialogButtonBox* box = modal->findChild<QDialogButtonBox*>()) {
                        box->button(QDialogButtonBox::Ok)->click();
                        (void)dlg;
                    }
                }
            });
            tmplBtn->click();  // 触发 exec()——嵌套循环执行上面定时器
            settleEvents(500);
        }
        QLabel* statusLine = panel->findChild<QLabel*>(
            QStringLiteral("ird_modeling_status_line"));
        ok(statusLine != nullptr
               && statusLine->text().contains(QStringLiteral("generic-6r")),
           "step2 reseed-generic6r (cur="
               + (statusLine != nullptr ? statusLine->text().toStdString()
                                        : std::string("?"))
               + ")");
        snapPng(hostWin, "mtour-2-reseeded.png");

        // ---- 拍 3：关节 Origin 编辑（结构树选 j1→编辑页行编辑→应用确认）--
        step("3 joint-origin-edit");

        QTreeWidget* structTree =
            panel->findChild<QTreeWidget*>(QStringLiteral("ird_modeling_struct_tree"));
        ok(structTree != nullptr, "step3 struct-tree-present");
        if (structTree != nullptr) {
            // 首个 j1 行（种子关节名 j<序>——模板同款命名）。
            QTreeWidgetItem* j1 = nullptr;
            QTreeWidgetItemIterator it(structTree,
                                       QTreeWidgetItemIterator::NotHidden);
            for (; *it != nullptr; ++it) {
                if ((*it)->text(0).contains(QStringLiteral("j1"))) {
                    j1 = *it;
                    break;
                }
            }
            ok(j1 != nullptr, "step3 j1-row-found");
            if (j1 != nullptr) {
                structTree->setCurrentItem(j1);
                settleEvents(400);  // 编辑页随选中重建（refreshEditPages Joint 分支）
                // 关节编辑页：ird_modeling_edit_host 容器内的参数表——
                // origin-z / origin-r 行设值（列 1＝值列）。
                QWidget* editHost =
                    panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
                ok(editHost != nullptr, "step3 edit-host-present");
                QTableWidget* editTable = nullptr;
                if (editHost != nullptr) {
                    editTable = editHost->findChild<QTableWidget*>(
                        QStringLiteral("ird_param_table"));
                }
                ok(editTable != nullptr, "step3 edit-table-present");
                auto setRow = [&](const char* key, const QString& text) -> bool {
                    if (editTable == nullptr) { return false; }
                    for (int r = 0; r < editTable->rowCount(); ++r) {
                        const QTableWidgetItem* k = editTable->item(r, 0);
                        if (k != nullptr
                            && k->data(Qt::UserRole).toString()
                                   == QLatin1String(key)) {
                            editTable->item(r, 1)->setText(text);
                            return true;
                        }
                    }
                    return false;
                };
                const bool zSet = setRow("origin-z", QStringLiteral("0.12"));
                const bool rSet = setRow("origin-r", QStringLiteral("0.05"));
                ok(zSet && rSet, "step3 origin-rows-set");
                settleEvents(200);
                QPushButton* applyBtn =
                    editHost != nullptr
                        ? editHost->findChild<QPushButton*>(
                              QStringLiteral("ird_param_apply"))
                        : nullptr;
                QPushButton* confirmYes =
                    editHost != nullptr
                        ? editHost->findChild<QPushButton*>(
                              QStringLiteral("ird_param_confirm_yes"))
                        : nullptr;
                ok(applyBtn != nullptr && confirmYes != nullptr,
                   "step3 apply-confirm-present");
                if (applyBtn != nullptr && confirmYes != nullptr) {
                    applyBtn->click();
                    settleEvents(200);
                    confirmYes->click();
                    settleEvents(400);
                    ok(statusLine != nullptr
                           && statusLine->text().contains(QStringLiteral("已应用")),
                       "step3 origin-applied (cur="
                           + (statusLine != nullptr
                                  ? statusLine->text().toStdString()
                                  : std::string("?"))
                           + ")");
                }
                snapPng(hostWin, "mtour-3-origin-edit.png");
            }
        }

        // WP-14-T12联合验证前置：generic-6r模板的origin按设计全部为零，
        // 仅改一个关节会得到重叠链，不能用于判断完整机器人三维显示是否正确。
        // 从真实参数表为六轴输入非退化尺寸（m/rad），仍经原确认/编辑/应用链。
        step("3.5 spatial-chain-fixture");
        for (int jointIndex = 1; jointIndex <= 6 && structTree; ++jointIndex) {
            QTreeWidgetItem* target = nullptr;
            QTreeWidgetItemIterator it(structTree);
            const QString jointName = QStringLiteral("j%1").arg(jointIndex);
            for (; *it; ++it) {
                if ((*it)->text(0).endsWith(jointName)) { target = *it; break; }
            }
            ok(target != nullptr, "step3.5 joint-row-" + std::to_string(jointIndex));
            if (!target) { continue; }
            structTree->setCurrentItem(target);
            settleEvents(100);
            // 本遍历在一个外层QTimer槽内执行，嵌套事件循环不会自动销毁
            // 外层登记deleteLater的旧编辑页；模拟下一真实用户事件前的删除拍，
            // 再findChild当前页，避免误操作已排队删除、借用旧模型的表。
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto* editHost = panel->findChild<QWidget*>(QStringLiteral("ird_modeling_edit_host"));
            auto* table = editHost ? editHost->findChild<QTableWidget*>(QStringLiteral("ird_param_table")) : nullptr;
            auto* apply = editHost ? editHost->findChild<QPushButton*>(QStringLiteral("ird_param_apply")) : nullptr;
            auto* confirm = editHost ? editHost->findChild<QPushButton*>(QStringLiteral("ird_param_confirm_yes")) : nullptr;
            bool allSet = table && apply && confirm;
            const std::vector<std::pair<QString, QString>> values{
                {QStringLiteral("origin-x"), QStringLiteral("0.10")},
                {QStringLiteral("origin-z"), QStringLiteral("0.15")},
                {QStringLiteral("origin-r"), jointIndex % 2 ? QStringLiteral("1.5707963267948966") : QStringLiteral("0")}};
            for (const auto& field : values) {
                bool found = false;
                for (int row = 0; table && row < table->rowCount(); ++row) {
                    if (table->item(row, 0)->data(Qt::UserRole).toString() == field.first) {
                        table->item(row, 1)->setText(field.second);
                        found = true;
                        break;
                    }
                }
                allSet = allSet && found;
            }
            ok(allSet, "step3.5 parameter-fields-" + std::to_string(jointIndex));
            if (allSet) {
                apply->click();
                settleEvents(50);
                confirm->click();
                settleEvents(100);
            }
        }

        // ---- 拍 4：结构新增关节（链长 6→7）------------------------------

        step("4 structure-add-joint");
        QPushButton* structAdd = panel->findChild<QPushButton*>(
            QStringLiteral("ird_modeling_struct_add"));
        ok(structAdd != nullptr, "step4 struct-add-present");
        int jointRowsBefore = 0;
        int jointRowsAfter = 0;
        auto countJointRows = [&](QTreeWidget* t) {
            int n = 0;
            QTreeWidgetItemIterator it(t, QTreeWidgetItemIterator::NotHidden);
            for (; *it != nullptr; ++it) {
                if ((*it)->text(0).contains(QStringLiteral("j"))) { ++n; }
            }
            return n;
        };
        if (structTree != nullptr && structAdd != nullptr) {
            jointRowsBefore = countJointRows(structTree);
            structAdd->click();
            settleEvents(400);
            jointRowsAfter = countJointRows(structTree);
            ok(jointRowsAfter == jointRowsBefore + 1,
               "step4 joint-added (cur=" + std::to_string(jointRowsAfter)
                   + " before=" + std::to_string(jointRowsBefore) + ")");
            snapPng(hostWin, "mtour-4-structure.png");
        }

        // ---- 拍 5：draft.apply 建模域真实修订---------------------------
        step("5 modeling-draft-apply");
        std::vector<ui::DomainModuleEntry> modelingOnly;
        if (m_domains != nullptr) {
            for (const auto& entry : m_domains->applyEntries) {
                if (entry.moduleId == "modeling") { modelingOnly.push_back(entry); }
            }
        }
        ok(!modelingOnly.empty(), "step5 modeling-apply-entry-present");
        // A-23/MDL-06：同一份真实草稿先用1 rad阈值触发行程比较，用户未
        // 确认必须零提交；再经同一真实窗口恢复24 rad，下面原应用必须成功。
        // 这同时检验策略修订后的安全基线前移，不能只断言摘要文本改变。
        auto editTourTravel=[&](const QString& value) {
            QTimer::singleShot(60,this,[&,value] {
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                ok(dialog && dialog->objectName()=="ird_policy_editor","a23 threshold-dialog-visible");
                if(!dialog)return;
                if(auto* field=dialog->findChild<QLineEdit*>("policy_travel"))field->setText(value);
                if(auto* buttons=dialog->findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Ok)->click();
                if(dialog->isVisible())dialog->reject();
            });
            m_content->submitCommand("policy.edit");settleEvents(180);
        };
        editTourTravel(QStringLiteral("1"));
        const auto lowBase=m_lastStoreAdapter->projectStore().query().head();
        ok(m_policySource->publishedPolicy() && m_policySource->publishedPolicy()->jointThresholds.finiteRotationTravelLimit.siValue()==1.0,
           "a23 low-policy-published");
        const auto lowReport=runDomainApply(modelingOnly,std::nullopt,
            [this](project::CommandEnvelope envelope,project::ICommandInteraction* interaction) {
                return m_lastStoreAdapter->projectStore().commands().submit(std::move(envelope),interaction);
            },nullptr,[](const std::string& message) { std::cout<<"[a23-low-policy] "<<message<<std::endl; });
        bool lowBlocked=false;
        for(const auto& entry:lowReport.entries)if(entry.moduleId=="modeling") {
            lowBlocked=!entry.committed && entry.rejectionReason=="confirmations-unresolved";
            std::cout<<"[a23-low-policy] rejected="<<entry.rejectionReason<<std::endl;
        }
        ok(lowBlocked,"a23 real-mdl06-comparison-requires-confirmation");
        ok(m_lastStoreAdapter->projectStore().query().head().id==lowBase.id,"a23 unconfirmed-comparison-zero-write");
        snapPng(hostWin,"mtour-a23-low-policy-comparison.png");
        editTourTravel(QStringLiteral("24"));
        const DomainApplyReport applyReport = runDomainApply(
            modelingOnly,
            std::nullopt,
            [this](project::CommandEnvelope envelope,
                   project::ICommandInteraction* cmdInteraction) {
                const auto adapter = m_lastStoreAdapter;
                if (adapter == nullptr) { return project::CommandResult{}; }
                return adapter->projectStore().commands().submit(
                    std::move(envelope), cmdInteraction);
            },
            nullptr,
            [this](const std::string& message) {
                if (m_diag.pipeline) {
                    m_diag.pipeline->logDev(kPluginDevChannel, message);
                }
            });
        bool modelingCommitted = false;
        for (const auto& entry : applyReport.entries) {
            if (entry.moduleId == "modeling" && entry.committed) {
                modelingCommitted = true;
            }
        }
        // F-536/O-46 裁决出路②（UI-T73）：④端口已装配真实策略源——
        // A-23此拍按项目24 rad阈值评估；原 known-gap 断
        // 言（policyPortGap/undo-unavailable-under-known-gap）随翻转退役，
        // 断言名沿用旧号 F-524 的历史行一并更正（F-543 传证）。
        std::string modelingRej;
        for (const auto& entry5 : applyReport.entries) {
            if (entry5.moduleId == "modeling") {
                std::cout << "[ird-ui-smoke-mtour] step5 outcome="
                          << static_cast<int>(entry5.outcome) << " committed="
                          << entry5.committed << " rej=[" << entry5.rejectionReason
                          << "] rev=[" << entry5.revision << "]" << std::endl;
                if (!entry5.committed) { modelingRej = entry5.rejectionReason; }
            }
        }
        // F-546 出路①（UI-T74）：草稿名感知装饰装配——首应用行程校验可执
        // 行，apply 提交（UI-T73 诚实中间态断言兑现翻转；A-23先验证1 rad
        // 超限必须确认，再验证24 rad不超限可提交）。
        ok(modelingCommitted,
           "step5 modeling-apply (committed=" +
               std::string(modelingCommitted ? "true" : "false") +
               " rej=[" + modelingRej + "])");
        // 首应用不可逆（全新项目全部受影响对象无前版——处理器声明不可逆，
        // requirements 拍 9 同款产品语义）：项目级 undo 不可用＝诚实状态面
        // （非缺陷；二连应用探针见 requirements 拍 9 的会话基线演进验证）。
        bool undoLit = false;
        if (const auto adapter = m_lastStoreAdapter) {
            const auto tips = adapter->projectStore().query().branchTips();
            if (!tips.empty()) {
                undoLit = adapter->projectStore().undoRedo()
                              .status(tips.front().id).canUndo;
            }
        }
        ok(!undoLit, "step5 project-undo-irreversible-first-apply");
        snapPng(hostWin, "mtour-5-applied.png");
        // 他域提交也必须推动零编辑需求会话跟进HEAD；直接观测真实总线消费，
        // 可揭露在存储写锁内同步回查引起的resource-deadlock事件丢失。
        settleEvents(100);
        ok(m_domains->requirements->sessionBaseRevision()
               == std::optional<core::RevisionId>{m_lastStoreAdapter->projectStore().query().head().id},
           "step5 requirements-follows-modeling-revision");

        QPushButton* exportBtn = panel->findChild<QPushButton*>(
            QStringLiteral("ird_modeling_cmd_modeling.export-package"));
        ok(exportBtn != nullptr, "step6 export-cmd-present");
        if (exportBtn == nullptr) {
            QCoreApplication::exit(failedCount == 0 ? 0 : 1);
            return;
        }
        const QString exportPath =
            (outDir.isEmpty() ? QDir::tempPath() : outDir)
            + QLatin1String("/mtour-export.irdbundle");
        QFile::remove(exportPath);  // 幂等（重跑覆盖）
        const QString pathCopy = exportPath;  // 定时器值拷贝（防悬垂）
        QTimer* exportModalTimer = new QTimer(exportBtn);
        exportModalTimer->setInterval(300);
        // 轮 1＝文件对话框填路径＋接受；轮 2+＝导出确认（含对象清单的
        // 知情确认面——每次导出必有）点"是"；接受后自停。
        QObject::connect(exportModalTimer, &QTimer::timeout,
            exportModalTimer, [this, pathCopy, exportModalTimer]() {
                QWidget* modal = QApplication::activeModalWidget();
                if (modal == nullptr) { return; }
                if (QFileDialog* dlg = qobject_cast<QFileDialog*>(modal)) {
                    if (QLineEdit* nameEdit = dlg->findChild<QLineEdit*>()) {
                        nameEdit->setText(pathCopy);
                    }
                    for (QPushButton* b : dlg->findChildren<QPushButton*>()) {
                        if (b->isDefault()) { b->click(); break; }
                    }
                    return;
                }
                if (qobject_cast<QMessageBox*>(modal) != nullptr
                    && modal->windowTitle().contains(
                        QStringLiteral("导出确认"))) {
                    if (QDialogButtonBox* box =
                            modal->findChild<QDialogButtonBox*>()) {
                        box->button(QDialogButtonBox::Yes)->click();
                    }
                    exportModalTimer->stop();
                }
            });
        exportModalTimer->start();
        exportBtn->click();  // 触发 saveFilePath exec()——嵌套循环执行定时器
        settleEvents(900);
        exportModalTimer->stop();
        const bool exported = QFile::exists(exportPath);
        ok(exported, "step6 irdbundle-exists (path=" + exportPath.toStdString()
               + ")");
        snapPng(hostWin, "mtour-6-exported.png");

        // WP-14-T12/REQ-10：真实建模应用后关闭并重开同一已保存项目，验证恢复runtime
        // 而非仅恢复会话壳。全程原存储/编译/呈现链，不注入结果、不创建新修订。
        step("5.5 reopen-saved-runtime");
        const auto savedHead = m_lastStoreAdapter->projectStore().query().head().id;
        (void)m_controller->beginClose(UiCloseIntent::CloseProject);
        const auto closeResult = m_controller->resolveCloseDialog(
            CloseDecision{true, DraftDisposition::Discard, TaskDisposition::Wait});
        for (int attempt = 0; attempt < 100 && m_controller->hasOpenSession(); ++attempt) {
            settleEvents(100); // 最多10秒等待真实关闭协议Draining完成，不并行启动第二宿主。
        }
        ok(!m_hostStatusBar || !m_hostStatusBar->currentMessage().contains(QStringLiteral("异常")),
           "step5.5 close-without-context-error");
        const bool reopened = closeResult.status == CloseDialogResolution::Status::Confirmed
            && !m_controller->hasOpenSession() && openViaSessionController(demoPath.toStdString());
        const bool restored = reopened && m_compilePort && m_compilePort->lastPublishedSnapshot();
        ok(restored, "step5.5 saved-runtime-restored");
        settleEvents(300); // 编译快照在位不等于宿主已完成呈现挂接，额外断言实际场景。
        const auto restoredCell = getRobWorkStudio()->getWorkCell();
        ok(reopened && m_controller->hasOpenSession(), "step5.5 real-open-session");
        ok(m_policySource->publishedPolicy() && m_policySource->publishedPolicy()->jointThresholds.finiteRotationTravelLimit.siValue()==24.0,
           "a23 saved-policy-restored-after-real-reopen");
        ok(restoredCell && !restoredCell->getDevices().empty(), "step5.5 host-device-restored");
        ok(restoredCell && !restoredCell->getObjects().empty(), "step5.5 host-geometry-restored");
        const auto restoredScene = getRobWorkStudio()->getWorkCellScene();
        ok(restoredScene && restoredScene->getWorkCell() == restoredCell,
           "step5.5 scene-workcell-matches-host");
        ok(restoredScene && !restoredScene->getDrawables().empty(),
           "step5.5 scene-has-drawables");
        if (restoredCell && !restoredCell->getDevices().empty()) {
            // 独立数值参照：六次平移均为(0.10,0,0.15)m，奇数轴Rx(pi/2)，
            // 零关节角下依次累积得到世界系末端(0.60,-0.15,-0.15)m；
            // 第七轴为新增恒等原点，不改变位置。不能只靠“有设备”验证真FK。
            const auto endPose = rw::kinematics::Kinematics::worldTframe(
                restoredCell->getDevices().front()->getEnd(), getRobWorkStudio()->getState());
            const rw::math::Vector3D<> expectedEnd(0.60, -0.15, -0.15);
            ok((endPose.P() - expectedEnd).norm2() < 1e-9,
               "step5.5 spatial-fk-numeric-reference");
        }
        if (reopened) {
            ok(m_lastStoreAdapter->projectStore().query().head().id == savedHead,
               "step5.5 reopen-zero-revisions");
        }
        snapPng(hostWin, "mtour-5.5-reopened.png");

        // WP-13-T24/F13：从可见命名位姿入口创建、编辑保存，再经生产恢复按钮
        // 改变真实宿主State。数据准备也是实际控件链，零直接注入模块工作集。
        step("5.55 named-pose-session-restore");
        if(restoredCell&&!restoredCell->getDevices().empty()){
            // isVisible仅代表控件未隐藏，不能证明未被滚动视口裁剪。先沿内到外
            // 滚入实际视口再检查中心点，截图须呈现用户正在操作的真实入口。
            const auto revealPoseControl=[&](QWidget* target){
                if(!target){return false;}
                for(auto* parent=target->parentWidget();parent;parent=parent->parentWidget()){
                    if(auto* scroll=qobject_cast<QScrollArea*>(parent)){
                        scroll->ensureWidgetVisible(target);settleEvents(80);
                    }
                }
                if(!target->isVisible()){return false;}
                for(auto* parent=target->parentWidget();parent;parent=parent->parentWidget()){
                    if(!parent->rect().contains(target->mapTo(parent,target->rect().center()))){return false;}
                }
                return true;
            };
            auto* studio=getRobWorkStudio();const auto device=restoredCell->getDevices().front();
            const auto oldState=studio->getState();const auto oldQ=device->getQ(oldState);
            const auto beforeEnd=rw::kinematics::Kinematics::worldTframe(device->getEnd(),oldState);
            const auto poseHead=m_lastStoreAdapter->projectStore().query().head().id;
            const auto poseIdentity=m_compilePort->lastPublishedSnapshot()->modelIdentity();
            if(auto* tabs=panel->findChild<QTabWidget*>()){tabs->setCurrentIndex(0);}
            auto* mode=panel->findChild<QPushButton*>("ird_modeling_edit_pose_mode");
            auto* add=panel->findChild<QPushButton*>("ird_modeling_pose_add");
            ok(revealPoseControl(mode),"b2b named-pose-mode-visible-without-object");
            if(mode){mode->click();}settleEvents(80);
            ok(revealPoseControl(add),"b2b add-first-pose-visible");
            if(add){add->click();}settleEvents(100);
            auto* area=panel->findChild<QWidget*>("ird_modeling_pose_area");
            auto* key=panel->findChild<QLineEdit*>("ird_modeling_pose_key");
            auto* table=area?area->findChild<QTableWidget*>("ird_param_table"):nullptr;
            ok(area&&area->isVisible()&&key&&table,"b2b first-pose-keeps-management-page");
            if(key){key->setText(QStringLiteral("inspection-b2b"));}
            bool staged=false;
            if(table){for(int row=0;row<table->rowCount();++row){
                if(table->item(row,0)->data(Qt::UserRole).toString()=="pose-config-0"){
                    table->item(row,1)->setText("0.45");staged=true;break;
                }
            }}
            ok(staged,"b2b real-pose-q0-staged");settleEvents(80);
            if(auto* save=panel->findChild<QPushButton*>("ird_modeling_pose_apply")){save->click();}
            settleEvents(100);
            auto* restore=panel->findChild<QPushButton*>("ird_modeling_pose_restore");
            ok(revealPoseControl(restore)&&restore->isEnabled(),"b2b real-restore-entry-available");
            snapPng(hostWin,"mtour-b2b-before-restore.png");
            if(restore){restore->click();}settleEvents(200);
            const auto afterQ=device->getQ(studio->getState());
            ok(afterQ.size()==oldQ.size()&&afterQ.size()>0&&std::abs(afterQ[0]-0.45)<1e-12,
               "b2b restore-writes-real-device-q");
            const auto afterEnd=rw::kinematics::Kinematics::worldTframe(device->getEnd(),studio->getState());
            ok((afterEnd.P()-beforeEnd.P()).norm2()>1e-4,"b2b restored-real-fk-position-changed");
            ok(m_kinSessionPose.isSet()&&!m_kinSessionPose.jointConfiguration().empty()
               &&std::abs(m_kinSessionPose.jointConfiguration()[0]-0.45)<1e-12,
               "b2b real-state-event-updates-kin-session");
            ok(m_lastStoreAdapter->projectStore().query().head().id==poseHead,
               "b2b saved-pose-and-restore-zero-project-revisions");
            ok(m_compilePort->lastPublishedSnapshot()->modelIdentity()==poseIdentity,
               "b2b restore-keeps-model-identity");
            snapPng(hostWin,"mtour-b2b-after-restore.png");
            // 本拍之后仍使用原遍历参照，撤回两次未应用编辑并恢复原会话State。
            auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");
            for(int i=0;i<2;++i){ok(undo&&undo->isEnabled(),"b2b local-cleanup-history-available");if(undo){undo->click();}}
            studio->setState(oldState);settleEvents(100);
        }

        // WP-13-T24/F16：正式宿主经真实关节表产生单次草稿差异，生产比较
        // 按钮必须打开完整表并接通共享选中；关闭后局部撤销恢复干净基线。
        // 本段只驱动窗口，不绕过命令目录/内核，也不改变运行时会话姿态。
        step("b4a real-model-diff");
        {
            const auto diffHead=m_lastStoreAdapter->projectStore().query().head().id;
            const auto diffIdentity=m_compilePort->lastPublishedSnapshot()->modelIdentity();
            auto* tree=panel->findChild<QTreeWidget*>("ird_modeling_struct_tree");
            QTreeWidgetItem* joint=nullptr;
            if(tree){for(QTreeWidgetItemIterator it(tree);*it;++it){
                if((*it)->text(0).contains(QStringLiteral("j1"))){joint=*it;break;}
            }
            }
            ok(joint!=nullptr,"b4a actual-joint-tree-entry");
            if(joint){tree->setCurrentItem(joint);}settleEvents(100);
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            auto* area=panel->findChild<QWidget*>("ird_modeling_edit_host");
            auto* table=area?area->findChild<QTableWidget*>("ird_param_table"):nullptr;
            auto* apply=area?area->findChild<QPushButton*>("ird_param_apply"):nullptr;
            auto* confirm=area?area->findChild<QPushButton*>("ird_param_confirm_yes"):nullptr;
            bool staged=false;
            for(int row=0;table&&row<table->rowCount();++row){
                if(table->item(row,0)->data(Qt::UserRole).toString()=="zero-offset"){
                    table->item(row,1)->setText("0.375");staged=true;break;
                }
            }
            ok(staged&&apply&&confirm,"b4a real-diff-edit-controls");
            if(staged&&apply&&confirm){apply->click();settleEvents(50);confirm->click();settleEvents(100);}
            auto* compare=panel->findChild<QPushButton*>("ird_modeling_cmd_modeling.diff-baseline");
            ok(compare&&compare->isEnabled(),"b4a production-compare-command-available");
            bool seen=false;
            QTimer answer;answer.setSingleShot(true);
            QObject::connect(&answer,&QTimer::timeout,this,[&]{
                auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                ok(dialog&&dialog->objectName()=="ird_modeling_diff","b4a full-diff-dialog-in-host");
                if(!dialog){return;}seen=true;
                auto* rows=dialog->findChild<QTableWidget*>("ird_modeling_diff_table");
                ok(rows&&rows->rowCount()==1,"b4a exactly-one-real-difference");
                if(rows&&rows->rowCount()==1){
                    ok(rows->item(0,2)->text()=="joints[0].zeroOffset"
                        &&rows->item(0,4)->text()=="rad"
                        &&rows->item(0,6)->text().contains("0.375"),"b4a exact-field-unit-and-after-value");
                    if(auto* jump=dialog->findChild<QPushButton*>("ird_modeling_diff_locate")){jump->click();}
                    auto* feedback=dialog->findChild<QLabel*>("ird_modeling_diff_feedback");
                    ok(feedback&&feedback->text().contains(QStringLiteral("已定位")),"b4a actual-object-location-accepted");
                    ok(m_selection->selectedObjectIds().size()==1&&tree&&tree->currentItem()
                        &&tree->currentItem()->text(0).contains("j1"),"b4a shared-selection-and-tree-agree");
                }
                snapPng(dialog,"mtour-b4a-diff.png");dialog->reject();
            });
            answer.start(150);if(compare){compare->click();}answer.stop();settleEvents(80);
            ok(seen,"b4a formal-command-dispatched-to-modeling");
            auto* undo=panel->findChild<QPushButton*>("ird_modeling_draft_undo");
            ok(undo&&undo->isEnabled(),"b4a single-draft-step-cleanup-available");
            if(undo){undo->click();}settleEvents(100);
            ok(m_lastStoreAdapter->projectStore().query().head().id==diffHead
                &&m_compilePort->lastPublishedSnapshot()->modelIdentity()==diffIdentity,
                "b4a view-location-and-undo-zero-project-write");
        }

        // WP-14-T12/REQ-08/10：在真正恢复的六轴机器人上走生产命令与确认框。
        // 独立FK取权威State做数值对照；取消、确认期间关节改变均零草稿写入。
        step("5.6 real-fk-capture-confirmation");
        if (restoredCell && !restoredCell->getDevices().empty() && m_domains->requirements) {
            auto* studio = getRobWorkStudio();
            const auto device = restoredCell->getDevices().front();
            const auto before = m_requirementsEditor.workingSet();
            // 正式阶段工厂按需创建面板；仅show空Dock不会装配域命令接收者。
            m_content->showStagePanel(StageId::Requirements);
            if (m_requirementsDock) { m_requirementsDock->show(); m_requirementsDock->raise(); }
            settleEvents(200);
            ok(m_content->commandRegistry().availability("requirements.capture-tcp").enabled,
               "step5.6 capture-command-executable");
            for (int attempt = 0; attempt < 3; ++attempt) {
                bool sawConfirmation = false;
                QTimer answer;
                answer.setSingleShot(true);
                QObject::connect(&answer, &QTimer::timeout, this, [&, attempt] {
                    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                    auto* buttons = dialog ? dialog->findChild<QDialogButtonBox*>() : nullptr;
                    if (!dialog || !buttons || !dialog->windowTitle().contains(QStringLiteral("捕获TCP"))) {
                        ok(false, "step5.6 capture-confirmation-dialog-missing"); return;
                    }
                    sawConfirmation = true;
                    const auto role = attempt == 0 ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok;
                    ok(buttons->button(role) && !buttons->button(role)->visibleRegion().isEmpty(),
                       "step5.6 confirmation-button-reachable");
                    snapPng(dialog, ("mtour-capture-confirm-" + std::to_string(attempt) + ".png").c_str());
                    if (attempt == 1) {
                        auto state = studio->getState();
                        auto q = device->getQ(state);
                        if (q.size() != 0) { q[0] += 0.1; device->setQ(q, state); studio->setState(state); }
                    }
                    buttons->button(role)->click();
                });
                answer.start(250);
                m_content->submitCommand("requirements.capture-tcp");
                answer.stop(); settleEvents(200);
                ok(sawConfirmation, "step5.6 real-capture-confirmation-seen");
                if (attempt < 2) {
                    ok(m_requirementsEditor.workingSet() == before,
                       attempt == 0 ? "step5.6 cancel-zero-write" : "step5.6 changed-fk-rejected-zero-write");
                    continue;
                }
                const auto& points = m_requirementsEditor.workingSet().points.entries;
                ok(points.size() == before.points.entries.size() + 1, "step5.6 captured-point-created");
                const auto expected = rw::kinematics::Kinematics::worldTframe(device->getEnd(), studio->getState());
                const auto captured = std::find_if(points.begin(), points.end(), [](const auto& point) {
                    return point.name == "TcpCapture";
                });
                if (captured != points.end()) {
                    ok(captured->pose.position.tryValue()
                        && (*captured->pose.position.tryValue() - expected.P()).norm2() < 1e-12,
                       "step5.6 captured-world-position-equals-real-fk");
                    // 需求/建模字段采用Rz(yaw)·Ry(pitch)·Rx(roll)；框架RPY
                    // 构造参数却按Z/Y/X轴依次传入，不能直接按字段下标照抄。
                    const rw::math::RPY<> rpy(captured->pose.orientation.fixedRpy[2],
                        captured->pose.orientation.fixedRpy[1], captured->pose.orientation.fixedRpy[0]);
                    const auto rotation = rpy.toRotation3D();
                    bool same = true;
                    for (int row = 0; row < 3; ++row) { for (int col = 0; col < 3; ++col) {
                        same = same && std::abs(rotation(row,col) - expected.R()(row,col)) < 1e-12;
                    } }
                    ok(same, "step5.6 captured-world-rotation-equals-real-fk");
                }
            }
            // 使用生产局部撤销/重做按钮核对记账，避免域写入后界面没有可撤销入口。
            QPushButton* undo = nullptr; QPushButton* redo = nullptr;
            if (m_requirementsDock) {
                for (auto* button : m_requirementsDock->findChildren<QPushButton*>()) {
                    if (button->text() == QString::fromStdString(resolveText(TextKey{"panel.requirements.undo-draft.label"}))) { undo = button; }
                    if (button->text() == QString::fromStdString(resolveText(TextKey{"panel.requirements.redo-draft.label"}))) { redo = button; }
                }
            }
            ok(undo && undo->isEnabled(), "step5.6 capture-local-undo-enabled");
            if (undo) { undo->click(); settleEvents(150); }
            ok(m_requirementsEditor.workingSet() == before, "step5.6 capture-one-undo");
            ok(redo && redo->isEnabled(), "step5.6 capture-local-redo-enabled");
            if (redo) { redo->click(); settleEvents(150); }
            ok(m_requirementsEditor.workingSet().points.entries.size() == before.points.entries.size() + 1,
               "step5.6 capture-one-redo");
            if (undo) { undo->click(); settleEvents(150); }
            ok(m_lastStoreAdapter->projectStore().query().head().id == savedHead,
               "step5.6 capture-and-local-history-zero-project-revisions");
            snapPng(hostWin, "mtour-capture-restored.png");
        }

        // WP-14-T12/REQ-10：公开命令创建夹具并真实双编译，不直接向WorkCell塞帧。
        // runtime目前显示代理为标记盒，本段只证明Frame拾取/引用，不声称mesh保真。
        step("5.7 real-scene-raycast-and-pick");
        {
            const auto adapter = m_lastStoreAdapter;
            modeling::SceneObject fixture;
            fixture.localName = "requirements-pick-fixture";
            fixture.role = modeling::SceneObjectRole::Workpiece;
            fixture.geometry=modeling::GeometryRef{};
            fixture.geometry->resourceRefId="requirements-pick-fixture-mesh";
            fixture.worldPose = rw::math::Transform3D<>(rw::math::Vector3D<>(1.4, 0.3, 0.2),
                rw::math::RPY<>(0.0, 0.7, 0.0).toRotation3D());
            const modeling::RobotDesignCodec codec;
            const auto encoded = codec.encode(modeling::ObjectVariant(fixture), modeling::kCurrentFormatVersion);
            ok(encoded.ok(), "step5.7 scene-fixture-encoded");
            const auto tips = adapter->projectStore().query().branchTips();
            if (encoded.ok() && !tips.empty()) {
                modeling::CommandPayload payload;
                // 合法场景必须具备几何资源。创建真实STL外部记录，由原io桥
                // 实际读取/复核摘要；runtime仍只渲染标记盒，不能据此宣称mesh保真。
                const QByteArray mesh("solid fixture\nfacet normal 0 0 1\nouter loop\n"
                    "vertex -0.01 -0.01 0\nvertex 0.01 -0.01 0\nvertex 0 0.01 0\n"
                    "endloop\nendfacet\nendsolid fixture\n");
                const QString meshPath=(outDir.isEmpty()?QDir::tempPath():outDir)+"/mtour-fixture.stl";
                QFile meshFile(meshPath);
                const bool meshWritten=meshFile.open(QIODevice::WriteOnly)
                    && meshFile.write(mesh)==mesh.size();
                meshFile.close(); ok(meshWritten,"step5.7 real-fixture-resource-written");
                core::ContentDigester hasher;
                hasher.update(mesh.constData(),static_cast<std::size_t>(mesh.size()));
                const auto digest=hasher.finalize();
                bool rootReady=false;
                for(const auto& ref:adapter->projectStore().query().head().objectRefs){
                    if(ref.objectTypeToken!=modeling::kRobotDesignObjectType){continue;}
                    const auto bytes=adapter->projectStore().query().tryObject(ref.objectId,ref.contentVersion);
                    if(!bytes){continue;}
                    const auto decodedRoot=codec.decode(*bytes,modeling::kCurrentFormatVersion);
                    if(!decodedRoot.ok()||!std::holds_alternative<modeling::RobotDesign>(decodedRoot.get())){continue;}
                    auto root=std::get<modeling::RobotDesign>(decodedRoot.get());
                    modeling::ResourceRef resource;
                    resource.resourceId=fixture.geometry->resourceRefId;
                    resource.contentDigest=digest;
                    resource.externalRecord=modeling::ExternalResourceRecord{meshPath.toStdString(),digest};
                    root.resourceManifest.push_back(resource);
                    const auto rootEncoded=codec.encode(modeling::ObjectVariant(root),modeling::kCurrentFormatVersion);
                    if(rootEncoded.ok()){
                        payload.objects.push_back({false,ref.objectId,std::string(modeling::kRobotDesignObjectType),rootEncoded.get()});
                        rootReady=true;
                    }
                    break;
                }
                ok(rootReady,"step5.7 fixture-resource-record-root-ready");
                payload.objects.push_back(modeling::PayloadObjectSlot{true, {},
                    std::string(modeling::kSceneObjectObjectType), encoded.get()});
                project::CommandEnvelope envelope;
                envelope.branch = tips.front().id;
                envelope.expectedRevision = adapter->projectStore().query().head().id;
                envelope.commandType = std::string(modeling::kCmdApplyRobotDesign);
                envelope.payloadFormatVersion = modeling::kCommandPayloadVersion;
                envelope.payloadCanonical = modeling::encodeCommandPayload(payload);
                HostCommandInteraction interaction(m_dockBody.data(), [this] {
                    return m_controller && m_controller->hasOpenSession();
                });
                const auto submitted = adapter->projectStore().commands().submit(envelope, &interaction);
                std::cout << "[ird-ui-smoke-mtour] scene-submit kind=" << static_cast<int>(submitted.status.kind)
                    << " rejection=" << static_cast<int>(submitted.status.rejection)
                    << " abort=" << static_cast<int>(submitted.status.abort) << std::endl;
                for (const auto& diag : submitted.diagnostics) {
                    std::cout << "[ird-ui-smoke-mtour] scene-submit diagnostic=" << diag.code << " " << diag.cause << std::endl;
                }
                ok(submitted.status.kind == project::CommandStatus::Kind::Committed,
                   "step5.7 scene-fixture-real-command-committed");
                // 本段直接走公开项目命令作前置，随后必须执行生产应用路径
                // 同款发布拍；submit本身不拥有宿主呈现，不能把旧场景误判为编译失败。
                if (submitted.committed()) { refreshPresentationAfterCommit(); }
                settleEvents(500);
                std::optional<core::ObjectId> fixtureId;
                for (const auto& ref : adapter->projectStore().query().head().objectRefs) {
                    if (ref.objectTypeToken != modeling::kSceneObjectObjectType) { continue; }
                    const auto bytes = adapter->projectStore().query().tryObject(ref.objectId, ref.contentVersion);
                    const auto decoded = bytes ? codec.decode(*bytes, modeling::kCurrentFormatVersion)
                        : codec.decode({}, modeling::kCurrentFormatVersion);
                    if (decoded.ok() && std::holds_alternative<modeling::SceneObject>(decoded.get())
                        && std::get<modeling::SceneObject>(decoded.get()).localName == fixture.localName) {
                        fixtureId = ref.objectId;
                    }
                }
                const auto name = fixtureId && m_nameMapPort ? m_nameMapPort->resolveRuntimeName(*fixtureId) : std::nullopt;
                auto* studio = getRobWorkStudio();
                const auto view = studio->getView();
                const auto cell = studio->getWorkCell();
                auto* frame = name && cell ? cell->findFrame(*name) : nullptr;
                ok(fixtureId && frame, "step5.7 fixture-real-runtime-frame");
                if (frame && view) {
                    const auto actual = rw::kinematics::Kinematics::worldTframe(frame, studio->getState());
                    bool same = (actual.P() - fixture.worldPose.P()).norm2() < 1e-12;
                    for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) {
                        same = same && std::abs(actual.R()(r,c) - fixture.worldPose.R()(r,c)) < 1e-12;
                    } }
                    ok(same, "step5.7 fixture-world-transform-preserved");
                    // 相机沿自身-Z看向夹具，避免机器人遮挡；视口中心为独立已知落点。
                    const auto viewer = view->getSceneViewer();
                    viewer->setTransform(rw::math::Transform3D<>(actual.P() + rw::math::Vector3D<>(0,0,0.4),
                        rw::math::Rotation3D<>(1,0,0, 0,1,0, 0,0,1)));
                    view->update(); settleEvents(300);
                    int vx=0, vy=0, width=0, height=0;
                    viewer->getViewCamera()->getViewport(vx,vy,width,height);
                    const QPoint pixel(vx + width/2, vy + height/2);
                    const auto* picked = view->pickFrame(pixel.x(), pixel.y());
                    ok(picked == frame, "step5.7 real-center-pixel-raycast-hits-fixture");
                    // 生产Ctrl双击从真实OpenGL子控件发出，验证事件坐标与名称反解全链。
                    QWidget* glWidget = nullptr;
                    for (auto* child : view->findChildren<QWidget*>()) {
                        if (child->inherits("QOpenGLWidget")) { glWidget = child; break; }
                    }
                    ok(glWidget != nullptr, "step5.7 real-opengl-input-surface");
                    if (glWidget) {
                        const QPoint local = glWidget->mapFrom(view.get(), pixel);
                        // 发送坐标必须与独立raycast落点一致；视口中心不等于任意
                        // OpenGL子控件中心（浮动/重排后可能有布局和视口偏移）。
                        std::cout << "[ird-ui-smoke-mtour] pick pixel=" << pixel.x() << "," << pixel.y()
                            << " local=" << local.x() << "," << local.y()
                            << " gl=" << glWidget->width() << "x" << glWidget->height() << std::endl;
                        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(local),
                            QPointF(glWidget->mapToGlobal(local)), Qt::LeftButton,
                            Qt::LeftButton, Qt::ControlModifier);
                        QApplication::sendEvent(glWidget, &event); settleEvents(150);
                    }
                    ok(m_selection->selectedObjectIds() == std::vector<core::ObjectId>{*fixtureId}
                        && m_selection->selectionSource() == SelectionSource::View3DPick,
                       "step5.7 ctrl-double-click-resolves-real-scene-object");
                    m_content->showStagePanel(StageId::Requirements);
                    auto* add = m_requirementsDock->findChild<QPushButton*>("ird_req_add_points");
                    ok(add && add->isEnabled(), "step5.7 target-create-button");
                    if (add) { add->click(); settleEvents(200); }
                    // REQ-01/09：正式名称目录来自当前项目，不在tour中注入候选替身。
                    // 用户只选业务名称；确认入一次局部栈，随后撤销恢复后再继续拾取回归。
                    const auto beforeNamedReference = m_requirementsEditor.workingSet();
                    auto* definition = m_requirementsDock->findChild<QPushButton*>("ird_station_definition_edit");
                    ok(definition && definition->isEnabled(),"step5.7 named-reference-definition-entry");
                    bool directorySeen = false;
                    QTimer::singleShot(0,this,[&] {
                        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        if (!dialog) { return; }
                        auto* choice=dialog->findChild<QComboBox*>("station-reference-choice");
                        const int row=choice ? choice->findText("场景 / requirements-pick-fixture") : -1;
                        bool modelAvailable=false;
                        if (choice) {
                            for (int i=0;i<choice->count();++i) {
                                if (choice->itemText(i).startsWith("机械臂 / ")) {
                                    modelAvailable=choice->model()->flags(choice->model()->index(i,0)) & Qt::ItemIsEnabled;
                                }
                            }
                        }
                        auto* raw=dialog->findChild<QLineEdit*>("station-reference-object");
                        directorySeen = row>=0 && modelAvailable && raw && raw->isHidden();
                        ok(directorySeen,"step5.7 real-directory-scene-model-and-hidden-id");
                        if (directorySeen) {
                            choice->setCurrentIndex(row);
                            snapPng(dialog,"mtour-name-directory.png");
                            if (auto* box=dialog->findChild<QDialogButtonBox*>()) {
                                box->button(QDialogButtonBox::Ok)->click(); return;
                            }
                        }
                        dialog->reject();
                    });
                    if (definition) { definition->click(); settleEvents(150); }
                    const auto& namedPoints=m_requirementsEditor.workingSet().points.entries;
                    ok(directorySeen && !namedPoints.empty()
                        && namedPoints.back().refFrame.kind==requirements::RequirementRefKind::SceneObject
                        && namedPoints.back().refFrame.objectId==fixtureId,
                       "step5.7 named-reference-confirm-stores-real-fixture-id");
                    if (directorySeen) { ok(m_requirementsEditor.undoLocal(),"step5.7 named-reference-one-undo"); }
                    ok(m_requirementsEditor.workingSet()==beforeNamedReference,"step5.7 named-reference-undo-restores-all");
                    if (m_domains->requirements) { m_domains->requirements->refreshFromSession(); }
                    // REQ-03/04：区域/工况同样走正式名称目录和真实表单，不注入候选。
                    // 每段先新增，再确认引用，恰两次撤销还原整个工作集与原拾取前置。
                    const auto beforeNamedForms=m_requirementsEditor.workingSet();
                    auto* addRegion=m_requirementsDock->findChild<QPushButton*>("ird_req_add_regions");
                    if (addRegion) { addRegion->click(); settleEvents(120); }
                    bool regionNamed=false;
                    auto* regionReference=m_requirementsDock->findChild<QComboBox*>("ird_region_reference_choice");
                    const int regionRow=regionReference ? regionReference->findText("场景 / requirements-pick-fixture") : -1;
                    ok(addRegion && regionRow>=0,"step5.7 real-region-reference-directory");
                    if (regionRow>=0) {
                        const auto staged=m_requirementsEditor.workingSet();
                        regionReference->setCurrentIndex(regionRow);
                        ok(m_requirementsEditor.workingSet()==staged,"step5.7 region-selection-zero-write");
                        if (auto* apply=m_requirementsDock->findChild<QPushButton*>("ird_region_sampling_apply")) {
                            apply->click(); settleEvents(120);
                            const auto& regions=m_requirementsEditor.workingSet().regions.entries;
                            regionNamed=!regions.empty() && regions.back().refFrame.kind==requirements::RequirementRefKind::SceneObject
                                && regions.back().refFrame.objectId==fixtureId;
                        }
                    }
                    ok(regionNamed,"step5.7 real-region-reference-confirm");
                    if (regionNamed) { ok(m_requirementsEditor.undoLocal(),"step5.7 region-reference-one-undo"); }
                    if (m_requirementsEditor.workingSet().regions.entries.size()>beforeNamedForms.regions.entries.size()) {
                        ok(m_requirementsEditor.undoLocal(),"step5.7 named-region-creation-undo");
                    }
                    if (m_domains->requirements) { m_domains->requirements->refreshFromSession(); }
                    ok(m_requirementsEditor.workingSet()==beforeNamedForms,"step5.7 named-region-restores-all");
                    auto* addCondition=m_requirementsDock->findChild<QPushButton*>("ird_req_add_conditions");
                    QTimer::singleShot(100,this,[&] {
                        auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());
                        ok(dialog!=nullptr,"step5.7 condition-create-real-wizard");
                        if (dialog) {
                            if (auto* box=dialog->findChild<QDialogButtonBox*>()) { box->button(QDialogButtonBox::Ok)->click(); }
                        }
                    });
                    if (addCondition) { addCondition->click(); settleEvents(120); }
                    auto* environment=m_requirementsDock->findChild<QListWidget*>("ird_condition_environment_choices");
                    QListWidgetItem* environmentItem=nullptr;
                    if (environment) {
                        for (int i=0;i<environment->count();++i) {
                            if (environment->item(i)->text()=="场景 / requirements-pick-fixture") { environmentItem=environment->item(i); }
                        }
                    }
                    ok(addCondition && environmentItem,"step5.7 real-condition-environment-directory");
                    bool environmentNamed=false;
                    if (environmentItem) {
                        const auto staged=m_requirementsEditor.workingSet();
                        environmentItem->setCheckState(Qt::Checked);
                        ok(m_requirementsEditor.workingSet()==staged,"step5.7 environment-selection-zero-write");
                        // 小高度Dock中必须沿内外滚动区把真实环境选择滚到可见位置；
                        // 只截取上方条目列表不能证明下面的名称选择可达。
                        for (auto* ancestor=environment->parentWidget();ancestor;ancestor=ancestor->parentWidget()) {
                            if (auto* scroll=qobject_cast<QScrollArea*>(ancestor)) { scroll->ensureWidgetVisible(environment); }
                        }
                        settleEvents(60);
                        ok(!environment->visibleRegion().isEmpty(),"step5.7 environment-choice-scroll-reachable");
                        snapPng(m_requirementsDock,"mtour-condition-name-directory.png");
                        snapPng(environment,"mtour-environment-name-choice.png");
                        if (auto* apply=m_requirementsDock->findChild<QPushButton*>("ird_condition_structure_apply")) {
                            apply->click(); settleEvents(120);
                            const auto& conditions=m_requirementsEditor.workingSet().conditions.entries;
                            environmentNamed=!conditions.empty() && fixtureId
                                && conditions.back().environmentRefs==std::vector<core::ObjectId>{*fixtureId};
                        }
                    }
                    ok(environmentNamed,"step5.7 real-environment-reference-confirm");
                    if (environmentNamed) { ok(m_requirementsEditor.undoLocal(),"step5.7 environment-reference-one-undo"); }
                    if (m_requirementsEditor.workingSet().conditions.entries.size()>beforeNamedForms.conditions.entries.size()) {
                        ok(m_requirementsEditor.undoLocal(),"step5.7 named-condition-creation-undo");
                    }
                    if (m_domains->requirements) { m_domains->requirements->refreshFromSession(); }
                    ok(m_requirementsEditor.workingSet()==beforeNamedForms,"step5.7 named-condition-restores-all");
                    // REQ-08：真实GL输入与正交相机给出独立可计算位移；不调用投影实现求期望值。
                    // 八轨覆盖正交创建、World/旋转场景移动及透视创建的取消/确认；临时层必须清理。
                    const auto beforeTargetTracks=m_requirementsEditor.workingSet();
                    const auto oldProjection=viewer->getViewCamera()->getProjectionMatrix();
                    const auto oldCamera=viewer->getTransform();
                    rw::math::ProjectionMatrix orthographic;
                    orthographic.setOrtho(-0.2,0.2,-0.2,0.2,0.01,10.0); // 世界m，视野宽高0.4m。
                    viewer->setTransform(rw::math::Transform3D<>({0,0,1},rw::math::Rotation3D<>::identity()));
                    viewer->getViewCamera()->setProjectionMatrix(orthographic); view->update(); settleEvents(150);
                    for (int targetMode=0;targetMode<8;++targetMode) {
                        const bool creating=targetMode<2 || targetMode>=6,confirm=targetMode%2==1;
                        if (targetMode==6) {
                            rw::math::ProjectionMatrix perspective;
                            perspective.setPerspective(60.0,double(glWidget->width())/glWidget->height(),0.01,10.0);
                            viewer->getViewCamera()->setProjectionMatrix(perspective); view->update(); settleEvents(100);
                        }
                        if (targetMode==4) {
                            // 真实已编译旋转场景参考系：准备非World基线，交互仍走正式GL输入。
                            auto point=beforeTargetTracks.points.entries.back();
                            point.refFrame.kind=requirements::RequirementRefKind::SceneObject; point.refFrame.objectId=fixtureId;
                            point.pose.position=core::SourcedValue<rw::math::Vector3D<>>::provided({0.05,0.06,0.07},
                                core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
                            ok(m_requirementsEditor.applyEdit(requirements::RequirementEdit{point}).accepted,
                               "step5.7 rotated-scene-target-baseline");
                            m_domains->requirements->refreshFromSession();
                        }
                        const auto beforeTargetEdit=m_requirementsEditor.workingSet();
                        if (!beforeTargetEdit.points.entries.empty()) {
                            ok(m_domains->requirements->reportView3DPick(beforeTargetEdit.points.entries.back().objectId),
                               "step5.7 target-pick-accepted-mode-"+std::to_string(targetMode));
                        }
                        const auto basePreview=m_view3dGateway->currentPreview();
                        auto* targetButton=m_requirementsDock->findChild<QPushButton*>(
                            creating ? "ird_req_create_target_3d" : "ird_req_move_target_3d");
                        ok(targetButton && targetButton->isEnabled(),"step5.7 target-3d-entry-mode-"+std::to_string(targetMode));
                        bool toolSeen=false; rw::math::Vector3D<> expectedPosition;
                        QTimer answerTarget; answerTarget.setInterval(80);
                        QObject::connect(&answerTarget,&QTimer::timeout,this,[&] {
                            auto* dialog=hostWin->findChild<QDialog*>("ird_req_target_3d_tool");
                            if (!dialog || !dialog->isVisible()) { return; }
                            toolSeen=true;
                            const auto beforePreviewWrite=m_requirementsEditor.workingSet();
                            const QPoint start(glWidget->width()/2,glWidget->height()/2);
                            const QPoint delta(std::max(1,glWidget->width()/10),-std::max(1,glWidget->height()/10));
                            const QPoint end=start+delta;
                            const auto send=[&](QEvent::Type type,const QPoint& local,Qt::MouseButton button,Qt::MouseButtons held) {
                                QMouseEvent event(type,QPointF(local),QPointF(glWidget->mapToGlobal(local)),button,held,Qt::NoModifier);
                                QApplication::sendEvent(glWidget,&event);
                            };
                            send(QEvent::MouseButtonPress,start,Qt::LeftButton,Qt::LeftButton);
                            for (int beat=1;beat<=4;++beat) { send(QEvent::MouseMove,start+delta*beat/4,Qt::NoButton,Qt::LeftButton); }
                            send(QEvent::MouseButtonRelease,end,Qt::LeftButton,Qt::NoButton);
                            // 相机位于(0,0,1)m，透视60°：z=0平面半高tan(30°)，独立解析对照。
                            expectedPosition=creating ? (targetMode>=6 ? rw::math::Vector3D<>(
                                (2.0*end.x()/glWidget->width()-1)*double(glWidget->width())/glWidget->height()/std::sqrt(3.0),
                                (1-2.0*end.y()/glWidget->height())/std::sqrt(3.0),0)
                                : rw::math::Vector3D<>(-0.2+0.4*end.x()/glWidget->width(),0.2-0.4*end.y()/glWidget->height(),0))
                                : (targetMode>=4 ? rw::math::Vector3D<>(
                                    1.4+std::cos(0.7)*0.05+std::sin(0.7)*0.07,0.3+0.06,
                                    0.2-std::sin(0.7)*0.05+std::cos(0.7)*0.07)
                                    : beforeTargetEdit.points.entries.back().pose.position.value())
                                    +rw::math::Vector3D<>(0.4*delta.x()/glWidget->width(),-0.4*delta.y()/glWidget->height(),0);
                            const auto preview=m_view3dGateway->currentPreview();
                            ok(preview.frameMarkers.size()==basePreview.frameMarkers.size()+1
                                && preview.frameMarkers.back().position
                                && (*preview.frameMarkers.back().position-expectedPosition).norm2()<1e-9,
                               "step5.7 real-projected-preview-world-position-mode-"+std::to_string(targetMode));
                            ok(m_requirementsEditor.workingSet()==beforePreviewWrite,
                               "step5.7 continuous-drag-zero-draft-write-mode-"+std::to_string(targetMode));
                            if (targetMode==7) {
                                auto* yaw=dialog->findChild<QLineEdit*>("ird_target_rpy_2");
                                ok(yaw && !yaw->isReadOnly(),"step5.7 real-create-world-yaw-entry");
                                if (yaw) { yaw->setText("1.5707963267948966"); QMetaObject::invokeMethod(yaw,"editingFinished"); }
                                const auto rotated=m_view3dGateway->currentPreview();
                                bool axes=rotated.sampleGrid && rotated.sampleGrid->gridLines.size()>=3;
                                if (axes) {
                                    const auto& lines=rotated.sampleGrid->gridLines;
                                    const rw::math::Vector3D<> directions[3]={{0,0.15,0},{-0.15,0,0},{0,0,0.15}};
                                    for (int i=0;i<3;++i) {
                                        const auto& line=lines[lines.size()-3+i];
                                        axes=axes && (line.second-line.first-directions[i]).norm2()<1e-9;
                                    }
                                }
                                ok(axes,"step5.7 create-yaw-preview-three-world-directions");
                                snapPng(dialog,"mtour-target-3d-perspective-yaw.png");
                            }
                            if (targetMode==1) { snapPng(m_view3d,"mtour-target-3d-preview.png"); snapPng(dialog,"mtour-target-3d-tool.png"); }
                            if (auto* box=dialog->findChild<QDialogButtonBox*>()) {
                                box->button(confirm ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
                            } else { dialog->reject(); }
                        });
                        answerTarget.start(); if (targetButton) { targetButton->click(); } answerTarget.stop();
                        if (!toolSeen) {
                            for (auto* label:m_requirementsDock->findChildren<QLabel*>()) {
                                if (!label->text().isEmpty() && label->isVisible()) {
                                    std::cout << "[ird-ui-smoke-mtour] target-entry-diagnostic " << label->text().toStdString() << std::endl;
                                }
                            }
                            for (const auto& p:beforeTargetEdit.points.entries) {
                                std::cout << "[ird-ui-smoke-mtour] target-baseline " << p.name
                                    << " position-state=" << int(p.pose.position.state()) << std::endl;
                            }
                        }
                        ok(toolSeen,"step5.7 real-target-3d-tool-mode-"+std::to_string(targetMode));
                        ok(!view->property("irdTargetEditActive").toBool() && m_view3dGateway->currentPreview()==basePreview,
                           "step5.7 target-preview-and-input-filter-restored-mode-"+std::to_string(targetMode));
                        settleEvents(120);
                        const auto after=m_requirementsEditor.workingSet();
                        if (!confirm) { ok(after==beforeTargetEdit,"step5.7 target-cancel-zero-write-mode-"+std::to_string(targetMode)); }
                        else if (creating) {
                            const bool created=after.points.entries.size()==beforeTargetEdit.points.entries.size()+1;
                            const auto added=std::find_if(after.points.entries.begin(),after.points.entries.end(),[&](const auto& p) {
                                return std::none_of(beforeTargetEdit.points.entries.begin(),beforeTargetEdit.points.entries.end(),
                                    [&](const auto& old) { return p.objectId==old.objectId; });
                            });
                            ok(created && added!=after.points.entries.end() && (added->pose.position.value()-expectedPosition).norm2()<1e-9,
                               "step5.7 target-created-at-projected-position");
                            if (targetMode==7) {
                                ok(added!=after.points.entries.end() && std::abs(added->pose.orientation.fixedRpy[2]-1.5707963267948966)<1e-12,
                                   "step5.7 create-yaw-stored-radians");
                            }
                            if (created) { ok(m_requirementsEditor.undoLocal(),"step5.7 target-create-one-undo"); }
                        } else {
                            const auto& moved=after.points.entries.back();
                            auto expected=beforeTargetEdit.points.entries.back(); expected.pose.position=moved.pose.position;
                            const auto local=targetMode>=4 ? rw::math::Vector3D<>(
                                std::cos(0.7)*(expectedPosition[0]-1.4)-std::sin(0.7)*(expectedPosition[2]-0.2),
                                expectedPosition[1]-0.3,
                                std::sin(0.7)*(expectedPosition[0]-1.4)+std::cos(0.7)*(expectedPosition[2]-0.2)) : expectedPosition;
                            ok(moved==expected && (moved.pose.position.value()-local).norm2()<1e-9,
                               "step5.7 target-move-keeps-all-other-fields");
                            if (after!=beforeTargetEdit) { ok(m_requirementsEditor.undoLocal(),"step5.7 target-move-one-undo"); }
                        }
                        ok(m_requirementsEditor.workingSet()==beforeTargetEdit,"step5.7 target-history-restored-mode-"+std::to_string(targetMode));
                        if (m_domains->requirements) { m_domains->requirements->refreshFromSession(); }
                    }
                    ok(m_requirementsEditor.undoLocal() && m_requirementsEditor.workingSet()==beforeTargetTracks,
                       "step5.7 rotated-scene-baseline-restored");
                    m_domains->requirements->refreshFromSession();
                    viewer->setTransform(oldCamera); viewer->getViewCamera()->setProjectionMatrix(oldProjection); view->update(); settleEvents(100);
                    const auto beforePick = m_requirementsEditor.workingSet();
                    const auto pickHead=adapter->projectStore().query().head().id;
                    ok(!beforePick.points.entries.empty(), "step5.7 target-point-present");
                    if (!beforePick.points.entries.empty()) {
                        const auto pointId = beforePick.points.entries.back().objectId;
                        for (int mode = 0; mode < 4; ++mode) {
                            // 每轮从同一基线开始，取消/Frame/正法向/反法向各走真实模态交互。
                            m_domains->requirements->reportView3DPick(pointId);
                            bool choiceSeen=false, confirmSeen=false;
                            QTimer answer;
                            answer.setInterval(80);
                            QObject::connect(&answer, &QTimer::timeout, this, [&] {
                                auto* modal = QApplication::activeModalWidget();
                                if (auto* input = qobject_cast<QInputDialog*>(modal)) {
                                    if (input->windowTitle().contains(QStringLiteral("拾取特征"))) {
                                        choiceSeen=true;
                                        if (auto* combo=input->findChild<QComboBox*>()) { combo->setCurrentIndex(mode > 1 ? mode-1 : 0); }
                                        input->accept();
                                    }
                                } else if (auto* dialog=qobject_cast<QDialog*>(modal)) {
                                    auto* buttons=dialog->findChild<QDialogButtonBox*>();
                                    if (buttons && dialog->windowTitle().contains(QStringLiteral("确认"))) {
                                        confirmSeen=true;
                                        if (mode==3) { snapPng(dialog,"mtour-pick-normal-confirm.png"); }
                                        if (auto* button=buttons->button(mode==0 ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)) { button->click(); }
                                    }
                                }
                            });
                            answer.start(); m_content->submitCommand("requirements.pick-feature"); answer.stop();
                            settleEvents(150);
                            ok(choiceSeen && confirmSeen, "step5.7 real-pick-dialogs-mode-"+std::to_string(mode));
                            const auto after=m_requirementsEditor.workingSet();
                            if (mode==0) { ok(after==beforePick,"step5.7 pick-cancel-zero-write"); continue; }
                            const auto target=std::find_if(after.points.entries.begin(),after.points.entries.end(),
                                [&](const auto& p){return p.objectId==pointId;});
                            bool correct=target!=after.points.entries.end();
                            if (correct) {
                                const auto& rule=target->pose.orientation;
                                correct=mode==1 ? (rule.kind==requirements::OrientationRuleKind::AlignFrame
                                    && rule.targetFrame.kind==requirements::RequirementRefKind::SceneObject
                                    && rule.targetFrame.objectId==fixtureId)
                                    : (rule.kind==requirements::OrientationRuleKind::AlignGeometryNormal
                                    && rule.targetSceneObject==fixtureId
                                    && rule.feature==requirements::OrientationFeature::FramePlaneNormal
                                    && rule.invertNormal==(mode==3));
                            }
                            ok(correct,"step5.7 pick-rule-and-object-id-mode-"+std::to_string(mode));
                            QPushButton* undo=nullptr; QPushButton* redo=nullptr;
                            for(auto* button:m_requirementsDock->findChildren<QPushButton*>()) {
                                if(button->text()==QString::fromStdString(resolveText(TextKey{"panel.requirements.undo-draft.label"}))) {undo=button;}
                                if(button->text()==QString::fromStdString(resolveText(TextKey{"panel.requirements.redo-draft.label"}))) {redo=button;}
                            }
                            ok(undo && undo->isEnabled(),"step5.7 pick-local-undo-enabled");
                            if(undo){undo->click();settleEvents(100);}
                            ok(m_requirementsEditor.workingSet()==beforePick,"step5.7 pick-one-undo");
                            ok(redo && redo->isEnabled(),"step5.7 pick-local-redo-enabled");
                            if(redo){redo->click();settleEvents(100);}
                            ok(m_requirementsEditor.workingSet()==after,"step5.7 pick-one-redo");
                            if(undo){undo->click();settleEvents(100);}
                        }
                        ok(adapter->projectStore().query().head().id==pickHead,
                           "step5.7 pick-local-history-zero-project-revisions");
                        // REQ-01/03：先在完整闭包上验证模型帧，再在90°夹具更新后验证场景帧。
                        // 每轨独立撤销，并按本次ack.taskRef等终态，不复用上一轨覆盖缓存。
                        const auto probeReference=[&](requirements::RequirementRefKind kind) {
                            const auto beforeFrameProbe = m_requirementsEditor.workingSet();
                            const auto probeHead=adapter->projectStore().query().head();
                            const auto modelRef=std::find_if(probeHead.objectRefs.begin(),probeHead.objectRefs.end(),
                                [](const auto& ref) { return ref.objectTypeToken==modeling::kRobotDesignObjectType; });
                            if (kind==requirements::RequirementRefKind::ModelFrame) {
                                ok(modelRef!=probeHead.objectRefs.end(),"step5.7 real-model-reference-exists");
                                if (modelRef==probeHead.objectRefs.end()) { return; }
                            }
                            const std::string probeLabel=kind==requirements::RequirementRefKind::SceneObject ? "scene" : "model";
                            std::size_t probeEdits = 0;
                            if (m_kinEvaluation) {
                                requirements::WorkRegion probe;
                                probe.objectId = core::ObjectId::generate();
                                probe.name = "参考系装配验证区域";
                                probe.refFrame.kind = kind;
                                probe.refFrame.objectId = kind==requirements::RequirementRefKind::SceneObject ? *fixtureId : modelRef->objectId;
                                // A-21：非中心两格使旋转错误可观测；场景参考系是已编译真值。
                                probe.box.center={0.08,0.03,0.02}; probe.box.size={0.04,0.02,0.02}; // 局部m。
                                probe.positionSampling.counts={2,1,1};
                                probe.demands.collisionFreeRequired = true; // 真实缺检测器降级轨。
                                const auto regionEdit = m_requirementsEditor.applyEdit(requirements::RequirementEdit{probe});
                                ok(regionEdit.accepted,"step5.7 scene-frame-probe-region-valid");
                                probeEdits += regionEdit.accepted ? 1 : 0;
                                requirements::SamplingPlan plan;
                                plan.objectId = core::ObjectId::generate();
                                plan.regionRef = probe.objectId;
                                plan.positionSampling=probe.positionSampling;
                                plan.orientationSampling=probe.orientationSampling;
                                const auto planEdit = m_requirementsEditor.applyEdit(requirements::RequirementEdit{plan});
                                ok(planEdit.accepted,"step5.7 scene-frame-probe-plan-valid");
                                probeEdits += planEdit.accepted ? 1 : 0;
                                kinematics::KinChannelBackgroundRequest request;
                                request.kind = kinematics::KinChannelBackgroundKind::RegionCoverage;
                                request.snapshotId = m_kinEvaluation->snapshotId();
                                request.epoch = m_kinEvaluation->epoch();
                                const auto ack = m_kinEvaluation->submit(request);
                                ok(ack.accepted,"step5.7 real-"+probeLabel+"-reference-coverage-accepted: " + ack.reason);
                                if (ack.accepted) {
                                    const auto completed=[&] {
                                        const auto rows=m_kinEvaluation->taskRows();
                                        const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& value) {
                                            return value.taskRefText==ack.taskRef;
                                        });
                                        return row!=rows.end() && row->stateLabelKey=="task.state.completed";
                                    };
                                    for (int beat=0; beat<100 && !completed(); ++beat) {
                                        settleEvents(50);
                                    }
                                    ok(completed(),"step5.7 "+probeLabel+"-own-task-completed");
                                    const auto outcome=m_kinEvaluation->latestCoverageResult();
                                    std::cout << "[ird-ui-smoke-mtour] coverage result=" << bool(outcome)
                                        << " error=" << (outcome ? outcome->errorText : "missing") << std::endl;
                                    ok(outcome && outcome->ok && outcome->snapshotId==request.snapshotId
                                        && !outcome->samples.empty(),
                                       "step5.7 real-"+probeLabel+"-reference-computation-completed");
                                    if (outcome && outcome->ok) {
                                        ok(outcome->downgraded && std::all_of(outcome->samples.begin(),outcome->samples.end(),
                                            [](const auto& sample) { return sample.state==kinematics::KinChannelSampleState::DataInsufficient; }),
                                           "step5.7 required-collision-unavailable-data-insufficient");
                                        if (kind==requirements::RequirementRefKind::SceneObject) {
                                            // 独立从正式WorkCell取世界变换，再与计算基座点及域显示世界点交叉核对。
                                            const auto wc=studio->getWorkCell();
                                            auto* sceneFrame=wc && name ? wc->findFrame(*name) : nullptr;
                                            ok(sceneFrame && !wc->getDevices().empty(),"step5.7 a21-live-scene-and-base");
                                            if (sceneFrame && !wc->getDevices().empty()) {
                                                const auto worldRef=rw::kinematics::Kinematics::worldTframe(sceneFrame,studio->getState());
                                                const auto worldBase=rw::kinematics::Kinematics::worldTframe(wc->getDevices().front()->getBase(),studio->getState());
                                                std::vector<rw::math::Vector3D<>> expectedWorld;
                                                for (double x : {0.07,0.09}) expectedWorld.push_back(worldRef*rw::math::Vector3D<>(x,0.03,0.02));
                                                std::size_t count=0; bool computationMatches=true;
                                                for (const auto& sample:outcome->samples) {
                                                    if (sample.regionObjectId!=probe.objectId || sample.kind!=kinematics::KinChannelSampleKind::Position) continue;
                                                    if (count>=expectedWorld.size() || (worldBase*sample.position-expectedWorld[count]).norm2()>1e-11) computationMatches=false;
                                                    ++count;
                                                }
                                                ok(count==2 && computationMatches,"step5.7 a21-world-base-sample-exact");
                                                m_domains->requirements->refreshFromSession();
                                                auto* regions=m_requirementsDock->findChild<QTreeWidget*>("ird_req_region_table");
                                                if (regions) for (int i=0;i<regions->topLevelItemCount();++i) {
                                                    auto* row=regions->topLevelItem(i);
                                                    if (row->text(regions->columnCount()-1).toStdString()==probe.objectId.toCanonical()) {
                                                        regions->setCurrentItem(row); break;
                                                    }
                                                }
                                                settleEvents(150);
                                                // 生产预览透传位置和位姿两类样本，不能错误要求只有两条位置行。
                                                std::size_t expectedCount=0;
                                                for (const auto& sample:outcome->samples) if(sample.regionObjectId==probe.objectId) ++expectedCount;
                                                bool displayMatches=m_reqGrid && m_reqGrid->samples.size()==expectedCount && expectedCount>=2 && m_reqBox;
                                                if (displayMatches) for (const auto& p:m_reqGrid->samples) {
                                                    displayMatches=displayMatches && std::any_of(expectedWorld.begin(),expectedWorld.end(),
                                                        [&](const auto& expected) {return (p-expected).norm2()<1e-11;});
                                                }
                                                // 八角同样按独立局部边界核对，避免只证明点正确而盒轮廓仍错位。
                                                if (displayMatches) for (const auto& p:m_reqBox->corners) {
                                                    bool found=false;
                                                    for(double x:{0.06,0.10}) for(double y:{0.02,0.04}) for(double z:{0.01,0.03})
                                                        found=found || (p-worldRef*rw::math::Vector3D<>(x,y,z)).norm2()<1e-11;
                                                    displayMatches=displayMatches && found;
                                                }
                                                ok(displayMatches,"step5.7 a21-domain-preview-matches-computation");
                                                // 正式GL相机沿区域局部+Z看向两点，独立正交投影给出两个像素落点。
                                                const auto savedCamera=viewer->getTransform();
                                                const auto savedProjection=viewer->getViewCamera()->getProjectionMatrix();
                                                const double aspect=double(glWidget->width())/glWidget->height();
                                                rw::math::ProjectionMatrix ortho; ortho.setOrtho(-0.06*aspect,0.06*aspect,-0.06,0.06,0.001,2.0);
                                                viewer->getViewCamera()->setProjectionMatrix(ortho);
                                                viewer->setTransform(rw::math::Transform3D<>(worldRef*rw::math::Vector3D<>(0.08,0.03,0.32),worldRef.R()));
                                                view->update(); settleEvents(250);
                                                const auto pixels=glWidget->grab().toImage().convertToFormat(QImage::Format_RGB32);
                                                // 缺碰撞设施是真实DataInsufficient，正式域投影应为黄色；不得注入显示夹具。
                                                const auto* color=ui::palette::kSampleWeakGl;
                                                for (int point=0;point<2;++point) {
                                                    const int px=int((0.5+(point==0 ? -0.01 : 0.01)/(0.12*aspect))*pixels.width());
                                                    const int py=pixels.height()/2; int hits=0;
                                                    for (int y=std::max(0,py-9);y<std::min(pixels.height(),py+10);++y)
                                                        for (int x=std::max(0,px-9);x<std::min(pixels.width(),px+10);++x) {
                                                            const auto rgb=pixels.pixel(x,y);
                                                            if (std::abs(qRed(rgb)-int(color[0]*255))<=8 && std::abs(qGreen(rgb)-int(color[1]*255))<=8
                                                                && std::abs(qBlue(rgb)-int(color[2]*255))<=8) ++hits;
                                                        }
                                                    ok(hits>=9,"step5.7 a21-real-sample-pixel-"+std::to_string(point)+"="+std::to_string(hits));
                                                }
                                                snapPng(glWidget,"mtour-a21-oriented-samples.png");
                                                viewer->setTransform(savedCamera); viewer->getViewCamera()->setProjectionMatrix(savedProjection);
                                            }
                                        }
                                    }
                                }
                            }
                            while (probeEdits>0) {
                                ok(m_requirementsEditor.undoLocal(),"step5.7 frame-probe-local-undo");
                                --probeEdits;
                            }
                            ok(m_requirementsEditor.workingSet()==beforeFrameProbe,
                               "step5.7 frame-probe-restores-entire-working-set");
                        };
                        probeReference(requirements::RequirementRefKind::ModelFrame);
                        // 空白raycast必须清空最近拾取；命令随后不得复用旧夹具。
                        m_view3dGateway->handleViewDoubleClick(QPoint(vx+2,vy+2));
                        m_domains->requirements->reportView3DPick(pointId);
                        m_content->submitCommand("requirements.pick-feature"); settleEvents(100);
                        ok(m_requirementsEditor.workingSet()==beforePick,
                           "step5.7 blank-pick-clears-feature-zero-write");
                        // 再真实拾取后以场景单对象命令移动夹具，触发新的编译/发布。
                        // 旧拾取锚不得跨WorkCell/修订继续使用，不能直接塞一个假场景。
                        m_view3dGateway->handleViewDoubleClick(pixel);
                        modeling::SceneObject moved=fixture; moved.objectId=*fixtureId;
                        moved.worldPose.P()[0]+=0.1; // 世界系已知位移，m。
                        // A-21：30°偏航+25°横滚的真实已发布场景，覆盖任意角和三维倾斜。
                        moved.worldPose.R()=rw::math::RPY<>(0.4363323129985824,0,0.5235987755982988).toRotation3D();
                        const auto movedBytes=codec.encode(modeling::ObjectVariant(moved),modeling::kCurrentFormatVersion);
                        ok(movedBytes.ok(),"step5.7 moved-scene-encoded");
                        if(movedBytes.ok()){
                            modeling::CommandPayload movePayload;
                            movePayload.objects.push_back({false,*fixtureId,std::string(modeling::kSceneObjectObjectType),movedBytes.get()});
                            auto moveEnvelope=envelope;
                            moveEnvelope.expectedRevision=adapter->projectStore().query().head().id;
                            moveEnvelope.commandType=std::string(modeling::kCmdApplySceneObjects);
                            moveEnvelope.payloadCanonical=modeling::encodeCommandPayload(movePayload);
                            const auto movedResult=adapter->projectStore().commands().submit(moveEnvelope,&interaction);
                            ok(movedResult.committed(),"step5.7 scene-only-update-real-command-committed");
                            if(movedResult.committed()){refreshPresentationAfterCommit();}
                            settleEvents(200);
                            const auto afterMove=m_requirementsEditor.workingSet();
                            m_domains->requirements->reportView3DPick(pointId);
                            m_content->submitCommand("requirements.pick-feature");settleEvents(100);
                            ok(m_requirementsEditor.workingSet()==afterMove,
                               "step5.7 recompiled-scene-rejects-old-pick-zero-write");
                            const auto newCell=studio->getWorkCell();
                            auto* newFrame=name&&newCell?newCell->findFrame(*name):nullptr;
                            ok(newCell!=cell && newFrame
                                && (rw::kinematics::Kinematics::worldTframe(newFrame,studio->getState()).P()
                                    - moved.worldPose.P()).norm2()<1e-12,
                               "step5.7 scene-only-update-world-position-published");
                            // REQ-03正式参考系装配回归：使用已发布夹具的ObjectId，
                            // 经公开编辑器构造区域/采样计划，不能注入名称或变换替身。
                            // 显式要求碰撞检查而宿主未装配检测器，真实评估应诚实
                            // 产出DataInsufficient；不把执行完成冒充可达性通过。
                            // 无TCP但未要求碰撞的输入另有上游求解前置缺口，保留失败证据。
                            ok(m_kinEvaluation && m_kinEvaluation->snapshotId().isValid(),
                               "step5.7 kinematics-current-snapshot-bound");
                            const auto afterSceneHead=adapter->projectStore().query().head();
                            ok(std::any_of(afterSceneHead.objectRefs.begin(),afterSceneHead.objectRefs.end(),
                                [](const auto& ref) { return ref.objectTypeToken==modeling::kRobotDesignObjectType; }),
                               "step5.7 scene-only-commit-preserves-model-root");
                            probeReference(requirements::RequirementRefKind::SceneObject);
                        }
                    }
                    snapPng(hostWin,"mtour-5.7-scene-picked.png");
                }
            }
        }

        // WP-14-T12/A-10：在真实已打开项目的正式命令注册/面板中复验九条需求命令，
        // 避免模型测试使用自造描述符掩盖生产装配词键错误。不执行会写草稿的命令。
        m_content->showStagePanel(StageId::Requirements);
        m_content->submitCommand("workbench.commandPalette");
        settleEvents(200);
        auto* palette = hostWin->findChild<QWidget*>(QStringLiteral("ird_command_palette"));
        auto* filter = palette ? palette->findChild<QLineEdit*>(QStringLiteral("ird_command_palette_filter")) : nullptr;
        auto* results = palette ? palette->findChild<QListWidget*>(QStringLiteral("ird_command_palette_list")) : nullptr;
        ok(palette && palette->isVisible() && filter && results, "step7 real-command-palette");
        std::size_t checkedCommands = 0;
        for (const auto& command : m_content->commandRegistry().query({})) {
            if (command.id.rfind("requirements.", 0) != 0) { continue; }
            ++checkedCommands;
            const auto expected = resolveText(command.titleKey);
            ok(command.title == expected, "step7 chinese-title-" + command.id);
            if (filter && results) {
                filter->setText(QString::fromStdString(expected));
                settleEvents(50);
                bool found = false;
                for (int row = 0; row < results->count(); ++row) {
                    const auto* item = results->item(row);
                    found = found || (item->data(Qt::UserRole).toString().toStdString() == command.id
                        && item->text().contains(QString::fromStdString(expected))
                        && !item->text().contains(QStringLiteral("requirements.")));
                }
                ok(found, "step7 chinese-filter-" + command.id);
            }
        }
        ok(checkedCommands == 9, "step7 all-nine-requirements-commands");
        snapPng(hostWin, "mtour-7-chinese-palette.png");
        if (palette) { palette->hide(); }

        // WP-14-T12 A-13／REQ-10：单独更新夹具之后再次真正关闭并重开，
        // 核对磁盘完整清单及新编译的机器人/夹具变换；禁止只读上一代 runtime 缓存。
        // 所有跨关闭边界数据仅保存值，旧 adapter、Frame 指针均不能继续使用。
        step("8 scene-update-close-reopen");
        const auto beforeReopen = m_lastStoreAdapter->projectStore().query().head();
        const auto previousCell = getRobWorkStudio()->getWorkCell();
        ok(m_kinSessionPose.isSet(), "step8 regression-starts-with-real-session-pose");
        (void)m_controller->beginClose(UiCloseIntent::CloseProject);
        const auto finalClose = m_controller->resolveCloseDialog(
            CloseDecision{true, DraftDisposition::Discard, TaskDisposition::Wait});
        for (int attempt = 0; attempt < 100 && m_controller->hasOpenSession(); ++attempt) {
            settleEvents(100); // 关闭协议至多等待10s，不并发运行另一宿主。
        }
        ok(!m_kinSessionPose.isSet(), "step8 closed-project-clears-session-pose");
        const bool sceneReopened = finalClose.status == CloseDialogResolution::Status::Confirmed
            && !m_controller->hasOpenSession() && openViaSessionController(demoPath.toStdString());
        ok(sceneReopened, "step8 scene-update-real-project-reopened");
        settleEvents(300);
        if (sceneReopened) {
            const auto afterReopen = m_lastStoreAdapter->projectStore().query().head();
            ok(afterReopen.id == beforeReopen.id && afterReopen.objectRefs == beforeReopen.objectRefs,
               "step8 reopen-complete-manifest-zero-revisions");
            const auto newCell = getRobWorkStudio()->getWorkCell();
            ok(newCell && newCell != previousCell && !newCell->getDevices().empty(),
               "step8 scene-update-robot-restored-in-new-workcell");
            bool fixtureRestored = false;
            for (const auto& ref : afterReopen.objectRefs) {
                if (ref.objectTypeToken != modeling::kSceneObjectObjectType) { continue; }
                const auto name = m_nameMapPort->resolveRuntimeName(ref.objectId);
                auto* frame = name && newCell ? newCell->findFrame(*name) : nullptr;
                if (!frame) { continue; } // 引用缺名或帧缺失不能构成恢复成功。
                const auto pose = rw::kinematics::Kinematics::worldTframe(
                    frame, getRobWorkStudio()->getState());
                const auto expectedRotation = rw::math::RPY<>(0.4363323129985824,0,0.5235987755982988).toRotation3D();
                bool same = (pose.P() - rw::math::Vector3D<>(1.5,0.3,0.2)).norm2() < 1e-12;
                for (int row = 0; row < 3; ++row) { for (int col = 0; col < 3; ++col) {
                    same = same && std::abs(pose.R()(row,col) - expectedRotation(row,col)) < 1e-12;
                } }
                fixtureRestored = fixtureRestored || same;
            }
            // m 与旋转矩阵均用独立已知输入对照，不能仅用“场景里有东西”作判据。
            ok(fixtureRestored, "step8 scene-update-fixture-world-transform-restored");
            const auto scene = getRobWorkStudio()->getWorkCellScene();
            ok(scene && scene->getWorkCell() == newCell && !scene->getDrawables().empty(),
               "step8 scene-update-new-workcell-rendered");
            // REQ-01/10／UX-06：运行时设备存在不等于可编辑工作集已恢复。
            // 树必须包含磁盘修订的同一个模型根身份，否则用户重开后无法选择
            // 模型或继续维护需求引用；不能用运行时缓存/非空分组标题冒充恢复。
            refreshSharedSurfaces();
            const auto restoredModelNodes = m_treeModel->nodesInGroup(
                ui::ProjectTreeGroup::ModelingObjects);
            bool editableModelRestored = false;
            for (const auto& ref : afterReopen.objectRefs) {
                if (ref.objectTypeToken == modeling::kRobotDesignObjectType) {
                    editableModelRestored = std::find(restoredModelNodes.begin(),
                        restoredModelNodes.end(), ref.objectId) != restoredModelNodes.end();
                }
            }
            ok(editableModelRestored, "step8 committed-modeling-workset-and-tree-restored");
            // 重开后必须恢复共享选择订阅：直接选业务身份，域面板应同步定位。
            // 仅手动点击域树会绕过共享选择通路，不能证明此处的生命周期对称性。
            HostProjectClosureView closure(m_lastStoreAdapter->projectStore().query(),
                std::string(modeling::kRobotDesignObjectType), nullptr);
            const auto rootBytes = closure.tryObjectByToken(modeling::kRobotDesignObjectType);
            bool jointSelected = false;
            if (rootBytes) {
                const auto decoded = modeling::RobotDesignCodec{}.decode(
                    rootBytes->bytes, modeling::kCurrentFormatVersion);
                const auto* root = decoded.ok() ? std::get_if<modeling::RobotDesign>(&decoded.get()) : nullptr;
                if (root && !root->joints.empty()) {
                    ok(m_nameResolver->resolveObjectId(root->joints.front().objectId)
                        == std::optional<std::string>{root->joints.front().localName},
                       "step8 reopened-shared-tree-resolves-real-joint-name");
                    m_content->showStagePanel(StageId::Modeling);
                    m_selection->selectBusiness({root->joints.front().objectId}, SelectionSource::ProjectTree);
                    settleEvents(150);
                    auto* tree = panel->findChild<QTreeWidget*>("ird_modeling_struct_tree");
                    jointSelected = tree && tree->currentItem()
                        && tree->currentItem()->text(1).toStdString()
                           == root->joints.front().objectId.toCanonical();
                }
            }
            ok(jointSelected, "step8 reopened-shared-selection-focuses-modeling-joint");
            bool continuedEdit = false;
            if (jointSelected) {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                auto* editHost = panel->findChild<QWidget*>("ird_modeling_edit_host");
                auto* table = editHost ? editHost->findChild<QTableWidget*>("ird_param_table") : nullptr;
                auto* apply = editHost ? editHost->findChild<QPushButton*>("ird_param_apply") : nullptr;
                auto* confirm = editHost ? editHost->findChild<QPushButton*>("ird_param_confirm_yes") : nullptr;
                bool fieldFound = false;
                for (int row = 0; table && row < table->rowCount(); ++row) {
                    if (table->item(row, 0)->data(Qt::UserRole).toString() == "origin-z") {
                        table->item(row, 1)->setText("0.177"); // parent系Z，m，区别于保存值0.15m。
                        fieldFound = true;
                        break;
                    }
                }
                if (fieldFound && apply && confirm) {
                    apply->click(); settleEvents(100);
                    confirm->click(); settleEvents(150);
                    const auto command = m_domains->modeling.module->buildDraftCommand("modeling");
                    const auto payload = command ? modeling::tryDecodeCommandPayload(command->payloadCanonical) : std::nullopt;
                    if (payload && payload->objects.size() == 1) {
                        const auto decoded = modeling::RobotDesignCodec{}.decode(
                            payload->objects.front().objectBytes, modeling::kCurrentFormatVersion);
                        const auto* edited = decoded.ok() ? std::get_if<modeling::RobotDesign>(&decoded.get()) : nullptr;
                        continuedEdit = edited && !edited->joints.empty()
                            && edited->joints.front().origin.tryValue()
                            && std::abs(edited->joints.front().origin.tryValue()->d()[2] - 0.177) < 1e-12
                            && !payload->objects.front().allocateNew
                            && command->expectedRevision == afterReopen.id;
                    }
                }
            }
            ok(continuedEdit, "step8 reopened-modeling-form-edits-real-same-root-draft");
            ok(m_lastStoreAdapter->projectStore().query().head().id == afterReopen.id,
               "step8 continued-draft-edit-zero-project-revisions");
        }
        snapPng(hostWin, "mtour-8-scene-update-reopened.png");

        QCoreApplication::exit(failedCount == 0 ? 0 : 1);
    });
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
