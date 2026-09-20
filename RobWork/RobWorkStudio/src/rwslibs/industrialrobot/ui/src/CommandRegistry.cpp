/**
 * @file   CommandRegistry.cpp
 * @brief  命令注册表实现（ICommandRegistry 的唯一实现）——登记/查询/提交
 *         三面＋面板模糊投影＋UI-CMD-* 诊断出线。
 *
 * 设计依据：
 *   - units/ui.md §7.2（注册协议：句法/白名单/重复拒绝＋UI-CMD-DUPLICATE）、
 *     §7.4（面板模糊搜索规则原文：子序列匹配大小写不敏感＋词边界前缀加权
 *     ＋关键字命中加权；并列按 registrationOrder；上限 50；近期置顶）、
 *     §7.5/§7.6（谓词求值口径与只读双保险）、§10.3（契约表：未知拒绝＋
 *     UI-CMD-UNKNOWN/不可执行拒绝＋UI-CMD-NOT-EXECUTABLE/处理器异常捕获
 *     不穿透事件循环）；
 *   - O-31 裁决（任务契约 UI-T06 acceptance 3）：本实现零对
 *     project/evidence/execution/policy/runtime 的链接或 include——提交
 *     协作由处理器经 IUiCommandGateway 端口（L5 适配）完成，注册表只派发
 *     处理器（NoCrossUnitInclude_O31_UI_BUILD 守卫常驻自证）；
 *   - P-UI-10 处置（契约 acceptance 3 维持）：UI-CMD-＊ 与 UI-HOTKEY-CONFLICT
 *     码值按 §3.5 建议值经 uiDiagnosticCodeDescriptors 向 diagnostics
 *     StableCodeRegistry 注册（动作归 L5 装配序列）——本实现的诊断出线
 *     走 IDiagnosticFactory::create 唯一入口（码未注册＝装配序违约，异常
 *     显性失败，不吞）。
 *
 * 线程模型：全部方法 UI 线程（§3.4 M-1）；非线程安全（§10.3 契约表）。
 */

#include <sdurws/ird/ui/ICommandRegistry.hpp>

#include <QKeySequence>
#include <QtGlobal>  // Q_ASSERT（运行期注册违约的调试期断言——§10.3）

#include <algorithm>
#include <utility>

#include <sdurws/ird/core/DiagData.hpp>   // core::DiagnosticRecord/ComparativeFields（诊断记录装配——表内边）
#include <sdurws/ird/core/Identity.hpp>   // core::ObjectId（用户级码 subject——core §4.8 完整性强制）
#include <sdurws/ird/core/Provenance.hpp> // core::SourcedValue（比较字段「不适用」四态承载）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 过渡文案表（UI-T09 UiText 前的呈现承载——键冻结、值过渡；
// statusWordTransitionalLabel 同案，迁移时键不变只换值源）
// =====================================================================

