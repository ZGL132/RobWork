/**
 * @file   FormEditCommon.hpp
 * @brief  参数表与表单公共件（UI-T08 阶段 B 交付面）——UX-04/05/07 公共
 *         编辑规则的唯一承载：数值＋单位同显、非法输入就地显示原因保留
 *         原值、表单级确认应用、批量粘贴/筛选/错误定位、取消恢复。
 *
 * 设计依据：
 *   - 需求 UX-04（高级面板承载求解器/采样/随机种子/开发诊断）、UX-05
 *     （参数表批量粘贴、筛选、单位显示、错误定位——不用模态对话框做大量
 *     重复编辑）、UX-07（表单级应用设计修改时确认；仅改变会话显示的
 *     操作不弹出保存或冻结提示）、KIN-12（显示单位只是投影——切换仅
 *     影响显示与导出格式，不改变 SI 真值、不产生修订、不触发重算）；
 *   - units/ui.md §4.2（右栏/属性编辑红线：修改只能进草稿，不得就地写
 *     权威对象）、§4.5/§4.6（单位显示是会话/用户级显示设置——与计算
 *     策略严格分离，默认 m/rad）、§6.6（参数中的数值一律带单位显示——
 *     core Quantity::displayValueIn 同一换算入口）、§13 UI-T08 行
 *     （阶段 B 交付：表单公共件）、§14.1（阶段 B 承接：单位换算唯一
 *     入口 core::Quantity::displayValueIn 已锁定）、§3.3（本头落位行，
 *     §16.7 v1.0 登记）；
 *   - 任务契约 tasks/foundation/UI-T08.json acceptance 1（公共编辑规则
 *     全量登记——本头即其代码承载）＋acceptance 2（O-31 处置：本交付物
 *     不持有 C-3/4/5/7/8/10/11 对端类型——单位显示走已链接的 core
 *     Units 显示投影；草稿接入经 ui 自有 IDraftController.attachModule
 *     接口，本头只定义域编辑器实现的最小编辑出口 IFormEditOutlet）；
 *   - 同构先例：PolicySummaryCard.hpp（UI-T07——模型层纯函数＋文案单点
 *     ＋GUI 渲染层分工；单位换算经 core Units；零 Q_OBJECT）。
 *
 * 背景说明（为什么表单公共件要分层成"模型规则＋面板交互"两半）：
 *   UX-04/05/07 的验收点是**编辑规则本身**（同显/就地错误/确认/恢复/
 *   批量明细），不是某个具体控件。把规则收口在无 Qt 的模型层
 *   （ParamEditModel），域消费者（WP-13-T15/WP-14-T08/WP-15-T12 等
 *   插件界面）就能以同一套规则接入不同宿主；Qt 面板
 *   （createParamTablePanel）只是规则的第一渲染面，GUI 与模型测试对
 *   同一规则互证（PolicySummaryCard 同案）。分层同时把 UX-05"不用模态
 *   对话框"做成结构事实：所有编辑就地发生在表格单元格里，确认是面板
 *   内嵌的非模态区域——不存在第二编辑形态。
 *
 * O-31 处置（acceptance 2，代码面边界）：
 *   - 单位显示与换算只消费 core Units（UnitToken/convert/tryConvert——
 *     ui→core 是 ARCH §3.5 表内登记边；SA-12 唯一换算入口）；
 *   - 草稿/命令协作面只见 ui 自有值类型（ParamEditSet）与 ui 自有最小
 *     端口（IFormEditOutlet——由域编辑器实现；域编辑器经
 *     IDraftController::attachModule 接入的阶段 B 装配路径，本头不
 *     include 任何对端公共头）；
 *   - 断言与放行归 project：本件只做"呈现层输入合法性"（可解析/整数/
 *     范围），不做任何业务判定；确认应用的语义是"用户已确认这批修改，
 *     经编辑出口移交域编辑器"，硬断言与放行在 project 命令边界
 *     （§9.2——ui 不持有判定权、不自行放行命令）。
 *
 * 线程模型：ParamEditModel 非线程安全——仅 UI 线程访问（§3.4 M-1：
 * 表单编辑是 UI 线程交互面）；纯函数（解析/格式化/字段集工厂）任意
 * 线程可调用。IFormEditOutlet::applyEdits 由 UI 线程调用，实现方自行
 * 保证后续命令提交的线程编排（归域编辑器/命令端口）。
 */

#ifndef SDURWS_IRD_UI_FORMEDITCOMMON_HPP
#define SDURWS_IRD_UI_FORMEDITCOMMON_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Units.hpp>   // core::UnitToken/QuantityKind/convert/tryConvert（显示投影——表内登记边）

