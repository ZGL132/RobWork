/**
 * @file   HarnessMain.cpp
 * @brief  dynamics 插件面板开发验证 harness（sdurws_ird_dynamics_app）——
 *         "插件开发完成即可手动 GUI 验证"的 dynamics 侧载体（单元卡
 *         §11.5 Windows GUI 手动点验流程的宿主位；WP-17-T09）。
 *
 * 设计依据：
 *   - units/dynamics.md §11.5（手动点验流程：①构建→②启动本 harness
 *     →③工作流页可见、命令可达→④发起评估→⑤曲线联动/峰值定位→
 *     ⑥时刻回放零修订→⑦截图留痕入 traceability/builds/wp17-t09/）；
 *     DTB §5.1（插件 GUI 手动验证通道——先例＝kinematics_app〔WP-15-T12〕/
 *     modeling_app〔WP-13-T15〕）；§9.5（五命令呈现链路）
 *   - 任务契约 tasks/foundation/WP-17-T09.json（acceptance 1——界面
 *     链路；无人值守门禁不做 GUI 运行验证：本 harness 的**构建**留痕
 *     属本任务交付，**运行**属手动验证流程，未启动如实登记不标注通过）
 *
 * ★ 与产品装配层的关系（kinematics_app 先例同款口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层创建与持有
 *   的产品契约不变（ui.md §10.9）；本 harness 在进程内扮演装配层的最
 *   小角色：经装配门面创建模块与面板，注入演示服务缝（黄金序列现产
 *   的曲线/回放/峰值数据），把真实面板与真实装配描述符跑起来供人工
 *   验证。
 *
 * 真值边界（诚实声明，防误读验证结论）：
 *   - 真实部分：真实装配门面（描述符＋五命令目录＋面板工厂）、真实
 *     面板控件链路（就绪投影/命令受理/曲线视图/回放读数）、真实计算
 *     库数据面（黄金序列→投影/回放/峰值行集——投影器/统计器现产）；
 *   - 演示部分：归档读取缝（曲线/回放/峰值）在进程内现取现算（真实
 *     归档链路＝归档 payload 读取，归宿主装配层）、命令提交出口以
 *     回显承载（真实 CommandRegistry 接线归宿主装配批次——不虚构提
 *     交语义）、文案解析器以键名回显（宿主 UiText 资源接线归装配批次
 *     ——UX-02 键值分离的产品形态见装配登记）；
 *   - 不覆盖：三维姿态同步的真实 View3D 驱动（回放读数为会话姿态素
 *     材呈现——姿态驱动归宿主 View3D 桥，KIN-06 零修订语义经模型层
 *     契约测试钉住）。
 *
 * 用法（开发期交互验证——单元卡 §11.5 流程第②步）：
 *   sdurws_ird_dynamics_app
 *     启动即见工作流页（就绪投影＋五命令＋最近命令＋回放区）＋曲线
 *     视图页（关节/通道下拉＋折线＋游标＋峰值定位）。手动可验：切关
 *     节/通道看联动；点峰值定位看游标跳转；改时刻查回放读数（零修订）。
 *
 * 线程模型：单线程——QApplication/缝/面板全部 main 线程（卡 §3.4）。
 */

#include <QApplication>
#include <QDebug>
#include <QMainWindow>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>            // core::ContentDigester/Digest256
                                                 //   （演示数据确定性派生）
#include <sdurws/ird/core/Identity.hpp>          // core::ObjectId/TaskIdentity
#include <sdurws/ird/dynamics/DynTypes.hpp>      // 样本行值类型
#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp> // 装配门面（产品入口）
#include <sdurws/ird/dynamics/Envelope.hpp>      // 峰值统计器（缝组装侧）
#include <sdurws/ird/dynamics/Replay.hpp>        // 曲线/回放投影器（缝组装侧）
#include <sdurws/ird/dynamics/SeriesBuilder.hpp> // 序列构建（黄金序列组装）
#include "plugin/DynPanelTypes.hpp"              // DynPanelServices/DynPeakJump
                                                 //   （服务缝值——app 目标
                                                 //   include 单元根，同单元
                                                 //   私有头 plugin/ 直指）

