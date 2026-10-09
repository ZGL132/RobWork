# audit-p2-fixes 验证留痕（2026-10-09）

分支 `audit-p2-fixes`（基于 redesign-main@35f2b3cb）。本批十项修复（F-584/F-589/F-590/F-591/F-592/F-593/F-614/F-615/F-616/F-617+F-620 预探）。

## 门禁
- `ird_gates`（构建即门禁）：全部检查通过、零命中——`gates.txt`。
- `validate-docs`：PASS（20 units, 12 trace entries, 312 task files）——`validate-docs.txt`。

## 集成模式构建（唯一交付口径）
- `build/`（RWS_BUILD_INDUSTRIALROBOT=ON，配置再生后目录级重建 runtime 单元）：
  全部 `sdurws_ird_*` 目标零错误。
- 已知例外（DTB §5.1 明文不入验收面）：框架侧宿主外插件 RWSimulatorPlugin
  在 fresh 树编译失败（eigen 适配/模板问题）——非本批引入、非 industrialrobot 面。

## 受影响套件直跑（ctest 对 ui_test 挂起＝已登记 F-619，一律直跑 exe）
- 集成模式：`suites-integrated.txt`——runtime 268/268、dynamics 104/105、
  dynamics_contract 26/26、modeling 346/346、modeling_contract 17/17、
  requirements 205/205、requirements_contract 16/16、ui 255/255
  （各套件另计入设计性跳过，见文件内 SKIPPED 行——均为既有设计：父子进程/
  GUI harness 环境面/Gui 注册面）。
- GUI 预探生效证据：`suites-gui-probe.txt`——本环境系统剪贴板被 MATLAB
  占用（F-620 现场）：PreviewPane_MultiTypeChain_UI_T59 与
  BatchPasteImpactDetail_UI_T08_ACC1 SKIPPED 且输出
  "clipboard blocked by external owner (F-620)" 归因字样；
  modeling_gui 41 过＋2 跳过、ui_gui 77 过＋2 跳过、零失败。
- 冒烟模式：`suites-smoke.txt`——runtime 177/177、modeling 285/285、
  dynamics 104/105（另 1 设计性跳过）。

## 验收者注意点（重要）
1. **改结构体后必须目录级重建**：F-593 扩展了 ValidationIssue（单元内部
   结构），MSVC 增量构建曾出现跨 TU 布错位的陈旧目标混链（表现为
   DescriptionValidator 组用例偶发 SEH 0xc0000005）。runtime（集成＋冒烟）
   均已目录级删除＋重配重建后稳定全绿。验收复验如遇同征兆，先做目录级
   重建再判定。
2. **新专用警告码 RT-BOUNDS-MAGNITUDE**：建议码面待 diagnostics 收编
   〔PA-1〕（比照 RT-RESOURCE-RECORDED 先例）；runtime.md §10.11 v0.19
   已登记"事件码面、无枚举对应、冻结关系不扩"。
3. **黄金数据集重制两处**：mdl-dh-equivalence（F-590/F-591 两次——
   expected/expansion.json＋generate 脚本＋manifest 完整性哈希同步），
   重跑 `node generate/make_dh_equivalence.mjs` 可复核确定性。
4. **findings F-611 resolution 引用的旧用例名 \_F613 已改名 \_F611**
   （F-615）——历史登记文本不改，新映射锚＝本批 F-615 提交。

## 提交清单（分支头起）
见 `git log redesign-main..audit-p2-fixes --oneline`。
