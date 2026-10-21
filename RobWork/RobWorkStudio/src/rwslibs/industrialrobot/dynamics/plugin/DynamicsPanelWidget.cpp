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
#include <algorithm>
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
std::string verdictKey(core::EngineeringStatus verdict)
{
    switch (verdict) {
        case core::EngineeringStatus::Feasible:
            return "plugin.dynamics.verdict.feasible";
        case core::EngineeringStatus::EngineeringInfeasible:
            return "plugin.dynamics.verdict.infeasible";
        case core::EngineeringStatus::DataInsufficient:
            return "plugin.dynamics.verdict.insufficient";
        case core::EngineeringStatus::NotApplicable:
            return "plugin.dynamics.verdict.none";
    }
    return "plugin.dynamics.verdict.none";
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
    m_tabs = tabs;
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
    auto* commandBox = new QGroupBox(panelText("plugin.dynamics.commands.title"), workflowPage);
    auto* commandGrid = new QGridLayout(commandBox);
    int commandSlot = 0;
    for (const DynCommandDescriptor& command : dynDomainCommands()) {
        auto* button = new QPushButton(panelText(command.titleKey), commandBox);
        button->setObjectName(QString::fromStdString(command.token));
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
    m_recentEdit->setPlaceholderText(panelText("plugin.dynamics.commands.recent"));
    workflowLayout->addWidget(m_recentEdit);

    // 回放区（dynamics.replay-at 呈现落点——时刻输入＋逐关节读数；
    // KIN-06 零修订：只驱动会话姿态素材呈现，不触模型/修订）。
    auto* replayBox = new QGroupBox(panelText("plugin.dynamics.replay.title"), workflowPage);
    auto* replayLayout = new QGridLayout(replayBox);
    m_replayTimeS = new QDoubleSpinBox(replayBox);
    m_replayTimeS->setObjectName("dyn_replay_time");
    m_replayTimeS->setDecimals(9); // 呈现精度，不作为峰值查询的数值输入。
    m_replayTimeS->setRange(-1.0e6, 1.0e6);   // 单位 s；范围由查询空态兜底
    m_replayTimeS->setSuffix(QStringLiteral(" s"));
    m_replayTimeS->setValue(0.0);
    replayLayout->addWidget(m_replayTimeS, 0, 0);
    auto* replayButton = new QPushButton(panelText("plugin.dynamics.replay.query"), replayBox);
    m_replayButton = replayButton;
    replayButton->setObjectName("dyn_replay");
    connect(replayButton, &QPushButton::clicked, this,
            &DynamicsPanelWidget::runReplayAt);
    replayLayout->addWidget(replayButton, 0, 1);
    m_replayReading = new QLabel(replayBox);
    m_replayReading->setObjectName("dyn_replay_reading");
    m_replayReading->setWordWrap(true);
    replayLayout->addWidget(m_replayReading, 1, 0, 1, 2);
    workflowLayout->addWidget(replayBox);
    workflowLayout->addStretch(1);

    // ================= 曲线视图页 ===================================
    auto* curvesPage = new QWidget(tabs);
    auto* curvesLayout = new QVBoxLayout(curvesPage);

    // 关节/通道选择行（曲线联动入口——数据源＝曲线投影缝的行集）。
    // 两行控件在窄 Dock 中保持可用，避免四个下拉/按钮挤在同一行。
    auto* controlsRow = new QGridLayout();
    m_jointCombo = new QComboBox(curvesPage);
    m_channelCombo = new QComboBox(curvesPage);
    m_peakTokenCombo = new QComboBox(curvesPage);
    m_jointCombo->setObjectName("dyn_joint");
    m_channelCombo->setObjectName("dyn_channel");
    m_peakTokenCombo->setObjectName("dyn_peak_channel");
    controlsRow->addWidget(m_jointCombo, 0, 0);
    controlsRow->addWidget(m_channelCombo, 0, 1);
    // 峰值 token 与 统计公共头的行序一致，允许定位力矩正/反峰、速度、
    // 加速度及功率正/反峰；不再把所有定位请求固定到 token 0。
    const char* peakLabels[] = {"force-positive", "force-negative", "velocity", "acceleration",
                                "power-positive", "power-negative"};
    for (int token = 0; token < kPeakTokenCount; ++token) {
        m_peakTokenCombo->addItem(panelText(std::string("plugin.dynamics.peak.") + peakLabels[token]), token);
    }
    controlsRow->addWidget(m_peakTokenCombo, 1, 0);
    auto* locateButton = new QPushButton(panelText("cmd.dynamics.locate-peak.title"), curvesPage);
    m_locateButton = locateButton;
    locateButton->setObjectName("dyn_locate_peak");
    connect(locateButton, &QPushButton::clicked, this,
            &DynamicsPanelWidget::locatePeakAndJump);
    controlsRow->addWidget(locateButton, 1, 1);
    curvesLayout->addLayout(controlsRow);

    // 数据完整性标注（UX-10"数据不足"呈现——nonOkCount/完整性透传）。
    m_dataStatusLabel = new QLabel(curvesPage);
    m_dataStatusLabel->setWordWrap(true);
    curvesLayout->addWidget(m_dataStatusLabel);

    // 曲线视图（自绘单通道折线＋游标＋峰值标记——DynCurveChartView）。
    m_chart = new DynCurveChartView(curvesPage);
    m_chart->setTextResolver([this](const std::string& key) { return panelText(key); });
    m_chart->setObjectName("dyn_chart");
    curvesLayout->addWidget(m_chart, 1);

    // 通道下拉：五通道词表（DynChannelId 序＝JointCurves 字段序——
    // 键与量纲标签由模型层词表函数产出，零本地词表复制）。
    for (int channel = 0; channel < kDynChannelCount; ++channel) {
        const auto id = static_cast<DynChannelId>(channel);
        m_channelCombo->addItem(
            panelText("plugin.dynamics.channel." + dynChannelKey(id)) + "（"
                + QString::fromStdString(dynChannelUnit(id, DynJointType::Revolute))
                + "）",
            channel);
    }
    m_channelCombo->setCurrentIndex(3); // 首屏显示广义力，便于观察动力学峰值。
    m_chart->setChannel(DynChannelId::GeneralizedForce);
    // 联动信号：关节/通道变更→重取快照刷新曲线（缝消费——零计算）。
    connect(m_jointCombo, &QComboBox::currentIndexChanged, this,
            [this](int) {
                m_chart->clearCurve(); // 换关节后旧标记不属于新对象。
                refreshCurveView();
            });
    connect(m_channelCombo, &QComboBox::currentIndexChanged, this,
            [this](int) {
                // 通道切换即视图通道位切换（清空快照等待重取——不沿用
                // 旧通道曲线冒充新通道）。
                const int channel = m_channelCombo->currentData().toInt();
                m_chart->setChannel(static_cast<DynChannelId>(channel));
                // q 没有统计峰值。其余通道只保留相应峰值，避免跨量纲标记。
                const int defaults[] = {-1, 2, 3, 0, 4};
                m_peakTokenCombo->setEnabled(channel != 0);
                if (channel > 0) {
                    const int token = m_peakTokenCombo->currentData().toInt();
                    const int tokenChannels[] = {3, 3, 1, 2, 4, 4};
                    if (tokenChannels[token] != channel) {
                        QSignalBlocker block(m_peakTokenCombo);
                        m_peakTokenCombo->setCurrentIndex(defaults[channel]);
                    }
                }
                refreshCurveView();
            });
    connect(m_peakTokenCombo, &QComboBox::currentIndexChanged, this,
            [this](int token) {
                // 选择峰值时主动切到同量纲曲线；六 token 对应四种曲线。
                const int channels[] = {3, 3, 1, 2, 4, 4};
                if (token >= 0 && token < kPeakTokenCount) {
                    m_channelCombo->setCurrentIndex(channels[token]);
                    m_chart->clearPeakMarker();
                }
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
    // 会话刷新可能切换了结果修订；峰值标记和“已同步”反馈均属于上次查询，
    // 不能无条件继承到新结果。刷新后由用户重新定位/回放，避免呈现过期成功。
    m_chart->clearCurve();
    m_replayReading->clear();
    refreshReadiness();   // L-D1 就绪投影呈现
    rebuildJointCombo();  // 关节下拉按当前曲线投影行集重建
    refreshCurveView();   // 曲线快照刷新（选中关节/通道）

    // 命令按钮可用性门控刷新（统一按钮门控——权威在可用性缝；空缝＝
    // 全部按可用呈现，点击时经提交缝语义如实反馈）。
    {
        const auto commands = dynDomainCommands();
        for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
            // 目录序与按钮序一致（构造期同循环建立——防御下标守卫）。
            if (i < commands.size()) {
                m_commandButtons[i]->setEnabled(
                    m_module.services.commandSubmit
                    && (!m_module.services.commandAvailability
                        || m_module.services.commandAvailability(commands[i].token)));
            }
        }
    }
    // 无结果的入口直接给出不可操作状态，避免用户每一步都点到空流程。
    const bool hasReplay = m_module.services.replaySource
                          && !m_module.services.replaySource().frames.empty()
                          && (!m_module.services.commandAvailability
                              || m_module.services.commandAvailability(std::string{kCmdReplayAt}));
    m_replayButton->setEnabled(hasReplay);
    m_replayTimeS->setEnabled(hasReplay);
    if (!hasReplay) { m_replayReading->setText(panelText("plugin.dynamics.replay.empty")); }
}

void DynamicsPanelWidget::refreshReadiness()
{
    // L-D1：会话事实→投影行→多行工程用语（UX-10 素材如实呈现：
    // 缺项清单逐键解析列出；在途任务/可写态补位——不判定不粉饰）。
    const DynReadinessRow row = readinessProjection(m_module.session);
    QStringList lines;
    lines << panelText(verdictKey(row.verdict));
    lines << (row.inputComplete ? panelText("plugin.dynamics.input.complete")
                                : panelText("plugin.dynamics.input.incomplete"));
    for (const std::string& missing : row.missingItemKeys) {
        lines << panelText("plugin.dynamics.input.missing")
              + QString::fromStdString(resolvePanelText(m_module.services, missing));
    }
    lines << (row.hasActiveTask ? panelText("plugin.dynamics.task.running")
                                : panelText("plugin.dynamics.task.idle"));
    lines << (m_module.session.writable
                  ? panelText("plugin.dynamics.session.writable")
                  : panelText("plugin.dynamics.session.readonly"));
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
        // 拒绝 token 留在会话记录供诊断；用户只见统一中文原因，避免机器词泄漏。
        m_recentEdit->appendPlainText(panelText("plugin.dynamics.command.rejected")
                                      + panelText("cmd." + commandToken + ".title")
                                      + panelText("plugin.dynamics.command.rejection-reason"));
        return;
    }

    // 受理成功→经提交缝转发宿主 CommandRegistry（O13：命令权威归 ui；
    // 空缝＝出口未装配——诚实反馈，不虚构提交成功）。
    if (m_module.services.commandSubmit) {
        m_module.services.commandSubmit(commandToken);
        m_recentEdit->appendPlainText(panelText("plugin.dynamics.command.submitted")
                                      + panelText("cmd." + commandToken + ".title"));
    } else {
        m_recentEdit->appendPlainText(panelText("plugin.dynamics.command.accepted")
                                      + panelText("cmd." + commandToken + ".title")
                                      + panelText("plugin.dynamics.command.unbound"));
    }

    // 受理后的本地呈现效果（数据面消费——零计算）：
    //   show-curves→曲线视图刷新；其余命令的执行面归宿主通道（analyze
    //   提交即返、locate-peak/replay-at 由对应按钮流承载、export 归
    //   宿主导出通道——§9.5 表执行面注释）。
    if (commandToken == kCmdShowCurves) {
        m_tabs->setCurrentIndex(1);
        refreshCurveView();
    } else if (commandToken == kCmdLocatePeak) {
        m_tabs->setCurrentIndex(1);
        locatePeakAndJump();
    } else if (commandToken == kCmdReplayAt) {
        runReplayAt();
    }
}

void DynamicsPanelWidget::rebuildJointCombo()
{
    // 曲线投影缝空＝数据面未装配——下拉清空＋空态标注（不伪造关节数）。
    if (!m_module.services.curveSource) {
        m_jointCombo->clear();
        m_jointCombo->addItem(panelText("plugin.dynamics.curves.unbound-choice"), -1);
        m_dataStatusLabel->setText(panelText("plugin.dynamics.curves.unbound"));
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
            m_jointCombo->addItem(panelText("plugin.dynamics.joint.label").arg(row.jointIndex + 1),
                                  static_cast<int>(row.jointIndex));
            lastJoint = row.jointIndex;
            first = false;
        }
    }
    if (m_jointCombo->count() == 0) {
        // 空投影（无 Ok 行）——Empty 显式语义：空态呈现，不伪造关节。
        m_jointCombo->addItem(panelText("plugin.dynamics.curves.empty-choice"), -1);
        m_dataStatusLabel->setText(panelText("plugin.dynamics.curves.empty"));
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
    refreshChannelLabels();
    m_chart->setChannelLabel(panelText("plugin.dynamics.channel."
        + dynChannelKey(static_cast<DynChannelId>(m_channelCombo->currentData().toInt()))));
    m_locateButton->setEnabled(m_module.services.peakLocate
        && m_jointCombo->currentData().toInt() >= 0
        && m_channelCombo->currentData().toInt() != 0
        && (!m_module.services.commandAvailability
            || m_module.services.commandAvailability(std::string{kCmdLocatePeak})));
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
        status << panelText("plugin.dynamics.curves.count").arg(
            static_cast<qulonglong>(dynChannelValues(joint, id).size()));
        if (joint.nonOkCount > 0) {
            status << panelText("plugin.dynamics.curves.invalid")
                          .arg(static_cast<qulonglong>(joint.nonOkCount));
        }
        if (projection.completeness != DynamicsValidity::Completeness::Complete) {
            status << panelText("plugin.dynamics.curves.partial");
        }
        m_dataStatusLabel->setText(status.join(QStringLiteral("　")));
        return;
    }
    // 关节号在投影中不存在（下拉与投影错位——刷新竞态防御）：视图
    // 空态呈现，不伪造曲线。
    m_chart->clearCurve();
    m_dataStatusLabel->setText(panelText("plugin.dynamics.curves.no-joint"));
}

