/**
 * @file   XacroExpand.cpp
 * @brief  Xacro 受控展开语义的实现——pugixml DOM 只读遍历＋单遍流式序列化
 *         （属性/宏/参数声明式替换；护栏复核与护栏码透传；诊断按源文件
 *         行序稳定排序）。
 *
 * 设计依据：
 *   - units/modeling.md §6.5（受控预处理——展开失败/依赖缺失→可定位诊断
 *     （宏名/行列），不产生草稿；产物走 mapUrdf 同一边界）、§9.4.9
 *     （IXacroExpandService 契约）、§9.5（MDL-IMPORT-XACRO-UNRESOLVED——
 *     实现期增登码面，§14.6 v0.7 登记）、§3.4（纯函数确定性）
 *   - units/io.md §6.2（展开边界：递归深度 ≤ IncludeDepth(16)、产物总字节
 *     ≤ TotalBytes、循环→IO-FORMAT-XML-CYCLE、缺文件→IO-RES-MISSING＋
 *     缺失清单、未定义宏/参数→定位诊断、绝不执行任意代码——语言子集按
 *     声明式处理）、§6.5（依赖树——include 边与缺失叶）、§10.5（modeling
 *     不自行读文件）
 *   - 需求 MDL-19、AT-31/V-09（io 码透传；无草稿、无修订、无对象写入）、
 *     NFR-COR-01/02（locale 无关＋同输入同输出同诊断序）、NFR-COR-03
 *     （不静默：子集外构造显式失败不猜测）
 *   - 任务契约 tasks/foundation/WP-13-T06.json acceptance 1~5
 *
 * 实现结构（阅读地图）：
 *   匿名命名空间：行索引（字节偏移→行列，诊断稳定排序键——Import.cpp
 *   同款）/诊断收集与稳定排序出口/XML 转义（text/attribute 两族）/relPath
 *   折叠键规范化（正斜杠＋词法折叠小写——io relPath 约定）/include 图环
 *   检测（防御性 DFS——io 契约无环，此处透传 V-09）/`${}` 替换扫描器
 *   （纯标识符引用；表达式与 $(...) ROS 替换参数显式拒绝——绝不执行）。
 *   主体 Engine 与 expand 六步：①输入契约校验（SourceInconsistent 面）
 *   ②include 边缺失预扫描（IO-RES-MISSING＋缺失清单）③include 图环检测
 *   （IO-FORMAT-XML-CYCLE＋环路径——V-09）④解析入口与 include 字节
 *   （pugixml——O-40 复用 T05 依赖；文档对象随引擎同栈保活）⑤单遍展开
 *   走查（include 拼接/property 定义/macro 登记/宏调用绑定/普通元素原样
 *   传递＋${} 替换；深度与总字节护栏逐点检查）⑥出口装配（诊断稳定排序；
 *   成功＝产物字节＋参数清单＋来源记录，失败＝无产物——无草稿面）。
 *   每步的业务依据见段前注释。
 *
 * 确定性（NFR-COR-01/02）：替换单遍扫描（替换文本不二次展开）；属性绑定
 * 按宏签名序（非调用点属性序）；诊断按（相对键，行，列，产出序）字典序
 * 稳定排序；同输入字节＋同 substitutions→逐字节相等产物与逐条相等诊断。
 * 线程安全：Engine 为栈上局部对象——无共享可变状态，可重入。
 */

#include <sdurws/ird/modeling/XacroExpand.hpp>

#include <sdurws/ird/io/IoDiagnostics.hpp>   // io::errorCodeToken——IO-* 护栏码面唯一来源（禁字符串拼码）
#include <sdurws/ird/modeling/DiagCodes.hpp> // MDL-IMPORT-XACRO-UNRESOLVED 码值常量（唯一书写点）

#include <pugixml.hpp>  // O-40：Xacro DOM 解析（复用 WP-13-T05 已登记依赖——vcpkg 经典模式，
                        // modeling PRIVATE）。有界性由 io BudgetGuard 前置保证：进入本单元的
                        // 字节已经 io 预算入账（SingleFileBytes/TotalBytes），DOM 规模与输入
                        // 字节同阶；pugixml 默认不解析外部实体、不执行任何文档内设施（无
                        // XXE/代码执行通道——SA-14/§9.4.9"绝不执行任意代码"）。

#include <algorithm>
#include <cctype>        // std::tolower——relPath 折叠小写（io relPath 约定）
#include <cstdint>
#include <functional>    // std::function——include 环检测 DFS 闭包
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::modeling {

// =====================================================================
// 错误码 token 表（switch 全枚举、无 default——新增值漏登记编译告警；
// importErrorCodeToken 同款防线）
// =====================================================================

std::string_view xacroExpandErrorCodeToken(XacroExpandErrorCode code) noexcept
{
    switch (code) {
    case XacroExpandErrorCode::UndefinedSymbol:         return "UndefinedSymbol";
    case XacroExpandErrorCode::DependencyMissing:       return "DependencyMissing";
    case XacroExpandErrorCode::IncludeCycle:            return "IncludeCycle";
    case XacroExpandErrorCode::ExpansionBudgetExceeded: return "ExpansionBudgetExceeded";
    case XacroExpandErrorCode::SourceInconsistent:      return "SourceInconsistent";
    }
    return "unknown";  // 防御（全枚举覆盖后不可达——io/modeling token 表同款口径）
}

namespace {

// =====================================================================
// 源文本行索引（诊断"按源文件行序稳定排序"的定位基础——Import.cpp
// LineIndex 同款：pugixml 只提供字节偏移（offset_debug），不提供行列）
// =====================================================================

/**
 * @brief 源文本行索引（字节偏移→（行,列）——列按字节计：稳定性优先于
 *        显示宽度，跨平台一致；Import.cpp 同款取舍）。
 */
class LineIndex {
public:
    explicit LineIndex(std::string_view text)
    {
        lineStarts_.push_back(0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') { lineStarts_.push_back(i + 1); }
        }
    }

    /// 偏移→（行,列），各 1 起；偏移无效（-1）→（0,0）＝无定位。
    std::pair<std::uint32_t, std::uint32_t> lineColumn(std::size_t offset) const
    {
        if (offset == static_cast<std::size_t>(-1)) { return {0, 0}; }
        const auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
        if (it == lineStarts_.begin()) { return {0, 0}; }
        const std::size_t lineNo = static_cast<std::size_t>(it - lineStarts_.begin());
        const std::size_t col = offset - lineStarts_[lineNo - 1] + 1;
        return {static_cast<std::uint32_t>(lineNo), static_cast<std::uint32_t>(col)};
    }

private:
    std::vector<std::size_t> lineStarts_;  ///< 各行首字节偏移（升序；0 为首行起点）
};

// =====================================================================
// 诊断收集（产出＋源位置＋产出序——出口统一按行序稳定排序；Import.cpp
// DiagEntry/flushDiags 同款结构，展开服务的码面与定位语义独立成套）
// =====================================================================

/**
 * @brief 诊断中间条目（DiagnosticRecord＋排序键）。
 *
 * 码面唯一来源：MDL-IMPORT-XACRO-UNRESOLVED 取 DiagCodes.hpp 常量、IO-*
 * 取 io::errorCodeToken——禁字符串拼码（§9.5 尾段）。结构化参数经
 * DiagnosticRecord 的 cause 文本以 "key=value" 形态承载（DiagnosticRecord
 * 无独立参数表——core §4.8 字段面；键名与描述符 paramSchema 对齐：
 * item-kind/symbol；行列已由 context 定位段承载）。
 */
struct DiagEntry {
    core::DiagnosticRecord record;  ///< 已构造诊断记录（make 工厂产出——C-3 校验）
    std::string relFile;            ///< 排序键：源相对键（空＝无定位，排最前）
    std::uint32_t line = 0;         ///< 排序键：行（0＝无定位）
    std::uint32_t column = 0;       ///< 排序键：列（0＝无定位）
    std::size_t seq = 0;            ///< 排序键：同位置产出序（稳定化兜底）
};

/**
 * @brief 诊断稳定排序出口（（相对键，行，列，产出序）字典序——NFR-COR-02
 *        "诊断顺序稳定"）后一次性追加到调用方容器（追加不清空——输出参数
 *        契约）。成功/失败出口共用（失败面诊断同样有序）。
 */
void flushDiags(std::vector<DiagEntry>& entries,
                std::vector<core::DiagnosticRecord>& diags)
{
    std::sort(entries.begin(), entries.end(),
              [](const DiagEntry& a, const DiagEntry& b) {
                  if (a.relFile != b.relFile) { return a.relFile < b.relFile; }
                  if (a.line != b.line) { return a.line < b.line; }
                  if (a.column != b.column) { return a.column < b.column; }
                  return a.seq < b.seq;
              });
    for (const DiagEntry& entry : entries) {
        diags.push_back(entry.record);
    }
}

// =====================================================================
// XML 转义（单遍流式序列化的良构性保证——产物必须可被 mapUrdf 再次解析）
// =====================================================================

/**
 * @brief 元素文本转义（& < > 三字符——XML 1.0 必需集；实体解码发生在
 *        pugixml 解析侧（parse_escapes 默认开），此处输出侧再转义，标准
 *        实体幂等）。纯函数；确定性。
 */
std::string escapeXmlText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        default:   out += ch;       break;
        }
    }
    return out;
}

