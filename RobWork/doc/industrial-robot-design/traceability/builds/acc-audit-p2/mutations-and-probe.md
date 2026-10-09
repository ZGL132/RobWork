# 真实失败能力（变异翻红）与剪贴板预探验证——验收者复现记录（attempt 1）

环境：独立 worktree `C:/Users/zgl18/AppData/Local/Temp/acc-p2-repro`（detached @ `c0bdd020b4acc558630e50d5f7d3ad7e04a1f344`），集成模式 MSVC 2022 x64 Release。全部变异在复现后还原、重建复绿，`git status` 跟踪文件零漂移。

## 变异①——F-584（默认 TCP 排序重定位）

- 对象：`runtime/src/CanonicalModel.cpp` builder 第 11 步；将排序后按 ObjectId 重定位的 `if` 分支禁用（`if (false && …)`），恢复"排序前取下标原样保留"的修复前行为。
- 基线（未变异）：`CompilerTest.DefaultTcpStaysDescriptionFirstAfterToolSort_F584` PASSED（1/1）。
- 变异后：同一用例 **FAILED**（1 failed），输出见 `mut1-F584-red.txt` 关键行：
  `[  FAILED  ] CompilerTest.DefaultTcpStaysDescriptionFirstAfterToolSort_F584`
- 还原后：PASSED（1/1），跟踪文件零漂移。

## 变异②——F-591（DH 展开轴坐标系）

- 对象：`modeling/src/DhConvert.cpp` expandDhChain 步③；将 `axis = Rx(−α)·ez = (0, sinα, cosα)` 改回修复前的基座系累积帧 z（`zAxisOf(acc)`）。
- 基线（未变异）：`MdlCanonicalBridge.DhDerivedWorldAxesMatchDhConvention_F591` PASSED（1/1）。
- 变异后：同一用例 **FAILED**（1 failed），输出见 `mut2-F591-red.txt` 关键行：
  `[  FAILED  ] MdlCanonicalBridge.DhDerivedWorldAxesMatchDhConvention_F591`
- 还原后：PASSED（1/1），跟踪文件零漂移。

## 剪贴板预探（F-617/F-620）——预探不吞真失败验证

本机环境实况：剪贴板被桌面应用 MATLAB 占用（Qt 写剪贴板报 COM 0x800401d0 OpenClipboard 失败）＝F-620 场景。

1. **探针生效（原实现）**：
   - `sdurws_ird_ui_gui_test`：78 ran，77 PASSED，1 SKIPPED＝`ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1`，skip 归因行 `clipboard blocked by external owner (F-620)——系统剪贴板写读往返失败（环境面跳过，非代码缺陷）` 在册。
   - `sdurws_ird_modeling_gui_test`：42 ran，41 PASSED，1 SKIPPED＝`ModelingPanelGuiTest.PreviewPane_MultiTypeChain_UI_T59`，同款归因在册。
2. **探针失效（变异③：`clipboardUsable()` 首行短路 `return true`）**：
   - 过滤直跑 `BatchPasteImpactDetail_UI_T08_ACC1`：用例**真实执行并 FAILED**（剪贴板输入通道落空——证明该用例在环境占用下会真失败、不会假绿；探针只在不可用时跳过）。
   - 还原后重建：同一用例恢复 SKIPPED（归因同上）。
3. 预探实现位置仅两处（`modeling/gui_test/ModelingPanelGuiTest.cpp`、`ui/gui_test/ParamTablePanelGuiTest.cpp`），零 testkit 泄漏。

## 黄金重制确定性

`node …/testdata/golden/mdl-dh-equivalence/1.0.0/generate/make_dh_equivalence.mjs` 重生成后 `git status --porcelain`（testdata/ 范围）**空输出＝零漂移**，manifest 四文件 SHA-256 全部 MATCH（inputs/samples.json、expected/expansion.json、expected/roundtrip.json、generate/make_dh_equivalence.mjs）。

## 原始输出留档说明

变异/还原六份原始输出（mut1-F584-red / restore1-F584-green / mut2-F591-red / restore2-F591-green / mut3-probe-disabled / restore3-probe-skip）与两份 GUI 全量输出（gui-ui-probe-skip / gui-mdl-probe-skip）留存于验收者复现树 `acc-p2-repro/acc-runs/`，本文为其关键行汇总；本目录以汇总为主、不批量塞原始 XML。
