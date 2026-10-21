/**
 * @file   TrajectoryPanelWidget.cpp
 * @brief  trajectory 工作流面板的实现翻译单元——界面组装、命令受理/提
 *         交两步、动画查表驱动与曲线联动（WP-16-T12）。
 *
 * 设计依据：TrajectoryPanelWidget.hpp 文件头（交互语义与诚实边界）。
 *
 * 零计算红线执行面：本 TU 只做控件组装、词表查表（命令受理）、动画查
 * 表（sampleAt——纯插值查表）与文本/曲线呈现；零规划/碰撞/时间化/复检
 * 符号（契约测试全文词表扫描钉住）。
 */

#include "TrajectoryPanelWidget.hpp"

#include "TrajCurveView.hpp"           // TrajCurveView（曲线视图——私有头）
#include "TrajPanelCommandCatalog.hpp" // trjDomainCommands（命令目录——§14.5.1
                                       //   七命令登记值；assembly 头随其传递
                                       //   ——TrjCommandDescriptor 完整类型）
#include "TrajPanelModule.hpp"  // TrajPanelModule（完整类型——缝/会话态访问）

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>     // std::isfinite（帧读数的非有限防御——不呈现坏值）
#include <cstddef>   // std::size_t
#include <algorithm>
#include <exception>
#include <string>

namespace sdurws::ird::trajectory {

namespace {

/// 动画进度滑杆的分辨率（等分刻度数——呈现参数：拖动定位的像素级平滑
/// 度，非业务阈值；0..kProgressMax 映射 [0, totalDurationS]）。
constexpr int kProgressMax = 1000;

}  // namespace

// =====================================================================
// 构造/析构与界面组装
// =====================================================================

TrajectoryPanelWidget::TrajectoryPanelWidget(TrajPanelModule& module,
                                             QWidget* parent)
    : QWidget(parent), m_module(module), m_playbackEpoch(module.session.epoch)
{
    // Tab 容器两页（工作流页＋曲线视图入口——§16.1 合一主面板形态：
    // 曲线联动/动画读数是工作流命令的直达呈现面，拆分双面板会造成同
    // 数据双实例的呈现漂移，dynamics 先例口径）。
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildWorkflowPage(), tr("轨迹工作流"));
    tabs->addTab(buildCurvesPage(), tr("曲线视图"));
    layout->addWidget(tabs);

    refreshFromSession();  // 面板创建即呈现会话事实（空缝如实降级）
    // 曲线页虽然尚未显示，也必须在构造完成时建立首屏数据快照；否则
    // 用户第一次切换到“曲线视图”前不会触发下拉项与曲线源读取，形成
    // 空白首屏（TRJ-UI-01）。refreshCurvesPage 只查归档投影，不执行规划。
}

TrajectoryPanelWidget::~TrajectoryPanelWidget() = default;

