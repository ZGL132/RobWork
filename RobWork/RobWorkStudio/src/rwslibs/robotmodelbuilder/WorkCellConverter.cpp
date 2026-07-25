#include "WorkCellConverter.hpp"

#include "RobotModelSpecJson.hpp"
#include "RobotModelXmlWriter.hpp"

#include <rw/core/PropertyMap.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/JointDevice.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/PrismaticJoint.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionSetup.hpp>
#include <rw/proximity/ProximitySetup.hpp>
#include <rw/proximity/ProximitySetupRule.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using namespace rws;

namespace {

QString qstr (const std::string& value)
{
    return QString::fromStdString (value);
}

std::vector< double > parseDoubles (const QString& text)
{
    std::vector< double > values;
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double value = part.toDouble (&ok);
        if (ok)
            values.push_back (value);
    }
    return values;
}

QString resolveRelativeTo (const QString& anchorFile, const QString& file)
{
    const QString trimmed = file.trimmed ();
    if (trimmed.isEmpty ())
        return trimmed;
    const QFileInfo info (trimmed);
    if (info.isAbsolute ())
        return info.absoluteFilePath ();
    return QDir (QFileInfo (anchorFile).absolutePath ()).absoluteFilePath (trimmed);
}

QString resolveRelativeToDirectory (const QString& directory, const QString& file)
{
    const QString trimmed = file.trimmed ();
    if (trimmed.isEmpty ())
        return trimmed;
    const QFileInfo info (trimmed);
    if (info.isAbsolute ())
        return info.absoluteFilePath ();
    return QDir (directory).absoluteFilePath (trimmed);
}

QString withoutSceneSuffix (const QString& name)
{
    if (name.endsWith ("Scene", Qt::CaseSensitive))
        return name.left (name.size () - 5);
    return name;
}

bool containsString (const std::vector< std::string >& values, const std::string& candidate)
{
    return std::find (values.begin (), values.end (), candidate) != values.end ();
}

bool containsFramePair (const std::vector< FramePairSpec >& values, const FramePairSpec& candidate)
{
    for (const FramePairSpec& value : values) {
        if (value.first == candidate.first && value.second == candidate.second)
            return true;
    }
    return false;
}

void addFramePairOnce (std::vector< FramePairSpec >& values, const FramePairSpec& candidate)
{
    if (!candidate.first.empty () && !candidate.second.empty () &&
        !containsFramePair (values, candidate)) {
        values.push_back (candidate);
    }
}

void addQProperty (const rw::core::PropertyMap& map,
                   const std::string& name,
                   RobotModelSpec& spec)
{
    const rw::math::Q* q = map.getPtr< rw::math::Q > (name);
    if (q == NULL || q->size () == 0)
        return;

    for (const PoseSpec& pose : spec.poses) {
        if (pose.name == name)
            return;
    }

    PoseSpec pose;
    pose.name = name;
    for (size_t i = 0; i < q->size (); ++i)
        pose.q.push_back ((*q)[i] * rw::math::Rad2Deg);
    spec.poses.push_back (pose);
}

void collectSubtreeFrames (const rw::kinematics::Frame* frame,
                           const rw::kinematics::State& state,
                           std::set< const rw::kinematics::Frame* >& frames)
{
    if (frame == NULL || frames.find (frame) != frames.end ())
        return;
    frames.insert (frame);
    const rw::kinematics::Frame::const_iterator_pair children = frame->getChildren (state);
    for (rw::kinematics::Frame::const_iterator it = children.first; it != children.second; ++it)
        collectSubtreeFrames (&*it, state, frames);
}

bool isDeviceFrameName (const std::vector< rw::core::Ptr< rw::models::Device > >& devices,
                        const std::string& frameName)
{
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        rw::core::Ptr< rw::models::JointDevice > jointDevice =
            dev.cast< rw::models::JointDevice > ();
        if (jointDevice == NULL)
            continue;
        if (jointDevice->getBase () != NULL && jointDevice->getBase ()->getName () == frameName)
            return true;
        for (const rw::models::Joint* joint : jointDevice->getJoints ()) {
            if (joint != NULL && joint->getName () == frameName)
                return true;
        }
    }
    return false;
}

void addUnique (std::vector< std::string >& values, const std::string& value)
{
    if (!value.empty () && std::find (values.begin (), values.end (), value) == values.end ())
        values.push_back (value);
}

std::vector< std::string > deviceScopePrefixes (const rw::models::WorkCell& workcell,
                                                const RobotModelSpec& spec)
{
    std::vector< std::string > prefixes;
    addUnique (prefixes, spec.robotName);
    addUnique (prefixes, withoutSceneSuffix (qstr (workcell.getName ())).toStdString ());

    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        if (dev == NULL)
            continue;
        addUnique (prefixes, dev->getName ());
    }

    std::sort (prefixes.begin (), prefixes.end (),
               [] (const std::string& a, const std::string& b) {
                   return a.size () > b.size ();
               });
    return prefixes;
}

