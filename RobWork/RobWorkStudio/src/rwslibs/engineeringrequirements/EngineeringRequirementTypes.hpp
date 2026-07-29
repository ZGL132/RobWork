#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTTYPES_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTTYPES_HPP

#include <array>
#include <string>
#include <vector>

namespace rws {

enum class RequirementLevel { Must, Should, Info };
enum class PoseTaskSource { Manual, CapturedTcp, FrameOffset, GeometryFeature, Template, Imported };
enum class ProcessType { Generic, Pick, Place, MachineLoad, MachineUnload, Inspect, WeldStart,
                         WeldEnd, ToolChange, SafeStandby, Handover };
enum class OrientationMode { Fixed, AlignFrame, AlignGeometryNormal, PointAtTarget };
enum class OffsetAxis { ToolZ, ReferenceZ };
enum class GeometryFeatureType { None, FrameOrigin, FramePlaneNormal };

struct RobotModelBinding {
    std::string sourcePath;
    std::string robotModelFingerprint;
    std::string robotName;
};

struct PoseTolerance {
    double positionMeters = 0.001;
    double orientationDeg = 1.0;
    bool allowToolRollFree = false;
};

struct OrientationRule {
    OrientationMode mode = OrientationMode::Fixed;
    std::string targetFrame;
    std::string targetGeometry;
    std::string targetPoint;
    bool invertNormal = false;
    bool allowToolRollFree = false;
    double rollMinimumDeg = -180.0;
    double rollMaximumDeg = 180.0;
};

struct ApproachRetractRule {
    bool enabled = false;
    OffsetAxis axis = OffsetAxis::ToolZ;
    double distanceMeters = 0.0;
    bool collisionFreeRequired = true;
};

struct ValidationPolicy {
    bool collisionFreeRequired = true;
    double minimumJointMargin = 0.0;
    double minimumManipulability = 0.0;
};

struct GeometryFeatureReference {
    GeometryFeatureType type = GeometryFeatureType::None;
    std::string frameName;
    std::string objectName;
    std::string geometryName;
};

struct GenerationParameter {
    std::string key;
    std::string value;
};

// Keeps generated stations traceable without making the requirement set depend on a UI dialog.
struct StationGenerationProvenance {
    std::string generatorId;
    std::string instanceId;
    bool linked = false;
    std::vector<GenerationParameter> parameters;
};

struct KeyStation {
    std::string id;
    std::string name;
    ProcessType processType = ProcessType::Generic;
    RequirementLevel level = RequirementLevel::Must;
    PoseTaskSource source = PoseTaskSource::Manual;
    std::string refFrame = "WORLD";
    std::string tcpFrame;
    std::array<double, 3> position = {{0.0, 0.0, 0.0}};
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};
    PoseTolerance tolerance;
    GeometryFeatureReference geometryFeature;
    StationGenerationProvenance generation;
    OrientationRule orientation;
    ApproachRetractRule approach;
    ApproachRetractRule retract;
    ValidationPolicy validation;
    double confidence = 1.0;
    std::string note;
};

// Backward-compatible name used by the initial MVP and its existing JSON field.
using PoseTask = KeyStation;

struct BoxRegion {
    std::string id;
    std::string name;
    RequirementLevel level = RequirementLevel::Must;
    std::string refFrame = "WORLD";
    std::array<double, 3> center = {{0.0, 0.0, 0.0}};
    std::array<double, 3> size = {{0.1, 0.1, 0.1}};
    double minimumCoverage = 0.8;
    int samplesPerAxis = 5;
};

struct RequirementSet {
    int schemaVersion = 1;
    std::string name;
    int version = 1;
    bool frozen = false;
    RobotModelBinding modelBinding;
    std::vector<PoseTask> poseTasks;
    std::vector<BoxRegion> boxRegions;
};

struct CompiledPoseTask {
    std::string id;
    std::string name;
    RequirementLevel level = RequirementLevel::Must;
    std::string refFrame;
    std::string tcpFrame;
    std::array<double, 3> position = {{0.0, 0.0, 0.0}};
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};
    PoseTolerance tolerance;
    ProcessType processType = ProcessType::Generic;
    GeometryFeatureReference geometryFeature;
    OrientationRule orientation;
    ValidationPolicy validation;
    bool pathValidationPending = false;
};

struct WorkspaceDemandRegion {
    std::string id;
    std::string name;
    RequirementLevel level = RequirementLevel::Must;
    std::string refFrame;
    std::array<double, 3> center = {{0.0, 0.0, 0.0}};
    std::array<double, 3> size = {{0.1, 0.1, 0.1}};
    double minimumCoverage = 0.8;
    int samplesPerAxis = 5;
};

struct RequirementDiagnostic {
    std::string requirementId;
    RequirementLevel level = RequirementLevel::Must;
    std::string message;
    bool blocking = true;
};

struct CompiledRequirementSet {
    int schemaVersion = 1;
    bool frozen = false;
    std::string compilerVersion = "EngineeringRequirements.MVP.1";
    RobotModelBinding modelBinding;
    std::string requirementFingerprint;
    std::vector<CompiledPoseTask> poseTasks;
    std::vector<WorkspaceDemandRegion> workspaceRegions;
    std::vector<RequirementDiagnostic> diagnostics;
};

const char* toString(RequirementLevel value);
const char* toString(PoseTaskSource value);
const char* toString(ProcessType value);
const char* toString(OrientationMode value);
const char* toString(OffsetAxis value);
const char* toString(GeometryFeatureType value);
bool requirementLevelFromString(const std::string& text, RequirementLevel& value);
bool poseTaskSourceFromString(const std::string& text, PoseTaskSource& value);
bool processTypeFromString(const std::string& text, ProcessType& value);
bool orientationModeFromString(const std::string& text, OrientationMode& value);
bool offsetAxisFromString(const std::string& text, OffsetAxis& value);
bool geometryFeatureTypeFromString(const std::string& text, GeometryFeatureType& value);

} // namespace rws

#endif
