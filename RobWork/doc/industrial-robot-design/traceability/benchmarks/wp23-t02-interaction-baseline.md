# WP-23-T02 交互响应基准首测报告（B-01／NFR-PERF-01）

> 基准清单行：performance-baseline §2 B-01（承接任务 WP-23-T02，分支 wp23-t02）。
> 本报告为**真实测量**留痕（协议 §4.8：环境/版本/数据集/样本数/分位数约定/原始样本序列必含）——测量数字全部由运行产生，零预填零虚构。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation（ZGL） |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 线程） |
| 内存 | 63.5 GB（物理） |
| 存储 | SK hynix PC801 2TB NVMe SSD（D: 仓库所在盘） |
| 操作系统 | Windows 11 专业版，10.0.26200 |
| 电源策略 | HP Optimized (Modern Standby)（`powercfg /getactivescheme` 实录——logs/hardware-env.txt） |
| 后台进程基线 | 常规开发工作站会话（无附加受控负载；空闲态判定即无后台评估任务） |
| 软件版本 | commit `a63c4b20d128fe60eceb226ea28c296ab83a4631`（分支 wp23-t02，工作树零未提交改动）；构建配置 Release（MSVC 2022 x64，冒烟模式独立配置） |
| 测量时点 | 2026-10-13（证据目录生成时间戳为准） |

同一轮基准的全部样本在同一台机器、同一硬件配置、同一软件版本上采集（协议 §4.1——两态各自分组内混入零跨机样本）。

## 2. 数据集与后台负载（performance-baseline §5.1 挂点）

- **标准规模数据集**：`ird-perf-b01-interaction@1`（manifest＝`industrialrobot/testdata/performance/b01-interaction-manifest.json`，2026-10-13 冻结）——黄金项目锚定（PM-16 原文 1,000 对象/100 修订）的 **ui 侧投影数据集**：1,000 业务对象（树五分组 10/420/380/90/100 扁平分布）、100 修订语境条数、表单 24 字段（16 长度 m/mm＋8 角度 rad/deg）、筛选词 "obj-007"、选中批量 50。
  **与 B-04 黄金项目文件数据集的关系**：本数据集只承载 ui 模型层投影面（树/选择/表单/导航供数），黄金项目文件生成器明细参数仍归 WP-23-T05 冻结（协议 §3.2）——两套数据集不混用（协议 §3.3）。
- **后台负载注入形态**（§4.3 委托本任务登记、manifest backgroundLoad 节冻结）：独立 worker 进程（`sdurws_ird_execution_worker`，阶段 A 内置脚本替身评估器——execution.md §3.4 口径）经 `execution::WorkerSupervisor` 启动，派发 `stage-a-script:v1 / batches 5,000,000` 流式评估批次（协作取消收尾——CancelAck→退出码 20）；主进程侧独立泵线程 poll 事件流（调度线程纪律），批次事件每 16 帧经 Qt 排队 Marshal 投递 UI 线程消费（M-1 测试面形态）。
- **样本计划**（协议 §4.2）：每操作 × 每负载态＝1 次预热（剔除）＋15 有效样本；P95＝线性插值分位数（p95Convention 随报告登记——testkit PerfBenchmark.hpp 唯一实现点）；**未使用 3 次中位数替代**（F-03）。

## 3. 五操作实现面（测量对象——协议 §4.3 端到端口径）

| 操作 | 模型层载体（产品代码本体） | 端到端区间（操作发起→用户可感知完成） |
| --- | --- | --- |
| 导航 nav | `IStageNavigationModel::requestNavigate`（七阶段轮转；门控评估＋汇聚快照＋事件分发） | 调用进入→NavigateResult 返回（模型状态更新＋观察者广播完成） |
| 选择 select | `SelectionService::selectBusiness`（50/1000 旋转子集；去重保序＋L2 正向存在性＋高亮动作＋SelectionChange 广播） | 调用进入→广播完成（观察者同步消费） |
| 筛选 filter | applyFilter 规则的模型侧等价施加（1,000 节点组序查询＋名称端口解析＋大小写不敏感子串匹配＋分组可见性计数） | 调用进入→全组可见性计数完成 |
| 编辑 edit | `ParamEditModel` 编辑会话（8 字段旋转暂存→表单级确认应用→IFormEditOutlet 移交） | 调用进入→移交完成（基线推进） |
| 视图切换 viewswitch | `ParamEditModel::setDisplayUnitForKind`（长度制式 mm↔cm 批量切换——KIN-12 仅投影）＋全字段 displayValueText 重读 | 调用进入→全字段显示投影重算完成 |

