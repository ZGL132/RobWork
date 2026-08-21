#include "StructureOptimizationContracts.hpp"

namespace rws {
namespace {

/**
 * @brief 内部辅助函数：处理枚举反序列化失败时的错误信息格式化。
 * @param[in] value 引起解析失败的非法字符串
 * @param[out] error 可选的错误信息字符串接收指针
 * @return 始终返回 false，便于在调用处以单行返回语法 `return conversionFailed(...)` 结束。
 */
bool conversionFailed(const std::string& value, std::string* error)
{
    if (error != nullptr)
        *error = "Unknown CandidateLifecycle: " + value;
    return false;
}

}    // namespace

/**
 * @brief 判断当前评估阶段是否 100% 完整执行。
 *
 * @details
 * 判定依据必须同时满足两个硬性条件：
 * 1. 执行期间未被取消（!canceled）；
 * 2. 实际成功完成的样本计数严格等于计划请求数（completedCount == requestedCount）。
 */
bool EvaluationCompletion::complete() const
{
    return !canceled && completedCount == requestedCount;
}

/**
 * @brief 判断当前评估阶段是否处于“部分完成/数据不完整”状态。
 *
 * @details
 * 只要满足以下任一条件即判定为 Partial：
 * 1. 任务中途收到了取消信号（canceled == true）；
 * 2. 实际完成的样本数未达到计划请求的总数（completedCount < requestedCount）。
 *
 * 此方法为上层判定是否触发 `Feasibility::DataInsufficient` 提供了事实基础。
 */
bool EvaluationCompletion::partial() const
{
    return canceled || completedCount < requestedCount;
}

/**
 * @brief 将 CandidateLifecycle 枚举转换为标准化的常量文本。
 *
 * @details
 * 保证导出的字符串与 JSON 序列化、UI 展示以及日志调试格式严格统一。
 * 若遇到非法枚举整数值，安全回退返回 "Unknown"，防止内存越界。
 */
const char* toString(CandidateLifecycle lifecycle)
{
    switch (lifecycle) {
        case CandidateLifecycle::Pending: return "Pending";
        case CandidateLifecycle::Compiling: return "Compiling";
        case CandidateLifecycle::Evaluating: return "Evaluating";
        case CandidateLifecycle::Completed: return "Completed";
        case CandidateLifecycle::Failed: return "Failed";
        case CandidateLifecycle::Canceled: return "Canceled";
    }
    return "Unknown";
}

/**
 * @brief 从文本字符串严格反序列化 CandidateLifecycle 枚举。
 *
 * @details
 * 采用严格的白名单字符串匹配：
 * - 匹配成功：写入解析结果，清空 error 字符串，返回 true；
 * - 匹配失败（如未知字符串、空字符串）：调用 conversionFailed 构造诊断信息，绝不静默使用默认值，返回 false。
 */
bool candidateLifecycleFromString(const std::string& text,
                                  CandidateLifecycle& value,
                                  std::string* error)
{
    if (text == "Pending") value = CandidateLifecycle::Pending;
    else if (text == "Compiling") value = CandidateLifecycle::Compiling;
    else if (text == "Evaluating") value = CandidateLifecycle::Evaluating;
    else if (text == "Completed") value = CandidateLifecycle::Completed;
    else if (text == "Failed") value = CandidateLifecycle::Failed;
    else if (text == "Canceled") value = CandidateLifecycle::Canceled;
    else return conversionFailed(text, error);

    if (error != nullptr)
        error->clear();
    return true;
}

/**
 * @brief 核心向后兼容单向映射函数：将旧版单一状态投影为四维正交状态。
 *
 * @details
 * 【核心架构规则与映射逻辑】
 * 1. Pending:
 *    保持默认投影值（Lifecycle: Pending, Feasibility: NotEvaluated, EvidenceStage: Quick, Quality: Unknown）。
 *
 * 2. Feasible:
 *    旧版表示计算完成且合格 -> Lifecycle: Completed, Feasibility: Feasible, Quality: Good。
 *
 * 3. Infeasible:
 *    旧版表示计算完成但不合格 -> Lifecycle: Completed, Feasibility: Infeasible, Quality: Critical。
 *
 * 4. Failed:
 *    系统/计算崩溃导致失败 -> Lifecycle: Failed, Feasibility: DataInsufficient。
 *    【注】：系统错误绝不能被映射为物理上的 Infeasible，因为并未得到有效的工程证据，只能认定为“数据不足”。
 *
 * 5. Canceled:
 *    用户取消计算 -> Lifecycle: Canceled, Feasibility: DataInsufficient。
 *    【注】：任务中断代表证据链不全，严禁将未算完的方案定性为不可行，必须明确标记为“数据不足”。
 */
CandidateStateProjection projectLegacyCandidateStatus(StructureCandidateStatus legacyStatus)
{
    CandidateStateProjection projected;
    switch (legacyStatus) {
        case StructureCandidateStatus::Pending:
            // 保持结构体初始化时的默认值
            break;
        case StructureCandidateStatus::Feasible:
            projected.lifecycle = CandidateLifecycle::Completed;
            projected.feasibility = Feasibility::Feasible;
            projected.quality = Quality::Good;
            break;
        case StructureCandidateStatus::Infeasible:
            projected.lifecycle = CandidateLifecycle::Completed;
            projected.feasibility = Feasibility::Infeasible;
            projected.quality = Quality::Critical;
            break;
        case StructureCandidateStatus::Failed:
            projected.lifecycle = CandidateLifecycle::Failed;
            projected.feasibility = Feasibility::DataInsufficient;
            break;
        case StructureCandidateStatus::Canceled:
            projected.lifecycle = CandidateLifecycle::Canceled;
            projected.feasibility = Feasibility::DataInsufficient;
            break;
    }
    return projected;
}

}    // namespace rws