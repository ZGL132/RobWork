#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELPUBLISHSERVICE_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELPUBLISHSERVICE_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

#include <functional>

namespace rws {

struct RobotModelPublishRequest
{
    RobotModelSpec spec;
    QString projectRoot;
    std::function< bool (const QString&, const QStringList&, QString*) > promote;
};

class RobotModelPublishService
{
  public:
    static bool publishAndLoad (const RobotModelPublishRequest& request,
                                QString* error = nullptr);
};

}    // namespace rws

#endif
