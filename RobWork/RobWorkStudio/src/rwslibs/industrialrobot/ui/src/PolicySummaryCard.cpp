/**
 * @file   PolicySummaryCard.cpp
 * @brief  工程策略摘要只读卡实现（UI-T07 阶段 A）——§6.7 呈现数据的纯
 *         函数装配（单位换算/显式不适用/分组异名）＋右栏卡控件构建。
 *
 * 设计依据：
 *   - units/ui.md §6.7（策略摘要只读卡：EngineeringPolicySet 公开字段投影、
 *     显示值经单位换算投影不改身份、"修改策略…"跳转编辑适配器→①命令端口、
 *     表单归阶段 B）、§4.2 右栏行（策略摘要＝只读输入投影；"修改"跳转不
 *     就地写权威对象）、§4.6（显示单位偏好默认 m/rad——KIN-12；会话显示
 *     开关与策略严格分离）、§11.4（不虚构业务能力——占位/延期说明口径）、
 *     §3.4（UI 线程纪律）、§16.7 v0.9（本文件登记行）；
 *   - 需求 UX-08、ARC-05（权威归 policy——ui 零判定权/零计算开关权威）、
 *     POL-ID-3（显示设置不存在于策略字段）；
 *   - 任务契约 tasks/foundation/UI-T07.json acceptance 1~3；
 *   - 同构先例：src/View3DPlaceholder.cpp（UI-T05——占位面板构建＋公共
 *     契约头同源文案，零 Q_OBJECT，lambda 接线）。
 *
 * O-31 处置（acceptance 3）：本 TU 对 policy 零 include 零链接——卡的
 * 全部数据输入是 ui 自有值投影（PolicySummaryProjection）经 ui 自有端口
 * （IPolicySummarySource）注入，L5 装配期适配 policy::IPolicyProvider；
 * 显示换算经 core Units（ui→core 表内登记边）。守卫
 * NoCrossUnitInclude_O31_UI_BUILD（test/BuildRedLineTest.cpp）对本文件
 * 常驻自证。
 *
 * 线程模型：装配与刷新只在 UI 线程调用（§3.4 M-1——端口实现在 UI 线程
 * 消费）；无后台工作、无长计算（呈现路径，NFR-PERF-01）。
 */

#include "WorkbenchShell_p.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <charconv>
#include <functional>
#include <stdexcept>
#include <string>

#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/ui/PolicySummaryCard.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// 纯函数：显示单位默认、阈值换算、行装配（PolicySummaryCard.hpp 契约）
// =====================================================================

PolicySummaryDisplayUnits PolicySummaryDisplayUnits::siDefaults()
{
    // §4.6 出厂默认＝KIN-12 显示投影缺省制式（m/rad）。core 单位注册表
    // 为进程级静态注册，"m"/"rad" 是表内冻结条目——find 失败属构建期
    // 事实破坏（不可达），命中即 logic_error fail-fast，不静默出空 token。
    const auto length = core::UnitToken::find("m");
    const auto angle = core::UnitToken::find("rad");
    if (!length.has_value() || !angle.has_value()) {
        throw std::logic_error("ui/policy-card: core 单位注册表缺少 m/rad（构建期事实破坏）");
    }
    PolicySummaryDisplayUnits units;
    units.length = *length;
    units.angle = *angle;
    return units;
}

namespace {

/**
 * @brief 显示数值的确定性文本化（6 位有效数字，general 格式）。
 *
 * 用 std::to_chars 而非 snprintf/snprintf-locale 系：浮点 to_chars 按
 * 标准规定与 locale 无关（恒 '.' 小数点）——NFR-COR-02 确定性不受宿主
 * C locale 影响（Qt 应用可能改设 locale）。呈现精度 6 位有效数字：摘要
 * 卡是呈现投影（非工程比较值——SI 真值恒为唯一权威），6 位足够辨识且
 * 换算误差不进入任何计算/身份。
 */
std::string formatDisplayNumber(double value)
{
    char buffer[40] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                      std::chars_format::general, 6);
    if (result.ec != std::errc()) {
        // 可达性：double 全值域在 40 字节缓冲内恒可容纳（general 格式）；
        // 命中属实现假设破坏——fail-fast 不出错误文本。
        throw std::logic_error("ui/policy-card: 显示数值格式化失败（缓冲假设破坏）");
    }
    return std::string{buffer, result.ptr};
}

/**
 * @brief 单行值文本组装（换算值＋单位符号，无量纲只显数值）。
 *
 * @param display [in] 已换算的显示投影（present==true）
 * @return "300 mm"/"720 deg"/"0.85"（无量纲单位符号 "1" 不呈现——工程
 *         惯例：无量纲值不带"1"后缀，呈现语义仍是"经 core 注册表投影"）
 */
