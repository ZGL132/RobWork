/**
 * @file   KinematicsPanelWidget.cpp
 * @brief  kinematics 域主面板与求解配置高级面板的实现（头文件全部落点）。
 *
 * 设计依据：KinematicsPanelWidget.hpp 文件头；本 TU 零业务判定——按钮
 * 转接零 Qt 编排函数、表格渲染零 Qt 行集（acceptance 2 的 widget 半区）。
 */

#include "KinematicsPanelWidget.hpp"

#include <QFormLayout>
#include <QBuffer>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <cmath>
#include <limits>
#include <sdurws/ird/runtime/BaseWorldTransform.hpp> // 世界变换唯一权威，兼容独立模式。
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <sdurws/ird/core/Units.hpp>               // UnitToken::find（字段量纲锚）
#include <sdurws/ird/runtime/CanonicalModel.hpp>  // 模型值真值（工作域/统计呈现）
#include <sdurws/ird/runtime/Snapshot.hpp>         // REUSE-T17：IRuntimeModelView 完整类型（锚 upcast）
#include <sdurws/ird/ui/FormEditCommon.hpp>        // 表单公共件（UI-T08 对端——参数表面板）
#include <sdurws/ird/ui/UiText.hpp>                // 文案键解析（UI-T25——迁移标记标签挂键；
                                                   // 构造期命令标题 resolver 尚未注入，此处直读
                                                   // UiText 静态表与 requirements 面板先例同款）
#include "KinPanelRotation.hpp"                     // rotationFromRpySi（F-919：RPY→旋转，独立冒烟可链接）
#include "KinPanelCommandCatalog.hpp"              // 命令目录（按钮词形与只读门控）
#include "KinPanelUnits.hpp"                       // resolveKinDisplayToSi/resolveKinAngleDisplayToSi（提交面逆换算——A07/KIN-R2）

namespace sdurws {
namespace ird {
namespace kinematics {
namespace {

/// 表格便捷构建：只读行表（列头给定——零编辑语义；widget 层唯一重复处）。
QTableWidget* makeTable(const QStringList& headers, QWidget* parent)
{
    auto* table = new QTableWidget(0, headers.size(), parent);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 呈现零编辑
    table->verticalHeader()->setVisible(false);
    return table;
}

/// 填充"数值文本"单列表（状态行/统计类——零数值处理）。
void fillSingleColumn(QTableWidget* table, const QStringList& texts)
{
    table->setRowCount(static_cast<int>(texts.size()));
    for (int i = 0; i < texts.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(texts[i]));
    }
}

// REUSE-T17：原 FrozenRuntimeView（提交期值复制视图）整类删除——提交冻结
// 改为捕获 runtimeModelView() 锚（RuntimeSnapshot 为真实 owner 的
// shared_ptr）：换绑/宿主释放不影响在途任务，且同一锚同时承载值面与编译
// 产物面（workCell/makeState/nameResolver——Device/State FK 替换的准入
// 前提）。runtime::IRuntimeModelView 与 kinematics::IKinRuntimeView 是无
// 继承关系的两个接口族，故保留一个**纯转发桥**（不复制任何模型值——与
// 旧值复制的本质区别），提交任务视图经桥消费锚。
/**
 * @brief 提交期锚视图——已编译模型锚的 kinematics 面纯转发适配。
 *
 * 只做成员转发：model()/worldToBase() 委托锚的同名成员（语义逐字同
 * runtime §8.3），runtimeModelView() 返回同源锚。构造后不可变、并发只读
 * 安全；锚的 shared_ptr 保活即本视图全部生命周期依据。前置：锚非空
 * （提交拍已检查——空锚走两分错误轨，不构造本视图）。
 */
class AnchorRuntimeView final : public IKinRuntimeView {
public:
    explicit AnchorRuntimeView(
        std::shared_ptr<const runtime::IRuntimeModelView> anchor)
        : m_anchor(std::move(anchor))
    {
    }

    const runtime::CanonicalModel& model() const override
    {
        return m_anchor->model();
    }

    rw::math::Transform3D<double> worldToBase() const override
    {
        return m_anchor->worldToBase();
    }

    std::shared_ptr<const runtime::IRuntimeModelView> runtimeModelView() const override
    {
        return m_anchor;
    }

private:
    std::shared_ptr<const runtime::IRuntimeModelView> m_anchor;
};


/**
 * @brief 比较两个基座系 SI 位姿是否代表同一目标。
 *
 * 编辑器在显示单位切换时会经历一次字符串重投影，浮点格式化可能带来
 * 最后几位的舍入差异；这里比较位置（m）和旋转矩阵逐项（无量纲），以
 * 1e-12 的绝对容差吸收该显示层误差，同时仍能识别用户实际改变的目标。
 */
bool equivalentTargetPose(const rw::math::Transform3D<double>& lhs,
                          const rw::math::Transform3D<double>& rhs)
{
    constexpr double kTargetComparisonTolerance = 1e-12;
    for (std::size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(lhs.P()[i]) || !std::isfinite(rhs.P()[i])
            || std::fabs(lhs.P()[i] - rhs.P()[i])
                   > kTargetComparisonTolerance) {
            return false;
        }
    }
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            if (!std::isfinite(lhs.R()(row, column))
                || !std::isfinite(rhs.R()(row, column))
                || std::fabs(lhs.R()(row, column) - rhs.R()(row, column))
                       > kTargetComparisonTolerance) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

// =====================================================================
// KinematicsPanelWidget——四面板合一主面板
// =====================================================================

KinematicsPanelWidget::KinematicsPanelWidget(KinPanelServices services,
                                             KinModuleSessionState& session,
                                             QWidget* parent)
    : QWidget(parent)
    , m_services(std::move(services))
    , m_session(session)
{
    // 四面板 Tab 容器（§9.8 面板表行 1~4 的落位载体——页序＝表行序，
    // 确定性登记序）。
    auto* layout = new QVBoxLayout(this);

    // 自持导航迁移状态标记已随 UI-T36 退役（原 WP-15-T18 迁移期双形态
    // 并存的标记标签与 UiText 键族⑨ kinematics 键同步删行——业务主导航
    // 在共享项目树的口径不变，本面板页签呈现零变化）。

    m_tabs = new QTabWidget(this);
    // WP-15-T20 F03/UX-09：QTabWidget会采纳所有页面的最大最小尺寸，
    // 即便当前页很短，结果长页也曾把整个宿主撑出屏幕。滚动容器隔离内容
    // 的最低尺寸，使小窗口可操作到最后一行；Qt父子链负责释放内容和容器。
    /**
     * @brief 将已有页面装入滚动容器，仅UI构造期调用。
     * @param content [in,out] 非空页控件，Qt所有权转交给新容器。
     * @return 由m_tabs最终持有的容器，不持有业务状态。
     */
    const auto scrollable = [this](QWidget* content) {
        auto* scroll = new QScrollArea(m_tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(content);
        return scroll;
    };
    m_tabs->addTab(scrollable(buildPosePane()), tr("位姿指标"));
    m_tabs->addTab(scrollable(buildTaskPane()), tr("任务点验证"));
    m_tabs->addTab(scrollable(buildCoveragePane()), tr("区域覆盖"));
    m_tabs->addTab(scrollable(buildResultsPane()), tr("结果与可视化"));
    layout->addWidget(m_tabs);

    // 状态行（非模态反馈——UX-03/07：一切反馈就地，零模态对话框）。
    m_statusLine = new QLabel(tr("就绪"), this);
    m_statusLine->setWordWrap(true); // 长诊断换行，不能再由单行文字撑宽宿主。
    m_statusLine->setObjectName(QStringLiteral("ird_kin_status_line"));  // 验证定位锚（宿主遍历）
    layout->addWidget(m_statusLine);

    // 初刷（事件驱动出口的首调——现取重投影）。
    refreshAll();
}

void KinematicsPanelWidget::setCommandTitleResolver(CommandTitleResolver resolver)
{
    m_titleResolver = std::move(resolver);
}

void KinematicsPanelWidget::setCommandAvailability(CommandAvailabilityFn availability)
{
    m_commandAvailability = std::move(availability);
    refreshAll();
}

QString KinematicsPanelWidget::lastStatusText() const
{
    return m_statusLine != nullptr ? m_statusLine->text() : QString();
}

void KinematicsPanelWidget::showStatusText(const QString& text)
{
    if (m_statusLine != nullptr) {
        m_statusLine->setText(text);
        // 状态文字与颜色同步表达反馈等级：过期/失败需要用户重新处理，
        // 求解中提示正在执行，普通完成态使用稳定的成功色；不改变原有
        // 文案和测试定位，只把状态差异变成可扫读的视觉信号。
        if (text.contains(tr("过期")) || text.contains(tr("失败"))) {
            m_statusLine->setStyleSheet(QStringLiteral("color: #b42318;"));
        } else if (text.contains(tr("求解中"))) {
            m_statusLine->setStyleSheet(QStringLiteral("color: #9a6700;"));
        } else {
            m_statusLine->setStyleSheet(QStringLiteral("color: #1f6f43;"));
        }
    }
}

void KinematicsPanelWidget::onTargetEditorChanged()
{
    // 没有已写入会话的结果时，编辑目标只是草稿变化，不显示“过期”，
    // 避免用户首次输入就看到与结果无关的警告；异步在途结果由完成回调
    // 另行用提交快照核对。
    if (m_session.lastSessionSolutionSet == nullptr) {
        return;
    }

    bool expired = true;
    if (m_lastSolvedTarget.has_value()) {
        // 静默解析当前编辑值。输入不完整时保持过期态，但不在每次击键时
        // 覆盖状态行，让用户能连续完成一个数值的输入。
        const auto currentTarget = readTargetPoseFromEditors(QString());
        expired = !currentTarget.has_value()
                  || !equivalentTargetPose(*currentTarget,
                                           *m_lastSolvedTarget);
    }
    if (m_solveResultExpired != expired) {
        m_solveResultExpired = expired;
        refreshAll();
    }
}

QWidget* KinematicsPanelWidget::buildPosePane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QHBoxLayout(pane);

    // ---- 左半：参考系标识行＋位姿指标行表＋关节表（KIN-12 显示投影）。
    // ★ KIN-R2 任务4（审查 §4 坐标系标识）：标识行由 refreshPosePane 随
    // TCP 解析现投（工具身份＋双参考系口径说明——用户不必猜位姿值在哪个
    // 系下表达）。
    auto* left = new QVBoxLayout();
    m_frameLabel = new QLabel(pane);
    left->addWidget(m_frameLabel);
    m_poseTable = makeTable({tr("指标"), tr("值")}, pane);
    left->addWidget(m_poseTable);
    m_jointTable = makeTable({tr("关节"), tr("当前构型")}, pane);
    left->addWidget(m_jointTable);
    layout->addLayout(left);

    // ---- 右半：目标编辑＋Solve＋候选检查器。
    auto* right = new QVBoxLayout();
    auto* form = new QFormLayout();
    // 目标位置编辑（**显示制式**数值——行标签随当前制式标注单位；提交时
    // 经 resolveKinDisplayToSi 逆换算回 SI 真值，会话/求解器只见 SI）。
    // 行标签显式创建并持有（QFormLayout 内建标签拿不到可更新句柄——
    // L-K8 切换制式后标签必须随制式重建；审查 A07 缺陷面①：修复前标签
    // 为构造期硬编码 "（m）"，切到 mm 后标签失实引导错误输入解释）。
    m_targetX = new QLineEdit(pane);
    m_targetY = new QLineEdit(pane);
    m_targetZ = new QLineEdit(pane);
    m_targetXLabel = new QLabel(pane);
    m_targetYLabel = new QLabel(pane);
    m_targetZLabel = new QLabel(pane);
    // objectName＝稳定定位锚（gui_test 按名取控件——findChildren 顺序
    // 依赖创建序，六字段扩展后按名取值为确定性语义；KIN-R2 任务4）。
    m_targetX->setObjectName(QStringLiteral("target-x"));
    m_targetY->setObjectName(QStringLiteral("target-y"));
    m_targetZ->setObjectName(QStringLiteral("target-z"));
    form->addRow(m_targetXLabel, m_targetX);
    form->addRow(m_targetYLabel, m_targetY);
    form->addRow(m_targetZLabel, m_targetZ);
    // ★ KIN-R2 任务4（审查 §4"目标姿态固定为单位旋转"修复面）：目标
    // 姿态以 RPY（roll-pitch-yaw，Z-Y-X 欧角约定随 rw::math::RPY 语义）
    // 三字段编辑——**显示制式**角度值（deg/rad 随制式），提交经
    // resolveKinAngleDisplayToSi 逆换算回 SI rad 后经 rw::math::RPY
    // 构造完整旋转（空输入＝缺省 0，即历史单位旋转行为——向后兼容）。
    m_targetRoll = new QLineEdit(pane);
    m_targetPitch = new QLineEdit(pane);
    m_targetYaw = new QLineEdit(pane);
    m_targetRollLabel = new QLabel(pane);
    m_targetPitchLabel = new QLabel(pane);
    m_targetYawLabel = new QLabel(pane);
    m_targetRoll->setObjectName(QStringLiteral("target-roll"));
    m_targetPitch->setObjectName(QStringLiteral("target-pitch"));
    m_targetYaw->setObjectName(QStringLiteral("target-yaw"));
    // 用户编辑目标时立即重进结果状态；单位重投影和 TCP 回填会在写入时
    // 屏蔽信号，避免同一 SI 目标被误判为过期（KIN-02/NFR-REL-03）。
    for (QLineEdit* editor : {m_targetX, m_targetY, m_targetZ,
                              m_targetRoll, m_targetPitch, m_targetYaw}) {
        connect(editor, &QLineEdit::textChanged, this,
                &KinematicsPanelWidget::onTargetEditorChanged);
    }
    form->addRow(m_targetRollLabel, m_targetRoll);
    form->addRow(m_targetPitchLabel, m_targetPitch);
    form->addRow(m_targetYawLabel, m_targetYaw);
    // 首版标注（按当前会话制式——构造期 displayUnits 尚为缺省 SI 直显，
    // setDisplayUnits 注入后经 refreshAll 重建；占位符也在该函数统一
    // 重建——审查 O1 必填提示随行标签同源）。
    refreshTargetUnitPresentation();
    // ---- 显示单位切换入口（审查 G5 修复——L-K8 对用户可达；WP-15-T20
    // 第三批）：长度/角度两制式下拉（KIN-12 R1 冻结子集 m/cm/mm＋rad/
    // deg）；切换经 setDisplayUnits 唯一入口全面板重投影（零修订/零重算
    // /结果不动——V-17）。构造期按会话缺省（SI 直显）选中。
    auto* unitRow = new QHBoxLayout();
    m_lengthUnitBox = new QComboBox(pane);
    m_lengthUnitBox->addItem(tr("m"));
    m_lengthUnitBox->addItem(tr("cm"));
    m_lengthUnitBox->addItem(tr("mm"));
    m_angleUnitBox = new QComboBox(pane);
    m_angleUnitBox->addItem(tr("rad"));
    m_angleUnitBox->addItem(tr("deg"));
    unitRow->addWidget(new QLabel(tr("长度单位"), pane));
    unitRow->addWidget(m_lengthUnitBox);
    unitRow->addWidget(new QLabel(tr("角度单位"), pane));
    unitRow->addWidget(m_angleUnitBox);
    unitRow->addStretch(1);
    connect(m_lengthUnitBox, &QComboBox::currentIndexChanged, this,
            &KinematicsPanelWidget::onDisplayUnitChanged);
    connect(m_angleUnitBox, &QComboBox::currentIndexChanged, this,
            &KinematicsPanelWidget::onDisplayUnitChanged);
    layout->addLayout(form);
    layout->addLayout(unitRow);  // 单位切换行在目标表单之后（就近原则）

    m_solveButton = new QPushButton(tr("求解 IK"), pane);
    m_solveButton->setToolTip(tr("需要模型视图和逆运动学求解服务"));
    connect(m_solveButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSolveClicked);
    m_solveStatusLabel = new QLabel(tr("求解状态：未求解"), pane);
    m_solveStatusLabel->setObjectName(QStringLiteral("kin-solve-status"));
    m_solveStatusLabel->setWordWrap(true);
    right->addWidget(m_solveStatusLabel);
    right->addWidget(m_solveButton);
    // ---- TCP 位姿回填入口（审查 G4 修复——L-K5 产品闭环；WP-15-T20
    // 第三批）：把会话姿态对应的当前 TCP 位姿回填进目标六字段（仅呈现
    // 默认值——求解身份不含会话，D-KIN-4）。可用性＝FK 执行面＋会话姿态
    // 缝在场（门控随 refreshAll 现取——与"复位 Home"两级门控同则）。
    m_backfillTcpButton = new QPushButton(tr("以当前 TCP 为目标"), pane);
    m_backfillTcpButton->setToolTip(
        tr("需要模型视图、FK 评估器与会话姿态（把当前 TCP 位姿填入目标编辑器）"));
    connect(m_backfillTcpButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onBackfillTcpClicked);
    right->addWidget(m_backfillTcpButton);

    m_inspectorTable = makeTable({tr("检查器"), tr("值")}, pane);
    right->addWidget(m_inspectorTable);
    layout->addLayout(right);
    return pane;
}

QWidget* KinematicsPanelWidget::buildTaskPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    // 任务点消费视图（只读——真值归 requirements，acceptance 4；本表
    // 无编辑触发、无写回路径）。
    m_taskPointTable = makeTable({tr("任务点"), tr("启用"), tr("结果"),
                                  tr("必验覆盖")}, pane);
    layout->addWidget(m_taskPointTable);
    m_batchButton = new QPushButton(tr("批量验证（后台）"), pane);
    connect(m_batchButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onBatchClicked);
    layout->addWidget(m_batchButton);
    return pane;
}

QWidget* KinematicsPanelWidget::buildCoveragePane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    m_coverageTable = makeTable({tr("覆盖轴"), tr("计数比"), tr("标识")}, pane);
    layout->addWidget(m_coverageTable);
    m_coverageRunButton = new QPushButton(tr("运行覆盖评估（后台）"), pane);
    connect(m_coverageRunButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onCoverageRunClicked);
    layout->addWidget(m_coverageRunButton);
    return pane;
}

QWidget* KinematicsPanelWidget::buildResultsPane()
{
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);

