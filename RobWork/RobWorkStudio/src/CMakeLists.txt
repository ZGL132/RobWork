# MESSAGE("ROBWORKSTUDIO LIBS: ${ROBWORKSTUDIO_LIBRARIES}")
add_definitions(-DQT_NO_KEYWORDS)
# First we compile the robworkstudio library
add_subdirectory(rws)

# compile the component libraries (plugins and other static libs)
add_subdirectory(rwslibs)

if(POLICY CMP0077) # Introduce cmake 3.13
    cmake_policy(SET CMP0077 NEW)
endif()

# Propagate libraries to parent directory
set(RWS_PLUGIN_LIBRARIES ${RWS_PLUGIN_LIBRARIES} PARENT_SCOPE)
set(RWS_COMPONENT_LIBRARIES ${RWS_COMPONENT_LIBRARIES} PARENT_SCOPE)

# now create the actual RobWorkStudio executable Now the RobWorkStudio executable need to be build:
set(SUBSYS_NAME RobWorkStudio)
set(SUBSYS_DESC "The RobWorkStudio GUI.")
set(SUBSYS_DEPS sdurws sdurws_robworkstudioapp)

set(build TRUE)
set(DEFAULT TRUE)
rw_subsys_option(build ${SUBSYS_NAME} ${SUBSYS_DESC} ${DEFAULT} DEPENDS ${SUBSYS_DEPS} ADD_DOC)

if(build)
    if(WIN32 AND IS_RELEASE)
        add_executable(${SUBSYS_NAME} WIN32 main.cpp)
    else()
        add_executable(${SUBSYS_NAME} main.cpp)
    endif()

    set(LIBS_TMP ${RWS_PLUGIN_LIBRARIES} sdurws ${RWS_COMPONENT_LIBRARIES})

    target_link_libraries(${SUBSYS_NAME} ${LIBS_TMP})
    target_link_libraries(${SUBSYS_NAME} RW::sdurw sdurws_robworkstudioapp ${GLUT_glut_LIBRARY})

    if(WIN32)
        add_custom_command(
            TARGET ${SUBSYS_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_BINARY_DIR}/RobWork/bin"
                    "$<TARGET_FILE_DIR:${SUBSYS_NAME}>"
            COMMENT "Copying RobWork runtime DLLs next to RobWorkStudio"
        )

        set(RWS_QT_RUNTIME_TARGETS)
        foreach(qt_runtime_target
                Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
                Qt5::Core Qt5::Gui Qt5::Widgets)
            if(TARGET ${qt_runtime_target})
                list(APPEND RWS_QT_RUNTIME_TARGETS ${qt_runtime_target})
            endif()
        endforeach()
        foreach(qt_runtime_target ${RWS_QT_RUNTIME_TARGETS})
            add_custom_command(
                TARGET ${SUBSYS_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_runtime_target}>
                    $<TARGET_FILE_DIR:${SUBSYS_NAME}>
            )
        endforeach()

        set(RWS_QT_PLATFORM_PLUGIN_TARGETS)
        foreach(qt_platform_plugin_target Qt6::QWindowsIntegrationPlugin Qt5::QWindowsIntegrationPlugin)
            if(TARGET ${qt_platform_plugin_target})
                list(APPEND RWS_QT_PLATFORM_PLUGIN_TARGETS ${qt_platform_plugin_target})
            endif()
        endforeach()
        foreach(qt_platform_plugin_target ${RWS_QT_PLATFORM_PLUGIN_TARGETS})
            add_custom_command(
                TARGET ${SUBSYS_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:${SUBSYS_NAME}>/platforms
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_platform_plugin_target}>
                    $<TARGET_FILE_DIR:${SUBSYS_NAME}>/platforms
            )
        endforeach()

        set(RWS_QT_STYLE_PLUGIN_TARGETS)
        foreach(qt_style_plugin_target Qt6::QModernWindowsStylePlugin Qt5::QWindowsVistaStylePlugin)
            if(TARGET ${qt_style_plugin_target})
                list(APPEND RWS_QT_STYLE_PLUGIN_TARGETS ${qt_style_plugin_target})
            endif()
        endforeach()
        foreach(qt_style_plugin_target ${RWS_QT_STYLE_PLUGIN_TARGETS})
            add_custom_command(
                TARGET ${SUBSYS_NAME}
                POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    $<TARGET_FILE_DIR:${SUBSYS_NAME}>/styles
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${qt_style_plugin_target}>
                    $<TARGET_FILE_DIR:${SUBSYS_NAME}>/styles
            )
        endforeach()
    endif()

    # Some install stuff
    install(TARGETS ${SUBSYS_NAME} DESTINATION ${BIN_INSTALL_DIR} COMPONENT sdurws )
    if(RWS_USE_STATIC_LINK_PLUGINS)
        install(
            FILES "${RWS_ROOT}/bin/RobWorkStudio.ini.template.static"
            DESTINATION ${BIN_INSTALL_DIR}
            COMPONENT sdurws
            RENAME "RobWorkStudio.ini.template"
        )
        configure_file("${RWS_ROOT}/bin/RobWorkStudio.ini.template.static"
                       "${RWS_ROOT}/bin/${RWS_BUILD_TYPE}/RobWorkStudio.ini.template" COPYONLY)
    else()
        install(FILES "${RWS_ROOT}/bin/RobWorkStudio.ini.shared" DESTINATION ${BIN_INSTALL_DIR} RENAME "RobWorkStudio.ini")
        configure_file("${RWS_ROOT}/bin/RobWorkStudio.ini.shared.in" "${RWS_ROOT}/bin/RobWorkStudio.ini.shared")
        file(
            GENERATE
            OUTPUT $<TARGET_FILE_DIR:RobWorkStudio>/RobWorkStudio.ini
            INPUT "${RWS_ROOT}/bin/RobWorkStudio.ini.shared"
        )
    endif()
endif()

# Install all headerfiles
install(
    DIRECTORY sdurws
    DESTINATION ${INCLUDE_INSTALL_DIR}
    COMPONENT sdurws
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
    PATTERN ".svn" EXCLUDE
)
