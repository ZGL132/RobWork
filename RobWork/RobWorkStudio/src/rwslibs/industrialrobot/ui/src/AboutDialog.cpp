/**
 * @file   AboutDialog.cpp
 * @brief  帮助入口与关于对话框的实现——静态白名单 ∩ 装配报告的清单装配、
 *         版本基线呈现行装配、用户手册入口与关于对话框 Widget（§11.4）。
 *
 * 设计依据：
 *   - units/ui.md §11.4（关于对话框数据＝静态白名单 ∩ AssemblyReport
 *     〔逐插件：标题/版本/面板数/命令数/装配状态〕＋产品/组件版本——
 *     版本数据由 L5 提供、ui 呈现；帮助入口链接用户手册）、§11.1（静态
 *     白名单八 token 冻结词表与顺序）、§10.9（PluginAssemblyReport 值
 *     形状）、§3.5（插件标题/装配状态键族——值经 UiText 唯一出口解析，
 *     界面零内部插件名——UX-02）、§3.4（UI 线程纪律——对话框与打开动
 *     作只在帮助命令处理器路径）；
 *   - 需求 UX-14、NFR-DEP-05；任务契约 tasks/foundation/UI-T10.json
 *     acceptance 1（UI-PLG-2）＋acceptance 2（O-31：版本数据 L5 注入、
 *     插件清单静态白名单——本 TU 零对端类型）。
 *
 * 背景说明（清单一致性为什么收口在一个装配函数）：acceptance 1 的两半句
 * "清单与白名单∩报告一致、与静态白名单一致"都是数据事实——装配规则只
 * 写一遍（aboutPluginRows），模型测试与 GUI 呈现消费同一份输出，不存在
 * "对话框里另写一套过滤逻辑"的第二权威（NFR-MNT-03；PolicySummaryCard
 * 行装配同案）。
 *
 * 线程模型：装配纯函数任意线程可调用；createAboutDialog/openUserManual
 * 只允许 UI 线程（§3.4 M-1——帮助命令处理器路径）。
 */

#include <sdurws/ird/ui/AboutDialog.hpp>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <array>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/ui/UiText.hpp>  // resolveText/notApplicableText（唯一出口）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 静态白名单（§11.1 冻结词表——编译期常量表）
// =====================================================================

std::vector<std::string> pluginUiWhitelist()
{
    // §11.1 原文词表（八 token 逐字对齐；阶段 A 占位先行登记——"modeling,
    // requirements, kinematics, trajectory, dynamics, selection,
    // optimization, workflow"）。顺序即装配顺序与清单呈现序（行锚测试
    // 钉序——重排＝词表变更，必须走单元卡修订而非代码私改）。
    static const std::array<std::string, 8> kTokens{{
        "modeling",
        "requirements",
        "kinematics",
        "trajectory",
        "dynamics",
        "selection",
        "optimization",
        "workflow",
    }};
    return {kTokens.begin(), kTokens.end()};
}

TextKey pluginTitleKey(const std::string& pluginId)
{
    // 键族词形：plugin.<token>.title（§3.5 UI-T10 冻结登记）。token 只进
    // 键（机器面），不进用户文本——键的值半区（中文名）在 UiText 内建表。
    return "plugin." + pluginId + ".title";
}

TextKey pluginAssemblyStatusLabelKey(PluginAssemblyStatus status)
{
    // 键族词形：plugin.assembly.<state>.label（三态词根与枚举值一一对应
    // ——switch 全列，新增枚举值漏登记＝编译期警告而非静默串）。
    switch (status) {
    case PluginAssemblyStatus::NotAssembled:
        return "plugin.assembly.not-assembled.label";
    case PluginAssemblyStatus::Ok:
        return "plugin.assembly.ok.label";
    case PluginAssemblyStatus::Failed:
        return "plugin.assembly.failed.label";
    }
    // 可达性：枚举全覆盖（调用方传越界值属未定义行为——与全产品枚举
    // switch 口径一致，不加运行期兜底分支以免吞掉词表漂移）。
    return {};
}

