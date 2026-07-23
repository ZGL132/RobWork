#include "RobotModelSpecJson.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <limits>

namespace rws {

namespace {

// -----------------------------------------------------------------------
// readFixedArray -- template helper for fixed-length double arrays
// -----------------------------------------------------------------------
template <std::size_t N>
static bool readFixedArray(const QJsonObject& obj, const char* key,
                            std::array<double, N>& out, std::string* error)
{
    const QJsonArray arr = obj.value(key).toArray();
    if (arr.size() != static_cast<int>(N)) {
        if (error) *error = std::string(key) + " must contain " + std::to_string(N) + " values";
        return false;
    }
    for (std::size_t i = 0; i < N; ++i) {
        const double v = arr.at(static_cast<int>(i)).toDouble();
        if (!std::isfinite(v)) {
            if (error) *error = std::string(key) + " contains non-finite value";
            return false;
        }
        out[i] = v;
    }
    return true;
}

// -----------------------------------------------------------------------
// writeFixedArray -- write fixed-length double array to QJsonArray
// -----------------------------------------------------------------------
template <std::size_t N>
static QJsonArray writeFixedArray(const std::array<double, N>& arr)
{
    QJsonArray result;
    for (std::size_t i = 0; i < N; ++i)
        result.append(arr[i]);
    return result;
}

// =======================================================================
//  Enum helpers -- write (enum -> string)
// =======================================================================

static const char* kinematicsViewModeToString(KinematicsViewMode mode)
{
    switch (mode) {
        case KinematicsViewMode::JointRPYPos:  return "JointRPYPos";
        case KinematicsViewMode::DHProjection: return "DHProjection";
    }
    return "JointRPYPos";
}

static const char* jsonSceneFrameType(SceneFrameType type)
{
    switch (type) {
        case SceneFrameType::Normal:  return "Normal";
        case SceneFrameType::Fixed:   return "Fixed";
        case SceneFrameType::Movable: return "Movable";
    }
    return "Fixed";
}

static const char* jsonPoseMode(PoseMode mode)
{
    switch (mode) {
        case PoseMode::RPYPos:       return "RPYPos";
        case PoseMode::Transform4x4: return "Transform4x4";
    }
    return "RPYPos";
}

static const char* jsonGeometryKind(GeometryKind kind)
{
    switch (kind) {
        case GeometryKind::Box:      return "Box";
        case GeometryKind::Cylinder: return "Cylinder";
        case GeometryKind::Sphere:   return "Sphere";
        case GeometryKind::Cone:     return "Cone";
        case GeometryKind::Plane:    return "Plane";
        case GeometryKind::STL:      return "STL";
        case GeometryKind::Mesh:     return "Mesh";
        case GeometryKind::Polytope: return "Polytope";
        case GeometryKind::Unknown:  return "Unknown";
    }
    return "Box";
}

static const char* jsonIncludeKind(IncludeKind kind)
{
    switch (kind) {
        case IncludeKind::Device:    return "Device";
        case IncludeKind::WorkCell:  return "WorkCell";
        case IncludeKind::Collision: return "Collision";
        case IncludeKind::Proximity: return "Proximity";
    }
    return "Device";
}

static const char* jsonProximityRuleKind(ProximityRuleKind kind)
{
    switch (kind) {
        case ProximityRuleKind::Include: return "Include";
        case ProximityRuleKind::Exclude: return "Exclude";
    }
    return "Include";
}

// =======================================================================
//  Enum helpers -- read (string -> enum), return false on unknown value
// =======================================================================

static bool kinematicsViewModeFromString(const QString& s, KinematicsViewMode& out, std::string* error)
{
    if (s == QLatin1String("JointRPYPos"))  { out = KinematicsViewMode::JointRPYPos; return true; }
    if (s == QLatin1String("DHProjection")) { out = KinematicsViewMode::DHProjection; return true; }
    if (error) *error = "Unknown KinematicsViewMode value: " + s.toStdString();
    return false;
}

static bool sceneFrameTypeFromString(const QString& s, SceneFrameType& out, std::string* error)
{
    if (s == QLatin1String("Normal"))  { out = SceneFrameType::Normal; return true; }
    if (s == QLatin1String("Fixed"))   { out = SceneFrameType::Fixed; return true; }
    if (s == QLatin1String("Movable")) { out = SceneFrameType::Movable; return true; }
    if (error) *error = "Unknown SceneFrameType value: " + s.toStdString();
    return false;
}

static bool poseModeFromString(const QString& s, PoseMode& out, std::string* error)
{
    if (s == QLatin1String("RPYPos"))       { out = PoseMode::RPYPos; return true; }
    if (s == QLatin1String("Transform4x4")) { out = PoseMode::Transform4x4; return true; }
    if (error) *error = "Unknown PoseMode value: " + s.toStdString();
    return false;
}

static bool geometryKindFromString(const QString& s, GeometryKind& out, std::string* error)
{
    if (s == QLatin1String("Box"))      { out = GeometryKind::Box; return true; }
    if (s == QLatin1String("Cylinder")) { out = GeometryKind::Cylinder; return true; }
    if (s == QLatin1String("Sphere"))   { out = GeometryKind::Sphere; return true; }
    if (s == QLatin1String("Cone"))     { out = GeometryKind::Cone; return true; }
    if (s == QLatin1String("Plane"))    { out = GeometryKind::Plane; return true; }
    if (s == QLatin1String("STL"))      { out = GeometryKind::STL; return true; }
    if (s == QLatin1String("Mesh"))     { out = GeometryKind::Mesh; return true; }
    if (s == QLatin1String("Polytope")) { out = GeometryKind::Polytope; return true; }
    if (s == QLatin1String("Unknown"))  { out = GeometryKind::Unknown; return true; }
    if (error) *error = "Unknown GeometryKind value: " + s.toStdString();
    return false;
}

static bool includeKindFromString(const QString& s, IncludeKind& out, std::string* error)
{
    if (s == QLatin1String("Device"))    { out = IncludeKind::Device; return true; }
    if (s == QLatin1String("WorkCell"))  { out = IncludeKind::WorkCell; return true; }
    if (s == QLatin1String("Collision")) { out = IncludeKind::Collision; return true; }
    if (s == QLatin1String("Proximity")) { out = IncludeKind::Proximity; return true; }
    if (error) *error = "Unknown IncludeKind value: " + s.toStdString();
    return false;
}

static bool proximityRuleKindFromString(const QString& s, ProximityRuleKind& out, std::string* error)
{
    if (s == QLatin1String("Include")) { out = ProximityRuleKind::Include; return true; }
    if (s == QLatin1String("Exclude")) { out = ProximityRuleKind::Exclude; return true; }
    if (error) *error = "Unknown ProximityRuleKind value: " + s.toStdString();
    return false;
}

// =======================================================================
//  Writer helpers -- struct -> QJsonObject
// =======================================================================

static QJsonObject writeFrameSpec(const FrameSpec& f)
{
    QJsonObject obj;
    obj["name"]      = QString::fromStdString(f.name);
    obj["refFrame"]  = QString::fromStdString(f.refFrame);
    obj["frameType"] = jsonSceneFrameType(f.frameType);
    obj["daf"]       = f.daf;
    obj["poseMode"]  = jsonPoseMode(f.poseMode);
    obj["rpyDeg"]    = writeFixedArray(f.rpyDeg);
    obj["pos"]       = writeFixedArray(f.pos);
    obj["transform"] = writeFixedArray(f.transform);
    return obj;
}

static QJsonObject writeSceneGeometrySpec(const SceneGeometrySpec& g)
{
    QJsonObject obj;
    obj["name"]          = QString::fromStdString(g.name);
    obj["refFrame"]      = QString::fromStdString(g.refFrame);
    obj["kind"]          = jsonGeometryKind(g.kind);
    obj["size"]          = writeFixedArray(g.size);
    obj["radius"]        = g.radius;
    obj["length"]        = g.length;
    obj["file"]          = QString::fromStdString(g.file);
    obj["rpyDeg"]        = writeFixedArray(g.rpyDeg);
    obj["pos"]           = writeFixedArray(g.pos);
    obj["rgb"]           = writeFixedArray(g.rgb);
    obj["collisionModel"] = g.collisionModel;
    return obj;
}

static QJsonObject writeJointTransformSpec(const JointTransformSpec& j)
{
    QJsonObject obj;
    obj["name"]   = QString::fromStdString(j.name);
    obj["type"]   = QString::fromStdString(j.type);
    obj["rpyDeg"] = writeFixedArray(j.rpyDeg);
    obj["pos"]    = writeFixedArray(j.pos);
    return obj;
}

static QJsonObject writeDHJointSpec(const DHJointSpec& d)
{
    QJsonObject obj;
    obj["name"]      = QString::fromStdString(d.name);
    obj["alphaDeg"]  = d.alphaDeg;
    obj["a"]         = d.a;
    obj["d"]         = d.d;
    obj["offsetDeg"] = d.offsetDeg;
    return obj;
}

static QJsonObject writeDrawableSpec(const DrawableSpec& d)
{
    QJsonObject obj;
    obj["name"]             = QString::fromStdString(d.name);
    obj["refFrame"]         = QString::fromStdString(d.refFrame);
    obj["shape"]            = QString::fromStdString(d.shape);
    obj["filePath"]         = QString::fromStdString(d.filePath);
    obj["dimensions"]       = writeFixedArray(d.dimensions);
    obj["radius"]           = d.radius;
    obj["length"]           = d.length;
    obj["rpyDeg"]           = writeFixedArray(d.rpyDeg);
    obj["pos"]              = writeFixedArray(d.pos);
    obj["rgb"]              = writeFixedArray(d.rgb);
    obj["collisionModel"]   = d.collisionModel;
    obj["autoLinkGeometry"] = d.autoLinkGeometry;
    return obj;
}

static QJsonObject writeCollisionModelSpec(const CollisionModelSpec& c)
{
    QJsonObject obj;
    obj["name"]       = QString::fromStdString(c.name);
    obj["refFrame"]   = QString::fromStdString(c.refFrame);
    obj["shape"]      = QString::fromStdString(c.shape);
    obj["filePath"]   = QString::fromStdString(c.filePath);
    obj["dimensions"] = writeFixedArray(c.dimensions);
    obj["radius"]     = c.radius;
    obj["length"]     = c.length;
    obj["rpyDeg"]     = writeFixedArray(c.rpyDeg);
    obj["pos"]        = writeFixedArray(c.pos);
    return obj;
}

static QJsonObject writeJointLimitSpec(const JointLimitSpec& l)
{
    QJsonObject obj;
    obj["jointName"] = QString::fromStdString(l.jointName);
    obj["posMin"]    = l.posMin;
    obj["posMax"]    = l.posMax;
    obj["velMax"]    = l.velMax;
    obj["accMax"]    = l.accMax;
    return obj;
}

static QJsonObject writePoseSpec(const PoseSpec& p)
{
    QJsonObject obj;
    obj["name"] = QString::fromStdString(p.name);
    QJsonArray qArr;
    for (const double v : p.q)
        qArr.append(v);
    obj["q"] = qArr;
    return obj;
}

static QJsonObject writeLinkDynamicsSpec(const LinkDynamicsSpec& ld)
{
    QJsonObject obj;
    obj["name"]            = QString::fromStdString(ld.linkName);
    obj["objectName"]      = QString::fromStdString(ld.objectName);
    obj["mass"]            = ld.mass;
    obj["cog"]             = writeFixedArray(ld.cog);
    obj["inertia"]         = writeFixedArray(ld.inertia);
    obj["estimateInertia"] = ld.estimateInertia;
    obj["material"]        = QString::fromStdString(ld.material);
    return obj;
}

static QJsonObject writeJointForceLimitSpec(const JointForceLimitSpec& fl)
{
    QJsonObject obj;
    obj["jointName"] = QString::fromStdString(fl.jointName);
    obj["maxForce"]  = fl.maxForce;
    return obj;
}

static QJsonObject writeDynamicModelSpec(const DynamicModelSpec& dm)
{
    QJsonObject obj;
    obj["generateDynamicWorkCell"] = dm.generateDynamicWorkCell;
    obj["baseFrame"]               = QString::fromStdString(dm.baseFrame);
    obj["baseMaterial"]            = QString::fromStdString(dm.baseMaterial);

    QJsonArray linksArr;
    for (const auto& ld : dm.links)
        linksArr.append(writeLinkDynamicsSpec(ld));
    obj["links"] = linksArr;

    QJsonArray forceArr;
    for (const auto& fl : dm.forceLimits)
        forceArr.append(writeJointForceLimitSpec(fl));
    obj["forceLimits"] = forceArr;

    return obj;
}

static QJsonObject writeIncludeSpec(const IncludeSpec& inc)
{
    QJsonObject obj;
    obj["file"] = QString::fromStdString(inc.file);
    obj["kind"] = jsonIncludeKind(inc.kind);
    return obj;
}

static QJsonObject writeFramePairSpec(const FramePairSpec& fp)
{
    QJsonObject obj;
    obj["first"]  = QString::fromStdString(fp.first);
    obj["second"] = QString::fromStdString(fp.second);
    return obj;
}

static QJsonObject writeCollisionSetupSpec(const CollisionSetupSpec& cs)
{
    QJsonObject obj;
    obj["enabled"]                   = cs.enabled;
    obj["file"]                      = QString::fromStdString(cs.file);
    obj["excludeAdjacentLinkPairs"]  = cs.excludeAdjacentLinkPairs;
    obj["excludeStaticPairs"]        = cs.excludeStaticPairs;

    QJsonArray excludePairsArr;
    for (const auto& fp : cs.excludePairs)
        excludePairsArr.append(writeFramePairSpec(fp));
    obj["excludePairs"] = excludePairsArr;

    QJsonArray volatileArr;
    for (const auto& vf : cs.volatileFrames)
        volatileArr.append(QString::fromStdString(vf));
    obj["volatileFrames"] = volatileArr;

    return obj;
}

static QJsonObject writeProximityRuleSpec(const ProximityRuleSpec& pr)
{
    QJsonObject obj;
    obj["kind"]     = jsonProximityRuleKind(pr.kind);
    obj["patternA"] = QString::fromStdString(pr.patternA);
    obj["patternB"] = QString::fromStdString(pr.patternB);
    return obj;
}

static QJsonObject writeProximitySetupSpec(const ProximitySetupSpec& ps)
{
    QJsonObject obj;
    obj["enabled"]               = ps.enabled;
    obj["file"]                  = QString::fromStdString(ps.file);
    obj["useIncludeAll"]         = ps.useIncludeAll;
    obj["useExcludeStaticPairs"] = ps.useExcludeStaticPairs;

    QJsonArray rulesArr;
    for (const auto& pr : ps.rules)
        rulesArr.append(writeProximityRuleSpec(pr));
    obj["rules"] = rulesArr;

    return obj;
}

// =======================================================================
//  Reader helpers -- QJsonObject -> struct, return false on error
// =======================================================================

static bool readFrameSpec(const QJsonObject& obj, FrameSpec& f, std::string* error)
{
    f.name     = obj.value("name").toString().toStdString();
    f.refFrame = obj.value("refFrame").toString().toStdString();

    const QString ft = obj.value("frameType").toString();
    if (!ft.isEmpty()) {
        if (!sceneFrameTypeFromString(ft, f.frameType, error))
            return false;
    }

    f.daf = obj.value("daf").toBool(false);

    const QString pm = obj.value("poseMode").toString();
    if (!pm.isEmpty()) {
        if (!poseModeFromString(pm, f.poseMode, error))
            return false;
    }

    if (!readFixedArray(obj, "rpyDeg",    f.rpyDeg,    error)) return false;
    if (!readFixedArray(obj, "pos",       f.pos,       error)) return false;
    if (!readFixedArray(obj, "transform", f.transform, error)) return false;

    return true;
}

static bool readSceneGeometrySpec(const QJsonObject& obj, SceneGeometrySpec& g, std::string* error)
{
    g.name     = obj.value("name").toString().toStdString();
    g.refFrame = obj.value("refFrame").toString().toStdString();

    const QString k = obj.value("kind").toString();
    if (!k.isEmpty()) {
        if (!geometryKindFromString(k, g.kind, error))
            return false;
    }

    if (!readFixedArray(obj, "size",   g.size,   error)) return false;

    g.radius = obj.value("radius").toDouble(0.05);
    if (!std::isfinite(g.radius)) { if (error) *error = "SceneGeometrySpec.radius contains non-finite value"; return false; }

    g.length = obj.value("length").toDouble(0.1);
    if (!std::isfinite(g.length)) { if (error) *error = "SceneGeometrySpec.length contains non-finite value"; return false; }

    g.file = obj.value("file").toString().toStdString();

    if (!readFixedArray(obj, "rpyDeg", g.rpyDeg, error)) return false;
    if (!readFixedArray(obj, "pos",    g.pos,    error)) return false;
    if (!readFixedArray(obj, "rgb",    g.rgb,    error)) return false;

    g.collisionModel = obj.value("collisionModel").toBool(true);

    return true;
}

static bool readJointTransformSpec(const QJsonObject& obj, JointTransformSpec& j, std::string* error)
{
    j.name = obj.value("name").toString().toStdString();
    j.type = obj.value("type").toString().toStdString();

    if (!readFixedArray(obj, "rpyDeg", j.rpyDeg, error)) return false;
    if (!readFixedArray(obj, "pos",    j.pos,    error)) return false;

    return true;
}

static bool readDHJointSpec(const QJsonObject& obj, DHJointSpec& d, std::string* error)
{
    d.name      = obj.value("name").toString().toStdString();
    d.alphaDeg  = obj.value("alphaDeg").toDouble(0.0);
    if (!std::isfinite(d.alphaDeg))  { if (error) *error = "DHJointSpec.alphaDeg contains non-finite value"; return false; }
    d.a         = obj.value("a").toDouble(0.0);
    if (!std::isfinite(d.a))         { if (error) *error = "DHJointSpec.a contains non-finite value"; return false; }
    d.d         = obj.value("d").toDouble(0.0);
    if (!std::isfinite(d.d))         { if (error) *error = "DHJointSpec.d contains non-finite value"; return false; }
    d.offsetDeg = obj.value("offsetDeg").toDouble(0.0);
    if (!std::isfinite(d.offsetDeg)) { if (error) *error = "DHJointSpec.offsetDeg contains non-finite value"; return false; }

    return true;
}

static bool readDrawableSpec(const QJsonObject& obj, DrawableSpec& d, std::string* error)
{
    d.name     = obj.value("name").toString().toStdString();
    d.refFrame = obj.value("refFrame").toString().toStdString();
    d.shape    = obj.value("shape").toString("Box").toStdString();
    d.filePath = obj.value("filePath").toString().toStdString();

    if (!readFixedArray(obj, "dimensions", d.dimensions, error)) return false;

    d.radius = obj.value("radius").toDouble(0.05);
    if (!std::isfinite(d.radius)) { if (error) *error = "DrawableSpec.radius contains non-finite value"; return false; }

    d.length = obj.value("length").toDouble(0.1);
    if (!std::isfinite(d.length)) { if (error) *error = "DrawableSpec.length contains non-finite value"; return false; }

    if (!readFixedArray(obj, "rpyDeg", d.rpyDeg, error)) return false;
    if (!readFixedArray(obj, "pos",    d.pos,    error)) return false;
    if (!readFixedArray(obj, "rgb",    d.rgb,    error)) return false;

    d.collisionModel   = obj.value("collisionModel").toBool(false);
    d.autoLinkGeometry = obj.value("autoLinkGeometry").toBool(false);

    return true;
}

static bool readCollisionModelSpec(const QJsonObject& obj, CollisionModelSpec& c, std::string* error)
{
    c.name     = obj.value("name").toString().toStdString();
    c.refFrame = obj.value("refFrame").toString().toStdString();
    c.shape    = obj.value("shape").toString("Box").toStdString();
    c.filePath = obj.value("filePath").toString().toStdString();

    if (!readFixedArray(obj, "dimensions", c.dimensions, error)) return false;

    c.radius = obj.value("radius").toDouble(0.05);
    if (!std::isfinite(c.radius)) { if (error) *error = "CollisionModelSpec.radius contains non-finite value"; return false; }

    c.length = obj.value("length").toDouble(0.1);
    if (!std::isfinite(c.length)) { if (error) *error = "CollisionModelSpec.length contains non-finite value"; return false; }

    if (!readFixedArray(obj, "rpyDeg", c.rpyDeg, error)) return false;
    if (!readFixedArray(obj, "pos",    c.pos,    error)) return false;

    return true;
}

static bool readJointLimitSpec(const QJsonObject& obj, JointLimitSpec& l, std::string* error)
{
    l.jointName = obj.value("jointName").toString().toStdString();
    l.posMin    = obj.value("posMin").toDouble(0.0);
    if (!std::isfinite(l.posMin)) { if (error) *error = "JointLimitSpec.posMin contains non-finite value"; return false; }
    l.posMax    = obj.value("posMax").toDouble(0.0);
    if (!std::isfinite(l.posMax)) { if (error) *error = "JointLimitSpec.posMax contains non-finite value"; return false; }
    l.velMax    = obj.value("velMax").toDouble(0.0);
    if (!std::isfinite(l.velMax)) { if (error) *error = "JointLimitSpec.velMax contains non-finite value"; return false; }
    l.accMax    = obj.value("accMax").toDouble(0.0);
    if (!std::isfinite(l.accMax)) { if (error) *error = "JointLimitSpec.accMax contains non-finite value"; return false; }

    return true;
}

static bool readPoseSpec(const QJsonObject& obj, PoseSpec& p, std::string* error)
{
    p.name = obj.value("name").toString().toStdString();

    p.q.clear();
    const QJsonArray qArr = obj.value("q").toArray();
    for (int i = 0; i < qArr.size(); ++i) {
        const double v = qArr.at(i).toDouble();
        if (!std::isfinite(v)) {
            if (error) *error = "PoseSpec.q contains non-finite value at index " + std::to_string(i);
            return false;
        }
        p.q.push_back(v);
    }

    return true;
}

static bool readLinkDynamicsSpec(const QJsonObject& obj, LinkDynamicsSpec& ld, std::string* error)
{
    ld.linkName   = obj.value("name").toString().toStdString();
    ld.objectName = obj.value("objectName").toString().toStdString();

    ld.mass = obj.value("mass").toDouble(0.0);
    if (!std::isfinite(ld.mass)) { if (error) *error = "LinkDynamicsSpec.mass contains non-finite value"; return false; }

    if (!readFixedArray(obj, "cog",     ld.cog,     error)) return false;
    if (!readFixedArray(obj, "inertia", ld.inertia, error)) return false;

    ld.estimateInertia = obj.value("estimateInertia").toBool(false);
    ld.material        = obj.value("material").toString().toStdString();

    return true;
}

static bool readJointForceLimitSpec(const QJsonObject& obj, JointForceLimitSpec& fl, std::string* error)
{
    fl.jointName = obj.value("jointName").toString().toStdString();
    fl.maxForce  = obj.value("maxForce").toDouble(0.0);
    if (!std::isfinite(fl.maxForce)) { if (error) *error = "JointForceLimitSpec.maxForce contains non-finite value"; return false; }

    return true;
}

static bool readDynamicModelSpec(const QJsonObject& obj, DynamicModelSpec& dm, std::string* error)
{
    dm.generateDynamicWorkCell = obj.value("generateDynamicWorkCell").toBool(false);
    dm.baseFrame               = obj.value("baseFrame").toString("Base").toStdString();
    dm.baseMaterial            = obj.value("baseMaterial").toString("Steel").toStdString();

    dm.links.clear();
    const QJsonArray linksArr = obj.value("links").toArray();
    for (int i = 0; i < linksArr.size(); ++i) {
        LinkDynamicsSpec ld;
        if (!readLinkDynamicsSpec(linksArr.at(i).toObject(), ld, error))
            return false;
        dm.links.push_back(std::move(ld));
    }

    dm.forceLimits.clear();
    const QJsonArray forceArr = obj.value("forceLimits").toArray();
    for (int i = 0; i < forceArr.size(); ++i) {
        JointForceLimitSpec fl;
        if (!readJointForceLimitSpec(forceArr.at(i).toObject(), fl, error))
            return false;
        dm.forceLimits.push_back(std::move(fl));
    }

    return true;
}

static bool readIncludeSpec(const QJsonObject& obj, IncludeSpec& inc, std::string* error)
{
    inc.file = obj.value("file").toString().toStdString();

    const QString k = obj.value("kind").toString();
    if (!k.isEmpty()) {
        if (!includeKindFromString(k, inc.kind, error))
            return false;
    }

    return true;
}

static bool readFramePairSpec(const QJsonObject& obj, FramePairSpec& fp, std::string* error)
{
    Q_UNUSED(error);
    fp.first  = obj.value("first").toString().toStdString();
    fp.second = obj.value("second").toString().toStdString();
    return true;
}

static bool readCollisionSetupSpec(const QJsonObject& obj, CollisionSetupSpec& cs, std::string* error)
{
    cs.enabled                  = obj.value("enabled").toBool(true);
    cs.file                     = obj.value("file").toString("CollisionSetup.xml").toStdString();
    cs.excludeAdjacentLinkPairs = obj.value("excludeAdjacentLinkPairs").toBool(true);
    cs.excludeStaticPairs       = obj.value("excludeStaticPairs").toBool(false);

    cs.excludePairs.clear();
    const QJsonArray epArr = obj.value("excludePairs").toArray();
    for (int i = 0; i < epArr.size(); ++i) {
        FramePairSpec fp;
        if (!readFramePairSpec(epArr.at(i).toObject(), fp, error))
            return false;
        cs.excludePairs.push_back(std::move(fp));
    }

    cs.volatileFrames.clear();
    const QJsonArray vfArr = obj.value("volatileFrames").toArray();
    for (int i = 0; i < vfArr.size(); ++i)
        cs.volatileFrames.push_back(vfArr.at(i).toString().toStdString());

    return true;
}

static bool readProximityRuleSpec(const QJsonObject& obj, ProximityRuleSpec& pr, std::string* error)
{
    const QString k = obj.value("kind").toString();
    if (!k.isEmpty()) {
        if (!proximityRuleKindFromString(k, pr.kind, error))
            return false;
    }

    pr.patternA = obj.value("patternA").toString().toStdString();
    pr.patternB = obj.value("patternB").toString().toStdString();

    return true;
}

static bool readProximitySetupSpec(const QJsonObject& obj, ProximitySetupSpec& ps, std::string* error)
{
    ps.enabled               = obj.value("enabled").toBool(false);
    ps.file                  = obj.value("file").toString("ProximitySetup.xml").toStdString();
    ps.useIncludeAll         = obj.value("useIncludeAll").toBool(true);
    ps.useExcludeStaticPairs = obj.value("useExcludeStaticPairs").toBool(false);

    ps.rules.clear();
    const QJsonArray rulesArr = obj.value("rules").toArray();
    for (int i = 0; i < rulesArr.size(); ++i) {
        ProximityRuleSpec pr;
        if (!readProximityRuleSpec(rulesArr.at(i).toObject(), pr, error))
            return false;
        ps.rules.push_back(std::move(pr));
    }

    return true;
}

} // anonymous namespace

// =======================================================================
//  RobotModelSpecJson public API
// =======================================================================

QJsonObject RobotModelSpecJson::toObject(const RobotModelSpec& spec)
{
    QJsonObject obj;

    obj["robotName"]             = QString::fromStdString(spec.robotName);
    obj["saveDirectory"]         = QString::fromStdString(spec.saveDirectory);
    obj["mode"]                  = kinematicsViewModeToString(spec.mode);
    obj["exportDhJointsAdvanced"] = spec.exportDhJointsAdvanced;
    obj["showFrameAxes"]         = spec.showFrameAxes;
    obj["generateDrawables"]     = spec.generateDrawables;
    obj["generateScene"]         = spec.generateScene;

    obj["robotBaseFrame"] = writeFrameSpec(spec.robotBaseFrame);

    // Arrays
    {
        QJsonArray arr;
        for (const auto& f : spec.sceneFrames)
            arr.append(writeFrameSpec(f));
        obj["sceneFrames"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& g : spec.sceneGeometries)
            arr.append(writeSceneGeometrySpec(g));
        obj["sceneGeometries"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& j : spec.transformJoints)
            arr.append(writeJointTransformSpec(j));
        obj["transformJoints"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& d : spec.dhJoints)
            arr.append(writeDHJointSpec(d));
        obj["dhJoints"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& d : spec.drawables)
            arr.append(writeDrawableSpec(d));
        obj["drawables"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& c : spec.collisionModels)
            arr.append(writeCollisionModelSpec(c));
        obj["collisionModels"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& l : spec.limits)
            arr.append(writeJointLimitSpec(l));
        obj["limits"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& p : spec.poses)
            arr.append(writePoseSpec(p));
        obj["poses"] = arr;
    }
    {
        QJsonArray arr;
        for (const auto& inc : spec.includes)
            arr.append(writeIncludeSpec(inc));
        obj["includes"] = arr;
    }

    obj["dynamics"]       = writeDynamicModelSpec(spec.dynamics);
    obj["collisionSetup"] = writeCollisionSetupSpec(spec.collisionSetup);
    obj["proximitySetup"] = writeProximitySetupSpec(spec.proximitySetup);

    return obj;
}

std::string RobotModelSpecJson::toJson(const RobotModelSpec& spec)
{
    QJsonObject root;
    root["schemaVersion"] = SchemaVersion;
    root["type"]          = QStringLiteral("RobotModelSpec");
    root["data"]          = toObject(spec);

    const QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Compact).toStdString();
}

