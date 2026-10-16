/**
 * @file   KinHostServicesAssembly.hpp
 * @brief  kinematics 面板服务缝的宿主装配半区（KIN-P4——审查报告 §4
 *         第一条"降级服务逐项接通"的宿主侧实现；ui 插件私有，编入
 *         sdurws_ird_ui_plugin，测试目标同源直编）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.7/§9.8（D-KIN-5 设默认命令归宿 modeling、
 *     L-K9/L-K10 数据流；命令门面 KinematicsCommandHandler 的装配义务
 *     ——宿主以适配器实现 IKinProjectCommandGateway 注入）；§3.2
 *     （R-1/R-2——宿主只消费各单元公共头）；
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §4：
 *     KinematicsPluginAssembly.cpp:163 起保留的降级服务（FK/IK、命令
 *     门面、会话姿态、导出、配置持久化）须逐项接通或登记不可用原因
 *     ——本 TU 即接通面：命令门面网关/根对象补丁缝（modeling 编解码
 *     权威）、导出写出（io AtomicFile）、求解配置持久化（QSettings×
 *     AnalysisConfigurationCodec）、具名 Home 来源（named-pose-set
 *     保留键 homeConfiguration）；
 *   - O-37/O-31 裁决先例（宿主适配器转译到真端口——kinematics 门面只
 *     依赖自有最小投影接口）。
 *
 * 背景说明（第一读者须知——本 TU 与 KinEvaluationChannel 的分工）：
 *   KinEvaluationChannel 承载"评估执行通道"（后台求解/批量/覆盖的受理
 *   与执行）；本 TU 承载"服务缝实体"（命令门面/写出器/持久化/命名位姿
 *   ——产品的注入物本体）。两者同属宿主装配半区，由 UiPlugin 在
 *   wireKinematicsEvaluationChannel 拍一次装配。
 *
 * 线程约束：全部产品入口仅 UI 线程调用（面板缝纪律传递；gateway 的
 *   ①端口内部命令执行槽串行——project §6.1）。
 */
#ifndef IRD_UI_PLUGIN_KINHOSTSERVICESASSEMBLY_HPP
#define IRD_UI_PLUGIN_KINHOSTSERVICESASSEMBLY_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/kinematics/AnalysisConfig.hpp>  // AnalysisConfiguration（配置持久化值面）
#include <sdurws/ird/kinematics/Commands.hpp>   // IKinProjectCommandGateway/KinProjectBaseline/
                                                // KinCommandEnvelope/CommandSubmission/RootDesignPatcher
#include <sdurws/ird/kinematics/KinTypes.hpp>   // TcpRef（补丁入参）
#include <sdurws/ird/project/CommandService.hpp>  // project::CommandEnvelope/CommandResult（提交闭包值面）
#include <sdurws/ird/project/QueryPort.hpp>       // project::IProjectQueryPort（基线取数端口）
#include <sdurws/ird/runtime/Errors.hpp>        // runtime::Expected

class QSettings;  // 前置声明（配置持久化载体——Qt 头不入本头，实现 TU 消费）