namespace {

/// §7.1 最小命令集的标题过渡中文（键 "cmd.<id>.title" → 值；§7.1 表行序）。
constexpr struct {
    const char* key;    ///< 文案键（cmd.<id>.title）
    const char* title;  ///< 过渡中文标题
} kTitleTable[] = {
    {"cmd.project.new.title",              "新建项目"},
    {"cmd.project.open.title",             "打开项目"},
    {"cmd.draft.save.title",               "保存草稿"},
    {"cmd.draft.apply.title",              "应用修改"},
    {"cmd.project.undo.title",             "撤销"},
    {"cmd.project.redo.title",             "重做"},
    {"cmd.scheme.switch.title",            "切换方案"},
    {"cmd.project.saveAs.title",           "项目另存为"},
    {"cmd.package.export.title",           "导出评估包"},
    {"cmd.report.export.title",            "导出报告"},
    {"cmd.analysis.collisionCheck.title",  "碰撞检查"},
    {"cmd.view.displayMode.title",         "显示模式"},
    {"cmd.view.resetHome.title",           "复位到 home 位"},
    {"cmd.view.resetZero.title",           "复位到零位"},
    {"cmd.workbench.commandPalette.title", "命令面板"},
    {"cmd.workbench.closeProject.title",   "关闭项目"},
    {"cmd.view.resetLayout.title",         "恢复默认布局"},
    {"cmd.help.about.title",               "关于"},
    {"cmd.help.contents.title",            "帮助手册"},
};

/// §7.1 最小命令集的关键字过渡中文（键 "cmd.<id>.kw.<n>" → 值；UX-13
/// 模糊搜索的中文命中词——面板可用性的关键字半区；行集与壳登记表
/// WorkbenchShell.cpp kMinimalCommandRows 的关键字键一一对应）。
constexpr struct {
    const char* key;      ///< 文案键（cmd.<id>.kw.<n>）
    const char* keyword;  ///< 过渡中文关键字
} kKeywordTable[] = {
    {"cmd.project.new.kw.0",             "新建"},
    {"cmd.project.new.kw.1",             "创建"},
    {"cmd.project.open.kw.0",            "打开"},
    {"cmd.draft.save.kw.0",              "保存"},
    {"cmd.draft.apply.kw.0",             "应用"},
    {"cmd.project.undo.kw.0",            "撤销"},
    {"cmd.project.redo.kw.0",            "重做"},
    {"cmd.scheme.switch.kw.0",           "方案"},
    {"cmd.project.saveAs.kw.0",          "另存为"},
    {"cmd.package.export.kw.0",          "导出"},
    {"cmd.report.export.kw.0",           "导出"},
    {"cmd.analysis.collisionCheck.kw.0", "碰撞"},
    {"cmd.analysis.collisionCheck.kw.1", "检查"},
    {"cmd.view.displayMode.kw.0",        "显示"},
    {"cmd.view.resetHome.kw.0",          "home"},
    {"cmd.view.resetHome.kw.1",          "复位"},
    {"cmd.view.resetZero.kw.0",          "零位"},
    {"cmd.view.resetZero.kw.1",          "复位"},
    {"cmd.workbench.commandPalette.kw.0", "面板"},
    {"cmd.workbench.commandPalette.kw.1", "搜索"},
    {"cmd.workbench.closeProject.kw.0",  "关闭"},
    {"cmd.view.resetLayout.kw.0",        "布局"},
    {"cmd.help.about.kw.0",              "关于"},
    {"cmd.help.contents.kw.0",           "帮助"},
};

/// 过渡表查找（键未登记返回空串——调用方回退，不虚构文案）。
std::string lookupTitle(const std::string& key)
{
    for (const auto& row : kTitleTable) {
        if (key == row.key) {
            return row.title;
        }
    }
    return {};
}

std::string lookupKeyword(const std::string& key)
{
    for (const auto& row : kKeywordTable) {
        if (key == row.key) {
            return row.keyword;
        }
    }
    return {};
}

}  // namespace

std::string commandTransitionalTitle(const TextKey& titleKey)
{
    return lookupTitle(titleKey);
}

std::string commandTransitionalKeyword(const TextKey& keywordKey)
{
    return lookupKeyword(keywordKey);
}

// =====================================================================
// 模糊匹配（§7.4 规则的唯一实现点——纯函数，模型层可断言）
// =====================================================================