std::string stripDeviceScope (const std::string& name,
                              const std::vector< std::string >& prefixes)
{
    for (const std::string& prefix : prefixes) {
        const std::string scoped = prefix + ".";
        if (name.size () > scoped.size () && name.compare (0, scoped.size (), scoped) == 0)
            return name.substr (scoped.size ());
    }
    return name;
}

void normalizeDeviceScopedNames (const rw::models::WorkCell& workcell,
                                 RobotModelSpec& spec)
{
    const std::vector< std::string > prefixes = deviceScopePrefixes (workcell, spec);
    if (prefixes.empty ())
        return;

    for (JointTransformSpec& joint : spec.transformJoints)
        joint.name = stripDeviceScope (joint.name, prefixes);
    for (JointLimitSpec& limit : spec.limits)
        limit.jointName = stripDeviceScope (limit.jointName, prefixes);
    for (DrawableSpec& drawable : spec.drawables) {
        drawable.name = stripDeviceScope (drawable.name, prefixes);
        drawable.refFrame = stripDeviceScope (drawable.refFrame, prefixes);
    }
    for (CollisionModelSpec& collision : spec.collisionModels) {
        collision.name = stripDeviceScope (collision.name, prefixes);
        collision.refFrame = stripDeviceScope (collision.refFrame, prefixes);
    }
    for (FrameSpec& frame : spec.sceneFrames) {
        frame.name = stripDeviceScope (frame.name, prefixes);
        frame.refFrame = stripDeviceScope (frame.refFrame, prefixes);
    }
    for (SceneGeometrySpec& geometry : spec.sceneGeometries) {
        geometry.name = stripDeviceScope (geometry.name, prefixes);
        geometry.refFrame = stripDeviceScope (geometry.refFrame, prefixes);
    }
    for (FramePairSpec& pair : spec.collisionSetup.excludePairs) {
        pair.first = stripDeviceScope (pair.first, prefixes);
        pair.second = stripDeviceScope (pair.second, prefixes);
    }
    for (std::string& frame : spec.collisionSetup.volatileFrames)
        frame = stripDeviceScope (frame, prefixes);
    for (JointForceLimitSpec& limit : spec.dynamics.forceLimits)
        limit.jointName = stripDeviceScope (limit.jointName, prefixes);
    for (LinkDynamicsSpec& link : spec.dynamics.links) {
        link.linkName = stripDeviceScope (link.linkName, prefixes);
        link.objectName = stripDeviceScope (link.objectName, prefixes);
    }
    spec.dynamics.baseFrame = stripDeviceScope (spec.dynamics.baseFrame, prefixes);
}

void readVector3 (QXmlStreamReader& xml, std::array< double, 3 >& values)
{
    const std::vector< double > parsed = parseDoubles (xml.readElementText ());
    if (parsed.size () == 3)
        values = {{parsed[0], parsed[1], parsed[2]}};
}

void readDrawableShape (QXmlStreamReader& xml, DrawableSpec& drawable)
{
    const QString shape = xml.name ().toString ();
    drawable.shape = shape.toStdString ();
    const QXmlStreamAttributes attributes = xml.attributes ();
    if (shape == "Box") {
        drawable.dimensions = {{attributes.value ("x").toDouble (),
                                attributes.value ("y").toDouble (),
                                attributes.value ("z").toDouble ()}};
    }
    else if (shape == "Plane") {
        drawable.dimensions[0] = attributes.value ("x").toDouble ();
        drawable.dimensions[1] = attributes.value ("y").toDouble ();
    }
    else if (shape == "Cylinder" || shape == "Cone") {
        drawable.radius = attributes.value ("radius").toDouble ();
        drawable.length = attributes.value ("z").toDouble ();
    }
    else if (shape == "Sphere") {
        drawable.radius = attributes.value ("radius").toDouble ();
    }
    else if (shape == "Polytope" || shape == "Mesh" || shape == "STL") {
        drawable.filePath = attributes.value ("file").toString ().toStdString ();
    }
    xml.skipCurrentElement ();
}

void readDrawableElement (QXmlStreamReader& xml, DrawableSpec& drawable)
{
    const QXmlStreamAttributes attributes = xml.attributes ();
    drawable.name = attributes.value ("name").toString ().toStdString ();
    drawable.refFrame = attributes.value ("refframe").toString ().toStdString ();
    drawable.collisionModel = attributes.value ("colmodel").toString () == "Enabled";
    while (xml.readNextStartElement ()) {
        const QString name = xml.name ().toString ();
        if (name == "RPY")
            readVector3 (xml, drawable.rpyDeg);
        else if (name == "Pos")
            readVector3 (xml, drawable.pos);
        else if (name == "RGB")
            readVector3 (xml, drawable.rgb);
        else
            readDrawableShape (xml, drawable);
    }
}

