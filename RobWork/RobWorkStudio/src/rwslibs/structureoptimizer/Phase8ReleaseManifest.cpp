#include "Phase8ReleaseManifest.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace rws {

bool Phase8ReleaseAuditResult::hasCode(const std::string& code) const
{
    for (const Phase8ReleaseFinding& finding : findings)
        if (finding.code == code)
            return true;
    return false;
}

namespace {

void add(Phase8ReleaseAuditResult& result, const char* code, const char* message)
{
    result.findings.push_back({code, message});
}

std::string lower(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

bool containsTemporaryMarker(const std::string& value)
{
    const std::string normalized = lower(value);
    return normalized.find("qtemporarydir") != std::string::npos ||
           normalized.find("structure-optimizer-preview-") != std::string::npos ||
           normalized.find("%temp%") != std::string::npos ||
           normalized.find("/tmp/") != std::string::npos ||
           normalized.find("\\temp\\") != std::string::npos;
}

bool projectRelativeId(const std::string& value)
{
    if (value.empty() || value.front() == '/' || value.front() == '\\' ||
        (value.size() >= 2 && std::isalpha(static_cast<unsigned char>(value[0])) &&
         value[1] == ':') || value.find(':') != std::string::npos)
        return false;
    if (containsTemporaryMarker(value))
        return false;

    std::string segment;
    for (std::size_t i = 0; i <= value.size(); ++i) {
        const char character = i < value.size() ? value[i] : '/';
        if (character == '/' || character == '\\') {
            if (segment == "." || segment == "..")
                return false;
            segment.clear();
        }
        else {
            segment.push_back(character);
        }
    }
    return true;
}

bool validFiniteNonNegative(double value)
{
    return std::isfinite(value) && value >= 0.0;
}

std::string escapeJson(const std::string& value)
{
    std::ostringstream stream;
    stream << '"';
    for (unsigned char character : value) {
        switch (character) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20)
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(character) << std::dec;
            else
                stream << static_cast<char>(character);
            break;
        }
    }
    stream << '"';
    return stream.str();
}

std::string number(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(17) << value;
    return stream.str();
}

// 统一字段校验，避免 audit 与 serializeStable 互相调用造成递归。
void validateManifest(const Phase8ReleaseManifest& manifest,
                      Phase8ReleaseAuditResult& output)
{
    if (manifest.manifestVersion <= 0)
        add(output, "Phase8.Release.ManifestVersionInvalid", "Manifest version must be positive.");
    if (manifest.productVersion.empty())
        add(output, "Phase8.Release.ProductVersionMissing", "Product version is required.");
    if (manifest.envelopeSchemaVersion <= 0)
        add(output, "Phase8.Release.EnvelopeSchemaInvalid", "Envelope schema version must be positive.");
    if (manifest.evaluatorId.empty() || manifest.evaluatorVersion.empty())
        add(output, "Phase8.Release.EvaluatorMissing", "Evaluator id and version are required.");
    if (manifest.buildIdentifier.empty())
        add(output, "Phase8.Release.BuildIdentifierMissing", "Build identifier is required.");
    if (!validFiniteNonNegative(manifest.totalRunSeconds))
        add(output, "Phase8.Release.NonFiniteNumber", "Release timing must be finite and non-negative.");

    std::set<std::string> ids;
    for (const Phase8ReleaseArtifact& artifact : manifest.artifacts) {
        if (artifact.id.empty())
            add(output, "Phase8.Release.ArtifactIdMissing", "Artifact id is required.");
        else if (!ids.insert(artifact.id).second)
            add(output, "Phase8.Release.ArtifactDuplicate", "Artifact ids must be unique.");
        if (!projectRelativeId(artifact.projectResourceId))
            add(output, "Phase8.Release.ResourceIdNotRelative",
                "Artifact resource id must be project-relative and non-temporary.");
        if (artifact.fingerprint.empty())
            add(output, "Phase8.Release.FingerprintMissing", "Artifact fingerprint is required.");
        if (artifact.required && !artifact.present)
            add(output, "Phase8.Release.RequiredArtifactMissing",
                "A required release artifact is not present.");
        if (!validFiniteNonNegative(artifact.sizeMegabytes))
            add(output, "Phase8.Release.NonFiniteNumber", "Artifact size must be finite and non-negative.");
    }
}

} // namespace

Phase8ReleaseAuditResult Phase8ReleaseManifestAudit::audit(
    const Phase8ReleaseManifest& manifest)
{
    Phase8ReleaseAuditResult output;
    validateManifest(manifest, output);

    output.passed = output.findings.empty();
    if (output.passed) {
        std::string error;
        output.serializable = serializeStable(manifest, output.stableJson, &error);
        if (!output.serializable)
            add(output, "Phase8.Release.SerializationFailed", error.c_str());
        output.passed = output.serializable && output.findings.empty();
    }
    return output;
}

bool Phase8ReleaseManifestAudit::serializeStable(const Phase8ReleaseManifest& manifest,
                                                 std::string& json,
                                                 std::string* error)
{
    Phase8ReleaseAuditResult validation;
    validateManifest(manifest, validation);
    if (!validation.findings.empty()) {
        if (error)
            *error = validation.findings.front().message;
        json.clear();
        return false;
    }

    std::vector<Phase8ReleaseArtifact> artifacts = manifest.artifacts;
    std::sort(artifacts.begin(), artifacts.end(), [](const Phase8ReleaseArtifact& left,
                                                     const Phase8ReleaseArtifact& right) {
        if (left.id != right.id)
            return left.id < right.id;
        return left.projectResourceId < right.projectResourceId;
    });

    std::ostringstream stream;
    stream << "{\"artifacts\":[";
    for (std::size_t i = 0; i < artifacts.size(); ++i) {
        if (i > 0)
            stream << ',';
        const Phase8ReleaseArtifact& artifact = artifacts[i];
        stream << "{\"fingerprint\":" << escapeJson(artifact.fingerprint)
               << ",\"id\":" << escapeJson(artifact.id)
               << ",\"present\":" << (artifact.present ? "true" : "false")
               << ",\"projectResourceId\":" << escapeJson(artifact.projectResourceId)
               << ",\"required\":" << (artifact.required ? "true" : "false")
               << ",\"sizeMegabytes\":" << number(artifact.sizeMegabytes) << '}';
    }
    stream << "],\"buildIdentifier\":" << escapeJson(manifest.buildIdentifier)
           << ",\"evaluatorId\":" << escapeJson(manifest.evaluatorId)
           << ",\"evaluatorVersion\":" << escapeJson(manifest.evaluatorVersion)
           << ",\"envelopeSchemaVersion\":" << manifest.envelopeSchemaVersion
           << ",\"manifestVersion\":" << manifest.manifestVersion
           << ",\"productVersion\":" << escapeJson(manifest.productVersion)
           << ",\"totalRunSeconds\":" << number(manifest.totalRunSeconds) << '}';
    json = stream.str();
    return true;
}

} // namespace rws
