# WP-02-T03 领域黄金数据

- **Task ID / 需求 ID / ADR / 阶段：** WP-02-T03；NFR-COR-01（WP-02 唯一主包需求：为 AT-01～19 与后续 WP 提供解析/独立参考数据集）；无直接关联 ADR；阶段 A 前提 / R1。契约：`module-design/testkit.md` §3（`ird-golden-0.7.1` 组成清单表）与 §6（哈希、版本与再生成协议）、`architecture/testing-contract.md` §2。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-02-T01（loader/manifest 可用）与 WP-02-T02（两层容差断言库可用）；工件：`testdata/manifest.json` 最小样本版本、`sdurws_ird_assertion_library_test` 通过日志。
- **允许创建/修改/删除的文件：**
  - 创建：`testdata/models/`、`testdata/requirements/`、`testdata/collision/`、`testdata/dynamics/`、`testdata/catalog/`、`testdata/optimization/`、`testdata/performance/` 各域样本文件
  - 创建：`testdata/generators/`（确定性生成器与版本记录，黄金数据生成种子口径 20260829）
  - 修改：`testdata/manifest.json`（扩充为全量样本清单，`datasetVersion=ird-golden-0.7.1`）
  - 创建：`testkit/tests/golden_data_test.cpp`；修改：`testkit/CMakeLists.txt`（注册 `sdurws_ird_golden_data_test`）
  - 写证据：`out/test-evidence/wp-02/<run-id>/`（动态性能数据也只写此目录）
- **禁止修改的文件和公共接口：** T01/T02 冻结的 manifest 字段、Loader 顺序与断言 API；`benchmark-manifest.json`（性能规模须与其 workloads 一致，不得反向修改）；`industrialrobot/` 既有模块；`requirements.md`、CSV；不得用运行中产品输出或 Widget/会话状态覆盖黄金样本。
- **修改前接口：** `testdata/manifest.json` 为 T01 最小样本集版本（仅完整性检查所需样本）。
- **修改后接口：** `ird-golden-0.7.1` 全量组成严格按 `module-design/testkit.md` §3 表 11 行交付：models 等价三套（标准 DH、显式关节、URDF 三套等价六轴模型；≥2 可动关节非 Z 局部轴；4/7 轴边界）、models URDF 边界（缺失/零/非单位 axis、continuous、prismatic、固定附件分支、多可动分支、planar/floating、Mimic）、models 转换判定（Exact、ExactNonUnique、Approximate、NotRepresentable＋一次 AnalysisFailed 注入）、models 名称作用域（Arm/ArmA、同局部名、前缀、重命名）、collision 场景（启用/草稿禁用、允许/排除配对、安全距离边界、路径验证 Profile、纯显示开关）、requirements 含轨迹基准（3～5 关键工位、接近/撤离段、工作区域、末端工具、≥两档负载、障碍；可行与不可达/碰撞/超限任务集、含错误行任务表）、dynamics 基准（二连杆解析 FK/Jacobian、静态重力矩、完整循环包络、正动力学 h 与 h/2 收敛）、dynamics 传动映射（多速比效率、反射惯量、摩擦不重复计入、峰值窗/RMS）、catalog（可行/不可行条目、曲线、坏引用、移动关节目标链）、optimization（基线、预期改进候选、硬约束失败候选、检查点/恢复、排序）、performance（5,000 任务点、100,000 采样摘要、10,000 候选）；每样本 11 字段齐备且 consumers 至少一项、指向存在的任务。
- **实施步骤：**
  1. 先写 `golden_data_test.cpp` RED 断言并注册目标，构建确认失败（样本与组成缺失）。
  2. 逐类生成样本：解析推导值写 source 与 generationMethod，登记 consumers（对应 AT/WP 任务）。
  3. 实现确定性生成器并登记版本、commit 与固定种子 20260829。
  4. 扩充 `manifest.json` 至全量清单并计算每样本 SHA-256。
  5. 核查仓库 `300kg_urdf/`（关节拓扑、轴定义、物性、许可证）能否作为 URDF 等价模型来源，结论记入证据后方可采用。
  6. 按验证命令（脚本＋原生双形式）转绿，执行再生成一致性检查并写证据。
- **RED 测试：** 先写的失败断言：`ManifestCoversFrozenComposition`（§3 表 11 行逐行有样本）、`EverySampleDeclaresConsumers`（consumers ≥1 且指向存在的任务）、`RegenerationReproducesIdenticalHashes`（固定 seed/线程/版本重跑生成器 → 文件哈希、参考结果、stableId 与排序完全一致）、`UrdfSourceSuitabilityIsRecorded`（300kg_urdf 核查结论存在于证据）。
- **最小实现：** 仅交付 §3 表最小组成所列样本、生成器与 manifest 扩充；不实现消费算法（消费者测试属后续 WP）；不为凑数添加表外类别。
- **正常/边界/失败测试：**
  - 正常：Given 固定 seed 20260829 与单线程，When 重跑生成器再加载，Then 全部样本哈希与参考结果一致、`sdurws_ird_golden_data_test` 通过。
  - 边界：Given performance 样本，When 与 `benchmark-manifest.json` workloads 对照，Then 规模一致（5,000/100,000/10,000），性能基准种子 20260828 与黄金种子不混用。
  - 失败：Given 样本缺 expected/source/units/sha256 或消费者缺失，When 运行 manifest 校验，Then 非零且该样本不得进入测试；参考值无法解析推导或独立复核时标记数据不足，不得生成精确通过。
- **精确验证命令：**（仓库根目录、VS x64 环境；脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_golden_data_test$'`；预期退出码 0。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_golden_data_test` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_golden_data_test$"`；预期构建成功、测试通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `testdata/`、`testkit/tests/golden_data_test.cpp`、`testkit/CMakeLists.txt`；`benchmark-manifest.json` 与 `requirements.md` 零变化；无期望值绕过 manifest 直接硬编码在测试中。
- **证据工件：** `out/test-evidence/wp-02/<run-id>/`：datasetVersion、manifest SHA-256、生成器 commit 与 seed、逐样本哈希清单、AT 消费关系表、再生成一致性与 300kg_urdf 核查记录、测试日志（含命令与提交 SHA）。
- **提交格式：** `WP-02-T03: 新增版本化领域黄金数据集`

  - 新增 各域黄金样本、确定性生成器与全量 manifest 清单（ird-golden-0.7.1）
  - 新增 `sdurws_ird_golden_data_test` 测试与目标登记
  - 新增 逐样本哈希与再生成一致性证据记录
- **停止与升级条件：** 参考值无法由解析推导或独立工具复核、或样本组成与 §3 表冲突时，停止并标记数据不足上报；数据生产者不得批准自己的期望值，独立算法验证者复核后方可冻结哈希。
