/**
 * @file   ParamTablePanel.cpp
 * @brief  参数表面板实现（UI-T08 阶段 B）——表单公共件的第一渲染面：
 *         筛选、单位切换、就地编辑、非模态确认区、批量粘贴与影响明细。
 *
 * 设计依据：
 *   - units/ui.md §13 UI-T08 行（阶段 B 交付：表单公共件）、§4.2（参数
 *     表/属性面板承载位；修改只能进草稿，不就地写权威对象）、§4.5
 *     （单位显示是会话/用户级显示设置——与计算设置严格分离）、§11.4
 *     （不虚构业务能力——未接编辑出口时禁用应用）、§16.7 v1.0（本文件
 *     登记行）；
 *   - 需求 UX-04（高级面板承载——开发诊断开关作为会话显示开关归本面板
 *     选项）、UX-05（批量粘贴/筛选/单位显示/错误定位——不用模态对话框
 *     做大量重复编辑）、UX-07（表单级应用前确认；仅改变会话显示的操作
 *     不触发确认）、KIN-12（单位切换仅影响显示）；
 *   - 任务契约 tasks/foundation/UI-T08.json acceptance 1/2（公共编辑
 *     规则全量＋O-31 处置——草稿接入经 ui 自有 IDraftController.
 *     attachModule 接口，本面板只把确认过的修改集移交 IFormEditOutlet，
 *     不持有任何对端类型）；
 *   - 同构先例：src/PolicySummaryCard.cpp（UI-T07——GUI 只渲染不持语义，
 *     规则在模型层；零 Q_OBJECT，既有信号＋lambda 接线）。
 *
 * 交互红线（结构事实，测试钉住）：
 *   - 一切反馈就地（状态列/影响明细区），确认交互是面板内嵌区域——全
 *     文零 QDialog/零 exec()（test/FormEditModelTest.cpp 静态扫描钉住
 *     ——UX-05"不用模态对话框"的常驻自证）；
 *   - 值列就地编辑是唯一编辑形态；非法输入在状态列就地显示原因、值列
 *     显示立即回到最后一次有效值（保留原值）；
 *   - "开发诊断"开关是会话显示设置：只触发 options 回调，不进模型、
 *     不产生脏标记、不触发确认（UX-07 后半句的结构保证）。
 *
 * O-31 处置（acceptance 2）：本 TU 仅 include Qt Widgets（R-3 例外面）、
 * core Units 与本单元公共头——对 project/evidence/execution/policy/
 * runtime 零 include 零链接（NoCrossUnitInclude_O31_UI_BUILD 常驻自证）。
 *
 * 线程模型：全部装配与回调仅在 UI 线程（§3.4 M-1）；无后台工作、无长
 * 计算（NFR-PERF-01——刷新为行级 setText，几十行量级微秒完成）。
 */

#include <sdurws/ird/ui/FormEditCommon.hpp>

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

// =====================================================================
// 面板内契约常量（objectName 锚——GUI 测试与样式定位；文案冻结件在
// FormEditCommon.hpp 单点，此处只登记控件标识与本地组排文案）
// =====================================================================

constexpr const char* kPanelObjectName = "ird_param_table_panel";
constexpr const char* kFilterObjectName = "ird_param_filter";
constexpr const char* kUnitSwitchObjectName = "ird_param_unit_switch";
constexpr const char* kTableObjectName = "ird_param_table";
constexpr const char* kImpactObjectName = "ird_param_impact";
constexpr const char* kConfirmRegionObjectName = "ird_param_confirm_region";
constexpr const char* kConfirmTextObjectName = "ird_param_confirm_text";
constexpr const char* kConfirmYesObjectName = "ird_param_confirm_yes";
constexpr const char* kConfirmNoObjectName = "ird_param_confirm_no";
constexpr const char* kApplyObjectName = "ird_param_apply";
constexpr const char* kCancelObjectName = "ird_param_cancel";
constexpr const char* kPasteObjectName = "ird_param_paste";
constexpr const char* kDevDiagObjectName = "ird_param_dev_diag";

