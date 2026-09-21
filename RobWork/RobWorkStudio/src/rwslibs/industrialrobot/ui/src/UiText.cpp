/**
 * @file   UiText.cpp
 * @brief  工程用语文案体系的实现——内建中文过渡文案表＋键解析＋参数替换
 *         ＋UX-02 内部身份守卫（§3.5/§6.6）。
 *
 * 设计依据：
 *   - units/ui.md §3.5（文案键体系：键冻结、值归 ui 文案资源——P-DIAG-9
 *     交接；"UiText::resolve(TextKey, params) 是唯一出口"）、§6.6（数值带
 *     单位显示、"不适用"占位不伪造 0、对象定位经名称端口局部名）；
 *   - 需求 UX-02（工程用语——零哈希/Schema 版本/内部插件名进用户可见文本）、
 *     ERR-01（"不适用"显式化）、NFR-COR-02（同键同文——确定性呈现）；
 *   - 任务契约 UI-T09.json acceptance 2（UiText 体系＋UX-02 断言）。
 *
 * 背景说明（表的构成与过渡语义）：内建表覆盖本单元阶段 A 已产出的全部
 * 键族——①七阶段标题 stage.<id>.title（§3.5 冻结族，§6.4 七阶段中文名）；
 * ②七态短标签 state.<token>.label（§6.3 词表"中文"列——UI-T04 过渡承载
 * 的值源切换，键与值逐字不变，P-UI-1 词表未改一字）；③core 九态短标签
 * （PM-03/PM-11——同上过渡迁移）；④当前性「无法判定」原因两键（P-UI-2
 * 建议口径原文）；⑤门控数据不可用呈现键（§10.2 错误类型行"门控数据
 * 缺失→Blocked＋'门控数据不可用'"的呈现面）；⑥⑦插件标题与装配状态标签
 * （plugin.<id>.title 八键＋plugin.assembly.<state>.label 三键——UI-T10
 * 关于框清单的呈现值，§11.4/UX-02：token 不进用户文本，用户见中文名）。
 * 值迁移到资源文件时仅替换本表的值源，键不变——调用方与测试零改动
 * （UI-T03 文案表同案）。
 *
 * 线程安全：表为编译期固定的静态只读数组，全部函数可重入。
 */

#include <sdurws/ird/ui/UiText.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 内建中文过渡文案表（键冻结——值过渡承载，见文件头注释）
// ---------------------------------------------------------------------

/// 文案表行（键＋值；值均为中文 UTF-8——§3.5 键值分离的过渡值源）。
struct TextRow
{
    const char* key;    ///< 文案键（§3.5 冻结词形——不可改，改键＝契约变更）
    const char* value;  ///< 中文文本（过渡承载——迁资源文件时仅换值源）
};

/// 键族①：七阶段标题（stage.<id>.title——token 为 StageId 词表小写连字符
/// 形态；中文＝§6.4 七阶段括注名，"轨迹/动力学"含分隔符原文）。
constexpr std::array<TextRow, 7> kStageTitleTable{{
    { "stage.modeling.title",           "建模"         },
    { "stage.requirements.title",       "需求"         },
    { "stage.kinematics.title",         "运动学"       },
    { "stage.trajectory-dynamics.title", "轨迹/动力学" },
    { "stage.selection.title",          "选型"         },
    { "stage.optimization.title",       "优化"         },
    { "stage.reporting.title",          "报告"         },
}};

/// 键族②：七态短标签（state.<token>.label——§6.3 词表 token＋"中文"列；
/// 与 StatusWordModelTest 钉住的过渡值逐字一致——值源切换零漂移）。
constexpr std::array<TextRow, 7> kStatusWordLabelTable{{
    { "state.empty-project.label",     "空项目"   },
    { "state.incomplete.label",        "未完成"   },
    { "state.computing.label",         "计算中"   },
    { "state.results-stale.label",     "结果过期" },
    { "state.data-insufficient.label", "数据不足" },
    { "state.failed.label",            "失败"     },
    { "state.computable.label",        "可计算"   },
}};

/// 键族③：core 九态短标签（state.<token>.label——token 段与 core
/// toToken(TaskState) 词表逐字一致；中文＝§6.3 九态短标签行原文）。
constexpr std::array<TextRow, 9> kTaskStateLabelTable{{
    { "state.queued.label",      "排队中" },
    { "state.preparing.label",   "准备中" },
    { "state.running.label",     "计算中" },
    { "state.paused.label",      "已暂停" },
    { "state.canceling.label",   "取消中" },
    { "state.canceled.label",    "已取消" },
    { "state.completed.label",   "已完成" },
    { "state.failed.label",      "失败"   },
    { "state.interrupted.label", "已中断" },
}};

/// 键族④：当前性「无法判定」原因（P-UI-2 建议口径原文——D-12 同稿）。
constexpr std::array<TextRow, 2> kUnevaluableLabelTable{{
    { "state.currentness.unevaluable.cross-context.label",
      "当前性无法判定（上下文变化）" },
    { "state.currentness.unevaluable.unresolved-dependency.label",
      "当前性无法判定（依赖缺失）" },
}};

