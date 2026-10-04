/**
 * @file   ModelingPanelWidget.cpp
 * @brief  建模域面板实现——五区控件装配与事件转接（卡 §9.7.1/§9.7.2）。
 *
 * 设计依据：units/modeling.md §9.7 系（信息架构与接线）、§9.7.4（事件驱动
 * ／零缓存／UI 线程）、ui.md §4.2/§6.6（UX-02）；契约 WP-13-T15
 * acceptance 1/2/3。实现纪律：零计算逻辑（判定唯一在计算库——本文件只有
 * 控件装配与"事件→呈现模型/域入口"的转接）；一切投影现取（零权威数据
 * 缓存）；错误就地呈现（无任何 exec()/模态路径——UX-07）。
 */

#include "ModelingPanelWidget.hpp"

#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>                            // 六轴重置确认对话（UI-T47——有损操作知情面）
#include <QLabel>
#include <QScrollBar>
#include <QTabWidget>
#include <QTime>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <array>
#include <map>
#include <string>

#include <sdurws/ird/ui/UiText.hpp>   // 文案键解析（UI-T25——迁移标记标签挂键；
                                      // 构造期命令标题 resolver 尚未注入，此处直读
                                      // UiText 静态表，requirements 面板先例同款）
#include <sdurws/ird/ui/UiTheme.hpp>  // 工业风主题（UI-T37 基建——批次B B5 接入；
                                      // 调色板五色词表＝唯一色值源，防彩虹化）
#include <sdurws/ird/ui/UiTypes.hpp>  // ui::TextKey（命令标题键——呈现层键解析约定）
#include <sdurws/ird/core/Units.hpp>  // core::UnitToken::find/QuantityKind（编辑页字段
                                      // 量纲 token——SA-12 换算入口的装配面）

#include "RpyPresentation.hpp"        // 单元插件私有头——RPY 反解（编辑页 origin
                                      // 基线回填——UI-T53 提升共享）

namespace sdurws::ird::modeling {
namespace {

/// 树锚列（隐藏列——节点锚 ObjectId 规范文本；L-1 关联键的控件承载）。
constexpr int kAnchorColumn = 1;  // 锚列（树第 2 列——UI-T47 gui 定位复用）
/// 树标签列（呈现列——localName＋工程用语标签，UX-02）。
constexpr int kLabelColumn = 0;

// （rowText 值/单位拼接辅助已随 UI-T27 P0-2 移除——编辑器文本＝纯值，
//  单位并入行标签；两处原调用点分别改纯值回显/标签拼接。）

/// 使能三态→控件只读位（灰显＝值可见不可改——§7.2/L-7 共用语义）。
bool rowReadOnly(FieldEnablement en) noexcept
{
    return en != FieldEnablement::Editable;
}

// ---- UI-T53 关节详细编辑页字段键（编辑页 12 行——outlet 分组装配与基线
//      回填的寻址锚；小写连字符词法与共享检查器字段键同风格）------------
constexpr const char* kJointAxisXKey = "axis-x";
constexpr const char* kJointAxisYKey = "axis-y";
constexpr const char* kJointAxisZKey = "axis-z";
constexpr const char* kJointOriginXKey = "origin-x";
constexpr const char* kJointOriginYKey = "origin-y";
constexpr const char* kJointOriginZKey = "origin-z";
constexpr const char* kJointOriginRollKey = "origin-r";
constexpr const char* kJointOriginPitchKey = "origin-p";
constexpr const char* kJointOriginYawKey = "origin-yaw";
constexpr const char* kJointZeroOffsetKey = "zero-offset";
constexpr const char* kJointBoundsMinKey = "bounds-min";
constexpr const char* kJointBoundsMaxKey = "bounds-max";

/// 关节零位/限位的量纲分流（Prismatic＝移动 m，其余＝转动 rad——与共享
/// 检查器 jointQuantityKind 同一规则；两处为呈现辅助零业务判定，公式/规则
/// 漂移由各自单元测试钉住——rotationToRpy 提升前同款先例）。
core::QuantityKind jointEditQuantityKind(JointType type) noexcept
{
    return type == JointType::Prismatic ? core::QuantityKind::Length
                                        : core::QuantityKind::Angle;
}

/// 量纲→单位 token（建模字段 SI 恒同显示——m/rad 制式；KIN-12 显示切换
/// 是 ParamTablePanel 会话面不在装配面）。
core::UnitToken jointEditUnitToken(core::QuantityKind kind)
{
    switch (kind) {
    case core::QuantityKind::Length: return core::UnitToken::find("m").value();
    case core::QuantityKind::Angle: return core::UnitToken::find("rad").value();
    default: break;
    }
    return core::UnitToken::find("1").value();  // 轴向＝无量纲单位向量
}

}  // namespace

// =====================================================================
// 关节详细编辑页的移交出口（UI-T53——IFormEditOutlet 的域转译壳）
// =====================================================================

/**
 * @brief ParamEditModel::confirmApply → 本面板域编辑流的移交壳（ui 公共件
 *        O-31 最小端口的产品实现）。
 *
 * 本类零判定零状态（仅回指宿主面板）——分组装配、域提交与拒绝分流全部
 * 在 ModelingPanelWidget::applyJointDetailEdits（与属性行提交轨同一落点，
 * L-2 sink 分流复用）。线程：applyEdits 仅 UI 线程（公共件契约——§3.4）。
 */
class ModelingPanelWidget::JointDetailEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit JointDetailEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}

    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        m_owner.applyJointDetailEdits(editSet);  // 移交即转接——零第二实现
    }

private:
    ModelingPanelWidget& m_owner;  ///< 宿主面板（非 owning——面板持有本出口）
};

// =====================================================================
// 构造与五区骨架
// =====================================================================

ModelingPanelWidget::ModelingPanelWidget(bool writable, QWidget* parent)
    : QWidget(parent), m_writable(writable)
{
    // 区位总布局：左右分栏（树｜属性）＋下部叠放（工具/就绪/预览——
    // 页签承载三区，对应 UX-09 五区布局的建模域投影）。
    auto* mainLayout = new QHBoxLayout(this);
    auto* left = new QVBoxLayout();
    auto* right = new QVBoxLayout();
    mainLayout->addLayout(left, 5);
    mainLayout->addLayout(right, 4);
    buildStructureTreePane(left);   // 区①建模结构树（左栏）
    buildPropertyPane(right);       // 区②属性编辑区（右栏）

    auto* tabs = new QTabWidget(this);
    auto* editPage = new QWidget(tabs);
    auto* toolsPage = new QWidget(tabs);
    auto* readinessPage = new QWidget(tabs);
    auto* previewPage = new QWidget(tabs);
    buildJointEditPane(new QVBoxLayout(editPage));     // 区⑥编辑页（UI-T53——关节详细编辑）
    buildToolsPane(new QVBoxLayout(toolsPage));        // 区③域工具区（StageId=modeling）
    buildReadinessPane(new QVBoxLayout(readinessPage));// 区④就绪与诊断条
    buildPreviewPane(new QVBoxLayout(previewPage));    // 区⑤预览页（仅已应用修订）
    // 编辑页签居首（B.1 复杂对象编辑模式——结构树选择→详细编辑页的主入口；
    // 与共享检查器高频标量页分野：大批量/权威敏感字段收口本页）。
    tabs->addTab(editPage, QStringLiteral("编辑"));
    tabs->addTab(toolsPage, QStringLiteral("工具"));
    tabs->addTab(readinessPage, QStringLiteral("就绪"));
    tabs->addTab(previewPage, QStringLiteral("预览"));
    right->addWidget(tabs, 1);
    // 编辑页出口（构造期一次——转译到本面板域编辑流；域裁决唯一在域函数）。
    m_jointEditOutlet = std::make_unique<JointDetailEditOutlet>(*this);

    // 就地状态行（错误/横幅——非模态呈现的唯一出口，置底部常驻）。
    // UI-T41 批次B（B2）：状态行分级着色（词表色）＋可折叠诊断历史（最近
    // 20 条不清空覆盖——后条不再覆盖前条）；objectName 供契约测试定位。
    m_statusLine = new QLabel(this);
    m_statusLine->setWordWrap(true);
    m_statusLine->setObjectName(QStringLiteral("ird_modeling_status_line"));
    right->addWidget(m_statusLine);

    m_historyToggle = new QPushButton(
        QString::fromStdString(ui::resolveText("panel.modeling.history.title")), this);
    m_historyToggle->setObjectName(QStringLiteral("ird_modeling_history_toggle"));
    m_historyToggle->setCheckable(true);
    m_historyToggle->setToolTip(QString::fromStdString(
        ui::resolveText("panel.modeling.history.tooltip")));
    m_historyToggle->setStyleSheet(
        QStringLiteral("font-weight: 400; color: %1; padding: 2px; text-align: left;")
            .arg(QString::fromLatin1(ui::palette::kTextMuted)));
    right->addWidget(m_historyToggle);
    m_historyView = new QPlainTextEdit(this);
    m_historyView->setObjectName(QStringLiteral("ird_modeling_history_view"));
    m_historyView->setReadOnly(true);
    m_historyView->setMaximumHeight(96);
    m_historyView->hide();  // 默认折叠（B2——展开面，非弹窗；UX-07）
    right->addWidget(m_historyView);
    connect(m_historyToggle, &QPushButton::toggled, m_historyView,
            &QPlainTextEdit::setVisible);

    // 命令目录装载（§9.7.3 十条——装配数据，构造期一次；按钮使能态随
    // writable 与注入出口切换——见 refreshPanel/setWritable）。
    // UI-T26 流程分节：纵列按目录语义四组分节标题（对象与导入｜参数｜
    // 几何与姿态｜校验与导出）——只加分节标题行，按钮集合、目录顺序、
    // 使能逻辑零变化（分组语义＝目录 menuPath/作用域的呈现归纳，非新
    // 命令语义）。
    m_commands = modelingDomainCommands();
    // UI-T41 批次B（B5）：工业风主题安装（面板作用域——宿主 chrome/其他域
    // 面板不受影响）；分节标题色值从硬编码 #555 迁移至词表 kTextMuted
    // （UiTheme 五色词表＝唯一色值源，防彩虹化 NFR-DEP-05）。
    ui::applyIndustrialTheme(this);
    auto* toolsLayout = static_cast<QVBoxLayout*>(toolsPage->layout());
    auto addSectionHeader = [toolsPage, toolsLayout](const char* title) {
        auto* header = new QLabel(QString::fromUtf8(title), toolsPage);
        header->setStyleSheet(
            QStringLiteral("font-weight: 600; color: %1; padding-top: 4px;")
                .arg(QString::fromLatin1(ui::palette::kTextMuted)));
        toolsLayout->addWidget(header);
    };
    // 组首 id → 组标题（§9.7.3 目录行序内首组"对象与导入"无组首 id——
    // 循环前先挂；其余三组在组首命令按钮前挂）。
    const std::pair<const char*, const char*> sectionLeaders[] = {
        {"modeling.switch-authority", "参数"},
        {"modeling.generate-placeholder-geometry", "几何与姿态"},
        {"modeling.diff-baseline", "校验与导出"},
    };
    addSectionHeader("对象与导入");
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        for (const auto& [leaderId, title] : sectionLeaders) {
            if (m_commands[i].id == leaderId) {
                addSectionHeader(title);
            }
        }
        auto* btn = new QPushButton(QString::fromStdString(m_commands[i].titleKey), toolsPage);
        // UI-T41 批次B（B4）：悬停文案＝UiText tooltip 键（工程中文，UX-02——
        // 去裸命令 id 呈现）；解析空回退命令 id 原文（对账兜底，不虚构文案）。
        const std::string tooltipText =
            ui::resolveText("cmd." + m_commands[i].id + ".tooltip");
        btn->setToolTip(QString::fromStdString(
            tooltipText.empty() ? m_commands[i].id : tooltipText));
        connect(btn, &QPushButton::clicked, this, &ModelingPanelWidget::onCommandButtonClicked);
        m_commandButtons.push_back(btn);
        // 命令按 readOnlyAllowed 分组布局（简单纵排——呈现密度非本层关切）。
        toolsLayout->addWidget(btn);
    }
    toolsLayout->addStretch(1);  // 分节后的尾部弹性（纵列顶端对齐）
}

