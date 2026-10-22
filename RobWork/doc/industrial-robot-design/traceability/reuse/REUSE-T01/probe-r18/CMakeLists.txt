cmake_minimum_required(VERSION 3.23)
project(ird_policy_boundary_audit LANGUAGES CXX)
# 仅在隔离审计树复用已导出框架目标；同名别名接通原产品CMake的集成分支，
# 不修改原policy源码或产品构建，不自列另一份生产源集。
find_package(RobWork REQUIRED CONFIG COMPONENTS common models proximity)
find_package(GTest CONFIG REQUIRED)
foreach(component math kinematics models proximity)
    if(NOT TARGET sdurw_${component})
        add_library(sdurw_${component} ALIAS RW::sdurw_${component})
    endif()
endforeach()
get_filename_component(IRD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../RobWorkStudio/src/rwslibs/industrialrobot" ABSOLUTE)
add_subdirectory("${IRD_SOURCE_DIR}/core" "${CMAKE_CURRENT_BINARY_DIR}/ird-core" EXCLUDE_FROM_ALL)
add_subdirectory("${IRD_SOURCE_DIR}/testkit" "${CMAKE_CURRENT_BINARY_DIR}/ird-testkit" EXCLUDE_FROM_ALL)
add_subdirectory("${IRD_SOURCE_DIR}/policy" "${CMAKE_CURRENT_BINARY_DIR}/ird-policy" EXCLUDE_FROM_ALL)
add_executable(ird_policy_boundary_audit PolicyBoundaryTest.cpp "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
set_target_properties(ird_policy_boundary_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_policy_boundary_audit PRIVATE /utf-8)
endif()
# 复用原有会话夹具与其替身，不把Fixture拷贝成另一个测试库；原用例被注册，
# 本批仅过滤运行新增边界探测，不声称已跑原套件。
target_compile_definitions(ird_policy_boundary_audit PRIVATE IRD_POLICY_EXISTING_TEST="${IRD_SOURCE_DIR}/policy/test/CollisionSessionTest.cpp")
target_link_libraries(ird_policy_boundary_audit PRIVATE sdurws_ird_policy sdurws_ird_testkit GTest::gtest)
