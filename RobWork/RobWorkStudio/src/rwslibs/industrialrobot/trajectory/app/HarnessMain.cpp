/**
 * @file   HarnessMain.cpp
 * @brief  trajectory 插件面板开发验证 harness（sdurws_ird_trajectory_app）
 *         ——“插件开发完成即可手动 GUI 验证”的 trajectory 侧载体（单元卡
 *         §17.4 Windows GUI 手动点验流程的宿主位；WP-16-T12）。
 *
 * 设计依据：
 *   - units/trajectory.md §17.4（GUI/插件流程——手动点验流程设计：①构建
 *     →②启动本 harness→③轨迹页可见、命令面板可达→④动画播放（会话姿
 *     态零修订）→⑤曲线查看→⑥导出等命令经面板呈现→⑦截图留痕入
 *     traceability/builds/wp16-t12/）；§16.1（T12 落位建 _app harness 做
 *     人工点验，不入产品交付路径）；DTB §5.1（插件 GUI 手动验证通道——
 *     先例＝dynamics_app〔WP-17-T09〕/kinematics_app〔WP-15-T12〕/
 *     modeling_app〔WP-13-T15〕）；
 *   - 任务契约 tasks/foundation/WP-16-T12.json（acceptance 1——界面链
 *     路；无人值守门禁不做 GUI 运行验证：本 harness 的**构建**留痕属本
 *     任务交付，**运行**属手动验证流程，未启动如实登记不标注通过
 *     ——AGENTS §4.2）。
 *
 * ★ 与产品装配层的关系（dynamics_app 先例同款口径）：
 *   本文件是**开发工具**，不是产品交付路径——面板由装配层创建与持有
 *   的产品契约不变（ui.md §10.9）；本 harness 在进程内扮演装配层的最
 *   小角色：经装配门面创建模块与面板，注入演示服务缝（黄金轨迹现产的
 *   查表/曲线数据），把真实面板与真实装配描述符跑起来供人工验证。
 *   真实 IPluginUiRegistrar 注册链路的宿主半区（真实 registrar 工厂消
 *   费＋ui 库链接面）归宿主装配批次收口——本 harness 不虚构注册（消费
 *   面＝assembly 门面的描述符与面板工厂，登记值经 stdout 打印供点验
 *   核对）。
 *
 * 真值边界（诚实声明，防误读验证结论）：
 *   - 真实部分：真实装配门面（描述符＋七命令目录＋面板工厂）、真实
 *     面板控件链路（就绪投影/命令受理/动画查表驱动/曲线视图）、真实
 *     计算库数据面（黄金轨迹 payload→TrajectoryPlaybackTable 查表与
 *     曲线投影器现产——查表缝的组装侧消费真实查表面）；
 *   - 演示部分：归档读取缝（动画查表/曲线投影）在进程内现取现算（真
 *     实归档链路＝归档 payload 读取，归宿主装配层）、命令提交出口以
 *     回显承载（真实命令注册表接线归宿主装配批次——不虚构提交语义）、
 *     文案解析器以键名回显（宿主文案资源接线归装配批次——UX-02 键值
 *     分离的产品形态见装配登记）、会话姿态驱动缝以空实现承载（真实
 *     View3D 桥归宿主装配批——KIN-06 零修订语义经契约测试钉住）；
 *   - 不覆盖：三维视图姿态同步的真实效果（帧读数为会话姿态素材呈现）。
 *
 * 用法（开发期交互验证——单元卡 §17.4 流程第②步）：
 *   sdurws_ird_trajectory_app
 *     启动即见轨迹工作流页（就绪投影＋七命令＋最近命令＋动画播放区）
 *     ＋曲线视图页（关节/通道下拉＋折线）。手动可验：点命令看受理/提
 *     交回显；拖进度/点播放看帧读数与查表定位；切关节/通道看曲线联动。
 *
 * 目标形态（诚实登记）：本 TU 消费 rw::math::Q（演示 payload 构造）与
 * TrajectoryPlaybackTable/projectTrajectoryCurves（src/Playback.cpp 库
 * 符号——集成条件源），故 app 目标随集成模式条件创建（CMake
 * if(TARGET sdurw_kinematics) gating——T04 条件源同款口径）；冒烟树不
 * 建 app（GUI 手动点验流程第①步即"构建集成模式"，冒烟口径＝目标注册
 * ＋include 路径，不承载 GUI 点验）。
 *
 * 线程模型：单线程——QApplication/缝/面板全部 main 线程（卡 §3.4）。
 */

