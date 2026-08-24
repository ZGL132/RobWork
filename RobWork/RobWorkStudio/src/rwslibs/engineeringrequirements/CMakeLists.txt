set(SUBSYS_NAME sdurws_engineeringrequirements)
set(SUBSYS_DESC "Engineering requirements plugin")
set(SUBSYS_DEPS
    PUBLIC sdurws
           sdurws_robotmodelbuilder
           sdurws_robotanalysiscore
)

set(build TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ON DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    set(CoreSrcFiles
        GeometryFeatureResolver.cpp
        OrientationRuleResolver.cpp
        RequirementSetJson.cpp
        RequirementCompiler.cpp
        RequirementFreezer.cpp
        RequirementMigration.cpp
        RequirementSetUndoStack.cpp
        StationImportService.cpp
        StationTemplateService.cpp
        WorkspaceRegionSceneVisualizer.cpp
    )
    set(CoreHeaderFiles
        EngineeringRequirementTypes.hpp
        GeometryFeatureResolver.hpp
        OrientationRuleResolver.hpp
        RequirementSetJson.hpp
        RequirementCompiler.hpp
        RequirementFreezer.hpp
        RequirementMigration.hpp
        RequirementSetUndoStack.hpp
        StationImportService.hpp
        StationTemplateService.hpp
        WorkspaceRegionSceneVisualizer.hpp
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
    target_compile_definitions(sdurws_engineeringrequirements_test PRIVATE
        ENGINEERINGREQUIREMENTS_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    if(WIN32)
        add_custom_command(
            TARGET sdurws_engineeringrequirements_test
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_BINARY_DIR}/RobWork/bin/$<CONFIG>"
                    "$<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>"
            COMMENT "Copying RobWork runtime DLLs next to engineering requirements tests"
        )
        foreach(qt_runtime_target
                Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
                Qt5::Core Qt5::Gui Qt5::Widgets Qt5::OpenGL Qt5::OpenGLWidgets)
            if(TARGET ${qt_runtime_target})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${qt_runtime_target}>
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
                )
            endif()
        endforeach()
        foreach(qt_platform_plugin_target Qt6::QWindowsIntegrationPlugin Qt5::QWindowsIntegrationPlugin)
            if(TARGET ${qt_platform_plugin_target})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>/platforms
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${qt_platform_plugin_target}>
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>/platforms
                )
            endif()
        endforeach()
        foreach(qt_style_plugin_target Qt6::QModernWindowsStylePlugin Qt5::QWindowsVistaStylePlugin)
            if(TARGET ${qt_style_plugin_target})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E make_directory
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>/styles
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${qt_style_plugin_target}>
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>/styles
                )
            endif()
        endforeach()
        foreach(boost_runtime_target Boost::filesystem Boost::system Boost::program_options Boost::thread)
            if(TARGET ${boost_runtime_target})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${boost_runtime_target}>
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
                )
            endif()
        endforeach()
        # Some vcpkg packages expose runtime DLLs without imported CMake
        # targets.  Copy only the known transitive runtime dependencies used
        # by this test; do not copy the entire vcpkg bin directory.
        if(DEFINED Boost_DIR)
            get_filename_component(_boost_share_dir "${Boost_DIR}" DIRECTORY)
            get_filename_component(_vcpkg_triplet_dir "${_boost_share_dir}" DIRECTORY)
            file(GLOB _boost_runtime_dlls
                 "${_vcpkg_triplet_dir}/debug/bin/boost_filesystem*.dll"
                 "${_vcpkg_triplet_dir}/debug/bin/boost_program_options*.dll"
                 "${_vcpkg_triplet_dir}/debug/bin/boost_thread*.dll"
                 "${_vcpkg_triplet_dir}/bin/boost_filesystem*.dll"
                 "${_vcpkg_triplet_dir}/bin/boost_program_options*.dll"
                 "${_vcpkg_triplet_dir}/bin/boost_thread*.dll")
            foreach(boost_runtime_dll ${_boost_runtime_dlls})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${boost_runtime_dll}"
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
                )
            endforeach()
            file(GLOB _vcpkg_runtime_dlls
                 "${_vcpkg_triplet_dir}/debug/bin/qhull_rd.dll"
                 "${_vcpkg_triplet_dir}/debug/bin/zip.dll"
                 "${_vcpkg_triplet_dir}/debug/bin/bz2d.dll"
                 "${_vcpkg_triplet_dir}/debug/bin/zd.dll"
                 "${_vcpkg_triplet_dir}/bin/qhull_r.dll"
                 "${_vcpkg_triplet_dir}/bin/zip.dll"
                 "${_vcpkg_triplet_dir}/bin/bz2.dll"
                 "${_vcpkg_triplet_dir}/bin/z.dll")
            foreach(vcpkg_runtime_dll ${_vcpkg_runtime_dlls})
                add_custom_command(
                    TARGET sdurws_engineeringrequirements_test
                    POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${vcpkg_runtime_dll}"
                            $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
                )
            endforeach()
        endif()
    endif()
    if(BUILD_TESTING)
        add_test(NAME sdurws_engineeringrequirements_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test>)
        add_test(NAME sdurws_engineeringrequirements_migration_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> migration)
        add_test(NAME sdurws_engineeringrequirements_region_visualization_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> region_visualization)
        # UI 测试单独启动 QApplication，覆盖当前 JOG State 如何传入几何工位
        # 解析的回归路径；不能只依赖默认的无界面核心测试。
        add_test(NAME sdurws_engineeringrequirements_widget_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> widget)
        add_test(NAME sdurws_engineeringrequirements_relative_source_base_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> relative_source_base)
        set_tests_properties(
            sdurws_engineeringrequirements_relative_source_base_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
        )
        add_test(NAME sdurws_engineeringrequirements_managed_project_root_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> managed_project_root)
        add_test(NAME sdurws_engineeringrequirements_managed_project_gate_test
                 COMMAND $<TARGET_FILE:sdurws_engineeringrequirements_test> managed_project_gate)
        if(WIN32)
            set(ENGINEERINGREQUIREMENTS_TEST_QPA_PLATFORM windows)
        else()
            set(ENGINEERINGREQUIREMENTS_TEST_QPA_PLATFORM offscreen)
        endif()
        set_tests_properties(
            sdurws_engineeringrequirements_widget_test
            sdurws_engineeringrequirements_managed_project_gate_test
            PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=${ENGINEERINGREQUIREMENTS_TEST_QPA_PLATFORM}"
        )
        set_tests_properties(
            sdurws_engineeringrequirements_managed_project_root_test
            PROPERTIES
                WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_engineeringrequirements_test>
                ENVIRONMENT "QT_QPA_PLATFORM=${ENGINEERINGREQUIREMENTS_TEST_QPA_PLATFORM}"
        )
    endif()
endif()
