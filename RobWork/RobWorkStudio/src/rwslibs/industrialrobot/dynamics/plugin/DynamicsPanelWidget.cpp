/**
 * @file   DynamicsPanelWidget.cpp
 * @brief  dynamics 工作流页＋曲线视图主面板的实现翻译单元——控件接线
 *         与五命令呈现流（全部业务语义经模型层 L-D1~L-D6 与服务缝；
 *         本 TU 零动力学计算——卡 §9.5 红线，契约测试词表扫描钉住）。
 *
 * 设计依据：DynamicsPanelWidget.hpp 文件头；键族口径见
 * DynPanelCommandCatalog.hpp（§3.5——值归宿主文案资源，本域零文案值）。
 */

#include "DynamicsPanelWidget.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

#include "DynPanelCommandCatalog.hpp" // dynDomainCommands（命令描述符目录）
#include "DynPanelModule.hpp"         // DynPanelModule（缝＋会话态持有者——
                                      //   完整类型）

namespace sdurws::ird::dynamics {

namespace {

/// 面板页标题键（§3.5 键族 plugin.<id>.panel.<page>.title——键在单元卡
/// §9.5 呈现键表登记，值归宿主文案资源；UX-02：键本身不进用户文本，
/// 经 L-D6 解析呈现）。
constexpr const char* kWorkflowPageKey = "plugin.dynamics.panel.workflow.title";
constexpr const char* kCurvesPageKey = "plugin.dynamics.panel.curves.title";

/// 判定 token→工程用语（core::EngineeringStatus 词表的呈现映射——
/// 值语义：无判定/数据不足等七态素材如实呈现，不粉饰；token 本体是
/// core 词表（§4.7 小写连字符），此处只做呈现映射不造新词）。
QString verdictText(core::EngineeringStatus verdict)
{
    switch (verdict) {
        case core::EngineeringStatus::Feasible:
            return QStringLiteral("正式判定：可行");
        case core::EngineeringStatus::EngineeringInfeasible:
            return QStringLiteral("正式判定：工程不可行");
        case core::EngineeringStatus::DataInsufficient:
            return QStringLiteral("正式判定：数据不足");
        case core::EngineeringStatus::NotApplicable:
            return QStringLiteral("正式判定：无（尚未评估）");
    }
    return QStringLiteral("正式判定：无（尚未评估）");
}

}  // namespace

DynamicsPanelWidget::DynamicsPanelWidget(DynPanelModule& module,
                                         QWidget* parent)
    : QWidget(parent)
    , m_module(module)
{
    // ---- 顶层：两页合一 Tab 容器（工作流页＋曲线视图——宿主挂位
    //      面板的最小自足呈现）。 -----------------------------------
    auto* tabs = new QTabWidget(this);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);

    // ================= 工作流页 =====================================
    auto* workflowPage = new QWidget(tabs);
    auto* workflowLayout = new QVBoxLayout(workflowPage);

    // 就绪投影区（L-D1——UX-10 七态素材：判定/输入完整/在途任务/缺项
    // 清单一处呈现；换行标签承载多行文本）。
    m_readinessLabel = new QLabel(workflowPage);
    m_readinessLabel->setWordWrap(true);
    workflowLayout->addWidget(m_readinessLabel);

    // 命令区（§9.5 五命令按钮——文案经 DynCommandDescriptor.titleKey
    // 解析；点击流见 submitCommand）。
    auto* commandBox = new QGroupBox(QStringLiteral("动力学命令"), workflowPage);
    auto* commandGrid = new QGridLayout(commandBox);
    int commandSlot = 0;
    for (const DynCommandDescriptor& command : dynDomainCommands()) {
        auto* button = new QPushButton(panelText(command.titleKey), commandBox);
        // 点击→命令受理流（lambda 捕获 token 值拷贝——词表值随按钮
        // 固定，零二次解析）。
        const std::string token = command.token;
        connect(button, &QPushButton::clicked, this,
                [this, token]() { submitCommand(token); });
        // 可用性门控初始刷新（refreshFromSession 统一重刷——此处先按
        // 缝现状置位；空缝＝按可用呈现，点击时如实反馈）。
        commandGrid->addWidget(button, commandSlot / 2, commandSlot % 2);
        m_commandButtons.push_back(button);
        ++commandSlot;
    }
    workflowLayout->addWidget(commandBox);

