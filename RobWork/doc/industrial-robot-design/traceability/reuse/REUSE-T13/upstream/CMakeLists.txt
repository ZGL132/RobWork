# REUSE-T13：仅复用上游原测试文件和既有TestMainReport，不复制测试/算法。
cmake_minimum_required(VERSION 3.23)
project(ReuseT13Upstream LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(GTest CONFIG REQUIRED)
find_package(RobWork CONFIG REQUIRED)
get_filename_component(REUSE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../../.." ABSOLUTE)
set(PRODUCT "${REUSE_ROOT}/RobWork/RobWorkStudio/src/rwslibs/industrialrobot")
add_executable(upstream_test
    "${REUSE_ROOT}/RobWork/RobWork/gtest/models/JointTest.cpp"
    "${REUSE_ROOT}/RobWork/RobWork/gtest/models/PrismaticJointTest.cpp"
    "${REUSE_ROOT}/RobWork/RobWork/gtest/math/Transform3DTest.cpp"
    "${REUSE_ROOT}/RobWork/RobWork/gtest/geometry/Model3DTest.cpp"
    "${PRODUCT}/runtime/test/TestMainReport.cpp")
target_compile_options(upstream_test PRIVATE /utf-8)
target_include_directories(upstream_test PRIVATE "${PRODUCT}/core/include" "${PRODUCT}/testkit/include")
target_link_libraries(upstream_test PRIVATE GTest::gtest RW::sdurw_models RW::sdurw_geometry
    "${REUSE_ROOT}/.cache/integrated/RobWorkStudio/libs/Release/sdurws_ird_testkit.lib"
    "${REUSE_ROOT}/.cache/integrated/RobWorkStudio/libs/Release/sdurws_ird_core.lib")
