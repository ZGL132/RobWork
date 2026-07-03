// =============================================================================
//  文件: RobotModelXmlWriterTest.cpp
//  说明: RobotModelXmlWriter 的命令行测试可执行文件(无 GUI 依赖)。
//        不依赖 Google Test,仅用一组断言式的 fail() 返回错误码:
//          - 退出码 0:全部通过;
//          - 退出码 1:任意一条 fail() 触发。
//        覆盖内容:
//          1) 默认数据能 validate;
//          2) SerialDevice XML 包含必要节点(RPY 转换、限位、位姿、Drawable);
//          3) DH 模式 XML 用 <DHJoint> 替代 <Joint>;
//          4) DWC XML 包含 ForceLimit/Link/Mass/COG/Inertia;
//          5) computeLinkPose 在 RPY+Pos 与 DH 两种模式下都给出正确结果;
//          6) 非法输入(零质量/零力限/含空格的名字/重复关节名)被 validate 拦截;
//          7) 把生成的 XML 落到 temp 目录,供人工检查。
// =============================================================================
#include "RobotModelXmlWriter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <cmath>
#include <iostream>

using namespace rws;

namespace {
// -----------------------------------------------------------------------------
//  测试辅助:打印错误并返回 1
// -----------------------------------------------------------------------------
int fail (const QString& message)
{
    std::cerr << message.toStdString () << std::endl;
    return 1;
}

/// 文本是否包含子串(简化 QString::contains 调用)
bool contains (const QString& text, const QString& needle)
{
    return text.contains (needle);
}
}    // namespace