// =====================================================================
// 插件清单装配（§11.4 白名单 ∩ 报告——唯一权威规则）
// =====================================================================

std::vector<AboutPluginRow>
aboutPluginRows(const std::vector<PluginAssemblyReport>& reports)
{
    // 装配规则 1：行集＝白名单全集（行序＝白名单序）——清单与静态白名单
    // 一致的字面承载（acceptance 1）；报告只是充实数据源，不产生行。
    const std::vector<std::string> tokens = pluginUiWhitelist();
    std::vector<AboutPluginRow> rows;
    rows.reserve(tokens.size());

    for (const std::string& token : tokens) {
        // 装配规则 2：报告对位（每插件恰好一次——§11.1；重复 id 取首个
        // 的确定性防御，避免装配器违约时输出抖动）。
        const PluginAssemblyReport* matched = nullptr;
        for (const PluginAssemblyReport& report : reports) {
            if (report.pluginId == token) {
                matched = &report;
                break;
            }
        }

        AboutPluginRow row;
        row.pluginId = token;
        // 标题列：经键解析取中文名（token 不进用户文本——UX-02）。键未
        // 登记＝键表与词表失同步，resolveText fail-fast 上抛（不显示键名
        // 不返回空串——呈现层契约，UiText.hpp 错误语义行）。
        row.titleText = resolveText(pluginTitleKey(token));

        if (matched != nullptr) {
            // 命中报告：装配状态与计数取报告值（失败码透传——失败证据
            // 不丢弃，呈现归 §9.1 诊断面/UI-T13）。
            row.status = matched->ok ? PluginAssemblyStatus::Ok
                                     : PluginAssemblyStatus::Failed;
            row.panelCount = matched->panelsLoaded;
            row.commandCount = matched->commandsRegistered;
            row.failureDiagnostics = matched->failureDiagnostics;
        } else {
            // 未命中：NotAssembled、计数 0（"没有面板/命令"是装配事实，
            // 不是占位语义——阶段 A 占位 token 的常态行）。
            row.status = PluginAssemblyStatus::NotAssembled;
        }
        // 状态列文本：三态键族解析（全态登记——pluginAssemblyStatusLabelKey
        // 的 switch 契约）。
        row.statusText = resolveText(pluginAssemblyStatusLabelKey(row.status));
        // 版本列：§10.9 报告形状无版本字段——阶段 A 恒「不适用」占位
        // （ERR-01 显式化，不虚构版本号；插件版本来源随装配任务增量登记，
        // 届时只扩本装配点，列模型不变）。
        row.versionText = notApplicableText();
        rows.push_back(std::move(row));
    }

    // 装配规则 3（交集丢弃）无需显式动作：行只从白名单产生，报告中的白
    // 名单外条目天然不进输出（清单不受报告数据异常污染——防御性注释即
    // 规则本体，测试以"注入白名单外报告后行集不变"自证）。
    return rows;
}

// =====================================================================
// 版本行装配（NFR-DEP-05 呈现——注入值原样，占位不伪造）
// =====================================================================