namespace {

/// 小写折叠（ASCII 与 Qt 中文无大小写语义——toLower 只影响拉丁词）。
QString lowerFold(const QString& text)
{
    return text.toLower();
}

/// 子序列判定（大小写不敏感——§7.4 原文）：query 逐字符按序出现在
/// target 中即命中；返回首字符命中位（无命中→-1）。
int subsequencePos(const QString& target, const QString& query)
{
    if (query.isEmpty()) {
        return 0;
    }
    int cursor = 0;
    for (const QChar qc : query) {
        const int found = target.indexOf(qc, cursor);
        if (found < 0) {
            return -1;  // 任一字符断链即非子序列
        }
        cursor = found + 1;
    }
    return static_cast<int>(target.indexOf(query.front()));
}

/// 单字段得分（词表：词边界前缀＞连续包含＞散点子序列；权重越高越靠前）。
int fieldScore(const QString& field, const QString& query)
{
    if (query.isEmpty()) {
        return 1;  // 空查询＝全命中基线分（排序退化为注册序）
    }
    const int pos = field.indexOf(query);
    if (pos == 0) {
        return 60;  // 字段前缀（词边界前缀的最强形态——§7.4"词边界前缀加权"）
    }
    // 词边界前缀：分隔符（'.'/' '/'-'/'/'）后的段以 query 开头。
    int scan = static_cast<int>(field.indexOf(query));
    while (scan > 0) {
        const QChar prev = field.at(scan - 1);
        if (prev == QLatin1Char('.') || prev == QLatin1Char('-')
            || prev == QLatin1Char(' ') || prev == QLatin1Char('/')) {
            return 50;  // 词边界命中
        }
        scan = field.indexOf(query, scan + 1);
    }
    if (pos > 0) {
        return 30;  // 连续包含（词中缀）
    }
    if (subsequencePos(field, query) >= 0) {
        return 10;  // 散点子序列（§7.4"子序列匹配"兜底）
    }
    return -1;  // 未命中
}

/// 单命令对模糊词的综合得分（>0＝命中；关键字命中加权＞标题＞id＞菜单路径
/// ——§7.4"关键字命中加权"的权重序。加成只作用于**已命中**字段：未命中
/// 字段保持 -1，否则"未命中＋加成"会变成假命中——加权是命中内的排序
/// 加权，不是命中本身）。
int commandMatchScore(const CommandDescriptor& desc, const QString& query)
{
    const auto lift = [](int score, int bonus) { return score > 0 ? score + bonus : score; };
    const QString id = lowerFold(QString::fromStdString(desc.id));
    const QString title = lowerFold(QString::fromStdString(commandTransitionalTitle(desc.titleKey)));
    // 得分取各字段最大值（任一字段强命中即代表命令可达——不累加，
    // 避免"多字段重复计权"把权重设计复杂化——稳定优先）。
    int best = fieldScore(id, query);
    best = std::max(best, lift(fieldScore(title, query), 10));  // 标题命中比 id 命中更贴近用户语义
    for (const TextKey& kw : desc.keywordKeys) {
        const QString text = lowerFold(QString::fromStdString(commandTransitionalKeyword(kw)));
        best = std::max(best, lift(fieldScore(text, query), 20));  // 关键字最高权重（§7.4 加权）
    }
    const QString menu = lowerFold(QString::fromStdString(desc.menuPath));
    best = std::max(best, fieldScore(menu, query));
    return best;
}

/// 命令 id 句法（§7.2 第 1 步）：点分段（"." 分隔、至少一段、段非空），
/// 段内字符 [A-Za-z0-9-]。
///
/// 措辞差登记（ui.md §16.7 v0.8）：§7.1 注记"点分小写"与本表冻结 id 的
/// 实际词形不一致——表内 workbench.commandPalette／view.displayMode／
/// view.resetHome／view.resetZero／help.contents 等段含大写（camelCase）。
/// id 词形的唯一权威＝§7.1 冻结表原文（UI-T03 壳板已按表登记同一批 id），
/// 本校验按冻结词形放行：点分结构＋段字符集强制，段内大小写不强制——
/// "全部小写"作为词表约定保留，不私改成强制（不重构需求语义）。
bool isValidCommandId(const std::string& id)
{
    if (id.empty() || id.size() > 128) {
        return false;  // 空串/超长（128＝防御上限——id 进文案键，过长无意义）
    }
    bool segmentHasChar = false;
    bool hasDot = false;
    for (const char c : id) {
        if (c == '.') {
            if (!segmentHasChar) {
                return false;  // 空段（"a..b"/前导点）
            }
            segmentHasChar = false;
            hasDot = true;
            continue;
        }
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isLower = (c >= 'a' && c <= 'z');
        const bool isUpper = (c >= 'A' && c <= 'Z');
        if (!isDigit && !isLower && !isUpper && c != '-') {
            return false;  // 段内字符越界（下划线/空白/中文等——§7.1 词形）
        }
        segmentHasChar = true;
    }
    return segmentHasChar && hasDot;
}

}  // namespace

// =====================================================================
// CommandRegistryImpl——唯一实现
// =====================================================================