    // 最近命令清单（L-D3 受理记录的只读呈现——工程用语行文本）。
    m_recentEdit = new QPlainTextEdit(workflowPage);
    m_recentEdit->setReadOnly(true);
    m_recentEdit->setMaximumHeight(96);
    m_recentEdit->setPlaceholderText(QStringLiteral("最近命令（会话级，零修订）"));
    workflowLayout->addWidget(m_recentEdit);

    // 回放区（dynamics.replay-at 呈现落点——时刻输入＋逐关节读数；
    // KIN-06 零修订：只驱动会话姿态素材呈现，不触模型/修订）。
    auto* replayBox = new QGroupBox(QStringLiteral("三维轨迹时刻回放"), workflowPage);
    auto* replayLayout = new QHBoxLayout(replayBox);
    m_replayTimeS = new QDoubleSpinBox(replayBox);
    m_replayTimeS->setDecimals(3);
    m_replayTimeS->setRange(-1.0e6, 1.0e6);   // 单位 s；范围由查询空态兜底
    m_replayTimeS->setSuffix(QStringLiteral(" s"));
    m_replayTimeS->setValue(0.0);
    replayLayout->addWidget(m_replayTimeS);
    auto* replayButton = new QPushButton(QStringLiteral("按时刻查询"), replayBox);
    connect(replayButton, &QPushButton::clicked, this,
            &DynamicsPanelWidget::runReplayAt);
    replayLayout->addWidget(replayButton);
    m_replayReading = new QLabel(replayBox);
    m_replayReading->setWordWrap(true);
    replayLayout->addWidget(m_replayReading, 1);
    workflowLayout->addWidget(replayBox);
    workflowLayout->addStretch(1);

    // ================= 曲线视图页 ===================================
    auto* curvesPage = new QWidget(tabs);
    auto* curvesLayout = new QVBoxLayout(curvesPage);

    // 关节/通道选择行（曲线联动入口——数据源＝曲线投影缝的行集）。
    auto* controlsRow = new QHBoxLayout();
    m_jointCombo = new QComboBox(curvesPage);
    m_channelCombo = new QComboBox(curvesPage);
    controlsRow->addWidget(m_jointCombo);
    controlsRow->addWidget(m_channelCombo, 1);
    auto* locateButton = new QPushButton(QStringLiteral("峰值定位"), curvesPage);
    connect(locateButton, &QPushButton::clicked, this,
            &DynamicsPanelWidget::locatePeakAndJump);
    controlsRow->addWidget(locateButton);
    curvesLayout->addLayout(controlsRow);

    // 数据完整性标注（UX-10"数据不足"呈现——nonOkCount/完整性透传）。
    m_dataStatusLabel = new QLabel(curvesPage);
    m_dataStatusLabel->setWordWrap(true);
    curvesLayout->addWidget(m_dataStatusLabel);

    // 曲线视图（自绘单通道折线＋游标＋峰值标记——DynCurveChartView）。
    m_chart = new DynCurveChartView(curvesPage);
    curvesLayout->addWidget(m_chart, 1);

    // 通道下拉：五通道词表（DynChannelId 序＝JointCurves 字段序——
    // 键与量纲标签由模型层词表函数产出，零本地词表复制）。
    for (int channel = 0; channel < kDynChannelCount; ++channel) {
        const auto id = static_cast<DynChannelId>(channel);
        m_channelCombo->addItem(
            QString::fromStdString(dynChannelKey(id)) + "（"
                + QString::fromStdString(dynChannelUnit(id, DynJointType::Revolute))
                + "）",
            channel);
    }
    // 联动信号：关节/通道变更→重取快照刷新曲线（缝消费——零计算）。
    connect(m_jointCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshCurveView(); });
    connect(m_channelCombo, &QComboBox::currentIndexChanged, this,
            [this](int) {
                // 通道切换即视图通道位切换（清空快照等待重取——不沿用
                // 旧通道曲线冒充新通道）。
                const int channel = m_channelCombo->currentData().toInt();
                m_chart->setChannel(static_cast<DynChannelId>(channel));
                refreshCurveView();
            });

    tabs->addTab(workflowPage, panelText(kWorkflowPageKey));
    tabs->addTab(curvesPage, panelText(kCurvesPageKey));

    // 构造即整面刷新（装配层创建面板后立即呈现当前会话事实）。
    refreshFromSession();
}