    // 筛选（仅可用解——域规范谓词语义的呈现开关；排序权威在解集视图）。
    m_usableOnly = new QCheckBox(tr("仅可用解（碰撞证据完备且无碰撞）"), pane);
    connect(m_usableOnly, &QCheckBox::toggled, this,
            [this](bool) { refreshResultsPane(); });  // 筛选切换→现取重投影
    layout->addWidget(m_usableOnly);

    m_solutionTable = makeTable({tr("rank"), tr("构型签名"), tr("q"),
                                 tr("最小裕量"), tr("条件数"), tr("残差"),
                                 tr("碰撞")}, pane);
    // L-K1 选中联动：解表选中→检查器；L-K4 双击→会话姿态回写（零修订，
    // KIN-06——双击只改会话姿态）。
    connect(m_solutionTable, &QTableWidget::itemSelectionChanged, this,
            &KinematicsPanelWidget::onSolutionSelectionChanged);
    connect(m_solutionTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem* item) {
                if (item != nullptr) {
                    onSolutionDoubleClicked(item->row(), item->column());
                }
            });
    layout->addWidget(m_solutionTable);

    m_statTable = makeTable({tr("统计"), tr("值")}, pane);
    layout->addWidget(m_statTable);

    // ---- 探索式采样组（★ KIN-R2 任务3/KIN-09——采样模式编辑入口）：
    // 模式三值（区域盒 IK 采样/关节随机 FK 采样/关节网格 FK 采样——域
    // WorkspaceSamplingMode 词表序）；参数字段随模式互斥启用
    // （refreshWsParamVisibility）；运行/导出/回写全部经编排函数零内联
    // 逻辑。字段 objectName＝"ws-*"（gui_test 稳定定位锚）。
    m_wsModeBox = new QComboBox(pane);
    m_wsModeBox->setObjectName(QStringLiteral("ws-mode-box"));
    m_wsModeBox->addItem(tr("区域盒（IK 采样）"));
    m_wsModeBox->addItem(tr("关节随机（FK 采样）"));
    m_wsModeBox->addItem(tr("关节网格（FK 采样）"));
    connect(m_wsModeBox, &QComboBox::currentIndexChanged, this,
            &KinematicsPanelWidget::onWsModeChanged);
    m_wsPositionMethodBox = new QComboBox(pane);
    m_wsPositionMethodBox->setObjectName(QStringLiteral("ws-position-method"));
    m_wsPositionMethodBox->addItem(tr("随机均匀"));
    m_wsPositionMethodBox->addItem(tr("体心网格"));

    auto makeWsEdit = [pane](const QString& objectName,
                             const QString& text) {
        QLineEdit* edit = new QLineEdit(text, pane);
        edit->setObjectName(objectName);
        return edit;
    };
    m_wsRegionCX = makeWsEdit(QStringLiteral("ws-region-cx"), QStringLiteral("0"));
    m_wsRegionCY = makeWsEdit(QStringLiteral("ws-region-cy"), QStringLiteral("0"));
    m_wsRegionCZ = makeWsEdit(QStringLiteral("ws-region-cz"), QStringLiteral("0"));
    m_wsRegionSX = makeWsEdit(QStringLiteral("ws-region-sx"), QStringLiteral("0.2"));
    m_wsRegionSY = makeWsEdit(QStringLiteral("ws-region-sy"), QStringLiteral("0.2"));
    m_wsRegionSZ = makeWsEdit(QStringLiteral("ws-region-sz"), QStringLiteral("0.2"));
    m_wsRegionRandom = makeWsEdit(QStringLiteral("ws-region-random"), QStringLiteral("64"));
    m_wsRegionGridX = makeWsEdit(QStringLiteral("ws-region-grid-x"), QStringLiteral("2"));
    m_wsRegionGridY = makeWsEdit(QStringLiteral("ws-region-grid-y"), QStringLiteral("2"));
    m_wsRegionGridZ = makeWsEdit(QStringLiteral("ws-region-grid-z"), QStringLiteral("2"));
    m_wsJointRandom = makeWsEdit(QStringLiteral("ws-joint-random"), QStringLiteral("64"));
    m_wsJointGrid = makeWsEdit(QStringLiteral("ws-joint-grid"), QStringLiteral("2"));
    m_wsSampleIndex = makeWsEdit(QStringLiteral("ws-sample-index"), QString());

    auto* wsForm = new QFormLayout();
    wsForm->addRow(tr("采样模式"), m_wsModeBox);
    wsForm->addRow(tr("区域中心 X（长度制式单位，基座系）"),
                   m_wsRegionCX);
    wsForm->addRow(tr("区域中心 Y（长度制式单位，基座系）"),
                   m_wsRegionCY);
    wsForm->addRow(tr("区域中心 Z（长度制式单位，基座系）"),
                   m_wsRegionCZ);
    wsForm->addRow(tr("区域边长 X（长度制式单位，基座系）"),
                   m_wsRegionSX);
    wsForm->addRow(tr("区域边长 Y（长度制式单位，基座系）"),
                   m_wsRegionSY);
    wsForm->addRow(tr("区域边长 Z（长度制式单位，基座系）"),
                   m_wsRegionSZ);
    wsForm->addRow(tr("区域位置采样"), m_wsPositionMethodBox);
    wsForm->addRow(tr("区域随机样本数"), m_wsRegionRandom);
    wsForm->addRow(tr("区域网格计数 X/Y/Z"), m_wsRegionGridX);
    wsForm->addRow(QString(), m_wsRegionGridY);
    wsForm->addRow(QString(), m_wsRegionGridZ);
    wsForm->addRow(tr("关节随机样本数"), m_wsJointRandom);
    wsForm->addRow(tr("关节网格每轴取点数"), m_wsJointGrid);
    layout->addLayout(wsForm);

    auto* wsButtons = new QHBoxLayout();
    m_wsRunButton = new QPushButton(tr("运行采样"), pane);
    m_wsRunButton->setToolTip(tr("探索式采样：后台运行，可取消；取消后保留已计算样本"));
    m_wsCancelButton = new QPushButton(tr("取消采样"), pane);
    m_wsCancelButton->setObjectName(QStringLiteral("ws-cancel"));
    m_wsCancelButton->setEnabled(false);
    m_wsProgressBar = new QProgressBar(pane);
    m_wsProgressBar->setObjectName(QStringLiteral("ws-progress"));
    m_wsProgressBar->setRange(0, 100);
    m_wsProgressBar->setValue(0);
    m_wsProgressTimer = new QTimer(this);
    m_wsProgressTimer->setInterval(100); // 10Hz呈现足够可读，不向UI排队逐样本事件。
    connect(m_wsProgressTimer, &QTimer::timeout, this, [this] {
        if (m_wsProgress) { m_wsProgressBar->setValue(m_wsProgress->percent.load()); }
    });
    connect(m_wsCancelButton, &QPushButton::clicked, this, [this] {
        if (m_wsProgress) {
            m_wsProgress->cancel.store(true);
            m_wsCancelButton->setEnabled(false);
            showStatusText(tr("正在取消采样，请等待当前计算检查点……"));
        }
    });
    m_wsExportCsvButton = new QPushButton(tr("导出点云 CSV"), pane);
    m_wsWritebackButton = new QPushButton(tr("回写样本构型到会话"), pane);
    connect(m_wsRunButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onWsRunClicked);
    connect(m_wsExportCsvButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onWsExportCsvClicked);
    connect(m_wsWritebackButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onWsWritebackClicked);
    wsButtons->addWidget(m_wsRunButton);
    wsButtons->addWidget(m_wsCancelButton);
    wsButtons->addWidget(m_wsExportCsvButton);
    wsButtons->addWidget(m_wsSampleIndex);
    wsButtons->addWidget(m_wsWritebackButton);
    layout->addLayout(wsButtons);
    layout->addWidget(m_wsProgressBar);

    // 采样摘要表（lastWorkspaceRun 现投——模式 token/状态计数/取消标记；
    // F-905 感知半区：模式 token 行即"点从哪来"的强制溯源标注）。
    m_wsSummaryTable = makeTable({tr("采样摘要"), tr("值")}, pane);
    layout->addWidget(m_wsSummaryTable);

    // ---- 投影视图（★ KIN-R2 任务6——"三维或投影视图＋PNG 导出"最小
    // 可用入口）：面板内 2D 正交投影散点画布（XY/XZ/YZ 舍轴语义随域
    // projectWorkspacePoints；坐标恒基座系 {B}，单位 m）。三维视图截图
    // PNG 权威归框架 RWStudioView3D（附录 A"等价承接"——勘察口径随卡
    // §14.6 v0.30 登记）；本画布为零框架依赖的投影视图（评审/取证的
    // 确定性出图面）。objectName＝"ws-projection-*"（gui_test 定位锚）。
    m_wsProjectionPlaneBox = new QComboBox(pane);
    m_wsProjectionPlaneBox->setObjectName(QStringLiteral("ws-projection-plane"));
    m_wsProjectionPlaneBox->addItem(tr("XY 投影（俯视）"));
    m_wsProjectionPlaneBox->addItem(tr("XZ 投影（正视）"));
    m_wsProjectionPlaneBox->addItem(tr("YZ 投影（侧视）"));
    connect(m_wsProjectionPlaneBox, &QComboBox::currentIndexChanged, this,
            &KinematicsPanelWidget::onWsProjectionPlaneChanged);
    auto* projectionRow = new QHBoxLayout();
    projectionRow->addWidget(new QLabel(tr("投影面"), pane));
    projectionRow->addWidget(m_wsProjectionPlaneBox);
    projectionRow->addStretch(1);
    m_wsExportPngButton = new QPushButton(tr("导出投影视图 PNG"), pane);
    connect(m_wsExportPngButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onWsExportPngClicked);
    projectionRow->addWidget(m_wsExportPngButton);
    layout->addLayout(projectionRow);
    m_wsCanvasLabel = new QLabel(pane);
    m_wsCanvasLabel->setObjectName(QStringLiteral("ws-projection-canvas"));
    m_wsCanvasLabel->installEventFilter(this);
    m_wsCanvasLabel->setToolTip(tr("单击选择样本，双击将有解样本的构型回写会话"));
    connect(m_wsSampleIndex, &QLineEdit::textChanged, this, [this] {
        refreshWsProjectionCanvas();
        refreshVisualization();
    });
    m_wsCanvasLabel->setMinimumSize(480, 360);
    m_wsCanvasLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_wsCanvasLabel);

    // 命令行（导出/复位/设默认/复算——全部经编排函数，零内联逻辑）。
    auto* buttons = new QHBoxLayout();
    m_exportJsonButton = new QPushButton(tr("导出 JSON"), pane);
    m_exportCsvButton = new QPushButton(tr("导出 CSV"), pane);
    m_resetHomeButton = new QPushButton(tr("复位 Home"), pane);
    m_setTcpButton = new QPushButton(tr("设默认 TCP"), pane);
    m_setDeviceButton = new QPushButton(tr("设默认设备"), pane);
    // ★ KIN-P4：复算按钮（审查 §4——"原任务定义与当前快照重绑定"的
    // 产品入口；会话级零修订——AT-04）。
    m_recomputeButton = new QPushButton(tr("复算（当前快照）"), pane);
    m_recomputeButton->setToolTip(
        tr("按原任务定义（seed/参考构型自原结果身份携带）在当前快照重跑求解"));
    connect(m_exportJsonButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onExportJsonClicked);
    connect(m_exportCsvButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onExportCsvClicked);
    connect(m_resetHomeButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onResetHomeClicked);
    connect(m_setTcpButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSetDefaultTcpClicked);
    connect(m_setDeviceButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onSetDefaultDeviceClicked);
    connect(m_recomputeButton, &QPushButton::clicked, this,
            &KinematicsPanelWidget::onRecomputeClicked);
    for (QPushButton* b : {m_exportJsonButton, m_exportCsvButton, m_resetHomeButton,
                           m_setTcpButton, m_setDeviceButton, m_recomputeButton}) {
        buttons->addWidget(b);
    }
    layout->addLayout(buttons);
    return pane;
}