namespace {

/// 单条登记项（描述符拷贝＋处理器＋可选谓词——注册后描述符不可变）。
struct CommandEntry {
    CommandDescriptor descriptor;
    ICommandRegistry::CommandHandler handler;
    VisibilityPredicate visible = nullptr;      ///< 可见谓词（nullptr＝默认恒可见）
    EnablementPredicate enablement = nullptr;   ///< 使能谓词（nullptr＝默认作用域/只读规则）
};

/// 默认使能规则（§7.5/§7.6——与 UI-T03 壳板同口径）：Session/View 恒可用；
/// Project 须有项目，readOnlyAllowed=false 者还须可写。
std::optional<DisableReason> defaultEnablement(const CommandDescriptor& desc,
                                               const UiContextSnapshot& snap)
{
    if (desc.scope != ShellCommandScope::Project) {
        return std::nullopt;  // 会话/视图命令无项目也可用（PM-10 首页面）
    }
    if (!snap.hasActiveProject) {
        return std::string{"reason.no-project"};  // PM-10"禁用＋说明"
    }
    if (!desc.readOnlyAllowed && !snap.writable) {
        return std::string{"reason.readonly"};    // §7.6 只读条件
    }
    return std::nullopt;
}

class CommandRegistryImpl final : public ICommandRegistry {
public:
    explicit CommandRegistryImpl(CommandRegistryDeps deps) : m_deps(std::move(deps)) {}

    // ---- 装配期 ----

    RegistrationResult registerCommand(const CommandDescriptor& descriptor,
                                       CommandHandler handler) override
    {
        // 无自定义谓词＝默认谓词轨（可见恒真/使能按作用域只读）。
        return registerInternal(descriptor, std::move(handler), nullptr, nullptr);
    }

    RegistrationResult registerCommandWithPredicates(
        const CommandDescriptor& descriptor, CommandHandler handler,
        VisibilityPredicate visible, EnablementPredicate enablement) override
    {
        return registerInternal(descriptor, std::move(handler), visible, std::move(enablement));
    }

    void seal() override
    {
        m_sealed = true;  // 幂等（§7.2 运行期只读——无注销接口）
    }

    // ---- 运行期 ----

    void presentContext(const UiContextSnapshot& snapshot) override
    {
        m_context = snapshot;  // 值快照（谓词求值的唯一上下文——§7.5）
    }

    std::vector<CommandView> query(const CommandQuery& query) const override
    {
        std::vector<CommandView> out;
        const QString fuzzyText = lowerFold(QString::fromStdString(query.fuzzy));
        for (const CommandEntry& entry : m_entries) {
            if (query.category.has_value() && entry.descriptor.category != *query.category) {
                continue;  // 分类过滤
            }
            // 模糊过滤：非空词必须命中（与面板同一匹配器——单一口径）。
            if (!fuzzyText.isEmpty()
                && commandMatchScore(entry.descriptor, fuzzyText) <= 0) {
                continue;
            }
            out.push_back(buildView(entry));
            if (query.limit > 0 && out.size() >= query.limit) {
                break;  // 上限截断（注册序优先——§7.2 稳定序）
            }
        }
        return out;
    }

    CommandAvailability availability(CommandId id) const override
    {
        const CommandEntry* entry = findEntry(id);
        if (entry == nullptr) {
            return CommandAvailability{};  // 未注册＝全 false（不虚构存在性）
        }
        return evaluateAvailability(*entry);
    }