void ModelingPanelWidget::setCommandSubmit(CommandSubmitFn submitFn)
{
    m_commandSubmit = std::move(submitFn);
    // 提交出口可能在面板创建后才接入；立即重算按钮，避免按钮一直
    // 保持构造期禁用态（宿主装配顺序不应影响可用性呈现）。
    // UI-T43：使能判定收敛到 refreshCommandEnablement 单出口（原处循环
    // 与 setCommandAvailability/refreshPanel 三处重复实现——只读门控输入
    // 的合取语义在此唯一维护，见其头注）。
    refreshCommandEnablement();
}

void ModelingPanelWidget::setCommandAvailability(CommandAvailabilityFn availability)
{
    m_commandAvailability = std::move(availability);
    // UI-T43 修复（审核 P1）：本注入点此前缺失只读门控合取项——只读会话
    // 下宿主重新注入/刷新可用性快照时，可用性提供器返回 enabled=true 的
    // 写命令会被错误复活（setCommandSubmit/refreshPanel 两处均含
    // readonlyBlocked 判定，唯此处遗漏——三处不一致即门控旁路）。统一走
    // refreshCommandEnablement：可用性刷新永不越过 writable 门（L-7）。
    refreshCommandEnablement();
}

void ModelingPanelWidget::setPostEditAction(PostEditAction action)
{
    m_postEditAction = std::move(action);
}

void ModelingPanelWidget::setCommandTitleResolver(CommandTitleResolver resolver)
{
    m_titleResolver = std::move(resolver);
    // 绑定即时重渲染既有按钮（装配序无关——解析器可在面板创建后注入）；
    // 解析失败（空串）回退键名原文——呈现面不空洞、不伪造文案。
    for (std::size_t i = 0; i < m_commandButtons.size() && i < m_commands.size(); ++i) {
        if (!m_titleResolver) { break; }
        const std::string key = m_commands[i].titleKey;
        const QString resolved = m_titleResolver(key);
        m_commandButtons[i]->setText(
            resolved.isEmpty() ? QString::fromStdString(key) : resolved);
        // 批次B（B4）：tooltip 与标题同源重解析（装配序无关——解析器后注入
        // 时悬停文案同步升级为工程中文）。
        const QString resolvedTip =
            m_titleResolver("cmd." + m_commands[i].id + ".tooltip");
        if (!resolvedTip.isEmpty()) {
            m_commandButtons[i]->setToolTip(resolvedTip);
        }
    }
}

void ModelingPanelWidget::setEditTargetProvider(EditTargetProvider provider)
{
    m_editTarget = std::move(provider);
    // L-4 重演接线（UI-T41 A4）：基线提供器＝编辑目标现取（零缓存同源），
    // 刷新出口＝属性区重投影；重演入口经 replayOnRevisionEvent 显式触发
    // （事件驱动——模块 onRevisionCommitted 转达，无轮询面）。
    if (m_editTarget) {
        m_refresh.setSinks(
            [this]() -> ModelingWorkingSet& {
                // 返回引用契约（PanelRefresh.hpp）：调用期保证工作集在位——
                // 重演入口先经 replayOnRevisionEvent 空会话守卫，不触本路。
                static ModelingWorkingSet empty;
                ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
                return ws != nullptr ? *ws : empty;
            },
            [this]() { refreshPropertiesFromLastWorkingSet(); });
    }
}

// ---- UI-T41 A3/A2/A4：预览注入／命令回执／重演入口 ---------------------

void ModelingPanelWidget::setAppliedPreview(
    const std::optional<AppliedRevisionView>& view)
{
    m_appliedPreview = view;
    if (m_preview == nullptr) { return; }
    if (!m_appliedPreview.has_value()) {
        // 空态占位（不伪造内容——D-MDL-10：预览页仅呈现已应用修订）。
        m_preview->setPlainText(
            QStringLiteral("尚无已应用修订——预览页仅呈现已应用修订内容（D-MDL-10）。\n"
                           "编辑后请经顶栏『应用草稿』提交，预览随应用刷新。"));
        return;
    }
    QString text;
    for (const std::string& line : buildPreviewPage(*m_appliedPreview)) {
        text += QString::fromStdString(line) + QLatin1Char('\n');
    }
    if (text.isEmpty()) {
        text = QStringLiteral("已应用修订无预览摘要内容。");
    }
    m_preview->setPlainText(text);
}

void ModelingPanelWidget::setOutcomeMessage(const QString& message,
                                            OutcomeSeverity severity)
{
    // UI-T41 批次B（B2）：分级着色（UiTheme 词表——Success 绿＝接受、
    // Warning 橙＝拒绝/待处置、Info＝次级文本；阻断不设红——词表无红，
    // 防彩虹化纪律）＋诊断历史追加（最近 20 条，FIFO 裁剪——后条不再
    // 覆盖前条，回执可回溯）。
    const char* color = ui::palette::kTextMuted;
    const char* weight = "400";
    switch (severity) {
    case OutcomeSeverity::Success: color = ui::palette::kSuccess; break;
    case OutcomeSeverity::Warning: color = ui::palette::kWarning; weight = "600"; break;
    case OutcomeSeverity::Info: break;
    }
    m_statusLine->setStyleSheet(
        QStringLiteral("color: %1; font-weight: %2;")
            .arg(QString::fromLatin1(color), QString::fromLatin1(weight)));
    m_statusLine->setText(message);

    m_history.push_back(
        QStringLiteral("[%1] %2")
            .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message));
    if (m_history.size() > 20) {
        m_history.erase(m_history.begin());  // 定容 FIFO——会话内回执可回溯
    }
    m_historyView->setPlainText(m_history.join(QLatin1Char('\n')));
}

