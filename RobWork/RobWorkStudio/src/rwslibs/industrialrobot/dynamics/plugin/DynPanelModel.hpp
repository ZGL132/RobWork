/**
 * @file   DynPanelModel.hpp
 * @brief  dynamics 插件面板的模型层流（零 Qt 可测半区）——就绪投影合成、
 *         曲线通道行集重组、会话命令受理流、峰值定位流与回放查表流
 *         （units/dynamics.md §9.5 面板侧模型面；WP-17-T09 界面链路用例
 *         的主被测面——"界面链路按单元卡 §9 模型面＋契约用例承载"）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作表五命令；"UI 线程不得执行动力学
 *     计算"红线；投影行 DomainReadinessItem 同构供七态呈现——本域零 ui
 *     编译边，以 DynReadinessRow 自持同构值承载〔字段一一对应 ui.md
 *     §6.5 DomainReadinessItem 冻结形状〕，翻译归宿主装配层）；
 *   - units/dynamics.md §10.7（命令适配器零计算零修订——本模型只做
 *     词表受理＋会话记录）、§4.5（量纲类型化——转动 N·m／移动 N 的
 *     呈现标签面）、§4.6（Partial 不阻断呈现，nonOkCount 如实标注——
 *     UX-10"数据不足"素材）；
 *   - 先例：kinematics/plugin/KinPanelModel.hpp＋KinPanelFlows.hpp
 *     （模型层行集投影＋具名数据流——WP-15-T12 的 L-Kx 具名纪律；
 *     dynamics 对应为 L-Dx 流，测试用例以流编号具名）；
 *   - 需求 DYN-08（曲线联动/峰值定位/时刻回放）、UX-02（工程用语、零
 *     哈希/内部标识进用户文本）、UX-10（七态统一呈现素材）、AT-04
 *     （预览类交互零修订——机器可断言）；
 *   - 任务契约 tasks/foundation/WP-17-T09.json acceptance 1/2。
 *
 * 流清单（L-Dx——每条流的具名测试见 test/DynPluginPanelTest.cpp）：
 *   - L-D1 就绪投影合成：会话事实→DynReadinessRow（透传，零判定——
 *     判定权威在域就绪校验，插件不复制）。
 *   - L-D2 曲线通道行集：CurveProjection→逐关节×五通道呈现行（量纲
 *     标签按关节型类型化；nonOkCount/完整性透传——数据重组零统计）。
 *   - L-D3 会话命令受理流：token＋负载→DynamicsCommandHandler 受理→
 *     会话记录追加（零修订逐条断言面；缓冲截断＝呈现语义）。
 *   - L-D4 峰值定位流：经缝查询→DynPeakJump→游标跳转目标时刻。
 *   - L-D5 回放查表流：ReplayData＋时刻→计算库投影器 sampleAt（UI
 *     线程插值查表——域设施消费，非本地重算）。
 *   - L-D6 文案解析流：键→缝解析（空缝兜底键名原文）；解析结果哈希
 *     形态守卫（UX-02 零哈希进用户文本——64 位十六进制串检测）。
 *
 * 线程约束：仅 UI 线程访问（会话态实参非线程安全——DynPanelTypes.hpp）。
 * 错误语义：词表外 token＝用户可见的命令不受理（accepted=false——§10.7
 *   outcome 语义，非异常）；曲线行集对空投影返回空行集（Empty 显式
 *   语义——不伪造行）；其余查询的空态语义见各函数注。
 */

#ifndef IRD_DYNAMICS_PLUGIN_DYNPANELMODEL_HPP
#define IRD_DYNAMICS_PLUGIN_DYNPANELMODEL_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>    // core::EngineeringStatus（投影行
                                             //   verdict 同构字段——core 词表直用）
#include <sdurws/ird/dynamics/Commands.hpp>  // CommandPayload/CommandOutcome
                                             //   （§10.7 受理语义——T08 契约）
#include <sdurws/ird/dynamics/DynTypes.hpp>  // DynJointType（量纲类型化标签）
#include <sdurws/ird/dynamics/Replay.hpp>    // CurveProjection/JointCurves/
                                             //   ReplayData/ReplaySample（数据面）
