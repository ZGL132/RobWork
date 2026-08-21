#include "FinalValidationPlan.hpp"

#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace rws {
namespace {

void appendText(std::string& bytes, const std::string& value)
{
    // 长度前缀避免 seed 或指纹中的分隔符导致规范串歧义。
    bytes.append(std::to_string(value.size()));
    bytes.push_back(':');
    bytes.append(value);
    bytes.push_back(';');
}

std::string fingerprintFor(const std::string& bytes)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

} // namespace

FinalValidationPlanResult FinalValidationPlan::create(
    const std::string& searchPlanFingerprint,
    const std::vector<std::string>& verificationSeeds)
{
    FinalValidationPlanResult result;
    if (searchPlanFingerprint.empty()) {
        result.diagnostic = "Final validation requires a non-empty search plan fingerprint.";
        return result;
    }
    if (verificationSeeds.empty()) {
        result.diagnostic = "Final validation requires at least one verification seed.";
        return result;
    }

    std::unordered_set<std::string> uniqueSeeds;
    for (const std::string& seed : verificationSeeds) {
        if (seed.empty()) {
            result.diagnostic = "Final validation seeds must be non-empty.";
            return result;
        }
        if (!uniqueSeeds.insert(seed).second) {
            result.diagnostic = "Final validation seeds must be unique.";
            return result;
        }
    }

    FinalValidationPlan plan;
    plan.searchPlanFingerprint = searchPlanFingerprint;
    plan.verificationSeeds = verificationSeeds;
    std::string& bytes = plan.canonicalBytes;
    appendText(bytes, "structure-final-validation-plan-v1");
    appendText(bytes, plan.searchPlanFingerprint);
    appendText(bytes, std::to_string(plan.verificationSeeds.size()));
    for (const std::string& seed : plan.verificationSeeds)
        appendText(bytes, seed);
    plan.fingerprint = fingerprintFor(bytes);
    result.plan = std::move(plan);
    result.ok = true;
    return result;
}

} // namespace rws
