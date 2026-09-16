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
# 报"No SOURCES given to target"，实测）。PRJ-T02 落位：src/win32/
# AtomicFile.cpp 增列（§12 PRJ-T02 行产物——存储原语：publishNew/原子
# 替换/write-through 写＋IFileOps 接缝；IFileOps.hpp/AtomicFile.hpp 为
# src/win32/ 私有实现头，不列 include、不经 PUBLIC include 根暴露——R-2）。
# PRJ-T03 落位：src/win32/{StoreLock,PathCanonical}.cpp 增列（§12 PRJ-T03
# 行产物——写锁〔独占句柄/心跳/PID 读取/失权防护〕与路径规范化；
# StoreLock.cpp 内含 ILockOps 的真实实现 Win32LockOps，ILockOps.hpp/
# StoreLock.hpp/PathCanonical.hpp 同为 src/win32/ 私有实现头——R-2）。
# PRJ-T04 落位：src/Codec.cpp 增列（§12 PRJ-T04 行产物——格式契约与编码：
# §4.8 canonical 编解码 parse(dump(x))==x、§8.11 版本判定拒绝、CR-02 唯一
# 摘要入口 contentVersionOf；Codec.hpp 为 src/ 私有实现头——R-2；公共类型
# 面＝include/sdurws/ird/project/PersistenceFormat.hpp＋StoreTypes.hpp 增量
# 的 StoreError/StoreErrorCode）。
# PRJ-T05 落位：src/ObjectStore.cpp 增列（§12 PRJ-T05 行产物——对象库：
# §4.6 内容编址 objects/<oid>/<cv>、只增发布/共享去重、读取侧 size＋SHA-256
# 校验、LRU 缓存、引用存在性扫描；ObjectStore.hpp 为 src/ 私有实现头——R-2；
# 摘要一律经 Codec 的 contentVersionOf＝core ContentDigester，CR-02 唯一
# 哈希入口）。
# PRJ-T06 落位：src/RevisionIndex.cpp 增列（§12 PRJ-T06 行产物——修订/分支/
# 元数据：§4.5 提交闭包双通道〔D-4：修订 DAG parent 链∪元数据谱系
# supersedes→committedBy〕、§4.4.3 分支表、INV-M1/M2/M3 发布边界防回退
# 校验〔branch-metadata-regression〕、§6.1 S6 rev 单调序号分配〔O-15〕、
# D-5 权威元数据底本拷贝更新；RevisionIndex.hpp 为 src/ 私有实现头——R-2；
# 纯进程内语义模型，零磁盘 I/O——文件读写归 TxEngine/T07）。
# PRJ-T07 落位：src/TxEngine.cpp 增列（§12 PRJ-T07 行产物——事务引擎：
# §7.1 七步提交协议〔.staging 暂存/验证/只增发布/HEAD 原子切换＝唯一提交点/
# 事件 FIFO D-18/清理〕、§7.3 闭包外识别、§7.4①④⑤ 恢复扫描〔PM-08〕；
# TxEngine.hpp 为 src/ 私有实现头——R-2；文件/目录操作一律经 win32 原语
# ——IFileOps 接缝随本任务增补目录两方法〔createDirectories/removeTree〕，
# F1/F7 注入点登记 faultpoint::kCreateDirectories/kRemoveTree；对象发布
# 的共享判定复用 ObjectStore 读回校验通道，摘要一律经 contentVersionOf
# ＝core ContentDigester〔CR-02/P-PR-1 消费基线〕）。
# PRJ-T08 落位：src/ProjectStoreImpl.cpp 增列（§12 PRJ-T08 行产物——打开
# 与恢复：ProjectStoreFactory open/createNew〔PM-02②③⑤ 打开协议/PM-01
# 存储侧组装〕、ProjectStoreImpl 生命周期〔§9.6 门卫＋§9.7 Draining 排空〕
# ＋RecoveryReport 汇编〔§7.4 恢复顺序〕；ProjectStoreImpl.hpp 为 src/
# 私有实现头——R-2；公共契约面＝include/sdurws/ird/project/ProjectStore.hpp
# ＋StoreTypes.hpp 增量的 §5.1 打开协议类型；端口访问器随 PRJ-T09/T10/
# T12/T13/T14 增量挂载——不预建无实现接口，NFR-MNT-04）。
# PRJ-T09 落位：src/QueryPortImpl.cpp 增列（§12 PRJ-T09 行产物——查询
# 端口：IProjectQueryPort 六类只读视图〔修订/元数据/分支/对象/草稿/运行〕、
# 闭包约束〔tryRevision 仅会话索引可达＋head/revision 的一致性窗口〕、
# ObjectCache 消费〔预算经打开请求传导至 ObjectStore——§5.2〕、读写隔离
# 〔值拷贝视图 PA-3＋writer 互斥窗口防"已注册未切 HEAD"中间态〕；§4.7
# 全方法线程安全/context-closed 拒绝/只读实例可查询；QueryPortImpl.hpp
# 为 src/ 私有实现头——R-2；公共契约面＝include/sdurws/ird/project/
# QueryPort.hpp〔§3.1 组成表行：IProjectQueryPort/RevisionView/
# ProjectMetadataView/BranchTip/RunInfo/DraftInfo〕＋ProjectStore.hpp
# 增挂 query() 访问器＋PersistenceFormat.hpp 增量 RunManifest 读取类型
# 〔首消费者＝listRuns；dump 随 T14〕＋Codec 增量 parseRunManifest）。
# PRJ-T10 落位：src/CommandServiceImpl.cpp 增列（§12 PRJ-T10 行产物——
# 命令服务与内置命令：S1～S7 编排〔串行槽/形式校验/基线解析/确认决策
# 映射/双编译编排/CommitPlan 装配〕＋HandlerRegistry＋QueryPort 的
# hasUnresolvedPayload 判据注入＋ProjectStore::commands() 访问器挂载；
# CommandServiceImpl.hpp 为 src/ 私有实现头——R-2；公共契约面＝include/
# sdurws/ird/project/CommandService.hpp〔§3.1 组成表行〕＋ProjectStore.
#hpp 增挂 commands()；**P-PR-9 阻断登记**：内置元数据命令族
# （project.create-branch 等，§6.5）的注册 token 被任务卡明示待所有者
# 裁决（§15.3），本任务不落位该族——机制面与测试处理器先行）。
# PRJ-T10 同步增列：src/DiagRecords.hpp（PRJ-* 诊断记录共享工厂——
# ProjectStoreImpl 原文件局部两工厂收敛至此＋stale-revision 新增；
# 私有实现头不出 include/——R-2）。
# PRJ-T11 增列：src/ConfirmationFlow.cpp（§12 PRJ-T11 行产物——确认放行
# 流绑定设施：四元组〔findingDigest/policyContentId/commandDigest/
# baseRevisionId〕的计算/冻结/复核＋生产探针＋IConfirmationProbe D-10
# 接缝；ConfirmationFlow.hpp 为 src/ 私有实现头——R-2。实现口径①～⑤
# 登记于该头/.cpp 文件头：摘要编码表、阶段 A 策略身份来源、复核时机、
# 失配归类、探针注入面。同文件时间辅助增量：win32::formatIsoMilli——
# 确认凭据 time_point→ISO-8601 文本〔StoreLock 重构，行为零变化〕）。
# PRJ-T12 增列：src/DraftServiceImpl.cpp（§12 PRJ-T12 行产物——草稿服务：
# §5.4 落盘协议〔.new→flush→.bak 轮换→原子就位→失败清理〕＋§8.4 恢复
# ／损坏诊断〔draft-corrupt→PRJ-RECOVERY-ORPHAN-DRAFT〕／.bak 优先／
# .new 残留丢弃＋§8.5 多模块汇总投影〔磁盘事实投影，present 位口径〕＋
# §8.6 放弃〔幂等目标态〕与只读/生命周期边界；DraftServiceImpl.hpp 为
# src/ 私有实现头——R-2；公共契约面＝include/sdurws/ird/project/
# DraftService.hpp〔§3.1 组成表行：DraftService/DraftProjection/
# SaveResult/DiscardResult——DraftDocument 复用 PersistenceFormat 冻结
# 类型，不重定义〕＋ProjectStore.hpp 增挂 drafts() 访问器＋DiagRecords
# .hpp 增量 makeDraftCorrupt 装配点〔PRJ-RECOVERY-ORPHAN-DRAFT 的单文件
# 形态，diagnostics.md §8.2 映射行〕）。
# PRJ-T13 增列：src/UndoRedoServiceImpl.cpp（§12 PRJ-T13 行产物——撤销/
# 重做服务：§5.5 接口〔undo＝逆命令提交产生新修订，envelope 取自当前
# tip 修订的 inverse 记录；redo＝重放被撤销命令原始载荷〕＋§6.9 语义
# 〔会话栈按分支、不持久化——D-11；新命令清空 redo／过期基线 stale
# 说明；空历史稳定提示〕＋ProjectStore.hpp 增挂 undoRedo() 访问器；
# UndoRedoServiceImpl.hpp 为 src/ 私有实现头——R-2；公共契约面＝
# include/sdurws/ird/project/UndoRedo.hpp〔§3.1 组成表行：
# UndoRedoService/UndoRedoStatus/InverseRecord〕。零自有写通道——
# undo/redo 一律经命令端口 submit，§6.5 注册协议：逆命令由业务处理器
# 产出，project 只提交）。
# PRJ-T14 增列：src/ArchiveServiceImpl.cpp（§12 PRJ-T14 行产物——归档
# 端口：§5.6 begin/writeBatch/finalize/abandon〔begin 校验清单/分批写
# 只增＋批次幂等/manifest 原子发布＝D-13 完整判据＋D-14 摘要幂等与
# PRJ-ARCHIVE-CONFLICT 冲突拒绝/全路径责任终结〕＋§10.1 归档协作要素
# 〔绑定原修订不因 HEAD 拒绝、会话持宿主在途票据＝引用持有通道、
# 失权探测＝防护通道〕＋§10.2 流程〔finalize→ResultArchived 事件→
# 票据释放→排空归零 Closed〕＋Codec 增量 dump(RunManifest)〔§4.4.7
# 写侧编码——manifestDigest 由端口计算回填，CR-02 唯一哈希路径〕；
# ArchiveServiceImpl.hpp 为 src/ 私有实现头——R-2；公共契约面＝
# include/sdurws/ird/project/ArchivePort.hpp〔§3.1 组成表行：
# ArchiveRequest/ArchiveBatch/RunManifest/ArchiveStatus、
# IResultArchivePort、ArchiveSessionRef——RunManifest 复用
# PersistenceFormat 冻结类型，不重定义〕＋ProjectStore.hpp 增挂
# archive() 访问器〔五端口访问器全部挂载完成〕＋DiagRecords.hpp 增量
# makeArchiveConflict 装配点〔PRJ-ARCHIVE-CONFLICT〕。调用线程单侧
# 冻结——任意线程进入、writer 互斥内部串行〔P-PR-4 处置〕；事件经
# core IDomainEventBus v0.1 基线〔P-PR-1 处置〕）。
add_library(sdurws_ird_project STATIC
    src/Project.cpp
    src/Codec.cpp
    src/ObjectStore.cpp
    src/RevisionIndex.cpp
    src/TxEngine.cpp
    src/ProjectStoreImpl.cpp
    src/QueryPortImpl.cpp
    src/CommandServiceImpl.cpp
    src/ConfirmationFlow.cpp
    src/DraftServiceImpl.cpp
    src/UndoRedoServiceImpl.cpp
    src/ArchiveServiceImpl.cpp
    src/win32/AtomicFile.cpp
    src/win32/StoreLock.cpp
    src/win32/PathCanonical.cpp)

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
# P-PR-1 处置登记更新（PRJ-T03，2026-09-15）：本单元首个 core 头消费点
# 已随 PRJ-T03 落地——公共头 StoreTypes.hpp 的 IDiagnosticsSink::report()
# 以 core::DiagnosticRecord 为参数（§5.0 原文签名；任务契约 PRJ-T03.json
# acceptance 4 明文要求"锁诊断经 IDiagnosticsSink 适配器产出
# core::DiagnosticRecord"）。消费面＝core 公共契约 DiagData.hpp（其
# DiagnosticRecord 已随 DIAG-T03/T04 在 diagnostics 侧钉住 v0.1 基线），
# 不触及 core.md 其余 Draft 面。diagnostics 边按注入形态不落链接
# （IDiagnosticsSink 由 project 自有接口定义、diagnostics/L5 提供实现
# ——P-PR-6/P-EX-8 裁决链接形态，ird_gates 白名单已预登记
# project->diagnostics，不因卡存在自动转链接）。零 rw 基线库直链
# （rw::math 经 core 公共头传递，§1.4）、零 Qt（D-01 零 Qt 含 Core）、
# 零其他 industrialrobot 单元（业务域协作全走注入处理器/端口，
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
# （diagnostics 落位同款）。PRJ-T02+ 用例按 §11 各 PRJ-TX 组增列。
# PRJ-T02 增列：AtomicFileTest（PRJ-TX-2/4/5 的单文件原语层——F2/F4/F5
# 注入）。PRJ-T03 增列：StoreLockTest（acceptance 1 锁面/2 失权三道防线/
# 4 诊断契约——进程内多实例＋ILockOps 接缝 fake）、PathCanonicalTest
# （acceptance 3——规范化一致性＋同项目多路径打开 §9.3）。
# PRJ-T04 增列：PersistenceFormatTest（acceptance 1 round-trip 全类型/
# 2 非法拒绝＋版本兼容/3 externalRefs/4 CR-02 摘要边界——§4.4/§4.8/§8.11）。
# PRJ-T05 增列：ObjectStoreTest（acceptance 1 内容编址/只增发布/共享去重/
# LRU 预算；2 篡改检测＋PA-2 不可变；3 引用存在性＋D-10 原样存储；
# 4 CR-02/P-PR-1 处置自证——§4.6 全语义面）。
# PRJ-T06 增列：RevisionIndexTest（acceptance 1 §4.5.1 走查七步＋PM-12
# baseRevisionId 不复制对象——PRJ-TX-5 模型部分；2 闭包双通道孤岛/残留
# ＋谱系成环/分支表回退注入拒绝——PRJ-TX-12/INV-M1/M2/M3；3 O-15 序号
# 单调分配＋D-5 底本拷贝；4 P-PR-1 core 身份基线/P-PR-8 label 一次写入
# 处置自证）。
# PRJ-T07 增列：TxEngineTest（acceptance 1 §7.6 F1～F7 边界注入逐项——
# 经 IFileOps 接缝＋失败前后已提交内容字节比对＋恢复诊断；acceptance 2
# F8 崩溃边界进程内模拟——提交点前后两态字节不变，进程级 kill 归
# PRJ-T15；acceptance 3 §7.3 闭包外识别/查询不可见＋.staging/tmp 启动
# 清理〔D-09〕；acceptance 4 P-PR-1 处置——清单摘要与 core ContentDigester
# 直算比对）。
# PRJ-T08 增列：ProjectStoreTest（acceptance 1 PRJ-TX-9 落位面——Closed/
# Draining 迟到写拒绝＋稳定诊断〔PRJ-WRITE-AUTHORITY-LOST〕＋闭包外修订
# 不可见＋激活前失败不影响当前项目〔完成条件具名用例〕；acceptance 2
# 旧格式/未来版本稳定拒绝＋升级指引数据〔PM-06〕＋恢复报告逐字段断言
# 〔PM-08/§7.4①③④〕；acceptance 3 createNew 骨架/关闭排空〔Draining〕/
# 只读打开〔PM-07〕/进程内重复可写打开拒绝〔§9.3〕；acceptance 4 P-PR-1
# core 身份基线＋P-PR-6 诊断码收编清单/sink 可选注入）。
# PRJ-T09 增列：QueryPortTest（acceptance 1 并发只读全方法线程安全＋
# 与提交并发的一致性窗口压力〔4 线程×40 轮×全方法〕＋值拷贝/一经返回
# 不可变〔PA-2/PA-3〕；acceptance 2 闭包外修订端口级不可见〔tryRevision
# nullopt/revision StoreCorrupt/branchHistory 不含——A-6〕＋try 轨不存在
# 不抛＋context-closed 全方法拒绝〔tryObject noexcept 面 nullopt＋开发
# 诊断〕＋只读实例查询全量可用〔PM-07〕；acceptance 3 修订/元数据/分支
# /草稿/结果六类视图齐备逐字段断言〔含 INV-M3 权威纪律与 R2 历史浏览
# 区分〕＋ObjectCache 预算参数生效〔预算传导＋LRU 逐出观测面〕；acceptance 4
# P-PR-1 视图身份类型 core v0.1 同一性〔编译期 static_assert＋运行期
# 规范文本往返〕）。
# PRJ-T10 增列：CommandServiceTest（acceptance 3 单元面＝S1 形式校验
# 拒绝矩阵〔unknown-command/invalid-payload/未知分支/not-writable＋
# PRJ-LOCK-HELD/PRJ-WRITE-AUTHORITY-LOST 稳定诊断〕＋S3 处理器三态＋S4
# 确认决策四路〔unresolved/rejected/lost/放行——§6.7〕＋S6 提交面
# 〔恰一修订/seq 单调/负载透传 D-10/逆命令持久化 round-trip/对象身份
# 双路径/元数据增量机制面〔P-PR-9 不越界——无点 token〕〕＋S7 事件
# FIFO 与 D-18 不回滚＋hasUnresolvedPayload 判据注入〔T09 承诺兑现〕
# ＋注册表契约/命令槽直接观测/装配违约）；StubHandlers.cpp（§12 行
# test/StubHandlers.*——测试处理器桩/编译端口桩/交互桩/共享夹具）。
# PRJ-T12 增列：DraftServiceTest（acceptance 1 PRJ-TX-6 草稿用例——
# 保存与应用分离〔不产生修订 PM-04〕/破坏→重开 .bak 恢复＋损坏诊断
# PRJ-RECOVERY-ORPHAN-DRAFT/.new 残留丢弃 current·.bak 保留/放弃语义
# DiscardResult〔幂等目标态〕；acceptance 2 StaleRevisionRejected 冲突
# 路径〔应用＝恰一修订/过期拒绝并保留＋apply-retained 标记〕＋多模块
# 汇总投影〔排序/stale 判定/present 位/损坏面〕；acceptance 3 生命周期
# 边界〔§8.1 覆盖写 vs 只增/§8.6 只读拒绝写可读/Closed 拒绝/60 s 周期
# 边界＝autosave·manual 同一入口——定时器归 ui 本服务无定时器〕；
# acceptance 4 P-PR-1 透传自证〔SourcedValue/ValueProvenance 负载
# round-trip 逐字节一致＋磁盘直读佐证〕＋门卫/白名单 fail-fast 面）。
# PRJ-T13 增列：UndoRedoServiceTest（acceptance 1 PRJ-TX-5 undo/redo
# 部分——应用→撤销→重做各恰一新修订〔revisionSeq 递增＋revisions/ 目录
# 只增〕＋历史不改写〔既有修订视图逐字节不变＋branchHistory 清单只增〕
# ＋AT-29 锚点；acceptance 2 边界语义——空历史稳定提示数据〔不抛错〕/
# 跨分支提交后再回〔栈按分支隔离——D-11〕/重开项目会话栈清空〔redo
# 仅会话内、canUndo 由 tip 推导〕/逆命令由业务处理器声明、project 只
# 提交〔未注册逆命令 token→unknown-command 透传〕/新命令清空 redo＋
# 一次性 stale 说明；acceptance 3 与草稿局部撤销的边界——草稿保存/
# 恢复面不产生修订、不经本服务（§2.3/REQ-11 分界的存储侧形态）；status
# 前置违约 fail-fast 面）；StubHandlers.hpp 增补对称逆命令处理器桩
# （test-append-undo——撤销/重做用例的可逆命令族）。
# PRJ-T14 增列：ArchiveServiceTest（acceptance 2 F9 归档边界——manifest
# 原子发布＝完整判据〔D-13：无 manifest 即不完整，listRuns 不列＋重开
# 不列〕/§4.4.7 字段契约与 manifestDigest 自洽〔round-trip＋重算比对〕/
# 同 attempt 重投递幂等〔不重写〕/内容不一致＝PRJ-ARCHIVE-CONFLICT/
# 批次级重投递〔一致跳过/不一致冲突〕；acceptance 3 端口与引用语义——
# ArchiveSessionRef 生命周期〔空/失效句柄 fail-fast、终结后 fail-fast、
# 会话持票据＝requestClose 排空计数〕/relPath 白名单〔穿越/保留名/
# 设备名 fail-fast——NFR-SEC-01〕/跨项目 begin 拒绝＋B 项目 results 无
# 外来 run〔AT-10〕/只读上下文 begin 拒绝；acceptance 4 陷阱处置自证——
# 绑定原修订不因 HEAD 前进拒绝〔TASK-03/CON-02〕、abandon 全路径终结
# 〔四值原因＋幂等＋排空归零〕、失败即 abandon 不阻塞关闭〔A-4〕）。
# PRJ-T15 增列一：Tx11Tx13SuiteTest（§11 表 PRJ-TX-13 具名用例体——只读
# 上下文遍历全部写 API〔submit/undo/redo/save/discard/archive.begin〕一律
# 拒绝＋稳定诊断 PRJ-LOCK-HELD、错误码集合＝设计清单、零磁盘副作用；
# PRJ-TX-11 阻塞登记——另存为/包实体归阶段 B WP-04-T17/T18〔§13.1〕，
# GTEST_SKIP 如实标注前置未就绪，不计通过〔§7.2 四类分列〕）。
# PRJ-T15 增列二：TestMainReport（单元测试目标自有 main——安装 testkit
# TestRecordListener，IRD_TEST_INFO/IRD_* 断言详情聚合为
# ird-test-report.json 与 gtest XML 并存〔testkit §7.2/§7.3；acceptance 2
# 的 IRD_TEST_INFO/IRD_EXPECT_* 接入面〕；链接面同步 GTest::gtest_main
# →GTest::gtest——gtest_main 固定入口无监听器安装点）。
enable_testing()

