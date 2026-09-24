/**
 * @file   WorkbenchShell_p.hpp
 * @brief  工作台壳私有实现头（R-2：不进 include/、不跨单元暴露）。
 *
 * 设计依据：
 *   - units/ui.md §3.3（"私有实现（ui/src/）：WorkbenchShell_p.hpp、五区
 *     DockWidget 组装……不进 include/"）、§4.1~§4.6（五区/最小布局/布局
 *     归属/用户级设置）、§7.1~§7.6（命令注册表/快捷键/面板/谓词/只读）、
 *     §10.3/§10.4（两注册表接口）；
 *   - 任务契约 tasks/foundation/UI-T03.json acceptance 1~4＋tasks/foundation/
 *     UI-T06.json acceptance 1~3（命令注册表/快捷键表/命令面板的壳集成）。
 *
 * 背景说明（本头承载的内部设施——UI-T16 起＝宿主层与内容装配层共用面）：
 *   1. WorkbenchText——壳层界面文案的单点登记表（顶层窗口宿主层菜单/Dock
 *      标题与内容装配层共用——单一权威，禁止散落字面量）。
 *   2. CommandRegistry/GlobalShortcutRegistry（§10.3/§10.4）的装配语义在
 *      内容装配层（WorkbenchContent_p.hpp）；本头承载其支撑设施：
 *      RecentProjectsModel（最近项目）、UiSettingsWriter（后台落盘线程）、
 *      LayoutMemory（布局记忆读写与损坏判别）。
 *   3. 占位/卡面板工厂：createView3DPlaceholder（三维视图占位——UI-T05）、
 *      createPolicySummaryCard（策略摘要卡——UI-T07）；命令面板
 *      （CommandPalettePanel——§7.4）在 CommandPalette_p.hpp。
 *   4. WorkbenchShellImpl＝顶层窗口宿主层（chrome），界面语义在
 *      WorkbenchContentImpl（WorkbenchContent_p.hpp——两种宿主层共用的
 *      内容装配面，O-38 裁决②）。
 *
 * 线程模型：除 UiSettingsWriter 的工作线程外，全部设施只在 UI 线程使用
 * （§3.4 M-1）；UiSettingsWriter 只接收值任务（std::function 捕获已拷贝
 * 的字节/字符串），不触碰任何 QWidget。
 */

#ifndef SDURWS_IRD_UI_WORKBENCHSHELL_P_HPP
#define SDURWS_IRD_UI_WORKBENCHSHELL_P_HPP

#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QStackedWidget>
#include <QString>
#include <QStringList>

#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sdurws/ird/ui/ICommandRegistry.hpp>
#include <sdurws/ird/ui/IGlobalShortcutRegistry.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/WorkbenchContent.hpp>  // 内容装配面（UI-T16——宿主层持有其 unique_ptr）