std::string thresholdValueText(const PolicyThresholdDisplay& display)
{
    // 无量纲判定用符号而非量纲枚举：displayUnitSymbol 即注册表 token，
    // 无量纲冻结 token 原文为 "1"（core Units.cpp 单位表）。
    if (display.displayUnitSymbol == "1") {
        return formatDisplayNumber(display.displayValue);
    }
    return formatDisplayNumber(display.displayValue) + ' ' + display.displayUnitSymbol;
}

/// 行构造小工厂（键/组/标签集中——装配函数主体只列事实，防拼写漂移）。
PolicySummaryRow makeRow(const char* key, PolicySummaryGroup group,
                         const char* label, std::string value)
{
    PolicySummaryRow row;
    row.key = key;
    row.group = group;
    row.label = label;
    row.value = std::move(value);
    return row;
}

}  // namespace

PolicyThresholdDisplay formatPolicyThreshold(const PolicyThresholdProjection& threshold,
                                             core::UnitToken siUnit,
                                             core::UnitToken displayUnit)
{
    // 显式不适用：直接空态返回——不换算、不发明数值（P-POL-2/O-10；
    // 呈现层取 kPolicyNotApplicableText）。
    if (!threshold.present) {
        return PolicyThresholdDisplay{};
    }
    // 换算经 core Units（一次乘除，注册表因子精确）——量纲不匹配/未注册
    // 由 core::convert fail-fast 抛出（调用方错误，不用错误值继续渲染）。
    PolicyThresholdDisplay display;
    display.present = true;
    display.displayValue = core::convert(threshold.siValue, siUnit, displayUnit);
    display.displayUnitSymbol = std::string{displayUnit.symbol()};
    return display;
}

const char* policySummaryGroupTitle(PolicySummaryGroup group) noexcept
{
    // 分组异名的"名"字面（POL-ID-3）：两标题各自点明影响范围归属——
    // "计算权威"（策略字段随内容身份参与计算）对"不属策略字段"（会话
    // 显示设置，零计算影响）。文案改动＝契约改动，须升单元卡修订。
    switch (group) {
    case PolicySummaryGroup::PolicyAuthority:
        return "工程策略（计算权威）";
    case PolicySummaryGroup::SessionDisplay:
        return "会话显示设置（不属策略字段）";
    }
    // 全枚举 switch 无 default：新增枚举值未登记标题时编译期告警
    // （全表锚定惯例——policy 码表同款防漂移）。不可达路径返回空串。
    return "";
}

