/**
 * @file   DynPanelTypes.hpp
 * @brief  dynamics 插件界面的零 Qt 值类型半区——服务缝聚合、会话态与
 *         呈现私有 DTO（units/dynamics.md §9.5 UI 协作面板侧的模型层
 *         承载；WP-17-T09 落位）。
 *
 * 设计依据：
 *   - units/dynamics.md §9.5（UI 协作表五命令 token；"UI 线程不得执行
 *     动力学计算——评估/仿真/统计全在 worker 计算库"红线；投影行
 *     "回放数据逐时刻关节状态随 payload"；DomainReadinessItem 同构投影
 *     供七态呈现——本单元零 ui 编译边，投影行以同构值承载、翻译归宿主
 *     装配层，见文件头"落地面口径"）；
 *   - units/dynamics.md §10.7（领域命令适配器硬边界——零计算、零修订；
 *     命令注册权威＝ui CommandRegistry，插件侧只有提交出口缝）、
 *     §2.4 O13（IDynamicsCommandHandler 只是适配器不是注册表）；
 *   - 先例：kinematics/plugin/KinPanelTypes.hpp（服务缝聚合＋会话态的
 *     零 Qt 模型半区形态——WP-15-T12 落位；dynamics 按其"缝由装配层
 *     注入、面板零环境依赖"的同款纪律收缩为五缝）；
 *   - 任务契约 tasks/foundation/WP-17-T09.json（acceptance 1/2）。
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. **零 ui 编译边**：本单元依赖白名单（卡 §3.2 五条登记边）不含
 *      ui，ird_gates 机器面（IRD_ALLOWED_UNIT_EDGES）亦无 dynamics->ui
 *      登记边——ui 单元公共类型（IPluginUiModule/PluginUiDescriptor/
 *      DomainReadinessItem 等）本插件面**零消费**；就绪投影行
 *      DynReadinessRow（DynPanelModel.hpp）与面板/命令登记记录
 *      （DynPanelCommandCatalog.hpp）以**字段同构的自持值**承载，真实
 *      ui 类型注册归宿主装配批次收口（缺口登记见单元卡 P-DYN-8）。
 *   2. **缝纪律（kinematics 先例同款）**：归档数据读取（曲线投影/回放
 *      数据集/峰值定位）与命令提交/可用性/文案解析全部为
 *      std::function 缝，由装配层（宿主或开发 harness）注入；缝为空
 *      ＝该面未装配——呈现侧如实降级（"数据未装配"），绝不虚构数值
 *      （NFR-COR-03）。数据缝的返回类型为计算库数据面值（Replay.hpp
 *      的只读投影产物——T08 落位的公共契约），本头经包含消费之；峰值
 *      定位缝返回本头私有的 DynPeakJump 跳转值（翻译归缝的组装侧——
 *      插件面零统计符号，契约测试词表扫描钉住）。
 *   3. **零计算红线（卡 §9.5）**：本头与整个插件面不消费任何评估器/
 *      统计器/投影组装器符号；面板侧只做数据重组、查表与呈现——
 *      契约测试 DynamicsPluginAssemblyContractTest 全文词表扫描钉住。
 *
 * 线程约束：全部值类型无同步原语——仅 UI 线程访问（卡 §3.4——面板/
 * 会话态生命周期随装配层；数据缝的线程安全语义由缝的提供方声明，
 * 本侧约定全部缝在 UI 线程调用）。
 */

#ifndef IRD_DYNAMICS_PLUGIN_DYNPANELTYPES_HPP
#define IRD_DYNAMICS_PLUGIN_DYNPANELTYPES_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>   // core::EngineeringStatus（最近正式
                                            //   判定词表——ui.md §6.5 投影行
                                            //   verdict 字段同构；core 词表直用，
                                            //   装配层翻译零语义）
#include <sdurws/ird/dynamics/Commands.hpp> // 命令 token 词表/零修订契约/
                                            //   CommandOutcome（§10.7——同单元
                                            //   公共契约，T08 落位）
#include <sdurws/ird/dynamics/Replay.hpp>   // CurveProjection/ReplayData（数据
                                            //   缝返回类型——T08 数据面公共契约；
                                            //   只读投影产物，构建后不变）