/// 键族⑤：阶段呈现辅助键（blocked/failed 展开缺项清单时的计数摘要行——
/// §6.4"失败定位：点击展开'缺项清单'"的汇总呈现；{0}＝缺项计数）与门控
/// 数据不可用呈现键（§10.2 错误类型行原文"门控数据不可用"——门控适配器
/// 在数据源不可得时以 blocked＋本键表达，模型透传）。
constexpr std::array<TextRow, 2> kStagePresentationTable{{
    { "stage.missing.items.count", "缺项 {0} 项" },
    { "stage.gate.unavailable.reason", "门控数据不可用" },
}};

/// 键族⑥：插件标题（plugin.<id>.title——UI-T10 冻结登记，§3.5 键族增量；
/// token 与 units/ui.md §11.1 静态白名单词表逐字一一对应。中文名是关于框
/// 插件清单"标题"列的呈现值——UX-02"界面禁止出现内部插件名"：token 本身
/// 只作键词根，用户只见此处登记的中文名）。
constexpr std::array<TextRow, 8> kPluginTitleTable{{
    { "plugin.modeling.title",      "建模"   },
    { "plugin.requirements.title",  "需求"   },
    { "plugin.kinematics.title",    "运动学" },
    { "plugin.trajectory.title",    "轨迹"   },
    { "plugin.dynamics.title",      "动力学" },
    { "plugin.selection.title",     "选型"   },
    { "plugin.optimization.title",  "优化"   },
    { "plugin.workflow.title",      "工作流" },
}};

/// 键族⑦：插件装配状态标签（plugin.assembly.<state>.label——UI-T10 冻结
/// 登记；三态与 ui::PluginAssemblyStatus 枚举一一对应。关于框清单"装配
/// 状态"列的呈现值——"未装配"是白名单占位常态的如实呈现，不虚构"已
/// 装配"；"装配失败"对应 §11.3 装配失败降级态）。
constexpr std::array<TextRow, 3> kPluginAssemblyLabelTable{{
    { "plugin.assembly.not-assembled.label", "未装配"   },
    { "plugin.assembly.ok.label",            "已装配"   },
    { "plugin.assembly.failed.label",        "装配失败" },
}};