/**
 * @brief 属性值转义（文本必需集外加引号——属性值以双引号包裹）。纯函数；
 *        确定性。
 */
std::string escapeXmlAttribute(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        default:   out += ch;       break;
        }
    }
    return out;
}

// =====================================================================
// relPath 折叠键规范化（include filename→依赖树相对键——io relPath 约定：
// 正斜杠、词法折叠小写；"./"/"../"按词法消解，空段折叠）
// =====================================================================

/**
 * @brief include filename 相对所在文件目录解析为依赖树折叠键。
 *
 * @param filenameRaw  [in] filename 属性替换后的字面值
 * @param includingKey [in] 所在文件的依赖树相对键（目录取其父路径）
 * @return 折叠键（正斜杠＋小写；不判存在性——存在性由调用方在树节点集/
 *         字节表中查证）
 *
 * 纯函数；确定性（词法操作——不触文件系统，SA-14）。
 */
std::string normalizeIncludeKey(const std::string& filenameRaw,
                                const std::string& includingKey)
{
    // 基目录：所在文件相对键的父路径（无斜杠＝入口在根——基为空）。
    std::string base;
    const auto slash = includingKey.rfind('/');
    if (slash != std::string::npos) { base = includingKey.substr(0, slash + 1); }

    // 拼接后词法消解（"."跳过、".."弹层；反斜杠统一为正斜杠；折叠小写
    // ——与 io 树节点折叠键同一形态，查重键一致）。
    std::vector<std::string> segments;
    std::string joined = base + filenameRaw;
    for (char& ch : joined) {
        if (ch == '\\') { ch = '/'; }
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    std::size_t i = 0;
    while (i < joined.size()) {
        if (joined[i] == '/') { ++i; continue; }             // 空段（根/双斜杠）——跳过
        const std::size_t start = i;
        while (i < joined.size() && joined[i] != '/') { ++i; }
        const std::string seg = joined.substr(start, i - start);
        if (seg == ".") { continue; }                        // 当前目录——无效果
        if (seg == "..") {
            if (!segments.empty()) { segments.pop_back(); }  // 上一级——弹一层
            continue;
        }
        segments.push_back(seg);
    }
    std::string out;
    for (const std::string& seg : segments) {
        if (!out.empty()) { out += '/'; }
        out += seg;
    }
    return out;
}

// =====================================================================
// ${} 标识符判别（受控子集：${} 内只允许纯标识符引用）
// =====================================================================

/// 纯标识符判定：[A-Za-z_][A-Za-z0-9_]*（Xacro 属性命名惯例；表达式/
/// 函数调用/算式一律不在其内——显式拒绝，绝不求值）。
bool isPlainIdentifier(std::string_view text)
{
    if (text.empty()) { return false; }
    const auto isLead = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
    };
    const auto isTail = [&isLead](char ch) {
        return isLead(ch) || (ch >= '0' && ch <= '9');
    };
    if (!isLead(text.front())) { return false; }
    for (std::size_t i = 1; i < text.size(); ++i) {
        if (!isTail(text[i])) { return false; }
    }
    return true;
}

// =====================================================================
// 展开引擎（栈上局部对象——无共享可变状态，可重入，卡 §3.4）
// =====================================================================

/// 宏参数形态（params 属性按空白切分："name" 或 "name:=default"——默认值
/// 为字面文本，不做 ${} 解析：受控子集边界，头文件契约注）。
struct MacroParam {
    std::string name;          ///< 参数名
    std::string defaultValue;  ///< 默认值字面（仅 hasDefault 时有效）
    bool hasDefault = false;   ///< 是否带 ":=" 默认值
};

/// 宏定义登记（名字→定义处：文件下标＋定义节点＋签名；名字唯一——重定义
/// 失败；登记序＝走查文档序）。
struct MacroDef {
    std::size_t fileIdx = 0;         ///< 定义所在源文件（files 下标——体走查/定位用）
    pugi::xml_node node;             ///< xacro:macro 定义节点（体为其子节点）
    ImportSourceSpan defSpan;        ///< 定义处定位（诊断面）
    std::vector<MacroParam> params;  ///< 形参签名（params 属性原文序——绑定序依据）
};

