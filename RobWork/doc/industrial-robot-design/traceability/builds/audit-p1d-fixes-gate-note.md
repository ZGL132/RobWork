# audit-p1d-fixes 批次（P1·ui 呈现面）验证证据（F-609~F-613，2026-10-09）

## 修复内容（5 项 fixed，提交于隔离工作树分支 audit-p1d-fixes）

| 编号 | 文件 | 摘要 | 提交 |
| --- | --- | --- | --- |
| F-609 | WorkbenchContent.cpp | 首页最近项目点击判别条件去掉取反——可用项恢复触发 project.open | 3dc382d3 |
| F-610 | TaskPresentation.cpp | refresh() 进度基线并入改用已展示行旧值/旧 attempt 调 mergeProgressDecision——回退不再倒退、新 attempt 首帧不再冻结 | 3dc382d3 |
| F-611 | StageNavigationModel.cpp | 观察者登记改 shared_ptr 堆持有——deque 中段摘除后记录地址稳定（退订幂等/通知不再悬垂反查） | 3dc382d3 |
| F-612 | IndustrialProjectTree.cpp | itemSelectionChanged 空选中补 clearSelection 写回＋refresh clear() 加 suppress 守卫 | 3dc382d3 |
| F-613 | GlobalShortcutRegistry.cpp | m_shortcutParent/m_shortcutByCommand 改 QPointer——宿主窗口销毁后 detach/rebind 不触碰已析构 QShortcut | 3dc382d3 |

## 验证方式与结果

1. **新增回归测试 1 个**：F-613 MidDequeUnsubscribeKeepsSiblingRecords-
   Stable_F613——三观察者中段退订→混合摘除→计数精确（修复前中段
   erase 使后续句柄地址反查错位）。sdurws_ird_ui_test 255/255 通过。
2. **受影响测试目标全量**：ui_test 255/255（含 F-613 新增）；modeling
   344/344、kinematics 182/182、requirements 203/203、project 254/254、
   diagnostics 115/115、testkit 101/101、io 122/122（以上各批次累计零
   回归）。
3. **全量门禁**：`audit-p1d-fixes-gate-run3.log`——**85/85 通过
   （EXIT=0）**，双模式构建零错误、ird_gates 零命中、40 个注册测试目
   标双模式全绿（**含**先前受剪贴板竞争影响的 PreviewPane_MultiType-
   Chain_UI_T59 与 BatchPasteImpactDetail_UI_T08_ACC1——并行会话 GUI
   冒烟结束后剪贴板释放，本批门禁窗口期全绿）。
   - 首轮 `audit-p1d-fixes-gate.log`（52/85）：33 项冒烟构建 C1083
     （并行会话共用 %TEMP%\ird-gate-smoke 目录——文件锁竞争）；
   - 二轮 `audit-p1d-fixes-gate-run2.log`（77/85）：C1083 减至 6 项
     （同因，竞争面缩小）＋2 项 GUI 剪贴板竞争；
   - 三轮改用 **-SmokeDir 隔离冒烟目录**（门禁脚本既有参数）——85/85。
   - 三轮对照证明：C1083＝目录共用竞争（-SmokeDir 消除）、GUI 失败＝
     剪贴板竞争（并行会话结束后消除），均非代码缺陷。

## 结论

批次 D 五项发现闭环（5 修）。分支 audit-p1d-fixes（基于 audit-p1c-
fixes@4d6452a2）待验收合入。累计：F-570~F-613 共 44 项登记（31 fixed、
6 open 待裁决、1 closed 改判、3 项并入批次 B/C 相邻条目）。