void ModelingPanelWidget::replayOnRevisionEvent()
{
    // 空会话守卫（重演需要权威工作集——无会话＝清队列静默返回，不虚构重演）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr) {
        m_refresh.clearPending();
        return;
    }
    if (m_refresh.pending().empty() && !m_refresh.manualInterventionRequired()) {
        return;  // 无待重演编辑＝零开销（事件驱动——无刷新风暴）
    }
    const ReplayOutcome outcome = m_refresh.onRevisionEvent(std::nullopt);
    if (outcome == ReplayOutcome::BlockedAtEdit) {
        const auto blockedAt = m_refresh.blockedAtIndex();
        setOutcomeMessage(QStringLiteral("修订刷新后存在 %1 条未应用编辑待手工处置"
                                         "（下标 %2 起重演被拒）——请重新编辑或放弃")
                              .arg(m_refresh.pending().size())
                              .arg(blockedAt.has_value() ? qint64(*blockedAt) : qint64(-1)));
    }
}

// ---- 区①建模结构树 ---------------------------------------------------

void ModelingPanelWidget::buildStructureTreePane(QVBoxLayout* left)
{
    // 自持导航 deprecated 横幅已随 UI-T36 退役（原 B1-SPEC §5.2 迁移期
    // 双形态并存的标记标签与 UiText 键族⑨ modeling 键同步删行——迁移期
    // 结束，本自持树升格为面板主导航呈现；与共享项目树的 L-R1 双向联动
    // 语义不变）。

    left->addWidget(new QLabel(QStringLiteral("建模结构"), this));
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderHidden(true);
    m_tree->setObjectName(QStringLiteral("ird_modeling_struct_tree"));  // UI-T47——gui 定位锚（B6 可访问性同款）
    m_tree->hideColumn(kAnchorColumn);  // 锚列隐藏——UX-02：界面不见哈希/内部标识
    // UI-T41 批次B（B6）：可访问性——树/状态行/预览挂 accessibleName
    // （读屏与自动化定位锚；文案挂 UiText 键，UX-02 同源）。
    m_tree->setAccessibleName(QString::fromStdString(
        ui::resolveText("panel.modeling.tree.accessible")));
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ModelingPanelWidget::onTreeSelectionChanged);
    left->addWidget(m_tree, 1);

    // 结构操作按钮行（UI-T47——§5.2 v0.28 四操作词表的面板承载；六轴重置
    // 为有损操作，确认对话在槽内呈现）。objectName 供 gui 具名用例定位。
    auto* structBar = new QWidget(this);
    auto* barLay = new QHBoxLayout(structBar);
    barLay->setContentsMargins(0, 0, 0, 0);
    structBar->setObjectName(QStringLiteral("ird_modeling_struct_bar"));
    static const char* kStructButtons[] = {
        "新增关节", "删除关节", "上移", "下移", "六轴重置",
    };
    static const char* kStructNames[] = {
        "ird_modeling_struct_add", "ird_modeling_struct_remove",
        "ird_modeling_struct_up", "ird_modeling_struct_down",
        "ird_modeling_struct_reset",
    };
    for (int i = 0; i < 5; ++i) {
        auto* btn = new QPushButton(QString::fromUtf8(kStructButtons[i]), structBar);
        btn->setObjectName(QString::fromLatin1(kStructNames[i]));
        btn->setToolTip(QString::fromUtf8(kStructButtons[i])
                        + QStringLiteral("（作用于选中关节；草稿级编辑——"
                                         "应用修改后才产生修订）"));
        connect(btn, &QPushButton::clicked, this,
                [this, i] { onStructureOpClicked(i); });
        barLay->addWidget(btn);
    }
    barLay->addStretch(1);
    left->addWidget(structBar);
}

// ---- 区②属性编辑区 ---------------------------------------------------

void ModelingPanelWidget::buildPropertyPane(QVBoxLayout* right)
{
    right->addWidget(new QLabel(QStringLiteral("属性"), this));
    auto* formHost = new QWidget(this);
    m_propertyForm = new QFormLayout(formHost);
    m_propertyForm->setLabelAlignment(Qt::AlignRight);
    right->addWidget(formHost);
}

// ---- 区③域工具区 -----------------------------------------------------

void ModelingPanelWidget::buildToolsPane(QVBoxLayout* bottom)
{
    bottom->setContentsMargins(0, 0, 0, 0);
    // 命令按钮在构造函数主体统一装载（目录来自 modelingDomainCommands）。
}

// ---- 区④就绪与诊断条 -------------------------------------------------

void ModelingPanelWidget::buildReadinessPane(QVBoxLayout* bottom)
{
    m_readinessCounts = new QLabel(this);  // 三组计数行（L0~L11 分层结果经逐项行呈现）
    bottom->addWidget(m_readinessCounts);
    m_readinessItems = new QTreeWidget(this);
    m_readinessItems->setHeaderHidden(true);
    m_readinessItems->setRootIsDecorated(false);
    bottom->addWidget(m_readinessItems, 1);
    // 逐项点击→定位跳转（L-1 反向半区——锚＝隐藏列的 ObjectId 规范文本）。
    connect(m_readinessItems, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item == nullptr) { return; }
                const std::string anchor =
                    item->text(kAnchorColumn).toStdString();
                if (anchor.empty()) { return; }  // 无主体行（note）——不可定位
                const auto oid = core::ObjectId::tryFromCanonical(anchor);
                if (oid.has_value()) {
                    m_selection.locate(*oid);  // 树滚动＋三维高亮（经 sink——零修订）
                }
            });
}

// ---- 区⑤预览页 -------------------------------------------------------

void ModelingPanelWidget::buildPreviewPane(QVBoxLayout* bottom)
{
    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);  // 只读预览（内容仅来自 AppliedRevisionView——D-MDL-10）
    m_preview->setAccessibleName(QStringLiteral("已应用修订预览"));  // B6 可访问性
    bottom->addWidget(m_preview, 1);
}

// =====================================================================
// 结构树重建（UI-T47 从 refreshPanel 抽出——与结构操作按钮槽共用）
// =====================================================================

void ModelingPanelWidget::refreshStructureTree(const ModelingWorkingSet& ws)
{
    // UI-T41 批次B（B3）：滚动位跨刷新保持（树行重建后滚回原视口——刷新
    // 不打断浏览位置；选中恢复既有锚机制不变）。
    const int treeScroll = m_tree->verticalScrollBar()->value();
    m_tree->blockSignals(true);  // 重建期的选中变化不回环（避免重投影风暴）
    m_tree->clear();
    const std::string lastAnchor =
        m_lastSelected.has_value() ? m_lastSelected->toCanonical() : std::string();
    for (const StructureNode& n : buildStructureTree(ws)) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(kLabelColumn, QString::fromStdString(n.displayLabel));
        item->setText(kAnchorColumn,
                      n.objectId.has_value()
                          ? QString::fromStdString(n.objectId->toCanonical())
                          : QString());
        item->setDisabled(!n.objectId.has_value());  // 分组行不可选（无锚——仅折叠呈现）
        // 选中保持：重建后按锚恢复当前行（L-1 选中态跨刷新恒定）。失效锚
        // （被删对象）恢复落空＝空选中——属性区空态如实（不伪造行）。
        if (!lastAnchor.empty()
            && item->text(kAnchorColumn) == QString::fromStdString(lastAnchor)) {
            m_tree->setCurrentItem(item);
        }
    }
    m_tree->blockSignals(false);
    m_tree->verticalScrollBar()->setValue(treeScroll);  // B3 滚动位还原
}

// =====================================================================
// 结构操作按钮槽（UI-T47——五钮共用落点；域裁决唯一在四原语）
// =====================================================================

void ModelingPanelWidget::onStructureOpClicked(int opIndex)
{
    m_threadGuard.assertOnUiThread();  // §3.4——编辑面

    // 六轴重置＝有损操作（§5.2 v0.28 ④——既有链参数被表值覆盖）：确认
    // 对话承载知情（域原语纯执行不内嵌确认——UI 层职责面）。
    if (opIndex == 4) {
        const QMessageBox::StandardButton confirmed = QMessageBox::question(
            this, QStringLiteral("六轴重置"),
            QStringLiteral("将整链重建为六轴模板参数（%1 轴→6 轴，既有链参数"
                           "与工具/场景引用被覆盖/清空——不可撤销草稿外的历"
                           "史）。确认重置？")
                .arg(QString::number(m_editTarget && m_editTarget()
                                         ? m_editTarget()->design.joints.size()
                                         : 0)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirmed != QMessageBox::Yes) { return; }
    }

    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr) { return; }  // 无会话＝编辑禁用（装配层门控同形态）

    // 目标下标：Add 的插入位＝选中关节之后（未选中＝链尾追加）；Remove/
    // Up/Down 需选中关节（未选中＝就地提示——不虚构操作位）；Reset 忽略。
    std::size_t targetIndex = ws->design.joints.size();  // 缺省＝尾追加位
    if (opIndex != 4) {
        const auto target =
            m_lastSelected.has_value()
                ? resolveSelection(*ws, *m_lastSelected)
                : std::optional<SelectedTarget>{};
        const bool needsSelection = (opIndex != 0);
        if (target.has_value() && target->kind == SelectedTarget::Kind::Joint) {
            targetIndex = target->index;
        } else if (needsSelection) {
            EditRejection r;
            r.codeToken = "no-selection";
            r.detail = "请先在结构树选中目标关节（结构操作作用于选中位）";
            onEditRejected(r);
            return;
        }
    }

    const auto outcome = submitStructureOp(
        *ws, *this, static_cast<StructureOp>(opIndex), targetIndex);
    if (outcome == EditSubmitOutcome::Applied) {
        // 结构变更＝树/属性全面重建（链长变了——增量投影形状前提失效）。
        refreshStructureTree(*ws);
        refreshPropertiesFromLastWorkingSet();
    }
}

