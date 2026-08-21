#ifndef RWS_STRUCTUREOPTIMIZATION_PHASE8RELEASEMANIFEST_HPP
#define RWS_STRUCTUREOPTIMIZATION_PHASE8RELEASEMANIFEST_HPP

#include <string>
#include <vector>

namespace rws {

/**
 * @brief 发布包中一个可追溯工件的声明。
 *
 * projectResourceId 必须是项目资源 ID，而不是磁盘绝对路径；这样清单可以
 * 在不同机器之间复现，并由现有 ProjectResource 解析器负责定位实际文件。
 */
struct Phase8ReleaseArtifact
{
    std::string id;
    std::string projectResourceId;
    std::string fingerprint;
    bool required = true;
    bool present = true;
    double sizeMegabytes = 0.0;
};

/** @brief Phase 8 发布清单的稳定协议字段。 */
struct Phase8ReleaseManifest
{
    int manifestVersion = 1;
    std::string productVersion;
    int envelopeSchemaVersion = 1;
    std::string evaluatorId;
    std::string evaluatorVersion;
    std::string buildIdentifier;
    double totalRunSeconds = 0.0;
    std::vector<Phase8ReleaseArtifact> artifacts;
};

struct Phase8ReleaseFinding
{
    std::string code;
    std::string message;
};

struct Phase8ReleaseAuditResult
{
    bool passed = false;
    bool serializable = false;
    std::string stableJson;
    std::vector<Phase8ReleaseFinding> findings;

    bool hasCode(const std::string& code) const;
};

/**
 * @brief S84 发布清单的纯核心审计和稳定 JSON 序列化器。
 *
 * 该类不读取文件系统，也不依赖 UI；文件是否已被打包由 present 字段和上层
 * ProjectResource 解析结果提供。序列化前会拒绝绝对路径、临时目录和非有限数值。
 */
class Phase8ReleaseManifestAudit
{
public:
    static Phase8ReleaseAuditResult audit(const Phase8ReleaseManifest& manifest);

    static bool serializeStable(const Phase8ReleaseManifest& manifest,
                                std::string& json,
                                std::string* error = nullptr);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_PHASE8RELEASEMANIFEST_HPP
