/**
 * @file   FormEditCommon.cpp
 * @brief  参数表与表单公共件实现（UI-T08 阶段 B）——显示投影（解析/
 *         格式化，core Units 唯一换算入口）、编辑会话模型（就地错误/
 *         筛选/批量粘贴/确认应用/取消恢复）与高级面板字段集。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T08 行（阶段 B 交付：表单公共件）、§14.1
 *     （阶段 B 承接：单位换算唯一入口 core::Quantity::displayValueIn
 *     已锁定）、§6.6（数值一律带单位显示）、§4.2（修改只进草稿，不就
 *     地写权威对象）、§16.7 v1.0（本文件登记行）；
 *   - 需求 UX-04/05/07、KIN-12；任务契约 tasks/foundation/UI-T08.json
 *     acceptance 1/2（公共编辑规则全量＋O-31 处置）；
 *   - 公共契约：include/sdurws/ird/ui/FormEditCommon.hpp（本实现的
 *     唯一权威声明——注释不重复，读规则先读头文件）。
 *
 * 实现要点（为什么这样做）：
 *   - 解析/格式化走 std::from_chars/std::to_chars：浮点文本与 locale
 *     无关（标准规定恒 '.' 小数点）——Qt 应用可能改设 locale，用
 *     locale 系接口会让"同一输入"在不同宿主解析出不同值（NFR-COR-02
 *     确定性）；core Units 的换算因子是编译期冻结表（SA-12 唯一入口），
 *     本文件不做任何第二实现点的单位换算；
 *   - "保留原值"不靠额外保存：值只有基线/暂存两层，非法输入路径根本
 *     不写这两层——结构上不可能改值，比"先存旧值失败再还原"少一个
 *     可错环节；
 *   - 本 TU 零 Qt include（模型层——PolicySummaryCard 的模型半区同案），
 *     模型测试（无 GUI）即可逐条验收公共编辑规则。
 *
 * O-31 处置（acceptance 2）：本 TU 仅 include core Units 与本单元公共
 * 头——对 project/evidence/execution/policy/runtime 零 include 零链接；
 * 守卫 NoCrossUnitInclude_O31_UI_BUILD（test/BuildRedLineTest.cpp）对
 * 本文件常驻自证。
 *
 * 线程模型：模型非线程安全（UI 线程）；纯函数（解析/格式化/字段集
 * 工厂）任意线程可调用。
 */

#include <sdurws/ird/ui/FormEditCommon.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

// ---------------------------------------------------------------------
// 文本小工具（ASCII 语义——键/数值文本不含本地化字符）
// ---------------------------------------------------------------------

/// 去首尾 ASCII 空白（空格/制表/回车/换行——单元格与粘贴行共用的
/// 规整口径；只动 ASCII，中文标签内容不受影响）。
std::string trimAscii(const std::string& text)
{
    const auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && isSpace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/// ASCII 大小写不敏感子串包含（筛选的匹配口径——对键与标签同口径）。
bool containsIgnoreCase(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;  // 空词匹配一切（筛选词为空的平凡情形）
    }
    const auto lower = [](const std::string& s) {
        std::string out(s);
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return out;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

/// 非有限值检测（NaN/±Inf——与 core Units Quantity 的 isFinite 同判定
/// 形态：NaN 不自等；±Inf 两倍自减不为零。不引 <limits> 依赖面）。
bool notFinite(double v) noexcept
{
    return !(v == v) || v - v != 0;
}

/// 显示数值的确定性文本化（6 位有效数字 general——与 PolicySummaryCard
/// 的 formatDisplayNumber 同案同精度：呈现投影非工程比较值，6 位足够
/// 辨识且换算误差不进入任何计算/身份；to_chars 与 locale 无关）。
std::string displayNumber(double value)
{
    char buffer[40] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general, 6);
    if (result.ec != std::errc()) {
        // 可达性：double 全值域在 40 字节内恒可容纳（general 格式）；
        // 命中属实现假设破坏——fail-fast 不出错误文本。
        throw std::logic_error("ui/form-edit: 显示数值格式化失败（缓冲假设破坏）");
    }
    return std::string{buffer, result.ptr};
}

/// 拼接越界原因（含实际范围——"超出允许范围 [min, max]（保留原值）"；
/// 范围数值按 SI 真值呈现，与约束定义同制式，避免二次换算引入歧义）。
std::string outOfRangeReason(const QuantityBounds& bounds)
{
    return std::string{kFieldOutOfRangeReasonPrefix} + "[" + displayNumber(bounds.minSi)
           + ", " + displayNumber(bounds.maxSi) + "]（保留原值）";
}

/// 拼接非数值原因（回显用户输入原文——就地定位"哪个输入被拒"；
/// 原文经 trim 后回显，避免首尾空白干扰判读）。
std::string notNumericReason(const std::string& trimmedText)
{
    return std::string{kFieldNotNumericReasonPrefix} + "\"" + trimmedText
           + "\"（保留原值）";
}

}  // namespace

