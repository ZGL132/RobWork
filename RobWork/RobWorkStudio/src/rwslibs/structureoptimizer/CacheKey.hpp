#ifndef RWS_STRUCTUREOPTIMIZATION_CACHEKEY_HPP
#define RWS_STRUCTUREOPTIMIZATION_CACHEKEY_HPP

#include "CandidateResult.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace rws {

/**
 * 评估缓存条目的完整内容身份。
 * 这里保存影响结果的输入内容，不保存指针、地址或文件时间戳。
 */
struct CacheKeyInput
{
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string requirementFingerprint;
    std::string evaluationPlanFingerprint;
    std::string designSpaceFingerprint;
    std::string designVectorFingerprint;

    std::string compilerId;
    std::string compilerVersion;
    std::string compilerConfiguration;
    std::string evaluatorId;
    std::string evaluatorVersion;
    std::string evaluatorConfiguration;
    std::string solverId;
    std::string solverVersion;
    std::string solverConfiguration;
    std::string toolFingerprint;

    std::vector<std::string> samplingPlan;
    std::string samplingMethod;
    std::uint64_t samplingSeed = 0;
    double positionToleranceMeters = 0.0;
    double orientationToleranceRadians = 0.0;
    AnalysisEvidenceStage evidenceStage = AnalysisEvidenceStage::Quick;
    std::string platformNumericPolicy;
};

struct CacheKeyResult;

struct CacheKey
{
    int schemaVersion = 1;
    std::string canonicalBytes;
    std::string fingerprint;

    bool operator==(const CacheKey& other) const
    {
        return schemaVersion == other.schemaVersion && canonicalBytes == other.canonicalBytes;
    }
    bool operator!=(const CacheKey& other) const { return !(*this == other); }
    bool operator<(const CacheKey& other) const
    {
        if (schemaVersion != other.schemaVersion) return schemaVersion < other.schemaVersion;
        return canonicalBytes < other.canonicalBytes;
    }

    // 先生成规范字节串，再计算稳定指纹；失败时返回可读诊断。
    static CacheKeyResult create(const CacheKeyInput& input);
};

struct CacheKeyResult
{
    bool ok = false;
    CacheKey key;
    std::string diagnostic;
};

} // namespace rws

#endif
