# WP-23-T05 首测报告——B-04 黄金项目打开基准（PM-16）

> 任务：WP-23-T05（DTB §2.24 行）｜基准行：performance-baseline §2 B-04｜分支 wp23-t05｜软件版本锚：git commit `21a6501421c778f6c5d2018920fa6476ce3b5e47`（实现 tip——测量目标与数据集 manifest 落位后）＋构建配置 Release（冒烟模式）
> 测量口径：B-04 为 **P95 分位数类基准**（performance-baseline §2 统计口径区分行——禁 3 次中位数替代，F-03）；**验收门禁仅限热启动组**（V13 §4 收窄——热启动 P95 ≤ 3 s，≥15 有效样本）；冷启动组单独报告、软指标 P95 ≤ 8 s（M-14，v1.8）不作门禁，超限登记原因与改进项。分组样本量：每组 1 次预热剔除＋15 有效，两组合计 30 ≥ 30（PM-16 原文合计下限）；P95＝线性插值分位数（协议 §4.2 公式——testkit `percentileLinearInterpolated` 唯一实现点）。

---

## 1. 达标结论

**达标（热启动门禁组全绿；冷启动软指标组单独报告且实测达标）**——黄金项目（1,000 对象/100 修订，ird-perf-b04-golden-project@1）在产品打开协议（`ProjectStoreFactory::open` 完整 ②③⑤＋Writable 锁获取与恢复扫描）上：热启动组 P95＝**153.01 ms**（阈值 3,000 ms，pass）；冷启动组 P95＝**149.17 ms**（软指标阈值 8,000 ms，参考达标——不作门禁）；两组各 1 预热＋15 有效、零无效样本；两组合计 30 有效满足 PM-16 原文合计下限。

| 达标口径（照抄需求原文语义） | 判定 | 证据 |
| --- | --- | --- |
| 黄金项目（1,000 对象/100 修订） | ✅ 生成器规模恒等式钉住：100 数据修订×10 对象＝1,000 数据对象（磁盘 oidDirs=1,101＝1,000 数据＋101 元数据 oid）；修订历史 101＝100 数据修订＋createNew 骨架 r0（机制面，如实登记） | 夹具磁盘地面事实核对＋stderr 打点 `[b04-golden] disk-facts headRefs=11 history=101 oidDirs=1101`（直跑日志留痕） |
| **热启动**打开 P95 ≤ 3 s（≥15 有效样本） | ✅ P95＝153.01 ms ≤ 3,000 ms（门禁）——15 有效＋零无效 | 报告 `b04-warm-open.json`（verdict pass=true；原始样本序列 §4） |
| 每组有效样本 ≥15、剔除首次预热、合计 ≥30 | ✅ 热 1 预热（excluded 留痕）＋15 有效；冷同构；合计 30 | 两报告 samples 序列（预热样本 excluded=true 原位留痕） |
| P95 线性插值分位数 | ✅ `p95Convention=linear-interpolation:h=(n-1)*q;x=floor+f*(next-floor)`（夹具唯一实现点；本报告 §4 脚注手工复核逐位一致） | 报告 p95Convention 字段＋§4 复核 |
| 冷启动组单独报告、超限登记（不阻断验收） | ✅ 单独报告 `b04-cold-open.json`；实测 P95 149.17 ms 低于 8,000 ms 软指标（登记性质＝参考达标，**不作门禁**——用例断言面不含 P95 阈值） | 报告 verdicts＋notes（口径登记）；DTB 行禁止项"冷启动不作门禁"的用例结构承载 |
| 冷启动重置＝采样前重启进程 | ✅ 每样本全新子进程（`sdurws_ird_project_openbench_worker`——CREATE_SUSPENDED→Job Object→Resume；进程启动＋DLL 加载＋符号绑定全部重置） | 用例实现＋报告 notes 口径登记 |

## 2. 硬件与环境（协议 §4.1 清单）

| 项 | 值 |
| --- | --- |
| 机器 | ZGL（单机采集——同一轮基准全部样本同机同配置） |
| CPU | 13th Gen Intel(R) Core(TM) i9-13950HX（24 核 32 线程；报告 cpuInfo 字段登记 PROCESSOR_IDENTIFIER 原文：Intel64 Family 6 Model 183） |
| 内存 | 64 GB（63.5 GB 可见——DDR5 SODIMM） |
| 存储 | SSD（SK hynix PC801 HFS002TEJ9X101N NVMe——黄金项目落盘通道） |
| OS | Microsoft Windows 11 专业版（Windows_NT AMD64） |
| 电源策略 | 交流供电桌面常态（未做节流干预；如实登记） |
| 后台进程基线 | 空闲态（无后台评估任务——B-04 为项目打开分组基准，无两态负载面） |
| 软件版本 | commit `21a6501421c778f6c5d2018920fa6476ce3b5e47`（wp23-t05 tip）＋Release 冒烟构建（MSVC 2022 x64，/O2 优化） |

## 3. 数据集（ird-perf-b04-golden-project@1）

manifest＝`testdata/performance/b04-golden-project-manifest.json`（参数冻结，升级换版本——testkit.md §4.2.1）。黄金项目构成与生成器：