// =============================================================================
//  main
//  说明: 测试入口。所有断言失败都用 fail() 立即返回 1;全部通过则返回 0。
// =============================================================================
int main (int argc, char** argv)
{
    QCoreApplication app (argc, argv);

    // ---- 默认模型基础校验 ----
    RobotModelSpec spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());

    QStringList errors;
    if (!RobotModelXmlWriter::validate (spec, errors))
        return fail ("Default model did not validate: " + errors.join ("; "));

    const QString serialXml = RobotModelXmlWriter::makeSerialDeviceXml (spec);
    const QString sceneXml  = RobotModelXmlWriter::makeSceneXml (spec);

    // SerialDevice XML 必须包含必要的根节点、Base/TCP、6 个 Joint
    if (!contains (serialXml, "<SerialDevice name=\"GenericSixAxis\">"))
        return fail ("Serial XML missing SerialDevice root.");
    if (!contains (serialXml, "<Frame name=\"Base\">"))
        return fail ("Serial XML missing Base frame.");
    if (!contains (serialXml, "<Frame name=\"TCP\""))
        return fail ("Serial XML missing TCP frame.");
    if (serialXml.count ("<Joint name=\"") != 6)
        return fail ("Serial XML must contain six explicit Joint elements.");
    // 默认 RPY 应当把 DH 的 alpha 翻成 RobWork 的 Z-Y-X 顺序
    if (!contains (serialXml,
                   "<Joint name=\"Joint2\" type=\"Revolute\">\n"
                   "    <RPY>0 0 90</RPY>\n"
                   "    <Pos>0.12 0 0</Pos>"))
        return fail ("Joint RPY+Pos defaults should convert DH alpha to RobWork RPY Z-Y-X order.");
    // 默认 Drawable 不应该再包含坐标轴几何(老版本遗留)
    if (contains (serialXml, "<Drawable name=\"Joint1Axis\""))
        return fail ("Default drawables should not include coordinate-axis geometry.");
    // 默认应包含关节外壳
    if (!contains (serialXml, "<Drawable name=\"Joint1Housing\""))
        return fail ("Serial XML missing default joint housing drawable.");
    // 默认应包含关节限位
    if (!contains (serialXml, "<PosLimit refjoint=\"Joint1\""))
        return fail ("Serial XML missing position limit.");
    // 默认不应包含 "Home"(RobWork 会把 Home 当作初始 state,容易引发混淆)
    if (contains (serialXml, "<Q name=\"Home\">"))
        return fail ("Default model should not use Home because RobWork loads it as the initial state.");
    // Ready 位姿应当转为弧度:0, -pi/2, pi/2, 0, 0, 0
    if (!contains (serialXml, "<Q name=\"Ready\">0 -1.5707963267949 1.5707963267949 0 0 0</Q>"))
        return fail ("Ready pose was not converted to radians.");
    // Scene XML 应通过 <Include> 引用机器人文件
    if (!contains (sceneXml, "<Include file=\"GenericSixAxis.wc.xml\" />"))
        return fail ("Scene XML missing include.");

    // ---- DH 模式 XML 验证 ----
    RobotModelSpec dhSpec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    dhSpec.mode           = RobotModelMode::DH;
    const QString dhXml   = RobotModelXmlWriter::makeSerialDeviceXml (dhSpec);
    if (dhXml.count ("<DHJoint name=\"") != 6)
        return fail ("DH XML must contain six DHJoint elements.");
    // Base + Joint1..Joint6 + TCP = 8 个坐标系应开启 ShowFrameAxis
    if (dhXml.count ("<Property name=\"ShowFrameAxis\">true</Property>") != 8)
        return fail ("DH XML should show frame axes for Base, Joint1-6, and TCP.");
    // DHJoint 的子节点应是 ShowFrameAxis
    if (!contains (dhXml,
                   "<DHJoint name=\"Joint1\" alpha=\"0\" a=\"0\" d=\"0.35\" offset=\"0\" "
                   "type=\"schilling\">\n"
                   "    <Property name=\"ShowFrameAxis\">true</Property>\n"
                   "  </DHJoint>"))
        return fail ("DH XML should emit ShowFrameAxis inside DHJoint elements.");

    // ---- DWC 模式验证 ----
    spec.dynamics.generateDynamicWorkCell = true;
    if (!RobotModelXmlWriter::validate (spec, errors))
        return fail ("Default DWC model did not validate: " + errors.join ("; "));

    const QString dwcXml = RobotModelXmlWriter::makeDynamicWorkCellXml (spec);
    if (!contains (dwcXml, "<DynamicWorkCell workcell=\"GenericSixAxisScene.wc.xml\">"))
        return fail ("DWC XML missing DynamicWorkCell root with workcell attribute.");
    if (!contains (dwcXml, "<RigidDevice device=\"GenericSixAxis\">"))
        return fail ("DWC XML missing RigidDevice.");
    if (!contains (dwcXml, "<KinematicBase frame=\"Base\">"))
        return fail ("DWC XML missing KinematicBase.");
    if (!contains (dwcXml, "<Link object=\"Joint1\">"))
        return fail ("DWC XML missing Joint1 link.");
    if (!contains (dwcXml, "<Mass>"))
        return fail ("DWC XML missing Mass.");
    if (!contains (dwcXml, "<COG>"))
        return fail ("DWC XML missing COG.");
    if (!contains (dwcXml, "<Inertia>"))
        return fail ("DWC XML missing Inertia.");
    if (dwcXml.count ("<ForceLimit joint=\"Joint") != 6)
        return fail ("DWC XML must contain six ForceLimit entries.");
    if (!contains (dwcXml, "<MaterialID>Steel</MaterialID>"))
        return fail ("DWC XML missing base MaterialID.");

    // ---- 斜向关节:计算 Link1To2 长度与位置 ----
    RobotModelSpec skewed = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    skewed.transformJoints[1].pos = {{0.3, 0, 0.4}};
    RobotModelXmlWriter::applyLinkGeometry (skewed);
    bool foundLink = false;
    for (const DrawableSpec& d : skewed.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundLink = true;
            const double expectedLength = std::sqrt (0.3 * 0.3 + 0.4 * 0.4);
            if (std::abs (d.length - expectedLength) > 1e-6)
                return fail (QString ("Link1To2 length should be ~%1, got %2")
                                 .arg (expectedLength)
                                 .arg (d.length));
            if (std::abs (d.pos[0] - 0.15) > 1e-6 || std::abs (d.pos[2] - 0.20) > 1e-6)
                return fail (QString ("Link1To2 pos should be 0.15 0 0.20, got %1 %2 %3")
                                 .arg (d.pos[0])
                                 .arg (d.pos[1])
                                 .arg (d.pos[2]));
            // 不应该再被 X/Z 轴硬编码 RPY 顶住(必须真的旋转到斜向)
            const bool isHardcodedX =
                std::abs (d.rpyDeg[0]) < 1e-6 && std::abs (d.rpyDeg[1] - 90.0) < 1e-6 &&
                std::abs (d.rpyDeg[2]) < 1e-6;
            const bool isHardcodedZ =
                std::abs (d.rpyDeg[0]) < 1e-6 && std::abs (d.rpyDeg[1]) < 1e-6 &&
                std::abs (d.rpyDeg[2]) < 1e-6;
            if (isHardcodedX || isHardcodedZ)
                return fail ("Link1To2 RPY still hardcoded for diagonal case.");
            break;
        }
    }
    if (!foundLink)
        return fail ("Could not find Link1To2 drawable.");

    // ---- 标准 DH 模式:Link4To5 应是纯 +Z 方向(因为 Joint4->Joint5 在默认数据里只有 d=0.38) ----
    RobotModelSpec standardDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    standardDh.mode           = RobotModelMode::DH;
    RobotModelXmlWriter::applyLinkGeometry (standardDh);
    bool foundDhLink = false;
    for (const DrawableSpec& d : standardDh.drawables) {
        if (QString::fromStdString (d.name) == "Link4To5") {
            foundDhLink = true;
            if (std::abs (d.length - 0.38) > 1e-6)
                return fail (QString ("Link4To5 length should be 0.38 in DH mode, got %1")
                                 .arg (d.length));
            if (std::abs (d.pos[0]) > 1e-6 || std::abs (d.pos[1]) > 1e-6 ||
                std::abs (d.pos[2] - 0.19) > 1e-6)
                return fail (
                    QString ("Link4To5 pos should be 0 0 0.19 in standard DH, got %1 %2 %3")
                        .arg (d.pos[0])
                        .arg (d.pos[1])
                        .arg (d.pos[2]));
            break;
        }
    }
    if (!foundDhLink)
        return fail ("Could not find Link4To5 drawable in DH mode.");

    // ---- DH 偏移:把 Joint2 设为 a=0.4, d=0.2, offset=90°,验证 xy/z 投影 ----
    RobotModelSpec offsetDh = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    offsetDh.mode           = RobotModelMode::DH;
    offsetDh.dhJoints[1].alphaDeg  = 0;
    offsetDh.dhJoints[1].a         = 0.4;
    offsetDh.dhJoints[1].d         = 0.2;
    offsetDh.dhJoints[1].offsetDeg = 90;
    RobotModelXmlWriter::applyLinkGeometry (offsetDh);
    bool foundOffsetLink = false;
    for (const DrawableSpec& d : offsetDh.drawables) {
        if (QString::fromStdString (d.name) == "Link1To2") {
            foundOffsetLink = true;
            const double expectedLength = std::sqrt (0.4 * 0.4 + 0.2 * 0.2);
            if (std::abs (d.length - expectedLength) > 1e-6)
                return fail ("Link1To2 length should follow standard DH XY/Z projection.");
            if (std::abs (d.pos[0]) > 1e-6 || std::abs (d.pos[1] - 0.2) > 1e-6 ||
                std::abs (d.pos[2] - 0.1) > 1e-6)
                return fail (
                    QString ("Link1To2 pos should be 0 0.2 0.1 for 90 deg offset, got %1 %2 %3")
                        .arg (d.pos[0])
                        .arg (d.pos[1])
                        .arg (d.pos[2]));
            break;
        }
    }
    if (!foundOffsetLink)
        return fail ("Could not find Link1To2 drawable for offset DH case.");

    // ---- 自动 Drawable 应跟随关节几何重算,而不是用用户改的值 ----
    RobotModelSpec autoDrawable = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    autoDrawable.drawables[6].length = 0.33;
    autoDrawable.drawables[6].rpyDeg = {{10, 20, 30}};
    autoDrawable.drawables[6].pos    = {{0.11, 0.22, 0.33}};
    RobotModelXmlWriter::applyLinkGeometry (autoDrawable);
    if (std::abs (autoDrawable.drawables[6].length - 0.12) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].rpyDeg[1] - 90) > 1e-6 ||
        std::abs (autoDrawable.drawables[6].pos[0] - 0.06) > 1e-6)
        return fail ("Auto Link1To2 drawable geometry should be derived from kinematics.");

    // ---- 非法输入应被 validate 拦截 ----
    RobotModelSpec badMass = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badMass.dynamics.generateDynamicWorkCell = true;
    badMass.dynamics.links[0].mass = 0;
    if (RobotModelXmlWriter::validate (badMass, errors))
        return fail ("Zero mass should fail validation.");

    RobotModelSpec badForce = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badForce.dynamics.generateDynamicWorkCell = true;
    badForce.dynamics.forceLimits[0].maxForce = 0;
    if (RobotModelXmlWriter::validate (badForce, errors))
        return fail ("Zero force limit should fail validation.");

    RobotModelSpec badName = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    badName.robotName = "My Robot";      // 含空格,需要被清洗
    if (RobotModelXmlWriter::validate (badName, errors))
        return fail ("Robot names requiring sanitization should fail validation.");

    RobotModelSpec noDrawables = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    noDrawables.generateDrawables = false;
    noDrawables.drawables[0].radius = 0;  // 即使几何非法,未启用 Drawable 时也应通过
    if (!RobotModelXmlWriter::validate (noDrawables, errors))
        return fail ("Disabled drawables should not be validated: " + errors.join ("; "));

    // 重复关节名应被拒绝
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.transformJoints[1].name = spec.transformJoints[0].name;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Duplicate joint name should fail validation.");

    // 启用 Drawable 后,几何非法应被拒绝
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (QDir::tempPath ());
    spec.drawables[0].radius = 0;
    if (RobotModelXmlWriter::validate (spec, errors))
        return fail ("Zero drawable radius should fail validation.");

    // ---- 把生成的 XML 落到 temp 目录,方便人工核对 ----
    const QString dumpDir = QDir::tempPath () + "/robotmodelbuilder_dump";
    QDir ().mkpath (dumpDir);
    std::cerr << "Dumping to: " << dumpDir.toStdString () << std::endl;
    spec = RobotModelXmlWriter::makeDefaultSixAxisModel (dumpDir);
    spec.dynamics.generateDynamicWorkCell = true;
    if (!RobotModelXmlWriter::saveFiles (spec, errors))
        return fail ("saveFiles failed in dump step: " + errors.join ("; "));
    std::cerr << "Wrote:\n  " << RobotModelXmlWriter::serialDeviceFilePath (spec).toStdString ()
              << "\n  " << RobotModelXmlWriter::sceneFilePath (spec).toStdString () << "\n  "
              << RobotModelXmlWriter::dynamicWorkCellFilePath (spec).toStdString () << std::endl;

    // 把"斜向关节"模型也落到磁盘,方便单独对比
    const QString skewedDir = QDir::tempPath () + "/robotmodelbuilder_skewed";
    QDir ().mkpath (skewedDir);
    RobotModelSpec skewedSave = skewed;
    skewedSave.saveDirectory = skewedDir.toStdString ();
    skewedSave.robotName = "SkewedRobot";
    if (!RobotModelXmlWriter::saveFiles (skewedSave, errors))
        return fail ("saveFiles failed for skewed: " + errors.join ("; "));
    std::cerr << "Wrote skewed:\n  "
              << RobotModelXmlWriter::serialDeviceFilePath (skewedSave).toStdString () << std::endl;

    return 0;
}