/**
 * @file   Reporting.cpp
 * @brief  reporting 单元构建落位的首个翻译单元（src/ 空起步——单元卡 §11
 *         RPT-T01 行"涉及文件"列）。
 *
 * 设计依据：
 *   - units/reporting.md §11 RPT-T01 行（≙WP-12-T02：产物 `sdurws_ird_
 *     reporting` 升级 STATIC〔C++17、链 core＋evidence＋diagnostics＋
 *     project〕；涉及文件 `industrialrobot/CMakeLists.txt`、`reporting/
 *     CMakeLists.txt`（新）、`src/`（空起步））与 §3.4（命名空间/目标与
 *     CMake 集成——头包含形式 <sdurws/ird/reporting/Xxx.hpp>，私有实现头
 *     不入 include/）；
 *   - 任务契约 tasks/foundation/RPT-T01.json acceptance 1（双模式构建零
 *     错误：INTERFACE 占位升级 STATIC、"src/ 空起步，不在骨架预建占位
 *     实现"）；
 *   - 先例：core/src/Core.cpp（CORE-T01）、policy/src/Policy.cpp
 *     （POL-T01）、diagnostics/src/Diagnostics.cpp（DIAG-T02）、project/
 *     src/Project.cpp（PRJ-T01）、execution/src/Execution.cpp（EX-T01）
 *     ——落位任务以"锚点翻译单元"起步，真实实现随后续同名任务文件填充。
 *
 * 背景说明（为什么是注释文件而不是"零源码"）：CMake 对 STATIC 库强制
 * "至少一个翻译单元"——零源码的 add_library(STATIC) 在生成期即报
 * "No SOURCES given to target"（实测），库无法参与构建。因此"src/ 空起步"
 * 的既定形态＝仅含本注释块、不含任何声明或定义的锚点翻译单元：让
 * sdurws_ird_reporting 以真实库形态参与双模式构建（集成／独立冒烟），验证
 * 目标注册、C++17 编译与四条登记链接边（reporting→core/evidence/
 * diagnostics/project，ARCH §3.5——§3.4 原文 PUBLIC 全链），同时**不预建
 * 任何接口**（NFR-MNT-04：无消费者的能力不预建——契约 acceptance 1"不在
 * 骨架预建占位实现"的本义；报告模型/渲染/导出/归档等语义能力全部随
 * RPT-T02+ 进入本目录）。
 *
 * 后续任务落位安排（units/reporting.md §11，实现文件随同名任务进入 src/
 * 或 include/，本文件届时或承载跨实现文件共享的内部工具，或拆分为同名
 * 任务文件后删除；以单元任务卡当前正文为准）：
 *   - RPT-T02：Errors.hpp/.cpp（§3.5 错误码/ReportError）、Identity.hpp
 *     （§4.1 ReportId/PublishedReportRecord/ReportLevel）；
 *   - RPT-T03：ReportModel.hpp/.cpp（§4 全部引用类型/字段校验）＋
 *     ReportCodec（§4.4 Data/Full 双编码）；
 *   - RPT-T04：Sections.hpp/.cpp（§5 章节词表/注册表）＋SectionProvider.hpp
 *     （§9.2 接口＋注册表）；
 *   - RPT-T05：Builder.hpp/.cpp（§7.1 会话状态机/锚定/冻结）；
 *   - RPT-T06/T08：Render.hpp/.cpp（§8 三格式渲染＋FieldMatrix 提取＋措辞
 *     冻结规则——注入 io 工厂）；
 *   - RPT-T07：Consistency.hpp/.cpp（§8.5 逐字段一致性检查）；
 *   - RPT-T09：Export.hpp/.cpp、Archive.hpp/.cpp（§7.3/§7.4 幂等导出与
 *     归档协调＋IReportArtifactSink 契约与 Fake）；
 *   - RPT-T10：Bundle.hpp/.cpp（§7.6 证据包组装＋IArchiveWriter 契约与
 *     Fake）；
 *   - RPT-T12：ModelSummary.hpp＋ITaskStatusSource 最小接口（§9.7/§3.3
 *     注入边界）。
 *   测试替身与 RP-* 用例体落位 test/ 与 contract_test/（RPT-T11）。
 *
 * 陷阱处置约束（任务契约 acceptance 5）：
 *   - P-RPT-1：io 写出能力不落编译边——CSV/JSON 写出与原子目标经注入的
 *     IReportIoFactory（§9.5）消费，公共头零 io 类型；P-RPT-1/P-IO-1/
 *     P-PR-5 合并裁决前维持注入、届时签名零改动直连。本文件有意不
 *     include 任何 io 头。
 *   - P-RPT-2：runtime 只读摘要经注入的 IModelSummaryProvider（§9.7），
 *     零 runtime 编译边——本文件有意不 include 任何 runtime 头。
 *   - P-RPT-9：core/evidence/diagnostics/project 各卡 v0.1 仍 Draft 未
 *     冻结，本任务仅建立四条登记链接边、**零上游公共头消费**——本文件
 *     有意不 include 任何 sdurws/ird/<他单元> 头；首个消费任务（RPT-T02，
 *     错误与身份类型）起方按各卡冻结签名进入 include 面，冻结出 diff 后
 *     按影响面增量同步留痕。
 *
 * 线程约束：本文件当前无可执行代码，无线程安全性议题；后续各实现的线程
 * 模型遵循 units/reporting.md §9.9 跨单元协作总表逐一标注（报告对象构造
 * 后不可变——PA-2/§4.2，构建器会话状态机的线程归属随 RPT-T05 落地时
 * 标注）。
 */

// 有意留空：见文件头"背景说明"——锚点翻译单元只承担"库可生成、C++17 可编译"
// 的构建面职责，语义能力全部随 RPT-T02+ 的同名任务文件进入本目录。
