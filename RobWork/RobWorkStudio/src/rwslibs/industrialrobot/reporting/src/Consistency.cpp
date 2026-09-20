/**
 * @file   Consistency.cpp
 * @brief  一致性检查器实现——三格式逐字段回读提取＋矩阵驱动七元组比对
 *         （RPT-T07 产物，§8.5/§9.4）。
 *
 * 设计依据（对齐 units/reporting.md）：
 *   - §8.5 判定规则：对 FieldMatrix 的每个 FieldCell，三格式回读值与源值
 *     逐字符一致；缺失字段（某格式未输出该 fieldKey）＝mismatch；限定语
 *     丢失＝mismatch（§6.4 硬约束）；数值以文本规范形比对（非浮点再解析
 *     ——D-09 避免二次舍入假差异）；mismatches 全量列出（不短路）；
 *   - §9.4 契约：确定性纯函数、不修改任何输入、并发安全；回读本身失败
 *     （格式损坏）→ ConsistencyMismatch（dimension=parse-failed，错误轨
 *     抛出）；artifacts<2 平凡通过＋注记；
 *   - §3.3/NFR-SEC-03：CSV 回读经注入 IReportCsvReader 消费 io reader
 *     （转义唯一实现归 io）——本文件零 CSV 字节解析（P-RPT-1，acceptance 5）；
 *   - NFR-DEP-04 读取侧：JSON 回读解析消费 kJsonTopLevelKeys 冻结集拒绝
 *     外来未知字段（Render.hpp 键集注释的执行点）。
 *
 * 各格式提取器的失败语义边界（parse-failed 轨 vs mismatch 轨——设计裁定，
 * 随单元卡 §14.4 登记）：
 *   - parse-failed（抛 ConsistencyMismatch）＝**结构层**损坏：扫描/解析
 *     进行到一半无法继续（JSON 语法错、CSV reader 报失败、HTML 标签/
 *     单元格/行/表未闭合、模板结构违约如数据行缺 data-entry 属性、
 *     fieldKey 重复锚定）。此时"哪个字段不一致"无从谈起，只能整体拒绝；
 *   - mismatch（入列）＝**内容层**差异：结构完好但某字段缺席（missing）
 *     或值/单位/状态/限定语/引用与矩阵不一致。工件整体缺失某条目表时，
 *     矩阵驱动比对把全部字段判为 missing——"提取器只对它进入的结构负责"。
 *
 * 线程安全：check() 无共享可变状态（提取缓冲全局部）——并发安全（§9.4）。
 * 确定性：比对序＝矩阵序→工件输入序→维度序（value→unit→status→
 * qualifier→ref）；无时钟/随机/locale 依赖（NFR-COR-02）。
 */

#include <sdurws/ird/reporting/Consistency.hpp>   // 本单元公共头（include 根——目标 include 路径）

#include <charconv>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/reporting/Errors.hpp>  // ReportError/ReportErrorCode（错误轨）

#include "RenderText.hpp"   // status token↔显示名共享表（HTML status 反查——值单源）