// =====================================================================
// 全面板刷新（事件驱动出口——零缓存：内容全部来自入参现取）
// =====================================================================

void ModelingPanelWidget::refreshPanel(const ModelingWorkingSet& ws,
                                       const ModelReadinessReport& report)
{
    m_threadGuard.assertOnUiThread();  // §3.4——刷新触点同样是编辑面

    // ---- 区①结构树：全量重建（UI-T47 抽出 refreshStructureTree——与
    //      结构操作按钮槽共用同一树投影，零第二实现）----
    refreshStructureTree(ws);

    // ---- 区②属性区：按当前选中锚现取重投影（选中失效＝空态——不伪造行）----
    refreshPropertiesFromLastWorkingSet();

    // ---- 区④就绪条：三组计数＋逐项行（报告直投——零判定）----
    const ReadinessBarProjection bar = projectReadinessBar(report);
    m_readinessCounts->setText(QStringLiteral("阻断 %1 ｜ 警告 %2 ｜ 待确认 %3 ｜ 提示 %4")
                                   .arg(bar.counts.blockers)
                                   .arg(bar.counts.warnings)
                                   .arg(bar.counts.confirmables)
                                   .arg(bar.counts.notes));
    m_readinessItems->clear();
    for (const ReadinessItemRow& row : bar.items) {
        auto* item = new QTreeWidgetItem(m_readinessItems);
        item->setText(kLabelColumn,
                      QStringLiteral("[%1] %2")
                          .arg(QString::fromStdString(row.severity),
                               QString::fromStdString(row.summary)));
        item->setText(kAnchorColumn, row.jumpTarget.has_value()
                                         ? QString::fromStdString(row.jumpTarget->toCanonical())
                                         : QString());  // 无主体＝不可点击定位
        m_readinessItems->addTopLevelItem(item);
    }

    // ---- 区④补：L0～L11 分层结果行（UI-T41 A5——层结论保序呈现；层行
    //      无锚不可点击定位——定位走逐项行 jumpTarget）。
    for (std::size_t i = 0; i < bar.layerResults.size(); ++i) {
        const LayerResultRow& layer = bar.layerResults[i];
        auto* item = new QTreeWidgetItem(m_readinessItems);
        item->setText(kLabelColumn,
                      QStringLiteral("L%1 %2：%3")
                          .arg(i)
                          .arg(layer.passed ? QStringLiteral("通过")
                                            : QStringLiteral("未通过"),
                               QString::fromStdString(layer.note)));
        m_readinessItems->addTopLevelItem(item);
    }

    // ---- 区③命令使能态：统一出口重算（UI-T43——原内联循环迁移至
    //      refreshCommandEnablement；writable/提交出口/可用性三输入的
    //      合取判定在该单出口维护，本处零重复实现）。
    refreshCommandEnablement();
}

void ModelingPanelWidget::setWritable(bool writable)
{
    m_writable = writable;
    // L-7 控件半区：现有属性行即时降级/恢复（行序同键——只变使能）。
    // UI-T27 P0-2：编辑器文本＝纯值（单位在标签——值/单位分离后回显口径）。
    m_propertyRows = applyReadOnlyGate(m_propertyRows, m_writable);
    for (std::size_t i = 0; i < m_propertyEditors.size() && i < m_propertyRows.size(); ++i) {
        m_propertyEditors[i]->setReadOnly(rowReadOnly(m_propertyRows[i].enablement)
                                              || m_propertyRows[i].fieldKey != "zero-offset");
        m_propertyEditors[i]->setText(
            QString::fromStdString(m_propertyRows[i].valueText));
    }
    // L-7 编辑页半区（UI-T53）：只读会话＝ParamTablePanel 整体禁用（页内
    // 全部编辑控件同步灰显——出口侧 applyJointDetailEdits 另有防御面）。
    if (m_jointEditPanel != nullptr) {
        m_jointEditPanel->setEnabled(m_writable);
    }
    // 命令按钮使能态即时重算（UI-T43 修复——审核 P1：此前注释推迟到"下次
    // refreshPanel"实现；但宿主只读降级后可能长时间无刷新事件，期间写命令
    // 残留可用呈现，与需求域"切换后必须同步刷新全部状态承载面"的整改口径
    // 不一致。本域无重投影依赖的会话数据（零缓存——ACC5），使能重算零代价，
    // 即时执行消除残留窗口）。
    refreshCommandEnablement();
}

void ModelingPanelWidget::refreshCommandEnablement()
{
    // UI-T43 统一收口（四个触发点共用——判定面唯一，语义见头注）。循环内
    // 合取顺序：提交出口→只读门控（§7.6/L-7 面板半区）→可用性快照；
    // 任一不满足＝禁用，不虚构可达性。
    for (std::size_t i = 0; i < m_commandButtons.size() && i < m_commands.size(); ++i) {
        const bool readonlyBlocked = !m_writable && !m_commands[i].readOnlyAllowed;
        const auto availability = m_commandAvailability
                                      ? m_commandAvailability(m_commands[i].id)
                                      : ui::CommandAvailability{};
        const bool enabled = m_commandSubmit != nullptr && !readonlyBlocked
                             && (!m_commandAvailability || availability.enabled);
        m_commandButtons[i]->setEnabled(enabled);
        // 禁用原因随 tooltip 呈现（UX-02——键解析归 UiText；解析空＝键缺失
        // 回退键名原文，不虚构文案）。启用/无原因＝恢复悬停说明（消除"禁用
        // 一次后原因文案残留"——需求域 refreshCommandEnablement 同款先例）。
        if (m_commandAvailability && !enabled && !availability.disableReasonKey.empty()) {
            m_commandButtons[i]->setToolTip(
                QString::fromStdString(ui::resolveText(availability.disableReasonKey)));
        } else {
            const std::string tooltipText =
                ui::resolveText("cmd." + m_commands[i].id + ".tooltip");
            m_commandButtons[i]->setToolTip(QString::fromStdString(
                tooltipText.empty() ? m_commands[i].id : tooltipText));
        }
    }
}

void ModelingPanelWidget::focusObject(const std::optional<core::ObjectId>& oid)
{
    m_threadGuard.assertOnUiThread();  // §3.4——定位触点同样是会话编辑面

    // 无目标/闭包外对象＝清除高亮的对称收口（多选/清空选中时由适配器
    // 调用）——仅清会话选中锚，树呈现保持原状（不伪造定位）。
    if (!oid.has_value()) {
        if (m_selection.select(std::nullopt)) {
            m_lastSelected.reset();
            m_tree->clearSelection();
            refreshPropertiesFromLastWorkingSet();
        }
        return;
    }

    // 会话选中锚（幂等——重复定位同一对象不重复刷新，防聚焦循环：
    // SelectionAdapter 回调→focusObject→树选中信号→select 同值＝false）。
    if (!m_selection.select(oid)) { return; }
    m_lastSelected = oid;

    // 自持树滚动＋置当前行（行选中信号因会话锚已置而幂等短路——属性区
    // 投影与选中事件在本函数内显式驱动，L-1 数据流复用零新增路径）。
    const QString anchor = QString::fromStdString(oid->toCanonical());
    QTreeWidgetItemIterator it(m_tree);
    while (*it != nullptr) {
        if ((*it)->text(kAnchorColumn) == anchor) {
            m_tree->setCurrentItem(*it);   // 树高亮（选中信号幂等——不回环）
            m_tree->scrollToItem(*it);     // 树滚动（L-1"反向定位→树滚动"）
            Q_EMIT selectionChanged(anchor);
            refreshPropertiesFromLastWorkingSet();
            return;
        }
        ++it;
    }
    // 树中无该行（内容漂移——如重建间隙）：属性区仍按锚重投影（选中
    // 是会话态不依赖树行），树呈现等待下次 refreshPanel。
    Q_EMIT selectionChanged(anchor);
    refreshPropertiesFromLastWorkingSet();
}

// =====================================================================
// 属性区投影与 L-2 编辑转接
// =====================================================================

void ModelingPanelWidget::refreshPropertiesFromLastWorkingSet()
{
    // UI-T53 统一刷新入口：属性行重投影＋编辑页同步（两区同源——选中/
    // 编辑后/refreshPanel 全部经此，零第二刷新路径）。
    refreshPropertyRowsFromLastWorkingSet();
    refreshJointEditPane();
}

