#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

namespace rws {

class RobotModelXmlWriter
{
  public:
    static RobotModelSpec makeDefaultSixAxisModel (const QString& saveDirectory);
    static QString sanitizeFileBaseName (const QString& name);
    static bool validate (const RobotModelSpec& spec, QStringList& errors);
    static QString makeSerialDeviceXml (const RobotModelSpec& spec);
    static QString makeSceneXml (const RobotModelSpec& spec);
    static QString serialDeviceFilePath (const RobotModelSpec& spec);
    static QString sceneFilePath (const RobotModelSpec& spec);
    static bool saveFiles (const RobotModelSpec& spec, QStringList& errors);

  private:
    static QString number (double value);
    static QString vector3 (const std::array< double, 3 >& values);
    static double degToRad (double value);
};

}    // namespace rws

#endif
