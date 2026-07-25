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

// -----------------------------------------------------------------------------
//  1. 基础字符串与路径处理工具函数
// -----------------------------------------------------------------------------

/// 辅助函数：将 std::string 转换为 Qt 的 QString
QString qstr (const std::string& value)
{
    return QString::fromStdString (value);
}

/// 解析文本中由空白分隔的浮点数字符串（例如 "1.0 2.0 3.0" -> vector<double>{1.0, 2.0, 3.0}）
std::vector< double > parseDoubles (const QString& text)
{
    std::vector< double > values;
    // 使用正则表达式按空白字符（空格、制表符等）分割字符串，并跳过空项
    const QStringList parts = text.split (QRegularExpression ("\\s+"), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const double value = part.toDouble (&ok);
        if (ok)
            values.push_back (value); // 成功解析则存入数组
    }
    return values;
}

/// 相对于基准文件（anchorFile）所在的目录，解析目标文件（file）的绝对路径
QString resolveRelativeTo (const QString& anchorFile, const QString& file)
{
    const QString trimmed = file.trimmed ();
    if (trimmed.isEmpty ())
        return trimmed;
    const QFileInfo info (trimmed);
    if (info.isAbsolute ())
        return info.absoluteFilePath (); // 如果本身就是绝对路径，直接返回
    // 提取基准文件所在的目录，再拼接相对路径
    return QDir (QFileInfo (anchorFile).absolutePath ()).absoluteFilePath (trimmed);
}

/// 相对于指定目录（directory），解析目标文件（file）的绝对路径
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

/// 移除名称末尾的 "Scene" 后缀（例如 "GenericSixAxisScene" -> "GenericSixAxis"）
QString withoutSceneSuffix (const QString& name)
{
    if (name.endsWith ("Scene", Qt::CaseSensitive))
        return name.left (name.size () - 5);
    return name;
}

/// 判断 std::vector<std::string> 容器中是否已经包含某个字符串
bool containsString (const std::vector< std::string >& values, const std::string& candidate)
{
    return std::find (values.begin (), values.end (), candidate) != values.end ();
}

/// 检查碰撞排除对列表（values）中是否已经存在指定的配对（candidate）
bool containsFramePair (const std::vector< FramePairSpec >& values, const FramePairSpec& candidate)
{
    for (const FramePairSpec& value : values) {
        if (value.first == candidate.first && value.second == candidate.second)
            return true;
    }
    return false;
}

/// 向碰撞排除对列表中去重追加一项（防止重复写入相同 Pair）
void addFramePairOnce (std::vector< FramePairSpec >& values, const FramePairSpec& candidate)
{
    if (!candidate.first.empty () && !candidate.second.empty () &&
        !containsFramePair (values, candidate)) {
        values.push_back (candidate);
    }
}

// -----------------------------------------------------------------------------
//  2. 运行时数据提取辅助函数
// -----------------------------------------------------------------------------

/// 从 RobWork 的属性图（PropertyMap）中提取名称为 name 的关节位姿向量（Q）
/// 并转换为角度（deg）写入 spec.poses 预设位姿列表中
void addQProperty (const rw::core::PropertyMap& map,
                   const std::string& name,
                   RobotModelSpec& spec)
{
    // 获取类型为 rw::math::Q 的属性指针
    const rw::math::Q* q = map.getPtr< rw::math::Q > (name);
    if (q == NULL || q->size () == 0)
        return;

    // 检查 spec.poses 中是否已经存在同名位姿，避免重复追加
    for (const PoseSpec& pose : spec.poses) {
        if (pose.name == name)
            return;
    }

    PoseSpec pose;
    pose.name = name;
    // 遍历 Q 向量中的每一个关节角，从 RobWork 的弧度（rad）转换为插件 UI 使用的角度（deg）
    for (size_t i = 0; i < q->size (); ++i)
        pose.q.push_back ((*q)[i] * rw::math::Rad2Deg);
    spec.poses.push_back (pose);
}

/// 递归收集指定 frame 及其下方所有的子坐标系（Frame Subtree）
void collectSubtreeFrames (const rw::kinematics::Frame* frame,
                           const rw::kinematics::State& state,
                           std::set< const rw::kinematics::Frame* >& frames)
{
    if (frame == NULL || frames.find (frame) != frames.end ())
        return;
    frames.insert (frame); // 将当前 frame 放入 set 集合
    // 遍历当前 frame 的所有直接子节点，并进行递归深度搜索
    const rw::kinematics::Frame::const_iterator_pair children = frame->getChildren (state);
    for (rw::kinematics::Frame::const_iterator it = children.first; it != children.second; ++it)
        collectSubtreeFrames (&*it, state, frames);
}

/// 判断给定的 frameName 是否属于 WorkCell 中的某个机器人设备（Device）内部的坐标系
bool isDeviceFrameName (const std::vector< rw::core::Ptr< rw::models::Device > >& devices,
                        const std::string& frameName)
{
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        // 尝试转型为关节设备（JointDevice）
        rw::core::Ptr< rw::models::JointDevice > jointDevice =
            dev.cast< rw::models::JointDevice > ();
        if (jointDevice == NULL)
            continue;
        // 检查是否为设备的 Base 基座坐标系
        if (jointDevice->getBase () != NULL && jointDevice->getBase ()->getName () == frameName)
            return true;
        // 检查是否为设备的某个 Joint 关节坐标系
        for (const rw::models::Joint* joint : jointDevice->getJoints ()) {
            if (joint != NULL && joint->getName () == frameName)
                return true;
        }
    }
    return false; // 不属于任何设备，说明是场景外部坐标系
}

/// 向 string 数组中唯一追加字符串（去重）
void addUnique (std::vector< std::string >& values, const std::string& value)
{
    if (!value.empty () && std::find (values.begin (), values.end (), value) == values.end ())
        values.push_back (value);
}

// -----------------------------------------------------------------------------
//  3. 设备作用域前缀剥离/规范化工具 (Scope Normalization)
// -----------------------------------------------------------------------------

/// 收集场景中可能出现的设备作用域前缀（例如 ["GenericSixAxis", "MyRobot"]）
/// 按前缀长度从长到短排序，防止前缀互相覆盖
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

    // 关键：按字符串长度降序排列（长的在前面）
    std::sort (prefixes.begin (), prefixes.end (),
               [] (const std::string& a, const std::string& b) {
                   return a.size () > b.size ();
               });
    return prefixes;
}

