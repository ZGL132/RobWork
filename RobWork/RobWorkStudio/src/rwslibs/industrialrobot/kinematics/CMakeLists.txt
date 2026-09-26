# =====================================================================
# kinematics 单元（业务域：运动学可达性事实——Stage 3）构建脚本
#
# 设计依据：
#   - units/kinematics.md §3.1（单元组成——计算库 STATIC 零 Qt＋两测试
#     目标；"_worker 目标：不新建——worker 进程复用本计算库"）、§3.2
#     （目标升级与门禁边登记——WP-15-T02 执行清单：六条接口依赖边/配置
#     期红线守卫自持/_plugin 随 WP-15-T12 落位）、§3.3（公共头布局——
#     T02 行：Errors.hpp/DiagCodes.hpp 两头）、§3.4（线程与确定性总约定
#     ——纯函数服务可重入、数值恒 SI、随机性唯一来源）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构）、§3.5（依赖表——各业务
#     域单元→L2/L3 公共接口的许可方向；六边为其实例化，登记于
#     cmake/ird_gates_whitelist.cmake 的 "kinematics->…" 六行）
#   - 需求 NFR-MNT-01（计算内核零 Qt——kinematics 为 L2 等效业务计算库）
#   - 任务契约 tasks/foundation/WP-15-T02.json（本文件即其 acceptance
#     1/2/4 的落地面：INTERFACE 占位→STATIC＋_test/_contract_test 随
#     文件注册＋T02 公共面两头）
#
# 本文件随 WP-15-T02 落地（kinematics 由上级 INTERFACE 占位升级为真实
# 库）：
#   1. sdurws_ird_kinematics STATIC（目标名与别名 RWS::ird::kinematics
#      不变，DTB §5.1）；T02 公共面＝Errors/DiagCodes 两头＋对应实现
#      翻译单元（§3.3 表 T02 行；KinTypes/Fk/Ik 等随 T03/T04 增列——
#      §3.3 布局表任务列权威）；
#   2. 测试目标 sdurws_ird_kinematics_test 与
#      sdurws_ird_kinematics_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/modeling/requirements
#      同款；ctest LABELS "ird"）；
#   3. 配置期依赖红线守卫（R-1/R-3 的 kinematics 侧断言——卡 §3.2；
#      全仓正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
#   _plugin 目标（Qt Widgets）不在本任务：卡 §3.1/§3.2/契约 acceptance 2
#   明文"随 WP-15-T12 落位"（不预建空目标——总体详设 §1.5 原则）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Eigen 接入（治理登记 O-40，DTB §4.2——WP-15-T03 携带）----
# Jacobian SVD/线性代数经 vcpkg 经典模式引入 eigen3（实测安装版本
# 5.0.1）；rw::math 无 SVD——该依赖为 kinematics 计算所必需。PRIVATE
# 仅本计算库实现文件消费（PUBLIC 面零 Eigen 类型——Fk.hpp 契约面纯
# rw::math/标准库），不向下游传播（业务域依赖面最小化）。
find_package(Eigen3 CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝两公共头的实现翻译单元（头内码值常量为 inline constexpr，
# 零运行时成本无需 TU；函数面按头分文件——modeling/io/requirements 同款
# 布局）：
#   src/Errors.cpp     域错误 token 转发表＋域错误→稳定码映射数据（§9.2/
#                      §9.6；T03 起映射两行——NoDevice/NoTcp 随消费任务）
#   src/DiagCodes.cpp  KIN-* 15 码描述符清单（§9.6 全表逐码落值）＋注册
#                      函数（§9.6 表头"装配期注册"的执行面）
# T03 增列（tasks/foundation/WP-15-T03.json，§3.3 布局表 T03 行）：
#   src/Fk.cpp         FK 与位姿指标实现（KIN-01——链式 FK/基础雅可比/
#                      Eigen SVD 指标链/canonical 载荷编码；全单元唯一
#                      Eigen 消费点——O-40 PRIVATE）
#   src/Evaluators.cpp kin.pose-metrics 评估器形态（IEngineeringEvaluator
#                      实现＋宿主注入工厂＋descriptor——§4.3/§9.3）
# T04 增列（tasks/foundation/WP-15-T04.json，§3.3 布局表 T04 行）：
#   src/KinTypes.cpp   值模型函数面（ConfigurationSignature 构型签名——
#                      §6.1/I-KIN-3；KinTypes.cpp 随 T04 值模型落位增列——
#                      T03 批注释预言的落位）
#   src/Bounds.cpp     解析工作半径界限（§5.4 结局 5 素材面——连杆长度和
#                      上界推导＋证明素材组装）
#   src/Ik.cpp         多初值 IK 求解（KIN-02——初值策略生成/阻尼最小二乘/
#                      解析界限检查/结局组装；Eigen 消费点之一——O-40 PRIVATE）
add_library(sdurws_ird_kinematics STATIC
    src/Errors.cpp
    src/DiagCodes.cpp
    src/Fk.cpp
    src/Evaluators.cpp
    src/KinTypes.cpp
    src/Bounds.cpp
    src/Ik.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：kinematics 公共头使用 C++17 设施（std::string_view/
# inline constexpr 变量等），消费者编译 kinematics 头也必须至少 C++17。
target_compile_features(sdurws_ird_kinematics PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_kinematics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 kinematics
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics PUBLIC /utf-8)
endif()

# 公共头根：仅 kinematics/include（内部含 sdurws/ird/kinematics/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 kinematics 唯一对外
# 的头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_kinematics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics＋runtime＋policy＋evidence＋execution（卡
# §3.2 边表六条接口依赖边——ARCH §3.5"各业务域单元→L2/L3 公共接口"许可
# 方向的实例化，ird_gates_whitelist.cmake 的 "kinematics->…" 六行同批
# 登记）。
# PUBLIC 形态：DiagCodes.hpp 内嵌 diagnostics 承载的 CodeDescriptor/
# IDiagnosticRegistry 契约类型（其头又内嵌 core::DiagCode——消费方必须
# 随链到达）；core/diagnostics 两边本任务真实消费（DiagCodes.hpp include
# 面），runtime/policy/evidence/execution 四边本任务"仅链接目标不消费
# 上游公共头"（P-RPT-9/modeling/requirements T02 同款先例——边随落位
# 登记，消费随 T03+ 任务回填：runtime IRuntimeModelView/RuntimeSnapshot
# →T03 FK 输入面、policy ④端口 ICollisionEvaluator/IPolicyProvider→
# T04/T07 硬过滤面、evidence IEngineeringEvaluator/EvaluationRequest→
# T03~T07 评估器族、execution 任务提交/取消令牌/检查点→T05 批量通道；
# 六边统一 PUBLIC 保持形态一致——契约 acceptance 3 六边全登记）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——core/io/requirements 落位时同款取舍）。
# 零 Qt（R-3/NFR-MNT-01——_plugin 目标随 WP-15-T12 落位时方为 Qt 例外
# 载体）、零其他业务域单元（R-1——kinematics 不直链 modeling/
# requirements/trajectory/dynamics/drivetrain/selection/optimization，
# 跨单元协作走六类端口）、零 testkit（T-1——testkit 仅随测试目标消费）、
# 零 sdurw_proximity（R-5——碰撞唯一实现归 policy，卡 §1.4）。
target_link_libraries(sdurws_ird_kinematics PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics
    sdurws_ird_runtime
    sdurws_ird_policy
    sdurws_ird_evidence
    sdurws_ird_execution)

# Eigen（O-40）：PRIVATE 仅本计算库消费——公共头零 Eigen 类型（Fk.hpp/
# KinTypes.hpp/Evaluators.hpp 契约面纯 rw::math/标准库），消费面收敛在
# src/Fk.cpp 的 SVD 计算点；不向下游传播（静态库 PRIVATE 不进接口面）。
target_link_libraries(sdurws_ird_kinematics PRIVATE Eigen3::Eigen)

# 别名（集成模式）：供其余单元以 RWS::ird::kinematics 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::kinematics ALIAS sdurws_ird_kinematics)
endif()

# ---- 测试目标 sdurws_ird_kinematics_test（随 WP-15-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/ErrorsTest.cpp           域错误 token 全表往返＋KinematicsError
#                                 值语义＋映射分批纪律（全表 nullopt——
#                                 acceptance 4，§9.2/§5.2 行）
#   test/DiagCodesTest.cpp        §9.6 全表 15 码清单范围＋逐码级别列＋
#                                 真实 StableCodeRegistry 注册行为＋重复
#                                 注册拒绝（acceptance 4——DiagCodes.hpp
#                                 行；P-KIN-7 处置：以真实注册表验证）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋六边
#                                 include 面＋R-4 字面量＋文件头注释——
#                                 acceptance 1/5，与文件末尾配置期守卫
#                                 互为两道防线）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/modeling/requirements 同款先例；IRD_TEST_INFO 携带需求/AT
# 追溯字段）。
enable_testing()

add_executable(sdurws_ird_kinematics_test
    test/TestMainReport.cpp
    test/ErrorsTest.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/FkTest.cpp
    test/IkTest.cpp)

target_compile_features(sdurws_ird_kinematics_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_kinematics_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_kinematics 同时验证"公共头可被正常 include
# ＋库可链接"（六边 PUBLIC 传染链随目标自动到达）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO——
# T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句＝契约
# 测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——modeling/
# reporting/requirements 同款）。
target_link_libraries(sdurws_ird_kinematics_test PRIVATE sdurws_ird_kinematics sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 kinematics 源码核对红线
# （零 Qt/零表外 include——§3.2/§14.5）。经编译定义传 industrialrobot 根
# （modeling/io/requirements 同款——值内以单元名作子目录前缀，扫描面随
# 单元自包含）。
target_compile_definitions(sdurws_ird_kinematics_test PRIVATE
    IRD_KINEMATICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_kinematics_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_kinematics_test>
            $<TARGET_FILE_DIR:sdurws_ird_kinematics_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/modeling/requirements
# 同款形态，不做跨单元共享——kinematics 是业务域单元，不提供构建工具
# 设施，NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/<target>.xml；
# 任务留痕时拷贝入 doc/industrial-robot-design/traceability/（AGENTS.md
# §4.2 验证留痕）。报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是
# 显式动作，与框架"不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_kinematics_test)

# ---- 契约测试目标 sdurws_ird_kinematics_contract_test（随 WP-15-T02
#      登记——卡 §3.1/§3.3）----
# 落位期契约面＝构建图边界契约（modeling/requirements BuildGraphContract-
# Test 同款扫描形态——六条登记边的存在性/封闭性/无业务域互链/testkit
# 仅测试目标/plugin 不落位/include 面同界）；业务对端面契约（runtime/
# policy/evidence/execution 四对端的 fake 套件）随 T03~T07 落地（卡 §11
# 内部顺序——T02 三头不触对端契约面，不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_kinematics_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/PoseMetricsEvaluatorContractTest.cpp)

target_compile_features(sdurws_ird_kinematics_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_kinematics_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
target_link_libraries(sdurws_ird_kinematics_contract_test PRIVATE sdurws_ird_kinematics sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_kinematics_contract_test PRIVATE
    IRD_KINEMATICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库——同 _test 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_kinematics_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_kinematics_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_kinematics_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_kinematics_contract_test)

# ---- ctest LABELS "ird"（modeling/ui/requirements 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（插件目标随 WP-15-T12 另登记）。
set_tests_properties(sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，core/io/modeling/requirements
#      foreach 同款）----
# kinematics 的 industrialrobot 单元依赖仅限六条登记边：core＋diagnostics
# ＋runtime＋policy＋evidence＋execution（卡 §3.2 边表——ARCH §3.5 业务
# 域→L2/L3 公共接口许可方向的实例化，白名单 "kinematics->…" 六行同源）；
# 链接到任何其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；表外平台
# 单元＝SUB 命中面）即配置失败。零 Qt（R-3/NFR-MNT-01）。产品库本体零
# gtest/testkit（D-06/T-1——断言宏只在消费方测试目标展开；本守卫遍历的
# 是产品目标 sdurws_ird_kinematics 的链接面，测试目标的 gtest/testkit 边
# 不在其内）。两面读取（LINK_LIBRARIES 含 PRIVATE 边＋
# INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不
# 会误报。testkit 不在第一条模式中逐字列出（modeling 同款先例）：
# sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的拦截范围（≠六条登记边即
# FATAL），避免守卫正则字样干扰契约测试的 T-1 链接语句扫描
# （BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认测试目标链接
# 语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_kinematics_link_libs sdurws_ird_kinematics LINK_LIBRARIES)
get_target_property(_ird_kinematics_iface_libs sdurws_ird_kinematics INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_kinematics_link_libs _ird_kinematics_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_runtime"
       AND NOT _lib STREQUAL "sdurws_ird_policy"
       AND NOT _lib STREQUAL "sdurws_ird_evidence"
       AND NOT _lib STREQUAL "sdurws_ird_execution")
        message(FATAL_ERROR
            "kinematics 依赖红线（R-1/卡 §3.2）：sdurws_ird_kinematics "
            "仅可链接六条登记边 core/diagnostics/runtime/policy/evidence/"
            "execution（units/kinematics.md §3.2、ARCH §3.5；含 testkit——"
            "T-1 产品目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "kinematics 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.1）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "kinematics 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
