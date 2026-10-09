# audit-p0~p1d 合入后全量复验证据（2026-10-09）

合并序列：redesign-main 快进并 origin（d15d4dc5）→ 按 SHA 合 evidence cc30cd15（530f861d）
→ 按冻结 SHA 合链尾 5f8c3904d（438986b2，findings.json 经并集脚本解算 38 条）
→ 推送前再并 origin 增量 d23f1ded（WP-20-T10，与链产品面零重叠，e6c6e3db）。

## 复验矩阵（合并树 e6c6e3db）

| 项 | 结论 | 证据 |
| --- | --- | --- |
| validate-docs | PASS | validate-docs-merge.log |
| ird_gates | PASS（构建目标成功，红线零命中） | gates.log、gate-all-verify3.log |
| 集成构建 | sdurws_ird_* 全目标零错误（仅框架侧宿主外插件 bt_plugin/RWSimulatorPlugin 失败——DTB §5.1 明文排除面） | rebuild-verify.log |
| 集成测试 | 38 套件直跑 36 绿；ui_test 255/255（ctest 挂起 workaround＝直跑，见 F-619） | sweep-summary.txt、ui-test-direct-255.log |
| 冒烟构建 | 零错误 | rebuild-verify.log |
| 冒烟测试 | 36/39 绿（含 ui_test 240/240、requirements_gui 45/45）；lint 19 数据集 0 违规 | smoke-run-summary.txt、sdurws_ird_testdata_lint-rerun.log |
| 例外归因 | modeling_gui／ui_gui 各 1 用例失败＝剪贴板传输被占，功能断言内容正确（见 F-620） | modeling-gui-case-individual.log、ui-gui-case-individual.log |

## 剪贴板归因铁证（F-620）

- 系统级：`echo | clip` 返回「错误: 拒绝访问」；PowerShell GetClipboardOwner＝**MATLAB pid 26828**。
- 用例级：PreviewPane_MultiTypeChain_UI_T59 断言两侧中 preview->toPlainText() 内容正确、
  clipboard()->text() 为空；BatchPasteImpactDetail_UI_T08_ACC1 以 clipboard()->setText()
  为输入通道（ParamTablePanelGuiTest.cpp:313）落空致 "100"≠"150"。
- 对照组：requirements_gui_test 45/45 全绿（不用剪贴板的 GUI 套件），GUI 基础面健康。
- 与批次 D 三轮门禁、验收 attempt1 批跑失败的「GUI 剪贴板竞争」同源（F-620 登记）。

## ctest 挂起归因（F-619）

gate-all 于本 fresh 树两次独立复现：ctest 对 sdurws_ird_ui_test 套件启动后零 CPU、
无子进程、无输出；同一 exe 直跑 1.6~2.2 秒 255/255 全绿（工作目录差异已排除）。
本次复验以「同命令直跑 exe」口径补齐该套件证据（ui-test-direct-255.log）。