namespace sdurws::ird {
namespace ui {
namespace detail {

// =====================================================================
// 常量（魔法数字全部在此登记来源——AGENTS.md §2.4）
// =====================================================================

/// QSettings 组织/应用名（用户级设置的存储身份——PM-14；IniFormat＋UserScope
/// 使落盘位置固定于用户 profile（Windows：%APPDATA%\sdurws\ird-workbench.ini），
/// 与任何项目目录（.rwdesign）物理隔离——acceptance 2"绝不写入 .rwdesign"）。
inline constexpr const char* kSettingsOrg = "sdurws";
inline constexpr const char* kSettingsApp = "ird-workbench";

/// 布局记忆格式版本（§4.5"版本不识别→回退默认"的判别基准；结构变更时 +1）。
inline constexpr int kLayoutFormatVersion = 1;

/// 触发侧栏自动折叠的最小窗口尺寸（§4.4："最小窗口 1280×720……低于最小
/// 尺寸时左/右栏自动折叠、底栏折叠为单行状态条"；宽或高任一低于阈值即折叠）。
inline constexpr int kCollapseWidth = 1280;
inline constexpr int kCollapseHeight = 720;

/// 各区最小内容尺寸（§4.4 原文数值：左栏 240 px、右栏 280 px、底栏 160 px；
/// 中央区吃剩余空间，浮动窗口下限由各区内容最小尺寸自然保证）。
inline constexpr int kLeftDockMinWidth = 240;
inline constexpr int kRightDockMinWidth = 280;
inline constexpr int kBottomDockMinHeight = 160;

/// 出厂窗口几何（§4.4"推荐 1920×1080"的测试/无记忆回退形态——不假设宿主
/// 屏幕不小于推荐值，取折叠阈值以上的稳妥值；跨会话记忆会覆盖此出厂值）。
inline constexpr int kFactoryWindowWidth = 1280;
inline constexpr int kFactoryWindowHeight = 800;

/// 最近项目上限（PM-10 原文："最近项目上限 10"）。
inline constexpr std::size_t kMaxRecentProjects = 10;

/// 开发日志通道 token（diagnostics.md §7.2 LogChannel ≤48 字符；对齐
/// kCatalogInternalChannel 的 "diag/<单元>" 命名族——Dev 码出线通道，
/// §6.2"Dev 走日志"）。
inline constexpr const char* kUiDevChannel = "diag/ui";

/// 最近项目的用户级设置键（PM-14；与 LayoutMemory 的 "layout" 组物理分离
/// ——布局损坏整段丢弃不得波及最近项目，§4.5"损坏段整段丢弃"的边界）。
inline constexpr const char* kRecentGroup = "recentProjects";
inline constexpr const char* kRecentKey = "paths";

/// 快捷键用户改绑的用户级设置键（PM-14/§7.3——User 绑定集持久化；格式：
/// 每条 "commandId\\t键 PortableText" 一行入 QStringList——键/命令两个
/// 事实一个键组分两条线承载，防拆半条）。与 layout 组物理分离同理。
inline constexpr const char* kShortcutGroup = "shortcuts";
inline constexpr const char* kShortcutKey = "userBindings";

/// 命令面板近期使用的用户级设置键（§7.4"用户级持久化最近 20 条"——
/// QStringList 载荷，最新在前；PM-14）。
inline constexpr const char* kPaletteRecentGroup = "palette";
inline constexpr const char* kPaletteRecentKey = "recentCommands";

// =====================================================================
// 工作台壳文案表（UX-02 的 UI-T03 过渡形态）
// =====================================================================

/**
 * @brief 壳层界面文案的单点登记表。
 *
 * 为什么不是 UiText：UiText（文案键→本地化文本解析，UX-02 唯一出口）随
 * UI-T09 落地；本表把壳层用到的全部用户可见文案集中一处（禁止散落字面量），
 * UI-T09 迁移时逐行改为 TextKey 解析、无第二处文案需要清扫（单一权威的
 * 过渡承载——登记 ui.md §10.1 v0.5）。
 */
struct WorkbenchText {
    // ---- 菜单栏（§4.1：文件/编辑/视图/阶段/工具/帮助）----
    static constexpr const char* kMenuFile = "文件";
    static constexpr const char* kMenuEdit = "编辑";
    static constexpr const char* kMenuView = "视图";
    static constexpr const char* kMenuStage = "阶段";
    static constexpr const char* kMenuTools = "工具";
    static constexpr const char* kMenuHelp = "帮助";

    // ---- 命令动作标题（§7.1 壳层子集的呈现名）----
    static constexpr const char* kCmdNewProject = "新建项目";
    static constexpr const char* kCmdOpenProject = "打开项目";
    static constexpr const char* kCmdCloseProject = "关闭项目";
    static constexpr const char* kCmdExit = "退出";
    static constexpr const char* kCmdUndo = "撤销";
    static constexpr const char* kCmdRedo = "重做";
    static constexpr const char* kCmdResetLayout = "恢复默认布局";
    static constexpr const char* kCmdPalette = "命令面板";
    static constexpr const char* kCmdSaveDraft = "保存草稿";
    static constexpr const char* kCmdApplyDraft = "应用修改";
    // 帮助菜单条目（§7.1 壳层命令 help.contents/help.about 的呈现名——
    // UI-T10 起帮助菜单从禁用占位升级为实条目；菜单序＝帮助手册在前、
    // 关于在后，与 §7.1 menuPath"帮助/帮助手册""帮助/关于"一致）。
    static constexpr const char* kCmdHelpContents = "帮助手册";
    static constexpr const char* kCmdHelpAbout = "关于";

