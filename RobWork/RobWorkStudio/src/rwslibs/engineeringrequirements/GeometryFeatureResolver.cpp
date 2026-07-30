#include "GeometryFeatureResolver.hpp"

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/WorkCell.hpp>

namespace rws {
namespace {

/**
 * @brief 内部辅助函数：在 WorkCell（场景图）中查找指定的参考坐标系 (Frame)
 * 
 * 逻辑说明：
 * - 如果传入的坐标系名称为空，或者显式指定为 "WORLD"（世界坐标系），
 *   则直接返回 WorkCell 的根节点坐标系（WorldFrame）；
 * - 否则通过名称在 WorkCell 中搜索对应的 Frame 指针，若找不到则返回 nullptr。
 * 
 * @param workcell 3D 场景图引用
 * @param name 坐标系名称
 * @return rw::kinematics::Frame* 坐标系指针（找不到时返回 nullptr）
 */
rw::kinematics::Frame* findReferenceFrame(const rw::models::WorkCell& workcell, const std::string& name)
{
    if (name.empty() || name == "WORLD") return workcell.getWorldFrame();
    return workcell.findFrame(name);
}

} // namespace 匿名空间

/**
 * @brief 核心函数 1：解析几何特征在参考坐标系下的相对位姿
 * 
 * 深入浅出解析：
 * 本函数解决的核心问题是：“已知场景中有一个目标几何体 Frame（如工件表面上的某个坐标系），
 * 以及一个基准 Frame（如工作台坐标系），在当前机器人/场景状态下，目标相对基准的位置和姿态角是多少？”
 * 
 * 计算流程：
 * 1. 强校验：检查几何特征类型是否有效、特征 Frame 名称是否非空；
 * 2. 节点查找：在场景图中查找 referenceFrame（基准）和 feature.frameName（目标）两个 Frame 指针；
 * 3. 运动学正解计算：调用 RobWork 的 Kinematics::frameTframe API，
 *    根据当前场景的运动学状态（state，包含关节角度、物体位姿等），
 *    计算出从参考系到目标系的齐次变换矩阵 $T_{ref \to feat}$；
 * 4. 矩阵拆解与单位转换：
 *    - 位置提取：从变换矩阵提取平移向量 $P = [X, Y, Z]$（单位：米）；
 *    - 姿态提取：从旋转矩阵 $R$ 构造 RPY（Roll-Pitch-Yaw）欧拉角对象；
 *    - 弧度转角度：RobWork 内部欧拉角使用弧度表示，此处乘以 $\frac{180}{\pi}$ 转换为角度，
 *      保存至 resolution。
 */
bool GeometryFeatureResolver::resolve(const GeometryFeatureReference& feature, const std::string& referenceFrame,
                                      const rw::models::WorkCell& workcell, const rw::kinematics::State& state,
                                      GeometryFeatureResolution& resolution, std::string* error)
{
    // 1. 检查特征引用的合法性
    if (feature.type == GeometryFeatureType::None || feature.frameName.empty()) {
        if (error != nullptr) *error = "A geometry feature frame is required.";
        return false;
    }

    // 2. 在场景图中检索参考系与目标特征系
    rw::kinematics::Frame* reference = findReferenceFrame(workcell, referenceFrame);
    rw::kinematics::Frame* source = workcell.findFrame(feature.frameName);
    if (reference == nullptr || source == nullptr) {
        if (error != nullptr) *error = "The geometry feature or reference frame is no longer available in the WorkCell.";
        return false;
    }

    // 3. 利用 RobWork 运动学引擎计算相对齐次变换矩阵 T (Transform3D)
    const rw::math::Transform3D<> referenceTfeature =
        rw::kinematics::Kinematics::frameTframe(reference, source, state);

    // 4. 从旋转矩阵 R 中提取 RPY 欧拉角 (Roll, Pitch, Yaw)
    const rw::math::RPY<> rpy(referenceTfeature.R());

    // 5. 填入解析输出结果（位置取米，姿态从弧度转换为度）
    for (int axis = 0; axis < 3; ++axis) {
        resolution.position[axis] = referenceTfeature.P()[axis];
        resolution.rpyDeg[axis] = rpy[axis] * 180.0 / rw::math::Pi; // rad -> deg
    }

    if (error != nullptr) error->clear();
    return true;
}

/**
 * @brief 核心函数 2：解析几何特征并自动配置工位（KeyStation）的工艺意图与姿态规则
 * 
 * 深入浅出解析：
 * 当工程师在 3D 视图中用鼠标拾取了某个工装或工件表面的 Frame 后，不能只把“位置 XYZ”填进工位，
 * 还需要把相关联的“工艺语义”和“姿态规则”一并自动化配置好。
 * 
 * 自动化更新流程：
 * 1. 运动学解算：调用 resolve()，基于当前工位设置的 refFrame，计算出 3D 特征的相对位姿；
 * 2. 关联特征与源标记：将 geometryFeature 存入工位，并将工位来源标记为 PoseTaskSource::GeometryFeature；
 * 3. 相对位姿覆盖：将解析出的 relative [X, Y, Z] 和 [Roll, Pitch, Yaw] 角度反写到工位的位姿字段；
 * 4. 姿态规则联动：
 *    - 记录对齐目标系名称为 feature.frameName；
 *    - 根据特征类型智能选择对齐模式：
 *      * 若特征类型为 `FramePlaneNormal`（平面法线），说明工位要求末端工具垂直于表面，
 *        自动将模式设为 `AlignGeometryNormal`（对齐几何法线）；
 *      * 否则设为普通的 `AlignFrame`（对齐坐标系）。
 */
bool GeometryFeatureResolver::applyToStation(const GeometryFeatureReference& feature,
                                             const rw::models::WorkCell& workcell,
                                             const rw::kinematics::State& state, KeyStation& station,
                                             std::string* error)
{
    // 1. 先解算出几何特征在工位参考系 (station.refFrame) 下的相对位姿
    GeometryFeatureResolution resolution;
    if (!resolve(feature, station.refFrame, workcell, state, resolution, error)) return false;

    // 2. 更新工位的几何特征引用与来源标记
    station.geometryFeature = feature;
    station.source = PoseTaskSource::GeometryFeature;

    // 3. 将解算得到的数值覆盖到工位的位置与姿态（角度）字段
    station.position = resolution.position;
    station.rpyDeg = resolution.rpyDeg;

    // 4. 智能联动配置工位的姿态规则（Orientation Rule）
    station.orientation.targetFrame = feature.frameName;
    station.orientation.targetGeometry = "frame:" + feature.frameName;
    
    // 平面法线类型自动切换为“法线对齐模式”，其他类型切换为“坐标向对齐模式”
    station.orientation.mode = (feature.type == GeometryFeatureType::FramePlaneNormal) ?
        OrientationMode::AlignGeometryNormal : OrientationMode::AlignFrame;

    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws