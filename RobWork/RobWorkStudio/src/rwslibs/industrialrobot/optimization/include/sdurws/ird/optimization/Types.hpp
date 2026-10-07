/**
 * @file   Types.hpp
 * @brief  optimization 基础词表与域异常——优化阶段词表、随机种子别名与
 *         OptimizationError（本任务 WP-20-T03 落位子集）。
 *
 * 设计依据：
 *   - units/optimization.md §4.3（OptimizationStage 词表——"本域自有——core
 *     无 EvaluationStage 枚举，不与之混用"；stage-b＝OPT-B 静态子集〔R1〕，
 *     stage-d＝OPT-D 联合优化〔R2，P-04 冻结后启用〕）、§12.3（错误语义——
 *     调用方错误 fail-fast〔OptimizationError〕；环境错误走稳定诊断码＋
 *     结构化返回；禁止吞错）、§3.1（布局表——Types.hpp 承载 OptimizationStage/
 *     CandidateStatus/MetricId/淘汰原因词表）
 *   - 需求 OPT-01/02（本任务承载的变量/初始化语义都以"阶段"为作用域——
 *     REQUIREMENTS §15.0 OPT-B/OPT-D 分期表）
 *   - 任务契约 tasks/foundation/WP-20-T03.json（allowedFiles/acceptance；
 *     knownPitfalls P-OPT-2/P-OPT-3/P-OPT-4）
 *
 * ★ 本批落位子集口径（诚实登记，防扩大）：§3.1 布局表把 CandidateStatus/
 *   MetricId/淘汰原因词表也列在 Types.hpp——三者分别是 Pareto（WP-20-T05）/
 *   指标（WP-20-T05）/管线淘汰（WP-20-T04/T06）的消费面，本任务不消费、
 *   不预建（NFR-MNT-04 不建无边界价值占位）；本批只落变量/补丁模型直接
 *   需要的 OptimizationStage、RandomSeed 与 OptimizationError。后续任务
 *   增补词表只允许表尾追加（枚举值序＝登记契约，确定性 NFR-COR-02）。
 *
 * 背景说明（错误语义归类——AGENTS §2.5/卡 §6.6）：本域的错误分两轨——
 *   ① 调用方错误（研究定义非法、补丁触及锁定变量、值越界等）：fail-fast，
 *      抛 OptimizationError，stableCode() 携带 OPT-* 稳定码 token（码值
 *      权威＝DiagCodes.hpp 常量与卡面 §6.6 登记表，禁字符串拼码）；
 *   ② 环境错误（评估器异常、编译链失败等）：经诊断实例（diagnostics 工厂）
 *      结构化返回——归 WP-20-T04/T06 管线面，本任务不涉。
 * 线程安全：全部为纯值类型/纯函数；OptimizationError 不可变（消息在
 * runtime_error 基类），可跨线程拷贝传递。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_TYPES_HPP
#define SDURWS_IRD_OPTIMIZATION_TYPES_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace sdurws::ird::optimization {

// =====================================================================
// 优化阶段词表（卡 §4.3 原文契约）
// =====================================================================

/**
 * @brief 优化阶段词表（本域自有——core 无 EvaluationStage 枚举，不与之混用）。
 *
 * stage-b＝OPT-B 静态子集（阶段 B/R1，WP-20 落位范围——变量 9 类＋静态硬
 * 约束＋三项静态指标）；stage-d＝OPT-D 联合优化（阶段 D/R2，WP-21——P-04
 * 冻结后方可正式启用，REQUIREMENTS 附录 C 硬前置）。阶段的变量面差异见
 * Variable.hpp 的 builtinVariableDefinitions（离散器件类仅 StageD 启用）。
 */
enum class OptimizationStage {
    StageB,  ///< 阶段 B／OPT-B（R1 静态子集）
    StageD,  ///< 阶段 D／OPT-D（R2 联合优化——P-04 未冻结前不得正式启用）
};

/**
 * @brief 阶段稳定 token（"stage-b"/"stage-d"）——进 config.opt canonical
 *        与日志/导出的确定性书写（卡 §4.3 config.opt canonical 序列化
 *        的"阶段 token 定序定宽编码"要素）。
 * @param s [in] 阶段枚举值
 * @return 稳定 token 视图（编译期字面量，生命周期静态——调用方无需释放）
 */
std::string_view toToken(OptimizationStage s) noexcept;

// =====================================================================
// 随机种子（确定性来源之一；本任务仅承载别名——消费归 WP-20-T06 策略面）
// =====================================================================

/**
 * @brief 随机种子（卡 §4.3：语义与 KIN-13 一致——0 非法，不做静默替换，
 *        NFR-COR-03；合法域 ≥1）。
 *
 * 单位：无量纲整数。消费点＝候选生成策略（WP-20-T06）与审计抽样（R2）；
 * 种子进入 config.opt canonical（进 sliceId——同种子同线程配置 ⇒ 等价
 * 候选集合，卡 §4.4 I-OPT-3）。
 */
using RandomSeed = std::uint64_t;

// =====================================================================
// 域异常（卡 §12.3 错误语义——调用方错误 fail-fast 的异常轨载体）
// =====================================================================

/**
 * @brief optimization 域唯一异常类型（调用方错误 fail-fast 轨）。
 *
 * 为什么派生 std::runtime_error 而非 core::CoreError：core 的异常类型是
 * core 单元契约（units/core.md §4.10"core 唯一异常类型"）——跨单元复用会
 * 模糊"错误归谁所有"（diagnostics §8.1 转译按异常类型匹配，一型一码）；
 * 各域自有异常类型是已落位单元的统一先例（project/evidence/execution 同款
 * 域内异常）。消息 what() 供开发诊断（日志），stableCode() 供编排面结构化
 * 消费——两者都是只读不可变（抛出后可安全跨线程传递）。
 *
 * stableCode() 的取值域＝DiagCodes.hpp 的 OPT-* 码值常量（唯一书写点；
 * §6.6 登记表）。本任务抛出点只用两码：kOptInputInvalid（研究定义/配置
 * 非法——绑定互斥、步长≤0、种子 0 等）与 kOptPatchIllegal（补丁非法——
 * 未知绑定/重复/越界/非有限）。环境错误不用异常轨（卡 §12.3——走诊断
 * 实例＋结构化返回，归 T04/T06 管线面）。
 */
class OptimizationError : public std::runtime_error {
public:
    /**
     * @brief 由稳定码＋消息构造。
     * @param stableCode [in] OPT-* 稳定码 token（取 DiagCodes.hpp 常量——
     *        禁运行期拼码/私造码值，NFR-MNT-03 单一权威）
     * @param message [in] 开发诊断消息（中文，说明违规语义与定位；非用户
     *        可见文案——文案键归 diagnostics 注册表，NFR-REL-05）
     */
    OptimizationError(std::string_view stableCode, const std::string& message);

    /// 违反的 OPT-* 稳定码（与 what() 同源构造；引用有效期＝本对象生存期）。
    const std::string& stableCode() const noexcept { return m_stableCode; }

private:
    std::string m_stableCode;  ///< 稳定码 token（构造后不可变——错误语义面）
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_TYPES_HPP
