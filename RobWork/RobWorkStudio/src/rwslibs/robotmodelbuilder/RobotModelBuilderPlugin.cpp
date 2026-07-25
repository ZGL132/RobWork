// =============================================================================
//  文件: RobotModelBuilderPlugin.cpp
//  说明: RobotModelBuilder 插件入口实现。本文件非常薄,只负责把 Widget 装到
//        RobWorkStudio 中,并把 Widget 的"加载场景"信号转发给宿主,真正的建模
//        UI 和 XML 生成逻辑都在 RobotModelBuilderWidget / RobotModelXmlWriter 中。
// =============================================================================
#include "RobotModelBuilderPlugin.hpp"

#include "RobotModelBuilderWidget.hpp"
#include "WorkCellConverter.hpp"

#include <rws/RobWorkStudio.hpp>

#include <rw/models/WorkCell.hpp>

using namespace rws;

// -----------------------------------------------------------------------------
//  构造函数
//  说明: 向基类传入插件名(显示在 RobWorkStudio 菜单/插件列表中)和图标(留空)。
//        _widget 暂时为空指针,待 initialize() 中再实例化。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::RobotModelBuilderPlugin () :
    RobWorkStudioPlugin ("RobotModelBuilder", QIcon (":/robotmodelbuilder/robotmodelbuilder_icon.png")),
    _widget (NULL),
    _ignoreNextOpenFromSelfLoad (false)
{}

// -----------------------------------------------------------------------------
//  析构函数
//  说明: Qt 的对象父子机制会在本对象销毁时自动 delete _widget,无需手动释放。
// -----------------------------------------------------------------------------
RobotModelBuilderPlugin::~RobotModelBuilderPlugin ()
{}

// -----------------------------------------------------------------------------
//  initialize()
//  说明: RobWorkStudio 加载插件时调用一次。完成三件事:
//        1) 创建 UI(Widget)实例;
//        2) 把 Widget 的 loadSceneRequested 信号连接到此处的 loadSceneFile 槽;
//        3) 通过 setWidget 把 UI 注入到 RobWorkStudio 的 Dock/容器中。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::initialize ()
{
    _widget = new RobotModelBuilderWidget (this);
    // 当 Widget 完成 "Save and Load" 操作时,会发出场景文件名,我们在这里负责真正去加载它
    connect (_widget, SIGNAL (loadSceneRequested (const QString&)), this,
             SLOT (loadSceneFile (const QString&)));
    setWidget (_widget);
}

// -----------------------------------------------------------------------------
//  open() / close()
//  说明: WorkCell 切换钩子。本插件并不直接缓存 WorkCell 数据,因此两个回调保持空实现。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::open (rw::models::WorkCell* workcell)
{
    if (_ignoreNextOpenFromSelfLoad) {
        _ignoreNextOpenFromSelfLoad = false;
        return;
    }
    syncFromWorkCell (workcell);
}

void RobotModelBuilderPlugin::close ()
{}

// =============================================================================
//  syncFromWorkCell
//  说明: 当宿主程序 (RobWorkStudio) 加载或切换 WorkCell 场景时，触发此同步回调。
//        负责将 C++ 内存中的 WorkCell 场景模型反向解析并灌入 UI 界面中。
//
//  工作流程:
//    1) 指针有效性防御检查：确认 UI 界面控件 (_widget) 与场景指针 (workcell) 均非空；
//    2) 保存路径推导：通过场景对象的磁盘文件信息推算 saveDirectory 目录；
//    3) 核心反向转换：调用 WorkCellConverter::convert，结合内存 C++ 对象与磁盘源 XML
//       无损提取/缝合出 RobotModelSpec 数据模型；
//    4) 可建模模型判定：调用 WorkCellConverter::hasConvertibleRobotModel 检查转换出的 spec
//       是否包含有效的机器人模型 (包含非空名称与运动学关节)；
//    5) UI 数据回填：将解析好的 spec 与警告信息同步给 UI 控件 (_widget)，刷新界面表格。
//
//  参数:
//    - workcell : 当前在 RobWorkStudio 中被加载/选中的工作单元场景指针
// =============================================================================
void RobotModelBuilderPlugin::syncFromWorkCell (rw::models::WorkCell* workcell)
{
    // ---- 1. 空指针防御检查 ----
    // 若 UI 尚未实例化或当前没有激活的 WorkCell 场景，直接返回
    if (_widget == NULL || workcell == NULL)
        return;

    // 用于收集场景转换与文件解析过程中的非致命警告信息
    QStringList warnings;

    // ---- 2. 自动推导保存路径 ----
    // 优先从 workcell 对象的元数据中提取绝对磁盘路径，并算出其所在目录
    const std::string saveDirectory = WorkCellConverter::inferSaveDirectory (*workcell);

    // ---- 3. 执行核心场景转换 ----
    // 将内存中的 WorkCell 对象、默认状态 (State) 及目标保存目录传入转换器，
    // 提取串联关节、SE(3) 矩阵、几何体、碰撞矩阵及伴生 XML 配置文件，构建出纯数据结构 spec
    RobotModelSpec spec =
        WorkCellConverter::convert (*workcell, workcell->getDefaultState (), saveDirectory, warnings);

    // ---- 4. 检查模型有效性 ----
    // 验证转换出来的 spec 是否包含可编辑/可转换的机器人模型
    // （例如：场景中如果仅有一张桌子而没有串联机器人设备，则不触发插件界面同步）
    if (!WorkCellConverter::hasConvertibleRobotModel (spec))
        return;

    // ---- 5. 驱动 UI 界面同步 ----
    // 将解析提取出的模型规范 spec 以及警告列表灌入 Builder Widget 中，
    // 触发 UI 各个标签页表格 (Kinematics, Drawables, Limits, Poses 等) 的全量回填
    _widget->syncFromWorkCellSpec (spec, warnings);
}

// -----------------------------------------------------------------------------
//  loadSceneFile()
//  说明: 由 Widget 发出的信号触发,要求 RobWorkStudio 加载指定路径的场景 XML。
//        这里做了一次空指针保护:getRobWorkStudio() 在插件被卸载等极端情况下
//        可能会返回 NULL,避免崩溃。
// -----------------------------------------------------------------------------
void RobotModelBuilderPlugin::loadSceneFile (const QString& filename)
{
    if (getRobWorkStudio () != NULL) {
        _ignoreNextOpenFromSelfLoad = true;
        getRobWorkStudio ()->setWorkcell (filename.toStdString ());
    }
}