QWidget* TrajectoryPanelWidget::buildWorkflowPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);

    // ---- 就绪投影文本（UX-10 七态素材：判定/输入完整/缺项/在途任务
    //       ——refreshFromSession 现取会话态填充；构造期占位文案）。 -
    m_readinessLabel = new QLabel(page);
    m_readinessLabel->setWordWrap(true);
    root->addWidget(m_readinessLabel);

    // ---- 命令区：七命令按钮（词表序＝§14.5.1 表行序——目录现产；
    //       点击→onCommandButtonClicked 单槽统一处理，sender→token 反查
    //       ——零 Q_OBJECT 形态下的函数指针 connect）。 -----------------
    auto* commandGrid = new QGridLayout();
    const std::vector<TrjCommandDescriptor> commands = trjDomainCommands();
    m_commandButtons.reserve(commands.size());
    for (std::size_t i = 0; i < commands.size(); ++i) {
        auto* button = new QPushButton(commands[i].token.c_str(), page);
        // 按钮初始文本＝token 字面（缝空＝键名原文呈现——开发态可见缺
        // 口；refreshFromSession 经文案缝解析为工程用语，UX-02）。
        button->setToolTip(commands[i].titleKey.c_str());
        connect(button, &QPushButton::clicked, this,
                &TrajectoryPanelWidget::onCommandButtonClicked);
        m_commandButtons.push_back(button);
        commandGrid->addWidget(button,
                               static_cast<int>(i / 3),   // 每行三枚（呈现布局）
                               static_cast<int>(i % 3));
    }
    auto* commandBox = new QGroupBox(tr("规划命令"), page);
    auto* commandBoxLayout = new QVBoxLayout(commandBox);
    commandBoxLayout->addLayout(commandGrid);
    commandBox->setToolTip(tr("先校验输入，再把受理的命令交给宿主执行管线"));
    root->addWidget(commandBox);

    // ---- 动画播放区：播放按钮＋进度滑杆＋帧读数（§14.5.2 播放＝按
    //       时刻插值查表驱动会话姿态——查表在本类完成，姿态应用经缝
    //       出帧；零规划/碰撞/时间化——UI 线程红线内）。 ----------------
    auto* playbackRow = new QHBoxLayout();
    m_playButton = new QPushButton(tr("播放"), page);
    m_playButton->setObjectName(QStringLiteral("trajectory_play"));
    connect(m_playButton, &QPushButton::clicked, this,
            &TrajectoryPanelWidget::onPlayClicked);
    m_resetButton = new QPushButton(tr("回到起点"), page);
    m_resetButton->setObjectName(QStringLiteral("trajectory_reset"));
    connect(m_resetButton, &QPushButton::clicked, this, [this] {
        stopPlayback();
        // 即使滑杆已经为零，也必须重新查表，才能覆盖外部会话姿态变化。
        const QSignalBlocker blocker(m_progressSlider);
        m_progressSlider->setValue(0);
        seekToSliderValue(0);
    });
    m_timeSpin = new QDoubleSpinBox(page);
    m_timeSpin->setObjectName(QStringLiteral("trajectory_time"));
    m_timeSpin->setDecimals(3);
    m_timeSpin->setSuffix(tr(" s"));
    m_timeSpin->setKeyboardTracking(false);
    connect(m_timeSpin, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        if (!m_module.services.durationSource) { return; }
        try {
            const double duration = m_module.services.durationSource();
            if (!std::isfinite(duration) || duration <= 0.0) { return; }
            stopPlayback();
            const int value = qBound(0, qRound(seconds / duration * kProgressMax), kProgressMax);
            const QSignalBlocker blocker(m_progressSlider);
            m_progressSlider->setValue(value);
            seekToSliderValue(value);
        } catch (const std::exception& error) {
            stopPlayback();
            m_frameReadout->setText(tr("回放数据读取失败：%1").arg(QString::fromUtf8(error.what())));
        }
    });
    m_progressSlider = new QSlider(Qt::Horizontal, page);
    m_progressSlider->setRange(0, kProgressMax);
    m_progressSlider->setValue(0);
    connect(m_progressSlider, &QSlider::valueChanged, this,
            &TrajectoryPanelWidget::onSeekMoved);
    m_frameReadout = new QLabel(page);
    m_frameReadout->setWordWrap(true);
    playbackRow->addWidget(m_playButton);
    playbackRow->addWidget(m_resetButton);
    playbackRow->addWidget(m_progressSlider, /*stretch=*/1);
    playbackRow->addWidget(m_timeSpin);
    // 16 ms 是界面刷新周期（约 60 Hz），不改变归档采样或数值精度；真实
    // 时刻按单调时钟计算，繁忙事件循环只降低帧率，不拖慢轨迹总时长。
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(16);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        try {
            const double duration = m_module.services.durationSource
                                        ? m_module.services.durationSource() : -1.0;
            if (!m_module.services.playbackSource || !std::isfinite(duration)
                || duration <= 0.0) {
                stopPlayback(); refreshPlaybackControls(); return;
            }
            const double time = std::min(duration, m_playStartS + m_playClock.elapsed() / 1000.0);
            const int value = qBound(0, qRound(time / duration * kProgressMax), kProgressMax);
            const QSignalBlocker blocker(m_progressSlider);
            m_progressSlider->setValue(value);
            seekToSliderValue(value);
            if (time >= duration) { stopPlayback(); }
        } catch (const std::exception& error) {
            stopPlayback();
            m_frameReadout->setText(tr("回放数据读取失败：%1").arg(QString::fromUtf8(error.what())));
        }
    });
    auto* playbackBox = new QGroupBox(tr("轨迹回放"), page);
    auto* playbackLayout = new QVBoxLayout(playbackBox);
    playbackLayout->addLayout(playbackRow);
    playbackLayout->addWidget(m_frameReadout);
    root->addWidget(playbackBox);

    // 失败定位独立成组：失败时给出稳定码/段号，未装配定位出口时明确
    // 告知用户，而不是显示一个点击后无反应的按钮（TRJ-06/UX-10）。
    auto* failureBox = new QGroupBox(tr("诊断与三维定位"), page);
    auto* failureLayout = new QHBoxLayout(failureBox);
    m_failureLabel = new QLabel(failureBox);
    m_failureLabel->setWordWrap(true);
    m_failureLocateButton = new QPushButton(tr("定位到三维"), failureBox);
    m_failureLocateButton->setObjectName(QStringLiteral("trajectory_failure_locate"));
    connect(m_failureLocateButton, &QPushButton::clicked, this,
            &TrajectoryPanelWidget::onFailureLocateClicked);
    failureLayout->addWidget(m_failureLabel, 1);
    failureLayout->addWidget(m_failureLocateButton);
    root->addWidget(failureBox);

    // ---- 最近命令清单（只读呈现缓冲——受理记录的会话留痕；零修订
    //       契约的逐条观测面）。 --------------------------------------
    m_recentList = new QPlainTextEdit(page);
    m_recentList->setReadOnly(true);
    m_recentList->setMaximumHeight(120);
    root->addWidget(m_recentList);
    root->addStretch(1);
    return page;
}

