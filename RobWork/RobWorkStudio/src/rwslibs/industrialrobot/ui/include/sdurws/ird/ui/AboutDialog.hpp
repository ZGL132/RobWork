/**
 * @file   AboutDialog.hpp
 * @brief  帮助入口与关于对话框的呈现契约（UI-T10）——静态白名单 ∩ 装配
 *         报告的插件清单模型、冻结版本基线投影与关于对话框工厂（§11.4）。
 *
 * 设计依据：
 *   - 需求 UX-14（软件内帮助入口与版本信息：帮助入口链接用户手册；"关于"
 *     对话框显示产品版本与组件版本及插件清单）、NFR-DEP-05（冻结版本基线
 *     ——版本数据由 L5 提供、ui 呈现；关于框显示组件版本属 UX-02 合法
 *     场景，REQUIREMENTS UX-14 行括注原文）；
 *   - units/ui.md §11.4（关于对话框数据＝静态白名单 ∩ AssemblyReport
 *     〔逐插件：标题/版本/面板数/命令数/装配状态〕＋产品版本与组件版本；
 *     帮助入口链接用户手册〔share\ 帮助文件〕）、§11.1（静态白名单装配：
 *     白名单为编译期/装配期常量，阶段 A 占位 token 先行登记——八 token
 *     词表在本头单点冻结）、§10.9（PluginAssemblyReport 值形态与
 *     assemblyReports() 查询语义——本头承载其值形状，注册端口
 *     IPluginUiRegistrar 随装配任务落位时该形状迁移至其自有头，
 *     NFR-MNT-03 单点不变）、§3.5（文案键体系——插件标题键族
 *     plugin.<id>.title 的键冻结登记，值经 UiText 唯一出口解析）；
 *   - 任务契约 tasks/foundation/UI-T10.json acceptance 1（UI-PLG-2：清单
 *     与白名单∩报告一致、版本与注入基线一致、帮助入口链接用户手册）＋
 *     acceptance 2（O-31 处置：本交付物不持有对端类型——版本数据 L5 注入
 *     〔IUiAboutDataSource 自有端口〕、插件清单静态白名单 §11.1）；
 *   - 同构先例：PolicySummaryCard.hpp（UI-T07——契约头承载行模型＋冻结
 *     文案＋工厂，模型层与 GUI 同源互证）。
 *
 * 背景说明（为什么清单是"白名单 ∩ 报告"而不是直接读装配器）：SA-01 静态
 * 白名单装配下，"本产品有哪些插件界面"是编译期/装配期事实（§11.1 词表），
 * 而"各插件装配成什么样"（面板/命令数量、成败）是运行期累积的装配报告
 * （§10.9）。关于对话框两者都要：以白名单为行锚（词表序＝呈现序，§11.1
 * "装配顺序＝白名单顺序"），用报告充实各行明细；报告里出现白名单外条目
 * （不可能发生——注册边界拒绝 NotWhitelisted，防御性丢弃）不得进清单。
 * 阶段 A 无任何插件产出：八 token 以占位先行登记，清单照常呈现八行"未装
 * 配"——帮助入口与清单结构先行可用，不虚构装配事实（§11.4 不虚构业务
 * 能力）。
 *
 * O-31 处置（acceptance 2）：本头及对话框消费面只见 ui 自有类型（白名单
 * 常量/报告值形状/基线投影）与 IUiAboutDataSource 自有端口（UiPorts.hpp，
 * L5 适配装配器与冻结基线）；对 project/evidence/execution/policy/runtime
 * 零 include 零链接（NoCrossUnitInclude_O31_UI_BUILD 常驻扫描）。
 *
 * 线程模型：白名单/行装配/版本行装配为纯函数（内建表编译期固定），任意
 * 线程可调用；对话框工厂与 openUserManual 只允许 UI 线程（§3.4 M-1——
 * 帮助命令处理器路径）。
 */

#ifndef SDURWS_IRD_UI_ABOUTDIALOG_HPP
#define SDURWS_IRD_UI_ABOUTDIALOG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/ui/IPluginUiRegistrar.hpp>  // PluginAssemblyReport（v1.2 预注的形状迁移——NFR-MNT-03 单点：报告形状自此归 IPluginUiRegistrar.hpp，本头经包含保持兼容）
#include <sdurws/ird/ui/UiTypes.hpp>  // TextKey（插件标题/装配状态文案键——§3.5）