void KinematicsPanelWidget::renderRows(QTableWidget* table,
                                       const std::vector<KinNamedValueRow>& rows)
{
    if (table == nullptr) {
        return;
    }
    table->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        table->setItem(static_cast<int>(i), 0,
                       new QTableWidgetItem(QString::fromStdString(rows[i].label)));
        table->setItem(static_cast<int>(i), 1,
                       new QTableWidgetItem(QString::fromStdString(rows[i].displayText)));
    }
}

void KinematicsPanelWidget::renderSolutions(QTableWidget* table,
                                            const std::vector<KinSolutionRow>& rows)
{
    if (table == nullptr) {
        return;
    }
    table->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const KinSolutionRow& r = rows[i];
        const auto rank = static_cast<int>(i);
        table->setItem(rank, 0, new QTableWidgetItem(QString::number(
                                     static_cast<qlonglong>(r.rank))));
        table->setItem(rank, 1, new QTableWidgetItem(
                                     QString::fromStdString(r.signature)));
        table->setItem(rank, 2, new QTableWidgetItem(
                                     QString::fromStdString(r.qText)));
        table->setItem(rank, 3, new QTableWidgetItem(
                                     QString::fromStdString(r.minJointMarginText)));
        table->setItem(rank, 4, new QTableWidgetItem(
                                     QString::fromStdString(r.conditionNumberText)));
        table->setItem(rank, 5, new QTableWidgetItem(
                                     QString::fromStdString(r.positionResidualText)));
        table->setItem(rank, 6, new QTableWidgetItem(
                                     QString::fromStdString(r.collisionToken)));
    }
}

void KinematicsPanelWidget::refreshAll()
{
    // 目标输入行标注随制式重建（KIN-P1 A07——L-K8 切换经 setDisplayUnits
    // 进入本函数；标签/占位是 displayUnits 的呈现函数，零重算零修订）。
    refreshTargetUnitPresentation();
    refreshPosePane();
    refreshResultsPane();
    refreshTaskArea();
    // 覆盖页：结果行经装配门面 applyCoverageResult→showCoverageRows 投影
    // （审查 G1 修复落位——锚核对＋聚合→行翻译在装配面，本处仅按钮态）。
    const bool writable = m_session.writable;
    const auto commandEnabled = [this](const std::string& id, bool local) {
        return local && (!m_commandAvailability || m_commandAvailability(id).enabled);
    };
    m_batchButton->setEnabled(commandEnabled("kinematics.validate-task-points",
                                             writable && static_cast<bool>(m_services.backgroundSubmit)));
    m_coverageRunButton->setEnabled(commandEnabled("kinematics.evaluate-coverage",
                                                   writable && static_cast<bool>(m_services.backgroundSubmit)));
    m_setTcpButton->setEnabled(commandEnabled("kinematics.set-default-tcp",
                                              kinCommandEnabledInSession("kinematics.set-default-tcp", writable)
                                              && m_services.commandHandler != nullptr));
    m_setDeviceButton->setEnabled(commandEnabled("kinematics.set-default-device",
                                                 kinCommandEnabledInSession("kinematics.set-default-device", writable)
                                                 && m_services.commandHandler != nullptr));
    // 复位 Home（KIN-P3/审查 A09；★ KIN-R2 任务5——正式宿主装配的可用性
    // 语义收敛）：按钮可用性＝具名 Home 来源缝已装配**且现取查询确实
    // 解析出 Home 定义**。有定义才可用、无定义显式禁用（诚实降级——
    // 修复前"缝在场即点亮"，模型未定义 Home 时按钮可达但点击必得"未
    // 定义"提示，可用性暗示与实际能力不符）；缝未装配同样禁用。三条
    // 门控共同保证"绝不出现以当前姿态充当 Home 的可达路径"（审查 A09
    // 红线）。现取查询零缓存（KinHomePoseProvider 契约——纯查询幂等，
    // UI 线程现调；项目态变化经 refreshAll 重进门控）。缝在场但查询
    // 未定义 Home 的明确提示保留在点击路径（resetSessionHomeFromNamed-
    // Source 拒绝态③——直达槽防御面语义不变）。只读会话不禁用
    // （reset-session-pose＝readOnlyAllowed，会话级命令——目录表权威）。
    m_resetHomeButton->setEnabled(
        static_cast<bool>(m_services.homePose)
        && m_services.homePose().has_value());
    const bool exportReady = static_cast<bool>(m_services.exportWriter)
                             && m_session.lastSessionSolutionSetView != nullptr;
    m_exportJsonButton->setEnabled(commandEnabled("kinematics.export-results", exportReady));
    m_exportCsvButton->setEnabled(commandEnabled("kinematics.export-results", exportReady));
    // 复算按钮（KIN-P4）：可用＝原解集在场（原任务定义来源）＋模型视图
    // 与求解器在场（复算执行面）。复算为会话级零修订流（AT-04）——只读
    // 会话不禁用（与 solve-ik 同 readOnlyAllowed 语义）。
    m_recomputeButton->setEnabled(
        m_session.lastSessionSolutionSet != nullptr
        && m_services.ikSolver != nullptr && m_services.modelView != nullptr);
    m_solveButton->setEnabled(commandEnabled("kinematics.solve-ik",
                                             m_services.ikSolver != nullptr
                                             && m_services.modelView != nullptr
                                             && !m_solveInFlight));
    if (m_solveStatusLabel != nullptr) {
        if (m_solveInFlight) {
            m_solveStatusLabel->setText(tr("求解状态：执行中，输入和会话切换结果不会自动合并"));
        } else if (m_solveResultExpired) {
            m_solveStatusLabel->setText(tr("求解状态：结果已过期，请重新求解"));
        } else if (m_session.lastSessionSolutionSet != nullptr) {
            m_solveStatusLabel->setText(tr("求解状态：已有当前会话结果"));
        } else {
            m_solveStatusLabel->setText(tr("求解状态：未求解"));
        }
    }
    // ---- 探索式采样组门控（★ KIN-R2 任务3）：运行＝模型视图在场（执行
    // 面；TCP 解析在流内显式拒绝）；导出＝写出缝＋会话已有采样产物；
    // 回写＝会话姿态写点＋产物在场。缺一即禁用（诚实降级，与其他缝同
    // 纪律）。
    m_wsRunButton->setEnabled(m_services.modelView != nullptr && !m_wsInFlight);
    m_wsCancelButton->setEnabled(m_wsInFlight && m_wsProgress && !m_wsProgress->cancel.load());
    m_wsExportCsvButton->setEnabled(
        static_cast<bool>(m_services.exportWriter)
        && m_session.lastWorkspaceRun != nullptr);
    m_wsWritebackButton->setEnabled(
        m_services.sessionPose != nullptr
        && m_session.lastWorkspaceRun != nullptr);
    m_wsExportPngButton->setEnabled(
        static_cast<bool>(m_services.exportWriter)
        && m_session.lastWorkspaceRun != nullptr);
    // 采样参数字段使能面＋摘要表现投＋画布重绘（事件驱动出口——模式
    // 切换经 onWsModeChanged、投影面切换经 onWsProjectionPlaneChanged
    // 亦单独重进对应半区）。
    refreshWsParamVisibility();
    refreshWsSummary();
    refreshWsProjectionCanvas();
    refreshVisualization();
}

void KinematicsPanelWidget::setDisplayUnits(const std::optional<DisplayUnitProjection>& units)
{
    // WP-15-T20 F02/AT-27：编辑框也承载显示制式，不能只改标签再用新
    // 制式解释旧数值（原0.1 m会被当成0.1 mm）。先由旧制式逆投影为SI，
    // 再经唯一单位设施投到新制式。空/非法原文保留，让用户修正而非清零。
    // 所有转换先暂存；任一合法数值换算溢出时整次切换不生效，避免混合制式。
    std::vector<std::pair<QLineEdit*, QString>> pending;
    /**
     * @brief 暂存一个编辑字段的新显示值，不在此步改任何控件或会话。
     * @param edit [in] 本面板持有的非空编辑框；原文按旧显示制式解释。
     * @param angle [in] true为rad角度，false为m长度；坐标系保持基座系。
     * @return false表示换算溢出；非法/空原文原样保留并返回true。
     * 单位设施异常交由调用处归为切换拒绝，pending仅活到本次UI调用结束。
     */
    const auto reproject = [this, &units, &pending](QLineEdit* edit, bool angle) {
        bool parsed = false;
        const double value = edit->text().toDouble(&parsed);
        if (!parsed || !std::isfinite(value)) {
            return true; // 非法原文/空缺省不被替换，后续提交仍由原校验拒绝。
        }
        const double si = angle ? resolveKinAngleDisplayToSi(value, m_session.displayUnits)
                                : resolveKinDisplayToSi(value, m_session.displayUnits);
        if (!std::isfinite(si)) { return false; }
        const double projected = units.has_value()
            ? (angle ? units->projectAngle(si) : units->projectLength(si)) : si;
        if (!std::isfinite(projected)) { return false; }
        // 可编辑数值会重新进入求解，必须用double往返精度；不能采用只读
        // 指标的6位显示精度，否则反复切换将改变物理目标及求解结果。
        pending.emplace_back(edit, QString::number(
            projected, 'g', std::numeric_limits<double>::max_digits10));
        return true;
    };
    bool convertible = true;
    try {
        for (auto* edit : {m_targetX, m_targetY, m_targetZ, m_wsRegionCX,
                           m_wsRegionCY, m_wsRegionCZ, m_wsRegionSX,
                           m_wsRegionSY, m_wsRegionSZ}) {
            if (!reproject(edit, false)) { convertible = false; break; }
        }
        if (convertible) {
            for (auto* edit : {m_targetRoll, m_targetPitch, m_targetYaw}) {
                if (!reproject(edit, true)) { convertible = false; break; }
            }
        }
    } catch (const std::exception&) {
        // 单位设施拒绝数值范围属于用户输入问题，不让异常穿越Qt槽；
        // 下面统一保留全部原文和旧制式，提示用户缩小数值后重试。
        convertible = false;
    }
    if (!convertible) {
        showStatusText(tr("显示单位切换未应用：输入换算后超出有限数值范围；"
                          "请减小数值后重试。原单位与全部原值保留。"));
        return;
    }
    // 重投影只是同一 SI 目标的显示层换算，不应触发“用户改目标”失效
    // 逻辑；六个目标编辑器的信号在这段写入期间统一屏蔽。
    const QSignalBlocker blockTargetX(m_targetX);
    const QSignalBlocker blockTargetY(m_targetY);
    const QSignalBlocker blockTargetZ(m_targetZ);
    const QSignalBlocker blockTargetRoll(m_targetRoll);
    const QSignalBlocker blockTargetPitch(m_targetPitch);
    const QSignalBlocker blockTargetYaw(m_targetYaw);
    for (const auto& value : pending) { value.first->setText(value.second); }
    // 只改变显示文本与投影句柄，结果SI真值、配置、修订和缓存身份都不动。
    m_session.displayUnits = units;
    refreshAll();
}

void KinematicsPanelWidget::refreshTargetUnitPresentation()
{
    // 目标输入行的单位标注随显示制式重建（审查 A07 缺陷面①修复：修复前
    // 标签为构造期硬编码 "（m）"，切到 mm 后标签失实）。标注同时携带
    // **参考系标识"基座系"**（KIN-P1 验收"参考系标识"半区）：提交目标
    // 经 runSinglePointSolve 进入 IkRequest.targetInBase（基座系 SI——
    // 单元卡 §5.3），输入语义即基座系坐标。单位符号取当前制式长度/角度
    // token 原文（core Units 冻结词表——"m"/"cm"/"mm"、"rad"/"deg"）；
    // nullopt＝SI 直显制式。★ KIN-R2 任务4：新增姿态 RPY 三行的角度
    // 标注（角度量纲与长度量纲分立——resolveKinAngleDisplayToSi 的
    // 提交面对位；R/P/Y 为 Z-Y-X 欧角约定随 rw::math::RPY 语义）。
    const QString lengthSymbol =
        m_session.displayUnits.has_value()
            ? QString::fromStdString(
                std::string(m_session.displayUnits->lengthUnit().symbol()))
            : QStringLiteral("m");
    const QString angleSymbol =
        m_session.displayUnits.has_value()
            ? QString::fromStdString(
                std::string(m_session.displayUnits->angleUnit().symbol()))
            : QStringLiteral("rad");
    if (m_targetXLabel != nullptr) {
        m_targetXLabel->setText(tr("目标 X（%1，基座系）").arg(lengthSymbol));
        m_targetX->setPlaceholderText(
            tr("必填——基座系坐标，单位：%1").arg(lengthSymbol));
    }
    if (m_targetYLabel != nullptr) {
        m_targetYLabel->setText(tr("目标 Y（%1，基座系）").arg(lengthSymbol));
        m_targetY->setPlaceholderText(
            tr("必填——基座系坐标，单位：%1").arg(lengthSymbol));
    }
    if (m_targetZLabel != nullptr) {
        m_targetZLabel->setText(tr("目标 Z（%1，基座系）").arg(lengthSymbol));
        m_targetZ->setPlaceholderText(
            tr("必填——基座系坐标，单位：%1").arg(lengthSymbol));
    }
    // ★ 审查 G7 消歧（WP-15-T20 第三批）：RPY 轴义随标签显式标注——本
    // 面板 RPY 随 rw::math::RPY 参数命名（roll 驱动 Rz、pitch 驱动 Ry、
    // yaw 驱动 Rx）；requirements 面板"固定 Roll"按航空惯例绕 X。两侧
    // 各自自洽但同名反轴——标签带轴后消除静默误抄风险（跨域词表统一归
    // 所有者裁决，登记随卡 §14.6 第三批）。
    if (m_targetRollLabel != nullptr) {
        m_targetRollLabel->setText(tr("目标 RPY-Roll＝绕Z（%1，基座系）").arg(angleSymbol));
        m_targetRoll->setPlaceholderText(
            tr("姿态角，单位：%1（空＝0）").arg(angleSymbol));
    }
    if (m_targetPitchLabel != nullptr) {
        m_targetPitchLabel->setText(tr("目标 RPY-Pitch＝绕Y（%1，基座系）").arg(angleSymbol));
        m_targetPitch->setPlaceholderText(
            tr("姿态角，单位：%1（空＝0）").arg(angleSymbol));
    }
    if (m_targetYawLabel != nullptr) {
        m_targetYawLabel->setText(tr("目标 RPY-Yaw＝绕X（%1，基座系）").arg(angleSymbol));
        m_targetYaw->setPlaceholderText(
            tr("姿态角，单位：%1（空＝0）").arg(angleSymbol));
    }
    // ---- 单位切换下拉同步（G5 随附——会话制式为权威，组合框随刷新对
    // 齐；QSignalBlocker 防 setCurrentIndex 触发 onDisplayUnitChanged 的
    // 回环——呈现同步零切换语义）。
    if (m_lengthUnitBox != nullptr && m_angleUnitBox != nullptr) {
        const QSignalBlocker blockLength(m_lengthUnitBox);
        const QSignalBlocker blockAngle(m_angleUnitBox);
        const std::string lengthSymbol =
            m_session.displayUnits.has_value()
                ? std::string(m_session.displayUnits->lengthUnit().symbol())
                : std::string("m");
        const std::string angleSymbolStd =
            m_session.displayUnits.has_value()
                ? std::string(m_session.displayUnits->angleUnit().symbol())
                : std::string("rad");
        const int lengthIndex = m_lengthUnitBox->findText(
            QString::fromStdString(lengthSymbol));
        const int angleIndex = m_angleUnitBox->findText(
            QString::fromStdString(angleSymbolStd));
        if (lengthIndex >= 0) {
            m_lengthUnitBox->setCurrentIndex(lengthIndex);
        }
        if (angleIndex >= 0) {
            m_angleUnitBox->setCurrentIndex(angleIndex);
        }
    }
}