QWidget* TrajectoryPanelWidget::buildCurvesPage()
{
    auto* page = new QWidget(this);
    auto* root = new QVBoxLayout(page);

    // ---- 联动控件行：关节选择＋通道选择（三值词表——Position/Velocity/
    //       Acceleration；切换即重投影——零重采样，投影序列即归档样本
    //       表的逐关节视图）。 ------------------------------------------
    auto* selectorRow = new QHBoxLayout();
    m_jointCombo = new QComboBox(page);
    m_channelCombo = new QComboBox(page);
    for (const int channelIndex : {0, 1, 2}) {
        // 通道下拉项＝通道中文标签（本域唯一文案面——通道词表是界面语
        // 义，非宿主文案资源键族）。
        m_channelCombo->addItem(
            channelLabel(channelFromIndex(channelIndex)));
    }
    connect(m_jointCombo, &QComboBox::currentIndexChanged, this,
            &TrajectoryPanelWidget::onCurveJointChanged);
    connect(m_channelCombo, &QComboBox::currentIndexChanged, this,
            &TrajectoryPanelWidget::onCurveChannelChanged);
    selectorRow->addWidget(new QLabel(tr("关节"), page));
    selectorRow->addWidget(m_jointCombo);
    selectorRow->addWidget(new QLabel(tr("通道"), page));
    selectorRow->addWidget(m_channelCombo);
    selectorRow->addStretch(1);
    root->addLayout(selectorRow);

    // ---- 曲线视图（自绘折线——空态如实呈现）。 -----------------------
    m_curveView = new TrajCurveView(page);
    root->addWidget(m_curveView, /*stretch=*/1);
    return page;
}

// =====================================================================
// 会话刷新（现取会话态重投影——防第二真值）
// =====================================================================