class QWidget;  // 前置声明：面板工厂返回类型；头文件不拖入 Widgets（消费者按需自含——IWorkbenchShell.hpp 同款）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 冻结文案（公共编辑规则的呈现文案单点——模型测试与 GUI 同源消费）
// =====================================================================

/// 未设值的呈现文案（基线未注入/字段显式为空——不伪造 0，§6.6"不适用
/// 字段显示占位不伪造 0"同案；数值字段未设与 0 是两个语义）。
inline constexpr const char* kFieldUnsetText = "未设";

/// 空输入的就地错误原因（UX-05：非法输入就地显示原因——空串是非法输入
/// 的一种，不静默当作 0 或跳过；括号注明原值未被改动）。
inline constexpr const char* kFieldEmptyReason = "值不能为空（保留原值）";

/// 非数值输入的就地错误原因前缀（拼接形如：不是有效数值："abc"（保留原值）
/// ——把原文回显给用户，错误定位更直接；引号内为用户输入原文）。
inline constexpr const char* kFieldNotNumericReasonPrefix = "不是有效数值：";

/// 非有限值（nan/inf 文本）的就地错误原因——SI 真值必须有限（core Units
/// Quantity::fromSi 同口径拒绝），不静默截断成有限数。
inline constexpr const char* kFieldNotFiniteReason = "值必须有限（保留原值）";

/// 整数约束的就地错误原因（随机种子/迭代次数等计数类字段——宿主约束
/// 标注在字段 spec 上，非 ui 发明的业务规则）。
inline constexpr const char* kFieldNotIntegerReason = "必须为整数（保留原值）";

/// 越界（spec 声明了允许范围）的就地错误原因前缀（拼接实际范围——
/// "超出允许范围 [1, 100]（保留原值）"，范围值按 SI 真值呈现）。
inline constexpr const char* kFieldOutOfRangeReasonPrefix = "超出允许范围 ";

/// 确认区提示前缀（UX-07 应用前确认交互的提示文案首段；后续逐行拼
/// "标签：旧值→新值 单位"的比较型明细——与 §9.2 确认对话"比较型三要素"
/// 同风格的表单级轻量呈现）。
inline constexpr const char* kConfirmPromptPrefix = "将应用以下修改：";

/// 无待应用修改时点"应用"的就地反馈（不做无效确认——表单级应用的
/// 前置是存在实际变更）。
inline constexpr const char* kApplyNoChangesText = "无待应用修改";

/// 存在未解决非法输入时点"应用"的就地反馈前缀（拼接首个错误定位——
/// 非法输入未修复前不允许进入确认交互，UX-05 错误定位的触发面之一）。
inline constexpr const char* kApplyHasErrorsPrefix = "存在非法输入，请先修复：";

/// 应用完成的就地反馈前缀（拼接"已应用修改，共 N 项"＋后缀；明示语义
/// 边界：编辑出口只接收移交——硬断言与放行归 project 命令边界，§9.2
/// "ui 不自行放行命令"；防止把"表单已应用"误读成"修订已生成"）。
inline constexpr const char* kApplyHandedOffPrefix = "已应用修改，共 ";

/// 应用完成的就地反馈后缀（与 kApplyHandedOffPrefix 拼接使用——见上）。
inline constexpr const char* kApplyHandedOffSuffix =
    "项（已移交编辑出口——断言与放行归 project 命令边界）";

/// 取消恢复的就地反馈（取消按钮的完成文案——"恢复原值"与模型行为
/// 一致：基线在应用成功前恒不变，取消只丢弃暂存层）。
inline constexpr const char* kCancelRestoredText = "已取消修改，恢复原值";

/// 未接入编辑出口时"应用"按钮的禁用提示（tooltip——域编辑器未装配时
/// 不虚构可用性，§11.4 不虚构业务能力同案）。
inline constexpr const char* kNoOutletTooltip = "未接入编辑出口（域编辑器），无法应用";

// =====================================================================
// 字段描述（QuantityFieldSpec——表单字段的宿主契约）
// =====================================================================

/// 字段允许范围（SI 真值、闭区间；nullopt＝不设范围约束）。
struct QuantityBounds {
    /// 允许下界（SI 真值，单位 siUnit；闭——等于下界合法）。
    double minSi = 0.0;
    /// 允许上界（SI 真值，单位 siUnit；闭——等于上界合法）。
    double maxSi = 0.0;
};

