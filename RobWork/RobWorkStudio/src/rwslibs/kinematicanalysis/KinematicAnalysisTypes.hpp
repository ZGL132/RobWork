#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP

#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>

#include <array>
#include <string>
#include <vector>

namespace rws {

enum class KinematicFailureReason
{
    None,
    NoDevice,
    NoTcpFrame,
    IkNoSolution,
    Collision,
    JointLimit,
    NearJointLimit,
    Singular,
    NearSingular,
    InvalidTarget,
    SolverError
};

enum class WorkspaceColorMode
{
    Reachability,
    Manipulability,
    JointLimitMargin,
    Collision
};

enum class WorkspaceSamplingMode
{
    RandomUniform,
    Grid
};

struct WorkspaceSamplingConfig
{
    WorkspaceSamplingMode mode         = WorkspaceSamplingMode::RandomUniform;
    int sampleCount                    = 1000;
    int gridStepsPerJoint              = 5;
    bool checkCollision                = true;
    unsigned int randomSeed            = 1;
};

struct PoseReachabilityConfig
{
    int directionSamples = 24;
    int rollSamples      = 1;
    bool checkCollision  = true;
};

struct KinematicThresholds
{
    double nearJointLimitRatio      = 0.05;
    double singularValueWarning     = 1e-4;
    double conditionWarning         = 100.0;
    double conditionFail            = 1000.0;
    double manipulabilityWarning    = 1e-5;
    double positionToleranceMeters  = 0.001;
    double orientationToleranceDeg  = 1.0;
};

struct KinematicCurrentPoseResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::string deviceName;
    std::string tcpFrameName;
    std::vector< double > q;
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};
    std::array< double, 3 > tcpRpyDeg   = {{0.0, 0.0, 0.0}};
    std::vector< double > jointLimitMargins;
    double minJointLimitMargin = 0.0;
    std::vector< double > jacobianRowMajor;
    int jacobianRows = 0;
    int jacobianCols = 0;
    std::vector< double > singularValues;
    double conditionNumber = 0.0;
    double manipulability  = 0.0;
    std::vector< AnalysisWarning > warnings;
};

struct KinematicIkSolution
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    std::vector< double > q;
    double distanceToCurrentQ    = 0.0;
    double minJointLimitMargin   = 0.0;
    double manipulability        = 0.0;
    double conditionNumber       = 0.0;
    double positionErrorMeters   = 0.0;
    double orientationErrorDeg   = 0.0;
    bool inCollision             = false;
    double score                 = 0.0;
    std::vector< KinematicFailureReason > failureReasons;
};

struct KinematicIkAnalysisResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint target;
    std::vector< KinematicIkSolution > solutions;
    std::vector< AnalysisWarning > warnings;
};

struct TaskPointReachabilityResult
{
    AnalysisStatus status = AnalysisStatus::Unknown;
    TaskPoint taskPoint;
    KinematicIkAnalysisResult ik;
    KinematicFailureReason primaryFailure    = KinematicFailureReason::None;
    std::vector< KinematicFailureReason > failureReasons;
};

struct WorkspaceSample
{
    std::vector< double > q;
    std::array< double, 3 > tcpPosition = {{0.0, 0.0, 0.0}};
    double manipulability     = 0.0;
    double minJointLimitMargin = 0.0;
    double conditionNumber    = 0.0;
    bool inCollision          = false;
    AnalysisStatus status     = AnalysisStatus::Unknown;
};

struct PoseReachabilitySample
{
    std::array< double, 3 > position = {{0.0, 0.0, 0.0}};
    int sampledDirections = 0;
    int reachableDirections = 0;
    double coverage = 0.0;
    AnalysisStatus status = AnalysisStatus::Unknown;
};

struct KinematicAnalysisResult
{
    AnalysisResultHeader header;
    AnalysisStatus status = AnalysisStatus::Unknown;
    KinematicCurrentPoseResult currentPose;
    std::vector< TaskPointReachabilityResult > taskPointResults;
    double reachableRate = 0.0;
    std::vector< PoseReachabilitySample > poseReachability;
    std::vector< WorkspaceSample > workspaceSamples;
    std::vector< AnalysisWarning > singularityWarnings;
    std::vector< AnalysisWarning > jointLimitWarnings;
    std::vector< MetricValue > manipulabilityMap;
    std::vector< AnalysisWarning > warnings;
};

const char* toString(KinematicFailureReason reason);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISTYPES_HPP