void TrajectoryPanelWidget::refreshFromSession()
{
    const TrajModuleSessionState& state = m_module.session;
    const auto& text = m_module.services.textResolver;

    // 项目/修订或归档来源切换后，上一轮播放的时刻已失去意义。只复位
    // 控件且不调用姿态出口；由用户在新会话重新播放，防旧帧污染新模型。
    if (m_playbackEpoch != state.epoch) {
        stopPlayback();
        m_playbackEpoch = state.epoch;
        const QSignalBlocker sliderBlocker(m_progressSlider);
        const QSignalBlocker timeBlocker(m_timeSpin);
        m_progressSlider->setValue(0);
        m_timeSpin->setValue(0.0);
        m_frameReadout->clear();
        m_frameReadout->setProperty("playbackValid", false);
        m_commandFeedback.clear();
    }

    // ---- 就绪投影文本（字段逐一呈现；缺项清单逐行——UX-10"未完成附
    //       缺项列表"；文案经宿主解析缝——空缝回退键名原文，如实）。 -
    QString readiness;
    readiness += tr("最近正式判定：");
    // 判定词表的界面语义映射（core 词表四值——呈现文本在本面板单点，
    // 不动 core 词表；NotApplicable＝无判定，不伪造可行性）。
    switch (state.verdict) {
    case core::EngineeringStatus::Feasible: readiness += tr("可行"); break;
    case core::EngineeringStatus::EngineeringInfeasible:
        readiness += tr("不可行");
        break;
    case core::EngineeringStatus::DataInsufficient:
        readiness += tr("数据不足");
        break;
    case core::EngineeringStatus::NotApplicable:
        readiness += tr("无判定");
        break;
    }
    readiness += state.inputComplete ? tr("；输入完整")
                                     : tr("；输入未完成");
    if (state.hasActiveTask) {
        readiness += tr("；计算中（存在在途任务）");
    }
    if (!state.missingItemKeys.empty()) {
        readiness += tr("；缺项：");
        for (const std::string& key : state.missingItemKeys) {
            // 缺项文案键经宿主解析（UX-02——界面只见键与局部名，零哈希
            // /内部标识；空缝＝键名原文——开发态可见缺口）。
            readiness += QStringLiteral("、")
                         + QString::fromStdString(
                             text ? text(key) : key);
        }
    }
    m_readinessLabel->setText(readiness);

    // ---- 命令按钮：文本经文案缝解析（titleKey→工程用语）＋可用性门控
    //       （宿主权威缝——空缝全可用，点击时经提交缝语义如实反馈）。 -
    const std::vector<TrjCommandDescriptor> commands = trjDomainCommands();
    for (std::size_t i = 0; i < m_commandButtons.size() && i < commands.size();
         ++i) {
        m_commandButtons[i]->setText(
            text ? QString::fromStdString(text(commands[i].titleKey))
                 : QString::fromStdString(commands[i].token));
        const bool available = m_module.services.commandAvailability
                                   ? m_module.services.commandAvailability(
                                       commands[i].token)
                                   : true;  // 空缝＝全可用呈现（降级纪律）
        m_commandButtons[i]->setEnabled(available);
        m_commandButtons[i]->setToolTip(
            available ? tr("提交到宿主；结果在任务与诊断中查看")
                      : tr("当前不可用：执行服务尚未就绪，或输入、权限、在途任务不满足要求"));
    }
    // ---- 最近命令清单（受理记录逐行；producesRevision 恒 false——零
    //       修订契约的呈现面披露）。 ----------------------------------
    m_recentList->clear();
    if (state.recentCommands.empty()) {
        m_recentList->setPlainText(tr("（最近命令：无）"));
    } else {
        QString lines;
        for (const TrajCommandRecord& record : state.recentCommands) {
            lines += QString::fromStdString(record.commandToken);
            lines += record.accepted ? tr("：已受理") : tr("：不受理");
            if (!record.accepted && !record.rejectionToken.empty()) {
                lines += tr("（") + QString::fromStdString(record.rejectionToken)
                         + tr("）");
            }
            // 零修订披露：AT-04 的机器断言面在会话呈现层的对应用户可见
            // 语义——受理记录不声称任何修订效果。
            lines += QLatin1Char('\n');
        }
        m_recentList->setPlainText(lines);
    }
    if (!m_commandFeedback.isEmpty()) {
        m_recentList->appendPlainText(m_commandFeedback);
    }

    // 失败素材只取会话事实；段号没有来源时不生成定位目标。定位出口
    // 由宿主三维网关提供，缺席时按钮置灰并保留可解释的降级文案。
    if (m_failureLabel != nullptr && m_failureLocateButton != nullptr) {
        if (!state.failedSegmentIndex.has_value()) {
            m_failureLabel->setText(tr("暂无失败段"));
            m_failureLocateButton->setEnabled(false);
        } else {
            QString text = tr("失败段 %1").arg(*state.failedSegmentIndex);
            if (!state.failureReasonToken.empty()) {
                text += tr("：") + QString::fromStdString(state.failureReasonToken);
            }
            m_failureLabel->setText(text);
            if (!m_module.services.failureLocate) {
                m_failureLabel->setText(text + tr("；三维定位未装配"));
            }
            m_failureLocateButton->setEnabled(
                static_cast<bool>(m_module.services.failureLocate));
        }
    }

    refreshPlaybackControls();
    // 会话数据变更后必须同步曲线，避免首屏修好后仍展示旧运行的结果。
    refreshCurvesPage();
}

