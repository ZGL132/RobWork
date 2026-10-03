# UI-T41 建模插件升级批次 A~C 实施留痕

- 任务：UI-T41（批次 A 打通断链／批次 B 编辑与反馈体验／批次 C 旧版亮点迁移）
- 分支：`ui-t41`（基点 redesign-main@295d60e0）
- 实现提交：`af14e621`（批次A）→ `5ae6b232`（批次A 收口修正）→ `f2b77e1c`（批次B）→ `1a239bd8`（批次C）→ 本留痕提交
- 任务契约：`tasks/foundation/UI-T41.json`（validate-task PASS）

## 证据清单

| 文件 | 内容 | 产出方式 |
| --- | --- | --- |
| `build-integrated.log` | 集成模式 Release：11 个受影响目标（modeling/ui 两域 plugin+test+contract+gui+studio）重链零错误（grep "error C\|LNK" 零命中，尾行 build exit=0） | `cmake --build build --config Release --target …` |
| `tests/ird-modeling.xml` ＋ `ird-modeling-console.log` | sdurws_ird_modeling_test 290/290（含批次C 新增 ModelingCommandFlows 六用例＋PluginPanelTest 两投影用例） | `--gtest_output=xml:` |
| `tests/ird-modeling-contract.xml` | 契约测试 17/17 | 同上 |
| `tests/ird-modeling-gui.xml` | 面板 gui 9/9（批次B 四新用例在内） | 同上 |
| `tests/ird-ui.xml` | ui 单元 239/239 | 同上 |
| `tests/ird-ui-contract.xml` | ui 契约 32/32 | 同上 |
| `tests/ird-ui-gui.xml` | ui gui 67/67 | 同上 |
| `tests/ird-test-report.json` | testkit 汇总报告（运行后自 bin/Release 拷贝） | 测试执行自动产出 |
| `smoke/console-auto.log` | 真宿主冒烟 auto 通道（净室夹具）：exit=0；`domain assembly: plugin=modeling ok=1 panels=1 commands=10`（F-421 真机装配证据）＋`[ird-ui-smoke] step7 modeling-assembly=ok commands=10`（批次A⑤ 真链路断言） | `IRD_UI_PLUGIN_SMOKE=auto`＋sdurws_ird_studio.exe（驱动脚本同 ui-t39 形态） |

## 门禁口径注（验收者请先读）

1. **ird_gates（F-463，open，owner＝治理会话）**：gate-all 通道的 ird_gates cmake 目标在基点 295d60e0 即 98 处命中恒 FATAL（归因实测：checkout 本批改动路径后复跑仍 98）。本批曾新增第 99 处（modeling_test→sdurws_ird_ui，批次A 首提交）已随 5ae6b232 回退——**本批净增命中为零**。验收 4.4/回归门禁按"净增零"口径执行，勿以该先在性红判本批 fail。
2. **ui_gui_test 负载敏感（F-464，open）**：HostIntegrationGuiTest.AssemblyFailureGroupPlaceholder 在 gate 并发负载下偶发翻红（1ms 级断言）；隔离 5/5、静置全量 3/3 全绿。复现失败先查系统负载并单独重跑该过滤。
3. 独立冒烟模式：gate-all -QtPrefix 通道的冒烟构建/测试行全 PASS（含 sdurws_ird_modeling_test——批次A 收口修正后回归）。

## 已登记的偏差与边界（详 ui.md UI-T41 行／modeling.md v0.24~v0.26）

- 导入文件读取用标准库 fstream（用户自选路径，io 受管路径治理面不适用）——io 端口接线随导入向导任务。
- C4（结构变更参数保留）/C5（几何资源选择器）登记不实施——依赖结构编辑面与引用字段编辑流。
- 批次 D（覆盖确认清单增强/发布即所见/3D 联动准备/WorkCell 反向导入对齐）未实施——D3 受 UI-T33 前置、D4 归 WP-13-T17。
- GUI 人工点验截图：本批自动化证据以宿主冒烟 auto 通道＋gui 用例承载；人工点验截图归验收会话按 §4 复现时补录。
