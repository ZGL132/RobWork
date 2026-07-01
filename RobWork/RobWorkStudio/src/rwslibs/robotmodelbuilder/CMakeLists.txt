set(SUBSYS_NAME sdurws_robotmodelbuilder)
set(SUBSYS_DESC "Robot model builder plugin")
set(SUBSYS_DEPS
    PUBLIC  sdurws
            RW::sdurw_core
            RW::sdurw_models
            RW::sdurw_loaders
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(ModelSrcFiles RobotModelXmlWriter.cpp)
    set(ModelHeaderFiles RobotModelSpec.hpp RobotModelXmlWriter.hpp)
    set(SrcFiles
        RobotModelBuilderPlugin.cpp
        ${ModelSrcFiles}
    )
    set(SRC_FILES_HPP
        RobotModelBuilderPlugin.hpp
        ${ModelHeaderFiles}
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
    rw_add_includes(${SUBSYS_NAME} "rwslibs/robotmodelbuilder" ${SRC_FILES_HPP})
    target_link_libraries(${SUBSYS_NAME} ${SUBSYS_DEPS} PUBLIC ${QT_LIBRARIES})
    target_include_directories(${SUBSYS_NAME}
        INTERFACE
        $<BUILD_INTERFACE:${RWS_ROOT}/src> $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
    )

    rws_plugin_load_details(${SUBSYS_NAME} 2 RobotModelBuilder false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    add_executable(sdurws_robotmodelbuilder_xmltest
        RobotModelXmlWriterTest.cpp
        ${ModelSrcFiles}
        ${ModelHeaderFiles}
    )
    target_link_libraries(sdurws_robotmodelbuilder_xmltest PRIVATE ${QT_LIBRARIES})
    target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