void KinematicsPanelWidget::refreshPosePane()
{
    // 位姿指标：域设施直调（IFkEvaluator——缝未装配＝空态呈现，不虚构行）。
    if (m_services.fkEvaluator == nullptr || m_services.modelView == nullptr) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        if (m_frameLabel != nullptr) {
            m_frameLabel->setText(tr("参考系标识：模型视图或评估器未装配——位姿页空态"));
        }
        return;
    }
    // 会话姿态在设即评估之；未设＝空呈现（零虚构数据行——零位评估会
    // 伪造"当前构型"语义）。
    if (m_services.sessionPose == nullptr || !m_services.sessionPose->isSet()) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        m_jointTable->setRowCount(0);
        if (m_frameLabel != nullptr) {
            m_frameLabel->setText(tr("参考系标识：会话姿态未设置——位姿页空态"));
        }
        return;
    }
    const std::vector<double>& q = m_services.sessionPose->jointConfiguration();
    // ★ KIN-P4/审查 §4：位姿页刷新的 TCP 经三入口统一解析单点
    // （resolveSessionTcp——KIN-14 默认 TCP 权威）。修复前此处构造空
    // TcpRef（canonical TCP 语义）——但 FK 不会仅凭空 tcpKey 自动找到
    // toolObject，无工具快照下评估恒 KIN-NO-TCP 失败、有工具快照下依赖
    // 隐式约定。解析失败＝位姿表空＋状态行显式提示（建模侧配置指引），
    // 不虚构指标行。
    const KinTcpResolution tcp = resolveSessionTcp(m_services);
    if (!tcp.ok) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        m_jointTable->setRowCount(0);
        if (m_frameLabel != nullptr) {
            m_frameLabel->setText(tr("参考系标识：TCP 未解析（见状态行）"));
        }
        showStatusText(QString::fromStdString(tcp.statusText));
        return;
    }
    // ★ KIN-R2 任务4（审查 §4 坐标系标识）：TCP 工具身份＋双参考系口径
    // 随解析结果现投——用户据此可知位姿值在哪个系下表达、TCP 属哪个
    // 工具（tcpKey 空＝canonical TCP——解析单点的显式名纪律）。
    if (m_frameLabel != nullptr) {
        m_frameLabel->setText(
            tr("TCP：%1（工具 %2）——位姿值按基座系 {B}（T_base_tcp）与"
               "世界系 {W}（T_world_tcp）双参考系呈现")
                .arg(tcp.tcp.tcpKey.empty()
                         ? QStringLiteral("canonical TCP")
                         : QString::fromStdString(tcp.tcp.tcpKey),
                     QString::fromStdString(tcp.tcp.toolObject.toCanonical())));
    }
    const auto metrics =
        m_services.fkEvaluator->evaluate(*m_services.modelView, tcp.tcp, q);
    if (!metrics.ok()) {
        // 失败如实呈现（KinematicsError 的稳定码 token＋detail——非模态
        // 状态行；token 转发表唯一出口 Errors.hpp）。
        const KinematicsError& err = metrics.error();
        showStatusText(QString::fromStdString(
            std::string("位姿指标不可用：")
            + std::string(kinematicsErrorCodeToken(err.code)) + " "
            + err.detail));
        return;
    }
    // ---- 呈现兜底（审查 G11 修复——WP-15-T20 第三批）：模型违约态的
    // NaN/非有限值会令 poseMetricRows→formatKinQuantityText 抛
    // invalid_argument（合法 +∞ 已按 A03 三分类放行，此异常仅剩"模型
    // 违约"形态）——就地转状态行，不让异常穿越 Qt 槽（与 A03"异常不
    // 穿越槽"教训同则；正常装配下不可达，防御深度）。
    try {
        renderRows(m_poseTable,
                   poseMetricRows(metrics.get(), m_session.displayUnits));
    } catch (const std::exception& e) {
        renderRows(m_poseTable, std::vector<KinNamedValueRow>{});
        m_jointTable->setRowCount(0);
        showStatusText(QString::fromStdString(
            std::string("位姿指标呈现异常（模型违约——值面含非法数值）：")
            + e.what()));
        return;
    }
    // 关节表（KIN-12 显示投影——显示制式下的关节向量；KIN-P3/审查 A05：
    // 逐轴按天然量纲投影——转动/连续轴 rad→角度、移动轴 m→长度，"数值
    // ＋单位"同显。逐轴类型从模型关节表提取（dofJointTypes——FK 成功
    // 蕴含 q 维度＝dof，类型表逐轴对齐）。
    const std::vector<runtime::JointType> axisTypes =
        dofJointTypes(m_services.modelView->model());
    m_jointTable->setRowCount(static_cast<int>(q.size()));
    for (std::size_t i = 0; i < q.size(); ++i) {
        m_jointTable->setItem(static_cast<int>(i), 0,
                              new QTableWidgetItem(tr("J%1").arg(i + 1)));
        // 防御面：FK 契约保证 q.size()==axisTypes.size()（同 dof 链序）；
        // 越界即装配层模型不一致——如实呈现占位不虚构数值（不抛异常穿
        // 越 Qt 槽，与 NFR-COR-03 的就地反馈同则）。
        const QString cell =
            i < axisTypes.size()
                ? QString::fromStdString(
                    formatJointAxisText(q[i], axisTypes[i],
                                        m_session.displayUnits))
                : QStringLiteral("—");
        m_jointTable->setItem(static_cast<int>(i), 1,
                              new QTableWidgetItem(cell));
    }
}

void KinematicsPanelWidget::refreshResultsPane()
{
    if (!m_session.lastSessionSolutionSetView) {
        renderSolutions(m_solutionTable, std::vector<KinSolutionRow>{});
        fillSingleColumn(m_statTable, QStringList{});
        renderRows(m_inspectorTable, std::vector<KinNamedValueRow>{});
        return;
    }
    // 逐轴类型真值（KIN-P3/审查 A05——q 列逐轴投影的模型来源）。解集在
    // 场而模型视图缺失的形态不经本面板任何流产生（求解前置模型视图）；
    // 防御面＝如实清空不虚构（零投影语义可用——猜量纲即 A05 复辟）。
    if (m_services.modelView == nullptr) {
        renderSolutions(m_solutionTable, std::vector<KinSolutionRow>{});
        fillSingleColumn(m_statTable, QStringList{});
        renderRows(m_inspectorTable, std::vector<KinNamedValueRow>{});
        return;
    }
    const IKinematicSolutionSet& set = *m_session.lastSessionSolutionSetView;
    renderSolutions(m_solutionTable,
                    solutionRowsFor(set, m_usableOnly->isChecked(),
                                    dofJointTypes(m_services.modelView->model()),
                                    m_session.displayUnits));
    // 统计行（四计数直投——零自算）。
    const auto st = set.statistics();
    fillSingleColumn(m_statTable,
                     {tr("初值展开数 %1").arg(qlonglong(st.rawCount)),
                      tr("收敛数 %1").arg(qlonglong(st.convergedCount)),
                      tr("去重后解数 %1").arg(qlonglong(st.dedupedCount)),
                      tr("硬过滤数 %1").arg(qlonglong(st.filteredCount))});
}

void KinematicsPanelWidget::setWritable(bool writable)
{
    m_session.writable = writable;  // 会话事实更新（ui 会话投影同源）
    refreshAll();
}

void KinematicsPanelWidget::refreshTaskArea()
{
    // 任务点消费视图现取重投影（taskPointsProvider——只读行集＋结果列）。
    // ★ 审查 G10 修正：本函数不消费 taskRowsProvider（任务九态/进度）——
    // 任务状态呈现归 ui 任务 Dock（ITaskPresentationModel 投影），本面板
    // 无进度列（呈现边界如实登记，注释此前声称"taskRowsProvider 现取"
    // 与实现不符——已对齐）。
    // 任务点表（只读消费视图——provider 现取零缓存）。列 0 行锚额外携带
    // pointOid 规范文本（Qt::UserRole——WP-15-T18 focusTaskPoint 定位键；
    // 呈现文本仍为工程用语标签，锚对用户不可见）。
    const std::vector<KinTaskPointRow> points =
        m_services.taskPoints ? m_services.taskPoints() : std::vector<KinTaskPointRow>{};
    m_taskPointTable->setRowCount(static_cast<int>(points.size()));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const KinTaskPointRow& p = points[i];
        const auto row = static_cast<int>(i);
        QTableWidgetItem* labelItem = new QTableWidgetItem(
            QString::fromStdString(p.label));
        labelItem->setData(Qt::UserRole,
                           QString::fromStdString(p.pointOid.toCanonical()));
        m_taskPointTable->setItem(row, 0, labelItem);
        m_taskPointTable->setItem(row, 1, new QTableWidgetItem(
                                              p.enabled ? tr("启用") : tr("停用")));
        m_taskPointTable->setItem(row, 2, new QTableWidgetItem(
                                              p.outcomeText.has_value()
                                                  ? QString::fromStdString(*p.outcomeText)
                                                  : tr("未计算")));
        m_taskPointTable->setItem(row, 3, new QTableWidgetItem(
                                              p.requiredCoverageDone.has_value()
                                                  ? (*p.requiredCoverageDone ? tr("完成")
                                                                             : tr("未完成"))
                                                  : tr("—")));
    }
}

void KinematicsPanelWidget::showCoverageRows(std::vector<KinCoverageRow> rows)
{
    // 覆盖结果行渲染（审查 G1 修复——WP-15-T20 第三批）：装配面已完成
    // 锚核对与"聚合→行"翻译，本函数只搬运成表（widget 零计算逻辑纪律
    // 不变）。空行集＝清表（对账失效/无结果的诚实空态——不保留陈旧行）。
    // 三列：覆盖轴（位置/姿态双口径——KIN-04 R3/R8 分列）、计数比
    // （"分子/分母"计数比直投——零第二口径）、标识（降级/零样本如实）。
    if (m_coverageTable == nullptr) {
        return;
    }
    m_coverageTable->setRowCount(static_cast<int>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const KinCoverageRow& r = rows[i];
        const auto row = static_cast<int>(i);
        m_coverageTable->setItem(row, 0, new QTableWidgetItem(
                                             QString::fromStdString(r.label)));
        m_coverageTable->setItem(row, 1, new QTableWidgetItem(
                                             QString::fromStdString(r.ratioText)));
        // 标识列组合词（零样本优先——比率不存在时降级词无意义）。
        QString flags;
        if (r.zeroSamples) {
            flags = tr("零样本——比率不存在");
        } else {
            if (r.degraded) {
                flags = tr("数据不足——已降级");
            }
        }
        m_coverageTable->setItem(row, 2, new QTableWidgetItem(flags));
    }
}

void KinematicsPanelWidget::focusTaskPoint(const std::optional<core::ObjectId>& oid)
{
    // 下行联动呈现半区（WP-15-T18——SelectionAdapter 消费选择服务后的
    // 执行器落点；UI 线程约束同其余会话交互面，§3.4）。
    if (m_taskPointTable == nullptr) {
        return;
    }

    // 无目标（多选/清空选中）＝仅清除高亮：清当前行，不切页不伪造定位
    // （requirements focusObject 同款幂等清除语义）。
    if (!oid.has_value()) {
        m_taskPointTable->clearSelection();
        m_taskPointTable->setCurrentItem(nullptr);
        return;
    }

    // 线性扫描行锚（refreshTaskArea 写入的 Qt::UserRole 规范文本——行序
    // ＝provider 供给序，扫描即稳定语义）。命中＝切到任务点页并置当前行
    // （结果面板高亮可见性——acceptance 4 v1 链路"任务点选择→结果面板
    // 高亮"的呈现落点；任务点定义真值归 requirements，本表是其结果列的
    // 本域消费视图，高亮动作零写回）；未命中＝清除（该选中对象在本域
    // 消费视图无结果行——不伪造定位，与 requirements"闭包外对象不清行
    // 定位"同案）。
    const QString anchorText = QString::fromStdString(oid.value().toCanonical());
    for (int i = 0; i < m_taskPointTable->rowCount(); ++i) {
        if (QTableWidgetItem* it = m_taskPointTable->item(i, 0);
            it != nullptr && it->data(Qt::UserRole).toString() == anchorText) {
            if (m_tabs != nullptr) {
                m_tabs->setCurrentIndex(1);  // 页②任务点验证（构建序固定）
            }
            m_taskPointTable->setCurrentItem(it);
            m_taskPointTable->scrollToItem(it);
            return;
        }
    }
    m_taskPointTable->clearSelection();
    m_taskPointTable->setCurrentItem(nullptr);
}

