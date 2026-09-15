# =====================================================================
# project 单元（.rwdesign 存储与项目生命周期平台服务）构建脚本
#
# 设计依据：
#   - units/project.md §3.3（命名空间/目标/CMake 集成：骨架 INTERFACE →
#     PRJ-T01 升级 STATIC；别名 RWS::ird::project；cxx_std_17；PUBLIC 链
#     core；`_test`/`_contract_test` 随 PRJ-T01 登记；gtest 接入＝DTB §5.5
#     定稿——vcpkg＋find_package(GTest CONFIG REQUIRED)，失败即停〔CR-07〕）、
#     §3.2（依赖图：core＋标准库＋Win32——零 Qt 含 Core〔D-01〕、零 Eigen/
#     rw::math 直接包含、diagnostics 边按注入形态不落链接、零其他单元）、
#     §12 PRJ-T01 行（产物：STATIC＋注册 `_test`/`_contract_test`；涉及文件
#     src/（空起步）；验证方式：双模式配置构建＋PRJ-TX-14 前两项）、
#     §11.2 PRJ-TX-14（构建红线全项：无 Qt 头/无私有头出 include/CMake 无
#     业务单元边/无 testkit 边）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用 C++20）、
#     §5.5（gtest 定稿：不消费 RW::gtest、不源码 vendor、测试 main 自持）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界）；红线
#     R-1/R-2/T-1（产品目标不链 testkit）
#
# 本文件随 PRJ-T01（≙WP-04-T01，任务契约 tasks/foundation/PRJ-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_project
#      不变，别名 RWS::ird::project 不变，公共 include 根不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_project_test（PRJ-TX-14 前两项＝源码
#      include 面红线扫描）与跨单元契约目标 sdurws_ird_project_contract_test
#      （PRJ-TX-14 后两项＝构建图边界：仅 project→core 一条单元边、零
#      testkit 边、diagnostics 边注入不落链接、零 io 编译期消费——O-09）；
#   3. src/ 空起步＝锚点翻译单元 Project.cpp（§12 PRJ-T01 行产物定义的
#      既定先例形态——CORE-T01/POL-T01/DIAG-T02 同款：CMake STATIC 库
#      至少一个翻译单元纪律，无任何接口预建——NFR-MNT-04）；
#   4. 陷阱处置：P-ENV-1——目标显式 cxx_std_17、不用 C++20，双模式构建即
#      C++17 与框架 C++11 基线混链验证（governance-log P-ENV-1 已随
#      CORE-T01 关闭〔F-003〕，本任务按同口径复证并留痕）；P-PR-1——
#      core.md v0.1 仍为 Draft 未冻结，本任务**仅建立 project→core 链接边、
#      零 core 头消费**（产品源码与测试源码均不 include 任何
#      sdurws/ird/core 头），core 冻结出 diff 后按影响面增量同步留痕。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即配置
# 失败（失败即停，不静默跳过测试目标——卡行"gtest 按 P-TK-1 结论"＝P-TK-1
# 定稿结论）。不消费框架的 USE_gtest/RW::gtest（从未生成），也不源码
# vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# src/Project.cpp＝落位期锚点翻译单元（§12 PRJ-T01 行"src/（空起步）"；
# STATIC 库至少一个翻译单元纪律——core/src/Core.cpp、policy/src/Policy.cpp、
# diagnostics/src/Diagnostics.cpp 同先例，零源码 STATIC 在 CMake 生成期即
# 报"No SOURCES given to target"，实测）。PRJ-T02 起的落位安排见该文件头
# 注释（win32/AtomicFile、StoreLock、TxEngine 等随同名任务文件增列）。
add_library(sdurws_ird_project STATIC
    src/Project.cpp)

