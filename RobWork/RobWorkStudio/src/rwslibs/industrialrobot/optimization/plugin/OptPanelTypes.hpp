/**
 * @file   OptPanelTypes.hpp
 * @brief  optimization 插件界面的零 Qt 值类型半区——服务缝聚合、会话态
 *         与进度样本（units/optimization.md §9.1 运行控制 UI 协作的模型
 *         层承载；WP-20-T10 落位）。
 *
 * 设计依据：
 *   - units/optimization.md §9.1（运行控制 R1 形态：取消＋进度为 B 期
 *     承诺〔OPT-06/AT-34〕；进度样本 {percent, phaseToken, batchesDone/
 *     Total} ≤10 Hz 节流、phaseToken 八词表、"UI 进度漏斗消费"行；
 *     "UI 只显示进度和取消状态；不在 UI 线程执行候选评估"红线）；
 *     §6.5（阶段锁诊断呈现边界——阻塞横幅＋缺项清单，绝不呈现为候选
 *     淘汰）；§6.4（Preflight 报告——阻塞横幅与启动门控的事实来源）
 *   - units/optimization.md §10.7 红线精神（插件不直接写项目、不经手
 *     候选物化——运行启动/取消/结果读取全部经缝，宿主编排执行）
 *   - 先例：dynamics/plugin/DynPanelTypes.hpp（服务缝聚合＋会话态的
 *     零 Qt 模型半区形态——WP-17-T09 落位；optimization 按其"缝由装配
 *     层注入、面板零环境依赖"的同款纪律收缩）
 *   - 任务契约 tasks/foundation/WP-20-T10.json（acceptance 1/2/3）
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.3 同步登记）：
 *   1. **缝纪律（dynamics 先例同款）**：变量绑定投影、约束清单、进度
 *      样本、运行结果四类数据与运行启动/取消两个动作、文案解析一共
 *      七缝，全部为 std::function，由装配层（宿主或开发 harness）注入；
 *      缝为空＝该面未装配——呈现侧如实降级（"数据未装配"），绝不虚构
 *      数值（NFR-COR-03）。数据缝返回类型为计算库只读值（Run.hpp 运行
 *      结果聚合／Preflight.hpp 检查报告——构建后按不可变纪律消费）或
 *      本头私有的进度样本值，本头经包含消费之。
 *   2. **零计算红线（卡 §3.2）**：本头与整个插件面不调用任何评估/生成/
 *      排序/指标合成/编码符号；面板侧只做数据重组、查表与呈现——
 *      契约测试 OptPluginAssembly 全文词表扫描钉住。
 *   3. **进度样本（OptProgressSample）是本头私有的呈现值**：执行侧的
 *      进度回报类型归 execution（§9.1 ProgressReport 语义），本单元零
 *      execution 进度类型承载义务——宿主装配层把执行侧进度翻译为本值
 *      （phaseToken 必取 §9.1 八词表——模型层漏斗函数对词表外 token
 *      fail-fast）。节流（≤10 Hz）是缝提供方（宿主）的义务，本面按
 *      收到即呈现，不自建定时器。
 *
 * 线程约束：全部值类型无同步原语——仅 UI 线程访问（卡 §9.1——面板/
 * 会话态生命周期随装配层；数据缝的线程安全语义由缝的提供方声明，
 * 本侧约定全部缝在 UI 线程调用）。
 */

#ifndef IRD_OPTIMIZATION_PLUGIN_OPTPANELTYPES_HPP
#define IRD_OPTIMIZATION_PLUGIN_OPTPANELTYPES_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>   // core::EngineeringStatus（最近
                                            //   正式判定词表——投影行同构
                                            //   字段；core 词表直用，装配层
                                            //   翻译零语义）
#include <sdurws/ird/optimization/Preflight.hpp> // PreflightReport（最近检查
                                            //   报告——阻塞横幅与启动门控
                                            //   的事实来源，§6.4；T08 落位
                                            //   公共契约）
#include <sdurws/ird/optimization/Run.hpp>  // OptimizationRunResult（运行
                                            //   结果缝返回类型——T07 聚合
                                            //   载体；构造后按不可变历史
                                            //   纪律消费，I-OPT-6）
#include <sdurws/ird/optimization/Constraint.hpp> // ConstraintSpec（约束清单
                                            //   缝返回类型——§6.2 执行清单
                                            //   条目，T04 落位公共契约）
#include <sdurws/ird/optimization/Types.hpp> // OptimizationStage/RunPhase
                                            //   （阶段与运行状态词表——
                                            //   §4.3/§4.4）
