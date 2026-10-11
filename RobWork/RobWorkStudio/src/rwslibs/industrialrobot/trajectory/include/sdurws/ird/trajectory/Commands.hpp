/**
 * @file   Commands.hpp
 * @brief  trajectory 领域命令适配器（units/trajectory.md §15.11/§14.5.1
 *         的具体承载）——§14.5.1 七命令 token 词表＋零修订会话契约＋命令
 *         受理语义（零计算逻辑；WP-16-T10 批）。
 *
 * 设计依据：
 *   - units/trajectory.md §14.5.1（领域命令表 TRJ-07 会话能力七行：
 *     plan-sequence/preview-segment/local-replan/play/playback-seek/
 *     export/show-curve——修订列全部"无（AT-04）"；"命令经 ICommandRegistry
 *     装配期注册（CommandDescriptor：id 点分小写、ownerUnit="trajectory"……）"）
 *   - units/trajectory.md §15.11（命令接入基线——"命令描述符数据＋处理器
 *     薄适配（§14.5.1 清单）；命令处理器组装快照并 submit，**零计算逻辑**
 *     （计算在评估器）；无修订命令集（AT-04 语义）；注册经 ui CommandRegistry
 *     （装配期，重复拒绝）"；"project 命令 token 语法争议（P-PR-9）不适用
 *     于本域命令——本域命令走 ui CommandRegistry，非 project ICommandHandler"）
 *   - units/trajectory.md §16.2（零修订与零计算红线机器可断言——"界面预览/
 *     动画/导出不产生项目修订：会话命令零修订（§14.5.1）；View3DSession-
 *     PoseContract 四常量同款（sessionStateOnly=true/writesDesignModel=false/
 *     producesRevision=false/invalidatesResults=false）适用于轨迹播放会话态"）
 *   - 需求 TRJ-07（P1——动画/曲线查看/路点局部重规划/标准轨迹导出的会话
 *     能力承载）、AT-04（双击候选/动画/预览不产生项目修订）、KIN-06（动画
 *     播放驱动会话姿态——§14.5.1 play 行"KIN-06 语义：sessionStateOnly、
 *     producesRevision=false、invalidatesResults=false 四常量契约"）
 *   - 先例：dynamics Commands.hpp（WP-17-T08 落位——§9.5 五命令词表＋零修
 *     订会话契约＋DynamicsCommandHandler 的"适配器零计算"形态；本头同构：
 *     token 词表封闭＋四常量契约 constexpr 钉住＋handle 纯查表）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 T10 注同步）：
 *   1. §15.11 基线以**域内具体类** TrajectoryCommandHandler 落地（T03～T08
 *      先例——ui CommandRegistry 注册面与真实任务提交归 WP-16-T12 装配批；
 *      P-TRJ-8 双向登记缺口维持待 ui 侧冻结，本头不虚构注册面）。
 *   2. handle 只做词表查表与零修订语义应答，结构性零计算（实现 TU 零
 *      rw/io/评估器消费——契约测试词表扫描钉住）；真实计算入口＝评估器
 *      （§15.10）/动画查表与曲线投影（Playback.hpp）/导出（Export.hpp），
 *      均由装配层在受理后调用，命令处理器不触碰。
 *   3. **零修订会话契约的 trajectory 侧承载**：§16.2 红线要求四常量机器
 *      可断言。R-1/R-2 红线：trajectory 依赖白名单（卡 §4.2 CMake 行七条
 *      登记边）不含 ui——本单元禁止 include ui 公共头，故以自有常量结构
 *      TrajectorySessionContract 承载**同值语义**（字段一一对应、值冻结相
 *      同；dynamics ReplaySessionContract 先例同款）；对账锚＝两侧各自的
 *      契约测试以同一文档出处（ui.md/View3DContract.hpp 的 KIN-06/AT-04
 *      钉住值）字面冻结四值，T12 装配层在同一编译单元消费两侧时值漂移即
 *      测试失败。
 *
 * 背景说明（受理/拒绝为什么用 CommandOutcome 而不是异常——dynamics 先例
 *   同款分界）：调用方契约违约的 fail-fast 轨面向程序缺陷；命令适配器的
 *   消费方是 UI 会话（用户交互态），token 未收录/负载错配属**用户可见的
 *   命令不受理**——以 accepted=false＋rejectionToken 显式应答，UI 侧可
 *   正常呈现不可用态。本类无异常轨（纯查表无前置可违约）。
 *
 * 头文件依赖纪律（双模式皆可编译——dynamics Commands.hpp 同款）：本头仅
 *   消费 std 头，零 rw/io/域内类型依赖；定义与实现在 src/Commands.cpp
 *   （纯字符串查表——双模式无条件编译，dynamics Commands.cpp 先例）。
 *
 * 线程安全：无状态、handle 纯函数；多实例并行安全。
 * 确定性：同（token, payload）→同 outcome（词表查表、无环境依赖——
 *   NFR-COR-02）。
 */