bool RobotModelSpecJson::fromObject(const QJsonObject& dataObject,
                                     RobotModelSpec& spec,
                                     std::string* error)
{
    spec.robotName     = dataObject.value("robotName").toString().toStdString();
    spec.saveDirectory = dataObject.value("saveDirectory").toString().toStdString();

    // mode (enum)
    {
        const QString modeStr = dataObject.value("mode").toString();
        if (!modeStr.isEmpty()) {
            if (!kinematicsViewModeFromString(modeStr, spec.mode, error))
                return false;
        }
    }

    spec.exportDhJointsAdvanced = dataObject.value("exportDhJointsAdvanced").toBool(false);
    spec.showFrameAxes          = dataObject.value("showFrameAxes").toBool(false);
    spec.generateDrawables      = dataObject.value("generateDrawables").toBool(false);
    spec.generateScene          = dataObject.value("generateScene").toBool(false);

    // robotBaseFrame (object)
    {
        const QJsonObject bfObj = dataObject.value("robotBaseFrame").toObject();
        if (!bfObj.isEmpty()) {
            if (!readFrameSpec(bfObj, spec.robotBaseFrame, error))
                return false;
        }
    }

    // sceneFrames (array)
    {
        spec.sceneFrames.clear();
        const QJsonArray arr = dataObject.value("sceneFrames").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            FrameSpec f;
            if (!readFrameSpec(arr.at(i).toObject(), f, error))
                return false;
            spec.sceneFrames.push_back(std::move(f));
        }
    }

    // sceneGeometries (array)
    {
        spec.sceneGeometries.clear();
        const QJsonArray arr = dataObject.value("sceneGeometries").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            SceneGeometrySpec g;
            if (!readSceneGeometrySpec(arr.at(i).toObject(), g, error))
                return false;
            spec.sceneGeometries.push_back(std::move(g));
        }
    }

    // transformJoints (array)
    {
        spec.transformJoints.clear();
        const QJsonArray arr = dataObject.value("transformJoints").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            JointTransformSpec j;
            if (!readJointTransformSpec(arr.at(i).toObject(), j, error))
                return false;
            spec.transformJoints.push_back(std::move(j));
        }
    }

    // dhJoints (array)
    {
        spec.dhJoints.clear();
        const QJsonArray arr = dataObject.value("dhJoints").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            DHJointSpec d;
            if (!readDHJointSpec(arr.at(i).toObject(), d, error))
                return false;
            spec.dhJoints.push_back(std::move(d));
        }
    }

    // drawables (array)
    {
        spec.drawables.clear();
        const QJsonArray arr = dataObject.value("drawables").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            DrawableSpec d;
            if (!readDrawableSpec(arr.at(i).toObject(), d, error))
                return false;
            spec.drawables.push_back(std::move(d));
        }
    }

    // collisionModels (array)
    {
        spec.collisionModels.clear();
        const QJsonArray arr = dataObject.value("collisionModels").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            CollisionModelSpec c;
            if (!readCollisionModelSpec(arr.at(i).toObject(), c, error))
                return false;
            spec.collisionModels.push_back(std::move(c));
        }
    }

    // limits (array)
    {
        spec.limits.clear();
        const QJsonArray arr = dataObject.value("limits").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            JointLimitSpec l;
            if (!readJointLimitSpec(arr.at(i).toObject(), l, error))
                return false;
            spec.limits.push_back(std::move(l));
        }
    }

    // poses (array)
    {
        spec.poses.clear();
        const QJsonArray arr = dataObject.value("poses").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            PoseSpec p;
            if (!readPoseSpec(arr.at(i).toObject(), p, error))
                return false;
            spec.poses.push_back(std::move(p));
        }
    }

    // dynamics (object)
    {
        const QJsonObject dmObj = dataObject.value("dynamics").toObject();
        if (!dmObj.isEmpty()) {
            if (!readDynamicModelSpec(dmObj, spec.dynamics, error))
                return false;
        }
    }

    // includes (array)
    {
        spec.includes.clear();
        const QJsonArray arr = dataObject.value("includes").toArray();
        for (int i = 0; i < arr.size(); ++i) {
            IncludeSpec inc;
            if (!readIncludeSpec(arr.at(i).toObject(), inc, error))
                return false;
            spec.includes.push_back(std::move(inc));
        }
    }

    // collisionSetup (object)
    {
        const QJsonObject csObj = dataObject.value("collisionSetup").toObject();
        if (!csObj.isEmpty()) {
            if (!readCollisionSetupSpec(csObj, spec.collisionSetup, error))
                return false;
        }
    }

    // proximitySetup (object)
    {
        const QJsonObject psObj = dataObject.value("proximitySetup").toObject();
        if (!psObj.isEmpty()) {
            if (!readProximitySetupSpec(psObj, spec.proximitySetup, error))
                return false;
        }
    }

    return true;
}

bool RobotModelSpecJson::fromJson(const std::string& json,
                                   RobotModelSpec& spec,
                                   std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(json), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = "JSON parse error: " + parseError.errorString().toStdString();
        return false;
    }

    if (!doc.isObject()) {
        if (error) *error = "JSON root is not an object";
        return false;
    }

    const QJsonObject root = doc.object();

    // Check schema version -- refuse versions newer than what we understand
    const int schemaVersion = root.value("schemaVersion").toInt(0);
    if (schemaVersion > SchemaVersion) {
        if (error)
            *error = "Unsupported schema version: " + std::to_string(schemaVersion);
        return false;
    }

    // Check type discriminator
    const QString type = root.value("type").toString();
    if (type != QLatin1String("RobotModelSpec")) {
        if (error)
            *error = "Unexpected JSON type: " + type.toStdString();
        return false;
    }

    // Extract data object
    const QJsonObject dataObject = root.value("data").toObject();
    if (dataObject.isEmpty()) {
        if (error) *error = "Missing or empty 'data' object";
        return false;
    }

    return fromObject(dataObject, spec, error);
}

} // namespace rws