// =====================================================================
// 字段描述：工厂校验（装配期 fail-fast——契约见头文件）
// =====================================================================

QuantityFieldSpec makeQuantityFieldSpec(std::string key, std::string label,
                                        core::QuantityKind kind,
                                        core::UnitToken siUnit,
                                        core::UnitToken displayUnit,
                                        std::optional<QuantityBounds> bounds,
                                        bool integerOnly)
{
    // 键/标签非空：键是行定位锚（批量粘贴/筛选/错误定位全靠它），标签
    // 是呈现必需——空值属装配错误，留到编辑期才炸会变成用户可见故障。
    if (key.empty()) {
        throw std::invalid_argument("ui/form-edit: 字段键不能为空");
    }
    if (label.empty()) {
        throw std::invalid_argument("ui/form-edit: 字段标签不能为空（key=" + key + "）");
    }
    // 单位有效性：无效 token（未经 find 取得）的 kind/siFactor 无意义，
    // 后续一切换算会建立在垃圾上——在装配边界拒绝。
    if (!siUnit.isValid() || !displayUnit.isValid()) {
        throw std::invalid_argument(
            "ui/form-edit: 单位 token 无效（须经 core::UnitToken::find 取得，key=" + key
            + "）");
    }
    // 量纲一致性：SI 单位与显示单位都必须和字段量纲同类——core::convert
    // 的量纲核对是运行期最后防线，本处提前到装配期（调用方错误尽早炸）。
    if (siUnit.kind() != kind) {
        throw std::invalid_argument(
            "ui/form-edit: SI 单位量纲与字段量纲不匹配（key=" + key + "）");
    }
    if (displayUnit.kind() != kind) {
        throw std::invalid_argument(
            "ui/form-edit: 显示单位量纲与字段量纲不匹配（key=" + key + "）");
    }
    QuantityFieldSpec spec;
    spec.key = std::move(key);
    spec.label = std::move(label);
    spec.kind = kind;
    spec.siUnit = siUnit;
    spec.displayUnit = displayUnit;
    spec.bounds = bounds;
    spec.integerOnly = integerOnly;
    return spec;
}

// =====================================================================
// 显示投影：格式化与解析（KIN-12 数值＋单位同显的唯一出口）
// =====================================================================

std::string formatFieldValueText(double siValue, core::UnitToken siUnit,
                                 core::UnitToken displayUnit)
{
    // SI 真值必须有限：显示非有限值即伪造数据（core Quantity::fromSi
    // 同口径拒绝）——调用方错误 fail-fast，不出"nan mm"这类显示。
    if (notFinite(siValue)) {
        throw std::logic_error("ui/form-edit: 显示值必须有限（NaN/±Inf 拒绝）");
    }
    if (!siUnit.isValid() || !displayUnit.isValid()) {
        throw std::logic_error("ui/form-edit: 单位 token 无效");
    }
    if (siUnit.kind() != displayUnit.kind()) {
        throw std::logic_error("ui/form-edit: 显示单位与 SI 单位量纲不匹配");
    }
    // 换算经 core Units（SI→显示一次乘除——SA-12 唯一入口；注册表因子
    // 编译期冻结，rad/deg 的 PI 精度换算随 core 的相对容差口径）。
    const double display = core::convert(siValue, siUnit, displayUnit);
    std::string text = displayNumber(display);
    // 单位符号（注册表 token 原文）与数值同显；无量纲冻结 token 为 "1"
    // ——工程惯例只显数值不带"1"后缀（PolicySummaryCard 同案）。
    const std::string symbol{displayUnit.symbol()};
    if (symbol != "1") {
        text += ' ';
        text += symbol;
    }
    return text;
}

