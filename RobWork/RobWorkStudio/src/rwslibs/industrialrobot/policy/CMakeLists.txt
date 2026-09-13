# =====================================================================
# policy 单元（策略单元）构建脚本
#
# 设计依据：
#   - units/policy.md §3.4（命名空间/目标/CMake 集成）、§3.2（依赖：core＋标准库
#     ＋RobWork 基线——含 sdurw_proximity 直链，R-5 唯一产品例外）、§12 POL-T01 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿）、
#     §4.5（R-5 例外登记册：policy 产品实现＝proximity 直链唯一许可，生效行）
#   - 需求 ARC-05（策略单一权威）、CON-06；红线 R-3（零 Qt）、CR-02/CR-04 陷阱
#
# 本文件随 POL-T01（≙WP-07-T01，任务契约 tasks/foundation/POL-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_policy 不变，
#      别名 RWS::ird::policy 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_policy_test（红线扫描：零 Qt/依赖图仅
#      core＋RobWork 边/R-5 例外钉住）；
#   3. 首个翻译单元为占位 Policy.cpp（真实接口随 POL-T02+ 落地）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC ----
# POL-T02 增量：错误与策略类型落地——新增 Errors.cpp（token/registryCode
# 稳定表＋PolicyError 构造，units/policy.md §3.1 POL-T02 产物行）；
# PolicySet.hpp 为 header-only 类型契约（无对应 .cpp——产物清单原文）。
# POL-T03 增量：编码与身份落地——新增 PolicyInput.cpp（PolicyCodec
# encode/decode/encodeSemanticProjection/contentIdentity＋canonicalF64
# 浮点原语，§3.1 POL-T03 产物行/§5.3 编码规则唯一实现点；CR-02 摘要
# 单点在其 contentIdentity）。
# POL-T04 增量：解析器与校验器落地——新增 PolicyParsing.cpp（resolvePolicy
# 七段管线/PolicyValidator——§5.1/§5.2/§9.2，POL-T04 产物行）。
# POL-T05 增量：④端口与注入接口落地——新增 PolicyPort.cpp（PolicyProvider
# 实现＋(对象, 版本) 记忆化——§9.1/§12 POL-T05 产物行；Contexts.hpp 为
# header-only 接口契约，无对应 .cpp）。
# POL-T06 增量：场景与会话构建期落地——src/CollisionEvaluator.cpp 为
# **集成模式专属**（见下方 target_sources 注释：消费 rw 非模板类
# WorkCell/ProximitySetup/ProximityStrategyRW——冒烟模式无框架库可链）；
# CollisionEvaluator.hpp 公共头零 rw include（rw 类型仅前向声明——冒烟模式
# 无 TU include 该头，公共头 include 面不影响冒烟口径）。
# POL-T10 增量：诊断构造落地——新增 Diagnostics.cpp（policyDiagCodes 建议
# 码表／IPolicyDiagnostics＋PolicyDiagnostics／makeComparative 辅助——§3.1
# POL-T10 产物行/§9.6 契约；纯 core＋std 依赖，双模式无条件编译；Errors.cpp
# 的 registryCode 同步改为委托本 TU 码表——NFR-MNT-03 单一字面量点）。
# 占位 Policy.cpp 保留（单元锚点符号——STATIC 库至少一个翻译单元纪律不变）。
add_library(sdurws_ird_policy STATIC
    src/Policy.cpp
    src/Errors.cpp
    src/Diagnostics.cpp
    src/PolicyInput.cpp
    src/PolicyParsing.cpp
    src/PolicyPort.cpp)

target_compile_features(sdurws_ird_policy PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_policy PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sdurws_ird_policy PUBLIC /utf-8)
endif()