namespace sdurws::ird::dynamics {

// =====================================================================
// 峰值跳转值（locate-peak 呈现面私有 DTO——缝的组装侧从计算库峰值行集
// 翻译而来；插件面零统计符号——词表扫描纪律见文件头）。
// =====================================================================

/**
 * @brief 峰值定位跳转值（dynamics.locate-peak 命令的呈现面产物——
 *        "曲线游标跳转峰值时刻＋三维姿态同步"的定位素材）。
 *
 * 语义边界（诚实登记）：本结构是缝的组装侧翻译产物，字段值全部取自
 * 计算库统计产出的归档行（量纲按 token 类型化：力矩 N·m／力 N／速度
 * rad·s⁻¹|m·s⁻¹／加速度 rad·s⁻²|m·s⁻²／功率 W——卡 §4.5 量纲表）；
 * 本插件面不重算任何统计（卡 §9.5 红线），value 仅作读数呈现。
 * 值语义纯结构；线程安全。
 */
struct DynPeakJump {
    std::uint32_t jointIndex = 0;  ///< 目标关节序号（0 基链序）
    int tokenIndex = 0;            ///< 峰值通道 token 序号（0..5——计算库
                                   ///<   token 表行序；语义见缝提供方）
    double value = 0.0;            ///< 峰值数值（量纲按 jointType＋token——
                                   ///<   仅读数呈现，零业务判定）
    double tPeakS = 0.0;           ///< 峰值时刻，单位 s——曲线游标跳转目标
    std::uint32_t segmentIndex = 0; ///< 峰值所在轨迹段序号（0 基——三维
                                    ///<   姿态同步的段定位素材）
};

// =====================================================================
// 会话命令受理记录（工作流页呈现缓冲——零修订契约的逐条观测面）。
// =====================================================================

/**
 * @brief 单条会话命令受理记录（§10.7 handle 结果的会话留痕——工作流页
 *        "最近命令"只读清单的行素材）。
 *
 * producesRevision 逐条取自受理结果（零修订契约——kReplaySessionContract
 * 的 producesRevision 恒 false；AT-04"预览类交互不产生项目修订"的
 * 机器可断言面：模型层测试对每条记录断言本位为 false）。
 * 值语义纯结构；线程安全。
 */
struct DynCommandRecord {
    std::string commandToken;      ///< 命令 token（Commands.hpp 词表值）
    bool accepted = false;         ///< 是否受理（false＝用户可见的不受理
                                   ///<   ——拒绝原因见 rejectionToken）
    std::string rejectionToken;    ///< 拒绝 token（受理时空串——Commands.hpp
                                   ///<   CommandOutcome 语义透传）
    bool producesRevision = false; ///< 是否产生修订（受理时恒 false——零
                                   ///<   修订契约；未受理亦 false）
};

// =====================================================================
// 插件会话态（面板呈现事实的唯一载体——装配层注入与刷新）。
// =====================================================================

/**
 * @brief dynamics 插件会话态（kinematics 先例 KinModuleSessionState 同款
 *        收缩形态——只承载呈现所需事实，零权威语义）。
 *
 * 权威边界（PA-1 纪律）：本结构的全部字段都是**装配层注入的呈现事实
 * 投影**——就绪真值归域就绪校验、任务事实归 execution、当前性归
 * evidence；本插件不判定、不缓存权威结论（防第二真值）。插件唯一
 * "写"的字段是命令受理记录缓冲（会话级呈现史——零修订，不入任何
 * 持久层）。
 *
 * 线程约束：仅 UI 线程访问（装配层注入与面板读取同线程——卡 §3.4）。
 */
struct DynModuleSessionState {
    /// 会话纪元（§6.2——装配层刷新时递增；面板据此丢弃迟到刷新）。
    std::uint64_t epoch = 0;
    /// 会话是否可写（只读模式——写类呈现入口降级；本域五命令全部零修订，
    /// 只读模式仅影响"发起评估"的提交出口可用性，由命令可用性缝统一
    /// 门控——插件本地零二次判定）。
    bool writable = true;
    /// 就绪校验结论透传（true＝输入完整；结论权威在域就绪校验——
    /// UX-10"未完成附缺项列表"的素材面）。
    bool inputComplete = false;
    /// 缺项文案键清单（UX-02/UX-10——键经宿主文案解析呈现；本插件
    /// 零文案值、零哈希/内部标识进用户文本）。
    std::vector<std::string> missingItemKeys;
    /// 是否存在在途任务（true＝"计算中"呈现素材之一——§6.3 触发面；
    /// 事实归 execution，装配层注入）。
    bool hasActiveTask = false;
    /// 最近正式判定（core 词表直用——默认 NotApplicable＝无判定，不伪造
    /// 可行性；UX-10"数据不足"等七态素材之一）。
    core::EngineeringStatus verdict = core::EngineeringStatus::NotApplicable;
    /// 最近命令受理记录（呈现缓冲——新记录追加于尾；超过容量上限时
    /// 从头丢弃最旧记录。容量 8 条是呈现缓冲截断，非业务阈值——
    /// 完整受理史归 execution 任务清单，本缓冲只服务工作流页清单）。
    std::vector<DynCommandRecord> recentCommands;
};

// =====================================================================
// 服务缝聚合（面板数据源——装配层注入；空缝＝未装配，呈现如实降级）。
// =====================================================================

/**
 * @brief dynamics 插件服务缝聚合（kinematics 先例 KinPanelServices 同款
 *        形态——全部 std::function 缝，装配层在面板创建前注入）。
 *
 * 缝清单与空缝语义（逐缝注明降级呈现——绝不虚构数值，NFR-COR-03）：
 *   - curveSource：当前工况曲线投影读取（读归档 payload——§9.5
 *     show-curves 行"读归档 payload 投影"；组装在装配层/worker 侧完成，
 *     本缝只取现成投影值）。空缝→曲线视图呈现"曲线数据未装配"空态。
 *   - replaySource：当前工况回放数据集读取（§9.5 投影行"回放数据
 *     （逐时刻关节状态）随 payload"——DYN-08 数据面）。空缝→回放读数
 *     区呈现"回放数据未装配"。
 *   - peakLocate：峰值定位查询（入参关节序＋token 序；组装侧消费计算
 *     库统计行集后翻译为 DynPeakJump——插件面零统计）。空缝→峰值跳转
 *     呈现"峰值定位未装配"。
 *   - commandSubmit：命令提交出口（§9.5 五命令 token——真实注册权威＝
 *     ui CommandRegistry（宿主装配），本缝是插件侧唯一出口；O13 边界：
 *     插件不私占注册表、不注册全局快捷键）。空缝→按钮点击呈现
 *     "命令出口未装配"（不静默丢弃——诚实反馈）。
 *   - commandAvailability：命令可用性查询（统一按钮门控——只读模式/
 *     未装配态的可用性判定权威在宿主；空缝→全部命令按可用呈现，
 *     点击时经提交缝/空缝语义如实反馈）。
 *   - textResolver：文案解析（titleKey→工程用语——宿主文案资源唯一
 *     出口 UX-02；空缝→按钮/标题呈现键名原文——kinematics 同纪律，
 *     开发 harness 的可见缺口，不是产品装配形态）。
 *
 * 值语义：可拷贝（缝闭包共享装配层捕获物——shared 语义由闭包自然
 * 承载）；仅 UI 线程调用。
 */
struct DynPanelServices {
    /// 当前工况曲线投影读取缝（空＝未装配——空态呈现）。
    std::function<CurveProjection()> curveSource;
    /// 当前工况回放数据集读取缝（空＝未装配——空态呈现）。
    std::function<ReplayData()> replaySource;
    /// 峰值定位查询缝（空＝未装配；nullopt＝该关节/通道无峰——合法
    /// 空态，呈现"无峰值记录"）。
    std::function<std::optional<DynPeakJump>(std::uint32_t jointIndex,
                                             int tokenIndex)> peakLocate;
    /// 命令提交出口缝（空＝未装配——点击反馈"出口未装配"）。
    std::function<void(const std::string& commandToken)> commandSubmit;
    /// 命令可用性查询缝（空＝全部按可用呈现）。
    std::function<bool(const std::string& commandToken)> commandAvailability;
    /// 文案解析缝（空＝键名原文呈现——开发态可见缺口）。
    std::function<std::string(const std::string& titleKey)> textResolver;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_PLUGIN_DYNPANELTYPES_HPP
