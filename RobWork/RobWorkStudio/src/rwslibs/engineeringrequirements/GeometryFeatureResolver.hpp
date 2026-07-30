#ifndef RWS_ENGINEERINGREQUIREMENTS_GEOMETRYFEATURERESOLVER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_GEOMETRYFEATURERESOLVER_HPP

#include "EngineeringRequirementTypes.hpp"

#include <array>
#include <string>

// 前向声明 RobWork 框架的核心运动学与场景类，避免包含庞大的第三方头文件，提高编译速度
namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

/**
 * @brief 几何特征解析结果结构体
 * 
 * 用于保存从 3D 场景（WorkCell）中运动学解析得出的相对位姿：
 * 包含了在指定参考坐标系（Reference Frame）下，目标几何特征的位置与欧拉角姿态。
 */
struct GeometryFeatureResolution {
    std::array<double, 3> position = {{0.0, 0.0, 0.0}}; ///< 相对位置坐标 [X, Y, Z]（单位：米）
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};   ///< 相对姿态欧拉角 [Roll, Pitch, Yaw]（单位：度）
};

/**
 * @brief 几何特征解析器 (Geometry Feature Resolver)
 * 
 * 该类作为纯静态工具类，负责将工程需求中的“几何特征绑定（GeometryFeatureReference）”
 * 与 RobWork 的 3D 场景图（WorkCell）及当前运动学状态（State）进行实时的运动学位姿解算与反写。
 */
class GeometryFeatureResolver {
public:
    /**
     * @brief 位姿解析函数：计算几何特征在参考坐标系下的相对位姿
     * 
     * 求解逻辑：
     * 1. 在 WorkCell 中查找目标几何特征的坐标系（Feature Frame）及指定的参考坐标系（Reference Frame）；
     * 2. 利用 RobWork 运动学接口计算从 Reference Frame 到 Feature Frame 的相对变换矩阵 $T_{ref \to feat}$；
     * 3. 将相对变换阵提取为位置平移量 $[X, Y, Z]$ 和 RPY 旋转角，并转换为度数存储于 resolution 中。
     * 
     * @param feature 待解析的几何特征引用（包含目标 Frame 名称及特征类型）
     * @param referenceFrame 基准参考系名称（若为空或 "WORLD" 则以世界坐标系为基准）
     * @param workcell RobWork 场景图句柄
     * @param state 当前 RobWork 运动学状态（包含各关节角/坐标系当前位姿）
     * @param resolution[out] 解析输出：在 referenceFrame 下的相对位置与 RPY 姿态
     * @param error[out] 可选输出：若解析失败（如 Frame 在场景中不存在），返回错误描述字符串
     * @return true 解析成功；false 解析失败
     */
    static bool resolve(const GeometryFeatureReference& feature, const std::string& referenceFrame,
                        const rw::models::WorkCell& workcell, const rw::kinematics::State& state,
                        GeometryFeatureResolution& resolution, std::string* error = nullptr);

    /**
     * @brief 应用与更新函数：将解析出的几何位姿及规则反写更新到关键工位 (KeyStation) 中
     * 
     * 处理逻辑：
     * 1. 调用 resolve() 计算几何特征在工位指定的 referenceFrame 下的相对位姿；
     * 2. 将结果反写到工位的 position 与 rpyDeg 字段中；
     * 3. 自动更新工位的 Source 为 PoseTaskSource::GeometryFeature；
     * 4. 根据几何特征类型自动设置姿态对齐规则（例如：平面法向特征类型 `FramePlaneNormal` 自动
     *    设置 OrientationMode 为 `AlignGeometryNormal`，否则设置为 `AlignFrame`）。
     * 
     * @param feature 欲绑定的几何特征引用
     * @param workcell RobWork 场景图句柄
     * @param state 当前 RobWork 运动学状态
     * @param station[in,out] 目标工位对象（解析成功后其位姿、姿态规则及溯源标记将被更新）
     * @param error[out] 可选输出：失败原因描述
     * @return true 更新成功；false 更新失败
     */
    static bool applyToStation(const GeometryFeatureReference& feature, const rw::models::WorkCell& workcell,
                               const rw::kinematics::State& state, KeyStation& station,
                               std::string* error = nullptr);
};

} // namespace rws

#endif