SceneGeometrySpec sceneGeometryFromDrawable (const DrawableSpec& drawable)
{
    SceneGeometrySpec result;
    result.name = drawable.name;
    result.refFrame = drawable.refFrame;
    result.kind = geometryKindFromString (drawable.shape);
    result.size = drawable.dimensions;
    result.radius = drawable.radius;
    result.length = drawable.length;
    result.file = drawable.filePath;
    result.rpyDeg = drawable.rpyDeg;
    result.pos = drawable.pos;
    result.rgb = drawable.rgb;
    result.collisionModel = drawable.collisionModel;
    return result;
}

bool mergeSourceGeometryDocument (const QString& fileName,
                                  RobotModelSpec& spec,
                                  QStringList& warnings,
                                  std::set< QString >& visited)
{
    const QString absoluteFile = QFileInfo (fileName).absoluteFilePath ();
    if (visited.find (absoluteFile) != visited.end ())
        return true;
    visited.insert (absoluteFile);

    QFile file (absoluteFile);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read imported XML %1.").arg (absoluteFile);
        return false;
    }

    QXmlStreamReader xml (&file);
    bool deviceDocument = false;
    QStringList includes;
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;
        const QString name = xml.name ().toString ();
        if (name == "SerialDevice") {
            deviceDocument = true;
            spec.imported.deviceFile = QDir (qstr (spec.saveDirectory)).relativeFilePath (absoluteFile)
                                          .toStdString ();
        }
        else if (name == "WorkCell") {
            spec.imported.sceneFile = QDir (qstr (spec.saveDirectory)).relativeFilePath (absoluteFile)
                                         .toStdString ();
        }
        else if (name == "Include" && !deviceDocument) {
            const QString include = xml.attributes ().value ("file").toString ();
            if (!include.isEmpty ())
                includes << resolveRelativeTo (absoluteFile, include);
            xml.skipCurrentElement ();
        }
        else if (name == "Drawable") {
            DrawableSpec drawable;
            readDrawableElement (xml, drawable);
            if (deviceDocument)
                spec.drawables.push_back (drawable);
            else
                spec.sceneGeometries.push_back (sceneGeometryFromDrawable (drawable));
        }
        else if (name == "CollisionModel" && deviceDocument) {
            DrawableSpec drawable;
            readDrawableElement (xml, drawable);
            CollisionModelSpec collision;
            collision.name = drawable.name;
            collision.refFrame = drawable.refFrame;
            collision.shape = drawable.shape;
            collision.filePath = drawable.filePath;
            collision.dimensions = drawable.dimensions;
            collision.radius = drawable.radius;
            collision.length = drawable.length;
            collision.rpyDeg = drawable.rpyDeg;
            collision.pos = drawable.pos;
            spec.collisionModels.push_back (collision);
        }
    }
    if (xml.hasError ()) {
        warnings << QString ("Could not parse imported XML %1: %2")
                        .arg (absoluteFile, xml.errorString ());
        return false;
    }
    for (const QString& include : includes)
        mergeSourceGeometryDocument (include, spec, warnings, visited);
    return true;
}

bool mergeSourceGeometry (const rw::models::WorkCell& workcell,
                          RobotModelSpec& spec,
                          QStringList& warnings)
{
    const QString source = qstr (WorkCellConverter::inferWorkCellFilePath (workcell));
    if (source.isEmpty () || !QFileInfo::exists (source))
        return false;

    spec.drawables.clear ();
    spec.sceneGeometries.clear ();
    spec.collisionModels.clear ();
    spec.imported.active = true;
    std::set< QString > visited;
    return mergeSourceGeometryDocument (source, spec, warnings, visited);
}

}    // namespace

RobotModelSpec WorkCellConverter::convert (const rw::models::WorkCell& workcell,
                                           const rw::kinematics::State& state,
                                           const std::string& saveDirectory,
                                           QStringList& warnings)
{
    RobotModelSpec spec;
    std::string wcName = workcell.getName ();
    if (wcName.size () >= 5 && wcName.compare (wcName.size () - 5, 5, "Scene") == 0)
        spec.robotName = wcName.substr (0, wcName.size () - 5);
    else
        spec.robotName = wcName;

    spec.saveDirectory = saveDirectory.empty () ? inferSaveDirectory (workcell) : saveDirectory;
    spec.mode = KinematicsViewMode::JointRPYPos;
    spec.showFrameAxes = false;
    spec.generateDrawables = true;
    spec.generateScene = true;
    spec.dynamics.generateDynamicWorkCell = false;

    extractSerialDevice (workcell, spec, warnings);
    extractSceneFrames (workcell, state, spec);
    extractDrawables (workcell, spec, warnings);
    extractCollisionSetup (workcell, spec);
    extractProximitySetup (workcell, spec);

    mergeSourceGeometry (workcell, spec, warnings);

    RobotModelXmlWriter::refreshDhProjectionFromTransform (spec);

    mergeCompanionXmlMetadata (workcell, spec, warnings);

    RobotModelSpec sidecarSpec;
    if (tryLoadSidecar (workcell, spec.saveDirectory, sidecarSpec, warnings)) {
        spec = sidecarSpec;
        spec.saveDirectory = saveDirectory.empty () ? inferSaveDirectory (workcell) : saveDirectory;
        mergeCompanionXmlMetadata (workcell, spec, warnings);
    }

    normalizeDeviceScopedNames (workcell, spec);

    return spec;
}

