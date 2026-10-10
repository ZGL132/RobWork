# =====================================================================
# drivetrain 单元（L2 共享计算服务：传动映射——dynamics 与 selection 之间
# 的唯一共享传动计算服务）构建脚本
#
# 设计依据：
#   - units/drivetrain.md §2.3（单元定位——L2 共享计算服务，非 L4 业务
#     插件；零 Qt；不要求 plugin/worker 目标）、§3.2（依赖形态与红线——
#     登记依赖边仅两条：drivetrain→core、drivetrain→evidence；表外同层边
#     含 drivetrain→policy/runtime/diagnostics＝构建失败）、§4.2（目标
#     布局——sdurws_ird_drivetrain STATIC＋_test/_contract_test；plugin/
#     worker 默认禁止创建）、§1.3/§18.2 P-DT-7（DT-* 前缀注册权威归
#     diagnostics StableCodeRegistry——本单元登记表为注册数据源）、§5.3
#     （传动比全文唯一口径 c＝Δq_joint/Δθ_motor——D-DT-4/P-DT-10）
#   - development-task-breakdown.md §2.19（WP-18-T02 行：构建落位——
#     STATIC 链 core＋evidence；双模式构建零错误；红线扫描零命中；零 Qt；
#     依赖仅 core+evidence——ARCH §3.5）、§5.1（双模式/目标命名/C++17）、
#     §5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.5（依赖表——drivetrain→core/evidence 两边为
#     §3.5 原始登记边，ird_gates_whitelist.cmake 的 "drivetrain->…" 两行
#     〔2026-09-10 建册即含，dependency-graph.json 第 8/9 行同源镜像——
#     本任务零白名单增补，两边非"新边"〕）
#   - 需求 NFR-MNT-01（计算内核零 Qt、可由模型测试直接调用）
#   - 任务契约 tasks/foundation/WP-18-T02.json（本文件即其 acceptance
#     1/2 的落地面：INTERFACE 占位→STATIC＋_test/_contract_test 随文件
#     注册＋DT-* 域诊断码登记面＋红线守卫；acceptance 3 留痕面）
#
# 本文件随 WP-18-T02 落地（drivetrain 由上级 INTERFACE 占位升级为真实库）：
#   1. sdurws_ird_drivetrain STATIC（目标名与别名 RWS::ird::drivetrain
#      不变，DTB §5.1）；T02 源码集＝src/DiagCodes.cpp——DT-* 码值常量
#      的登记清单实现（19 码：units/drivetrain.md §6.3 阻断面 8 码＋
#      §7.2 矩阵形态 4 码＋§9 惯量 2 码＋§10 效率/统计 4 码＋§12.1 序列
#      契约 1 码；映射实现 src 面随 WP-18-T03 增列——卡 §4.2 布局表）；
#   2. 测试目标 sdurws_ird_drivetrain_test 与
#      sdurws_ird_drivetrain_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/kinematics/workflow 同款；
#      ctest LABELS "ird"）；
#   3. 配置期依赖红线守卫（R-1/R-3 的 drivetrain 侧断言——卡 §3.2；
#      全仓正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
#
# 与 kinematics（业务域落位唯一参照）的差异说明（诚实登记，非遗漏）：
#   - 无 _plugin 目标：卡 §2.3/§4.2 明文"drivetrain 无独立界面（工作点
#     呈现由 dynamics/selection/reporting 的界面承载）"——plugin/worker
#     默认禁止创建（如需走 P-DT-8 架构变更通道），契约 acceptance 1
#     "无插件目标"同口径；
#   - 无 diagnostics 依赖边：kinematics 六边含 diagnostics（其 DiagCodes
#     消费 CodeDescriptor/IDiagnosticRegistry 并以真实 StableCodeRegistry
#     注册验证）；drivetrain 依赖白名单仅 core＋evidence（卡 §3.2 自查表
#     点名"drivetrain→policy/runtime/diagnostics＝表外边"），故本单元
#     DiagCodes 登记表以 core 句法承载（core::DiagCode＝码值 token 权威
#     承载，core.md/DiagData.hpp），登记行的物化注册（装配期经
#     diagnostics::StableCodeRegistry）归 L5 装配与 diagnostics 收编
#     （P-DT-7——diagnostics.md §4.5 前缀表补登 DT 前缀属 diagnostics
#     所有者治理动作，不在本任务 allowedFiles）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝src/DiagCodes.cpp（DT-* 登记清单——码值常量的唯一实现翻译
# 单元；逐码落值依据见该文件逐码注释）。
# WP-18-T03 增列 src/MappingCore.cpp（映射核心唯一实现——卡 §4.2 布局表：
# 阻断面检查序/R1 对角精确虚功映射/一致性检查/反射惯量/效率折算/工作点
# 统计；评估器适配面 src/Evaluator.cpp 随同任务工作点统计批次增列）。
add_library(sdurws_ird_drivetrain STATIC
    src/DiagCodes.cpp
    src/MappingCore.cpp
    src/DriveTrainCodec.cpp
    src/Evaluator.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：drivetrain 公共头使用 C++17 设施（inline constexpr 变量
# 等），消费者编译 drivetrain 头也必须至少 C++17。
target_compile_features(sdurws_ird_drivetrain PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_drivetrain PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 drivetrain
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_drivetrain PUBLIC /utf-8)
endif()

# 公共头根：仅 drivetrain/include（内部含 sdurws/ird/drivetrain/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 drivetrain 唯一对外
# 的头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_drivetrain PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋evidence（卡 §3.2 边表两条接口依赖边——ARCH §3.5 原始
# 登记边，ird_gates_whitelist.cmake 的 "drivetrain->core"/
# "drivetrain->evidence" 两行〔建册即含〕与 dependency-graph.json 第 8/9
# 行同源；本任务零白名单增补）。
# PUBLIC 形态：两边本任务"仅链接目标不消费上游公共头"（P-RPT-9/kinematics
# T02 六边同款先例——边随落位登记，消费随 T03+ 任务回填：core ObjectId/
# DiagCode 句法/单位换算→T03 映射输入面、evidence IEngineeringEvaluator/
# EvaluationRequest→T03 评估器适配面；两边统一 PUBLIC 保持形态一致——
# 契约 acceptance 1"依赖仅 core＋evidence"两登记边全建立）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——core/io/kinematics 落位时同款取舍）。
# 零 Qt（R-3/NFR-MNT-01——本单元无 plugin/worker 目标，无 Qt 例外载体）、
# 零其他业务域单元（R-1——drivetrain 虽非 L4 业务域插件，自身仍不直链
# modeling/requirements/kinematics/trajectory/dynamics/selection/
# optimization 任何计算库，dynamics 关节侧序列经 UpstreamResult 切片条目
# 值传递消费、selection 消费经③评估器端口，卡 §3.3/§12）、零表外平台
# 单元（卡 §3.2 点名 drivetrain→policy/runtime/diagnostics＝表外边——
# diagnostics 注册面经 L5 装配收编，不在本单元落编译边）、零 testkit
# （T-1——testkit 仅随测试目标消费）、零 sdurw_proximity（R-5——碰撞
# 唯一实现归 policy，与本单元无交集）。
target_link_libraries(sdurws_ird_drivetrain PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence)

# 别名（集成模式）：供其余单元以 RWS::ird::drivetrain 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::drivetrain ALIAS sdurws_ird_drivetrain)
endif()

# ---- 测试目标 sdurws_ird_drivetrain_test（随 WP-18-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/DiagCodesTest.cpp        DT-* 19 码登记表全表清单＋码值句法
#                                 （经 core::DiagnosticRecord 句法权威
#                                 校验）＋登记出处完整性（acceptance 2
#                                 ——DiagCodes.hpp 行；P-DT-7 处置：以
#                                 core 句法面验证，真实 StableCodeRegistry
#                                 注册收编归 diagnostics 侧治理动作）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋两登记
#                                 边 include 面＋R-4 字面量＋文件头注释
#                                 ——acceptance 1/2，与文件末尾配置期守卫
#                                 互为两道防线）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/workflow 同款先例；IRD_TEST_INFO 携带需求/AT 追溯字段）。
# 零 QCoreApplication 构造（workflow T02 同款诚实登记）：drivetrain 零 Qt
# 消费（无插件目标），测试面无 Qt 类型，无运行期标记需求。
enable_testing()

add_executable(sdurws_ird_drivetrain_test
    test/TestMainReport.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/MappingGoldenTest.cpp
    test/MappingR2GoldenTest.cpp
    test/GoldenDtTest.cpp)

target_compile_features(sdurws_ird_drivetrain_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_drivetrain_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_drivetrain_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_drivetrain 同时验证"公共头可被正常 include
# ＋库可链接"（两登记边 PUBLIC 传染链随目标自动到达——core/evidence 的
# 链接语义经本链路在链接器层面自证）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO
# ——T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——
# kinematics/reporting/workflow 同款）。
target_link_libraries(sdurws_ird_drivetrain_test PRIVATE sdurws_ird_drivetrain sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 drivetrain 源码核对红线
# （零 Qt/零表外 include——卡 §3.2/§13.7）。经编译定义传 industrialrobot
# 根（kinematics/io/workflow 同款——值内以单元名作子目录前缀，扫描面随
# 单元自包含）。
target_compile_definitions(sdurws_ird_drivetrain_test PRIVATE
    IRD_DRIVETRAIN_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_drivetrain_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_drivetrain_test>
            $<TARGET_FILE_DIR:sdurws_ird_drivetrain_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/kinematics/workflow
# 同款形态，不做跨单元共享——NFR-MNT-04 不建无边界价值包装器）。报告
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

ird_add_gtest(sdurws_ird_drivetrain_test)

# ---- 契约测试目标 sdurws_ird_drivetrain_contract_test（随 WP-18-T02
#      登记——卡 §4.2 目标表）----
# 落位期契约面＝构建图边界契约（kinematics/workflow BuildGraphContract-
# Test 同款扫描形态——两条登记边的存在性/封闭性/无业务域互链/无表外
# 平台边/testkit 仅测试目标/无 plugin/worker 形态/include 面同界）；
# 业务对端面契约（evidence 评估器注册/切片依赖声明 fake 套件——卡 §4.2
# contract_test 行）随 WP-18-T03 落地（卡 §15 内部顺序——T02 登记面不触
# 对端契约面，不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
# WP-18-T03 增列 contract_test/EvaluatorContractTest.cpp（业务对端面契约
# ——evidence 评估器注册/切片依赖声明套件；卡 §4.2 contract_test 行）。
# WP-18-T04 增列 contract_test/GoldenDatasetContractTest.cpp（黄金数据集
# 登记契约——DatasetManifest 装载/完整性覆盖/交叉一致/档案通道与 P-DT-3
# 保守纪律；testkit 数据资产真实装载，不桩化）。
add_executable(sdurws_ird_drivetrain_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/EvaluatorContractTest.cpp
    contract_test/GoldenDatasetContractTest.cpp)

target_compile_features(sdurws_ird_drivetrain_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_drivetrain_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_drivetrain_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
target_link_libraries(sdurws_ird_drivetrain_contract_test PRIVATE sdurws_ird_drivetrain sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_drivetrain_contract_test PRIVATE
    IRD_DRIVETRAIN_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库——同 _test 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_drivetrain_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_drivetrain_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_drivetrain_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_drivetrain_contract_test)

# ---- ctest LABELS "ird"（kinematics/ui/workflow 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（卡 §2.3——无 plugin 目标即无 GUI 载体）。
set_tests_properties(sdurws_ird_drivetrain_test sdurws_ird_drivetrain_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，kinematics/workflow foreach
#      同款）----
# drivetrain 的 industrialrobot 单元依赖仅限两条登记边：core＋evidence
# （卡 §3.2 边表——ARCH §3.5 原始登记边，白名单 "drivetrain->…" 两行
# 同源）；链接到任何其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；
# 表外平台单元＝SUB 命中面——含卡 §3.2 点名的 diagnostics/runtime/
# policy）即配置失败。零 Qt（R-3/NFR-MNT-01）。产品库本体零 gtest/
# testkit（D-06/T-1——断言宏只在消费方测试目标展开；本守卫遍历的是
# 产品目标 sdurws_ird_drivetrain 的链接面，测试目标的 gtest/testkit 边
# 不在其内）。两面读取（LINK_LIBRARIES 含 PRIVATE 边＋
# INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不
# 会误报。testkit 不在第一条模式中逐字列出（kinematics 同款先例）：
# sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的拦截范围（≠两条登记边即
# FATAL），避免守卫正则字样干扰契约测试的 T-1 链接语句扫描
# （BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认测试目标链接
# 语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_drivetrain_link_libs sdurws_ird_drivetrain LINK_LIBRARIES)
get_target_property(_ird_drivetrain_iface_libs sdurws_ird_drivetrain INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_drivetrain_link_libs _ird_drivetrain_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_evidence")
        message(FATAL_ERROR
            "drivetrain 依赖红线（R-1/卡 §3.2）：sdurws_ird_drivetrain "
            "仅可链接两条登记边 core/evidence（units/drivetrain.md §3.2、"
            "ARCH §3.5——diagnostics/runtime/policy 为卡面点名表外边，"
            "含 testkit——T-1 产品目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "drivetrain 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §2.3）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "drivetrain 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
