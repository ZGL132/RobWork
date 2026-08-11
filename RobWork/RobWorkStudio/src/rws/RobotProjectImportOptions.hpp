#ifndef RWS_ROBOTPROJECTIMPORTOPTIONS_HPP
#define RWS_ROBOTPROJECTIMPORTOPTIONS_HPP

#include <QStringList>

namespace rws {

enum class MeshImportMode { Disabled, VisualOnly, VisualAndCollision };
enum class MissingMeshPolicy { Fail, GenerateCylinder };
enum class AssetImportPolicy { ManagedCopy, ExternalReference };

struct RobotProjectImportOptions
{
    MeshImportMode meshImportMode = MeshImportMode::VisualAndCollision;
    MissingMeshPolicy missingMeshPolicy = MissingMeshPolicy::Fail;
    AssetImportPolicy assetPolicy = AssetImportPolicy::ManagedCopy;
    QStringList packageRoots;
    QString xacroExecutable;
    QStringList xacroArguments;
};

}    // namespace rws

#endif    // RWS_ROBOTPROJECTIMPORTOPTIONS_HPP