namespace sdurws::ird::reporting {

namespace {

// =====================================================================
// 提取值模型（三格式统一的回读单元格形状）
// =====================================================================

/**
 * @brief 单格式对单 fieldKey 的回读值（提取产物——比对的 actual 面）。
 *
 * anchored 语义：HTML 提取器对"行在、data-field 锚被删"的坏样本仍能以
 * 行内其余单元格重建 fieldKey，但值/限定语无从关联——置 anchored=false，
 * 比对只产出 missing 一条（不比较其余维度：无锚即无对齐基准）；JSON/
 * CSV 提取天然有键，恒 anchored=true。
 *
 * unit/status/resultRef 的缺席以 nullopt 表达（与空串值区分——空串值在
 * value 维度是合法文本，缺席是结构事实）。
 */
struct ExtractedField {
    bool anchored = false;                     ///< 是否存在字段对齐锚
    std::optional<std::string> value;          ///< 值文本规范形（value 维度）
    std::optional<std::string> unit;           ///< 显示单位 token（缺席＝nullopt）
    std::optional<std::string> status;         ///< 工程判定 token（HTML 已反查）
    std::vector<std::string> qualifier;        ///< 限定语 token 序（矩阵推导序）
    std::optional<std::string> resultRef;      ///< 结果引用规范文本
    std::vector<std::string> evidenceRef;      ///< 证据引用规范文本序
};

/// 提取映射（fieldKey → 回读值；map 保序仅为诊断可读——比对序由矩阵驱动）。
using Extraction = std::map<std::string, ExtractedField>;

// =====================================================================
// parse-failed 错误轨（§9.4 错误行——抛 ConsistencyMismatch）
// =====================================================================

/**
 * @brief 抛出"回读本身失败"错误（dimension=parse-failed 进入 detail）。
 *
 * detail 为开发诊断面（就地定位：格式/原因），面向用户前经 diagnostics
 * 脱敏设施（§5.8 同则——本函数不面向终端用户）。
 *
 * @param format [in] 回读失败的格式（detail 定位面）
 * @param why    [in] 失败原因（开发级，如"字符串未闭合"）
 * @throws ReportError ConsistencyMismatch（恒抛出——[[noreturn]]）
 */
[[noreturn]] void failParseFailed(ReportRenderFormat format, std::string_view why)
{
    throw ReportError(ReportErrorCode::ConsistencyMismatch,
                      std::string("工件回读失败（格式损坏）：format=") + std::string(token(format))
                          + "，原因：" + std::string(why) + "，dimension="
                          + std::string(kMismatchDimParseFailed));
}

// =====================================================================
// mismatch 收集（不短路——全量入列）
// =====================================================================

/**
 * @brief 追加一条 mismatch（缺席值以 kMismatchAbsentMark 表达）。
 *
 * @param out      [out] 收集容器
 * @param format   [in] 工件格式
 * @param fieldKey [in] 字段对齐键
 * @param dim      [in] 维度词（kMismatchDim* 常量）
 * @param expected [in] 源值（nullopt＝源缺席）
 * @param actual   [in] 回读值（nullopt＝回读缺席）
 */
void addMismatch(std::vector<FieldMismatch>& out, ReportRenderFormat format,
                 const std::string& fieldKey, std::string_view dim,
                 const std::optional<std::string>& expected,
                 const std::optional<std::string>& actual)
{
    FieldMismatch m;
    m.format = format;
    m.fieldKey = fieldKey;
    m.dimension = std::string(dim);
    m.expected = expected.value_or(std::string(kMismatchAbsentMark));
    m.actual = actual.value_or(std::string(kMismatchAbsentMark));
    out.push_back(std::move(m));
}

// =====================================================================
// HTML 反转义（Render.cpp htmlEscape 的逆——提取器专用，模板内联样式
// 转义面仅此五实体；转义/反转义双射是"逐字符比对"成立的前提）
// =====================================================================

/**
 * @brief HTML 实体反转义（htmlEscape 的精确逆）。
 *
 * 顺序纪律：具名实体（&lt; &gt; &quot; &#39;）先替换、&amp; 最后替换
 * ——htmlEscape 先转义 & 产生 &amp;，逆序还原才不产生二次解码（原文
 * "&lt;" 经转义为 "&amp;lt;"，若先替换 &amp; 会错得 "&lt;" 再错得 "<"）。
 *
 * @param raw [in] 转义文本（HTML 单元格/属性值原文）
 * @return 原文（UTF-8）
 *
 * 复杂度：O(n·实体数)——报告单元格量级无虞。
 */
std::string htmlUnescape(std::string_view raw)
{
    const auto replaceAll = [](std::string text, std::string_view from, std::string_view to) {
        std::size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos) {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
        return text;
    };
    // 顺序：具名实体在前、&amp; 收尾（见函数注——二次解码防线）。
    std::string out = replaceAll(std::string(raw), "&lt;", "<");
    out = replaceAll(out, "&gt;", ">");
    out = replaceAll(out, "&quot;", "\"");
    out = replaceAll(out, "&#39;", "'");
    out = replaceAll(out, "&amp;", "&");
    return out;
}

// =====================================================================
// HTML 提取器（§8.5"HTML：data-field 提取器——模板自有的机器可解析
// 结构"；严格行列文法扫描——结构层损坏走 parse-failed 轨）
// =====================================================================

/**
 * @brief 从 HTML 工件字节提取全部条目字段回读值。
 *
 * 文法（Render.cpp appendSection 冻结结构——提取器与渲染器同模板纪律）：
 *   条目表 ＝ `<table data-section="SID">` 行* `</table>`
 *   行     ＝ `<tr data-entry="E">` 单元格* `</tr>`（表头行含 `<th>`，跳过）
 *   单元格 ＝ `<td data-field="K">值文本 [限定语 span*]</td>`
 *          ／ `<td class="unit">单位|&mdash;</td>`
 *          ／ `<td>纯文本</td>`（序位：0=条目显示名、1=字段键尾段、
 *            末位=工程判定显示名——RPT-T07 起状态列以共享映射表反查 token）
 *
 * 严格性裁定（见文件头失败语义边界）：行缺 data-entry／单元格里表结构
 * 截断／fieldKey 重复锚定＝parse-failed；data-field 锚整格缺失＝内容层
 * 坏样本（anchored=false——missing 轨），不判结构损坏。文本内容经
 * htmlEscape 全量转义（字面 '<' 不可能出现），标签扫描无歧义。
 *
 * @param bytes [in] 工件完整字节（UTF-8 无 BOM、LF 行尾——§8.1 总则）
 * @return fieldKey → 回读值映射
 *
 * @throws ReportError ConsistencyMismatch（结构层损坏——dimension=
 *         parse-failed）
 */
Extraction extractHtml(const std::vector<std::uint8_t>& bytes)
{
    // 字节→文本视图（零拷贝——扫描只读；UTF-8 按字节扫描，标签均为 ASCII）。
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    Extraction out;
    std::size_t pos = 0;
    static constexpr std::string_view kTableOpen = "<table data-section=\"";
    static constexpr std::string_view kTableClose = "</table>";
    static constexpr std::string_view kRowOpen = "<tr";
    static constexpr std::string_view kRowClose = "</tr>";

    // 外层：定位条目表（data-section 属性——§8.1 机器可提取锚的载体）。
    while ((pos = text.find(kTableOpen, pos)) != std::string_view::npos) {
        pos += kTableOpen.size();
        const std::size_t sectionIdEnd = text.find('"', pos);
        if (sectionIdEnd == std::string_view::npos) {
            failParseFailed(ReportRenderFormat::Html, "data-section 属性值未闭合");
        }
        // 属性值为 htmlEscape 形——反转义还原 sectionId（拼接 fieldKey 用）。
        const std::string sectionId = htmlUnescape(text.substr(pos, sectionIdEnd - pos));
        pos = sectionIdEnd + 1;
        const std::size_t tableEnd = text.find(kTableClose, pos);
        if (tableEnd == std::string_view::npos) {
            failParseFailed(ReportRenderFormat::Html, "条目表未闭合（缺 </table>）");
        }

        // 内层：逐行扫描至表尾（行结构见函数注文法）。
        while (pos < tableEnd) {
            const std::size_t rowStart = text.find(kRowOpen, pos);
            if (rowStart == std::string_view::npos || rowStart >= tableEnd) {
                break;   // 表尾前无更多行——本表扫描结束（正常出口）
            }
            const std::size_t rowTagEnd = text.find('>', rowStart);
            if (rowTagEnd == std::string_view::npos || rowTagEnd >= tableEnd) {
                failParseFailed(ReportRenderFormat::Html, "<tr> 标签未闭合");
            }
            const std::string_view rowTag = text.substr(rowStart, rowTagEnd - rowStart + 1);
            const std::size_t rowEnd = text.find(kRowClose, rowTagEnd + 1);
            if (rowEnd == std::string_view::npos || rowEnd > tableEnd) {
                failParseFailed(ReportRenderFormat::Html, "行未闭合（缺 </tr>）");
            }
            const std::string_view rowBody = text.substr(rowTagEnd + 1, rowEnd - rowTagEnd - 1);
            pos = rowEnd + kRowClose.size();

            // 表头行（含 <th>——列名词表行）跳过；数据行必须携带 data-entry。
            if (rowBody.find("<th") != std::string_view::npos) {
                continue;
            }
            static constexpr std::string_view kEntryAttr = "data-entry=\"";
            const std::size_t entryAttr = rowTag.find(kEntryAttr);
            if (entryAttr == std::string_view::npos) {
                // 数据行缺条目锚＝模板结构违约（fieldKey 无从重建——整体拒绝）。
                failParseFailed(ReportRenderFormat::Html, "数据行缺 data-entry 属性");
            }
            const std::size_t entryKeyEnd = rowTag.find('"', entryAttr + kEntryAttr.size());
            if (entryKeyEnd == std::string_view::npos) {
                failParseFailed(ReportRenderFormat::Html, "data-entry 属性值未闭合");
            }
            const std::string entryKey = htmlUnescape(
                rowTag.substr(entryAttr + kEntryAttr.size(), entryKeyEnd - entryAttr - kEntryAttr.size()));

            // 单元格切分与分类（<td 不嵌套——模板事实；逐格找 </td>）。
            std::optional<std::string> anchorKey;          // data-field 锚（0 或 1 个）
            std::string_view valueBody;                    // 锚单元格内容（值文本＋限定语 span）
            std::optional<std::string_view> unitBody;      // class="unit" 单元格内容
            std::vector<std::string_view> plainCells;      // 纯文本单元格（序位语义见函数注）
            std::size_t cellPos = 0;
            while (true) {
                const std::size_t cellStart = rowBody.find("<td", cellPos);
                if (cellStart == std::string_view::npos) {
                    break;
                }
                const std::size_t cellTagEnd = rowBody.find('>', cellStart);
                if (cellTagEnd == std::string_view::npos) {
                    failParseFailed(ReportRenderFormat::Html, "<td> 标签未闭合");
                }
                const std::string_view cellTag = rowBody.substr(cellStart, cellTagEnd - cellStart + 1);
                const std::size_t cellEnd = rowBody.find("</td>", cellTagEnd + 1);
                if (cellEnd == std::string_view::npos) {
                    failParseFailed(ReportRenderFormat::Html, "单元格未闭合（缺 </td>）");
                }
                const std::string_view cellBody = rowBody.substr(cellTagEnd + 1, cellEnd - cellTagEnd - 1);
                cellPos = cellEnd + 5;   // "</td>" 长度

                // 分类：值锚（data-field）／单位（class="unit"）／纯文本。
                static constexpr std::string_view kFieldAttr = "data-field=\"";
                static constexpr std::string_view kUnitAttr = "class=\"unit\"";
                const std::size_t fieldAttr = cellTag.find(kFieldAttr);
                if (fieldAttr != std::string_view::npos) {
                    if (anchorKey.has_value()) {
                        // 同行双锚＝结构违约（fieldKey 对齐歧义——整体拒绝）。
                        failParseFailed(ReportRenderFormat::Html, "同行出现多个 data-field 锚");
                    }
                    const std::size_t keyEnd = cellTag.find('"', fieldAttr + kFieldAttr.size());
                    if (keyEnd == std::string_view::npos) {
                        failParseFailed(ReportRenderFormat::Html, "data-field 属性值未闭合");
                    }
                    anchorKey = htmlUnescape(
                        cellTag.substr(fieldAttr + kFieldAttr.size(), keyEnd - fieldAttr - kFieldAttr.size()));
                    valueBody = cellBody;
                } else if (cellTag.find(kUnitAttr) != std::string_view::npos) {
                    if (unitBody.has_value()) {
                        failParseFailed(ReportRenderFormat::Html, "同行出现多个单位单元格");
                    }
                    unitBody = cellBody;
                } else {
                    plainCells.push_back(cellBody);
                }
            }

            // 行内结构要求：单位单元格必在（渲染器每行恒输出）；纯文本
            // ≥3（条目显示名＋字段键尾段＋工程判定——值锚缺席时仍成立）。
            if (!unitBody.has_value() || plainCells.size() < 3) {
                failParseFailed(ReportRenderFormat::Html, "数据行结构不完整（缺单位列或状态列）");
            }
            // fieldKey 重建：锚在＝直接取锚；锚被删（坏样本形态）＝以行内
            // 其余序位单元格重建（sectionId.data-entry.字段键尾段）——missing
            // 定位仍可落到具体 fieldKey。
            std::string fieldKey = anchorKey.has_value()
                                       ? *anchorKey
                                       : sectionId + "." + entryKey + "." + htmlUnescape(plainCells[1]);
            const auto inserted = out.emplace(fieldKey, ExtractedField{});
            if (!inserted.second) {
                // 重复锚定＝结构违约（同 fieldKey 两处输出——对齐歧义）。
                failParseFailed(ReportRenderFormat::Html, "fieldKey 重复锚定：" + fieldKey);
            }
            ExtractedField& extracted = inserted.first->second;

            // 值与限定语：锚在才有对齐基准（缺席→missing 轨，不比较其余维度）。
            if (anchorKey.has_value()) {
                extracted.anchored = true;
                // 值文本＝锚单元格内容首个 <span 之前段（限定语 span 尾随——
                // 渲染序冻结；字面 '<' 已被转义，切分无歧义）。
                const std::size_t spanStart = valueBody.find("<span");
                extracted.value = htmlUnescape(
                    spanStart == std::string_view::npos ? valueBody : valueBody.substr(0, spanStart));
                // 限定语 token：data-qualifier 属性逐个提取（出现序＝矩阵
                // 推导序——§6.4 结构性输出；属性值恒为词表 token，无转义
                // 字符但统一反转义以对齐属性读取路径）。
                static constexpr std::string_view kQualifierAttr = "data-qualifier=\"";
                std::size_t qPos = 0;
                while ((qPos = valueBody.find(kQualifierAttr, qPos)) != std::string_view::npos) {
                    const std::size_t tokStart = qPos + kQualifierAttr.size();
                    const std::size_t tokEnd = valueBody.find('"', tokStart);
                    if (tokEnd == std::string_view::npos) {
                        failParseFailed(ReportRenderFormat::Html, "data-qualifier 属性值未闭合");
                    }
                    extracted.qualifier.push_back(htmlUnescape(valueBody.substr(tokStart, tokEnd - tokStart)));
                    qPos = tokEnd + 1;
                }
            }

            // 单位维度：&mdash;＝缺席（渲染器缺席形——§8.1 状态徽标同款
            // 占位纪律）；其余文本反转义为单位 token。
            static constexpr std::string_view kMdash = "&mdash;";
            extracted.unit = (*unitBody == kMdash)
                                 ? std::nullopt
                                 : std::optional<std::string>(htmlUnescape(*unitBody));

            // 状态维度：末位纯文本单元格 → 反查 token（共享映射表——正向
            // 渲染与反向回读同表，见 RenderText.hpp 文件头"为什么"）。
            // &mdash;＝缺席。
            const std::string_view statusRaw = plainCells.back();
            extracted.status = (statusRaw == kMdash)
                                   ? std::nullopt
                                   : std::optional<std::string>(
                                       detail::statusDisplayToToken(htmlUnescape(statusRaw)));
        }
        pos = tableEnd + kTableClose.size();
    }
    return out;
}

// =====================================================================
// JSON 严格解析器（§8.5"JSON：parse→字段遍历"——reporting 自有 schema
// （ird-report-json/1）的回读解析面；io 注入缝只覆盖 CSV（P-RPT-1 同案
// 契约口径），JSON 解析为检查器内部职责——RPT-T06 的 ReportJsonDom 为
// 解析目标值型，重复键拒绝由本解析器前置判定（parse-failed 轨），不落
// DataInvalid（DataInvalid 是报告数据错误——此处是工件格式损坏，语义
// 分立，§3.5）。
// =====================================================================

/**
 * @brief 严格 JSON 解析器（RFC 8259 子集——canonical 超集容忍）。
 *
 * 严格性：字符串控制字符必须转义、重复键拒绝、文档尾随内容拒绝、
 * 数值文法逐项校验（前导零/孤立小数点拒绝）。\\uXXXX 按 UTF-8 编码
 * （代理对成对校验——孤立代理拒绝）。不做全文 UTF-8 合法性校验（报告
 * 工件由 canonical 写出保证 UTF-8；结构层严格性已覆盖坏样本注入面）。
 */
class JsonParser {
public:
    JsonParser(const std::uint8_t* data, std::size_t size) noexcept
        : m_data(reinterpret_cast<const char*>(data)), m_size(size)
    {
    }

