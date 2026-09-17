# =====================================================================
# io 单元（项目 I/O：路径安全/CSV/JSON/资源读取/.rwpack 包）构建脚本
#
# 设计依据：
#   - units/io.md §3.3（命名空间/目标/CMake 集成：STATIC 库 sdurws_ird_io；
#     别名 RWS::ird::io；测试目标 sdurws_ird_io_test＋sdurws_ird_io_contract_
#     test——googletest 经 vcpkg find_package(GTest CONFIG REQUIRED)；零 Qt、
#     零 Widgets）、§3.4（源码目录布局）、§2.1（拥有/不拥有总表）、
#     §12 IO-T01/T02 行（产物与验收）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项：L2 计算内核零 Qt——R-3）、§5.5（gtest
#     接入定稿）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界——无私有
#     头出 include）；红线 R-1（业务单元互链禁止）/R-2（跨单元私有头
#     禁止）/T-1（产品目标不链 testkit）
#
# 本文件随 IO-T01（≙WP-11-T02）落地、随 IO-T02（≙WP-11-T03）扩展：
#   1. IO-T01：骨架期 INTERFACE 占位升级为 STATIC 库（目标名/别名/include
#      根不变）；公共头 IoFwd.hpp/IoError.hpp；锚点翻译单元 Io.cpp；
#      P-IO-3 候选库仅验证可用性不登记链接目标（选型冻结权在 WP-11 评审）。
#   2. IO-T02：src/ 增列 SafePath.cpp/Budget.cpp/IoDiagnostics.cpp
#      （§3.4 布局同名任务文件）；公共头增列 SafePath.hpp/Budget.hpp/
#      IoDiagnostics.hpp（§3.1 表对应行）；**sdurws_ird_io_test 随本任务
#      登记**——任务契约 verify 命令指向该目标、acceptance 1/2/3 要求
#      V09/V10/V11/V06 全绿（§11.2 矩阵），故测试目标（含 gtest 接入）
#      提前于 IO-T06 落位为"已含真实用例的目标"，不是骨架期禁止的
#      "预建空测试目标"（§12 IO-T01 禁止项语义＝无内容的占位目标；
#      IO-T06/T07 届时增列资源/包/契约面用例与 _contract_test 目标）。
#   3. IO-T03（≙WP-11-T04）：src/ 增列 Csv.cpp；公共头增列 Csv.hpp
#      （§3.1 表 Csv.hpp 行）；测试目标增列 test/CsvTest.cpp（V01~V05
#      ＋方言封闭集/预算接线/数据-only 用例——契约 acceptance 1~5）。
#   4. IO-T04（≙WP-11-T05）：src/ 增列 Json.cpp/ZipChannel.cpp（§3.4
#      布局同名任务文件）；公共头增列 Json.hpp/ZipChannel.hpp（§3.1 表
#      Json.hpp 行＋ZipChannel.hpp 行——后者为 DTB §5.4 登记的文件名等
#      价增补，io.md §15.5 v0.7）；P-IO-3 冻结选型（ZIP=libzip 1.11.4、
#      XML=expat 2.8.3——io.md §15.3/governance-log §1.9）的 L1 目标
#      链接登记落位（§3.3"链接 …＋L1 文件/XML 目标（选型 P-IO-3 冻结
#      后登记）"；§15.3 R-IO-1 行"L1 文件/XML 目标链接登记随 IO-T04"）；
#      测试目标增列 test/JsonTest.cpp（V07/V08＋canonical/摘要——契约
#      acceptance 1~3）与 test/ZipChannelTest.cpp（逐字节还原＋哈希校验
#      ——acceptance 4，恶意 zip 构造器为 §11.1 登记的测试基础设施）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随 IO-T02 首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::gtest
# （从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- L1 第三方依赖（P-IO-3 冻结选型——io.md §15.3/§3.3，随 IO-T04 登记）----
# ZIP＝libzip 1.11.4（ZipChannel.cpp 消费——§7.1 容器层/zip64/加密检测）；
# XML＝expat 2.8.3（§6.2 URDF/Xacro XML 设施——消费方 ResourceIo.cpp 随
# IO-T05 落位，链接面按 §15.3 R-IO-1"链接登记随 IO-T04"先行登记，避免
# 下一任务再动本文件）。两库均经 vcpkg x64-windows 经典模式（仓库根 vendored
# ports 树为准——NFR-DEP-03 离线/NFR-SEC-05 依赖清单），REQUIRED＝不可用
# 即配置失败（失败即停，不私选第二渠道——acceptance 5 红线）。
find_package(libzip CONFIG REQUIRED)
find_package(expat CONFIG REQUIRED)