    CommandOutcome submit(CommandId id, std::vector<CommandParameter> params) override
    {
        // 前置校验链（§7.7 时序①）：未知→UI-CMD-UNKNOWN；不可执行/只读→
        // UI-CMD-NOT-EXECUTABLE——两拒绝都不派发（命令恰好执行一次或被拒）。
        const CommandEntry* entry = findEntry(id);
        if (entry == nullptr) {
            emitUserDiag("UI-CMD-UNKNOWN", "commands.submit",
                         "提交了未注册命令",
                         "命令 id '" + id + "' 未在命令注册表登记",
                         "检查命令 id 拼写，或确认提供该命令的插件已完成装配",
                         std::nullopt);
            return CommandOutcome{};  // accepted=false（拒绝轨）
        }
        const CommandAvailability avail = evaluateAvailability(*entry);
        if (!avail.enabled) {
            emitUserDiag("UI-CMD-NOT-EXECUTABLE", "commands.submit",
                         "命令在当前上下文不可执行",
                         "命令 '" + id + "' 被界面使能态拒绝（原因键 "
                             + (avail.disableReasonKey.empty() ? std::string{"(未给出)"} : avail.disableReasonKey)
                             + (avail.readOnlyBlocked ? "；只读阻断" : "") + "）",
                         "按禁用原因提示调整上下文（如打开项目/解除只读）后重试",
                         std::nullopt);
            CommandOutcome out;
            out.messageKey = avail.disableReasonKey.empty()
                                 ? std::optional<TextKey>{}
                                 : std::optional<TextKey>{avail.disableReasonKey};
            return out;
        }

        // 处理器执行（§10.3 错误类型行：异常→捕获→NOT-EXECUTABLE＋Dev 日志
        // ——不让异常穿透事件循环）。
        CommandOutcome out;
        try {
            out = entry->handler(params);
        } catch (const std::exception& e) {
            emitDev("UI-CMD-NOT-EXECUTABLE", std::string{"命令处理器异常（已捕获，未穿透）: "}
                                                  + e.what());
            emitUserDiag("UI-CMD-NOT-EXECUTABLE", "commands.submit",
                         "命令在当前上下文不可执行",
                         "命令 '" + id + "' 处理器执行异常（详情见开发日志）",
                         "请重试；若持续出现请导出诊断反馈",
                         std::nullopt);
            return CommandOutcome{};
        } catch (...) {
            emitDev("UI-CMD-NOT-EXECUTABLE", "命令处理器抛出非 std::exception（已捕获，未穿透）");
            emitUserDiag("UI-CMD-NOT-EXECUTABLE", "commands.submit",
                         "命令在当前上下文不可执行",
                         "命令 '" + id + "' 处理器执行异常（详情见开发日志）",
                         "请重试；若持续出现请导出诊断反馈",
                         std::nullopt);
            return CommandOutcome{};
        }
        out.accepted = true;

        // 近期使用登记（§7.4——接受即置顶去重；上限 20）。
        noteRecent(id);
        return out;
    }

    std::vector<CommandView> paletteSnapshot(const std::string& fuzzy,
                                             std::size_t limit) const override
    {
        const QString query = lowerFold(QString::fromStdString(fuzzy));
        // 匹配集＝可见命令（§7.4 可见性过滤——隐藏命令不进面板）。
        struct Scored {
            const CommandEntry* entry;
            int score;
        };
        std::vector<Scored> matched;
        for (const CommandEntry& entry : m_entries) {
            if (!evaluateAvailability(entry).visible) {
                continue;  // §7.4"按可见谓词过滤"
            }
            const int score = query.isEmpty() ? 1 : commandMatchScore(entry.descriptor, query);
            if (score > 0) {
                matched.push_back({&entry, score});
            }
        }
        // 排序：得分降序；并列按注册序稳定（NFR-COR-02——§7.4 原文）。
        std::stable_sort(matched.begin(), matched.end(),
                         [](const Scored& a, const Scored& b) { return a.score > b.score; });

        // 近期置顶分组（§7.4"最近使用置顶"——仅保留命中集内的近期命令，
        // 置于结果头部；重复命令不因近期性二次出现）。
        std::vector<CommandView> out;
        std::vector<bool> taken(matched.size(), false);
        for (const CommandId& recent : m_recent) {
            for (std::size_t i = 0; i < matched.size(); ++i) {
                if (!taken[i] && matched[i].entry->descriptor.id == recent) {
                    taken[i] = true;
                    out.push_back(buildView(*matched[i].entry));
                    break;
                }
            }
            if (limit > 0 && out.size() >= limit) {
                return out;
            }
        }
        for (std::size_t i = 0; i < matched.size() && (limit == 0 || out.size() < limit); ++i) {
            if (!taken[i]) {
                out.push_back(buildView(*matched[i].entry));
            }
        }
        return out;  // 调用方对截断负责提示（§7.4"继续输入以缩小范围"）
    }

    std::vector<CommandId> recentUsed() const override { return m_recent; }