/// 已解析源文件上下文（文本副本供 pugixml 解析与行索引共用；pugixml 文档
/// 对象在 Engine.docs 中与引擎同栈保活——解析缓冲必须活过文档，同栈无
/// 悬空面）。
struct SourceFile {
    std::string relKey;   ///< 依赖树相对键（定位面；入口＝树 rootRel）
    std::string text;     ///< 源文本副本（字节→char；解析缓冲所有权）
    LineIndex lines;      ///< 行列索引（offset_debug→行列）
    SourceFile(std::string key, std::string content)
        : relKey(std::move(key)), text(std::move(content)), lines(text) {}
    // 移动合法（行索引随 text 语义整体迁移——vector 扩容按移动）；拷贝
    // 禁止（text 与 lines 的行表须同源，拷贝易造出失配副本）。
    SourceFile(SourceFile&&) = default;
    SourceFile& operator=(SourceFile&&) = default;
    SourceFile(const SourceFile&) = delete;
    SourceFile& operator=(const SourceFile&) = delete;
};

/**
 * @brief 展开引擎（单次 expand 调用的全部可变状态——栈上对象，调用结束
 *        即销毁；两次调用零共享，可重入）。
 *
 * 失败模型：单失败出口（first-fail）——任何失败置 failed＋err 并沿调用
 * 链回退；诊断已在产生点入列，出口统一稳定排序。这与"识别＋报告"语义
 * 一致：失败前的参数收集部分保留（向导页可展示已见参数面）。
 */
struct Engine {
    const ValidatedXacroSource& src;    ///< 输入（只读引用——调用方所有）
    const XacroSubstitutionMap& subs;   ///< 展开环境（只读引用）
    std::vector<SourceFile> files;      ///< 已解析源文件（[0]＝入口）
    std::vector<pugi::xml_document> docs;     ///< pugixml 文档（与 files 一一对应——保活）
    std::vector<pugi::xml_node> docRoots;     ///< 各文档根元素（include 拼接取其子）
    std::map<std::string, std::size_t> fileByKey;  ///< 折叠键→文件下标
    std::map<std::string, MacroDef> macros;        ///< 宏表（名字→定义；重定义失败）
    std::vector<DiagEntry> diagEntries; ///< 诊断中间列（出口稳定排序）
    std::size_t diagSeq = 0;            ///< 产出序（同位置稳定化兜底）
    std::string out;                    ///< 展开产物缓冲（流式写入）
    bool failed = false;                ///< 单失败出口标志
    XacroExpandError err;               ///< 首个失败（最严重阻断条件）
    std::vector<XacroParameterItem> parameters; ///< 参数列表展示数据（定义序）
    std::vector<std::string> includeStack;      ///< include 拼接栈（环检测防御面）
    /// 作用域层栈（back＝最内层；层内后写覆盖先写；解析序见 resolveSymbol）
    std::vector<std::vector<std::pair<std::string, std::string>>> layers;

    /// 置失败（首个失败生效——后续失败不再覆盖 err；调用链借此回退）。
    void fail(XacroExpandErrorCode code,
              std::vector<std::pair<std::string, std::string>> params,
              std::string detail)
    {
        if (failed) { return; }
        failed = true;
        XacroExpandError e;
        e.code = code;
        e.params = std::move(params);
        e.detail = std::move(detail);
        err = std::move(e);
    }

    /// 追加一条语义面诊断（MDL-IMPORT-XACRO-UNRESOLVED——宏名/参数名＋
    /// 源行列定位；结构化键值入 cause，定位入 context——T05 pushImportDiag
    /// 同款形态）。
    void pushUnresolvedDiag(const std::string& itemKind, const std::string& symbol,
                            const std::string& cause, const std::string& action,
                            const ImportSourceSpan& span)
    {
        DiagEntry entry;
        entry.record = core::DiagnosticRecord::make(
            std::string(kMdlImportXacroUnresolved), std::nullopt, std::nullopt,
            std::nullopt,
            "xacro-expand@" + span.relFile + ":" + std::to_string(span.line) + ":"
                + std::to_string(span.column),
            cause + "（item-kind=" + itemKind + "; symbol=" + symbol + "）",
            action);
        entry.relFile = span.relFile;
        entry.line = span.line;
        entry.column = span.column;
        entry.seq = diagSeq++;
        diagEntries.push_back(std::move(entry));
    }

    /// 追加一条透传诊断（IO-* 护栏码——码面唯一来源 io::errorCodeToken；
    /// 透传语义：码值/含义保持 io 注册面，本单元只承载定位与上下文）。
    void pushIoPassthroughDiag(std::string_view ioCode, const std::string& cause,
                               const std::string& action, const ImportSourceSpan& span)
    {
        DiagEntry entry;
        entry.record = core::DiagnosticRecord::make(
            std::string(ioCode), std::nullopt, std::nullopt, std::nullopt,
            "xacro-expand@" + span.relFile + ":" + std::to_string(span.line) + ":"
                + std::to_string(span.column),
            cause, action);
        entry.relFile = span.relFile;
        entry.line = span.line;
        entry.column = span.column;
        entry.seq = diagSeq++;
        diagEntries.push_back(std::move(entry));
    }

    /// 节点定位面（文件下标＋pugixml 字节偏移→ImportSourceSpan）。
    ImportSourceSpan spanOf(std::size_t fileIdx, std::ptrdiff_t offset) const
    {
        const auto lc = files[fileIdx].lines.lineColumn(static_cast<std::size_t>(offset));
        return ImportSourceSpan{files[fileIdx].relKey, lc.first, lc.second};
    }

    /// 符号解析（作用域链：展开环境 substitutions（首匹配）→层栈由内向外
    /// （层内后写覆盖先写）→未命中返回 nullptr）。
    const std::string* resolveSymbol(const std::string& name) const
    {
        for (const auto& sub : subs) {
            if (sub.first == name) { return &sub.second; }
        }
        for (auto layerIt = layers.rbegin(); layerIt != layers.rend(); ++layerIt) {
            for (auto entryIt = layerIt->rbegin(); entryIt != layerIt->rend(); ++entryIt) {
                if (entryIt->first == name) { return &entryIt->second; }
            }
        }
        return nullptr;
    }
};

// =====================================================================
// ${} 替换扫描器（单遍——替换结果不再扫描，防注入递归；失败即置引擎
// 失败并回退）
// =====================================================================

/**
 * @brief 对属性值/文本节点做 ${name} 替换（受控子集核心语义）。
 *
 * 语法与失败面：
 *   - "${标识符}"→作用域解析（resolveSymbol）；未命中→undefined-param
 *     定位诊断（符号名＋宿主元素行列）并失败（MDL-19"未定义参数→行列
 *     定位"）。
 *   - "${非标识符}"（表达式/算式/函数调用）→unsupported-expression 失败
 *     ——绝不求值（§9.4.9"绝不执行任意代码"；NFR-COR-03 不静默猜测）。
 *   - "$(...)"（ROS 替换参数——find/env 等会触环境/文件系统）→
 *     unsupported-construct 失败（显式拒绝执行）。
 *   - "${" 无配对 "}"→unsupported-construct 失败（语法边界）。
 *   - 其余字符原样拷贝。
 *
 * @param engine     [in,out] 引擎（诊断入列；失败置位）
 * @param fileIdx    [in] 宿主文件（定位）
 * @param text       [in] 原文（pugixml 已解码实体——替换作用于解码后值）
 * @param hostOffset [in] 宿主元素字节偏移（pugixml 属性无独立偏移——属性
 *                   内定位取宿主元素行列，头文件契约注）
 * @param out        [out] 替换结果（仅返回 true 时有效）
 * @return true＝成功；false＝失败（诊断已记，engine.failed 已置）
 */