    // ---- 视图菜单五区开关（§4.1"支持隐藏"）----
    static constexpr const char* kToggleLeft = "左栏";
    static constexpr const char* kToggleRight = "右栏";
    static constexpr const char* kToggleBottom = "底部任务和状态区";

    // ---- 五区 Dock 标题（§4.1 总图行名）----
    static constexpr const char* kDockTop = "顶栏";
    static constexpr const char* kDockLeft = "项目";
    static constexpr const char* kDockRight = "属性与诊断";
    static constexpr const char* kDockBottom = "任务和状态";

    // ---- 底部页签（§4.1：结果/任务进度/诊断/日志/下一步建议）----
    static constexpr const char* kTabResults = "结果";
    static constexpr const char* kTabTasks = "任务进度";
    static constexpr const char* kTabDiagnostics = "诊断";
    static constexpr const char* kTabLog = "日志";
    static constexpr const char* kTabAdvice = "下一步建议";

    // ---- 无项目首页（PM-10）----
    static constexpr const char* kHomeTitle = "工程工作台";
    static constexpr const char* kHomeSubtitle = "未打开项目——从下方入口开始";
    static constexpr const char* kHomeSummary = "项目状态摘要：打开项目后在此显示";
    static constexpr const char* kHomeRecent = "最近项目";
    static constexpr const char* kRecentUnavailableSuffix = "（项目位置不可用）";

    // ---- 阶段 A 占位说明（§4.1 原文口径："本阶段将在后续版本提供"，
    //      不虚构业务能力——中央区阶段面板/左/右/底区内容/入口触发提示共用）----
    static constexpr const char* kStagePlaceholder = "本阶段将在后续版本提供";
    static constexpr const char* kEntryDeferredNotice = "该入口将在后续版本提供";

    // ---- 三维视图占位面板（UI-T05 阶段 A——中央区占位契约的呈现面文案）----
    // 标题：点明中央区在产品形态下的区域身份（三维视图宿主——§4.2 中央
    // 工作区行），避免占位页被误读为泛化空白。
    static constexpr const char* kView3dTitle = "三维视图";
    // 交互清单引导行：显式声明清单内容尚未提供（呈现的是 UX-11 登记的
    // 阶段 B 交付计划，不是当前可用功能——不虚构业务能力红线）。
    static constexpr const char* kView3dManifestLead = "计划提供的视图交互（将在后续版本提供）：";

    // ---- 禁用原因文案键（ShellCommandAvailability.reasonKey 的稳定键值；
    //      值解析随 UI-T09 UiText 落地——键即契约）----
    static constexpr const char* kReasonNoProject = "reason.no-project";
    static constexpr const char* kReasonReadOnly = "reason.readonly";
    // 禁用原因的用户呈现值（键→值的 UI-T03 内联映射；迁移 UiText 时随键走）。
    static constexpr const char* kReasonNoProjectText = "当前无打开项目";
    static constexpr const char* kReasonReadOnlyText = "项目为只读，写操作不可用";

    // ---- Dev 诊断消息模板（UI-LAYOUT-RESTORE-FAILED——§3.5；Dev 级不进
    //      用户目录，消息面向开发排障：码＋损坏原因＋处置结果）----
    static constexpr const char* kLayoutRestoreFailedCode = "UI-LAYOUT-RESTORE-FAILED";

