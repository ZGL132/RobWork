# 系统质量与规模化模块详细方案（system-quality）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-23；阶段/发布：阶段 A 起持续建设，规模化门禁阶段 D 收口 / R1+R2；任务卡：`agent-tasks/WP-23-T01～T05`
- 架构契约：`architecture/testing-contract.md`、`architecture/execution-model.md`、`architecture/persistence-schema.md`、`architecture/evaluation-semantics.md`（§1～2）、`architecture/symbol-registry.md`
- 依赖：各阶段模块（总纲 §5.4，按阶段接入）；黄金数据与断言库来自 WP-02；门禁入口来自 WP-01；基准输入为 `benchmark-manifest.json`（变更须双负责人批准）

## 1. 模块职责与持续建设

拥有 AT-01～AT-19 系统级执行清单、故障注入矩阵、确定性/并行复现测试、性能与内存基准、恢复演练和 R1/R2 发布门禁证据。从阶段 A 起每阶段交付对应子集：A＝A-GATE-01～07 脚本化＋故障注入雏形；B＝AT-01～05、15～18 的 B 链路子集；C＝AT-06～08、19 的 C 链路子集；D＝AT-09～14 与 AT-18/19 全链路＋规模化基准；E＝发布门禁汇总。非目标：修改业务实现以绕过门禁、拥有业务黄金数据（WP-02 所有）、替代 WP-01 构建门禁。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/
  include/sdurws/ird/system/
    AtRegistry.hpp FaultInjectionMatrix.hpp BenchmarkRunner.hpp RecoveryDrill.hpp ReleaseGate.hpp
  src/AtRegistry.cpp FaultInjectionMatrix.cpp BenchmarkRunner.cpp RecoveryDrill.cpp ReleaseGate.cpp
  scenarios/at-01…at-19/  faultpoints/  benchmarks/  drills/
  test/SystemSuiteTest.cpp FaultInjectionTest.cpp BenchmarkTest.cpp DeterminismTest.cpp ReleaseGateTest.cpp
  evidence/WP-23/