#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QWidget>

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <rw/math/Q.hpp>

#include <sdurws/ird/trajectory/Playback.hpp>  // TrajectoryPlaybackTable/
                                                //   projectTrajectoryCurves（缝
                                                //   组装侧——真实计算库数据面）
#include <sdurws/ird/trajectory/TimeParam.hpp>  // TimeParameterization/TimedSample
                                                //   （演示 payload 值类型）
#include <sdurws/ird/trajectory/TrajectoryPluginAssembly.hpp> // 装配门面（产品入口）
#include "plugin/TrajPanelTypes.hpp"           // TrajPanelServices/TrajPlaybackSample
                                                //   （服务缝值——app 目标 include
                                                //   单元根，同单元私有头直指）

using sdurws::ird::trajectory::TimeParameterization;
using sdurws::ird::trajectory::TimedSample;
using sdurws::ird::trajectory::TrajPlaybackSample;
using sdurws::ird::trajectory::TrajectoryCurveChannel;
using sdurws::ird::trajectory::TrajectoryCurveSeries;
using sdurws::ird::trajectory::TrajectoryPlaybackTable;
using sdurws::ird::trajectory::TrajectoryPluginAssembly;
using sdurws::ird::trajectory::createTrajectoryPluginAssembly;
using sdurws::ird::trajectory::projectTrajectoryCurves;