    // ---- 帮助入口反馈（UI-T10——§11.4"帮助入口链接用户手册"的状态栏
    //      用户可见反馈；成功/缺失二态都不静默：点了没反应属入口可见性
    //      违例。缺失文案不携带文件路径——呈现面零内部路径，细节走 Dev
    //      日志通道）----
    static constexpr const char* kHelpManualOpenedNotice = "用户手册已在系统查看器中打开";
    static constexpr const char* kHelpManualMissingNotice = "用户手册文件缺失（随部署提供）——详见安装目录 share 帮助文件";
};

// =====================================================================
// 壳门控事实（§7.5 UiContextSnapshot 直用——UI-T06 起命令可用性求值
// 的上下文载体；UI-T03 的 WorkbenchGateState 子集结构由该投影取代）
// =====================================================================

// 说明：§7.5 谓词求值消费 UiContextSnapshot（ICommandRegistry.hpp——
// UI-T06 冻结的阶段 A 子集：hasActiveProject/writable/草稿存在性）。壳的
// 门控事实不再是独立结构——presentProjectContext 直接构建该快照推送注册
// 表（presentContext），状态栏/徽标刷新消费同一份（单一口径，NFR-MNT-03）。

// =====================================================================
// 最近项目模型（PM-10/PM-14）
// =====================================================================

/**
 * @brief 最近项目路径表：最近使用序、按规范路径去重、上限 10、失效保留。
 *
 * 只承载路径事实；条目"位置是否可用"在读取时按文件系统现状计算（失效≠
 * 删除——PM-10 要求失效项保留并提示）。持久化载荷＝QStringList（一个
 * QSettings 键），由 UiSettingsWriter 写盘（PM-14 用户级）。
 *
 * 非线程安全：仅 UI 线程访问；写盘经 UiSettingsWriter 异步（值快照提交）。
 */
class RecentProjectsModel {
public:
    /// @brief 从持久化载荷装载（启动路径；空表＝无记忆——非损坏）。
    void load(const QStringList& stored);

    /// @brief 导出持久化载荷（写盘快照——QStringList 原生 QSettings 类型）。
    QStringList stored() const;

    /// @brief 当前条目（最近使用序，最新在前）。
    const std::vector<std::string>& entries() const { return m_paths; }

    /// @brief 登记一次成功打开：已存在则移到最前，否则插入最前并裁剪到上限。
    void note(const std::string& canonicalPath);

    /// @brief 移除一项（未在列则无操作——幂等，PM-10"移除"动作）。
    void remove(const std::string& canonicalPath);

    /**
     * @brief 规范化项目路径（去重键口径）。
     *
     * weakly_canonical：存在的路径解析真实规范形（含符号链接/相对段），
     * 不存在的路径做词法规范化——"重新选择"与"失效保留"场景下路径可以
     * 尚不存在（PM-10 去重键的工程口径）。同盘大小写差异不在本层折叠
     * （NTFS 大小写不敏感但保留大小写——登记 ui.md §10.1 v0.5 实现口径）。
     */
    static std::string canonicalize(const std::string& rawPath);

private:
    std::vector<std::string> m_paths;  ///< 最近使用序（最新在前；≤kMaxRecentProjects）
};

// =====================================================================
// 用户级设置后台落盘线程（§3.4"ui 后台落盘线程"）
// =====================================================================

/**
 * @brief 串行写盘队列（单工作线程；UI 线程只提交已序列化的值任务）。
 *
 * 纪律（§3.4 原文）：布局/设置写盘在本线程执行；任务体**禁止访问任何
 * Widget**（调用方以值捕获提交——QByteArray/QStringList 等已拷贝载荷）。
 * QSettings 实例在工作线程内构造与使用（QSettings 可重入；本类保证单线程
 * 串行，无跨线程共享实例）。
 *
 * 有界收口（§10.1 有界 shutdown）：drainAndStop 停止接收后**同步清空**
 * 剩余任务并 join——任务均为微秒级写盘，队列深度受 UI 线程提交速率约束，
 * 无无上限等待面。
 */
class UiSettingsWriter {
public:
    /// 写任务：对工作线程私有的 QSettings 执行一组写操作（值捕获——禁触 Widget）。
    using Job = std::function<void(QSettings&)>;

    UiSettingsWriter() = default;
    ~UiSettingsWriter() { drainAndStop(); }  ///< 析构兜底收口（正常路径显式调用）

    UiSettingsWriter(const UiSettingsWriter&) = delete;             ///< 非拷贝（线程所有权唯一）
    UiSettingsWriter& operator=(const UiSettingsWriter&) = delete;

    /// @brief 启动工作线程（幂等；initialize 读盘完成后才允许调用——读写不重叠）。
    void start();

    /// @brief 提交一个写任务（FIFO 串行；未 start 则丢弃并返回 false——
    ///        调用方据此置 layoutPersisted=false，不静默）。
    bool enqueue(Job job);

