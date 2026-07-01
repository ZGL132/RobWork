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

    add_executable(sdurws_robotmodelbuilder_xmltest
        RobotModelXmlWriterTest.cpp
        ${ModelSrcFiles}
        ${ModelHeaderFiles}
    )
    target_link_libraries(sdurws_robotmodelbuilder_xmltest PRIVATE ${QT_LIBRARIES})
    target_include_directories(sdurws_robotmodelbuilder_xmltest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
