# =====================================================================
# execution 单元（任务执行平台：任务状态机/调度/工作进程池/检查点/缓存
# 治理/RunRegistry）构建脚本
#
# 设计依据：
#   - units/execution.md §3.4（命名空间/错误类型与 CMake 集成：骨架
#     INTERFACE → EX-T01 升级 STATIC；别名 RWS::ird::execution；
#     cxx_std_17；PUBLIC 链 core/evidence/project；diagnostics 边已登记
#     但当前经注入实现、目标暂不链接——链接形态按 P-EX-8 裁决；`_test`/
#     `_contract_test` 随 EX-T01 登记；gtest 按 DTB §5.5 接入）、§3.2
#     （依赖图：三条登记单元边＋标准库＋Win32 kernel32〔隔离于 src/win32/，
#     随 EX-T06 落地〕——零 Qt 含 Core〔D-01〕、零 runtime/policy/业务
#     单元编译边〔R-1/R-2 门禁口径——P-EX-3 处置：经 §3.3 最小接口注入
#     消费，不私建 ARCH §3.5 表外边〕、零 testkit〔T-1〕）、§12 EX-T01
#     行（产物：STATIC＋注册 `_test`/`_contract_test`；涉及文件 src/
#     （空起步）；验证方式：双模式构建＋EX-BLD-1 前两项；完成条件：
#     两模式零错误＋红线扫描零命中）、§11 EX-BLD-1（构建红线：零 Qt、
#     无私有头出 include/、依赖图仅登记边、产品目标零 testkit）
#   - development-task-breakdown.md §5.1（双模式/目标命名 INTERFACE→真实库
#     升级时目标名不变/C++17 不用 C++20）、§5.3（全局禁止项）、§5.5
#     （gtest 定稿：vcpkg＋find_package(GTest CONFIG REQUIRED)，失败即停，
#     不消费 RW::gtest、不源码 vendor、测试 main 自持）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界——无私有头
#     出 include）；红线 R-1（业务单元互链禁止）/R-2（跨单元私有头禁止）/
#     T-1（产品目标不链 testkit）
#
# 本文件随 EX-T01（≙WP-08-T02，任务契约 tasks/foundation/EX-T01.json）
# 落地，先例：project/CMakeLists.txt（PRJ-T01 同款落位形态）；EX-T02
# （≙WP-08-T03，任务契约 tasks/foundation/EX-T02.json）增列产品源
# Errors/TaskTypes/StateMachine 三翻译单元与两组业务用例（依赖红线与
# 链接边零变更——本任务消费 core/evidence 公共头属于已登记边的合法
# include 面，P-EX-1 处置：冻结 diff 后按影响面增量同步）：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_execution
#      不变，别名 RWS::ird::execution 不变，公共 include 根不变——DTB
#      §5.1"INTERFACE→真实库升级时目标名不变"）；
#   2. 注册 gtest 测试目标 sdurws_ird_execution_test（单元内红线扫描）与
#      sdurws_ird_execution_contract_test（跨单元构建图边界契约）；落位期
#      首批用例＝最小构建冒烟用例（契约 acceptance 3 明文允许），业务用例
#      （状态机/登记表/调度/真进程）随 EX-T02~T09 落地，不在骨架预建占位
#      实现——父 CMakeLists 既有约定；
#   3. src/ 空起步＝锚点翻译单元 Execution.cpp（§12 EX-T01 行产物定义的
#      既定先例形态——CORE-T01/POL-T01/DIAG-T02/PRJ-T01 同款：CMake STATIC
#      库至少一个翻译单元纪律，无任何接口预建——NFR-MNT-04）；
#   4. 陷阱处置：P-EX-1——消费的 core/evidence/project 卡 v0.1 仍 Draft
#      未冻结，本任务**仅建立三条登记链接边、零跨单元头消费**（产品源码
#      与测试源码均不 include 任何 sdurws/ird/<他单元> 头；测试用例对
#      include 面的扫描即该处置的常驻自证），各卡冻结出 diff 后按影响面
#      增量同步留痕；O-24——TaskState/TaskOutcome/EvaluationMode/
#      EngineeringStatus 四词表按 core 词表现状承接（sdurws/ird/core/
#      Evaluation.hpp，九态词表归 core——本卡 §2.2 不可越界列），execution
#      侧零重定义（落位期连公共头都未产出，词表承接约束随 EX-T02
#      TaskTypes.hpp 落地时兑现），O-24 在 DTB §4 仍为登记未决态，本任务
#      不私裁消账（消账权在 DTB §4 登记流程/所有者）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/
# RW::gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# src/Execution.cpp＝EX-T01 锚点翻译单元（§12 EX-T01 行"src/（空起步）"）。
# EX-T02 按本卡 §3.5/§12 增列同名任务文件：Errors（§3.4 错误面——EX-SM-6
# 观测点 ExecutionError.code）、TaskTypes（§4 三级身份＋§5.5 能力声明）、
# StateMachine（§5 逐转移表＋守卫＋事件发布接缝）；EX-T03（≙WP-08-T04，
# 任务契约 tasks/foundation/EX-T03.json）增列 Controller（§7.1 取消命令
# 通道＋协作窗 2 s/10 s＋强杀编排＋运行超时——§3.1 Controller.hpp 行）
# 与 Scheduler（§7.5 DrainPolicy 排空编排——DrainPolicy 枚举按 §3.1 登记
# 于 Scheduler.hpp 行）；EX-T04（≙WP-08-T05，任务契约
# tasks/foundation/EX-T04.json）增列 Ports 公共头（§3.3 注入接口——接纳
# 路径消费的三件：诊断 sink/名称反解/产出解码；IExecutionModelService/
# ICompileCacheJudge 随其消费任务 EX-T05/T08 落位——归置登记单元卡
# §15.4）、RunRegistry 公共头＋实现（§9.1 登记记录/§10.5 接口/一次性
# 判定）与 Admission 实现（§9.2 步 8~9＋§9.3 重投递/冲突/重试＋§9.5 归
# 档事务）；EX-T05（≙WP-08-T06，任务契约 tasks/foundation/EX-T05.json）
# 增列 EventBus 公共头＋实现（§3.1 EventBus.hpp 行/§10.4——core
# IDomainEventBus 的进程内参考实现：专用投递线程/同发布者 FIFO/RAII 退订）
# 并扩展 Scheduler 公共头＋实现（§3.1 Scheduler.hpp 行/§10.1——ITaskScheduler
# 契约面＋TaskScheduler 调度本体：提交验证 V1~V4/双优先级队列/预算闸/
# 内联门槛/进度节流；StateMachine 增 updateProgress 受控进度写面——
# §4.2 progress 行）；锚点文件保留（文件头登记后续任务落位安排，届时或
# 承载共享工具或拆除）。EX-T06（≙WP-08-T07，任务契约
# tasks/foundation/EX-T06.json）增列：ChannelProtocol 公共头＋实现（§3.1
# 组成行/§6.5——IRDCHN/1 帧编解码/seq 序控/分帧续传/载荷编解码，主/worker
# 共用）、WorkerSupervisor 公共头＋实现（§3.1 组成行/§10.3——worker 池
# 监督：launch/握手/心跳失联/退出码分类/池纪律）、win32 三件私有实现
# （§3.5：ChannelPair 命名管道对/JobScope 作业 RAII/ProcessLauncher 进程
# 启动——kernel32 隔离于 src/win32/）、worker 宿主入口 worker/main.cpp
# （目标 sdurws_ird_execution_worker，§3.4——阶段 A 内置脚本驱动测试替身
# 评估器）。
# EX-T07（≙WP-08-T08，任务契约 tasks/foundation/EX-T07.json）增列：
# win32/MemProbe 私有实现两件（§3.5：主进程＋worker 内存汇总——
# GlobalMemoryStatusEx＋GetProcessMemoryInfo 工作集＋QueryInformationJobObject
# 作业内存；kernel32/psapi 调用隔离于 src/win32/——§3.2 依赖图）；汇总＋
# 节流决策＋资源不足诊断落位为 Scheduler 公共头扩展（ResourceController/
# IMemorySampler/MemoryReading/SupervisorMemorySampler——归置登记单元卡
# §15.4，Scheduler.hpp 组成行先例同 EX-T03 DrainCoordinator）＋WorkerSupervisor
# 增量两法（aggregateJobMemoryBytes/reclaimIdleWorkers——§10.3 具体类
# 治理/观测面，登记单元卡 §15.4）。
# EX-T08（≙WP-08-T09，任务契约 tasks/foundation/EX-T08.json）增列：
# Checkpoint 公共头＋实现（§8.1/§10.6——CheckpointRecord 契约字段面/
# 复合摘要单点/归档端口三段写出/恢复期摘要校验＋evidence 兼容判定转译）
# 与 CacheCoordinator 公共头＋实现（§8.2/§10.7——结果/模型/诊断三分账
# 存储＋LRU 预算淘汰＋D-10 同键在途合并；ICompileCacheJudge 注入消费
# ——§3.3 原文形态落位，evidence judgeCacheHit 直接调用；判定逻辑零复制
# ——兼容性结论全部来自两个判定单点）。
# EX-T09（≙WP-08-T10，任务契约 tasks/foundation/EX-T09.json）落地：
# 契约测试套件（§11 矩阵补齐 EX-ARC/EX-RCV/EX-RES 缺口用例＋testkit 设施
# 接入）——两测试目标增列源与链接面变更见下方各目标块注释；产品库与
# worker 目标零变更（T-1：产品目标零 testkit 边——LinkageContractTest
# 收窄重述钉住）。
add_library(sdurws_ird_execution STATIC
    src/Execution.cpp
    src/Errors.cpp
    src/TaskTypes.cpp
    src/StateMachine.cpp
    src/Controller.cpp
    src/Scheduler.cpp
    src/RunRegistry.cpp
    src/Admission.cpp
    src/EventBus.cpp
    src/ChannelProtocol.cpp
    src/WorkerSupervisor.cpp
    src/Checkpoint.cpp
    src/CacheCoordinator.cpp
    src/win32/ChannelPair.cpp
    src/win32/JobScope.cpp
    src/win32/MemProbe.cpp
    src/win32/ProcessLauncher.cpp)

# worker 宿主可执行（§3.4：WIN32 可执行；阶段 A 评估器＝内置脚本替身
# 〔见 worker/main.cpp 文件头〕，正式评估器链接属 L5 装配决策——登记为
# 装配目标，业务互链红线 R-1 不因此失效；零 Qt、零 Widgets、零 _plugin
# ——§6.4 边界规则"无窗口、无控制台输出到 UI"）。
# 链接形态说明：协议与宿主的单点实现＝**同一批源文件**（ChannelProtocol/
# Errors/TaskTypes 三个翻译单元同时编入产品库与 worker 可执行——同单元
# 双二进制的合法编译形态，无 ODR 问题）。不采用 worker→sdurws_ird_execution
# 直链：ird_gates 以"单元级边"为粒度，同单元产品目标互链不在 §3.5 白名单
# 词表内（白名单登记归 cmake/ird_gates_whitelist.cmake——不在本任务
# allowedFiles）；worker 只链 core/evidence 两条 §3.5 白名单边
# （execution->core / execution->evidence），实现文件仍单点（杜绝主/worker
# 各写一份协议的漂移——§3.1 ChannelProtocol 组成行"主/worker 共用"）。
add_executable(sdurws_ird_execution_worker
    worker/main.cpp
    src/ChannelProtocol.cpp
    src/Errors.cpp
    src/TaskTypes.cpp)
