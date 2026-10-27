cmake_minimum_required(VERSION 3.23)
project(ird_log_pipeline_audit LANGUAGES CXX)
# 继续复用同一审计注册链；目标直接编译原日志/线程测试，不拷贝上游实现。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../batch35" "file-probe" EXCLUDE_FROM_ALL)
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../batch35" DEFINITION IRD_SOURCE_DIR)
add_executable(ird_log_pipeline_audit LogPipelineSuitabilityTest.cpp
    "${IRD_SOURCE_DIR}/diagnostics/test/LoggingTest.cpp"
    "${IRD_SOURCE_DIR}/../../../../RobWork/gtest/common/ThreadTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
set_target_properties(ird_log_pipeline_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
target_compile_definitions(ird_log_pipeline_audit PRIVATE IRD_DIAGNOSTICS_UNIT_ROOT="${IRD_SOURCE_DIR}")
if(MSVC)
    target_compile_options(ird_log_pipeline_audit PRIVATE /utf-8)
endif()
target_link_libraries(ird_log_pipeline_audit PRIVATE sdurws_ird_diagnostics sdurws_ird_testkit GTest::gtest RW::sdurw_common RW::sdurw_math)