/// 表头词表（列序＝参数｜值｜单位｜状态——值/单位两列合起来即"数值＋
/// 单位同显"的分列呈现；状态列承载就地错误原因）。
constexpr const char* kColumnTitles[] = {"参数", "值", "单位", "状态"};
constexpr int kColumnCount = 4;
constexpr int kValueColumn = 1;  // 值列下标（就地编辑的唯一开放列）

/**
 * @brief 面板共享状态（全部回调经 shared_ptr 捕获——面板销毁时 lambda
 *        随控件树销毁，状态引用计数归零，无悬垂窗口期）。
 *
 * 装配期把控件指针收拢进上下文（高频回调不做 findChild）；行→键映射
 * （rowKeys）是表格与模型之间的定位锚——行内 UserRole 数据面向测试/
 * 样式定位，本结构面向回调；单位下拉项表（unitEntries）把"项下标→
 * 量纲＋单位"钉住，切换回调按下标取语义（不经 QVariant 元类型注册）。
 *
 * 线程：仅 UI 线程访问（非线程安全——§2.5 线程约束注明）。
 */
struct PanelContext {
    ParamEditModel& model;              ///< 编辑会话模型（调用方持有——本面板是它的视图）
    IFormEditOutlet* outlet = nullptr;  ///< 编辑出口（可空——未装配时应用禁用；不接管所有权）
    ParamTablePanelOptions options;     ///< 装配选项（开发诊断开关及其回调）
    bool updating = false;              ///< 刷新守卫（程序化 setText 不当作用户编辑）
    std::vector<std::string> rowKeys;   ///< 表行→字段键（与表格行平行——回调定位锚）
    std::vector<std::pair<core::QuantityKind, QString>>
        unitEntries;                    ///< 单位下拉"项下标→量纲＋单位"（切换回调的语义表）
    QTableWidget* table = nullptr;      ///< 参数表（值列就地编辑）
    QLabel* impact = nullptr;           ///< 影响明细/就地反馈区（非模态）
    QWidget* confirmRegion = nullptr;   ///< 确认区（内嵌非模态——UX-07）
    QLabel* confirmText = nullptr;      ///< 确认区提示文本（逐项 old→new 明细）
};

/// 刷新守卫的 RAII 壳：作用域内置位、离开即复位——异常路径也不漏旗标。
struct UpdatingGuard {
    explicit UpdatingGuard(bool& flag) : m_flag(flag) { m_flag = true; }
    ~UpdatingGuard() { m_flag = false; }
    UpdatingGuard(const UpdatingGuard&) = delete;
    UpdatingGuard& operator=(const UpdatingGuard&) = delete;

private:
    bool& m_flag;
};

/// 可切换量纲组（长度/角度是 R1 单位表里有多显示单位的量纲——无量纲等
/// 单制式量纲无切换语义，不出项；候选＝R1 冻结 token 的呈现子集，
/// KIN-12 需求原文口径 m/cm/mm、rad/deg；扩展单位 R2 随 KIN-12-S1 追加）。
struct UnitSwitchGroup {
    core::QuantityKind kind;            ///< 目标量纲
    const char* kindLabel;              ///< 组前缀（"长度"/"角度"——工程用语）
    std::vector<const char*> symbols;   ///< 候选显示单位（R1 冻结 token）
};

// ---------------------------------------------------------------------
// 呈现小工具（模型投影 → 呈现文本；纯投影——无业务语义）
// ---------------------------------------------------------------------

/// 修改明细的"旧值"文本（基线未设→kFieldUnsetText 占位——"从未设→设
/// 为"的呈现语义；有值按字段当前显示制式换算，与确认区所见一致）。
QString oldChangeText(ParamEditModel& model, const ParamChange& change)
{
    if (!change.oldSi.has_value()) {
        return QString::fromUtf8(kFieldUnsetText);
    }
    const auto& sp = model.spec(change.key);
    return QString::fromStdString(
        formatFieldValueText(*change.oldSi, sp.siUnit, model.displayUnit(change.key)));
}

/// 修改明细的"新值"文本（同制式换算——确认区呈现即应用后所见）。
QString newChangeText(ParamEditModel& model, const ParamChange& change)
{
    const auto& sp = model.spec(change.key);
    return QString::fromStdString(
        formatFieldValueText(change.newSi, sp.siUnit, model.displayUnit(change.key)));
}