/**
 * @brief 表单数值字段的宿主描述（键/标签/量纲/单位/约束——纯值）。
 *
 * 语义分工：key/label/kind/siUnit/displayUnit/约束由**域消费者**在装配
 * 期给定（ui 是宿主不是权威——字段语义属于域单元；ui 只按本描述执行
 * 公共编辑规则）；displayUnit 是初始显示单位（KIN-12 显示投影——仅
 * 影响呈现，可被 setDisplayUnitForKind 运行期切换）。
 *
 * 约束：siUnit/displayUnit 必须经 core::UnitToken::find 取得（注册表
 * token）且量纲等于 kind——违约属调用方装配错误，由 makeQuantityFieldSpec
 * fail-fast（std::invalid_argument），不静默出错误换算。
 */
struct QuantityFieldSpec {
    /// 字段稳定键（行定位锚；面板 objectName 与批量粘贴的键都基于它；
    /// 建议小写连字符词法——与 §6.3 token 同风格，模型不强制词法）。
    std::string key;
    /// 字段标签（工程用语呈现文案——UX-02；中文，域消费者给定）。
    std::string label;
    /// 量纲类别（类型层锚——单位量纲核对与 KIN-12 显示切换分组依据）。
    core::QuantityKind kind = core::QuantityKind::Dimensionless;
    /// SI 单位 token（真值锚；量纲须等于 kind——core 单位表每个量纲
    /// 恰有一个 factor=1 的 SI 行）。
    core::UnitToken siUnit{};
    /// 初始显示单位 token（KIN-12 显示投影；量纲须等于 kind）。
    core::UnitToken displayUnit{};
    /// 允许范围（SI 闭区间；nullopt＝不设范围——呈现层不做范围暗示）。
    std::optional<QuantityBounds> bounds{};
    /// 仅接受整数值（对显示值判定——用户输入什么就核什么；随机种子/
    /// 迭代次数等计数类字段置 true）。
    bool integerOnly = false;
};

/**
 * @brief 构造并校验字段描述（spec 装配的唯一入口——把"单位量纲一致"
 *        的调用方契约前置到装配期失败，而不是留到编辑期出错误换算）。
 *
 * @param key         [in] 字段稳定键（非空——空键无法定位行）
 * @param label       [in] 字段标签（非空——空标签无法呈现）
 * @param kind        [in] 量纲类别
 * @param siUnit      [in] SI 单位 token（须有效且 kind 匹配）
 * @param displayUnit [in] 初始显示单位 token（须有效且 kind 匹配）
 * @param bounds      [in] 允许范围（可空——不设范围）
 * @param integerOnly [in] 是否仅接受整数值（默认 false）
 * @return 校验通过的字段描述
 *
 * @throws std::invalid_argument key/label 为空，或 siUnit/displayUnit
 *         无效（未经 find 取得），或其量纲与 kind 不匹配
 */
QuantityFieldSpec makeQuantityFieldSpec(std::string key, std::string label,
                                        core::QuantityKind kind,
                                        core::UnitToken siUnit,
                                        core::UnitToken displayUnit,
                                        std::optional<QuantityBounds> bounds = std::nullopt,
                                        bool integerOnly = false);

// =====================================================================
// 显示投影（数值＋单位同显的唯一格式化/解析出口——KIN-12）
// =====================================================================

/**
 * @brief 数值文本解析结果（就地错误的数据载体）。
 *
 * ok==false 时 reason 是面向用户的就地错误原因（含"（保留原值）"后缀
 * ——原值确实未被改动，文案与行为一致）；siValue 仅在 ok==true 时有效
 * （SI 真值——已按字段显示单位换算回 SI）。
 */
struct ValueParseResult {
    /// 解析是否成功（false＝就地显示 reason、保留原值）。
    bool ok = false;
    /// SI 真值（ok==true 时有效；显示值×显示单位因子——core Units 唯一
    /// 换算入口的产出）。
    double siValue = 0.0;
    /// 就地错误原因（ok==false 时非空；ok==true 为空串）。
    std::string reason;
};

/**
 * @brief SI 真值 → "数值 单位"显示文本（数值＋单位同显的唯一格式化点）。
 *
 * 数值按 6 位有效数字 general 格式（呈现投影精度——与 PolicySummaryCard
 * 同案：SI 真值恒为唯一权威，显示误差不进入任何计算/身份）；换算经
 * core::convert（SA-12 唯一入口，SI→显示一次除法）；单位符号直用 core
 * 注册表 token 原文；无量纲（token "1"）只显数值不带"1"后缀（工程惯例
 * ——PolicySummaryCard thresholdValueText 同案）。确定性：std::to_chars
 * 浮点格式化与 locale 无关（NFR-COR-02——Qt 应用可能改设 locale，不用
 * locale 系格式化）。
 *
 * @param siValue     [in] SI 真值（须有限——违约属调用方错误，抛
 *                    std::logic_error；显示非有限值即伪造数据）
 * @param siUnit      [in] 该字段量纲的 SI 单位 token（与 spec.siUnit 同源；
 *                    core Units 换算接口需要换算两端——同量纲核对在此
 *                    fail-fast，量纲不匹配不出错误数值）
 * @param displayUnit [in] 显示单位 token（须有效且与 siUnit 同量纲）
 * @return "300 mm"/"0.3 m"/"500" 形态的显示文本
 *
 * @throws std::logic_error siValue 非有限，或 siUnit/displayUnit 无效，
 *         或两者量纲不匹配（调用方错误——fail-fast）
 */
