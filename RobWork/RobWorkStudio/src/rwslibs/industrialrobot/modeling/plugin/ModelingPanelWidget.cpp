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

#include <QApplication>                           // clipboard（UI-T59 预览复制钮）
#include <QClipboard>                             // QClipboard 完整类型（复制钮——UI-T59）
#include <QComboBox>
#include <QDoubleValidator>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <algorithm>
#include <QMessageBox>                            // 六轴重置确认对话（UI-T47——有损操作知情面）
#include <QLabel>
#include <QLineEdit>                              // TCP 显示名行编辑（UI-T58）
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTime>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <array>
#include <map>
#include <string>

#include <sdurws/ird/modeling/Package.hpp>  // previewExportKindToken（UI-T59——预览类型词表单一来源）
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

// ---- UI-T55 工具/场景位姿编辑页字段键（各 6 行——出口分组装配与基线
//      回填的寻址锚；小写连字符词法同上）---------------------------
constexpr const char* kToolMountXKey = "mount-x";
constexpr const char* kToolMountYKey = "mount-y";
constexpr const char* kToolMountZKey = "mount-z";
constexpr const char* kToolMountRollKey = "mount-r";
constexpr const char* kToolMountPitchKey = "mount-p";
constexpr const char* kToolMountYawKey = "mount-yaw";
constexpr const char* kSceneWorldXKey = "world-x";
constexpr const char* kSceneWorldYKey = "world-y";
constexpr const char* kSceneWorldZKey = "world-z";
constexpr const char* kSceneWorldRollKey = "world-r";
constexpr const char* kSceneWorldPitchKey = "world-p";
constexpr const char* kSceneWorldYawKey = "world-yaw";

// ---- UI-T57 TCP offset 编辑段字段键（6 行——选中 TCP 的安装偏移，键面
//      固定不随 TCP 键变化；选中经 combo 承载）-------------------------
constexpr const char* kTcpOffsetXKey = "tcp-offset-x";
constexpr const char* kTcpOffsetYKey = "tcp-offset-y";
constexpr const char* kTcpOffsetZKey = "tcp-offset-z";
constexpr const char* kTcpOffsetRollKey = "tcp-offset-r";
constexpr const char* kTcpOffsetPitchKey = "tcp-offset-p";
constexpr const char* kTcpOffsetYawKey = "tcp-offset-yaw";

/// 工具页字段集（6 行——安装接口 XYZ m＋RPY rad；specs 常量→模型重建仅在
/// 换目标时发生，字段集本身与目标无关）。
std::vector<ui::QuantityFieldSpec> toolMountSpecs()
{
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    return {
        ui::makeQuantityFieldSpec(kToolMountXKey, "安装接口 X", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kToolMountYKey, "安装接口 Y", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kToolMountZKey, "安装接口 Z", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kToolMountRollKey, "安装接口 R（roll）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kToolMountPitchKey, "安装接口 P（pitch）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kToolMountYawKey, "安装接口 Y（yaw）", core::QuantityKind::Angle, rad, rad),
    };
}

/// 场景页字段集（6 行——世界位姿 XYZ m＋RPY rad）。
std::vector<ui::QuantityFieldSpec> sceneWorldSpecs()
{
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    return {
        ui::makeQuantityFieldSpec(kSceneWorldXKey, "世界位姿 X", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kSceneWorldYKey, "世界位姿 Y", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kSceneWorldZKey, "世界位姿 Z", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kSceneWorldRollKey, "世界位姿 R（roll）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kSceneWorldPitchKey, "世界位姿 P（pitch）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kSceneWorldYawKey, "世界位姿 Y（yaw）", core::QuantityKind::Angle, rad, rad),
    };
}

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

// ---- 属性行标签的中文呈现映射（UX-02 工程用语）------------------------
// fieldKey 是 PanelModel 投影的稳定机器键（小写连字符词法——HostMigration
// 批量粘贴/基线注入的寻址锚，键面保持英文不动）；本表只是"键→行标签"的
// 呈现层固定映射（基座安装预设中文标签同款先例——机器判别仍以键为权威）。
// 表外键回退键名原文（不虚构文案——UiText 解析空回退的既有纪律），前缀键
// （tcp:/pose:）取后半段拼接中文主题词。
//
// @param key [in] PanelModel 投影行字段键（"type"/"tcp:tcp-center" 等）
// @return 行标签中文文本（单位后缀由调用方另行拼接——值/单位分离 UX-05）
std::string chineseFieldLabel(const std::string& key)
{
    // 前缀键先行（TCP/命名位姿行——键尾为用户数据 key，不入静态表）。
    static constexpr const char* kTcpPrefix = "tcp:";
    static constexpr const char* kPosePrefix = "pose:";
    if (key.rfind(kTcpPrefix, 0) == 0) {
        return std::string("TCP ") + key.substr(std::char_traits<char>::length(kTcpPrefix));
    }
    if (key.rfind(kPosePrefix, 0) == 0) {
        return std::string("位姿 ") + key.substr(std::char_traits<char>::length(kPosePrefix));
    }

    // 无前缀键的固定映射（键序＝PanelModel 各类别投影行序——查表可读性）。
    struct LabelEntry {
        const char* key;
        const char* label;
    };
    static constexpr LabelEntry kLabels[] = {
        // 关节（§9.7.1 六字段面＋DH 投影四参数）。
        {"type", "类型"},
        {"axis", "轴向"},
        {"origin", "原点位姿"},
        {"zero-offset", "零位偏置"},
        {"bounds", "限位"},
        {"working-range", "工作范围"},
        {"dh-alpha", "DH-α"},
        {"dh-a", "DH-a"},
        {"dh-d", "DH-d"},
        {"dh-theta", "DH-θ偏置"},
        // 连杆物性＋几何引用。
        {"mass", "质量"},
        {"center-of-mass", "质心"},
        {"inertia", "惯量张量"},
        {"visual-geometry", "视觉几何"},
        {"collision-geometry", "碰撞几何"},
        // 工具/场景/位姿集/传动。
        {"mount-interface", "安装接口"},
        {"world-pose", "世界位姿"},
        {"role", "场景角色"},
        {"ratio-per-joint", "逐关节减速比"},
        {"friction-per-joint", "逐关节摩擦"},
        {"torque-limits-per-joint", "逐关节力矩限值"},
        // 模型根/基座安装。
        {"display-name", "显示名"},
        {"authority", "权威模式"},
        {"preset", "安装预设"},
        {"base-position", "基座位置"},
    };
    for (const LabelEntry& e : kLabels) {
        if (key == e.key) { return e.label; }
    }
    return key;  // 表外键回退键名原文（防御面——不虚构中文文案）
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
// 工具/场景位姿编辑页的移交出口（UI-T55——IFormEditOutlet 转译壳×2）
// =====================================================================

/// 工具安装接口页出口（转接 applyToolMountEdits——与关节页出口同构零判定）。
class ModelingPanelWidget::ToolMountEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit ToolMountEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}
    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        m_owner.applyToolMountEdits(editSet);
    }
private:
    ModelingPanelWidget& m_owner;
};

/// 场景世界位姿页出口（转接 applyScenePoseEdits——同上）。
class ModelingPanelWidget::ScenePoseEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit ScenePoseEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}
    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        m_owner.applyScenePoseEdits(editSet);
    }
private:
    ModelingPanelWidget& m_owner;
};

/// TCP offset 编辑页出口（UI-T57——转接 applyTcpOffsetEdits，同构零判定）。
class ModelingPanelWidget::TcpOffsetEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit TcpOffsetEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}
    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        m_owner.applyTcpOffsetEdits(editSet);
    }