#include <sdurws/ird/optimization/Variable.hpp> // VariableBinding（变量绑定
                                            //   投影缝返回类型——§5.2 研究
                                            //   定义投影值，T03 落位公共契约）

namespace sdurws::ird::optimization {

// =====================================================================
// 进度样本（运行控制页漏斗的数据源——宿主从执行侧进度回报翻译而来）。
// =====================================================================

/**
 * @brief 进度样本（§9.1 进度行的呈现面承载——漏斗合成函数的输入值）。
 *
 * 语义边界（诚实登记）：本结构是缝的组装侧（宿主装配层）从执行侧进度
 * 回报翻译而来的呈现值；percent/phaseToken/batchesDone/batchesTotal 逐
 * 字段直拷透传，本插件面不产生、不平滑、不外推任何进度（≤10 Hz 节流
 * 与 phaseToken 词表合法性由缝提供方保证——词表外 token 在漏斗合成处
 * fail-fast，防呈现面静默容忍宿主漂移）。
 * 值语义纯结构；线程安全。
 */
struct OptProgressSample {
    /// 完成百分比，单位 %（0..100 整数——执行侧节流回报值直拷）。
    int percent = 0;
    /// 当前阶段 token（§9.1 八词表——"preflight"/"generate"/"compile"/
    /// "hard-constraints"/"evaluate-quick"/"evaluate-verified"/"pareto"/
    /// "export"；词表唯一书写点＝OptPanelModel.hpp 常量表，注释零复制）。
    std::string phaseToken;
    /// 已完成候选批数（批边界粒度——§9.1 批计数字段；无量纲整数）。
    std::uint32_t batchesDone = 0;
    /// 总候选批数（分母——0 合法＝编排尚未定总数，呈现"批 —/—"）。
    std::uint32_t batchesTotal = 0;
};

// =====================================================================
// 插件会话态（面板呈现事实的唯一载体——装配层注入与刷新）。
// =====================================================================

/**
 * @brief optimization 插件会话态（dynamics 先例 OptModuleSessionState
 *        同款收缩形态——只承载呈现所需事实，零权威语义）。
 *
 * 权威边界（PA-1 纪律）：本结构的全部字段都是**装配层注入的呈现事实
 * 投影**——运行状态真值归运行编排（RunPhase 词表消费）、检查结论归
 * Preflight 服务（T08）、当前性归 evidence、任务事实归 execution；
 * 本插件不判定、不缓存权威结论（防第二真值）。插件唯一"写"的字段是
 * cancelRequested（本面板取消按钮的呈现位——协作取消已请求的本地
 * 标记；权威取消完成以 runPhase 推进到终态取消值为准）。
 *
 * 阶段锁呈现语义（卡 §6.5——acceptance 2 的呈现边界）：阻塞横幅素材
 * 唯一来自 latestPreflight 的阻塞发现（§6.4 五元组逐项定位）＋阶段
 * 锁码的目录呈现；**绝不**进入候选淘汰列（候选行的淘汰原因词表不含
 * 阶段锁码——模型层映射纪律，测试钉住）。
 *
 * 线程约束：仅 UI 线程访问（装配层注入与面板读取同线程——卡 §9.1）。
 */
struct OptModuleSessionState {
    /// 会话纪元（§6.2——装配层刷新时递增；面板据此丢弃迟到刷新）。
    std::uint64_t epoch = 0;
    /// 会话是否可写（只读项目——运行启动按钮降级；PM-07 事实由装配层
    /// 注入，插件本地零二次判定）。
    bool writable = true;
    /// 研究阶段（§4.3 词表——变量表阶段启用列与约束页横幅的判型依据）。
    OptimizationStage stage = OptimizationStage::StageB;
    /// 就绪校验结论透传（true＝输入完整；结论权威在 Preflight/上游门控
    /// ——UX-10"未完成附缺项列表"的素材面）。
    bool inputComplete = false;
    /// 缺项文案键清单（UX-02/UX-10——键经宿主文案解析呈现；本插件
    /// 零文案值、零哈希/内部标识进用户文本）。
    std::vector<std::string> missingItemKeys;
    /// 是否存在在途运行任务（true＝"计算中"呈现素材——事实归执行侧，
    /// 装配层注入）。
    bool hasActiveTask = false;
    /// 运行编排状态（§4.4 词表消费——终态集 Completed/Canceled 为 R1
    /// 产出；呈现面直译，零判定）。
    RunPhase runPhase = RunPhase::Draft;
    /// 协作取消已请求的呈现位（本面板取消按钮写入；权威完成以 runPhase
    /// 推进为准——本位只防重复提交，非取消成败语义，UX-03 零错误诊断）。
    bool cancelRequested = false;
    /// 最近正式判定（core 词表直用——默认 NotApplicable＝无判定，不伪造
    /// 可行性；UX-10 七态素材之一）。
    core::EngineeringStatus verdict = core::EngineeringStatus::NotApplicable;
    /// 最近 Preflight 报告（§6.4——T08 服务产出值直拷；缺省＝本次会话
    /// 尚未执行检查，启动按钮不本地设卡〔宿主编排面会在启动缝内先检查
    /// 并给反馈〕，阻塞横幅区呈现"尚未检查"提示而非伪造阻塞项）。
    std::optional<PreflightReport> latestPreflight;
};

// =====================================================================
// 服务缝聚合（面板数据源与动作出口——装配层注入；空缝＝未装配，
// 呈现如实降级）。
// =====================================================================

/**
 * @brief optimization 插件服务缝聚合（dynamics 先例同款形态——全部
 *        std::function 缝，装配层在面板创建前注入）。
 *
 * 缝清单与空缝语义（逐缝注明降级呈现——绝不虚构数值，NFR-COR-03）：
 *   - variableSource：研究定义变量绑定投影读取（§5.2 绑定值直拷——
 *     行集重组在模型层）。空缝→变量表呈现"变量表未装配"空态。
 *   - constraintPlanSource：当前研究约束执行清单读取（§6.2 清单值；
 *     宿主经约束编排面按阶段解析——StageD 的阶段锁拒绝由宿主翻译为
 *     会话横幅素材，本缝对 StageD 恒 nullopt〔清单未登记〕）。空缝→
 *     约束页呈现"约束清单未装配"空态。
 *   - progressSource：当前运行进度样本读取（OptProgressSample——宿主
 *     从执行侧翻译）。空缝或 nullopt→漏斗区呈现"无在途任务"。
 *   - runResultSource：当前运行结果聚合读取（OptimizationRunResult——
 *     T07 载体；宿主自运行编排面取值）。空缝或 nullopt→候选表呈现
 *     "尚无运行结果"空态。
 *   - runStart：运行启动缝（"检查并计算"按钮——宿主编排面先预检后
 *     提交，§12.4 合法调用序；返回受理位，false＝宿主拒绝）。空缝→
 *     点击反馈"启动出口未装配"（不静默丢弃——诚实反馈）。
 *   - runCancel：取消请求缝（"取消计算"按钮——宿主转发执行侧协作
 *     取消请求；返回受理位）。空缝→点击反馈"取消出口未装配"。
 *   - textResolver：文案解析（titleKey→工程用语——宿主文案资源唯一
 *     出口 UX-02；空缝→按钮/标题呈现键名原文——开发 harness 的可见
 *     缺口，不是产品装配形态）。
 *
 * 值语义：可拷贝（缝闭包共享装配层捕获物——shared 语义由闭包自然
 * 承载）；仅 UI 线程调用。
 */
struct OptPanelServices {
    /// 研究定义变量绑定投影读取缝（空＝未装配——空态呈现）。
    std::function<std::vector<VariableBinding>()> variableSource;
    /// 约束执行清单读取缝（空＝未装配；nullopt＝阶段清单未登记〔如
    /// StageD 阶段锁〕——约束页以横幅呈现，非候选淘汰）。
    std::function<std::optional<std::vector<ConstraintSpec>>()> constraintPlanSource;
    /// 当前运行进度样本读取缝（nullopt＝无在途任务——合法空态）。
    std::function<std::optional<OptProgressSample>()> progressSource;
    /// 当前运行结果聚合读取缝（nullopt＝尚无结果——合法空态）。
    std::function<std::optional<OptimizationRunResult>()> runResultSource;
    /// 运行启动缝（空＝未装配——点击反馈"出口未装配"）。
    std::function<bool()> runStart;
    /// 取消请求缝（空＝未装配——点击反馈"出口未装配"）。
    std::function<bool()> runCancel;
    /// 文案解析缝（空＝键名原文呈现——开发态可见缺口）。
    std::function<std::string(const std::string& titleKey)> textResolver;
};

}  // namespace sdurws::ird::optimization

#endif  // IRD_OPTIMIZATION_PLUGIN_OPTPANELTYPES_HPP
