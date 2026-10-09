# acc-diag-schema-level 验收复现留痕（独立子会话对抗式验收，attempt 1，2026-10-10）

验收者独立复现现场（worktree `acc-dsl-repro`，detach 于冻结 headSha
`317edd5f11032250eb85aeff2ef38e03e22a2df7`）。全部产物为本验收者亲手生成，
不采信实施者留痕。目录文件清单：

| 文件 | 内容 | 结论 |
| --- | --- | --- |
| `acc-configure.log` | 集成模式配置（VS2022 x64，RWS_BUILD_INDUSTRIALROBOT=ON，缓存核实 `:BOOL=ON`） | 配置成功 |
| `acc-build.log` | 集成全量构建：EXIT=1，**73 个编译错误全部位于排除面**（RWSimulatorPlugin.vcxproj/rwplugin.vcxproj，逐错误码归类见验收记录 §4.2），**sdurws_ird_* 零错误**（grep 零命中） | 符合 DTB §5.1 排除面口径 |
| `acc-build-testtargets.log` | 按 F-622 口径显式 `--target` 补建全部 40 个测试目标：EXIT=0，零编译/链接错误；40 套件 exe 全产出（另有 testkit_prochelper 非 suite） | 通过 |
| `acc-gates.log` | `ird_gates` 目标：EXIT=0；7 个自测预期检出（IRD-GATE-R1/R3/R4/R5/SUB/T1/T2，控制台 GBK mojibake 为 F-621 已登记现象），实扫描 R-1~R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中 | 与 wp22-t05 基线形态一致 |
| `acc-validate-docs.log` | validate-docs：PASS（20 units, 12 trace entries, 312 task files） | 通过 |
| `acc-run-exitcodes.txt` | 集成模式 40 套件逐一**直跑**（F-619 口径）退出码：40/40 全 EXIT=0 | 通过 |
| `gtest/`（40 XML＋log） | 集成直跑汇总：**4224 用例 / 0 失败 / 0 错误 / 9 设计性跳过**（dynamics·optimization·selection 的 WidgetPresentationDeferredToHarnessEnvUnavailable＋kinematics·requirements 的 *_ACC1＋modeling_gui PreviewPane＋project StageB＋runtime ChildProcessWorker_RT_ID_1 子进程形态＋ui_gui BatchPasteImpactDetail 剪贴板 F-620 归因——XML `<skipped>` 消息原文核对） | 通过；**注：实施者声称 8448＝恰为本数 2 倍（root＋children 双加簿记口径），实质通过结论不受影响——转登 F-627** |
| `acc-smoke-configure.log`/`acc-smoke-build.log` | 独立冒烟模式（vcpkg toolchain＋Qt 前缀）：配置＋构建 EXIT=0，零错误 | 通过 |
| `acc-smoke-exitcodes.txt`/`gtest-smoke/`（40 XML＋log） | 冒烟 40 套件直跑：**3874 用例 / 0 失败 / 0 错误 / 8 skipped** | 通过；同 F-627（实施者声称 7748＝2×3874） |
| `mutations/` | 双变异真实失败能力（4.5）：①`CompilerImpl.cpp` warningRecord 删显式 `DiagnosticLevel::Warning`（跌落缺省 Error）→ 重建后 `CompilerTest.BoundsMagnitudeWarningPublishesWithDedicatedCode_F593` **FAILED**（`out.status` 1≠Published——builder 第 10 步按级别拒绝生效）→ 还原（git diff 空）→ PASSED；②`Codec.cpp` readDiagnostic 的 enumByte 词表上界改 `0xFF`（放行未知级别字节）→ `CodecRoundtripTest.DiagnosticLevelRoundtripsAndRejectsUnknownByte_F618` **FAILED** → 还原 → 复绿（另附 RejectsErrorLevelDiagnostics 一并 PASSED 佐证） | 缺省 fail-closed 承诺与词表值域拒绝均有真实失败能力 |

## 复现环境

- A 树：`git worktree add --detach <tmp>/acc-dsl-repro 317edd5f…`＋RobWorkStudio.ini.template.static 复制＋vcpkg junction。
- 测试直跑 PATH 前置：A 树 `build/RobWorkStudio/bin/Release`＋主树 vcpkg `installed/x64-windows/bin`＋Miniconda3（F-619 直跑口径）。
- 冒烟首跑一轮因验收者脚本统一 SMOKEBIN 前缀致 33 套件 EXIT=127（exe 路径错，非被验代码问题）——改按各单元目录全路径重跑后 40/40 EXIT=0；首跑留痕未入库（无效数据），以本目录重跑产物为准。
