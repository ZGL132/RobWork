set(SUBSYS_NAME sdurws_robotanalysiscore)
set(SUBSYS_DESC "Robot analysis shared core")
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_models
            sdurws_robotmodelbuilder
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(SrcFiles
        RobotAnalysisTypes.cpp
        RobotAnalysisJson.cpp
        RobotAnalysisCsv.cpp
        RobotAnalysisValidation.cpp
    )

    set(SRC_FILES_HPP
        RobotAnalysisTypes.hpp
        RobotAnalysisJson.hpp
        RobotAnalysisCsv.hpp
        RobotAnalysisValidation.hpp
    )

    rws_add_component(
        ${SUBSYS_NAME}
        ${SrcFiles}
        ${SRC_FILES_HPP}
    )
    rw_add_includes(${SUBSYS_NAME} "rwslibs/robotanalysiscore" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )
    set_target_properties(${SUBSYS_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

    add_executable(sdurws_robotanalysiscore_test
        RobotAnalysisCoreTest.cpp
    )
    target_link_libraries(sdurws_robotanalysiscore_test
        PRIVATE
        ${SUBSYS_NAME}
        ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_robotanalysiscore_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(BUILD_TESTING)
        add_test(
            NAME sdurws_robotanalysiscore_test
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test>
        )
        add_test(
            NAME sdurws_robotanalysiscore_test_types
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test> types
        )
        add_test(
            NAME sdurws_robotanalysiscore_test_validation
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test> validation
        )
        add_test(
            NAME sdurws_robotanalysiscore_test_json
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test> json
        )
        add_test(
            NAME sdurws_robotanalysiscore_test_csv
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test> csv
        )
        add_test(
            NAME sdurws_robotanalysiscore_test_invalid_suite
            COMMAND $<TARGET_FILE:sdurws_robotanalysiscore_test> invalid-suite
        )
        set_tests_properties(sdurws_robotanalysiscore_test_invalid_suite PROPERTIES WILL_FAIL TRUE)
    endif()
endif()
