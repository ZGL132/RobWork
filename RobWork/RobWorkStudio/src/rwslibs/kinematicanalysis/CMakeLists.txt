set(SUBSYS_NAME sdurws_kinematicanalysis)
set(SUBSYS_DESC "Kinematic analysis plugin")
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
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(SrcFiles
        KinematicAnalysisPlugin.cpp
        KinematicAnalysisWidget.cpp
        KinematicAnalysisTypes.cpp
        KinematicMetrics.cpp
        KinematicAnalyzer.cpp
    )

    set(SRC_FILES_HPP
        KinematicAnalysisPlugin.hpp
        KinematicAnalysisWidget.hpp
        KinematicAnalysisTypes.hpp
        KinematicMetrics.hpp
        KinematicAnalyzer.hpp
    )

    if(DEFINED Qt6Core_VERSION)
        qt_add_resources(RccSrcFiles resources.qrc)
    else()
        qt5_add_resources(RccSrcFiles resources.qrc)
    endif()

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

    rws_plugin_load_details(${SUBSYS_NAME} 1 KinematicAnalysis false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    add_executable(sdurws_kinematicanalysis_test
        KinematicAnalysisTest.cpp
        KinematicAnalysisTypes.cpp
        KinematicMetrics.cpp
        KinematicAnalyzer.cpp
    )
    target_link_libraries(sdurws_kinematicanalysis_test
        PRIVATE
            ${SUBSYS_DEPS}
            ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_kinematicanalysis_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