```

CMake target（与任务卡命令一致）：`sdurws_ird_system_suite_test`、`sdurws_ird_fault_injection_test`、`sdurws_ird_benchmark_test`、`sdurws_ird_determinism_test`、`sdurws_ird_release_gate_test`；`sdurws_ird_system_test` 为聚合别名目标（运行全部五域，对应 WP 计划验证命令）。允许依赖：WP-02 testkit、各模块测试目标与 failpoint 夹具、WP-01 脚本；禁止：绕过统一测试入口直改业务代码、写回 testdata、无批准修改 `benchmark-manifest.json`。

## 3. AT-01～19 全量执行清单（自需求 §15.2/§16 推导）

| AT | 覆盖需求（§16） | 阶段 | 执行入口 | 数据集（WP-02） | 通过判据 |
| --- | --- | --- | --- | --- | --- |
| 01 | MDL-01～04、07、09～12 | B | 建模：DH/显式/URDF 三入口建同一模型 | models 三套等价模型 | 权威类型正确；Origin/Axis/限制/Home/Zero/FK 在 §15.3 关节/FK 容差内一致 |
| 02 | REQ-05、07 | B | 需求：导入含错误行任务表 CSV | requirements 错误行样本 | 正确行保留、错误行定位字段与单位；预览不入正式证据 |
| 03 | REQ-01～04、KIN-01～05 | B | 运动学：批量工位验证 | requirements 可行/不可行任务集 | 状态、原因、候选详情与三维姿态一致；缺证据＝DataInsufficient |
| 04 | KIN-06～08、TRJ-07 | B | Jog、IK 预览、轨迹播放 | models＋requirements 标准工位 | 修订号与结果当前性不变（A-GATE-01 同口径） |
| 05 | CON-05、KIN-08 | B | 修改 TCP 并应用；单独改电机成本 | 同上 | TCP→实际依赖下游 Superseded；电机成本不使运动学/轨迹失效 |
| 06 | TRJ-01～06 | C | 轨迹：Verified 规划搬运轨迹 | trajectory 基准（接近/撤离/避障） | 碰撞协议结论；限值超差≤1e-6 且节拍规则满足（§15.3 轨迹行） |
| 07 | DYN-01～03、05～08 | C | 动力学：逆动力学＋正动力学收敛 | dynamics 解析算例 | 相对误差≤1e-6（近零下限 1e-8）；h 与 h/2 收敛；峰值可定位 |
| 08 | SEL-01～09 | C | 选型：器件筛选 | catalog 可行/淘汰/范围外样本 | 可行通过；淘汰含实际值/阈值；移动关节"范围外"诊断 |
| 09 | OPT-01～10 | B（静态子集）/D（全量） | 优化：分层联合优化 | optimization 基线/改进/硬约束失败候选 | 可行 Pareto 集不含硬约束失败候选；固定种子复现 |
| 10 | TASK-03、NFR-REL | D | 优化期间切换/关闭项目 | optimization 长任务样本 | 旧结果只追加原分支历史；项目完整 |
| 11 | CON-04、TASK-01 | D | 中断并恢复长任务 | optimization 检查点样本 | 从兼容检查点恢复；已完成批次不重复计入 |
| 12 | OPT-07～09、MDL-08 | D | 应用优化候选 | 同 AT-09 | 方案分支＋恰好一个新修订；基线不覆盖；运行期间修订数不随候选增长 |
| 13 | NFR-REL-02、TASK-01 | A 起（D 全链路） | 注入 worker 崩溃 | faultpoints | 主程序存活、项目可恢复、任务显示 Failed（A-GATE-05 同口径） |
| 14 | NFR-PERF-03～06 | D | 大型负载 | performance 样本（5,000 任务/100k 采样/10k 候选） | 界面持续响应（§11.2 预算）；无候选静默丢失、无错误写入 |
| 15 | MDL-03、11、12 | B | URDF 轴样本导入 | models URDF 边界集 | 非 Z 轴保持原值＋运行时等价；缺失轴＋X 草稿；零轴仅报告且修订不变 |
| 16 | MDL-01、02、09、10 | B | DH↔显式权威转换 | models 四类转换判定样本 | 四类判定正确；`AnalysisFailed` 注入生效；切换只产生一个新修订 |
| 17 | MDL-03、11、12 | B | URDF 拓扑样本 | models 分支/关节类型样本 | 固定附件完整导入；多分支未选择不成模；不支持关节阻断不降级 |
| 18 | ARC-04、MDL-14 | B/C/D 分链路 | 编译＋导入＋重命名后八链路解析 | models 名称作用域样本 | objectId 绑定不变；无未解析/旧/双前缀（A-GATE-06 同口径） |
| 19 | ARC-05、NFR-COR-05 | B/C/D 分入口 | 运动学/轨迹/优化三入口同状态求值 | collision 策略样本 | 对象 ID 碰撞对、判定、原因完全一致；显示开关不改判定（A-GATE-07 同口径） |

## 4. 故障注入矩阵（对接 WP-04/WP-08 failpoints）

| 故障类别 | 注入点 | 恢复断言 |
| --- | --- | --- |
| 版本目录写入失败 | WP-04 staging failpoint | 旧 HEAD/树哈希不变；重启忽略 staging（A-GATE-04） |
| 资源写入失败／提交指针切换失败 | WP-04 资源与 HEAD failpoint | 逐文件校验一致；项目可再次打开 |
| worker 崩溃／IPC 中断 | WP-08 worker failpoint | 任务 Failed 附退出原因；主界面存活；检查点保留（AT-13） |
| 取消超时强杀 | WP-08 取消时限 failpoint | Canceled＋"强制终止"诊断；最近兼容检查点可恢复 |
| 磁盘满／内存阈值 | WP-08 资源 failpoint | 先节流后 `IRD-EXEC-RESOURCE-BUDGET`；无项目损坏 |
| 外部资源缺失/变化 | WP-05 源监控样本 | `IRD-PERSIST-SOURCE-*`；重新关联入口可用 |
| 结果工件损坏 | results 目录哈希篡改 | 读回 `ArtifactIntegrity=Corrupt`；拒绝正式用途 |
| 报告渲染失败 | WP-12 渲染 failpoint | 既有工件保留；`IRD-RPT-RENDER-FAILED` 可重试 |

## 5. 确定性、性能与恢复演练

- 并行复现（§15.3 并行复现行）：固定 seed/threadCount（`benchmark-manifest.json`：seed 20260828、threadCounts [1,8]）下候选稳定 ID、可行集合与 Pareto 支配关系一致；数值满足算法级相对/绝对误差；不要求浮点文件逐字节相同（NFR-COR-02）。
- 性能与内存基准：按 `benchmark-manifest.json` 全字段执行——验收机 16 核/64GB/SSD、数据集 `ird-standard-six-axis-v1`、预热 2＋测量 3、冷热启动均记录、中位数聚合且保留全部样本、后台负载（1 个 5,000 任务批处理 worker）、排除首次依赖编译与数据导入；交付断言：交互 P95≤200 ms（NFR-PERF-01）、5,000 任务批处理 30 min、100k Quick 样本 20 min、8 线程加速比≥4、标准优化基准 8 h 内、内存峰值≤物理内存 70%（节流先于诊断）。
- 恢复演练：断电（进程终止注入）→ 重启发现 `Interrupted` 不伪装完整结果；崩溃 → `Failed`；锁夺取 → 心跳超时夹具（WP-04 默认 30 s）后安全夺取；恢复后批次统计不重复（A-GATE-05、AT-11）。

## 6. 发布门禁证据格式（R1/R2 gate 清单）

每条门禁证据按 testing-contract §4：任务 ID、需求 ID、AT/门禁 ID、提交 SHA、环境（机器/OS/编译器/线程）、命令原文、输入哈希（datasetVersion＋manifest SHA-256）、实际/期望结果、日志路径、评审者、结论。R1 gate＝阶段 B/C 退出条件 AT 子集＋NFR-PERF-01～03＋NFR-REL；R2 追加＝阶段 D 退出条件 AT-09～14＋NFR-PERF-04～06＋确定性＋恢复演练。缺陷口径按 §15.4：发布时开放 Blocker＝0、未关闭 Critical 均有负责人与计划日期；"不得以关闭测试、降低阈值或隐藏诊断代替修复"由门禁脚本断言（与既有阈值/诊断清单比对）。

## 7. 测试与证据

验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_(system_suite|fault_injection|benchmark|determinism|release_gate)_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_system_suite_test sdurws_ird_fault_injection_test sdurws_ird_benchmark_test sdurws_ird_determinism_test sdurws_ird_release_gate_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_(system_suite|fault_injection|benchmark|determinism|release_gate)_test$"
```

GUI 相关 AT 单独启动、一次一个（testing-contract §5）；性能目标只在验收机执行，开发机结果标注"非基准"。证据：AT 执行矩阵、故障注入日志、基准报告（含全部样本）、恢复统计、门禁清单与独立质量负责人签名（评审者不得参与被测实现）。

## 8. 迁移与删除表

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 历史测试结果 | 只读归档 | — |
| 失效测试入口与重复基准配置 | Delete | 新系统门禁稳定后（WP 计划） |
| 绕过统一入口的私有测试脚本 | Delete | WP-01 入口覆盖全部五域 |