bool substituteText(Engine& engine, std::size_t fileIdx, std::string_view text,
                    std::ptrdiff_t hostOffset, std::string* out)
{
    const ImportSourceSpan hostSpan = engine.spanOf(fileIdx, hostOffset);
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        if (text[i] != '$') {
            out->push_back(text[i]);
            ++i;
            continue;
        }
        // '$' 起始：区分 ${引用}、$(ROS 替换参数)、字面 '$'。
        if (i + 1 >= n) {
            out->push_back('$');  // 尾随孤立 '$'——无替换语义，原样保留
            break;
        }
        if (text[i + 1] == '{') {
            // 找配对 '}'（计数嵌套——嵌套内容在标识符判别处被拒，计数只
            // 为正确圈定边界）。
            std::size_t depth = 1;
            std::size_t j = i + 2;
            while (j < n && depth > 0) {
                if (text[j] == '{') { ++depth; }
                else if (text[j] == '}') { --depth; }
                ++j;
            }
            if (depth != 0) {
                const std::string raw(text.substr(i));
                engine.pushUnresolvedDiag(
                    "unsupported-construct", raw,
                    "${ 替换未闭合——受控子集要求完整 ${标识符} 形态",
                    "改为 ${参数名} 形态或在展开环境提供该参数", hostSpan);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "unsupported-construct"},
                             {"symbol", raw}},
                            "未闭合的 ${ 替换——定位见诊断");
                return false;
            }
            // 圈定内容并去首尾空白（"${ name }" 与 "${name}" 同义——字面
            // 空白不参与符号名）。
            std::string_view inner = text.substr(i + 2, j - i - 3);
            while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t'
                                      || inner.front() == '\r' || inner.front() == '\n')) {
                inner.remove_prefix(1);
            }
            while (!inner.empty() && (inner.back() == ' ' || inner.back() == '\t'
                                      || inner.back() == '\r' || inner.back() == '\n')) {
                inner.remove_suffix(1);
            }
            if (!isPlainIdentifier(inner)) {
                // 表达式/算式/函数调用——明确不求值（绝不执行任意代码；
                // ${a+b}/${foo()} 等一律定位失败，NFR-COR-03 不静默）。
                engine.pushUnresolvedDiag(
                    "unsupported-expression", std::string(inner),
                    "${} 内为表达式形态——受控子集只支持纯标识符引用，"
                    "不做任何表达式求值",
                    "改为纯标识符引用，或在展开环境预计算后以字面代入",
                    hostSpan);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "unsupported-expression"},
                             {"symbol", std::string(inner.substr(0, 64))}},
                            "${} 表达式形态被拒绝（绝不执行任意代码）——定位见诊断");
                return false;
            }
            const std::string* value = engine.resolveSymbol(std::string(inner));
            if (value == nullptr) {
                // 未定义参数——行列定位诊断（MDL-19/AT-31 验收面）。
                engine.pushUnresolvedDiag(
                    "undefined-param", std::string(inner),
                    "引用了未定义参数——展开环境（substitutions）与文档属性中均无此名",
                    "在展开环境提供该参数，或先以 xacro:property 定义", hostSpan);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "undefined-param"},
                             {"symbol", std::string(inner)}},
                            "未定义参数 '" + std::string(inner) + "'——定位见诊断");
                return false;
            }
            out->append(*value);  // 替换值直接拼入——不二次扫描（防注入递归）
            i = j;                // 越过整个 ${...}
            continue;
        }
        if (text[i + 1] == '(') {
            // ROS 替换参数（$(find pkg)/$(env ...) 等）——会触文件系统/
            // 环境变量，显式拒绝（SA-14/绝不执行）。
            std::size_t j = i + 2;
            while (j < n && text[j] != ')') { ++j; }
            const std::string raw(j < n ? std::string(text.substr(i, j - i + 1))
                                        : std::string(text.substr(i)));
            engine.pushUnresolvedDiag(
                "unsupported-construct", raw,
                "$( ROS 替换参数——受控子集不支持，绝不触环境/文件系统",
                "改为 xacro:property 或展开环境代入（离线依赖交付）", hostSpan);
            engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                        {{"item-kind", "unsupported-construct"},
                         {"symbol", raw.substr(0, 64)}},
                        "$( ROS 替换参数被拒绝（绝不执行）——定位见诊断");
            return false;
        }
        out->push_back('$');  // '$' 后非 '{'/'(' ——字面保留
        ++i;
    }
    return true;
}

// =====================================================================
// 展开走查（单遍流式序列化——源 DOM 只读，产物写 out；深度/总字节护栏
// 逐点检查；返回 false＝已失败，调用链逐层回退）
// =====================================================================

// 前置声明（宏调用与 include 拼接互相嵌套——互递归）。
bool emitElement(Engine& engine, std::size_t fileIdx, const pugi::xml_node& node,
                 std::uint32_t depth);

/**
 * @brief 发出元素的全部子节点（元素/文本分派——文档序＝确定性）。
 *
 * pugixml 默认解析不含注释/PI（parse_default 未开 parse_comments/
 * parse_pi）——源注释天然不进入产物（头文件契约注）。子节点内联空白
 * （缩进）按 pcdata 原样替换后输出——产物结构与源一致。
 */
bool emitChildren(Engine& engine, std::size_t fileIdx, const pugi::xml_node& node,
                  std::uint32_t depth)
{
    for (const pugi::xml_node& child : node.children()) {
        if (engine.failed) { return false; }
        const pugi::xml_node_type type = child.type();
        if (type == pugi::node_element) {
            if (!emitElement(engine, fileIdx, child, depth)) { return false; }
        } else if (type == pugi::node_pcdata || type == pugi::node_cdata) {
            // 文本节点：${} 替换后转义输出（CDATA 按纯文本并入——声明式
            // 子集不保留 CDATA 形态，语义等价）。
            std::string replaced;
            if (!substituteText(engine, fileIdx, child.value(), child.offset_debug(),
                                &replaced)) {
                return false;
            }
            engine.out += escapeXmlText(replaced);
        }
        // node_declaration/node_pi/node_doctype：跳过（声明只发入口文档
        // 顶层；PI 不解析；DOCTYPE 声明式子集不承载）。
    }
    return !engine.failed;
}

/**
 * @brief 发出一个元素（普通元素原样传递＋${} 替换；xacro: 前缀按受控
 *        子集分派：include 拼接/property 定义/macro 登记/宏调用展开/
 *        保留字拒绝）。
 *
 * @param depth [in] 当前展开递归深度（include 拼接与宏调用各计一层；
 *        普通 XML 嵌套不计——"展开递归深度"的 §6.2 语义；1 起）
 * @return true＝成功；false＝失败（诊断已记，engine.failed 已置）
 */
