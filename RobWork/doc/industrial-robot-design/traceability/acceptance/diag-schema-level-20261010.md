# diag-schema-level 对抗式验收记录（attempt 1）

- **验收日期**：2026-10-10
- **验收模式**：独立子会话对抗式验收（acceptance-protocol v1.7 §2.2 模式，所有者 2026-10-10 指令授权）——验收者与实施者不共享会话记忆，为"不通过"找证据；一切结论以验收者亲手复现产物为准
- **任务 ID**：`diag-schema-level`（F-618 诊断面 schema 演进批次：DiagnosticRecord 增设 level＋builder 按级别拒绝＋RT-BOUNDS-MAGNITUDE 收编；无 canonical 契约，治理性批次适配口径）
- **分支**：`diag-schema-level`
- **冻结 headSha**：`317edd5f11032250eb85aeff2ef38e03e22a2df7`（验收首步 `git fetch origin` 后 `rev-parse origin/diag-schema-level` 与之相等——无分支漂移）
- **commit 范围**：`ffb191b0..317edd5f`（7 提交：3efc328a 单元卡三卡先行 / 1c7c0a0c core level＋RT-Codec / 9366fd38 注册表 level＋收编 88 码 / 92a1e1d5 builder 按级别拒绝 / 2fb913e9 域消费面派生 / 5ddc2ad0 findings 销账 / 317edd5f 复验留痕）
- **验收者复现现场**：A 树 worktree（detach 于冻结 head），B 树 `acc/diag-schema-level/1`；复现产物 `traceability/builds/acc-diag-schema-level/`

## 逐项结论（4.1~4.11 适配口径）