/// 全表拼接视图（查找入口——各键族数组顺序拼接，避免维护一份重复大表）。
/// 注意 state.failed.label 在键族②与③中重复登记（七态与九态同键同值
/// "失败"——§6.3 两表原文即同文），查找取先命中者，值一致故无歧义。
const TextRow* findRow(const TextKey& key)
{
    // 表小（37 行）且调用频率为呈现路径，线性扫描足够（NFR-PERF-01 预算内）；
    // 换哈希表反而引入构建期初始化顺序顾虑——呈现函数必须任何时刻可调用。
    for (const auto& row : kStageTitleTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kStatusWordLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kTaskStateLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kUnevaluableLabelTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kStagePresentationTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kPluginTitleTable) {
        if (key == row.key) { return &row; }
    }
    for (const auto& row : kPluginAssemblyLabelTable) {
        if (key == row.key) { return &row; }
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// 位置占位符解析（{0}、{1}…——resolveText 两参形态的替换引擎）
// ---------------------------------------------------------------------

/**
 * @brief 判定 text 的 [begin, begin+n) 是否为一个合法位置占位符 {k}。
 *
 * 合法条件：首字符 '{'、末字符 '}'、中间为纯十进制数字、无前导零
 * （"0" 合法、"01" 非法——前导零形态必非任何调用点产出，按字面文本
 * 处置而不是占位符，避免歧义替换）。命中时经 outIndex 返回 k。
 */
bool parsePlaceholder(const std::string& text, std::size_t begin, std::size_t n,
                      std::size_t& outIndex)
{
    // 形态前置：最少 "{}"+1 位数字＝3 字符；两端括号。
    if (n < 3 || text[begin] != '{' || text[begin + n - 1] != '}') {
        return false;
    }
    // 中段逐字符校验十进制数字；同时累积索引值（位数超出 size_t 表达
    // 能力的文本必非本体系产出——按字面处置，累积截断无害）。
    std::size_t index = 0;
    for (std::size_t i = begin + 1; i + 1 < begin + n; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        index = index * 10 + static_cast<std::size_t>(c - '0');
    }
    // 前导零拒绝（见函数注释——"01" 不是占位符）。
    if (n > 3 && text[begin + 1] == '0') {
        return false;
    }
    outIndex = index;
    return true;
}

}  // namespace

// =====================================================================
// 键解析（§3.5 唯一出口）
// =====================================================================

std::string resolveText(const TextKey& key)
{
    // 缺键＝调用方契约违约（拼写错误/漏登记）——fail-fast 而不是回显键名
    // 或空串（静默降级会把缺陷渲染给用户，见头文件契约注释）。
    const TextRow* row = findRow(key);
    if (row == nullptr) {
        throw std::invalid_argument("ui/uitext/unknown-key: 未登记文案键 " + key);
    }
    return std::string(row->value);
}

std::string resolveText(const TextKey& key, const std::vector<std::string>& args)
{
    // 先取值文本（缺键fail-fast 同无参形态），再做占位符校验＋替换。
    const TextRow* row = findRow(key);
    if (row == nullptr) {
        throw std::invalid_argument("ui/uitext/unknown-key: 未登记文案键 " + key);
    }
    const std::string value(row->value);

    // ---- 第 1 步：扫值文本，收集占位符引用并校验越界 ----------------
    // 每个 {k} 必须 k < args.size()——引用越界＝文案与调用点参数面不齐
    // （调用方错误，见头文件替换纪律）。
    std::vector<bool> used(args.size(), false);
    bool hasPlaceholder = false;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '{') {
            // 找配对 '}'（占位符不嵌套——值为工程用语短句，形态受控）。
            const std::size_t close = value.find('}', i);
            if (close != std::string::npos) {
                std::size_t index = 0;
                if (parsePlaceholder(value, i, close - i + 1, index)) {
                    hasPlaceholder = true;
                    if (index >= args.size()) {
                        throw std::invalid_argument(
                            "ui/uitext/arg-missing: 键 " + key + " 引用 {"
                            + std::to_string(index) + "} 但调用方仅提供 "
                            + std::to_string(args.size()) + " 个参数");
                    }
                    used[index] = true;
                    i = close + 1;
                    continue;
                }
            }
        }
        ++i;
    }

    // ---- 第 2 步：参数冗余校验（每个传入参数必须在文本中出现）--------
    // 传了没用的参数＝调用方误传（同上——替换纪律对称半区）。
    for (std::size_t i = 0; i < used.size(); ++i) {
        if (!used[i]) {
            throw std::invalid_argument(
                "ui/uitext/arg-unused: 键 " + key + " 未引用参数位置 "
                + std::to_string(i) + "（参数面与文案不齐）");
        }
    }

    // ---- 第 3 步：无占位符即原样返回（args 必为空——已被第 2 步保证）--
    if (!hasPlaceholder) {
        return value;
    }

    // ---- 第 4 步：参数守卫＋顺序拼装 --------------------------------
    // 每个参数先过内部身份守卫（哈希形态拒绝——UX-02 红线在呈现边界
    // 执行；参数是动态文本进入用户可见面的唯一通道）。
    for (const auto& arg : args) {
        ensureNoInternalIdentity(arg);
    }
    std::string out;
    out.reserve(value.size() + 16);
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '{') {
            const std::size_t close = value.find('}', i);
            if (close != std::string::npos) {
                std::size_t index = 0;
                if (parsePlaceholder(value, i, close - i + 1, index)) {
                    // index < args.size() 已由第 1 步保证（同一文本同一解析器）。
                    out += args[index];
                    i = close + 1;
                    continue;
                }
            }
        }
        out += value[i];
        ++i;
    }
    return out;
}

// =====================================================================
// 数值＋单位显示（§6.6）
// =====================================================================

std::string notApplicableText()
{
    // §6.6 原文占位——"不适用"字段显示"不适用"，不伪造 0（ERR-01）。
    return "不适用";
}

// =====================================================================
// 键清单盘点面（值不暴露——P-DIAG-9 键值分离，值唯一出口是 resolveText）
// =====================================================================

std::vector<TextKey> registeredTextKeys()
{
    // 按各族登记序拼接；state.failed.label 双族重复登记只输出一次
    // （集合语义——键清单是"已登记键"的盘点，不是物理行清单）。
    std::vector<TextKey> keys;
    keys.reserve(37);
    for (const auto& row : kStageTitleTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kStatusWordLabelTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kTaskStateLabelTable) {
        // 七态族已登记的同键（state.failed.label——两族同值"失败"）去重。
        const TextKey key(row.key);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            keys.push_back(key);
        }
    }
    for (const auto& row : kUnevaluableLabelTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kStagePresentationTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kPluginTitleTable) {
        keys.emplace_back(row.key);
    }
    for (const auto& row : kPluginAssemblyLabelTable) {
        keys.emplace_back(row.key);
    }
    return keys;
}

// =====================================================================
// UX-02 内部身份守卫
// =====================================================================
void ensureNoInternalIdentity(const std::string& text)
{
    // 滑窗扫描：任意连续 64 个十六进制字符＝SHA-256 摘要呈现形态
    // （CON-05 内容寻址——本产品唯一哈希形态，绝无呈现语义）。
    auto isHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
            || (c >= 'A' && c <= 'F');
    };
    std::size_t run = 0;  // 当前连续十六进制段长度
    for (const char c : text) {
        if (isHex(c)) {
            ++run;
            if (run >= 64) {
                // 命中即 fail-fast（调用方装配/接线错误——把内容身份当
                // 参数传入呈现层）；错误信息不带摘要内容（避免摘要经
                // 异常消息再进入日志呈现面）。
                throw std::invalid_argument(
                    "ui/uitext/internal-identity: 参数含 64 位十六进制摘要"
                    "形态（UX-02 禁止进用户可见文本）");
            }
        } else {
            run = 0;
        }
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