bool emitElement(Engine& engine, std::size_t fileIdx, const pugi::xml_node& node,
                 std::uint32_t depth)
{
    if (engine.failed) { return false; }

    const std::string name = node.name();
    const ImportSourceSpan span = engine.spanOf(fileIdx, node.offset_debug());

    // 护栏检查：展开递归深度 ≤ kXacroMaxExpansionDepth（io.md §6.2 边界
    // ——include 与宏调用共用深度计；超限→IO-SEC-BUDGET-INCLUDE 透传，
    // 比较三要素 actual/limit）。
    if (depth > kXacroMaxExpansionDepth) {
        engine.pushIoPassthroughDiag(
            io::errorCodeToken(io::IoErrorCode::SecBudgetInclude),
            "展开递归深度超限（actual=" + std::to_string(depth)
                + "; limit=" + std::to_string(kXacroMaxExpansionDepth)
                + "; unit=depth）——include 拼接/宏调用互相嵌套过深",
            "压平 include 层级或改写宏递归为显式展开", span);
        engine.fail(XacroExpandErrorCode::ExpansionBudgetExceeded,
                    {{"actual", std::to_string(depth)},
                     {"limit", std::to_string(kXacroMaxExpansionDepth)}},
                    "展开递归深度超限——io.md §6.2 边界（IncludeDepth=16）");
        return false;
    }

    // ---- xacro: 前缀面（受控子集分派）----
    if (name.rfind("xacro:", 0) == 0) {
        const std::string local = name.substr(6);

        // ① include：拼接目标文件根元素的子元素（io 树已枚举 include 边；
        // 字节来自 includeBytes——本单元零文件访问，SA-14）。filename 属性
        // 词表＝io dependencyTree include 边同源（filename|file|href，io.md
        // §9.6 边抽取行）。
        if (local == "include") {
            pugi::xml_attribute useAttr = node.attribute("filename");
            if (!useAttr) { useAttr = node.attribute("file"); }
            if (!useAttr) { useAttr = node.attribute("href"); }
            if (!useAttr) {
                engine.pushUnresolvedDiag(
                    "missing-attribute", "filename",
                    "xacro:include 缺 filename 属性——受控子集要求显式目标",
                    "补 filename 属性（相对所在文件目录）", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "missing-attribute"},
                             {"symbol", "filename"}},
                            "xacro:include 缺 filename——定位见诊断");
                return false;
            }
            std::string raw;
            if (!substituteText(engine, fileIdx, useAttr.value(), node.offset_debug(),
                                &raw)) {
                return false;
            }
            const std::string key = normalizeIncludeKey(raw, engine.files[fileIdx].relKey);
            const auto fileIt = engine.fileByKey.find(key);
            if (fileIt == engine.fileByKey.end()) {
                // 走查期防御面（预扫描已在边级兜底——此处带元素行列定位）。
                engine.pushIoPassthroughDiag(
                    io::errorCodeToken(io::IoErrorCode::ResMissing),
                    "include 目标不在已验证输入中（missing-file=" + key
                        + "）——离线依赖必须随 io 产物交付",
                    "把被包含文件置于导入根内并重跑 io 读取", span);
                engine.fail(XacroExpandErrorCode::DependencyMissing,
                            {{"missing-file", key}},
                            "include 目标缺失 '" + key + "'——IO-RES-MISSING");
                return false;
            }
            // 环检测（纵深防御：io 契约无环＋图预扫描已查；此处拦拼接栈
            // 自环——V-09 透传 IO-FORMAT-XML-CYCLE）。
            if (std::find(engine.includeStack.begin(), engine.includeStack.end(), key)
                != engine.includeStack.end()) {
                std::string cyclePath;
                for (const std::string& k : engine.includeStack) {
                    cyclePath += k + " -> ";
                }
                cyclePath += key;
                engine.pushIoPassthroughDiag(
                    io::errorCodeToken(io::IoErrorCode::FormatXmlCycle),
                    "include 拼接栈检出环（cycle-path=" + cyclePath + "）",
                    "打断 include 环（io.md §6.2 循环引用拒绝）", span);
                engine.fail(XacroExpandErrorCode::IncludeCycle,
                            {{"cycle-path", cyclePath}},
                            "include 环（拼接栈）——IO-FORMAT-XML-CYCLE");
                return false;
            }
            // 拼接：目标文档根元素的子元素按文档序发出（xacro include 语义
            // ——拼接根的子节点，不复制根元素本身）。
            engine.includeStack.push_back(key);
            const bool ok = emitChildren(engine, fileIt->second,
                                         engine.docRoots[fileIt->second], depth + 1);
            engine.includeStack.pop_back();
            return ok && !engine.failed;
        }

        // ② property：定义进当前作用域层（全局层或宏体层——定义即生效，
        // 文档序后者覆盖前者）；同步入参数列表展示数据（MDL-19"参数列表"）。
        if (local == "property") {
            const pugi::xml_attribute nameAttr = node.attribute("name");
            const pugi::xml_attribute valueAttr = node.attribute("value");
            if (!nameAttr || !valueAttr) {
                engine.pushUnresolvedDiag(
                    "missing-attribute", "name/value",
                    "xacro:property 缺 name 或 value 属性——受控子集要求完整定义",
                    "补全 name 与 value 属性", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "missing-attribute"},
                             {"symbol", "property"}},
                            "xacro:property 缺属性——定位见诊断");
                return false;
            }
            std::string resolved;
            if (!substituteText(engine, fileIdx, valueAttr.value(), node.offset_debug(),
                                &resolved)) {
                return false;
            }
            engine.layers.back().emplace_back(nameAttr.value(), resolved);
            engine.parameters.push_back(
                XacroParameterItem{nameAttr.value(), resolved, "property", span});
            return true;  // 定义节点不进产物
        }

        // ③ macro：登记宏表（体节点延迟展开——调用时走查）；重定义失败
        // （受控子集不含作用域化宏——同名单一权威定义）。
        if (local == "macro") {
            const pugi::xml_attribute nameAttr = node.attribute("name");
            if (!nameAttr) {
                engine.pushUnresolvedDiag(
                    "missing-attribute", "name",
                    "xacro:macro 缺 name 属性——无法登记宏定义",
                    "补 name 属性（调用名）", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "missing-attribute"}, {"symbol", "macro"}},
                            "xacro:macro 缺 name——定位见诊断");
                return false;
            }
            if (engine.macros.find(nameAttr.value()) != engine.macros.end()) {
                engine.pushUnresolvedDiag(
                    "macro-redefinition", nameAttr.value(),
                    "宏重定义——受控子集要求同名宏唯一",
                    "删除或改名重复的 xacro:macro 定义", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "macro-redefinition"},
                             {"symbol", nameAttr.value()}},
                            "宏 '" + std::string(nameAttr.value()) + "' 重定义——定位见诊断");
                return false;
            }
            MacroDef def;
            def.fileIdx = fileIdx;
            def.node = node;
            def.defSpan = span;
            // params 属性：空白分隔 token；"name:=default" 携带字面默认值
            // （默认值不做 ${} 解析——受控子集边界，头文件契约注）。
            const std::string paramsText = node.attribute("params").value();
            std::size_t i = 0;
            while (i < paramsText.size()) {
                while (i < paramsText.size()
                       && (paramsText[i] == ' ' || paramsText[i] == '\t')) {
                    ++i;
                }
                if (i >= paramsText.size()) { break; }
                const std::size_t start = i;
                while (i < paramsText.size() && paramsText[i] != ' '
                       && paramsText[i] != '\t') {
                    ++i;
                }
                const std::string token = paramsText.substr(start, i - start);
                MacroParam p;
                const auto assign = token.find(":=");
                if (assign != std::string::npos) {
                    p.name = token.substr(0, assign);
                    p.defaultValue = token.substr(assign + 2);
                    p.hasDefault = true;
                } else {
                    p.name = token;
                }
                def.params.push_back(std::move(p));
            }
            engine.macros[nameAttr.value()] = std::move(def);
            return true;  // 定义节点不进产物
        }

        // ④ 保留字（条件/块插入——受控子集显式不支持，拒绝而非猜测）。
        if (local == "if" || local == "unless" || local == "insert_block") {
            engine.pushUnresolvedDiag(
                "unsupported-construct", local,
                "xacro:" + local + " 为受控子集外构造（条件/块插入）——显式拒绝",
                "改写为无条件展开形式（子集：include/property/macro/调用）", span);
            engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                        {{"item-kind", "unsupported-construct"}, {"symbol", local}},
                        "xacro:" + local + " 不受支持——定位见诊断");
            return false;
        }

        // ⑤ 宏调用：解析调用点属性（当前作用域）→按签名绑定（缺参/多参
        // 失败）→推作用域层递归展开宏体（深度＋1——展开递归计层）。
        const auto macroIt = engine.macros.find(local);
        if (macroIt == engine.macros.end()) {
            // 未定义宏——行列定位诊断（MDL-19/AT-31 验收面：宏名＋源行列）。
            engine.pushUnresolvedDiag(
                "undefined-macro", local,
                "调用了未定义宏——宏表（含 include 拼接已登记者）中无此名",
                "先定义 xacro:macro 或修正调用名（include 必须先于调用）",
                span);
            engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                        {{"item-kind", "undefined-macro"}, {"symbol", local}},
                        "未定义宏 '" + local + "'——定位见诊断");
            return false;
        }
        const MacroDef& macro = macroIt->second;
        // 调用点属性解析（当前作用域替换；文档序——pugixml 保序；重复属性
        // 名取首个——XML 非法输入的确定性取舍）。
        std::vector<std::pair<std::string, std::string>> provided;
        for (const pugi::xml_attribute& attr : node.attributes()) {
            std::string value;
            if (!substituteText(engine, fileIdx, attr.value(), node.offset_debug(),
                                &value)) {
                return false;
            }
            const bool duplicated = std::any_of(
                provided.begin(), provided.end(),
                [&attr](const auto& kv) { return kv.first == attr.name(); });
            if (!duplicated) { provided.emplace_back(attr.name(), std::move(value)); }
        }
        // 绑定：签名序遍历形参——调用点有值用之，否则默认值，再缺即失败
        // （缺参＝"参数未定义"定位面之一：参数名＋调用点行列）。
        std::vector<std::pair<std::string, std::string>> bindings;
        for (const MacroParam& p : macro.params) {
            const auto it = std::find_if(provided.begin(), provided.end(),
                                         [&p](const auto& kv) {
                                             return kv.first == p.name;
                                         });
            if (it != provided.end()) {
                bindings.emplace_back(p.name, it->second);
            } else if (p.hasDefault) {
                bindings.emplace_back(p.name, p.defaultValue);
            } else {
                engine.pushUnresolvedDiag(
                    "missing-param", p.name,
                    "宏调用缺必需参数（宏 '" + local + "' 的形参无默认值）",
                    "补传该属性，或为形参声明 name:=default 默认值", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "missing-param"}, {"symbol", p.name}},
                            "宏 '" + local + "' 缺参数 '" + p.name + "'——定位见诊断");
                return false;
            }
        }
        // 多余属性：调用点给了签名外属性——显式失败（受控子集不含"透传
        // 万能属性"——静默丢弃违反 NFR-COR-03 不静默）。
        for (const auto& kv : provided) {
            const bool claimed = std::any_of(
                macro.params.begin(), macro.params.end(),
                [&kv](const MacroParam& p) { return p.name == kv.first; });
            if (!claimed) {
                engine.pushUnresolvedDiag(
                    "unknown-attr", kv.first,
                    "宏调用传入了签名外属性（宏 '" + local + "' 未声明）",
                    "在 xacro:macro params 中声明该参数，或移除多余属性", span);
                engine.fail(XacroExpandErrorCode::UndefinedSymbol,
                            {{"item-kind", "unknown-attr"}, {"symbol", kv.first}},
                            "宏 '" + local + "' 收到未知属性 '" + kv.first
                                + "'——定位见诊断");
                return false;
            }
        }
        // 推作用域层（绑定入层——宏体内 ${} 解析：本层绑定→外层→全局），
        // 逐子节点展开宏体，弹层。
        engine.layers.push_back(std::move(bindings));
        const bool ok = emitChildren(engine, macro.fileIdx, macro.node, depth + 1);
        engine.layers.pop_back();
        return ok;
    }

    // ---- 普通元素：原样传递＋${} 替换（展开产物进入 URDF 同一边界的
    // 形态基础——mapUrdf 直接可解析）----
    engine.out += '<';
    engine.out += name;
    for (const pugi::xml_attribute& attr : node.attributes()) {
        std::string value;
        if (!substituteText(engine, fileIdx, attr.value(), node.offset_debug(),
                            &value)) {
            return false;
        }
        engine.out += ' ';
        engine.out += attr.name();
        engine.out += "=\"";
        engine.out += escapeXmlAttribute(value);
        engine.out += '"';
    }
    if (node.first_child().empty()) {
        engine.out += "/>";  // 空元素自闭合——确定性序列化形态
    } else {
        engine.out += '>';
        if (!emitChildren(engine, fileIdx, node, depth)) { return false; }
        engine.out += "</";
        engine.out += name;
        engine.out += '>';
    }

    // 护栏检查：展开产物总字节 ≤ TotalBytes 防御值（io.md §6.2 边界——
    // 宏递归可放大输入，深度护栏之外的第二道纵深；超限→
    // IO-SEC-BUDGET-TOTAL 透传。单元测试不触发该值——如实声明于头文件）。
    if (engine.out.size() > static_cast<std::size_t>(kXacroExpandedTotalBytesGuard)) {
        engine.pushIoPassthroughDiag(
            io::errorCodeToken(io::IoErrorCode::SecBudgetTotal),
            "展开产物总字节超限（actual=" + std::to_string(engine.out.size())
                + "; limit=" + std::to_string(kXacroExpandedTotalBytesGuard)
                + "; unit=bytes）",
            "缩减输入规模或宏展开倍率（io.md §6.2 TotalBytes 边界）", span);
        engine.fail(XacroExpandErrorCode::ExpansionBudgetExceeded,
                    {{"actual", std::to_string(engine.out.size())},
                     {"limit", std::to_string(kXacroExpandedTotalBytesGuard)}},
                    "展开产物总字节超限——io.md §6.2 边界（TotalBytes）");
        return false;
    }
    return true;
}