void ModelingPanelWidget::refreshPropertyRowsFromLastWorkingSet()
{
    // 现取编辑目标（装配层会话工作集——面板零副本）；无会话＝空态。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;

    // 先投影本次目标行（不触碰既有控件——增量/重建的分路判据）。
    std::vector<PropertyFieldRow> rows;
    std::optional<SelectedTarget> target;
    if (ws != nullptr && m_lastSelected.has_value()) {
        target = resolveSelection(*ws, *m_lastSelected);
        if (target.has_value()) {
            rows = applyReadOnlyGate(propertyFieldsFor(*ws, *target), m_writable);
        }
    }

    // UI-T41 批次B（B3）增量路径：行集合形状（字段键序）与上次一致＝原地
    // 更新值文本——不删行不重建控件，输入焦点与未完成输入不丢失（选中/
    // 就绪刷新不再打断编辑）；形状变化（换选中对象/只读切换）才走重建。
    // UI-T27 P0-1 修复的"清空删尽"语义保留在重建路径（while 删尽）。
    const bool sameShape = rows.size() == m_propertyRows.size()
                           && m_propertyEditors.size() == rows.size();
    if (sameShape) {
        bool keysEqual = true;
        for (std::size_t i = 0; i < rows.size() && keysEqual; ++i) {
            keysEqual = rows[i].fieldKey == m_propertyRows[i].fieldKey;
        }
        if (keysEqual) {
            for (std::size_t i = 0; i < rows.size(); ++i) {
                QLineEdit* editor = m_propertyEditors[i];
                const QString authoritative =
                    QString::fromStdString(rows[i].valueText);
                // 焦点中的编辑器不回写（用户正在输入——权威值回显由提交
                // 分支负责）；非焦点编辑器回显最新权威值；值一致时顺手清
                // 上次校验拒绝的警示描边（B1——重编辑恢复正常呈现）。
                if (!editor->hasFocus()) {
                    editor->setText(authoritative);
                }
                if (editor->text() == authoritative) {
                    editor->setStyleSheet({});
                }
                m_propertyRows[i] = rows[i];
            }
            // UI-T49：复制钮禁用态随 visual 有无同步（visual 挂/摘是行键
            // 不变形变更——增量路径不重建钮，禁用态必须就地刷新，否则
            // "诚实禁用"滞后一次投影）。
            if (target.has_value() && target->kind == SelectedTarget::Kind::Link) {
                refreshCollisionCopyGating(*ws, target->index);
            }
            return;
        }
    }

    // 重建路径：清空必须删尽全部行（UI-T27 P0-1 修复——while 循环删尽，
    // 行数不累积）。
    while (m_propertyForm->rowCount() > 0) {
        m_propertyForm->removeRow(0);
    }
    m_propertyEditors.clear();
    m_warningEditor = nullptr;  // 重建即清警示描边（B1 状态随行销毁）
    m_propertyRows = std::move(rows);
    if (ws == nullptr || !target.has_value()) { return; }  // 闭包外身份——空态

    for (PropertyFieldRow& row : m_propertyRows) {
        auto* editor = new QLineEdit(this);
        // UI-T27 P0-2 单位分离：编辑器文本＝纯值（toDouble 全串可解析），
        // 单位并入行标签（UX-05 数值＋单位同显的呈现位迁移——值/单位分离
        // 不改变 PanelModel 投影产出，仅呈现拼接位从值文本移到标签）。
        // UI-T27 P0-3 面板半区：仅 zero-offset 有提交轨（MDL-07 表单最小
        // 版——面板侧只允许该键可编辑，其余字段只读灰显如实呈现"不支持
        // 就地提交"，消除"看着可改实际拒绝"的交互语义错误）。
        const bool panelEditable = (row.fieldKey == "zero-offset");
        editor->setText(QString::fromStdString(row.valueText));
        editor->setReadOnly(!panelEditable || rowReadOnly(row.enablement));
        QString label = QString::fromStdString(row.fieldKey);
        if (!row.unitText.empty()) {
            label += QStringLiteral("（") + QString::fromStdString(row.unitText)
                     + QStringLiteral("）");
        }
        if (!panelEditable) {
            // 只读复合行指引（与提交拒绝分流同口径——UI-T41 B7＋UI-T53）：
            // 关节行指向"编辑"页逐字段提交轨；其余行挂 UiText 键（物性
            // 估算/占位几何域命令的真实归宿，批次A 已装配的命令与后续
            // 批次的入口如实区分）。
            if (target->kind == SelectedTarget::Kind::Joint) {
                editor->setToolTip(QStringLiteral(
                    "该字段为复合行，不支持就地编辑——请切到『编辑』页"
                    "逐字段数值提交（批量粘贴/表单级确认应用）"));
            } else {
                editor->setToolTip(QString::fromStdString(
                    ui::resolveText("panel.modeling.field.composite.tooltip")));
            }
        } else {
            // UI-T41 批次B（B1）：实时校验提示——QDoubleValidator 范围取
            // 选中关节权威限位（bounds，rad/m——域函数仍是唯一裁决者，
            // 校验器只作输入期引导；范围未提供＝不限，不伪造约束）。
            if (target->kind == SelectedTarget::Kind::Joint
                && target->index < ws->design.joints.size()) {
                const auto& joint = ws->design.joints[target->index];
                if (const auto bounds = joint.bounds.tryValue()) {
                    auto* validator = new QDoubleValidator(
                        bounds->first, bounds->second, 6, editor);
                    validator->setNotation(QDoubleValidator::StandardNotation);
                    editor->setValidator(validator);
                }
            }
            editor->setAccessibleName(label);  // B6：读屏锚＝行标签同源
        }
        connect(editor, &QLineEdit::editingFinished, this,
                &ModelingPanelWidget::onPropertyEditingFinished);
        m_propertyForm->addRow(label, editor);
        m_propertyEditors.push_back(editor);

        // UI-T48 几何引用操作行：连杆选中且行键为几何槽时，在行编辑器下
        // 补挂接/摘除两钮（资源选择器入口——C5 消账；io 真装经
        // GeometryResourceFlow，域裁决唯一在 GeometryLinkEdit 三原语）。
        if (target->kind == SelectedTarget::Kind::Link
            && (row.fieldKey == "visual-geometry"
                || row.fieldKey == "collision-geometry")) {
            const GeometrySlot slot = row.fieldKey == "visual-geometry"
                                          ? GeometrySlot::Visual
                                          : GeometrySlot::Collision;
            auto* geoBar = new QWidget(this);
            auto* geoLay = new QHBoxLayout(geoBar);
            geoLay->setContentsMargins(0, 0, 0, 0);
            const std::string keySuffix = row.fieldKey == "visual-geometry"
                                              ? "visual" : "collision";
            auto* attachBtn = new QPushButton(QStringLiteral("挂接/替换…"), geoBar);
            attachBtn->setObjectName(
                QString::fromUtf8("ird_modeling_geo_attach_") + keySuffix.c_str());
            attachBtn->setToolTip(QStringLiteral(
                "选择外部几何文件（stl/obj/dae）登记为资源并挂接到本槽"
                "（Recorded——固化随项目资源区既有轨）"));
            connect(attachBtn, &QPushButton::clicked, this,
                    [this, slot] { onGeometryAttachClicked(slot); });
            auto* detachBtn = new QPushButton(QStringLiteral("摘除"), geoBar);
            detachBtn->setObjectName(
                QString::fromUtf8("ird_modeling_geo_detach_") + keySuffix.c_str());
            detachBtn->setToolTip(QStringLiteral(
                "摘除本槽几何引用（清单条目保留——共享引用不级联删除）"));
            connect(detachBtn, &QPushButton::clicked, this,
                    [this, slot] { onGeometryDetachClicked(slot); });
            geoLay->addWidget(attachBtn);
            geoLay->addWidget(detachBtn);
            // UI-T49 视觉→碰撞复制辅助（G7——§5.2 几何生成辅助②）：仅在
            // collision 行追加第三钮（方向固定 visual→collision）。visual
            // 未设＝诚实禁用＋toolTip 原因（"非置灰无解释"纪律——§9.7.1
            // 交互；域侧 VisualNotSet 码仍是直接调用的防御面）。
            QPushButton* copyBtn = nullptr;
            if (row.fieldKey == "collision-geometry") {
                copyBtn = new QPushButton(QStringLiteral("从视觉复制"), geoBar);
                copyBtn->setObjectName(
                    QString::fromUtf8("ird_modeling_geo_copy_collision"));
                copyBtn->setToolTip(QStringLiteral(
                    "把本连杆视觉几何引用复制为碰撞引用（同资源共享——"
                    "localTransform 初值随复制，独立可改；零网格重画/凸包简化）"));
                connect(copyBtn, &QPushButton::clicked, this,
                        &ModelingPanelWidget::onGeometryCopyToCollisionClicked);
                geoLay->addWidget(copyBtn);
                m_collisionCopyBtn = copyBtn;  // 增量路径禁用态刷新的握把
            }
            geoLay->addStretch(1);
            if (!m_writable) {
                attachBtn->setEnabled(false);
                detachBtn->setEnabled(false);  // 只读会话禁用（L-R12 同款门控）
                if (copyBtn != nullptr) { copyBtn->setEnabled(false); }
            } else if (copyBtn != nullptr) {
                // 复制源缺失＝诚实禁用＋原因（非置灰无解释——§9.7.1；
                // 禁用态判定收敛到共用辅助——重建/增量两路径同源）。
                refreshCollisionCopyGating(*ws, target->index);
            }
            m_propertyForm->addRow(QString(), geoBar);
        }
    }
}

