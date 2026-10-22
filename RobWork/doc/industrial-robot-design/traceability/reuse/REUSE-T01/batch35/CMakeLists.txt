cmake_minimum_required(VERSION 3.23)
project(ird_log_file_audit LANGUAGES CXX)
# imported目标有目录作用域：本目标的GTest/上游目标在本目录解析。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
# 只读日志审计：沿用B33已存在的库注册与依赖，不复制产品日志器或报告基础。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../batch33" "schema-probe" EXCLUDE_FROM_ALL)
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../batch33" DEFINITION IRD_SOURCE_DIR)
add_executable(ird_log_file_audit LogFileSuitabilityTest.cpp
    "${IRD_SOURCE_DIR}/diagnostics/test/LoggingTest.cpp"
    "${IRD_SOURCE_DIR}/../../../../RobWork/gtest/common/LogTestSuite.cpp"
    "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
set_target_properties(ird_log_file_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
# 原LoggingTest的源码边界用例需要同名既有宏；保持与原目标相同含义。
target_compile_definitions(ird_log_file_audit PRIVATE IRD_DIAGNOSTICS_UNIT_ROOT="${IRD_SOURCE_DIR}")
if(MSVC)
    target_compile_options(ird_log_file_audit PRIVATE /utf-8)
endif()
target_link_libraries(ird_log_file_audit PRIVATE sdurws_ird_diagnostics sdurws_ird_testkit GTest::gtest RW::sdurw_common RW::sdurw_math)
