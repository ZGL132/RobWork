cmake_minimum_required(VERSION 3.20)
project(setcheck_perf_probe LANGUAGES CXX)
# REUSE-T19 补正轮性能对照探针（measurement-only）：最小产品源集直接编译
# （SetCheckMatch/ToleranceProfile/JsonLite/Dataset/Compare——零产品库链接、
# 零上游 RobWork、零 vcpkg 工具链；Boost 仅头文件路径）。基线 Kuhn 为
# 691cbd42e 逐字快照（probe TU 内，标注 measurement-only，非产品 fallback）。
# 不注册进产品/测试目标，不参与 ird_gates 产品树扫描。
add_executable(setcheck_perf_probe perf_probe.cpp)

set(IRD_PRODUCT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../../../RobWorkStudio/src/rwslibs/industrialrobot" CACHE PATH
    "industrialrobot 产品根（perf-compare 上级 7 层至 RobWork 后接 RobWorkStudio/src/rwslibs/industrialrobot）")
# 归一化：原始含 7 层".."的路径串超 MAX_PATH，cmd/cl 拒绝（C1083）——
# 展开为干净绝对路径后再供 include/sources 使用。
get_filename_component(IRD_PRODUCT_ROOT "${IRD_PRODUCT_ROOT}" ABSOLUTE)
set(BOOST_INCLUDEDIR "" CACHE PATH
    "已安装 Boost 头目录（vcpkg installed/x64-windows/include）")

target_compile_features(setcheck_perf_probe PRIVATE cxx_std_17)
set_target_properties(setcheck_perf_probe PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(setcheck_perf_probe PRIVATE /utf-8)
endif()

target_include_directories(setcheck_perf_probe PRIVATE
    "${IRD_PRODUCT_ROOT}/testkit/include"
    "${IRD_PRODUCT_ROOT}/core/include"
    "${BOOST_INCLUDEDIR}")

# 产品 TU 最小源集（按相对路径引用，不复制）：接缝 TU（Boost 唯一编译面）
# ＋profile/JSON 链＋TestKitError（TestPaths.cpp）＋core C4 比较原语。
target_sources(setcheck_perf_probe PRIVATE
    "${IRD_PRODUCT_ROOT}/testkit/src/SetCheckMatch.cpp"
    "${IRD_PRODUCT_ROOT}/testkit/src/ToleranceProfile.cpp"
    "${IRD_PRODUCT_ROOT}/testkit/src/JsonLite.cpp"
    "${IRD_PRODUCT_ROOT}/testkit/src/Dataset.cpp"
    "${IRD_PRODUCT_ROOT}/testkit/src/TestPaths.cpp"
    "${IRD_PRODUCT_ROOT}/core/src/Compare.cpp"
    "${IRD_PRODUCT_ROOT}/core/src/Units.cpp"
    "${IRD_PRODUCT_ROOT}/core/src/Digest.cpp")

# TestPaths.cpp 编译期默认数据根（探针不读黄金数据，仅为满足该 TU 的
# fail-fast 宏要求；运行期环境变量未设即不触达）。
target_compile_definitions(setcheck_perf_probe PRIVATE
    IRD_TESTDATA_DEFAULT_DIR="${IRD_PRODUCT_ROOT}/testdata")

# psapi：GetProcessMemoryInfo（峰值工作集参照）。
target_link_libraries(setcheck_perf_probe PRIVATE psapi)