// =====================================================================
// 几何引用操作槽（UI-T48——资源选择器入口；域裁决唯一在 GeometryLinkEdit）
// =====================================================================

void ModelingPanelWidget::onGeometryAttachClicked(GeometrySlot slot)
{
    m_threadGuard.assertOnUiThread();  // §3.4——编辑面
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }  // 无会话/只读——编辑禁用
    const auto target = m_lastSelected.has_value()
                            ? resolveSelection(*ws, *m_lastSelected)
                            : std::optional<SelectedTarget>{};
    if (!target.has_value() || target->kind != SelectedTarget::Kind::Link) { return; }

    // 编辑器警示落点预置（拒绝时行内描边——字段轨同款呈现位）。
    m_warningEditor = nullptr;
    // 资源选择流（文件对话框＋io 真装探测＋域原语；拒绝原因经 sink 呈现）。
    if (runGeometryResourceSelection(*this, *ws, target->index, slot, *this)) {
        // 挂接落草稿——树/属性全面重建（清单变更——增量投影形状失效）。
        refreshStructureTree(*ws);
        refreshPropertiesFromLastWorkingSet();
    }
}

void ModelingPanelWidget::onGeometryDetachClicked(GeometrySlot slot)
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }
    const auto target = m_lastSelected.has_value()
                            ? resolveSelection(*ws, *m_lastSelected)
                            : std::optional<SelectedTarget>{};
    if (!target.has_value() || target->kind != SelectedTarget::Kind::Link) { return; }

    // 摘除（域原语幂等——本无引用不出摘要；拒绝仅越界面）。
    const std::optional<GeometryLinkError> err =
        detachGeometry(*ws, target->index, slot);
    if (!err.has_value()) {
        refreshStructureTree(*ws);
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(geometryLinkErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onGeometryCopyToCollisionClicked()
{
    m_threadGuard.assertOnUiThread();  // §3.4——编辑面
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }  // 无会话/只读——编辑禁用
    const auto target = m_lastSelected.has_value()
                            ? resolveSelection(*ws, *m_lastSelected)
                            : std::optional<SelectedTarget>{};
    if (!target.has_value() || target->kind != SelectedTarget::Kind::Link) { return; }

    // 编辑器警示落点预置（拒绝时行内描边——字段轨同款呈现位）。
    m_warningEditor = nullptr;
    // 首调不携覆盖确认——collision 已有时域原语返回 CollisionOccupied，
    // 由本函数承载显式确认交互（estimate 覆盖确认同款纪律：域裁决＋UI 确
    // 认分离，域原语不弹窗）。
    std::optional<GeometryLinkError> err =
        copyVisualToCollision(*ws, target->index, false);
    if (err.has_value() && err->code == GeometryLinkErrorCode::CollisionOccupied) {
        const QMessageBox::StandardButton confirmed = QMessageBox::question(
            this, QStringLiteral("覆盖碰撞几何"),
            QStringLiteral("本连杆碰撞几何已有引用（%1）。用视觉引用覆盖它？"
                           "\n\n覆盖后旧引用被替换（清单条目保留——共享资源不级联删除）。")
                .arg(QString::fromStdString(
                    ws->design.links[target->index].collision->resourceRefId)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirmed != QMessageBox::Yes) {
            return;  // 用户取消＝零变更（不落摘要不刷新）
        }
        err = copyVisualToCollision(*ws, target->index, true);
    }
    if (!err.has_value()) {
        // 复制落草稿——属性区刷新（清单零改动，树形状不变；重投影重评复
        // 制钮禁用态）。
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(geometryLinkErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::refreshCollisionCopyGating(const ModelingWorkingSet& ws,
                                                     std::size_t linkIndex)
{
    if (m_collisionCopyBtn.isNull()) { return; }  // 行未构建/已被重建销毁
    if (linkIndex < ws.design.links.size()
        && !ws.design.links[linkIndex].visual.has_value()) {
        // 复制源缺失＝诚实禁用＋原因（非置灰无解释——§9.7.1 交互纪律；
        // 域侧 VisualNotSet 码仍是直接调用的防御面）。
        m_collisionCopyBtn->setEnabled(false);
        m_collisionCopyBtn->setToolTip(QStringLiteral(
            "视觉几何未挂接——先挂接 visual 槽后可复制为碰撞引用"));
    } else {
        m_collisionCopyBtn->setEnabled(true);
        m_collisionCopyBtn->setToolTip(QStringLiteral(
            "把本连杆视觉几何引用复制为碰撞引用（同资源共享——"
            "localTransform 初值随复制，独立可改；零网格重画/凸包简化）"));
    }
}

// =====================================================================
// 关节详细编辑页（UI-T53——B.1 复杂对象编辑模式的关节侧承载）
// =====================================================================

void ModelingPanelWidget::buildJointEditPane(QVBoxLayout* bottom)
{
    bottom->setContentsMargins(0, 0, 0, 0);
    // 空态提示行（非关节选中/无会话＝如实呈现，不伪造编辑页——ERR-01）。
    m_jointEditHint = new QLabel(this);
    m_jointEditHint->setObjectName(QStringLiteral("ird_modeling_edit_hint"));
    m_jointEditHint->setWordWrap(true);
    m_jointEditHint->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    bottom->addWidget(m_jointEditHint);
    // 语义说明行（常驻——审核语义固定清单的呈现半区：参考系/角度制式/
    // 权威守卫随页可读，工程师不看设计文档也能读懂字段语义）。
    auto* caption = new QLabel(this);
    caption->setObjectName(QStringLiteral("ird_modeling_edit_caption"));
    caption->setWordWrap(true);
    caption->setText(QStringLiteral(
        "原点：父连杆系（位置 m／姿态 rad；ZYX 约定 R＝Rz(yaw)·Ry(pitch)·"
        "Rx(roll)）；轴向：连杆系单位向量。Explicit 权威下轴向/原点可编辑，"
        "StandardDH 权威下为派生只读（提交被域守卫拒绝）。"));
    caption->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    bottom->addWidget(caption);
    // ParamTablePanel 宿主（面板产物换选中目标时重建——父子挂本容器）。
    m_jointEditHost = new QWidget(this);
    m_jointEditHost->setObjectName(QStringLiteral("ird_modeling_edit_host"));
    auto* hostLay = new QVBoxLayout(m_jointEditHost);
    hostLay->setContentsMargins(0, 0, 0, 0);
    bottom->addWidget(m_jointEditHost, 1);
    m_jointEditHint->setText(QStringLiteral(
        "在结构树选中一个关节后，此处呈现其 12 行数值编辑表"
        "（轴向／原点 XYZ＋RPY／零位偏置／限位）——批量粘贴与表单级"
        "确认应用经 ui 表单公共件（UX-05/07）。"));
    m_jointEditHint->show();
}

void ModelingPanelWidget::refreshJointEditPane()
{
    m_threadGuard.assertOnUiThread();  // §3.4——刷新触点同样是编辑面

    // 现取编辑目标＋选中锚→关节下标（与属性行同一 resolveSelection——
    // L-1 关联键复用，零第二选中语义）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    std::optional<std::size_t> jointIndex;
    if (ws != nullptr && m_lastSelected.has_value()) {
        const auto target = resolveSelection(*ws, *m_lastSelected);
        if (target.has_value() && target->kind == SelectedTarget::Kind::Joint) {
            jointIndex = target->index;
        }
    }

    // 空态：无会话／非关节选中（含失效锚）——面板收起＋提示行呈现。
    if (ws == nullptr || !jointIndex.has_value()
        || *jointIndex >= ws->design.joints.size()) {
        m_jointEditTarget.reset();
        if (m_jointEditPanel != nullptr) { m_jointEditPanel->hide(); }
        if (ws == nullptr) {
            m_jointEditHint->setText(QStringLiteral(
                "编辑页需已打开项目（草稿会话）——选中结构树中的关节后"
                "呈现数值编辑表。"));
        } else {
            m_jointEditHint->setText(QStringLiteral(
                "当前选中对象无数值编辑页（本批次承载关节——工具/场景/"
                "基座安装编辑随后续批次）。"));
        }
        m_jointEditHint->show();
        return;
    }

    const JointEntry& joint = ws->design.joints[*jointIndex];
    // 同目标＝推基线（暂存编辑保留——UI-T41 B3 增量语义的同源纪律：刷新
    // 不打断未完成输入）；换目标/首建＝重建（字段量纲随关节类型，模型
    // 构造后字段集不可变——ParamEditModel 契约）。
    const bool sameTarget = m_jointEditTarget.has_value()
                            && *m_jointEditTarget == *jointIndex
                            && m_jointEditPanel != nullptr;
    if (!sameTarget) {
        rebuildJointEditModel(joint);
    }
    m_jointEditHint->hide();
    if (m_jointEditPanel != nullptr) {
        m_jointEditPanel->show();
        m_jointEditPanel->setEnabled(m_writable);  // L-7 页级门控（只读会话整体灰显）
    }
    m_jointEditTarget = jointIndex;
    pushJointEditBaselines(joint);
}

void ModelingPanelWidget::rebuildJointEditModel(const JointEntry& joint)
{
    // 12 行字段装配（行序＝登记序——表单呈现与确认区明细的稳定序）：
    // 轴向 3（无量纲）→原点 6（m＋rad）→零位偏置 1→限位 2（量纲随类型）。
    const core::QuantityKind qk = jointEditQuantityKind(joint.type);
    const core::UnitToken jointUnit = jointEditUnitToken(qk);
    const core::UnitToken dimensionless = core::UnitToken::find("1").value();

    std::vector<ui::QuantityFieldSpec> specs;
    specs.reserve(12);
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointAxisXKey, "轴向 X", core::QuantityKind::Dimensionless,
        dimensionless, dimensionless));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointAxisYKey, "轴向 Y", core::QuantityKind::Dimensionless,
        dimensionless, dimensionless));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointAxisZKey, "轴向 Z", core::QuantityKind::Dimensionless,
        dimensionless, dimensionless));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginXKey, "原点 X", core::QuantityKind::Length,
        core::UnitToken::find("m").value(), core::UnitToken::find("m").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginYKey, "原点 Y", core::QuantityKind::Length,
        core::UnitToken::find("m").value(), core::UnitToken::find("m").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginZKey, "原点 Z", core::QuantityKind::Length,
        core::UnitToken::find("m").value(), core::UnitToken::find("m").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginRollKey, "原点 R（roll）", core::QuantityKind::Angle,
        core::UnitToken::find("rad").value(), core::UnitToken::find("rad").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginPitchKey, "原点 P（pitch）", core::QuantityKind::Angle,
        core::UnitToken::find("rad").value(), core::UnitToken::find("rad").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointOriginYawKey, "原点 Y（yaw）", core::QuantityKind::Angle,
        core::UnitToken::find("rad").value(), core::UnitToken::find("rad").value()));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointZeroOffsetKey, "零位偏置", qk, jointUnit, jointUnit));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointBoundsMinKey, "限位下限", qk, jointUnit, jointUnit));
    specs.push_back(ui::makeQuantityFieldSpec(
        kJointBoundsMaxKey, "限位上限", qk, jointUnit, jointUnit));

    // 换目标重建：旧面板随 Qt 父子析构（deleteLater——重建处于刷新路径内，
    // 延迟删除避免重入）；模型随之重建（字段集不可变契约）。
    if (m_jointEditPanel != nullptr) {
        m_jointEditPanel->deleteLater();
        m_jointEditPanel = nullptr;
    }
    m_jointEditModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
    // 出口为空＝只读呈现（"应用…"禁用＋kNoOutletTooltip——不虚构可达性；
    // 本面板构造期即建出口，此为防御面）。
    m_jointEditPanel = ui::createParamTablePanel(
        *m_jointEditModel, m_jointEditOutlet.get(), {}, m_jointEditHost);
    static_cast<QVBoxLayout*>(m_jointEditHost->layout())->addWidget(m_jointEditPanel);
}