/**
 * @brief include 图环检测（防御性 DFS——io 契约已在树构建期拒绝环；本步
 *        透传 V-09：装配面带来带环树时给 IO-FORMAT-XML-CYCLE＋环路径）。
 *
 * 邻接表自树 Include 边构建（边序＝树稳定序——遍历/环路径确定性）；
 * path 为当前栈，命中栈内键即环（自 key 首次出现处截断闭合呈现）。
 * 返回 false＝已检出环并置失败。
 */
bool detectIncludeCycle(Engine& engine, const ValidatedXacroSource& source)
{
    std::map<std::string, std::vector<std::string>> includeAdj;
    for (const io::ResourceEdge& e : source.dependencyTree.edges) {
        if (e.kind == io::ResourceEdgeKind::Include) {
            includeAdj[e.fromRel].push_back(e.toRel);
        }
    }
    std::set<std::string> visited;
    std::vector<std::string> path;
    // 递归闭包（邻接序稳定——环路径确定性）。
    const std::function<bool(const std::string&)> dfs
        = [&](const std::string& key) -> bool {
        if (std::find(path.begin(), path.end(), key) != path.end()) {
            std::string cycle;
            auto start = std::find(path.begin(), path.end(), key);
            for (auto it = start; it != path.end(); ++it) {
                cycle += *it + " -> ";
            }
            cycle += key;
            const ImportSourceSpan rootSpan{source.dependencyTree.rootRel, 0, 0};
            engine.pushIoPassthroughDiag(
                io::errorCodeToken(io::IoErrorCode::FormatXmlCycle),
                "include 图检出环（cycle-path=" + cycle + "）",
                "打断 include 环（io.md §6.2 循环引用拒绝）", rootSpan);
            engine.fail(XacroExpandErrorCode::IncludeCycle,
                        {{"cycle-path", cycle}},
                        "include 图环——IO-FORMAT-XML-CYCLE（V-09 透传）");
            return false;
        }
        if (!visited.insert(key).second) { return true; }  // 已查子图——剪枝
        path.push_back(key);
        const auto adj = includeAdj.find(key);
        if (adj != includeAdj.end()) {
            for (const std::string& next : adj->second) {
                if (!dfs(next)) { return false; }
            }
        }
        path.pop_back();
        return true;
    };
    return dfs(source.dependencyTree.rootRel);
}

}  // namespace