private:
    ModelingPanelWidget& m_owner;
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

    // 编辑页出口先行（构造顺序纪律——编辑页构建传入 *outlet，晚于解引用
    // 即空 unique_ptr 解引用崩溃；UI-T55 建页入栈时实测暴露，前置修正）。
    m_jointEditOutlet = std::make_unique<JointDetailEditOutlet>(*this);
    m_toolOutlet = std::make_unique<ToolMountEditOutlet>(*this);
    m_sceneOutlet = std::make_unique<ScenePoseEditOutlet>(*this);
    m_tcpOutlet = std::make_unique<TcpOffsetEditOutlet>(*this);

    auto* tabs = new QTabWidget(this);
    auto* editPage = new QWidget(tabs);
    auto* toolsPage = new QWidget(tabs);
    auto* readinessPage = new QWidget(tabs);
    auto* previewPage = new QWidget(tabs);
    // 编辑页签＝模式入口行＋模态堆栈（0＝关节区〔UI-T53〕，1＝基座安装页
    // 〔UI-T54——锚外对象显式入口，B.1 模式的锚外适配〕）。
    auto* editLay = new QVBoxLayout(editPage);
    auto* modeRow = new QHBoxLayout();
    auto* jointModeBtn = new QPushButton(QStringLiteral("关节编辑"), editPage);
    jointModeBtn->setObjectName(QStringLiteral("ird_modeling_edit_joint_mode"));
    jointModeBtn->setToolTip(QStringLiteral("按结构树选中关节呈现 12 行数值编辑表"));
    auto* baseModeBtn = new QPushButton(QStringLiteral("基座安装…"), editPage);
    baseModeBtn->setObjectName(QStringLiteral("ird_modeling_edit_base_mode"));
    baseModeBtn->setToolTip(QStringLiteral(
        "基座安装姿态编辑（预设/位置/自定义 EAA——MDL-22，整体替换语义）"));
    modeRow->addWidget(jointModeBtn);
    modeRow->addWidget(baseModeBtn);
    modeRow->addStretch(1);
    editLay->addLayout(modeRow);
    m_editStack = new QStackedWidget(editPage);
    auto* jointArea = new QWidget(m_editStack);
    buildJointEditPane(new QVBoxLayout(jointArea));    // 区⑥编辑页（关节）
    m_editStack->addWidget(jointArea);                 // index 0
    m_basePage = new QWidget(m_editStack);
    buildBasePlacementPane();                          // 区⑥编辑页（基座）
    m_editStack->addWidget(m_basePage);                // index 1
    // 工具/场景页（UI-T55——选择驱动分派：二者均有 ObjectId 树锚，B.1 主
    // 通道；与关节页同款"换目标重建/同目标推基线"纪律）。
    m_toolArea = buildPoseEditPage(
        QStringLiteral("工具安装接口编辑（T_flange_tool——法兰坐标系；"
                       "位置 m／姿态 rad，ZYX 约定 R＝Rz·Ry·Rx）"),
        toolMountSpecs(), m_toolEditModel, *m_toolOutlet, m_toolEditPanel);
    m_toolArea->setObjectName(QStringLiteral("ird_modeling_tool_area"));
    m_editStack->addWidget(m_toolArea);                // index 2
    buildTcpPane();                                    // UI-T57——TCP 段（工具页内，mount 面板之下）
    m_sceneArea = buildPoseEditPage(
        QStringLiteral("场景世界位姿编辑（世界坐标系固连——M-11 不预乘安装"
                       "旋转；位置 m／姿态 rad，ZYX 约定 R＝Rz·Ry·Rx）"),
        sceneWorldSpecs(), m_sceneEditModel, *m_sceneOutlet, m_sceneEditPanel);
    m_sceneArea->setObjectName(QStringLiteral("ird_modeling_scene_area"));
    m_editStack->addWidget(m_sceneArea);               // index 3
    buildPoseSetPane();                                // UI-T60——位姿集页（F-497 余项）
    m_editStack->addWidget(m_poseSetArea);             // index 4
    buildDrivetrainPane();                             // UI-T60——传动页（F-497 余项）
    m_editStack->addWidget(m_drivetrainArea);          // index 5
    editLay->addWidget(m_editStack, 1);
    connect(baseModeBtn, &QPushButton::clicked, this, [this] {
        m_editStack->setCurrentWidget(m_basePage);
        refreshBasePlacementPane();  // 进页即回填权威值（零脏化）
    });
    connect(jointModeBtn, &QPushButton::clicked, this, [this] {
        m_editStack->setCurrentIndex(0);
        refreshJointEditPane();  // 回关节模式——树选中锚照常驱动
    });
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
    // （出口初始化已前置至编辑页构建之前——构造顺序纪律，UI-T55。）

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
    // F-502（宿主审核 P2）频率分层：高频（对象与导入组＋复位 Home/Zero）
    // 常驻，低频诊断组（权威切换/物性估算/占位几何/基线比较/规范包导出
    // 导入）收进『更多操作』折叠区（默认收起）——压缩工具页纵向长度，
    // 命令集合/id/使能逻辑零变化（按钮仍在 m_commandButtons，仅容器迁移）。
    m_commands = modelingDomainCommands();
    // UI-T41 批次B（B5）：工业风主题安装（面板作用域——宿主 chrome/其他域
    // 面板不受影响）；分节标题色值从硬编码 #555 迁移至词表 kTextMuted
    // （UiTheme 五色词表＝唯一色值源，防彩虹化 NFR-DEP-05）。
    ui::applyIndustrialTheme(this);
    auto* toolsLayout = static_cast<QVBoxLayout*>(toolsPage->layout());
    // 折叠区承载（低频组容器＋开关钮——先建后挂，按钮循环中按 id 入组）。
    auto* moreToggle = new QPushButton(QStringLiteral("更多操作 ▸"), toolsPage);
    moreToggle->setObjectName(QStringLiteral("ird_modeling_more_toggle"));
    moreToggle->setCheckable(true);
    moreToggle->setToolTip(QStringLiteral(
        "展开低频与诊断操作（权威切换/物性估算/占位几何/基线比较/规范包）"));
    moreToggle->setStyleSheet(
        QStringLiteral("font-weight: 400; color: %1; padding: 2px; text-align: left;")
            .arg(QString::fromLatin1(ui::palette::kTextMuted)));
    auto* moreHost = new QWidget(toolsPage);
    moreHost->setObjectName(QStringLiteral("ird_modeling_more_host"));
    auto* moreLayout = new QVBoxLayout(moreHost);
    moreLayout->setContentsMargins(0, 0, 0, 0);
    moreHost->hide();  // 默认收起（F-502——展开面非删除，命令使能逻辑不受影响）
    connect(moreToggle, &QPushButton::toggled, moreToggle,
            [moreToggle, moreHost](bool checked) {
                moreHost->setVisible(checked);
                moreToggle->setText(checked ? QStringLiteral("更多操作 ▾")
                                            : QStringLiteral("更多操作 ▸"));
            });
    auto addSectionHeader = [toolsPage, toolsLayout, moreLayout](const char* title,
                                                                 bool collapsed) {
        auto* header = new QLabel(QString::fromUtf8(title), toolsPage);
        header->setStyleSheet(
            QStringLiteral("font-weight: 600; color: %1; padding-top: 4px;")
                .arg(QString::fromLatin1(ui::palette::kTextMuted)));
        (collapsed ? moreLayout : toolsLayout)->addWidget(header);
    };
    // 组首 id → 组标题（§9.7.3 目录行序内首组"对象与导入"无组首 id——
    // 循环前先挂；其余三组随其组首命令挂入折叠区——低频组整体收起）。
    const std::pair<const char*, const char*> sectionLeaders[] = {
        {"modeling.switch-authority", "参数"},
        {"modeling.generate-placeholder-geometry", "几何与姿态"},
        {"modeling.diff-baseline", "校验与导出"},
    };
    addSectionHeader("对象与导入", false);
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        // F-502 分层归组：低频诊断六命令入折叠区，其余（对象与导入三条＋
        // 复位 Home/Zero——常用姿态操作）常驻。组首标题随命令同容器。
        const bool collapsed = m_commands[i].id != "modeling.reset-home-zero";
        for (const auto& [leaderId, title] : sectionLeaders) {
            if (m_commands[i].id == leaderId) {
                addSectionHeader(title, collapsed);
            }
        }
        auto* btn = new QPushButton(QString::fromStdString(m_commands[i].titleKey), toolsPage);
        // 对象名挂命令 id（UI-T56——F-502 折叠重排后 findChildren 子树序≠
        // 目录序，测试对账需按 id 恢复目录序；生产零消费此名，仅测试锚）。
        btn->setObjectName(QStringLiteral("ird_modeling_cmd_")
                           + QString::fromStdString(m_commands[i].id));
        // UI-T41 批次B（B4）：悬停文案＝UiText tooltip 键（工程中文，UX-02——
        // 去裸命令 id 呈现）；解析空回退命令 id 原文（对账兜底，不虚构文案）。
        const std::string tooltipText =
            ui::resolveText("cmd." + m_commands[i].id + ".tooltip");
        btn->setToolTip(QString::fromStdString(
            tooltipText.empty() ? m_commands[i].id : tooltipText));
        connect(btn, &QPushButton::clicked, this, &ModelingPanelWidget::onCommandButtonClicked);
        m_commandButtons.push_back(btn);
        // 命令按 readOnlyAllowed 分组布局（使能逻辑与容器无关——m_commands/
        // m_commandButtons 同序 push 的索引对应保持，refreshCommandEnablement
        // 零改动）。
        (collapsed ? moreLayout : toolsLayout)->addWidget(btn);
    }
    toolsLayout->addWidget(moreToggle);  // 折叠开关（低频组之后——常驻区尾部）
    toolsLayout->addWidget(moreHost);
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
// （setAppliedPreview 实现随 UI-T59 迁至区⑤预览页段——refreshPreview 单一
//   渲染出口；本节保留命令回执/重演入口注释锚）

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
                        + QStringLiteral("（作用于选中关节；草稿级编辑——经"
                                         "『应用草稿』提交后才产生修订）"));
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
    // 类型选择行（UI-T59——F-498 预览半区）：五类预览——"模型摘要"为既有
    // D-MDL-10 轨（AppliedRevisionView 摘要，零外部供给），其余四类走域侧
    // 内存导出面（exportPreviewXml——宿主 bindWorkCellPreview 接源；UI 零
    // 拼 XML 纪律的强制点＝XML 类唯一来源是供给器）。
    auto* kindRow = new QHBoxLayout();
    auto* kindLabel = new QLabel(QStringLiteral("预览类型"), this);
    kindRow->addWidget(kindLabel);
    m_previewKindCombo = new QComboBox(this);
    m_previewKindCombo->setObjectName(QStringLiteral("ird_modeling_preview_kind"));
    // itemData＝域词表 token（空串＝摘要轨）；词表单一来源＝
    // previewExportKindToken（禁字符串字面量分叉）。
    m_previewKindCombo->addItem(QStringLiteral("模型摘要"), QString());
    m_previewKindCombo->addItem(
        QStringLiteral("SerialDevice XML"),
        QString::fromStdString(std::string(
            modeling::previewExportKindToken(modeling::PreviewExportKind::SerialDeviceXml))));
    m_previewKindCombo->addItem(
        QStringLiteral("Scene XML"),
        QString::fromStdString(std::string(
            modeling::previewExportKindToken(modeling::PreviewExportKind::SceneXml))));
    m_previewKindCombo->addItem(
        QStringLiteral("Collision XML"),
        QString::fromStdString(std::string(
            modeling::previewExportKindToken(modeling::PreviewExportKind::CollisionXml))));
    m_previewKindCombo->addItem(
        QStringLiteral("DWC XML"),
        QString::fromStdString(std::string(
            modeling::previewExportKindToken(modeling::PreviewExportKind::DwcXml))));
    // 第六项"规范包清单"（UI-T61——F-498 余项）：非 XML 类——数据源＝已
    // 应用修订闭包的草稿值视图（模块会话基线定格；不经宿主编译快照回调），
    // 文本由域 packageChecklistText 确定性渲染（UI 零拼装同款纪律）。
    m_previewKindCombo->addItem(QStringLiteral("规范包清单"),
                                QStringLiteral("package-checklist"));
    kindRow->addWidget(m_previewKindCombo, 1);
    m_previewCopyBtn = new QPushButton(QStringLiteral("复制全部"), this);
    m_previewCopyBtn->setObjectName(QStringLiteral("ird_modeling_preview_copy"));
    m_previewCopyBtn->setToolTip(QStringLiteral(
        "把当前预览全文复制到剪贴板（只读文本——可复制导出）"));
    kindRow->addWidget(m_previewCopyBtn);
    bottom->addLayout(kindRow);

    // 来源头行（F-498"来源修订号/模型身份/生成时间/来源对象明确"——XML 类
    // 由宿主回调组装〔非 XML 呈现元数据〕；摘要类留空——摘要文本自述）。
    m_previewSourceHeader = new QLabel(this);
    m_previewSourceHeader->setObjectName(QStringLiteral("ird_modeling_preview_source"));
    m_previewSourceHeader->setWordWrap(true);
    m_previewSourceHeader->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    bottom->addWidget(m_previewSourceHeader);

    m_preview = new QPlainTextEdit(this);
    m_preview->setObjectName(QStringLiteral("ird_modeling_preview_text"));
    m_preview->setReadOnly(true);  // 只读预览（内容仅来自已应用修订——D-MDL-10）
    m_preview->setAccessibleName(QStringLiteral("已应用修订预览"));  // B6 可访问性
    bottom->addWidget(m_preview, 1);

    connect(m_previewKindCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        refreshPreview();  // 类型切换→按新类型重渲（零状态——渲染即现取）
    });
    connect(m_previewCopyBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onPreviewCopyClicked);
}

void ModelingPanelWidget::setPreviewContentProvider(PreviewContentProvider provider)
{
    m_threadGuard.assertOnUiThread();
    m_previewProvider = std::move(provider);
    refreshPreview();  // 供给到位即重渲（晚绑定装配序——当前类型可能是 XML 类）
}

void ModelingPanelWidget::onPreviewCopyClicked()
{
    m_threadGuard.assertOnUiThread();
    if (m_preview == nullptr) { return; }
    QApplication::clipboard()->setText(m_preview->toPlainText());
    setOutcomeMessage(QStringLiteral("预览全文已复制到剪贴板"),
                      OutcomeSeverity::Info);
}