bool WorkCellConverter::hasSerialDevice (const rw::models::WorkCell& workcell)
{
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        if (dev.cast< rw::models::JointDevice > () != NULL)
            return true;
    }
    return false;
}

std::string WorkCellConverter::inferWorkCellFilePath (const rw::models::WorkCell& workcell)
{
    const std::string filename = workcell.getFilename ();
    if (!filename.empty ())
        return filename;

    const std::string* prop =
        workcell.getPropertyMap ().getPtr< std::string > ("WorkCellFileName");
    if (prop != NULL && !prop->empty ())
        return *prop;

    const std::string filePath = workcell.getFilePath ();
    return filePath;
}

std::string WorkCellConverter::inferSaveDirectory (const rw::models::WorkCell& workcell)
{
    const std::string file = inferWorkCellFilePath (workcell);
    if (!file.empty ()) {
        const QFileInfo info (qstr (file));
        if (info.isDir ())
            return info.absoluteFilePath ().toStdString ();
        return info.absolutePath ().toStdString ();
    }
    return QDir::currentPath ().toStdString ();
}

bool WorkCellConverter::hasConvertibleRobotModel (const RobotModelSpec& spec)
{
    return !spec.robotName.empty () && !spec.transformJoints.empty ();
}

bool WorkCellConverter::extractSerialDevice (const rw::models::WorkCell& workcell,
                                             RobotModelSpec& spec,
                                             QStringList& warnings)
{
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    rw::core::Ptr< rw::models::JointDevice > jointDevice = NULL;
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        jointDevice = dev.cast< rw::models::JointDevice > ();
        if (jointDevice != NULL)
            break;
    }

    if (jointDevice == NULL) {
        warnings << "No JointDevice found in WorkCell. Cannot extract robot kinematics.";
        return false;
    }

    if (spec.robotName.empty ())
        spec.robotName = jointDevice->getName ();

    extractJoints (*jointDevice, spec, warnings);
    extractLimits (*jointDevice, spec);
    extractQConfigs (*jointDevice, spec);

    const rw::kinematics::Frame* base = jointDevice->getBase ();
    if (base != NULL && hasShowFrameAxes (*base))
        spec.showFrameAxes = true;

    return true;
}

void WorkCellConverter::extractJoints (const rw::models::JointDevice& device,
                                       RobotModelSpec& spec,
                                       QStringList& warnings)
{
    spec.transformJoints.clear ();
    const std::vector< rw::models::Joint* >& joints = device.getJoints ();
    for (size_t i = 0; i < joints.size (); ++i) {
        const rw::models::Joint* joint = joints[i];
        if (joint == NULL) {
            warnings << QString ("Null joint at index %1.").arg (static_cast< int > (i));
            continue;
        }

        JointTransformSpec out;
        out.name = joint->getName ();
        if (dynamic_cast< const rw::models::PrismaticJoint* > (joint) != NULL)
            out.type = "Prismatic";
        else if (dynamic_cast< const rw::models::RevoluteJoint* > (joint) != NULL)
            out.type = "Revolute";
        else {
            out.type = "Revolute";
            warnings << QString ("Joint %1 has unknown type; importing as Revolute.")
                            .arg (qstr (out.name));
        }
        transformToRpyPos (joint->getFixedTransform (), out.rpyDeg, out.pos);
        spec.transformJoints.push_back (out);
    }
}

