#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

#include <array>

namespace rws {

class RobotModelXmlWriter
{
  public:
    static RobotModelSpec makeDefaultSixAxisModel (const QString& saveDirectory);
    static QString sanitizeFileBaseName (const QString& name);
    static bool validate (const RobotModelSpec& spec, QStringList& errors);
    static QString makeSerialDeviceXml (const RobotModelSpec& spec);
    static QString makeSceneXml (const RobotModelSpec& spec);
    static QString makeDynamicWorkCellXml (const RobotModelSpec& spec);
    static QString serialDeviceFilePath (const RobotModelSpec& spec);
    static QString sceneFilePath (const RobotModelSpec& spec);
    static QString dynamicWorkCellFilePath (const RobotModelSpec& spec);
    static bool saveFiles (const RobotModelSpec& spec, QStringList& errors);

    // 计算从 joint_i 到 joint_{i+1} 的连杆圆柱姿态 (center, RPY)
    static void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                                 std::array< double, 3 >& posOut,
                                 std::array< double, 3 >& rpyDegOut,
                                 double& lengthOut);
    // 重新计算所有 Link{i}To{i+1} Drawable 的 pos/rpy/length
    static void applyLinkGeometry (RobotModelSpec& spec);

  private:
    static QString number (double value);
    static QString vector3 (const std::array< double, 3 >& values);
    static double degToRad (double value);
};

}    // namespace rws

#endif