void ModelingPanelWidget::refreshPreview()
{
    if (m_preview == nullptr) { return; }
    const QString kindToken = m_previewKindCombo != nullptr
                                  ? m_previewKindCombo->currentData().toString()
                                  : QString();
    // ---- 摘要轨（D-MDL-10 既有行为——内容只来自 AppliedRevisionView）----
    if (kindToken.isEmpty()) {
        if (m_previewSourceHeader != nullptr) { m_previewSourceHeader->clear(); }
        if (!m_appliedPreview.has_value()) {
            // 空态占位（不伪造内容——D-MDL-10：预览页仅呈现已应用修订）。
            m_preview->setPlainText(
                QStringLiteral("尚无已应用修订——预览页仅呈现已应用修订内容（D-MDL-10）。\n"
                               "编辑后请经菜单 File→工业机器人项目→『应用草稿』提交，"
                               "预览随应用刷新。"));
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
        return;
    }

    // ---- XML 轨（域侧内存导出——UI 零拼装；缺席诚实呈现）----
    if (!m_previewProvider.has_value()) {
        if (m_previewSourceHeader != nullptr) { m_previewSourceHeader->clear(); }
        m_preview->setPlainText(
            QStringLiteral("预览供给未接线（宿主未绑定编译快照源）——XML 类预览不可用。"));
        return;
    }
    const auto answer = (*m_previewProvider)(kindToken.toStdString());
    if (!answer.has_value()) {
        if (m_previewSourceHeader != nullptr) { m_previewSourceHeader->clear(); }
        // 缺席文案按 kind 分流（F-521——acc/ui-t61/1 D-1：清单轨是对象级
        // 闭包清单语义，"编译产物/物性齐备"措辞不适用；XML 轨文案保持）。
        if (kindToken == QStringLiteral("package-checklist")) {
            m_preview->setPlainText(
                QStringLiteral("预览不可用：尚无已应用修订的闭包内容——规范包"
                               "清单呈现已应用修订的对象级逐项清单（D-MDL-10），"
                               "请先经顶栏『应用草稿』提交。"));
        } else {
            m_preview->setPlainText(
                QStringLiteral("预览不可用：尚无已应用修订的编译产物——请先经顶栏"
                               "『应用草稿』提交（DWC XML 另需物性齐备编译）。"));
        }
        return;
    }
    if (m_previewSourceHeader != nullptr) {
        m_previewSourceHeader->setText(QString::fromStdString(answer->headerLine));
    }
    m_preview->setPlainText(QString::fromStdString(answer->text));
}

void ModelingPanelWidget::setAppliedPreview(
    const std::optional<AppliedRevisionView>& view)
{
    m_appliedPreview = view;
    // UI-T59：注入即按当前选中类型重渲（摘要类即刻呈现新定格；XML 类经
    // 供给器现取最新编译产物——应用修订后 XML 预览不再滞留旧快照）。
    refreshPreview();
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
    // F-502（宿主审核 P2）分段知情文案：逐项列出将被替换/清空的对象面＋
    // 明确项目历史语义（草稿级操作不立即产生修订；应用后修订不可变——
    // PA-2；草稿期无撤销栈，如实告知），默认按钮保持"否"。
    if (opIndex == 4) {
        const QMessageBox::StandardButton confirmed = QMessageBox::question(
            this, QStringLiteral("六轴重置"),
            QStringLiteral("即将把整链重建为六轴模板参数（%1 轴 → 6 轴）。\n\n"
                           "将被替换/清空：\n"
                           "・全部关节参数（类型/轴向/原点/限位/零位）\n"
                           "・已挂接的工具与场景对象引用\n"
                           "・物性与几何引用随连杆重建一并清空\n\n"
                           "项目历史：本操作只改当前草稿，不会立即产生修订；"
                           "点『应用草稿』后才生成新修订——修订不可变（历史只"
                           "增不改，项目级撤销＝以新修订对冲），草稿期亦无撤"
                           "销栈，请确认后执行。\n\n"
                           "确认重置？")
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
    // L-7 基座页半区（UI-T54）：应用钮同步禁用（EAA 行使能在
    // refreshBasePlacementPane 按 writable 复合判定，进页/应用后刷新）。
    if (m_baseApplyBtn != nullptr) {
        m_baseApplyBtn->setEnabled(m_writable);
    }
    // L-7 工具/场景页半区（UI-T55）：面板整体禁用（刷新入口按 writable
    // 复合，此处即时同步不等待下次选中事件）。
    if (m_toolEditPanel != nullptr) { m_toolEditPanel->setEnabled(m_writable); }
    if (m_sceneEditPanel != nullptr) { m_sceneEditPanel->setEnabled(m_writable); }
    // L-7 TCP 段半区（UI-T57）：增删/默认钮与 offset 面板即时同步。
    if (m_tcpAddBtn != nullptr) {
        m_tcpAddBtn->setEnabled(m_writable);
        m_tcpRemoveBtn->setEnabled(m_writable);
        m_tcpDefaultBtn->setEnabled(m_writable);
    }
    if (m_tcpOffsetPanel != nullptr) { m_tcpOffsetPanel->setEnabled(m_writable); }
    // L-7 显示名行半区（UI-T58）：行编辑与应用钮即时同步（刷新入口按
    // writable 复合判定，此处不等待下次选中事件）。
    if (m_tcpDisplayNameEdit != nullptr) { m_tcpDisplayNameEdit->setEnabled(m_writable); }
    if (m_tcpDisplayNameApplyBtn != nullptr) { m_tcpDisplayNameApplyBtn->setEnabled(m_writable); }
    // L-7 位姿集/传动页半区（UI-T60）：两页控件即时同步（刷新入口按
    // writable 复合判定，此处不等待下次选中事件）。
    if (m_poseSetAddBtn != nullptr) {
        m_poseSetAddBtn->setEnabled(m_writable);
        m_poseSetRemoveBtn->setEnabled(m_writable);
        m_poseSetApplyBtn->setEnabled(m_writable);
        m_poseSetKeyEdit->setEnabled(m_writable);
        m_poseSetNoteEdit->setEnabled(m_writable);
    }
    if (m_poseSetConfigPanel != nullptr) { m_poseSetConfigPanel->setEnabled(m_writable); }
    if (m_drivetrainPanel != nullptr) { m_drivetrainPanel->setEnabled(m_writable); }
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
    // UI-T55 统一刷新入口：编辑页选择驱动分派（Joint/Tool/Scene 三页）——
    // 属性行重投影在其内联动，零第二刷新路径。
    refreshPropertyRowsFromLastWorkingSet();
    refreshEditPages();
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
        // 行标签＝中文呈现映射（UX-02 工程用语——fieldKey 机器键经
        // chineseFieldLabel 固定映射；表外键回退键名原文，不虚构文案）。
        QString label = QString::fromStdString(chineseFieldLabel(row.fieldKey));
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
    // 关节类型枚举行（UI-T66——M-R4 划界消账：域 JointEditField::Type 词表
    // 直投——I-MDL-4 组合约束归域裁决（TypeBoundsConflict 拒绝就地呈现＋
    // 回退）；四值词表＝jointTypeToken 同源）。回填屏蔽旗标：换目标回填
    // 触发的 currentIndexChanged 不提交（防幽灵编辑——knownPitfalls）。
    auto* typeRow = new QWidget(m_jointEditHost);
    auto* typeLay = new QHBoxLayout(typeRow);
    typeLay->setContentsMargins(0, 0, 0, 0);
    auto* typeLabel = new QLabel(QStringLiteral("关节类型"), typeRow);
    m_jointEditTypeBox = new QComboBox(typeRow);
    m_jointEditTypeBox->setObjectName(QStringLiteral("ird_modeling_edit_type"));
    for (const JointType t : {JointType::Revolute, JointType::Continuous,
                              JointType::Prismatic, JointType::Fixed}) {
        m_jointEditTypeBox->addItem(
            QString::fromStdString(std::string(jointTypeToken(t))));
    }
    typeLay->addWidget(typeLabel);
    typeLay->addWidget(m_jointEditTypeBox);
    typeLay->addStretch(1);
    m_jointEditTypeRow = typeRow;
    hostLay->addWidget(typeRow);
    connect(m_jointEditTypeBox, &QComboBox::currentIndexChanged, this,
            &ModelingPanelWidget::onJointEditTypeChanged);
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
        if (m_jointEditTypeRow != nullptr) { m_jointEditTypeRow->hide(); }
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
    // 关节类型行（UI-T66——回填当前权威类型＋L-7 门控；blockSignals 屏蔽
    // 回填触发的 currentIndexChanged——防换目标幽灵提交，knownPitfalls）。
    if (m_jointEditTypeRow != nullptr) {
        m_jointEditTypeRow->show();
    }
    if (m_jointEditTypeBox != nullptr) {
        QSignalBlocker blocker(*m_jointEditTypeBox);
        const int idx = m_jointEditTypeBox->findText(
            QString::fromStdString(std::string(jointTypeToken(joint.type))));
        m_jointEditTypeBox->setCurrentIndex(idx < 0 ? -1 : idx);
        m_jointEditTypeBox->setEnabled(m_writable);
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
    // UI-T66 分组重演：本轮提交分配一个重演组 id，域接受的编辑逐条入队
    // 同组——修订事件重演时组内保序整组落位/整组手工处置（L-4 组粒度）。
    const std::uint64_t replayGroup = m_refresh.allocateGroupId();
    const auto submit = [this, ws, jointIndex, replayGroup](
                            JointEditField field, JointEditValue&& value) {
        const auto outcome =
            submitJointFieldEdit(*ws, *this, jointIndex, field, value);
        if (outcome == EditSubmitOutcome::Applied) {
            PendingEdit e;
            e.jointIndex = jointIndex;
            e.field = field;
            e.value = std::move(value);
            e.groupId = replayGroup;
            m_refresh.recordPending(e);  // 入重演队列（真实编辑意图——被拒不入）
        }
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
    // UI-T66 落位注（UI-T53 划界"编辑页分组编辑不入 L-4 重演队列"消账）：
    // 本轮域接受的编辑已带同组 id 入重演队列（submit 闭包内 recordPending）
    // ——修订事件到达时按组保序重演、组内失败整组手工处置（PanelRefresh
    // 分组语义）；被拒组不入队（队列＝真实编辑意图）。
    refreshPropertiesFromLastWorkingSet();
}

// =====================================================================
// 基座安装姿态编辑页（UI-T54——MDL-22；域原语 applyBasePlacementEdit 接线）
// =====================================================================

void ModelingPanelWidget::buildBasePlacementPane()
{
    m_basePage->setObjectName(QStringLiteral("ird_modeling_base_page"));
    auto* lay = new QVBoxLayout(m_basePage);
    lay->setContentsMargins(0, 0, 0, 0);
    // 语义说明行（常驻——审核语义固定清单的呈现半区：参考系/单位/整体
    // 替换语义随页可读）。
    m_baseHint = new QLabel(this);
    m_baseHint->setObjectName(QStringLiteral("ird_modeling_base_hint"));
    m_baseHint->setWordWrap(true);
    m_baseHint->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    m_baseHint->setText(QStringLiteral(
        "基座安装姿态＝基座坐标系相对世界坐标系（MDL-22/M-11）：预设给出安装"
        "旋转（地面 0°／倒挂 180°／壁装 90°），自定义预设须给 EAA 旋转矢量"
        "（轴×角，rad）；位置为基座原点世界系坐标（m）。提交＝整体替换"
        "（落草稿，经顶栏『应用草稿』产生修订）。"));
    lay->addWidget(m_baseHint);

    auto* form = new QFormLayout();
    m_basePreset = new QComboBox(m_basePage);
    m_basePreset->setObjectName(QStringLiteral("ird_modeling_base_preset"));
    // 呈现层固定映射（词表 token 为机器权威——PanelModel 预设中文标签同款）。
    m_basePreset->addItem(QStringLiteral("地面（Ground）"));
    m_basePreset->addItem(QStringLiteral("倒挂（Inverted）"));
    m_basePreset->addItem(QStringLiteral("壁装（Wall）"));
    m_basePreset->addItem(QStringLiteral("自定义（Custom）"));
    form->addRow(QStringLiteral("安装预设"), m_basePreset);

    const auto makeSpin = [this](const char* objectName, double lo, double hi,
                                 double step) {
        auto* spin = new QDoubleSpinBox(m_basePage);
        spin->setObjectName(QString::fromLatin1(objectName));
        spin->setRange(lo, hi);
        spin->setDecimals(4);
        spin->setSingleStep(step);
        return spin;
    };
    // 位置（世界系 m）与自定义 EAA（rad）——范围宽设防误触，业务裁决在域。
    m_basePosX = makeSpin("ird_modeling_base_pos_x", -1e4, 1e4, 0.01);
    m_basePosY = makeSpin("ird_modeling_base_pos_y", -1e4, 1e4, 0.01);
    m_basePosZ = makeSpin("ird_modeling_base_pos_z", -1e4, 1e4, 0.01);
    m_baseEaaX = makeSpin("ird_modeling_base_eaa_x", -6.283185307179586, 6.283185307179586, 0.01);
    m_baseEaaY = makeSpin("ird_modeling_base_eaa_y", -6.283185307179586, 6.283185307179586, 0.01);
    m_baseEaaZ = makeSpin("ird_modeling_base_eaa_z", -6.283185307179586, 6.283185307179586, 0.01);
    // EAA 用 6 位小数（rad 的呈现精度——0.000001 rad ≈ 0.00006°，位置 4 位
    // ＝0.1 mm 已足；两者均为呈现精度，域接受任意有限 double）。
    for (auto* spin : {m_baseEaaX, m_baseEaaY, m_baseEaaZ}) { spin->setDecimals(6); }
    form->addRow(QStringLiteral("位置 X（m）"), m_basePosX);
    form->addRow(QStringLiteral("位置 Y（m）"), m_basePosY);
    form->addRow(QStringLiteral("位置 Z（m）"), m_basePosZ);
    form->addRow(QStringLiteral("自定义 EAA X（rad）"), m_baseEaaX);
    form->addRow(QStringLiteral("自定义 EAA Y（rad）"), m_baseEaaY);
    form->addRow(QStringLiteral("自定义 EAA Z（rad）"), m_baseEaaZ);
    lay->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    m_baseApplyBtn = new QPushButton(QStringLiteral("应用"), m_basePage);
    m_baseApplyBtn->setObjectName(QStringLiteral("ird_modeling_base_apply"));
    m_baseApplyBtn->setToolTip(QStringLiteral(
        "整体替换基座安装姿态（预设/自定义 EAA/位置）——落草稿，拒绝原因就地呈现"));
    auto* restoreBtn = new QPushButton(QStringLiteral("还原"), m_basePage);
    restoreBtn->setObjectName(QStringLiteral("ird_modeling_base_restore"));
    restoreBtn->setToolTip(QStringLiteral("放弃未应用输入，回填当前权威值（零脏化）"));
    btnRow->addWidget(m_baseApplyBtn);
    btnRow->addWidget(restoreBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    // 预设切换→EAA 行使能跟随（仅 Custom 有语义——I-MDL-7；非 Custom 携带
    // EAA 是域原语 fail-fast 面，UI 侧先行禁用即呈现层同口径）。
    connect(m_basePreset, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool custom = index == 3;
        for (auto* spin : {m_baseEaaX, m_baseEaaY, m_baseEaaZ}) {
            spin->setEnabled(custom);
        }
    });
    connect(m_baseApplyBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onBasePlacementApplyClicked);
    connect(restoreBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onBasePlacementRestoreClicked);
}

void ModelingPanelWidget::refreshBasePlacementPane()
{
    m_threadGuard.assertOnUiThread();  // §3.4——刷新触点同样是编辑面
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    const bool usable = ws != nullptr && m_writable;
    m_baseApplyBtn->setEnabled(usable);  // L-7 页级门控（无会话/只读＝禁用）
    if (ws == nullptr) {
        m_baseHint->setText(QStringLiteral(
            "基座安装编辑需已打开项目（草稿会话）。"));
        return;
    }
    const auto& bp = ws->design.basePlacement;
    // 预设回填（枚举→组合框序——构建期固定映射的逆）。
    int index = 0;
    switch (bp.preset) {
    case runtime::InstallationPresetToken::Ground: index = 0; break;
    case runtime::InstallationPresetToken::Inverted: index = 1; break;
    case runtime::InstallationPresetToken::Wall: index = 2; break;
    case runtime::InstallationPresetToken::Custom: index = 3; break;
    }
    m_basePreset->setCurrentIndex(index);
    const bool custom = index == 3;
    for (auto* spin : {m_baseEaaX, m_baseEaaY, m_baseEaaZ}) {
        spin->setEnabled(custom && m_writable);
    }
    // 位置回填（未提供＝0 占位——模板/导入缺省即地面零位，不伪造"已设"徽标）。
    const auto pos = bp.basePosition.tryValue();
    m_basePosX->setValue(pos.has_value() ? (*pos)[0] : 0.0);
    m_basePosY->setValue(pos.has_value() ? (*pos)[1] : 0.0);
    m_basePosZ->setValue(pos.has_value() ? (*pos)[2] : 0.0);
    // Custom EAA 回填（仅 Custom 态有值——切离 Custom 时域已复位 NotProvided）。
    const auto eaa = bp.customEaa.tryValue();
    m_baseEaaX->setValue(custom && eaa.has_value() ? (*eaa)[0] : 0.0);
    m_baseEaaY->setValue(custom && eaa.has_value() ? (*eaa)[1] : 0.0);
    m_baseEaaZ->setValue(custom && eaa.has_value() ? (*eaa)[2] : 0.0);
}

void ModelingPanelWidget::onBasePlacementApplyClicked()
{
    m_threadGuard.assertOnUiThread();  // §3.4——编辑面
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }  // 无会话/只读——编辑禁用（L-7）

    // 表单→编辑值（整体替换语义；非 Custom 不携 customEaa——携带即域原语
    // fail-fast 契约违约，UI 侧按预设分流避免）。
    BasePlacementEditValue edit;
    switch (m_basePreset->currentIndex()) {
    case 1: edit.preset = runtime::InstallationPresetToken::Inverted; break;
    case 2: edit.preset = runtime::InstallationPresetToken::Wall; break;
    case 3: edit.preset = runtime::InstallationPresetToken::Custom; break;
    default: edit.preset = runtime::InstallationPresetToken::Ground; break;
    }
    if (edit.preset == runtime::InstallationPresetToken::Custom) {
        edit.customEaa = rw::math::Vector3D<double>(
            m_baseEaaX->value(), m_baseEaaY->value(), m_baseEaaZ->value());
    }
    edit.basePosition = rw::math::Vector3D<double>(
        m_basePosX->value(), m_basePosY->value(), m_basePosZ->value());

    // 域裁决唯一（先校验后提交——拒绝时工作集字节不变）；接受走 L-2 分流
    // （脏通知＋状态行＋就绪重算钩子），拒绝经局部错误 token 就地呈现。
    const std::optional<BasePlacementEditError> err =
        applyBasePlacementEdit(*ws, edit);
    if (!err.has_value()) {
        onEditApplied("basePlacement");
        refreshPropertiesFromLastWorkingSet();
        refreshBasePlacementPane();  // 回填权威值（清未应用输入）
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(basePlacementEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onBasePlacementRestoreClicked()
{
    m_threadGuard.assertOnUiThread();
    // 纯呈现动作——权威值回填，零域调用零脏化。
    refreshBasePlacementPane();
}

// =====================================================================
// 工具/场景位姿编辑页（UI-T55——F-497 兑现③；域原语 applyToolMountEdit/
// applyScenePoseEdit 接线；选择驱动分派——二者均有 ObjectId 树锚）
// =====================================================================

QWidget* ModelingPanelWidget::buildPoseEditPage(
    const QString& caption, std::vector<ui::QuantityFieldSpec> specs,
    std::unique_ptr<ui::ParamEditModel>& model,
    ui::IFormEditOutlet& outlet, QWidget*& panelOut)
{
    auto* area = new QWidget(m_editStack);
    auto* lay = new QVBoxLayout(area);
    lay->setContentsMargins(0, 0, 0, 0);
    // 语义说明行（常驻——参考系/单位/约定随页可读，与关节/基座页同款）。
    auto* cap = new QLabel(caption, area);
    cap->setWordWrap(true);
    cap->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    lay->addWidget(cap);
    model = std::make_unique<ui::ParamEditModel>(std::move(specs));
    panelOut = ui::createParamTablePanel(*model, &outlet, {}, area);
    lay->addWidget(panelOut, 1);
    return area;
}

void ModelingPanelWidget::refreshToolMountPane()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    std::optional<std::size_t> toolIndex;
    if (ws != nullptr && m_lastSelected.has_value()) {
        const auto target = resolveSelection(*ws, *m_lastSelected);
        if (target.has_value() && target->kind == SelectedTarget::Kind::Tool) {
            toolIndex = target->index;
        }
    }
    // 空态：面板禁用＋锚清空（页本体保留——选择漂移后回归复用）。
    if (ws == nullptr || !toolIndex.has_value()
        || *toolIndex >= ws->toolObjects.size()) {
        m_toolEditTarget.reset();
        if (m_toolEditPanel != nullptr) { m_toolEditPanel->setEnabled(false); }
        return;
    }
    // ★ 与关节页的差异：本页字段集为常量（工具安装接口无类型相关单位——
    // 六行固定 m/rad），模型/面板构造期一次建成、**不随换目标重建**——
    // ParamTablePanel 的 deleteLater 重建会引入"旧表悬空引用"窗口（旧
    // 面板入删除队列而测试/用户仍持其表项引用——UI-T55 实测 AV），基线
    // 推送（setBaseline）即可完成对象切换（暂存/错误随新基线清除）。
    m_toolEditTarget = toolIndex;
    // 基线回填（mountInterface 恒有值——Transform3D 值语义，无未提供态）。
    const auto& mount = ws->toolObjects[*toolIndex].mountInterface;
    const auto rpy = rpyview::rotationToRpy(mount.R());
    m_toolEditModel->setBaseline(kToolMountXKey, mount.P()[0]);
    m_toolEditModel->setBaseline(kToolMountYKey, mount.P()[1]);
    m_toolEditModel->setBaseline(kToolMountZKey, mount.P()[2]);
    m_toolEditModel->setBaseline(kToolMountRollKey, rpy[0]);
    m_toolEditModel->setBaseline(kToolMountPitchKey, rpy[1]);
    m_toolEditModel->setBaseline(kToolMountYawKey, rpy[2]);
    m_toolEditPanel->setEnabled(m_writable);  // L-7 页级门控
}

void ModelingPanelWidget::refreshScenePosePane()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    std::optional<std::size_t> sceneIndex;
    if (ws != nullptr && m_lastSelected.has_value()) {
        const auto target = resolveSelection(*ws, *m_lastSelected);
        if (target.has_value() && target->kind == SelectedTarget::Kind::Scene) {
            sceneIndex = target->index;
        }
    }
    if (ws == nullptr || !sceneIndex.has_value()
        || *sceneIndex >= ws->sceneObjects.size()) {
        m_sceneEditTarget.reset();
        if (m_sceneEditPanel != nullptr) { m_sceneEditPanel->setEnabled(false); }
        return;
    }
    // 与工具页同款：字段集常量，不随换目标重建（deleteLater 悬空窗口——
    // UI-T55 实测教训；基线推送完成对象切换）。
    m_sceneEditTarget = sceneIndex;
    const auto& world = ws->sceneObjects[*sceneIndex].worldPose;
    const auto rpy = rpyview::rotationToRpy(world.R());
    m_sceneEditModel->setBaseline(kSceneWorldXKey, world.P()[0]);
    m_sceneEditModel->setBaseline(kSceneWorldYKey, world.P()[1]);
    m_sceneEditModel->setBaseline(kSceneWorldZKey, world.P()[2]);
    m_sceneEditModel->setBaseline(kSceneWorldRollKey, rpy[0]);
    m_sceneEditModel->setBaseline(kSceneWorldPitchKey, rpy[1]);
    m_sceneEditModel->setBaseline(kSceneWorldYawKey, rpy[2]);
    m_sceneEditPanel->setEnabled(m_writable);
}

void ModelingPanelWidget::applyToolMountEdits(const ui::ParamEditSet& editSet)
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable) { return; }
    const std::size_t toolIndex = *m_toolEditTarget;
    if (toolIndex >= ws->toolObjects.size()) { return; }  // 结构变更竞态防御

    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : editSet.changes) { staged[change.key] = change.newSi; }
    if (staged.empty()) { return; }
    // 组内分量：脏键取移交新值，未脏键取当前权威（与关节页 assembly 同款）。
    const auto component = [this, &staged](const char* key) -> std::optional<double> {
        if (const auto it = staged.find(key); it != staged.end()) { return it->second; }
        return m_toolEditModel ? m_toolEditModel->currentValueSi(key) : std::nullopt;
    };
    const auto x = component(kToolMountXKey);
    const auto y = component(kToolMountYKey);
    const auto z = component(kToolMountZKey);
    const auto r = component(kToolMountRollKey);
    const auto p = component(kToolMountPitchKey);
    const auto w = component(kToolMountYawKey);
    if (!(x && y && z && r && p && w)) { return; }  // 权威值恒在——防御面

    // 域裁决唯一（六分量有限性在域；RPY 组合归域内核）。
    const std::optional<PartPoseEditError> err = applyToolMountEdit(
        *ws, toolIndex,
        PartPoseEditValue{*x, *y, *z, *r, *p, *w});
    if (!err.has_value()) {
        onEditApplied("tools[" + std::to_string(toolIndex) + "].mountInterface");
    } else {
        EditRejection rejection;
        rejection.codeToken = std::string(partPoseEditErrorCodeToken(err->code));
        rejection.detail = err->detail;
        onEditRejected(rejection);
    }
    refreshPropertiesFromLastWorkingSet();  // 权威同步（乐观基线修正）
}

void ModelingPanelWidget::applyScenePoseEdits(const ui::ParamEditSet& editSet)
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_sceneEditTarget.has_value() || !m_writable) { return; }
    const std::size_t sceneIndex = *m_sceneEditTarget;
    if (sceneIndex >= ws->sceneObjects.size()) { return; }

    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : editSet.changes) { staged[change.key] = change.newSi; }
    if (staged.empty()) { return; }
    const auto component = [this, &staged](const char* key) -> std::optional<double> {
        if (const auto it = staged.find(key); it != staged.end()) { return it->second; }
        return m_sceneEditModel ? m_sceneEditModel->currentValueSi(key) : std::nullopt;
    };
    const auto x = component(kSceneWorldXKey);
    const auto y = component(kSceneWorldYKey);
    const auto z = component(kSceneWorldZKey);
    const auto r = component(kSceneWorldRollKey);
    const auto p = component(kSceneWorldPitchKey);
    const auto w = component(kSceneWorldYawKey);
    if (!(x && y && z && r && p && w)) { return; }

    const std::optional<PartPoseEditError> err = applyScenePoseEdit(
        *ws, sceneIndex,
        PartPoseEditValue{*x, *y, *z, *r, *p, *w});
    if (!err.has_value()) {
        onEditApplied("scenes[" + std::to_string(sceneIndex) + "].worldPose");
    } else {
        EditRejection rejection;
        rejection.codeToken = std::string(partPoseEditErrorCodeToken(err->code));
        rejection.detail = err->detail;
        onEditRejected(rejection);
    }
    refreshPropertiesFromLastWorkingSet();
}

