cmake_minimum_required(VERSION 3.23)
project(ird_code_manifest_audit LANGUAGES CXX)
# 沿用B33真实库和原测试注册，避免重复一份诊断服务/测试main。
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../batch33" "schema-probe" EXCLUDE_FROM_ALL)
target_sources(ird_diagnostic_schema_audit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/CodeManifestSuitabilityTest.cpp")
