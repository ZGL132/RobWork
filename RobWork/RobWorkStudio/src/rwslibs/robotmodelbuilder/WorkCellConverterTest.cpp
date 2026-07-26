#include "RobotModelXmlWriter.hpp"
#include "WorkCellConverter.hpp"

#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/WorkCell.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>

// 辅助打印错误信息并返回退出码 1 (表示测试失败)
static int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

static bool writeTextFile (const QString& path, const QString& text)
{
    QFileInfo info (path);
    if (!QDir ().mkpath (info.absolutePath ()))
        return false;
    QFile file (path);
    if (!file.open (QFile::WriteOnly | QFile::Text))
        return false;
    file.write (text.toUtf8 ());
    return true;
}

static QString readTextFile (const QString& path)
{
    QFile file (path);
    if (!file.open (QFile::ReadOnly | QFile::Text))
        return QString ();
    return QString::fromUtf8 (file.readAll ());
}

// 获取项目源码根目录路径（向上跳转 4 级目录），用于寻找 RobWork 内置的示例测试模型
static QString sourceRoot ()
{
    QDir dir (QFileInfo (__FILE__).absolutePath ());
    if (!dir.cdUp () || !dir.cdUp () || !dir.cdUp () || !dir.cdUp ())
        return QString ();
    return dir.absolutePath ();
}