// =====================================================================
// TCP 列表编辑段（UI-T57——F-497 兑现④；MDL-13 不变量流；域原语
// applyTcpAddEdit/applyTcpRemoveEdit/applyTcpOffsetEdit/applyDefaultTcpSwitchEdit；
// UI-T58 增显示名行编辑——applyTcpDisplayNameEdit）
// =====================================================================

void ModelingPanelWidget::buildTcpPane()
{
    // 挂工具页既有布局（mount 面板之下）——工具页＝安装接口＋TCP 两段。
    auto* lay = qobject_cast<QVBoxLayout*>(m_toolArea->layout());
    if (lay == nullptr) { return; }  // 布局未建（装配序防御）
    auto* cap = new QLabel(QStringLiteral(
        "TCP 列表（tcp 系相对安装接口——位置 m／姿态 rad，ZYX 约定；"
        "键为 defaultTcp 引用锚，须非空唯一；列表至少保留一条）"), m_toolArea);
    cap->setWordWrap(true);
    cap->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    lay->addWidget(cap);

    auto* row = new QHBoxLayout();
    m_tcpCombo = new QComboBox(m_toolArea);
    m_tcpCombo->setObjectName(QStringLiteral("ird_modeling_tcp_combo"));
    row->addWidget(m_tcpCombo, 1);
    m_tcpAddBtn = new QPushButton(QStringLiteral("新增 TCP"), m_toolArea);
    m_tcpAddBtn->setObjectName(QStringLiteral("ird_modeling_tcp_add"));
    m_tcpAddBtn->setToolTip(QStringLiteral(
        "追加一条 TCP（自动生成键 tcp-N，offset 恒位姿——数值随后可编辑）"));
    m_tcpRemoveBtn = new QPushButton(QStringLiteral("删除 TCP"), m_toolArea);
    m_tcpRemoveBtn->setObjectName(QStringLiteral("ird_modeling_tcp_remove"));
    m_tcpRemoveBtn->setToolTip(QStringLiteral(
        "删除当前选中 TCP（最后一条与被 defaultTcp 引用者被域守卫拒绝）"));
    m_tcpDefaultBtn = new QPushButton(QStringLiteral("设为默认"), m_toolArea);
    m_tcpDefaultBtn->setObjectName(QStringLiteral("ird_modeling_tcp_default"));
    m_tcpDefaultBtn->setToolTip(QStringLiteral(
        "把当前选中 TCP 设为工具默认（根 defaultTcp 整体写入）"));
    row->addWidget(m_tcpAddBtn);
    row->addWidget(m_tcpRemoveBtn);
    row->addWidget(m_tcpDefaultBtn);
    lay->addLayout(row);

    // 显示名行（UI-T58——TCP displayName 文本行编辑，UI-T57 卡"诚实边界"
    // 的顺延项）：仅呈现字段（UX-02 口径），不进 Description 编译身份；
    // 空值经 placeholder 提示"按 TCP 键呈现"回落（域对空串接受——MDL-13
    // 不变量只约束键与列表长度，不约束呈现名）。
    auto* nameRow = new QHBoxLayout();
    auto* nameLabel = new QLabel(QStringLiteral("显示名"), m_toolArea);
    nameRow->addWidget(nameLabel);
    m_tcpDisplayNameEdit = new QLineEdit(m_toolArea);
    m_tcpDisplayNameEdit->setObjectName(QStringLiteral("ird_modeling_tcp_displayname"));
    m_tcpDisplayNameEdit->setPlaceholderText(QStringLiteral("留空＝按 TCP 键呈现"));
    m_tcpDisplayNameEdit->setToolTip(QStringLiteral(
        "当前选中 TCP 的显示名（仅呈现，不入编译身份）——修改后用右侧按钮应用"));
    nameRow->addWidget(m_tcpDisplayNameEdit, 1);
    m_tcpDisplayNameApplyBtn = new QPushButton(QStringLiteral("应用显示名"), m_toolArea);
    m_tcpDisplayNameApplyBtn->setObjectName(QStringLiteral("ird_modeling_tcp_displayname_apply"));
    m_tcpDisplayNameApplyBtn->setToolTip(QStringLiteral(
        "把文本提交为当前选中 TCP 的显示名（与现值相同＝零修订零动作）"));
    nameRow->addWidget(m_tcpDisplayNameApplyBtn);
    lay->addLayout(nameRow);

    // TCP offset 编辑模型/面板（字段集常量——构造期一次建成，选中切换仅
    // 推基线；与工具/场景页同款不重建纪律）。
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    std::vector<ui::QuantityFieldSpec> specs = {
        ui::makeQuantityFieldSpec(kTcpOffsetXKey, "TCP X", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kTcpOffsetYKey, "TCP Y", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kTcpOffsetZKey, "TCP Z", core::QuantityKind::Length, m, m),
        ui::makeQuantityFieldSpec(kTcpOffsetRollKey, "TCP R（roll）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kTcpOffsetPitchKey, "TCP P（pitch）", core::QuantityKind::Angle, rad, rad),
        ui::makeQuantityFieldSpec(kTcpOffsetYawKey, "TCP Y（yaw）", core::QuantityKind::Angle, rad, rad),
    };
    m_tcpOffsetModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
    m_tcpOffsetPanel = ui::createParamTablePanel(
        *m_tcpOffsetModel, m_tcpOutlet.get(), {}, m_toolArea);
    lay->addWidget(m_tcpOffsetPanel, 1);

    connect(m_tcpCombo, &QComboBox::currentIndexChanged, this,
            &ModelingPanelWidget::onTcpComboChanged);
    connect(m_tcpAddBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onTcpAddClicked);
    connect(m_tcpRemoveBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onTcpRemoveClicked);
    connect(m_tcpDefaultBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onTcpDefaultClicked);
    connect(m_tcpDisplayNameApplyBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onTcpDisplayNameApplyClicked);
}

void ModelingPanelWidget::refreshTcpPane()
{
    // 选中工具现取（工具页目标——refreshToolMountPane 已定 toolIndex；
    // 本段复用同一目标：无工具/越界＝整段禁用）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    const bool hasTool = ws != nullptr && m_toolEditTarget.has_value()
                         && *m_toolEditTarget < ws->toolObjects.size();
    if (m_tcpCombo == nullptr) { return; }

    // combo 重建（保留选中键——列表条目变化后仍定位原键；失效键回落首项；
    // QSignalBlocker 防重建期 currentIndexChanged 触发重入）。
    if (!hasTool) {
        {
            QSignalBlocker blocker(m_tcpCombo);
            if (m_tcpCombo->count() > 0) { m_tcpCombo->clear(); }
        }
        m_tcpSelectedKey.clear();
        if (m_tcpOffsetPanel != nullptr) { m_tcpOffsetPanel->setEnabled(false); }
        // 显示名行同步禁用（L-7 无目标分支——UI-T58）。
        if (m_tcpDisplayNameEdit != nullptr) { m_tcpDisplayNameEdit->setEnabled(false); }
        if (m_tcpDisplayNameApplyBtn != nullptr) { m_tcpDisplayNameApplyBtn->setEnabled(false); }
        return;
    }
    const auto& tool = ws->toolObjects[*m_toolEditTarget];
    const QString oldKey = m_tcpSelectedKey;
    {
        QSignalBlocker blocker(m_tcpCombo);
        m_tcpCombo->clear();
        int keepIndex = -1;
        for (std::size_t i = 0; i < tool.tcpList.size(); ++i) {
            m_tcpCombo->addItem(QString::fromStdString(tool.tcpList[i].key));
            if (QString::fromStdString(tool.tcpList[i].key) == oldKey) {
                keepIndex = static_cast<int>(i);
            }
        }
        m_tcpCombo->setCurrentIndex(keepIndex >= 0 ? keepIndex : 0);
    }
    m_tcpSelectedKey = m_tcpCombo->currentText();
    m_tcpAddBtn->setEnabled(m_writable);
    m_tcpRemoveBtn->setEnabled(m_writable && !m_tcpSelectedKey.isEmpty());
    m_tcpDefaultBtn->setEnabled(m_writable && !m_tcpSelectedKey.isEmpty());
    if (m_tcpOffsetPanel != nullptr) { m_tcpOffsetPanel->setEnabled(m_writable); }
    if (m_tcpDisplayNameEdit != nullptr) { m_tcpDisplayNameEdit->setEnabled(m_writable); }
    if (m_tcpDisplayNameApplyBtn != nullptr) {
        m_tcpDisplayNameApplyBtn->setEnabled(m_writable && !m_tcpSelectedKey.isEmpty());
    }

    // offset 基线回填（选中 TCP——offset 恒有值；RPY 反解呈现）。
    const int sel = m_tcpCombo->currentIndex();
    if (sel < 0) { return; }
    const auto& entry = tool.tcpList[static_cast<std::size_t>(sel)];
    // 显示名基线回填（UI-T58——选中条目 displayName 原样呈现；空值经
    // placeholder 提示回落。setText 不触发 editingFinished，无重入面）。
    if (m_tcpDisplayNameEdit != nullptr) {
        m_tcpDisplayNameEdit->setText(QString::fromStdString(entry.displayName));
    }
    const auto rpy = rpyview::rotationToRpy(entry.offset.R());
    m_tcpOffsetModel->setBaseline(kTcpOffsetXKey, entry.offset.P()[0]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetYKey, entry.offset.P()[1]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetZKey, entry.offset.P()[2]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetRollKey, rpy[0]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetPitchKey, rpy[1]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetYawKey, rpy[2]);
}

void ModelingPanelWidget::onTcpComboChanged(int index)
{
    // 选中切换→键更新＋offset 基线回填（零脏化——基线是权威非编辑）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value()
        || *m_toolEditTarget >= ws->toolObjects.size()
        || m_tcpCombo == nullptr || index < 0
        || index >= static_cast<int>(ws->toolObjects[*m_toolEditTarget].tcpList.size())) {
        return;
    }
    m_tcpSelectedKey = m_tcpCombo->currentText();
    const auto& entry =
        ws->toolObjects[*m_toolEditTarget].tcpList[static_cast<std::size_t>(index)];
    // 显示名基线随选中切换回填（UI-T58——与 offset 基线同源同刻）。
    if (m_tcpDisplayNameEdit != nullptr) {
        m_tcpDisplayNameEdit->setText(QString::fromStdString(entry.displayName));
    }
    const auto rpy = rpyview::rotationToRpy(entry.offset.R());
    m_tcpOffsetModel->setBaseline(kTcpOffsetXKey, entry.offset.P()[0]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetYKey, entry.offset.P()[1]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetZKey, entry.offset.P()[2]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetRollKey, rpy[0]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetPitchKey, rpy[1]);
    m_tcpOffsetModel->setBaseline(kTcpOffsetYawKey, rpy[2]);
}