# 公共头根：仅 policy/include（R-2 纪律同 core/testkit）。
target_include_directories(sdurws_ird_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ---- RobWork 基线库链接（集成模式；冒烟模式占位实现零 rw 引用各自成立）----
# 判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在——runtime RT-T01
# 同款探测；详见其 CMakeLists 顶层顺序说明）。
# ⚠ R-5（DTB §4.5 例外登记册生效行）：policy 产品实现是 proximity 直链的**唯一**
# 许可方（碰撞评估唯一实现——ARC-05）；其他单元直链 proximity 即构建失败。
set(IRL_BASELINE_LIBS "")
if(TARGET sdurw_kinematics)
    # ---- POL-T06 增列（场景与会话构建期；tasks/foundation/POL-T06.json）——
    # 仅集成模式：CollisionEvaluator.cpp 消费 rw 非模板类（WorkCell 场景校验
    # findDevice／ProximitySetup・ProximitySetupRule 规则生成／
    # ProximityStrategyRW 内置后端实例化），冒烟模式无框架库可链（runtime
    # RT-T07 同款分工——workcell/proximity 为已登记 L1 依赖，§3.2/R-5 例外）。
    # 公共头 CollisionEvaluator.hpp 对 rw 类型仅前向声明、零 rw include——
    # 冒烟模式下无任何 TU include 该头（新测试 TU 同因仅集成编译），"目标
    # 注册＋include 路径"冒烟口径不受影响（runtime §15.4 v0.10 同款登记）。
    target_sources(sdurws_ird_policy PRIVATE
        src/CollisionEvaluator.cpp        # §6.1 场景校验＋§6.4 会话三步（作用域
                                          # 展开/冲突同码复核/检测器初始化/
                                          # sessionIdentity）＋复现要素字面量
                                          # 单点（detail::makeBuiltinBackend-
                                          # Descriptor）——§12 POL-T06 产物行；
                                          # 构造函数末段调用评估半区装配
                                          # （POL-T07，实现在 CollisionQuery.cpp）
        src/CollisionQuery.cpp            # §6.3/§6.4/§6.6/§7.4 评估半区
                                          # （POL-T07：buildEvaluationHalf
                                          # 装配＋evaluate 执行序——setState/
                                          # 逐对查询/取消/异常捕获/稳定排序/
                                          # 边界值决策；消费 rw 非模板类
                                          # Device/Frame/Kinematics/
                                          # CollisionStrategy/DistanceStrategy）
        src/RobWorkCollisionEvaluator.cpp # §6.5/§9.3 唯一实现绑定（RobWork-
                                          # CollisionEvaluator 方法＋make 唯一
                                          # 构造入口＋内置 ProximityStrategyRW
                                          # 实例化＋评估器级查询互斥——含
                                          # RobWork 命名标识符的零字面量 TU，
                                          # 切分说明见该文件头）
        src/JointLimits.cpp         # §7.4/§9.4 关节限位与行程阈值评估
                                          # （POL-T08：JointLimitEvaluator::
                                          # evaluate 七步执行序——区间/工程范围
                                          # 诊断级检出〔D-13〕/行程 4π 边界/
                                          # 近限位比/限位违例/P-POL-2 显式
                                          # NotApplicable 标记/稳定排序；本体
                                          # 零框架调用，rw include 面仅经
                                          # JointLimits.hpp→CollisionQuery.hpp
                                          # 状态词表复用链传导〔§9.4"复用状态
                                          # 词表"〕——集成模式专属 gating 与
                                          # CollisionQuery.cpp 同因）
        src/Compatibility.cpp)            # §8.1/§8.2/§9.5 策略兼容判定
                                          # （POL-T09：七项检查固定序全量收集
                                          # ＋POLICY-VERSION-INCOMPATIBLE 诊断
                                          # 构造；backend-mismatch 臂经
                                          # CollisionEvaluator.hpp 的字面量
                                          # 单点 makeBuiltinBackendDescriptor
                                          # ＋框架配置头版本宏取"当前注册后端"
                                          # 复现要素——P-POL-5 暂取构建版本，
                                          # 不私定最终取值。框架配置头在冒烟
                                          # 模式不可解析——集成模式专属 gating
                                          # 与 RobWorkCollisionEvaluator.cpp
                                          # 同因；头内类型面零框架依赖）

    target_link_libraries(sdurws_ird_policy PUBLIC
        sdurw_math
        sdurw_kinematics
        sdurw_models
        sdurw_proximity)
    set(IRL_BASELINE_LIBS "sdurw_math, sdurw_kinematics, sdurw_models, sdurw_proximity")
endif()

target_link_libraries(sdurws_ird_policy PUBLIC sdurws_ird_core)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::policy ALIAS sdurws_ird_policy)
endif()

# ---- 测试目标 sdurws_ird_policy_test（随 POL-T01 登记）----
enable_testing()

add_executable(sdurws_ird_policy_test
    test/BuildRedLineTest.cpp
    test/PolicySetTest.cpp    # POL-T02：字段约束与不可变性用例（POL-PARSE-2 前置）
    test/PolicyInputTest.cpp  # POL-T03：canonical 往返/位模式/排序无关/CR-02/错误路径
    test/PolicyParsingTest.cpp # POL-T04：POL-PARSE-1~6 错误分类全量反例＋O-10 默认口径
    test/PolicyPortTest.cpp   # POL-T05：注入接口契约/端口契约错误矩阵/POL-ID-1 缓存一致
    test/DiagnosticsTest.cpp) # POL-T10：建议码表全表锚定＋构造不变量
                              # （testkit checkDiagnosticRecord 族——§11 测试
                              # 设施在 policy 测试面的首次真实链接；纯 core
                              # 依赖，双模式无条件编译）