ValueParseResult parseFieldValueText(const std::string& text,
                                     const QuantityFieldSpec& spec)
{
    ValueParseResult out;
    // 第 1 步：空输入（规整后）——空串是非法输入而非 0，就地拒绝。
    const std::string trimmed = trimAscii(text);
    if (trimmed.empty()) {
        out.reason = kFieldEmptyReason;
        return out;
    }
    // 第 2 步：浮点解析。std::from_chars（general）与 locale 无关，接受
    // 前导 '-' 与科学计数法，但不接受前导 '+'（标准规定）——工程输入
    // 常写 "+5"，剥掉单个前导 '+' 后再解析；须整串消费（"12abc" 不是
    // 数值，半截接受会静默丢尾——呈现层输入宁严勿松）。
    std::string numberText = trimmed;
    if (numberText.front() == '+') {
        numberText.erase(0, 1);
    }
    double display = 0.0;
    if (numberText.empty()) {
        out.reason = notNumericReason(trimmed);  // 原文只有 "+" 的退化情形
        return out;
    }
    const char* const first = numberText.data();
    const char* const last = first + numberText.size();
    const auto parsed = std::from_chars(first, last, display, std::chars_format::general);
    if (parsed.ec != std::errc() || parsed.ptr != last) {
        out.reason = notNumericReason(trimmed);
        return out;
    }
    // 第 3 步：非有限拒绝（from_chars 的 general 语法接受 "nan"/"inf"
    // 文本——SI 真值必须有限，不静默截断成有限数）。
    if (notFinite(display)) {
        out.reason = kFieldNotFiniteReason;
        return out;
    }
    // 第 4 步：整数约束（对显示值判定——用户输入什么核什么；迭代次数/
    // 种子这类计数按定义是整数，约束标注在字段 spec 上由宿主给定）。
    if (spec.integerOnly && std::trunc(display) != display) {
        out.reason = kFieldNotIntegerReason;
        return out;
    }
    // 第 5 步：显示单位 → SI 真值（core Units 唯一入口的 try 轨——工厂
    // 已保证量纲一致，nullopt 属实现假设破坏，fail-fast 不出错误真值）。
    const auto si = core::tryConvert(display, spec.displayUnit, spec.siUnit);
    if (!si.has_value()) {
        throw std::logic_error("ui/form-edit: 显示→SI 换算失败（量纲装配假设破坏）");
    }
    // 第 6 步：范围核对（SI 真值、闭区间——约束定义在 SI 语义上，判定
    // 必须换算回同一制式，避免"显示制式下看起来合法实际越界"）。
    if (spec.bounds.has_value()
        && (*si < spec.bounds->minSi || *si > spec.bounds->maxSi)) {
        out.reason = outOfRangeReason(*spec.bounds);
        return out;
    }
    out.ok = true;
    out.siValue = *si;
    return out;
}

// =====================================================================
// 编辑会话模型（ParamEditModel——公共编辑规则的无 Qt 承载）
// =====================================================================

ParamEditModel::ParamEditModel(std::vector<QuantityFieldSpec> specs)
{
    // 空表单无编辑语义（面板无处落行）——调用方装配错误 fail-fast。
    if (specs.empty()) {
        throw std::invalid_argument("ui/form-edit: 字段集不能为空");
    }
    // 键唯一性 O(n²)：表单规模是几十行量级，平方扫描最简且零分配；
    // 重复键会让批量粘贴/定位/状态查询歧义，必须在构造期拒绝。
    for (std::size_t i = 0; i < specs.size(); ++i) {
        for (std::size_t j = i + 1; j < specs.size(); ++j) {
            if (specs[i].key == specs[j].key) {
                throw std::invalid_argument("ui/form-edit: 字段键重复: " + specs[i].key);
            }
        }
    }
    m_specs = std::move(specs);
    // 编辑态与描述平行展开：初始基线未设、无暂存、无错误；显示单位取
    // 各字段 spec 的初始制式（KIN-12 显示投影的出厂态）。
    m_states.resize(m_specs.size());
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        m_states[i].displayUnit = m_specs[i].displayUnit;
    }
}