QString DynamicsPanelWidget::panelText(const std::string& titleKey) const
{
    // L-D6 文案解析（缝解析＋键名兜底＋哈希守卫——UX-02 唯一入口）。
    return QString::fromStdString(
        resolvePanelText(m_module.services, titleKey));
}

void DynamicsPanelWidget::refreshFromSession()
{
    refreshReadiness();   // L-D1 就绪投影呈现
    rebuildJointCombo();  // 关节下拉按当前曲线投影行集重建
    refreshCurveView();   // 曲线快照刷新（选中关节/通道）

    // 命令按钮可用性门控刷新（统一按钮门控——权威在可用性缝；空缝＝
    // 全部按可用呈现，点击时经提交缝语义如实反馈）。
    if (m_module.services.commandAvailability) {
        const auto commands = dynDomainCommands();
        for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
            // 目录序与按钮序一致（构造期同循环建立——防御下标守卫）。
            if (i < commands.size()) {
                m_commandButtons[i]->setEnabled(
                    m_module.services.commandAvailability(commands[i].token));
            }
        }
    }
}

void DynamicsPanelWidget::refreshReadiness()
{
    // L-D1：会话事实→投影行→多行工程用语（UX-10 素材如实呈现：
    // 缺项清单逐键解析列出；在途任务/可写态补位——不判定不粉饰）。
    const DynReadinessRow row = readinessProjection(m_module.session);
    QStringList lines;
    lines << verdictText(row.verdict);
    lines << (row.inputComplete ? QStringLiteral("输入：完整")
                                : QStringLiteral("输入：不完整（待补齐）"));
    for (const std::string& missing : row.missingItemKeys) {
        lines << QStringLiteral("缺项：")
              + QString::fromStdString(resolvePanelText(m_module.services, missing));
    }
    lines << (row.hasActiveTask ? QStringLiteral("状态：计算中（在途任务存在）")
                                : QStringLiteral("状态：无在途任务"));
    lines << (m_module.session.writable
                  ? QStringLiteral("会话：可写")
                  : QStringLiteral("会话：只读（提交出口由可用性门控）"));
    m_readinessLabel->setText(lines.join('\n'));
}

void DynamicsPanelWidget::submitCommand(const std::string& commandToken)
{
    // L-D3：先经领域适配器受理（词表查表＋负载交叉校验——§10.7）；
    // 受理记录入会话缓冲（零修订逐条留痕）。
    CommandPayload payload;
    payload.commandToken = commandToken;  // 交叉校验位一致——受理路径
    const CommandOutcome outcome =
        applySessionCommand(m_module.session, commandToken, payload);

    if (!outcome.accepted) {
        // 不受理＝用户可见反馈（拒绝 token 是稳定语义词非内部标识——
        // §10.7 outcome 语义；如实呈现不静默）。
        m_recentEdit->appendPlainText(QStringLiteral("不受理：")
                                      + QString::fromStdString(commandToken)
                                      + QStringLiteral("（")
                                      + QString::fromStdString(outcome.rejectionToken)
                                      + QStringLiteral("）"));
        return;
    }

    // 受理成功→经提交缝转发宿主 CommandRegistry（O13：命令权威归 ui；
    // 空缝＝出口未装配——诚实反馈，不虚构提交成功）。
    if (m_module.services.commandSubmit) {
        m_module.services.commandSubmit(commandToken);
        m_recentEdit->appendPlainText(QStringLiteral("已受理并提交：")
                                      + QString::fromStdString(commandToken));
    } else {
        m_recentEdit->appendPlainText(QStringLiteral("已受理：")
                                      + QString::fromStdString(commandToken)
                                      + QStringLiteral("（命令出口未装配）"));
    }

    // 受理后的本地呈现效果（数据面消费——零计算）：
    //   show-curves→曲线视图刷新；其余命令的执行面归宿主通道（analyze
    //   提交即返、locate-peak/replay-at 由对应按钮流承载、export 归
    //   宿主导出通道——§9.5 表执行面注释）。
    if (commandToken == kCmdShowCurves) {
        refreshCurveView();
    }
}