void ModelingPanelWidget::onTcpAddClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable) { return; }
    // 自动键：tcp-N 首空位（tcp-1 为种子惯例首键）。
    std::size_t n = ws->toolObjects[*m_toolEditTarget].tcpList.size() + 1;
    std::string key = "tcp-" + std::to_string(n);
    while (std::any_of(ws->toolObjects[*m_toolEditTarget].tcpList.begin(),
                       ws->toolObjects[*m_toolEditTarget].tcpList.end(),
                       [&key](const TcpEntry& e) { return e.key == key; })) {
        key = "tcp-" + std::to_string(++n);
    }
    const std::optional<TcpEditError> err = applyTcpAddEdit(
        *ws, *m_toolEditTarget, key, "TCP " + key, PartPoseEditValue{});
    if (!err.has_value()) {
        onEditApplied("tools[" + std::to_string(*m_toolEditTarget) + "].tcpList");
        m_tcpSelectedKey = QString::fromStdString(key);  // 新键即选中
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(tcpEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onTcpRemoveClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable
        || m_tcpSelectedKey.isEmpty()) { return; }
    const std::optional<TcpEditError> err = applyTcpRemoveEdit(
        *ws, *m_toolEditTarget, m_tcpSelectedKey.toStdString());
    if (!err.has_value()) {
        onEditApplied("tools[" + std::to_string(*m_toolEditTarget) + "].tcpList");
        m_tcpSelectedKey.clear();  // 回落首项（refreshTcpPane 内定位）
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(tcpEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onTcpDefaultClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable
        || m_tcpSelectedKey.isEmpty()) { return; }
    const std::optional<TcpEditError> err = applyDefaultTcpSwitchEdit(
        *ws, *m_toolEditTarget, m_tcpSelectedKey.toStdString());
    if (!err.has_value()) {
        onEditApplied("design.defaultTcp");
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(tcpEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onTcpDisplayNameApplyClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable
        || m_tcpSelectedKey.isEmpty() || m_tcpDisplayNameEdit == nullptr) { return; }
    const std::string key = m_tcpSelectedKey.toStdString();
    const std::string displayName = m_tcpDisplayNameEdit->text().toStdString();
    // 零差异＝零修订（PA-2 修订只增不改——与现值相同的提交不产生无信息
    // 量历史；ParamTablePanel"未脏不提交"同款诚实语义）。选中键与工作集
    // 失配（刷新滞后窗口）＝零动作，不伪造提交面。
    const auto& tcpList = ws->toolObjects[*m_toolEditTarget].tcpList;
    const auto it = std::find_if(tcpList.begin(), tcpList.end(),
                                 [&key](const TcpEntry& e) { return e.key == key; });
    if (it == tcpList.end() || it->displayName == displayName) { return; }

    const std::optional<TcpEditError> err = applyTcpDisplayNameEdit(
        *ws, *m_toolEditTarget, key, displayName);
    if (!err.has_value()) {
        onEditApplied("tools[" + std::to_string(*m_toolEditTarget) + "].tcp(" + key + ")");
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(tcpEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::applyTcpOffsetEdits(const ui::ParamEditSet& editSet)
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_toolEditTarget.has_value() || !m_writable
        || m_tcpSelectedKey.isEmpty()) { return; }

    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : editSet.changes) { staged[change.key] = change.newSi; }
    if (staged.empty()) { return; }
    const auto component = [this, &staged](const char* key) -> std::optional<double> {
        if (const auto it = staged.find(key); it != staged.end()) { return it->second; }
        return m_tcpOffsetModel ? m_tcpOffsetModel->currentValueSi(key) : std::nullopt;
    };
    const auto x = component(kTcpOffsetXKey);
    const auto y = component(kTcpOffsetYKey);
    const auto z = component(kTcpOffsetZKey);
    const auto r = component(kTcpOffsetRollKey);
    const auto p = component(kTcpOffsetPitchKey);
    const auto w = component(kTcpOffsetYawKey);
    if (!(x && y && z && r && p && w)) { return; }

    const std::optional<TcpEditError> err = applyTcpOffsetEdit(
        *ws, *m_toolEditTarget, m_tcpSelectedKey.toStdString(),
        PartPoseEditValue{*x, *y, *z, *r, *p, *w});
    if (!err.has_value()) {
        onEditApplied("tools[" + std::to_string(*m_toolEditTarget) + "].tcp("
                      + m_tcpSelectedKey.toStdString() + ")");
    } else {
        EditRejection rejection;
        rejection.codeToken = std::string(tcpEditErrorCodeToken(err->code));
        rejection.detail = err->detail;
        onEditRejected(rejection);
    }
    refreshPropertiesFromLastWorkingSet();
}

// =====================================================================
// 位姿集/传动编辑段（UI-T60——F-497 余项收尾；域原语 applyPoseSetEntry*
// ／applyDrivetrain*；位姿集经 mergeNamedPoseEntries 单一合并实现——
// NFR-MNT-04；两页行集随关节表（重建策略——UI-T55"字段集可变需重建"侧）
// =====================================================================

class ModelingPanelWidget::PoseSetEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit PoseSetEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}
    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        // config 表确认流＝模型基线已推进（confirmApply 后 pendingChanges
        // 清空）——本出口把确认值同步进页侧权威基线缓存（位姿条目的提交
        // 锚＝"保存条目"钮，组装"净取权威"须读到确认后的值；不同步即确认
        // 值丢失——UI-T60 实测）。
        m_owner.stagePoseSetConfigEdits(editSet);
    }