void ModelingPanelWidget::pushJointEditBaselines(const JointEntry& joint)
{
    if (m_jointEditModel == nullptr) { return; }  // 页未建（空态）——零回填
    const auto set = [this](const char* key, std::optional<double> v) {
        m_jointEditModel->setBaseline(key, std::move(v));
    };
    // 轴向：未提供＝nullopt 基线（kFieldUnsetText 占位——不伪造 0；ERR-01）。
    if (const auto axis = joint.axis.tryValue()) {
        set(kJointAxisXKey, (*axis)[0]);
        set(kJointAxisYKey, (*axis)[1]);
        set(kJointAxisZKey, (*axis)[2]);
    } else {
        set(kJointAxisXKey, std::nullopt);
        set(kJointAxisYKey, std::nullopt);
        set(kJointAxisZKey, std::nullopt);
    }
    // 原点：平移直读；姿态半区经 RPY 反解（呈现层唯一实现——
    // RpyPresentation.hpp，与域内核正解互为正逆）。
    if (const auto origin = joint.origin.tryValue()) {
        const auto& d = origin->d();
        const auto rpy = rpyview::rotationToRpy(origin->r());
        set(kJointOriginXKey, d[0]);
        set(kJointOriginYKey, d[1]);
        set(kJointOriginZKey, d[2]);
        set(kJointOriginRollKey, rpy[0]);
        set(kJointOriginPitchKey, rpy[1]);
        set(kJointOriginYawKey, rpy[2]);
    } else {
        set(kJointOriginXKey, std::nullopt);
        set(kJointOriginYKey, std::nullopt);
        set(kJointOriginZKey, std::nullopt);
        set(kJointOriginRollKey, std::nullopt);
        set(kJointOriginPitchKey, std::nullopt);
        set(kJointOriginYawKey, std::nullopt);
    }
    // 零位偏置非 SourcedValue（恒有值）。
    set(kJointZeroOffsetKey, joint.zeroOffset);
    // 限位：Continuous＝NotApplicable（I-MDL-4）→ nullopt 基线。
    if (const auto bounds = joint.bounds.tryValue()) {
        set(kJointBoundsMinKey, bounds->first);
        set(kJointBoundsMaxKey, bounds->second);
    } else {
        set(kJointBoundsMinKey, std::nullopt);
        set(kJointBoundsMaxKey, std::nullopt);
    }
}

void ModelingPanelWidget::applyJointDetailEdits(const ui::ParamEditSet& editSet)
{
    m_threadGuard.assertOnUiThread();  // §3.4——编辑面
    // 出口只在页绑定有效且会话可写时可达（ParamTablePanel 在只读会话已
    // 整体禁用——此处为防御面，L-7 双半区）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_jointEditTarget.has_value() || !m_writable) { return; }
    const std::size_t jointIndex = *m_jointEditTarget;
    if (jointIndex >= ws->design.joints.size()) { return; }  // 结构变更竞态防御

    // 键→新值表（一次装配；同组多键一次域提交——UX-05 批量语义的域半区）。
    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : editSet.changes) {
        staged[change.key] = change.newSi;
    }
    // 组内分量取值：脏键取移交新值，未脏键取当前权威基线（confirmApply 已
    // 推进脏键基线——currentValueSi 对两态统一可读）。
    const auto component = [this, &staged](const char* key) -> std::optional<double> {
        if (const auto it = staged.find(key); it != staged.end()) { return it->second; }
        return m_jointEditModel ? m_jointEditModel->currentValueSi(key) : std::nullopt;
    };
    // 提交轨复用 L-2 分流（submitJointFieldEdit→域裁决→sink 接受/拒绝——
    // 与属性行/共享检查器同一落点，零第二分流实现）；拒绝不阻断其余组。
    const auto submit = [this, ws, jointIndex](JointEditField field,
                                               JointEditValue&& value) {
        submitJointFieldEdit(*ws, *this, jointIndex, field, value);
    };

    // 拒绝就地呈现的辅助（组级装配缺分量＝调用面提示，域函数未触）。
    const auto rejectAssembly = [this](const std::string& detail) {
        EditRejection r;
        r.codeToken = "value-not-finite";
        r.detail = detail;
        onEditRejected(r);
    };

    // ---- 轴向组（axis-* 任一脏→三键组装 Vector3D——未脏分量取权威）----
    if (staged.count(kJointAxisXKey) || staged.count(kJointAxisYKey)
        || staged.count(kJointAxisZKey)) {
        const auto x = component(kJointAxisXKey);
        const auto y = component(kJointAxisYKey);
        const auto z = component(kJointAxisZKey);
        if (x.has_value() && y.has_value() && z.has_value()) {
            submit(JointEditField::Axis,
                   JointEditValue{rw::math::Vector3D<double>(*x, *y, *z)});
        } else {
            rejectAssembly("轴向分量缺失（该关节 axis 未提供）——无法组装"
                           "向量编辑，请先补全三行分量");
        }
    }

    // ---- 原点组（origin-* 任一脏→六键组装 JointOriginEditValue——旋转
    //      矩阵组合归域内核 ZYX 正解）----
    if (staged.count(kJointOriginXKey) || staged.count(kJointOriginYKey)
        || staged.count(kJointOriginZKey) || staged.count(kJointOriginRollKey)
        || staged.count(kJointOriginPitchKey)
        || staged.count(kJointOriginYawKey)) {
        const auto x = component(kJointOriginXKey);
        const auto y = component(kJointOriginYKey);
        const auto z = component(kJointOriginZKey);
        const auto r = component(kJointOriginRollKey);
        const auto p = component(kJointOriginPitchKey);
        const auto w = component(kJointOriginYawKey);
        if (x.has_value() && y.has_value() && z.has_value() && r.has_value()
            && p.has_value() && w.has_value()) {
            submit(JointEditField::Origin,
                   JointEditValue{JointOriginEditValue{*x, *y, *z, *r, *p, *w}});
        } else {
            rejectAssembly("原点分量缺失（该关节 origin 未提供）——无法组装"
                           "位姿编辑，请先补全六行分量");
        }
    }

    // ---- 零位偏置组（单值直投——rad/m 随类型，域内裁决）----
    if (const auto it = staged.find(kJointZeroOffsetKey); it != staged.end()) {
        submit(JointEditField::ZeroOffset, JointEditValue{it->second});
    }

    // ---- 限位组（bounds-* 任一脏→单侧替换整对提交——未脏侧取权威；与
    //      共享检查器出口同口径）----
    if (staged.count(kJointBoundsMinKey) || staged.count(kJointBoundsMaxKey)) {
        const JointEntry& current = ws->design.joints[jointIndex];
        const auto cur = current.bounds.tryValue();
        if (!cur.has_value()) {
            rejectAssembly("该关节限位未提供（Continuous 不适用或未填）——"
                           "无法单侧修改");
        } else {
            JointLimits updated = *cur;
            if (const auto it = staged.find(kJointBoundsMinKey);
                it != staged.end()) { updated.first = it->second; }
            if (const auto it = staged.find(kJointBoundsMaxKey);
                it != staged.end()) { updated.second = it->second; }
            submit(JointEditField::Bounds, JointEditValue{updated});
        }
    }

    // 权威同步（接受组基线推进对齐、被拒组基线回退真实权威——
    // ParamEditModel.confirmApply 的移交推进是乐观的，域拒绝在此修正）。
    // 边界登记（诚实缺席）：编辑页分组编辑不入 L-4 重演队列（PendingEdit
    // 是单字段轨——分组表单编辑的重演语义归后续批次；修订事件到达时的
    // 处置＝本刷新把基线对齐新权威，未应用暂存由用户重录）。
    refreshPropertiesFromLastWorkingSet();
}