    void restoreRecentUsed(std::vector<CommandId> ids) override
    {
        // 收敛到模型不变量：去重保序＋未注册丢弃＋上限 20（历史可能跨版本
        // 失效——不虚构）。
        std::vector<CommandId> cleaned;
        for (CommandId& id : ids) {
            if (findEntry(id) == nullptr) {
                continue;
            }
            if (std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
                cleaned.push_back(std::move(id));
            }
        }
        if (cleaned.size() > kMaxRecent) {
            cleaned.resize(kMaxRecent);
        }
        m_recent = std::move(cleaned);
    }

private:
    /// 近期使用上限（§7.4"最近 20 条"）。
    static constexpr std::size_t kMaxRecent = 20;

    const CommandEntry* findEntry(const CommandId& id) const
    {
        const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                     [&id](const CommandEntry& e) { return e.descriptor.id == id; });
        return it == m_entries.end() ? nullptr : &*it;
    }

    RegistrationResult registerInternal(const CommandDescriptor& descriptor,
                                        CommandHandler handler,
                                        VisibilityPredicate visible,
                                        EnablementPredicate enablement)
    {
        // 运行期注册＝调用方契约违约（§10.3"非法"行）：Debug 断言＋全构型
        // 安全拒绝轨（InvalidDescriptor——不覆盖不静默，值语义上同"描述符
        // 不被接受"）。
        if (m_sealed) {
            Q_ASSERT(false && "运行期 registerCommand（§10.3 非法——装配期已收口）");
            return RegistrationResult::InvalidDescriptor;
        }
        // ①id 句法＋描述符必填面（§7.2 第 1 步）。
        if (!isValidCommandId(descriptor.id) || descriptor.titleKey.empty()
            || descriptor.ownerUnit.empty()) {
            return RegistrationResult::InvalidDescriptor;
        }
        // ①owner 白名单（SA-01 静态白名单的命令侧）。
        const bool ownerOk = std::find(m_deps.ownerWhitelist.begin(),
                                       m_deps.ownerWhitelist.end(), descriptor.ownerUnit)
                          != m_deps.ownerWhitelist.end();
        if (!ownerOk) {
            return RegistrationResult::OwnerNotWhitelisted;
        }
        // ②重复 id（含不同 owner）→ 拒绝＋UI-CMD-DUPLICATE（Dev，装配期
        // ——Dev 不入目录，经 devLog 出线，§6.2/P-DIAG-9）。
        if (findEntry(descriptor.id) != nullptr) {
            emitDev("UI-CMD-DUPLICATE",
                    "命令 id 重复注册被拒绝: '" + descriptor.id + "'（owner '"
                        + descriptor.ownerUnit + "'；既有登记保留——不覆盖不静默，§7.2）");
            return RegistrationResult::DuplicateId;
        }
        // ③写入注册表（registrationOrder＝装配序）。
        CommandEntry entry;
        entry.descriptor = descriptor;
        entry.handler = std::move(handler);
        entry.visible = visible;
        entry.enablement = std::move(enablement);
        m_entries.push_back(std::move(entry));
        return RegistrationResult::Ok;
    }

    CommandAvailability evaluateAvailability(const CommandEntry& entry) const
    {
        CommandAvailability out;
        out.registered = true;  // 存在性（本函数只对已登记项调用——未注册路径走全 false 快照）
        // 可见轴：自定义谓词优先，缺省恒可见（§7.4"禁用＋说明"保留发现性）。
        out.visible = entry.visible != nullptr ? entry.visible(m_context) : true;
        // 使能轴：自定义谓词优先，缺省作用域/只读规则（§7.5/§7.6）。
        std::optional<DisableReason> reason =
            entry.enablement != nullptr ? entry.enablement(m_context)
                                        : defaultEnablement(entry.descriptor, m_context);
        out.enabled = !reason.has_value();
        if (reason.has_value()) {
            out.disableReasonKey = *reason;
        }
        // 只读阻断位（§7.6 的机器观测面——呈现层"只读专属说明"的判据）。
        out.readOnlyBlocked = entry.descriptor.scope == ShellCommandScope::Project
                              && !entry.descriptor.readOnlyAllowed && !m_context.writable;
        return out;
    }