using sdurws::ird::dynamics::CurveProjection;
using sdurws::ird::dynamics::DynamicsCurveProjector;
using sdurws::ird::dynamics::DynamicsEnvelopeCalculator;
using sdurws::ird::dynamics::DynamicsPeakLocator;
using sdurws::ird::dynamics::DynamicsPluginAssembly;
using sdurws::ird::dynamics::DynamicsReplayProjector;
using sdurws::ird::dynamics::DynamicsSample;
using sdurws::ird::dynamics::DynamicsSeries;
using sdurws::ird::dynamics::DynamicsSeriesBuilder;
using sdurws::ird::dynamics::DynJointType;
using sdurws::ird::dynamics::DynPeakJump;
using sdurws::ird::dynamics::PeakRecord;
using sdurws::ird::dynamics::ReplayData;
using sdurws::ird::dynamics::SampleNumericState;
using sdurws::ird::dynamics::SeriesIdentity;
using sdurws::ird::dynamics::createDynamicsPluginAssembly;

namespace {

// =====================================================================
// 演示黄金序列（两关节×五时刻——同 WP-17-T08 数据面黄金算例的数值
// 形态；harness 演示数据零业务语义，全部值手算可得）。
//
// 时间轴 t ∈ {0, 0.5, 1, 1.5, 2} s，段结构 t<1 归段 0、t≥1 归段 1：
//   关节 0（Revolute，N·m）：q=t²、q̇=2t、q̈=2、τ=10−t、P=10t
//   关节 1（Prismatic，N）： q=1−t、q̇=−1、q̈=0、τ=−5−t、P=−3t
// =====================================================================

/// 演示时刻表，单位 s。
constexpr double kDemoT[5] = {0.0, 0.5, 1.0, 1.5, 2.0};
/// 时刻所属段（t<1→0，t≥1→1）。
constexpr std::uint32_t kDemoSeg[5] = {0, 0, 1, 1, 1};
/// 演示时刻数。
constexpr std::size_t kDemoSteps = 5;

/// 固定种子摘要（演示数据确定性——非产品路径，测试夹具同款精神）。
sdurws::ird::core::Digest256 demoDigest(const std::string& seed)
{
    sdurws::ird::core::ContentDigester d;
    d.update(seed.data(), seed.size());
    return d.finalize();
}

/// 从固定种子派生 16 字节强类型 id（演示值）。
template <typename Id>
Id demoId(const std::string& seed)
{
    const auto d = demoDigest(seed);
    Id id;
    std::copy(d.begin(), d.begin() + 16, id.bytes.begin());
    return id;
}

/// 演示样本行构造（关节 0/1 的黄金解析式取值——五分项字段置 0 自洽：
/// 呈现投影只消费 τ_total 与运动学通道）。
DynamicsSample demoRow(std::size_t step, std::uint32_t joint)
{
    const double t = kDemoT[step];
    DynamicsSample r;
    r.t = t;                                           // s
    r.segmentIndex = kDemoSeg[step];                   // 所在轨迹段（0 基）
    r.conditionId = demoId<sdurws::ird::core::ObjectId>("demo-cond");
    r.jointIndex = joint;
    r.jointObjectId = joint == 0 ? demoId<sdurws::ird::core::ObjectId>("demo-j0")
                                 : demoId<sdurws::ird::core::ObjectId>("demo-j1");
    r.jointType = joint == 0 ? DynJointType::Revolute : DynJointType::Prismatic;
    r.q = joint == 0 ? t * t : 1.0 - t;                // rad 或 m
    r.qd = joint == 0 ? 2.0 * t : -1.0;                // rad/s 或 m/s
    r.qdd = joint == 0 ? 2.0 : 0.0;                    // rad/s² 或 m/s²
    r.tauGravity = 0.0;                                // 分项置 0（演示数据）
    r.tauInertia = 0.0;
    r.tauCoriolisCentrifugal = 0.0;
    r.tauFriction = 0.0;
    r.tauExternal = 0.0;
    r.tauTotal = joint == 0 ? 10.0 - t : -5.0 - t;     // N·m 或 N
    r.mechanicalPower = joint == 0 ? 10.0 * t : -3.0 * t; // W
    r.energyIntegralJ = 0.0;                           // J（演示不消费）
    r.payloadVariantIndex = 0;                         // 基线变体
    r.toolObjectId = sdurws::ird::core::ObjectId{};    // 无工具模型
    r.numericState = SampleNumericState::Ok;
    return r;
}

/// 组装演示序列（身份块确定性派生——序列内容身份随之确定）。
DynamicsSeries makeDemoSeries()
{
    SeriesIdentity id;
    id.snapshotId.bytes = demoDigest("demo-snap");           // 内容身份＝摘要值
    id.sliceId.bytes = demoDigest("demo-slice");
    id.trajectoryPayloadId.bytes = demoDigest("demo-trj");
    id.conditionId = demoId<sdurws::ird::core::ObjectId>("demo-cond");
    id.toolObjectId = sdurws::ird::core::ObjectId{};
    id.dynConfigDigest = "demo-config-digest";
    id.task.project = demoId<sdurws::ird::core::ProjectId>("demo-prj");
    id.task.branch = demoId<sdurws::ird::core::BranchId>("demo-brn");
    id.task.revision = demoId<sdurws::ird::core::RevisionId>("demo-rev");
    id.task.run = demoId<sdurws::ird::core::RunId>("demo-run");
    id.task.attempt.value = 1u;
    id.plannedSampleCount = kDemoSteps;
    DynamicsSeriesBuilder builder;
    for (std::size_t s = 0; s < kDemoSteps; ++s) {
        builder.addSample(demoRow(s, 0));
        builder.addSample(demoRow(s, 1));
    }
    return builder.finalize(id);  // 5 刻度全到→Complete
}

}  // namespace

