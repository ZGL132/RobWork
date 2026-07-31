#include "EngineeringRequirementsPlugin.hpp"
#include "GeometryFeatureResolver.hpp"
#include "EngineeringRequirementsWidget.hpp"
#include "OrientationRuleResolver.hpp"

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/RobWorkStudio.hpp>
#include <rws/RWStudioView3D.hpp>

#include <rw/graphics/DrawableNode.hpp>
#include <rw/graphics/WorkCellScene.hpp>
#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/Constants.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/WorkCell.hpp>

#include <QTimer>

#include <string>

namespace {

/**
 * @brief 3D 渲染节点掩码 (Drawable Node Mask)
 * 
 * 结合了 Virtual（虚拟辅助节点）与 DrawableObject（可渲染对象）掩码，
 * 告诉 RobWork 渲染引擎：这是一个用于可视化辅助展示的 3D Marker（标记），不参与物理碰撞体计算。
 */
const int stationMarkerMask = rw::graphics::DrawableNode::Virtual |
                              rw::graphics::DrawableNode::DrawableObject;

/**
 * @brief 辅助函数：触发 3D 场景重绘
 * 
 * 当工位 3D 标记发生增删或位置更新后，调用 RWStudioView3D 的 update() 请求 OpenGL 重新渲染当前帧。
 */
void requestSceneRedraw(rws::RobWorkStudio* studio)
{
    if (studio != nullptr && !studio->getView().isNull())
        studio->getView()->update();
}

/**
 * @brief 核心数学计算函数：计算工位在“世界坐标系 (WORLD)”下的绝对齐次变换矩阵 $T_{world \to station}$
 * 
 * 深入浅出讲解：
 * 工位在配置文件中保存的坐标是“相对坐标”（例如相对于工装 `Fixture_A` 的位置 $[x, y, z]$ 和姿态 $[R, P, Y]$）。
 * 但在 3D 渲染引擎中绘制 Marker 时，必须提供该 Marker 在**世界坐标系**下的绝对位姿矩阵 $T_{world \to station}$。
 * 
 * 计算推导公式：
 * $$T_{world \to station} = T_{world \to reference} \times T_{reference \to station}$$
 * 
 * 计算流程：
 * 1. 检查工位来源：若工位绑定的几何特征 (`PoseTaskSource::GeometryFeature`)，
 *    先调用 GeometryFeatureResolver::resolve 解算出在参考系下的实时相对位姿；
 * 2. 寻找基准 Frame：在场景图中找到 `station.refFrame`（若为空或 "WORLD" 则默认为世界原点）；
 * 3. 运动学计算：调用 Kinematics::worldTframe 计算基准系在世界坐标系下的变换 $T_{world \to reference}$；
 * 4. 矩阵复合：构建相对变换 $T_{reference \to station}$，与基准系世界变换相乘，得出最终的绝对世界变换。
 * 
 * @param station 工位对象
 * @param workcell 3D 场景图引用
 * @param state 当前运动学状态
 * @param[out] valid 输出标记：用于指示该工位的参考系或几何特征是否在当前场景中有效存在
 * @return rw::math::Transform3D<> 计算出的世界坐标变换矩阵
 */
rw::math::Transform3D<> stationTransform(const rws::PoseTask& station,
                                         const rw::models::WorkCell& workcell,
                                         const rw::kinematics::State& state, bool* valid)
{
    // Resolve on a copy so marker refreshes never overwrite the authored task.
    rws::PoseTask resolvedStation = station;

    // 1. 如果工位来源是“几何特征绑定”，先实时解算其在参考系下的相对位姿
    if (station.source == rws::PoseTaskSource::GeometryFeature) {
        rws::GeometryFeatureResolution resolution;
        if (!rws::GeometryFeatureResolver::resolve(station.geometryFeature, station.refFrame,
                                                   workcell, state, resolution, nullptr)) {
            if (valid != nullptr) *valid = false; // 特征解算失败（可能场景中模型被删除）
            return rw::math::Transform3D<>::identity();
        }
        resolvedStation.position = resolution.position;
        resolvedStation.rpyDeg = resolution.rpyDeg;
    }

    // A geometry feature provides the current location; an orientation rule then
    // supplies the final rotation for the same current State.  This ordering is
    // essential for PointAtTarget because it uses the station position as source.
    if (resolvedStation.orientation.mode != rws::OrientationMode::Fixed) {
        std::string orientationError;
        if (!rws::OrientationRuleResolver::applyToStation(
                resolvedStation, workcell, state, &orientationError)) {
            if (valid != nullptr) *valid = false;
            return rw::math::Transform3D<>::identity();
        }
    }

    // 2. 查找工位所引用的基准参考系 (Reference Frame)
    rw::kinematics::Frame* reference = station.refFrame.empty() || station.refFrame == "WORLD" ?
        workcell.getWorldFrame() : workcell.findFrame(station.refFrame);
    if (reference == nullptr) {
        if (valid != nullptr) *valid = false; // 参考系不存在
        return rw::math::Transform3D<>::identity();
    }

    if (valid != nullptr) *valid = true;

    // 3. 计算基准系相对于世界坐标系的齐次变换矩阵 T_world_reference
    const rw::math::Transform3D<> worldTreference = rw::kinematics::Kinematics::worldTframe(reference, state);

    // 4. 构建工位相对于基准系的齐次变换矩阵 T_reference_station（角度转弧度）
    const double degToRad = rw::math::Pi / 180.0;
    const rw::math::Transform3D<> referenceTstation(
        rw::math::Vector3D<>(resolvedStation.position[0], resolvedStation.position[1],
                             resolvedStation.position[2]),
        rw::math::RPY<>(resolvedStation.rpyDeg[0] * degToRad,
                        resolvedStation.rpyDeg[1] * degToRad,
                        resolvedStation.rpyDeg[2] * degToRad));

    // 5. 矩阵乘法组合：T_world_station = T_world_reference * T_reference_station
    return worldTreference * referenceTstation;
}

} // namespace 匿名空间