std::vector<PolicySummaryRow> policySummaryRows(const PolicySummaryProjection& summary,
                                                const PolicySummaryDisplayUnits& units)
{
    std::vector<PolicySummaryRow> rows;

    // ---- 工程策略（计算权威）组：策略字段只读投影（§6.7 五类字段）----
    if (!summary.available) {
        // 未装载：占位一行，不虚构任何数值（§6.7 摘要只读面语义——
        // available=false 的唯一合法呈现）。
        rows.push_back(makeRow("policy", PolicySummaryGroup::PolicyAuthority,
                               "工程策略", kPolicyNotLoadedText));
    } else {
        // 碰撞检查（计算开关——POL-ID-3"计算开关"侧）：启用行附启用域
        // token 列表（对端 §4.3 冻结 token 直用，"、"？——工程清单分隔，
        // 非 UX-02 禁区）；停用行明确"停用"（不做暗示性省略）。
        if (summary.collisionDomainEnabled) {
            std::string domains;
            for (const auto& token : summary.enabledDomainTokens) {
                if (!domains.empty()) {
                    domains += "、";
                }
                domains += token;
            }
            rows.push_back(makeRow("collision", PolicySummaryGroup::PolicyAuthority,
                                   "碰撞检查",
                                   domains.empty() ? "启用" : "启用（" + domains + "）"));
        } else {
            rows.push_back(makeRow("collision", PolicySummaryGroup::PolicyAuthority,
                                   "碰撞检查", "停用"));
        }
        // 安全间距（SI m→显示长度单位）：停用/未设置时为「显式不适用」
        // （对端 optional 空＝语义内容，不补默认值）。
        const auto clearance =
            formatPolicyThreshold(summary.safetyClearance,
                                  *core::UnitToken::find("m"), units.length);
        rows.push_back(makeRow("safety-clearance", PolicySummaryGroup::PolicyAuthority,
                               "安全间距",
                               clearance.present ? thresholdValueText(clearance)
                                                 : kPolicyNotApplicableText));
        // 碰撞对规则（必检对/过滤对计数——§6.7"过滤对计数"原文＋同源
        // 对偶必检对：过滤不得隐藏必检，policy.md §7.2）。
        rows.push_back(makeRow(
            "pairs", PolicySummaryGroup::PolicyAuthority, "碰撞对规则",
            "必检 " + std::to_string(summary.mandatoryPairCount) + " 对·过滤 "
                + std::to_string(summary.filterPairCount) + " 对"));
        // 行程上限（SI rad→显示角度单位；已发布集合恒有值——4π 冻结默认；
        // 防御性 N/A 分支保留：适配器只可能对非法投影给 present=false，
        // 呈现层统一走「显式不适用」不虚构）。
        const auto travel =
            formatPolicyThreshold(summary.travelLimit,
                                  *core::UnitToken::find("rad"), units.angle);
        rows.push_back(makeRow("travel-limit", PolicySummaryGroup::PolicyAuthority,
                               "行程上限",
                               travel.present ? thresholdValueText(travel)
                                              : kPolicyNotApplicableText));
        // 判定阈值族（近限位比/条件数——无量纲，经 "1" 注册表投影直显）：
        // 各自独立呈现「显式不适用」——ERR-01"不适用"逐字段显式化。
        const auto nearLimit =
            formatPolicyThreshold(summary.nearLimitRatio,
                                  *core::UnitToken::find("1"),
                                  *core::UnitToken::find("1"));
        rows.push_back(makeRow("near-limit-ratio", PolicySummaryGroup::PolicyAuthority,
                               "近限位比",
                               nearLimit.present ? thresholdValueText(nearLimit)
                                                 : kPolicyNotApplicableText));
        const auto condition =
            formatPolicyThreshold(summary.conditionNumberWarning,
                                  *core::UnitToken::find("1"),
                                  *core::UnitToken::find("1"));
        rows.push_back(makeRow("condition-number", PolicySummaryGroup::PolicyAuthority,
                               "条件数警告",
                               condition.present ? thresholdValueText(condition)
                                                 : kPolicyNotApplicableText));
        // 修改策略路径说明（acc2：编辑经①命令端口提交生成新修订——
        // ARC-05 权威归策略的界面表达；表单归阶段 B，入口见卡面按钮）。
        rows.push_back(makeRow("edit-path", PolicySummaryGroup::PolicyAuthority,
                               "修改策略", kPolicyEditPathNote));
    }

    // ---- 会话显示设置组（恒呈现）：POL-ID-3 分组异名的"另一名"内容面
    // ——与策略装载状态无关（显示设置边界是恒成立的单元级事实）。
    rows.push_back(makeRow("display-separation", PolicySummaryGroup::SessionDisplay,
                           "显示开关与策略的关系", kPolicyDisplaySeparationNote));
    rows.push_back(makeRow("display-controls", PolicySummaryGroup::SessionDisplay,
                           "显示控件", kPolicyDisplayControlsDeferredNote));
    return rows;
}

// =====================================================================
// 卡控件构建（右栏"诊断与设置区"内嵌——§6.7；私有装配面，R-2）
// =====================================================================