std::optional<rw::math::Transform3D<double>>
KinematicsPanelWidget::readTargetPoseFromEditors(const QString& errorPrefix)
{
    // errorPrefix 为空表示调用方只需要无副作用的合法性探测（目标编辑
    // 变化时用于更新“结果已过期”状态）；正式提交路径传入可读前缀，才
    // 把输入错误写入状态行。
    const auto reportInvalid = [this, &errorPrefix](const QString& detail) {
        if (!errorPrefix.isEmpty()) {
            showStatusText(tr("%1位姿非法（%2）——原值保留")
                               .arg(errorPrefix, detail));
        }
    };
    // ---- 位置分量（显示制式 → SI）：解析失败**或非有限**（"inf"/"nan"
    // 可被解析但不是合法坐标——NFR-COR-03 非有限不静默）＝调用方输入
    // 错误：就地反馈保留原值（UX-05），不向求解器传递非有限目标（域
    // IkRequest 校验 fail-fast 的调用方侧前置拦截——交互层拒绝而非异常
    // 穿越 Qt 槽）。审查条目 A07：显示制式→SI 逆换算唯一出口
    // resolveKinDisplayToSi（mm 输入 0.1 → 1e-4 m——修复前方向反转）。
    bool okX = false, okY = false, okZ = false;
    double x = m_targetX->text().toDouble(&okX);
    double y = m_targetY->text().toDouble(&okY);
    double z = m_targetZ->text().toDouble(&okZ);
    if (!okX || !okY || !okZ || !std::isfinite(x) || !std::isfinite(y)
        || !std::isfinite(z)) {
        reportInvalid(tr("位置分量解析失败或非有限"));
        return std::nullopt;
    }
    x = resolveKinDisplayToSi(x, m_session.displayUnits);
    y = resolveKinDisplayToSi(y, m_session.displayUnits);
    z = resolveKinDisplayToSi(z, m_session.displayUnits);

    // ---- 姿态分量（★ KIN-R2 任务4——RPY 三字段，审查 §4"姿态固定为
    // 单位旋转"修复面）：空输入＝0（历史行为兼容——修复前姿态恒单位
    // 旋转，即 RPY 全零的等价形态）；非空须可解析且有限。显示制式→
    // SI rad 经 resolveKinAngleDisplayToSi（角度量纲独立入口——误走
    // 长度版会按长度因子换算，量纲错配）。
    auto parseAngle = [this](QLineEdit* edit, bool* ok) -> double {
        const QString text = edit->text().trimmed();
        if (text.isEmpty()) {
            *ok = true;      // 空＝0（单位旋转缺省——向后兼容语义）
            return 0.0;
        }
        return text.toDouble(ok);
    };
    bool okR = false, okP = false, okYw = false;
    double roll = parseAngle(m_targetRoll, &okR);
    double pitch = parseAngle(m_targetPitch, &okP);
    double yaw = parseAngle(m_targetYaw, &okYw);
    if (!okR || !okP || !okYw || !std::isfinite(roll) || !std::isfinite(pitch)
        || !std::isfinite(yaw)) {
        reportInvalid(tr("姿态分量解析失败或非有限"));
        return std::nullopt;
    }
    roll = resolveKinAngleDisplayToSi(roll, m_session.displayUnits);    // rad
    pitch = resolveKinAngleDisplayToSi(pitch, m_session.displayUnits);  // rad
    yaw = resolveKinAngleDisplayToSi(yaw, m_session.displayUnits);      // rad

    // ---- 基座系 {B} 目标位姿组装：位置（m）＋RPY→旋转。Z-Y-X 语义由
    // KinPanelRotation::rotationFromRpySi 单点承载（F-919）：该实现用 Eigen 头文件
    // 轴角组合复现 rw::math::RPY 的同一矩阵，刻意不实例化 RPY::toRotation3D
    // （非内联符号——独立冒烟树无框架库会 LNK2019）；等价性由集成侧
    // IRD_HAVE_SDURW_MATH 用例逐元钉住，界面层零自写三角。
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(x, y, z),
        rotationFromRpySi(roll, pitch, yaw));
}

void KinematicsPanelWidget::onSolveClicked()
{
    // L-K2 转接（零内联逻辑——编排函数承担）。目标位姿六字段读取与
    // 逆换算收拢到 readTargetPoseFromEditors 单点（★ KIN-R2 任务4——
    // 位置 XYZ＋姿态 RPY；解析失败＝就地反馈并停在提交前，绝不向求解
    // 器传递非法/非有限目标）。
    const std::optional<rw::math::Transform3D<double>> target =
        readTargetPoseFromEditors(tr("目标"));
    if (!target.has_value()) {
        return;  // 反馈已在 helper 内就地给出——保留原值停在原地
    }
    // 会话求解请求装配（KIN-P2/审查 A08——从会话/模型装配完整合法请求：
    // TCP 经 KIN-14 默认工具解析、初值/容差/迭代来自求解配置、referenceQ
    // 取会话姿态、intervals 取模型限位）。装配失败＝显式呈现稳定诊断码
    // 并停在原地，绝不提交残缺请求（修复前此处以默认构造 IkRequest 直传
    // ——TCP 空/初值空/referenceQ 与 intervals 维度 0，真实 IkSolver 校验
    // 必 fail-fast，异常穿越 Qt 槽）。
    const KinSolveRequestAssembly assembly =
        assembleSessionSolveRequest(m_services, m_session);
    if (!assembly.ok) {
        showStatusText(QString::fromStdString(assembly.statusText));
        return;
    }
    // ---- 求解执行形态分派（审查 G3 修复——WP-15-T20 第三批）：异步缝
    // 在场＝非阻塞（job 经装配层线程设施执行，UI 线程立即返回——插件零
    // 自建线程红线〔acceptance 2〕的通道化落位）；缝缺席＝同步内联降级
    // （runSinglePointSolve 原径——其内部超预算转后台语义不变，诚实边
    // 缘；同步路径的"UI 全程占用＋双份计算"残留随装配缝缺席登记）。
    if (m_services.inlineSolveAsync) {
        runSolveNonBlocking(*target, assembly.request);
        return;  // 状态行/刷新在异步回调（UI 线程）内完成
    }
    const KinSolveFlowResult r =
        runSinglePointSolve(m_services, m_session, *target, assembly.request);
    if (r.resultCommitted) {
        m_lastSolvedTarget = *target;
        m_solveResultExpired = false;
    }
    m_solveTargetAtSubmit.reset();
    showStatusText(QString::fromStdString(r.statusText));
    // 全面板刷新（★ KIN-P4——refreshAll 而非仅结果页：解集入会话态后
    // 导出/复算按钮的门控随即点亮——修复前按钮态停留在求解前的"无结果
    // 禁用"，用户求解成功仍不能导出，闭环断链）。现取重投影零缓存。
    refreshAll();
}

void KinematicsPanelWidget::runSolveNonBlocking(
    const rw::math::Transform3D<double>& targetInBase, const IkRequest& request)
{
    // ---- G3 修复（WP-15-T20 第三批）——缝化非阻塞单点求解：
    // 请求按值快照（targetInBase 绑定）经 inlineSolveAsync 装配缝提交
    // （job 在装配层工作线程恰执行一次——IIkSolver 纯函数线程安全 §9.2；
    // 本函数零线程创建——插件零自建线程红线）。完成回调（装配层保证回
    // UI 线程）经 finishSinglePointSolve 与同步内联路径同一写点入会话
    // 态（零双份计算——修复前同步跑完才按耗时事后转后台，超时场景
    // UI 冻结全程＋同一请求后台重算）。在途旗标防重复提交（完成回调清
    // 位＋refreshAll 重进按钮门控）。context＝this——面板先于回调销毁
    // 时回投被 Qt 弃置（迟到结果不触碰已销毁面板——缝面契约）。
    IkRequest req = request;
    req.targetInBase = targetInBase;
    // 提交拍冻结三类身份键，完成回调必须用同一组键核对当前会话。
    // 这一步把“后台任务正在算什么”与 UI 线程随后可能换绑的会话明确
    // 分开；旧结果即使完成，也只能作为过期反馈，不能写入新会话。
    const core::ContentIdentity snapshotIdAtSubmit = m_session.snapshotId;
    const std::uint64_t epochAtSubmit = m_session.epoch;
    const core::ContentIdentity configDigestAtSubmit =
        analysisConfigurationDigest(m_session.savedConfig);

    if (m_services.modelView == nullptr) {
        m_solveInFlight = false;
        m_solveTargetAtSubmit.reset();
        showStatusText(tr("求解失败：提交时没有可用的模型快照"));
        refreshAll();
        return;
    }
    // REUSE-T17：提交拍经锚能力入口取已编译模型视图（RuntimeSnapshot 为
    // 真实 owner）；经 AnchorRuntimeView 纯转发桥供任务消费——后台任务只
    // 持有该锚，UI 换绑/宿主释放均不影响其存活与内容。纯值视图（无编译
    // 产物）在提交拍即拒绝——不回退旧手写链（两分错误轨）。
    auto modelAnchor = m_services.modelView->runtimeModelView();
    if (modelAnchor == nullptr) {
        m_solveInFlight = false;
        m_solveTargetAtSubmit.reset();
        showStatusText(tr("求解失败：提交时没有可用的模型快照"));
        refreshAll();
        return;
    }
    auto modelSnapshot = std::make_shared<const AnchorRuntimeView>(
        std::move(modelAnchor));
    req.modelView = modelSnapshot.get();
    m_solveInFlight = true;
    m_solveTargetAtSubmit = targetInBase;
    // 重试尚无新结局：取消或设施失败时仍须展示旧结果的真实有效性，
    // 只有完成回调实际提交新解集才允许清除过期态（KIN-02）。
    showStatusText(tr("求解中（异步执行，界面持续响应）……"));
    refreshAll();  // 求解按钮随在途旗标禁用（门控在 refreshAll）
    // 注意：job 按值捕获请求快照与求解器缝指针——面板/会话态在求解期间
    // 被改不回读（D-KIN-4：身份不含会话）；完成回调写会话态（装配层保证
    // 回 UI 线程）。
    const IIkSolver* solver = m_services.ikSolver;  // 缝指针——存活期由装配层契约保证
    const auto t0 = std::chrono::steady_clock::now();
    m_services.inlineSolveAsync(
        [solver, modelSnapshot = std::move(modelSnapshot), req = std::move(req)]() mutable {
            // modelSnapshot 必须在 req.modelView 使用期间保持存活；后台线程
            // 只读这一份不可变视图，UI 线程 rebind 不会改变其内容。
            req.modelView = modelSnapshot.get();
            return solver->solve(req);
        },
        [this, t0, snapshotIdAtSubmit, epochAtSubmit, configDigestAtSubmit](
            IkOutcome outcome) {
            m_solveInFlight = false;
            // 目标快照由 UI 线程提交前保存；回调只在 UI 线程消费并随后清除，
            // 与会话/配置身份一起构成这次异步请求的完整绑定。
            const auto targetAtSubmit = m_solveTargetAtSubmit;
            bool sameSession = m_session.snapshotId == snapshotIdAtSubmit
                               && m_session.epoch == epochAtSubmit;
            try {
                sameSession = sameSession
                    && analysisConfigurationDigest(m_session.savedConfig)
                           == configDigestAtSubmit;
            } catch (const std::exception&) {
                // 当前配置若已失去合法性，旧结果同样不能写回；把异常
                // 归入过期态，避免 Qt 回调线程再次抛出调用方错误。
                sameSession = false;
            }
            const auto currentTarget =
                readTargetPoseFromEditors(QString());
            const bool sameTarget = targetAtSubmit.has_value()
                                    && currentTarget.has_value()
                                    && equivalentTargetPose(*currentTarget,
                                                            *targetAtSubmit);
            if (!sameSession || !sameTarget) {
                // 会话、配置或目标已经变化；旧结果不能污染当前结果集。
                m_solveTargetAtSubmit.reset();
                m_solveResultExpired = true;
                showStatusText(tr("求解结果已过期：会话、配置或目标已变化，请重新求解"));
                refreshAll();
                return;
            }
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0);
            const KinSolveFlowResult r = finishSinglePointSolve(
                m_session, std::move(outcome), elapsed);
            m_solveTargetAtSubmit.reset();
            if (r.resultCommitted && targetAtSubmit.has_value()) {
                m_lastSolvedTarget = *targetAtSubmit;
                m_solveResultExpired = false;
            }
            showStatusText(QString::fromStdString(r.statusText));
            refreshAll();
        },
        this);
}

void KinematicsPanelWidget::onBatchClicked()
{
    // L-K3 转接。
    showStatusText(QString::fromStdString(submitBatchValidation(m_services, m_session)));
    refreshTaskArea();
}

void KinematicsPanelWidget::onCoverageRunClicked()
{
    // L-K6 转接。
    showStatusText(QString::fromStdString(
        submitCoverageEvaluation(m_services, m_session)));
    refreshTaskArea();
}

void KinematicsPanelWidget::onResetHomeClicked()
{
    // L-K4 转接（KIN-P3/审查 A09——具名来源版）：Home 目标只能来自
    // KinPanelServices.homePose 具名来源缝（装配层从命名位姿集保留键
    // homeConfiguration 解析）；来源缺失＝按钮已禁用（refreshAll 门控），
    // 缝在场但未定义 Home/维度不符＝编排函数给明确提示。修复前此处把
    // 当前会话姿态原样写回（无目标来源的假成功——审查 A09 根因），已废。
    // 零修订语义不变：只写会话姿态（KIN-06/AT-04——PA-2 不动设计修订）。
    showStatusText(QString::fromStdString(
        resetSessionHomeFromNamedSource(m_services)));
}

void KinematicsPanelWidget::onSetDefaultTcpClicked()
{
    // L-K9 转接。★ KIN-P4/审查 §4：设默认 TCP 的目标身份经三入口统一
    // 解析单点（resolveSessionTcp——KIN-14 默认工具权威）取得合法工具
    // 身份；解析失败＝显式提示并停在原地，绝不提交空 TcpRef（修复前恒
    // 传 TcpRef{}——modeling 侧 defaultTcp 断言对空 toolObject 的悬空
    // 引用必拒，用户只见抽象诊断码不见原因）。
    const KinTcpResolution tcp = resolveSessionTcp(m_services);
    if (!tcp.ok) {
        showStatusText(QString::fromStdString(
            "设默认 TCP 不可用：" + tcp.statusText));
        return;
    }
    showStatusText(QString::fromStdString(
        submitSetDefaultTcp(m_services, m_session, tcp.tcp)));
}

void KinematicsPanelWidget::onSetDefaultDeviceClicked()
{
    // L-K9 转接（设备根身份取自模型视图——快照内解析的会话事实）。
    core::ObjectId robotOid;
    if (m_services.modelView != nullptr) {
        robotOid = m_services.modelView->model().chain().robotObjectId;
    }
    showStatusText(QString::fromStdString(
        submitSetDefaultDevice(m_services, m_session, robotOid)));
}

void KinematicsPanelWidget::onExportJsonClicked()
{
    showStatusText(QString::fromStdString(
        exportSessionResults(m_services, m_session, "kin-results.json", false)));
}

void KinematicsPanelWidget::onExportCsvClicked()
{
    showStatusText(QString::fromStdString(
        exportSessionResults(m_services, m_session, "kin-results.csv", true)));
}