## 4. 无头等价口径与边界（如实申报）

测量在 **QCoreApplication 级模型层**（无 GUI 平台插件——testkit.md §6.7 模型测试豁免行）驱动上述产品模型本体；与真实界面测量的等价性论证与边界：

1. **等价半区**：200 ms 预算的规模敏感部分（投影合成/选择索引/名称解析过滤/编辑解析/显示投影重算）全部在模型层承载——五操作与 Widget 呈现层消费的是同一模型 API（M-1 UI 线程纪律同源）。
2. **口径外半区**：Widget 重绘与输入事件分派不在本测量区间（两侧同担且与数据规模近似无关）；后台态 Marshal 抽样投递（1/16）为事件流的受控代表形态——全量投递会淹没事件队列，抽样保序保活（manifest 登记在案）。
3. **筛选的规则镜像**：产品中"筛选"为呈现层规则（`IndustrialProjectTree.cpp applyFilter`——搜索子串＋分组可见性，无模型层 API）；无头驱动按其规则语义在模型侧**等价施加**（三段规则逐条对齐：组序查询/名称解析＋子串匹配/可见性计数），非共享代码——如实登记为规则镜像而非同一路径。
4. **视图切换的承载选择**：取"仅改变会话显示"的显示投影切换（KIN-12 单位制式批量切换——零脏零移交）为视图切换的模型侧承载；命令面板 `view.displayMode` 命令的模型半区归 workflow 单元（跨单元命令入口），不在本测量面——如实登记，**该映射若与验收者对"视图切换"语义的认定不一致，以验收裁决为准**（登记为开放语义点而非既成结论）。

## 5. 首测结果（真实测量——2026-10-13）

### 5.1 P95 汇总（阈值＝200 ms，REQUIREMENTS NFR-PERF-01 原文）

| 操作 | 空闲态 P95 (ms) | 空闲态 max (ms) | 后台态 P95 (ms) | 后台态 max (ms) | 判定 |
| --- | --- | --- | --- | --- | --- |
| 导航 nav | 0.0007 | 0.0007 | 0.0026 | 0.0061 | ≤200 ✓✓ |
| 选择 select | 0.0025 | 0.0026 | 0.0013 | 0.0014 | ≤200 ✓✓ |
| 筛选 filter | 0.155 | 0.159 | 0.097 | 0.101 | ≤200 ✓✓ |
| 编辑 edit | 0.0062 | 0.0066 | 0.0084 | 0.0116 | ≤200 ✓✓ |
| 视图切换 viewswitch | 0.0042 | 0.0044 | 0.0036 | 0.0038 | ≤200 ✓✓ |

全部 10 组：validCount＝15、invalidCount＝0（零无效样本——操作闭包零异常）；原始样本序列全量在报告 JSON `groups[].samples[]`（perf-reports/b01-interaction-{idle,background}.json）。P95 至 200 ms 预算的余量 ≥3 个数量级。

### 5.2 无 >2 s 无响应窗口（NFR-PERF-01 第二判据）

单次交互的 UI 线程不可交互窗＝该次操作时长（同步模型路径）：两态全部 150 个有效样本 max＝0.159 ms ≪ 2 s——零违约（逐样本在报告 JSON，消费方测试对每组断言 `maxValidMs < 2000`）。

### 5.3 >1 s 转后台用例（NFR-PERF-01 第三判据）

用例 `LongWorkRunsOffUiThreadAndMarshalsBack_WP23T02_B01`：后台评估批次于**独立 worker 进程**（PID 45708 ≠ 主进程）真实运行 ≥1200.03 ms 观察窗（未自然完结＝仍在执行），期间 UI 线程交互 61 次（最大不可交互窗 0.0914 ms ≪ 2 s）、批次事件经排队 Marshal 落地 UI 线程消费 41,587 次——长工作在后台执行且结果回流 UI 消费的闭环成立。

