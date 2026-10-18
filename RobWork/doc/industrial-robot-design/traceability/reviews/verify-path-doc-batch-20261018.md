# verify 路径与 ACC 留痕口径修正（2026-10-18）

| 项 | 内容 |
| --- | --- |
| F-251 | RPT-T11 ctest 根路径→reporting 叶子；UI-T14 两行→ui 叶子；同型 WP-23-T09 拆六个叶子目录 ctest 行 |
| F-869 | WP-23-T04 verify ③ 集成布局路径→冒烟树叶子目录两条（kinematics/ui） |
| F-201 | ACC §4.10 删除停更 phase-one-validation.log／不存在的 resultNote，改为 builds/<branch>/ 三件套 |
| F-257/F-527 | 历史证据悬空指引／计数口径说明——按「历史不可改＋实质证据在案」「说明性非缺陷」关闭 |

## 证据
- `reporting-ctest.log`——reporting 叶子路径 ctest 2/2 passed（1.52s）
- `ui-ctest-ird.log`——ui 叶子路径 ctest 4/4 passed（17.04s）
- `validate-task.log`——RPT-T11/UI-T14/WP-23-T09/WP-23-T04 全 PASS

> 根因：框架未在顶层 enable_testing，build/ 根无 CTestTestfile.cmake——`--test-dir build` 恒 exit 0 且 No tests found（空转）。