// =====================================================================
// 命令受理＋提交两步（§14.5.1/§15.11——零计算、零修订）
// =====================================================================

void TrajectoryPanelWidget::onCommandButtonClicked()
{
    // sender→按钮序反查 token（布局序＝词表序——目录现产的稳定序）。
    auto* button = qobject_cast<QPushButton*>(sender());
    if (button == nullptr) { return; }
    std::size_t index = 0;
    bool found = false;
    for (std::size_t i = 0; i < m_commandButtons.size(); ++i) {
        if (m_commandButtons[i] == button) {
            index = i;
            found = true;
            break;
        }
    }
    if (!found) { return; }
    const std::vector<TrjCommandDescriptor> commands = trjDomainCommands();
    if (index >= commands.size()) { return; }
    const std::string& token = commands[index].token;

    // 第一步：受理（TrajectoryCommandHandler 词表查表——零计算适配器；
    // 受理记录入会话缓冲并刷新呈现——零修订断言面：受理结果
    // producesRevision 恒 false）。
    TrajectoryCommandPayload payload;
    payload.commandToken = token;  // 交叉校验位与命令一致（合法负载）
    const TrajectoryCommandOutcome outcome = m_commandHandler.handle(token, payload);

    TrajModuleSessionState& state = m_module.session;
    TrajCommandRecord record;
    record.commandToken = token;
    record.accepted = outcome.accepted;
    record.rejectionToken = outcome.rejectionToken;
    record.producesRevision = outcome.producesRevision;  // 恒 false（契约）
    state.recentCommands.push_back(record);
    // 呈现缓冲容量截断（8 条——TrajModuleSessionState 注；超出从头丢弃
    // 最旧记录，完整受理史归 execution 任务清单）。
    constexpr std::size_t kRecentCapacity = 8;
    if (state.recentCommands.size() > kRecentCapacity) {
        state.recentCommands.erase(state.recentCommands.begin());
    }

    // 第二步：仅有“受理”才允许进入宿主提交管线。拒绝命令必须停在
    // 会话记录层，否则宿主可能把非法请求当成真实规划命令执行；这也是
    // UI 受理与域命令语义之间的原子边界（TRJ-UI-02）。
    m_commandFeedback.clear();
    if (outcome.accepted && m_module.services.commandSubmit) {
        try {
            // 点击时复验宿主门控，防止上一帧启用、当前已切成只读/计算中的竞态。
            if (m_module.services.commandAvailability
                && !m_module.services.commandAvailability(token)) {
                state.recentCommands.back().accepted = false;
                m_commandFeedback = tr("命令当前不可用；请检查输入、只读状态或在途任务");
            } else {
                m_module.services.commandSubmit(token);
                m_commandFeedback = tr("已交给宿主处理；执行结果请查看任务与诊断");
            }
        } catch (const std::exception& error) {
            state.recentCommands.back().accepted = false;
            m_commandFeedback = tr("命令提交失败：%1").arg(QString::fromUtf8(error.what()));
        }
    } else if (outcome.accepted) {
        state.recentCommands.back().accepted = false;
        m_commandFeedback = tr("命令出口未装配——提交未发出");
    }
    refreshFromSession();
}