# ---- 产品库目标：STATIC（目标名不变，DTB §5.1）----
# src/Io.cpp＝IO-T01 锚点翻译单元（先例：core/src/Core.cpp 等）；
# IO-T02 增列 SafePath.cpp（路径安全核——§4.2/§4.3/§9.1）、Budget.cpp
# （预算守卫——§4.5/§9.2）、IoDiagnostics.cpp（IO-* 码表注册＋比较型
# 三要素助手——§9.12/P-IO-6）。IO-T03 增列 Csv.cpp（§5/§9.3/§9.4——
# 方言标识行＋可逆转义＋流式读取/逐行错误定位＋canonical 写出，任务卡
# §12 IO-T03 行同名任务文件）。IO-T04 增列 Json.cpp（§5.9/§9.5——受限
# DOM＋JsonProfile 校验＋canonical 写出＋内容摘要，同名任务文件）与
# ZipChannel.cpp（§7.1——ZIP 容器层读取/逐字节还原/manifest 哈希校验；
# ZipChannel.hpp 为 §3.4 布局的同名公共头——文件名等价增补见 io.md
# §15.5 v0.7）。后续按 §3.4 布局继续增列：
# ResourceIo.cpp（IO-T05）、Package.cpp/TempArea.cpp/AtomicFile.cpp（IO-T06）。
add_library(sdurws_ird_io STATIC
    src/Io.cpp
    src/SafePath.cpp
    src/Budget.cpp
    src/IoDiagnostics.cpp
    src/Csv.cpp
    src/Json.cpp)