std::vector<AboutVersionRow> aboutVersionRows(const AboutVersionBaseline& baseline)
{
    std::vector<AboutVersionRow> rows;

    if (!baseline.available) {
        // 基线未注入（阶段 A 无 WP-24-T01 产出/部署未携带）：单行占位，
        // 不虚构任何版本号（与策略摘要卡"未装载"同口径——「占位行」也是
        // 信息：告诉用户基线面尚未装载，而不是显示假版本）。
        AboutVersionRow placeholder;
        placeholder.key = "baseline";
        placeholder.label = kAboutBaselineRowLabel;
        placeholder.valueText = kAboutBaselineNotLoadedText;
        rows.push_back(std::move(placeholder));
        return rows;
    }

    // 产品版本行（首行——kAboutProductVersionLabel；空串版本＝基线未登记
    // 产品版本项→「不适用」占位，不伪造）。
    AboutVersionRow product;
    product.key = "product";
    product.label = kAboutProductVersionLabel;
    product.valueText = baseline.productVersion.empty() ? notApplicableText()
                                                        : baseline.productVersion;
    rows.push_back(std::move(product));

    // 组件版本行（序＝注入序，呈现不重排；行键按注入序编号——组件名来
    // 自注入值，不作控件命名词根以隔离注入内容与 objectName 词表）。
    for (std::size_t i = 0; i < baseline.components.size(); ++i) {
        const AboutComponentVersion& component = baseline.components[i];
        // 空组件名＝注入方装配错误（无名行无法呈现也无法定位）——fail-fast
        // 而不是渲染"：值"空标签行（调用方错误 fail-fast，AGENTS §3）。
        if (component.componentName.empty()) {
            throw std::invalid_argument(
                "ui/about/version-rows: 组件版本行缺少组件名（注入方装配错误）");
        }
        AboutVersionRow row;
        row.key = "component-" + std::to_string(i + 1);  // 1 起编号——呈现序可读
        row.label = component.componentName;
        row.valueText = component.versionText.empty() ? notApplicableText()
                                                      : component.versionText;
        rows.push_back(std::move(row));
    }
    return rows;
}

// =====================================================================
// 用户手册入口（§11.4——share\ 帮助文件）
// =====================================================================

std::string userManualPath()
{
    // 落位口径（实现口径登记 ui.md §16.7 v1.2）：部署树 bin\ 与 share\
    // 同级——"<appDir>/../share/ird/user-manual/index.html"。以应用目录为
    // 基准（测试/部署环境同构解析，不依赖工作目录）；QCoreApplication
    // 未构造时 applicationDirPath 返回空串→路径退化为相对形态，存在性
    // 判定照常工作（模型测试环境恒已构造，正常路径不触达该分支）。
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString manual = appDir + QStringLiteral("/../share/ird/user-manual/index.html");
    // 规范化消除 ".." 段（错误信息与日志中的路径可读；QFileInfo::exists
    // 本身对 ".." 容忍，规范化只为呈现一致性）。
    const QFileInfo info(manual);
    return info.absoluteFilePath().toStdString();
}

bool openUserManual()
{
    // 第 1 步：入口文件存在性判定（§11.4 用户可见性纪律：文件缺失时不
    // 启动打开动作、不静默——返回 false 由调用方给用户可见反馈）。
    const QString manual = QString::fromStdString(userManualPath());
    if (!QFileInfo::exists(manual)) {
        return false;
    }
    // 第 2 步：交系统打开器（"链接用户手册"的语义本体——html 入口文件
    // 由系统默认处理方式接管；本函数不内嵌浏览器控件，保持壳零重量）。
    return QDesktopServices::openUrl(QUrl::fromLocalFile(manual));
}

// =====================================================================
// 关于对话框（§11.4 呈现面——模型产出逐字渲染，零二次加工）
// =====================================================================