const QuantityFieldSpec& ParamEditModel::spec(const std::string& key) const
{
    const auto it = std::find_if(m_specs.begin(), m_specs.end(),
                                 [&key](const QuantityFieldSpec& s) {
                                     return s.key == key;
                                 });
    if (it == m_specs.end()) {
        // 未知键＝调用方错误（行定位键应来自本模型输出）——fail-fast，
        // 不返回哑引用把错误扩散到别处。
        throw std::out_of_range("ui/form-edit: 未知字段键: " + key);
    }
    return *it;
}

const ParamEditModel::FieldState& ParamEditModel::stateOf(const std::string& key) const
{
    const auto it = std::find_if(m_specs.begin(), m_specs.end(),
                                 [&key](const QuantityFieldSpec& s) {
                                     return s.key == key;
                                 });
    if (it == m_specs.end()) {
        throw std::out_of_range("ui/form-edit: 未知字段键: " + key);
    }
    return m_states[static_cast<std::size_t>(std::distance(m_specs.begin(), it))];
}

ParamEditModel::FieldState& ParamEditModel::stateOf(const std::string& key)
{
    // 非 const 版经 const 版复用（单一查找实现——避免两处查找逻辑漂移）。
    return const_cast<FieldState&>(static_cast<const ParamEditModel*>(this)->stateOf(key));
}

core::UnitToken ParamEditModel::displayUnit(const std::string& key) const
{
    return stateOf(key).displayUnit;
}

std::string ParamEditModel::displayValueText(const std::string& key) const
{
    const FieldState& st = stateOf(key);
    // 暂存优先、基线兜底：编辑会话中用户看到的是"自己改后的值"；未编辑
    // 时看到权威基线。就地错误不进入本读点——显示恒为最后一次有效值
    // （"保留原值"的呈现面）。
    std::optional<double> current = st.stagedSi.has_value() ? st.stagedSi : st.baselineSi;
    if (!current.has_value()) {
        return kFieldUnsetText;  // 未设占位——不伪造 0（§6.6 同案）
    }
    return formatFieldValueText(*current, spec(key).siUnit, st.displayUnit);
}

std::string ParamEditModel::displayNumberText(const std::string& key) const
{
    const FieldState& st = stateOf(key);
    // 与 displayValueText 同优先级（暂存优先、基线兜底；未设占位同文）。
    std::optional<double> current = st.stagedSi.has_value() ? st.stagedSi : st.baselineSi;
    if (!current.has_value()) {
        return kFieldUnsetText;
    }
    // 数值-only 投影：经 core Units 换算到当前显示制式后不带单位符号
    // （displayNumber 与 formatFieldValueText 的数值半区同源——同一
    // 精度同一确定性口径，面板"值｜单位"分列呈现时两列拼起来与
    // displayValueText 完全一致）。值由解析/基线边界保证有限。
    const double display = core::convert(*current, spec(key).siUnit, st.displayUnit);
    return displayNumber(display);
}

std::optional<double> ParamEditModel::currentValueSi(const std::string& key) const
{
    const FieldState& st = stateOf(key);
    return st.stagedSi.has_value() ? st.stagedSi : st.baselineSi;
}

std::string ParamEditModel::statusText(const std::string& key) const
{
    return stateOf(key).errorReason;
}