namespace sdurws {
namespace ird {
namespace ui {

// =====================================================================
// KinProjectCommandGateway——设默认命令门面的宿主网关（①/②端口投影）
// =====================================================================

/**
 * @brief IKinProjectCommandGateway 的产品实现（KIN-P4——审查 §4"命令
 *        门面"接通面：把 kinematics 门面的最小投影转译到真 project
 *        端口）。
 *
 * 转译语义（O-37/O-31 裁决先例——宿主适配器形态）：
 *   - fetchBaseline()：IProjectQueryPort::branchTips() 首条（INV-M3 单
 *     默认分支——与宿主应用编排同源取数）＋HEAD 闭包扫描 robot-design
 *     根对象（token 常量＝kinematics 公共头 kRobotDesignObjectTypeToken
 *     与 modeling kRobotDesignObjectType 同串）＋tryObject 取根对象
 *     canonical 字节（modeling Codec 消费面——本网关零解码）。闭包无
 *     根＝NoDevice 错误轨（"无可用设备"业务出口——§9.6）；端口数据
 *     侧异常（StoreError 族）转译为 NoDevice 错误轨＋detail 留痕（门面
 *     契约的值面错误轨——异常不穿越门面）。
 *   - submit(envelope)：KinCommandEnvelope → project::CommandEnvelope
 *     逐字段转译（两结构同构——分支/期望基线/token/版本/载荷字节）→
 *     提交闭包（宿主组装——含确认交互桥）→ 四态结果两态投影（Committed
 *     携新修订；Rejected/Aborted/Failed 一律 NotCommitted＋诊断透传——
 *     细分理由词表属 project 权威语义，门面不复制第二份词表，本网关以
 *     诊断记录承载原因文本）。
 *
 * 生命周期与所有权：宿主构造并持有（非 owning 成员指针——存活期须
 *   覆盖 kinematics::KinematicsCommandHandler 使用期）；提交闭包值持有。
 * 线程约束：仅 UI 线程（面板缝纪律传递）。
 */
class KinProjectCommandGateway final : public kinematics::IKinProjectCommandGateway {
public:
    /// 命令信封提交闭包（宿主组装——把 project::CommandEnvelope 递交真
    /// ①端口并回带四态结果；可注入确认交互桥）。可空＝装配违约（构造
    /// 即 fail-fast——不留"半装配"网关）。
    using SubmitFn = std::function<project::CommandResult(
        project::CommandEnvelope)>;

    /**
     * @brief 构造网关（UI 线程——装配期一次）。
     *
     * @param query  [in] project 查询端口（非 owning；空＝装配违约）
     * @param submit [in] 信封提交闭包（空 function＝装配违约）
     *
     * @throws std::invalid_argument query 为空或 submit 为空
     */
    KinProjectCommandGateway(project::IProjectQueryPort* query, SubmitFn submit);

    [[nodiscard]] runtime::Expected<kinematics::KinProjectBaseline,
                                    kinematics::KinematicsError>
        fetchBaseline() const override;

