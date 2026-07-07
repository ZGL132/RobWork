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
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_kinematicanalysis_test
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test>
        )
        add_test(
            NAME sdurws_kinematicanalysis_test_types
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test> types
        )
        add_test(
            NAME sdurws_kinematicanalysis_test_metrics
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test> metrics
        )
        add_test(
            NAME sdurws_kinematicanalysis_test_current_pose
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test> current_pose
        )
        add_test(
            NAME sdurws_kinematicanalysis_test_ik
            COMMAND $<TARGET_FILE:sdurws_kinematicanalysis_test> ik
        )
    endif()
endif()