// =====================================================================
// 主体：XacroExpandService::expand（六步——契约校验→缺失预扫描→环检测
// →解析→展开走查→出口装配）
// =====================================================================

ExpandOutcome XacroExpandService::expand(const ValidatedXacroSource& source,
                                         const XacroSubstitutionMap& substitutions,
                                         std::vector<core::DiagnosticRecord>& diags) const
{
    ExpandOutcome outcome;

    // 来源记录恒装配（§6.5"来源记录"——成功失败都成立：digest 来自 io
    // 快照，substitutions 保序透传；mapXacroExpanded 的第二输入）。路径
    // 文本取快照实体路径的 UTF-8 通用形（正斜杠——跨平台留痕一致；路径
    // 不作身份，SP-5）。
    outcome.provenance.sourceDigest = source.entrySnapshot.contentDigest;
    outcome.provenance.sourceAbsPath = source.entrySnapshot.finalPath.generic_u8string();
    outcome.provenance.substitutions = substitutions;

    // 展开环境先入参数列表（输入序——头文件契约注："substitutions 在前"）。
    for (const auto& sub : substitutions) {
        outcome.parameters.push_back(
            XacroParameterItem{sub.first, sub.second, "substitution",
                               ImportSourceSpan{std::string{}, 0, 0}});
    }

    // 诊断收集（出口统一稳定排序——成功失败同一实现）。
    Engine engine{source, substitutions};
    engine.layers.push_back({});  // 全局作用域层（文档属性定义层）
    engine.out.reserve(source.entryBytes.size() * 2 + 64);  // 产物规模预估（宏放大留量）

    // -----------------------------------------------------------------
    // 第①步：输入契约校验（调用方违约→值面 SourceInconsistent——无草稿
    // 产出，不抛异常；来源记录消费契约（mapXacroExpanded 要求非零摘要＋
    // 非空路径）在此一并保证）。
    // -----------------------------------------------------------------
    const bool digestZero = [&] {
        for (const std::uint8_t b : source.entrySnapshot.contentDigest) {
            if (b != 0) { return false; }
        }
        return true;
    }();
    if (source.entryBytes.empty() || source.dependencyTree.rootRel.empty()
        || digestZero || source.entrySnapshot.finalPath.empty()) {
        engine.fail(XacroExpandErrorCode::SourceInconsistent,
                    {{"stage", "input-contract"}},
                    "ValidatedXacroSource 契约违约：入口字节/树根键/来源摘要/"
                    "来源路径缺一不可（io 产物装配面）");
    } else {
        // 树节点集＋入口在树中校验（io §6.5 树契约：入口文档必为节点）。
        std::set<std::string> nodeKeys;
        for (const io::ResourceNode& n : source.dependencyTree.nodes) {
            nodeKeys.insert(n.relPath);
        }
        if (nodeKeys.find(source.dependencyTree.rootRel) == nodeKeys.end()) {
            engine.fail(XacroExpandErrorCode::SourceInconsistent,
                        {{"stage", "input-contract"},
                         {"root-rel", source.dependencyTree.rootRel}},
                        "依赖树缺入口文档节点——ValidatedXacroSource 契约违约");
        } else {
            // 字节表键必须都在树节点集中且树未声明其缺失（键不在树＝装配
            // 面与树不一致；键在缺失叶＝矛盾输入——io 语义面 exists=false
            // 即 io 未取得内容）。
            for (const auto& kv : source.includeBytes) {
                const auto nodeIt = std::find_if(
                    source.dependencyTree.nodes.begin(),
                    source.dependencyTree.nodes.end(),
                    [&kv](const io::ResourceNode& n) { return n.relPath == kv.first; });
                if (nodeIt == source.dependencyTree.nodes.end()) {
                    engine.fail(XacroExpandErrorCode::SourceInconsistent,
                                {{"stage", "input-contract"}, {"bytes-key", kv.first}},
                                "includeBytes 键不在依赖树节点集——装配面与树不一致");
                    break;
                }
                if (!nodeIt->exists) {
                    engine.fail(XacroExpandErrorCode::SourceInconsistent,
                                {{"stage", "input-contract"}, {"bytes-key", kv.first}},
                                "includeBytes 键对应树缺失叶（exists=false）——矛盾输入");
                    break;
                }
            }
        }
    }

    if (!engine.failed) {
        // -----------------------------------------------------------------
        // 第②步：include 边缺失预扫描（离线依赖硬失败——io.md §6.2"展开
        // 所需全部文件必须已在本机；缺文件→IO-RES-MISSING＋缺失清单"）。
        // 边序＝树稳定序（io 折叠键字典序）——缺失清单确定性。mesh 类边不
        // 在本步（缺失网格是 mapUrdf 的 Recorded 软事实，§6.7/V-08）。
        // -----------------------------------------------------------------
        std::vector<std::string> missingList;
        for (const io::ResourceEdge& e : source.dependencyTree.edges) {
            if (e.kind != io::ResourceEdgeKind::Include) { continue; }
            // 入口文档自身的字节随 entryBytes 交付（不在字节表——根键命中
            // 即视为在位；环样例的回边依赖此豁免，缺失判定不被误触）。
            if (e.toRel == source.dependencyTree.rootRel) { continue; }
            const auto target = std::find_if(
                source.dependencyTree.nodes.begin(), source.dependencyTree.nodes.end(),
                [&e](const io::ResourceNode& n) { return n.relPath == e.toRel; });
            const bool targetMissing
                = target == source.dependencyTree.nodes.end() || !target->exists;
            const bool bytesAbsent
                = source.includeBytes.find(e.toRel) == source.includeBytes.end();
            if (targetMissing || bytesAbsent) {
                missingList.push_back(e.toRel);
            }
        }
        if (!missingList.empty()) {
            std::string joined;
            for (const std::string& m : missingList) {
                if (!joined.empty()) { joined += "; "; }
                joined += m;
            }
            const ImportSourceSpan rootSpan{source.dependencyTree.rootRel, 0, 0};
            engine.pushIoPassthroughDiag(
                io::errorCodeToken(io::IoErrorCode::ResMissing),
                "include 类依赖缺失（missing-count=" + std::to_string(missingList.size())
                    + "; missing-list=" + joined + "）",
                "把被包含文件置于导入根内并重跑 io 读取（离线依赖交付）", rootSpan);
            engine.fail(XacroExpandErrorCode::DependencyMissing,
                        {{"missing-count", std::to_string(missingList.size())},
                         {"missing-list", joined}},
                        "include 依赖缺失——IO-RES-MISSING（V-09 同族透传）");
        }
    }

    if (!engine.failed) {
        // -----------------------------------------------------------------
        // 第③步：include 图环检测（防御性复核——io 契约无环，本步透传
        // V-09：调用方装配了带环树时给出 IO-FORMAT-XML-CYCLE＋环路径）。
        // -----------------------------------------------------------------
        detectIncludeCycle(engine, source);
    }

    if (!engine.failed) {
        // -----------------------------------------------------------------
        // 第④步：解析入口与 include 字节（pugixml——io 良构检查保证可解析；
        // 解析失败即调用方装配了非 io 字节→SourceInconsistent，附
        // IO-FORMAT-XML-SYNTAX 透传定位）。文档对象入 Engine.docs（与引擎
        // 同栈保活——解析缓冲 SourceFile.text 同栈，生命周期闭合）。
        // -----------------------------------------------------------------
        engine.files.emplace_back(
            std::string(source.dependencyTree.rootRel),
            std::string(reinterpret_cast<const char*>(source.entryBytes.data()),
                        source.entryBytes.size()));
        for (const auto& kv : source.includeBytes) {
            engine.files.emplace_back(
                kv.first,
                std::string(reinterpret_cast<const char*>(kv.second.data()),
                            kv.second.size()));
        }
        for (std::size_t i = 0; i < engine.files.size(); ++i) {
            engine.fileByKey[engine.files[i].relKey] = i;
        }
        engine.docs.resize(engine.files.size());
        engine.docRoots.resize(engine.files.size());
        for (std::size_t i = 0; i < engine.files.size(); ++i) {
            const pugi::xml_parse_result parsed = engine.docs[i].load_buffer(
                engine.files[i].text.data(), engine.files[i].text.size());
            if (!parsed) {
                const ImportSourceSpan span
                    = engine.spanOf(i, static_cast<std::ptrdiff_t>(parsed.offset));
                engine.pushIoPassthroughDiag(
                    io::errorCodeToken(io::IoErrorCode::FormatXmlSyntax),
                    "XML 解析失败（io 良构检查之上不应到达——调用方装配了"
                    "非已验证字节；解析器描述见 detail）",
                    "经 io 重新读取并装配已验证字节", span);
                engine.fail(XacroExpandErrorCode::SourceInconsistent,
                            {{"stage", "parse"}, {"file", engine.files[i].relKey}},
                            std::string("解析失败：") + parsed.description());
                break;
            }
            engine.docRoots[i] = engine.docs[i].document_element();
        }
    }

    if (!engine.failed) {
        // -----------------------------------------------------------------
        // 第⑤步：展开走查——入口文档顶层 XML 声明（若有）原样发出，其后
        // 根元素以深度 1 走查（xacro: 前缀分派在其子节点逐层发生；宏调用/
        // include 拼接才计层——"展开递归深度"的 §6.2 语义）。
        // -----------------------------------------------------------------
        const pugi::xml_node first = engine.docs[0].first_child();
        if (first.type() == pugi::node_declaration) {
            engine.out += "<?xml";
            for (const pugi::xml_attribute& attr : first.attributes()) {
                engine.out += ' ';
                engine.out += attr.name();
                engine.out += "=\"";
                engine.out += escapeXmlAttribute(attr.value());
                engine.out += '"';
            }
            engine.out += "?>\n";
        }
        emitElement(engine, 0, engine.docRoots[0], 1);
    }

    // -----------------------------------------------------------------
    // 第⑥步：出口装配（诊断稳定排序——成功失败同一实现）。
    // 成功：产物字节＋参数清单＋来源记录（provenance 已在入口装配）。
    // 失败：无产物（无草稿面——MDL-19"展开失败不产生草稿"），参数清单
    // 保留失败前已收集部分（报告面不因失败丢失——"识别＋报告"同风味）。
    // -----------------------------------------------------------------
    flushDiags(engine.diagEntries, diags);
    outcome.parameters.insert(outcome.parameters.end(),
                              std::make_move_iterator(engine.parameters.begin()),
                              std::make_move_iterator(engine.parameters.end()));
    if (engine.failed) {
        outcome.error = std::move(engine.err);
        return outcome;
    }
    outcome.expandedBytes.emplace(engine.out.begin(), engine.out.end());
    return outcome;
}

}  // namespace sdurws::ird::modeling
