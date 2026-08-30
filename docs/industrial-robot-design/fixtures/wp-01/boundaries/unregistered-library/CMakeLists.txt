# Fixture unregistered-library: a link target outside the registered topology (WP-01 plan §2.1/§7).
# Expected diagnostic keyword: 未登记 target.
cmake_minimum_required(VERSION 3.21)
project(ird_fixture_unregistered LANGUAGES CXX)
add_library(sdurws_ird_core STATIC src/Core.cpp)
target_link_libraries(sdurws_ird_core PRIVATE some_unregistered_lib)