target_compile_features(sdurws_ird_execution_worker PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_execution_worker PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    WIN32_EXECUTABLE ON)   # WIN32 子系统——无控制台（§6.4 诊断一律经通道）
if(MSVC)
    target_compile_options(sdurws_ird_execution_worker PRIVATE /utf-8)
endif()
# worker 消费本单元公共头（ChannelProtocol/Errors/TaskTypes——include 根
# 与产品库同源）与 core/evidence 两条白名单边（公共头传染链的链接器半区）。
target_include_directories(sdurws_ird_execution_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(sdurws_ird_execution_worker PRIVATE
    sdurws_ird_core
    sdurws_ird_evidence)

# C++17（单元卡 §1.4/core.md D-01 同源；P-ENV-1 同口径复证）：逐目标显式
# 设定，不设全局标准。target_compile_features PUBLIC 传播——execution
# 公共头将使用 std::optional/variant/string_view/filesystem 等 C++17 设施
# （§1.4），消费者编译含 execution 头的翻译单元也必须至少 C++17。不用
# C++20（DTB §5.1）。
target_compile_features(sdurws_ird_execution PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_execution PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 execution 头的
# 翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_execution PUBLIC /utf-8)
endif()

# 公共头根：仅 execution/include（内部含 sdurws/ird/execution/）。R-2 红线：
# 私有实现头不得置于 include/——本目录是 execution 唯一对外的头暴露位，
# 实现细节头（如 src/win32/ProcessLauncher.hpp，EX-T06 落地）只能放 src/
# 并以相对路径被实现文件包含（§3.5 布局的构建面表达）。
target_include_directories(sdurws_ird_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译链接依赖：core/evidence/project 三条 ARCH §3.5 登记边（§3.4 原文
# "target_link_libraries(sdurws_ird_execution PUBLIC RWS::ird::core
# RWS::ird::evidence RWS::ird::project)"——PUBLIC 形态；alias 形态在集成
# 模式即其 ALIAS，此处用统一真名 sdurws_ird_*：集成与独立冒烟两种模式下
# 都可解析——project 落位同款取舍）。diagnostics 边：ARCH §3.5 已登记，
# 但当前经 IExecutionDiagnosticsSink 注入实现（本卡 §3.3）、目标暂不链接
# ——链接形态按 P-EX-8 裁决，不因 diagnostics.md 已产出而自动消账
# （ird_gates 白名单预登记 execution->diagnostics 不构成链接义务）。
# 零 Qt（D-01 零 Qt 含 Core——比 L3 层规则上限更严，worker 目标同口径）、
# 零 runtime/policy 编译边（P-EX-3 处置：runtime 能力经 §3.3 三注入接口
# IExecutionModelService/ICompileCacheJudge/INameResolverAdapter＋ICancelSignal
# 消费、policy 经快照 PolicyRef 值传递〔CON-06〕——ARCH §3.5 未登记
# execution→runtime/policy 边，表外边＝构建失败 SA-10；如架构侧补登边可
# 平滑直连，注入接口形状不变——D-17）、零业务域单元（R-1）、零 testkit
# （T-1——产品目标不链 testkit）。
target_link_libraries(sdurws_ird_execution PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence
    sdurws_ird_project)

# 别名（集成模式）：供消费方单元（ui/workflow 等——ARCH §3.5 入边单元）
# 以 RWS::ird::execution 消费；冒烟模式无别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::execution ALIAS sdurws_ird_execution)
endif()

# ---- 测试目标 sdurws_ird_execution_test（随 EX-T01 登记）----
# EX-T01 首批用例＝最小构建冒烟用例（落位期红线扫描）；EX-T02 增列
# TaskStateMachineTest.cpp（§11 EX-SUB-1/EX-SM-1~7＋强制终止矩阵＋恢复
# 期指断＋词表 P-EX-4/P-EX-6 行为钉子——纯逻辑替身，零进程/磁盘断言，
# 后两者归 EX-T03/T04/T06/T09 对应用例）；EX-T03 增列
# CancelProtocolTest.cpp（§11 EX-SM-1/3/4、EX-WKR-4/5 的协议编排时序用例
# ——ManualClock 可注入时钟虚拟推进断言、不 sleep〔testkit §6.5/§6.6〕；
# EX-SM-4 的检查点保留为 TempDir 磁盘断言；worker/派发闸门为替身接缝，
# 真实进程场景归 EX-T06/T09）；EX-T04 增列 RunRegistryAdmissionTest.cpp
# （§11 EX-REG-1/2/3/6/7/8＋EX-ORT-1 单元内面——登记判定矩阵/接纳编排/
# 归档事务；归档写面用真实 ProjectStore〔PRJ-T14——本任务验证与 project
# 的协作语义〕＋故障注入转发器，退避经 DelayFn 虚拟推进不 sleep）；EX-T05
# 增列 SchedulerEventBusTest.cpp（§11 EX-SUB-1/2 调度面＋PM-07/预算/
# 优先级/进度节流 D-07/UX-10 透传边界/NFR-PERF-01 非阻塞结构断言/§7.5
# 关闭排空——校验面与执行体为替身接缝，存储写权限面用真实 ProjectStore）；
# EX-T06 增列 ChannelProtocolTest.cpp（§11 EX-CHN-1 的协议逻辑面＋§6.5
# 帧/seq/分帧/载荷编解码＋§6.4 退出码分类表＋HeartbeatPolicy D-06 默认值
# ——纯逻辑零进程/管道，真进程面归 contract_test）；EX-T07 增列
# ResourceGovernanceTest.cpp（§11 EX-RES-2：内存采样 fake 推至 >70%——
# 先节流〔停派发/降并行〕→仍超限→EX-RESOURCE-INSUFFICIENT 诊断、已排队/
# 运行任务不失败〔观测点＝派发暂停＋诊断〕＋三段阈值边界＋采样失败保持
# ＋MemProbe 真实探针冒烟——私有头经 src/ 私有包含根，R-2 纪律下测试
# 与实现同单元边界内）。
# EX-T09（≙WP-08-T10，任务契约 tasks/foundation/EX-T09.json）增列：
# ContractSuiteFacilitiesTest.cpp（§11 设施列自证——faultPointId 登记/
# P-EX-8 空占位 sink/收编 18 项码值镜像/ManualClock/脚本替身边界的具名
# 断言）与 TestMainReport.cpp（自有 main——安装 testkit RecordListener，
# IRD_TEST_INFO 追溯聚合为 ird-test-report.json 与 gtest XML 并存，
# testkit §7.2/§7.3；链接面由 GTest::gtest_main 改为 GTest::gtest＋
# sdurws_ird_testkit——T-1 允许形态：仅测试目标链 testkit，产品目标
# 零 testkit 边不变）。
enable_testing()

add_executable(sdurws_ird_execution_test
    test/TestMainReport.cpp
    test/BuildRedLineTest.cpp
    test/TaskStateMachineTest.cpp
    test/CancelProtocolTest.cpp
    test/RunRegistryAdmissionTest.cpp
    test/SchedulerEventBusTest.cpp
    test/ChannelProtocolTest.cpp
    test/ResourceGovernanceTest.cpp
    test/CheckpointCacheTest.cpp
    test/ContractSuiteFacilitiesTest.cpp)

target_compile_features(sdurws_ird_execution_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_execution_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。PRIVATE 显式再设一次以自洽——测试目标应独立可读，
# 不依赖被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_execution_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5/P-TK-1 定稿）：EX-T09 起以自有 main 提供
# 入口（test/TestMainReport.cpp——安装 testkit RecordListener，IRD_TEST_INFO
# 追溯聚合为 ird-test-report.json 与 gtest XML 并存）；链接面相应为
# GTest::gtest（无入口）＋sdurws_ird_testkit（T-1 允许形态——testkit 仅
# 测试目标可链：RecordListener/TempDir/Fault 等消费；产品目标零 testkit
# 边不变——LinkageContractTest::NoTestkitEdge 用例钉住）。测试链接产品库
# sdurws_ird_execution 同时验证"公共头可被正常 include＋库可链接"
# （execution→core/evidence/project 的 PUBLIC 传染链随目标自动到达——
# 三条登记边的链接器半区实证）。
target_link_libraries(sdurws_ird_execution_test PRIVATE sdurws_ird_execution sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：EX-BLD-1 用例在运行期扫描 execution 源码核对红线。经
# 编译定义传 industrialrobot 根（project/diagnostics/evidence/runtime 同款
# ——值内以单元名作子目录前缀，扫描面随单元自包含）。另开 src/ 私有包含
# 根（EX-T07：ResourceGovernanceTest 直测 win32/MemProbe 真实探针——私有
# 头仅本单元测试目标可见，不进 include/ 公共根，R-2 纪律不受影响）。
target_include_directories(sdurws_ird_execution_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(sdurws_ird_execution_test PRIVATE
    IRD_EXECUTION_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_execution_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_execution_test>
            $<TARGET_FILE_DIR:sdurws_ird_execution_test>
    COMMAND_EXPAND_LISTS)

# ---- 跨单元契约测试目标 sdurws_ird_execution_contract_test（随 EX-T01 登记）----
# §3.4 测试目标分工：`_contract_test`＝跨单元契约面。EX-T01 首批＝构建图
# 边界契约；EX-T02 增列 IdentityVocabularyContractTest.cpp（acceptance 2：
# 五元组值类型/九态词表归 core、execution 零重定义——§2.2 不可越界列的
# 定义形态扫描＋core 类型链接半区实证；TaskId 为 execution 自有数据模型
# 的正反两面核对——§4.1）；EX-T04 增列
# ArchiveCollaborationContractTest.cpp（acceptance 1/3：与 project 真实
# 存储的归档协作——EX-REG-4 切换场景/D-13 完整性可见性/D-14 冲突全链路；
# 与 evidence 校验器的单点复用——接纳产物全字段等于 evidence API 直接
# 组装产物）；EX-T05 增列 EventBusDeliveryContractTest.cpp（acceptance 2：
# core IDomainEventBus 接口的 execution 参考实现契约——同发布者 FIFO/
# 九态事件流＋ResultArchived 家族按序投递/并发发布零丢失/RAII 退订幂等/
# 退订不与投递死锁/waitForIdle 确定性同步——§10.4/§6.2）。
# EX-T09 增列 ContractTestMain.cpp（自有 main——先于 gtest 拦截子进程
# 模式再安装 RecordListener，PRJ-T15 同款双形态协议）、
# ArchiveFaultContractTest.cpp（EX-ARC-1/2/4——归档端口 fake 经 testkit
# FaultInterceptor 按 execution/archive/* 故障点注入、缓存跳过登记联动、
# EX-ARC-4 关闭排空两形态）、RecoveryProcessContractTest.cpp＋
# RecoveryChild.cpp（EX-RCV-1 真进程——TestProcessRunner kill 主进程/
# 重启恢复重建 Interrupted，子进程半边＝同一 exe 双形态停靠协议）；
# 另增 EX-ARC-3/EX-RES-1 两用例于 WorkerProcessContractTest.cpp（真进程
# 注入）。契约目标消费 testkit 设施（Fault/Fixture/ProcessRunner/
# AssertMacros/RecordListener——T-1 允许形态；test/ 亦入私有包含根供
# 契约套件设施头 ContractSuiteFacilities.hpp 共享——同单元测试目标
# 内部共享，非跨单元暴露）。
add_executable(sdurws_ird_execution_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/LinkageContractTest.cpp
    contract_test/IdentityVocabularyContractTest.cpp
    contract_test/ArchiveCollaborationContractTest.cpp
    contract_test/EventBusDeliveryContractTest.cpp
    contract_test/WorkerProcessContractTest.cpp
    contract_test/CacheJudgeContractTest.cpp
    contract_test/ArchiveFaultContractTest.cpp
    contract_test/RecoveryProcessContractTest.cpp
    contract_test/RecoveryChild.cpp)

# 契约测试须在 worker exe 构建之后（真进程场景消费其产物）。
add_dependencies(sdurws_ird_execution_contract_test sdurws_ird_execution_worker)

target_compile_features(sdurws_ird_execution_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_execution_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_execution_contract_test PRIVATE /utf-8)
endif()

# 链接面与 sdurws_ird_execution_test 同款（EX-T09 起自有 main——
# ContractTestMain.cpp 子进程拦截＋RecordListener；GTest::gtest 无入口＋
# sdurws_ird_testkit——T-1 允许形态仅测试目标，产品目标零 testkit 边）。
target_link_libraries(sdurws_ird_execution_contract_test PRIVATE sdurws_ird_execution sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图边扫描用例输入——与 _test 目标同源）。另注入
# worker exe 路径（EX-T06 契约测试的真进程场景输入——测试进程以该路径
# 启动 sdurws_ird_execution_worker，EX-WKR-1~3 真进程实证面）。
# EX-T09：test/ 入私有包含根——契约套件共享设施头 ContractSuiteFacilities.hpp
# （同单元测试目标内部共享；R-2 不受影响——不入 include/ 公共根）。
target_include_directories(sdurws_ird_execution_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test)
target_compile_definitions(sdurws_ird_execution_contract_test PRIVATE
    IRD_EXECUTION_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_EXECUTION_WORKER_EXE="$<TARGET_FILE:sdurws_ird_execution_worker>")

# EX-T06 增列 WorkerProcessContractTest.cpp（acceptance 1：EX-WKR-1~3 真进程
# ——崩溃只失败当前任务/心跳失联强杀/正常完成走九步接纳；acceptance 3：
# 分批流式回传＋临时目录隔离；acceptance 4：R-3 句柄纪律实证——真进程
# 场景按 §3.4 `_contract_test` 分工落位）。

add_custom_command(TARGET sdurws_ird_execution_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_execution_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_execution_contract_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析）；不做跨单元共享（execution
# 是平台服务单元，不是构建工具设施提供方——NFR-MNT-04）。报告输出于
# 构建树本目录 gtest_reports/<target>.xml；任务留痕时拷贝入 doc/
# industrial-robot-design/traceability/（AGENTS.md §4.2 验证留痕）。报告
# 目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架"不随
# ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_execution_test)
ird_add_gtest(sdurws_ird_execution_contract_test)

# ---- 依赖红线守卫（配置期断言——EX-BLD-1 的 CMake 依赖图半区）----
# execution 的 industrialrobot 单元依赖仅限 core/evidence/project 三条
# ARCH §3.5 登记边（本卡 §3.2/§3.4；ird_gates 白名单 "execution->core"/
# "execution->evidence"/"execution->project" 行；diagnostics 边按注入形态
# 不落链接——P-EX-8，白名单预登记不构成链接义务）；零 Qt（NFR-MNT-01/
# D-01，比 L3 上限更严——含 Core 亦禁）；产品库本体零 gtest（断言宏只在
# 消费方 _test 目标展开——project D-06 同款）。^sdurws_ird_ 通配同时拦截
# testkit（sdurws_ird_testkit ≠ core/evidence/project 即 FATAL——T-1 产品
# 目标不链 testkit）与 runtime/policy/业务域单元（P-EX-3/R-1——表外边）。
# 两面读取（LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含
# PUBLIC 边）防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND"
# 字面串，不匹配任何模式，不会误报。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_exe_link_libs sdurws_ird_execution LINK_LIBRARIES)
get_target_property(_ird_exe_iface_libs sdurws_ird_execution INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_exe_link_libs _ird_exe_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_evidence"
                                       AND NOT _lib STREQUAL "sdurws_ird_project")
        message(FATAL_ERROR
            "execution 依赖红线（R-1/T-1/P-EX-3）：sdurws_ird_execution 仅可链接 "
            "core/evidence/project 三条登记边（units/execution.md §3.2/§3.4、"
            "ARCH §3.5；diagnostics 经注入不落链接——P-EX-8），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "execution 依赖红线（NFR-MNT-01/D-01 零 Qt 含 Core）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "execution 依赖红线（产品库本体不链接 gtest）：发现 ${_lib}")
    endif()
endforeach()