class QDialog;   // 前置声明：createAboutDialog 返回类型；头文件不拖入
class QWidget;   // Widgets（消费者按需自含——IWorkbenchShell.hpp 同款口径）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 静态白名单（§11.1——编译期/装配期常量的运行时只读载体）
// =====================================================================

/**
 * @brief 插件界面静态白名单（§11.1 冻结词表：装配顺序＝白名单顺序）。
 *
 * 八 token 逐字对齐 §11.1 原文（modeling, requirements, kinematics,
 * trajectory, dynamics, selection, optimization, workflow——阶段 A 均未
 * 产出，占位 token 先行登记）。注意 token 是**插件注册标识**（§10.9
 * PluginUiDescriptor.pluginId 同词表），不是用户可见文案：UX-02"界面禁止
 * 出现内部插件名"——清单标题列经 pluginTitleKey 键解析（见下），token 本
 * 身只作模型锚点（行 id/测试断言），不进任何用户可见文本。
 *
 * 为什么是函数而不是全局常量数组：调用方拿到值拷贝即与内部表隔离（避免
 * 引用逸出后被改写——白名单是装配期冻结事实，语义上只读）；表内容经
 * static const 数组承载，首次调用后恒定（进程生命周期内稳定）。
 *
 * @return 八 token（§11.1 词表序；纯函数——每次新值拷贝，内容编译期固定）
 */
std::vector<std::string> pluginUiWhitelist();

/**
 * @brief 插件标题文案键（§3.5 键族 plugin.<id>.title——UI-T10 冻结登记）。
 *
 * token→键的词法拼接是唯一允许引用 token 的呈现路径（键的值半区在 UiText
 * 内建表登记中文名，token 本身不进用户文本）。八 token 全部有登记键
 * （AboutDialogModelTest 逐 token 解析自证——键表与白名单同步由测试钉住，
 * 漏登记＝resolveText fail-fast 而非显示键名）。
 *
 * @param pluginId [in] 白名单 token（pluginUiWhitelist() 词表值）
 * @return 文案键（"plugin.<token>.title"）
 */
TextKey pluginTitleKey(const std::string& pluginId);

// =====================================================================
// 装配报告值形状（§10.9 PluginAssemblyReport——关于框数据源的报告半区）
// =====================================================================

// 单插件装配报告——形状已迁移至 IPluginUiRegistrar.hpp（本头经包含保持
// 同名类型可见——NFR-MNT-03 单点；WP-24-T03 首版装配落位时执行迁移，
// v1.2 预注兑现；形状零变化，源/二进制兼容）。

// =====================================================================
// 冻结版本基线投影（NFR-DEP-05——L5 注入值，ui 呈现）
// =====================================================================

/**
 * @brief 单组件版本行（NFR-DEP-05 冻结基线登记的组件条目投影）。
 *
 * componentName 为组件呈现名（L5 按冻结基线登记，如框架/构建组件名——
 * 注入值原样呈现，ui 不维护组件词表）；versionText 为版本文本（注入值
 * 原样呈现——与冻结基线一致的语义即"ui 不改写、不补默认"，显示什么由
 * L5 给什么）。空串版本＝基线未登记该项——呈现层以「不适用」占位
 * （ERR-01 显式化，不伪造版本号）。
 */
struct AboutComponentVersion {
    /// 组件呈现名（L5 注入；空名＝调用方装配错误，装配函数 fail-fast）。
    std::string componentName;
    /// 版本文本（L5 注入；空串＝未登记——呈现「不适用」占位）。
    std::string versionText;
};

/**
 * @brief 冻结版本基线投影（关于框"产品/组件版本"区的全部数据源）。
 *
 * available==false＝L5 未注入基线（阶段 A 无 WP-24-T01 产出、或部署未
 * 携带）——版本区呈现「未装载」占位，不虚构任何版本号；available==true
 * 时 productVersion/components 为 L5 按冻结基线（NFR-DEP-05）登记的值，
 * ui 逐字呈现。投影≠重定义（NFR-MNT-03）：基线的权威记录在 L5/部署侧，
 * 本结构只是呈现值载体。
 */
struct AboutVersionBaseline {
    /// 基线是否已注入（false＝版本区整体占位——不逐项伪造）。
    bool available = false;
    /// 产品版本文本（available==true 时有效；空串呈现「不适用」）。
    std::string productVersion;
    /// 组件版本清单（序＝注入序——呈现不重排，NFR-COR-02 精神）。
    std::vector<AboutComponentVersion> components;
};

