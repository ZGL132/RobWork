/**
 * @file   Commands.hpp
 * @brief  dynamics 领域命令适配器（units/dynamics.md §10.7
 *         IDynamicsCommandHandler 的具体实现载体）——§9.5 五命令 token
 *         词表＋零修订会话契约＋命令受理语义（零计算逻辑）。
 *
 * 设计依据：
 *   - units/dynamics.md §10.7（IDynamicsCommandHandler 契约——"领域命令
 *     适配器（O13 边界：只是适配器——不是全局命令注册表、不是 UI 命令
 *     权威；命令注册权威＝ui CommandRegistry，命令 token 见 §9.5 表）"；
 *     "硬边界：处理器内零动力学计算（组装快照/提交任务/投影查询）；
 *     零修订（全部会话命令，AT-04）；不注册全局快捷键"；R1 标注
 *     "WP-17-T08/T09"）
 *   - units/dynamics.md §9.5（UI 协作表五命令——analyze/show-curves/
 *     locate-peak/replay-at/export-curve-data，修订列全部"无"；replay-at
 *     行的 View3DSessionPoseContract 四常量口径）
 *   - units/dynamics.md §2.4 O13（领域命令适配器所有权归本单元）、
 *     §3.2 布局表 Commands.hpp 行（"领域命令适配器，零计算逻辑"）、
 *     §10.0（副作用行"零写盘、零修订、零项目目录写入"）
 *   - 需求 DYN-08（曲线联动/峰值定位/三维时刻回放的命令承载）、AT-04
 *     （预览类交互不产生项目修订——零修订机器可断言）、KIN-06（三维
 *     交互只改变会话姿态）
 *   - 任务契约 tasks/foundation/WP-17-T08.json（acceptance 2："联动与
 *     回放不产生项目修订（会话/研究态零修订，AT-04 同口径）"）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. §10.7 草案抽象接口以**域内具体类** DynamicsCommandHandler 落地
 *      （T03～T06 先例——evidence 适配与 ui CommandRegistry 注册面随
 *      WP-17-T09/T10 装配冻结）；方法签名与卡面一致
 *      （handle(commandToken, payload)→CommandOutcome）。
 *   2. T08/T09 分界（卡 §12 两行口径——T08"联动与回放不产生修订"、
 *      T09"插件界面"）：本头承载**领域侧**的词表、受理语义与零修订
 *      契约（T09 面板与 ui CommandRegistry 的装配消费本面）；真实投影
 *      查询（归档 payload 读取）与任务提交（TaskSubmission）归 T09
 *      装配层——本类 handle 只做词表查表与零修订语义应答，结构性零
 *      计算（本实现 TU 零评估器/统计器消费，契约测试词表扫描钉住）。
 *   3. **View3DSessionPoseContract 四常量的 dynamics 侧承载**：§9.5
 *      replay-at 行要求回放驱动会话姿态遵守 ui 单元
 *      View3DSessionPoseContract 四常量（KIN-06/AT-04 钉住值——
 *      ui.md/View3DContract.hpp：sessionStateOnly=true／
 *      writesDesignModel=false／producesRevision=false／
 *      invalidatesResults=false，"零修订机器可断言"）。R-1/R-2 红线：
 *      dynamics 依赖白名单（卡 §3.2 CMake 行边表五条登记边）不含 ui，
 *      **本单元禁止 include ui 公共头**——故本头以自有常量结构
 *      ReplaySessionContract 承载**同值语义**（字段一一对应、值冻结
 *      相同），对账锚＝两侧各自的契约测试以同一文档出处字面冻结四值
 *      （ui 侧：sdurws_ird_ui_test View3DContractTest 已钉；dynamics
 *      侧：本单元契约测试钉）；T09 装配层（UI 侧）消费 ui 公共头时
 *      两侧常量在同一编译单元会合，值漂移即测试失败。
 *
 * 背景说明（受理/拒绝为什么用 CommandOutcome 而不是异常）：§10.0 的
 *   fail-fast 轨面向"调用方契约违约"（程序员错误——评估入口前置违例）；
 *   命令适配器的消费方是 UI 会话（用户交互态），token 未收录/负载不
 *   匹配属**用户可见的命令不受理**而非进程级缺陷——以 accepted=false
 *   ＋rejectionToken 显式应答（§10.7 outcome 字段"受理/查询句柄"的
 *   受理语义），UI 侧可正常呈现不可用态。异常轨保留给结构性违约（本
 *   类无——纯查表无前置）。
 *
 * 线程安全：无状态、handle 纯函数；多实例并行安全。
 * 确定性：同（token, payload）→同 outcome（词表查表、无环境依赖——
 *   NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_COMMANDS_HPP
#define IRD_DYNAMICS_COMMANDS_HPP

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace sdurws::ird::dynamics {

// =====================================================================
// 命令 token 词表（§9.5 表五行——唯一书写点；契约测试静态钉扎面：
// token 字面值冻结＝跨版本命令契约，改动即 ui CommandRegistry 注册面
// 断链，必须走单元卡增量修订）。
// =====================================================================

/// §9.5 表行 1：发起动力学评估任务（组装快照→提交 worker；修订＝无）。
inline constexpr std::string_view kCmdAnalyze = "dynamics.analyze";
/// §9.5 表行 2：各关节曲线联动（q/q̇/q̈/τ/P 逐关节多曲线＋游标联动——
/// 读归档 payload 投影，数据面见 Replay.hpp CurveProjection）。
inline constexpr std::string_view kCmdShowCurves = "dynamics.show-curves";
/// §9.5 表行 3：峰值定位（曲线游标跳转峰值时刻＋三维姿态同步——数据面
/// 见 Replay.hpp DynamicsPeakLocator）。
inline constexpr std::string_view kCmdLocatePeak = "dynamics.locate-peak";
/// §9.5 表行 4：三维轨迹时刻回放（按 t 驱动会话姿态——数据面见
/// Replay.hpp ReplayData/ReplaySample；四常量契约见 ReplaySessionContract）。
inline constexpr std::string_view kCmdReplayAt = "dynamics.replay-at";
/// §9.5 表行 5：曲线数据导出（经 reporting/io 通道或域 DTO 副本——格式
/// 登记 P-DYN-10，导出执行面归 T09 装配；本词表行仅承载 token）。
inline constexpr std::string_view kCmdExportCurveData = "dynamics.export-curve-data";

/// 命令 token 全集（§9.5 表行序——稳定序；词表封闭：表外 token 一律
/// 不受理）。
inline constexpr std::array<std::string_view, 5> kCommandTokens{
    kCmdAnalyze, kCmdShowCurves, kCmdLocatePeak, kCmdReplayAt, kCmdExportCurveData};

// =====================================================================
// 零修订会话契约（§9.5 replay-at 行"View3DSessionPoseContract 四常量"
// 的 dynamics 侧承载——与 ui 单元 View3DSessionPoseContract 同值对账，
// 文件头登记 3；KIN-06/AT-04 钉住语义）。
// =====================================================================

/**
 * @brief dynamics 会话命令的零修订语义契约（AT-04 同口径——四布尔位
 *        钉住"会话/研究态零修订"，机器可断言）。
 *
 * 字段与 ui 单元 View3DSessionPoseContract（View3DContract.hpp，KIN-06/
 * AT-04）一一对应、值冻结相同：
 *   - sessionStateOnly＝true　命令效果仅是会话级 UI 状态（切换项目/
 *     关闭会话即消亡——不入任何持久层）；
 *   - writesDesignModel＝false　不修改设计模型（模型快照零触——ARC-04）；
 *   - producesRevision＝false　不产生修订（不可变历史 PA-2——§9.5 表
 *     修订列全部"无"；acceptance 2 的机器断言面）；
 *   - invalidatesResults＝false　不触发结果失效（CON-02 当前性正交）。
 *
 * 本结构为 constexpr 值（kReplaySessionContract）——任何一位被实现
 * 改动即契约测试失败（语义变更必须走需求/单元卡修订，代码不得私改）。
 */