# C++17（单元卡 §1.4/core.md D-01 同源；P-ENV-1 处置）：逐目标显式设定，
# 不设全局标准。target_compile_features PUBLIC 传播——project 公共头将使用
# std::filesystem/optional/variant 等 C++17 设施（§1.4），消费者编译含
# project 头的翻译单元也必须至少 C++17。不用 C++20（DTB §5.1）。
target_compile_features(sdurws_ird_project PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_project PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 project 头的
# 翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_project PUBLIC /utf-8)
endif()

# 公共头根：仅 project/include（内部含 sdurws/ird/project/）。R-2 红线：
# 私有实现头不得置于 include/——本目录是 project 唯一对外的头暴露位，
# 实现细节头（如 src/win32/AtomicFile.hpp，PRJ-T02 落地）只能放 src/ 并以
# 相对路径被实现文件包含（§3.4 布局的构建面表达）。
target_include_directories(sdurws_ird_project PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译链接依赖：仅 core（§3.2 唯一单元边——ARCH §3.5 登记行"project →
# core, diagnostics"中的 core 边，PUBLIC 形态＝§3.3 原文；alias 形态
# RWS::ird::core 在集成模式即其 ALIAS，此处用统一真名 sdurws_ird_core：
# 集成与独立冒烟两种模式下都可解析——diagnostics 落位同款取舍）。
# P-PR-1 处置：本任务仅建立该链接边、零 core 头消费（core v0.1 Draft，
# 消费面随 PRJ-T04+ 按冻结签名进入）。diagnostics 边按注入形态不落链接
# （IDiagnosticsSink 适配器注入，§3.2/§5.0——P-PR-6/P-EX-8 裁决链接形态，
# ird_gates 白名单已预登记 project->diagnostics，不因卡存在自动转链接）。
# 零 rw 基线库直链（rw::math 经 core 公共头传递，§1.4）、零 Qt（D-01 零 Qt
# 含 Core）、零其他 industrialrobot 单元（业务域协作全走注入处理器/端口，
# §6.5——R-1）、零 testkit（T-1 产品目标不链 testkit）、零 io（O-09 处置：
# 阶段 A 零 io 编译期消费，包导入导出/固化归阶段 B WP-04-T17/T18）。
target_link_libraries(sdurws_ird_project PUBLIC sdurws_ird_core)

# 别名（集成模式）：供消费方单元（execution 等——ARCH §3.5 execution→
# project 入边）以 RWS::ird::project 消费；冒烟模式无别名（与骨架期行为
# 一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::project ALIAS sdurws_ird_project)
endif()

# ---- 测试目标 sdurws_ird_project_test（随 PRJ-T01 登记，不预建于骨架）----
# 首批用例＝PRJ-TX-14 前两项的源码面扫描（§11.2：目标无 Qt 头〔NFR-MNT-01/
# D-01 零 Qt 含 Core〕、无私有头出 include〔R-2〕；另含 §1.4 零 Eigen/
# rw::math 直接包含与 include 面 sdurws/ird/* 白名单＝{core, project}——
# ARCH §3.5 唯一出边）。运行期源码扫描与文件末尾配置期守卫互为两道防线
# （diagnostics 落位同款）。PRJ-T02+ 用例按 §11 各 PRJ-TX 组增列
# （AtomicFileTest/StoreLockTest/CodecRoundTripTest 等同名测试文件）。
enable_testing()

add_executable(sdurws_ird_project_test
    test/BuildRedLineTest.cpp)

target_compile_features(sdurws_ird_project_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_project_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为 ASCII
# 不受影响）。PRIVATE 显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_project_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5/P-TK-1 定稿）：GTest::gtest_main 提供入口；测试
# 链接产品库 sdurws_ird_project 同时验证"公共头可被正常 include＋库可链接"。
# 不链 sdurws_ird_testkit：PRJ-TX-14 红线用例以 gtest 原生断言承载（需求/AT
# 追溯在用例名与 ird-test-report.json 的 trace 字段——diagnostics DT-BUILD
# 同款）；testkit 消费（FaultInterceptor/TempDir 等，§11 头注）随 PRJ-T15
# 契约测试落地时按 T-1 允许形态在对应目标登记。
target_link_libraries(sdurws_ird_project_test PRIVATE
    sdurws_ird_project
    GTest::gtest_main)

# 源码树位置注入：PRJ-TX-14 用例在运行期扫描 project 源码核对红线。
# 经编译定义传 industrialrobot 根（diagnostics/evidence/runtime 同款——值内
# 以单元名作子目录前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_project_test PRIVATE
    IRD_PROJECT_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake ≥3.21，
# 本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_project_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_project_test>
            $<TARGET_FILE_DIR:sdurws_ird_project_test>
    COMMAND_EXPAND_LISTS)

# ---- 跨单元契约测试目标 sdurws_ird_project_contract_test（随 PRJ-T01 登记）----
# §3.3 测试目标分工：`_contract_test`＝跨单元契约面（锁双实例、归档协作、
# 命令契约等——随 PRJ-T10/T14/T15 展开）。落位期首批用例＝构建图边界契约
# （PRJ-TX-14 后两项：CMake 无业务单元边〔R-1〕、无 testkit 边〔T-1〕；另含
# 唯一登记边 project→core 必须真实存在、diagnostics 边注入不落链接、零 io
# 编译期消费〔O-09〕——任务契约 acceptance 2 逐项的具名自证）。
# P-PR-1 处置：本任务零 core 头消费，diagnostics 首个用例那类"消费 core
# 类型证明 PUBLIC 传染"的行为级自证随 PRJ-T04（首个 core 头消费任务）建立，
# 此处以构建图文本扫描＋配置期守卫＋双模式链接成功承载边界证据。
add_executable(sdurws_ird_project_contract_test
    test/LinkageContractTest.cpp)

target_compile_features(sdurws_ird_project_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_project_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_project_contract_test PRIVATE /utf-8)
endif()

# 链接面：仅产品目标＋gtest（T-1 允许形态的落位期最小子集——不链 testkit，
# PRJ-T15 再登记；不显式链 core——P-PR-1 零头消费，显式链会在测试面引入
# 第二条到达 core 的路径语义，PUBLIC 传染的行为级自证留待 PRJ-T04）。
target_link_libraries(sdurws_ird_project_contract_test PRIVATE
    sdurws_ird_project
    GTest::gtest_main)

# 源码树位置注入（构建图边扫描用例输入——与 _test 目标同源）。
target_compile_definitions(sdurws_ird_project_contract_test PRIVATE
    IRD_PROJECT_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_project_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_project_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_project_contract_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_report
# （运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内定义（定义
# 先于全部调用——CMake 顺序解析）；不做跨单元共享（project 是平台服务
# 单元，不是构建工具设施提供方——NFR-MNT-04）。报告输出于构建树本目录
# gtest_reports/<target>.xml；任务留痕时拷贝入 doc/industrial-robot-design/
# traceability/（AGENTS.md §4.2 验证留痕）。报告目标不进默认构建
# （EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架"不随 ordinary build 执行
# 测试"的约定一致。
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

ird_add_gtest(sdurws_ird_project_test)
ird_add_gtest(sdurws_ird_project_contract_test)

# ---- 依赖红线守卫（配置期断言，PRJ-TX-14 的 CMake 依赖图半区）----
# project 唯一 industrialrobot 单元依赖＝core（ARCH §3.5 唯一登记编译链接
# 边）；零 Qt（NFR-MNT-01/D-01，比 L3 上限更严——含 Core 亦禁）；产品库
# 本体零 gtest（断言宏只在消费方 _test 目标展开——diagnostics D-06 同款）。
# testkit 边由 ^sdurws_ird_ 通配拦截（sdurws_ird_testkit ≠ core/project 即
# FATAL——T-1 产品目标不链 testkit）。两面读取（LINK_LIBRARIES 含 PRIVATE
# 边＋INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不会
# 误报。正式全仓门禁（依赖图白名单）归 ird_gates（WP-01-T01），本守卫不
# 替代它。
get_target_property(_ird_prj_link_libs sdurws_ird_project LINK_LIBRARIES)
get_target_property(_ird_prj_iface_libs sdurws_ird_project INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_prj_link_libs _ird_prj_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_project")
        message(FATAL_ERROR
            "project 依赖红线（R-1/T-1）：sdurws_ird_project 仅可链接 core，"
            "发现单元目标 ${_lib}（units/project.md §3.2、§11.2 PRJ-TX-14、"
            "ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "project 依赖红线（NFR-MNT-01/D-01 零 Qt 含 Core）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "project 依赖红线（产品库本体不链接 gtest）：发现 ${_lib}")
    endif()
endforeach()
