# =============================================================================
#  CMakeLists.txt - RobotModelBuilder 插件构建脚本
#  说明: 本脚本编译出两件产物:
#         1) sdurws_robotmodelbuilder       - RobWorkStudio 插件(动态/静态库)
#         2) sdurws_robotmodelbuilder_xmltest - 不依赖 GUI 的命令行测试可执行文件
#        其中第 2 件只链接 QtCore,把 XmlWriter + Spec 的纯逻辑部分单独拎出来
#        跑断言测试,适合 CI 中无头环境下回归。
# =============================================================================

# 插件子系统标识与说明
set(SUBSYS_NAME sdurws_robotmodelbuilder)
set(SUBSYS_DESC "Robot model builder plugin")
# 依赖:RobWorkStudio 插件框架 + 三个核心 RW 库(几何/模型/加载器)
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_models
            RW::sdurw_loaders
)

set(build TRUE)
# 标准 RW 子系统开关:可被 -DRW_BUILD_xxx=OFF 关闭;ON 时也注册到文档构建中
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    # ---- 源代码分组 ----
    # Model 系列:不依赖 Qt Widget,只依赖 QtCore,可独立被命令行测试程序复用
    set(ModelSrcFiles RobotModelXmlWriter.cpp)
    set(ModelHeaderFiles RobotModelSpec.hpp RobotModelXmlWriter.hpp)
    # 插件编译入口:Plugin + Widget + Model 三者合并
    set(SrcFiles
        RobotModelBuilderPlugin.cpp
        RobotModelBuilderWidget.cpp
        ${ModelSrcFiles}
    )
    # 公共头文件:供其他插件/外部项目引用
    set(SRC_FILES_HPP
        RobotModelBuilderPlugin.hpp
        RobotModelBuilderWidget.hpp
        ${ModelHeaderFiles}
    )

    # ---- 资源文件:按 Qt 版本选 API ----
    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

    # ---- 注册为 RobWorkStudio 插件 ----
    rws_add_plugin(
        ${SUBSYS_NAME}
        ${RWS_DEFAULT_LIB_TYPE}
        ${SrcFiles}
        ${MocSrcFiles}
        ${RccSrcFiles}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/robotmodelbuilder" ${SRC_FILES_HPP})
    # 链接 Qt(Widgets/Gui 等由 QT_LIBRARIES 统一给出)+ 上面声明的 RW 依赖
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    # 把 rws 源码路径与安装路径都暴露给下游消费者
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    # RobWorkStudio 插件元数据:load=2 表示"按需加载",显示名 RobotModelBuilder
    rws_plugin_load_details(${SUBSYS_NAME} 2 RobotModelBuilder false)
    # 静态链接模式下,把本插件追加到 RWS_PLUGIN_LIBRARIES 让主程序能链入
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    # ---- 命令行测试可执行文件 ----
    # 只编译 XmlWriter/Test,不需要 Widget/GUI 库,便于 CI 无头运行
    add_executable(sdurws_robotmodelbuilder_xmltest
        RobotModelXmlWriterTest.cpp
        ${ModelSrcFiles}
        ${ModelHeaderFiles}
    )
    target_link_libraries(sdurws_robotmodelbuilder_xmltest PRIVATE ${QT_LIBRARIES})
    target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()