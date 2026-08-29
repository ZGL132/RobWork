# WP-03-T01 工程数学和类型安全

- **Task ID / 需求 ID / ADR / 阶段：**WP-03-T01；需求 ARC-01、ARC-03、NFR-COR-03、NFR-MNT-04；ADR-001、ADR-004；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、module-design/core-domain.md v0.3。
- **前置任务及必需工件：**WP-01-T02（CMake 骨架，`sdurws_ird_core`/`sdurws_ird_core_test` 目标已登记）；WP-01-T03（`run-tests.ps1` 测试入口）；WP-02-T02（数值断言库与 `AlgorithmTolerance` 容差夹具）。
- **允许创建/修改/删除的文件**（模块根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/`）：创建 `include/sdurws/ird/core/EngineeringUnits.hpp`、`include/sdurws/ird/core/EngineeringPose.hpp`、`include/sdurws/ird/core/DomainValidation.hpp`、`src/DomainValidation.cpp`、`test/DomainValuesTest.cpp`、`testdata/domain/invalid-axis.json`、`testdata/domain/invalid-quaternion.json`、`out/test-evidence/wp-03/<run-id>/`；修改 `CMakeLists.txt`；删除：无。
- **禁止修改的文件和公共接口：**requirements.md、architecture/ 与 module-design/ 全部文档；WP-01 脚本与依赖清单；WP-02 testkit 及黄金数据；其他 WP 公共头。本任务不得定义 `ObjectId/ObjectIdentity`（SYM-ID-001～003 归 T02）与评估枚举（SYM-STA-001～005 归 T03）。
- **修改前接口：**无（core 模块尚不存在，仅 WP-01 构建骨架与 WP-02 testkit）。
- **修改后接口：**单位包装 `Length/Angle/Mass/Time/Power/RotationalTorque/LinearForce` 与 `InertiaComponent/Volume/Money`（显式构造、finite、Mass/Time 非负、禁止隐式 double 转换）；`EngineeringPose` 持久化只用单位四元数 `{"x","y","z","w"}`，提供测地角与有向轴误差；失败一律 `expected<T, Diagnostic>`（CTR-DIA-001），值域错误码 `IRD-CORE-VALUE-INVALID`。
- **实施步骤：**1) 写失败夹具：单位混用、NaN/Infinity、负 Mass、零轴、非单位/反平行四元数、转动/移动力混用、Money 缺货币；2) 实现单位包装与构造期校验；3) 按 canonical-kinematics §6 实现四元数符号规范化、测地角与有向轴误差；4) 用 `testdata/domain/` 非法样本与 WP-02 黄金值跑通；5) 挂接 CMake 目标并跑 `sdurws_ird_core_test`。
- **RED 测试：**`Angle` 传入 `Length` 的编译失败样例；NaN/Infinity/零轴（范数 <1e-12）/非单位或反平行四元数构造返回 `IRD-CORE-VALUE-INVALID`，不落默认值、不静默归一化。
- **最小实现：**仅上述类型与值级校验函数；不实现身份（T02）、评估枚举（T03）与聚合 JSON（T04）。
- **正常/边界/失败测试：**
  - 失败：Given NaN、Infinity、零轴、非法旋转或把 N 传入 N·m，When 构造/校验，Then 返回结构化 `IRD-CORE-VALUE-INVALID`，旧对象不变且不转默认值。
  - 正常：Given 合法 SE(3)、轴和解析黄金值（固定种子），When 计算测地角/有向轴误差，Then 在 `AlgorithmTolerance` 内得到确定结果。
  - 边界：Money 缺 ISO 4217 拒绝；`Time=0` 合法；`Power` 正负号保留；RPY 仅存在于输入/显示换算 helper，不进持久化结构（静态断言）。
- **精确验证命令**（文档约定工作根执行，VS x64；无 QApplication，不需要 GUI 平台插件）：
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**`git diff --stat` 仅命中允许清单；公共头无 `QWidget/QApplication`、旧 `sdurws_*` 插件头、RobWork include、裸 double 公共字段；无省略号命令。
- **证据工件：**`out/test-evidence/wp-03/<run-id>/`：类型编译失败样例、非有限/零轴回归日志、容差报告、测试命令输出与提交 SHA。
- **提交格式：**`WP-03-T01: 新增类型化工程值与值级校验`

  - 新增 单位包装、工程姿态与值级校验实现（expected 诊断通道）
  - 新增 值域测试夹具与 `sdurws_ird_core_test` 登记
  - 新增 非法样本与容差报告证据记录
- **停止与升级条件：**单位值域或需求 §15.3 容差无法从契约确定时停止并报告；新增公共符号或修改容差必须先走 symbol-registry/ADR 变更，不得在实现内自行冻结。