QDialog* createAboutDialog(const AboutVersionBaseline& baseline,
                           const std::vector<AboutPluginRow>& pluginRows,
                           QWidget* parent)
{
    auto* dialog = new QDialog(parent);
    dialog->setObjectName(QString::fromUtf8(kAboutDialogObjectName));
    dialog->setWindowTitle(QString::fromUtf8(kAboutDialogTitle));
    // 模态语义的承载决定：窗口模态＋调用方 open() 非阻塞打开（不用 exec()
    // ——命令处理器内嵌套事件循环会让 §7.7 提交时序重入，§3.4 纪律；
    // GUI 测试因此可在同栈断言对话框可见性）。
    dialog->setWindowModality(Qt::WindowModal);
    dialog->resize(520, 420);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // ---- 上半区：版本信息（NFR-DEP-05 呈现面）----
    auto* versionsTitle = new QLabel(QString::fromUtf8(kAboutVersionsSectionTitle), dialog);
    layout->addWidget(versionsTitle);

    // 版本行集经模型装配（同源纪律——GUI 断言与模型测试消费同一函数输出；
    // 本工厂不做任何占位/取值判定）。
    const std::vector<AboutVersionRow> versionRows = aboutVersionRows(baseline);
    for (const AboutVersionRow& row : versionRows) {
        // 行形态＝只读文本标签"标签：值"（全角冒号——与策略摘要卡行
        // 形态一致；objectName＝词根＋行键，测试定位锚）。
        auto* rowLabel = new QLabel(QString::fromUtf8(row.label) + u8"："
                                        + QString::fromUtf8(row.valueText),
                                    dialog);
        rowLabel->setObjectName(QString::fromUtf8(kAboutVersionRowObjectNamePrefix)
                                + QString::fromStdString(row.key));
        rowLabel->setWordWrap(true);
        layout->addWidget(rowLabel);
    }

    // 分隔线（版本区与清单区的视觉分界——纯呈现，无语义）。
    auto* separator = new QFrame(dialog);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    // ---- 下半区：插件清单（§11.4 五列表）----
    auto* pluginsTitle = new QLabel(QString::fromUtf8(kAboutPluginsSectionTitle), dialog);
    layout->addWidget(pluginsTitle);

    auto* table = new QTableWidget(static_cast<int>(pluginRows.size()), 5, dialog);
    table->setObjectName(QString::fromUtf8(kAboutPluginTableObjectName));
    // 列头＝§11.4 冻结列词（契约头常量——列序即原文列序：标题/版本/
    // 面板数/命令数/装配状态）。
    table->setHorizontalHeaderLabels({QString::fromUtf8(kAboutPluginColTitle),
                                      QString::fromUtf8(kAboutPluginColVersion),
                                      QString::fromUtf8(kAboutPluginColPanels),
                                      QString::fromUtf8(kAboutPluginColCommands),
                                      QString::fromUtf8(kAboutPluginColStatus)});
    // 清单是信息呈现不是编辑面：整表只读、整行选中、列宽随内容（行数≤
    // 白名单大小＝8，无滚动压力）。
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    table->resizeColumnsToContents();
    table->verticalHeader()->setVisible(false);

    // 逐行填充（行序＝装配序＝白名单序；单元格文本＝模型产出值——五列
    // 中三个文本列已是最终呈现文本，两个计数列 to 整数十进制）。
    for (int r = 0; r < static_cast<int>(pluginRows.size()); ++r) {
        const AboutPluginRow& row = pluginRows[static_cast<std::size_t>(r)];
        table->setItem(r, 0, new QTableWidgetItem(QString::fromUtf8(row.titleText)));
        table->setItem(r, 1, new QTableWidgetItem(QString::fromUtf8(row.versionText)));
        table->setItem(r, 2, new QTableWidgetItem(QString::number(
                                      static_cast<qulonglong>(row.panelCount))));
        table->setItem(r, 3, new QTableWidgetItem(QString::number(
                                      static_cast<qulonglong>(row.commandCount))));
        table->setItem(r, 4, new QTableWidgetItem(QString::fromUtf8(row.statusText)));
    }
    layout->addWidget(table);

    // 失败诊断明细不在本清单呈现：AboutPluginRow.failureDiagnostics 只承
    // 载失败证据（模型层保留），诊断的文案键解析与条目呈现归 §9.1 诊断
    // 呈现面（UI-T13）——清单只呈现 §11.4 五列，不复制诊断呈现职责
    // （PA-1 权威唯一；UX-02：内部插件 token 亦不进任何用户可见面）。

    layout->addStretch(1);
    return dialog;
}

}  // namespace ui
}  // namespace ird
