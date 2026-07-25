#ifndef RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP
#define RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP

#include "RobotModelSpec.hpp"

#include <QStringList>

#include <string>

// 前向声明 RobWork 相关核心类，减少头文件依赖
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

/**
 * @brief WorkCell 场景转换器类
 * @details 负责将 RobWork 内存中的 WorkCell 场景对象及源 XML 文件信息，
 *          反向转换与反序列化为插件使用的纯数据结构 RobotModelSpec。
 */
class WorkCellConverter
{
  public:
    /**
     * @brief 核心主转换函数：将 WorkCell 转换为 RobotModelSpec
     * @param workcell 内存中的 RobWork WorkCell 对象
     * @param state 场景的默认状态 (用于获取当前位姿等)
     * @param saveDirectory 保存/保存目录 (可空，为空时自动推导)
     * @param warnings [out] 转换过程中的警告信息输出
     * @return RobotModelSpec 填充完成的机器人模型规范对象
     */
    static RobotModelSpec convert (const rw::models::WorkCell& workcell,
                                   const rw::kinematics::State& state,
                                   const std::string& saveDirectory,
                                   QStringList& warnings);

    /// 检查 WorkCell 中是否包含可转换的机器人设备 (JointDevice)
    static bool hasSerialDevice (const rw::models::WorkCell& workcell);
    
    /// 推导 WorkCell 的源文件磁盘路径
    static std::string inferWorkCellFilePath (const rw::models::WorkCell& workcell);
    
    /// 推导保存目录
    static std::string inferSaveDirectory (const rw::models::WorkCell& workcell);
    
    /// 检查 spec 是否包含可转换的机器人数据 (包含名称且关节非空)
    static bool hasConvertibleRobotModel (const RobotModelSpec& spec);

  private:
    // ---- 运行时 WorkCell C++ 对象数据提取私有辅助函数 ----
    
    /// 从 WorkCell 提取主串联机器人设备 (SerialDevice)
    static bool extractSerialDevice (const rw::models::WorkCell& workcell,
                                     RobotModelSpec& spec,
                                     QStringList& warnings);
                                     
    /// 提取关节 (Joints) 的坐标系变换与类型
    static void extractJoints (const rw::models::JointDevice& device,
                               RobotModelSpec& spec,
                               QStringList& warnings);
                               
    /// 提取关节限位 (位置、速度、加速度限位)
    static void extractLimits (const rw::models::JointDevice& device,
                               RobotModelSpec& spec);
                               
    /// 提取预设关节位姿构型 (Q / Poses)
    static void extractQConfigs (const rw::models::JointDevice& device,
                                 RobotModelSpec& spec);
                                 
    /// 提取场景中的非设备参考系 (Scene Frames，如 Table, Workpiece)
    static void extractSceneFrames (const rw::models::WorkCell& workcell,
                                    const rw::kinematics::State& state,
                                    RobotModelSpec& spec);
                                    
    /// 提取基本 Drawable 绘制对象
    static void extractDrawables (const rw::models::WorkCell& workcell,
                                  RobotModelSpec& spec,
                                  QStringList& warnings);
                                  
    /// 提取碰撞矩阵配置 (CollisionSetup)
    static void extractCollisionSetup (const rw::models::WorkCell& workcell,
                                       RobotModelSpec& spec);
                                       
    /// 提取临近查询配置 (ProximitySetup)
    static void extractProximitySetup (const rw::models::WorkCell& workcell,
                                       RobotModelSpec& spec);

    // ---- 源 XML 文件扫描与伴生文件融合 private 方法 ----

    /// 尝试读取侧车 JSON 配置文件 (.rmb.json)
    static bool tryLoadSidecar (const rw::models::WorkCell& workcell,
                                const std::string& saveDirectory,
                                RobotModelSpec& spec,
                                QStringList& warnings);
                                
    /// 扫描主 WorkCell XML，提取 <Include>、CollisionSetup、ProximitySetup 文件引用
    static void mergeCompanionXmlMetadata (const rw::models::WorkCell& workcell,
                                           RobotModelSpec& spec,
                                           QStringList& warnings);
                                           
    /// 解析并融合 CollisionSetup.xml
    static void mergeCollisionSetupXml (const QString& file, RobotModelSpec& spec,
                                        QStringList& warnings);
                                        
    /// 解析并融合 ProximitySetup.xml
    static void mergeProximitySetupXml (const QString& file, RobotModelSpec& spec,
                                        QStringList& warnings);
                                        
    /// 解析并融合 DynamicWorkCell (.dwc.xml) 动力学配置文件
    static void mergeDynamicWorkCellXml (const QString& file, RobotModelSpec& spec,
                                         QStringList& warnings);

    // ---- 通用工具私有函数 ----

    /// 将 Transform3D 齐次变换矩阵拆转为 RPY (角度) 和 Pos (米)
    static void transformToRpyPos (const rw::math::Transform3Dd& t,
                                   std::array< double, 3 >& rpyDeg,
                                   std::array< double, 3 >& pos);
                                   
    /// 检查 Frame 上是否设置了 ShowFrameAxis 属性
    static bool hasShowFrameAxes (const rw::kinematics::Frame& frame);
    
    /// 检查 Frame 是否为动态附着参考系 (DAF)
    static bool isDAF (const rw::kinematics::Frame* frame,
                       const rw::kinematics::State& state);
};

}    // namespace rws

#endif    // RWS_ROBOTMODELBUILDER_WORKCELLCONVERTER_HPP