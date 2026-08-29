# WP-06-T04 任意轴运行时适配

- **Task ID / 需求 ID / ADR / 阶段：**WP-06-T04；MDL-06、MDL-09、MDL-10、MDL-14、NFR-COR-05；无新 ADR（公式权威为 canonical-kinematics §7 冻结构造）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；契约 `architecture/canonical-kinematics.md` §7/§9（IRD-D2-20260829）；方案 `module-design/runtime-model.md` v0.3 §5
- **前置任务及必需工件：**WP-06-T01（canonical Origin/Axis 原始值保留）、WP-06-T03（RobWorkModelAdapter 与双编译工件）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/` 下 `src/RobWorkModelAdapter.cpp`、`src/AxisCompensation.cpp`（新增，R_c 夹具选择与帧插入）、`test/AxisAdapterTest.cpp`、`testdata/runtime/axes/`
- **禁止修改的文件和公共接口：**canonical Origin/Axis 原始值、关节物理类型（Revolute/Continuous/Prismatic/Fixed）、WP-07 碰撞策略、GUI、公共端口头；§7 的 R_c 公式与分支阈值不得在实现中改写
- **修改前接口：**基线仅支持 RobWork 局部 Z 轴关节，非 Z/非单位轴在 `WorkCellConverter.cpp` 中被丢弃或静默重投影，无补偿帧概念
- **修改后接口：**适配链 `T_P_Jm(q) = OriginPose · R_c · Motion_Z(q) · R_c⁻¹`（前置补偿固定帧 → RobWork Z 关节 → 后置补偿固定帧）；补偿帧 `objectKind=CompensationFrame`、拥有稳定 objectId、不入 `q`；模块私有，不新增公共端口
- **实施步骤：**按 §7 计算 `c = ẑ · â` 并选择三夹具 → 插入前置/后置补偿帧 → 视觉/碰撞几何、COM、惯量绑定规范坐标系（或等价整体重表达）→ 编译 → 与规范链 FK 对照 → 反向查询仍以规范链为准
- **RED 测试：**`test/AxisAdapterTest.cpp`（注册于 `sdurws_ird_runtime_test`）：三夹具断言——平行（`1−c ≤ 1e-12` → `R_c = I`）、反平行（`1+c ≤ 1e-12` → `R_c = diag(1,−1,−1)`）、一般（Rodrigues `I+[v]×+[v]×²·(1−c)/s²`）；零轴/非有限轴报 `IRD-RUNTIME-AXIS-INVALID` 且无部分工件——先确认测试在无实现时失败
- **最小实现：**夹具选择＋两枚补偿帧插入＋资产坐标系绑定；连续/棱柱关节位移语义与 limits 保持；补偿只存在运行时适配层，resolver 仍返回原 objectId
- **正常/边界/失败测试：**正常：任意有限非零轴下 Zero、Home、正负有限边界与固定种子 100 姿态的末端位姿/世界轴线满足 §15.3"URDF 轴对齐适配"容差（世界轴线 `1e-9 rad`、位置 `1e-9 m`、惯量 `1e-10 kg·m²`），TCP 位置 `1e-9 m`/姿态 `1e-9 rad`。边界：轴范数略偏 1（入模前规范化记录）、Prismatic 沿非 Z 轴平移、Continuous 无界角。失败：零轴、非有限轴、补偿构造数值异常 → Input/Engineering 诊断且不产生部分工件
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；canonical `OriginPose/Axis` 字段无改动（grep 确认）；`CompensationFrame` 不进入 qIndex 分配；无碰撞策略引用
- **证据工件：**`runtime/evidence/WP-06/T04/`：三夹具补偿矩阵、位姿/轴线误差报告、几何与惯量对照、诊断清单、RobWork 版本、命令日志与评审签名
- **提交格式：**`WP-06-T04: implement arbitrary-axis runtime adapter`
- **停止与升级条件：**补偿会改写领域原始物理值、容差未在需求 §15.3 定义或 RobWork 无法表达某夹具时暂停并升级至架构负责人