    kinematics::CommandSubmission submit(
        const kinematics::KinCommandEnvelope& envelope) override;

private:
    /// project 查询端口（非 owning——基线取数与根对象字节读取）。
    project::IProjectQueryPort* m_query;
    /// 信封提交闭包（值持有——宿主组装的①端口递交面）。
    SubmitFn m_submit;
};

// =====================================================================
// 根对象补丁缝——RootDesignPatcher 的产品实现（modeling 编解码权威保持）
// =====================================================================

/**
 * @brief 设默认 TCP 的根对象字节补丁（KIN-P4——Commands.hpp RootDesign-
 *        Patcher 缝的产品实现）。
 *
 * 实现（零第二套编解码器——NFR-MNT-03/04）：modeling::RobotDesignCodec
 * 解码基线根字节 → 写 defaultTcp＝(tcp.toolObject, tcp.tcpKey)（仅
 * 覆盖该字段——其余字段逐位保留）→ 重新编码为 canonical 字节。解码/
 * 编码失败（数据侧）＝nullopt（门面转 KIN-ROOT-BYTES-ILLEGAL 诊断，
 * 零提交）。
 *
 * 纯函数（编解码器无状态）；线程安全；UI 线程调用（缝纪律）。
 */
std::optional<std::vector<std::uint8_t>> patchRootDesignDefaultTcp(
    const std::vector<std::uint8_t>& baselineRootBytes,
    const kinematics::TcpRef& defaultTcp);

// =====================================================================
// 导出写出缝——io AtomicFile 装配（L-K10 的真实写出半区）
// =====================================================================

/**
 * @brief 导出写出缝工厂（KIN-P4——审查 §4"导出"接通面）。
 *
 * 写出语义：目标路径经 dirProvider 解析目录＋文件名拼合 → io 原子写
 * （AtomicFile：prepare→write→commit——失败旧文件完好，父目录由本
 * 闭包代建〔create_directories——io 层不代建目录的装配层补偿〕）。
 *
 * @param dirProvider [in] 导出目录解析闭包（返回目标目录绝对路径——
 *                    产品闭包从环境变量 IRD_KIN_EXPORT_DIR 取值，缺省
 *                    用户应用数据目录；测试注入临时目录。返回的目录
 *                    本闭包代建）。空 function＝装配违约（构造即抛）。
 * @return 写出缝（签名与通道值面 KinChannelExportWriterFn 同构——
 *         参数＝目标文件名＋编码完成的内容文本；false＝写出失败）
 *
 * @throws std::invalid_argument dirProvider 为空
 *
 * 仅 UI 线程调用（面板缝纪律）。
 */
std::function<bool(const std::string&, const std::string&)>
makeKinExportWriter(std::function<std::filesystem::path()> dirProvider);

// =====================================================================
// 求解配置持久化——QSettings×canonical 编解码（PM-14 用户级通道）
// =====================================================================

/// QSettings 键（kinematics 求解配置——base64 载荷的存储键；载体组织/
/// 应用名归宿主 QSettings 装配，本常量只定键面）。
inline constexpr char kKinConfigSettingsKey[] = "kinematics/solveConfig";

/**
 * @brief 保存缝工厂（KIN-P4——审查 §4"配置持久化"保存半区）。
 *
 * 实现：AnalysisConfigurationCodec 编码为 canonical 字节 → base64 文本
 * 入 QSettings（sync 即落盘）→ true。编码失败（非法配置）＝false（调
 * 用方如实呈现"保存失败"——非法配置本不应到达保存缝，防御面）。
 *
 * @param settings [in] 用户级设置载体（非 owning——宿主持有；空＝装配
 *                 违约，构造即抛）
 * @return 保存缝（签名与通道值面 KinChannelConfigPersistFn 同构）
 *
 * @throws std::invalid_argument settings 为空
 *
 * 仅 UI 线程调用。
 */
std::function<bool(const kinematics::AnalysisConfiguration&)>
makeKinConfigPersist(QSettings* settings);

/**
 * @brief 恢复读取（KIN-P4 保存/恢复闭环的恢复半区——装配期调用）。
 *
 * 实现：读 base64 文本 → 解码 canonical 字节 → validateAnalysisConfig-
 * uration 合法域校验。任何一步失败（键不存在/字节损坏/非法值域）＝
 * nullopt——调用方回退缺省基线（I-KIN-4 seed≥1）并留痕，绝不把半损坏
 * 配置静默钳制成合法配置（NFR-COR-03）。
 *
 * @param settings [in] 同保存缝载体（可空＝nullopt——恢复不可用如实）
 * @return 恢复的配置（合法域内）；nullopt＝无持久化配置或损坏
 *
 * 仅 UI 线程调用。
 */
std::optional<kinematics::AnalysisConfiguration> restoreKinConfig(
    QSettings* settings);

// =====================================================================
// 具名 Home 来源缝——named-pose-set 保留键解析（KIN-P3 A09 的装配半区）
// =====================================================================

/// 命名位姿集加载闭包（宿主从项目闭包现取——返回 modeling::PoseSet 值；
/// nullopt＝闭包无位姿集/解码失败，如实空态）。前置声明避免头拖
/// modeling 类型——签名以 std::function 包裹实现侧具体型。
struct KinPoseSetLoader {
    /// 加载函数（UI 线程现取——零缓存）。
    std::function<std::optional<std::vector<std::vector<double>>>()> entries;
};

/**
 * @brief 具名 Home 来源缝工厂（KIN-P4——审查 §4"会话姿态"接通半区：
 *        复位 Home 按钮在正式程序可用的前提缝）。
 *
 * 实现：从加载闭包的位姿集条目中找保留键 homeConfiguration（modeling
 * kHomeConfigurationPoseKey 同串——MDL-17 保留键纪律）→ 返回其关节
 * 向量（rad|m 链序 SI 真值）。键缺失＝nullopt（面板给"未定义 Home"
 * 明确提示——绝不以当前姿态充当 Home，审查 A09 语义）。
 *
 * @param loader [in] 位姿集条目加载闭包（空 function＝装配违约）
 * @return Home 来源缝（签名与通道值面 KinChannelHomePoseFn 同构）
 *
 * @throws std::invalid_argument loader.entries 为空
 *
 * 仅 UI 线程调用；纯查询幂等（零缓存——每次点击现取）。
 */
std::function<std::optional<std::vector<double>>()> makeKinHomePoseProvider(
    KinPoseSetLoader loader);

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_UI_PLUGIN_KINHOSTSERVICESASSEMBLY_HPP