void ParamEditModel::setDisplayUnit(const std::string& key, core::UnitToken unit)
{
    if (!unit.isValid()) {
        throw std::invalid_argument("ui/form-edit: 显示单位 token 无效");
    }
    const QuantityFieldSpec& sp = spec(key);
    if (unit.kind() != sp.kind) {
        throw std::invalid_argument("ui/form-edit: 显示单位量纲与字段量纲不匹配（key=" + key
                                    + "）");
    }
    // 只切显示制式——基线/暂存/错误一字不动（KIN-12：切换仅影响显示，
    // 不触发重算、不产生修订；模型层面即"不产生脏标记"）。
    stateOf(key).displayUnit = unit;
}

void ParamEditModel::setDisplayUnitForKind(core::QuantityKind kind, core::UnitToken unit)
{
    if (!unit.isValid()) {
        throw std::invalid_argument("ui/form-edit: 显示单位 token 无效");
    }
    if (unit.kind() != kind) {
        throw std::invalid_argument("ui/form-edit: 显示单位量纲与目标量纲不匹配");
    }
    // 同量纲全部字段一次切换（"长度单位切到 mm"的单位切换控件语义）；
    // 其余量纲字段不受影响。
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        if (m_specs[i].kind == kind) {
            m_states[i].displayUnit = unit;
        }
    }
}

void ParamEditModel::setBaseline(const std::string& key, std::optional<double> siValue)
{
    if (siValue.has_value() && notFinite(*siValue)) {
        // 权威值非有限＝数据错误——不静默出"nan mm"显示，装配/回执边界拒绝。
        throw std::invalid_argument("ui/form-edit: 基线值必须有限（NaN/±Inf 拒绝）");
    }
    FieldState& st = stateOf(key);
    st.baselineSi = siValue;
    // 新权威到达后旧暂存/旧错误失据：一并清除，显示立即回到新基线
    // （否则会出现"显示与权威不一致但无暂存"的歧义态）。
    st.stagedSi.reset();
    st.errorReason.clear();
}

bool ParamEditModel::setEditText(const std::string& key, const std::string& text)
{
    FieldState& st = stateOf(key);
    const QuantityFieldSpec& sp = spec(key);
    const ValueParseResult parsed = parseFieldValueText(text, sp);
    if (!parsed.ok) {
        // 就地错误路径：只记录原因——基线/暂存一字不写（"保留原值"的
        // 结构保证；错误原因由呈现层经 statusText 就地显示）。
        st.errorReason = parsed.reason;
        return false;
    }
    // 成功路径：暂存新值（SI 真值）并清除该字段旧错误——新有效输入
    // 使旧错误失据。
    st.stagedSi = parsed.siValue;
    st.errorReason.clear();
    return true;
}

bool ParamEditModel::isDirty(const std::string& key) const
{
    const FieldState& st = stateOf(key);
    if (!st.stagedSi.has_value()) {
        return false;  // 无暂存＝未编辑
    }
    if (!st.baselineSi.has_value()) {
        return true;  // 基线未设→已设：从无到有也是修改
    }
    // 逐值精确比较：把同一值重新输入一遍不产生"待应用修改"（无效编辑
    // 不进确认交互——确认区只呈现真实变更）。
    return *st.stagedSi != *st.baselineSi;
}

std::vector<std::string> ParamEditModel::dirtyKeys() const
{
    std::vector<std::string> keys;
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        if (isDirty(m_specs[i].key)) {
            keys.push_back(m_specs[i].key);
        }
    }
    return keys;
}

std::vector<ParamChange> ParamEditModel::pendingChanges() const
{
    std::vector<ParamChange> changes;
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        const FieldState& st = m_states[i];
        if (!st.stagedSi.has_value()) {
            continue;  // 无暂存＝无修改
        }
        if (st.baselineSi.has_value() && *st.baselineSi == *st.stagedSi) {
            continue;  // 与基线同值＝无效编辑（isDirty 同口径）
        }
        ParamChange change;
        change.key = m_specs[i].key;
        change.label = m_specs[i].label;
        change.oldSi = st.baselineSi;
        change.newSi = *st.stagedSi;
        changes.push_back(std::move(change));
    }
    // 注册序输出——确认区/影响明细/移交载荷的稳定呈现序（NFR-COR-02）。
    return changes;
}