# POL-T06 增列（仅集成模式）：CollisionSessionTest.cpp 消费 rw 非模板类
# （WorkCell/FakeDevice 场景替身、ProximitySetup 规则核对）——冒烟模式无
# 框架库可链；与 src/CollisionEvaluator.cpp 同因同 gating（runtime RT-T07
# 同款分工）。冒烟模式测试面＝POL-T01~T05 既有 112 用例（不受影响）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_policy_test PRIVATE
        test/CollisionSessionTest.cpp)  # POL-T06：POL-SCOPE-1/2 构建期（作用域
                                        # 矩阵展开/冲突拒绝两层防御）＋场景校验
                                        # 守卫＋会话身份确定性＋检测器初始化
    target_sources(sdurws_ird_policy_test PRIVATE
        test/CollisionEvaluationTest.cpp)  # POL-T07：POL-EVAL-1~9/POL-EXC-1/
                                           # POL-CONC-1（AT-19 载体）＋POL-LATE-1
                                           # ＋R-POL-5/D-9 结构断言——真实后端
                                           # 解析算例＋脚本化碰撞/距离替身
    target_sources(sdurws_ird_policy_test PRIVATE
        test/JointLimitsTest.cpp)          # POL-T08：POL-JNT-1/2（行程 4π 边界/
                                           # 比较型三要素/continuous 豁免/P-POL-2
                                           # 显式 NotApplicable/区间与工程范围
                                           # 诊断级检出〔D-13〕/近限位与闭区间
                                           # 边界/裕量口径/违约矩阵——集成模式
                                           # 专属，与 src/JointLimits.cpp 同因）
    target_sources(sdurws_ird_policy_test PRIVATE
        test/CompatibilityTest.cpp)        # POL-T09：POL-COMPAT-1/2（版本/后端/
                                           # 锚失配全量 reason/比较型三要素/
                                           # 内容身份素材面/全量收集不短路/
                                           # 确定性/注入接口委托/O-20·P-POL-5
                                           # 保守口径）——后端真值经唯一装配线
                                           # 取得（框架库），集成模式专属，
                                           # 与 src/Compatibility.cpp 同因
endif()
target_compile_features(sdurws_ird_policy_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_policy_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_policy_test PRIVATE /utf-8)
endif()

target_link_libraries(sdurws_ird_policy_test PRIVATE
    sdurws_ird_policy
    sdurws_ird_testkit   # POL-T10：checkDiagnosticRecord/checkComparativeFields
                         # （§11 测试设施——units/testkit.md §5.5 签名的真实
                         # 消费；仅测试目标链接，产品库红线面不变）
    GTest::gtest_main)

# 源码树位置与 L1 链接清单注入（红线扫描用例输入；清单＝R-5 例外钉住——运行期
# 断言"清单与登记一致"，集成四库、冒烟空串）。
# 注：值内用逗号分隔——MSVC 预处理器定义按分号拆分（RT-T01 实测留痕）。
target_compile_definitions(sdurws_ird_policy_test PRIVATE
    IRD_POLICY_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_POLICY_BASELINE_LIBS="${IRL_BASELINE_LIBS}")

add_custom_command(TARGET sdurws_ird_policy_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_policy_test>
            $<TARGET_FILE_DIR:sdurws_ird_policy_test>
    COMMAND_EXPAND_LISTS)

# ---- 跨单元契约测试目标 sdurws_ird_policy_contract_test（随 POL-T11 登记）----
# §11 测试目标分工：`_test`＝单元内行为；`_contract_test`＝跨单元契约面
# （§3.4：与 core 值类型、与 testkit 谓词、与 evidence 证明字段形状的
# 对齐断言）。POL-T11 交付本目标——替身与契约套件（tasks/foundation/
# POL-T11.json，≙WP-07-T11）。
#
# 文件分层（两模式 vs 集成专属——与产品库同款 gating 判定）：
#   - 双模式无条件编译：PolicyCodecFixtureContractTest.cpp（解析/编码契约
#     夹具数据集消费——纯 core＋policy 无框架依赖）＋
#     PolicyTestDoubleBoundaryTest.cpp（POL-TD-1 边界机检——纯 std 文件
#     扫描）；
#   - 仅集成模式：PolicyContractDoublesTest.cpp（规范替身行为＋POL-SHARE-1
#     ＋POL-AT-1~3——消费 rw 非模板类 WorkCell/Device/ProximityStrategyRW）
#     ＋PolicyProofMaterialContractTest.cpp（POL-EVAL-3 证明素材对齐——
#     CollisionQuery.hpp 的 rw include 面）＋CollisionAnalyticDatasetTest.cpp
#     （pol-collision-analytic 数据集消费＋POL-AT-4——真实 WorkCell＋
#     内置后端）；
#   - 公共头 include：policy/test（替身头 <sdurws/ird/policy/testdouble/…>
#     ——§11 测试设施行的落位；仅测试目标可见，产品 include 面不变）。
add_executable(sdurws_ird_policy_contract_test
    test/PolicyCodecFixtureContractTest.cpp   # POL-T11：pol-policy-codec-fixture
                                              # 消费（双实现互证——编码/身份/
                                              # 变体无关性；GoldenDataset 全链）
    test/PolicyTestDoubleBoundaryTest.cpp)    # POL-T11：POL-TD-1 边界机检
                                              # （产品源码替身符号零命中＋
                                              # 声明两面在案断言）