// =====================================================================
// 插件清单行模型（§11.4 五列——模型产最终呈现文本，GUI 逐字渲染）
// =====================================================================

/**
 * @brief 插件装配状态词表（§11.4"装配状态"列的三值承载）。
 *
 * NotAssembled＝白名单在位但装配期无该插件报告（阶段 A 占位常态；§11.3
 * "白名单插件未注册"的清单侧呈现）；Ok＝报告 ok==true；Failed＝报告
 * ok==false（§11.3 装配失败——失败隔离不中止启动，清单如实呈现失败态）。
 * 枚举一经交付只允许表尾追加并升单元卡修订（状态文案被测试钉住）。
 */
enum class PluginAssemblyStatus : std::uint8_t {
    NotAssembled, ///< 未装配（白名单占位——无报告条目）
    Ok,           ///< 已装配（报告 ok==true）
    Failed,       ///< 装配失败（报告 ok==false——§11.3）
};

/**
 * @brief 装配状态标签文案键（§3.5 键族 plugin.assembly.<state>.label——
 *        UI-T10 冻结登记；三态全登记，未知枚举值为调用方错误）。
 *
 * @param status [in] 装配状态（全值皆有登记键）
 * @return 文案键（如 "plugin.assembly.ok.label"）
 */
TextKey pluginAssemblyStatusLabelKey(PluginAssemblyStatus status);

/**
 * @brief 插件清单一行（§11.4 逐插件五列：标题/版本/面板数/命令数/装配
 *        状态——文本列已由模型经 UiText 解析为最终呈现文本）。
 *
 * 与 PolicySummaryRow 同纪律：模型层产出即最终呈现文本（GUI 不二次加工，
 * UX-02 呈现值单一出口）；pluginId 是行级稳定锚（GUI objectName 与测试
 * 断言以它定位），不进用户可见文本。titleText 经 pluginTitleKey 键解析
 * （中文名），statusText 经装配状态键解析，versionText 在报告无版本事实
 * 时为「不适用」占位（§10.9 报告形状无版本字段——不虚构，版本来源随装
 * 配任务增量登记）。
 */
struct AboutPluginRow {
    /// 行锚＝白名单 token（机器面定位用；禁止进用户文本——UX-02）。
    std::string pluginId;
    /// 插件标题（plugin.<token>.title 解析值——中文名）。
    std::string titleText;
    /// 装配状态（三态词表——呈现文本在 statusText）。
    PluginAssemblyStatus status = PluginAssemblyStatus::NotAssembled;
    /// 装配状态呈现文本（plugin.assembly.<state>.label 解析值）。
    std::string statusText;
    /// 面板数（报告值；未装配＝0——"没有面板"是事实不是占位）。
    std::size_t panelCount = 0;
    /// 命令数（报告值；未装配＝0）。
    std::size_t commandCount = 0;
    /// 插件版本呈现文本（阶段 A 恒「不适用」——见结构体注释）。
    std::string versionText;
    /// 失败诊断码（status==Failed 时透传报告值——呈现归 §9.1/UI-T13，
    /// 本清单只承载失败证据不丢弃）。
    std::vector<std::string> failureDiagnostics;
};

/**
 * @brief 组装插件清单行集（§11.4"静态白名单 ∩ AssemblyReport"的唯一权威
 *        装配——UI-PLG-2 的数据源断言锚）。
 *
 * 装配规则（冻结——测试逐条断言的依据）：
 *   1. 行集＝白名单全集：每个白名单 token 恰一行，行序＝白名单序（§11.1
 *      "装配顺序＝白名单顺序"的呈现延伸）——清单与静态白名单一致
 *      （acceptance 1 字面）；
 *   2. 报告充实：token 命中报告→行取报告值（ok/面板数/命令数/失败码，
 *      status 随 ok 二值）；未命中→NotAssembled、计数 0（阶段 A 占位）；
 *   3. 交集丢弃：报告里白名单外条目不产生行（防御——注册边界本应拒绝，
 *      报告数据异常时清单仍与白名单一致，不受污染）；
 *   4. 文本列经 UiText 解析（标题/状态键族——漏登记 fail-fast 上抛），
 *      版本列恒「不适用」占位（见 AboutPluginRow 注释）。
 *
 * 为什么是纯函数：清单一致性（acceptance 1"与静态白名单一致"）是可独立
 * 断言的数据事实，纯函数使模型层（无 GUI）即可对照白名单/报告逐行验证
 * ——GUI 只渲染（PolicySummaryCard 同案，单一权威）。
 *
 * @param reports [in] 装配报告集（IUiAboutDataSource 注入值；序无关——
 *                装配按白名单序对位，报告内重复 pluginId 取首个——装配器
 *                "每插件恰好一次"（§11.1）违约时的确定性防御）
 * @return 清单行集（行数恒等于白名单大小——八行；纯函数，同输入同输出）
 *
 * @throws std::invalid_argument 白名单 token 的标题/状态键未在 UiText 登记
 *         （键表与词表失同步＝调用方装配缺陷，fail-fast 不显示键名）
 */
