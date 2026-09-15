/**
 * @file   Project.cpp
 * @brief  project 单元构建落位的首个翻译单元（src/ 空起步——单元卡 §12
 *         PRJ-T01 行"涉及文件"列）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T01 行（产物：`sdurws_ird_project` 升级
 *     STATIC〔C++17、链 core〕；涉及文件 `industrialrobot/CMakeLists.txt`、
 *     `project/CMakeLists.txt`（新）、`src/`（空起步））与 §3.3/§3.4
 *     （目标/命名空间/CMake 集成与源码目录布局）；
 *   - 任务契约 tasks/foundation/PRJ-T01.json acceptance 1（双模式构建零
 *     错误：INTERFACE 占位升级 STATIC、"src/ 空起步，不在骨架预建占位
 *     实现"）；
 *   - 先例：core/src/Core.cpp（CORE-T01）、policy/src/Policy.cpp
 *     （POL-T01）、diagnostics/src/Diagnostics.cpp（DIAG-T02）——落位任务
 *     以"锚点翻译单元"起步，真实实现随后续同名任务文件填充。
 *
 * 背景说明（为什么是注释文件而不是"零源码"）：CMake 对 STATIC 库强制
 * "至少一个翻译单元"——零源码的 add_library(STATIC) 在生成期即报
 * "No SOURCES given to target"（实测），库无法参与构建。因此"src/ 空起步"
 * 的既定形态＝仅含本注释块、不含任何声明或定义的锚点翻译单元：让
 * sdurws_ird_project 以真实库形态参与双模式构建（集成／独立冒烟），验证
 * 目标注册、C++17 编译与链接边界（project→core 唯一登记编译链接边，
 * ARCH §3.5），同时**不预建任何接口**（NFR-MNT-04：无消费者的能力不预建
 * ——契约 acceptance 1"不在骨架预建占位实现"的本义）。
 *
 * 后续任务落位安排（units/project.md §12，实现文件随同名任务进入 src/，
 * 本文件届时或承载跨实现文件共享的内部工具，或拆分为同名任务文件后删除）：
 *   - PRJ-T02：win32/AtomicFile.{hpp,cpp}——已落位（2026-09-15）：存储原语
 *     publishNew/原子替换/write-through 写＋IFileOps 接缝（§7.1/§7.2）；
 *   - PRJ-T03：win32/StoreLock.{hpp,cpp}、win32/PathCanonical.{hpp,cpp}
 *     （写锁与路径规范化——§9.1~§9.6）；
 *   - PRJ-T04：PersistenceFormat.hpp（include 侧）＋Codec.{hpp,cpp}
 *     （格式契约与 canonical 编码——§4.2/§4.4/§4.8）；
 *   - PRJ-T05：ObjectStore.{hpp,cpp}（内容编址对象库——§4.6）；
 *   - PRJ-T06：RevisionIndex.{hpp,cpp}（修订 DAG/分支/闭包双通道——§4.3/§4.5）；
 *   - PRJ-T07：TxEngine.{hpp,cpp}（七步事务状态机＋恢复扫描——§7）；
 *   - PRJ-T08：ProjectStoreFactory/ProjectStoreImpl（打开/恢复/生命周期——§8.7/§7.4）；
 *   - PRJ-T09：QueryPort 实现（§5.2 查询端口）；
 *   - PRJ-T10：CommandServiceImpl（命令服务＋内置元数据处理器——§5.3/§6）；
 *   - PRJ-T11：确认放行流编排（§5.3.3/§6.7）；
 *   - PRJ-T12：DraftServiceImpl（草稿服务——§5.4/§8.1~§8.6）；
 *   - PRJ-T13：UndoRedoServiceImpl（撤销/重做——§5.5/§6.9）；
 *   - PRJ-T14：ArchiveServiceImpl（归档端口——§5.6/§10.1）。
 * 以单元任务卡当前正文为准。
 *
 * P-PR-1 处置约束（任务契约 acceptance 5）：core.md v0.1 仍为 Draft 未冻结，
 * 本任务**仅建立 project→core 链接边、零 core 头消费**——本文件有意不
 * include 任何 sdurws/ird/core 头；首个消费任务（PRJ-T04，格式契约）起
 * 方按 core.md 冻结签名进入 include 面。
 *
 * 线程约束：本文件当前无可执行代码，无线程安全性议题；后续各实现的线程
 * 模型遵循 units/project.md §9.8 总表逐一标注。
 */

// 有意留空：见文件头"背景说明"——锚点翻译单元只承担"库可生成、C++17 可编译"
// 的构建面职责，语义能力全部随 PRJ-T02+ 的同名任务文件进入本目录。
