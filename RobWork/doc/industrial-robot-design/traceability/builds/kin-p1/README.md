# kin-p1 留痕说明（KIN-P1 坐标与输入修复批，2026-10-16）

分支 `kin-p1`（基点 `redesign-main@c1f901f4`）；修复审查报告
`audit/kin-current/运动学模块重新审查-20261016.md` §3 A01/A07。

## 留痕构成

| 文件 | 内容 |
| --- | --- |
| `build-integrated.log` | 集成模式构建（仓库根 build/，`RWS_BUILD_INDUSTRIALROBOT=ON`）——受影响目标：kinematics / kinematics_plugin / kinematics_test / kinematics_gui_test / kinematics_contract_test / ui / ui_plugin / studio / ui_test / ui_contract_test / ui_gui_test，零错误 |
| `smoke-configure.log` / `smoke-build.log` | 独立冒烟模式（`build/kin-p1-smoke` 新建树，vcpkg toolchain＋Qt 6.11.1 前缀）——kinematics 三测试目标构建零错误 |
| `kinematics-test.log/.xml` | 集成树 `sdurws_ird_kinematics_test`：232 用例＝231 过＋1 既有 GUI 呈现登记跳过（含 KIN-P1 新用例 DisplayToSi_ConsistentSiAcrossUnitSystems_KIN_P1） |
| `kinematics-gui-test.log/.xml` | 集成树新目标 `sdurws_ird_kinematics_gui_test`：4/4（KIN-P1 A07 widget 回归） |
| `kinematics-contract-test.log/.xml` | 集成树契约：57/57（含随本批同步的 BuildGraphContractTest 目标清单两断言） |
| `ui-test.log/.xml` | 集成树 `sdurws_ird_ui_test`：294/294（基线 289＋KIN-P1 A01 五用例） |
| `ui-contract-test.log/.xml` | 集成树 ui 契约：42/42 |
| `ui-gui-test.log/.xml` | 集成树 ui GUI：84/84 |
| `kinematics-test-smoke.log/.xml`、`kinematics-gui-test-smoke.log/.xml`、`kinematics-contract-test-smoke.log` | 冒烟树同三目标：225 过＋1 跳过、4/4、57/57 |
| `ird-gates.log` | `ird_gates` 引擎直跑（自测 12 项按预期）——R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中，退出码 0 |
| `ird-test-report.json` | testkit RecordListener 聚合报告（末次运行＝ui_gui_test 套件；逐套件明细以各 gtest XML 为准） |

## 修复前复现记录（本批新增用例在未修复代码上的实测，非留痕文件）

- A07（gui_test）：4/4 失败——mm 制式输入 `0.1` 提交 **100 m**（审查 A07 原始反例）；
  标签恒为"目标 X（m）"；`inf` 输入直通求解器。
- A01（ui_test ProjectPlan_*）：5/5 失败——平移反例实测 **1.65**（期望 −0.35）；
  倒装/侧装/轴交换三场景全部被旋转检测误拒（"区域参考系相对基座存在旋转——
  R1 轴对齐盒约束"）。

## 文档同步

- `units/kinematics.md` §14.6 v0.23＋文档版本头＋§3.1 目标表＋§9.8 L-K2 补行。
- `units/ui.md` §16.7 v1.103＋文档版本头（A01 根因与着色回投连带修正登记——
  UI-T65 行"worldToBase 逆变换"方向语义纠正）。