/// 剥离名称中的设备作用域前缀（例如将 "GenericSixAxis.Joint1" 还原为 "Joint1"）
std::string stripDeviceScope (const std::string& name,
                              const std::vector< std::string >& prefixes)
{
    for (const std::string& prefix : prefixes) {
        const std::string scoped = prefix + ".";
        // 检查 name 是否以 "prefix." 开头，如果是则切除前缀部分
        if (name.size () > scoped.size () && name.compare (0, scoped.size (), scoped) == 0)
            return name.substr (scoped.size ());
    }
    return name;
}

/// 遍历 spec 中的所有数据结构（关节、限位、Drawables、CollisionModels、SceneFrames 等）
/// 将所有带设备作用域前缀的名字统一净化还原，确保 UI 显示简洁
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

// -----------------------------------------------------------------------------
//  4. 源 XML 几何流式解析器 (XML Source Reader)
// -----------------------------------------------------------------------------

/// 从当前 XML 节点解析文本内容并填入 3D 数组 (RPY / Pos / RGB)
void readVector3 (QXmlStreamReader& xml, std::array< double, 3 >& values)
{
    const std::vector< double > parsed = parseDoubles (xml.readElementText ());
    if (parsed.size () == 3)
        values = {{parsed[0], parsed[1], parsed[2]}};
}

/// 读取 <Drawable> 节点内部的具体几何形状 (Box, Cylinder, Sphere, Polytope/Mesh/STL)
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
    xml.skipCurrentElement (); // 跳过该几何标签的结束符
}

/// 读取完整单个 <Drawable> 标签（提取名称、refframe、colmodel 属性及子节点）
void readDrawableElement (QXmlStreamReader& xml, DrawableSpec& drawable)
{
    const QXmlStreamAttributes attributes = xml.attributes ();
    drawable.name = attributes.value ("name").toString ().toStdString ();
    drawable.refFrame = attributes.value ("refframe").toString ().toStdString ();
    drawable.collisionModel = attributes.value ("colmodel").toString () == "Enabled";

    // 循环遍历 <Drawable> 的内部子标签 (<RPY>, <Pos>, <RGB>, 及几何体标签)
    while (xml.readNextStartElement ()) {
        const QString name = xml.name ().toString ();
        if (name == "RPY")
            readVector3 (xml, drawable.rpyDeg);
        else if (name == "Pos")
            readVector3 (xml, drawable.pos);
        else if (name == "RGB")
            readVector3 (xml, drawable.rgb);
        else
            readDrawableShape (xml, drawable); // 解析几何形状
    }
}

/// 工具：从 DrawableSpec 转换构造 SceneGeometrySpec
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