    /// 解析整个文档（值＋尾随空白检查——尾随内容＝结构损坏）。
    ReportJsonDom parseDocument()
    {
        skipWs();
        ReportJsonDom value = parseValue();
        skipWs();
        if (m_pos != m_size) {
            fail("文档尾随多余内容");
        }
        return value;
    }

private:
    [[noreturn]] void fail(std::string_view why) const
    {
        failParseFailed(ReportRenderFormat::Json,
                        std::string(why) + "（字节偏移 " + std::to_string(m_pos) + "）");
    }

    void skipWs()
    {
        while (m_pos < m_size) {
            const char c = m_data[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    char peek() const
    {
        if (m_pos >= m_size) {
            fail("文档意外结束");
        }
        return m_data[m_pos];
    }

    void expect(char c)
    {
        if (peek() != c) {
            fail(std::string("预期字符 '") + c + "'");
        }
        ++m_pos;
    }

    ReportJsonDom parseValue()
    {
        // 值前空白统一跳过（JSON 允许 token 间任意空白——canonical 恰在
        // ': '/逗号后携带空格/换行；入口跳过使各调用点不必重复处理）。
        skipWs();
        switch (peek()) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return ReportJsonDom::string(parseString());
        case 't': return parseLiteral("true", ReportJsonDom::boolean(true));
        case 'f': return parseLiteral("false", ReportJsonDom::boolean(false));
        case 'n': return parseLiteral("null", ReportJsonDom::nullValue());
        default:  return parseNumber();
        }
    }

    /// 字面量消费（true/false/null——前缀即全文，多字节拒收）。
    ReportJsonDom parseLiteral(std::string_view literal, ReportJsonDom value)
    {
        if (m_size - m_pos < literal.size()
            || std::string_view(m_data + m_pos, literal.size()) != literal) {
            fail("非法字面量");
        }
        m_pos += literal.size();
        return value;
    }

    ReportJsonDom parseObject()
    {
        expect('{');
        skipWs();
        if (peek() == '}') {
            ++m_pos;
            return ReportJsonDom::object({});
        }
        std::vector<std::pair<std::string, ReportJsonDom>> members;
        while (true) {
            skipWs();
            const std::string key = parseString();
            // 重复键前置判定（parse-failed 轨——ReportJsonDom::object 的
            // DataInvalid 拒绝面因此不可达，错误语义保持工件损坏分立）。
            for (const auto& existing : members) {
                if (existing.first == key) {
                    fail("对象重复键：" + key);
                }
            }
            skipWs();
            expect(':');
            members.emplace_back(std::move(key), parseValue());
            skipWs();
            const char c = peek();
            if (c == ',') {
                ++m_pos;
                continue;
            }
            if (c == '}') {
                ++m_pos;
                return ReportJsonDom::object(std::move(members));
            }
            fail("对象成员分隔符预期 ',' 或 '}'");
        }
    }

    ReportJsonDom parseArray()
    {
        expect('[');
        skipWs();
        if (peek() == ']') {
            ++m_pos;
            return ReportJsonDom::array({});
        }
        std::vector<ReportJsonDom> items;
        while (true) {
            items.push_back(parseValue());
            skipWs();
            const char c = peek();
            if (c == ',') {
                ++m_pos;
                continue;
            }
            if (c == ']') {
                ++m_pos;
                return ReportJsonDom::array(std::move(items));
            }
            fail("数组分隔符预期 ',' 或 ']'");
        }
    }

    /// 字符串解析（转义全表＋\uXXXX UTF-8 编码；未转义控制字符拒绝）。
    std::string parseString()
    {
        expect('"');
        std::string out;
        while (true) {
            if (m_pos >= m_size) {
                fail("字符串未闭合");
            }
            const char c = m_data[m_pos++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("字符串含未转义控制字符");
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            const char esc = m_pos < m_size ? m_data[m_pos++] : '\0';
            switch (esc) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u':  out += parseUnicodeEscape(); break;
            default:   fail("非法转义字符");
            }
        }
    }

    /// \\uXXXX→UTF-8（代理对成对——孤立高/低代理拒绝，防非法 UTF-8 入层）。
    std::string parseUnicodeEscape()
    {
        const auto hex4 = [this]() -> unsigned {
            if (m_size - m_pos < 4) {
                fail("\\u 转义需要 4 个十六进制位");
            }
            unsigned value = 0;
            for (int i = 0; i < 4; ++i) {
                const char h = m_data[m_pos++];
                value <<= 4;
                if (h >= '0' && h <= '9')      { value |= static_cast<unsigned>(h - '0'); }
                else if (h >= 'a' && h <= 'f') { value |= static_cast<unsigned>(h - 'a' + 10); }
                else if (h >= 'A' && h <= 'F') { value |= static_cast<unsigned>(h - 'A' + 10); }
                else { fail("\\u 转义含非十六进制字符"); }
            }
            return value;
        };
        unsigned code = hex4();
        if (code >= 0xD800 && code <= 0xDBFF) {
            // 高代理：必须跟随 \uDC00-\uDFFF 低代理（成对合成增补平面码点）。
            if (m_size - m_pos < 2 || m_data[m_pos] != '\\' || m_data[m_pos + 1] != 'u') {
                fail("高代理后缺低代理");
            }
            m_pos += 2;
            const unsigned low = hex4();
            if (low < 0xDC00 || low > 0xDFFF) {
                fail("低代理越界");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
        } else if (code >= 0xDC00 && code <= 0xDFFF) {
            fail("孤立低代理");
        }
        // 码点→UTF-8（1~4 字节——标准编码路径）。
        std::string encoded;
        if (code < 0x80) {
            encoded += static_cast<char>(code);
        } else if (code < 0x800) {
            encoded += static_cast<char>(0xC0 | (code >> 6));
            encoded += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            encoded += static_cast<char>(0xE0 | (code >> 12));
            encoded += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            encoded += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            encoded += static_cast<char>(0xF0 | (code >> 18));
            encoded += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            encoded += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            encoded += static_cast<char>(0x80 | (code & 0x3F));
        }
        return encoded;
    }

    /// 数值解析（JSON 文法逐项校验后 from_chars——canonical 的 to_chars
    /// 逆面；报告镜像数值仅计数量，不承载比对值——值维度恒字符串）。
    ReportJsonDom parseNumber()
    {
        const std::size_t start = m_pos;
        if (peek() == '-') {
            ++m_pos;
        }
        // 整数部分：'0' 或 [1-9][0-9]*（前导零拒绝）。
        if (peek() == '0') {
            ++m_pos;
        } else if (m_pos < m_size && m_data[m_pos] >= '1' && m_data[m_pos] <= '9') {
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
            }
        } else {
            fail("数值文法非法（缺整数部分）");
        }
        // 小数部分。
        if (m_pos < m_size && m_data[m_pos] == '.') {
            ++m_pos;
            if (m_pos >= m_size || m_data[m_pos] < '0' || m_data[m_pos] > '9') {
                fail("数值文法非法（小数点后缺数字）");
            }
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
            }
        }
        // 指数部分。
        if (m_pos < m_size && (m_data[m_pos] == 'e' || m_data[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_size && (m_data[m_pos] == '+' || m_data[m_pos] == '-')) {
                ++m_pos;
            }
            if (m_pos >= m_size || m_data[m_pos] < '0' || m_data[m_pos] > '9') {
                fail("数值文法非法（指数后缺数字）");
            }
            while (m_pos < m_size && m_data[m_pos] >= '0' && m_data[m_pos] <= '9') {
                ++m_pos;
            }
        }
        double value = 0.0;
        const char* begin = m_data + start;
        const char* end = m_data + m_pos;
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc() || result.ptr != end) {
            fail("数值转换失败");
        }
        return ReportJsonDom::number(value);
    }

    const char* m_data;        ///< 输入字节（借用——调用方保证存续）
    std::size_t m_size;        ///< 输入字节数
    std::size_t m_pos = 0;     ///< 当前扫描位置
};

/**
 * @brief 从 JSON 工件字节提取全部条目字段回读值（parse→字段遍历）。
 *
 * 遍历路径：顶层 → sections[] → entries[] → fields[]（fieldKey/value/
 * unit/qualifier 是比对消费面；其余成员仅做成员集与类型校验——NFR-DEP-04
 * 读取侧：各级成员集冻结，未知键拒绝而非忽略，PM-06 执行侧口径）。成员
 * 集与 Render.cpp buildReportDom 装配面逐字对应（ird-report-json/1 冻结
 * schema——渲染器增员即本处同步，版本升级走 kJsonSchemaVersion）。
 *
 * @param bytes [in] 工件完整字节（canonical UTF-8——§8.3）
 * @return fieldKey → 回读值映射（JSON 天然有键——anchored 恒 true）
 *
 * @throws ReportError ConsistencyMismatch（语法/结构/schema 违约——
 *         dimension=parse-failed）
 */
Extraction extractJson(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.empty()) {
        failParseFailed(ReportRenderFormat::Json, "工件为空字节");
    }
    JsonParser parser(bytes.data(), bytes.size());
    const ReportJsonDom dom = parser.parseDocument();
    if (dom.type() != ReportJsonDom::Type::Object) {
        failParseFailed(ReportRenderFormat::Json, "顶层非 JSON 对象");
    }