void KinematicsPanelWidget::onRecomputeClicked()
{
    // KIN-P4 转接（复算编排——目标位姿取面板当前编辑值；原任务定义的
    // seed/referenceQ 自原结果身份显式携带，快照重绑定为当前会话锚）。
    // 目标六字段读取与逆换算同一单点（★ KIN-R2 任务4——与求解入口共用
    // readTargetPoseFromEditors；复算同属提交求解的入口，解析失败就地
    // 反馈不向求解器传递）。
    const std::optional<rw::math::Transform3D<double>> target =
        readTargetPoseFromEditors(tr("复算目标"));
    if (!target.has_value()) {
        return;  // 反馈已在 helper 内就地给出——保留原值停在原地
    }
    const KinRecomputeFlowResult r =
        recomputeSessionFromIdentity(m_services, m_session, *target);
    if (r.ok) {
        // KIN-08：复算已经写入新解集，目标基线必须与该产物同步前移；
        // 拒绝、取消和设施失败不改基线，以免旧结果被误标为当前结果。
        m_lastSolvedTarget = *target;
        m_solveResultExpired = false;
    }
    showStatusText(QString::fromStdString(r.statusText));
    // 全面板刷新（同求解入口——新解集入会话态后门控/解表同步）。
    refreshAll();
}

void KinematicsPanelWidget::onBackfillTcpClicked()
{
    // ---- L-K5 位姿回填（审查 G4 修复——WP-15-T20 第三批）：会话姿态经
    // FK 得当前 TCP 位姿，按显示制式投影后回填目标六字段（仅呈现默认值
    // ——求解身份不含会话，D-KIN-4；零修订零求解器触碰）。前提缺失＝
    // 状态行如实说明并停在原地（与复位 Home 拒绝轨同纪律）。
    const std::optional<KinTcpTargetBackfill> backfill =
        tcpTargetBackfillValues(m_services, m_session);
    if (!backfill.has_value()) {
        showStatusText(tr("回填不可用：需要模型视图、FK 评估器、已设置的"
                          "会话姿态与可解析的默认 TCP"));
        return;
    }
    const auto text = [](double v) { return QString::number(v, 'g', 12); };
    // TCP 回填是新的用户求解起点，必须让旧结果失效；信号屏蔽避免六个
    // 中间文本状态被逐字段刷新，最后统一呈现一次过期状态。
    const QSignalBlocker blockTargetX(m_targetX);
    const QSignalBlocker blockTargetY(m_targetY);
    const QSignalBlocker blockTargetZ(m_targetZ);
    const QSignalBlocker blockTargetRoll(m_targetRoll);
    const QSignalBlocker blockTargetPitch(m_targetPitch);
    const QSignalBlocker blockTargetYaw(m_targetYaw);
    m_targetX->setText(text(backfill->xDisplay));
    m_targetY->setText(text(backfill->yDisplay));
    m_targetZ->setText(text(backfill->zDisplay));
    m_targetRoll->setText(text(backfill->rollDisplay));
    m_targetPitch->setText(text(backfill->pitchDisplay));
    m_targetYaw->setText(text(backfill->yawDisplay));
    m_solveResultExpired = true;
    refreshAll();
    showStatusText(tr("已回填当前 TCP 位姿（显示制式数值，单位随行标签）"
                      "——仅作编辑起点，请重新求解"));
}

void KinematicsPanelWidget::onDisplayUnitChanged(int index)
{
    // ---- L-K8 生产入口（审查 G5 修复——WP-15-T20 第三批）：长度/角度
    // 两下拉当前值→DisplayUnitProjection→setDisplayUnits 唯一入口全面
    // 板重投影（零修订/零重算/结果不动——V-17）。m＋rad 组合＝nullopt
    // （SI 直显制式）；下拉值域封闭（m/cm/mm＋rad/deg——KIN-12 R1 冻结
    // 子集），词表外组合不可达。index 未用（组合框自述当前值）。
    Q_UNUSED(index);
    if (m_lengthUnitBox == nullptr || m_angleUnitBox == nullptr) {
        return;
    }
    const std::string length = m_lengthUnitBox->currentText().toStdString();
    const std::string angle = m_angleUnitBox->currentText().toStdString();
    if (length == "m" && angle == "rad") {
        setDisplayUnits(std::optional<DisplayUnitProjection>{});
        return;
    }
    if (const auto units =
            DisplayUnitProjection::tryFind(length, angle)) {
        setDisplayUnits(units);
    }
}

// ---- 探索式采样组（★ KIN-R2 任务3/KIN-09——槽与刷新实现）------------

void KinematicsPanelWidget::refreshWsParamVisibility()
{
    // 字段使能面＝模式分派（与域 validateWorkspaceSamplingQuery 的按模式
    // 校验面对位——区域模式消费区域盒/位置采样定义、关节模式消费关节
    // 采样定义，两族互不越界；禁用态字段值不参与装配——widget 装配函数
    // 按模式显式取值）。
    const int mode = m_wsModeBox != nullptr ? m_wsModeBox->currentIndex() : 0;
    const bool cartesian = mode == 0;  // WorkspaceSamplingMode::CartesianRegion
    const bool jointRandom = mode == 1;
    const bool jointGrid = mode == 2;
    const bool positionRandom =
        cartesian
        && m_wsPositionMethodBox != nullptr
        && m_wsPositionMethodBox->currentIndex() == 0;
    QLineEdit* const cartesianEdits[] = {m_wsRegionCX, m_wsRegionCY, m_wsRegionCZ,
                                         m_wsRegionSX, m_wsRegionSY, m_wsRegionSZ};
    for (QLineEdit* edit : cartesianEdits) {
        if (edit != nullptr) {
            edit->setEnabled(cartesian);
        }
    }
    if (m_wsPositionMethodBox != nullptr) {
        m_wsPositionMethodBox->setEnabled(cartesian);
    }
    if (m_wsRegionRandom != nullptr) {
        m_wsRegionRandom->setEnabled(positionRandom);
    }
    QLineEdit* const gridEdits[] = {m_wsRegionGridX, m_wsRegionGridY, m_wsRegionGridZ};
    for (QLineEdit* edit : gridEdits) {
        if (edit != nullptr) {
            edit->setEnabled(cartesian && !positionRandom);
        }
    }
    if (m_wsJointRandom != nullptr) {
        m_wsJointRandom->setEnabled(jointRandom);
    }
    if (m_wsJointGrid != nullptr) {
        m_wsJointGrid->setEnabled(jointGrid);
    }
}

void KinematicsPanelWidget::refreshWsSummary()
{
    // 摘要表现投（lastWorkspaceRun 现取零缓存——无产物＝空表，不虚构
    // "未采样"数据行）。行集：模式 token（F-905 溯源标注）/样本总数/
    // 可达/不可达/数据不足/未运行/取消标记。全部计数来自域产物逐样本
    // 状态（SampleState 五值词表），零估算。
    if (m_wsSummaryTable == nullptr) {
        return;
    }
    if (!m_session.lastWorkspaceRun) {
        m_wsSummaryTable->setRowCount(0);
        return;
    }
    const WorkspaceSampleRun& run = *m_session.lastWorkspaceRun;
    std::uint64_t reached = 0;
    std::uint64_t unreachable = 0;
    std::uint64_t dataInsufficient = 0;
    for (const WorkspaceSampleResult& r : run.results) {
        switch (r.state) {
            case SampleState::Reached:
                ++reached;
                break;
            case SampleState::Unreachable:
                ++unreachable;
                break;
            case SampleState::DataInsufficient:
                ++dataInsufficient;
                break;
            default:
                break;
        }
    }
    const auto text = [](std::string v) {
        return QString::fromStdString(std::move(v));
    };
    fillSingleColumn(
        m_wsSummaryTable,
        {text("模式 " + std::string(workspaceSamplingModeToken(run.mode))),
         text("样本总数 " + std::to_string(run.results.size())),
         text("可达 " + std::to_string(reached)),
         text("不可达 " + std::to_string(unreachable)),
         text("数据不足 " + std::to_string(dataInsufficient)),
         text("未运行 " + std::to_string(run.notRunCount)),
         text(run.cancelled ? std::string("已取消（partial 产物）")
                            : std::string("完整运行"))});
}

void KinematicsPanelWidget::onWsModeChanged(int index)
{
    // 模式切换＝参数字段使能面重建（编辑即草稿——运行时才装配域查询；
    // index 即 WorkspaceSamplingMode 枚举序，QComboBox 值域排除非法值）。
    Q_UNUSED(index);
    refreshWsParamVisibility();
}

void KinematicsPanelWidget::onWsRunClicked()
{
    if (m_wsInFlight) { return; } // 直接槽调用同样受门控，避免覆盖在途桥。
    // 编辑字段 → 面板参数（SI 真值）：区域盒中心/边长按显示制式读入并
    // 经 resolveKinDisplayToSi 逆换算（与目标编辑器同纪律——提交面唯一
    // 换算点）；计数字段按无符号整数解析。解析失败/非有限/越界＝调用方
    // 输入错误：就地反馈保留原值，不提交残缺查询（域校验 fail-fast 的
    // 交互层前置拦截——与 F-903 提示轨同则，异常不穿越 Qt 槽）。
    KinWorkspaceSamplingParams params;
    params.mode = static_cast<WorkspaceSamplingMode>(
        m_wsModeBox != nullptr ? m_wsModeBox->currentIndex() : 0);
    // 区域盒几何（仅区域模式装配——关节模式禁用态字段不取值）。
    if (params.mode == WorkspaceSamplingMode::CartesianRegion) {
        bool ok = false;
        auto readLength = [this, &ok](QLineEdit* edit) -> double {
            const QString t = edit->text().trimmed();
            bool good = false;
            const double v = t.toDouble(&good);
            // WP-15-T20 F01：每次成功解析必须显式回写true；旧实现仅在
            // 失败时写false，外层初值false导致任何合法区域盒都被拒绝。
            ok = good && std::isfinite(v);
            if (!ok) {
                return 0.0;
            }
            const double si = resolveKinDisplayToSi(v, m_session.displayUnits); // 基座系m。
            ok = std::isfinite(si); // 换算后也要有限，不能把溢出交给数值求解。
            return si;
        };
        params.regionCenterX = readLength(m_wsRegionCX);
        if (ok) {
            params.regionCenterY = readLength(m_wsRegionCY);
        }
        if (ok) {
            params.regionCenterZ = readLength(m_wsRegionCZ);
        }
        if (ok) {
            params.regionSizeX = readLength(m_wsRegionSX);
        }
        if (ok) {
            params.regionSizeY = readLength(m_wsRegionSY);
        }
        if (ok) {
            params.regionSizeZ = readLength(m_wsRegionSZ);
        }
        if (!ok) {
            showStatusText(tr("采样参数非法（区域盒含不可解析或非有限数值）"
                              "——原值保留"));
            return;
        }
        params.positionRandom =
            m_wsPositionMethodBox->currentIndex() == 0;
        if (params.positionRandom) {
            params.regionRandomCount =
                m_wsRegionRandom->text().trimmed().toUInt(&ok);
            if (!ok) {
                showStatusText(tr("采样参数非法（区域随机样本数非非负整数）"
                                  "——原值保留"));
                return;
            }
        } else {
            bool okX = false, okY = false, okZ = false;
            params.regionGridCounts[0] =
                m_wsRegionGridX->text().trimmed().toUInt(&okX);
            params.regionGridCounts[1] =
                m_wsRegionGridY->text().trimmed().toUInt(&okY);
            params.regionGridCounts[2] =
                m_wsRegionGridZ->text().trimmed().toUInt(&okZ);
            if (!okX || !okY || !okZ) {
                showStatusText(tr("采样参数非法（区域网格计数非非负整数）"
                                  "——原值保留"));
                return;
            }
        }
    }
    // 关节模式参数（按模式装配——统一值施加全轴的 UI 子集语义）。
    if (params.mode == WorkspaceSamplingMode::JointSpaceRandom) {
        bool ok = false;
        params.jointRandomCount = m_wsJointRandom->text().trimmed().toUInt(&ok);
        if (!ok) {
            showStatusText(tr("采样参数非法（关节随机样本数非非负整数）——原值保留"));
            return;
        }
    }
    if (params.mode == WorkspaceSamplingMode::JointSpaceGrid) {
        bool ok = false;
        params.jointGridPerAxis = m_wsJointGrid->text().trimmed().toUInt(&ok);
        if (!ok || params.jointGridPerAxis < 1U) {
            showStatusText(tr("采样参数非法（关节网格每轴取点数须 ≥1）——原值保留"));
            return;
        }
    }
    if (m_services.workspaceAsync && m_services.modelView != nullptr) {
        // KIN-09：REUSE-T17——提交拍捕获编译模型锚并经 AnchorRuntimeView
        // 纯转发桥供后台独占临时会话；换绑/宿主释放不影响锚存活。纯值
        // 视图无锚即拒绝（两分错误轨，不回退旧手写链）。
        auto modelAnchor = m_services.modelView->runtimeModelView();
        if (modelAnchor == nullptr) {
            showStatusText(tr("采样失败：当前模型视图无已编译产物"));
            return;
        }
        const auto model = std::make_shared<const AnchorRuntimeView>(
            std::move(modelAnchor));
        const auto snapshot = m_session.snapshotId;
        const auto epoch = m_session.epoch;
        const auto config = m_session.savedConfig;
        const auto digest = analysisConfigurationDigest(config);
        const auto progress = std::make_shared<KinSamplingProgress>();
        // 回执由后台写、完成后UI读；宿主回投形成先行关系，不并发读写。
        const auto output = std::make_shared<KinModuleSessionState>();
        output->savedConfig = config;
        const auto result = std::make_shared<KinWorkspaceSamplingFlowResult>();
        m_wsProgress = progress;
        m_wsInFlight = true;
        m_wsProgressBar->setValue(0);
        m_wsProgressTimer->start();
        showStatusText(tr("采样中，可继续操作界面或取消……"));
        refreshAll();
        try {
            m_services.workspaceAsync(
                [model, params, progress, output, result] {
                    KinPanelServices isolated;
                    isolated.modelView = model.get();
                    try {
                        *result = runWorkspaceSamplingFlow(isolated, *output, params, *progress);
                    } catch (const std::exception& e) {
                        result->statusText = std::string("采样设施失败：") + e.what();
                    } catch (...) {
                        result->statusText = "采样设施失败：未知异常";
                    }
                },
                [this, snapshot, epoch, digest, output, result, progress] {
                    m_wsInFlight = false;
                    m_wsProgressTimer->stop();
                    m_wsProgressBar->setValue(progress->percent.load());
                    bool current = m_session.snapshotId == snapshot && m_session.epoch == epoch;
                    try {
                        current = current && analysisConfigurationDigest(m_session.savedConfig) == digest;
                    } catch (const std::exception&) { current = false; }
                    if (!current) {
                        showStatusText(tr("采样结果已过期：会话或配置已变化，请重新采样"));
                    } else {
                        if (result->ok) { m_session.lastWorkspaceRun = output->lastWorkspaceRun; }
                        showStatusText(QString::fromStdString(result->statusText));
                    }
                    m_wsProgress.reset();
                    refreshAll();
                }, this);
        } catch (const std::exception& e) {
            // 执行设施拒绝提交：恢复可重试状态，旧产物原样保留。
            progress->cancel.store(true);
            m_wsInFlight = false;
            m_wsProgressTimer->stop();
            m_wsProgress.reset();
            showStatusText(tr("采样提交失败：%1").arg(QString::fromUtf8(e.what())));
            refreshAll();
        }
        return;
    }
    // 缺宿主执行设施时保留同步诊断路径；正式宿主必须装配workspaceAsync。
    const KinWorkspaceSamplingFlowResult r =
        runWorkspaceSamplingFlow(m_services, m_session, params, m_wsContext);
    showStatusText(QString::fromStdString(r.statusText));
    // 全面板刷新（导出/回写按钮门控随产物点亮＋摘要表现投）。
    refreshAll();
}