    /// @brief 有界收口：拒绝新任务→执行完剩余任务→join。返回剩余任务是否
    ///        全部执行（true＝队列清空）。幂等。
    bool drainAndStop();

private:
    void run();  ///< 工作线程主循环（构造 QSettings→逐任务执行→退出）

    std::mutex m_mutex;                      ///< 队列与状态锁（UI 线程提交/工作线程消费）
    std::condition_variable m_cv;            ///< 工作线程唤醒（新任务/停止）
    std::deque<Job> m_queue;                 ///< FIFO 任务队列（串行纪律的载体）
    std::thread m_thread;                    ///< 工作线程（start 后有效）
    bool m_running = false;                  ///< 工作线程存活标志
    bool m_stopRequested = false;            ///< 收口标志（drainAndStop 置位）
};

// =====================================================================
// 布局记忆（§4.5/PM-14——读写、版本判别、损坏丢弃）
// =====================================================================

/**
 * @brief 布局记忆的载荷与读写（QSettings "layout" 组；调用方线程内执行）。
 *
 * 载荷：格式版本＋窗口几何＋QMainWindow 停靠位形＋左/右/底区用户可见性
 * （Top/Central 恒在不入载荷——§4.4 最小可用布局）。页签顺序/工具栏可见性
 * 随 QMainWindow::saveState 承载（§4.5"窗口几何、停靠位形、页签顺序、
 * 工具栏可见性"）；命令面板历史随 UI-T06 增列（本头不预建）。
 *
 * 宿主形态差异（ui.md §10.1 v1.10——O-38 裁决②的机器面）：load/store 全量
 * 五键＝顶层窗口宿主形态（harness，UI-T03 以来逐字节不变）；loadFlags/
 * storeFlags 仅三区可见性键＝嵌入式 Dock 宿主形态（插件无顶层窗口——
 * 几何/位形属框架主窗口，不读写；同组同键，跨形态共享用户级设置——PM-14，
 * 且插件写旗标不触碰顶层形态留下的几何/位形键）。
 */
struct LayoutMemory {
    /// 载荷键名（"layout" 组内；字面集中防拼写漂移）。
    static constexpr const char* kGroup = "layout";
    static constexpr const char* kKeyVersion = "version";
    static constexpr const char* kKeyGeometry = "geometry";
    static constexpr const char* kKeyState = "state";
    static constexpr const char* kKeyVisibleLeft = "visibleLeft";
    static constexpr const char* kKeyVisibleRight = "visibleRight";
    static constexpr const char* kKeyVisibleBottom = "visibleBottom";

    /// 读取结果：restored＝载荷有效并已应用；corrupt＝存在载荷但损坏/版本
    /// 不识别（§4.5——回退默认＋诊断＋整段丢弃）；absent＝无记忆（出厂首次）。
    struct LoadResult {
        enum class Kind { Absent, Restored, Corrupt } kind = Kind::Absent;
        QByteArray geometry;        ///< 窗口几何（Kind::Restored 时有效）
        QByteArray state;           ///< QMainWindow 停靠位形（同上）
        bool visibleLeft = true;    ///< 左区用户可见性（同上）
        bool visibleRight = true;   ///< 右区用户可见性（同上）
        bool visibleBottom = true;  ///< 底区用户可见性（同上）
    };

    /// 旗标读取结果（嵌入式宿主形态）：kind 语义与 LoadResult 一致——
    /// corrupt＝版本不识别或旗标类型不符（§4.5 同纪律的嵌入式适配面：
    /// 几何/位形半区不在本形态的读写范围，损坏处置只覆盖旗标半区）。
    struct FlagsLoadResult {
        enum class Kind { Absent, Restored, Corrupt } kind = Kind::Absent;
        bool visibleLeft = true;    ///< 左区用户可见性（Kind::Restored 时有效）
        bool visibleRight = true;   ///< 右区用户可见性（同上）
        bool visibleBottom = true;  ///< 底区用户可见性（同上）
    };

    /// @brief 读取布局记忆（无副作用；损坏判别＝版本不匹配或字段类型不符）。
    static LoadResult load(QSettings& settings);

    /// @brief 写入布局记忆（值语义入键——调用方在工作线程内调用）。
    static void store(QSettings& settings, const QByteArray& geometry,
                      const QByteArray& state, bool visibleLeft,
                      bool visibleRight, bool visibleBottom);

