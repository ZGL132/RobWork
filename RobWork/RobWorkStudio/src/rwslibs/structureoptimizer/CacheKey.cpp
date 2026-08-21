#include "CacheKey.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace rws {
namespace {

void appendText(std::string& bytes, const std::string& value)
{
    bytes.append(std::to_string(value.size()));
    bytes.push_back(':');
    bytes.append(value);
    bytes.push_back(';');
}

void appendInteger(std::string& bytes, std::uint64_t value)
{
    bytes.append(std::to_string(value));
    bytes.push_back(';');
}

void appendDouble(std::string& bytes, double value)
{
    if (value == 0.0) value = 0.0;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    static const char hex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        bytes.push_back(hex[(bits >> shift) & 0xf]);
    bytes.push_back(';');
}

std::string fnv1a64(const std::string& bytes)
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

bool required(const std::string& value) { return !value.empty(); }

} // namespace

CacheKeyResult CacheKey::create(const CacheKeyInput& input)
{
    CacheKeyResult result;
    if (!required(input.modelFingerprint) || !required(input.environmentFingerprint) ||
        !required(input.requirementFingerprint) || !required(input.evaluationPlanFingerprint) ||
        !required(input.designSpaceFingerprint) || !required(input.designVectorFingerprint) ||
        !required(input.compilerId) || !required(input.compilerVersion) ||
        !required(input.evaluatorId) || !required(input.evaluatorVersion) ||
        !required(input.solverId) || !required(input.solverVersion) ||
        !required(input.toolFingerprint) || !required(input.samplingMethod)) {
        result.diagnostic = "Cache key identity and version fields must be non-empty.";
        return result;
    }
    if (!std::isfinite(input.positionToleranceMeters) ||
        !std::isfinite(input.orientationToleranceRadians) ||
        input.positionToleranceMeters < 0.0 || input.orientationToleranceRadians < 0.0) {
        result.diagnostic = "Cache key tolerances must be finite and non-negative.";
        return result;
    }

    // 采样计划按稳定顺序编码，避免调用方容器插入顺序造成伪 miss。
    std::vector<std::string> sampling = input.samplingPlan;
    std::sort(sampling.begin(), sampling.end());

    CacheKey key;
    std::string& bytes = key.canonicalBytes;
    appendText(bytes, "structure-evaluation-cache-key-v1");
    appendText(bytes, input.modelFingerprint);
    appendText(bytes, input.environmentFingerprint);
    appendText(bytes, input.requirementFingerprint);
    appendText(bytes, input.evaluationPlanFingerprint);
    appendText(bytes, input.designSpaceFingerprint);
    appendText(bytes, input.designVectorFingerprint);
    appendText(bytes, input.compilerId);
    appendText(bytes, input.compilerVersion);
    appendText(bytes, input.compilerConfiguration);
    appendText(bytes, input.evaluatorId);
    appendText(bytes, input.evaluatorVersion);
    appendText(bytes, input.evaluatorConfiguration);
    appendText(bytes, input.solverId);
    appendText(bytes, input.solverVersion);
    appendText(bytes, input.solverConfiguration);
    appendText(bytes, input.toolFingerprint);
    appendText(bytes, input.samplingMethod);
    appendInteger(bytes, input.samplingSeed);
    appendInteger(bytes, sampling.size());
    for (const std::string& item : sampling) appendText(bytes, item);
    appendDouble(bytes, input.positionToleranceMeters);
    appendDouble(bytes, input.orientationToleranceRadians);
    appendInteger(bytes, static_cast<std::uint64_t>(input.evidenceStage));
    appendText(bytes, input.platformNumericPolicy);
    key.fingerprint = fnv1a64(bytes);
    result.key = std::move(key);
    result.ok = true;
    return result;
}

} // namespace rws