    // 顶层成员集校验（kJsonTopLevelKeys 冻结集——未知键拒绝，NFR-DEP-04
    // 读取侧；schemaVersion 兼容性判别）。
    for (const auto& member : dom.objectMembers()) {
        bool known = false;
        for (const std::string_view key : kJsonTopLevelKeys) {
            if (key == member.first) {
                known = true;
                break;
            }
        }
        if (!known) {
            failParseFailed(ReportRenderFormat::Json, "顶层未知字段：" + member.first);
        }
    }
    const auto findMember = [](const ReportJsonDom& object,
                               std::string_view key) -> const ReportJsonDom* {
        for (const auto& member : object.objectMembers()) {
            if (member.first == key) {
                return &member.second;
            }
        }
        return nullptr;
    };
    const ReportJsonDom* schemaVersion = findMember(dom, "schemaVersion");
    if (schemaVersion == nullptr || schemaVersion->type() != ReportJsonDom::Type::String
        || schemaVersion->stringValue() != kJsonSchemaVersion) {
        failParseFailed(ReportRenderFormat::Json, "schemaVersion 缺失或与本检查器支持版本不兼容");
    }
    const ReportJsonDom* sections = findMember(dom, "sections");
    if (sections == nullptr || sections->type() != ReportJsonDom::Type::Array) {
        failParseFailed(ReportRenderFormat::Json, "sections 缺失或非数组");
    }