/// 单条修改的一行呈现（"标签：旧 → 新"——确认区与影响明细共用格式，
/// 同源呈现避免两处文案漂移）。
QString changeLineText(ParamEditModel& model, const ParamChange& change)
{
    return QString::fromUtf8(change.label.c_str()) + u8"：" + oldChangeText(model, change)
           + u8" → " + newChangeText(model, change);
}

// ---------------------------------------------------------------------
// 刷新（模型 → 表格；纯投影）
// ---------------------------------------------------------------------

/**
 * @brief 按模型当前态重画表格（筛选可见行集 × 四列投影）。
 *
 * 为什么整表重建而不是逐格更新：可见行集随筛选变化、就地错误要求值列
 * 文本回退（保留原值），整表重建分支最少且几十行量级重建微秒级
 * （PolicySummaryCard 清空重建同案）；updating 守卫保证重建中的 setText
 * 不被当作用户编辑。
 */
void refreshTable(PanelContext& ctx)
{
    UpdatingGuard guard(ctx.updating);
    const std::vector<std::string> keys = ctx.model.visibleKeys();
    ctx.rowKeys = keys;
    ctx.table->setRowCount(static_cast<int>(keys.size()));
    for (int row = 0; row < static_cast<int>(keys.size()); ++row) {
        const std::string& key = keys[static_cast<std::size_t>(row)];
        const auto& sp = ctx.model.spec(key);
        // 参数列（只读；UserRole 携带键——行定位锚，测试与回调共用）。
        auto* labelItem = new QTableWidgetItem(QString::fromUtf8(sp.label.c_str()));
        labelItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        labelItem->setData(Qt::UserRole, QString::fromStdString(key));
        ctx.table->setItem(row, 0, labelItem);
        // 值列（唯一可编辑列——就地编辑；文本＝最后一次有效值，非法输入
        // 不改动它——"保留原值"的呈现面）。未设值也允许编辑（编辑后
        // 从无到有）。
        auto* valueItem =
            new QTableWidgetItem(QString::fromStdString(ctx.model.displayNumberText(key)));
        valueItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        ctx.table->setItem(row, kValueColumn, valueItem);
        // 单位列（只读——与值列同显的"单位"半区；KIN-12 切换即换此列；
        // 无量纲显示注册表 token "1"，诚实呈现制式不美化）。
        auto* unitItem = new QTableWidgetItem(
            QString::fromUtf8(ctx.model.displayUnit(key).symbol()));
        unitItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ctx.table->setItem(row, 2, unitItem);
        // 状态列（只读——就地错误原因；空串＝无错误，无占位噪声）。
        auto* statusItem = new QTableWidgetItem(
            QString::fromStdString(ctx.model.statusText(key)));
        statusItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        ctx.table->setItem(row, 3, statusItem);
    }
}

// ---------------------------------------------------------------------
// 反馈与定位（就地反馈——零对话框）
// ---------------------------------------------------------------------

/// 错误定位：把首个错误行滚入视野并选中（UX-05 错误定位的呈现面——
/// 被筛选掉的行无法高亮，由反馈区的原因文案兜底；返回是否在表中定位到）。
bool revealFirstError(PanelContext& ctx)
{
    const std::optional<std::string> firstKey = ctx.model.firstErrorKey();
    if (!firstKey.has_value()) {
        return false;
    }
    const auto it = std::find(ctx.rowKeys.begin(), ctx.rowKeys.end(), *firstKey);
    if (it == ctx.rowKeys.end()) {
        return false;  // 错误行被筛掉——呈现层以反馈文案兜底（不静默）
    }
    const int row = static_cast<int>(std::distance(ctx.rowKeys.begin(), it));
    if (auto* item = ctx.table->item(row, 0)) {
        ctx.table->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        ctx.table->selectRow(row);
    }
    return true;
}