bool ParamEditModel::hasErrors() const noexcept
{
    for (const auto& st : m_states) {
        if (!st.errorReason.empty()) {
            return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> ParamEditModel::errors() const
{
    std::vector<std::pair<std::string, std::string>> all;
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        if (!m_states[i].errorReason.empty()) {
            all.emplace_back(m_specs[i].key, m_states[i].errorReason);
        }
    }
    return all;
}

std::optional<std::string> ParamEditModel::firstErrorKey() const
{
    // "首个"＝注册序最先——错误定位行为确定（同一组错误恒定位同一格，
    // NFR-COR-02；呈现层据此 setCurrentCell+scrollTo）。
    for (std::size_t i = 0; i < m_specs.size(); ++i) {
        if (!m_states[i].errorReason.empty()) {
            return m_specs[i].key;
        }
    }
    return std::nullopt;
}

std::vector<std::string> ParamEditModel::visibleKeys() const
{
    std::vector<std::string> keys;
    for (const auto& sp : m_specs) {
        // 键或标签命中即可见（ASCII 大小写不敏感子串——工程键与中文
        // 标签同口径检索）；筛选是纯可见性裁剪，不改任何编辑态。
        if (containsIgnoreCase(sp.key, m_filter) || containsIgnoreCase(sp.label, m_filter)) {
            keys.push_back(sp.key);
        }
    }
    return keys;
}

BatchPasteReport ParamEditModel::batchPasteText(const std::string& text)
{
    BatchPasteReport report;
    std::vector<std::string> touchedKeys;  // 本次粘贴实际接受的键（影响明细的筛选面）
    std::size_t lineNo = 0;
    std::size_t pos = 0;
    // 逐行扫描：'\n' 分行（容忍行尾 '\r'——Windows 剪贴板常见形态）；
    // 末行无换行符也消费。空行/纯空白行跳过且不计入报告（复制粘贴的
    // 常见尾空行不当作错误——UX-05 的批量入口以"能贴的贴上"为先）。
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string line =
            text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        ++lineNo;

        const std::string trimmedLine = trimAscii(line);
        if (trimmedLine.empty()) {
            continue;  // 空行：跳过不计数
        }
        // 分隔符：TAB 或逗号取先出现者（表格复制的天然形态是 TAB；
        // 逗号照顾手工整理的清单）。都没有→整行拒绝并给文法提示。
        const std::size_t tab = trimmedLine.find('\t');
        const std::size_t comma = trimmedLine.find(',');
        const std::size_t sep =
            (tab != std::string::npos && comma != std::string::npos)
                ? std::min(tab, comma)
                : (tab != std::string::npos ? tab : comma);

        BatchPasteLineResult row;
        row.line = lineNo;
        if (sep == std::string::npos) {
            row.reason = "缺分隔符（应为「键<TAB或逗号>值」）";
            report.lines.push_back(std::move(row));
            ++report.rejectedCount;
            continue;
        }
        const std::string key = trimAscii(trimmedLine.substr(0, sep));
        const std::string value = trimAscii(trimmedLine.substr(sep + 1));
        row.key = key;
        // 键必须已知：未知键整行拒绝（不影响其他行——批量独立的判定粒度）。
        if (std::none_of(m_specs.begin(), m_specs.end(),
                         [&key](const QuantityFieldSpec& s) { return s.key == key; })) {
            row.reason = "未知参数：" + key;
            report.lines.push_back(std::move(row));
            ++report.rejectedCount;
            continue;
        }
        // 值判定与单格编辑同一条规则（parseFieldValueText——非法值的
        // 就地原因与单格编辑完全同源）；该字段原值保留，其他行继续。
        if (setEditText(key, value)) {
            row.accepted = true;
            report.lines.push_back(std::move(row));
            ++report.acceptedCount;
            touchedKeys.push_back(key);
        } else {
            row.reason = statusText(key);
            report.lines.push_back(std::move(row));
            ++report.rejectedCount;
        }
    }
    // 影响明细（"批量影响明细"验收点）：本次粘贴涉及键的当前待应用修改
    // ——同键被多行粘贴时暂存已以后行为准，明细呈现最终态而非逐行历史；
    // 与基线同值的接受行（无效编辑）自然不入明细。
    const std::vector<ParamChange> changes = pendingChanges();
    for (const auto& change : changes) {
        if (std::find(touchedKeys.begin(), touchedKeys.end(), change.key)
            != touchedKeys.end()) {
            report.impact.push_back(change);
        }
    }
    return report;
}

ConfirmApplyResult ParamEditModel::confirmApply(IFormEditOutlet& outlet)
{
    ConfirmApplyResult result;
    // 前置 a：表单级无错误——存在就地错误时拒绝应用并拼首个错误定位
    // （部分应用会制造"看似成功实际残缺"的状态；错误定位文案让用户
    // 知道先去修哪格）。
    if (hasErrors()) {
        result.reason = std::string{kApplyHasErrorsPrefix} + statusText(*firstErrorKey());
        return result;
    }
    // 前置 b：存在实际修改——无修改不做无效应用（就地反馈，不弹窗）。
    const std::vector<ParamChange> changes = pendingChanges();
    if (changes.empty()) {
        result.reason = kApplyNoChangesText;
        return result;
    }
    // 移交：确认过的修改集交域编辑器（转译领域命令→①命令端口——硬
    // 断言与放行归 project 命令边界，§9.2；本模型不做任何业务判定）。
    ParamEditSet editSet;
    editSet.changes = changes;
    outlet.applyEdits(editSet);
    // 移交即表单侧完成：基线推进到新值并清涉事字段的暂存/错误——表单
    // 与权威重新对齐；后续应用回执失败由域消费者经 setBaseline 修正
    // （§8.5 应用回执的协作面在域编辑器，不在本公共件）。
    for (const auto& change : changes) {
        FieldState& st = stateOf(change.key);
        st.baselineSi = change.newSi;
        st.stagedSi.reset();
        st.errorReason.clear();
    }
    result.ok = true;
    return result;
}

void ParamEditModel::cancelRestore()
{
    // 恢复＝丢弃整个暂存层：基线在应用成功前恒不变，清掉暂存与错误后
    // 显示自然回到基线。单位制式与筛选不回滚（会话显示设置与修改集
    // 正交——KIN-12/UX-07"仅改变会话显示的操作不是编辑"）。
    for (auto& st : m_states) {
        st.stagedSi.reset();
        st.errorReason.clear();
    }
}

// =====================================================================
// 高级面板字段集（UX-04 承载——宿主字段注册的参考集）
// =====================================================================

std::vector<QuantityFieldSpec> advancedPanelDefaultFields()
{
    // 三字段全为计数类（无量纲＋仅整数）：迭代次数/采样点数/随机种子按
    // 定义是整数计数——这是宿主形态约束，不是 ui 发明的业务阈值；基线
    // 一律未设，域消费者经 setBaseline 注入真实权威值（零默认数值＝
    // 不虚构业务能力，§11.4）。"开发诊断"是会话级显示开关，不属数值
    // 字段——归面板选项（ParamTablePanelOptions.showDevDiagnosticsToggle）。
    const auto dimensionless = core::UnitToken::find("1");
    if (!dimensionless.has_value()) {
        // core 单位注册表为进程级静态注册，"1" 是冻结条目——find 失败
        // 属构建期事实破坏（不可达），fail-fast 不出空 token。
        throw std::logic_error("ui/form-edit: core 单位注册表缺少无量纲 \"1\"（构建期事实破坏）");
    }
    std::vector<QuantityFieldSpec> fields;
    fields.push_back(makeQuantityFieldSpec("solver-max-iterations", "求解器最大迭代次数",
                                           core::QuantityKind::Dimensionless, *dimensionless,
                                           *dimensionless, std::nullopt, true));
    fields.push_back(makeQuantityFieldSpec("sampling-points", "采样点数",
                                           core::QuantityKind::Dimensionless, *dimensionless,
                                           *dimensionless, std::nullopt, true));
    fields.push_back(makeQuantityFieldSpec("random-seed", "随机种子",
                                           core::QuantityKind::Dimensionless, *dimensionless,
                                           *dimensionless, std::nullopt, true));
    return fields;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