# C++17（单元卡 §1.4／core.md D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：io 公共头使用 C++17 设施（std::string_view/inline constexpr
# 变量等——§1.4），消费者编译 io 头也必须至少 C++17。
target_compile_features(sdurws_ird_io PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_io PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 io 头的翻译
# 单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_io PUBLIC /utf-8)
endif()

# 公共头根：仅 io/include（内部含 sdurws/ird/io/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 io 唯一对外的头暴露
# 位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics（§3.2 依赖表仅有的两条单元边，与 ARCH §3.5
# 白名单、ird_gates_whitelist.cmake "io->core"/"io->diagnostics" 行一致）。
# PUBLIC 形态：IoDiagnostics.hpp 内嵌 diagnostics 承载的 CodeDescriptor/
# IDiagnosticRegistry 契约类型（P-IO-6 注册协议——§10.3），消费方必须随
# 链到达。L1（libzip/expat——P-IO-3 冻结）PRIVATE：实现细节（zip.h/expat.h
# 不出现在任何 io 公共头——ZipChannel.hpp 文件头注），STATIC 库的 PRIVATE
# 链接依赖经 $<LINK_ONLY> 自动传播给消费方（链接面完整），而用法要求
# （include 路径）不外泄——封装与链接两全。零 Qt（D-01/NFR-MNT-01）、
# 零 Widgets（WP-11-T02 禁止项）、零其他 industrialrobot 单元（R-1——
# io 不直链任何业务域单元，协作走六类端口/注入面，§10）、零 testkit
# （T-1——testkit 仅随 _test/_contract_test 目标消费）。用统一真名
# sdurws_ird_core/sdurws_ird_diagnostics：集成与独立冒烟两种模式下都可
# 解析（别名 RWS::ird::* 仅集成模式存在——core/diagnostics 落位时同款取舍）。
target_link_libraries(sdurws_ird_io PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics)
target_link_libraries(sdurws_ird_io PRIVATE
    libzip::zip
    expat::expat)

# 别名（集成模式）：供消费方单元以 RWS::ird::io 消费；冒烟模式无别名
# （与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::io ALIAS sdurws_ird_io)
endif()

# ---- 测试目标 sdurws_ird_io_test（随 IO-T02 登记——见文件头注第 2 条）----
# 首批用例＝任务契约 acceptance 1~5 的 §11.2 矩阵行：IoSec/PathTraversal
# （IO-V09）、IoSec/SameProjectAltPaths（IO-V10）、IoSec/SymlinkJunction
# （IO-V11）、IoCsv/StreamBudget（IO-V06——行预算经流式读取路径生效；
# CSV 读写器本体随 IO-T03 落位后由其通道复用同一 BudgetGuard 行维）、
# 预算三要素/双保险/账本可观测（acceptance 3）、IO-* 码表注册（P-IO-6，
# acceptance 5）＋源码头红线扫描（零 Qt/零跨单元头——BuildRedLine 同款
# 运行期扫描，与文件末尾配置期守卫互为两道防线）。IO-T04 增列
# test/JsonTest.cpp（IoJsonTest 组——IO-V07/V08＋canonical 字节一致＋
# 内容摘要＋版本门＋Preserve/预算接线，契约 acceptance 1~3）与
# test/ZipChannelTest.cpp（IoZipTest 组——逐字节还原＋manifest 哈希校验
# ＋加密/压缩方法拒绝，acceptance 4；恶意 zip 构造器＝§11.1 登记的测试
# 替身，经 libzip 写侧构造合法样例、手工字节构造恶意样例）。IO-T06/T07
# 增列资源/包/契约面用例与 sdurws_ird_io_contract_test（§3.3）。
enable_testing()

add_executable(sdurws_ird_io_test
    test/SafePathTest.cpp
    test/BudgetTest.cpp
    test/IoDiagnosticsTest.cpp
    test/CsvTest.cpp
    test/JsonTest.cpp)

target_compile_features(sdurws_ird_io_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_io_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_io_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5）：GTest::gtest_main 提供入口；测试链接产品库
# sdurws_ird_io 同时验证"公共头可被正常 include＋库可链接"（io→core/
# diagnostics 的 PUBLIC 传染链随目标自动到达）。
# 不链 sdurws_ird_testkit：红线用例以 gtest 原生断言承载（需求/AT 追溯
# 在用例名与 ird-test-report.json 的 trace 字段——diagnostics 先例同款）；
# testkit 契约谓词消费随 IO-T07 契约套件登记（届时 _contract_test 目标
# 链 testkit 为 T-1 允许形态，产品目标不链）。
target_link_libraries(sdurws_ird_io_test PRIVATE
    sdurws_ird_io
    GTest::gtest_main)
# libzip 供测试侧恶意 zip 构造器使用（§11.1 替身清单——合法样例经 libzip
# 写侧构造、恶意样例手工字节构造；T-1 同款"测试目标可链"形态——产品库
# 本体已 PRIVATE 链接，此处仅为测试翻译单元的 <zip.h> 头与链接符号）。
target_link_libraries(sdurws_ird_io_test PRIVATE
    libzip::zip)
# 峰值内存观测（V06）经 kernel32 的 K32GetProcessMemoryInfo 直调——零
# 词表外链接面（ird_gates IRD-GATE-LIB 零命中；曾试 Psapi.lib 被门禁拦
# 下，改走 kernel32 缺省链接面）。

# 源码树位置注入：头红线扫描用例在运行期扫描 io 源码核对红线（零 Qt/
# 零跨单元 include——§1.4/§3.2）。经编译定义传 industrialrobot 根
# （diagnostics/project 同款——值内以单元名作子目录前缀，扫描面随单元
# 自包含）。
target_compile_definitions(sdurws_ird_io_test PRIVATE
    IRD_IO_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_io_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_io_test>
            $<TARGET_FILE_DIR:sdurws_ird_io_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析）；不做跨单元共享（io 不是
# 构建工具设施提供方——NFR-MNT-04，diagnostics/project 同款口径）。报告
# 输出于构建树本目录 gtest_reports/<target>.xml；任务留痕时拷贝入
# doc/industrial-robot-design/traceability/（AGENTS.md §4.2 验证留痕）。
# 报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架
# "不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_io_test)

# ---- 依赖红线守卫（配置期断言——IO-T01 既有自律，IO-T02 保持）----
# io 的 industrialrobot 单元依赖仅限 core＋diagnostics（§3.2/§3.3：ARCH
# §3.5 仅有的两条登记出边）；零 Qt（R-3/NFR-MNT-01——比 L3 上限更严，
# D-01 精神：io 无界面职责）；产品库本体零 gtest（D-06——断言宏只在
# 消费方 _test 目标展开；本守卫遍历的是产品目标 sdurws_ird_io 的链接
# 面，测试目标的 gtest 边不在其内）。两面读取（LINK_LIBRARIES 含
# PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，
# 不会误报。正式全仓门禁（依赖图白名单）归 ird_gates（WP-01-T01），本
# 守卫不替代它。
get_target_property(_ird_io_link_libs sdurws_ird_io LINK_LIBRARIES)
get_target_property(_ird_io_iface_libs sdurws_ird_io INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_io_link_libs _ird_io_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_diagnostics")
        message(FATAL_ERROR
            "io 依赖红线（R-1/R-2）：sdurws_ird_io 仅可链接 core＋diagnostics"
            "（units/io.md §3.2/§3.3、ARCH §3.5），发现单元目标 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "io 依赖红线（R-3/NFR-MNT-01，零 Qt——io.md §1.4/§3.3）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$|^sdurws_ird_testkit$")
        message(FATAL_ERROR
            "io 依赖红线（D-06/T-1）：产品库本体不链接 gtest/testkit，发现 ${_lib}")
    endif()
endforeach()