    Extraction out;
    // 章节级冻结成员集（ird-report-json/1——与 buildReportDom 装配面一致）。
    static constexpr std::string_view kSectionMembers[] = {
        "sectionId", "sectionVersion", "selected", "status", "entries",
        "missingItems", "currentness", "diagnostics", "eligibilityNote",
        "renderHint", "order",
    };
    // 条目级冻结成员集。
    static constexpr std::string_view kEntryMembers[] = {
        "entryKey", "fields", "result", "evidence", "caseScope", "jump",
    };
    // 字段级冻结成员集（比对消费 fieldKey/value/unit/qualifier 四员）。
    static constexpr std::string_view kFieldMembers[] = {
        "key", "fieldKey", "state", "value", "unit", "methodTag", "qualifier",
    };
    // 成员集校验（泛型形参——数组边界在实例化点已知，MSVC 对未知边界
    // 数组引用的范围 for 不支持，故以 const auto& 承接）。
    const auto checkMembers = [](const ReportJsonDom& object, const auto& allowed,
                                 std::string_view what) {
        for (const auto& member : object.objectMembers()) {
            bool known = false;
            for (const std::string_view key : allowed) {
                if (key == member.first) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                failParseFailed(ReportRenderFormat::Json,
                                std::string(what) + "未知字段：" + member.first);
            }
        }
    };

