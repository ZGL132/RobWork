# WP-14-T03 姿态与区域语义

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T03；REQ-01、REQ-03、需求 §15.3（区域覆盖口径）；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.3 §3/§5
- **前置任务及必需工件：**WP-14-T01（`PoseRegion` 字段骨架与校验链工件）
- **允许创建/修改/删除的文件：**修改 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/PoseRegion.hpp`（补全实现）；创建 `requirements/src/PoseRegion.cpp`；`requirements/test/PoseRegionTest.cpp`；`requirements/testdata/requirements/regions/`；`requirements/out/test-evidence/wp-14/<run-id>/`；`requirements/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**T01 冻结的 `TaskPoint` 字段与容差语义；WP-15 拥有的覆盖率定义权；`schemas/`、`architecture/`、`module-design/`
- **修改前接口：**`PoseRegion` 仅为字段骨架（T01）
- **修改后接口：**`PoseRegion{regionId, priority, frameId, Box min/max, positionSampleCount/orientationSampleCount, minCoverageRatio}`；`PoseRegion::plannedDenominator()`（冻结定义：分母＝计划的位置—姿态组合＝`positionSampleCount×orientationSampleCount`，含边界样本，供 WP-15 消费）；`PoseRegion::validateBudget()`（单区域组合上限 1,000,000，模块冻结）
- **实施步骤：**1) 补全区域字段校验（Box min<max、采样数正整数、`minCoverageRatio ∈ (0,1]`）；2) 实现分母定义与预算校验（超限→`IRD-REQ-SAMPLING-BUDGET`，Engineering/Error）；3) 部分位姿约束声明（`constrainedComponents` 集合，未列出即自由）；4) 网格默认含边界的口径断言；5) 边界样本夹具
- **RED 测试：**Given `positionSampleCount×orientationSampleCount > 1,000,000` 的区域，When `validateBudget`，Then 拒绝（`IRD-REQ-SAMPLING-BUDGET`）且不产生可应用草稿（`PoseRegionTest` 先行）
- **最小实现：**区域校验＋分母计算＋预算拒绝；不做采样点生成与覆盖率计算（WP-15）
- **正常/边界/失败测试：**
  - 正常：Given 合法区域，When 序列化与读取，Then 字段、分母与来源稳定（JSON 往返 1e-12）
  - 边界：Given 组合数恰等于 1,000,000，When 校验，Then 通过；Box min/max 相等（退化维度）时按声明拒绝或通过并记录诊断（按详设口径：min<max 必须成立，相等拒绝）；网格默认含边界＝首末样本落在 Box 边界
  - 失败：Given 4/5 轴任务声明的受约束分量集合含未知分量名，When 校验，Then Input 诊断定位到字段（REQ-01：本模块只声明分量集合，子空间 Jacobian 构造归 WP-15）
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "coverage\|jacobian\|samplePose" requirements/src/PoseRegion.cpp` 零命中（覆盖率定义与子空间构造不归本卡）；上限 `1000000` 常量仅出现一处（冻结常量）
- **证据工件：**`requirements/out/test-evidence/wp-14/<run-id>/`——区域黄金数据、分母对照表、预算拒绝矩阵、边界包含断言输出
- **提交格式：** `WP-14-T03: 定义位姿区域`

  - 新增位姿区域与公差定义
  - 新增区域判定测试
  - 新增运行证据记录
- **停止与升级条件：**任务坐标系/容差语义不明确、或 WP-15 需要的分母定义与冻结口径冲突时暂停并升级 WP-14/WP-15 联合评审；上限 1,000,000 需变更时走模块详设版本升级