add_executable(sdurws_ird_project_test
    test/BuildRedLineTest.cpp
    test/AtomicFileTest.cpp
    test/StoreLockTest.cpp
    test/PathCanonicalTest.cpp
    test/PersistenceFormatTest.cpp
    test/ObjectStoreTest.cpp
    test/RevisionIndexTest.cpp
    test/TxEngineTest.cpp
    test/ProjectStoreTest.cpp
    test/QueryPortTest.cpp
    test/CommandServiceTest.cpp
    test/DraftServiceTest.cpp
    test/UndoRedoServiceTest.cpp
    test/ArchiveServiceTest.cpp
    test/Tx11Tx13SuiteTest.cpp
    test/StubHandlers.cpp
    test/TestMainReport.cpp)

# src/ 私有实现头目录（仅测试目标 PRIVATE）：AtomicFileTest.cpp 消费
# src/win32/{IFileOps,AtomicFile}.hpp 接缝（PRJ-T02 acceptance 3——fake 经
# 该接缝注入，D-10 形态）。产品目标的 PUBLIC include 根仍只有 include/——
# 私有头不出单元（R-2），此目录不进 install/接口，仅供同单元测试目标
# 以 "win32/xxx.hpp" 相对形式包含。
target_include_directories(sdurws_ird_project_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

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

# 测试 main 自持（DTB §5.5/P-TK-1 定稿）：PRJ-T15 起以自有 main
# （TestMainReport.cpp——安装 testkit RecordListener）提供入口；测试链接
# 产品库 sdurws_ird_project 同时验证"公共头可被正常 include＋库可链接"。
# sdurws_ird_testkit＝testkit.md §10.2 project 行的消费面（TempDir/
# IRD_TEST_INFO/IRD_EXPECT_*——T-1 允许形态：仅测试目标链 testkit，产品
# 目标不链——NoTestkitEdge 用例持续钉住产品面零 testkit 边）。
target_link_libraries(sdurws_ird_project_test PRIVATE
    sdurws_ird_project
    sdurws_ird_testkit
    GTest::gtest)

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
# PRJ-T03 增列：LockContractTest（acceptance 1——PRJ-TX-7 锁双实例契约的
# 真双进程形态：第二实例只读＋PID／心跳卡顿不接管〔D-03〕／崩溃后并发
# 接管仅一胜者〔D-02〕；§9.3 同项目多路径打开跨拼写拒绝；测试自再执行
# 子进程，TestProcessRunner 归 PRJ-T15 按 testkit 节奏重述）。
# P-PR-1 处置说明（PRJ-T03 更新）：锁诊断的行为级消费已随 StoreLockTest
# （_test 目标）建立——CapturingSink 实现 §5.0 IDiagnosticsSink 并以
# core::DiagnosticRecord 承载断言；本目标仍以构建图文本扫描＋配置期守卫
# ＋双模式链接成功承载边界证据（LinkageContractTest 零改动）。
# PRJ-T10 增列：CommandContractTest（acceptance 1/2 的契约面——§3.3
# "命令契约属跨单元契约测试面"：PRJ-TX-1 双线程并发 submit 全局串行
# 〔各恰一修订/seq 单调无重复/线性链〕＋过期基线 Rejected(stale-revision)
# ＋PRJ-STALE-REVISION-REJECTED 稳定码双通道；PRJ-TX-3 双编译桩三路
# 〔WC 失败/DWC 失败→Failed(compile-failed)＋无修订无发布＋.staging/tmp
# 清理；双成功→恰一修订〕；§5.3.6 冻结签名消费——P-PR-7）。
# PRJ-T14 增列：ArchiveContractTest（acceptance 1 的契约面——PRJ-TX-8
# 进程内模拟归属 §3.3 跨单元契约测试面〔契约 note 登记口径〕：
# requestClose 后迟到完成事件到达——上下文存活至归档 finalize、完成后
# 才 Closed〔一次性回调时序断言〕；跨项目事件不写入〔项目 B results
# 目录不含外来 run——TASK-03/PM-13〕；ResultArchived 事件经 core
# IDomainEventBus〔ReferenceEventBus 注入——P-PR-1 处置的行为级自证〕；
# 并发归档流与 writer 互斥〔P-PR-4 单侧冻结的行为级自证——双会话并发
# batch/finalize 全部成功且磁盘一致〕）。
# PRJ-T15 增列：ProcessCrashContractTest／ProcessLockContractTest／
# Tx2DiagnosticRecordContractTest／Tx15Child（§12 PRJ-T15 行产物——
# TestProcessRunner 接入的真进程用例体：F8 崩溃边界五挡〔kill 于 T1～
# T6 每步后重启恢复——RecoveryReport 逐项＋字节不变，AT-11/13〕、
# PRJ-TX-8 在途归档强杀〔D-13 无 manifest＝不完整〕、PRJ-TX-7 锁三场景
# 的 TestProcessRunner 重述〔第二实例只读+PID／存活不接管 D-03／崩溃
# 并发接管仅一胜者 D-02〕、PRJ-TX-2 观测点"诊断字段完整"经 ContractCheck
# 机器校验〔IRD_TEST_INFO/IRD_EXPECT_* 消费面〕。Tx15Child.cpp＝真进程
# 子进程半边〔无 gtest——main 拦截分派；FaultInterceptor 经 IFileOps
# 接缝停靠，testkit.md D-10 形态〕。testkit §10.1 触发登记的兑现：
# TestProcessRunner/EventWatch/FaultInterceptor/TempDir 按 T-1 允许形态
# 仅入本测试目标——产品目标零 testkit 边不变）。
add_executable(sdurws_ird_project_contract_test
    test/LinkageContractTest.cpp
    test/LockContractTest.cpp
    test/CommandContractTest.cpp
    test/ArchiveContractTest.cpp
    test/ProcessCrashContractTest.cpp
    test/ProcessLockContractTest.cpp
    test/Tx2DiagnosticRecordContractTest.cpp
    test/Tx15Child.cpp
    test/StubHandlers.cpp)

# src/ 私有实现头目录（仅测试目标 PRIVATE）：LockContractTest.cpp 消费
# src/win32/{StoreLock,PathCanonical}.hpp 与 ILockOps 真实实现（acceptance
# 1/3——真双进程锁契约直接驱动锁原语与路径规范化）。产品目标的 PUBLIC
# include 根仍只有 include/——私有头不出单元（R-2），此目录不进
# install/接口，仅供同单元测试目标以 "win32/xxx.hpp" 相对形式包含。
target_include_directories(sdurws_ird_project_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(sdurws_ird_project_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_project_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_project_contract_test PRIVATE /utf-8)
endif()

# 链接面：产品目标＋gtest＋testkit（T-1 允许形态——testkit 仅测试目标
# 可链，PRJ-T15 登记：TestProcessRunner/EventWatch/FaultInterceptor/
# TempDir/ContractCheck/RecordListener 消费；产品目标仍零 testkit 边
# ——NoTestkitEdge 用例持续钉住；不显式链 core——PUBLIC 经
# sdurws_ird_project 传染，公共头 StoreTypes.hpp 的 core::DiagnosticRecord
# 消费由该传染路径解析）。
# main 自持（DTB §5.5"测试 main 自持"——本目标以自有 main 落实）：GTest::
# gtest（无入口）——LockContractTest.cpp 的 main 须在 gtest 初始化**之前**
# 拦截子进程再执行分派（--ird-wp04-t03-child／--ird-wp04-t15-child，
# PRJ-TX-7 真双进程＋PRJ-T15 真进程契约），gtest_main 的入口无此拦截点；
# 同一 main 在 gtest 初始化后安装 testkit RecordListener（§7.3——
# ird-test-report.json 与 gtest XML 并存）。
target_link_libraries(sdurws_ird_project_contract_test PRIVATE
    sdurws_ird_project
    sdurws_ird_testkit
    GTest::gtest)

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