void KinematicsPanelWidget::onWsExportCsvClicked()
{
    // 编排转接（契约 v2 CSV——行头含 sampling-mode 溯源列，F-905 导出
    // 半区；文件名固定于导出目录——写出经 io AtomicFile 装配缝）。
    showStatusText(QString::fromStdString(exportWorkspacePointCloudCsv(
        m_services, m_session, "kin-workspace-pointcloud.csv")));
}

KinematicsPanelWidget::~KinematicsPanelWidget()
{
    // 销毁不等待计算，令后台在下一检查点终止；Qt释放定时器与控件，
    // 宿主负责弃置onDone，后台持有的冻结模型/进度由任务自行释放。
    if (m_wsProgress) { m_wsProgress->cancel.store(true); }
}

void KinematicsPanelWidget::onWsWritebackClicked()
{
    // 样本序解析（results 全局序——空/非法＝输入错误就地反馈）。
    bool ok = false;
    const std::uint64_t sampleIndex =
        m_wsSampleIndex->text().trimmed().toULongLong(&ok);
    if (!ok) {
        showStatusText(tr("回写样本序非法（须为非负整数）——原值保留"));
        return;
    }
    // 编排转接（域回写值→既有会话回写通道——KIN-06 零修订；无解/越界
    // 样本拒绝轨在编排内转状态行）。
    showStatusText(QString::fromStdString(
        writebackWorkspacePoint(m_services, m_session, sampleIndex)));
    // 会话姿态已更新——位姿页随刷新现投（事件驱动，零缓存）。
    refreshPosePane();
}

void KinematicsPanelWidget::refreshWsProjectionCanvas()
{
    // 投影散点画布重绘（★ KIN-R2 任务6）。数据链（全部域设施直调，零
    // 二次计算语义）：lastWorkspaceRun → buildWorkspacePointCloud（渲染
    // 数据唯一组装点）→ projectWorkspacePoints（舍轴投影唯一实现点）→
    // QPainter 离屏绘制。无产物＝占位画布（不虚构数据）。
    if (m_wsCanvasLabel == nullptr) {
        return;
    }
    const int width = 480;   // 画布宽（px——呈现几何，非业务量）
    const int height = 360;  // 画布高（px）
    QPixmap canvas(width, height);
    m_wsPickPixels.clear(); // 每次重画使旧像素命中失效，避免跨结果/投影串选。
    canvas.fill(Qt::white);
    if (!m_session.lastWorkspaceRun) {
        // 占位画布（无产物态——与摘要表空表同口径）。
        QPainter placeholder(&canvas);
        placeholder.setPen(Qt::gray);
        placeholder.drawRect(0, 0, width - 1, height - 1);
        placeholder.drawText(canvas.rect(), Qt::AlignCenter,
                             tr("暂无点云数据——请先运行工作空间采样"));
        m_wsCanvasLabel->setPixmap(canvas);
        return;
    }
    const WorkspaceSampleRun& run = *m_session.lastWorkspaceRun;
    const auto plane = static_cast<WorkspaceProjectionPlane>(
        m_wsProjectionPlaneBox != nullptr ? m_wsProjectionPlaneBox->currentIndex()
                                          : 0);
    const WorkspacePointCloudRenderData data = buildWorkspacePointCloud(run);
    const std::vector<std::pair<double, double>> projected =
        projectWorkspacePoints(data, plane);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::lightGray);
    painter.drawRect(0, 0, width - 1, height - 1);

    // 值域→像素映射（基座系 m 实测极值——data.positionMin/Max 值域
    // 自检面；退化值域（min==max，如单点云）以 ±0.05 m 邻域兜底避免
    // 除零——纯呈现几何边界，不触权威值）。边距 32 px 留轴标注。
    const double marginPx = 32.0;
    const double lo[3] = {data.positionMin[0], data.positionMin[1],
                          data.positionMin[2]};
    const double hi[3] = {data.positionMax[0], data.positionMax[1],
                          data.positionMax[2]};
    // 投影面→(横轴, 纵轴) 分量索引（XY→(0,1)/XZ→(0,2)/YZ→(1,2)——与
    // projectWorkspacePoints 的舍轴语义对位，仅用于轴标注）。
    const int axisPair[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    const int axisH = axisPair[static_cast<int>(plane)][0];
    const int axisV = axisPair[static_cast<int>(plane)][1];
    auto mapH = [&](double v) {
        const double lo_v = std::min(lo[axisH], hi[axisH] - 0.05);
        const double hi_v = std::max(hi[axisH], lo_v + 0.05);
        return marginPx
               + (v - lo_v) / (hi_v - lo_v) * (width - 2.0 * marginPx);
    };
    auto mapV = [&](double v) {
        const double lo_v = std::min(lo[axisV], hi[axisV] - 0.05);
        const double hi_v = std::max(hi[axisV], lo_v + 0.05);
        // 纵轴翻转（数学坐标向上、画布坐标向下）。
        return (height - marginPx)
               - (v - lo_v) / (hi_v - lo_v) * (height - 2.0 * marginPx);
    };

    // 逐点绘制（点序＝results 全序——确定性呈现；颜色按 SampleState
    // 五值类别直投——图例与摘要表同源计数，零业务判定）。
    for (std::size_t i = 0; i < projected.size(); ++i) {
        const WorkspacePointCloudPoint& p = data.points[i];
        switch (p.state) {
            case SampleState::Reached:
                painter.setPen(QColor(0, 144, 0));       // 可达＝绿
                break;
            case SampleState::Unreachable:
                painter.setPen(QColor(200, 0, 0));       // 不可达＝红
                break;
            case SampleState::DataInsufficient:
                painter.setPen(QColor(216, 120, 0));     // 数据不足＝橙
                break;
            default:
                painter.setPen(QColor(128, 128, 128));   // 未运行/不适用＝灰
                break;
        }
        const QPointF pixel(mapH(projected[i].first), mapV(projected[i].second));
        painter.drawPoint(pixel);
        m_wsPickPixels.emplace_back(pixel, p.sampleIndex);
        bool selected = false;
        const auto index = m_wsSampleIndex->text().toULongLong(&selected);
        if (selected && index == p.sampleIndex) {
            painter.setPen(QPen(Qt::blue, 2));
            painter.drawEllipse(pixel, 6, 6); // 6px蓝圈只表明选中，不改变状态色。
        }
    }

    // 轴标注＋图例（坐标恒基座系 {B} 单位 m——KIN-09 投影语义；图例
    // 计数与摘要表同源——一处真相）。
    painter.setPen(Qt::black);
    const QString axisNames[3] = {tr("X"), tr("Y"), tr("Z")};
    painter.drawText(width - marginPx, height - 8,
                     tr("%1 (m)").arg(axisNames[axisH]));
    painter.drawText(4, marginPx - 8, tr("%1 (m)").arg(axisNames[axisV]));
    painter.drawText(marginPx, 16,
                     tr("图例：绿=可达 红=不可达 橙=数据不足 灰=未运行"
                        "（%1 点）")
                         .arg(static_cast<qlonglong>(projected.size())));
    painter.end();
    m_wsCanvasLabel->setPixmap(canvas);
}

void KinematicsPanelWidget::onWsProjectionPlaneChanged(int index)
{
    // 投影面切换＝画布重绘（纯呈现态——零采样重算零修订）。
    Q_UNUSED(index);
    refreshWsProjectionCanvas();
}

bool KinematicsPanelWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_wsCanvasLabel ||
        (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)) {
        return QWidget::eventFilter(watched, event);
    }
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() != Qt::LeftButton || m_wsCanvasLabel->pixmap().isNull()) { return false; }
    // QLabel居中显示固定像素画布；先扣除居中留白，再以8px半径命中。
    // 距离相同取域全序中先出现者，保证重叠点选择确定性。
    const auto size = m_wsCanvasLabel->pixmap().deviceIndependentSize();
    const QPointF local = mouse->position() - QPointF(
        (m_wsCanvasLabel->width()-size.width())/2,
        (m_wsCanvasLabel->height()-size.height())/2);
    double bestDistance = 64.0; // 8px容差平方：便于细散点点击，无物理单位。
    std::optional<std::uint64_t> picked;
    for (const auto& item : m_wsPickPixels) {
        const auto delta = item.first-local;
        const double d = delta.x()*delta.x()+delta.y()*delta.y();
        if (d < bestDistance) { bestDistance=d; picked=item.second; }
    }
    if (!picked) { return false; } // 空白不改已有选择和会话姿态。
    m_wsSampleIndex->setText(QString::number(*picked));
    if (event->type() == QEvent::MouseButtonDblClick) { onWsWritebackClicked(); }
    return true;
}

void KinematicsPanelWidget::refreshVisualization()
{
    if (!m_services.previewSink || !m_services.modelView) { return; }
    ui::View3DPreviewUpdate update;
    std::vector<core::ObjectId> objects;
    const auto worldTbase = m_services.modelView->worldToBase();
    // 数据源仍为会话权威产物，世界变换只发生在呈现边界，SI真值不改。
    if (m_session.lastWorkspaceRun) {
        ui::View3DSampleGrid grid;
        const auto cloud = buildWorkspacePointCloud(*m_session.lastWorkspaceRun);
        for (const auto& p : cloud.points) {
            grid.samples.push_back(runtime::transformBasePointToWorld(worldTbase,p.position));
            grid.sampleIndices.push_back(p.sampleIndex);
            grid.cellStates.push_back(p.state==SampleState::Reached ? ui::View3DCellState::Good
                : p.state==SampleState::Unreachable ? ui::View3DCellState::Failed
                : p.state==SampleState::DataInsufficient ? ui::View3DCellState::Weak
                : ui::View3DCellState::NotSampled);
        }
        bool valid=false;
        const auto selected=m_wsSampleIndex->text().toULongLong(&valid);
        if(valid) { grid.highlightedSampleIndex=selected; }
        update.sampleGrid=std::move(grid);
    }
    try {
        // 四类诊断都消费域组装器；此处只投坐标/标签，不重新比较阈值或排序。
        if (m_services.batchRender) {
            const auto batch=m_services.batchRender();
            if (batch) {
                for(const auto& p:batch->failurePoints.points) if(p.targetInBase) {
                    update.frameMarkers.push_back({"失败点 "+p.pointOid.toCanonical(), "WORLD",
                        runtime::transformBasePointToWorld(worldTbase,p.targetInBase->P())});
                }
                for(const auto& z:batch->weakZones.zones) { objects.push_back(z.jointObject); }
                for(const auto& p:batch->collisionPairs) { objects.push_back(p.objectA); objects.push_back(p.objectB); }
            }
        }
        if(m_session.lastSessionSolutionSetView) {
            std::vector<core::ObjectId> joints;
            for(const auto& j:m_services.modelView->model().chain().joints) {
                if(j.type!=runtime::JointType::Fixed) { joints.push_back(j.objectId); }
            }
            const auto thresholds=m_services.weakThresholds ? m_services.weakThresholds() : WeakZoneThresholds{};
            const auto data=assembleSolutionSetRenderData(*m_session.lastSessionSolutionSetView,
                {}, {}, joints, thresholds);
            const auto tcp=resolveSessionTcp(m_services);
            const auto& solutions=m_session.lastSessionSolutionSetView->sorted();
            auto mark=[&](std::size_t index, const std::string& label) {
                if(!tcp.ok || !m_services.fkEvaluator || index>=solutions.size()) { return; }
                const auto pose=m_services.fkEvaluator->evaluate(*m_services.modelView,tcp.tcp,solutions[index].q);
                if(pose.ok()) { update.frameMarkers.push_back({label,"WORLD",pose.get().tcpInWorld.P()}); }
            };
            for(const auto& z:data.weakZones.zones) {
                objects.push_back(z.jointObject);
                if(z.solutionIndex) { mark(*z.solutionIndex,"薄弱区"); }
            }
            if(data.worstJointMargin) {
                const auto& worst=*data.worstJointMargin;
                objects.push_back(worst.jointObject);
                mark(worst.solutionIndex,"最差关节裕量 "+std::to_string(worst.minimumJointMargin));
            }
            for(const auto& p:data.collisionPairs) { objects.push_back(p.objectA); objects.push_back(p.objectB); }
            // 选择行仅定位现有解；检查器与三维共享规范rank，零第二套排序。
            const auto selected=m_solutionTable->currentIndex();
            if(selected.isValid()) {
                const auto rows=solutionRowsFor(*m_session.lastSessionSolutionSetView,
                    m_usableOnly->isChecked(),dofJointTypes(m_services.modelView->model()),m_session.displayUnits);
                if(static_cast<std::size_t>(selected.row())<rows.size()) { mark(rows[selected.row()].rank,"当前候选"); }
            }
        }
        if(!m_services.previewSink(update,objects)) { showStatusText(tr("三维显示不可用：宿主场景尚未就绪")); }
    } catch(const std::exception& e) {
        showStatusText(tr("三维数据投放失败：%1").arg(QString::fromUtf8(e.what())));
    }
}

void KinematicsPanelWidget::onWsExportPngClicked()
{
    // 画布 PNG 编码（QPixmap→PNG 字节缓冲——呈现面编码；字节经
    // exportWorkspaceProjectionPng 编排写出——io write 以 string_view
    // 承载二进制安全）。编码失败（空设备）＝就地反馈不写出。
    const QPixmap canvas = m_wsCanvasLabel != nullptr && !m_wsCanvasLabel->pixmap().isNull()
                               ? m_wsCanvasLabel->pixmap()
                               : QPixmap();
    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    if (canvas.isNull() || !canvas.save(&buffer, "PNG")) {
        showStatusText(tr("PNG 导出不可用：画布编码失败——原状态保留"));
        return;
    }
    buffer.close();
    showStatusText(QString::fromStdString(exportWorkspaceProjectionPng(
        m_services, m_session, "kin-workspace-projection.png",
        std::string(pngBytes.constData(), static_cast<std::size_t>(pngBytes.size())))));
}

