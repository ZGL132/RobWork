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
    /// @param linkIndex: 0..JointCount-2,对应连杆 i (Joint_{i+1} -> Joint_{i+2})
    /// @param posOut   : [out] 圆柱中心位置(米),在 Joint_{i+1} 坐标系下
    /// @param rpyDegOut: [out] 圆柱姿态 RPY(度,Z-Y-X 顺序)
    /// @param lengthOut: [out] 圆柱长度(米)
    static void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                                 std::array< double, 3 >& posOut,
                                 std::array< double, 3 >& rpyDegOut,
                                 double& lengthOut);

    /// 重新计算所有 autoLinkGeometry=true 的 Link{i}To{i+1} Drawable 的 pos/rpy/length
    /// 一般在保存 XML 前调用一次,使连杆几何随用户改关节参数自动同步
    static void applyLinkGeometry (RobotModelSpec& spec);

    // -------------------------------------------------------------------------
    //  DH <-> Joint+RPY+Pos 双向转换
    //  说明: 用于支持 UI 中"DH 表"和"Joint+RPY+Pos 表"实时联动。
    //        本插件的约定(也是默认模型使用的):
    //          roll  = offsetDeg
    //          yaw   = alphaDeg
    //          pitch = 0   (DH 没有独立的 pitch 项)
    //          pos   = (a*cos(offsetDeg), a*sin(offsetDeg), d)
    //        因此反向转换(transformJointToDh)会把通用 RPY/Pos 投影成简化 DH。
    //        当 pitch 非零,或 roll 与 Pos 的 xy 方向不一致时,该投影有损。
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

    /// 把 spec.transformJoints 全部按 spec.dhJoints 重写(行数取两者较小值)。
    /// 保留原 transformJoints[i].type。两侧长度不一致时不做插入/删除。
    static void syncTransformJointsFromDh (RobotModelSpec& spec);

    /// 把 spec.dhJoints 全部按 spec.transformJoints 的投影结果重写(行数取两者较小值)。
    /// 两侧长度不一致时不做插入/删除。
    static void syncDhJointsFromTransform (RobotModelSpec& spec);

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
