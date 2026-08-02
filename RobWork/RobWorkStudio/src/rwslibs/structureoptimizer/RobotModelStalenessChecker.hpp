#ifndef RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP
#define RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP

#include <rwslibs/robotanalysiscore/RobotAnalysisTypes.hpp>

#include <QString>

namespace rws {

enum class RobotModelSourceStatus
{
    Untracked,
    Current,
    Stale,
    SourceMissing,
    SourceInvalid
};

struct RobotModelStalenessResult
{
    RobotModelSourceStatus status = RobotModelSourceStatus::Untracked;
    QString resolvedSourcePath;
    QString message;
};

//! Compares a frozen optimization snapshot against its parameterized model source.
class RobotModelStalenessChecker
{
  public:
    static RobotModelStalenessResult check(const RobotDesignContext& context,
                                           const QString& projectPath);
    static RobotModelStalenessResult checkManaged(const RobotDesignContext& context,
                                                  const QString& projectPath,
                                                  const QString& managedProjectRoot);
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_ROBOTMODELSTALENESSCHECKER_HPP