// =====================================================================
// 动画查表驱动（§14.5.2——UI 线程纯插值查表；零修订）
// =====================================================================

void TrajectoryPanelWidget::onPlayClicked()
{
    if (m_playTimer->isActive()) { stopPlayback(); return; }
    refreshPlaybackControls();
    if (!m_playButton->isEnabled()) { return; }
    if (m_progressSlider->value() == kProgressMax) {
        const QSignalBlocker blocker(m_progressSlider);
        m_progressSlider->setValue(0);
    }
    seekToSliderValue(m_progressSlider->value());
    // 坏帧读取可能停播并写入错误；仅在数据源仍可用时启动后续帧。
    if (m_frameReadout->property("playbackValid").toBool()) {
        m_playStartS = m_timeSpin->value();
        m_playClock.start();
        m_playTimer->start();
        m_playButton->setText(tr("暂停"));
    }
}

void TrajectoryPanelWidget::onSeekMoved(int sliderValue)
{
    stopPlayback(); // 用户拖动定位时停播，避免定时器抢回滑杆位置。
    seekToSliderValue(sliderValue);
}

void TrajectoryPanelWidget::stopPlayback()
{
    if (m_playTimer != nullptr) { m_playTimer->stop(); }
    if (m_playButton != nullptr) { m_playButton->setText(tr("播放")); }
}

void TrajectoryPanelWidget::refreshPlaybackControls()
{
    bool available = false;
    double duration = 0.0;
    QString reason = tr("轨迹数据未装配（动画不可用）");
    try {
        if (m_module.services.durationSource && m_module.services.playbackSource) {
            duration = m_module.services.durationSource();
            available = std::isfinite(duration) && duration >= 0.0;
            reason = available ? tr("拖动进度或输入时刻定位；播放仅改变会话姿态")
                               : tr("轨迹时长无效，无法回放");
        }
    } catch (const std::exception& error) {
        reason = tr("回放数据读取失败：%1").arg(QString::fromUtf8(error.what()));
    }
    if (!available) { stopPlayback(); m_frameReadout->setText(reason); }
    else if (m_frameReadout->text().isEmpty()) { m_frameReadout->setText(tr("归档轨迹已就绪")); }
    m_playButton->setEnabled(available && duration > 0.0);
    m_resetButton->setEnabled(available);
    m_progressSlider->setEnabled(available);
    m_timeSpin->setEnabled(available);
    const QSignalBlocker blocker(m_timeSpin);
    m_timeSpin->setRange(0.0, available ? duration : 0.0);
    for (auto* widget : std::vector<QWidget*>{m_playButton, m_resetButton, m_progressSlider, m_timeSpin}) {
        widget->setToolTip(reason);
    }
}

