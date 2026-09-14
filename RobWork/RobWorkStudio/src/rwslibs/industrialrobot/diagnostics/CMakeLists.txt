# =====================================================================
# diagnostics 单元（诊断与日志设施）构建脚本
#
# 设计依据：
#   - units/diagnostics.md §3.3（命名空间/目标/CMake 集成：骨架 INTERFACE →
#     DIAG-T02 升级 STATIC；别名 RWS::ird::diagnostics；cxx_std_17；PUBLIC
#     链 core；`_test`/`_contract_test` 随 DIAG-T02 登记）、§3.2（编译依赖仅
#     core＋标准库——零 Qt 含 Core〔D-01〕、零 Eigen/rw::math 直接包含、零
#     其他单元、ARCH §3.5 diagnostics→core 唯一出边）、§10 DT-BUILD 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿：
#     vcpkg 安装＋find_package(GTest CONFIG REQUIRED)）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界）；红线 R-1/R-2
#
# 本文件随 DIAG-T02（≙WP-09-T02，任务契约 tasks/foundation/DIAG-T02.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_diagnostics
#      不变，别名 RWS::ird::diagnostics 不变，公共 include 根不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_diagnostics_test（DT-BUILD 红线扫描）
#      与跨单元契约目标 sdurws_ird_diagnostics_contract_test（落位期＝链接
#      边界＋core 契约消费面的桩形态验证）；
#   3. 首个翻译单元为锚点 Diagnostics.cpp（src/ 空起步——§11 DIAG-T02 行
#      产物定义；注册表/目录/工厂/日志/脱敏等实现随 DIAG-T03+ 同名任务文件
#      填充）；
#   4. P-DIAG-8 处置（governance-log 登记）：契约测试以桩＋契约夹具验证，
#      不私改 execution 侧链接——execution 链接启用（EX-T01 后按 P-EX-8）
#      时补双库集成冒烟，另行登记。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即配置
# 失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::gtest
# （从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# src/Diagnostics.cpp＝落位期锚点翻译单元（§11 DIAG-T02 行"src/ 空起步"；
# STATIC 库至少一个翻译单元纪律——core/src/Core.cpp、policy/src/Policy.cpp
# 同先例）。DIAG-T03 落地 Errors.cpp（§9.0 错误类型）与 DiagCodes.cpp
# （§4.5/§4.6/§9.1 注册表＋内置码表 87 码）——同名任务文件按 §11 行增列。
# DIAG-T04 增列 Catalog.cpp（§4.2/§6/§9.7 目录＋信封＋sink）与 Factory.cpp
# （§8/§9.2 工厂＋转译注册表）及私有实现头 ParamSchema（解析单点，诊断注册
# 期验证共用）。DIAG-T05 增列 Confirmable.cpp（§5/§9.3 可确认诊断服务——
# FindingId/绑定四元组/状态机/失效条件；公共头 Confirmable.hpp 同名落位）。
# DIAG-T06 增列 Aggregation.cpp（§6.3~§6.5/§9.4 聚合器——CauseLink 链遍历/
# 聚合规则/稳定排序；公共头 Aggregation.hpp 同名落位）。DIAG-T07 增列
# Logging.cpp（§7.1~§7.5/§9.6 两级日志——管线/节流采样/轮转/WorkerLogBatch
# 重放/ILogFileOps 接缝；公共头 Logging.hpp 同名落位）。
add_library(sdurws_ird_diagnostics STATIC
    src/Diagnostics.cpp
    src/Errors.cpp
    src/DiagCodes.cpp
    src/ParamSchema.cpp
    src/Catalog.cpp
    src/Factory.cpp
    src/Confirmable.cpp
    src/Aggregation.cpp
    src/Logging.cpp)