private:
    ModelingPanelWidget& m_owner;
};

class ModelingPanelWidget::DrivetrainEditOutlet final : public ui::IFormEditOutlet {
public:
    explicit DrivetrainEditOutlet(ModelingPanelWidget& owner) : m_owner(owner) {}
    void applyEdits(const ui::ParamEditSet& editSet) override
    {
        m_owner.applyDrivetrainEdits(editSet);
    }
private:
    ModelingPanelWidget& m_owner;
};

void ModelingPanelWidget::buildPoseSetPane()
{
    auto* area = new QWidget(m_editStack);
    area->setObjectName(QStringLiteral("ird_modeling_pose_area"));  // 对账排除＋gui 锚
    m_poseSetArea = area;  // 先落成员（rebuildPoseSetConfigModel 挂本布局）
    auto* lay = new QVBoxLayout(area);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* cap = new QLabel(QStringLiteral(
        "命名位姿编辑（与关节序一一对应——rad/m 随关节类型；"
        "homeConfiguration/zeroConfiguration 为保留键，写入/删除归会话"
        "复位面不在此页；保存条目＝键＋备注＋构型整批提交）"), area);
    cap->setWordWrap(true);
    cap->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    lay->addWidget(cap);

    auto* entryRow = new QHBoxLayout();
    m_poseSetCombo = new QComboBox(area);
    m_poseSetCombo->setObjectName(QStringLiteral("ird_modeling_pose_combo"));
    entryRow->addWidget(m_poseSetCombo, 1);
    m_poseSetAddBtn = new QPushButton(QStringLiteral("新增条目"), area);
    m_poseSetAddBtn->setObjectName(QStringLiteral("ird_modeling_pose_add"));
    m_poseSetAddBtn->setToolTip(QStringLiteral(
        "新增一条命名位姿（自动键 pose-N，构型零位种子——数值随后可编辑）"));
    m_poseSetRemoveBtn = new QPushButton(QStringLiteral("删除条目"), area);
    m_poseSetRemoveBtn->setObjectName(QStringLiteral("ird_modeling_pose_remove"));
    m_poseSetRemoveBtn->setToolTip(QStringLiteral(
        "删除当前选中条目（保留键被域守卫拒绝）"));
    m_poseSetApplyBtn = new QPushButton(QStringLiteral("保存条目"), area);
    m_poseSetApplyBtn->setObjectName(QStringLiteral("ird_modeling_pose_apply"));
    m_poseSetApplyBtn->setToolTip(QStringLiteral(
        "把键＋备注＋构型（脏值取新、未编辑取权威）组装为一条命名位姿提交"));
    entryRow->addWidget(m_poseSetAddBtn);
    entryRow->addWidget(m_poseSetRemoveBtn);
    entryRow->addWidget(m_poseSetApplyBtn);
    lay->addLayout(entryRow);

    auto* keyNoteForm = new QHBoxLayout();
    auto* keyLabel = new QLabel(QStringLiteral("键"), area);
    keyNoteForm->addWidget(keyLabel);
    m_poseSetKeyEdit = new QLineEdit(area);
    m_poseSetKeyEdit->setObjectName(QStringLiteral("ird_modeling_pose_key"));
    keyNoteForm->addWidget(m_poseSetKeyEdit, 1);
    auto* noteLabel = new QLabel(QStringLiteral("备注"), area);
    keyNoteForm->addWidget(noteLabel);
    m_poseSetNoteEdit = new QLineEdit(area);
    m_poseSetNoteEdit->setObjectName(QStringLiteral("ird_modeling_pose_note"));
    keyNoteForm->addWidget(m_poseSetNoteEdit, 2);
    lay->addLayout(keyNoteForm);

    // 构型表（行随关节表——关节计数缓存，变化才重建；UI-T55 两策略并存
    // 的"字段集可变需重建"侧。键前缀 pose-config-<序>）。
    rebuildPoseSetConfigModel();
    if (m_poseSetConfigPanel != nullptr) {
        lay->addWidget(m_poseSetConfigPanel, 1);  // 零关节守卫下为空——重建期再挂
    }

    connect(m_poseSetCombo, &QComboBox::currentIndexChanged, this,
            &ModelingPanelWidget::onPoseSetComboChanged);
    connect(m_poseSetAddBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onPoseSetAddClicked);
    connect(m_poseSetRemoveBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onPoseSetRemoveClicked);
    connect(m_poseSetApplyBtn, &QPushButton::clicked, this,
            &ModelingPanelWidget::onPoseSetApplyClicked);
}

void ModelingPanelWidget::rebuildPoseSetConfigModel()
{
    // 零关节守卫（构造期无编辑目标——ParamEditModel 空字段集 fail-fast，
    // 行集延迟到刷新期有目标时重建）。
    ModelingWorkingSet* wsProbe = m_editTarget ? m_editTarget() : nullptr;
    if (wsProbe == nullptr || wsProbe->design.joints.empty()) {
        if (m_poseSetConfigPanel != nullptr) {
            m_poseSetConfigPanel->deleteLater();
            m_poseSetConfigPanel = nullptr;
        }
        m_poseSetConfigJointCount = 0;
        return;
    }
    // 旧行随 Qt 父子析构（deleteLater——刷新路径内延迟删除防重入；
    // UI-T55 实测教训同款防悬空纪律）。
    if (m_poseSetConfigPanel != nullptr) {
        m_poseSetConfigPanel->deleteLater();
        m_poseSetConfigPanel = nullptr;
    }
    m_poseSetOutlet = std::make_unique<PoseSetEditOutlet>(*this);
    const core::UnitToken m = core::UnitToken::find("m").value();
    const core::UnitToken rad = core::UnitToken::find("rad").value();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    const std::size_t jointCount = ws != nullptr ? ws->design.joints.size() : 0;
    std::vector<ui::QuantityFieldSpec> specs;
    specs.reserve(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        // 量纲随关节类型（Prismatic＝移动 m，其余＝转动 rad——既有呈现
        // 辅助 jointEditQuantityKind 单一规则复用）。
        const core::QuantityKind kind =
            jointEditQuantityKind(ws->design.joints[i].type);
        const core::UnitToken unit = jointEditUnitToken(kind);
        specs.push_back(ui::makeQuantityFieldSpec(
            "pose-config-" + std::to_string(i),
            "J" + std::to_string(i + 1) + " 构型",
            kind, unit, unit));
    }
    m_poseSetConfigModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
    m_poseSetConfigPanel = ui::createParamTablePanel(
        *m_poseSetConfigModel, m_poseSetOutlet.get(), {}, m_poseSetArea);
    qobject_cast<QVBoxLayout*>(m_poseSetArea->layout())->addWidget(m_poseSetConfigPanel);
    m_poseSetConfigJointCount = jointCount;
}