std::string formatFieldValueText(double siValue, core::UnitToken siUnit,
                                 core::UnitToken displayUnit);

/**
 * @brief 用户输入文本 → SI 真值（就地错误的唯一判定点——UX-05）。
 *
 * 判定序（每步失败即返回原因、不继续）：
 *   1. 去首尾 ASCII 空白后为空 → kFieldEmptyReason；
 *   2. 按 general 浮点语法解析（std::from_chars，locale 无关；接受
 *      前导正负号与科学计数法；须整串消费——"12abc" 不是数值）→
 *      kFieldNotNumericReasonPrefix＋原文；
 *   3. 非有限（nan/inf 文本）→ kFieldNotFiniteReason；
 *   4. integerOnly 字段要求显示值为整数（3.5 拒绝、3.0 接受）→
 *      kFieldNotIntegerReason；
 *   5. spec 声明了范围时按 SI 真值核对闭区间 → 越界原因（拼实际范围）。
 * 通过后经 core::tryConvert（显示单位→SI 单位，唯一换算入口）得 SI
 * 真值——换算失败（量纲装配错误）属调用方错误，fail-fast。
 *
 * 注意：本判定只覆盖**呈现层输入合法性**（UX-05"非法输入"的字面口径
 * ——格式/类型/宿主约束），不是业务校验；业务断言归 project 命令边界
 * （acceptance 1"断言与放行归 project"）。
 *
 * @param text [in] 用户输入原文（就地错误原因会回显原文）
 * @param spec [in] 字段描述（显示单位/整数约束/范围——单位量纲与 kind
 *             的一致性由 makeQuantityFieldSpec 装配期保证）
 * @return 解析结果（ok==false 时 reason 非空且原值语义未被触碰——本
 *         函数是纯函数，"保留原值"由调用方（模型）不写回实现）
 */
ValueParseResult parseFieldValueText(const std::string& text,
                                     const QuantityFieldSpec& spec);

// =====================================================================
// 编辑会话模型（ParamEditModel——公共编辑规则的无 Qt 承载）
// =====================================================================

/**
 * @brief 单字段修改明细（确认区呈现与编辑出口移交的最小数据元）。
 *
 * oldSi 取基线值（未设基线时 nullopt——"从未设"→"设为"的呈现语义）；
 * newSi 恒有值（staged 解析成功才成 Change）。数值一律 SI 真值——显示
 * 制式是投影不进移交（KIN-12：接收方拿到的永远是 SI 语义）。
 */
struct ParamChange {
    /// 字段稳定键（spec.key 原文）。
    std::string key;
    /// 字段标签（呈现用——确认区/影响明细直接显示，域消费者可读）。
    std::string label;
    /// 修改前基线值（nullopt＝原未设；有值＝SI 真值）。
    std::optional<double> oldSi;
    /// 修改后值（SI 真值）。
    double newSi = 0.0;
};

/// 确认应用的移交载荷（ui 自有值类型——域编辑器把它转译为领域命令经
/// ①命令端口提交；本类型不携带也不引用任何对端类型——O-31）。
struct ParamEditSet {
    /// 逐字段修改明细（键序＝字段注册序——稳定呈现，NFR-COR-02）。
    std::vector<ParamChange> changes;
};

/**
 * @brief 表单级确认应用的结果（就地反馈的数据载体）。
 *
 * ok==false 时 reason 说明拒绝原因（无修改/存在非法输入）——呈现层
 * 据此就地显示，不弹模态提示（UX-05）。
 */
struct ConfirmApplyResult {
    /// 是否已移交编辑出口（true＝用户确认的修改集已交域编辑器）。
    bool ok = false;
    /// 拒绝原因（ok==false 时非空；ok==true 为空串）。
    std::string reason;
};

/**
 * @brief 批量粘贴行结果（逐行接受/拒绝＋原因——批量影响明细的行级数据）。
 */
