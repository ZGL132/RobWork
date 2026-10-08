/**
 * @file   Types.cpp
 * @brief  optimization 基础词表实现——阶段 token、候选状态 token、指标
 *         token、运行状态 token 与域异常（WP-20-T03 落位；WP-20-T04 表尾
 *         追加候选状态 token；WP-20-T05 表尾追加指标 token；WP-20-T06 表尾
 *         追加运行状态 token）。
 *
 * 设计依据：units/optimization.md §4.3（阶段 token "stage-b"/"stage-d"、
 * 候选状态七值词表——"候选状态 ≠ 任务状态 ≠ 工程判定"正交表、MetricId
 * 八值词表、RunPhase 九值词表与 §4.4 状态机）、§7.1（指标 token
 * "opt.metric.<slug>" 逐行原文）、§12.3（错误语义——fail-fast 异常携带
 * 稳定码）；DiagCodes.hpp 码值常量（唯一书写点——调用方传入，本文件不持
 * 码值字面量）。
 *
 * 确定性：token 为编译期字面量，与枚举值的对应关系恒定（NFR-COR-02）；
 * 候选状态 token 进候选表/导出/审计书写面，改名即消费面漂移——冻结。
 */

#include <sdurws/ird/optimization/Types.hpp>

#include <utility>

namespace sdurws::ird::optimization {

std::string_view toToken(OptimizationStage s) noexcept
{
    // 阶段稳定 token（卡 §4.3 config.opt canonical 的阶段编码要素）——
    // 编译期字面量，永不更改（进缓存键面，改名即全体身份漂移）。
    switch (s) {
    case OptimizationStage::StageB:
        return "stage-b";
    case OptimizationStage::StageD:
        return "stage-d";
    }
    // 不可达分支：枚举只有两个值；为未处理枚举值（未来追加——表尾追加
    // 纪律下的新值）兜底返回 StageB token 会让错误静默，故按域内约定
    // 断言失败路径不可达——返回空串并由消费方显式判空（防御式，不吞错）。
    return {};
}

std::string_view toToken(CandidateStatus s) noexcept
{
    // 候选状态稳定 token（卡 §4.3 七值；kebab 词形与证据侧状态词表消费
    // 惯例一致）。枚举值序＝卡面登记契约（Pending→…→ParetoNondominated），
    // 与 token 一一对应——值序一经交付不得改动/插入（表尾追加纪律）。
    switch (s) {
    case CandidateStatus::Pending:
        return "pending";
    case CandidateStatus::ScreenedOut:
        return "screened-out";
    case CandidateStatus::Infeasible:
        return "infeasible";
    case CandidateStatus::DataInsufficient:
        return "data-insufficient";
    case CandidateStatus::EvaluationFailed:
        return "evaluation-failed";
    case CandidateStatus::Feasible:
        return "feasible";
    case CandidateStatus::ParetoNondominated:
        return "pareto-nondominated";
    }
    // 不可达分支（同 toToken(OptimizationStage) 口径——防御式空串，不吞错）。
    return {};
}

OptimizationError::OptimizationError(std::string_view stableCode, const std::string& message)
    : std::runtime_error(message), m_stableCode(stableCode)
{
    // 码值与消息都为构造期拷贝——异常对象不可变（可安全跨线程传递，§12.3）。
    // stableCode 的取值域契约（DiagCodes.hpp 常量）由调用方保证；此处不校验
    // 注册表（注册表查询归 diagnostics 工厂面，异常轨不依赖装配状态）。
}

std::string_view toToken(MetricId m) noexcept
{
    // 指标稳定 token（卡 §7.1 表 MetricId 列逐行原样——"opt.metric.<slug>"）。
    // 枚举值序＝卡 §7.1 表行序（1~8），与 token 一一对应；token 进指标事实/
    // 候选表/导出书写面，改名即消费面漂移——冻结（NFR-MNT-03 单一书写点）。
    switch (m) {
    case MetricId::Envelope:
        return "opt.metric.envelope";
    case MetricId::StructuralMass:
        return "opt.metric.structural-mass";
    case MetricId::MinJointMargin:
        return "opt.metric.min-joint-margin";
    case MetricId::CycleTime:
        return "opt.metric.cycle-time";
    case MetricId::DeviceCost:
        return "opt.metric.device-cost";
    case MetricId::DeviceMass:
        return "opt.metric.device-mass";
    case MetricId::JointPositiveWork:
        return "opt.metric.joint-positive-work";
    case MetricId::MinDriveMargin:
        return "opt.metric.min-drive-margin";
    }
    // 不可达分支（同本文件既有 token 函数口径——防御式空串，不吞错）。
    return {};
}

std::string_view toToken(RunPhase p) noexcept
{
    // 运行状态稳定 token（卡 §4.3/§4.4 九值；kebab 词形）。枚举值序＝卡面
    // 登记契约（Draft→…→Interrupted），与 token 一一对应；token 进运行
    // 记录/导出/审计书写面——改名即消费面漂移，冻结（WP-20-T06 表尾追加）。
    switch (p) {
    case RunPhase::Draft:
        return "draft";
    case RunPhase::Preflight:
        return "preflight";
    case RunPhase::QuickScreening:
        return "quick-screening";
    case RunPhase::VerifiedReview:
        return "verified-review";
    case RunPhase::RobustnessReview:
        return "robustness-review";
    case RunPhase::Completed:
        return "completed";
    case RunPhase::Canceled:
        return "canceled";
    case RunPhase::Failed:
        return "failed";
    case RunPhase::Interrupted:
        return "interrupted";
    }
    // 不可达分支（同本文件既有 token 函数口径——防御式空串，不吞错）。
    return {};
}

}  // namespace sdurws::ird::optimization