void WorkCellConverter::extractLimits (const rw::models::JointDevice& device,
                                       RobotModelSpec& spec)
{
    spec.limits.clear ();
    const std::vector< rw::models::Joint* >& joints = device.getJoints ();
    const std::pair< rw::math::Q, rw::math::Q > bounds = device.getBounds ();
    const rw::math::Q velLimits = device.getVelocityLimits ();
    const rw::math::Q accLimits = device.getAccelerationLimits ();

    for (size_t i = 0; i < joints.size (); ++i) {
        const rw::models::Joint* joint = joints[i];
        if (joint == NULL)
            continue;
        const bool prismatic =
            dynamic_cast< const rw::models::PrismaticJoint* > (joint) != NULL;

        JointLimitSpec limit;
        limit.jointName = joint->getName ();

        const std::pair< rw::math::Q, rw::math::Q >& jointBounds = joint->getBounds ();
        if (jointBounds.first.size () > 0 && jointBounds.second.size () > 0) {
            limit.posMin = jointBounds.first (0);
            limit.posMax = jointBounds.second (0);
        }
        else if (bounds.first.size () > static_cast< int > (i) &&
                 bounds.second.size () > static_cast< int > (i)) {
            limit.posMin = bounds.first (static_cast< int > (i));
            limit.posMax = bounds.second (static_cast< int > (i));
        }
        else {
            limit.posMin = prismatic ? -1.0 : -RobotModelXmlWriter::kPi;
            limit.posMax = prismatic ? 1.0 : RobotModelXmlWriter::kPi;
        }

        if (!prismatic) {
            limit.posMin *= rw::math::Rad2Deg;
            limit.posMax *= rw::math::Rad2Deg;
        }

        if (velLimits.size () > static_cast< int > (i))
            limit.velMax = velLimits (static_cast< int > (i));
        else if (joint->getMaxVelocity ().size () > 0)
            limit.velMax = joint->getMaxVelocity () (0);
        else
            limit.velMax = prismatic ? 1.0 : RobotModelXmlWriter::kPi;

        if (accLimits.size () > static_cast< int > (i))
            limit.accMax = accLimits (static_cast< int > (i));
        else if (joint->getMaxAcceleration ().size () > 0)
            limit.accMax = joint->getMaxAcceleration () (0);
        else
            limit.accMax = prismatic ? 1.0 : 2.0 * RobotModelXmlWriter::kPi;

        if (!prismatic) {
            limit.velMax *= rw::math::Rad2Deg;
            limit.accMax *= rw::math::Rad2Deg;
        }

        spec.limits.push_back (limit);
    }
}

void WorkCellConverter::extractQConfigs (const rw::models::JointDevice& device,
                                         RobotModelSpec& spec)
{
    spec.poses.clear ();
    static const char* names[] = {"Home", "Zero", "Ready", "Setup"};
    for (const char* name : names)
        addQProperty (device.getPropertyMap (), name, spec);
    if (device.getBase () != NULL) {
        for (const char* name : names)
            addQProperty (device.getBase ()->getPropertyMap (), name, spec);
    }
}

void WorkCellConverter::extractSceneFrames (const rw::models::WorkCell& workcell,
                                            const rw::kinematics::State& state,
                                            RobotModelSpec& spec)
{
    spec.sceneFrames.clear ();

    std::set< const rw::kinematics::Frame* > deviceFrames;
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        if (dev->getBase () != NULL)
            collectSubtreeFrames (dev->getBase (), state, deviceFrames);
    }

    bool foundRobotBase = false;
    const std::vector< rw::kinematics::Frame* > frames = workcell.getFrames ();
    for (const rw::kinematics::Frame* frame : frames) {
        if (frame == NULL || deviceFrames.find (frame) != deviceFrames.end ())
            continue;
        if (frame == workcell.getWorldFrame ())
            continue;

        FrameSpec out;
        out.name = frame->getName ();
        const rw::kinematics::Frame* parent = frame->getParent (state);
        out.refFrame = parent != NULL ? parent->getName () : "WORLD";
        out.frameType =
            dynamic_cast< const rw::kinematics::MovableFrame* > (frame) != NULL
                ? SceneFrameType::Movable
                : SceneFrameType::Fixed;
        out.daf = isDAF (frame, state);
        out.poseMode = PoseMode::RPYPos;
        transformToRpyPos (frame->getTransform (state), out.rpyDeg, out.pos);

        if (hasShowFrameAxes (*frame))
            spec.showFrameAxes = true;

        if (out.name == "RobotBase" || out.name == "robotBase") {
            spec.robotBaseFrame = out;
            foundRobotBase = true;
        }
        else {
            spec.sceneFrames.push_back (out);
        }
    }

    if (!foundRobotBase) {
        spec.robotBaseFrame.name = "RobotBase";
        spec.robotBaseFrame.refFrame = "WORLD";
        spec.robotBaseFrame.frameType = SceneFrameType::Fixed;
        spec.robotBaseFrame.poseMode = PoseMode::RPYPos;
    }
}

void WorkCellConverter::extractDrawables (const rw::models::WorkCell& workcell,
                                          RobotModelSpec& spec,
                                          QStringList& warnings)
{
    spec.drawables.clear ();
    spec.sceneGeometries.clear ();

    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    const std::vector< rw::core::Ptr< rw::models::Object > > objects = workcell.getObjects ();
    for (const rw::core::Ptr< rw::models::Object >& obj : objects) {
        if (obj == NULL)
            continue;
        const rw::kinematics::Frame* base = NULL;
#ifdef RW_USE_PTR
        base = obj->getBase ().get ();
#else
        base = obj->getBase ();
#endif
        if (base == NULL)
            continue;

        const std::string refFrame = base->getName ();
        if (isDeviceFrameName (devices, refFrame)) {
            DrawableSpec drawable;
            drawable.name = obj->getName ();
            drawable.refFrame = refFrame;
            drawable.shape = "Box";
            try {
                drawable.collisionModel = !obj->getGeometry ().empty ();
            }
            catch (...) {
                warnings << QString ("Could not inspect geometry for %1.")
                                .arg (qstr (drawable.name));
            }
            spec.drawables.push_back (drawable);
        }
        else {
            SceneGeometrySpec geometry;
            geometry.name = obj->getName ();
            geometry.refFrame = refFrame;
            geometry.kind = GeometryKind::Box;
            geometry.collisionModel = true;
            spec.sceneGeometries.push_back (geometry);
        }
    }
}