struct BatchPasteLineResult {
    /// 行号（1 起——按 '\n' 切分；空行被跳过不计数，见 batchPasteText）。
    std::size_t line = 0;
    /// 行首键原文（解析出键即填，含未知键——定位"哪行键错了"）。
    std::string key;
    /// 该行是否被接受（接受＝已暂存为该字段的待应用值）。
    bool accepted = false;
    /// 拒绝原因（accepted==false 时非空——未知键/缺分隔符/值非法）。
    std::string reason;
};

/**
 * @brief 批量粘贴报告（UX-05 批量粘贴＋"批量影响明细"验收点的数据面）。
 *
 * 明细两层：lines 逐行结果（接受/拒绝与原因——错误定位的行级锚）；
 * impact 是本次粘贴实际造成的待应用修改集（按受影响键的当前暂存值对
 * 基线计算——同一键重复粘贴只留最后一行，影响以最终态为准）。
 */
struct BatchPasteReport {
    /// 逐行结果（源序＝粘贴文本行序；跳过的空行不计入）。
    std::vector<BatchPasteLineResult> lines;
    /// 接受行数（便于呈现"接受 x 项"摘要）。
    std::size_t acceptedCount = 0;
    /// 拒绝行数（呈现"拒绝 y 项"摘要；明细看 lines）。
    std::size_t rejectedCount = 0;
    /// 本次粘贴涉及字段的待应用修改明细（键序＝字段注册序）。
    std::vector<ParamChange> impact;
};

/**
 * @brief 编辑出口端口（ui 自有最小接口——域编辑器实现的移交面）。
 *
 * 角色与边界（O-31 处置／acceptance 2）：表单公共件确认应用后把
 * ParamEditSet 移交给本出口；实现方＝域编辑器（阶段 B 域消费者模块，
 * 经 IDraftController::attachModule 接入草稿体系），由它把修改转译为
 * 领域命令经①命令端口提交——**硬断言与放行归 project 命令边界**
 * （§9.2），本接口不做也做不了任何业务判定。ui 产品面对
 * project/evidence/... 零类型依赖（对端协作一律运行时注入——§3.1）。
 *
 * 生命周期：实现方（域编辑器模块）持有；表单面板只存裸指针——调用方
 * 保证出口存活期覆盖面板存活期（面板不接管所有权——§2.5 所有权注明）。
 *
 * 线程：applyEdits 仅 UI 线程调用（§3.4 M-1）。
 */
class IFormEditOutlet {
public:
    virtual ~IFormEditOutlet() = default;

    /**
     * @brief 接收用户确认的修改集（表单级确认应用的移交步）。
     *
     * @param editSet [in] 修改集（SI 真值；值语义拷贝，实现方可持有）。
     *                非空——确认应用前置已保证无修改不进入本步。
     *
     * 实现方约束：不得在本调用内同步做长任务（UI 线程——命令提交经
     * 命令端口的异步语义）；失败反馈经命令结果回执（§8.5）而非本接口
     * 返回值（移交即表单侧完成——应用成败的权威在命令边界）。
     */
    virtual void applyEdits(const ParamEditSet& editSet) = 0;
};

/**
 * @brief 表单字段的编辑会话模型（公共编辑规则的唯一规则承载——无 Qt）。
 *
 * 三层值语义（所有规则的词汇表）：
 *   - 基线（baseline）：字段当前权威值（SI；域消费者经 setBaseline 注入，
 *     表单打开/应用成功后与权威对齐）；
 *   - 暂存（staged）：用户已编辑并解析成功的待应用值（每字段至多一个
 *     ——再次编辑覆盖）；显示文本恒取暂存优先、基线兜底；
 *   - 就地错误（error reason）：最后一次非法输入的原因——只描述文本，
 *     不改动任何值（"保留原值"的机制面：错误态不触碰基线/暂存）。
 *
 * 单位制式是第四个正交维度：displayUnit 仅影响显示投影（KIN-12——
 * 切换不触碰基线/暂存/错误态，也不产生脏标记，UX-07"仅改变会话显示
 * 的操作不弹出保存提示"）。
 *
 * 线程安全：非线程安全——仅 UI 线程访问（表单交互面）。
 */
class ParamEditModel {
public:
    /**
     * @brief 由字段描述集构造（字段注册序即全部稳定呈现序）。
     *
     * @param specs [in] 字段描述集（非空且键唯一——空表单/重复键是调用
     *              方装配错误，fail-fast；描述经 makeQuantityFieldSpec
     *              构造即满足单位一致性）
     *
     * @throws std::invalid_argument specs 为空或存在重复 key
     */
    explicit ParamEditModel(std::vector<QuantityFieldSpec> specs);

    // ---- 字段与投影观测（只读） ----