/**
 * @brief harness 入口（QApplication＋装配门面＋演示缝注入＋主面板挂位）。
 *
 * @param argc [in] Qt 参数计数
 * @param argv [in] Qt 参数向量
 * @return 进程退出码（0＝正常退出）
 */
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle(QObject::tr("dynamics 插件验证 harness（WP-17-T09）"));

    // ---- 演示数据现产（缝组装侧——真实归档链路归宿主装配层）：序列
    //      →曲线投影/回放数据/峰值行集各产一次并缓存（面板每次取值
    //      零重算——演示缝简单性优先；真实缝由归档读取承担）。 --------
    const DynamicsSeries demoSeries = makeDemoSeries();
    const CurveProjection demoCurves =
        DynamicsCurveProjector{}.projectCurves(demoSeries);
    const ReplayData demoReplay =
        DynamicsReplayProjector{}.buildReplayData(demoSeries);
    const std::vector<PeakRecord> demoPeaks =
        DynamicsEnvelopeCalculator{}.computePeaks(demoSeries);

    // ---- 装配门面（产品入口——真实描述符＋五命令目录＋面板工厂）。
    DynamicsPluginAssembly bundle = createDynamicsPluginAssembly();

    // ---- 会话事实注入（就绪投影呈现素材——演示态：输入完整/无在途
    //      任务/无判定；真实事实归域就绪校验/execution，装配层注入）。
    bundle.session().epoch = 1;
    bundle.session().writable = true;
    bundle.session().inputComplete = true;

    // ---- 演示服务缝（归档读取三缝＝进程内现取；命令出口＝回显——
    //      真实 CommandRegistry 接线归宿主装配批次，不虚构提交语义）。
    sdurws::ird::dynamics::DynPanelServices services;
    services.curveSource = [&demoCurves]() { return demoCurves; };
    services.replaySource = [&demoReplay]() { return demoReplay; };
    services.peakLocate = [&demoSeries, &demoPeaks](
                              std::uint32_t jointIndex,
                              int tokenIndex) -> std::optional<DynPeakJump> {
        // 定位链路＝计算库峰值定位器（消费真实统计行集——零重算），
        // 翻译为面板跳转值（缝组装侧职责——翻译单点）。
        const std::optional<PeakRecord> record =
            DynamicsPeakLocator{}.locate(demoSeries, demoPeaks, jointIndex,
                                         tokenIndex);
        if (!record.has_value()) {
            return std::nullopt;  // 无峰关节——合法空态透传
        }
        DynPeakJump jump;
        jump.jointIndex = jointIndex;
        jump.tokenIndex = tokenIndex;
        jump.value = record->value;
        jump.tPeakS = record->tPeakS;
        jump.segmentIndex = record->segmentIndex;
        return jump;
    };
    services.commandSubmit = [](const std::string& commandToken) {
        qDebug("dynamics command submit: %s", commandToken.c_str());
    };
    services.commandAvailability =
        [](const std::string&) { return true; };  // 演示态全部可用
    services.textResolver = [](const std::string& titleKey) {
        return titleKey;  // 键名回显（宿主 UiText 资源接线归装配批次）
    };
    bundle.setServices(services);

    // ---- 面板挂位（descriptor.panels 工厂现调——主面板中央位；工厂
    //      产物归调用方接管所有权）。
    QWidget* mainPanel = nullptr;
    for (const auto& panel : bundle.descriptor.panels) {
        mainPanel = panel.factory();  // 主面板（工作流页＋曲线视图合一）
    }
    window.setCentralWidget(mainPanel);
    window.resize(1100, 720);

    std::cout << "[dynamics-harness] pluginId=" << bundle.descriptor.pluginId
              << " stage=" << bundle.descriptor.stageToken
              << " panels=" << bundle.descriptor.panels.size()
              << " commands=" << bundle.descriptor.commands.size() << std::endl;

    window.show();
    return app.exec();
}