void WorkCellConverter::extractCollisionSetup (const rw::models::WorkCell& workcell,
                                               RobotModelSpec& spec)
{
    const rw::proximity::CollisionSetup setup =
        rw::proximity::CollisionSetup::get (workcell);
    spec.collisionSetup.enabled = true;
    spec.collisionSetup.excludeStaticPairs = setup.excludeStaticPairs ();
    spec.collisionSetup.excludePairs.clear ();
    for (const rw::core::StringPair& pair : setup.getExcludeList ()) {
        FramePairSpec out;
        out.first = pair.first;
        out.second = pair.second;
        addFramePairOnce (spec.collisionSetup.excludePairs, out);
    }
}

void WorkCellConverter::extractProximitySetup (const rw::models::WorkCell& workcell,
                                               RobotModelSpec& spec)
{
    const rw::proximity::ProximitySetup setup =
        rw::proximity::ProximitySetup::get (workcell);
    spec.proximitySetup.enabled =
        setup.getLoadedFromFile () || !setup.getProximitySetupRules ().empty ();
    spec.proximitySetup.useIncludeAll = setup.useIncludeAll ();
    spec.proximitySetup.useExcludeStaticPairs = setup.useExcludeStaticPairs ();
    spec.proximitySetup.rules.clear ();
    for (const rw::proximity::ProximitySetupRule& rule : setup.getProximitySetupRules ()) {
        const std::pair< std::string, std::string > patterns = rule.getPatterns ();
        ProximityRuleSpec out;
        out.kind = rule.type () == rw::proximity::ProximitySetupRule::INCLUDE_RULE
                       ? ProximityRuleKind::Include
                       : ProximityRuleKind::Exclude;
        out.patternA = patterns.first;
        out.patternB = patterns.second;
        spec.proximitySetup.rules.push_back (out);
    }
}

bool WorkCellConverter::tryLoadSidecar (const rw::models::WorkCell& workcell,
                                        const std::string& saveDirectory,
                                        RobotModelSpec& spec,
                                        QStringList& warnings)
{
    QStringList candidates;
    const QString saveDir = qstr (saveDirectory);

    if (!spec.robotName.empty ()) {
        candidates << QDir (saveDir).filePath (
            RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".rmb.json");
    }

    const QString wcName = withoutSceneSuffix (qstr (workcell.getName ()));
    if (!wcName.isEmpty ()) {
        candidates << QDir (saveDir).filePath (
            RobotModelXmlWriter::sanitizeFileBaseName (wcName) + ".rmb.json");
    }

    const QString wcFile = qstr (inferWorkCellFilePath (workcell));
    if (!wcFile.isEmpty ()) {
        const QFileInfo info (wcFile);
        candidates << QDir (info.absolutePath ()).filePath (
            withoutSceneSuffix (info.baseName ()) + ".rmb.json");
    }

    candidates.removeDuplicates ();
    for (const QString& candidate : candidates) {
        QFile file (candidate);
        if (!file.exists ())
            continue;
        if (!file.open (QFile::ReadOnly | QFile::Text)) {
            warnings << QString ("Could not read RobotModelBuilder sidecar %1.")
                            .arg (candidate);
            continue;
        }
        const std::string json = QString::fromUtf8 (file.readAll ()).toStdString ();
        std::string error;
        RobotModelSpec loaded;
        if (!RobotModelSpecJson::fromJson (json, loaded, &error)) {
            warnings << QString ("Could not parse RobotModelBuilder sidecar %1: %2")
                            .arg (candidate, qstr (error));
            continue;
        }
        spec = loaded;
        return true;
    }

    return false;
}

