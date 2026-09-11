# =====================================================================
# runtime 单元（运动学编译内核）构建脚本
#
# 设计依据：
#   - units/runtime.md §3.4（命名空间/目标/CMake 集成）、§3.2（依赖：core＋
#     L1 RobWork 基线库〔rw kinematics/models＋rwsim dynamics——P-RT-3〕，零 Qt、
#     零 industrialrobot 其他单元）、§12 RT-T01 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿）、
#     §4.6（L1 基线库目标名实测表——本任务实测核对回登）
#   - 需求 ARC-03（规范真值唯一）、MDL-22；红线 R-3（零 Qt）、R-1/R-2
#
# 本文件随 RT-T01（≙WP-06-T01，任务契约 tasks/foundation/RT-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_runtime 不变，
#      别名 RWS::ird::runtime 不变，公共 include 根 runtime/include/ 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_runtime_test（构建红线扫描＋L1 链接清单
#      钉住——P-RT-3 消账输入）；
#   3. 首个翻译单元为占位 Runtime.cpp（与 core/Core.cpp 同例——真实接口随
#      RT-T02+ 落地，本任务只做构建落位）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（RT-T01）→ 契约源随 RT-T02 起增列 ----
add_library(sdurws_ird_runtime STATIC
    src/Runtime.cpp             # 单元锚点（RT-T01 占位，保留）
    src/Errors.cpp              # RT-T02：错误码全表（token/稳定码/分类）＋RuntimeError
    src/DescriptionValidator.cpp) # RT-T03：S3 结构与单位校验器（§5.2 S3——含耦合
                                  # 矩阵奇异/病态判定〔单侧 Jacobi SVD〕；私有头
                                  # src/DescriptionValidator.hpp，不入公共 include）

# ---- RT-T04 增列（CanonicalModel＋RT-Codec；tasks/foundation/RT-T04.json）----
target_sources(sdurws_ird_runtime PRIVATE
    src/CanonicalModel.cpp      # §4.3 字段表构造不变量（builder）＋§9.6 能力派生
                                # ＋§4.3.6 位模式等值；公共头 CanonicalModel.hpp
    src/Codec.cpp)              # §4.5 RT-Codec 三形态（encode/parse/内容身份；
                                # CR-02 排除字段声明点＝Codec.hpp；摘要只经
                                # core::ContentDigester）

# ---- RT-T05 增列（RuntimeNameMap；tasks/foundation/RT-T05.json）----
target_sources(sdurws_ird_runtime PRIVATE
    src/NameMap.cpp)            # §7 名称映射：生成/合法化/消歧/双射/内容身份
                                # ＋S8 交叉校验＋⑥端口适配（BoundRuntimeNameResolver）；
                                # 公共头 NameMap.hpp——R-4 前缀拼装唯一合法位置
                                # （joinScopeLocal 单点，NFR-MNT-07 例外实现文件）；
                                # IRDNAME 编解码随本任务落位 Codec.cpp（§7.6）

# ---- RT-T06 增列（基座—世界变换规则；tasks/foundation/RT-T06.json）----
target_sources(sdurws_ird_runtime PRIVATE
    src/BaseWorldTransform.cpp) # §6 规则面：安装预设→矩阵（P-RT-4 冻结值）、
                                # Custom EAA→R（Rodrigues）、T_world_base 正解
                                # （唯一产生入口）/矩阵层校验/预设一致性（S5）/
                                # 正反解（点变换/重力投影/FK 组合）/S9 一致性
                                # 判定（二次叠加检测）；公共头
                                # BaseWorldTransform.hpp（§3.1 模块表）；
                                # 全部矩阵运算逐元素实现——零 rw 外联符号
                                # （冒烟 header-only 纪律，见实现文件头）

target_compile_features(sdurws_ird_runtime PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_runtime PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sdurws_ird_runtime PUBLIC /utf-8)
endif()

