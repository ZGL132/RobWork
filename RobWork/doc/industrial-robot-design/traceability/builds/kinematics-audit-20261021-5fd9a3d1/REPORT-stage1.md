# WP-15-T20 首批实施报告：尚未全功能通过

基线 `5fd9a3d192ea1f67484bed7aec089ec10719c51a`，分支 `wp15-t20`，本会话串行执行，未使用子代理。需求拆分、方法与预期见 MATRIX.md，逐项当前状态见 MATRIX-RESULTS-stage1.md，开放问题见 ISSUES-stage1.md。本报告仅封闭F01～F03的私有修复，不代替独立验收，不批准主线合入。

## 已修复并真实复现的缺陷

- F01：默认区域盒输入全部合法，点击“运行采样”仍被拒绝。根因是成功路径不将外部ok置true。修复后真实IK/采样生成产物，换算后的非有限值仍拒绝。
- F02：填写m/rad目标再切换mm/deg，标签改变而文本未换算，实际求解目标改变。目标和区域几何现通过core单位设施重投影，反复往返保留double精度；非法原文保留，溢出时全部原文和旧单位原子保留。独立解析FK验证求解所得仍为原物理目标。当前仍缺用户可达的显示单位选择入口（F05），不宣称KIN-12整项完成。
- F03：结果页将面板最小高度撑到1205逻辑像素，小窗口无法到达底部操作。四页现可滚动；实际960×720窗口和底部按钮视口断言通过。正式宿主布局、高DPI仍未完成。

## 实际执行与证据范围

| 口径 | 运行结果 | 原始证据（stage1/raw/） |
| --- | --- | --- |
| fresh集成配置/四目标构建 | PASS，含正式宿主目标；不是全仓库所有目标构建声明 | configure-kinematics.log、build-kinematics.log、build-kinematics-green1.log |
| 集成基线模型 | 288跑：287通过、1跳过 | kinematics-baseline.xml及同名report、test日志 |
| 集成基线契约/原GUI | 57通过；20通过 | kinematics_contract-baseline、kinematics_gui-baseline |
| 新增6项修复前 | 6失败，实际红回归 | kinematics_gui-expanded-red.xml及report、test日志 |
| 新增6项修复后 | 6通过 | kinematics_gui-green1.xml及report、test日志 |
| 集成完整GUI | 26通过 | kinematics_gui-green-full.xml及report、test日志 |
| 集成模型/契约回归 | 287通过＋1跳过；57通过 | kinematics-green1、kinematics_contract-green1 |
| fresh独立配置/构建 | PASS，vcpkg与Qt参数均显式传入 | standalone-configure-kinematics-green1.log、standalone-build-kinematics-green1.log |
| 独立模型/契约/GUI | 279通过＋1跳过；57通过；26通过 | standalone_kinematics-green1、standalone_kinematics_contract-green1、standalone_kinematics_gui-green-full |
| ird_gates | PASS，零命中 | gates-kinematics-green1.log |
| WP-15-T20契约verify（收尾重跑） | PASS，11条机器命令、0条人工条目；包含双模式与六套测试及文档门禁 | verify-task-stage1.log，时间戳命名的XML/report/build日志 |
| 正式宿主真实网格与三维操作 | NOT RUN | 无实证，不能由面板截图外推 |
| 标准负载P95与全70项最终复验 | NOT RUN | 后续批次，不以普通单测耗时替代 |

模型套件跳过项均为`PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1`（GUI注册占位）；真实GUI单独运行不改写其跳过计数。集成比独立多8项依赖框架接线的测试，分别保留原始数量，不混称相同覆盖。

原始工具日志保留MSBuild/CMake输出的行尾空格，不为消除格式提示改写执行证据。源码、手写文档与可重跑脚本另做diff空白检查。

窗口在VS x64开发环境中，以`QT_QPA_PLATFORM=windows`、绝对路径、同桌面单个GUI进程运行，未用offscreen。新用例使用真实FK/IK/采样、Qt鼠标按下/释放事件。两连杆金标仅证明该模型与面板；程序化填值/切单位不是人手或OS级键盘鼠标验收，不宣称正式宿主全入口可达或真实mesh正确。

截图保留在stage1/screenshots/：green1为集成首轮6项，expanded-red为修复前，green-full为独立完整GUI最后一轮。早期runner按Label命名截图目录，独立轮green-full覆盖了同标签集成轮的3张截图；集成完整GUI的XML/报告/日志未覆盖，集成green1截图仍保留。不将覆盖后的PNG声称为集成完整GUI原图。留存的run-stage1.ps1已将截图目录改为Name＋Label，并以时间戳默认标签避免重跑覆盖；run-recorded.ps1保留当时原脚本。

网络直连核对曾失败，2026-10-21本批收尾再次`ls-remote`成功，远端redesign-main仍为本次基线。实际提交/推送状态以git记录与后续同步日志为准，不以本报告文字替代。

## 后续修复顺序

先补F04策略保存、F06完整矩阵/候选q、F10任务点双击的真实失败回归，再补设备/TCP/单位用户入口；随后后台取消、指标浏览、外包络/位姿计划、正式归档和完整宿主三维。涉及公共面或协作边时先增量设计与契约登记。F13/F14是尚未执行的证据项，不预先判断产品正确或渲染错误。