std::vector<AboutPluginRow>
aboutPluginRows(const std::vector<PluginAssemblyReport>& reports);

// =====================================================================
// 版本行装配（NFR-DEP-05 呈现——"与冻结基线一致"的模型半区）
// =====================================================================

/**
 * @brief 版本区一行（键＋标签＋呈现值——GUI 逐行渲染为只读文本）。
 *
 * key 是行级稳定锚（objectName 后缀；"product"＝产品版本行，组件行按
 * "component-<注入序>"编号——组件名来自注入值不宜直接作 objectName 词根，
 * 隔离注入内容与控件命名）。valueText 已含占位判定（模型产最终文本）。
 */
struct AboutVersionRow {
    /// 行稳定键（"product"/"component-<n>"——objectName 与测试断言锚）。
    std::string key;
    /// 行标签（"产品版本"或组件呈现名——组件名注入值原样）。
    std::string label;
    /// 行值文本（注入值原样／「不适用」占位——不伪造版本号）。
    std::string valueText;
};

/**
 * @brief 组装版本区行集（关于框版本区的呈现数据权威——模型与 GUI 同源）。
 *
 * 装配规则（冻结）：
 *   - available==false：仅一行占位（"版本基线"→kAboutBaselineNotLoadedText，
 *     不虚构任何版本号——与策略摘要卡"未装载"同口径）；
 *   - available==true：产品版本行（标签 kAboutProductVersionLabel，值为
 *     注入 productVersion；空串→「不适用」）＋组件行逐条（序＝注入序；
 *     值空串→「不适用」）。
 *
 * @param baseline [in] 版本基线投影（IUiAboutDataSource 注入值）
 * @return 版本行集（占位形态 1 行；已装载形态 1+n 行——n＝组件数）
 *
 * @throws std::invalid_argument 组件呈现名为空串（available==true 而组件
 *         无名＝注入方装配错误，fail-fast——无名行无法呈现也无法定位）
 */
std::vector<AboutVersionRow> aboutVersionRows(const AboutVersionBaseline& baseline);

// =====================================================================
// 用户手册入口（§11.4"帮助入口链接用户手册"——share\ 帮助文件）
// =====================================================================

/**
 * @brief 用户手册入口文件的解析路径（share\ 帮助文件约定的落位口径）。
 *
 * 解析规则（实现口径——登记 ui.md §16.7 v1.2）：以应用可执行文件目录
 * （QCoreApplication::applicationDirPath）为基准的相对部署布局
 * "<appDir>/../share/ird/user-manual/index.html"（部署树 bin\ 与 share\
 * 同级的框架惯例）。文件是否真实存在由 openUserManual 判定——阶段 A 部
 * 署树无手册文件，入口行为＝用户可见反馈（不虚构"已打开"）。
 *
 * @return 入口文件绝对路径（UTF-8；不要求存在——存在性判定在打开动作）
 *
 * @note UI 线程调用；QCoreApplication 实例必须已构造（applicationDirPath
 *       前置——模型测试环境同构）。
 */
std::string userManualPath();

/**
 * @brief 打开用户手册（§11.4 帮助入口的执行半区——help.contents 命令
 *        处理器调用）。
 *
 * 入口文件存在时经系统打开器（QDesktopServices::openUrl——链接用户手册
 * 的语义本体）；不存在时**不启动**任何打开动作、返回 false，由调用方给
 * 出用户可见反馈（壳层状态栏说明＋Dev 日志——不静默：入口点了没反应属
 * 违反 §11.4 用户可见性）。
 *
 * @return true＝已交系统打开器；false＝入口文件缺失（未启动打开动作）
 *
 * @note UI 线程调用（§3.4——命令处理器路径）；本函数零用户可见文本产出
 *       （反馈文案归调用方——单一职责，测试可断言返回值）。
 */