int main ()
{
    // ---- 1. 创建临时的测试输出目录 ----
    QTemporaryDir dir;
    if (!dir.isValid ())
        return fail ("Could not create temporary directory.");

    // ---- 2. 构造一个标准的 6 轴机器人模型规范对象 (original) 作为测试基准 ----
    rws::RobotModelSpec original =
        rws::RobotModelXmlWriter::makeDefaultSixAxisModel (dir.path ());
    original.robotName = "RoundTripBot";
    original.proximitySetup.enabled = true; // 开启临近查询配置
    original.proximitySetup.useExcludeStaticPairs = true; // 开启排除静态对功能

    // 往基准模型中注入一个自定义的 Box Drawable 对象，用来测试几何属性是否会被无损还原
    rws::DrawableSpec importedDrawable;
    importedDrawable.name = "ImportedBox";
    importedDrawable.refFrame = "Joint1";
    importedDrawable.shape = "Box";
    importedDrawable.dimensions = {{1.25, 2.5, 3.75}};
    importedDrawable.rpyDeg = {{11.0, 22.0, 33.0}};
    importedDrawable.pos = {{0.12, 0.23, 0.34}};
    importedDrawable.rgb = {{0.15, 0.45, 0.75}};
    original.drawables.push_back (importedDrawable);

    const QString stlText =
        "solid rmb\n"
        "facet normal 0 0 1\n"
        "outer loop\n"
        "vertex 0 0 0\n"
        "vertex 1 0 0\n"
        "vertex 0 1 0\n"
        "endloop\n"
        "endfacet\n"
        "endsolid rmb\n";
    if (!writeTextFile (QDir (dir.path ()).filePath ("meshes/imported_tool.stl"), stlText) ||
        !writeTextFile (QDir (dir.path ()).filePath ("meshes/imported_scene.stl"), stlText))
        return fail ("Could not create mesh fixture files.");

    rws::DrawableSpec importedMesh;
    importedMesh.name = "ImportedToolMesh";
    importedMesh.refFrame = "Joint1";
    importedMesh.shape = "STL";
    importedMesh.filePath = "meshes/imported_tool.stl";
    importedMesh.radius = 0.01;
    importedMesh.length = 0.01;
    importedMesh.rgb = {{0.8, 0.2, 0.1}};
    original.drawables.push_back (importedMesh);

    // 注入一条 Proximity 规则
    {
        rws::ProximityRuleSpec rule;
        rule.kind = rws::ProximityRuleKind::Exclude;
        rule.patternA = "RoundTripBot.Joint.*";
        rule.patternB = "Table";
        original.proximitySetup.rules.push_back (rule);
    }
    original.collisionSetup.excludeStaticPairs = true; // 开启碰撞排除静态对

    // This pair cannot be represented by the automatic base/adjacent rules.
    // It must remain an explicit Imported pair after the scene is converted.
    rws::FramePairSpec importedToolPair;
    importedToolPair.first  = "TCP";
    importedToolPair.second = original.transformJoints.back ().name;
    importedToolPair.source = "Manual";
    original.collisionSetup.excludePairs.push_back (importedToolPair);

    // ---- 3. 将基准模型写盘生成 XML 文件群 ----
    QStringList saveErrors;
    if (!rws::RobotModelXmlWriter::saveFiles (original, saveErrors))
        return fail ("Could not save generated XML: " + saveErrors.join ("; "));

    // ---- 4. 使用 RobWork 的 WorkCellLoader 官方加载管线读取刚生成的场景 XML ----
    rw::models::WorkCell::Ptr wc =
        rw::loaders::WorkCellLoader::Factory::load (
            rws::RobotModelXmlWriter::sceneFilePath (original).toStdString ());
    if (wc == NULL)
        return fail ("WorkCellLoader returned null.");

    // ---- 5. 核心测试点 1：使用 WorkCellConverter 将内存中的 WorkCell 对象反向转换回 spec ----
    QStringList warnings;
    rws::RobotModelSpec imported =
        rws::WorkCellConverter::convert (*wc, wc->getDefaultState (),
                                          dir.path ().toStdString (), warnings);

    // ---- 6. 验证步骤：断言检查各种字段是否被 100% 正确还原 ----
    if (imported.robotName != original.robotName)
        return fail ("Robot name was not recovered.");
    if (imported.transformJoints.size () != original.transformJoints.size ())
        return fail ("Joint count was not recovered.");
    if (imported.limits.size () != original.limits.size ())
        return fail ("Joint limits were not recovered.");
    if (imported.generateScene != true)
        return fail ("Scene generation flag should be true for loaded scene.");
    if (!imported.proximitySetup.enabled)
        return fail ("ProximitySetup enable flag was not recovered.");
    if (QFileInfo (rws::RobotModelXmlWriter::proximitySetupFilePath (imported)).fileName () !=
        "ProximitySetup.xml")
        return fail ("ProximitySetup filename was not recovered.");
    if (!imported.proximitySetup.useExcludeStaticPairs)
        return fail ("ProximitySetup UseExcludeStaticPairs flag was not recovered.");
    if (imported.proximitySetup.rules.empty ())
        return fail ("ProximitySetup companion rules were not recovered.");
    if (imported.proximitySetup.rules[0].patternA != "Joint.*")
        return fail ("Device-scoped ProximitySetup pattern was not normalized.");
    if (!imported.collisionSetup.enabled)
        return fail ("CollisionSetup enable flag was not recovered.");
    if (QFileInfo (rws::RobotModelXmlWriter::collisionSetupFilePath (imported)).fileName () !=
        "CollisionSetup.xml")
        return fail ("CollisionSetup filename was not recovered.");
    if (!imported.collisionSetup.excludeStaticPairs)
        return fail ("CollisionSetup ExcludeStaticPairs flag was not recovered.");
    if (!imported.collisionSetup.excludeBaseToFirstJoint)
        return fail ("Imported Base-first exclusion should be normalized into the automatic rule.");
    if (!imported.collisionSetup.excludeAdjacentLinkPairs)
        return fail ("Imported complete adjacent chain should be normalized into the automatic rule.");
    if (imported.collisionSetup.excludePairs.size () != 1)
        return fail ("Only CollisionSetup pairs outside automatic rules should remain Imported.");
    const rws::FramePairSpec& retainedPair = imported.collisionSetup.excludePairs.front ();
    if (retainedPair.first != "TCP" ||
        retainedPair.second != original.transformJoints.back ().name ||
        retainedPair.source != "Imported")
        return fail ("TCP exclusion should remain as the sole Imported CollisionSetup pair.");

    // 检查第 2 步中注入的 "ImportedBox" 几何体各项参数 (尺寸、RPY、Pos、RGB、碰撞标记) 是否无损
    const auto importedDrawableIt = std::find_if (
        imported.drawables.begin (), imported.drawables.end (),
        [] (const rws::DrawableSpec& drawable) { return drawable.name == "ImportedBox"; });
    if (importedDrawableIt == imported.drawables.end ())
        return fail ("Imported drawable was not recovered.");
    if (importedDrawableIt->dimensions != importedDrawable.dimensions ||
        importedDrawableIt->rpyDeg != importedDrawable.rpyDeg ||
        importedDrawableIt->pos != importedDrawable.pos ||
        importedDrawableIt->rgb != importedDrawable.rgb)
        return fail ("Imported drawable geometry was not preserved.");

    // ---- 7. 核心测试点 2：验证自定义导入目标的二次写盘与加载 ----
    if (!imported.imported.active)
        return fail ("Imported document metadata was not set.");
    // 修改导入目标文件名（测试多级相对目录）
    imported.exportLayout.deviceFile = "imported/RobotDevice.wc.xml";
    imported.exportLayout.sceneFile = "imported/RobotScene.wc.xml";
    if (!rws::RobotModelXmlWriter::saveFiles (imported, saveErrors))
        return fail ("Could not save imported geometry: " + saveErrors.join ("; "));
    
    // 确保二次写盘生成的新相对路径文件确实存在
    if (!QFileInfo::exists (rws::RobotModelXmlWriter::serialDeviceFilePath (imported)) ||
        !QFileInfo::exists (rws::RobotModelXmlWriter::sceneFilePath (imported)))
        return fail ("Imported document targets were not written.");
    const QString resavedDeviceXml =
        readTextFile (rws::RobotModelXmlWriter::serialDeviceFilePath (imported));
    if (!resavedDeviceXml.contains ("file=\"../meshes/imported_tool.stl\""))
        return fail ("Imported device mesh path was not rebased for the new output directory.");
        
    // 尝试重新加载二次写盘后的 Scene XML，验证生成的 XML 语法无误
    rw::models::WorkCell::Ptr reloaded = rw::loaders::WorkCellLoader::Factory::load (
        rws::RobotModelXmlWriter::sceneFilePath (imported).toStdString ());
    if (reloaded == NULL)
        return fail ("Saved imported scene could not be loaded.");

    rws::SceneGeometrySpec sceneMesh;
    sceneMesh.name = "ImportedSceneMesh";
    sceneMesh.refFrame = "RobotBase";
    sceneMesh.kind = rws::GeometryKind::STL;
    sceneMesh.file = "meshes/imported_scene.stl";
    sceneMesh.radius = 0.01;
    sceneMesh.length = 0.01;
    imported.sceneGeometries.push_back (sceneMesh);
    if (!rws::RobotModelXmlWriter::saveFiles (imported, saveErrors))
        return fail ("Could not save imported scene mesh geometry: " + saveErrors.join ("; "));
    const QString resavedSceneXmlWithMesh =
        readTextFile (rws::RobotModelXmlWriter::sceneFilePath (imported));
    if (!resavedSceneXmlWithMesh.contains ("file=\"../meshes/imported_scene.stl\""))
        return fail ("Imported scene mesh path was not rebased for the new output directory.");

    // ---- 8. 核心测试点 3：测试侧车文件 (.rmb.json) 的最高优先权加载逻辑 ----
    rws::RobotModelSpec sidecarSpec = original;
    sidecarSpec.generateDrawables = false; // 故意修改一个开关属性
    rws::IncludeSpec sidecarInclude;
    sidecarInclude.file = "sidecar-only-extra.wc.xml";
    sidecarInclude.kind = rws::IncludeKind::WorkCell;
    sidecarSpec.includes.push_back (sidecarInclude);
    if (!rws::RobotModelXmlWriter::saveSpecSidecar (sidecarSpec, saveErrors))
        return fail ("Could not save generated sidecar: " + saveErrors.join ("; "));

    // 重新通过 convert 转换，此时同目录下存在 .rmb.json 侧车文件
    rws::RobotModelSpec importedWithSidecar =
        rws::WorkCellConverter::convert (*wc, wc->getDefaultState (),
                                          dir.path ().toStdString (), warnings);
    // 断言验证：convert 是否优先读取了 Sidecar 中的 generateDrawables=false 覆盖了内存提取值
    if (importedWithSidecar.generateDrawables)
        return fail ("Sidecar metadata was not used as the authoritative editable spec.");
    if (importedWithSidecar.includes.size () != 1 ||
        importedWithSidecar.includes[0].file != sidecarInclude.file)
        return fail ("Sidecar-only include metadata was overwritten by companion XML.");

    {
        QTemporaryDir brokenSourceDir;
        if (!brokenSourceDir.isValid ())
            return fail ("Could not create broken-source temporary directory.");
        rws::RobotModelSpec brokenSourceSpec =
            rws::RobotModelXmlWriter::makeDefaultSixAxisModel (brokenSourceDir.path ());
        brokenSourceSpec.robotName = "BrokenSourceBot";
        QStringList brokenSaveErrors;
        if (!rws::RobotModelXmlWriter::saveFiles (brokenSourceSpec, brokenSaveErrors))
            return fail ("Could not save broken-source fixture: " +
                         brokenSaveErrors.join ("; "));
        rw::models::WorkCell::Ptr brokenWc =
            rw::loaders::WorkCellLoader::Factory::load (
                rws::RobotModelXmlWriter::sceneFilePath (brokenSourceSpec).toStdString ());
        if (brokenWc == NULL)
            return fail ("Could not load broken-source fixture before removing XML.");
        if (!QFile::remove (rws::RobotModelXmlWriter::serialDeviceFilePath (brokenSourceSpec)))
            return fail ("Could not remove included device XML fixture.");
        QStringList brokenWarnings;
        rws::RobotModelSpec brokenImported =
            rws::WorkCellConverter::convert (*brokenWc, brokenWc->getDefaultState (),
                                              brokenSourceDir.path ().toStdString (),
                                              brokenWarnings);
        if (brokenImported.drawables.empty ())
            return fail ("Runtime drawables were discarded when source XML include was missing.");
    }

    // ---- 9. 核心测试点 4：测试真实的第三方复杂机器人模型 (RobWork 官方 UR 机械臂模型) ----
    const QString urFile =
        QDir (sourceRoot ()).filePath ("RobWork/example/ModelData/XMLDevices/"
                                      "UR-6-85-5-A/UR.wc.xml");
    rw::models::WorkCell::Ptr ur =
        rw::loaders::WorkCellLoader::Factory::load (urFile.toStdString ());
    if (ur == NULL)
        return fail ("Could not load UR-6-85-5-A fixture.");

    QStringList urWarnings;
    rws::RobotModelSpec urSpec =
        rws::WorkCellConverter::convert (*ur, ur->getDefaultState (),
                                          QFileInfo (urFile).absolutePath ().toStdString (),
                                          urWarnings);
    // 自动重算连杆姿态与几何
    rws::RobotModelXmlWriter::applyLinkGeometry (urSpec);
    
    // 对转换出来的 UR 机械臂 spec 执行合法性校验，验证不会产生找不到 Frame 等报错
    QStringList validationErrors;
    if (!rws::RobotModelXmlWriter::validate (urSpec, validationErrors))
        return fail ("Imported UR fixture should validate without unknown frame errors: " +
                     validationErrors.join ("; "));

    // 运行到此处说明所有断言全部通过
    std::cout << "WorkCellConverter smoke test passed." << std::endl;
    return 0;
}