struct ReplaySessionContract {
    /// 命令效果仅是会话级 UI 状态（恒 true——KIN-06"只改变会话姿态"）。
    bool sessionStateOnly = true;
    /// 是否修改设计模型（恒 false——KIN-06"不修改设计模型"）。
    bool writesDesignModel = false;
    /// 是否产生修订（恒 false——AT-04"预览不产生项目修订"/§9.5 修订列）。
    bool producesRevision = false;
    /// 是否触发结果失效（恒 false——KIN-06"不触发结果失效"/CON-02 正交）。
    bool invalidatesResults = false;
};

/// 零修订会话契约钉住值（§9.5 五命令共同语义——全部会话命令，修订＝无；
/// constexpr＝编译期常量，契约测试静态断言逐位值）。
inline constexpr ReplaySessionContract kReplaySessionContract{};

// =====================================================================
// 命令负载与受理结果（§10.7 handle 的 [in]/[out] 形态——域内 DTO）。
// =====================================================================

/**
 * @brief 命令负载（§10.7 payload 参数的域内落地——"会话上下文（选中
 *        轨迹运行/工况集/时刻等；零计算语义）"的最小承载）。
 *
 * ★ 诚实边界：归档运行引用/工况集等富上下文随 T09 装配层经 ui 会话
 *   通道传递（ui CommandRegistry 注册面归 T09——文件头登记 2）；本
 *   结构只承载 handle 受理语义所需的**定位参数**（replay-at 的时刻、
 *   locate-peak 的目标关节/通道）。字段值本类不解读、不校验数值范围
 *   （时刻/关节的合法性归消费面——Replay.hpp 的查询空态语义）。
 *
 * commandToken 交叉校验位：允许为空（不校验）；非空时必须与 handle 的
 *   commandToken 参数一致（不一致＝负载与命令错配——受理拒绝，
 *   rejectionToken="payload-token-mismatch"）。
 * 值语义纯结构；线程安全。
 */