void WorkCellConverter::mergeCompanionXmlMetadata (const rw::models::WorkCell& workcell,
                                                   RobotModelSpec& spec,
                                                   QStringList& warnings)
{
    const QString wcFile = qstr (inferWorkCellFilePath (workcell));
    if (wcFile.isEmpty () || !QFileInfo::exists (wcFile))
        return;

    QFile file (wcFile);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read WorkCell XML %1.").arg (wcFile);
        return;
    }

    spec.includes.clear ();
    const QString primaryDevice =
        RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".wc.xml";
    QString collisionFile;
    QString proximityFile;

    QXmlStreamReader xml (&file);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;

        if (xml.name () == QLatin1String ("Include")) {
            const QString include = xml.attributes ().value ("file").toString ().trimmed ();
            if (include.isEmpty ())
                continue;
            if (QFileInfo (include).fileName ().compare (primaryDevice, Qt::CaseInsensitive) == 0)
                continue;
            IncludeSpec out;
            out.file = include.toStdString ();
            out.kind = IncludeKind::WorkCell;
            spec.includes.push_back (out);
        }
        else if (xml.name () == QLatin1String ("CollisionSetup")) {
            collisionFile = xml.attributes ().value ("file").toString ().trimmed ();
            if (!collisionFile.isEmpty ()) {
                spec.collisionSetup.enabled = true;
                spec.collisionSetup.file = collisionFile.toStdString ();
            }
        }
        else if (xml.name () == QLatin1String ("ProximitySetup")) {
            proximityFile = xml.attributes ().value ("file").toString ().trimmed ();
            if (!proximityFile.isEmpty ()) {
                spec.proximitySetup.enabled = true;
                spec.proximitySetup.file = proximityFile.toStdString ();
            }
        }
    }
    if (xml.hasError ())
        warnings << QString ("Could not fully parse WorkCell XML %1: %2")
                        .arg (wcFile, xml.errorString ());

    if (!collisionFile.isEmpty ())
        mergeCollisionSetupXml (resolveRelativeTo (wcFile, collisionFile), spec, warnings);
    if (!proximityFile.isEmpty ())
        mergeProximitySetupXml (resolveRelativeTo (wcFile, proximityFile), spec, warnings);

    const QString dwcFile = resolveRelativeToDirectory (
        qstr (spec.saveDirectory),
        RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".dwc.xml");
    if (QFileInfo::exists (dwcFile))
        mergeDynamicWorkCellXml (dwcFile, spec, warnings);
}

void WorkCellConverter::mergeCollisionSetupXml (const QString& file,
                                                RobotModelSpec& spec,
                                                QStringList& warnings)
{
    if (file.isEmpty () || !QFileInfo::exists (file))
        return;

    QFile input (file);
    if (!input.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read CollisionSetup XML %1.").arg (file);
        return;
    }

    spec.collisionSetup.excludePairs.clear ();
    spec.collisionSetup.volatileFrames.clear ();
    QXmlStreamReader xml (&input);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;
        if (xml.name () == QLatin1String ("FramePair")) {
            FramePairSpec pair;
            pair.first = xml.attributes ().value ("first").toString ().toStdString ();
            pair.second = xml.attributes ().value ("second").toString ().toStdString ();
            addFramePairOnce (spec.collisionSetup.excludePairs, pair);
        }
        else if (xml.name () == QLatin1String ("Volatile")) {
            const std::string frame = xml.readElementText ().trimmed ().toStdString ();
            if (!frame.empty () && !containsString (spec.collisionSetup.volatileFrames, frame))
                spec.collisionSetup.volatileFrames.push_back (frame);
        }
        else if (xml.name () == QLatin1String ("ExcludeStaticPairs")) {
            spec.collisionSetup.excludeStaticPairs = true;
        }
    }
    if (xml.hasError ())
        warnings << QString ("Could not fully parse CollisionSetup XML %1: %2")
                        .arg (file, xml.errorString ());
}

void WorkCellConverter::mergeProximitySetupXml (const QString& file,
                                                RobotModelSpec& spec,
                                                QStringList& warnings)
{
    if (file.isEmpty () || !QFileInfo::exists (file))
        return;

    QFile input (file);
    if (!input.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read ProximitySetup XML %1.").arg (file);
        return;
    }

    spec.proximitySetup.rules.clear ();
    QXmlStreamReader xml (&input);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;
        if (xml.name () == QLatin1String ("ProximitySetup")) {
            if (xml.attributes ().hasAttribute ("UseIncludeAll"))
                spec.proximitySetup.useIncludeAll =
                    xml.attributes ().value ("UseIncludeAll").toString ().compare (
                        "true", Qt::CaseInsensitive) == 0;
            if (xml.attributes ().hasAttribute ("UseExcludeStaticPairs"))
                spec.proximitySetup.useExcludeStaticPairs =
                    xml.attributes ().value ("UseExcludeStaticPairs").toString ().compare (
                        "true", Qt::CaseInsensitive) == 0;
        }
        else if (xml.name () == QLatin1String ("Include") ||
                 xml.name () == QLatin1String ("Exclude")) {
            ProximityRuleSpec rule;
            rule.kind = xml.name () == QLatin1String ("Include") ? ProximityRuleKind::Include
                                                                  : ProximityRuleKind::Exclude;
            rule.patternA = xml.attributes ().value ("PatternA").toString ().toStdString ();
            rule.patternB = xml.attributes ().value ("PatternB").toString ().toStdString ();
            if (!rule.patternA.empty () && !rule.patternB.empty ())
                spec.proximitySetup.rules.push_back (rule);
        }
    }
    if (xml.hasError ())
        warnings << QString ("Could not fully parse ProximitySetup XML %1: %2")
                        .arg (file, xml.errorString ());
}

