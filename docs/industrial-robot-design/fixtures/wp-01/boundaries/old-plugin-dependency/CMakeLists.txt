# Fixture old-plugin-dependency: a core library links a legacy plugin target (WP-01 plan §7 rule 2).
# Expected diagnostic keyword: 旧插件目标依赖.
cmake_minimum_required(VERSION 3.21)
project(ird_fixture_old_plugin LANGUAGES CXX)
add_library(sdurws_ird_core STATIC src/Core.cpp)
target_link_libraries(sdurws_ird_core PUBLIC sdurws_robotmodelbuilder)
