#ifndef RWS_ENGINEERINGREQUIREMENTS_STATIONTEMPLATESERVICE_HPP
#define RWS_ENGINEERINGREQUIREMENTS_STATIONTEMPLATESERVICE_HPP

#include "EngineeringRequirementTypes.hpp"

#include <array>
#include <string>
#include <vector>

namespace rws {

/**
 * @brief 工艺模板类型枚举
 * 预置的典型机器人工业应用场景模板
 */
enum class StationTemplateKind { 
    BinPicking,     ///< 料箱无序/有序散料取料（生成多层、多行、多列的取料工位阵列）
    MachineTending, ///< 机床上下料（自动生成安全待机点、接近点、装料点、卸料点、撤离点等组合工位）
    Palletizing,    ///< 码垛/拆垛（按层、行、列生成堆叠放置点工位）
    Inspection,     ///< 质检/视觉检测（生成标准检测动作点）
    ToolChange,     ///< 自动换刀/换工具（生成换具对接工位）
    Handover        ///< 双机/人机交接（生成工件交接位）
};

/**
 * @brief 批量工位阵列类型枚举
 * 基于某个基准工位（Source Station）扩展生成批量几何阵列的模式
 */
enum class StationArrayKind { 
    Linear,      ///< 线性一维阵列（沿指定方向和步长等距复制）
    Rectangular, ///< 矩形二维阵列（沿主方向与次方向矩阵复制）
    Circular,    ///< 圆周/弧形阵列（围绕指定半径和起始/终止角度环形复制）
    Polyline     ///< 沿 3D 折线等距采样阵列（沿着空间折线按累计弧长均匀布置工位）
};

/**
 * @brief 工艺模板生成请求参数结构体
 * 包含创建或更新某个工艺模板实例所需的所有维度、间距与安全距离参数
 */
struct StationTemplateRequest {
    StationTemplateKind kind = StationTemplateKind::BinPicking; ///< 模板工艺类型
    std::string instanceId;                  ///< 模板实例唯一标识（如 "bin_A"，用于追溯和后续批量更新）
    std::string idPrefix;                    ///< 生成工位 ID 的统一前缀（如 "pick"）
    std::string namePrefix;                  ///< 生成工位显示名称的前缀（如 "料箱A取料"）
    std::string referenceFrame = "WORLD";    ///< 基准参考坐标系（如工装/料箱坐标系）
    std::string tcpFrame;                    ///< 机器人末端工具坐标系（TCP）
    RequirementLevel level = RequirementLevel::Must; ///< 需求等级（Must/Should/Info）
    
    std::array<double, 3> operationOffsetMeters = {{0.0, 0.0, 0.0}}; ///< 作业中心平移偏置 [X, Y, Z]（单位：米）
    
    // 阵列维度参数
    int rows = 1;                            ///< 排列行数（行维数量）
    int columns = 1;                         ///< 排列列数（列维数量）
    int layers = 1;                          ///< 排列层数（堆叠层数）
    
    // 间距参数
    double rowSpacingMeters = 0.05;          ///< 行间距（单位：米）
    double columnSpacingMeters = 0.05;       ///< 列间距（单位：米）
    double layerSpacingMeters = 0.05;        ///< 层间距（单位：米）
    
    // 路径与安全参数
    double approachDistanceMeters = 0.10;    ///< 接近段距离（单位：米，沿工具 Z 轴方向延伸）
    double retractDistanceMeters = 0.10;     ///< 撤离段距离（单位：米，沿参考系 Z 轴方向延伸）
    double clearanceMeters = 0.15;           ///< 安全避让/待机距离（单位：米）
};

/**
 * @brief 工位批量阵列请求参数结构体
 * 描述如何从一个已有工位（源工位）按几何规律复制出一组新工位
 */
struct StationArrayRequest {
    StationArrayKind kind = StationArrayKind::Linear; ///< 阵列几何模式
    std::string instanceId;              ///< 阵列生成的实例唯一标识
    std::string idPrefix;                ///< 生成工位 ID 前缀
    std::string namePrefix;              ///< 生成工位名称前缀
    
    int primaryCount = 2;                ///< 主方向（第一维度）生成的工位数量
    int secondaryCount = 1;              ///< 次方向（第二维度）生成的工位数量
    
    std::array<double, 3> primaryStepMeters = {{0.05, 0.0, 0.0}};   ///< 主方向增量步长向量 $[dX, dY, dZ]$（米）
    std::array<double, 3> secondaryStepMeters = {{0.0, 0.05, 0.0}};  ///< 次方向增量步长向量 $[dX, dY, dZ]$（米）
    
    double radiusMeters = 0.10;          ///< 圆周阵列半径（单位：米）
    double startAngleDeg = 0.0;          ///< 圆周阵列起始角度（单位：度）
    double endAngleDeg = 360.0;          ///< 圆周阵列终止角度（单位：度）
    
    /**
     * @brief 折线采样顶点集合
     * 顶点使用源工位的参考系坐标。Polyline 阵列不把这些点当作相对偏移量，
     * 而是按照折线的累计弧长在空间中做二次等距采样，直接将采样点作为生成的工位位置，
     * 避免累加累积误差或转角处间距失真。
     */
    std::vector<std::array<double, 3>> polylinePointsMeters;
};

/**
 * @brief 模板更新预览结构体
 * 在修改已有模板参数时，用于“两阶段提交”交互（Preview -> Apply），防止破坏性覆盖
 */
struct TemplateUpdatePreview {
    std::string instanceId;                     ///< 正在更新的模板实例 ID
    std::vector<std::string> replacedStationIds;///< 即将真正被覆盖替换掉的关联工位 ID 列表
    std::vector<PoseTask> generatedStations;    ///< 重新生成的新工位列表
};

/**
 * @brief 模板与阵列生成服务类 (Station Template Service)
 * 
 * 核心功能与设计思想：
 * 1. 批量生成（Append）：根据工艺模板（如料箱取料、码垛）或几何阵列（线性、圆周、折线）批量产生一组工位；
 * 2. 状态追溯（Provenance）：每个生成的工位都会带有 `StationGenerationProvenance`，记录其模板 ID、实例 ID 及参数；
 * 3. 解除关联（Detach）：允许工程师对某个生成的工位单独微调，微调后该工位可标记为“解除关联 (linked=false)”，
 *    后续模板整体参数更新时将跳过该工位，不予覆盖；
 * 4. 两阶段安全更新（Preview -> Apply）：修改模板参数时，先计算预览结果（列出哪些工位受影响），确认无误后再应用覆盖。
 */
class StationTemplateService {
  public:
    /**
     * @brief 基于工艺模板参数，批量生成工位并追加到需求集 (RequirementSet) 中
     * 
     * 逻辑说明：
     * 1. 校验请求参数有效性（如维度 $\ge 1$，距离与步长不为 NaN 等）；
     * 2. 检查 instanceId 是否已存在（若存在且包含 linked 工位，将阻止重复追加，提示使用更新功能）；
     * 3. 根据模板类型（如 BinPicking、MachineTending）计算每个工位的坐标、姿态规则、接近/撤离动作；
     * 4. 为每个生成的工位分配全局唯一的 ID，并写入生成追溯信息（generatorId, instanceId, parameters）；
     * 5. 追加至 requirements.poseTasks。
     * 
     * @param[in,out] requirements 目标需求集对象
     * @param request 模板生成请求参数
     * @param error[out] 可选错误描述信息
     * @return true 追加成功；false 校验或生成失败
     */
    static bool appendTemplate(RequirementSet& requirements, const StationTemplateRequest& request,
                               std::string* error = nullptr);