#include "DynPanelTypes.hpp"                 // DynModuleSessionState/
                                             //   DynPanelServices/DynPeakJump
                                             //   （同目录私有头）

namespace sdurws::ird::dynamics {

// =====================================================================
// 曲线通道词表（五通道——JointCurves 字段序＝卡 §4.5 输出序列序；
// 呈现面唯一书写点，曲线视图与行集共用）。
// =====================================================================

/// 曲线通道标识（值＝通道序：JointCurves 五字段按序对位——稳定契约，
/// 改序即呈现错位，由黄金行集用例钉住）。
enum class DynChannelId : int {
    Position = 0,        ///< 通道 1 位置 q：rad（转动/连续）或 m（移动）
    Velocity = 1,        ///< 通道 2 速度 q̇：rad/s 或 m/s
    Acceleration = 2,    ///< 通道 3 加速度 q̈：rad/s² 或 m/s²
    GeneralizedForce = 3,///< 通道 4 总广义力 τ_total：N·m（转动/连续）
                         ///<   或 N（移动）——按关节型类型化（D-DYN-4）
    MechanicalPower = 4, ///< 通道 5 机械功率 P：W（两型同量纲）
};

/// 通道总数（五通道——JointCurves 字段数；循环上界）。
inline constexpr int kDynChannelCount = 5;

/**
 * @brief 通道呈现键（稳定 token——工作流页通道下拉与曲线视图标题共用；
 *         小写连字符形态与 §3.5 键词形一致）。
 *
 * @param id [in] 通道标识（全值合法——越界为调用方错误，fail-fast）
 * @return 通道键（"position"/"velocity"/"acceleration"/
 *         "generalized-force"/"mechanical-power"）
 */
std::string dynChannelKey(DynChannelId id);

/**
 * @brief 通道量纲标签（§4.5 量纲表按关节型类型化——转动/连续关节的
 *         广义力为 N·m、移动关节为 N；位置/速度/加速度同理分型）。
 *
 * @param id         [in] 通道标识
 * @param jointType  [in] 关节型（Revolute/Continuous 按 N·m 系解释，
 *                   Prismatic 按 N 系解释）
 * @return 量纲标签（"rad"/"rad/s"/"rad/s^2"/"N·m"/"N"/"m"/"m/s"/
 *         "m/s^2"/"W"——纯文本呈现符号，非 UnitToken 强类型；量纲
 *         强类型化归 core §4.4，呈现标签是副本不是第二权威）
 */
std::string dynChannelUnit(DynChannelId id, DynJointType jointType);

/**
 * @brief 通道数值列只读访问（JointCurves 五字段按通道对位——零拷贝
 *         视图；曲线视图绘线与行集点数共用）。
 *
 * @param curves [in] 单关节曲线行（只读）
 * @param id     [in] 通道标识
 * @return 该通道数值列的 const 引用（与时间轴逐下标对位——JointCurves
 *         契约；越界 id 为调用方错误，fail-fast）
 */
const std::vector<double>& dynChannelValues(const JointCurves& curves,
                                            DynChannelId id);

// =====================================================================
// L-D1 就绪投影行（ui.md §6.5 DomainReadinessItem 字段同构自持值）。
// =====================================================================

/**
 * @brief 域就绪投影行（宿主装配层翻译为 ui 汇聚行的素材——字段与
 *        ui::DomainReadinessItem 一一对应，零增删；真实 ui 类型注册
 *        缺口见单元卡 P-DYN-8）。
 *
 * 语义锚（透传纪律）：verdict/inputComplete/missingItemKeys/hasActiveTask
 * 全部取自会话事实（DynModuleSessionState——权威分属域就绪校验/
 * execution/evidence），本模型零判定、零缓存（防第二真值）；domainKey
 * 恒 "dynamics"（域注册键——§6.5 原文词表）。
 * 值语义纯结构；线程安全。
 */
struct DynReadinessRow {
    std::string domainKey;                        ///< 域注册键（恒 "dynamics"）
    core::EngineeringStatus verdict =
        core::EngineeringStatus::NotApplicable;   ///< 最近正式判定（core 词表
                                                  ///<   直用——无判定＝NotApplicable）
    bool inputComplete = false;                   ///< 就绪校验结论（REQ-06——
                                                  ///<   true＝输入完整）
    std::vector<std::string> missingItemKeys;     ///< 缺项文案键清单（UX-10
                                                  ///<   "未完成附缺项列表"素材）
    bool hasActiveTask = false;                   ///< 在途任务事实（"计算中"
                                                  ///<   呈现素材之一）
};

/**
 * @brief L-D1 就绪投影合成（会话事实→投影行——纯透传，零判定）。
 *
 * @param session [in] 插件会话态（事实由装配层注入/刷新）
 * @return 投影行（domainKey 恒 "dynamics"；其余字段逐项透传）
 */
DynReadinessRow readinessProjection(const DynModuleSessionState& session);

// =====================================================================
// L-D2 曲线通道行集（曲线联动呈现的结构面——数据重组零统计）。
// =====================================================================

/**
 * @brief 曲线通道呈现行（工作流页通道清单/曲线视图标题的行素材）。
 *
 * pointCount/nonOkCount 直接取 JointCurves 的数组长度与剔除计数
 * （直拷透传——本模型不校验、不重算；UX-10"数据不足"的标注素材＝
 * nonOkCount＞0 或投影 completeness 非 Complete）。
 * 值语义纯结构；线程安全。
 */
struct DynChannelRow {
    std::uint32_t jointIndex = 0;  ///< 关节序号（0 基链序）
    DynJointType jointType{};      ///< 关节型（量纲标签的判型依据）
    int channelId = 0;             ///< 通道序（DynChannelId 的 int 值 0..4）
    std::string channelKey;        ///< 通道键（dynChannelKey——稳定 token）
    std::string unitToken;         ///< 量纲标签（dynChannelUnit——按型分派）
    std::size_t pointCount = 0;    ///< 数据点数（该通道 Ok 行数）
    std::size_t nonOkCount = 0;    ///< 该关节被剔除的非 Ok 行数（透传）
};

/**
 * @brief L-D2 曲线通道行集重组（曲线投影→逐关节×五通道行）。
 *
 * 组装规则：行序＝（jointIndex 升序，channelId 升序）——投影 joints
 * 已按 jointIndex 升序（T08 投影器契约），通道按词表序展开即稳定序
 * （NFR-COR-02）；每行的 pointCount＝对应通道数组长度、nonOkCount＝
 * 关节行透传值；量纲标签按关节型逐行分派。空投影（无 Ok 行）→空
 * 行集（Empty 显式语义——不伪造行，呈现侧以"无数据"空态呈现）。
 *
 * @param projection [in] 曲线投影（T08 数据面产物——只读）
 * @return 通道行集（行数＝关节数×5；空投影→空）
 */
std::vector<DynChannelRow> curveChannelRows(const CurveProjection& projection);

// =====================================================================
// L-D3 会话命令受理流（§9.5 五命令——受理＋会话记录，零修订）。
// =====================================================================

/// 命令受理记录缓冲上限（呈现缓冲截断——非业务阈值；见
/// DynModuleSessionState::recentCommands 注）。
inline constexpr std::size_t kDynCommandRecordCapacity = 8;

/**
 * @brief L-D3 会话命令受理（token＋负载→领域适配器受理→会话记录）。
 *
 * 执行序：
 *   1. 经 DynamicsCommandHandler::handle 受理（词表查表＋负载交叉校验
 *      ——§10.7 语义；插件本地零命令判定，全部权威在适配器）；
 *   2. 受理结果追加入会话记录缓冲（DynCommandRecord——token/受理/
 *      拒绝 token/producesRevision 四字段直拷；超容量从头丢最旧）；
 *   3. 返回受理结果（调用方呈现——accepted=false 为用户可见不受理，
 *      非异常）。
 *
 * 零修订契约（AT-04 机器断言面）：本函数对项目/模型/结果零触——
 * 唯一写点是会话呈现缓冲；受理结果的 producesRevision 恒 false
 * （kReplaySessionContract），测试逐条断言。
 *
 * @param session      [in,out] 插件会话态（记录缓冲被追加——其余字段
 *                     不触）
 * @param commandToken [in] 命令 token（§9.5 词表——表外拒绝）
 * @param payload      [in] 命令负载（定位参数——语义见 Commands.hpp）
 * @return 受理结果（透传 handle 产物）
 */
CommandOutcome applySessionCommand(DynModuleSessionState& session,
                                   std::string_view commandToken,
                                   const CommandPayload& payload);

// =====================================================================
// L-D4 峰值定位流（经缝查询→游标跳转目标）。
// =====================================================================

/**
 * @brief L-D4 峰值定位查询（服务缝→跳转值；本函数只做缝的空态归一）。
 *
 * 空态语义（三态显式区分——呈现侧各自呈现，不混同）：
 *   - 缝未装配（services.peakLocate 为空）→nullopt（调用方以
 *     "未装配"空态呈现——与"无峰值"不同，本函数无法区分时由调用方
 *     先以缝判空分流；本函数对空缝直接 nullopt 并由参数回传空因）；
 *   - 缝返回 nullopt（该关节/通道无峰值——合法空态）；
 *   - 缝返回跳转值（tPeakS＝游标跳转目标时刻，s）。
 *
 * @param services   [in] 服务缝聚合
 * @param jointIndex [in] 目标关节序号（0 基链序）
 * @param tokenIndex [in] 峰值通道 token 序号（0..5——缝的组装侧契约）
 * @param outReason  [out] 可空回传：nullopt 时的空因（"not-assembled"＝
 *                     缝未装配；"no-peak"＝查询合法落空；受理时空串）
 * @return 跳转值（见 DynPeakJump）；空态→nullopt（空因见 outReason）
 */
std::optional<DynPeakJump> locatePeakJump(const DynPanelServices& services,
                                          std::uint32_t jointIndex,
                                          int tokenIndex,
                                          std::string* outReason = nullptr);

// =====================================================================
// L-D5 回放查表流（域设施消费——UI 线程插值查表）。
// =====================================================================

/**
 * @brief L-D5 按时刻查询单时刻关节状态（replay-at 数据面查询的模型层
 *        落点——直接消费计算库投影器 sampleAt，插件零本地重算）。
 *
 * 查询语义全部继承计算库契约（Replay.hpp）：精确命中＝帧原值直拷；
 * 区间＝相邻两帧线性混合（呈现投影语义——插值点数值不得用于统计/
 * 评估）；越界/空数据集＝nullopt（不外推——卡 §4.3 纪律）；t 非有限
 * ＝投影器 fail-fast（调用方错误透传，不静默）。
 *
 * @param data [in] 回放数据集（缝产物——只读）
 * @param t    [in] 查询时刻，单位 s（必须有限）
 * @return 单时刻状态；空态→nullopt
 *
 * @throws DynamicsError t 非有限（计算库投影器调用方错误轨——透传）
 */
std::optional<ReplaySample> replaySampleAt(const ReplayData& data, double t);

// =====================================================================
// L-D6 文案解析流（UX-02——键→工程用语，零哈希泄漏守卫）。
// =====================================================================

/**
 * @brief L-D6 文案解析（titleKey→用户文本；空缝兜底键名原文）。
 *
 * 解析序：缝存在→经缝解析（宿主文案资源唯一出口）；缝空→返回键名
 * 原文（kinematics 同纪律——开发 harness 的可见缺口，产品装配必接
 * 宿主解析器）。哈希形态守卫（UX-02"零哈希进用户文本"）：缝解析
 * 结果若呈 64 位十六进制串形态（内容摘要泄漏——ARC-04 身份不进
 * 呈现），按泄漏处置返回键名原文并回传标记——呈现宁可键名也不可
 * 哈希。
 *
 * @param services     [in] 服务缝聚合（textResolver 缝）
 * @param titleKey     [in] 文案键（§3.5 键族——值归宿主文案资源）
 * @param outFellBack  [out] 可空回传：true＝兜底（缝空或解析结果哈希
 *                     形态）；false＝缝解析原值
 * @return 用户文本（工程用语——或键名兜底）
 */
std::string resolvePanelText(const DynPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack = nullptr);

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_PLUGIN_DYNPANELMODEL_HPP
