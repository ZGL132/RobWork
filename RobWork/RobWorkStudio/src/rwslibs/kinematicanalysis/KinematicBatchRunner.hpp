// =============================================================================
//  KinematicBatchRunner.hpp —— 批量评估运行器(声明)
// =============================================================================
//
// 本组件把"一批执行需求任务"(RequirementExecutionSet)批量交给
// TargetEvaluator 逐条评估,并聚合成 RequirementValidationSummary。
// 它负责的额外职责:
//   - 处理未纳入编译集的任务(compileState != Included,标记 NotEvaluated);
//   - 透传任务的碰撞要求,并把"仅 Must 级任务"作为整体可行性判定的依据;
//   - 支持进度回调(progressCallback)与取消(CancellationToken),
//     便于 UI 在批量长任务期间持续刷新进度与响应中止;
//   - 定义缓存键(KinematicBatchCacheKey),使相同模型 / 环境 / 需求 /
//     参数组合的批量结果可以被上游缓存复用。
//
// 整体可行性聚合规则(见 validateRequirements):存在任一 Must 级任务
// DataInsufficient => DataInsufficient;否则存在任一 Must 级任务不可达 =>
// Infeasible;否则 Feasible(质量为所有 Must 任务中最差的等级)。
#ifndef RWS_KINEMATICANALYSIS_KINEMATICBATCHRUNNER_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICBATCHRUNNER_HPP

#include "TargetEvaluator.hpp"

namespace rws {

// =============================================================================
//  BatchRunOptions —— 一次批量运行的选项
// =============================================================================
//
// evidenceStage 作为批量运行的统一证据等级覆盖到每个任务;
// progressCallback 在每完成一个任务(以及每个批次起点)被调用,
// 用于驱动 UI 进度条;若为 nullptr 则不报告进度。
struct BatchRunOptions
{
    // 批量运行统一的证据等级,覆盖单个任务的默认等级。
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Verified;
    // 传给每个 TargetEvaluator::evaluate 的任务级选项(容差 / 碰撞等)。
    TargetEvaluationOptions targetOptions;
    // 进度回调触发粒度:每处理 maxBatchSize 个任务报告一次中间进度。
    int maxBatchSize = 128;
    // 进度回调函数指针;为 nullptr 表示不需要进度报告。
    void (*progressCallback) (int completed, int total, void* userData) = nullptr;
    // 随进度回调透传的用户数据(通常为 UI 控件指针)。
    void* progressUserData = nullptr;
};

// =============================================================================
//  KinematicBatchCacheKey —— 批量结果的缓存键
// =============================================================================
//
// 缓存键的设计不变量:只有当"模型指纹、环境指纹、需求指纹、证据等级、
// 配置哈希、随机种子"全部相同时,批量评估结果才允许复用。任何一项变化
// (哪怕只是某个容差被调过)都必须使缓存失效,因此 configHash 聚合了
// 所有影响结果的参数。
struct KinematicBatchCacheKey
{
    // 机器人模型指纹(设备 / TCP 等结构变化会使指纹改变)。
    std::string modelFingerprint;
    // 环境指纹(WorkCell / 障碍 / 碰撞对象等)。
    std::string environmentFingerprint;
    // 需求集合指纹(任务列表及其参数)。
    std::string requirementFingerprint;
    // 证据等级(Estimated / Quick / Verified),不同等级结果不可混用。
    AnalysisEvidenceStage analysisStage = AnalysisEvidenceStage::Verified;
    // 评估参数(容差 / 采样数等)的规范化哈希。
    std::string configHash;
    // 随机种子;种子不同则 IK 采样可能不同,故计入缓存键。
    unsigned int seed = 0;

    // 序列化为单行可读字符串,便于作为缓存存储的键名。
    std::string toString () const;
    // 值相等比较:全部字段逐一相等才相等。
    bool operator== (const KinematicBatchCacheKey& other) const;
    // 值不等比较:由 == 取反推导。
    bool operator!= (const KinematicBatchCacheKey& other) const
    {
        return !(*this == other);
    }
};

// 便捷工厂:把各项指纹 / 等级 / 哈希 / 种子打包成缓存键。
// 调用方只要保证传入的 configHash 已聚合全部影响结果的参数,缓存语义即正确。
KinematicBatchCacheKey makeKinematicBatchCacheKey (
    const std::string& modelFingerprint,
    const std::string& environmentFingerprint,
    const std::string& requirementFingerprint,
    AnalysisEvidenceStage analysisStage,
    const std::string& configHash,
    unsigned int seed);

// =============================================================================
//  KinematicBatchRunner —— 批量评估运行器(无状态,可安全复用)
// =============================================================================
//
// 内部复用 TargetEvaluator(局部创建),类本身不保存状态。
class KinematicBatchRunner
{
  public:
    // 批量评估 requirements 中的全部任务,返回聚合摘要。
    // 返回的 RequirementValidationSummary 中:
    //   - taskResults 与输入任务一一对应(含未纳入执行集的任务,标记 NotEvaluated);
    //   - mustTaskCount / mustTaskFeasibleCount 统计 Must 级任务的可行比例;
    //   - 整体 feasibility / quality 仅由 Must 级任务决定
    //     (Should / Info 级任务不参与硬性判定,只出现在明细里)。
    RequirementValidationSummary validateRequirements(
        const AnalysisContext& context,
        const RequirementExecutionSet& requirements,
        const BatchRunOptions& options = BatchRunOptions (),
        const CancellationToken& cancellation = CancellationToken ()) const;
};

} // namespace rws

#endif // RWS_KINEMATICANALYSIS_KINEMATICBATCHRUNNER_HPP