void ModelingPanelWidget::onTreeSelectionChanged()
{
    // L-1 正向半区：树点击→会话选中态（零修订）→属性区重投影。
    const auto anchor = nodeAnchor(m_tree->currentItem());
    if (m_selection.select(anchor)) {
        m_lastSelected = anchor;
        Q_EMIT selectionChanged(anchor.has_value()
                                  ? QString::fromStdString(anchor->toCanonical())
                                  : QString());
        refreshPropertiesFromLastWorkingSet();
    }
}

void ModelingPanelWidget::onPropertyEditingFinished()
{
    m_threadGuard.assertOnUiThread();  // §3.4（editingFinished 只在 UI 线程——防御面）
    // 编辑目标现取（零缓存）；行→编辑意图的转接在本函数内完成（不缓存
    // 编辑意图——重演队列由刷新协调器经 recordPending 维护）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr) { return; }  // 无会话编辑面——不虚构提交
    QLineEdit* senderEditor = qobject_cast<QLineEdit*>(sender());
    if (senderEditor == nullptr) { return; }
    m_warningEditor = senderEditor;  // B1：记住本次提交编辑器——拒绝时行内警示描边落点
    // 行定位：编辑器指针与投影行序一致（m_propertyEditors 与 m_propertyRows
    // 同序 push——构造/刷新纪律）。
    std::size_t row = 0;
    while (row < m_propertyEditors.size() && m_propertyEditors[row] != senderEditor) { ++row; }
    if (row >= m_propertyRows.size()) { return; }

    PropertyFieldRow& rowRef = m_propertyRows[row];
    const std::string& key = rowRef.fieldKey;
    const QString text = senderEditor->text();
    // UI-T27 P0-2：权威显示值＝纯值（单位在行标签——编辑器文本不再拼接
    // 单位后缀，toDouble 全串可解析）。
    const QString authoritative = QString::fromStdString(rowRef.valueText);
    // UI-T27 P1-⑤ 未修改短路：Qt editingFinished 语义＝失焦即触发（无论
    // 是否修改）——文本与权威值一致＝无编辑意图，零提交零脏化（消除
    // "点击输入框再点出去→会话被误标有未应用修改"的幻影脏化）。
    if (text == authoritative) { return; }
    // 先回显权威值——拒绝时保持原值（L-2"保留原值"的强顺序保证；接受时
    // 由 refreshPropertiesFromLastWorkingSet 重投影覆盖）。
    senderEditor->setText(authoritative);

    // 编辑面划界（MDL-07 表单最小版）：数值行（zero-offset）走单值编辑轨；
    // 复合行（bounds 双值/axis 向量/type 枚举/物性组）保留只读投影——其
    // 编辑走批量粘贴与域命令（面板不自行发明解析器——插件零计算逻辑）。
    if (key != "zero-offset") {
        // 拒绝指引与真实归宿同口径（UI-T41 批次B B7＋UI-T53 增量）：关节
        // 复合行（轴向/原点/限位/type）指向"编辑"页逐字段提交轨；连杆物性
        // /几何仍指域命令——不虚指不存在的入口。
        EditRejection r;
        r.codeToken = "value-not-finite";
        const auto target = m_lastSelected.has_value()
                                ? resolveSelection(*ws, *m_lastSelected)
                                : std::optional<SelectedTarget>{};
        if (target.has_value() && target->kind == SelectedTarget::Kind::Joint) {
            r.detail = "该字段为复合行（" + key
                       + "）——请切到『编辑』页逐字段数值提交（批量粘贴/表单级"
                         "确认应用）";
        } else {
            r.detail = "该字段为复合行（" + key
                       + "）——物性可经『物性估算』、几何可经『生成占位几何』域命令维护";
        }
        onEditRejected(r);
        return;
    }
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok) {
        // 非数值输入：就地拒绝（UX-03——域函数未触，工作集未动）。
        EditRejection r;
        r.codeToken = "value-not-finite";
        r.detail = "输入不是数值：" + text.toStdString();
        onEditRejected(r);
        return;
    }

    // 选中锚→关节下标（L-1 关联键复用——编辑只对选中关节生效）。
    const auto target = resolveSelection(*ws, *m_lastSelected);
    if (!target.has_value() || target->kind != SelectedTarget::Kind::Joint) { return; }

    // L-2 提交（域裁决唯一——合法性全部在 applyJointFieldEdit 内）。
    const auto outcome = submitJointFieldEdit(*ws, *this, target->index,
                                              JointEditField::ZeroOffset, parsed);
    if (outcome == EditSubmitOutcome::Applied) {
        // 接受：入重演队列（L-4 保序重演的编辑意图）＋属性区即时重投影。
        PendingEdit e;
        e.jointIndex = target->index;
        e.field = JointEditField::ZeroOffset;
        e.value = parsed;
        m_refresh.recordPending(e);
        refreshPropertiesFromLastWorkingSet();
    }
}

void ModelingPanelWidget::onCommandButtonClicked()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (btn == nullptr || !m_commandSubmit) { return; }
    // 命令激活→提交出口转发（id 点分小写——ui CommandRegistry 词表；
    // 装配层绑定 registry.submit——域命令语义归处理器族，面板零逻辑）。
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        if (m_commandButtons[i] == btn) {
            m_commandSubmit(m_commands[i].id);
            return;
        }
    }
}

// =====================================================================
// IPanelEditSink（L-2/L-8 分流回调）
// =====================================================================

void ModelingPanelWidget::onEditApplied(const std::string& subjectPath)
{
    // 接受分支：树/属性区/就绪条的增量刷新由装配层刷新出口统一驱动
    // （事件驱动纪律——本回调只标记脏＋呈现；全面板刷新随⑤/手工触发）。
    notifySessionDirty();
    setOutcomeMessage(QString::fromStdString("已应用：" + subjectPath),
                      OutcomeSeverity::Success);  // B2：接受＝成功绿＋入历史
    if (m_warningEditor != nullptr) {
        m_warningEditor->setStyleSheet({});  // B1：接受后清上次警示描边
        m_warningEditor = nullptr;
    }
    if (m_postEditAction) {
        m_postEditAction();  // T03b-2b——就绪重算钩子（编辑→真判定刷新）
    }
}

void ModelingPanelWidget::onEditRejected(const EditRejection& rejection)
{
    // 拒绝分支：就地呈现原因（非模态——UX-03/07）；值控件已回显权威值
    // （onPropertyEditingFinished 先回显后提交的强顺序保证）。批次B（B2）：
    // 状态行警示橙着色＋入历史；B1：被拒编辑器行内警示描边（词表色），
    // 重编辑/刷新即恢复。
    setOutcomeMessage(QString::fromStdString("未应用（" + rejection.codeToken
                                             + "）：" + rejection.detail),
                      OutcomeSeverity::Warning);
    if (m_warningEditor != nullptr) {
        m_warningEditor->setStyleSheet(
            QStringLiteral("border: 1px solid %1;")
                .arg(QString::fromLatin1(ui::palette::kWarning)));
    }
}

void ModelingPanelWidget::notifySessionDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        Q_EMIT sessionDirtyChanged(true);  // PM-04/PM-11——标题 `*`（装配层接 DraftController）
    }
}

// =====================================================================
// 辅助
// =====================================================================

std::optional<core::ObjectId> ModelingPanelWidget::nodeAnchor(QTreeWidgetItem* item) const
{
    if (item == nullptr) { return std::nullopt; }
    const std::string anchor = item->text(kAnchorColumn).toStdString();
    if (anchor.empty()) { return std::nullopt; }  // 分组行无锚
    return core::ObjectId::tryFromCanonical(anchor);
}

}  // namespace sdurws::ird::modeling