struct CommandPayload {
    /// 交叉校验位（可空；语义见类型注释——默认空串＝不校验）。
    std::string commandToken;
    /// replay-at 回放时刻，单位 s（默认 NaN＝未提供——显式无效位模式，
    /// 不伪造 0 时刻；其他命令忽略本字段）。
    double replayTimeS = std::numeric_limits<double>::quiet_NaN();
    /// locate-peak 目标关节序号（0 基链序；其他命令忽略）。
    std::uint32_t jointIndex = 0;
    /// locate-peak 目标峰值 token 序号（0..kPeakTokenCount-1，Envelope.hpp
    /// token 表；其他命令忽略）。
    int peakTokenIndex = 0;
};

/**
 * @brief 命令受理结果（§10.7 outcome 参数的域内落地——"提交结果
 *        （TaskSubmission 受理/投影查询句柄）"的最小承载）。
 *
 * 受理（accepted=true）时四语义位逐位等于 kReplaySessionContract
 * （零修订契约——acceptance 2 的机器断言面：五命令任一受理结果的
 * producesRevision 均为 false）；拒绝（accepted=false）时四语义位全
 * false（未受理的命令不作任何会话效果承诺）＋rejectionToken 非空。
 * 值语义纯结构；线程安全。
 */
struct CommandOutcome {
    /// 命令是否受理（token 在词表且负载一致——拒绝语义见类型注释）。
    bool accepted = false;
    /// 效果仅是会话级 UI 状态（受理时恒 true——kReplaySessionContract）。
    bool sessionStateOnly = false;
    /// 是否修改设计模型（恒 false——零修订契约；受理时保持 false）。
    bool writesDesignModel = false;
    /// 是否产生修订（恒 false——AT-04/acceptance 2 机器断言面）。
    bool producesRevision = false;
    /// 是否触发结果失效（恒 false——零修订契约；受理时保持 false）。
    bool invalidatesResults = false;
    /// 拒绝 token（accepted=false 时非空："unknown-token"＝token 不在
    /// §9.5 词表／"payload-token-mismatch"＝负载交叉校验位与命令不符；
    /// 受理时空串）。
    std::string rejectionToken;
};

// =====================================================================
// 领域命令适配器（§10.7 IDynamicsCommandHandler 的具体实现载体——
// 域内具体类；零计算逻辑、零修订）。
// =====================================================================

/**
 * @brief dynamics 领域命令适配器（§10.7——O13 边界：只是适配器，不是
 *        全局命令注册表、不是 UI 命令权威；命令注册权威＝ui
 *        CommandRegistry，注册面归 WP-17-T09 装配）。
 *
 * 硬边界（§10.7 原文承接——结构性保证）：
 *   - 处理器内**零动力学计算**：本类不消费任何评估器/统计器/投影器
 *     （实现 TU 零对应 include——契约测试词表扫描钉住）；真实投影查询
 *     （Replay.hpp 数据面）与任务提交由 T09 装配层在受理后执行；
 *   - **零修订（AT-04）**：handle 纯函数、零副作用（§10.0 副作用行），
 *     受理结果的 producesRevision 恒 false（kReplaySessionContract）；
 *   - 不注册全局快捷键（§10.7 硬边界行——注册面归 ui）。
 */
class DynamicsCommandHandler {
public:
    DynamicsCommandHandler() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    DynamicsCommandHandler(const DynamicsCommandHandler&) = default;
    DynamicsCommandHandler& operator=(const DynamicsCommandHandler&) = default;

    /**
     * @brief 受理一条 dynamics 领域命令（§10.7 handle；零计算、零修订）。
     *
     * 受理规则（逐步）：
     *   1. commandToken 在 §9.5 词表（kCommandTokens 全集——精确匹配，
     *      大小写敏感：命令 token 是机器契约非自由文本）→继续；否则
     *      拒绝（accepted=false，rejectionToken="unknown-token"）；
     *   2. payload.commandToken 非空且与 commandToken 不一致→拒绝
     *      （accepted=false，rejectionToken="payload-token-mismatch"
     *      ——负载与命令错配，防 UI 侧组装错位）；
     *   3. 受理：accepted=true，四语义位逐位取 kReplaySessionContract
     *      值（sessionStateOnly=true／writesDesignModel=false／
     *      producesRevision=false／invalidatesResults=false——零修订
     *      机器断言面），rejectionToken 清空。
     *
     * @param commandToken [in] 命令 token（§9.5 词表——表外拒绝）
     * @param payload      [in] 会话上下文定位参数（语义见 CommandPayload；
     *                     本方法不解读数值字段——零计算）
     * @return 受理结果（受理＝四语义位＝零修订契约；拒绝＝全 false＋
     *         rejectionToken）
     *
     * 复杂度：O(|词表|)（5 token 精确串比较——常数级）。
     */
    CommandOutcome handle(std::string_view commandToken,
                          const CommandPayload& payload) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_COMMANDS_HPP
