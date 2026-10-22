cmake_minimum_required(VERSION 3.23)
project(ird_digest_audit LANGUAGES CXX)
find_package(GTest CONFIG REQUIRED)
# B30 仅审计：复用原探测的上游导出目标、core/testkit 和原生报告入口。
# 不修改产品依赖；Windows CNG 仅为本机独立数值参照，不构成跨平台后端批准。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "parent-probe" EXCLUDE_FROM_ALL)
if(NOT WIN32)
    message(FATAL_ERROR "B30 CNG候选探测仅适用于Windows；不得据此声称跨平台适配已通过")
endif()
# 父目录变量不向子目录调用方反向传播；从目录属性取得其已解析的源码路径。
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." DEFINITION IRD_SOURCE_DIR)
add_executable(ird_digest_audit DigestSuitabilityTest.cpp "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
# 原摘要7例直接编译原文件，避免把现有已知向量/生命周期测试复制成新测试基础。
target_sources(ird_digest_audit PRIVATE "${IRD_SOURCE_DIR}/core/test/DigestTest.cpp")
set_target_properties(ird_digest_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_digest_audit PRIVATE /utf-8)
endif()
target_link_libraries(ird_digest_audit PRIVATE sdurws_ird_core sdurws_ird_testkit GTest::gtest bcrypt)