void TrajectoryPanelWidget::seekToSliderValue(int sliderValue)
{
    if (m_frameReadout == nullptr || m_progressSlider == nullptr) { return; }
    m_frameReadout->setProperty("playbackValid", false);
    if (!m_module.services.playbackSource || !m_module.services.durationSource) {
        // 数据缝未装配——如实呈现（不虚构帧值，NFR-COR-03）。
        m_frameReadout->setText(tr("轨迹数据未装配（动画不可用）"));
        return;
    }
    // 进度值→时刻（线性映射：进度比例×总时长；总时长由时长缝承接——
    // 宿主装配层从归档 payload totalDurationS 承接、harness 侧从演示
    // payload 承接；时刻量纲 s 的换算在本面板单点完成）。
    const double ratio = static_cast<double>(
        qBound(0, sliderValue, kProgressMax)) / static_cast<double>(kProgressMax);
    try {
    const double totalDurationS = m_module.services.durationSource();  // s
    if (!std::isfinite(totalDurationS) || totalDurationS < 0.0) {
        stopPlayback(); m_frameReadout->setText(tr("轨迹时长无效，无法回放")); return;
    }
    const double tS = ratio * totalDurationS;                          // s
    // 查表缝（组装侧消费查表面——纯插值查表＋逐轴拷贝为纯值帧；本面
    // 板零查表器符号消费——冒烟口径隔离，TrajPlaybackSample 注）。
    const TrajPlaybackSample frame = m_module.services.playbackSource(tS);
    // 归档源可能被切换或损坏。所有通道维度与有限性先校验，禁止把 NaN
    // 或半帧送给三维；失败只更新诊断，不产生项目修订（AT-04）。
    const auto finite = [](const std::vector<double>& values) {
        return std::all_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); });
    };
    if (!std::isfinite(frame.tS) || std::abs(frame.tS - tS) > 1e-9
        || frame.q.empty() || frame.qd.size() != frame.q.size()
        || frame.qdd.size() != frame.q.size()
        || !finite(frame.q) || !finite(frame.qd) || !finite(frame.qdd)) {
        stopPlayback(); m_frameReadout->setText(tr("轨迹帧无效，未应用到三维视图")); return;
    }
    const QSignalBlocker blocker(m_timeSpin);
    m_timeSpin->setValue(tS);

    // 帧读数（q/qd 逐轴数值文本——SI 单位；转动 rad、移动 m，逐轴按
    // 关节类型；读数只呈现不判定）。
    QString readout = tr("t=%1 s").arg(frame.tS, 0, 'f', 3);
    readout += frame.atSample ? tr("（样本位）") : tr("（插值）");
    readout += tr("　q=[");
    for (std::size_t i = 0; i < frame.q.size(); ++i) {
        if (i > 0) { readout += QLatin1String(", "); }
        readout += QString::number(frame.q[i], 'f', 4);
    }
    readout += QLatin1String("]");
    readout += tr("　qd=[");
    for (std::size_t i = 0; i < frame.qd.size(); ++i) {
        if (i > 0) { readout += QLatin1String(", "); }
        readout += QString::number(frame.qd[i], 'f', 4);
    }
    readout += QLatin1String("]");
    m_frameReadout->setText(readout);

    // 会话姿态驱动缝（§14.5.2——纯值帧出，宿主 View3D 桥组装应用；KIN-
    // 06 零修订语义经 kTrajectorySessionContract 契约钉扎；空缝＝零姿
    // 态效果，读数照常——开发 harness 形态如实降级）。
    if (m_module.services.sessionPoseSink) {
        m_module.services.sessionPoseSink(frame);
    } else {
        m_frameReadout->setText(readout + tr("；三维姿态出口未装配（仅显示读数）"));
    }
    m_frameReadout->setProperty("playbackValid", true);
    } catch (const std::exception& error) {
        stopPlayback();
        m_frameReadout->setText(tr("回放数据读取失败：%1").arg(QString::fromUtf8(error.what())));
    }
}

// =====================================================================
// 曲线联动（§14.5.1 show-curve 呈现面——读归档 payload 投影）
// =====================================================================

void TrajectoryPanelWidget::onCurveJointChanged(int /*index*/)
{
    refreshCurvesPage();
}

void TrajectoryPanelWidget::onCurveChannelChanged(int /*index*/)
{
    refreshCurvesPage();
}

void TrajectoryPanelWidget::onFailureLocateClicked()
{
    const auto index = m_module.session.failedSegmentIndex;
    if (!index.has_value() || !m_module.services.failureLocate) {
        if (m_failureLabel != nullptr) {
            m_failureLabel->setText(tr("失败段三维定位未装配"));
        }
        return;
    }
    try {
        const bool located = m_module.services.failureLocate(*index);
        if (m_failureLabel != nullptr) {
            m_failureLabel->setText(
                located ? tr("已定位到三维视图中的失败段 %1").arg(*index)
                        : tr("未找到失败段 %1 的三维对象").arg(*index));
        }
    } catch (const std::exception& error) {
        // 三维窗口可能在回调执行前被关闭；环境异常就地呈现，不能越出
        // Qt 信号槽导致整个宿主退出，也不能保留旧的“已定位”成功提示。
        m_failureLabel->setText(tr("三维定位失败：%1").arg(QString::fromUtf8(error.what())));
    }
}

