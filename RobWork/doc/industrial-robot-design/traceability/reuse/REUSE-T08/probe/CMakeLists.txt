cmake_minimum_required(VERSION 3.16)
project(RpySuitabilityProbe LANGUAGES CXX)
find_package(RobWork CONFIG REQUIRED)
add_executable(rpy_suitability Probe.cpp)
set_target_properties(rpy_suitability PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_options(rpy_suitability PRIVATE /utf-8)
target_link_libraries(rpy_suitability PRIVATE RW::sdurw_math)

# 原样编译上游RPY测试，复用同一vcpkg GTest运行器；不复制任何用例或新建测试框架。
find_package(GTest CONFIG REQUIRED)
add_executable(upstream_rpy_test "C:/Users/zgl18/.codex/worktrees/reuse-audit/RobWork/RobWork/RobWork/gtest/math/RPYTest.cpp")
set_target_properties(upstream_rpy_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(upstream_rpy_test PRIVATE RW::sdurw_math GTest::gtest_main)

# 与RobWorkSetup.cmake设置一致；Quaternion含虚基接口，静态对齐改变成员偏移。
target_compile_definitions(rpy_suitability PRIVATE EIGEN_DONT_ALIGN_STATICALLY=1)
target_compile_definitions(upstream_rpy_test PRIVATE EIGEN_DONT_ALIGN_STATICALLY=1)
