# 插件子系统名,与 RobWorkStudio 加载器约定的动态库名一致。
set(SUBSYS_NAME sdurws_kinematicanalysis)
set(SUBSYS_DESC "Kinematic analysis plugin")
# 依赖:
#   PUBLIC  — IK 求解需要;Widget 需要 sdurws 主框架;分析算法需要 kinematics /
#             models 与共享 RobotAnalysisCore;
#   PRIVATE — 仅核心算法/插件本体需要,不让下游插件 include 它们。
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_math
            RW::sdurw_kinematics
            RW::sdurw_models
            sdurws_robotanalysiscore
    PRIVATE RW::sdurw_invkin
            RW::sdurw_proximity
            RW::sdurw_proximitystrategies
)

set(build TRUE)
# rw_subsys_option 是 RobWork 提供的统一"子系统可选"宏:当依赖都可用时打开本插件。
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    # 主插件目标:P3 重构后用 TaskPointTableModel + TaskPointDelegates 替代 QTableWidget 单元格手动管理;
    # 同时保留 TaskPointResolver(P1)+ TaskPointUiLogic(P2)。
    set(SrcFiles
        KinematicAnalysisPlugin.cpp
        KinematicAnalysisWidget.cpp
        KinematicAnalysisTypes.cpp
        KinematicMetrics.cpp
        KinematicAnalyzer.cpp
        TaskPointResolver.cpp
        TaskPointUiLogic.cpp
        TaskPointTableModel.cpp
        TaskPointDelegates.cpp
        KinematicAnalysisVisualizationTypes.cpp
        KinematicAnalysisPlotWidget.cpp
        KinematicAnalysisWorkspace.cpp
        KinematicAnalysisPoseReachability.cpp
    )

    set(SRC_FILES_HPP
        KinematicAnalysisPlugin.hpp
        KinematicAnalysisWidget.hpp
        KinematicAnalysisTypes.hpp
        KinematicMetrics.hpp
        KinematicAnalyzer.hpp
        TaskPointResolver.hpp
        TaskPointUiLogic.hpp
        TaskPointTableModel.hpp
        TaskPointDelegates.hpp
        KinematicAnalysisVisualizationTypes.hpp
        KinematicAnalysisPlotWidget.hpp
        KinematicAnalysisWorkspace.hpp
        KinematicAnalysisPoseReachability.hpp
    )

    # Qt5 / Qt6 资源编译命令不同,这里做兼容分发。
    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

    # 注册成 RobWorkStudio 插件。RWS_DEFAULT_LIB_TYPE 控制 STATIC / SHARED。
    rws_add_plugin(
        ${SUBSYS_NAME}
        ${RWS_DEFAULT_LIB_TYPE}
        ${SrcFiles}
        ${MocSrcFiles}
        ${RccSrcFiles}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/kinematicanalysis" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    # 把插件元信息(plugin.json / 加载方式)记录到 RobWorkStudio 的插件清单中。
    rws_plugin_load_details(${SUBSYS_NAME} 1 KinematicAnalysis false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    # 单独的测试可执行文件:不依赖 Qt Widget,只验证核心算法。
    # 它和插件共享 Types/Metrics/Analyzer/Resolver/UiLogic/TableModel/Delegates
    # 七个 .cpp,避免重复实现。
    add_executable(sdurws_kinematicanalysis_test
        KinematicAnalysisTest.cpp
        KinematicAnalysisTypes.cpp
        KinematicMetrics.cpp
        KinematicAnalyzer.cpp
        TaskPointResolver.cpp
        TaskPointUiLogic.cpp
        TaskPointTableModel.cpp
        TaskPointDelegates.cpp
        KinematicAnalysisVisualizationTypes.cpp
        KinematicAnalysisWorkspace.cpp
        KinematicAnalysisPoseReachability.cpp
    )
    target_link_libraries(sdurws_kinematicanalysis_test
        PRIVATE
            ${SUBSYS_DEPS}
            ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_kinematicanalysis_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    # Windows 下把 Qt 运行库 / 平台插件 / 样式插件都拷到二进制同目录,
    # 这样 sdurws_kinematicanalysis_test 可以在不安装 Qt 的机器上独立运行。
    if(WIN32)
        set(KINEMATICANALYSIS_QT_RUNTIME_TARGETS)
        foreach(qt_runtime_target Qt6::Core Qt6::Gui Qt6::Widgets Qt5::Core Qt5::Gui Qt5::Widgets)
            if(TARGET ${qt_runtime_target})
                list(APPEND KINEMATICANALYSIS_QT_RUNTIME_TARGETS ${qt_runtime_target})
            endif()
        endforeach()
        foreach(qt_runtime_target ${KINEMATICANALYSIS_QT_RUNTIME_TARGETS})
            add_custom_command(
                TARGET sdurws_kinematicanalysis_test
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_runtime_target}>
                    $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>
            )
        endforeach()

        set(KINEMATICANALYSIS_QT_PLATFORM_PLUGIN_TARGETS)
        foreach(qt_platform_plugin_target Qt6::QWindowsIntegrationPlugin Qt5::QWindowsIntegrationPlugin)
            if(TARGET ${qt_platform_plugin_target})
                list(APPEND KINEMATICANALYSIS_QT_PLATFORM_PLUGIN_TARGETS ${qt_platform_plugin_target})
            endif()
        endforeach()
        foreach(qt_platform_plugin_target ${KINEMATICANALYSIS_QT_PLATFORM_PLUGIN_TARGETS})
            add_custom_command(
                TARGET sdurws_kinematicanalysis_test
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>/platforms
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_platform_plugin_target}>
                    $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>/platforms
            )
        endforeach()

        set(KINEMATICANALYSIS_QT_STYLE_PLUGIN_TARGETS)
        foreach(qt_style_plugin_target Qt6::QModernWindowsStylePlugin Qt5::QWindowsVistaStylePlugin)
            if(TARGET ${qt_style_plugin_target})
                list(APPEND KINEMATICANALYSIS_QT_STYLE_PLUGIN_TARGETS ${qt_style_plugin_target})
            endif()
        endforeach()
        foreach(qt_style_plugin_target ${KINEMATICANALYSIS_QT_STYLE_PLUGIN_TARGETS})
            add_custom_command(
                TARGET sdurws_kinematicanalysis_test
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>/styles
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_style_plugin_target}>
                    $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>/styles
            )
        endforeach()
    endif()
    # 注册到 CTest,工作目录设为可执行文件同目录,方便其按需加载 Qt 资源。
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_kinematicanalysis_test
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test>
        )
        set_tests_properties(
            sdurws_kinematicanalysis_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_kinematicanalysis_test>
        )
    endif()
endif()