#ifndef IRD_TRAJECTORY_COMMANDS_HPP
#define IRD_TRAJECTORY_COMMANDS_HPP

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace sdurws::ird::trajectory {

// =====================================================================
// 命令 token 词表（§14.5.1 表七行——唯一书写点；契约测试静态钉扎面：
// token 字面值冻结＝跨版本命令契约，改动即 ui CommandRegistry 注册面
// 断链，必须走单元卡增量修订）。
// =====================================================================

/// §14.5.1 表行 1：发起轨迹规划任务（组装快照→submit；修订＝无——评估
/// 行为；提交即返，计算在共享 worker——§14.1.2）。
inline constexpr std::string_view kCmdPlanSequence = "trajectory.plan-sequence";
/// §14.5.1 表行 2：预览单段（Preview 模式——无正式证据、不产生 envelope，
/// evidence §7 构造边界拒绝；修订＝无）。
inline constexpr std::string_view kCmdPreviewSegment = "trajectory.preview-segment";
/// §14.5.1 表行 3：路点局部重规划（对选中路段以冻结输入重新求值——新
/// RunId、新归档结果；不改既有结果、不产生修订；域内编排面＝Playback.hpp
/// localReplan，worker 线程）。
inline constexpr std::string_view kCmdLocalReplan = "trajectory.local-replan";
/// §14.5.1 表行 4：动画播放（按时刻驱动会话姿态——纯插值查表零规划/
/// 碰撞/时间化计算，§14.5.1"UI 线程"列；四常量契约见
/// kTrajectorySessionContract；数据面＝Playback.hpp TrajectoryPlaybackTable）。
inline constexpr std::string_view kCmdPlay = "trajectory.play";
/// §14.5.1 表行 4：动画进度跳转（playback-seek——与 play 同语义族，仅
/// 进度定位；四常量契约同上）。
inline constexpr std::string_view kCmdPlaybackSeek = "trajectory.playback-seek";
/// §14.5.1 表行 5：标准轨迹导出（§15.13——ExportTarget＋原子写；失败保留
/// 先前输出；导出副本**不产生修订**（AT-04）；域内执行面＝Export.hpp，
/// worker 或 IO 线程——数据已在归档结果中无需重算）。
inline constexpr std::string_view kCmdExport = "trajectory.export";
/// §14.5.1 表行 6：曲线视图（q/qd/qdd/时刻曲线只读投影——读归档 payload；
/// 数据面＝Playback.hpp projectTrajectoryCurves）。
inline constexpr std::string_view kCmdShowCurve = "trajectory.show-curve";

/// 命令 token 全集（§14.5.1 表行序——稳定序；词表封闭：表外 token 一律
/// 不受理）。
inline constexpr std::array<std::string_view, 7> kCommandTokens{
    kCmdPlanSequence, kCmdPreviewSegment, kCmdLocalReplan, kCmdPlay,
    kCmdPlaybackSeek, kCmdExport, kCmdShowCurve};

// =====================================================================
// 零修订会话契约（§16.2 红线"View3DSessionPoseContract 四常量同款……
// 适用于轨迹播放会话态"的 trajectory 侧承载——与 ui 单元
// View3DSessionPoseContract 同值对账；文件头登记 3；KIN-06/AT-04 钉住
// 语义）。
// =====================================================================

/**
 * @brief trajectory 会话命令的零修订语义契约（AT-04 同口径——四布尔位
 *        钉住"会话/研究态零修订"，机器可断言）。
 *
 * 字段与 ui 单元 View3DSessionPoseContract（View3DContract.hpp，KIN-06/
 * AT-04）一一对应、值冻结相同：
 *   - sessionStateOnly＝true　命令效果仅是会话级状态（播放姿态/视图投影/
 *     提交回执——切换项目/关闭会话即消亡，不入项目持久层；export 的产出
 *     是项目外的**导出副本文件**，同样不触达项目修订历史——"不入持久层"
 *     的载体说明，四常量位的零修订语义对七命令共同成立）；
 *   - writesDesignModel＝false　不修改设计模型（模型快照零触——ARC-04）；
 *   - producesRevision＝false　不产生修订（不可变历史 PA-2——§14.5.1 修
 *     订列全部"无"；AT-04 的机器断言面；局部重规划的新归档结果是**新增
 *     运行记录**而非修订——§14.5.2"两份结果都是历史运行，不改历史"）；
 *   - invalidatesResults＝false　不触发结果失效（CON-02 当前性正交——
 *     Superseded 判定只由输入变更驱动，会话命令不参与）。
 *
 * 本结构为 constexpr 值（kTrajectorySessionContract）——任何一位被实现
 * 改动即契约测试失败（语义变更必须走需求/单元卡修订，代码不得私改）。
 */
struct TrajectorySessionContract {
    /// 命令效果仅是会话级状态（恒 true——KIN-06"只改变会话姿态"语义族）。
    bool sessionStateOnly = true;
    /// 是否修改设计模型（恒 false——KIN-06"不修改设计模型"）。
    bool writesDesignModel = false;
    /// 是否产生修订（恒 false——AT-04"预览不产生项目修订"/§14.5.1 修订列）。
    bool producesRevision = false;
    /// 是否触发结果失效（恒 false——KIN-06"不触发结果失效"/CON-02 正交）。
    bool invalidatesResults = false;
};

/// 零修订会话契约钉住值（§14.5.1 七命令共同语义——全部会话命令，修订＝
/// 无；constexpr＝编译期常量，契约测试静态断言逐位值）。
inline constexpr TrajectorySessionContract kTrajectorySessionContract{};

// =====================================================================
// 命令负载与受理结果（§15.11 handle 的 [in]/[out] 形态——域内 DTO）。
// =====================================================================

/**
 * @brief 命令负载（§14.5.1 各命令的会话上下文定位参数——域内最小承载）。
 *
 * ★ 诚实边界：归档运行引用/快照组装上下文等富上下文随 WP-16-T12 装配层
 *   经 ui 会话通道传递（P-TRJ-8 缺口维持登记）；本结构只承载受理语义所需
 *   的**定位参数**。字段值本类不解读、不校验数值范围（时刻/段序号的合法
 *   性归消费面——Playback.hpp 查表越界 fail-fast、局部重规划请求校验
 *   fail-fast）；定位参数仅作 UI→装配层的意图透传。
 *
 * commandToken 交叉校验位：允许为空（不校验）；非空时必须与 handle 的
 *   commandToken 参数一致（不一致＝负载与命令错配——受理拒绝，
 *   rejectionToken="payload-token-mismatch"）。
 * 值语义纯结构；线程安全。
 */
struct TrajectoryCommandPayload {
    /// 交叉校验位（可空；语义见类型注释——默认空串＝不校验）。
    std::string commandToken;
    /// play/playback-seek 的目标时刻，单位 s（默认 NaN＝未提供——显式
    /// 无效位模式，不伪造 0 时刻；其余命令忽略本字段）。
    double playbackTimeS = std::numeric_limits<double>::quiet_NaN();
    /// show-curve 的游标关节序号（0 基链序；其余命令忽略）。
    std::uint32_t jointIndex = 0;
    /// local-replan 选中路段首段序号（0 基，闭区间端；其余命令忽略）。
    std::uint32_t selectedFirstSegmentIndex = 0;
    /// local-replan 选中路段末段序号（0 基，闭区间端；其余命令忽略）。
    std::uint32_t selectedLastSegmentIndex = 0;
};

/**
 * @brief 命令受理结果（§14.5.1"受理"语义的域内落地——"提交结果/投影查询
 *        句柄"由装配层在受理后建立，本结构只承载受理判定与零修订契约）。
 *
 * 受理（accepted=true）时四语义位逐位等于 kTrajectorySessionContract
 * （零修订契约——AT-04 的机器断言面：七命令任一受理结果的
 * producesRevision 均为 false）；拒绝（accepted=false）时四语义位全
 * false（未受理的命令不作任何会话效果承诺）＋rejectionToken 非空。
 * 值语义纯结构；线程安全。
 */
struct TrajectoryCommandOutcome {
    /// 命令是否受理（token 在词表且负载一致——拒绝语义见类型注释）。
    bool accepted = false;
    /// 效果仅是会话级状态（受理时恒 true——kTrajectorySessionContract）。
    bool sessionStateOnly = false;
    /// 是否修改设计模型（恒 false——零修订契约；受理时保持 false）。
    bool writesDesignModel = false;
    /// 是否产生修订（恒 false——AT-04/§16.2 机器断言面）。
    bool producesRevision = false;
    /// 是否触发结果失效（恒 false——零修订契约；受理时保持 false）。
    bool invalidatesResults = false;
    /// 拒绝 token（accepted=false 时非空："unknown-token"＝token 不在
    /// §14.5.1 词表／"payload-token-mismatch"＝负载交叉校验位与命令不符；
    /// 受理时空串）。
    std::string rejectionToken;
};

// =====================================================================
// 领域命令适配器（§15.11——域内具体类；零计算逻辑、零修订）。
// =====================================================================

/**
 * @brief trajectory 领域命令适配器（§15.11——只是适配器，不是全局命令
 *        注册表、不是 UI 命令权威；命令注册权威＝ui CommandRegistry，
 *        注册面归 WP-16-T12 装配批）。
 *
 * 硬边界（§15.11 原文承接——结构性保证）：
 *   - 处理器内**零计算逻辑**：本类不消费任何评估器/规划/查表/导出设施
 *     （实现 TU 零对应 include——契约测试词表扫描钉住）；真实计算入口
 *     （评估器提交/Playback 查表/Export 写出）由 T12 装配层在受理后执行；
 *   - **零修订（AT-04）**：handle 纯函数、零副作用，受理结果的
 *     producesRevision 恒 false（kTrajectorySessionContract）；
 *   - 不注册全局快捷键（注册面归 ui——§15.11）。
 */
class TrajectoryCommandHandler {
public:
    TrajectoryCommandHandler() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    TrajectoryCommandHandler(const TrajectoryCommandHandler&) = default;
    TrajectoryCommandHandler& operator=(const TrajectoryCommandHandler&) = default;

    /**
     * @brief 受理一条 trajectory 领域命令（§15.11；零计算、零修订）。
     *
     * 受理规则（逐步）：
     *   1. commandToken 在 §14.5.1 词表（kCommandTokens 全集——精确匹配，
     *      大小写敏感：命令 token 是机器契约非自由文本）→继续；否则
     *      拒绝（accepted=false，rejectionToken="unknown-token"）；
     *   2. payload.commandToken 非空且与 commandToken 不一致→拒绝
     *      （accepted=false，rejectionToken="payload-token-mismatch"
     *      ——负载与命令错配，防 UI 侧组装错位）；
     *   3. 受理：accepted=true，四语义位逐位取 kTrajectorySessionContract
     *      值（sessionStateOnly=true／writesDesignModel=false／
     *      producesRevision=false／invalidatesResults=false——零修订
     *      机器断言面），rejectionToken 清空。
     *
     * @param commandToken [in] 命令 token（§14.5.1 词表——表外拒绝）
     * @param payload      [in] 会话上下文定位参数（语义见
     *                     TrajectoryCommandPayload；本方法不解读数值字段
     *                     ——零计算）
     *
     * @return 受理结果（受理＝四语义位＝零修订契约；拒绝＝全 false＋
     *         rejectionToken）
     *
     * 复杂度：O(|词表|)（7 token 精确串比较——常数级）。
     */
    TrajectoryCommandOutcome handle(std::string_view commandToken,
                                    const TrajectoryCommandPayload& payload) const;
};

}  // namespace sdurws::ird::trajectory

#endif  // IRD_TRAJECTORY_COMMANDS_HPP
