#ifndef RWS_STRUCTUREOPTIMIZATION_EVALUATIONSTAGE_HPP
#define RWS_STRUCTUREOPTIMIZATION_EVALUATIONSTAGE_HPP

#include "EvaluationPlan.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace rws {

enum class EvaluationStageStatus { NotEvaluated, Passed, Failed, DataInsufficient, Canceled };

struct EvaluationStageResult {
    std::string stageId;
    std::string version;
    EvaluationStageStatus status = EvaluationStageStatus::NotEvaluated;
    std::size_t requestedCount = 0;
    std::size_t completedCount = 0;
    std::vector<EvaluationPlanDiagnostic> diagnostics;

    bool complete() const { return status == EvaluationStageStatus::Passed || status == EvaluationStageStatus::Failed || status == EvaluationStageStatus::DataInsufficient; }
};

struct EvaluationStageContext {
    const EvaluationPlan& plan;
    std::string candidateFingerprint;
};

class EvaluationStage {
  public:
    virtual ~EvaluationStage() = default;
    virtual std::string id() const = 0;
    virtual std::string version() const { return "1"; }
    virtual std::vector<std::string> requiredCapabilities() const { return {}; }
    virtual EvaluationStageResult run(const EvaluationStageContext& context,
                                      std::atomic_bool* cancel) const = 0;
};

class EvaluationPipeline {
  public:
    void addStage(std::shared_ptr<const EvaluationStage> stage);
    const std::vector<std::shared_ptr<const EvaluationStage>>& stages() const { return _stages; }
    std::vector<EvaluationStageResult> run(const EvaluationPlan& plan,
                                           const std::string& candidateFingerprint = {},
                                           std::atomic_bool* cancel = nullptr) const;

  private:
    std::vector<std::shared_ptr<const EvaluationStage>> _stages;
};

const char* toString(EvaluationStageStatus status);

} // namespace rws

#endif