    for (const ReportJsonDom& sectionDom : sections->arrayItems()) {
        if (sectionDom.type() != ReportJsonDom::Type::Object) {
            failParseFailed(ReportRenderFormat::Json, "sections 元素非对象");
        }
        checkMembers(sectionDom, kSectionMembers, "章节对象");
        const ReportJsonDom* entries = findMember(sectionDom, "entries");
        if (entries == nullptr || entries->type() != ReportJsonDom::Type::Array) {
            failParseFailed(ReportRenderFormat::Json, "章节 entries 缺失或非数组");
        }
        for (const ReportJsonDom& entryDom : entries->arrayItems()) {
            if (entryDom.type() != ReportJsonDom::Type::Object) {
                failParseFailed(ReportRenderFormat::Json, "entries 元素非对象");
            }
            checkMembers(entryDom, kEntryMembers, "条目对象");
            const ReportJsonDom* fields = findMember(entryDom, "fields");
            if (fields == nullptr || fields->type() != ReportJsonDom::Type::Array) {
                failParseFailed(ReportRenderFormat::Json, "条目 fields 缺失或非数组");
            }
            for (const ReportJsonDom& fieldDom : fields->arrayItems()) {
                if (fieldDom.type() != ReportJsonDom::Type::Object) {
                    failParseFailed(ReportRenderFormat::Json, "fields 元素非对象");
                }
                checkMembers(fieldDom, kFieldMembers, "字段对象");
                const ReportJsonDom* fieldKey = findMember(fieldDom, "fieldKey");
                const ReportJsonDom* value = findMember(fieldDom, "value");
                const ReportJsonDom* unit = findMember(fieldDom, "unit");
                const ReportJsonDom* qualifier = findMember(fieldDom, "qualifier");
                if (fieldKey == nullptr || fieldKey->type() != ReportJsonDom::Type::String
                    || fieldKey->stringValue().empty()) {
                    failParseFailed(ReportRenderFormat::Json, "字段 fieldKey 缺失或非非空字符串");
                }
                if (value == nullptr || value->type() != ReportJsonDom::Type::String) {
                    failParseFailed(ReportRenderFormat::Json, "字段 value 缺失或非字符串");
                }
                if (unit == nullptr
                    || (unit->type() != ReportJsonDom::Type::String
                        && unit->type() != ReportJsonDom::Type::Null)) {
                    // unit 在 ird-report-json/1 字段镜像中恒在（缺席以 null
                    // 表达——结构化镜像与 CSV 空字段、HTML &mdash; 同义）。
                    failParseFailed(ReportRenderFormat::Json, "字段 unit 缺失或非字符串/null");
                }
                if (qualifier == nullptr || qualifier->type() != ReportJsonDom::Type::Array) {
                    failParseFailed(ReportRenderFormat::Json, "字段 qualifier 缺失或非数组");
                }
                ExtractedField extracted;
                extracted.anchored = true;
                extracted.value = value->stringValue();
                if (unit->type() == ReportJsonDom::Type::String) {
                    extracted.unit = unit->stringValue();
                }
                for (const ReportJsonDom& tokenDom : qualifier->arrayItems()) {
                    if (tokenDom.type() != ReportJsonDom::Type::String) {
                        failParseFailed(ReportRenderFormat::Json, "qualifier 元素非字符串");
                    }
                    extracted.qualifier.push_back(tokenDom.stringValue());
                }
                const auto inserted = out.emplace(fieldKey->stringValue(), std::move(extracted));
                if (!inserted.second) {
                    failParseFailed(ReportRenderFormat::Json,
                                    "fieldKey 重复：" + fieldKey->stringValue());
                }
            }
        }
    }
    return out;
}

// =====================================================================
// CSV 提取器（§8.5"CSV：io reader→行/列映射"——字节解析全在注入的
// io reader（NFR-SEC-03 转义唯一实现），本函数只做**列名→维度**映射与
// 结构取值；零 CSV 文法处理）
// =====================================================================

/**
 * @brief 从回读表集提取主表字段（首列 field_key 的表——Render.cpp
 *        csvMainColumns 冻结列序）。
 *
 * @param tables [in] 注入 reader 的回读表集（文档序）
 * @return fieldKey → 回读值映射
 *
 * @throws ReportError ConsistencyMismatch（主表缺失/列缺失/行列数不符/
 *         fieldKey 空——dimension=parse-failed；多值列出现空 token 视为
 *         转义层损坏，同轨）
 */
Extraction extractCsv(const std::vector<ReportCsvTable>& tables)
{
    // 主表定位（表头行判别表身份——Render.hpp CsvReportRenderer 类注的
    // 读取侧对偶：field_key/code/case_id 首列）。
    const ReportCsvTable* main = nullptr;
    for (const ReportCsvTable& table : tables) {
        if (!table.header.empty() && table.header.front() == "field_key") {
            main = &table;
            break;
        }
    }
    if (main == nullptr) {
        failParseFailed(ReportRenderFormat::Csv, "回读表集中缺 field_key 主表");
    }
    // 列名→列号映射（按名取列——列序冻结但映射不依赖位置，附表列序
    // 演进不影响主表读取路径）。
    const auto columnIndex = [&main](std::string_view name) -> std::size_t {
        for (std::size_t i = 0; i < main->header.size(); ++i) {
            if (main->header[i] == name) {
                return i;
            }
        }
        failParseFailed(ReportRenderFormat::Csv, std::string("主表缺列：") + std::string(name));
    };
    const std::size_t keyCol = columnIndex("field_key");
    const std::size_t valueCol = columnIndex("value");
    const std::size_t unitCol = columnIndex("unit");
    const std::size_t statusCol = columnIndex("status");
    const std::size_t qualifierCol = columnIndex("qualifier");
    const std::size_t resultRefCol = columnIndex("result_ref");
    const std::size_t evidenceRefCol = columnIndex("evidence_ref");

    // 多值列拆分（qualifier/evidence_ref——渲染侧 joinSemicolon 的逆）；
    // 空 token（";;"/";a" 形）＝转义层损坏——parse-failed 轨。
    const auto splitSemicolon = [](std::string_view joined) -> std::vector<std::string> {
        std::vector<std::string> parts;
        if (joined.empty()) {
            return parts;   // 空字段＝空集（渲染侧空集 join 得空串——逆面成立）
        }
        std::size_t start = 0;
        while (true) {
            const std::size_t sep = joined.find(';', start);
            const std::string_view part = sep == std::string_view::npos
                                              ? joined.substr(start)
                                              : joined.substr(start, sep - start);
            if (part.empty()) {
                failParseFailed(ReportRenderFormat::Csv, "多值列出现空 token（分号结构损坏）");
            }
            parts.emplace_back(part);
            if (sep == std::string_view::npos) {
                return parts;
            }
            start = sep + 1;
        }
    };

    Extraction out;
    for (const std::vector<std::string>& row : main->rows) {
        if (row.size() != main->header.size()) {
            // 行列数不符＝io reader 校验漏网的结构损坏（适配器契约：行
            // 字段数与表头一致）——拒绝而非容错补齐。
            failParseFailed(ReportRenderFormat::Csv, "主表行列数与表头不符");
        }
        const std::string& fieldKey = row[keyCol];
        if (fieldKey.empty()) {
            failParseFailed(ReportRenderFormat::Csv, "主表出现空 field_key 行");
        }
        ExtractedField extracted;
        extracted.anchored = true;
        extracted.value = row[valueCol];
        extracted.unit = row[unitCol].empty()
                             ? std::nullopt
                             : std::optional<std::string>(row[unitCol]);
        extracted.status = row[statusCol].empty()
                               ? std::nullopt
                               : std::optional<std::string>(row[statusCol]);
        extracted.qualifier = splitSemicolon(row[qualifierCol]);
        extracted.resultRef = row[resultRefCol].empty()
                                  ? std::nullopt
                                  : std::optional<std::string>(row[resultRefCol]);
        extracted.evidenceRef = splitSemicolon(row[evidenceRefCol]);
        const auto inserted = out.emplace(fieldKey, std::move(extracted));
        if (!inserted.second) {
            failParseFailed(ReportRenderFormat::Csv, "主表 field_key 重复：" + fieldKey);
        }
    }
    return out;
}

// =====================================================================
// 单字段七元组比对（§8.5 判定规则——维度覆盖见 Consistency.hpp 文件头）
// =====================================================================

/**
 * @brief 序列诊断文本（';' 连接——与 CSV 多值列同形，便于人读定位；
 *        空集→空串）。
 */
std::string joinForDiag(const std::vector<std::string>& parts)
{
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            joined += ';';
        }
        joined += parts[i];
    }
    return joined;
}