void TrajectoryPanelWidget::refreshCurvesPage()
{
    if (m_curveView == nullptr || m_channelCombo == nullptr) { return; }
    if (!m_module.services.curvesSource) {
        // 数据缝未装配——空态呈现（不虚构曲线）。
        m_curveView->setSeries({}, channelLabel(
                                   channelFromIndex(m_channelCombo->currentIndex())));
        const QSignalBlocker blocker(m_jointCombo);
        m_jointCombo->clear();
        m_jointCombo->setEnabled(false);
        m_channelCombo->setEnabled(false);
        return;
    }
    // 缝取投影序列（通道词表值——投影器按关节升序返回全部关节；本面
    // 板把关节下拉选择映射为单关节视图传入曲线视图；下拉为空（尚未
    // 知关节数）时先以全序列重建下拉）。
    const TrajectoryCurveChannel channel =
        channelFromIndex(m_channelCombo->currentIndex());
    std::vector<TrajectoryCurveSeries> series;
    try {
        series = m_module.services.curvesSource(channel);
    } catch (const std::exception& error) {
        m_curveView->setSeries({}, tr("曲线数据读取失败：%1").arg(QString::fromUtf8(error.what())));
        const QSignalBlocker blocker(m_jointCombo);
        m_jointCombo->clear(); m_jointCombo->setEnabled(false);
        return;
    }
    m_jointCombo->setEnabled(!series.empty());
    m_channelCombo->setEnabled(!series.empty());

    // 关节下拉重建（关节数＝序列数；保持当前选择——首次进入默认关节 0；
    // 投影序列按关节升序——T10 投影产物不变量）。
    const int jointCount = static_cast<int>(series.size());
    if (m_jointCombo->count() != jointCount) {
        const int previous = m_jointCombo->currentIndex();
        m_jointCombo->blockSignals(true);  // 重建期间免信号风暴（单次重投影）
        m_jointCombo->clear();
        for (int j = 0; j < jointCount; ++j) {
            m_jointCombo->addItem(tr("关节 %1").arg(j));
        }
        m_jointCombo->setCurrentIndex(previous >= 0 && previous < jointCount
                                          ? previous
                                          : (jointCount > 0 ? 0 : -1));
        m_jointCombo->blockSignals(false);
    }

    // 关节选择→单关节曲线（越界回落空——呈现侧防御）。
    const int joint = m_jointCombo->currentIndex();
    std::vector<TrajectoryCurveSeries> selected;
    if (joint >= 0 && joint < static_cast<int>(series.size())) {
        selected.push_back(series[static_cast<std::size_t>(joint)]);
    }
    m_curveView->setSeries(selected, channelLabel(channel));
}

TrajectoryCurveChannel TrajectoryPanelWidget::channelFromIndex(int index)
{
    // 下拉序号→通道词表（0/1/2→Position/Velocity/Acceleration；越序号
    // 回落 Position——呈现侧防御，不抛）。
    switch (index) {
    case 1:  return TrajectoryCurveChannel::Velocity;
    case 2:  return TrajectoryCurveChannel::Acceleration;
    default: return TrajectoryCurveChannel::Position;
    }
}

QString TrajectoryPanelWidget::channelLabel(TrajectoryCurveChannel channel)
{
    // 通道中文标签（图例/空态文本素材——本域唯一文案面；通道词表是界
    // 面语义，单位标注随通道——SI：q rad|m、qd rad/s|m/s、qdd rad/s²|m/s²，
    // 逐轴按关节类型）。
    switch (channel) {
    case TrajectoryCurveChannel::Position:     return tr("位置 q");
    case TrajectoryCurveChannel::Velocity:     return tr("速度 qd");
    case TrajectoryCurveChannel::Acceleration: return tr("加速度 qdd");
    }
    return tr("未知通道");
}

}  // namespace sdurws::ird::trajectory