void KinematicsPanelWidget::onSolutionSelectionChanged()
{
    // L-K1 检查器联动（选中→SolutionRef→检查器行集现取）。
    if (!m_session.lastSessionSolutionSetView || m_inspectorTable == nullptr) {
        return;
    }
    // 逐轴类型真值缺失（模型视图不在场——与 refreshResultsPane 同防御
    // 口径）＝联动数据不可投影，如实不联动。
    if (m_services.modelView == nullptr) {
        return;
    }
    const QModelIndex idx = m_solutionTable->currentIndex();
    if (!idx.isValid()) {
        return;
    }
    const auto renderRow = static_cast<std::size_t>(idx.row());
    const auto rows = solutionRowsFor(*m_session.lastSessionSolutionSetView,
                                      m_usableOnly->isChecked(),
                                      dofJointTypes(m_services.modelView->model()),
                                      m_session.displayUnits);
    if (renderRow >= rows.size()) {
        return;
    }
    const SolutionRef ref{rows[renderRow].rank};
    renderRows(m_inspectorTable,
               solutionInspectorRows(*m_session.lastSessionSolutionSetView, ref,
                                     m_session.displayUnits));
    refreshVisualization();
}

void KinematicsPanelWidget::onSolutionDoubleClicked(int row, int column)
{
    // L-K4 双击候选→会话姿态回写（KIN-06：只写会话态——零修订、零失效；
    // 回写值经域设施 writebackOf 从解集视图产出）。
    Q_UNUSED(column);
    if (!m_session.lastSessionSolutionSetView || m_services.sessionPose == nullptr) {
        return;
    }
    // 逐轴类型真值缺失（与 refreshResultsPane 同防御口径）＝行集不可
    // 投影，如实不回写。
    if (m_services.modelView == nullptr) {
        return;
    }
    const auto rows = solutionRowsFor(*m_session.lastSessionSolutionSetView,
                                      m_usableOnly->isChecked(),
                                      dofJointTypes(m_services.modelView->model()),
                                      m_session.displayUnits);
    if (row < 0 || static_cast<std::size_t>(row) >= rows.size()) {
        return;
    }
    // 视图稳定序取解→回写值（Render.hpp writebackOf——域设施唯一产出点）。
    const SolutionRef ref{rows[static_cast<std::size_t>(row)].rank};
    const SolutionSetView& ordered = m_session.lastSessionSolutionSetView->sorted();
    if (ref.solutionIndex >= ordered.size()) {
        return;
    }
    showStatusText(QString::fromStdString(
        applyPoseWriteback(m_services.sessionPose,
                           writebackOf(ordered[ref.solutionIndex]))));
    refreshPosePane();
}

// =====================================================================
// KinematicsConfigPanel——求解配置高级面板（UX-04）
// =====================================================================

KinematicsConfigPanel::KinematicsConfigPanel(KinPanelServices services,
                                             KinModuleSessionState& session,
                                             QWidget* parent)
    : QWidget(parent)
    , m_services(std::move(services))
    , m_session(session)
{
    auto* layout = new QVBoxLayout(this);

    // ---- 初值策略（域枚举字段——三值词表 §5.3；表单公共件数值半区之外
    //      的域自渲染位）。
    m_strategyBox = new QComboBox(this);
    m_strategyBox->addItem(tr("ReferenceQ（参考构型）"));
    m_strategyBox->addItem(tr("SeededRandom（种子随机）"));
    m_strategyBox->addItem(tr("JointGrid（关节网格）"));
    connect(m_strategyBox, &QComboBox::currentIndexChanged, this,
            &KinematicsConfigPanel::onStrategyChanged);
    layout->addWidget(new QLabel(tr("初值策略"), this));
    layout->addWidget(m_strategyBox);

    // ---- 数值字段编辑（ui 表单公共件——UI-T08 对端；字段 spec 装配期
    //      给定键/标签/量纲/约束——域语义，ui 宿主规则）。
    const auto len = core::UnitToken::find("m").value();
    const auto rad = core::UnitToken::find("rad").value();
    const auto dim = core::UnitToken::find("1").value();
    std::vector<ui::QuantityFieldSpec> specs;
    specs.push_back(ui::makeQuantityFieldSpec(
        "initial-values-count", tr("多初值数量").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 1024.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "iteration-limit", tr("迭代上限").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 100000.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "position-residual-tolerance", tr("位置容差").toStdString(),
        core::QuantityKind::Length, len, len, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "orientation-residual-tolerance", tr("姿态容差").toStdString(),
        core::QuantityKind::Angle, rad, rad, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "ik-dedup-threshold-per-axis", tr("去重阈值（逐轴）").toStdString(),
        core::QuantityKind::Angle, rad, rad, ui::QuantityBounds{1e-9, 1.0}, false));
    specs.push_back(ui::makeQuantityFieldSpec(
        "region-budget-seed", tr("采样种子").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 9007199254740992.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "region-threads", tr("采样线程数").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 64.0}, true));
    specs.push_back(ui::makeQuantityFieldSpec(
        "seed", tr("随机种子").toStdString(),
        core::QuantityKind::Dimensionless, dim, dim,
        ui::QuantityBounds{1.0, 9007199254740992.0}, true));
    m_paramModel = std::make_unique<ui::ParamEditModel>(std::move(specs));

    // F04：公共件承载解析/单位/粘贴，整体确认包含策略；void数值出口
    // 无法反馈持久化失败，因此不让它提前清空草稿。对象仍由Qt父子链持有。
    m_paramTable = ui::createParamTablePanel(*m_paramModel, nullptr,
                                            ui::ParamTablePanelOptions{}, this);
    for (const char* name : {"ird_param_apply", "ird_param_cancel",
                             "ird_param_confirm_region", "ird_param_confirm_text",
                             "ird_param_confirm_yes", "ird_param_confirm_no"}) {
        auto* original = m_paramTable->findChild<QWidget*>(QString::fromLatin1(name));
        if (original != nullptr) {
            original->hide();
            original->setObjectName(QString::fromLatin1(name) + "_numeric_only");
        }
    }
    layout->addWidget(m_paramTable);
    m_confirmRegion = new QWidget(this);
    m_confirmRegion->setObjectName("ird_param_confirm_region");
    auto* confirmation = new QVBoxLayout(m_confirmRegion);
    m_confirmText = new QLabel(m_confirmRegion);
    m_confirmText->setObjectName("ird_param_confirm_text");
    m_confirmText->setWordWrap(true);
    m_confirmText->setTextFormat(Qt::PlainText);
    confirmation->addWidget(m_confirmText);
    auto* yes = new QPushButton(tr("确认保存"), m_confirmRegion);
    yes->setObjectName("ird_param_confirm_yes");
    auto* no = new QPushButton(tr("再改改"), m_confirmRegion);
    no->setObjectName("ird_param_confirm_no");
    confirmation->addWidget(yes);
    confirmation->addWidget(no);
    m_confirmRegion->hide();
    layout->addWidget(m_confirmRegion);
    auto* actions = new QHBoxLayout;
    auto* apply = new QPushButton(tr("保存配置…"), this);
    apply->setObjectName("ird_param_apply");
    apply->setEnabled(static_cast<bool>(m_services.configPersist));
    if (!m_services.configPersist) {
        apply->setToolTip(tr("用户级持久化通道未装配，无法保存；仍可编辑或取消恢复"));
    }
    auto* cancel = new QPushButton(tr("取消恢复"), this);
    cancel->setObjectName("ird_param_cancel");
    actions->addWidget(apply);
    actions->addWidget(cancel);
    layout->addLayout(actions);
    connect(apply, &QPushButton::clicked, this, &KinematicsConfigPanel::prepareConfigurationSave);
    connect(yes, &QPushButton::clicked, this, &KinematicsConfigPanel::confirmConfigurationSave);
    connect(no, &QPushButton::clicked, this, [this]() {
        // 再改改只撤回授权，保留完整草稿；取消恢复则还原整个配置。
        m_confirmationBaseline.reset();
        m_confirmRegion->hide();
    });
    connect(cancel, &QPushButton::clicked, this, [this]() {
        refreshAll();
        showStatusText(tr("已取消配置修改，恢复保存基线"));
    });

    // ---- 依赖提示行（L-K7 纯提示——不自动重算）。
    m_hintTable = makeTable({tr("提示"), tr("说明")}, this);
    layout->addWidget(m_hintTable);

    // ---- 策略只读摘要（无碰撞开关、无判定阈值——UX-08；文本由宿主从
    //      policy 投影，缺省如实说明）。
    m_policyTable = makeTable({tr("策略（只读）"), tr("值")}, this);
    layout->addWidget(m_policyTable);

    m_statusLine = new QLabel(tr("配置面板就绪"), this);
    layout->addWidget(m_statusLine);
    refreshAll();
}

KinematicsConfigPanel::~KinematicsConfigPanel() = default;

void KinematicsConfigPanel::refreshAll()
{
    // 基线注入（权威值来自会话态——表单零缓存基线）。
    const AnalysisConfiguration& c = m_session.savedConfig;
    m_paramModel->setBaseline("initial-values-count",
                              static_cast<double>(c.initialValuesCount));
    m_paramModel->setBaseline("iteration-limit",
                              static_cast<double>(c.iterationLimit));
    m_paramModel->setBaseline("position-residual-tolerance",
                              c.positionResidualTolerance);
    m_paramModel->setBaseline("orientation-residual-tolerance",
                              c.orientationResidualTolerance);
    m_paramModel->setBaseline("ik-dedup-threshold-per-axis",
                              c.ikDedupThresholdPerAxis);
    m_paramModel->setBaseline("region-budget-seed",
                              static_cast<double>(c.regionBudget.seed));
    m_paramModel->setBaseline("region-threads",
                              static_cast<double>(c.regionBudget.threadCount));
    m_paramModel->setBaseline("seed", static_cast<double>(c.seed));
    const QSignalBlocker blocker(m_strategyBox);
    m_draftStrategy = c.initialStrategy;
    m_strategyBox->setCurrentIndex(static_cast<int>(c.initialStrategy));
    ui::refreshParamTablePanel(m_paramTable);
    m_confirmationBaseline.reset();
    m_confirmationChanges.clear();
    m_confirmRegion->hide();

    // 策略只读摘要（缺省文本＝如实说明——宿主可经装配注入替换）。
    fillSingleColumn(m_policyTable,
                     {tr("碰撞策略（只读）：策略会话在场与启用事实由 policy 承载"),
                      tr("判定阈值（只读）：阈值唯一来源 policy JointThresholds")});
}

QString KinematicsConfigPanel::lastStatusText() const
{
    return m_statusLine != nullptr ? m_statusLine->text() : QString();
}

void KinematicsConfigPanel::showStatusText(const QString& text)
{
    if (m_statusLine != nullptr) {
        m_statusLine->setText(text);
    }
}

void KinematicsConfigPanel::onStrategyChanged(int index)
{
    // 域枚举字段编辑（index 即 InitialValueStrategy 枚举值——§5.3 三值词
    // 表；非法 index 由 QComboBox 值域排除）。编辑即草稿（不自动保存——
    // 保存经表单确认流，L-K7）。
    if (index < 0) {
        return;
    }
    m_draftStrategy = static_cast<InitialValueStrategy>(index);
}

std::vector<std::pair<std::string, double>> KinematicsConfigPanel::configurationChanges() const
{
    // 注册序使确认和提交稳定一致；枚举并入同一批次，支持单独修改策略。
    std::vector<std::pair<std::string, double>> changes;
    for (const auto& change : m_paramModel->pendingChanges()) {
        changes.emplace_back(change.key, change.newSi);
    }
    if (m_draftStrategy != m_session.savedConfig.initialStrategy) {
        changes.emplace_back("initial-strategy", static_cast<double>(m_draftStrategy));
    }
    return changes;
}

void KinematicsConfigPanel::prepareConfigurationSave()
{
    // 有错误即整批拒绝，避免只保存配置的合法部分。旧授权每次都撤回。
    m_confirmationBaseline.reset();
    m_confirmRegion->hide();
    if (m_paramModel->hasErrors()) {
        const auto key = m_paramModel->firstErrorKey().value();
        showStatusText(tr("配置未保存：%1：%2").arg(QString::fromStdString(key),
            QString::fromStdString(m_paramModel->statusText(key))));
        return;
    }
    const auto changes = configurationChanges();
    if (changes.empty()) {
        showStatusText(tr("配置没有修改"));
        return;
    }
    QStringList details;
    for (const auto& change : m_paramModel->pendingChanges()) {
        const auto& spec = m_paramModel->spec(change.key);
        const auto unit = m_paramModel->displayUnit(change.key);
        const QString before = change.oldSi ? QString::fromStdString(
            ui::formatFieldValueText(*change.oldSi, spec.siUnit, unit)) : tr("未设");
        details << tr("%1：%2 → %3").arg(QString::fromStdString(spec.label), before,
            QString::fromStdString(ui::formatFieldValueText(change.newSi, spec.siUnit, unit)));
    }
    if (m_draftStrategy != m_session.savedConfig.initialStrategy) {
        details << tr("初值策略：%1 → %2").arg(
            m_strategyBox->itemText(static_cast<int>(m_session.savedConfig.initialStrategy)),
            m_strategyBox->currentText());
    }
    m_confirmationChanges = changes;
    m_confirmationBaseline = m_session.savedConfig;
    m_confirmText->setText(tr("请确认整个配置的修改：\n%1").arg(details.join("\n")));
    m_confirmRegion->show();
}

void KinematicsConfigPanel::confirmConfigurationSave()
{
    // UX-07：只授权展示过的修改。新基线或再编辑都要求重新确认，零写入。
    if (!m_confirmationBaseline || m_paramModel->hasErrors()
        || !(*m_confirmationBaseline == m_session.savedConfig)
        || m_confirmationChanges != configurationChanges()) {
        m_confirmationBaseline.reset();
        m_confirmRegion->hide();
        showStatusText(tr("配置或保存基线已变化，请重新点击保存并确认"));
        return;
    }
    const auto result = applyConfigurationEdit(m_session, *m_confirmationBaseline,
        m_confirmationChanges, m_services.configPersist);
    m_confirmationBaseline.reset();
    m_confirmRegion->hide();
    if (result.ok) {
        refreshAll();
        // 仅真实保存成功才前移表单基线并呈现依赖提示；不触发重算。
        m_hintTable->setRowCount(static_cast<int>(result.hintRows.size()));
        for (int row = 0; row < m_hintTable->rowCount(); ++row) {
            const auto& hint = result.hintRows[static_cast<std::size_t>(row)];
            m_hintTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(hint.label)));
            m_hintTable->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(hint.displayText)));
        }
    }
    // 失败不刷新草稿，用户无需重输即可重试；错误原因在状态行可见。
    showStatusText(QString::fromStdString(result.ok ? result.statusText : result.reason));
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