/**
 * @brief 比对单个矩阵单元格与单格式回读值（mismatch 不短路——逐维度
 *        全查全收）。
 *
 * 维度序（确定性）：value → unit → status → qualifier → ref。
 * 值比对为逐字符文本比对（valueRepr 已是 to_chars 文本规范形——非浮点
 * 再解析，D-09）。
 *
 * @param cell      [in] 矩阵单元格（源值——七元组基准）
 * @param format    [in] 工件格式（维度覆盖的分派依据）
 * @param extracted [in] 回读值（anchored=false 时仅产 missing 一条）
 * @param out       [out] mismatch 收集容器
 */
void compareField(const FieldCell& cell, ReportRenderFormat format,
                  const ExtractedField& extracted, std::vector<FieldMismatch>& out)
{
    // 无锚（HTML 锚被删的坏样本形态）——只产 missing，其余维度无对齐基准。
    if (!extracted.anchored) {
        addMismatch(out, format, cell.fieldKey, kMismatchDimMissing, cell.valueRepr, std::nullopt);
        return;
    }

    // ---- value（三格式全携带——全比对）----
    if (extracted.value.value_or(std::string()) != cell.valueRepr) {
        addMismatch(out, format, cell.fieldKey, kMismatchDimValue, cell.valueRepr, extracted.value);
    }

    // ---- unit（三格式全携带——全比对；缺席面各格式归一为 nullopt 后比对）----
    if (extracted.unit != cell.unit) {
        addMismatch(out, format, cell.fieldKey, kMismatchDimUnit, cell.unit, extracted.unit);
    }

    // ---- status（覆盖：CSV 原生 token／HTML 反查 token——共享映射表；
    //      JSON 字段镜像不携带 field 对齐 status，无回读值可比，跳过）----
    if (format != ReportRenderFormat::Json && extracted.status != cell.status) {
        addMismatch(out, format, cell.fieldKey, kMismatchDimStatus, cell.status, extracted.status);
    }

    // ---- qualifier（§6.4 硬约束——三格式全携带全比对，任何格式丢失＝
    //      mismatch；token 序比对：三格式均按矩阵推导序结构化输出，序
    //      差异同样反映呈现失真）----
    std::vector<std::string> sourceTokens;
    sourceTokens.reserve(cell.qualifier.size());
    for (const QualifierToken qualifier : cell.qualifier) {
        sourceTokens.emplace_back(token(qualifier));
    }
    if (extracted.qualifier != sourceTokens) {
        FieldMismatch m;
        m.format = format;
        m.fieldKey = cell.fieldKey;
        m.dimension = std::string(kMismatchDimQualifier);
        m.expected = joinForDiag(sourceTokens);
        m.actual = joinForDiag(extracted.qualifier);
        out.push_back(std::move(m));
    }

    // ---- ref（覆盖：仅 CSV 字段对齐携带引用列——resultRef/evidenceRef
    //      分别比对；HTML/JSON 的引用在追溯区块/顶层镜像，非字段对齐）----
    if (format == ReportRenderFormat::Csv) {
        if (extracted.resultRef != cell.resultRef) {
            addMismatch(out, format, cell.fieldKey, kMismatchDimRef, cell.resultRef,
                        extracted.resultRef);
        }
        if (extracted.evidenceRef != cell.evidenceRef) {
            // 引用集诊断形＝';' 连接文本（空集→空串——与"缺席"不同质：
            // 该维度有值面，只是值为空集）。
            addMismatch(out, format, cell.fieldKey, kMismatchDimRef,
                        joinForDiag(cell.evidenceRef), joinForDiag(extracted.evidenceRef));
        }
    }
}

}  // namespace