namespace rws {

/**
 * @brief 构造函数：初始化 RobWorkStudio 插件基类，传入插件名称与默认图标
 */
EngineeringRequirementsPlugin::EngineeringRequirementsPlugin() :
    RobWorkStudioPlugin("EngineeringRequirements", QIcon())
{}

/**
 * @brief 析构函数：清理 3D 渲染节点，注销事件监听
 */
EngineeringRequirementsPlugin::~EngineeringRequirementsPlugin()
{
    clearStationMarkers();
    if (getRobWorkStudio() != nullptr) {
        // 安全移除在 initialize() 中注册的 RobWorkStudio 全局事件回调
        getRobWorkStudio()->frameSelectedEvent().remove(this);
        getRobWorkStudio()->stateChangedEvent().remove(this);
    }
    // 应用退出时主窗口先关闭项目资源，Registry 不会再回调 Provider；此处再释放插件
    // 持有的回调对象，避免把所有权错误交给仅保存非拥有指针的 Registry。
    delete _projectProvider;
}

/**
 * @brief 插件初始化生命周期函数
 * 
 * 逻辑说明：
 * 1. 实例化核心 UI 控件 `EngineeringRequirementsWidget` 并通过 `setWidget()` 嵌入到 RobWorkStudio 插件面板中；
 * 2. 连接 UI 面板发出的信号：
 *    - `geometryFeaturePickRequested` $\rightarrow$ 开启 3D 拾取模式；
 *    - `requirementsChanged` $\rightarrow$ 调度 3D 标记重绘；
 * 3. 监听 RobWorkStudio 全局事件：
 *    - `frameSelectedEvent`：用户在 3D 视图中点击 Frame 时触发；
 *    - `stateChangedEvent`：场景运动学状态改变（如拖动关节、移动模型）时触发。
 */
void EngineeringRequirementsPlugin::initialize() {
    _widget = new EngineeringRequirementsWidget(this);
    setWidget(_widget);

    _projectProvider = new CallbackProjectDocumentProvider(
        QStringLiteral("rws.engineering-requirements"),
        QStringLiteral("rws.engineering-requirements"),
        [this](const QString& path, const ProjectDocumentContext&, QString* error) {
            return _widget->loadProjectDocument(path, error);
        },
        [this](const QString& targetPath, const ProjectDocumentContext&, QString* error) {
            return _widget->saveProjectDocument(targetPath, error);
        },
        CallbackProjectDocumentProvider::CanCloseHandler(),
        CallbackProjectDocumentProvider::CloseHandler(),
        [this]() { _widget->markProjectDocumentClean(); });

    if (getRobWorkStudio() != nullptr) {
        QString providerError;
        // 注册失败时保留原有独立需求编辑能力，但不让该资源在 rwproj 中被静默跳过。
        // 打开项目会由 Registry 对缺失的必需 Provider 给出明确诊断。
        if (!getRobWorkStudio()->registerProjectDocumentProvider(_projectProvider, &providerError))
            RW_WARN("EngineeringRequirements project Provider registration failed: "
                    << providerError.toStdString());
    }

    // 连接 UI 面板的交互信号
    connect(_widget, &EngineeringRequirementsWidget::geometryFeaturePickRequested,
            this, &EngineeringRequirementsPlugin::beginGeometryFeaturePick);
    connect(_widget, &EngineeringRequirementsWidget::requirementsChanged,
            this, &EngineeringRequirementsPlugin::scheduleStationMarkerRefresh);
    connect(_widget, &EngineeringRequirementsWidget::requirementsChanged, this, [this]() {
        if (_projectProvider == nullptr || getRobWorkStudio() == nullptr)
            return;
        // requirementsChanged 同时覆盖表格编辑、撤销重做和冻结状态变化；Widget 使用
        // 规范 JSON 快照判断真实变化，Provider 因而不会把普通控件交互误报为脏数据。
        _projectProvider->setDirty(_widget->isProjectDocumentDirty());
        getRobWorkStudio()->notifyProjectDocumentChanged();
    });

// 检查当前插件是否已成功嵌入到 RobWorkStudio 主程序环境中（防止空指针调用导致程序崩溃）
    if (getRobWorkStudio() != nullptr) {
        
        /* 
         * 1. 订阅 3D 视图元素选中事件 (frameSelectedEvent)
         * 
         * 深入浅出解析：
         * - 触发时机：当工程师在 3D 视图窗口中点击（或 Ctrl+双击）了某个坐标系/工件 Frame 时触发。
         * - 交互逻辑：使用 Lambda 表达式捕捉 this 指针，将被选中的 3D 节点指针 (frame) 转发给本类的 handleFrameSelected 函数。
         * - 核心作用：实现“鼠标点击 3D 模型”与“UI 面板工位参数自动绑定”的实时交互。
         * - 安全机制：最后一个参数传入 `this`，用于将事件生命周期与当前插件绑定，确保插件销毁时自动移除监听，防止悬空指针回调。
         */
        getRobWorkStudio()->frameSelectedEvent().add(
            [this](rw::kinematics::Frame* frame) { handleFrameSelected(frame); }, this);

        /* 
         * 2. 订阅 3D 场景运动学状态改变事件 (stateChangedEvent)
         * 
         * 深入浅出解析：
         * - 触发时机：当场景发生动态变化时触发（如用户拖动了机器人关节、在 3D 视图中平移了工装模型，或播放仿真轨迹动画）。
         * - 交互逻辑：RobWorkStudio 会将最新的场景状态 (state) 传出来，插件收到通知后调用 handleStateChanged。
         * - 核心作用：重新计算并驱动场景中所有工位 3D Marker（坐标轴与文字标签）跟随物体一起运动，保证“画在 3D 空间里的标记”始终紧贴物体不脱节。
         */
        getRobWorkStudio()->stateChangedEvent().add(
            [this](const rw::kinematics::State& state) { handleStateChanged(state); }, this);
    }
}

/**
 * @brief 当用户在 RobWorkStudio 中打开一个新的 WorkCell 场景时触发
 */
void EngineeringRequirementsPlugin::open(rw::models::WorkCell* workcell) {
    if (_widget != nullptr) {
        _widget->setWorkCell(workcell);
        // open() 早于用户第一次 JOG 操作时也要把主程序当前 State 交给需求
        // 面板，保证首次 TCP 捕获、几何拾取和冻结不会落回无意的默认姿态。
        if (getRobWorkStudio() != nullptr)
            _widget->setCurrentState(getRobWorkStudio()->getState());
    }
    refreshStationMarkers(); // 新场景加载后，全量构建 3D 工位 Marker
}

/**
 * @brief 当用户关闭当前 WorkCell 场景时触发
 */
void EngineeringRequirementsPlugin::close() {
    clearStationMarkers(); // 清除所有 3D 视图渲染节点
    _geometryFeaturePickActive = false;
    if (_widget != nullptr) _widget->setWorkCell(nullptr);
}

/**
 * @brief 开启 3D 几何特征拾取状态
 */
void EngineeringRequirementsPlugin::beginGeometryFeaturePick()
{
    _geometryFeaturePickActive = true;
}

/**
 * @brief 处理 3D 视图中的 Frame 选中事件
 * 
 * 深入浅出解析：
 * 当工程师在 UI 面板点击“从 3D 拾取几何 Frame”后，系统处于 `_geometryFeaturePickActive == true` 激活状态。
 * 此时工程师在 3D 视图中按住 Ctrl 双击某个工件或工装上的 Frame：
 * 1. RobWorkStudio 捕获点击并调用此回调；
 * 2. 立即重置拾取标志为 false（单次拾取生效）；
 * 3. 提取被选中 Frame 的名称，调用 UI 面板的 `applyGeometryFeatureFrame` 自动完成位姿与规则绑定。
 */
void EngineeringRequirementsPlugin::handleFrameSelected(rw::kinematics::Frame* frame)
{
    if (!_geometryFeaturePickActive || _widget == nullptr || frame == nullptr) return;
    _geometryFeaturePickActive = false; // 单次拾取后关闭激活状态
    _widget->applyGeometryFeatureFrame(QString::fromStdString(frame->getName()));
}

/**
 * @brief 处理 3D 场景运动学状态改变事件
 * 
 * 例如用户在场景中拖动机械臂或平移工装，调用 updateStationMarkers 重新计算绝对矩阵，使 3D Marker 跟随移动。
 */
void EngineeringRequirementsPlugin::handleStateChanged(const rw::kinematics::State& state)
{
    // 标记刷新和工程需求解析必须观察同一份 JOG 状态；否则 3D 里显示的位置
    // 与“捕获当前 TCP”或“冻结需求”所使用的位置可能彼此不一致。
    if (_widget != nullptr)
        _widget->setCurrentState(state);
    updateStationMarkers(state);
}

/**
 * @brief 异步延迟/防抖刷新 3D 标记 (Event Loop Deferment)
 * 
 * 深入浅出解析（性能与稳定性优化）：
 * 工程师在 UI 表格中连续输入或批量修改工位时，可能在一毫秒内触发十几次 `requirementsChanged` 信号。
 * 如果每次信号都立即清空重绘 3D 场景，会导致严重的界面卡顿甚至场景图重入崩溃。
 * 
 * 解决方案：
 * 利用 `QTimer::singleShot(0, ...)` 将任务推迟到下一个事件循环迭代中执行。
 * 在同一帧事件循环内的多次调用会被 `_markerRefreshPending` 拦截，最终仅触发一次全量重绘！
 */
void EngineeringRequirementsPlugin::scheduleStationMarkerRefresh()
{
    if (_markerRefreshPending) return; // 已有刷新任务在队列中挂起，直接拦截
    _markerRefreshPending = true;
    QTimer::singleShot(0, this, [this] {
        _markerRefreshPending = false;
        refreshStationMarkers(); // 在事件循环空闲时执行全量重绘
    });
}

/**
 * @brief 清除 3D 场景中的所有工位 Marker 节点
 */
void EngineeringRequirementsPlugin::clearStationMarkers()
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio != nullptr && !studio->getWorkCellScene().isNull()) {
        // 从 RobWork 3D 场景图中逐个注销渲染节点
        for (const auto& item : _stationAxes) studio->getWorkCellScene()->removeDrawable(item.second);
        for (const auto& item : _stationLabels) studio->getWorkCellScene()->removeDrawable(item.second);
    }
    _stationAxes.clear();
    _stationLabels.clear();
    requestSceneRedraw(studio);
}

