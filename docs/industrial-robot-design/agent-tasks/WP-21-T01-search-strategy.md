# WP-21-T01 联合搜索策略

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T01；OPT-01（新机型/改型初始化）、OPT-02（连续/量化/离散变量，锁定语义由 WP-20 提供）、OPT-05（分层联合策略）、OPT-06（Quick 筛选/Verified 复核/局部改进/确定性种子）、OPT-10（`algorithmPolicy` 策略接口扩展）、AT-09、NFR-PERF-04～06；ADR-004（单一权威共享语义，不自建第二套调度/缓存）。阶段 D / R2。契约：`architecture/candidate-compilation.md`（最高权威）、`architecture/evaluation-semantics.md`、`architecture/execution-model.md` §2～§3、`architecture/symbol-registry.md`；模块详设 `module-design/optimization.md` v0.3 §5.6（联合搜索首版算法，模块冻结）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（optimization.md v0.3，检查点 `IRD-D2-20260829`）
- **前置任务及必需工件：** WP-20-T01/T02（`OptimizationStudyDefinition` 校验与 `CandidatePatch` 编译公共头，`sdurws_ird_optimization_definition_test` 通过）；WP-16-T01～T06、WP-17-T01～T06、WP-19-T01～T06（评估器经 WP-08-T01～T05 调度注册，返回 `ResultEnvelope`）；WP-02-T03（optimization 数据集：基线/改进/硬约束失败候选）。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建 `joint/include/sdurws/ird/opt/JointSearchOrchestrator.hpp`、`joint/src/JointSearchOrchestrator.cpp`、`test/JointSearchTest.cpp`、`testdata/optimization/` 下本任务夹具；修改本模块 CMakeLists（登记 joint 源与目标）；写 `out/test-evidence/wp-21/<run-id>/`。不删除任何文件。
- **禁止修改的文件和公共接口：** WP-20 的 definition/candidate 目录与公共头；WP-16～19 评估器源文件（仅经 WP-08 调度与 `ResultEnvelope` 交互，无业务插件代码依赖）；WP-08 execution 源文件；`requirements.md`、CSV、`schemas/`、`benchmark-manifest.json`；不新增/改名 CMake 目标（仅 `sdurws_ird_optimization_joint(_test)`）。
- **修改前接口：** `joint/` 目录与 `sdurws_ird_optimization_joint(_test)` 目标不存在；无联合搜索编排入口。
- **修改后接口：** `JointSearchOrchestrator.hpp` 提供分层搜索首版算法（模块冻结、可经 `algorithmPolicy` 替换）：外层＝量化/离散网格枚举或固定种子拉丁超立方采样＋保留池；局部改进＝对非支配候选沿激活目标做坐标步长搜索（步长＝量化步长或域宽 2%）；内层＝Quick 运动学筛选→Verified 轨迹/动力（WP-16/17）→器件目录匹配（WP-19），无组合可行时把原因反馈外层。CMake 新增 `sdurws_ird_optimization_joint` 与 `sdurws_ird_optimization_joint_test`。
- **实施步骤：**
  1. 写 RED 测试（外层采样确定性、保留池更新、内层三层调用序、器件不可行反馈外层、策略替换契约）。
  2. 定义编排接口：输入＝研究定义＋预算＋种子＋线程数，输出＝批次候选流与淘汰原因流。
  3. 实现外层网格/LHS 采样（固定种子，样本身份进日志）与非支配保留池。
  4. 实现局部坐标步长搜索（激活目标维度，量化变量取量化步长，连续变量取域宽 2%）。
  5. 实现内层调用序与器件反馈路径（Quick/Verified 一律经 WP-08 调度提交）。
  6. CMake 登记目标，执行验证命令，写证据。
- **RED 测试：** 实现前 `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test` 失败（目标不存在）；落地后同命令与对应 ctest 必须通过。
- **最小实现：** 仅外层采样＋保留池＋内层三层调用序＋反馈路径＋可替换 `algorithmPolicy`；不做四层判定（T02）、检查点/缓存策略（T03）、Pareto/审计（T04）、应用（T05）。
- **正常/边界/失败测试：**
  - 正常：Given WP-02 optimization 基线研究定义与固定种子，When 运行编排，Then 同种子同线程数下外层样本序列、保留池内容与候选集合逐项一致（OPT-06 确定性种子）。
  - 边界：Given 量化/离散/连续混合域及域宽极窄变量，When 采样与局部步长，Then 步长取量化步长或域宽 2% 之一且不越域；Given 器件目录对某候选无组合可行，When 内层匹配，Then 逐项原因反馈外层后继续探索或按预算终止。
  - 失败：Given 研究定义被 WP-20 校验拒绝（`IRD-OPT-UNREGISTERED-BINDING`、`IRD-OPT-STAGE-LOCKED` 等），When 编排，Then 返回稳定诊断且不产生部分候选工件。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；`joint/` 源码不出现对 WP-16～19 头文件或其他业务插件目标的直接 include/链接；无候选直写 revision 代码；无加权总分逻辑。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/t01-search-strategy.log`：命令原文、commit、种子、线程数、外层样本身份、保留池快照、同种子两轮复现对照表。
- **提交格式：** `WP-21-T01: 联合搜索策略`
- **停止与升级条件：** optimization.md §5.6 冻结算法与 WP-08 调度契约或 WP-16～19 评估器端口无法对接、WP-20 编译管线公共头缺失时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
