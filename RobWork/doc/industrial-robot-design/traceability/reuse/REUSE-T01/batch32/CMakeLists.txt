cmake_minimum_required(VERSION 3.23)
project(ird_event_lifetime_audit LANGUAGES CXX)
# 复用已有上游导出配置、原core/testkit和报告入口，不构建GUI或创建生产后端。
# 导入目标有目录作用域，本层须解析后再让子目录继承，不能反向引用子目录目标。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "parent-probe" EXCLUDE_FROM_ALL)
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." DEFINITION IRD_SOURCE_DIR)
# 直接编译既有execution总线和原core/上游用例，不能复制成审计专用总线。
add_executable(ird_event_lifetime_audit EventLifetimeSuitabilityTest.cpp
    "${IRD_SOURCE_DIR}/execution/src/EventBus.cpp"
    "${IRD_SOURCE_DIR}/core/test/EventsTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/ProvenanceTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp"
    "${IRD_SOURCE_DIR}/../../../../RobWork/gtest/core/EventTest.cpp")
target_include_directories(ird_event_lifetime_audit PRIVATE "${IRD_SOURCE_DIR}/execution/include")
set_target_properties(ird_event_lifetime_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_event_lifetime_audit PRIVATE /utf-8)
endif()
# 原TestMainReport经testkit包含Q，沿既有math导出目标供给Eigen包含路径。
target_link_libraries(ird_event_lifetime_audit PRIVATE sdurws_ird_core sdurws_ird_testkit GTest::gtest RW::sdurw_core RW::sdurw_math)
