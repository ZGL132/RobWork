/**
 * @file   KinematicsPluginAssembly.cpp
 * @brief  kinematics 插件装配门面实现（plugin/ 私有实现——编入
 *         sdurws_ird_kinematics_plugin；消费面仅见 assembly/ 公共头）。
 *
 * 设计依据：assembly/sdurws/ird/kinematics/KinematicsPluginAssembly.hpp
 * 文件头（本 TU 是其全部方法的实现落点——§3.2"_plugin 随 WP-15-T12"的
 * 装配承诺落地面）。
 */

#include <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>

// 同单元私有头（R-2 不跨单元——本 TU 编入 kinematics 插件目标自身）。
#include "KinPanelChannelTranslation.hpp"  // 通道↔私有值翻译（UI-T64——声明面，定义在本 TU）
#include "KinPanelCommandCatalog.hpp"  // kinematicsPanelRegistration/kinematicsDomainCommands
#include "KinPanelFlows.hpp"           // noteBackgroundResult（完成通知消费——L-K12 迟到判定）
#include "KinPanelTypes.hpp"           // KinPanelServices 及往返值类型（翻译目标——私有完整型）
#include "KinematicsUiModule.hpp"      // 具体模块（构造/转发面）

namespace sdurws {
namespace ird {
namespace kinematics {

// =====================================================================
// 通道↔私有值翻译（UI-T64 翻译单点纪律的执行面——同构投影逐字段拷贝；
// 任何一侧字段漂移都在本 TU 编译点暴露，不允许静默丢字段。具名外链
// 定义——声明面在 plugin/KinPanelChannelTranslation.hpp，PluginPanelTest
// 经该头做逐字段单元断言）
// =====================================================================

/**
 * @brief 通道请求 → 插件私有请求（逐字段——载荷最小面五字段全拷贝）。
 */
KinBackgroundRequest toPrivateRequest(const KinChannelBackgroundRequest& request)
{
    KinBackgroundRequest out;
    // 类别枚举：两侧枚举值序同构（SessionSolve/TaskPointsBatch/RegionCoverage
    // ——通道头注释登记的同构纪律），逐值映射不按数值强转（防枚举值
    // 漂移被静默吞掉）。
    switch (request.kind) {
        case KinChannelBackgroundKind::SessionSolve:
            out.kind = KinBackgroundKind::SessionSolve;
            break;
        case KinChannelBackgroundKind::TaskPointsBatch:
            out.kind = KinBackgroundKind::TaskPointsBatch;
            break;
        case KinChannelBackgroundKind::RegionCoverage:
            out.kind = KinBackgroundKind::RegionCoverage;
            break;
    }
    out.snapshotId = request.snapshotId;
    out.configDigest = request.configDigest;
    out.epoch = request.epoch;
    return out;
}

/**
 * @brief 通道回执 → 插件私有回执（逐字段——backgroundSubmit 翻译闭包
 *        的返回面：面板消费私有签名，通道回执须翻回私有值）。
 */
KinBackgroundAck toPrivateAck(const KinChannelBackgroundAck& ack)
{
    KinBackgroundAck out;
    out.accepted = ack.accepted;
    out.reason = ack.reason;
    out.taskRef = ack.taskRef;
    return out;
}

/**
 * @brief 通道完成通知 → 插件私有通知（逐字段——迟到判定三要素）。
 */
KinBackgroundResultNote toPrivateNote(const KinChannelBackgroundResultNote& note)
{
    KinBackgroundResultNote out;
    out.acceptedEpoch = note.acceptedEpoch;
    switch (note.kind) {
        case KinChannelBackgroundKind::SessionSolve:
            out.kind = KinBackgroundKind::SessionSolve;
            break;
        case KinChannelBackgroundKind::TaskPointsBatch:
            out.kind = KinBackgroundKind::TaskPointsBatch;
            break;
        case KinChannelBackgroundKind::RegionCoverage:
            out.kind = KinBackgroundKind::RegionCoverage;
            break;
    }
    out.interrupted = note.interrupted;
    out.summaryText = note.summaryText;
    return out;
}

/**
 * @brief 通道任务点行 → 插件私有行（逐字段——六字段全拷贝）。
 */
KinTaskPointRow toPrivateTaskPoint(const KinChannelTaskPointRow& row)
{
    KinTaskPointRow out;
    out.pointOid = row.pointOid;
    out.label = row.label;
    out.enabled = row.enabled;
    out.outcomeState = row.outcomeState;
    out.outcomeText = row.outcomeText;
    out.requiredCoverageDone = row.requiredCoverageDone;
    return out;
}

/**
 * @brief 通道任务状态行 → 插件私有行（逐字段——四字段全拷贝；面板是
 *        私有行的消费方——taskRows 缝数据流：装配层通道行→面板私有行）。
 */
KinTaskStatusRow toPrivateTaskStatus(const KinChannelTaskStatusRow& row)
{
    KinTaskStatusRow out;
    out.taskRefText = row.taskRefText;
    out.stateLabelKey = row.stateLabelKey;
    out.interrupted = row.interrupted;
    out.percent = row.percent;
    return out;
}

KinematicsPluginAssembly::KinematicsPluginAssembly() = default;

KinematicsPluginAssembly::~KinematicsPluginAssembly() = default;

KinematicsPluginAssembly::KinematicsPluginAssembly(KinematicsPluginAssembly&&) noexcept = default;

KinematicsPluginAssembly& KinematicsPluginAssembly::operator=(
    KinematicsPluginAssembly&&) noexcept = default;

void KinematicsPluginAssembly::bindCommandSubmit(
    std::function<void(const ui::CommandId&)> submit)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandSubmit(std::move(submit));
    }
}

void KinematicsPluginAssembly::bindCommandAvailability(
    std::function<ui::CommandAvailability(const std::string&)> availability)
{
    if (m_impl != nullptr) {
        m_impl->bindCommandAvailability(std::move(availability));
    }
}

void KinematicsPluginAssembly::setServices(KinPanelServices services)
{
    if (m_impl != nullptr) {
        m_impl->setServices(std::move(services));
    }
}

void KinematicsPluginAssembly::installAssemblyChannels(
    KinematicsAssemblyChannels channels)
{
    if (m_impl == nullptr) {
        return;  // 模块缺位＝装配缺陷（门面构造即持有模块——防御面静默
                 // 返回与既有 bind* 同纪律：不虚构注入成功）
    }
    // ---- 通道值面 → 私有服务缝翻译（UI-T64——F-490① 上游批装配半区）。
    // 仅翻译本批五缝（modelView/taskPoints/taskRows/backgroundSubmit 及
    // 完成通知入口）；其余缝（commandHandler/sessionPose/exportWriter/
    // configPersist/ikSolver/fkEvaluator）不触——维持降级语义（契约诚实
    // 边界：本批范围声明）。注意：不能整体替换模块内 KinPanelServices
    // （会清掉既有 bindCommandSubmit 等已注入缝）——经模块的逐缝合并
    // 面（mergeAssemblyChannels）写入。
    KinPanelServices merged;
    merged.modelView = channels.modelView;
    if (channels.taskPoints) {
        // 通道行→私有行翻译闭包（每次刷新现调——零缓存纪律随缝传递）。
        merged.taskPoints = [fn = channels.taskPoints]() {
            std::vector<KinTaskPointRow> out;
            for (const KinChannelTaskPointRow& row : fn()) {
                out.push_back(toPrivateTaskPoint(row));
            }
            return out;
        };
    }
    if (channels.taskRows) {
        merged.taskRows = [fn = channels.taskRows]() {
            std::vector<KinTaskStatusRow> out;
            for (const KinChannelTaskStatusRow& row : fn()) {
                out.push_back(toPrivateTaskStatus(row));
            }
            return out;
        };
    }
    if (channels.backgroundSubmit) {
        // 提交缝翻译闭包（请求/回执双向逐字段——受理语义原样透传）。
        merged.backgroundSubmit = [fn = channels.backgroundSubmit](
                                      const KinBackgroundRequest& request) {
            // 私有请求→通道请求：与 toPrivateRequest 互逆的逐字段拷贝
            // （面板侧只产私有值——翻译在这里就地做，不引入第二跳）。
            KinChannelBackgroundRequest channelRequest;
            switch (request.kind) {
                case KinBackgroundKind::SessionSolve:
                    channelRequest.kind = KinChannelBackgroundKind::SessionSolve;
                    break;
                case KinBackgroundKind::TaskPointsBatch:
                    channelRequest.kind = KinChannelBackgroundKind::TaskPointsBatch;
                    break;
                case KinBackgroundKind::RegionCoverage:
                    channelRequest.kind = KinChannelBackgroundKind::RegionCoverage;
                    break;
            }
            channelRequest.snapshotId = request.snapshotId;
            channelRequest.configDigest = request.configDigest;
            channelRequest.epoch = request.epoch;
            return toPrivateAck(fn(channelRequest));
        };
    }
    m_impl->mergeAssemblyChannels(std::move(merged));
}

std::string KinematicsPluginAssembly::noteAssemblyBackgroundResult(
    const KinChannelBackgroundResultNote& note)
{
    if (m_impl == nullptr) {
        return "完成通知丢弃：模块未装配";  // 诚实反馈——不虚构消费成功
    }
    // 翻译单点（通道 note→私有 note）＋流函数消费（迟到判定/中断如实/
    // 状态反馈文本产出——KinPanelFlows 权威语义，门面零复制判定逻辑）。
    return noteBackgroundResult(m_impl->session(), toPrivateNote(note));
}

void KinematicsPluginAssembly::bindSessionFacts(core::ContentIdentity snapshotId,
                                                std::uint64_t epoch,
                                                bool writable,
                                                const AnalysisConfiguration& config)
{
    if (m_impl == nullptr) {
        return;  // 模块缺位＝装配缺陷——静默返回与既有 bind* 同纪律
    }
    // 会话事实四值写入（私有聚合完整型在本 TU 可见——翻译单点面；
    // 调用方语义见公共头方法注）。
    KinModuleSessionState& session = m_impl->session();
    session.snapshotId = snapshotId;
    session.epoch = epoch;
    session.writable = writable;
    session.savedConfig = config;
}

void KinematicsPluginAssembly::bindTextResolver(
    std::function<QString(const std::string& titleKey)> resolve)
{
    if (m_impl != nullptr) {
        m_impl->bindTextResolver(std::move(resolve));
    }
}

KinModuleSessionState& KinematicsPluginAssembly::session() const
{
    // 会话权威态访问（装配层注入快照绑定/配置基线——模块内唯一载体）。
    return m_impl->session();
}

void KinematicsPluginAssembly::refreshFromSession()
{
    if (m_impl != nullptr) {
        m_impl->refreshFromSession();
    }
}

KinematicsSharedSurfaceHandles KinematicsPluginAssembly::sharedSurfaceProviders()
{
    // 转发模块句柄（门面自持结构与模块内结构逐字段同形——转换零语义）。
    const KinematicsUiModule::SharedSurfaceHandles handles =
        m_impl != nullptr ? m_impl->sharedSurfaceProviders()
                          : KinematicsUiModule::SharedSurfaceHandles{};
    KinematicsSharedSurfaceHandles out;
    out.treeNodes = handles.treeNodes;
    out.propertyPages = handles.propertyPages;
    return out;
}

void KinematicsPluginAssembly::attachSelectionService(ui::SelectionService& service)
{
    if (m_impl != nullptr) {
        m_impl->attachSelectionService(service);  // 适配器接线（RAII 句柄随模块）
    }
}

bool KinematicsPluginAssembly::applyHostJointState(const std::vector<double>& q)
{
    // D8/D9 会话姿态承接（零修订结构性保证——模块方法注释为权威语义）。
    return m_impl != nullptr && m_impl->applyHostJointState(q);
}

KinematicsPluginAssembly createKinematicsPluginAssembly()
{
    KinematicsPluginAssembly result;
    // 具体模块（接口 unique_ptr 持有——消费方只见 IPluginUiModule 三方法）。
    auto concrete = std::make_unique<KinematicsUiModule>();
    result.m_impl = concrete.get();
    result.module = std::move(concrete);

    // 装配描述符（§10.9 形状——登记数据权威在 KinPanelCommandCatalog 现产）：
    //   pluginId/stages/capabilities＝kinematicsPanelRegistration 值；
    //   commands＝八条域命令描述符（§9.8 表七行八个 id）；
    //   panels＝两条登记记录（主面板＝面板表行 1~4 的合一 Tab 容器；
    //   高级面板＝行 5，advanced=true——映射规则见 assembly 公共头注）。
    const KinPanelRegistration registration = kinematicsPanelRegistration();
    result.descriptor.pluginId = registration.pluginId;
    result.descriptor.titleKey = registration.titleKey;
    result.descriptor.stages = {registration.stage};
    result.descriptor.capabilities.providesStagePanel =
        registration.providesStagePanel;
    result.descriptor.capabilities.providesReadonlyProjection =
        registration.providesReadonlyProjection;
    result.descriptor.capabilities.registersCommands =
        registration.registersCommands;
    result.descriptor.commands = kinematicsDomainCommands();
    // 主面板登记记录（行 1~4 合一——工厂闭包转接模块 createPanel）。
    ui::PanelRegistration mainPanel;
    mainPanel.stage = registration.stage;
    mainPanel.titleKey = registration.panels.at(0).titleKey;
    mainPanel.advanced = false;
    KinematicsUiModule* moduleRaw =
        static_cast<KinematicsUiModule*>(result.module.get());
    ui::IPluginUiModule* moduleRef = result.module.get();
    mainPanel.factory = [moduleRef]() -> QWidget* {
        // 静态转换安全面：本门面产出的模块恒为 kinematics 具体模块（单一
        // 来源）——createPanel 经接口面不可达，需具体类型。
        return static_cast<KinematicsUiModule*>(moduleRef)->createPanel();
    };
    result.descriptor.panels.push_back(std::move(mainPanel));

    // 高级面板登记记录（行 5——求解配置，advanced=true；工厂闭包同构）。
    ui::PanelRegistration advancedPanel;
    advancedPanel.stage = registration.stage;
    advancedPanel.titleKey = registration.panels.at(4).titleKey;
    advancedPanel.advanced = true;
    advancedPanel.factory = [moduleRaw]() -> QWidget* {
        return moduleRaw->createAdvancedConfigPanel();
    };
    result.descriptor.panels.push_back(std::move(advancedPanel));
    return result;
}

}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws
