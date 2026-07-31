#ifndef RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP
#define RWS_ENGINEERINGREQUIREMENTS_ENGINEERINGREQUIREMENTSPLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

#include <map>

// 前向声明 RobWork 渲染节点与运动学类，减少跨库编译依赖并提高编译速度
namespace rw { namespace graphics { class DrawableNode; } }
namespace rw { namespace kinematics { class Frame; class State; } }

namespace rws {
class EngineeringRequirementsWidget;
class CallbackProjectDocumentProvider;

/**
 * @brief 工程需求 RobWorkStudio 插件入口类 (Engineering Requirements Plugin)
 * 
 * 核心职责与架构设计：
 * 1. 插件生命周期管理：继承自 `rws::RobWorkStudioPlugin`，实现 RobWorkStudio 主界面的插件加载、打开/关闭 WorkCell 场景等回调；
 * 2. 交互界面宿主：内部实例化并持有一份 UI 主面板 `EngineeringRequirementsWidget`；
 * 3. 3D 渲染与标记（Markers）：在 RobWork 3D 渲染场景图中，为每个编辑中的关键工位（KeyStation）实时动态绘制/更新坐标轴（Axes）与文字标签（Labels）；
 * 4. 3D 拾取与事件响应：监听 RobWorkStudio 的全局事件（如 Frame 选择事件 `frameSelectedEvent`、场景运动学状态改变事件 `stateChangedEvent`），
 *    实现“在 3D 视图中 Ctrl+双击 拾取几何特征 Frame”以及“机器人动时 3D 工位标记同步跟随更新”的交互体验。
 */
class EngineeringRequirementsPlugin : public RobWorkStudioPlugin {
    Q_OBJECT // Qt 宏，使该类支持信号槽（Signal & Slot）机制与元对象反射
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    // Qt 动态插件元数据声明：注册该插件作为 RobWorkStudio 的动态扩展模块
    Q_PLUGIN_METADATA(IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
    Q_INTERFACES(rws::RobWorkStudioPlugin)
#endif
public:
    /**
     * @brief 构造函数：初始化插件基础属性（如插件名称与图标）
     */
    EngineeringRequirementsPlugin();

    /**
     * @brief 析构函数：释放资源，清空 3D 视图中的渲染标记，并注销 RobWorkStudio 的事件监听
     */
    ~EngineeringRequirementsPlugin() override;

    /**
     * @brief 插件初始化入口（由 RobWorkStudio 框架在加载插件时自动调用）
     * 
     * 逻辑说明：
     * 1. 实例化核心 UI 面板 `EngineeringRequirementsWidget` 并通过 `setWidget()` 嵌入到 RobWorkStudio 的停靠窗口（Dock Widget）中；
     * 2. 绑定 UI 信号与插件槽函数（如拾取请求、需求变更刷新的信号槽）；
     * 3. 向 RobWorkStudio 主程序注册事件监听（`frameSelectedEvent` 和 `stateChangedEvent`）。
     */
    void initialize() override;

    /**
     * @brief 场景打开回调（当用户在 RobWorkStudio 中打开一个新的 .wc.xml / WorkCell 时触发）
     * 
     * @param workcell 加载完成的 3D 场景图句柄
     */
    void open(rw::models::WorkCell* workcell) override;

    /**
     * @brief 场景关闭回调（当用户关闭当前场景时触发）
     */
    void close() override;

private:
    /**
     * @brief 开启 3D 几何特征拾取模式
     * 
     * 由 UI 面板的“从 3D 拾取几何 Frame”按钮触发，将 `_geometryFeaturePickActive` 设为 true。
     */
    void beginGeometryFeaturePick();

    /**
     * @brief 3D 视图 Frame 选择事件处理回调（响应 RobWorkStudio 的 frameSelectedEvent）
     * 
     * 深入浅出解析：
     * 当工程师在 3D 视图中按住 Ctrl 双击某个工件或工装的 Frame 时，主程序会触发此事件。
     * 如果此时处于拾取模式（`_geometryFeaturePickActive == true`），该函数会被调用，
     * 提取被选中 Frame 的名称并传递给 UI 面板 `applyGeometryFeatureFrame()`，自动将工位贴合到该 Frame 上。
     * 
     * @param frame 当前在 3D 场景中被选择的坐标系节点指针
     */
    void handleFrameSelected(rw::kinematics::Frame* frame);

    /**
     * @brief 场景运动学状态改变回调（响应 RobWorkStudio 的 stateChangedEvent）
     * 
     * 当拖动机器人关节、改变物体位置或播放轨迹动画时触发，调用 updateStationMarkers() 实时重新计算
     * 并刷新所有工位 3D Marker 的空间位姿。
     * 
     * @param state 当前全新的 3D 场景运动学状态
     */
    void handleStateChanged(const rw::kinematics::State& state);

    /**
     * @brief 延迟/防抖调度 3D 标记重绘（Schedule Station Marker Refresh）
     * 
     * 深入浅出解析：
     * 工程师在 UI 表格里批量修改参数时，可能在一毫秒内触发多次需求变更信号。
     * 为了避免频繁擦除/重建 3D 场景图节点导致界面卡顿甚至场景图重入崩溃，
     * 此函数采用 `QTimer::singleShot(0, ...)` 进行“延迟异步刷新”（Event Loop Deferment），
     * 确保在一个 UI 渲染帧内多次修改只触发一次全量 3D 渲染刷新。
     */
    void scheduleStationMarkerRefresh();

    /**
     * @brief 结构性重建 3D 场景中的所有工位标记（Reconstruct Station Markers）
     * 
     * 当需求集合中的工位数量发生增删、ID 改变或需求重新装载时调用：
     * 1. 调用 clearStationMarkers() 清除旧的 3D 渲染节点；
     * 2. 遍历需求集中的每一个 KeyStation，向 RobWorkStudio 的 WorkCellScene 添加新的三维坐标轴节点（DrawableNode）
     *    和文字标签节点；
     * 3. 存入 `_stationAxes` 和 `_stationLabels` 映射表中备查。
     */
    void refreshStationMarkers();

    /**
     * @brief 增量更新已有 3D 标记的空间位姿与显隐状态（Update Station Marker Poses）
     * 
     * 不会删除或创建 3D 节点，仅更新节点的矩阵变换（setTransform）和显隐状态（setVisible）：
     * 1. 根据当前运动学状态（state）解算出各工位的空间实际位姿矩阵；
     * 2. 将坐标轴 Marker 和文字 Marker（稍向上平移 4cm 避免重叠）移动到最新位置；
     * 3. 若工位绑定的 Frame 无法解析，自动隐藏该 Marker。
     * 
     * @param state 当前 3D 场景运动学状态
     */
    void updateStationMarkers(const rw::kinematics::State& state);

    /**
     * @brief 彻底清空并销毁 3D 场景图中的所有工位标记节点
     * 
     * 从 RobWorkStudio 的 WorkCellScene 中安全移除 DrawableNode 节点并清空映射表，防止内存泄漏或空指针访问。
     */
    void clearStationMarkers();

    // ------------------------------------------------------------------------
    // 成员变量
    // ------------------------------------------------------------------------
    EngineeringRequirementsWidget* _widget = nullptr; ///< 嵌入插件的 Qt UI 主面板指针
    // Provider 不继承 QObject，生命周期由插件显式覆盖；主窗口 Registry 只保留非拥有
    // 引用，因此 Provider 必须在插件存活期间保持有效。
    CallbackProjectDocumentProvider* _projectProvider = nullptr;
    bool _geometryFeaturePickActive = false;           ///< 3D 视图几何拾取激活标志（true 表示下一次点击 3D 场景将捕获 Frame）
    bool _markerRefreshPending = false;                ///< 标记异步延迟刷新挂起标志（用于防抖）

    /**
     * @brief 工位三维坐标轴渲染节点映射表
     * Key: 工位 ID (station.id)
     * Value: 指向 RobWork 3D 场景图中绘制的三维坐标轴（RGB 三色轴）节点的智能指针 (Ptr<DrawableNode>)
     */
    std::map<std::string, rw::core::Ptr<rw::graphics::DrawableNode> > _stationAxes;

    /**
     * @brief 工位三维文字标签渲染节点映射表
     * Key: 工位 ID (station.id)
     * Value: 指向 RobWork 3D 场景图中绘制的文本（工位名称/ID）节点的智能指针 (Ptr<DrawableNode>)
     */
    std::map<std::string, rw::core::Ptr<rw::graphics::DrawableNode> > _stationLabels;
};

} // namespace rws

#endif