| 项 | 值 | 说明 |
| --- | --- | --- |
| 数据对象 | 1,000（PM-16 原文） | 每对象 2,048 字节确定性文本（序号派生——可复现）；对象类型 token "GoldenData" |
| 数据修订 | 100（PM-16 原文） | 每修订 1 个 golden-append-batch 命令声明写 10 个对象；另有 createNew 初始骨架修订 r0（机制面——PM-01 存储侧组装固定产出，非生成器参数），项目修订总数 101 |
| 生成管线 | 真实产品命令管线 | createNew→handlerRegistry() 公共装配通道注册基准处理器→100 次命令提交（七步提交协议——.staging 暂存/验证/只增发布/HEAD 原子切换）→requestClose 排空释放写锁；非预录数据 |
| 打开模式 | Writable（两组同路径） | 完整打开协议：②目录形态与版本＋③装载读校验＋⑤孤儿草稿扫描＋Writable 锁获取与恢复扫描 |
| 测量区间 | open() 调用＋HEAD 首读 | "自操作发起至项目数据可读"的模型层等价；打开后核对与关闭排空全部在区间外（目标内采样执行器——T03 CancelTimingRig 先例） |
| 冷启动承载 | 独立子进程/样本 | `sdurws_ird_project_openbench_worker`——进程启动＋DLL 加载＋符号绑定重置；子进程内计时经 ird-openbench/1 报告文件回传 |

## 4. 测量数据（原始样本序列全量——不得只报统计值）

### 4.1 热启动门禁组（commit 21a65014，opId=open-warm，stateId=process-running）

1 次预热（excluded 留痕）＋15 有效，零无效（单位 ms）：

| # | excluded | durationMs |
| --- | --- | --- |
| 0 | true（预热） | 140.4679 |
| 1 | false | 155.1396 |
| 2 | false | 140.0797 |
| 3 | false | 145.3231 |
| 4 | false | 145.7714 |
| 5 | false | 139.9243 |
| 6 | false | 134.5584 |
| 7 | false | 144.0861 |
| 8 | false | 135.9655 |
| 9 | false | 148.8930 |
| 10 | false | 139.8023 |
| 11 | false | 140.0854 |
| 12 | false | 145.8674 |
| 13 | false | 152.0932 |
| 14 | false | 142.5263 |
| 15 | false | 132.7853 |

**P95＝153.00712 ms**（有效样本最大 155.1396，最小 132.7853）——阈值 3,000 ms（REQUIREMENTS PM-16 原文），**pass（门禁达标）**。

手工复核（协议 §4.2 公式，与夹具唯一实现点逐位互证）：有效样本升序第 14/15 位为 152.0932/155.1396；h=(15−1)×0.95=13.3 → P95＝152.0932＋0.3×(155.1396−152.0932)＝153.00712 ✓。

### 4.2 冷启动软指标组（单独报告——不作门禁；opId=open-cold，stateId=fresh-process）

1 次预热（excluded 留痕）＋15 有效，零无效（单位 ms；每样本全新子进程）：

| # | excluded | durationMs |
| --- | --- | --- |
| 0 | true（预热） | 143.9490 |
| 1 | false | 148.5160 |
| 2 | false | 136.6380 |
| 3 | false | 136.9980 |
| 4 | false | 150.3170 |
| 5 | false | 140.7710 |
| 6 | false | 139.0960 |
| 7 | false | 145.7140 |
| 8 | false | 148.6770 |
| 9 | false | 142.9070 |
| 10 | false | 138.6980 |
| 11 | false | 132.0770 |
| 12 | false | 132.9430 |
| 13 | false | 137.4800 |
| 14 | false | 133.7190 |
| 15 | false | 135.9510 |

**P95＝149.169 ms**（有效样本最大 150.317，最小 132.077）——软指标阈值 8,000 ms（M-14，v1.8 参考目标），参考达标（报告 verdicts 登记 pass=true，但**不构成门禁**——冷启动组断言面＝样本纪律＋零无效，P95 超限亦只登记）。

手工复核：升序第 14/15 位为 148.677/150.317；h=13.3 → P95＝148.677＋0.3×(150.317−148.677)＝149.169 ✓。

### 4.3 观测记录（如实登记）

- **冷启动组读数与热启动组同量级（~150 ms）**：无头口径下冷启动的进程级重置成本（子进程启动＋DLL 加载＋符号绑定）不在测量区间内（区间＝子进程内 open＋HEAD 首读），且子进程不携带 gtest/测试目标载荷——两组打开协议成本同源（同一打开路径、同一磁盘布局）。真实 RobWorkStudio 宿主冷启动（插件加载＋Qt 资源解压＋框架初始化在区间内/前置的形态）归集成模式复测挂点（§7）。
- 试跑轮（同构建、无 gitCommit 锚——未入证据链）热启动组读数 ~293 ms：同机背景负载波动的真实观测，如实登记——两组样本均为完整测量（1+15＋零无效），首测以 §4.1/§4.2 锚定轮为准。

## 5. 无头等价口径与边界（如实申报）