void ModelingPanelWidget::refreshPoseSetPane()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (m_poseSetCombo == nullptr) { return; }
    // 关节表变化→构型表重建（行集契约）。
    const std::size_t jointCount = ws != nullptr ? ws->design.joints.size() : 0;
    if (jointCount != m_poseSetConfigJointCount) { rebuildPoseSetConfigModel(); }
    const bool writable = m_writable;

    // 条目清单重建（仅用户条目——保留键不进编辑面；QSignalBlocker 防重
    // 入；保留选中键，失效回落首项）。
    const QString oldKey = m_poseSetSelectedKey;
    {
        QSignalBlocker blocker(m_poseSetCombo);
        m_poseSetCombo->clear();
        int keepIndex = -1;
        if (ws != nullptr && ws->poseSetObject.has_value()) {
            int row = 0;
            for (const PoseSetEntry& e : ws->poseSetObject->entries) {
                if (isReservedPoseKey(e.key)) { continue; }
                m_poseSetCombo->addItem(QString::fromStdString(e.key));
                if (QString::fromStdString(e.key) == oldKey) { keepIndex = row; }
                ++row;
            }
        }
        m_poseSetCombo->setCurrentIndex(keepIndex >= 0 ? keepIndex
                                                       : (m_poseSetCombo->count() > 0 ? 0 : -1));
    }
    m_poseSetSelectedKey = m_poseSetCombo->currentText();
    m_poseSetAddBtn->setEnabled(writable);
    m_poseSetRemoveBtn->setEnabled(writable && !m_poseSetSelectedKey.isEmpty());
    m_poseSetApplyBtn->setEnabled(writable);
    m_poseSetKeyEdit->setEnabled(writable);
    m_poseSetNoteEdit->setEnabled(writable);
    if (m_poseSetConfigPanel != nullptr) { m_poseSetConfigPanel->setEnabled(writable); }

    // 选中条目基线回填（key/note 行编辑＋构型表；未选中＝键/备注清空、
    // 构型表基线清空〔nullopt——不虚构数值〕）。
    const int sel = m_poseSetCombo->currentIndex();
    std::optional<PoseSetEntry> entry;
    if (ws != nullptr && ws->poseSetObject.has_value() && sel >= 0) {
        for (const PoseSetEntry& e : ws->poseSetObject->entries) {
            if (e.key == m_poseSetSelectedKey.toStdString()) { entry = e; break; }
        }
    }
    m_poseSetKeyEdit->setText(entry.has_value()
                                  ? QString::fromStdString(entry->key)
                                  : QString());
    m_poseSetNoteEdit->setText(entry.has_value()
                                   ? QString::fromStdString(entry->note)
                                   : QString());
    m_poseSetConfigBaseline.assign(jointCount, 0.0);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::string key = "pose-config-" + std::to_string(i);
        if (entry.has_value() && i < entry->jointConfiguration.size()) {
            m_poseSetConfigBaseline[i] = entry->jointConfiguration[i];
            m_poseSetConfigModel->setBaseline(key, entry->jointConfiguration[i]);
        } else {
            m_poseSetConfigModel->setBaseline(key, std::nullopt);
        }
    }
}

void ModelingPanelWidget::onPoseSetComboChanged(int index)
{
    // 选中切换→基线回填（零脏化；refreshPoseSetPane 重建期被 QSignalBlocker
    // 屏蔽，仅用户切换到达此处）。
    Q_UNUSED(index);
    refreshPoseSetPane();
}

void ModelingPanelWidget::onPoseSetAddClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }
    // 自动键 pose-N 首空位（TCP add 的 tcp-N 同款惯例）。
    std::size_t n = 1;
    auto keyExists = [&ws](const std::string& k) {
        return ws->poseSetObject.has_value()
            && std::any_of(ws->poseSetObject->entries.begin(),
                           ws->poseSetObject->entries.end(),
                           [&k](const PoseSetEntry& e) { return e.key == k; });
    };
    std::string key = "pose-" + std::to_string(n);
    while (keyExists(key)) { key = "pose-" + std::to_string(++n); }
    // 零位姿种子（构型零向量，长度＝根关节表——TCP 恒位姿种子同款惯例；
    // 数值随后可编辑）。
    PoseSetEntry entry;
    entry.key = key;
    entry.jointConfiguration.assign(ws->design.joints.size(), 0.0);
    const std::optional<PoseEditError> err = applyPoseSetEntryUpsertEdit(*ws, entry);
    if (!err.has_value()) {
        onEditApplied("poseSet.entries");
        m_poseSetSelectedKey = QString::fromStdString(key);  // 新键即选中
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(poseEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::onPoseSetRemoveClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable || m_poseSetSelectedKey.isEmpty()) { return; }
    const std::optional<PoseEditError> err = applyPoseSetEntryRemoveEdit(
        *ws, m_poseSetSelectedKey.toStdString());
    if (!err.has_value()) {
        onEditApplied("poseSet.entries");
        m_poseSetSelectedKey.clear();  // 回落首项（refresh 内定位）
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(poseEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::stagePoseSetConfigEdits(const ui::ParamEditSet& editSet)
{
    // 确认值→页侧权威基线缓存（键 pose-config-<序>→槽位序；UI-T60——
    // 保存条目组装"净取权威"的数据源）。
    const std::string prefix = "pose-config-";
    for (const ui::ParamChange& change : editSet.changes) {
        if (change.key.rfind(prefix, 0) != 0) { continue; }
        const auto idx = std::stoull(change.key.substr(prefix.size()));
        if (idx < m_poseSetConfigBaseline.size()) {
            m_poseSetConfigBaseline[idx] = change.newSi;
        }
    }
}

void ModelingPanelWidget::onPoseSetApplyClicked()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable || m_poseSetConfigModel == nullptr) { return; }
    // 组装条目：键/备注现行编辑；构型脏值取暂存、未编辑取权威基线
    // （UI-T55 分组装配同款"脏取新/净取权威"）。
    PoseSetEntry entry;
    entry.key = m_poseSetKeyEdit->text().toStdString();
    entry.note = m_poseSetNoteEdit->text().toStdString();
    entry.jointConfiguration.assign(m_poseSetConfigBaseline.size(), 0.0);
    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : m_poseSetConfigModel->pendingChanges()) {
        staged[change.key] = change.newSi;
    }
    for (std::size_t i = 0; i < m_poseSetConfigBaseline.size(); ++i) {
        const std::string key = "pose-config-" + std::to_string(i);
        entry.jointConfiguration[i] =
            staged.count(key) != 0 ? staged[key] : m_poseSetConfigBaseline[i];
    }
    const std::optional<PoseEditError> err = applyPoseSetEntryUpsertEdit(*ws, entry);
    if (!err.has_value()) {
        onEditApplied("poseSet.entries");
        m_poseSetSelectedKey = QString::fromStdString(entry.key);
        refreshPropertiesFromLastWorkingSet();
        return;
    }
    EditRejection rejection;
    rejection.codeToken = std::string(poseEditErrorCodeToken(err->code));
    rejection.detail = err->detail;
    onEditRejected(rejection);
}

void ModelingPanelWidget::buildDrivetrainPane()
{
    auto* area = new QWidget(m_editStack);
    auto* lay = new QVBoxLayout(area);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* cap = new QLabel(QStringLiteral(
        "传动设计编辑（传动比无量纲须 >0；摩擦三元 fv/fc/bias 与力矩限值"
        "额定/峰值——单位随关节类型；耦合 coupling 属 R2/阶段 D、目录回填"
        "归选型命令，均不在此页）"), area);
    cap->setWordWrap(true);
    cap->setStyleSheet(
        QStringLiteral("color: %1;").arg(QString::fromLatin1(ui::palette::kTextMuted)));
    lay->addWidget(cap);
    // 数值表（行随关节表：ratio N＋摩擦 3N＋力矩 2N；关节计数缓存，变化
    // 才重建）。首次构建在 rebuildDrivetrainModel 内完成（m_drivetrainArea
    // 先落成员再建表——面板挂本布局）。
    area->setObjectName(QStringLiteral("ird_modeling_drivetrain_area"));  // 对账排除＋gui 锚
    m_drivetrainArea = area;
    rebuildDrivetrainModel();
    if (m_drivetrainPanel != nullptr) {
        lay->addWidget(m_drivetrainPanel, 1);  // 零关节守卫下为空——重建期再挂
    }
}

void ModelingPanelWidget::rebuildDrivetrainModel()
{
    // 零关节守卫（构造期无编辑目标——同 rebuildPoseSetConfigModel 口径）。
    ModelingWorkingSet* wsProbe = m_editTarget ? m_editTarget() : nullptr;
    if (wsProbe == nullptr || wsProbe->design.joints.empty()) {
        if (m_drivetrainPanel != nullptr) {
            m_drivetrainPanel->deleteLater();
            m_drivetrainPanel = nullptr;
        }
        m_drivetrainJointCount = 0;
        return;
    }
    if (m_drivetrainPanel != nullptr) {
        m_drivetrainPanel->deleteLater();
        m_drivetrainPanel = nullptr;
    }
    m_drivetrainOutlet = std::make_unique<DrivetrainEditOutlet>(*this);
    const core::UnitToken one = core::UnitToken::find("1").value();
    const core::UnitToken nm = core::UnitToken::find("N*m").value();
    const core::UnitToken n = core::UnitToken::find("N").value();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    const std::size_t jointCount = ws != nullptr ? ws->design.joints.size() : 0;
    std::vector<ui::QuantityFieldSpec> specs;
    specs.reserve(jointCount * 6);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const bool prismatic =
            ws->design.joints[i].type == JointType::Prismatic;
        const std::string idx = std::to_string(i);
        const std::string jn = "J" + std::to_string(i + 1) + " ";
        // 传动比（无量纲——SI 系数 1；I-MDL-11 须 >0）。
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-ratio-" + idx, jn + "传动比",
            core::QuantityKind::Dimensionless, one, one));
        // 摩擦三元：黏滞 fv 量纲（N·m·s/rad 或 N·s/m）不在 core 词表——
        // F-510 族诚实边界：数值面原样直投零换算（标签注明真实单位），
        // 不虚构量纲 token。fc/bias 量纲在表（转动 N·m／移动 N）。
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-friction-viscous-" + idx, jn + "摩擦 fv（N·m·s/rad 或 N·s/m）",
            core::QuantityKind::Dimensionless, one, one));
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-friction-coulomb-" + idx, jn + "摩擦 fc",
            prismatic ? core::QuantityKind::Force : core::QuantityKind::Torque,
            prismatic ? n : nm, prismatic ? n : nm));
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-friction-bias-" + idx, jn + "摩擦偏置",
            prismatic ? core::QuantityKind::Force : core::QuantityKind::Torque,
            prismatic ? n : nm, prismatic ? n : nm));
        // 力矩限值对（额定/峰值——转动 N·m／移动 N）。
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-torque-rated-" + idx, jn + "力矩额定",
            prismatic ? core::QuantityKind::Force : core::QuantityKind::Torque,
            prismatic ? n : nm, prismatic ? n : nm));
        specs.push_back(ui::makeQuantityFieldSpec(
            "dt-torque-peak-" + idx, jn + "力矩峰值",
            prismatic ? core::QuantityKind::Force : core::QuantityKind::Torque,
            prismatic ? n : nm, prismatic ? n : nm));
    }
    m_drivetrainModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
    m_drivetrainPanel = ui::createParamTablePanel(
        *m_drivetrainModel, m_drivetrainOutlet.get(), {}, m_drivetrainArea);
    qobject_cast<QVBoxLayout*>(m_drivetrainArea->layout())->addWidget(m_drivetrainPanel);
    m_drivetrainJointCount = jointCount;
}