    /**
     * @brief 预览更新模板实例（第一阶段：安全评估）
     * 
     * 深入浅出解析：
     * 工程师修改了某个料箱模板的列间距，需要查看修改后的效果。
     * 该函数不会直接修改原始需求集，而是生成一个“预览对象 (TemplateUpdatePreview)”：
     * 1. 在当前需求集中找出所有仍与 instanceId 关联（linked == true）的工位 ID，放入 replacedStationIds；
     * 2. 根据新参数计算出新的工位集合，放入 generatedStations；
     * 3. **保留独立工位**：曾经被工程师手动“解除关联”的工位不会被放入 replacedStationIds，从而免受覆盖影响。
     * 
     * @param requirements 当前需求集
     * @param instanceId 欲更新的模板实例 ID
     * @param request 新的模板配置请求
     * @param preview[out] 输出的更新预览结果（包含替换列表和新工位）
     * @param error[out] 失败原因
     * @return true 预览计算成功；false 失败
     */
    static bool previewTemplateUpdate(const RequirementSet& requirements, const std::string& instanceId,
                                      const StationTemplateRequest& request, TemplateUpdatePreview& preview,
                                      std::string* error = nullptr);

    /**
     * @brief 应用更新模板实例（第二阶段：提交更改）
     * 
     * 根据 previewPreview 提供的信息，从需求集中安全地擦除旧的被替换工位（replacedStationIds），
     * 并插入全新的工位（generatedStations），完成模板更新。
     * 
     * @param[in,out] requirements 目标需求集
     * @param preview previewTemplateUpdate() 计算产出的预览结果
     * @param error[out] 失败原因
     * @return true 应用更新成功；false 失败
     */
    static bool applyTemplateUpdate(RequirementSet& requirements, const TemplateUpdatePreview& preview,
                                    std::string* error = nullptr);

    /**
     * @brief 解除指定工位与模板/阵列的关联关系 (Detach)
     * 
     * 深入浅出解析：
     * 假设模板自动生成了 10 个料箱抓取点，但第 3 个抓取点旁边有个障碍物，工程师手动修改了第 3 个点的坐标。
     * 此时调用此函数将第 3 个点的 `generation.linked` 置为 `false`。
     * 以后无论怎么重置或更新整个料箱模板，第 3 个点都会保持工程师手工调整后的状态，绝不会被模板覆盖！
     * 
     * @param[in,out] requirements 需求集对象
     * @param stationId 欲解除关联的工位 ID
     * @param error[out] 失败原因
     * @return true 解除成功；false 该工位不存在或本来就未关联模板
     */
    static bool detachStation(RequirementSet& requirements, const std::string& stationId,
                              std::string* error = nullptr);

    /**
     * @brief 从指定源工位（Source Station）出发，批量生成几何阵列（线性、矩形、圆周、折线）
     * 
     * 逻辑说明：
     * 1. 查找源工位，继承其工艺类型、姿态规则、TCP 及校验策略；
     * 2. 根据阵列类型进行位置推演：
     *    - Linear：在源工位位置上按 primaryStepMeters 依次叠加；
     *    - Rectangular：按双向步长矩阵叠加；
     *    - Circular：在指定半径的圆弧/圆周上按角度插值分布；
     *    - Polyline：沿着输入的折线点进行空间弧长采样（samplePolylineByArcLength）；
     * 3. 将阵列工位追加至 requirements，其 `linked` 默认设为 `false`（阵列工位独立维护，不参与模板更新）。
     * 
     * @param[in,out] requirements 目标需求集
     * @param sourceStationId 作为阵列基准的源工位 ID
     * @param request 阵列参数请求
     * @param error[out] 失败原因
     * @return true 生成并追加阵列成功；false 失败
     */
    static bool appendArray(RequirementSet& requirements, const std::string& sourceStationId,
                            const StationArrayRequest& request, std::string* error = nullptr);

    // 枚举转字符串辅助工具函数（便于日志打印、Json 序列化及 UI 展示）
    static const char* toString(StationTemplateKind kind);
    static const char* toString(StationArrayKind kind);
};

} // namespace rws

#endif