1. **"项目打开"＝`ProjectStoreFactory::open` 完整协议＋HEAD 首读**：②目录形态与版本＋③装载读校验（HEAD 闭包完整性 SHA-256——闭包全部修订清单引用并集）＋⑤孤儿草稿扫描＋Writable 锁获取与恢复扫描；UI 会话激活/项目树呈现/模型编译面在口径外（PM-16 测量的是项目加载时长——PM-16 与 NFR-PERF-01 为不同测试，需求原文括注）。
2. **冷启动＝全新子进程承载进程启动＋DLL 加载＋RobWork/ird 符号绑定**；RobWorkStudio 插件加载与 Qt 资源解压面在无头口径外（插件宿主未启动）——协议 §4.4 明文冷启动受进程启动、插件加载与框架初始化支配，插件面缺失使本组软指标读数低于真实 Studio 冷启动，**登记为口径边界而非达标依据**（软指标不作门禁的边界一致性）。
3. **黄金项目经真实产品命令管线生成**（submit→七步提交协议→只增发布→HEAD 切换），打开测量消费的是产品持久化真磁盘布局——非预录数据；生成器参数冻结于 manifest（升级换版本）。
4. **实现口径观测（不作规模判据，归 product 侧澄清）**：当前产品实现的修订清单 objectRefs＝元数据＋本修订计划对象（TxEngine 装配口径），HEAD 视图引用数＝11（10 数据＋1 元数据）；打开的完整性校验面＝闭包全部修订清单引用并集（~1,111 对象 SHA-256——真实打开成本的组成部分）；与 units/project.md §4.4.2"全量状态闭包"设计语义的关系由所有者/product 侧裁决，本基准不私裁——已如实登记于 manifest `implObservation` 与测量目标文件头。

## 6. 复现命令（冒烟模式——协议 §4.8"仅凭该命令＋登记的环境前提可复现"）

```bash
# ①冒烟配置（AGENTS §4.1 口径——vcpkg toolchain＋Qt 前缀必带）
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
# ②构建基准目标（连带冷启动采样子进程 sdurws_ird_project_openbench_worker）
cmake --build <冒烟目录> --config Release --target sdurws_ird_project_perf_test
# ③设报告目录后经 ctest 运行（或直跑 exe——两形态同判）
#   Windows PowerShell：$env:IRD_PERF_REPORT_DIR="<证据目录>"（可再设 IRD_PERF_GIT_COMMIT=<SHA>）
ctest --test-dir <冒烟目录>/project -C Release -R "^sdurws_ird_project_perf_test$" --output-on-failure
```

**达标判读**：两用例全绿（ird-perf-b04-golden-project@1——热启动门禁 verdict pass＋两组 15 有效零无效＋规模恒等式/修订装载核对通过）＋逐例报告 JSON（`<证据目录>/b04-warm-open.json`、`b04-cold-open.json`：原始样本序列、p95Convention、阈值判定留痕 thresholdSource＝REQUIREMENTS PM-16）即 B-04 达标。

## 7. 复测挂点与边界

- **集成构建由合入批执行**（AGENTS §6.4 三段式——实施段不操作集成构建树）；集成模式复测建议挂点：合入批在集成树（RWS_BUILD_INDUSTRIALROBOT=ON）重跑 §6 命令（目标与 ctest 注册同名可达）；真实 Studio 宿主形态的冷启动（插件面在口径内）如需评估，属集成模式扩展测量——按 manifest 升级规则换版本登记，不在本卡面。
- ird_gates 直跑核定随合入批/治理面补做（预期命中集：project 单元 CMake 新增 perf_test/openbench_worker 两测试面目标——product 目标零变更；LinkageContractTest 增列钉已随本批落位自证）。
- 本基准无两态负载面（§2 统计口径区分行）；NFR-PERF-01 的两态交互面归 B-01/T02，两基准不混。
- HEAD 视图引用集实现口径（§5.4）如经 product 侧澄清发生变化（对齐 §4.4.2 全量闭包语义），打开成本构成将随之变化——届时按 manifest 升级规则换版本复测并留 history（不原地改写）。

## 8. 证据清单（build/wp23-t05-wt/_m5-evidence/）

| 文件 | 内容 |
| --- | --- |
| perf-reports/b04-warm-open.json | 热启动门禁组报告（gitCommit=21a65014 锚——原始样本序列 16 条/p95Convention/verdict thr=3000 pass=true/allPassed=true） |
| perf-reports/b04-cold-open.json | 冷启动软指标组报告（gitCommit=21a65014 锚——原始样本序列 16 条/verdict thr=8000〔不作门禁口径登记于 notes〕/allPassed=true） |
| gtest-reports/sdurws_ird_project_perf_test.xml | gtest XML（2/2 通过——直跑 --gtest_output 落盘） |
| b04-first-direct-run.log | 首测直跑全量日志（含 `[b04-golden] disk-facts headRefs=11 history=101 oidDirs=1101` 磁盘地面事实打点×2〔两用例各一〕） |
| b04-first-ctest.log | ctest 形态复跑留痕（100% passed，17.43 s） |
