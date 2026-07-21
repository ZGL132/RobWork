// =============================================================================
//  文件: RobotModelXmlWriter.hpp
//  说明: 将 RobotModelSpec 序列化为 RobWork / RobWorkSim 可识别 XML 文件的无状态静态工具类。
//
//  核心职责:
//    1) makeDefaultSixAxisModel : 构建 6 轴机器人的默认模型配置数据。
//    2) makeSerialDeviceXml     : 生成串行设备 XML (<SerialDevice>...</SerialDevice>)。
//    3) makeSceneXml            : 生成场景容器 XML (<WorkCell>...</WorkCell>)。
//    4) makeDynamicWorkCellXml  : 生成动力学配置文件 (.dwc.xml)。
//    5) makeCollision/Proximity : 生成碰撞与邻近度配置文件 (.xml)。
//    6) saveFiles               : 将上述配置文件统一保存至磁盘。
//    7) computeLinkPose / applyLinkGeometry : 依据关节位姿自动计算连杆几何参数（中心位置、姿态、长度）。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP
#define RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP

#include "RobotModelSpec.hpp"

#include <QString>
#include <QStringList>

#include <array>

class QTextStream;

namespace rws {

/**
 * @brief RobotModelSpec 的 XML 序列化与几何计算静态工具类。
 * @note 本类为无状态类，所有成员方法均为静态方法（static）。
 */
class RobotModelXmlWriter
{
  public:
    // =========================================================================
    //  常量与基础接口
    // =========================================================================

    /// 圆周率常量，供外部模块（如测试、UI 交互）进行角度/弧度转换
    static constexpr double kPi = 3.14159265358979323846;

    /**
     * @brief 构建通用 6 轴机器人的出厂默认配置数据（包含预设关节、限位、几何体及动力学参数）。
     * @param saveDirectory 保存的目标目录路径
     * @return 填充完毕的 RobotModelSpec 结构体
     */
    static RobotModelSpec makeDefaultSixAxisModel (const QString& saveDirectory);

    /**
     * @brief 将输入的机器人名称清洗为安全的文件名（仅保留字母、数字、下划线及连字符）。
     * @param name 原始机器人名称
     * @return 清洗后的安全文件名字符串
     */
    static QString sanitizeFileBaseName (const QString& name);

    /**
     * @brief 校验 RobotModelSpec 数据的合法性。
     * @param spec 待校验的模型配置
     * @param[out] errors 若校验失败，追加收集到的错误信息
     * @return 校验通过返回 true，否则返回 false
     */
    static bool validate (const RobotModelSpec& spec, QStringList& errors);

    /**
     * @brief 检查当前模型规范是否支持导出 DH 参数。
     * @param spec 模型配置
     * @param[out] errors [可选] 若不支持，追加收集到的原因说明
     * @return 支持导出返回 true，否则返回 false
     */
    static bool canExportDhJoints (const RobotModelSpec& spec, QStringList* errors = nullptr);

    // =========================================================================
    //  XML 字符串生成接口
    // =========================================================================

    /// 生成串行设备 XML 文本（根节点为 <SerialDevice>）
    static QString makeSerialDeviceXml (const RobotModelSpec& spec);

    /// 生成场景容器 XML 文本（根节点为 <WorkCell>，内部包含 <Include> 节点）
    static QString makeSceneXml (const RobotModelSpec& spec);

    /// 生成动力学 WorkCell XML 文本（用于 RobWorkSim 的配置文件）
    static QString makeDynamicWorkCellXml (const RobotModelSpec& spec);

    /**
     * @brief 生成碰撞检测配置 XML（CollisionSetup）。
     * @details 输出格式适配 RobWork 的 CollisionSetupLoader。
     *          会自动合并 spec 中指定的排除对与相邻关节对（若开启 excludeAdjacentLinkPairs）。
     */
    static QString makeCollisionSetupXml (const RobotModelSpec& spec);