# C++17（单元卡 D-01／core.md D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：diagnostics 公共头将使用 std::optional/variant/filesystem 等
# C++17 设施（§1.4），消费者编译 diagnostics 头也必须至少 C++17。
target_compile_features(sdurws_ird_diagnostics PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_diagnostics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 diagnostics 头
# 的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_diagnostics PUBLIC /utf-8)
endif()

# 公共头根：仅 diagnostics/include（内部含 sdurws/ird/diagnostics/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 diagnostics 唯一对外的
# 头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_diagnostics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：仅 core（§3.2 唯一单元边，PUBLIC——诊断信封/服务公共头内嵌 core
# 承载的 DiagnosticRecord/ConfirmableFinding 等契约类型，消费方必须随链到达）。
# 零 rw 基线库直链（零 Eigen/rw::math 直接包含——§1.4，rw 类型仅经 core 头
# 传递）、零 Qt（D-01）、零其他 industrialrobot 单元（ARCH §3.5 出边唯一）。
# 用统一真名 sdurws_ird_core：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::core 仅集成模式存在——core/evidence 落位时同款取舍）。
target_link_libraries(sdurws_ird_diagnostics PUBLIC sdurws_ird_core)

# 别名（集成模式）：供消费方单元以 RWS::ird::diagnostics 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::diagnostics ALIAS sdurws_ird_diagnostics)
endif()

# ---- 测试目标 sdurws_ird_diagnostics_test（随 DIAG-T02 登记，不预建于骨架）----
# 首批用例＝DT-BUILD 红线扫描（units/diagnostics.md §10 DT-BUILD 行：零 Qt/
# 零 Eigen·rw 直接包含/依赖图仅 core 边/无私有头出 include——运行期源码
# 扫描，与文件末尾配置期守卫互为两道防线）。DT-REG-1~5 矩阵用例随
# DIAG-T03 增列（StableCodeRegistryTest.cpp——§10）；DT-DIAG-1/2、DT-DUP-1/2、
# DT-CAT-1/2 及转译器/sink 用例随 DIAG-T04 增列（CatalogFactoryTest.cpp）。
# DT-CFM-1~5（服务端状态机全转移/绑定复核/失效路径/超时半区）随 DIAG-T05 增列
# （ConfirmableTest.cpp——§10）；DT-CFM-6~10 的 project 桩联动半区入
# _contract_test（ConfirmableProjectStubTest.cpp——P-DIAG-8 处置：桩验证不私改
# 对端链接）。DT-AGG-1~5/DT-CHAIN-1（聚合视图/稳定排序/不升级反例/原因链
# 遍历＋P-DIAG-1 钉住）随 DIAG-T06 增列（AggregationTest.cpp——§10）。
# DT-LOG-1~5（两级分流/稳定序/写失败注入/禁用独立性/WorkerLogBatch 重放）
# ＋节流采样/轮转/溢出/截断行为面＋P-DIAG-7/P-EX-8 处置钉住随 DIAG-T07 增列
# （LoggingTest.cpp——§10；故障注入经 ILogFileOps 接缝自持替身，不链 testkit）。
enable_testing()

add_executable(sdurws_ird_diagnostics_test
    test/BuildRedLineTest.cpp
    test/StableCodeRegistryTest.cpp
    test/CatalogFactoryTest.cpp
    test/ConfirmableTest.cpp
    test/AggregationTest.cpp
    test/LoggingTest.cpp)

target_compile_features(sdurws_ird_diagnostics_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_diagnostics_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为 ASCII
# 不受影响）。diagnostics 的 PUBLIC /utf-8 经链接传播亦可覆盖，显式再设一次
# 以自洽——测试目标应独立可读，不依赖被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_diagnostics_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5）：GTest::gtest_main 提供入口；测试链接产品库
# sdurws_ird_diagnostics 同时验证"公共头可被正常 include＋库可链接"。
# 不链 sdurws_ird_testkit：DT-BUILD 红线用例以 gtest 原生断言承载（需求/AT
# 追溯在用例名与 ird-test-report.json 的 trace 字段——evidence EV-T01 同款）；
# testkit 谓词（IRD_TEST_INFO/checkDiagnosticRecord）随 DIAG-T10 契约套件
# 消费，届时按 P-DIAG-8 处置登记其链接面。
target_link_libraries(sdurws_ird_diagnostics_test PRIVATE
    sdurws_ird_diagnostics
    GTest::gtest_main)

# 源码树位置注入：DT-BUILD 用例在运行期扫描 diagnostics 源码核对红线
# （§10 DT-BUILD）。经编译定义传 industrialrobot 根（evidence/runtime 同款
# ——值内以单元名作子目录前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_diagnostics_test PRIVATE
    IRD_DIAGNOSTICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake ≥3.21，
# 本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_diagnostics_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_diagnostics_test>
            $<TARGET_FILE_DIR:sdurws_ird_diagnostics_test>
    COMMAND_EXPAND_LISTS)

# ---- 跨单元契约测试目标 sdurws_ird_diagnostics_contract_test（随 DIAG-T02 登记）----
# §10 测试目标分工：`_test`＝单元内行为；`_contract_test`＝跨单元契约面
# （§3.3：与 core 值类型、与 testkit 谓词、与 project/execution sink 形态
# 对齐）。落位期首批用例＝链接边界契约（PUBLIC 链 core 的传染性自证＋构建
# 图仅 core 一条单元边）＋P-DIAG-8 处置的桩消费方形态验证（LinkageContract
# Test.cpp 文件头有完整处置说明）；DT-CFM-6~10（UI 关闭/未确认提交/worker 禁令
# /确认≠成功/确认留痕形状——project §6.7 桩联动）随 DIAG-T05 增列
# （ConfirmableProjectStubTest.cpp）。
add_executable(sdurws_ird_diagnostics_contract_test
    test/LinkageContractTest.cpp
    test/ConfirmableProjectStubTest.cpp)

target_compile_features(sdurws_ird_diagnostics_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_diagnostics_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_diagnostics_contract_test PRIVATE /utf-8)
endif()

# 链接面（T-1 允许形态：同单元产品目标＋gtest）：注意**不显式链 core**——
# LinkageContractTest 的 PublicCoreExposure 用例正是以"消费 TU 零显式 core
# 链接"为前提，行为级证明 PUBLIC 链接接口的传染性（链 diagnostics 即获得
# core 承载的诊断契约类型——未来 project/execution/io/ui/reporting 消费方
# 的依赖形态）。
target_link_libraries(sdurws_ird_diagnostics_contract_test PRIVATE
    sdurws_ird_diagnostics
    GTest::gtest_main)

# 源码树位置注入（构建图边扫描用例输入——与 _test 目标同源）。
target_compile_definitions(sdurws_ird_diagnostics_contract_test PRIVATE
    IRD_DIAGNOSTICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_diagnostics_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_diagnostics_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_diagnostics_contract_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_report
# （运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内定义（定义
# 先于全部调用——CMake 顺序解析）；不做跨单元共享（diagnostics 不是构建
# 工具设施提供方——NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/
# <target>.xml；任务留痕时拷贝入 doc/industrial-robot-design/traceability/
# （AGENTS.md §4.2 验证留痕）。报告目标不进默认构建（EXCLUDE_FROM_ALL）：
# 跑测试是显式动作，与框架"不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_diagnostics_test)
ird_add_gtest(sdurws_ird_diagnostics_contract_test)

# ---- 依赖红线守卫（配置期断言，DT-BUILD 的 CMake 依赖图半区）----
# diagnostics 唯一 industrialrobot 单元依赖＝core（ARCH §3.5 唯一出边）；
# 零 Qt（R-3/NFR-MNT-01，比 L3 上限更严——D-01 明示含 Core 亦禁）；产品库
# 本体零 gtest（D-06——断言宏只在消费方 _test 目标展开）。两面读取
# （LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）
# 防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND" 字面串，
# 不匹配任何模式，不会误报。正式全仓门禁（依赖图白名单）归 ird_gates
# （WP-01-T01），本守卫不替代它。
get_target_property(_ird_diag_link_libs sdurws_ird_diagnostics LINK_LIBRARIES)
get_target_property(_ird_diag_iface_libs sdurws_ird_diagnostics INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_diag_link_libs _ird_diag_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_diagnostics")
        message(FATAL_ERROR
            "diagnostics 依赖红线（R-1/R-2）：sdurws_ird_diagnostics 仅可链接 "
            "core，发现单元目标 ${_lib}（units/diagnostics.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "diagnostics 依赖红线（R-3/NFR-MNT-01，D-01 零 Qt 含 Core）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "diagnostics 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
