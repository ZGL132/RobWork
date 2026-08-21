#ifndef RWS_STRUCTUREOPTIMIZATION_FINALVALIDATIONPLAN_HPP
#define RWS_STRUCTUREOPTIMIZATION_FINALVALIDATIONPLAN_HPP

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 独立 Final Verification 的冻结采样计划。
 *
 * searchPlanFingerprint 标识搜索阶段使用的计划；fingerprint 则把独立
 * Final 阶段的固定 seed 顺序和 schemaVersion 一并编码。因此即使两个阶段
 * 使用相同候选设计，也不会因为重复执行同一确定性输入而伪造新的证据。
 */
struct FinalValidationPlan
{
    int schemaVersion = 1;
    std::string searchPlanFingerprint;
    std::vector<std::string> verificationSeeds;
    std::string canonicalBytes;
    std::string fingerprint;

    bool valid() const
    {
        return !searchPlanFingerprint.empty() && !verificationSeeds.empty() &&
               !canonicalBytes.empty() && !fingerprint.empty();
    }

    /** 按固定 seed 顺序创建稳定的独立验证计划。 */
    static struct FinalValidationPlanResult create(
        const std::string& searchPlanFingerprint,
        const std::vector<std::string>& verificationSeeds);
};

struct FinalValidationPlanResult
{
    bool ok = false;
    FinalValidationPlan plan;
    std::string diagnostic;
};

} // namespace rws

#endif