    /// 字段数（注册序呈现的行数基准）。
    std::size_t fieldCount() const noexcept { return m_specs.size(); }
    /// 字段描述集（注册序——只读）。
    const std::vector<QuantityFieldSpec>& fields() const noexcept { return m_specs; }
    /// 按键取字段描述（未知键＝调用方错误——抛 std::out_of_range；
    /// 行定位键来自本模型输出的场景不会命中）。
    const QuantityFieldSpec& spec(const std::string& key) const;

    /// 字段当前显示单位（KIN-12 投影的当前制式）。
    core::UnitToken displayUnit(const std::string& key) const;
    /**
     * @brief 字段当前显示文本（数值＋单位同显的唯一读点）。
     *
     * 值取暂存优先、基线兜底；两者皆无（未设）→ kFieldUnsetText 占位
     * （不伪造 0）。非法输入**不改变**本读点（保留原值的呈现面——原值
     * 是"最后一次有效值"）。
     */
    std::string displayValueText(const std::string& key) const;
    /// 字段当前 SI 真值（暂存优先、基线兜底；未设→nullopt——查询面，
    /// 不做显示格式化）。
    std::optional<double> currentValueSi(const std::string& key) const;
    /**
     * @brief 字段当前显示数值文本（不含单位符号——面板"值｜单位"分列
     *        呈现用；与 displayValueText 同值同制式同优先级〔暂存优先、
     *        基线兜底〕；未设→kFieldUnsetText）。
     */
    std::string displayNumberText(const std::string& key) const;
    /// 字段就地错误原因（空串＝无错误）。
    std::string statusText(const std::string& key) const;

    // ---- 显示投影操作（KIN-12——切换仅影响显示） ----

    /**
     * @brief 切换单个字段的显示单位（显示投影切换——不改任何值语义）。
     *
     * @param key  [in] 字段键（未知键抛 std::out_of_range）
     * @param unit [in] 新显示单位（量纲须与字段 kind 一致——违约属调用
     *              方错误，抛 std::invalid_argument；这是装配/接线错误
     *              而非用户输入错误，fail-fast 不进就地错误通道）
     */
    void setDisplayUnit(const std::string& key, core::UnitToken unit);
    /**
     * @brief 按量纲批量切换显示单位（单位切换控件的数据面——"长度单位
     *        切到 mm"一次作用于该量纲全部字段；KIN-12：切换仅影响显示
     *        与导出格式，不触发重算、不产生修订——本模型层面即"不产生
     *        脏标记/不改暂存"）。
     */
    void setDisplayUnitForKind(core::QuantityKind kind, core::UnitToken unit);

    // ---- 基线注入（域消费者装配/回执面） ----

    /**
     * @brief 注入/更新字段基线值（SI 真值；域消费者在装配期或应用回执
     *        后把权威值同步进表单）。
     *
     * @param key     [in] 字段键（未知键抛 std::out_of_range）
     * @param siValue [in] 基线值（nullopt＝未设；有值须有限——非有限是
     *                调用方错误，抛 std::invalid_argument，不静默出显示）
     *
     * 副作用：该字段的暂存与就地错误一并清除（新权威到达后旧暂存/旧
     * 错误已失据——显示立即回到新基线）。
     */
    void setBaseline(const std::string& key, std::optional<double> siValue);

    // ---- 编辑（就地错误的写入面） ----

    /**
     * @brief 提交一次单元格编辑（UX-05 就地规则的执行点）。
     *
     * 行为：parseFieldValueText 判定输入——成功→暂存新值并清除该字段
     * 错误；失败→记录就地错误原因、基线/暂存/显示**一字不动**（保留
     * 原值），返回 false 由呈现层就地显示 reason。
     *
     * @param key  [in] 字段键（未知键抛 std::out_of_range）
     * @param text [in] 用户输入原文（在字段当前显示单位制式下解析）
     * @return true＝已暂存；false＝非法输入（原因经 statusText 就地取）
     */
    bool setEditText(const std::string& key, const std::string& text);

    // ---- 状态查询（确认/定位/明细的观测面） ----

    /// 字段是否有暂存修改（暂存存在且异于基线——含"基线未设→已设"）。
    bool isDirty(const std::string& key) const;
    /// 全部脏字段键（注册序——确认区/影响明细的遍历序）。
    std::vector<std::string> dirtyKeys() const;
    /// 待应用修改集（脏字段的 old→new 明细——确认交互与移交共用此数据）。
    std::vector<ParamChange> pendingChanges() const;
    /// 表单是否存在未解决的就地错误（表单级应用的前置阻断条件）。
    bool hasErrors() const noexcept;
    /// 全部就地错误（注册序；key＋reason——呈现层逐行显示的来源）。
    std::vector<std::pair<std::string, std::string>> errors() const;
    /// 首个错误字段键（错误定位的锚——nullopt＝无错误；"首个"＝注册序
    /// 最先——定位行为确定，NFR-COR-02）。
    std::optional<std::string> firstErrorKey() const;

