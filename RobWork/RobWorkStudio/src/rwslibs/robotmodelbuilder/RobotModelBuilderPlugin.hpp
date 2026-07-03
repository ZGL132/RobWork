// =============================================================================
//  文件: RobotModelBuilderPlugin.hpp
//  说明: RobWorkStudio 插件入口声明。该插件提供一个可视化的 6 轴机器人模型
//        构建工具(SerialDevice / WorkCell / DynamicWorkCell 的 XML 生成器)。
//        这里是插件类本身的头文件,具体 UI 与业务逻辑见 RobotModelBuilderWidget。
// =============================================================================
#ifndef RWS_ROBOTMODELBUILDER_PLUGIN_HPP
#define RWS_ROBOTMODELBUILDER_PLUGIN_HPP

#include <rws/RobWorkStudioPlugin.hpp>

namespace rws {

// 前向声明:真正的 UI 与数据收集逻辑由 Widget 完成,这里只声明指针以减少头文件依赖
class RobotModelBuilderWidget;

/**
 * @brief RobotModelBuilder 插件类
 *
 * 继承自 RobWorkStudioPlugin,作为整个插件的对外门面:
 *   - 负责创建并托管 RobotModelBuilderWidget UI;
 *   - 监听 Widget 发出的"加载场景"信号,调用 RobWorkStudio 切换当前 WorkCell;
 *   - 提供 initialize/open/close 等插件生命周期回调(RobWorkStudio 在适当时机调用)。
 */
class RobotModelBuilderPlugin : public RobWorkStudioPlugin
{
    Q_OBJECT
#ifndef RWS_USE_STATIC_LINK_PLUGINS
    // 非静态链接时,显式声明接口与插件元数据,使 Qt 的插件机制可以发现并加载本插件
    Q_INTERFACES (rws::RobWorkStudioPlugin)
    Q_PLUGIN_METADATA (IID "dk.sdu.mip.Robwork.RobWorkStudioPlugin/0.1" FILE "plugin.json")
#endif
  public:
    /// 构造函数:设置插件显示名 "RobotModelBuilder"(即 RobWorkStudio 中菜单/工具栏上的名字)
    RobotModelBuilderPlugin ();
    /// 析构函数:目前为空,所有资源由 Qt 的父子对象树统一释放
    virtual ~RobotModelBuilderPlugin ();

    /// 插件被加载后由 RobWorkStudio 调用,用于创建 UI、连接信号槽
    void initialize ();
    /// RobWorkStudio 打开新的 WorkCell 时调用,这里无需处理
    void open (rw::models::WorkCell* workcell);
    /// RobWorkStudio 关闭 WorkCell 时调用,这里无需处理
    void close ();

  private Q_SLOTS:
    /**
     * @brief 槽函数:Widget 完成 XML 写入后,通过信号通知此处加载生成的场景文件
     * @param filename 由 Widget 拼装好的场景 XML 路径(Scene.wc.xml)
     */
    void loadSceneFile (const QString& filename);

  private:
    /// 实际的 UI 与业务逻辑对象,由本插件创建并管理生命周期
    RobotModelBuilderWidget* _widget;
};

}    // namespace rws

#endif