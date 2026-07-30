set(SUBSYS_NAME sdurws_engineeringrequirements)
set(SUBSYS_DESC "Engineering requirements plugin")
set(SUBSYS_DEPS
    PUBLIC sdurws
           sdurws_robotmodelbuilder
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(CoreSrcFiles
        GeometryFeatureResolver.cpp
        RequirementSetJson.cpp
        RequirementCompiler.cpp
        RequirementSetUndoStack.cpp
        StationImportService.cpp
        StationTemplateService.cpp
    )
    set(CoreHeaderFiles
        EngineeringRequirementTypes.hpp
        GeometryFeatureResolver.hpp
        RequirementSetJson.hpp
        RequirementCompiler.hpp
        RequirementSetUndoStack.hpp
        StationImportService.hpp
        StationTemplateService.hpp
    )
    rws_add_component(sdurws_engineeringrequirements_core ${CoreSrcFiles} ${CoreHeaderFiles})
    rw_add_includes(sdurws_engineeringrequirements_core "rwslibs/engineeringrequirements" ${CoreHeaderFiles})
    target_link_libraries(sdurws_engineeringrequirements_core PUBLIC ${SUBSYS_DEPS} ${QT_LIBRARIES})
    set_target_properties(sdurws_engineeringrequirements_core PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS TRUE)

    set(PluginSrcFiles
        EngineeringRequirementsPlugin.cpp
        EngineeringRequirementsWidget.cpp
    )
    set(PluginHeaderFiles
        EngineeringRequirementsPlugin.hpp
        EngineeringRequirementsWidget.hpp
    )
    rws_add_plugin(${SUBSYS_NAME} ${RWS_DEFAULT_LIB_TYPE} ${PluginSrcFiles})
    rw_add_includes(${SUBSYS_NAME} "rwslibs/engineeringrequirements" ${PluginHeaderFiles})
    target_link_libraries(${SUBSYS_NAME} PUBLIC ${SUBSYS_DEPS} ${QT_LIBRARIES}
                           PRIVATE sdurws_engineeringrequirements_core)
    rws_plugin_load_details(${SUBSYS_NAME} 1 EngineeringRequirements false)
    if("${RWS_DEFAULT_LIB_TYPE}" STREQUAL "STATIC")
        set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} ${SUBSYS_NAME} PARENT_SCOPE)
    endif()

    add_executable(sdurws_engineeringrequirements_test
        EngineeringRequirementsTest.cpp
        EngineeringRequirementsWidget.cpp
        EngineeringRequirementsWidget.hpp
    )
    target_link_libraries(sdurws_engineeringrequirements_test
        PRIVATE sdurws_engineeringrequirements_core ${QT_LIBRARIES}
    )
    target_include_directories(sdurws_engineeringrequirements_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(BUILD_TESTING)
        add_test(NAME sdurws_engineeringrequirements_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test>)
    endif()
endif()