### 5.4 后台态运行证据

后台态测量轮：worker PID 59672（≠主进程）、ResultBatch 事件 2,931 帧流式到达、UI 线程 Marshal 消费 183 次——"至少一个评估批次正在独立工作进程中运行"（协议 §4.3 后台态定义）的进程面证据齐备（事件流等待有界 10 s 前置——批次真实执行先于采样）。

## 6. 复现命令（B-01 复现命令位——已回填 performance-baseline §2 行）

```bash
# ①冒烟配置（AGENTS §4.1 口径）
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=<Qt msvc 目录>
# ②构建（worker 可执行经 add_dependencies 随目标带出）
cmake --build <冒烟目录> --config Release --target sdurws_ird_ui_perf_test
# ③测量（报告落 <证据目录>/b01-*.json；gitCommit 入报告可选）
IRD_PERF_REPORT_DIR=<证据目录> IRD_PERF_GIT_COMMIT=<SHA> \
  ctest --test-dir <冒烟目录>/ui -C Release -R "^sdurws_ird_ui_perf_test$" --output-on-failure
```

环境前提：本报告 §1 硬件清单＋Release 配置；跨机/跨配置样本不得与本统计混入（协议 §4.1）。

## 7. 范围限定与如实登记

1. **集成模式复测挂点**：本轮为冒烟模式（AGENTS §4.1 口径）；集成模式（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON）下同目标复测由**合入批执行**（实施者不操作集成构建——三段式分工），命令同 §6（目标与 ctest 面不变）。
2. **testkit 套件一次抖动记录**：证据采集首轮 ctest 中 `ProcessRunnerOutcome.TimeoutReportsTerminatedByTimeoutAndKillsTree`（既有 4 s 超时类用例，与本基准无关）超时一次；隔离直跑 4/4＋全量复跑 exit 0——按环境抖动如实登记（logs/ctest-testkit.log 首轮＋ctest-testkit-rerun.log 复跑双留痕）。
3. **开放语义点**：§4.4"视图切换"映射为验收开放语义点——若裁决为"视图切换必须含命令面板 view.displayMode 路径"，需补 workflow 侧模型半区测量（跨单元测试面直链扩登），达标数值不受影响（该路径为命令分发量级，远低于预算）。
4. 本报告零达标数值私设：200 ms/2 s/1 s 三值照抄 REQUIREMENTS NFR-PERF-01 原文；样本量与 P95 口径照抄 performance-baseline §4.2/§4.3。

## 8. 证据清单（build/wp23-t02-wt/_m5-evidence/）

| 文件 | 内容 |
| --- | --- |
| perf-reports/b01-interaction-idle.json | 空闲态会话报告（p95Convention＋10 组之 5 组原始样本＋阈值判定） |
| perf-reports/b01-interaction-background.json | 后台态会话报告（同上＋后台运行证据 notes） |
| perf-reports/b01-longwork-background.json | 转后台用例报告（allPassed＝true＋运行证据 notes） |
| gtest-xml/sdurws_ird_ui_perf_test.xml | 基准目标 gtest XML（3/3 通过） |
| gtest-xml/sdurws_ird_testkit_test.xml | testkit 套件 XML（110/110，含 PerfBenchmark 元测试 4 例） |
| gtest-xml/sdurws_ird_ui_contract_test.xml | ui 契约套件 XML（26/26，含 perf 目标直链钉） |
| logs/build-three-targets.log | 三目标构建日志（BUILD-EXIT=0，零 error） |
| logs/ctest-{testkit,testkit-rerun,ui-perf,ui-contract}.log | ctest 留痕（首轮抖动＋复跑全绿如实双留痕） |
| logs/{perf-direct-run,testkit-direct-run,contract-direct-run}.log | 直跑控制台日志 |
| logs/hardware-env.txt | 硬件/电源环境实录（CIM＋powercfg） |
| logs/run-env.txt | 采集时点与 commit 锚 |