    /**
     * @brief 生成邻近度检测配置 XML（ProximitySetup）。
     * @details 输出格式适配 RobWork 的 ProximitySetupLoader。
     */
    static QString makeProximitySetupXml (const RobotModelSpec& spec);

    // =========================================================================
    //  文件路径解析与落盘保存
    // =========================================================================

    /// 获取 SerialDevice XML 的完整目标路径 (saveDirectory / robotName.wc.xml)
    static QString serialDeviceFilePath (const RobotModelSpec& spec);

    /// 获取 Scene XML 的完整目标路径 (saveDirectory / robotNameScene.wc.xml)
    static QString sceneFilePath (const RobotModelSpec& spec);

    /// 获取 DynamicWorkCell XML 的完整目标路径 (saveDirectory / robotName.dwc.xml)
    static QString dynamicWorkCellFilePath (const RobotModelSpec& spec);

    /// 获取 CollisionSetup XML 的完整目标路径 (saveDirectory / collisionSetup.file)
    static QString collisionSetupFilePath (const RobotModelSpec& spec);

    /// 获取 ProximitySetup XML 的完整目标路径 (saveDirectory / proximitySetup.file)
    static QString proximitySetupFilePath (const RobotModelSpec& spec);

    /**
     * @brief 执行合法性校验并将所有生成的 XML 配置文件写入磁盘。
     * @param spec 模型配置
     * @param[out] errors 保存失败时的错误日志
     * @return 全部保存成功返回 true，否则返回 false
     */
    static bool saveFiles (const RobotModelSpec& spec, QStringList& errors);

    // =========================================================================
    //  运动学与几何图形计算
    // =========================================================================

    /**
     * @brief 计算从 Joint_{i+1} 到 Joint_{i+2} 的连杆圆柱几何姿态及尺寸。
     * @param spec 模型配置数据
     * @param linkIndex 连杆索引（范围: 0 至 transformJoints.size() - 2）
     * @param[out] posOut 在 Joint_{i+1} 坐标系下的圆柱中心位置 [x, y, z]（单位：米）
     * @param[out] rpyDegOut 圆柱姿态的 RPY 旋转角（Z-Y-X 顺序，单位：度）
     * @param[out] lengthOut 圆柱体的长度（单位：米）
     */
    static void computeLinkPose (const RobotModelSpec& spec, int linkIndex,
                                 std::array< double, 3 >& posOut,
                                 std::array< double, 3 >& rpyDegOut,
                                 double& lengthOut);

    /**
     * @brief 获取设备中的“可动关节”总数。
     * @details 仅计算 transformJoints 中类型为 Revolute 或 Prismatic 的关节，
     *          FixedFrame 与 ToolFrame 不计入其中。
     */
    static int movableJointCount (const RobotModelSpec& spec);

    /**
     * @brief 重组并应用默认的几何绘制项（外壳与连杆圆柱）。
     * @param[in,out] spec 待更新的模型配置
     * @param paddingBeforeFirst 首个关节基座处的附加偏移量
     */
    static void applyDefaultDrawables (RobotModelSpec& spec,
                                       double paddingBeforeFirst = 0.0);

    /**
     * @brief 重新计算所有标记为 `autoLinkGeometry=true` 的连杆几何属性（位置、姿态和长度）。
     * @note 建议在导出或保存 XML 前调用，以确保连杆几何与最新的关节参数同步。
     */
    static void applyLinkGeometry (RobotModelSpec& spec);

    // =========================================================================
    //  SE(3) 与 DH 参数转换工具
    //  -----------------------------------------------------------------------
    //  数据模型设计说明:
    //  1. SE(3) 变换参数（Joint + RPY + Pos）是系统的【唯一数据真值】。
    //  2. DH 参数在模型中仅作为“只读视图/缓存”存在。
    //  3. 转换约定:
    //       roll = offsetDeg, yaw = alphaDeg, pitch = 0 (DH 无 pitch)
    //       pos  = (a * cos(offset), a * sin(offset), d)
    //     若 Transform 中 pitch != 0 或 roll 与 pos 的 XY 方位不一致，转换即存在有损。
    // =========================================================================

