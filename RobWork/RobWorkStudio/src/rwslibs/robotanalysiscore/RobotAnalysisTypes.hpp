#ifndef RWS_ROBOTANALYSISCORE_ROBOTANALYSISTYPES_HPP
#define RWS_ROBOTANALYSISCORE_ROBOTANALYSISTYPES_HPP

#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

#include <array>
#include <string>
#include <vector>

namespace rws {

//! @brief Generic pass/warning/fail state used by analysis plugins.
enum class AnalysisStatus
{
    Unknown,
    Pass,
    Warning,
    Fail
};

//! @brief Semantic category for a task point.
enum class TaskPointType
{
    Generic,
    Pick,
    Place,
    Weld,
    Glue,
    Inspect,
    Screw,
    Custom
};

//! @brief Pose tolerance for task point validation.
struct PoseTolerance
{
    double positionMeters = 0.001;
    double orientationDeg = 1.0;
    bool allowToolRollFree = false;
};

//! @brief A target pose used by kinematics, dynamics, selection, and trajectory validation.
struct TaskPoint
{
    std::string id;
    std::string name;
    TaskPointType type = TaskPointType::Generic;

    std::string refFrame = "WORLD";
    std::string tcpFrame = "TCP";

    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    std::array< double, 3 > rpyDeg   = {{0.0, 0.0, 0.0}};

    PoseTolerance tolerance;
    double weight = 1.0;
    bool enabled  = true;
    std::string note;
};

//! @brief Payload data shared by dynamics and drive selection.
struct PayloadSpec
{
    std::string name = "Payload";
    double mass      = 0.0;
    std::array< double, 3 > cog     = {{0.0, 0.0, 0.0}};
    std::array< double, 6 > inertia = {{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
};

//! @brief Shared robot design context consumed by analysis plugins.
struct RobotDesignContext
{
    std::string projectName;
    std::string robotName;

    std::string sourceModelPath;
    std::string sourceScenePath;
    std::string sourceDynamicWorkCellPath;

    RobotModelSpec modelSpec;

    std::string deviceName;
    std::string baseFrame = "Base";
    std::string tcpFrame  = "TCP";
    std::string refFrame  = "WORLD";

    PayloadSpec payload;
    std::vector< TaskPoint > taskPoints;
};

//! @brief Named scalar metric with a display unit.
struct MetricValue
{
    std::string name;
    double value = 0.0;
    std::string unit;
};

//! @brief Warning or diagnostic message emitted by an analysis plugin.
struct AnalysisWarning
{
    std::string code;
    std::string message;
    std::string source;
    AnalysisStatus severity = AnalysisStatus::Warning;
};

//! @brief Per-joint summary shared across analysis result types.
struct JointAnalysisSummary
{
    std::string jointName;
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< MetricValue > metrics;
    std::vector< AnalysisWarning > warnings;
};

//! @brief Common result metadata.
struct AnalysisResultHeader
{
    std::string pluginName;
    std::string pluginVersion;
    std::string robotName;
    std::string createdAt;
};

//! @brief Lightweight generic result envelope used for reports and cross-plugin summaries.
struct AnalysisResult
{
    AnalysisResultHeader header;
    AnalysisStatus status = AnalysisStatus::Unknown;
    double score          = 0.0;

    std::vector< JointAnalysisSummary > jointSummaries;
    std::vector< AnalysisWarning > warnings;
};

}    // namespace rws

#endif    // RWS_ROBOTANALYSISCORE_ROBOTANALYSISTYPES_HPP
