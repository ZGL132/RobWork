/**
 * @file   Execution.cpp
 * @brief  execution 单元构建落位的首个翻译单元（src/ 空起步——单元卡 §12
 *         EX-T01 行"涉及文件"列）。
 *
 * 设计依据：
 *   - units/execution.md §12 EX-T01 行（产物：`sdurws_ird_execution` 升级
 *     STATIC〔C++17、链 core/evidence/project〕；涉及文件
 *     `industrialrobot/CMakeLists.txt`、`execution/CMakeLists.txt`（新）、
 *     `src/`（空起步））与 §3.4/§3.5（CMake 集成与源码目录布局）；
 *   - 任务契约 tasks/foundation/EX-T01.json acceptance 1（双模式构建零
 *     错误：INTERFACE 占位升级 STATIC）、acceptance 3（落位期允许最小
 *     构建冒烟用例，业务用例随 EX-T02~T09 落地——不在骨架预建占位实现）；
 *   - 先例：core/src/Core.cpp（CORE-T01）、policy/src/Policy.cpp
 *     （POL-T01）、diagnostics/src/Diagnostics.cpp（DIAG-T02）、
 *     project/src/Project.cpp（PRJ-T01）——落位任务以"锚点翻译单元"起步，
 *     真实实现随后续同名任务文件填充。
 *
 * 背景说明（为什么是注释文件而不是"零源码"）：CMake 对 STATIC 库强制
 * "至少一个翻译单元"——零源码的 add_library(STATIC) 在生成期即报
 * "No SOURCES given to target"（实测），库无法参与构建。因此"src/ 空起步"
 * 的既定形态＝仅含本注释块、不含任何声明或定义的锚点翻译单元：让
 * sdurws_ird_execution 以真实库形态参与双模式构建（集成／独立冒烟），验证
 * 目标注册、C++17 编译与三条登记链接边（execution→core/evidence/project，
 * ARCH §3.5），同时**不预建任何接口**（NFR-MNT-04：无消费者的能力不预建
 * ——契约 acceptance 3"不在骨架预建占位实现"的本义）。
 *
 * 后续任务落位安排（units/execution.md §12 与 §3.5 布局，实现文件随同名
 * 任务进入 src/，本文件届时或承载跨实现文件共享的内部工具，或拆分为同名
 * 任务文件后删除）：
 *   - EX-T02：TaskTypes/StateMachine（任务身份＋九态状态机转移表＋守卫
 *     ——§4/§5）；
 *   - EX-T03：Controller/取消命令通道＋协作窗＋强杀编排（§7——2 s/10 s
 *     协议，ManualClock 可注入时钟）；
 *   - EX-T04：RunRegistry/Admission（登记表＋九步接纳＋迟到结果拒绝
 *     ——§9）；
 *   - EX-T05：Scheduler/EventBus（调度线程＋事件/进度分发——§6.1~§6.3/
 *     §10.4）；
 *   - EX-T06：win32/ProcessLauncher/ChannelPair/JobScope＋worker/main＋
 *     ChannelProtocol（工作进程池与通道协议——§6.4~§6.6，Windows API
 *     隔离于 src/win32/）；
 *   - EX-T07：win32/MemProbe＋ResourceController 基础节流（§6.1/§6.6
 *     ——70% 先节流后诊断）；
 *   - EX-T08：Checkpoint/CacheCoordinator（检查点契约＋缓存存储治理
 *     ——§8）；
 *   - EX-T09：test/ 与 contract_test/ 契约测试套件整备（§11 全表）。
 * 以单元任务卡当前正文为准。
 *
 * 陷阱处置约束（任务契约 knownPitfalls）：
 *   - P-EX-1：消费的 core/evidence/project 卡 v0.1 仍 Draft 未冻结——本
 *     任务仅建立三条登记链接边、零跨单元头消费（本文件有意不 include 任何
 *     sdurws/ird/<他单元> 头；首个消费任务按各卡冻结签名进入 include 面，
 *     冻结 diff 后按影响面增量同步留痕）；
 *   - P-EX-3：runtime/policy 能力经本卡 §3.3 最小注入接口消费（适配器归
 *     L5），ARCH §3.5 未登记 execution→runtime/policy 边——本文件零
 *     runtime/policy 编译依赖，不私建表外边（SA-10 表外边＝构建失败）；
 *   - P-EX-8：EX-* 稳定码与 IExecutionDiagnosticsSink 形态的链接形态待
 *     裁决——本任务 diagnostics 边不落目标链接（经注入，§3.3），不因
 *     diagnostics.md 已产出而自动消账；
 *   - O-24：TaskState/TaskOutcome/EvaluationMode/EngineeringStatus 四词表
 *     按 core 词表现状承接（sdurws/ird/core/Evaluation.hpp——九态词表归
 *     core，本卡 §2.2 不可越界列），execution 侧零重定义；O-24 在 DTB §4
 *     仍为登记未决态，本任务不私裁消账（消账权在 DTB §4 登记流程/所有者）。
 *
 * 线程约束：本文件当前无可执行代码，无线程安全性议题；后续各实现的线程
 * 模型遵循 units/execution.md §6.2（调度/接纳在 execution 自有线程、UI
 * 线程零计算）与各 §10 接口的共性约束表（§10.8）逐一标注。
 */

// 有意留空：见文件头"背景说明"——锚点翻译单元只承担"库可生成、C++17 可编译"
// 的构建面职责，语义能力全部随 EX-T02+ 的同名任务文件进入本目录。