void DynamicsPanelWidget::refreshChannelLabels()
{
    // 下拉和图轴采用同一关节型，移动关节不得显示 rad 或 N·m。
    DynJointType type = DynJointType::Revolute;
    if (m_module.services.curveSource) {
        for (const auto& joint : m_module.services.curveSource().joints) {
            if (int(joint.jointIndex) == m_jointCombo->currentData().toInt()) {
                type = joint.jointType;
                break;
            }
        }
    }
    for (int i = 0; i < kDynChannelCount; ++i) {
        const auto channel = static_cast<DynChannelId>(i);
        m_channelCombo->setItemText(i, panelText("plugin.dynamics.channel." + dynChannelKey(channel))
            + QStringLiteral("（") + QString::fromStdString(dynChannelUnit(channel, type))
            + QStringLiteral("）"));
    }
    // 广义力同时覆盖转动和移动轴，峰值选择不把移动轴称为力矩。
    m_peakTokenCombo->setItemText(0, panelText("plugin.dynamics.peak.force-positive"));
    m_peakTokenCombo->setItemText(1, panelText("plugin.dynamics.peak.force-negative"));
}

void DynamicsPanelWidget::runReplayAt()
{
    showReplayAt(m_replayTimeS->value());
}

void DynamicsPanelWidget::showReplayAt(double tS)
{
    // 缝空＝回放数据未装配——诚实呈现，不虚构帧。
    if (!m_module.services.replaySource) {
        m_replayReading->setText(panelText("plugin.dynamics.replay.unbound"));
        return;
    }
    const ReplayData data = m_module.services.replaySource();
    if (m_module.services.curveSource) {
        const auto curve = m_module.services.curveSource();
        if (!(curve.sourceSeriesId == data.sourceSeriesId) || !(curve.conditionId == data.conditionId)) {
            m_replayReading->setText(panelText("plugin.dynamics.replay.source-mismatch"));
            return; // 不允许当前曲线驱动另一工况或另一轮结果的姿态。
        }
    }
    // L-D5：按时刻插值查表（域设施消费——UI 线程查表零计算；时刻单位 s）。
    const std::optional<ReplaySample> sample =
        replaySampleAt(data, tS);
    if (!sample.has_value()) {
        // 合法空态（越界不外推——§4.3 纪律；空数据集）：如实呈现。
        m_replayReading->setText(
            panelText("plugin.dynamics.replay.out-of-range"));
        return;
    }
    // 逐关节读数行（工程用语＋量纲按型——关节型决定 q/q̇/τ 单位标签；
    // 零姿态计算：q 值即会话姿态驱动素材，FK 归 runtime——卡 §9.5）。
    QStringList lines;
    lines << (sample->exact ? panelText("plugin.dynamics.replay.exact")
                            : panelText("plugin.dynamics.replay.interpolated"));
    lines << panelText("plugin.dynamics.replay.location")
                 .arg(sample->segmentIndex)
                 .arg(QString::number(sample->t, 'g', 6));
    for (const ReplayJointState& joint : sample->joints) {
        const bool prismatic = joint.jointType == DynJointType::Prismatic;
        lines << panelText("plugin.dynamics.replay.joint")
                     .arg(joint.jointIndex + 1)
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
        lines << panelText("plugin.dynamics.replay.incomplete");
    }
    m_replayReading->setText(lines.join('\n'));
    std::string poseReason;
    if (applyReplayPose(m_module.services, *sample, &poseReason)) {
        lines << panelText("plugin.dynamics.pose.synced");
    } else if (poseReason == "not-assembled") {
        lines << panelText("plugin.dynamics.pose.unbound");
    } else if (poseReason == "incomplete") {
        lines << panelText("plugin.dynamics.pose.incomplete");
    } else {
        lines << panelText("plugin.dynamics.pose.failed");
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
        m_dataStatusLabel->setText(panelText("plugin.dynamics.peak.no-joint"));
        return;
    }
    std::string reason;
    const int token = peakTokenIndexForSelection(
        m_peakTokenCombo != nullptr ? m_peakTokenCombo->currentData().toInt() : -1);
    if (token < 0) {
        m_dataStatusLabel->setText(panelText("plugin.dynamics.peak.invalid"));
        return;
    }
    const std::optional<DynPeakJump> jump =
        locatePeakJump(m_module.services, static_cast<std::uint32_t>(jointData),
                       token, &reason);
    if (!jump.has_value()) {
        m_dataStatusLabel->setText(
            reason == "not-assembled"
                ? panelText("plugin.dynamics.peak.unbound")
                : panelText("plugin.dynamics.peak.empty"));
        return;
    }
    // 命中→曲线游标跳转峰值时刻＋标记（locate-peak 呈现落点；三维
    // 姿态同步素材＝tPeakS，宿主经回放查询链消费——零修订四常量）。
    // 统计峰值可为反向幅值或绝对值，图上必须标原样本的有符号读数。
    // 这里只查表，不在 UI 重算统计；找不到同源曲线点就不画虚假标记。
    m_chart->clearPeakMarker();
    if (m_module.services.curveSource) {
        for (const auto& joint : m_module.services.curveSource().joints) {
            if (joint.jointIndex != static_cast<std::uint32_t>(jointData)) { continue; }
            const auto at = std::find(joint.t.begin(), joint.t.end(), jump->tPeakS);
            const auto& values = dynChannelValues(joint,
                static_cast<DynChannelId>(m_channelCombo->currentData().toInt()));
            const auto index = static_cast<std::size_t>(at - joint.t.begin());
            if (at != joint.t.end() && index < values.size()) {
                m_chart->setPeakMarker(jump->tPeakS, values[index]);
            }
        }
    }
    m_chart->setCursorTimeS(jump->tPeakS);
    m_replayTimeS->setValue(jump->tPeakS);
    showReplayAt(jump->tPeakS); // 不经过输入框舍入，与统计器命中时刻严格一致。
    m_dataStatusLabel->setText(
        panelText("plugin.dynamics.peak.result")
            .arg(QString::number(jump->tPeakS, 'g', 6))
            .arg(QString::number(jump->value, 'g', 6))
            .arg(m_channelCombo->currentText())
            .arg(jump->segmentIndex));
}

}  // namespace sdurws::ird::dynamics