/// 确认区提示文本组装（kConfirmPromptPrefix＋逐项"标签：旧 → 新"——
/// UX-07 应用前确认的比较型呈现；行序＝pendingChanges 注册序）。
QString buildConfirmText(PanelContext& ctx)
{
    QString text = QString::fromUtf8(kConfirmPromptPrefix);
    for (const auto& change : ctx.model.pendingChanges()) {
        text += u8"\n" + changeLineText(ctx.model, change);
    }
    return text;
}

}  // namespace

// =====================================================================
// 面板装配（createParamTablePanel——契约见 FormEditCommon.hpp）
// =====================================================================

QWidget* createParamTablePanel(ParamEditModel& model, IFormEditOutlet* outlet,
                               const ParamTablePanelOptions& options, QWidget* parent)
{
    auto* panel = new QWidget(parent);
    panel->setObjectName(QString::fromLatin1(kPanelObjectName));
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);

    // 共享上下文（lambda 全体经 shared_ptr 捕获——面板销毁即释放）。
    auto ctx = std::make_shared<PanelContext>(PanelContext{
        model, outlet, options, false, {}, {}, nullptr, nullptr, nullptr, nullptr});

    // ---- 筛选行：筛选输入＋显示单位切换（两个都是非编辑态操作——
    // 不产生脏标记、不触发确认：UX-07 后半句）。
    auto* filterRow = new QWidget(panel);
    auto* filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    auto* filterCaption = new QLabel(QString::fromUtf8("筛选"), filterRow);
    auto* filterEdit = new QLineEdit(filterRow);
    filterEdit->setObjectName(QString::fromLatin1(kFilterObjectName));
    filterEdit->setPlaceholderText(QString::fromUtf8("按参数名或标签筛选"));
    filterLayout->addWidget(filterCaption);
    filterLayout->addWidget(filterEdit, 1);
    auto* unitSwitch = new QComboBox(filterRow);
    unitSwitch->setObjectName(QString::fromLatin1(kUnitSwitchObjectName));
    unitSwitch->setToolTip(QString::fromUtf8("显示单位（仅影响显示，不改变真值）"));
    filterLayout->addWidget(new QLabel(QString::fromUtf8("显示单位"), filterRow));
    filterLayout->addWidget(unitSwitch);
    layout->addWidget(filterRow);

    // ---- 参数表：四列（参数｜值｜单位｜状态）；值列就地编辑（UX-05：
    // 不用模态对话框做大量重复编辑——编辑形态只有就地这一种）。
    auto* table = new QTableWidget(panel);
    table->setObjectName(QString::fromLatin1(kTableObjectName));
    table->setColumnCount(kColumnCount);
    for (int column = 0; column < kColumnCount; ++column) {
        table->setHorizontalHeaderItem(
            column, new QTableWidgetItem(QString::fromUtf8(kColumnTitles[column])));
    }
    table->horizontalHeader()->setStretchLastSection(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);
    ctx->table = table;
    layout->addWidget(table, 1);

    // ---- 影响明细/就地反馈区（非模态——批量粘贴报告、应用反馈、错误
    // 提示全部落这里；word wrap 承载多行明细）。
    auto* impact = new QLabel(panel);
    impact->setObjectName(QString::fromLatin1(kImpactObjectName));
    impact->setWordWrap(true);
    ctx->impact = impact;
    layout->addWidget(impact);

    // ---- 确认区（内嵌非模态——UX-07 应用前确认交互；默认隐藏，点
    // "应用…"后呈现待应用明细，确认/再改改双向可退）。
    auto* confirmRegion = new QWidget(panel);
    confirmRegion->setObjectName(QString::fromLatin1(kConfirmRegionObjectName));
    auto* confirmLayout = new QHBoxLayout(confirmRegion);
    confirmLayout->setContentsMargins(0, 0, 0, 0);
    auto* confirmText = new QLabel(confirmRegion);
    confirmText->setObjectName(QString::fromLatin1(kConfirmTextObjectName));
    confirmText->setWordWrap(true);
    auto* confirmYes = new QPushButton(QString::fromUtf8("确认应用"), confirmRegion);
    confirmYes->setObjectName(QString::fromLatin1(kConfirmYesObjectName));
    auto* confirmNo = new QPushButton(QString::fromUtf8("再改改"), confirmRegion);
    confirmNo->setObjectName(QString::fromLatin1(kConfirmNoObjectName));
    confirmLayout->addWidget(confirmText, 1);
    confirmLayout->addWidget(confirmYes);
    confirmLayout->addWidget(confirmNo);
    confirmRegion->hide();
    ctx->confirmRegion = confirmRegion;
    ctx->confirmText = confirmText;
    layout->addWidget(confirmRegion);

    // ---- 操作行：粘贴｜应用…｜取消恢复（｜可选"开发诊断"开关——分组
    // 异名：会话显示开关不与编辑操作混为同组语义，仅按选项附加行尾）。
    auto* buttonRow = new QWidget(panel);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    auto* pasteButton = new QPushButton(QString::fromUtf8("从剪贴板粘贴"), buttonRow);
    pasteButton->setObjectName(QString::fromLatin1(kPasteObjectName));
    pasteButton->setToolTip(
        QString::fromUtf8("粘贴「键<TAB或逗号>值」多行文本，逐行独立判定"));
    auto* applyButton = new QPushButton(QString::fromUtf8("应用…"), buttonRow);
    applyButton->setObjectName(QString::fromLatin1(kApplyObjectName));
    if (outlet == nullptr) {
        // 未接编辑出口＝域编辑器未装配：禁用应用并说明原因（不虚构
        // 可用性——§11.4；tooltip 即 kNoOutletTooltip 契约文案）。
        applyButton->setEnabled(false);
        applyButton->setToolTip(QString::fromUtf8(kNoOutletTooltip));
    }
    auto* cancelButton = new QPushButton(QString::fromUtf8("取消恢复"), buttonRow);
    cancelButton->setObjectName(QString::fromLatin1(kCancelObjectName));
    buttonLayout->addWidget(pasteButton);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(cancelButton);
    if (options.showDevDiagnosticsToggle) {
        // UX-04 高级面板承载项："开发诊断"是会话级显示开关——只回调
        // options，不进模型（不产生脏标记/不触发确认：UX-07 后半句的
        // 结构保证；GUI 测试钉住该行为）。
        auto* devDiag = new QCheckBox(QString::fromUtf8("开发诊断"), buttonRow);
        devDiag->setObjectName(QString::fromLatin1(kDevDiagObjectName));
        buttonLayout->addWidget(devDiag);
        if (options.onDevDiagnosticsToggled) {
            QObject::connect(devDiag, &QCheckBox::toggled, panel, [ctx](bool checked) {
                ctx->options.onDevDiagnosticsToggled(checked);
            });
        }
    }
    layout->addWidget(buttonRow);

    // =================================================================
    // 接线（既有信号＋lambda——零 Q_OBJECT，目标不开启 AUTOMOC）
    // =================================================================

    // 筛选：输入即过滤（纯可见性裁剪——模型不改任何编辑态），表格重画。
    QObject::connect(filterEdit, &QLineEdit::textChanged, panel, [ctx](const QString& text) {
        ctx->model.setFilterText(text.toStdString());
        refreshTable(*ctx);
    });

    // 单位切换：按量纲批量切显示制式（KIN-12——只动投影，不动值/脏标记/
    // 确认态）。用 activated（仅用户交互发射）：装配期的初始选中是
    // setCurrentIndex 程序动作，不会误触发本回调——无需守卫分支。
    QObject::connect(unitSwitch, &QComboBox::activated, panel,
                     [ctx, unitSwitch](int index) {
                         if (index < 0
                             || index >= static_cast<int>(ctx->unitEntries.size())) {
                             return;  // 语义表外下标——不可达，防御性忽略
                         }
                         const auto& kindAndSymbol = ctx->unitEntries[static_cast<std::size_t>(index)];
                         const auto unit = core::UnitToken::find(kindAndSymbol.second.toStdString());
                         if (!unit.has_value()) {
                             return;  // 候选表只含冻结 token——不可达；防御性忽略
                         }
                         ctx->model.setDisplayUnitForKind(kindAndSymbol.first, *unit);
                         refreshTable(*ctx);
                     });

    // 值列就地编辑：用户提交文本→模型判定（成功暂存/失败就地原因并保留
    // 原值），随后整体刷新让值列文本回退、状态列现原因。两点实现纪律：
    //   1. 刷新守卫期（updating）的 setText 是程序行为，不当作用户编辑；
    //   2. 刷新会重建并删除包括事件源 item 在内的全部行项——itemChanged
    //      的调用栈还在该 item 上，就地删除有悬垂风险，故经单发定时器
    //      排队到当前事件处理完成后执行（值文本已按值捕获）。
    QObject::connect(table, &QTableWidget::itemChanged, panel,
                     [ctx, panel](QTableWidgetItem* item) {
                         if (ctx->updating || item == nullptr
                             || item->column() != kValueColumn) {
                             return;
                         }
                         const QTableWidget* owner = item->tableWidget();
                         const QTableWidgetItem* keyItem =
                             owner == nullptr ? nullptr : owner->item(item->row(), 0);
                         if (keyItem == nullptr) {
                             return;
                         }
                         const std::string key =
                             keyItem->data(Qt::UserRole).toString().toStdString();
                         const std::string text = item->text().toStdString();
                         if (key.empty()) {
                             return;
                         }
                         // 规则执行点（与批量粘贴同一条 parseFieldValueText
                         // 规则）：失败就地显示原因、值不动；成功暂存待确认。
                         QTimer::singleShot(0, panel, [ctx, key, text]() {
                             ctx->model.setEditText(key, text);
                             refreshTable(*ctx);
                         });
                     });

    // 批量粘贴：读剪贴板→逐行独立判定→影响明细就地呈现（非模态——
    // 不弹任何对话框；拒绝行保留原值不影响其他行）。
    QObject::connect(pasteButton, &QAbstractButton::clicked, panel, [ctx]() {
        const QString clipboardText = QGuiApplication::clipboard()->text();
        const BatchPasteReport report =
            ctx->model.batchPasteText(clipboardText.toStdString());
        refreshTable(*ctx);
        // 影响明细（"批量影响明细"验收点的呈现面）：摘要行＋逐行拒绝
        // 原因＋逐项影响（"标签：旧 → 新"与确认区同源格式）。
        QString summary =
            QString::fromUtf8("粘贴 %1 行：接受 %2 项、拒绝 %3 项")
                .arg(static_cast<qulonglong>(report.lines.size()))
                .arg(static_cast<qulonglong>(report.acceptedCount))
                .arg(static_cast<qulonglong>(report.rejectedCount));
        for (const auto& line : report.lines) {
            if (line.accepted) {
                continue;
            }
            summary += u8"\n拒绝　第" + QString::number(static_cast<qulonglong>(line.line))
                       + u8"行　" + QString::fromStdString(line.key) + u8"："
                       + QString::fromStdString(line.reason);
        }
        for (const auto& change : report.impact) {
            summary += u8"\n影响　" + changeLineText(ctx->model, change);
        }
        ctx->impact->setText(summary);
    });

    // 应用（UX-07 表单级确认的第一步）：有错先定位（就地反馈＋滚入
    // 视野）；无修改就地说明；有修改→呈现确认区（非模态——用户仍可
    // 继续改表格，确认区只是待应用明细的呈现）。
    QObject::connect(applyButton, &QAbstractButton::clicked, panel, [ctx]() {
        if (ctx->outlet == nullptr) {
            return;  // 禁用态防御（按钮已 disabled——不可达，双保险）
        }
        if (ctx->model.hasErrors()) {
            // 存在非法输入：不允许进入确认——就地反馈＋错误定位（首个
            // 错误行滚入视野；被筛掉的行由反馈文案兜底）。
            revealFirstError(*ctx);
            ctx->impact->setText(
                QString::fromUtf8(kApplyHasErrorsPrefix)
                + QString::fromStdString(ctx->model.statusText(*ctx->model.firstErrorKey())));
            ctx->confirmRegion->hide();
            return;
        }
        if (ctx->model.pendingChanges().empty()) {
            ctx->impact->setText(QString::fromUtf8(kApplyNoChangesText));
            ctx->confirmRegion->hide();
            return;
        }
        ctx->confirmText->setText(buildConfirmText(*ctx));
        ctx->confirmRegion->show();
    });

    // 确认应用（UX-07 第二步——用户已在确认区看到逐项明细）：模型移交
    // 编辑出口并把基线推进到新值；反馈明示"断言与放行归 project"。
    // 移交前先取待应用数（移交成功后 pendingChanges 已清空）。
    QObject::connect(confirmYes, &QAbstractButton::clicked, panel, [ctx]() {
        if (ctx->outlet == nullptr) {
            return;  // 同上——禁用态防御
        }
        const std::size_t pendingCount = ctx->model.pendingChanges().size();
        const ConfirmApplyResult result = ctx->model.confirmApply(*ctx->outlet);
        ctx->confirmRegion->hide();
        if (result.ok) {
            refreshTable(*ctx);
            ctx->impact->setText(QString::fromUtf8(kApplyHandedOffPrefix)
                                 + QString::number(static_cast<qulonglong>(pendingCount))
                                 + QString::fromUtf8(kApplyHandedOffSuffix));
        } else {
            ctx->impact->setText(QString::fromStdString(result.reason));
        }
    });

    // 确认区"再改改"：不移交、隐藏确认区（确认可反悔——尚未点确认前
    // 修改集仍是暂存态，用户可继续编辑）。
    QObject::connect(confirmNo, &QAbstractButton::clicked, panel,
                     [ctx]() { ctx->confirmRegion->hide(); });

    // 取消恢复：丢弃全部暂存与就地错误——显示回到基线（UX-05/DTB 验收
    // 列"取消恢复"；单位制式与筛选是显示设置，不随取消回滚）。
    QObject::connect(cancelButton, &QAbstractButton::clicked, panel, [ctx]() {
        ctx->model.cancelRestore();
        ctx->confirmRegion->hide();
        refreshTable(*ctx);
        ctx->impact->setText(QString::fromUtf8(kCancelRestoredText));
    });

    // 首帧：按模型现状重画表格；随后装配单位切换选项（项表进上下文供
    // 回调查语义；初始选中＝该量纲首字段当前制式的精确匹配项）。
    refreshTable(*ctx);
    {
        const std::vector<UnitSwitchGroup> groups = [&model] {
            std::vector<UnitSwitchGroup> result;
            const std::vector<UnitSwitchGroup> candidates = {
                {core::QuantityKind::Length, "长度", {"m", "cm", "mm"}},
                {core::QuantityKind::Angle, "角度", {"rad", "deg"}},
            };
            for (const auto& candidate : candidates) {
                // 仅模型中真实存在该量纲字段时出组——空组的切换是无意义控件。
                const bool kindPresent =
                    std::any_of(model.fields().begin(), model.fields().end(),
                                [&candidate](const QuantityFieldSpec& s) {
                                    return s.kind == candidate.kind;
                                });
                if (kindPresent) {
                    result.push_back(candidate);
                }
            }
            return result;
        }();
        bool initialApplied = false;  // 单一下拉只呈现一个当前项——首个命中的组即定初始选中
        for (const auto& group : groups) {
            // 该组初始制式＝该量纲首字段的当前显示单位（同组字段被
            // setDisplayUnitForKind 同步切换，取首字段即可代表全组）。
            QString currentSymbol;
            for (const auto& field : ctx->model.fields()) {
                if (field.kind == group.kind) {
                    currentSymbol = QString::fromUtf8(ctx->model.displayUnit(field.key).symbol());
                    break;
                }
            }
            for (const char* symbol : group.symbols) {
                const QString symbolText = QString::fromUtf8(symbol);
                ctx->unitEntries.emplace_back(group.kind, symbolText);
                unitSwitch->addItem(QString::fromUtf8(group.kindLabel) + u8"：" + symbolText);
                // 初始选中：与该组首字段当前显示单位精确相等的项（字符串
                // 精确比对而非 endsWith——"cm" 也以"m"结尾，后缀匹配会
                // 误选）。注意 addItem 会把空下拉的当前项自动置为 0——
                // 不能以 currentIndex<0 作"尚未选中"判据，须用显式旗标。
                if (!initialApplied && symbolText == currentSymbol) {
                    unitSwitch->setCurrentIndex(unitSwitch->count() - 1);
                    initialApplied = true;
                }
            }
        }
    }

    return panel;
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