// =====================================================================
// FieldConsistencyChecker——产品实现（§9.4）
// =====================================================================

FieldConsistencyChecker::FieldConsistencyChecker(const IReportCsvReaderFactory& csvReaders) noexcept
    : m_csvReaders(&csvReaders)
{
}

ConsistencyResult FieldConsistencyChecker::check(const ConsistencyInput& input)
{
    // ---- 第一步：前置校验（调用方违约——Usage fail-fast，§3.5 错误
    //      语义"调用方错误 fail-fast"）----
    if (input.report == nullptr || input.matrix == nullptr) {
        throw ReportError(ReportErrorCode::Usage,
                          "一致性检查前置违约：report/matrix 指针为空（§9.4 ConsistencyInput 契约）");
    }
    if (!input.report->contentIdentity().isValid()) {
        // 与渲染链 validateRenderPreconditions 同判：非 make() 产出的
        // 报告没有冻结身份，同源校验失去基准——fail-fast。
        throw ReportError(ReportErrorCode::Usage,
                          "一致性检查前置违约：报告未冻结（contentIdentity 为零——ReviewReport::make 产出即非零）");
    }
    // 工件面：指针非空＋格式互异（§9.4 合法调用行"2~3 格式工件集"）＋
    // 与报告同源（RenderArtifact.sourceReportIdentity＝源报告内容身份——
    // RPT-T06 落位的同源锚，可廉价验证；矩阵↔报告同源按 §9.4 前置信任）。
    bool seen[3] = {false, false, false};   // 以 ReportRenderFormat 枚举序索引
    for (const RenderArtifact* artifact : input.artifacts) {
        if (artifact == nullptr) {
            throw ReportError(ReportErrorCode::Usage,
                              "一致性检查前置违约：artifacts 含空指针");
        }
        if (artifact->sourceReportIdentity != input.report->contentIdentity()) {
            throw ReportError(ReportErrorCode::Usage,
                              "一致性检查前置违约：工件与报告不同源（sourceReportIdentity 不符——§9.4 前置行）");
        }
        const std::size_t slot = static_cast<std::size_t>(artifact->format);
        if (slot < 3 && seen[slot]) {
            throw ReportError(ReportErrorCode::Usage,
                              "一致性检查前置违约：工件格式重复（" + std::string(token(artifact->format))
                                  + "）——合法调用为 2~3 个格式互异工件");
        }
        if (slot < 3) {
            seen[slot] = true;
        }
    }

    // ---- 第二步：结果骨架（fieldCount 恒为矩阵大小——比对宇宙）----
    ConsistencyResult result;
    result.fieldCount = input.matrix->size();

    // ---- 第三步：平凡通过（§9.4 ConsistencyInput 注释——artifacts<2
    //      无多格式一致性问题；注记承载于 ConsistencyResult.notes，偏差
    //      登记见 Consistency.hpp 文件头）----
    if (input.artifacts.size() < 2) {
        result.consistent = true;
        if (input.artifacts.empty()) {
            result.notes.push_back(
                "artifacts=0：无工件可比对——平凡通过（§9.4：artifacts<2 无多格式一致性问题），未执行逐字段比对");
        } else {
            result.notes.push_back(
                "artifacts=1（" + std::string(token(input.artifacts[0]->format))
                    + "）：单格式工件集——平凡通过（§9.4：artifacts<2 无多格式一致性问题），未执行逐字段比对");
        }
        return result;
    }

    // ---- 第四步：逐格式回读提取（§8.5 步④——每工件一遍，parse-failed
    //      轨在此抛出；提取序＝工件输入序，比对消费同序，mismatch 序确定）----
    std::vector<Extraction> extractions;
    extractions.reserve(input.artifacts.size());
    for (const RenderArtifact* artifact : input.artifacts) {
        switch (artifact->format) {
        case ReportRenderFormat::Html:
            extractions.push_back(extractHtml(artifact->bytes));
            break;
        case ReportRenderFormat::Json:
            extractions.push_back(extractJson(artifact->bytes));
            break;
        case ReportRenderFormat::Csv: {
            // CSV 回读经注入 reader（P-RPT-1——本函数零 CSV 字节解析，
            // NFR-SEC-03 转义唯一实现归 io；acceptance 5）。
            std::unique_ptr<IReportCsvReader> reader = m_csvReaders->makeCsvReader();
            std::vector<ReportCsvTable> tables;
            if (reader == nullptr
                || !reader->readTables(artifact->bytes.data(), artifact->bytes.size(), tables)) {
                failParseFailed(ReportRenderFormat::Csv,
                                "注入 CSV 读取器回读失败（格式损坏或适配缺位）");
            }
            extractions.push_back(extractCsv(tables));
            break;
        }
        }
    }

    // ---- 第五步：矩阵驱动比对（不短路——§9.4 后置行；序＝矩阵序→
    //      工件输入序→维度序，确定性 NFR-COR-02）----
    for (const FieldCell& cell : *input.matrix) {
        for (std::size_t i = 0; i < input.artifacts.size(); ++i) {
            const RenderArtifact* artifact = input.artifacts[i];
            const Extraction& extraction = extractions[i];
            const auto found = extraction.find(cell.fieldKey);
            if (found == extraction.end()) {
                // 缺失维度：该格式未输出该 fieldKey（§8.5 缺失字段行）。
                // expected 取源单元格值（定位诊断面），actual 为缺席标记。
                addMismatch(result.mismatches, artifact->format, cell.fieldKey,
                            kMismatchDimMissing, cell.valueRepr, std::nullopt);
                continue;
            }
            compareField(cell, artifact->format, found->second, result.mismatches);
        }
    }

    // ---- 第六步：结论汇总（一致＝mismatch 全空——§8.5 步⑤判定面）----
    result.consistent = result.mismatches.empty();
    return result;
}

}  // namespace sdurws::ird::reporting