    /// @brief 读取三区可见性键（嵌入式宿主；无副作用，判别口径见 FlagsLoadResult）。
    static FlagsLoadResult loadFlags(QSettings& settings);

    /// @brief 写入三区可见性键＋版本键（嵌入式宿主；不触碰几何/位形键——
    ///        QSettings 按键写入保留组内其余键）。
    static void storeFlags(QSettings& settings, bool visibleLeft,
                           bool visibleRight, bool visibleBottom);

    /// @brief 损坏段整段丢弃（§4.5 原文——remove("layout") 整组）。
    static void discard(QSettings& settings);
};

// =====================================================================
// 中央区三维视图占位面板（UI-T05 阶段 A——src/View3DPlaceholder.cpp）
// =====================================================================

/**
 * @brief 构建中央区三维视图占位面板（§4.1 阶段 A 占位——契约显式设计）。
 *
 * 面板内容＝标题＋"本阶段将在后续版本提供"说明＋UX-11 交互清单只读呈现
 * （逐行渲染 View3DContract.hpp 契约清单，每行以只读属性 view3dId 携带
 * 契约 id）。红线：纯 QLabel 呈现，零按钮/输入等可交互控件——不虚构业务
 * 能力、不以占位控件推断能力存在（§4.2/§11.3；acceptance 1）。
 * 交互实现归阶段 B（WP-10-T05 阶段 B 交付——本面板届时被三维视图宿主
 * 取代，清单与语义契约继续有效）。
 *
 * @param parent [in] 父控件（中央区页栈——所有权移交 Qt 对象树，调用方
 *               不手动释放；QObject 父子析构纪律）
 * @return 占位面板指针（窗口树子控件——不交出给壳外任何代码，ARC-02）
 */
QWidget* createView3DPlaceholder(QWidget* parent);

/**
 * @brief 构建右栏工程策略摘要只读卡（UI-T07 阶段 A——§6.7 策略摘要入口）。
 *
 * 卡面＝分组渲染的只读行集（分组异名——POL-ID-3）＋"修改策略…"跳转
 * 入口（阶段 A 仅延期提示——表单归阶段 B，不虚构编辑能力）。行数据由
 * 策略摘要端口（ShellWiring.policySource——O-31 ui 自有端口，L5 适配
 * policy::IPolicyProvider）的快照经 policySummaryRows 纯函数装配，卡
 * 控件零策略语义（模型层单一权威——PolicySummaryCard.hpp 契约）。
 * 实现于 src/PolicySummaryCard.cpp（§16.7 v0.9）。
 *
 * @param policySource [in] 策略摘要端口引用（壳已校验非空——§10.1 前置；
 *               引用须在卡存活期内有效——壳持有 wiring 共享引用保证之）
 * @param refreshOut   [in] 可选出参：接收"重拉端口快照并重建卡内容"的
 *               刷新钩子（UI 线程调用；钩子生命周期≤卡容器）
 * @param parent [in] 父控件（右栏内容区——所有权移交 Qt 对象树）
 * @return 卡容器指针（objectName=ird_policy_summary_card；不交出壳外
 *         ——ARC-02）
 */
QWidget* createPolicySummaryCard(IPolicySummarySource& policySource,
                                 std::function<void()>* refreshOut,
                                 QWidget* parent);

// =====================================================================
// WorkbenchShell 实现（UI-T16 起＝顶层窗口宿主层）
// =====================================================================

/**
 * @brief 工作台壳实现（IWorkbenchShell 的唯一实现；R-2 私有——L5 经
 *        createWorkbenchShell() 取得门面指针）。
 *
 * 宿主层定位（ui.md §10.1 v1.10——O-38 裁决②）：UI-T16 起本类收敛为
 * 「顶层窗口宿主层」——只承载顶层 QMainWindow 的窗口 chrome（菜单栏、五区
 * Dock 包裹、中央区安放、状态栏安放、出厂几何/位形快照、resize 折叠转发），
 * 一切界面语义（五区内容/命令设施/布局记忆编排/上下文投影/最近项目）都在
 * 内容装配层 WorkbenchContentImpl（WorkbenchContent_p.hpp——harness 与宿主
 * 插件共用的同一装配面）。门面方法把请求逐条转发内容装配层（语义权威——
 * 本类零界面判定逻辑）。
 *
 * 内部结构：QMainWindow（WorkbenchMainWindow 派生——resize 折叠转发钩子）
 * ＋菜单栏（§4.1 六菜单）＋五区 Dock（包裹内容装配层的五个内容 Widget）＋
 * 中央区＝内容页栈＋状态栏＝内容状态行。无 Q_OBJECT 声明（无信号槽/动属性
 * 需求——构建不开 AUTOMOC 的依据，登记 ui.md §10.1 v0.5）。
 *
 * 非线程安全：除 initialize/shutdown（允许 L5 装配线程）外仅 UI 线程。
 */
class WorkbenchShellImpl final : public IWorkbenchShell {
public:
    WorkbenchShellImpl() = default;
    ~WorkbenchShellImpl() override;  ///< 兜底收口（未 shutdown 即析构→有界拆卸）

