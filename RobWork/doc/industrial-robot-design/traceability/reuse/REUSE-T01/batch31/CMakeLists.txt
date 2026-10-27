cmake_minimum_required(VERSION 3.23)
project(ird_units_audit LANGUAGES CXX)
# 审计模式显式供给真实rw数学头，保证Q重载在core和测试双方编译，不能静默漏测。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "parent-probe" EXCLUDE_FROM_ALL)
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." DEFINITION IRD_SOURCE_DIR)
target_link_libraries(sdurws_ird_core PUBLIC RW::sdurw_math)
# 复用原Units/Compare测试与原生报告器，不复制已有换算断言/测试基础。
add_executable(ird_units_audit UnitsSuitabilityTest.cpp
    "${IRD_SOURCE_DIR}/core/test/UnitsTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/CompareTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
set_target_properties(ird_units_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_units_audit PRIVATE /utf-8)
endif()
target_link_libraries(ird_units_audit PRIVATE sdurws_ird_core sdurws_ird_testkit GTest::gtest RW::sdurw_core RW::sdurw_math)