void WorkCellConverter::mergeDynamicWorkCellXml (const QString& file,
                                                 RobotModelSpec& spec,
                                                 QStringList& warnings)
{
    QFile input (file);
    if (!input.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read DynamicWorkCell XML %1.").arg (file);
        return;
    }

    spec.dynamics.generateDynamicWorkCell = true;
    spec.dynamics.links.clear ();
    spec.dynamics.forceLimits.clear ();

    int currentLink = -1;
    bool inKinematicBase = false;
    QXmlStreamReader xml (&input);
    while (!xml.atEnd ()) {
        xml.readNext ();
        if (xml.isStartElement ()) {
            const auto name = xml.name ();
            if (name == QLatin1String ("KinematicBase")) {
                inKinematicBase = true;
                const QString frame = xml.attributes ().value ("frame").toString ();
                if (!frame.isEmpty ())
                    spec.dynamics.baseFrame = frame.toStdString ();
            }
            else if (name == QLatin1String ("ForceLimit")) {
                JointForceLimitSpec force;
                force.jointName = xml.attributes ().value ("joint").toString ().toStdString ();
                force.maxForce = xml.readElementText ().trimmed ().toDouble ();
                spec.dynamics.forceLimits.push_back (force);
            }
            else if (name == QLatin1String ("Link")) {
                LinkDynamicsSpec link;
                link.linkName = QString ("Link%1").arg (spec.dynamics.links.size () + 1).toStdString ();
                link.objectName = xml.attributes ().value ("object").toString ().toStdString ();
                link.mass = 1.0;
                link.cog = {{0, 0, 0}};
                link.inertia = {{1, 1, 1, 0, 0, 0}};
                link.estimateInertia = true;
                link.material = spec.dynamics.baseMaterial;
                spec.dynamics.links.push_back (link);
                currentLink = static_cast< int > (spec.dynamics.links.size ()) - 1;
            }
            else if (name == QLatin1String ("Mass") && currentLink >= 0) {
                spec.dynamics.links[currentLink].mass =
                    xml.readElementText ().trimmed ().toDouble ();
            }
            else if (name == QLatin1String ("COG") && currentLink >= 0) {
                const std::vector< double > values = parseDoubles (xml.readElementText ());
                if (values.size () >= 3)
                    spec.dynamics.links[currentLink].cog = {{values[0], values[1], values[2]}};
            }
            else if (name == QLatin1String ("EstimateInertia") && currentLink >= 0) {
                spec.dynamics.links[currentLink].estimateInertia = true;
            }
            else if (name == QLatin1String ("Inertia") && currentLink >= 0) {
                const std::vector< double > values = parseDoubles (xml.readElementText ());
                if (values.size () >= 9) {
                    spec.dynamics.links[currentLink].inertia =
                        {{values[0], values[4], values[8], values[1], values[2], values[5]}};
                    spec.dynamics.links[currentLink].estimateInertia = false;
                }
                else if (values.size () >= 6) {
                    spec.dynamics.links[currentLink].inertia =
                        {{values[0], values[1], values[2], values[3], values[4], values[5]}};
                    spec.dynamics.links[currentLink].estimateInertia = false;
                }
            }
            else if (name == QLatin1String ("MaterialID")) {
                const std::string material = xml.readElementText ().trimmed ().toStdString ();
                if (currentLink >= 0)
                    spec.dynamics.links[currentLink].material = material;
                else if (inKinematicBase)
                    spec.dynamics.baseMaterial = material;
            }
        }
        else if (xml.isEndElement ()) {
            if (xml.name () == QLatin1String ("KinematicBase"))
                inKinematicBase = false;
            else if (xml.name () == QLatin1String ("Link"))
                currentLink = -1;
        }
    }
    if (xml.hasError ())
        warnings << QString ("Could not fully parse DynamicWorkCell XML %1: %2")
                        .arg (file, xml.errorString ());
}

void WorkCellConverter::transformToRpyPos (const rw::math::Transform3Dd& t,
                                           std::array< double, 3 >& rpyDeg,
                                           std::array< double, 3 >& pos)
{
    const rw::math::Vector3D<>& p = t.P ();
    pos[0] = p[0];
    pos[1] = p[1];
    pos[2] = p[2];

    const rw::math::RPY<> rpy (t.R ());
    rpyDeg[0] = rpy (0) * rw::math::Rad2Deg;
    rpyDeg[1] = rpy (1) * rw::math::Rad2Deg;
    rpyDeg[2] = rpy (2) * rw::math::Rad2Deg;
}

bool WorkCellConverter::hasShowFrameAxes (const rw::kinematics::Frame& frame)
{
    const rw::core::PropertyMap& map = frame.getPropertyMap ();
    const bool* showAxes = map.getPtr< bool > ("ShowFrameAxis");
    if (showAxes != NULL && *showAxes)
        return true;
    const std::string* showString = map.getPtr< std::string > ("ShowFrameAxis");
    return showString != NULL &&
           (QString::fromStdString (*showString).compare ("true", Qt::CaseInsensitive) == 0);
}

bool WorkCellConverter::isDAF (const rw::kinematics::Frame* frame,
                               const rw::kinematics::State& state)
{
    return frame != NULL && frame->getDafParent (state) != NULL;
}