if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_policy_contract_test PRIVATE
        test/PolicyContractDoublesTest.cpp    # POL-T11：替身四轨行为＋装配
                                              # 分步镜像＋POL-SHARE-1（三入口
                                              # 同一实例/等价输出）＋POL-AT-1
                                              # （行程确认放行链）＋POL-AT-2
                                              # （AT-19 三入口一致）＋POL-AT-3
                                              # （AT-27 单位/配置不可覆盖）
        test/PolicyProofMaterialContractTest.cpp)  # POL-T11：POL-EVAL-3 证明
                                                   # 素材对齐（evidence 公共头
                                                   # 值类型消费——零链接边，
                                                   # T-1 合规）＋POL-COMPAT-2
                                                   # 素材面
    target_sources(sdurws_ird_policy_contract_test PRIVATE
        test/CollisionAnalyticDatasetTest.cpp)     # POL-T11：pol-collision-
                                                   # analytic 数据集消费
                                                   # （analytic-case 六工况
                                                   # 对照）＋POL-AT-4（倒挂
                                                   # 无二次旋转判别）

    # evidence 公共头 include 根（仅 ProofMaterial 对齐测试消费 evidence 值
    # 类型——R-2 允许形态的"其他单元公共头"；零链接边〔不 link
    # sdurws_ird_evidence，T-1 白名单合规〕；仅集成模式需要——消费文件
    # 均在本 gating 内）。
    target_include_directories(sdurws_ird_policy_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../evidence/include)
endif()

target_compile_features(sdurws_ird_policy_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_policy_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_policy_contract_test PRIVATE /utf-8)
endif()

# 链接面（T-1 允许形态：同单元产品目标＋testkit＋gtest——sdurws_ird_policy_
# contract_test→testkit 边与 F-072 同族〔白名单数据面明文允许而引擎 SUB
# 形态未豁免〕，命中如实登记，处置归 WP-01 所有者）。用统一真名：两模式
# 都可解析（别名仅集成模式存在——runtime/evidence 测试目标同款取舍）。
# evidence 证明对齐＝公共头值类型消费（R-2 允许形态）——零链接边。
target_link_libraries(sdurws_ird_policy_contract_test PRIVATE
    sdurws_ird_policy
    sdurws_ird_testkit   # §10.2 policy 行消费面（Dataset/JsonLite/ToleranceProfile/
                         # SetCheck/IRD_TEST_INFO——测试侧依赖，产品库红线面不变）
    GTest::gtest_main)

# 替身头 include 根（§11 落位 policy/test/sdurws/ird/policy/testdouble/）＋
# 源码树位置注入（边界机检/源码锚扫描的输入——与 _test 目标同源）。
target_include_directories(sdurws_ird_policy_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test)
target_compile_definitions(sdurws_ird_policy_contract_test PRIVATE
    IRD_POLICY_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_policy_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_policy_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_policy_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_policy_contract_test)

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

ird_add_gtest(sdurws_ird_policy_test)

# ---- 依赖红线守卫（配置期断言）----
# policy 唯一 industrialrobot 单元依赖＝core；RobWork 基线＝math/kinematics/models/
# proximity（R-5 例外清单）；零 Qt；产品库本体零 gtest（D-06）。
get_target_property(_ird_pol_link_libs sdurws_ird_policy LINK_LIBRARIES)
get_target_property(_ird_pol_iface_libs sdurws_ird_policy INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_pol_link_libs _ird_pol_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_policy")
        message(FATAL_ERROR
            "policy 依赖红线：sdurws_ird_policy 仅可链接 core＋RobWork 基线，"
            "发现单元目标 ${_lib}（units/policy.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "policy 依赖红线（R-3/NFR-MNT-01）：policy 禁止链接 Qt，发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "policy 依赖红线（D-06）：policy 库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