void DynamicsPanelWidget::rebuildJointCombo()
{
    // 曲线投影缝空＝数据面未装配——下拉清空＋空态标注（不伪造关节数）。
    if (!m_module.services.curveSource) {
        m_jointCombo->clear();
        m_jointCombo->addItem(QStringLiteral("（曲线数据未装配）"), -1);
        m_dataStatusLabel->setText(QStringLiteral("曲线数据未装配（归档读取通道未接线）"));
        return;
    }
    // L-D2：行集驱动关节下拉（行序稳定——jointIndex 升序×通道序）。
    const std::vector<DynChannelRow> rows =
        curveChannelRows(m_module.services.curveSource());
    const int previousJoint = m_jointCombo->currentData().toInt();
    QSignalBlocker blocker(m_jointCombo);  // 重建期禁联动信号（避免中间态重入）
    m_jointCombo->clear();
    std::uint32_t lastJoint = 0;
    bool first = true;
    for (const DynChannelRow& row : rows) {
        if (first || row.jointIndex != lastJoint) {
            m_jointCombo->addItem(QStringLiteral("关节 %1").arg(row.jointIndex),
                                  static_cast<int>(row.jointIndex));
            lastJoint = row.jointIndex;
            first = false;
        }
    }
    if (m_jointCombo->count() == 0) {
        // 空投影（无 Ok 行）——Empty 显式语义：空态呈现，不伪造关节。
        m_jointCombo->addItem(QStringLiteral("（无数据）"), -1);
        m_dataStatusLabel->setText(QStringLiteral("无数据（投影无 Ok 行）"));
        return;
    }
    // 恢复先前选中（联动连续性——找不到则落首项）。
    const int restored = m_jointCombo->findData(previousJoint);
    if (restored >= 0) {
        m_jointCombo->setCurrentIndex(restored);
    }
}

void DynamicsPanelWidget::refreshCurveView()
{
    // 缝空＝未装配（rebuildJointCombo 已标注——此处只保视图空态）。
    if (!m_module.services.curveSource) {
        m_chart->clearCurve();
        return;
    }
    const int jointData = m_jointCombo->currentData().toInt();
    if (jointData < 0) {
        m_chart->clearCurve();  // 无数据空态——视图清空呈现
        return;
    }
    // L-D2 数据消费：投影→目标关节行→所选通道快照（直拷零重算）。
    const CurveProjection projection = m_module.services.curveSource();
    const std::uint32_t jointIndex = static_cast<std::uint32_t>(jointData);
    for (const JointCurves& joint : projection.joints) {
        if (joint.jointIndex != jointIndex) {
            continue;
        }
        const int channel = m_channelCombo->currentData().toInt();
        const auto id = static_cast<DynChannelId>(channel);
        m_chart->setCurve(jointIndex, joint.jointType, joint.t,
                          dynChannelValues(joint, id));
        // 数据完整性标注（UX-10"数据不足"素材——剔除计数与完整性
        // 透传，如实呈现不阻断）。
        QStringList status;
        status << QStringLiteral("数据点：%1").arg(
            static_cast<qulonglong>(dynChannelValues(joint, id).size()));
        if (joint.nonOkCount > 0) {
            status << QStringLiteral("（已剔除无效行 %1——数据不足标注）")
                          .arg(static_cast<qulonglong>(joint.nonOkCount));
        }
        if (projection.completeness != DynamicsValidity::Completeness::Complete) {
            status << QStringLiteral("（序列非完备）");
        }
        m_dataStatusLabel->setText(status.join(QStringLiteral("　")));
        return;
    }
    // 关节号在投影中不存在（下拉与投影错位——刷新竞态防御）：视图
    // 空态呈现，不伪造曲线。
    m_chart->clearCurve();
    m_dataStatusLabel->setText(QStringLiteral("所选关节无数据行"));
}

