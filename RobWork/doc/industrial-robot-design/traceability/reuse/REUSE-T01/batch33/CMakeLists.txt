cmake_minimum_required(VERSION 3.23)
project(ird_diagnostic_schema_audit LANGUAGES CXX)
# 本目录解析导入目标，复用原产品库、原用例和原生报告器，不复制诊断服务。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
find_package(boost_property_tree CONFIG REQUIRED)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "parent-probe" EXCLUDE_FROM_ALL)
get_directory_property(IRD_SOURCE_DIR DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.." DEFINITION IRD_SOURCE_DIR)
add_subdirectory("${IRD_SOURCE_DIR}/diagnostics" "ird-diagnostics" EXCLUDE_FROM_ALL)
add_executable(ird_diagnostic_schema_audit DiagnosticSchemaSuitabilityTest.cpp
    "${IRD_SOURCE_DIR}/diagnostics/test/StableCodeRegistryTest.cpp"
    "${IRD_SOURCE_DIR}/diagnostics/test/CatalogFactoryTest.cpp"
    "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
# 私有头仅在本审计目标内读取同一单元的原辅助；不把该接缝变为产品公共接口。
target_include_directories(ird_diagnostic_schema_audit PRIVATE "${IRD_SOURCE_DIR}/diagnostics/src")
set_target_properties(ird_diagnostic_schema_audit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_diagnostic_schema_audit PRIVATE /utf-8)
endif()
target_link_libraries(ird_diagnostic_schema_audit PRIVATE sdurws_ird_diagnostics sdurws_ird_testkit GTest::gtest RW::sdurw_math Boost::property_tree)
