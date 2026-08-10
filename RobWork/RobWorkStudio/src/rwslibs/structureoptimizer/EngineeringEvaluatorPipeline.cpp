#include "EngineeringEvaluatorPipeline.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace rws {

namespace {

/**
 * @brief 辅助函数：判断指定的工件列表（artifacts）中是否存在特定 ID 的工件。
 * 
 * @param artifacts 已生成的工件容器
 * @param artifactId 待查找的目标工件 ID 字符串
 * @return true 找到对应 ID 的工件
 * @return false 未找到对应 ID 的工件
 */
bool hasArtifact(const std::vector<EngineeringArtifact>& artifacts,
                 const std::string& artifactId)
{
    return std::any_of(artifacts.begin(), artifacts.end(),
                       [&artifactId](const EngineeringArtifact& artifact) {
                           return artifact.artifactId == artifactId;
                       });
}

/**
 * @brief 辅助函数：当流水线检测到缺失必要的依赖工件导致评估死锁时，构建并返回“数据不足”的异常结果对象。
 * 
 * @param candidate 候选解上下文
 * @param artifactId 导致死锁/缺失的具体工件 ID
 * @return EngineeringEvaluationResult 包含 DataInsufficient 状态和详细缺失警告的结果结构体
 */
EngineeringEvaluationResult insufficientResult(
    const CandidateEvaluationContext& candidate, const std::string& artifactId)
{
    EngineeringEvaluationResult result;
    // 标记评估状态为数据不足（ DataInsufficient ）
    result.status = EngineeringEvaluationStatus::DataInsufficient;
    result.inputSnapshot = candidate.inputSnapshot;
    
    // 构造详细的缺失工件分析警告，供 UI 或日志排查
    AnalysisWarning warning;
    warning.code = "EngineeringEvaluatorPipeline.MissingArtifact";
    warning.message = "Required artifact is unavailable: " + artifactId;
    warning.source = "EngineeringEvaluatorPipeline";
    warning.severity = AnalysisStatus::Fail;
    result.warnings.push_back(warning);
    
    return result;
}

} // namespace

/**
 * @brief 向流水线中注册添加一个独立的工程评估器。
 * 
 * @param evaluator 待添加的评估器对象引用
 */
void EngineeringEvaluatorPipeline::addEvaluator(IEngineeringEvaluator& evaluator)
{
    _evaluators.push_back(&evaluator);
}

/**
 * @brief 执行管线评估主逻辑：基于工件依赖关系（Data DAG）的动态拓扑调度算法。
 * 
 * @param candidate 当前候选解的上下文（包含机器人 3D 构件等）
 * @param request 评估请求参数（包含外部传入的初始工件等）
 * @param callbacks 控制回调接口（用于检查取消、暂停等）
 * @return EngineeringEvaluationResult 汇总所有评估器指标与工件后的最终结果
 */
EngineeringEvaluationResult EngineeringEvaluatorPipeline::evaluate(
    const CandidateEvaluationContext& candidate, const EvaluationRequest& request,
    const EvaluationCallbacks& callbacks) const
{
    // 初始化汇总结果结构体
    EngineeringEvaluationResult aggregate;
    aggregate.status = EngineeringEvaluationStatus::Success;
    aggregate.inputSnapshot = candidate.inputSnapshot;
    
    // 初始化可用工件库，包含请求中传入的初始工件
    std::vector<EngineeringArtifact> artifacts = request.inputArtifacts;
    
    // 将所有注册的评估器放入待处理（ pending ）队列中
    std::vector<IEngineeringEvaluator*> pending = _evaluators;

    // ── 拓扑调度主循环：只要待处理队列不为空，就继续尝试推演执行 ─────────────────────
    while (!pending.empty()) {
        bool progressed = false; // 标记本轮循环中是否有评估器成功被触发执行

        // 遍历当前尚未执行的评估器队列
        for (std::vector<IEngineeringEvaluator*>::iterator it = pending.begin();
             it != pending.end();) {
            IEngineeringEvaluator* evaluator = *it;
            bool ready = true;

            // 1. 检查该评估器依赖的所有前置工件（ requiredArtifactIds ）是否均已在 artifacts 库中就绪
            for (const std::string& artifactId : evaluator->requiredArtifactIds()) {
                if (!hasArtifact(artifacts, artifactId)) {
                    ready = false; // 缺少必要工件，当前评估器暂不可执行
                    break;
                }
            }

            // 2. 若依赖未满足，跳过该评估器，等待后续轮次
            if (!ready) {
                ++it;
                continue;
            }

            // 3. 依赖已满足，构建该评估器的专属请求，并透传最新的工件库
            EvaluationRequest evaluatorRequest = request;
            evaluatorRequest.inputArtifacts = artifacts;

            // 4. 正式触发调用该评估器的 evaluate() 进行单项指标计算
            EngineeringEvaluationResult result =
                evaluator->evaluate(candidate, evaluatorRequest, callbacks);

            // 5. 将该单项评估器产出的指标、约束、警告和新工件合并累加到 aggregate 汇总对象中
            aggregate.metrics.insert(aggregate.metrics.end(), result.metrics.begin(),
                                     result.metrics.end());
            aggregate.constraints.insert(aggregate.constraints.end(), result.constraints.begin(),
                                         result.constraints.end());
            aggregate.warnings.insert(aggregate.warnings.end(), result.warnings.begin(),
                                      result.warnings.end());
            aggregate.artifacts.insert(aggregate.artifacts.end(), result.artifacts.begin(),
                                       result.artifacts.end());

            // 同时将该评估器产出的新工件补充进全局工件库，供后续其他评估器作为输入依赖使用
            artifacts.insert(artifacts.end(), result.artifacts.begin(), result.artifacts.end());

            // 6. 若某个单项评估器返回了非 Success 状态（如出错或数据不足），立即中断管线并返回当前结果
            if (result.status != EngineeringEvaluationStatus::Success) {
                aggregate.status = result.status;
                return aggregate;
            }

            // 7. 该评估器已成功执行完毕，将其从 pending 待处理队列中安全擦除
            it = pending.erase(it);
            progressed = true; // 标记本轮调度有推进
        }

        // ── 死锁/缺失依赖检查 ──────────────────────────────────────────────────
        // 如果完整遍历了一轮 pending 队列后，没有任何一个评估器能够被触发执行（ progressed == false ），
        // 说明发生了依赖循环死锁或缺少了关键的输入工件，管线无法继续推进。
        if (!progressed) {
            for (IEngineeringEvaluator* evaluator : pending) {
                for (const std::string& artifactId : evaluator->requiredArtifactIds()) {
                    if (!hasArtifact(artifacts, artifactId))
                        // 抛出明确的数据不足（ DataInsufficient ）及缺失的工件 ID
                        return insufficientResult(candidate, artifactId);
                }
            }
        }
    }

    // 所有评估器均成功按依赖顺序执行完毕，返回最终汇总的工程评估结果
    return aggregate;
}

} // namespace rws