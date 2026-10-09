# diag-schema-level 批次留痕（F-618 诊断面 schema 演进，2026-10-10）

分支 `diag-schema-level`（基于 redesign-main@ffb191b0）。提交链：

| 提交 | 内容 |
| --- | --- |
| 3efc328a | [units] 三卡增量修订（core v0.12／diagnostics v0.14／runtime v0.20——先单元卡后代码） |
| 1c7c0a0c | [diag-fix][F-618] DiagnosticRecord.level＋RT-Codec 序列化（canonical-model/1.1） |
| 9366fd38 | [diag-fix][F-618] StableCodeRegistry level 属性＋RT-BOUNDS-MAGNITUDE 正式注册（88 码） |
| 92a1e1d5 | [diag-fix][F-618] builder 发布拒绝改按 record.level（硬失败码集废除） |
| 2fb913e9 | [diag-fix][F-618] 域单元码表产码面按 severity 派生 level（首轮直跑暴露的 8 处消费面补齐） |
| 5ddc2ad0 | [governance] F-618 findings 销账 |

## 复验证据（本目录）

- `build-integration.log`——集成模式全量构建：EXIT=1，73 个错误全部位于
  DTB §5.1 排除面（RWSimulatorPlugin 71＋bt_plugin 2），**sdurws_ird_* 零错误**。
- `build-test-targets.log`——排除面失败后按 F-622 口径显式 `--target` 补建
  全部 40 个测试目标（`sdurws_ird_runtime_contract_test` 不存在——runtime 单元
  无该目标，枚举误列；最终 40 套件全产出）：零编译/链接错误。
- `gtest/`——集成模式 40 套件逐一**直跑**（gate-all 的 ctest 调用对 ui_test
  挂起——F-619 已登记，直跑为登记口径）：`run-all-exitcodes.txt` 40 唯一套件
  全 EXIT=0；XML 汇总 **8448 用例 / 0 失败 / 0 错误 / 9 skipped**（设计性跳过：
  ContractSuite.ChildProcessWorker_RT_ID_1 子进程形态＋F-620 剪贴板占用归因
  SKIPPED——预期形态非失败）。
- `gtest-smoke/`＋`build-smoke.log`＋`smoke-configure.log`——独立冒烟模式
  （按 DTB §5.1 口径配置，vcpkg toolchain＋Qt 前缀）：构建 EXIT=0 零错误；
  40 套件直跑全 EXIT=0，**7748 用例 / 0 失败 / 0 错误 / 8 skipped**。
- `gates-ird-gates-target.log`——`ird_gates` 目标 EXIT=0；7 命中
  （IRD-GATE-R1/R3/R4/R5/SUB/T1/T2）与 wp22-t05 基线**逐码一致**，全部为门禁
  自测的预期检出（fail_r\*/fail_t\*/fail_sub），零新增。注：控制台捕获链路的
  GBK mojibake 为 F-621 已登记现象（内容可辨），已按其 resolution 以 UTF-8
  落盘口径重抓。
- `validate-docs.log`——validate-docs PASS（20 units, 12 trace entries,
  312 task files）。

## 关键取舍（验收者注意）

1. **缺省级别＝Error（fail-closed）**：警告产生路径必须显式声明
   `DiagnosticLevel::Warning`，漏标在 builder 第 10 步可见地被拒——
   失败方向是"该发的发不出"（可测），不是"该拦的没拦"。三张卡均有登记。
2. **序列化兼容性**：IRDCANO/IRDMAT1 为 worker 物化传输面（不入 .rwdesign，
   无跨版本存量字节）；诊断块不入身份域（contentIdentity 不变，既有钉在位）。
   `kVersionMinor` 0→1 后旧 minor=0 字节经既有"版本不符＝拒绝"检查显式拒绝。
   未触发"停下该子项"的保守路线条件。
3. **拒绝面行为收敛不变**：原硬失败码集十码在注册表 severity 均为 Error
   ⇒ level 派生 Error ⇒ 照旧阻断；新放行面＝显式 Warning 级记录
   （"警告不阻断"承诺的机制承载）。同码 Warning 级放行钉在
   `CanonicalModelTest.RejectsErrorLevelDiagnostics`（级别是唯一判据的物证）。
4. **RT-RESOURCE-RECORDED 与 RT-NAME-DISAMBIGUATED 维持未注册建议码面**
   （本批裁决面只收编 RT-BOUNDS-MAGNITUDE；两码的级别已按事实显式声明
   Warning，收编留待后续批次）。
5. **registryCodeOverride 机制保留**：RuntimeErrorCode 枚举冻结（§10.11
   关系不扩），警告码无枚举对应——覆盖码面是已注册警告码的唯一承载通道。
