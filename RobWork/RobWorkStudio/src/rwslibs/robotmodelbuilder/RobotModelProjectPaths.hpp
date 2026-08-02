#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELPROJECTPATHS_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELPROJECTPATHS_HPP

#include "RobotModelSpec.hpp"

#include <QString>

namespace rws {

class RobotModelProjectPaths
{
  public:
    static bool makePortable (const RobotModelSpec& runtime,
                              const QString& projectRoot,
                              RobotModelSpec& portable,
                              QString* error = nullptr);

    static bool resolveManaged (const RobotModelSpec& portable,
                                const QString& projectRoot,
                                RobotModelSpec& runtime,
                                QString* error = nullptr);
};

}    // namespace rws

#endif
