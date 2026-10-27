# REUSE-T52 原始记录（WP-07 逆动力学 RNE 迁移——runtime 工厂薄适配）

任务：[tasks/REUSE-T52.json](../../../tasks/REUSE-T52.json)（migration §13.3 WP-07 / T04 RNE 子任务）。
基线：起点 `bf682893ff174d759a93e905889458d2f4e80e21`（GOV-T27 收口提交）；上游文件零改动。

## 1. 交付概要

- **换核**：`dynamics/src/InverseDynamics.cpp` 自写 RNEA 旧核全删（Vec3/Mat3/RneaChain/buildRneaChain/SampleGeometry/computeGeometry/rneaTorques——净 −284 行），改消费上游 `rwsim::util::RecursiveNewtonEuler`（H1 直接 API，target=sdurwsim）。请求/结局 DTO 与 evaluate 语义零变化（kInverseDynContractVersion=1）；kRneaAlgorithmVersion 1→2（CON-04）。
- **构造面上收（2026-10-26 所有者裁决，escalate 答复）**：首版 dynamics 侧一次性物化被 IRD-GATE-AD 模型所有权机器门禁拒绝（construct-model/mutate-model 仅 runtime-owner 可授权）→ 裁决＝runtime 公共工厂 `buildRneConsumptionDevice(CanonicalModel)`（新公共头 RneConsumptionDevice.hpp＋src 实现＋runtime CMake 集成/冒烟双模式编译）——R_align/关节帧换算知识回归模型 owner 本位，GAP-RNE-FRAME 单点；dynamics 经 dynamics->runtime 登记边消费（请求 DTO 零变化、既有测试零重做）；ARCHITECTURE §3.6.1 消费边界落点行＋units/runtime.md §3.1/§15.4 v0.27 已登记。
- **薄适配语义（A2 裁决）**：载体帧树与 S6 同源（关节帧 tAligned=origin·R_align＋zeroOffset 烘焙＋连杆帧 R_alignᵀ）；BodyInfo 关节帧换算（R_alignᵀ·com、R_alignᵀ·I·R_align）；setGravity 显式传递；工具/负载合成体并入法兰 BodyInfo（setPayload 通道因上游工具条目力矩臂符号缺陷不消费）；按关节类型投影（不盲用 solveMotorTorques——其对移动关节同投影 torque 的上游缺陷）；§8.2 canonical 断言/S7 产品映射/modeling DWC 导出零触碰。
- **冒烟供给**：runtime/dynamics 冒烟分支按 REUSE-T51 同款补框架链接（find_package(RobWork)＋同名 ALIAS；sdurwsim 直含 targets/sdurwsim.cmake 导入目标——RobWorkSimConfig 非安装树无条件 REQUIRED RobWorkStudio 且其 sdurws 验证链冒烟解析不了，探针 smoke-configure.log 在案）；sdurwsim 非集成判定符（全库 TARGET sdurwsim 零命中）；门禁代码/词表零改动。

## 2. 验证证据（PASS/FAIL/SKIP/NOT RUN 分列；构造面变化后全量真实重跑）

| 运行 | 结果 | 证据 |
| --- | --- | --- |
| dynamics 全量（集成） | 119 ran＝119 passed＋0 failed＋0 skipped | full-suite.xml/.log/-report.json |
| dynamics 契约 | 30 ran＝30 passed | contract.xml/.log/-report.json |
| runtime 消费者回归 | 312 ran＝311 passed＋1 skipped（ContractSuite.ChildProcessWorker_RT_ID_1 既有环境 skip，与 REUSE-T30 基线同名单）＋0 failed | runtime-consumer.xml/.log/-report.json |
| 独立冒烟（configure＋build＋全量运行；工厂 TU 双模式） | 119 ran＝119 passed＋0 failed | smoke/smoke-full-suite.*＋smoke-configure.log＋smoke-build.log |
| oracle（禁自比） | 8 ran＝8 passed（弯曲构型叉积平衡＋准静态能量平衡＋黄金数据集拉格朗日独立参考） | oracle.xml/.log/-report.json |
| 上游直接对照 | 1 ran＝1 passed（内联同构载体直连 solve 与产品输出 EXPECT_EQ 逐位一致） | upstream-comparison.xml/.log/-report.json |
| 断开实验①（runtime 工厂换算行去除） | exit=1、2 项真实失败（y 轴静态＋弯曲平衡）＝GAP-RNE-FRAME 复现 | disconnected.xml/.log＋disconnected-mutated-RneConsumptionDevice.cpp.txt |
| 断开实验②（dynamics 零位偏置提取置 0） | exit=1、1 项真实失败（权威角偏置用例）＝T13 语义链复现 | disconnected-devbuild.xml/.log＋disconnected-mutated-InverseDynamics-zeroffset.cpp.txt |
| 断开实验③（dynamics setGravity 注释） | exit=1、1 项真实失败（静态重力用例）＝T30 次要发现①复现 | disconnected-solve.xml/.log＋disconnected-mutated-InverseDynamics-setgravity.cpp.txt |
| 恢复复验 | 三路变异恢复后 dynamics 全量 119 passed | full-suite（末次运行） |
| ird_gates（直跑＋构建目标双形态） | R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中 | gates.log |
| 迁移前基线（对照锚） | bf682893f：dynamics 116 passed／contract 30／runtime 311+1 skip | baseline/ 目录 |

NOT RUN：无（GUI 手动点验不适用——本包零 Qt 界面变更；dynamics_app/gui_test 非本包验证面）。

## 3. 复现命令

```powershell
# 集成构建（受影响目标）
cmake --build build --config Release --target sdurws_ird_runtime sdurws_ird_dynamics sdurws_ird_dynamics_test sdurws_ird_dynamics_contract_test sdurws_ird_runtime_test ird_gates
# 证据运行
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/run-dynamics.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label full-suite
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/run-contract.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label contract
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/run-runtime.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label runtime-consumer
# 独立冒烟（三参数口径）
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build/wp07-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64 -DRobWork_DIR="$PWD/RobWork/RobWork/cmake" -DVCPKG_OVERRIDE_FIND_PACKAGE_NAME=vcpkg_find_package
cmake --build build/wp07-smoke --config Release --target sdurws_ird_runtime sdurws_ird_dynamics sdurws_ird_dynamics_test
pwsh -NoProfile -File RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T52/run-smoke.ps1 -RepoRoot D:/10_Source_Repos/21_robot/RobWork -Label smoke-full-suite
# 治理
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range bf682893f..HEAD
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T52.json -Base bf682893ff174d759a93e905889458d2f4e80e21 -Head HEAD
```

## 4. 矩阵与凭证生成

gen-matrix.ps1（functions/dynamics.json 10+7 行铺面＋functions/runtime.json 3 行追加＋changes/REUSE-T52.json 七文件凭证）、gen-production.ps1（三 adapter 行生产证据四段——断开各绑自身文件内变异）、gen-permissions3/4.ps1（四笔授权：runtime-owner 工厂 src＋契约头、dynamics task-private、RneaTest test-fixture）、gen-catalog-permissions.ps1（catalog DYN-RNE verified）——治理 Digest 口径与 validate-reuse.ps1 同一规范化；绑定值权威＝门禁本体实测。

## 5. 独立验收

由所有者另开会话或人工执行（GOV-T23/T24 先例同型），实施者不自签；验收须核工厂未越权扩面（runtime 改动仅限工厂三文件）、换算语义与断开实验证据；验收路径预留 traceability/acceptance/REUSE-T52-20261026-r1.json。
