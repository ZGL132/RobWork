# KIN-P3 修复前红证记录（红→绿两段执行）

本记录登记 KIN-P3（审查条目 A03/A05/A09）的"修复前能复现失败"证据：产品修复**前**
以旧 API 兼容红用例先行构建运行，四条红用例全部实测复现审查反例；产品修复后同批
用例转为最终回归用例组并全绿。两段执行的命令与输出摘录如下。

## 红段（产品修复前，kin-p3 工作树@实现前）

构建目标：`sdurws_ird_kinematics_test`、`sdurws_ird_kinematics_gui_test`
（产品库为 kin-p2 HEAD 240c35cf 的未修复实现）。

### A03——奇异条件数 +∞ 与无界裕量 +∞ 被显示层拒绝（KinPanelUnits.cpp 旧实现）

```
sdurws_ird_kinematics_test.exe --gtest_filter='*RED_A03*'
[ RUN      ] PluginPanelLinks.RED_A03_SingularConditionNumberDisplayable_KIN_P3
error: Expected: { const auto rows = poseMetricRows(metrics.get(), std::nullopt); } doesn't throw an exception.
  Actual: it throws std::invalid_argument with description "formatKinQuantityText: 非有限 SI 值不可渲染（NaN/±∞）".
[  FAILED  ] PluginPanelLinks.RED_A03_SingularConditionNumberDisplayable_KIN_P3
[ RUN      ] PluginPanelLinks.RED_A03_UnboundedJointMarginDisplayable_KIN_P3
error: ...（同上——+∞ 裕量行投影同样抛 invalid_argument）
[  FAILED  ] PluginPanelLinks.RED_A03_UnboundedJointMarginDisplayable_KIN_P3
[  PASSED  ] 0 tests.  [  FAILED  ] 2 tests
```

与审查报告 §3 A03 反例一致：FK 正确产出奇异条件数 +∞，`poseMetricRows()` 抛
invalid_argument 无法显示。报告文件：`red-model-test.json`。

### A05——混合链解列移动轴误用角度投影（reprojectSolutionColumns 旧实现）

```
sdurws_ird_kinematics_gui_test.exe --gtest_filter='*RED_A05*'
[ RUN      ] KinematicsPanelSession.RED_A05_SolutionColumnPerAxisUnits_KIN_P3
error: Expected: (qText.toStdString().find("100")) != (std::string::npos), actual: npos
5.72958, 5.72958
[  FAILED  ] KinematicsPanelSession.RED_A05_SolutionColumnPerAxisUnits_KIN_P3
```

与审查报告 §3 A05 反例数值一致：移动轴 0.1 m 在 mm/deg 制式下解列显示
5.72958（误用角度换算），期望 100 mm。报告文件：`red-gui-test.json`。

### A09——复位 Home 无目标来源、谎报成功（onResetHomeClicked 旧实现）

```
sdurws_ird_kinematics_gui_test.exe --gtest_filter='*RED_A09*'
[ RUN      ] KinematicsPanelSession.RED_A09_HomeWithoutSourceMustExplicitlyRefuse_KIN_P3
error: Expected: (find("不可用")) != npos, actual: npos
已复位 Home（零修订——仅会话态）
[  FAILED  ] KinematicsPanelSession.RED_A09_HomeWithoutSourceMustExplicitlyRefuse_KIN_P3
```

与审查报告 §3 A09 反例一致：无具名 Home 来源时点击复位，按钮把当前姿态原样
写回并谎报"已复位 Home"。报告文件：`red-gui-test.json`。

## 绿段（产品修复后）

红用例转正为最终回归用例组（PluginPanelTest 6 项＋KinematicsPanelSessionGuiTest
5 项——见 §14.6 v0.25 行④），与全量套件一并执行：

- `sdurws_ird_kinematics_test`：245 项（244 过＋1 既有 GUI 登记 skip）——
  `kinematics-test.log`/`kinematics-test.xml`
- `sdurws_ird_kinematics_gui_test`：12/12——`kinematics-gui-test.log`/`kinematics-gui-test.xml`
- `sdurws_ird_kinematics_contract_test`：57/57——`kinematics-contract-test.log`/`kinematics-contract-test.xml`
- 全仓 46 个 ird 测试可执行 4815 用例零失败——`ird-all-suites.log`

红段临时用例（RED_* 命名）在绿段构建中全部被最终版替换，不入库。