    CommandView buildView(const CommandEntry& entry) const
    {
        const CommandAvailability avail = evaluateAvailability(entry);
        CommandView view;
        view.id = entry.descriptor.id;
        view.titleKey = entry.descriptor.titleKey;
        view.title = commandTransitionalTitle(entry.descriptor.titleKey);
        view.category = entry.descriptor.category;
        view.menuPath = entry.descriptor.menuPath;
        view.keywordKeys = entry.descriptor.keywordKeys;
        view.visible = avail.visible;
        view.enabled = avail.enabled;
        view.disableReasonKey = avail.disableReasonKey;
        view.readOnlyBlocked = avail.readOnlyBlocked;
        view.bindable = entry.descriptor.bindable;  // §7.3 快捷键表前置校验的数据源
        view.registrationOrder = static_cast<std::size_t>(
            &entry - m_entries.data());  // 注册序（装配序——稳定排序锚）
        return view;
    }

    void noteRecent(const CommandId& id)
    {
        const auto it = std::find(m_recent.begin(), m_recent.end(), id);
        if (it != m_recent.end()) {
            m_recent.erase(it);  // 去重（旧位置移除）
        }
        m_recent.insert(m_recent.begin(), id);  // 最近使用置顶
        if (m_recent.size() > kMaxRecent) {
            m_recent.resize(kMaxRecent);  // 裁掉最旧（§7.4 最近 20 条）
        }
    }

    // ---- 诊断出线（P-UI-10 处置纪律：码值权威归 diagnostics 码表）----

    /// Dev 级出线（devLog 唯一通道——Dev 不入目录 §6.2；空 devLog＝显式
    /// 声明的无日志场景，行为不受影响）。
    void emitDev(const std::string& code, const std::string& message)
    {
        if (m_deps.devLog) {
            m_deps.devLog->logDev("diag/ui", code + ": " + message);
        }
    }

    /**
     * @brief 用户级码出线（factory.create＋sink.append——§9.2 唯一创建入口）。
     *
     * subject 装配口径（core §4.8 完整性强制——用户级码必须携带合法
     * ObjectId）：UI-CMD-* 拒绝事实**不绑定任何领域对象**（命令 id 是装配
     * 事实，不是模型对象）——subject 为该目录条目分配独立条目身份
     * （ObjectId::generate()；目录条目为会话态不持久化，不引用/不伪造任何
     * 既有对象，条目定位走 context 文案与 localName）。
     *
     * factory/sink 为空＝无目录测试场景（显式声明）——拒绝行为不受影响，
     * 只降级观测面（返回值轨恒在）。create 抛错（码未注册＝装配序违约）
     * 显性上抛，不吞（fail-fast——装配缺码必须在装配期暴露）。
     */
    void emitUserDiag(const std::string& code, const std::string& sourceInterface,
                      const std::string& context, const std::string& cause,
                      const std::string& action, std::optional<core::ComparativeFields> comparison)
    {
        if (!m_deps.diagFactory || !m_deps.diagSink) {
            return;  // 无目录场景（须显式声明）——拒绝语义已由返回值承载
        }
        core::DiagnosticRecord record = core::DiagnosticRecord::make(
            code, core::ObjectId::generate(), std::nullopt, std::nullopt,
            context, cause, action, std::move(comparison));
        diagnostics::DiagContext diagContext;
        diagContext.sourceUnit = "ui";              // §4.2 sourceUnit 词表含 ui
        diagContext.sourceInterface = sourceInterface;  // 通道 token（≤64）
        // params 留空：九码 paramSchema 统一"[]"（UI-T03 登记面——占位一致
        // 校验要求键集与 schema 完全一致）。
        m_deps.diagSink->append(m_deps.diagFactory->create(record, diagContext));
    }

    CommandRegistryDeps m_deps;          ///< 装配依赖（白名单/诊断面——所有权在外）
    std::vector<CommandEntry> m_entries; ///< 登记集（装配序＝registrationOrder）
    UiContextSnapshot m_context{};       ///< 最近上下文快照（presentContext 维护）
    std::vector<CommandId> m_recent;     ///< 近期使用（最新在前，≤20）
    bool m_sealed = false;               ///< 装配收口位（seal 后注册拒绝）
};

}  // namespace

std::unique_ptr<ICommandRegistry> createCommandRegistry(CommandRegistryDeps deps)
{
    return std::make_unique<CommandRegistryImpl>(std::move(deps));
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