void DynamicsPanelWidget::runReplayAt()
{
    // 缝空＝回放数据未装配——诚实呈现，不虚构帧。
    if (!m_module.services.replaySource) {
        m_replayReading->setText(QStringLiteral("回放数据未装配（归档读取通道未接线）"));
        return;
    }
    const ReplayData data = m_module.services.replaySource();
    // L-D5：按时刻插值查表（域设施消费——UI 线程查表零计算；时刻单位 s）。
    const std::optional<ReplaySample> sample =
        replaySampleAt(data, m_replayTimeS->value());
    if (!sample.has_value()) {
        // 合法空态（越界不外推——§4.3 纪律；空数据集）：如实呈现。
        m_replayReading->setText(
            QStringLiteral("该时刻无回放数据（越界或空数据集——不外推）"));
        return;
    }
    // 逐关节读数行（工程用语＋量纲按型——关节型决定 q/q̇/τ 单位标签；
    // 零姿态计算：q 值即会话姿态驱动素材，FK 归 runtime——卡 §9.5）。
    QStringList lines;
    lines << (sample->exact ? QStringLiteral("精确命中")
                            : QStringLiteral("区间插值（呈现投影语义——不用于统计）"));
    lines << QStringLiteral("段：%1　t = %2 s")
                 .arg(sample->segmentIndex)
                 .arg(QString::number(sample->t, 'g', 6));
    for (const ReplayJointState& joint : sample->joints) {
        const bool prismatic = joint.jointType == DynJointType::Prismatic;
        lines << QStringLiteral("关节 %1：q = %2 %3，q̇ = %4 %5，τ = %6 %7，P = %8 W")
                     .arg(joint.jointIndex)
                     .arg(QString::number(joint.q, 'g', 6))
                     .arg(prismatic ? QStringLiteral("m") : QStringLiteral("rad"))
                     .arg(QString::number(joint.qd, 'g', 6))
                     .arg(prismatic ? QStringLiteral("m/s") : QStringLiteral("rad/s"))
                     .arg(QString::number(joint.generalizedForce, 'g', 6))
                     .arg(prismatic ? QStringLiteral("N") : QStringLiteral("N·m"))
                     .arg(QString::number(joint.mechanicalPower, 'g', 6));
    }
    if (!sample->completeAllJoints) {
        // 帧不完整＝该时刻存在非 Ok 行/缺行（§4.6 不截断不伪造——如实
        // 标注数据不足态，UX-10）。
        lines << QStringLiteral("（该帧关节不齐全——数据不足标注）");
    }
    m_replayReading->setText(lines.join('\n'));
    // 峰值标记与游标随查询时刻联动（曲线游标同步——呈现联动零计算）。
    m_chart->setCursorTimeS(sample->t);
}

void DynamicsPanelWidget::locatePeakAndJump()
{
    // L-D4：峰值定位（经缝——三态显式：未装配/无峰/命中）。
    const int jointData = m_jointCombo->currentData().toInt();
    if (jointData < 0) {
        m_dataStatusLabel->setText(QStringLiteral("峰值定位：无可用关节（先装配曲线数据）"));
        return;
    }
    std::string reason;
    // token 序取 0（力矩正向峰值——首通道定位；多 token 循环属增强面，
    // 本落位以首 token 承载链路，词表全序对账由数据面测试钉住）。
    const std::optional<DynPeakJump> jump =
        locatePeakJump(m_module.services, static_cast<std::uint32_t>(jointData),
                       0, &reason);
    if (!jump.has_value()) {
        m_dataStatusLabel->setText(
            reason == "not-assembled"
                ? QStringLiteral("峰值定位未装配（统计行集通道未接线）")
                : QStringLiteral("该关节无峰值记录"));
        return;
    }
    // 命中→曲线游标跳转峰值时刻＋标记（locate-peak 呈现落点；三维
    // 姿态同步素材＝tPeakS，宿主经回放查询链消费——零修订四常量）。
    m_chart->setPeakMarker(jump->tPeakS, jump->value);
    m_chart->setCursorTimeS(jump->tPeakS);
    m_dataStatusLabel->setText(
        QStringLiteral("峰值：t = %1 s，值 = %2（游标已跳转）")
            .arg(QString::number(jump->tPeakS, 'g', 6))
            .arg(QString::number(jump->value, 'g', 6)));
}

}  // namespace sdurws::ird::dynamics