namespace {

/// 卡容器 objectName（GUI 测试定位锚——ird_ 前缀单元命名惯例）。
constexpr const char* kCardObjectName = "ird_policy_summary_card";

/**
 * @brief 把卡内容装配进容器（全部子控件在此创建——刷新＝清空重建）。
 *
 * 为什么刷新是"清空重建"而不是逐标签更新：行集随装载形态变化（未装载
 * 1＋2 行、已装载 7＋2 行），就地更新要同时处理行数增减与行类型（按钮/
 * 说明）差异，分支复杂度高于一次重建；卡为轻量 QLabel 集，重建为微秒级
 * （呈现路径——NFR-PERF-01）。重建过程对象名稳定（行 key/组名派生），
 * GUI 测试定位不受刷新影响。
 *
 * @param container [in] 卡容器（已带垂直布局——由 createPolicySummaryCard
 *                  创建，子控件所有权移交 Qt 对象树）
 * @param source    [in] 策略摘要端口（UI 线程调用——取快照渲染）
 * @param units     [in] 显示单位选项（阶段 A 恒 §4.6 出厂默认 m/rad）
 */
void buildPolicyCardContent(QWidget* container, IPolicySummarySource& source,
                            const PolicySummaryDisplayUnits& units)
{
    // 清空旧内容（takeAt 逐项取出：控件即删、间距项即删、条目本身即删）
    // ——注意三类条目的删除互斥：QWidgetItem 是包裹件（与控件对象不同体，
    // 两者都要删）；QSpacerItem 自身即条目（addStretch 的产物——删其一即
    // 可，双删＝堆损坏，首跑实测崩溃）；其余条目（布局/空白）只删条目。
    if (auto* old = container->layout()) {
        while (auto* item = old->takeAt(0)) {
            if (item->widget() != nullptr) {
                delete item->widget();
                delete item;
            } else if (item->spacerItem() != nullptr) {
                delete item;  // spacerItem() 返回 this——条目即间距项，单删
            } else {
                delete item;
            }
        }
        delete old;
    }

    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    // 取当前策略快照并装配行（模型层纯函数——GUI 只渲染不持语义）。
    const std::vector<PolicySummaryRow> rows = policySummaryRows(source.summary(), units);

    // 分组渲染：组标题在其组首行前插入一次（行序即装配序——分组异名
    // 的呈现面；标题 objectName 由组枚举派生，稳定可定位）。
    PolicySummaryGroup lastGroup = rows.empty()
        ? PolicySummaryGroup::PolicyAuthority
        : rows.front().group;
    bool groupHeaderPlaced = false;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const PolicySummaryRow& row = rows[i];
        if (!groupHeaderPlaced || row.group != lastGroup) {
            auto* header = new QLabel(QString::fromUtf8(policySummaryGroupTitle(row.group)),
                                      container);
            header->setObjectName(row.group == PolicySummaryGroup::PolicyAuthority
                                      ? "ird_policy_group_authority"
                                      : "ird_policy_group_display");
            header->setWordWrap(true);
            layout->addWidget(header);
            lastGroup = row.group;
            groupHeaderPlaced = true;
        }
        // 行标签："label：value"（全角冒号——与壳内其它呈现行一致）。
        // 只读 QLabel——摘要只读红线（acc1）：卡内零可编辑策略控件。
        auto* rowLabel = new QLabel(QString::fromUtf8(row.label) + u8"："
                                        + QString::fromUtf8(row.value),
                                    container);
        rowLabel->setObjectName("ird_policy_row_" + QString::fromStdString(row.key));
        rowLabel->setWordWrap(true);
        layout->addWidget(rowLabel);

        // 修改策略跳转入口（acc2）：按钮只挂"延期提示"反馈（§4.1 占位
        // 口径）——不打开任何表单、不触达任何写路径（编辑表单归阶段 B，
        // 届时经编辑适配器→①命令端口；本阶段不虚构编辑能力 §11.4）。
        if (row.key == "edit-path") {
            auto* editButton = new QPushButton(QString::fromUtf8("修改策略…"), container);
            editButton->setObjectName("ird_policy_edit_button");
            // QPointer 持有提示标签：重建（旧控件销毁）与点击事件投递的
            // 时序解耦——悬垂指针在 Qt 对象销毁时自动置空，零 UB 面。
            QPointer<QLabel> notice = new QLabel(container);
            notice->setObjectName("ird_policy_edit_notice");
            notice->setWordWrap(true);
            notice->hide();
            QObject::connect(editButton, &QPushButton::clicked, container,
                             [notice] {
                                 // 点击反馈＝延期提示（kPolicyEditDeferredNotice
                                 // ——契约文案单点，模型测试钉住原文）；重建后
                                 // 旧标签销毁则本守卫使回调空转。
                                 if (notice != nullptr) {
                                     notice->setText(
                                         QString::fromUtf8(kPolicyEditDeferredNotice));
                                     notice->show();
                                 }
                             });
            layout->addWidget(editButton);
            layout->addWidget(notice);
        }
    }
    layout->addStretch(1);
}

}  // namespace

QWidget* detail::createPolicySummaryCard(IPolicySummarySource& policySource,
                                         std::function<void()>* refreshOut,
                                         QWidget* parent)
{
    auto* container = new QWidget(parent);
    container->setObjectName(kCardObjectName);
    // 阶段 A 显示单位＝§4.6 出厂默认（m/rad——KIN-12 显示投影缺省制式；
    // 用户级显示单位偏好的设置承载与消费随用户设置任务接入）。
    const PolicySummaryDisplayUnits units = PolicySummaryDisplayUnits::siDefaults();
    buildPolicyCardContent(container, policySource, units);
    if (refreshOut != nullptr) {
        // 刷新钩子：容器存活期内重新拉取端口快照并重建内容（UI 线程——
        // §3.4 M-1；壳在 initialize 与 presentProjectContext 后触发）。
        // container 由调用方（Qt 父子树）销毁，钩子生命周期≤容器。
        *refreshOut = [container, &policySource, units] {
            if (container != nullptr) {
                buildPolicyCardContent(container, policySource, units);
            }
        };
    }
    return container;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
