#ifndef RWS_ROBOTPROJECTSOURCEPACKAGER_HPP
#define RWS_ROBOTPROJECTSOURCEPACKAGER_HPP

#include "ProjectManifest.hpp"
#include "RobotProjectImportOptions.hpp"

#include <QMap>

namespace rws {

struct PackagedRobotSource
{
    QString projectRoot;
    QString stagingRoot;
    QString stagingAttemptRoot;
    QString stagedManagedUrdfPath;
    ProjectResource sourceResource;
    QVector< ProjectResource > assetResources;
    QMap< QString, QString > stagedFilesByProjectPath;
    bool removeEmptyStagingParent = false;
};

class RobotProjectSourcePackager
{
  public:
    static bool prepare (const QString& sourceUrdfPath,
                         const QString& targetProjectFilePath,
                         PackagedRobotSource& packaged,
                         QString* error = nullptr);
    static bool prepare (const QString& sourceUrdfPath,
                         const QString& targetProjectFilePath,
                         const RobotProjectImportOptions& options,
                         PackagedRobotSource& packaged,
                         QString* error = nullptr);

    static void discard (PackagedRobotSource& packaged);
};

}    // namespace rws

#endif    // RWS_ROBOTPROJECTSOURCEPACKAGER_HPP
