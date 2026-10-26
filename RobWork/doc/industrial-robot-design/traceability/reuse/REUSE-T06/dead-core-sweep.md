# WP-20 / REUSE-T06 删死核心扫尾与测试回流检查记录（2026-10-27）

现场：`redesign-main` 主工作树，基线 HEAD `56bc28140`（本包开工实测 `git status --short` 干净）。以下全部为本包现场实测命令与结果；产品源码根＝`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`。

## 1. 删死核心扫尾（§12.8 第 2 项）

### 1.1 RneaChain 族（T04-A / REUSE-T52 旧核）——已删除且无回流

命令：`rg -n "RneaChain|buildRneaChain|rneaTorques" -g '*.hpp' -g '*.cpp'`（产品源码根）
结果：**零命中**（rg exit=1）。T52 收口批（`e21d61714`）删除生效，无回流路径。

同批核实：`rg -n "computeGeometry" -g '*.hpp' -g '*.cpp'` → **零命中**（exit=1）。

### 1.2 Ik.cpp 自写 DLS——完整位姿模式已删；仅位置 3 维专用保留（GAP-IK-01 blocked 如实保留）

命令：`rg -n "dampedLeastSquares|DLS" -g '*.cpp' -g '*.hpp' -g '!**/test/**'`
结果：仅剩 `kinematics/src/Ik.cpp:422` `dampedLeastSquaresStep`（3 维仅位置专用）及约束模式族。

判定依据（GOV-T27 分审记录 §8，`traceability/reuse/GOV-T27/README.md`，独验 PASS `GOV-T27-20261026-r1`）：
旧核心保留清单 5 函数（`dampedLeastSquaresStep`/`constrainedPoseError`/`constrainedDampedLeastSquaresStep`/`nearestToolRollTarget`/`zyxEulerAngles`）＝仅位置与约束模式唯一迭代核，上游无序列设备对应入口（ParallelIKSolver 为 EAA 行且要求 ParallelDevice，产品编译产物 SerialDevice 不可达）；删除前置"所有消费者迁移后"不满足。**非漏网，待批保留（GAP-IK-01 三选一待所有者裁决）**。

### 1.3 RK4 四阶段——保留（WP-09 blocked 如实保留，非漏网）

命令：`rg -n -i "rk4|rungeKutta" -g '*.hpp' -g '*.cpp' -g '!**/test/**'`
结果：命中 `dynamics/src/ForwardDynamics.cpp`（:612-740 RK4 子步循环）与 `ForwardDynamics.hpp`（`kForwardIntegratorToken="rk4-fixed"`，P-DYN-7 落位锁定）。
判定：FD 消费缺口＝GAP-FD-BUILD-01（REUSE-T31 探测 done、提案 Draft 未批准）；`ForwardDynamicsBoundaryContractTest.cpp:114` 明示"rwsim 引擎接入须先走单元卡增量修订"。WP-09 blocked 登记在案（migration §13.2/§13.3）。**待批保留**。

### 1.4 特性开关/旁路副本/兜底路径

- `rg -c -i "feature.?flag|legacy.?fallback|fallback.?path" -g '*.cpp' -g '*.hpp' -g '!**/test/**'` → **零命中**（exit=1）。
- `rg -n "#if 0" -g '*.cpp' -g '*.hpp'` → **零命中**（exit=1）。
- `rg -n "TODO|FIXME|XXX" -g '*.cpp' -g '*.hpp' -g '!**/test/**'` → 命中行均为 `\uXXXX` UTF-8 转义说明文本（reporting/Consistency.cpp、project/Codec.*），**零真实 TODO/FIXME 残留**（AGENTS §2 禁止占位 TODO 口径合规）。

### 1.5 黄金生成器内核（§12.8"黄金生成器内核"检查项）

`make_dyn_two_link_golden.mjs` 位于 `testdata/golden/dyn-two-link-analytic/1.0.0/generate/`（测试数据面，非产品库）；独立参考实现＝平面二连杆拉格朗日封闭式 M/C/G 标量直算，与产品 RNEA 递归牛顿—欧拉不同推导路线、零共享代码（WP-17-T10 落位登记，units/dynamics.md:102）。**属 §12.8 明文保留的"真正独立的测试 oracle"**——对应 functions 矩阵 foundation-governance 123 行 pending（业务专属自持类）。

## 2. 测试/备用路径回流生产检查

### 2.1 core::ReferenceEventBus 生产装配零引用（T41 收口面复核）

命令：`rg -n "ReferenceEventBus" -g '*.cpp' -g '*.hpp' -g '!**/test/**'`
结果：非 test 命中仅——core 类本体（`core/src/Events.cpp:129-171`、`core/include/sdurws/ird/core/Events.hpp:166`）与注释（`execution/EventBus.hpp:145`、`ui/plugin/UiPlugin.hpp:625`、`UiPlugin.cpp:106/760`，注明"非生产组件，2026-10-25 起本插件不再装配"）。**生产装配代码引用为零，无回流**；类本体保留＝测试语义钉子/契约钉（对应 CORE-EVENT 16 行 pending 保留面）。

### 2.2 test-fixture grant 封闭性（L1 例外合规）

`reuse/permissions.json` 全 22 grant 逐一核对：origin=test-fixture 恰 2 个（`policy-collision-test-fixture`→policy/test/CollisionSessionTest.cpp；`REUSE-T52-RneaTest.cpp`→dynamics/test/RneaTest.cpp），**均在 /test/ 路径**；其余 20 个 grant 均绑定任务凭证（T13/T17/T19/T30/T32/T41/T51/T52）且 PROD/TEST 归属与路径一致。非 test 路径零 test-fixture 例外。

### 2.3 测试替身不进生产目标

`RigView`（functions 行 kinematics.KIN-FK pending）`rg -ln` 全树仅 `kinematics/test/CollisionPortTest.cpp`——测试目标专用替身，生产零引用。

### 2.4 宿主例外封闭清单

`permissions.json hostExceptions`＝HOST-RENDER/HOST-PRESENT/HOST-WORKBENCH 等 5 项封闭清单（T41 落地批扩至 5，所有者追认），与 ARCH §3.6.2 一致；ui 19 行宿主呈现 pending＝墙内待批面，无未登记例外（gate 侧 IRD-GATE-AD 机器校验持续在案，UI-T86 gate-ad-blocked.log 为其拦截实证）。

## 3. 结论

- 已删旧核（RneaChain 族/computeGeometry/完整位姿 DLS/正规方程链）**无回流**；
- 在档保留面（仅位置 3 维 DLS＋约束族＝GAP-IK-01；RK4＋质量阵消元＝GAP-FD-BUILD-01；自持 SHA＝GAP-DIGEST-01）**全部有 blocked 登记与保留理由，不属于未清理债务**；
- 特性开关/旁路/兜底/**#if 0**/真实 TODO 零残留；
- 测试/备用路径无生产回流；test-fixture 例外封闭合规。
