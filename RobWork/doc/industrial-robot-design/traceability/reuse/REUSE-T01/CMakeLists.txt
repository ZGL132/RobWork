cmake_minimum_required(VERSION 3.23)
project(ird_reuse_audit_probe LANGUAGES CXX)
# 只读审计探测：复用上游导出目标及现有 GTest，不注册产品后端或复制算法。
find_package(RobWork REQUIRED CONFIG COMPONENTS common invkin models trajectory pathplanning)
find_package(GTest CONFIG REQUIRED)
# 已安装同渠道工具仅用于审计原语/限制，不增加供给渠道或修改产品依赖。
find_package(boost_endian CONFIG REQUIRED)
find_package(boost_property_tree CONFIG REQUIRED)
# 审计只消费现有 Sim 导出目标，不需要拉起 Studio/Widgets 包；路径由调用者显式给定。
include("${RWSIM_TARGETS_DIR}/sdurwsim.cmake")
include("${RWSIM_TARGETS_DIR}/sdurwsim_bullet.cmake")
# 直接使用已有 testkit 和报告入口，避免审计探测再造一套 JSON 报告器。
# EXCLUDE_FROM_ALL 只构建本探测实际依赖；不运行或注册产品 GUI。
get_filename_component(IRD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../RobWorkStudio/src/rwslibs/industrialrobot" ABSOLUTE)
add_subdirectory("${IRD_SOURCE_DIR}/core" "${CMAKE_CURRENT_BINARY_DIR}/ird-core" EXCLUDE_FROM_ALL)
add_subdirectory("${IRD_SOURCE_DIR}/testkit" "${CMAKE_CURRENT_BINARY_DIR}/ird-testkit" EXCLUDE_FROM_ALL)
add_executable(ird_reuse_audit_probe UpstreamSuitabilityTest.cpp ToolSuitabilityTest.cpp DependencySuitabilityTest.cpp TestHarnessSuitabilityTest.cpp PlanningSuitabilityTest.cpp "${IRD_SOURCE_DIR}/core/test/TestMainReport.cpp")
set_target_properties(ird_reuse_audit_probe PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES)
if(MSVC)
    target_compile_options(ird_reuse_audit_probe PRIVATE /utf-8)
endif()
target_link_libraries(ird_reuse_audit_probe PRIVATE RW::sdurw_common RW::sdurw_invkin RW::sdurw_models RW::sdurw_trajectory RW::sdurw_pathplanning RWSIM::sdurwsim RWSIM::sdurwsim_bullet GTest::gtest Boost::endian Boost::property_tree sdurws_ird_testkit)