bool openUserManual();

// =====================================================================
// 冻结文案（关于框契约文案单点——模型测试与 GUI 同源消费）
// =====================================================================

/// 对话框窗口标题（§11.4"关于对话框"——标准命名）。
inline constexpr const char* kAboutDialogTitle = "关于";

/// 版本区标题（产品/组件版本区——NFR-DEP-05 呈现面）。
inline constexpr const char* kAboutVersionsSectionTitle = "版本信息";

/// 插件清单区标题（§11.4"插件清单"）。
inline constexpr const char* kAboutPluginsSectionTitle = "插件清单";

/// 基线未装载占位（AboutVersionBaseline.available==false 的版本区呈现；
/// 不虚构版本号——与 PolicySummaryCard kPolicyNotLoadedText 同口径）。
inline constexpr const char* kAboutBaselineNotLoadedText = "未装载";

/// 产品版本行标签（available==true 的首行；产品名由部署侧呈现语境承载，
/// 本行只登记"版本"语义标签——不把产品名冻结进 ui）。
inline constexpr const char* kAboutProductVersionLabel = "产品版本";

/// 基线未装载时的占位行标签（版本区唯一一行的标签半区）。
inline constexpr const char* kAboutBaselineRowLabel = "版本基线";

/// 对话框 objectName（GUI 测试定位锚——ird_ 前缀单元命名惯例）。
inline constexpr const char* kAboutDialogObjectName = "ird_about_dialog";

/// 插件清单表 objectName（同上）。
inline constexpr const char* kAboutPluginTableObjectName = "ird_about_plugins";

/// 版本行 objectName 词根（完整形态＝词根＋"."＋AboutVersionRow.key）。
inline constexpr const char* kAboutVersionRowObjectNamePrefix = "ird_about_version_";

/// 插件清单表列头（§11.4 五列冻结词——列序即呈现序，模型/GUI/测试同源）。
inline constexpr const char* kAboutPluginColTitle = "标题";
inline constexpr const char* kAboutPluginColVersion = "版本";
inline constexpr const char* kAboutPluginColPanels = "面板数";
inline constexpr const char* kAboutPluginColCommands = "命令数";
inline constexpr const char* kAboutPluginColStatus = "装配状态";

// =====================================================================
// 关于对话框工厂（§11.4 呈现面——模态语义经 windowModality，非阻塞打开）
// =====================================================================

/**
 * @brief 创建关于对话框（§11.4 关于页的 Widget 承载——帮助命令处理器
 *        消费；所有权移交调用方/Qt 父子树）。
 *
 * 呈现纪律（冻结——GUI 测试断言依据）：
 *   - 上半区版本信息（kAboutVersionsSectionTitle）：aboutVersionRows 逐行
 *     渲染为只读文本（"标签：值"），行 objectName＝词根＋key；
 *   - 下半区插件清单（kAboutPluginsSectionTitle）：aboutPluginRows 行集
 *     渲染为只读表（标题/版本/面板数/命令数/装配状态五列——§11.4 原文
 *     列序），表 objectName 见常量；
 *   - 全部文本为模型产出值（本工厂零字面量文案、零数据加工——同源纪律
 *     ：GUI 呈现与模型断言逐字一致）。
 *
 * 为什么返回裸指针且不 show：QDialog 生命周期交 Qt 父子树（parent 为
 * nullptr 时调用方自管）；打开动作归调用方——以 QDialog::open()（窗口
 * 模态、非阻塞）呈现，**不用 exec()**：命令处理器内的嵌套事件循环会让
 * §7.7 提交时序重入（模态语义由 windowModality 承载，事件循环不重入，
 * §3.4 纪律），GUI 测试也因此可在同栈断言对话框可见性。
 *
 * @param baseline   [in] 版本基线投影（版本区数据源——值拷贝）
 * @param pluginRows [in] 插件清单行集（aboutPluginRows 产出——值拷贝；
 *                   空集合法＝清单区空表头呈现，不虚构行）
 * @param parent     [in] 父窗口（可为 nullptr——测试直构场景）
 * @return 未显示的对话框实例（open()/exec() 由调用方决定——本工厂不显示）
 *
 * @note UI 线程调用（§3.4——帮助命令处理器路径）。
 */
QDialog* createAboutDialog(const AboutVersionBaseline& baseline,
                           const std::vector<AboutPluginRow>& pluginRows,
                           QWidget* parent);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_ABOUTDIALOG_HPP
