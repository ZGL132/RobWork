#ifndef RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP
#define RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP

#include "RobotModelSpec.hpp"

#include <QStringList>

#include <string>

namespace rw { namespace models {
    class JointDevice;
    class WorkCell;
}}    // namespace rw::models

namespace rw { namespace kinematics {
    class Frame;
    class State;
}}    // namespace rw::kinematics

namespace rw { namespace math {
    template< class T > class Transform3D;
    typedef Transform3D< double > Transform3Dd;
}}    // namespace rw::math

namespace rws {

class WorkCellConverter
{
  public:
    static RobotModelSpec convert (const rw::models::WorkCell& workcell,
                                   const rw::kinematics::State& state,
                                   const std::string& saveDirectory,
                                   QStringList& warnings);

    static bool hasSerialDevice (const rw::models::WorkCell& workcell);
    static std::string inferWorkCellFilePath (const rw::models::WorkCell& workcell);
    static std::string inferSaveDirectory (const rw::models::WorkCell& workcell);
    static bool hasConvertibleRobotModel (const RobotModelSpec& spec);

  private:
    static bool extractSerialDevice (const rw::models::WorkCell& workcell,
                                     RobotModelSpec& spec,
                                     QStringList& warnings);
    static void extractJoints (const rw::models::JointDevice& device,
                               RobotModelSpec& spec,
                               QStringList& warnings);
    static void extractLimits (const rw::models::JointDevice& device,
                               RobotModelSpec& spec);
    static void extractQConfigs (const rw::models::JointDevice& device,
                                 RobotModelSpec& spec);
    static void extractSceneFrames (const rw::models::WorkCell& workcell,
                                    const rw::kinematics::State& state,
                                    RobotModelSpec& spec);
    static void extractDrawables (const rw::models::WorkCell& workcell,
                                  RobotModelSpec& spec,
                                  QStringList& warnings);
    static void extractCollisionSetup (const rw::models::WorkCell& workcell,
                                       RobotModelSpec& spec);
    static void extractProximitySetup (const rw::models::WorkCell& workcell,
                                       RobotModelSpec& spec);

    static bool tryLoadSidecar (const rw::models::WorkCell& workcell,
                                const std::string& saveDirectory,
                                RobotModelSpec& spec,
                                QStringList& warnings);
    static void mergeCompanionXmlMetadata (const rw::models::WorkCell& workcell,
                                           RobotModelSpec& spec,
                                           QStringList& warnings);
    static void mergeCollisionSetupXml (const QString& file, RobotModelSpec& spec,
                                        QStringList& warnings);
    static void mergeProximitySetupXml (const QString& file, RobotModelSpec& spec,
                                        QStringList& warnings);
    static void mergeDynamicWorkCellXml (const QString& file, RobotModelSpec& spec,
                                         QStringList& warnings);

    static void transformToRpyPos (const rw::math::Transform3Dd& t,
                                   std::array< double, 3 >& rpyDeg,
                                   std::array< double, 3 >& pos);
    static bool hasShowFrameAxes (const rw::kinematics::Frame& frame);
    static bool isDAF (const rw::kinematics::Frame* frame,
                       const rw::kinematics::State& state);
};

}    // namespace rws

#endif    // RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP
