# 四插件验收夹具只依赖 RobWork 核心加载器，不把宿主 GUI 状态混入结构自检。
if(BUILD_TESTING)
    if(DEFINED Qt6Core_VERSION)
        find_package(Qt6 COMPONENTS Core REQUIRED)
        set(FOURPLUGIN_ACCEPTANCE_QT_CORE Qt6::Core)
    else()
        find_package(Qt5 COMPONENTS Core REQUIRED)
        set(FOURPLUGIN_ACCEPTANCE_QT_CORE Qt5::Core)
    endif()

    add_executable(sdurws_fourplugin_acceptance_test
        FourPluginAcceptanceFixture.cpp
        FourPluginAcceptanceFixture.hpp
        FourPluginAcceptanceTest.cpp
    )
    target_link_libraries(sdurws_fourplugin_acceptance_test
        PRIVATE
            RW::sdurw_loaders
            RW::sdurw_models
            RW::sdurw_kinematics
            RW::sdurw_core
            ${FOURPLUGIN_ACCEPTANCE_QT_CORE}
    )
    target_compile_definitions(sdurws_fourplugin_acceptance_test PRIVATE
        FOURPLUGIN_ACCEPTANCE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )
    target_include_directories(sdurws_fourplugin_acceptance_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_test(
        NAME sdurws_fourplugin_acceptance_test
        COMMAND $<TARGET_FILE:sdurws_fourplugin_acceptance_test>
    )
    set_tests_properties(
        sdurws_fourplugin_acceptance_test
        PROPERTIES
            WORKING_DIRECTORY $<TARGET_FILE_DIR:sdurws_fourplugin_acceptance_test>
    )
endif()