    /**
     * @brief 将 DH 关节参数转换为 SE(3) 变换参数（Joint + RPY + Pos）。
     * @param dh 输入的 DH 参数结构体
     * @param existingType 保留的原始关节类型（如 "Prismatic"），若为空则默认设为 "Revolute"
     */
    static JointTransformSpec dhJointToTransform (const DHJointSpec& dh,
                                                  const std::string& existingType = std::string ());

    /**
     * @brief 将 SE(3) 变换参数反向求解为 DH 关节参数。
     * @param joint 关节变换参数
     * @param[out] lossy [可选] 若非空，当转换存在精度/信息损失时被置为 true
     */
    static DHJointSpec transformJointToDh (const JointTransformSpec& joint, bool* lossy = nullptr);

    /**
     * @brief 从 SE(3) 真值逐行刷新 DH 参数视图缓存。
     * @note 此函数为“真值 -> DH 视图”单向同步的唯一标准路径。
     */
    static void refreshDhProjectionFromTransform (RobotModelSpec& spec);

    /**
     * @brief 将 spec.dhJoints 的参数反向覆写回 spec.transformJoints（仅作为内部工具使用）。
     * @warning **此函数会直接修改 SE(3) 数据真值**。UI 默认不自动调用此接口，避免造成用户数据污染。
     */
    static void applyDhInputToTransform (RobotModelSpec& spec);

  private:
    // =========================================================================
    //  私有辅助接口（格式化与节点输出）
    // =========================================================================

    /// 浮点数格式化输出（保留 15 位有效数字）
    static QString number (double value);

    /// 将 3D 向量格式化为 "x y z" 字符串
    static QString vector3 (const std::array< double, 3 >& values);

    /// 将 4x4 矩阵（16 个元素）格式化为空格分隔的字符串（用于 <Transform> 节点）
    static QString vector16 (const std::array< double, 16 >& values);

    /// 生成 <Frame type="..." /> 的属性文本（若为 Normal 类型则返回空串）
    static QString frameTypeAttribute (SceneFrameType type);

    /// 将单个 FrameSpec 序列化写入 XML 流
    static void writeFrameXml (QTextStream& out, const FrameSpec& frame, bool showFrameAxes);

    /// 生成场景几何体 XML 节点文本
    static QString geometryShapeXml (const SceneGeometrySpec& geometry);
    static void writeSceneGeometryXml (QTextStream& out, const SceneGeometrySpec& geometry);

    /// 计算几何文件相对于保存目录的相对路径
    static QString relativeGeometryPath (const RobotModelSpec& spec,
                                         const std::string& filePath);

    /// 生成机器人本体 Drawable 的几何 XML 文本
    static QString drawableShapeXml (const RobotModelSpec& spec,
                                     const DrawableSpec& drawable);
    static void writeDrawableXml (QTextStream& out, const RobotModelSpec& spec,
                                  const DrawableSpec& drawable);

    /// 检查几何类型是否支持作为碰撞模型（排除 Plane / STL / Unknown）
    static bool isCollisionModelShapeSupported (GeometryKind kind);

    /// 生成碰撞模型 XML 节点文本
    static QString collisionShapeXml (const RobotModelSpec& spec,
                                      const CollisionModelSpec& collision);
    static void writeCollisionModelXml (QTextStream& out, const RobotModelSpec& spec,
                                        const CollisionModelSpec& collision);

    /// 角度转弧度
    static double degToRad (double value);

    /// 将绝对或相对路径转为相对于 spec.saveDirectory 的相对路径（用于 XML <Include> 引用）
    static QString relativeOutputPath (const RobotModelSpec& spec, const QString& filePath);

    /// 获取有效的碰撞排除对列表（合并用户自定义排除对与相邻关节排除对）
    static std::vector< FramePairSpec > effectiveCollisionExcludePairs (const RobotModelSpec& spec);
};

}    // namespace rws

#endif // RWS_ROBOTMODELBUILDER_ROBOTMODELXMLWRITER_HPP