void ModelingPanelWidget::refreshDrivetrainPane()
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (m_drivetrainArea == nullptr) { return; }
    // 关节表变化→数值表重建（行集契约）。
    const std::size_t jointCount = ws != nullptr ? ws->design.joints.size() : 0;
    if (jointCount != m_drivetrainJointCount) { rebuildDrivetrainModel(); }
    if (m_drivetrainPanel != nullptr) { m_drivetrainPanel->setEnabled(m_writable); }

    // SourcedValue 基线回填（NotProvided＝nullopt 空编辑器——不虚构数值；
    // 权威值缓存供分组装配"净取权威"）。
    m_drivetrainBaseline.assign(jointCount * 6, std::nullopt);
    const std::optional<DrivetrainDesign>& dt =
        ws != nullptr ? ws->drivetrainObject : std::nullopt;
    for (std::size_t i = 0; i < jointCount; ++i) {
        const auto setRow = [&](std::size_t slot, const std::string& key,
                                const std::optional<core::SourcedValue<double>>& v) {
            if (v.has_value()) {
                const auto value = v->tryValue();
                m_drivetrainBaseline[slot] = value;
                m_drivetrainModel->setBaseline(key, value);
            } else {
                m_drivetrainModel->setBaseline(key, std::nullopt);
            }
        };
        setRow(i * 6 + 0, "dt-ratio-" + std::to_string(i),
               dt.has_value() && i < dt->ratioPerJoint.size()
                   ? std::optional<core::SourcedValue<double>>{dt->ratioPerJoint[i]}
                   : std::nullopt);
        if (dt.has_value() && i < dt->frictionPerJoint.size()) {
            setRow(i * 6 + 1, "dt-friction-viscous-" + std::to_string(i),
                   dt->frictionPerJoint[i].viscous);
            setRow(i * 6 + 2, "dt-friction-coulomb-" + std::to_string(i),
                   dt->frictionPerJoint[i].coulomb);
            setRow(i * 6 + 3, "dt-friction-bias-" + std::to_string(i),
                   dt->frictionPerJoint[i].bias);
        } else {
            m_drivetrainModel->setBaseline("dt-friction-viscous-" + std::to_string(i), std::nullopt);
            m_drivetrainModel->setBaseline("dt-friction-coulomb-" + std::to_string(i), std::nullopt);
            m_drivetrainModel->setBaseline("dt-friction-bias-" + std::to_string(i), std::nullopt);
        }
        if (dt.has_value() && i < dt->torqueLimitsPerJoint.size()) {
            setRow(i * 6 + 4, "dt-torque-rated-" + std::to_string(i),
                   dt->torqueLimitsPerJoint[i].rated);
            setRow(i * 6 + 5, "dt-torque-peak-" + std::to_string(i),
                   dt->torqueLimitsPerJoint[i].peak);
        } else {
            m_drivetrainModel->setBaseline("dt-torque-rated-" + std::to_string(i), std::nullopt);
            m_drivetrainModel->setBaseline("dt-torque-peak-" + std::to_string(i), std::nullopt);
        }
    }
}

void ModelingPanelWidget::applyDrivetrainEdits(const ui::ParamEditSet& editSet)
{
    m_threadGuard.assertOnUiThread();
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_writable) { return; }
    // 分组装配（UI-T55 同款：脏键取新值、未脏取权威基线）：按关节×族
    // （ratio/摩擦/力矩）聚合脏键，逐关节逐族调用域原语（提交序＝登记序；
    // 单族拒绝不阻断其余族——ERR-01 就地呈现逐族回执）。
    std::map<std::string, double> staged;
    for (const ui::ParamChange& change : editSet.changes) { staged[change.key] = change.newSi; }
    if (staged.empty()) { return; }
    const std::size_t jointCount = ws->design.joints.size();
    auto component = [&](const std::string& key) -> std::optional<double> {
        if (const auto it = staged.find(key); it != staged.end()) { return it->second; }
        const auto slot = drivetrainBaselineSlot(key);
        return slot < m_drivetrainBaseline.size() ? m_drivetrainBaseline[slot]
                                                  : std::optional<double>{};
    };
    bool anyAccepted = false;
    std::size_t applied = 0;
    // 族分量齐备性守卫：三元/对组装取"脏取新、净取权威"——权威侧
    // NotProvided（空基线）且用户未编辑＝分量缺失，诚实拒绝该族（不虚构
    // 零值——ERR-01；数值面空编辑器语义＝"未提供"非"零"）。
    auto complete = [&](std::initializer_list<std::string> keys,
                        const char* family, std::size_t i,
                        double* out) {
        std::size_t k = 0;
        for (const std::string& key : keys) {
            const auto v = component(key);
            if (!v.has_value()) {
                EditRejection rejection;
                rejection.codeToken = std::string(drivetrainEditErrorCodeToken(
                    DrivetrainEditErrorCode::ValueNotFinite));
                rejection.detail = "joints[" + std::to_string(i) + "]：" + family
                                 + " 分量缺失（未提供的基线不可隐式为零——请"
                                   "给出全部分量数值）";
                onEditRejected(rejection);
                return false;
            }
            out[k] = *v;
            ++k;
        }
        return true;
    };
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::string idx = std::to_string(i);
        // 族一：传动比。
        if (staged.count("dt-ratio-" + idx) != 0) {
            double ratio = 0.0;
            if (complete({"dt-ratio-" + idx}, "传动比", i, &ratio)) {
                const std::optional<DrivetrainEditError> err =
                    applyDrivetrainRatioEdit(*ws, i, ratio);
                if (!err.has_value()) { anyAccepted = true; ++applied; }
                else { rejectDrivetrainEdit(*err); }
            }
        }
        // 族二：摩擦三元（任一分量脏＝整组组装提交——域内整组校验）。
        if (staged.count("dt-friction-viscous-" + idx) != 0
            || staged.count("dt-friction-coulomb-" + idx) != 0
            || staged.count("dt-friction-bias-" + idx) != 0) {
            double vals[3] = {0.0, 0.0, 0.0};
            if (complete({"dt-friction-viscous-" + idx, "dt-friction-coulomb-" + idx,
                          "dt-friction-bias-" + idx},
                         "摩擦三元", i, vals)) {
                const std::optional<DrivetrainEditError> err = applyDrivetrainFrictionEdit(
                    *ws, i, vals[0], vals[1], vals[2]);
                if (!err.has_value()) { anyAccepted = true; ++applied; }
                else { rejectDrivetrainEdit(*err); }
            }
        }
        // 族三：力矩限值对。
        if (staged.count("dt-torque-rated-" + idx) != 0
            || staged.count("dt-torque-peak-" + idx) != 0) {
            double vals[2] = {0.0, 0.0};
            if (complete({"dt-torque-rated-" + idx, "dt-torque-peak-" + idx},
                         "力矩限值", i, vals)) {
                const std::optional<DrivetrainEditError> err = applyDrivetrainTorqueLimitEdit(
                    *ws, i, vals[0], vals[1]);
                if (!err.has_value()) { anyAccepted = true; ++applied; }
                else { rejectDrivetrainEdit(*err); }
            }
        }
    }
    if (anyAccepted) {
        onEditApplied("drivetrain（" + std::to_string(applied) + " 族）");
    }
    refreshPropertiesFromLastWorkingSet();
}

void ModelingPanelWidget::rejectDrivetrainEdit(const DrivetrainEditError& err)
{
    EditRejection rejection;
    rejection.codeToken = std::string(drivetrainEditErrorCodeToken(err.code));
    rejection.detail = err.detail;
    onEditRejected(rejection);
}

std::size_t ModelingPanelWidget::drivetrainBaselineSlot(const std::string& key) const
{
    // 键→基线槽位（dt-<族>-<序>；解析失败＝SIZE_MAX——调用方越界回落）。
    const std::string prefixes[6] = {"dt-ratio-", "dt-friction-viscous-",
                                     "dt-friction-coulomb-", "dt-friction-bias-",
                                     "dt-torque-rated-", "dt-torque-peak-"};
    for (std::size_t fam = 0; fam < 6; ++fam) {
        if (key.rfind(prefixes[fam], 0) == 0) {
            return std::stoull(key.substr(prefixes[fam].size())) * 6 + fam;
        }
    }
    return SIZE_MAX;
}

void ModelingPanelWidget::refreshEditPages()
{
    // 选择驱动分派（B.1 主通道）：Joint/Tool/Scene 三页均挂 ObjectId 树锚
    // ——树选中即切页；其余目标/空选中不动当前页（基座模式为显式入口，
    // UI-T54 语义保持——仅刷新关节区提示面）。
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    SelectedTarget::Kind kind = SelectedTarget::Kind::Joint;
    bool resolved = false;
    if (ws != nullptr && m_lastSelected.has_value()) {
        const auto target = resolveSelection(*ws, *m_lastSelected);
        if (target.has_value()) {
            kind = target->kind;
            resolved = true;
        }
    }
    if (resolved && kind == SelectedTarget::Kind::Tool) {
        refreshToolMountPane();
        refreshTcpPane();  // UI-T57——TCP 段随工具页同刷（combo/基线/使能）
        m_editStack->setCurrentWidget(m_toolArea);
        return;
    }
    if (resolved && kind == SelectedTarget::Kind::Scene) {
        refreshScenePosePane();
        m_editStack->setCurrentWidget(m_sceneArea);
        return;
    }
    // 位姿集/传动页（UI-T60——F-497 余项：均有 ObjectId 树锚，B.1 主通道）。
    if (resolved && kind == SelectedTarget::Kind::PoseSet) {
        refreshPoseSetPane();
        m_editStack->setCurrentWidget(m_poseSetArea);
        return;
    }
    if (resolved && kind == SelectedTarget::Kind::Drivetrain) {
        refreshDrivetrainPane();
        m_editStack->setCurrentWidget(m_drivetrainArea);
        return;
    }
    // 两页基线随每次刷新驱动（不依赖选中——模板会话两对象缺席时无法经
    // 树选中触发，页面在选中前也须可建行集/可用：新建条目/传动值编辑不
    // 需要既有对象。行集随关节表重建，基线推送幂等——与工具/场景页同款
    // "刷新即回权威"语义）。页面切换仍由选中驱动（上方两分支）。
    refreshPoseSetPane();
    refreshDrivetrainPane();
    // Joint／未解析：关节区照常刷新（空态提示在其内）；BaseInstall/
    // ModelRoot 目标维持当前页（基座模式为显式入口，UI-T54 语义保持）。
    refreshJointEditPane();
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

void ModelingPanelWidget::onJointEditTypeChanged(int index)
{
    m_threadGuard.assertOnUiThread();  // §3.4（UI 线程信号——防御面）
    // 回填屏蔽（QSignalBlocker）之外的用户切换才提交；无会话/无选中/只读
    // ＝不虚构提交（ERR-01；L-7 禁用为常驻门控，此为防御半区）。
    if (!m_writable || !m_jointEditTypeBox || index < 0) { return; }
    ModelingWorkingSet* ws = m_editTarget ? m_editTarget() : nullptr;
    if (ws == nullptr || !m_jointEditTarget.has_value()
        || *m_jointEditTarget >= ws->design.joints.size()) {
        return;
    }
    // 下标→域词表（addItem 序＝词表序——buildJointEditPane 同源）。
    static const JointType kTypes[] = {JointType::Revolute, JointType::Continuous,
                                       JointType::Prismatic, JointType::Fixed};
    if (static_cast<std::size_t>(index) >= std::size(kTypes)) { return; }
    const JointType newType = kTypes[index];
    // L-2 分流（域 Type 分支裁决——I-MDL-4 组合约束：TypeBoundsConflict
    // 拒绝就地呈现；L-7 同款 sink 面板）；接受后入重演队列（独立单条组）
    // ＋全面板刷新（限位行量纲随类型重建——refreshPropertiesFromLast
    // WorkingSet→换量纲重建编辑页模型）。
    const auto outcome =
        submitJointFieldEdit(*ws, *this, *m_jointEditTarget,
                             JointEditField::Type, JointEditValue{newType});
    if (outcome == EditSubmitOutcome::Applied) {
        PendingEdit e;
        e.jointIndex = *m_jointEditTarget;
        e.field = JointEditField::Type;
        e.value = JointEditValue{newType};
        e.groupId = 0;  // 独立单条（单编辑动作——不与分组提交并组）
        m_refresh.recordPending(e);
        refreshPropertiesFromLastWorkingSet();
    } else {
        // 域拒绝（如 Continuous↔限位冲突）：下拉回退当前权威类型——
        // 拒绝详情已由 sink 面板就地呈现（L-2 同款），此处恢复呈现一致。
        QSignalBlocker blocker(*m_jointEditTypeBox);
        const int authoritative = m_jointEditTypeBox->findText(
            QString::fromStdString(std::string(
                jointTypeToken(ws->design.joints[*m_jointEditTarget].type))));
        m_jointEditTypeBox->setCurrentIndex(authoritative < 0 ? -1
                                                             : authoritative);
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