| # | 项 | 结论 | 证据（验收者亲手） |
| --- | --- | --- | --- |
| 4.1 | 改动面分类 | **PASS** | `git diff --name-only ffb191b0..317edd5f`：industrialrobot 产品/测试＋units/core.md·diagnostics.md·runtime.md 三卡＋traceability（findings.json、builds/diag-schema-level/）；patches/ 零改动、框架源码零改动（SA-02 红线过）。core.md 增补合规性核实：DiagnosticRecord 权威确在 core.md §4.8（实测 v0.12 §4.8 字段表含 `.level` 行与 DiagnosticLevel 行）——"计划外"增补属 DTB §5.4 同批修订纪律合规形态 |
| 4.2/4.3 | 双模式构建＋门禁 | **PASS** | 集成配置缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`；全量构建 73 错误**全部**归属排除面（RWSimulatorPlugin/rwplugin.vcxproj，逐错误码 C2039/C2059/C2086/… 归类核对），sdurws_ird_* 错误 grep 零命中；按 F-622 显式 `--target` 补建 40 测试目标 EXIT=0；冒烟配置＋构建 EXIT=0 零错误；`ird_gates` EXIT=0（7 自测预期检出 R1/R3/R4/R5/SUB/T1/T2，实扫描零命中——与 wp22-t05 基线形态一致）；validate-docs PASS（20 units, 12 trace, 312 task files） |
| 4.4 | findings 对照 | **PASS** | 基点 ffb191b0 实测：F-618 open / F-619 open / F-620 open / F-621~624 fixed（max=624）；冻结 head 实测：仅 F-618 open→fixed（resolution 与实测一致），F-619/F-620 保持 open，F-621~624 保持 fixed——逐项与基点一致，无越权销账。注：送验单"基点 F-620 已 fixed"表述与实测（open）不符，但实际状态＝保持基点，不构成偏差 |
| 4.5 | 真实失败能力（双变异） | **PASS** | 变异①删 warningRecord 显式 Warning（跌缺省 Error）→ `BoundsMagnitudeWarningPublishesWithDedicatedCode_F593` FAILED（status 1≠Published——builder 按级别拒绝的缺省 fail-closed 承诺有牙）→ 还原 PASSED；变异②Codec enumByte 上界改 0xFF → `DiagnosticLevelRoundtripsAndRejectsUnknownByte_F618` FAILED → 还原后三钉（含 RejectsErrorLevelDiagnostics）全 PASSED。两次变异/还原输出均留档 `builds/acc-diag-schema-level/mutations/` |
| 4.6 | 架构红线（AGENTS §5 八条） | **PASS** | L2 零 Qt：触碰的 core/diagnostics/runtime 产码文件 Qt include grep 零命中；R-2：diff 新增 include 仅 `<cstring>`（std），无跨单元私有头；PA-2：DiagnosticRecord/CodeDescriptor 均表尾追加、既有字段语义零改写（project 等未触碰单元既有 make() 调用抽样 5 处均零改写依赖缺省）；severity⇔level 映射不变式：注册期第⑨检查（DiagCodes.cpp，检查序①~⑧零变化）＋双内置钉 F618_LevelMappingAcrossBuiltinTable/F618_LevelSeverityMismatchRejected 在位；R-1 六类端口协作面零新增直链 |
| 4.7 | 注释规范（§2 全要素） | **PASS** | 公共头全查：core/DiagData.hpp（DiagnosticLevel 词表注释含置 core 依据 CR-08、fail-closed 取舍、与 severity 正交说明；make() 尾参 @param 契约）、diagnostics/DiagCodes.hpp（CodeDescriptor.level 字段约束、IRDDCM2、注册期验证行）、runtime/Codec.hpp（kVersionMinor 升版纪律、布局注释）、CanonicalModel.hpp（setDiagnostics 契约）；实现抽查 ≥3：DiagData.cpp（level 不校验的理由）、DiagCodes.cpp（levelFromSeverity 单点＋第⑨检查＋RT-BOUNDS-MAGNITUDE 登记）、Codec.cpp（尾追加字节纪律＋enumByte 上界）、CanonicalModel.cpp（判据历史注——废除原因与结构性根因如实留档）、Snapshot.cpp（Cancelled/消歧警告显式 Warning 语义）——单位/坐标系/错误语义/确定性来源等高危信息齐备，无 §2.6 反面形态 |
| 4.8 | 文档同步 | **PASS** | core.md v0.12/diagnostics.md v0.14/runtime.md v0.20 与实现逐点一致；**兼容性口径写实核实**：runtime.md v0.20 与 Codec.hpp 实文一致——IRDCANO 全字段编码仅存续于会话内 worker 物化通道（Snapshot.hpp §9.5 MaterializedSnapshotCodec magic IRDMAT1，"不是项目持久化格式"实文在位）、不入 .rwdesign、诊断块不入身份域 contentIdentity 不变；kVersionMinor 0→1 后旧 minor 字节经 parse 版本门（`major != kVersionMajor ‖ minor != kVersionMinor` 即拒，Codec.cpp:908）显式拒绝——实现与声明一致（但 minor 专项钉缺失，转登 F-626）；IRDDCM1→IRDDCM2 全库一致（grep 旧串仅存演进说明注释）；isHardErrorDiagCode 代码面零残留引用（唯一命中为 CanonicalModel.cpp:313 判据历史注的刻意留档说明，非失效引用） |
| 4.9 | commit 质量 | **PASS** | 7 提交逐条通读：四段式（What/Why/How/影响面）完整、前缀合规（[units]×1/[diag-fix][F-618]×4/[governance]×2）、验证段与实测相符、无粉饰——92a1e1d5 如实登记"判据历史注留档"、317edd5f 如实登记排除面 73 错误归属 |
| 4.10 | 留痕 | **PASS** | 链上 `traceability/builds/diag-schema-level/`（40 套件 XML＋构建/门禁/校验日志＋README）在库完整；验收者复现汇总已落 B 树 `traceability/builds/acc-diag-schema-level/`＋本记录 |
| 4.11 | 偷懒扫描＋断言强度 | **PASS** | 产品 diff 新增行 TODO/FIXME/XXX/HACK/stub/未实现 grep 零命中；空实现零（新增函数均承载语义）；新增/适配钉 ≥5 逐一读码：RejectsErrorLevelDiagnostics（同码 UnitMismatch Warning 放行＋Error 拒绝＋缺省拒绝＋Cancelled 放行——**级别是唯一判据的物证断言在位**）、CodecTest F618 钉（字节布局锚＋未知字节拒绝）、StableCodeRegistryTest 双钉（全表派生＋双向 mismatch 拒绝＋合法对放行）、CompilerTest F593 适配（逐条 level 断言）、core DiagRecordLevel 缺省/等值钉——断言强度全部合格；"make() 尾参缺省零改写"抽样 5 处（TxEngine×2/ProjectStoreImpl 等）核实成立 |

## 测试与门禁复现数据（验收者实测）

- 集成模式：40 套件直跑 40/40 EXIT=0；**4224 用例 / 0 失败 / 0 错误 / 9 设计性跳过**（含 F-620 剪贴板占用 SKIPPED，XML skip 消息原文核对："clipboard blocked by external owner (F-620)"）。
- 冒烟模式：40 套件直跑 40/40 EXIT=0；**3874 用例 / 0 失败 / 0 错误 / 8 skipped**。
- ird_gates：EXIT=0，7 命中全为门禁自测预期检出（与 wp22-t05 基线逐码同形态），实扫描零新增。
- validate-docs：PASS（20 units, 12 trace entries, 312 task files）。

## 问题分级

### 阻断（blocking）

无。

### 建议级（强制转登 findings.json，编号自 F-625 顺延）

1. **F-625**（minor）：`runtime/src/Codec.cpp` parse 版本拒绝文案硬编码"本编码器＝1.0"（:912），本批 kVersionMinor 0→1 升版后该用户可见诊断串失实（功能判定不受影响——拒绝比较仍用常量正确面）。升版批次应同步动态化或修正该串。
2. **F-626**（suggestion）：kVersionMinor 0→1"旧 minor=0 字节显式拒绝"有实现（parse 版本门同分支）与声明，但无 minor 专项钉——`CodecNegativeTest.RejectsMalformedEncodings` 仅翻 major 字节（bad.at(8)）；minor 位（bytes 10-11）的旧值拒绝行为当前无直接回归物证，与三卡"旧 minor 字节显式拒绝"写实声明存在钉面缺口。
3. **F-627**（minor，簿记）：实施者复验留痕（builds/diag-schema-level/README.md＋findings.json F-618 resolution）声称"集成 8448 用例/冒烟 7748 用例"，与验收者直跑实测（集成 4224/冒烟 3874）恰为 **2 倍**——root `<testsuites>` 属性与 `<testsuite>` 子行双重累加的聚合口径错误；通过/失败/跳过等实质结论不受影响（0 失败/0 错误复现成立），但用例总数登记失实应更正。

### 环境说明

- 剪贴板被外部占用（MATLAB）：ui_gui `BatchPasteImpactDetail_UI_T08_ACC1` SKIPPED，skip 消息自带 F-620 归因——预期形态非失败。
- ird_gates 控制台捕获链路 GBK mojibake：F-621 已登记现象，内容可辨，不影响结论。
- 验收者冒烟首跑因自身脚本路径缺陷 33 套件 EXIT=127（exe 定位错，与被验代码无关），改全路径重跑后 40/40 EXIT=0；以重跑产物为准（已在复现 README 声明）。
- 本验收仅 push `acc/diag-schema-level/1`；A 树复现现场与 B 树于记录推送确认后清理。

## 裁决

**verdict = PASS**（11 项全过零阻断；3 条建议级 F-625/F-626/F-627 强制转登，交所有者裁决排期）。