    // ---- 筛选（UX-05——大量行的可见性裁剪；不改任何值） ----

    /// 设置筛选词（空串＝全部可见；匹配键或标签的 ASCII 大小写不敏感
    /// 子串——工程键/中文标签都可检索）。
    void setFilterText(const std::string& text) { m_filter = text; }
    /// 当前筛选词。
    const std::string& filterText() const noexcept { return m_filter; }
    /// 当前可见字段键（注册序；筛选词为空的平凡情形即全量注册序）。
    std::vector<std::string> visibleKeys() const;

    // ---- 批量粘贴（UX-05——非模态批量编辑入口） ----

    /**
     * @brief 解析并暂存一批"键<分隔符>值"文本（批量粘贴的规则执行点）。
     *
     * 文法：按 '\n' 分行（容忍行尾 '\r'）；每行"键<TAB|逗号>值"（先
     * 出现的分隔符生效；两侧空白忽略）；空行/纯空白行跳过（不计入
     * 报告——用户复制粘贴常见尾空行，不当作错误）。逐行独立判定：
     * 键未知→拒绝（不影响其他行）；值非法→拒绝（该字段保留原值）；
     * 合法→暂存（同键多行时后行覆盖前行——以最终态为准）。任何行都
     * 不产生模态交互（UX-05——批量编辑禁止模态对话框）。
     *
     * @param text [in] 粘贴文本原文
     * @return 报告（逐行结果＋接受/拒绝计数＋影响明细——见 BatchPasteReport）
     */
    BatchPasteReport batchPasteText(const std::string& text);

    // ---- 表单级确认应用与取消恢复（UX-07／acceptance 1） ----

    /**
     * @brief 表单级确认应用（UX-07：应用前确认交互的规则半区——呈现半
     *        区在面板的确认区；本方法要求调用方先取得用户确认）。
     *
     * 前置（违反即拒绝并给就地反馈原因，不触碰任何状态）：
     *   a. 无未解决就地错误（hasErrors()==false——错误未修复不允许
     *      应用，部分应用会制造"看起来成功实际残缺"的状态）；
     *   b. 存在实际修改（pendingChanges() 非空——无修改不做无效应用）。
     * 通过后：把 pendingChanges 组装为 ParamEditSet 移交出口（域编辑器
     * 转译领域命令→①命令端口——断言与放行归 project）；移交成功即把
     * 涉事字段基线推进到新值并清其暂存/错误（表单与权威重新对齐——
     * 后续回执失败由域消费者经 setBaseline 修正）。
     *
     * @param outlet [in] 编辑出口（域编辑器实现——应用必须有移交面）
     * @return ok==true 已移交（reason 空串）；ok==false 被前置拒绝
     *         （reason＝就地反馈文案，呈现层显示、不改状态）
     */
    ConfirmApplyResult confirmApply(IFormEditOutlet& outlet);

    /**
     * @brief 取消恢复（UX-05/DTB 验收列——放弃全部未应用修改）。
     *
     * 清除全部暂存与就地错误——显示回到基线（"恢复"的机制面：基线在
     * 应用成功前恒不变，取消就是把暂存层整个丢掉）；单位制式与筛选是
     * 会话显示设置，**不随取消回滚**（KIN-12/UX-07：显示操作不是编辑，
     * 与修改集正交）。
     */
    void cancelRestore();

private:
    /// 字段编辑态（基线/暂存/错误/显示单位——按注册序与 specs 平行）。
    struct FieldState {
        std::optional<double> baselineSi;      ///< 基线值（nullopt＝未设；SI 真值）
        std::optional<double> stagedSi;        ///< 暂存待应用值（nullopt＝无暂存；SI 真值）
        std::string errorReason;               ///< 就地错误原因（空串＝无错误）
        core::UnitToken displayUnit{};         ///< 当前显示单位（KIN-12 投影制式）
    };
    /// 按键找状态（未知键属调用方错误——抛 std::out_of_range）。
    FieldState& stateOf(const std::string& key);
    const FieldState& stateOf(const std::string& key) const;

    std::vector<QuantityFieldSpec> m_specs;  ///< 字段描述（注册序——稳定呈现序）
    std::vector<FieldState> m_states;        ///< 编辑态（与 m_specs 平行——下标即行）
    std::string m_filter;                    ///< 当前筛选词（空串＝全量）
};

// =====================================================================
// 高级面板字段集（UX-04 承载——宿主字段注册的参考集）
// =====================================================================