/**
 * @brief 结构性重建 3D 场景中的所有工位标记（全量创建）
 * 
 * 流程说明：
 * 1. 先调用 clearStationMarkers() 清空旧节点；
 * 2. 遍历需求集中的每一个工位：
 *    - 忽略 RequirementLevel::Info 辅助信息项或空 ID 项；
 *    - 调用 `addFrameAxis` 动态创建 3D 坐标轴节点（三色 RGB 轴，轴长 5cm）；
 *    - 调用 `addText` 动态创建三维文字标签节点；
 * 3. 调用 updateStationMarkers 更新新节点的绝对位姿；
 * 4. 请求 3D 视图重绘。
 */
void EngineeringRequirementsPlugin::refreshStationMarkers()
{
    _markerRefreshPending = false;
    clearStationMarkers();

    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || _widget == nullptr || studio->getWorkCell().isNull() ||
        studio->getWorkCellScene().isNull())
        return;

    const RequirementSet requirements = _widget->requirementSet();
    rw::graphics::WorkCellScene::Ptr scene = studio->getWorkCellScene();

    for (const PoseTask& station : requirements.poseTasks) {
        // Info 级别的工位仅作为文本记录，不在 3D 场景中绘制标记
        if (station.level == RequirementLevel::Info || station.id.empty()) continue;

        const std::string name = "EngineeringRequirement." + station.id;

        // 向 3D 场景图中添加三维坐标轴 Marker (轴长 0.05m)
        rw::graphics::DrawableNode::Ptr axis = scene->addFrameAxis(
            name, 0.05, studio->getWorkCell()->getWorldFrame(), stationMarkerMask);
        _stationAxes[station.id] = axis;

        // 向 3D 场景图中添加三维文本标签 Marker
        const std::string label = station.name.empty() ? station.id : station.name;
        _stationLabels[station.id] = scene->addText(name + ".label", label,
                                                     studio->getWorkCell()->getWorldFrame(),
                                                     stationMarkerMask);
    }

    // 更新新节点的实际绝对位姿
    updateStationMarkers(studio->getState());
    requestSceneRedraw(studio);
}