namespace {

// =====================================================================
// 演示黄金轨迹（两关节×五时刻——解析式手算可核；harness 演示数据零业
// 务语义，TrajectoryPlaybackTable 构造校验〔首末样本对齐 total、时刻非
// 递减、维度一致〕全部满足）。
//
// 时间轴 t ∈ {0, 0.5, 1, 1.5, 2} s，总时长 2 s：
//   关节 0（转动，rad）：q = 0.5·t（匀速）——qd = 0.5 rad/s、qdd = 0
//   关节 1（移动，m）：  q = t²（匀加速）——qd = 2t m/s、qdd = 2 m/s²
// =====================================================================

/// 演示时刻表，单位 s。
constexpr double kDemoT[5] = {0.0, 0.5, 1.0, 1.5, 2.0};
/// 演示时刻数。
constexpr std::size_t kDemoSteps = 5;

/**
 * @brief 组装演示时间参数化 payload（查表/曲线投影的公共数据源——缝
 *        组装侧；真实链路＝归档 payload 读取，归宿主装配层）。
 *
 * @return 演示 payload（两关节五样本——值语义）
 */
TimeParameterization makeDemoPayload()
{
    TimeParameterization payload;
    payload.motionLawToken = "quintic-spline-c2";  // 运动律词表唯一值（演示
                                                   //   数据与真实形态同词表）
    payload.totalDurationS = 2.0;                  // 总时长（s）
    payload.knotTimesS = {0.0, 1.0, 2.0};          // 结点（s；首末对齐 total）
    payload.samples.reserve(kDemoSteps);
    for (std::size_t i = 0; i < kDemoSteps; ++i) {
        const double t = kDemoT[i];
        TimedSample sample;
        sample.t = t;              // s
        sample.q = rw::math::Q(2); // 两关节位置
        sample.qd = rw::math::Q(2);
        sample.qdd = rw::math::Q(2);
        sample.q[0] = 0.5 * t;     // rad（匀速）
        sample.q[1] = t * t;       // m（匀加速）
        sample.qd[0] = 0.5;        // rad/s
        sample.qd[1] = 2.0 * t;    // m/s
        sample.qdd[0] = 0.0;       // rad/s²
        sample.qdd[1] = 2.0;       // m/s²
        payload.samples.push_back(sample);
    }
    return payload;
}

/**
 * @brief 查表帧→纯值呈现帧的翻译（缝组装侧单点——TrajPlaybackSample
 *        注的"翻译单点"承载；逐轴拷贝，单位/轴序与帧值一致）。
 *
 * @param frame [in] 查表帧（PlaybackFrame——rw::math::Q 载体）
 * @return 纯值呈现帧（std::vector<double> 载体——面板缝出参面）
 */
TrajPlaybackSample toPanelSample(const sdurws::ird::trajectory::PlaybackFrame& frame)
{
    TrajPlaybackSample sample;
    sample.tS = frame.t;                        // s
    sample.q.resize(static_cast<std::size_t>(frame.q.size()));
    sample.qd.resize(static_cast<std::size_t>(frame.qd.size()));
    for (std::size_t i = 0; i < sample.q.size(); ++i) {
        sample.q[i] = frame.q[static_cast<int>(i)];   // rad|m 逐轴
    }
    for (std::size_t i = 0; i < sample.qd.size(); ++i) {
        sample.qd[i] = frame.qd[static_cast<int>(i)]; // rad/s|m/s 逐轴
    }
    sample.atSample = frame.atSample;
    sample.segmentIndex = frame.segmentIndex;   // 段归投影透传（可空）
    return sample;
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
    window.setWindowTitle(QObject::tr("trajectory 插件验证 harness（WP-16-T12）"));

    // ---- 演示数据现产（缝组装侧——真实归档链路归宿主装配层）：payload
    //      →查表实例与三通道曲线投影各产一次（面板每次取值零重算——演
    //      示缝简单性优先；真实缝由归档读取承担）。 ----------------------
    const TimeParameterization demoPayload = makeDemoPayload();
    const TrajectoryPlaybackTable demoTable(demoPayload);
    const std::vector<TrajectoryCurveSeries> demoPosition =
        projectTrajectoryCurves(demoPayload, TrajectoryCurveChannel::Position);
    const std::vector<TrajectoryCurveSeries> demoVelocity =
        projectTrajectoryCurves(demoPayload, TrajectoryCurveChannel::Velocity);
    const std::vector<TrajectoryCurveSeries> demoAcceleration =
        projectTrajectoryCurves(demoPayload,
                                TrajectoryCurveChannel::Acceleration);

    // ---- 装配门面（产品入口——真实描述符＋七命令目录＋面板工厂）。
    TrajectoryPluginAssembly bundle = createTrajectoryPluginAssembly();

    // ---- 会话事实注入（就绪投影呈现素材——演示态：输入完整/无在途
    //      任务/无判定；真实事实归域就绪校验/execution，装配层注入）。
    bundle.session().epoch = 1;
    bundle.session().writable = true;
    bundle.session().inputComplete = true;

    // ---- 演示服务缝（归档读取三缝＝进程内现取；命令出口＝回显——
    //      真实命令注册表接线归宿主装配批次，不虚构提交语义；会话姿态
    //      驱动缝＝空（真实 View3D 桥归宿主装配批——帧读数照常）。 ----
    sdurws::ird::trajectory::TrajPanelServices services;
    services.durationSource = [&demoTable]() {
        return demoTable.totalDurationS();  // s（演示 payload 总时长）
    };
    services.playbackSource = [&demoTable](double tS) {
        // 查表缝组装侧：真实查表面消费（纯插值查表——UI 线程语义由缝
        // 提供方在本 harness 的单线程下满足）＋纯值翻译（单点）。
        return toPanelSample(demoTable.sampleAt(tS));
    };
    services.curvesSource =
        [&demoPosition, &demoVelocity,
         &demoAcceleration](TrajectoryCurveChannel channel)
            -> std::vector<TrajectoryCurveSeries> {
            // 通道词表分派（三值——查表现成投影副本，零重算）。
            switch (channel) {
            case TrajectoryCurveChannel::Position: return demoPosition;
            case TrajectoryCurveChannel::Velocity: return demoVelocity;
            case TrajectoryCurveChannel::Acceleration:
                return demoAcceleration;
            }
            return {};  // 词表外不可达（枚举封闭）——防御性空序列
        };
    services.commandSubmit = [](const std::string& commandToken) {
        qDebug("trajectory command submit: %s", commandToken.c_str());
    };
    services.commandAvailability =
        [](const std::string&) { return true; };  // 演示态全部可用
    services.textResolver = [](const std::string& titleKey) {
        return titleKey;  // 键名回显（宿主文案资源接线归装配批次）
    };
    bundle.setServices(services);

    // ---- 面板挂位（descriptor.panels 工厂现调——主面板中央位；工厂
    //      产物归调用方接管所有权）。
    QWidget* mainPanel = nullptr;
    for (const auto& panel : bundle.descriptor.panels) {
        mainPanel = panel.factory();  // 主面板（工作流页＋曲线视图入口合一）
    }
    window.setCentralWidget(mainPanel);
    window.resize(1100, 720);

    std::cout << "[trajectory-harness] pluginId=" << bundle.descriptor.pluginId
              << " stage=" << bundle.descriptor.stageToken
              << " domainKey=" << bundle.descriptor.readinessDomainKey
              << " panels=" << bundle.descriptor.panels.size()
              << " commands=" << bundle.descriptor.commands.size() << std::endl;

    window.show();
    return app.exec();
}
