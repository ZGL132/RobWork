// =============================================================================
//  文件: RobotModelXmlWriter.hpp
//  说明: 把 RobotModelSpec 序列化为 RobWork / RobWorkSim 能识别的 XML 文件的
//        静态工具类。核心职责:
//          1) makeDefaultSixAxisModel : 构造一个 6 轴机器人的"出厂默认"数据
//          2) makeSerialDeviceXml     : 生成 <SerialDevice>...</SerialDevice>
//          3) makeSceneXml            : 生成场景容器 <WorkCell>...</WorkCell>
//          4) makeDynamicWorkCellXml  : 生成动力学 .dwc.xml
//          5) saveFiles               : 把上面三类写到磁盘
//          6) computeLinkPose / applyLinkGeometry
//                                     : 根据关节几何自动计算连杆圆柱
//                                       (中心位置、姿态、长度)
//        该类无状态,所有方法均为 static。
// =============================================================================
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
    /// 公开的圆周率常量,供外部代码(测试、UI)做度/弧度换算;
    /// 之前只放在匿名命名空间,测试无法访问。
    static constexpr double kPi = 3.14159265358979323846;

    /// 构造一份"通用 6 轴机器人"的默认 RobotModelSpec,所有关节/限位/Drawable/动力学都填好
    static RobotModelSpec makeDefaultSixAxisModel (const QString& saveDirectory);

    /// 把用户输入的机器人名清洗成可作为文件名的安全字符串(只保留字母/数字/_/-)
    static QString sanitizeFileBaseName (const QString& name);

    /// 校验 RobotModelSpec 是否合法,把错误信息追加到 errors 中;返回 true 表示通过
    static bool validate (const RobotModelSpec& spec, QStringList& errors);

    static bool canExportDhJoints (const RobotModelSpec& spec, QStringList* errors = nullptr);

    /// 生成 SerialDevice XML(<SerialDevice>...</SerialDevice>)
    static QString makeSerialDeviceXml (const RobotModelSpec& spec);

    /// 生成 Scene XML(WorkCell 容器,内含 <Include>)
    static QString makeSceneXml (const RobotModelSpec& spec);

    /// 生成 DynamicWorkCell XML(动力学配置文件)
    static QString makeDynamicWorkCellXml (const RobotModelSpec& spec);

    /// SerialDevice XML 的最终落盘路径(saveDirectory / robotName.wc.xml)
    static QString serialDeviceFilePath (const RobotModelSpec& spec);

    /// Scene XML 的最终落盘路径(saveDirectory / robotNameScene.wc.xml)
    static QString sceneFilePath (const RobotModelSpec& spec);

    /// DynamicWorkCell XML 的最终落盘路径(saveDirectory / robotName.dwc.xml)
    static QString dynamicWorkCellFilePath (const RobotModelSpec& spec);

    /// 校验 + 把 XML 写入磁盘;失败时把错误信息追加到 errors 中
    static bool saveFiles (const RobotModelSpec& spec, QStringList& errors);

    /// 计算从 joint_i 到 joint_{i+1} 的连杆圆柱姿态(中心位置、RPY、长度)
    /// @param spec     : 完整模型数据,从中读取关节信息
    /// @param linkIndex: 0..(transformJoints.size() - 2)
    ///                   对应连杆 i (Joint_{i+1} -> Joint_{i+2})
    /// @param posOut   : [out] 圆柱中心位置(米),在 Joint_{i+1} 坐标系下
    /// @param rpyDegOut: [out] 圆柱姿态 RPY(度,Z-Y-X 顺序)
    /// @param lengthOut: [out] 圆柱长度(米)
    /// 现在支持可变关节数:link 数 = transformJoints.size() - 1。
    static void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                                 std::array< double, 3 >& posOut,
                                 std::array< double, 3 >& rpyDegOut,
                                 double& lengthOut);

    /// 设备"可动关节"数量 = transformJoints 中 Revolute / Prismatic 行数。
    /// Q 维度、PosLimit/VelLimit/AccLimit 都按这个数量输出。
    /// Milestone 1 起,F1xedFrame / ToolFrame 不计入可动关节。
    static int movableJointCount (const RobotModelSpec& spec);

    /// 默认画几何(外壳 + 连杆)按"当前可动关节数 + RigidFrame 数"重组:
    ///   - 每个 transformJoints[i] 一个 Joint{i+1}Housing
    ///   - 每对相邻 transformJoints[i] / [i+1] 一个 Link{i+1}To{i+2}
    /// 即使 RigidFrame 出现在中间也会分配外壳,但它的 Link{i}To{i+1} 长度仍
    /// 由 transformJoints[i+1].pos 决定;RigidFrame 不可动,不影响 Q。
    static void applyDefaultDrawables (RobotModelSpec& spec,
                                       double paddingBeforeFirst = 0.0);

    /// 重新计算所有 autoLinkGeometry=true 的 Link{i}To{i+1} Drawable 的 pos/rpy/length
    /// 一般在保存 XML 前调用一次,使连杆几何随用户改关节参数自动同步
    static void applyLinkGeometry (RobotModelSpec& spec);

    // -------------------------------------------------------------------------
    //  DH projection / optional DH input conversion
    //  说明: SE(3) Joint+RPY+Pos 是唯一真值。DH 在数据模型中只是"投影视图
    //        缓存",在 UI 中是只读表。本节里的两个 API 用于维护这种关系:
    //          * refreshDhProjectionFromTransform : 从真值刷新 DH 投影缓存;
    //                                                唯一允许从真值单向往 DH
    //                                                写入的路径;
    //          * applyDhInputToTransform         : 把 DH 行当作"输入源"反向
    //                                                写入 SE(3) 真值;**会改写
    //                                                真值**,仅作为内部"Apply DH"
    //                                                工具,UI 默认不调用,仅
    //                                                在极端回填场景使用。
    //  本插件的 SE(3) <-> DH 约定:
    //          roll  = offsetDeg
    //          yaw   = alphaDeg
    //          pitch = 0   (DH 没有独立的 pitch 项)
    //          pos   = (a*cos(offsetDeg), a*sin(offsetDeg), d)
    //  当 pitch 非零,或 roll 与 Pos 的 xy 方向不一致时,投影有损。
    // -------------------------------------------------------------------------

    /// DH 关节 -> Joint+RPY+Pos。`existingType` 用来保留用户原来设置的
    /// 关节类型(例如 Prismatic),为空时默认 "Revolute"。
    static JointTransformSpec dhJointToTransform (const DHJointSpec& dh,
                                                   const std::string& existingType = std::string ());

    /// Joint+RPY+Pos -> DH 关节。
    /// 反向求解:从 (px, py) 反推 (a, offset),d 直接取 pz,yaw 作为 alpha。
    /// @param lossy [out, 可空] 若非空,当转换有损时被置为 true;
    ///              当前实现下"有损"=Transform 行的 pitch(rpyDeg[1]) 非零,
    ///              或 roll(rpyDeg[0]) 与 atan2(pos.y,pos.x) 不一致。
    static DHJointSpec transformJointToDh (const JointTransformSpec& joint, bool* lossy = nullptr);

    /// 从 SE(3) 真值刷新 DH 投影视图缓存(逐行)。两侧长度不一致时取较小值,
    /// 不做插入/删除。这是真值 -> 投影 的唯一允许路径;UI 中"Transform 表
    /// 编辑后立刻刷新 DH 表"就是用这个 API。
    static void refreshDhProjectionFromTransform (RobotModelSpec& spec);

    /// "Apply DH" 工具:把 spec.dhJoints 全部按行重写到 spec.transformJoints。
    /// 保留 transformJoints[i].type。两侧长度不一致时取较小值,不做插入/删除。
    /// **警告**:此函数会改写 SE(3) 真值,只能作为内部"Apply DH input"工具
    /// 使用;Widget 当前的 UI 不调用此函数(用户改 DH 表不会反向污染真值)。
    static void applyDhInputToTransform (RobotModelSpec& spec);

  private:
    /// 浮点数的统一序列化格式:有效数字 15 位,既保证精度又避免太长
    static QString number (double value);
    /// 把 std::array<double,3> 序列化为 "x y z" 字符串
    static QString vector3 (const std::array< double, 3 >& values);
    /// 度 -> 弧度(关节限位/位姿写入 XML 前都要换算)
    static double degToRad (double value);
};

}    // namespace rws

#endif