    // ---- IWorkbenchShell（契约注释见公共头——不复制；语义权威在内容装配层）----
    bool initialize(const ShellWiring& wiring) override;
    bool regionVisible(WorkbenchRegion region) const override;
    void setRegionVisible(WorkbenchRegion region, bool visible) override;
    void resetLayout() override;
    void presentProjectContext(const ProjectContextProjection& context) override;
    ShellCommandAvailability commandAvailability(const std::string& commandId) const override;
    std::vector<RecentProjectEntry> recentProjects() const override;
    void noteRecentProject(const std::string& canonicalPath) override;
    void removeRecentProject(const std::string& canonicalPath) override;
    QWidget* mainWindow() override;
    ShellTeardownReport shutdown() override;

private:
    // ---- 宿主层 chrome 安放（initialize 内部步骤）----
    void buildMenus();                     ///< 六菜单与命令动作（§4.1——动作触发统一转发内容装配层）
    void buildDocks();                     ///< 五区 Dock 包裹内容 Widget＋中央/状态栏安放＋出厂快照
    void refreshChromeCommandStates();     ///< 菜单动作/视图开关使能态同步（内容装配层观察回调）

    // ---- 注入协作面 ----
    ShellWiring m_wiring;                  ///< 注入包（initialize 校验后持有——透传内容装配层）
    ShellTeardownReport m_lastTeardown;    ///< 末次拆卸报告（幂等 shutdown 返回值）
    std::unique_ptr<IWorkbenchContent> m_content;  ///< 内容装配层（同一装配面——语义权威）

    // ---- 窗口树（shutdown 时整体销毁——mainWindow 唯一出口的私有侧）----
    class WorkbenchMainWindow : public QMainWindow {  ///< resize 转发钩子宿主（§4.4 折叠）
    public:
        using QMainWindow::QMainWindow;
        std::function<void(QResizeEvent*)> resizeHook;  ///< 壳注入的转发回调
    protected:
        void resizeEvent(QResizeEvent* event) override;
    };
    std::unique_ptr<WorkbenchMainWindow> m_window;  ///< 主窗口唯一所有权（shutdown 即销毁——
                                                    ///  "mainWindow 后置不可再用"的实现口径）
    std::array<QDockWidget*, 5> m_docks{};     ///< 五区 Dock（WorkbenchRegion 枚举序索引——窗口树子）
    QByteArray m_factoryGeometry;              ///< 出厂几何快照（resetLayout/损坏回退基准——
                                               ///  内容装配层经几何钩子回取，窗口事实归宿主层）
    QByteArray m_factoryState;                 ///< 出厂停靠位形快照（同上——含页签序/工具栏）

    // ---- chrome 命令动作面（使能态消费注册表求值结果——本类零判定逻辑）----
    std::vector<std::pair<QAction*, std::string>> m_commandActions;  ///< 菜单动作→命令 id
    std::vector<std::pair<WorkbenchRegion, QAction*>> m_viewToggles; ///< 视图菜单开关（勾选态回写）

    // ---- 状态位 ----
    bool m_initialized = false;                ///< initialize 已成功（恰好一次判据）
};

}  // namespace detail
}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_WORKBENCHSHELL_P_HPP