/**
 * @brief 增量更新所有已有 3D 标记的绝对位置与显隐状态（增量位置推算）
 * 
 * 性能优化说明：
 * 该函数不需要进行节点的销毁与重建，仅仅是遍历已有的 3D 渲染节点，更新其变换矩阵 `setTransform`。
 * 
 * 姿态与位置平移计算：
 * 1. 调用 stationTransform 计算得出 $T_{world \to station}$；
 * 2. 轴节点直接应用 $T_{world \to station}$ 矩阵；
 * 3. 文字标签节点在 $T_{world \to station}$ 的基础上，沿着 Z 轴正方向向上偏移 4cm ($0.04m$)，
 *    避免文字标签与坐标轴重叠在一起影响视角；
 * 4. 如果计算得出 `valid == false`（如参考系丢失），自动隐藏 Marker (`setVisible(false)`)。
 */
void EngineeringRequirementsPlugin::updateStationMarkers(const rw::kinematics::State& state)
{
    RobWorkStudio* studio = getRobWorkStudio();
    if (studio == nullptr || _widget == nullptr || studio->getWorkCell().isNull()) return;

    const RequirementSet requirements = _widget->requirementSet();

    for (const PoseTask& station : requirements.poseTasks) {
        const auto axis = _stationAxes.find(station.id);
        if (axis == _stationAxes.end()) continue;

        bool valid = false;
        // 计算工位在当前场景状态下的绝对世界变换矩阵
        const rw::math::Transform3D<> worldTstation = stationTransform(
            station, *studio->getWorkCell(), state, &valid);

        // 更新坐标轴 Marker 的位姿与显隐状态
        axis->second->setVisible(valid);
        axis->second->setTransform(worldTstation);

        // 更新文字标签 Marker 的位姿（向上微移 4cm，防止与坐标轴重叠）
        const auto label = _stationLabels.find(station.id);
        if (label != _stationLabels.end()) {
            label->second->setVisible(valid);
            label->second->setTransform(worldTstation * rw::math::Transform3D<>(
                rw::math::Vector3D<>(0.0, 0.0, 0.04)));
        }
    }

    requestSceneRedraw(studio);
}

} // namespace rws