/**
 * @brief 高级面板的缺省字段集（UX-04：高级面板承载求解器、采样、随机
 *        种子和开发诊断——本函数给出其中**数值编辑字段**半区的宿主
 *        注册参考；"开发诊断"是会话级显示开关，归面板选项
 *        （ParamTablePanelOptions.showDevDiagnosticsToggle——UX-07：
 *        仅改变会话显示的操作不进修改集、不触发确认）。
 *
 * 为什么在 ui 提供这个集合：UX-04 的承载面在 ui（高级面板是工作台
 * 常设面板位，§4.2 右区/高级面板位），而字段值语义归域单元——本集合
 * 只注册**键/标签/量纲/计数类约束**（迭代次数/采样点数/随机种子按
 * 定义是整数计数——宿主约束，非业务阈值），基线值一律"未设"，由域
 * 消费者经 setBaseline 注入真实权威值。零默认数值＝不虚构业务能力
 * （§11.4）。
 *
 * @return 三字段（solver-max-iterations/sampling-points/random-seed，
 *         全部无量纲＋仅整数——注册序即面板行序；文案改动＝契约改动，
 *         须升单元卡修订）
 */
std::vector<QuantityFieldSpec> advancedPanelDefaultFields();

// =====================================================================
// 参数表面板（Qt Widgets 渲染层——规则的第一宿主；R-3 例外面）
// =====================================================================

/// 面板装配选项（面板级会话显示元素——与编辑字段分组异名呈现）。
struct ParamTablePanelOptions {
    /// 是否显示"开发诊断"开关（UX-04 高级面板承载项——会话级显示开关，
    /// 不进修改集、不触发确认；参数表面板的普通宿主场景保持 false）。
    bool showDevDiagnosticsToggle = false;
    /// "开发诊断"开关回调（勾选态变化即调；nullopt＝开关仅呈现无副作用
    /// ——无消费方的纯展示场景）。非线程安全回调——UI 线程内同步调用。
    std::function<void(bool)> onDevDiagnosticsToggled{};
};

/**
 * @brief 构建参数表面板（表单公共件的第一渲染面——阶段 B 域消费者的
 *        通用宿主）。
 *
 * 面板构成（objectName 锚——GUI 测试与样式定位）：
 *   - 根容器 ird_param_table_panel；
 *   - 筛选行：QLineEdit（ird_param_filter——UX-05 筛选）＋显示单位切换
 *     QComboBox（ird_param_unit_switch——KIN-12 显示制式切换，按模型中
 *     存在的可切换量纲〔长度/角度〕出项；无量纲字段无切换项）；
 *   - 参数表 QTableWidget（ird_param_table）：列＝参数｜值｜单位｜状态
 *     ——值列就地编辑（UX-05：不用模态对话框），单位列与值列同显
 *     （数值＋单位同显），状态列就地显示非法输入原因（保留原值）；
 *   - 影响明细 QLabel（ird_param_impact）：批量粘贴报告与就地反馈的
 *     呈现区（非模态——一切反馈就地，零对话框）；
 *   - 确认区 QWidget（ird_param_confirm_region，默认隐藏）：提示文本
 *     （ird_param_confirm_text）＋"确认应用"（ird_param_confirm_yes）/
 *     "取消"（ird_param_confirm_no）——UX-07 应用前确认交互的内嵌
 *     非模态呈现；
 *   - 操作行："应用…"（ird_param_apply）/"取消恢复"（ird_param_cancel）
 *     ＋可选"开发诊断"开关（ird_param_dev_diag——分组异名：会话显示
 *     开关不与编辑操作混排成组，仅按选项附加于操作行尾）。
 *
 * 所有权与生命周期：返回面板由 Qt 父子树管理（parent 存活期）；model
 * 与 outlet 由调用方持有且须覆盖面板存活期（面板存裸引用——不接管）；
 * outlet 允许为空＝域编辑器未装配，此时"应用…"禁用并挂
 * kNoOutletTooltip（不虚构可用性）。
 *
 * 线程：仅 UI 线程调用与使用（§3.4 M-1）。
 *
 * @param model   [in] 编辑会话模型（调用方持有——面板是它的视图）
 * @param outlet  [in] 编辑出口（可空；确认应用时移交目标）
 * @param options [in] 装配选项（缺省＝纯参数表面板）
 * @param parent  [in] Qt 父控件（可空——调用方自行挂载）
 * @return 面板根控件（never null）
 */
QWidget* createParamTablePanel(ParamEditModel& model,
                               IFormEditOutlet* outlet,
                               const ParamTablePanelOptions& options = {},
                               QWidget* parent = nullptr);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_FORMEDITCOMMON_HPP