/// 递归解析源 XML 文件（及其通过 <Include> 引入的子 XML 文件），
/// 提取准确的 Drawable、CollisionModel 和场景几何体[cite: 14]
bool mergeSourceGeometryDocument (const QString& fileName,
                                  RobotModelSpec& spec,
                                  QStringList& warnings,
                                  std::set< QString >& visited)
{
    const QString absoluteFile = QFileInfo (fileName).absoluteFilePath ();
    // 记忆化防止多重包含或环形引用死循环
    if (visited.find (absoluteFile) != visited.end ())
        return true;
    visited.insert (absoluteFile);

    QFile file (absoluteFile);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read imported XML %1.").arg (absoluteFile);
        return false;
    }

    QXmlStreamReader xml (&file);
    bool deviceDocument = false; // 标记当前文件是设备 XML (<SerialDevice>) 还是场景 XML (<WorkCell>)
    QStringList includes;

    while (!xml.atEnd ()) {
        xml.readNext ();
        if (!xml.isStartElement ())
            continue;

        const QString name = xml.name ().toString ();
        
        if (name == "SerialDevice") {
            deviceDocument = true;
            // 记录导入的设备文件相对路径
            spec.imported.deviceFile = QDir (qstr (spec.saveDirectory)).relativeFilePath (absoluteFile)
                                          .toStdString ();
        }
        else if (name == "WorkCell") {
            // 记录导入的场景文件相对路径
            spec.imported.sceneFile = QDir (qstr (spec.saveDirectory)).relativeFilePath (absoluteFile)
                                         .toStdString ();
        }
        else if (name == "Include" && !deviceDocument) {
            // 收集 Scene XML 中通过 <Include file="..."/> 包含的子 XML 路径
            const QString include = xml.attributes ().value ("file").toString ();
            if (!include.isEmpty ())
                includes << resolveRelativeTo (absoluteFile, include);
            xml.skipCurrentElement ();
        }
        else if (name == "Drawable") {
            DrawableSpec drawable;
            readDrawableElement (xml, drawable);
            if (deviceDocument)
                spec.drawables.push_back (drawable); // 属于机器人设备本身的 Drawable
            else
                spec.sceneGeometries.push_back (sceneGeometryFromDrawable (drawable)); // 属于外部场景的 Drawable
        }
        else if (name == "CollisionModel" && deviceDocument) {
            // 独立碰撞模型标签
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

    // 递归去解析收集到的所有子 <Include> 文件
    for (const QString& include : includes)
        mergeSourceGeometryDocument (include, spec, warnings, visited);

    return true;
}

/// 核心接口：扫描源 XML，使用提取到的精确几何数据重置 spec 中的几何容器
bool mergeSourceGeometry (const rw::models::WorkCell& workcell,
                          RobotModelSpec& spec,
                          QStringList& warnings)
{
    // 获取当前 WorkCell 对应的磁盘 XML 文件路径
    const QString source = qstr (WorkCellConverter::inferWorkCellFilePath (workcell));
    if (source.isEmpty () || !QFileInfo::exists (source))
        return false;

    // 清空从内存粗略提取的默认 Box 占位几何
    spec.drawables.clear ();
    spec.sceneGeometries.clear ();
    spec.collisionModels.clear ();
    
    // 标记当前 spec 拥有导入来源，启用语义无损保存模式
    spec.imported.active = true;
    
    std::set< QString > visited;
    // 从主 WorkCell XML 开始递归深度扫描
    return mergeSourceGeometryDocument (source, spec, warnings, visited);
}

}    // namespace
// =============================================================================
//  WorkCellConverter::convert
//  说明: 将 RobWork 内存中的 WorkCell 场景对象转换为 RobotModelBuilder 插件
//        可编辑的 RobotModelSpec 纯数据结构。
//
//  整体设计哲学 (Runtime + XML Source 融合):
//    1) 优先提取 C++ 内存运行时 (WorkCell) 对象的权威骨架数据 (关节、变换、限位等)；
//    2) 扫描磁盘源 XML 文件 (mergeSourceGeometry) 补偿内存中被丢失/简化掉的
//       几何类型 (Box/Cylinder/Mesh) 及文件相对路径；
//    3) 解析配套 XML 文件 (CollisionSetup, ProximitySetup, DWC) 并合并配置；
//    4) 若存在同名 .rmb.json 侧车文件则优先恢复更完整的插件 UI 上下文；
//    5) 净化/剥离全名中的设备作用域前缀 (例如 GenericSixAxis.Joint1 -> Joint1)。
//
//  参数:
//    - workcell      : 已经加载进内存的 RobWork 场景对象
//    - state         : 场景当前状态 (Kinematic State)，用于求解全局/相对变换
//    - saveDirectory : 指定保存的目标磁盘路径 (若为空则自动通过文件路径推导)
//    - warnings      : [out] 输出参数，收集转换过程中的非致命警告信息
//
//  返回值:
//    - RobotModelSpec: 构建完成、可直接灌入 UI 控件回填的模型配置规范对象
// =============================================================================
RobotModelSpec WorkCellConverter::convert (const rw::models::WorkCell& workcell,
                                           const rw::kinematics::State& state,
                                           const std::string& saveDirectory,
                                           QStringList& warnings)
{
    RobotModelSpec spec;

    // ---- 1. 自动提取与整理机器人模型名称 ----
    std::string wcName = workcell.getName ();
    // 如果 WorkCell 名称包含 "Scene" 后缀 (例如 "GenericSixAxisScene")，
    // 裁剪掉末尾的 5 个字符，还原为标准的机器人名字 "GenericSixAxis"
    if (wcName.size () >= 5 && wcName.compare (wcName.size () - 5, 5, "Scene") == 0)
        spec.robotName = wcName.substr (0, wcName.size () - 5);
    else
        spec.robotName = wcName;

    // ---- 2. 初始化全局导出配置参数 ----
    // 目标保存目录: 优先使用外部传入的路径，为空时通过 WorkCell 所在路径自动推导
    spec.saveDirectory = saveDirectory.empty () ? inferSaveDirectory (workcell) : saveDirectory;
    spec.mode = KinematicsViewMode::JointRPYPos; // 默认采用 SE(3) RPY+Pos 运动学真值模式
    spec.showFrameAxes = false;                  // 默认不显式开启坐标轴绘制
    spec.generateDrawables = true;               // 默认勾选绘制几何体
    spec.generateScene = true;                   // 默认开启 WorkCell 场景生成
    spec.dynamics.generateDynamicWorkCell = false;// 默认不生成物理仿真 DWC

    // ---- 3. 从 C++ 内存 WorkCell 对象中提取骨架数据 ----
    // 3.1 提取串联关节设备 (JointDevice): 包含 Joint 名称、类型、相对 SE(3) 矩阵、Limits 及预设 Q 位姿
    extractSerialDevice (workcell, spec, warnings);
    // 3.2 提取场景级别的独立坐标系 (Scene Frames, 如 Table, Workpiece 等)
    extractSceneFrames (workcell, state, spec);
    // 3.3 初步提取渲染几何对象 (此时内存中的 Geometry 形状信息尚不完整)
    extractDrawables (workcell, spec, warnings);
    // 3.4 提取场景中的碰撞排除配对 (CollisionSetup)
    extractCollisionSetup (workcell, spec);
    // 3.5 提取场景中的临近查询与安全距离规则 (ProximitySetup)
    extractProximitySetup (workcell, spec);

    // ---- 4. 扫描磁盘源 XML 文件，无损融合真实几何与路径 ----
    // 从源 XML 流中解析 <Box>, <Cylinder>, <Polytope file="..."> 的原始特征，
    // 覆盖前面步骤 3.3 粗略提取出的占位几何体
    mergeSourceGeometry (workcell, spec, warnings);

    // ---- 5. 运动学视图同步 ----
    // 根据提取到的 SE(3) transformJoints 关节真值，反向计算并刷新只读的 DH 4 参数投影视图
    RobotModelXmlWriter::refreshDhProjectionFromTransform (spec);

    // ---- 6. 解析并合并伴生 XML 配置文件 ----
    // 扫描 WorkCell XML 中引用的 CollisionSetup.xml, ProximitySetup.xml 以及同目录下的 *.dwc.xml
    mergeCompanionXmlMetadata (workcell, spec, warnings);

    // ---- 7. 侧车配置文件 (.rmb.json) 优先加载逻辑 ----
    // 如果磁盘上存在插件先前导出的 .rmb.json，优先使用它替换当前构建的 spec，
    // 从而 100% 无损恢复包含自定义 UI 扩展、隐藏属性在内的完整上下文
    RobotModelSpec sidecarSpec;
    if (tryLoadSidecar (workcell, spec.saveDirectory, sidecarSpec, warnings)) {
        spec = sidecarSpec;
        // 恢复 saveDirectory，防止 sidecar 里的旧路径覆盖当前传入的新目录
        spec.saveDirectory = saveDirectory.empty () ? inferSaveDirectory (workcell) : saveDirectory;
        // 重新确保伴生 XML 元数据与文件最新状态同步
        mergeCompanionXmlMetadata (workcell, spec, warnings);
    }

    // ---- 8. 设备作用域前缀规范化/剥离 ----
    // 剥离 RobWork 加载设备时在 Frame 名字前自动追加的前缀 (如 "GenericSixAxis.Joint1" -> "Joint1")，
    // 确保回填到 UI 界面表格中的关节与坐标系名称干净简洁
    normalizeDeviceScopedNames (workcell, spec);

    // ---- 9. 返回解析与融合完成的最终 spec ----
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

// =============================================================================
//  WorkCellConverter::extractSerialDevice
//  说明: 从 RobWork 的 WorkCell 场景对象中寻找并提取主机器人设备 (JointDevice/SerialDevice)
//        的核心运动学与配置信息。
//
//  工作流程与职责:
//    1) 遍历 WorkCell 场景中包含的所有设备 (Device)，寻找第一个关节设备 (JointDevice)；
//    2) 若找到，将其作为主机器人模型，并填充 spec.robotName (若之前未设置)；
//    3) 依次调用下游专门方法提取:
//       - extractJoints:  提取所有关节的变换矩阵 (SE(3)) 及关节类型 (Revolute/Prismatic)；
//       - extractLimits:  提取关节的运动限位 (位置、速度、加速度限位)；
//       - extractQConfigs: 提取预设的关节姿态配置 (Poses / Q 向量，如 Home, Zero, Ready 等)；
//    4) 检查机器人的基座坐标系 (Base Frame) 是否开启了坐标轴绘制属性 (ShowFrameAxis)，
//       若开启则同步更新 spec.showFrameAxes 全局开关。
//
//  参数:
//    - workcell : RobWork 内存场景对象
//    - spec     : [out] 输出的目标数据结构，提取的关节/限位/位姿数据将写入其中
//    - warnings : [out] 警告信息收集列表 (例如无 JointDevice 时上报警告)
//
//  返回值:
//    - bool : 成功找到并提取出 JointDevice 返回 true；若场景中无任何 JointDevice 则返回 false
// =============================================================================
bool WorkCellConverter::extractSerialDevice (const rw::models::WorkCell& workcell,
                                             RobotModelSpec& spec,
                                             QStringList& warnings)
{
    // ---- 1. 获取 WorkCell 中加载的所有设备列表 ----
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    rw::core::Ptr< rw::models::JointDevice > jointDevice = NULL;

    // 遍历设备列表，寻找第一个可以成功转型为 JointDevice (关节设备/串联机械臂) 的对象
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        jointDevice = dev.cast< rw::models::JointDevice > ();
        if (jointDevice != NULL)
            break; // 优先选取找到的第一个主关节设备
    }

    // ---- 2. 检查是否找到有效的 JointDevice ----
    if (jointDevice == NULL) {
        // 如果 WorkCell 中完全没有关节设备 (例如只有静态环境或物体)，记录警告并中断提取
        warnings << "No JointDevice found in WorkCell. Cannot extract robot kinematics.";
        return false;
    }

    // ---- 3. 补全机器人模型名称 ----
    // 如果 spec 中尚未设置机器人名称，默认使用找到的 JointDevice 设备名
    if (spec.robotName.empty ())
        spec.robotName = jointDevice->getName ();

    // ---- 4. 链式调用三大核心提取函数 ----
    // 4.1 提取关节列表 (Joints: 名字、类型、相对齐次变换矩阵 Transform3D)
    extractJoints (*jointDevice, spec, warnings);
    
    // 4.2 提取关节限位 (Limits: 位置 min/max、速度 max、加速度 max)
    extractLimits (*jointDevice, spec);
    
    // 4.3 提取预设关节位姿 (Q Configurations: 如 Zero, Ready 等 Q 向量)
    extractQConfigs (*jointDevice, spec);

    // ---- 5. 检查基座 Frame 的 UI 属性 ----
    // 获取机器人的基座 Frame (Base Frame)
    const rw::kinematics::Frame* base = jointDevice->getBase ();
    // 若基座 Frame 包含 ShowFrameAxis 属性且为 true，开启全局的坐标轴显示开关
    if (base != NULL && hasShowFrameAxes (*base))
        spec.showFrameAxes = true;

    return true; // 成功完成主设备提取
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
// =============================================================================
//  WorkCellConverter::extractSceneFrames
//  说明: 从 RobWork 的 WorkCell 场景中提取所有“非机器人设备”的场景坐标系
//        (Scene Frames)，并识别专用的 RobotBase 坐标系。
//
//  核心分离算法 (Device Frame Filtering):
//    1) 遍历场景中的所有 Device (机器人/设备)，递归收集每个设备 Base 及其下属的
//       整个子树节点 (Subtree Frames) 存入 deviceFrames 集合；
//    2) 遍历 WorkCell 中的全部 Frame，跳过存在于 deviceFrames 中的节点 (设备内部 Frame)，
//       并跳过 WORLD 全局世界坐标系；
//    3) 剩下的 Frame 即为纯场景环境节点 (如工作台 Table、工件 Workpiece 等)；
//    4) 特殊逻辑: 识别名为 "RobotBase" 的场景节点作为机器人的基座挂载点；
//       若场景中不存在，则自动初始化一个默认的 RobotBase 兜底对象。
//
//  参数:
//    - workcell : RobWork 内存 WorkCell 对象
//    - state    : 场景当前状态 (Kinematic State)，用于求解相对变换、父节点及 DAF 状态
//    - spec     : [out] 转换目标数据结构， sceneFrames 与 robotBaseFrame 将写入其中
// =============================================================================
void WorkCellConverter::extractSceneFrames (const rw::models::WorkCell& workcell,
                                            const rw::kinematics::State& state,
                                            RobotModelSpec& spec)
{
    // 清空重置场景坐标系列表
    spec.sceneFrames.clear ();

    // ---- 1. 递归收集所有机器人设备内部的 Frame 集合 (用于过滤) ----
    std::set< const rw::kinematics::Frame* > deviceFrames;
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    for (const rw::core::Ptr< rw::models::Device >& dev : devices) {
        // 若设备有基座 Frame，递归将该设备下的所有关节与连杆 Frame 收集进集合
        if (dev->getBase () != NULL)
            collectSubtreeFrames (dev->getBase (), state, deviceFrames); //
    }

    // ---- 2. 遍历场景中的所有 Frame，筛选提取场景坐标系 ----
    bool foundRobotBase = false;
    const std::vector< rw::kinematics::Frame* > frames = workcell.getFrames ();
    for (const rw::kinematics::Frame* frame : frames) {
        // 过滤条件 A: 空指针 或 属于机器人设备内部的 Frame
        if (frame == NULL || deviceFrames.find (frame) != deviceFrames.end ())
            continue; //
            
        // 过滤条件 B: 跳过 WORLD 根世界坐标系 (WORLD 隐式存在，无需放入 sceneFrames 列表)
        if (frame == workcell.getWorldFrame ())
            continue; //

        // 创建场景 Frame 描述对象
        FrameSpec out;
        out.name = frame->getName (); // 提取坐标系名称
        
        // 提取参考父坐标系refframe (无父节点则挂载到 "WORLD")
        const rw::kinematics::Frame* parent = frame->getParent (state);
        out.refFrame = parent != NULL ? parent->getName () : "WORLD"; //
        
        // 判断 Frame 类型: 尝试转型为 MovableFrame
        // 可动坐标系标为 Movable，其余 (如 FixedFrame) 标为 Fixed
        out.frameType =
            dynamic_cast< const rw::kinematics::MovableFrame* > (frame) != NULL
                ? SceneFrameType::Movable
                : SceneFrameType::Fixed; //
                
        // 检查该 Frame 是否为动态附着参考系 (DAF: Dynamic Attached Frame)
        out.daf = isDAF (frame, state); //
        
        // 姿态模式设为 RPYPos
        out.poseMode = PoseMode::RPYPos; //
        
        // 求解当前 Frame 相对其父 Frame 的局部齐次变换矩阵，并转换为 RPY (角度) 和 Pos (米)
        transformToRpyPos (frame->getTransform (state), out.rpyDeg, out.pos); //

        // 检查该场景 Frame 是否启用了显示坐标轴属性
        if (hasShowFrameAxes (*frame))
            spec.showFrameAxes = true; //

        // 分支逻辑: 识别专用基座节点 "RobotBase" 或 "robotBase"
        if (out.name == "RobotBase" || out.name == "robotBase") {
            spec.robotBaseFrame = out; // 填入专门的 robotBaseFrame 结构中
            foundRobotBase = true;     // 标记已找到
        }
        else {
            // 普通场景节点 (如 Table, Workpiece, CameraFrame 等) 压入数组
            spec.sceneFrames.push_back (out); //
        }
    }

    // ---- 3. 兜底逻辑: 若场景中未显式定义 RobotBase 坐标系 ----
    if (!foundRobotBase) {
        // 自动初始化一个默认的 RobotBase 对象，挂载在 WORLD 原点 (0,0,0)
        spec.robotBaseFrame.name = "RobotBase"; //
        spec.robotBaseFrame.refFrame = "WORLD"; //
        spec.robotBaseFrame.frameType = SceneFrameType::Fixed; //
        spec.robotBaseFrame.poseMode = PoseMode::RPYPos; //
    }
}

void WorkCellConverter::extractDrawables (const rw::models::WorkCell& workcell,
                                          RobotModelSpec& spec,
                                          QStringList& warnings)
{
    // 1. 清空目标结构体中的旧数据，确保重新提取时的干净状态
    spec.drawables.clear ();
    spec.sceneGeometries.clear ();

    // 2. 从 WorkCell 中获取所有的设备（Devices，如机械臂等）和对象（Objects，如障碍物、工件等）
    const std::vector< rw::core::Ptr< rw::models::Device > > devices = workcell.getDevices ();
    const std::vector< rw::core::Ptr< rw::models::Object > > objects = workcell.getObjects ();

    // 3. 遍历工作空间中的每一个对象
    for (const rw::core::Ptr< rw::models::Object >& obj : objects) {
        // 空指针安全检查
        if (obj == NULL)
            continue;

        // 4. 获取当前对象绑定的基座/参考坐标系（Frame）
        const rw::kinematics::Frame* base = NULL;
#ifdef RW_USE_PTR
        base = obj->getBase ().get (); // 如果启用了智能指针宏，用 .get() 提取原始指针
#else
        base = obj->getBase ();        // 否则直接获取指针
#endif
        // 如果无法获取该对象的基座坐标系，跳过处理
        if (base == NULL)
            continue;

        // 获取坐标系的名称
        const std::string refFrame = base->getName ();

        // 5. 判断该坐标系是否属于某个机器人/设备（Device）
        if (isDeviceFrameName (devices, refFrame)) {
            // 【情况 A】：如果对象附着在设备坐标系上，归类为机器人的可绘制部件（DrawableSpec）
            DrawableSpec drawable;
            drawable.name = obj->getName ();      // 对象名称
            drawable.refFrame = refFrame;          // 绑定的参考坐标系
            drawable.shape = "Box";                // 默认/预设形状类型为长方体

            // 检查该对象是否包含碰撞模型/几何形状
            try {
                // 如果几何体列表不为空，则认为具有碰撞模型
                drawable.collisionModel = !obj->getGeometry ().empty ();
            }
            catch (...) {
                // 异常处理：若读取几何体失败，记录警告日志
                warnings << QString ("Could not inspect geometry for %1.")
                                .arg (qstr (drawable.name));
            }
            spec.drawables.push_back (drawable);   // 存入设备部件列表
        }
        else {
            // 【情况 B】：如果对象属于外部环境，归类为静态场景几何体（SceneGeometrySpec）
            SceneGeometrySpec geometry;
            geometry.name = obj->getName ();        // 几何体名称
            geometry.refFrame = refFrame;            // 参考坐标系（通常为世界坐标系或环境坐标系）
            geometry.kind = GeometryKind::Box;       // 几何类型指定为长方体
            geometry.collisionModel = true;          // 默认开启碰撞检测属性
            spec.sceneGeometries.push_back (geometry); // 存入场景几何体列表
        }
    }
}

// =============================================================================
//  extractCollisionSetup
//  说明: 从内存中的 WorkCell 对象中提取碰撞检测配置 (CollisionSetup)，
//        并填充到 RobotModelSpec 的 collisionSetup 成员中。
//
//  参数:
//    - workcell : RobWork 内存中的 WorkCell 对象，包含碰撞和距离查询的配置
//    - spec     : [out] 目标模型规范对象，提取的数据将存入 spec.collisionSetup 中
// =============================================================================
void WorkCellConverter::extractCollisionSetup (const rw::models::WorkCell& workcell,
                                               RobotModelSpec& spec)
{
    // 1. 从 WorkCell 实例中获取绑定的 CollisionSetup 规则对象
    const rw::proximity::CollisionSetup setup =
        rw::proximity::CollisionSetup::get (workcell);

    // 2. 在导出规范中使能碰撞配置 (表明当前模型包含碰撞设置)
    spec.collisionSetup.enabled = true;

    // 3. 读取并同步全局标志：是否排除所有静态物体/坐标系对之间的碰撞检测
    //    (例如：桌子和墙壁之间不需要做碰撞计算)
    spec.collisionSetup.excludeStaticPairs = setup.excludeStaticPairs ();

    // 4. 清空旧的排除配对列表，准备重新填充
    spec.collisionSetup.excludePairs.clear ();

    // 5. 遍历 RobWork 中设置的所有碰撞排除对 (FramePair)
    for (const rw::core::StringPair& pair : setup.getExcludeList ()) {
        FramePairSpec out;
        out.first = pair.first;   // 排除对中的第一个 Frame 名称
        out.second = pair.second; // 排除对中的第二个 Frame 名称

        // 调用辅助函数防重追加：仅当 pair 有效且不存在时才存入 spec.collisionSetup.excludePairs
        addFramePairOnce (spec.collisionSetup.excludePairs, out);
    }
}

// =============================================================================
//  extractProximitySetup
//  说明: 从内存中的 WorkCell 对象中提取临近/距离查询配置 (ProximitySetup)，
//        包含近邻规则模式 (Pattern A/B) 及全局过滤策略，并填充到 RobotModelSpec 中。
//
//  参数:
//    - workcell : RobWork 内存中的 WorkCell 对象，挂载了原场景的临近检测规则
//    - spec     : [out] 目标模型规范对象，提取的数据将存入 spec.proximitySetup 中
// =============================================================================
void WorkCellConverter::extractProximitySetup (const rw::models::WorkCell& workcell,
                                               RobotModelSpec& spec)
{
    // 1. 从 WorkCell 实例中获取绑定的 ProximitySetup 规则配置对象
    const rw::proximity::ProximitySetup setup =
        rw::proximity::ProximitySetup::get (workcell);

    // 2. 确定是否使能临近配置：
    //    只要配置是从配置文件中加载的 (getLoadedFromFile)，或者规则列表不为空，即判定为开启使能
    spec.proximitySetup.enabled =
        setup.getLoadedFromFile () || !setup.getProximitySetupRules ().empty ();

    // 3. 提取全局过滤标志：是否默认包含所有坐标系/物体 (UseIncludeAll)
    spec.proximitySetup.useIncludeAll = setup.useIncludeAll ();

    // 4. 提取全局过滤标志：是否排除静态物体之间的临近/距离查询 (UseExcludeStaticPairs)
    spec.proximitySetup.useExcludeStaticPairs = setup.useExcludeStaticPairs ();

    // 5. 清空旧的规则列表，准备重新填充
    spec.proximitySetup.rules.clear ();

    // 6. 遍历 RobWork 中设置的所有临近规则 (ProximitySetupRule)
    for (const rw::proximity::ProximitySetupRule& rule : setup.getProximitySetupRules ()) {
        // 获取规则中定义的两个通配符模式匹配串 (例如 PatternA="Joint.*", PatternB="Table.*")
        const std::pair< std::string, std::string > patterns = rule.getPatterns ();

        ProximityRuleSpec out;

        // 判断规则类型：包含 (Include) 还是排除 (Exclude)
        out.kind = rule.type () == rw::proximity::ProximitySetupRule::INCLUDE_RULE
                       ? ProximityRuleKind::Include
                       : ProximityRuleKind::Exclude;

        // 填充匹配模式字符串
        out.patternA = patterns.first;  // 匹配集合 A 的通配符
        out.patternB = patterns.second; // 匹配集合 B 的通配符

        // 存入 spec 的规则列表中
        spec.proximitySetup.rules.push_back (out);
    }
}
// =============================================================================
//  tryLoadSidecar
//  说明: 尝试搜索并加载插件特有的侧车 (Sidecar) JSON 配置文件 (*.rmb.json)。
//
//  设计背景:
//    因为 RobWork 原生的 XML 文件可能无法表达插件 UI 界面上的所有自定义扩展参数，
//    插件在保存模型时通常会同步导出一个同名的 .rmb.json 侧车文件[cite: 3, 9]。
//    当主程序重新打开该场景时，本函数会优先寻找该 JSON，若存在则直接反序列化恢复[cite: 3, 9]。
//
//  搜索候选策略 (Candidates):
//    1) 按当前的 spec.robotName 组合: <saveDir>/<robotName>.rmb.json
//    2) 按 WorkCell 对象的名称组合:  <saveDir>/<workcellName>.rmb.json
//    3) 按 WorkCell 文件的主文件名组合: <workcellFileDir>/<baseName>.rmb.json
//
//  参数:
//    - workcell      : 内存中的 WorkCell 对象
//    - saveDirectory : 目标保存目录
//    - spec          : [out] 输出的目标 spec，若读取 JSON 成功，解析出的完整 spec 将写入其中
//    - warnings      : [out] 记录文件打开失败或 JSON 语法错误的警告列表
//
//  返回值:
//    - bool : 成功找到并解析任意一个 sidecar 文件返回 true；否则返回 false
// =============================================================================
bool WorkCellConverter::tryLoadSidecar (const rw::models::WorkCell& workcell,
                                        const std::string& saveDirectory,
                                        RobotModelSpec& spec,
                                        QStringList& warnings)
{
    // 用于按优先级存放可能存在的侧车 JSON 文件路径列表
    QStringList candidates;
    const QString saveDir = qstr (saveDirectory);

    // ---- 1. 候选路径策略 1: 基于 spec 中已有的机器人名称 ----
    if (!spec.robotName.empty ()) {
        // 对机器人名称进行文件名安全清洗 (如清洗空格/特殊字符)，拼接成 <robotName>.rmb.json
        candidates << QDir (saveDir).filePath (
            RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".rmb.json");
    }

    // ---- 2. 候选路径策略 2: 基于 WorkCell 对象的 Name (裁剪掉 Scene 后缀) ----
    const QString wcName = withoutSceneSuffix (qstr (workcell.getName ()));
    if (!wcName.isEmpty ()) {
        candidates << QDir (saveDir).filePath (
            RobotModelXmlWriter::sanitizeFileBaseName (wcName) + ".rmb.json");
    }

    // ---- 3. 候选路径策略 3: 基于 WorkCell 文件的磁盘主文件名 ----
    const QString wcFile = qstr (inferWorkCellFilePath (workcell));
    if (!wcFile.isEmpty ()) {
        const QFileInfo info (wcFile);
        // 在 XML 所在绝对目录下，寻找同名 .rmb.json 文件
        candidates << QDir (info.absolutePath ()).filePath (
            withoutSceneSuffix (info.baseName ()) + ".rmb.json");
    }

    // ---- 4. 对候选路径列表列表去重，避免对同一文件进行重复打开测试 ----
    candidates.removeDuplicates ();

    // ---- 5. 顺序遍历候选文件列表，尝试打开并解析 ----
    for (const QString& candidate : candidates) {
        QFile file (candidate);
        
        // 检查磁盘上是否存在该文件，若不存在直接测试下一个候选路径
        if (!file.exists ())
            continue;

        // 尝试打开该 JSON 文件
        if (!file.open (QFile::ReadOnly | QFile::Text)) {
            warnings << QString ("Could not read RobotModelBuilder sidecar %1.")
                            .arg (candidate);
            continue;
        }

        // 读取整个 JSON 文件的 UTF-8 字符串内容
        const std::string json = QString::fromUtf8 (file.readAll ()).toStdString ();
        std::string error;
        RobotModelSpec loaded;

        // 调用 JSON 反序列化模块将字符串还原为 RobotModelSpec 结构体
        if (!RobotModelSpecJson::fromJson (json, loaded, &error)) {
            // 如果 JSON 格式损坏或 Schema 不兼容，记录警告并尝试下一个候选文件
            warnings << QString ("Could not parse RobotModelBuilder sidecar %1: %2")
                            .arg (candidate, qstr (error));
            continue;
        }

        // 解析成功，将加载到的完整 spec 赋予输出参数并直接返回成功
        spec = loaded;
        return true;
    }

    // 搜索完所有候选路径均未找到合法的侧车 JSON 文件，返回 false
    return false;
}

// =============================================================================
//  mergeCompanionXmlMetadata
//  说明: 解析与融合主场景 XML (WorkCell XML) 中的关联/伴生文件元数据。
//
//  详细流程:
//    1) 推导并打开磁盘上的主场景 XML 文件 (如 "GenericSixAxisScene.wc.xml")；
//    2) 使用 QXmlStreamReader 流式扫描 XML 节点:
//       - <Include file="...">: 收集用户额外包含的场景/设备文件，自动跳过主机器人设备文件；
//       - <CollisionSetup file="...">: 记录碰撞配置文件路径并开启使能标记；
//       - <ProximitySetup file="...">: 记录临近检测配置文件路径并开启使能标记；
//    3) 若找到了 CollisionSetup.xml 或 ProximitySetup.xml，解析其绝对路径并调用
//       对应的子函数 (mergeCollisionSetupXml / mergeProximitySetupXml) 深入解析内容；
//    4) 检查保存目录下是否存在对应的物理动力学文件 (*.dwc.xml)，若存在则调用
//       mergeDynamicWorkCellXml 融合动力学参数。
//
//  参数:
//    - workcell : 内存中的 WorkCell 对象 (用于推导文件路径)
//    - spec     : [out] 转换目标数据模型，关联文件配置将写入其中
//    - warnings : [out] 记录文件打不开或 XML 解析语法错误的非致命警告
// =============================================================================
void WorkCellConverter::mergeCompanionXmlMetadata (const rw::models::WorkCell& workcell,
                                                   RobotModelSpec& spec,
                                                   QStringList& warnings)
{
    // ---- 1. 获取 WorkCell 的磁盘文件路径，如果路径无效或文件不存在则直接返回 ----
    const QString wcFile = qstr (inferWorkCellFilePath (workcell));
    if (wcFile.isEmpty () || !QFileInfo::exists (wcFile))
        return;

    // ---- 2. 打开 WorkCell XML 文件 ----
    QFile file (wcFile);
    if (!file.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read WorkCell XML %1.").arg (wcFile);
        return;
    }

    // 清空当前 spec 中的额外 includes 引用列表，准备重新收集
    spec.includes.clear ();

    // 计算当前主机器人设备文件的标准文件名 (例如 "GenericSixAxis.wc.xml")，
    // 用于在扫描 <Include> 时区别“主机器人文件”与“用户追加的其他 Include”
    const QString primaryDevice =
        RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".wc.xml";

    // 局部变量：用于暂存扫描到的碰撞和临近配置文件相对路径
    QString collisionFile;
    QString proximityFile;

    // ---- 3. 使用 QXmlStreamReader 流式解析主 WorkCell XML ----
    QXmlStreamReader xml (&file);
    while (!xml.atEnd ()) {
        xml.readNext ();
        // 只关心起始元素标签 (StartElement)
        if (!xml.isStartElement ())
            continue;

        // 分支 A: 处理 <Include file="..."> 节点
        if (xml.name () == QLatin1String ("Include")) {
            const QString include = xml.attributes ().value ("file").toString ().trimmed ();
            if (include.isEmpty ())
                continue;

            // 关键判断：如果该 Include 引用的是机器人自身的主文件 (primaryDevice)，
            // 说明是系统默认生成的机器人加载节点，跳过它，防止重复追加到自定义 includes 列表
            if (QFileInfo (include).fileName ().compare (primaryDevice, Qt::CaseInsensitive) == 0)
                continue;

            // 属于用户额外引用的场景/设备文件，记录到 spec.includes
            IncludeSpec out;
            out.file = include.toStdString ();
            out.kind = IncludeKind::WorkCell;
            spec.includes.push_back (out);
        }
        // 分支 B: 处理 <CollisionSetup file="..."> 节点
        else if (xml.name () == QLatin1String ("CollisionSetup")) {
            collisionFile = xml.attributes ().value ("file").toString ().trimmed ();
            if (!collisionFile.isEmpty ()) {
                spec.collisionSetup.enabled = true; // 标记开启碰撞矩阵导出
                spec.collisionSetup.file = collisionFile.toStdString (); // 记录碰撞文件名
            }
        }
        // 分支 C: 处理 <ProximitySetup file="..."> 节点
        else if (xml.name () == QLatin1String ("ProximitySetup")) {
            proximityFile = xml.attributes ().value ("file").toString ().trimmed ();
            if (!proximityFile.isEmpty ()) {
                spec.proximitySetup.enabled = true; // 标记开启临近查询导出
                spec.proximitySetup.file = proximityFile.toStdString (); // 记录临近查询文件名
            }
        }
    }

    // 防御性检查：如果在流解析过程中发生语法截断或报错，记录警告
    if (xml.hasError ())
        warnings << QString ("Could not fully parse WorkCell XML %1: %2")
                        .arg (wcFile, xml.errorString ());

    // ---- 4. 根据扫描到的路径，深入解析具体的伴生 XML 配置文件 ----
    
    // 如果配置了 CollisionSetup.xml，将其相对路径转换为绝对路径，并调用 mergeCollisionSetupXml 深入解析
    if (!collisionFile.isEmpty ())
        mergeCollisionSetupXml (resolveRelativeTo (wcFile, collisionFile), spec, warnings);

    // 如果配置了 ProximitySetup.xml，同理调用 mergeProximitySetupXml 深入解析规则
    if (!proximityFile.isEmpty ())
        mergeProximitySetupXml (resolveRelativeTo (wcFile, proximityFile), spec, warnings);

    // ---- 5. 检查与融合动力学配置文件 (*.dwc.xml) ----
    // 根据机器人名称推导 DWC 文件路径 (如 "/path/to/GenericSixAxis.dwc.xml")
    const QString dwcFile = resolveRelativeToDirectory (
        qstr (spec.saveDirectory),
        RobotModelXmlWriter::sanitizeFileBaseName (qstr (spec.robotName)) + ".dwc.xml");

    // 若磁盘上实际存在该 DWC 物理文件，解析并提取其 Mass, COG, Inertia 和 ForceLimit 数据
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
// =============================================================================
//  mergeDynamicWorkCellXml
//  说明: 解析物理仿真动力学配置文件 (*.dwc.xml)，并将动力学参数融合填入 spec.dynamics。
//
//  解析的核心 XML 结构 (RobWorkSim 规范):
//    <DynamicWorkCell workcell="...">
//      <RigidDevice device="...">
//        <KinematicBase frame="Base"><MaterialID>Steel</MaterialID></KinematicBase>
//        <ForceLimit joint="Joint1">1000</ForceLimit>
//        <Link object="Joint1">
//          <Mass>5.0</Mass>
//          <COG>0 0 0</COG>
//          <Inertia>Ixx Ixy Ixz Ixy Iyy Iyz Ixz Iyz Izz</Inertia> <!-- 9元矩阵或6元分量 -->
//          <MaterialID>Aluminum</MaterialID>
//        </Link>
//      </RigidDevice>
//    </DynamicWorkCell>
//
//  参数:
//    - file     : *.dwc.xml 文件的绝对磁盘路径
//    - spec     : [out] 目标数据模型，提取的物理动力学参数将写入 spec.dynamics 中
//    - warnings : [out] 警告信息收集列表 (记录打不开或 XML 解析错误)
// =============================================================================
void WorkCellConverter::mergeDynamicWorkCellXml (const QString& file,
                                                 RobotModelSpec& spec,
                                                 QStringList& warnings)
{
    // ---- 1. 打开 DWC XML 文件 ----
    QFile input (file);
    if (!input.open (QFile::ReadOnly | QFile::Text)) {
        warnings << QString ("Could not read DynamicWorkCell XML %1.").arg (file);
        return; // 打开失败，记录警告后中断
    }

    // ---- 2. 重置并使能 spec 中的动力学配置 ----
    spec.dynamics.generateDynamicWorkCell = true; // 标记开启 DWC 生成与导出
    spec.dynamics.links.clear ();                  // 清空旧的连杆动力学列表
    spec.dynamics.forceLimits.clear ();            // 清空旧的关节驱动力上限列表

    // 状态标记变量
    int currentLink = -1;       // 记录当前正在解析的连杆在 spec.dynamics.links 中的索引 (-1 表示不在 Link 节点内)
    bool inKinematicBase = false; // 标记当前是否处于 <KinematicBase> 节点内部

    // ---- 3. 使用 QXmlStreamReader 进行流式解析 ----
    QXmlStreamReader xml (&input);
    while (!xml.atEnd ()) {
        xml.readNext ();

        // 处理起始标签 (StartElement)
        if (xml.isStartElement ()) {
            const auto name = xml.name ();

            // 分支 A: 处理 <KinematicBase frame="..."> 运动学基座节点
            if (name == QLatin1String ("KinematicBase")) {
                inKinematicBase = true; // 开启基座节点上下文标记
                const QString frame = xml.attributes ().value ("frame").toString ();
                if (!frame.isEmpty ())
                    spec.dynamics.baseFrame = frame.toStdString (); // 提取基座 Frame 名称
            }
            // 分支 B: 处理 <ForceLimit joint="..."> 关节最大驱动力限制节点
            else if (name == QLatin1String ("ForceLimit")) {
                JointForceLimitSpec force;
                force.jointName = xml.attributes ().value ("joint").toString ().toStdString (); // 关联关节名
                force.maxForce = xml.readElementText ().trimmed ().toDouble ();                 // 读取驱动力上限数值 (Nm 或 N)
                spec.dynamics.forceLimits.push_back (force);
            }
            // 分支 C: 处理 <Link object="..."> 刚体连杆动力学节点
            else if (name == QLatin1String ("Link")) {
                LinkDynamicsSpec link;
                // 自动生成连杆内部标识名 (如 "Link1", "Link2")
                link.linkName = QString ("Link%1").arg (spec.dynamics.links.size () + 1).toStdString ();
                link.objectName = xml.attributes ().value ("object").toString ().toStdString (); // 绑定的可动关节名
                // 初始化默认物理属性
                link.mass = 1.0;
                link.cog = {{0, 0, 0}};
                link.inertia = {{1, 1, 1, 0, 0, 0}};
                link.estimateInertia = true;
                link.material = spec.dynamics.baseMaterial;

                spec.dynamics.links.push_back (link);
                // 更新 currentLink 指向刚压入的最后一个连杆对象，供后续子标签填充数据
                currentLink = static_cast< int > (spec.dynamics.links.size ()) - 1;
            }
            // 分支 C1: 处理 <Mass> 连杆质量 (kg)
            else if (name == QLatin1String ("Mass") && currentLink >= 0) {
                spec.dynamics.links[currentLink].mass =
                    xml.readElementText ().trimmed ().toDouble ();
            }
            // 分支 C2: 处理 <COG> 质心位置 (Center of Gravity, 米)
            else if (name == QLatin1String ("COG") && currentLink >= 0) {
                const std::vector< double > values = parseDoubles (xml.readElementText ());
                if (values.size () >= 3)
                    spec.dynamics.links[currentLink].cog = {{values[0], values[1], values[2]}};
            }
            // 分支 C3: 处理 <EstimateInertia /> 自动估算惯量标记
            else if (name == QLatin1String ("EstimateInertia") && currentLink >= 0) {
                spec.dynamics.links[currentLink].estimateInertia = true;
            }
            // 分支 C4: 处理 <Inertia> 惯性张量矩阵 (Inertia Tensor)
            else if (name == QLatin1String ("Inertia") && currentLink >= 0) {
                const std::vector< double > values = parseDoubles (xml.readElementText ());
                
                // 情况 1: RobWork 导出的 3x3 行优先展开的 9 元素对称矩阵:
                // [ Ixx, Ixy, Ixz,
                //   Ixy, Iyy, Iyz,
                //   Ixz, Iyz, Izz ]
                if (values.size () >= 9) {
                    // 提取并重构为 6 元素独立分量格式: {Ixx, Iyy, Izz, Ixy, Ixz, Iyz}
                    spec.dynamics.links[currentLink].inertia =
                        {{values[0], values[4], values[8], values[1], values[2], values[5]}};
                    spec.dynamics.links[currentLink].estimateInertia = false; // 显式提供了惯量，关闭自动估算
                }
                // 情况 2: 直接提供的 6 元素对称分量: {Ixx, Iyy, Izz, Ixy, Ixz, Iyz}
                else if (values.size () >= 6) {
                    spec.dynamics.links[currentLink].inertia =
                        {{values[0], values[1], values[2], values[3], values[4], values[5]}};
                    spec.dynamics.links[currentLink].estimateInertia = false;
                }
            }
            // 分支 D: 处理 <MaterialID> 材质名称节点
            else if (name == QLatin1String ("MaterialID")) {
                const std::string material = xml.readElementText ().trimmed ().toStdString ();
                if (currentLink >= 0)
                    // 若在 <Link> 内部，赋值给当前连杆的材质 (如 "Aluminum")
                    spec.dynamics.links[currentLink].material = material;
                else if (inKinematicBase)
                    // 若在 <KinematicBase> 内部，赋值给基座的材质 (如 "Steel")
                    spec.dynamics.baseMaterial = material;
            }
        }
        // 处理结束标签 (EndElement)，维护上下文状态
        else if (xml.isEndElement ()) {
            if (xml.name () == QLatin1String ("KinematicBase"))
                inKinematicBase = false; // 离开基座节点
            else if (xml.name () == QLatin1String ("Link"))
                currentLink = -1;        // 离开连杆节点，重置 currentLink 标记
        }
    }

    // ---- 4. 异常检查 ----
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