# 公共头根：仅 runtime/include（R-2 纪律同 core/testkit）。
target_include_directories(sdurws_ird_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ---- L1 基线库链接（P-RT-3：rw kinematics/models＋rwsim dynamics）----
# sdurw_math 经 sdurws_ird_core 的 PUBLIC 边传递获得（core 是唯一 industrialrobot
# 编译依赖——ARCH §3.5）。
# 集成/冒烟判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在——
# CORE-T01 同款探测；顶层目录比较在单元子目录不可用）。集成模式：kinematics/
# models 直接 TARGET 链接；sdurwsim 因顶层顺序（RobWorkStudio 先于 RobWorkSim
# 配置）尚无目标，按名称链接（CMake 生成期解析）。
#
# ★ 冒烟模式自 RT-T03 起引入 rw **模板头**（header-only）include 路径：
#   §4.2 Description 的字段类型即 rw::math 值类型（单一类型形态——两模式
#   不允许分叉，否则同一 Description 在两模式间类型不一致）。rw 的
#   math/common/core 头为模板/内联为主的自包含闭包（外部依赖仅 Eigen——
#   经 vcpkg Eigen3 提供）；冒烟编译只实例化模板内联代码，不引用任何
#   框架库符号，故仍不链接 rw/rwsim（L1 清单保持空——BaselineLibsPinned
#   钉住）。约束（实现纪律）：冒烟可达代码不得调用 Rotation3D::identity()
#   等外联符号（构造一律逐元素构造函数）；集成模式无此约束。
set(IRL_BASELINE_LIBS "")
if(TARGET sdurw_kinematics)
    target_link_libraries(sdurws_ird_runtime PUBLIC
        sdurw_kinematics
        sdurw_models
        sdurwsim)
    set(IRL_BASELINE_LIBS "sdurw_kinematics, sdurw_models, sdurwsim")
else()
    # 冒烟分支：rw 源码树头路径（模板头；本目录相对仓库根 RobWork/RobWork/src
    # 的固定布局）＋Eigen（vcpkg 提供 Eigen3 配置包；rw 头以 <Eigen/Core> 引用）。
    set(IRL_RW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../RobWork/src")
    if(NOT EXISTS "${IRL_RW_SRC_DIR}/rw/math/Vector3D.hpp")
        message(FATAL_ERROR
            "runtime 冒烟模式：未找到 rw 模板头（期望 ${IRL_RW_SRC_DIR}/rw/math/"
            "Vector3D.hpp）——RT-T03 起 Description.hpp 需要 rw::math 头"
            "（header-only），请勿移动仓库目录结构")
    endif()
    target_include_directories(sdurws_ird_runtime PUBLIC "${IRL_RW_SRC_DIR}")
    find_package(Eigen3 CONFIG QUIET)
    if(TARGET Eigen3::Eigen)
        # Eigen 为 header-only——只取 include 目录，不把 Eigen3::Eigen 放入
        # LINK_LIBRARIES：ird_gates 的 LIB 门禁按链接清单核对（框架外目标
        # 即命中），而 header-only 依赖的语义就是"头可达"而非"库链接"。
        get_target_property(IRL_EIGEN_INC Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT IRL_EIGEN_INC)
            message(FATAL_ERROR
                "runtime 冒烟模式：Eigen3::Eigen 无 INTERFACE_INCLUDE_DIRECTORIES"
                "——vcpkg eigen3 配置包异常（RT-T03 冒烟 header-only 机制）")
        endif()
        target_include_directories(sdurws_ird_runtime PUBLIC ${IRL_EIGEN_INC})
    else()
        message(FATAL_ERROR
            "runtime 冒烟模式：未找到 Eigen3（vcpkg eigen3）——rw 数学头依赖"
            " <Eigen/Core>（RT-T03 冒烟 header-only 机制）")
    endif()
endif()

target_link_libraries(sdurws_ird_runtime PUBLIC sdurws_ird_core)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::runtime ALIAS sdurws_ird_runtime)
endif()

# ---- 测试目标 sdurws_ird_runtime_test（随 RT-T01 登记）----
enable_testing()

add_executable(sdurws_ird_runtime_test
    test/BuildRedLineTest.cpp           # RT-T01：构建红线扫描＋L1 清单钉住
    test/ErrorsTest.cpp                 # RT-T02：RT-ERR 全表（token/稳定码/分类逐值）＋异常轨＋错误载荷
    test/SourcesTest.cpp                # RT-T02：Sources 注入契约语义（§3.3）＋错误归属可定位（§5.2 表）
    test/ExpectedTest.cpp               # RT-T02：Expected 两态语义（§3.4——成功/失败不静默吞错）
    test/DescriptionTest.cpp            # RT-T03：§4.2 值类型契约（默认态/值语义/四态/reader 纯函数）
    test/DescriptionValidatorTest.cpp   # RT-T03：S3 校验器（RT-CPL-1/RT-CPX-3/RT-BW-6 校验器部分＋硬反例逐条）
    test/CanonicalModelTest.cpp         # RT-T04：§4.3 builder 不变量＋§9.6 能力派生＋索引＋D-01 不持久化
    test/CodecTest.cpp                  # RT-T04：RT-ID-1/2/3＋parse(encode)==x 往返＋身份域排除
    test/NameMapTest.cpp                # RT-T05：RT-NM-1～7（AT-18 双向往返/消歧/未知名/
                                        # 跨项目/重命名/双前缀/身份不混用）＋R-4 源码扫描
                                        # （ird_add_gtest 宏登记于文件末尾）
    test/BaseWorldTransformTest.cpp)    # RT-T06：RT-BW-1/2/3/6 规则部分（§6.5 数值例/
                                        # 四预设精确矩阵/默认地面/非法变换反例）
                                        # ＋S5 预设一致性＋S9 二次叠加反例（acceptance 2）
target_compile_features(sdurws_ird_runtime_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_runtime_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_runtime_test PRIVATE /utf-8)
endif()

target_link_libraries(sdurws_ird_runtime_test PRIVATE
    sdurws_ird_runtime
    GTest::gtest_main)

# 校验器私有实现头（src/DescriptionValidator.hpp）对单元内测试可见——
# §3.1 把校验器列在 src（非模板实现），R-2 只禁跨单元私有头（RT-T11 编译器
# 同样经 src 路径消费；测试与其同权）。
target_include_directories(sdurws_ird_runtime_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 源码树位置与 L1 链接清单注入（红线扫描用例输入；清单＝P-RT-3 消账输入——
# 运行期断言"清单与声明一致"，集成三库、冒烟空串）。
target_compile_definitions(sdurws_ird_runtime_test PRIVATE
    IRD_RUNTIME_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_RUNTIME_BASELINE_LIBS="${IRL_BASELINE_LIBS}")

add_custom_command(TARGET sdurws_ird_runtime_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_runtime_test>
            $<TARGET_FILE_DIR:sdurws_ird_runtime_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
macro(ird_add_gtest target)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    add_custom_target(
        ${target}_report
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/gtest_reports
        COMMAND $<TARGET_FILE:${target}>
                --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/gtest_reports/${target}.xml
        DEPENDS ${target}
        COMMENT "ird: run ${target} and write gtest XML report")
    set_target_properties(${target}_report PROPERTIES
        EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endmacro()

ird_add_gtest(sdurws_ird_runtime_test)

# ---- 依赖红线守卫（T 系/R 系的 runtime 侧配置期断言）----
# runtime 唯一 industrialrobot 编译依赖＝core（ARCH §3.5）；L1 基线库白名单
# ＝P-RT-3 清单（kinematics/models/sdwsim 拼写以实际目标为准——见上方实测循环）。
# 出现任何其他 industrialrobot 单元目标或 Qt 目标即配置失败。
get_target_property(_ird_rt_link_libs sdurws_ird_runtime LINK_LIBRARIES)
get_target_property(_ird_rt_iface_libs sdurws_ird_runtime INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_rt_link_libs _ird_rt_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_runtime")
        message(FATAL_ERROR
            "runtime 依赖红线：sdurws_ird_runtime 仅可链接 core＋L1 基线库，"
            "发现单元目标 ${_lib}（units/runtime.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "runtime 依赖红线（R-3/NFR-MNT-01）：runtime 禁止链接 Qt，发现 ${_lib}")
    endif()
endforeach()
