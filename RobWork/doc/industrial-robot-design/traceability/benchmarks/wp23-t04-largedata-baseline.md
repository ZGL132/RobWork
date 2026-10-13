# WP-23-T04 首测报告——B-03 大规模数据加载/保存/分页基准（NFR-PERF-03）

> 任务：WP-23-T04（DTB §2.24 行）｜基准行：performance-baseline §2 B-03｜分支 wp23-t04｜软件版本锚：git commit `8045e331`（实现 tip——测量目标与 kinematics 优化落位后）＋构建配置 Release（冒烟模式）
> 测量口径：B-03 为**规模/功能类判定**（performance-baseline §2 统计口径区分行明文——不涉及分位数统计），需求无数值时延阈值；本报告计时数据全部为原始样本统计留痕（testkit PerfBenchmark 夹具——每组 1 次预热剔除＋15 有效样本，禁 3 次中位数替代的 P95 约定不适用/不消费），达标判定面＝三类数据操作全部成功＋规模恰为需求原文＋"不一次性把全部明细装入界面"结构不变量＋零无效样本。

---

## 1. 达标结论

**达标（三腿九组全绿，全部判定面通过）**——5,000 任务点／100,000 采样结果／10,000 候选摘要的加载、保存、分页浏览全部成功；规模与往返核对逐项钉住；"不整体装入界面"结构不变量全程成立（单页物化恒 ≤100 行、全量翻页累计恰为 N、页数恰为 ⌈N/100⌉）；27 个计时组样本（9 组×15 有效）零无效。

| 达标口径（照抄需求原文语义） | 判定 | 证据 |
| --- | --- | --- |
| 成功**加载**、**保存**三类数据 | ✅ 三腿保存/加载组全绿（canonical/归档/CSV 全链＋完整性核对） | 逐腿"无锚定校验腿"全量等值/完整性核对＋15×3 组零无效样本 |
| **分页浏览** | ✅ 50/1,000/100 页全量翻页，累计物化 5,000/100,000/10,000 行 | 三腿 page 组结构不变量断言 |
| **不一次性把全部明细装入界面** | ✅ 单次投影物化行数恒 ≤ 页大小 100（manifest 冻结窗口上界） | pageThrough 观测账本 maxWindow 断言 |
| 规模恰为 5,000/100,000/10,000（零增减） | ✅ 夹具构造即断言＋加载往返计数核对 | 三用例 ASSERT_EQ 规模钉 |

## 2. 硬件与环境（协议 §4.1 清单）

| 项 | 值 |
| --- | --- |
| 机器 | ZGL（单机采集——同一轮基准全部样本同机同配置） |
| CPU | 13th Gen Intel(R) Core(TM) i9-13950HX（24 核 32 线程；报告 cpuInfo 字段登记 PROCESSOR_IDENTIFIER 原文） |
| 内存 | 64 GB（63.5 GB 可见——DDR5 SODIMM） |
| 存储 | SSD（SK hynix PC801 HFS002TEJ9X101N NVMe——归档/CSV 落盘通道） |
| OS | Microsoft Windows 11 专业版（Windows_NT AMD64） |
| 电源策略 | 交流供电桌面常态（未做节流干预；软指标参考面如实登记） |
| 后台进程基线 | 空闲态（无后台评估任务——B-03 无两态负载面，§2 统计口径区分行） |
| 软件版本 | commit `8045e331`（wp23-t04 tip）＋Release 冒烟构建（MSVC 2022 x64，/O2 优化） |

## 3. 数据集（ird-perf-b03-largedata@1）

manifest＝`testdata/performance/b03-largedata-manifest.json`（参数冻结，升级换版本——testkit.md §4.2.1）。三类数据构成：

| 数据类别 | 规模（需求原文） | 生成器与持久化面 |
| --- | --- | --- |
| 任务点 | 5,000 | requirements TaskPoint 确定性内容（位置/容差 SI，工艺标签轮转，约 1% 停用）；保存＝RequirementCodec canonical 编码＋io 原子文件写出；加载＝字节读回＋解码＋全量往返等值 |
| 采样结果 | 100,000 | 产品确定性生成器真跑：generateSampleSet（单计划 Grid 100×100×1＝10,000 位置＋姿态 9×1＝90,000 位姿）＋computeCoverage＋逐样本状态表（Unreachable 17 倍数／DataInsufficient 19 倍数〔非 17〕／其余 Reached）；保存＝encodeRegionCoveragePayloadCanonical（IRDCV01）＋project 归档端口 begin→writeBatch→finalize；加载＝listRuns/runDir 产品编址读回＋SHA-256 完整性核对＋冻结布局独立最小解析 |
| 候选摘要 | 10,000 | 产品工厂 assembleRunResult（Quick 9,000 筛选批在前＋Verified 1,000 正式批；补丁逐候选取异——候选身份互异）；保存＝OptimizationExportProvider 六工件数据面组装＋工件 2 候选 CSV canonical 写出（21 列字段契约 v1）；加载＝io ICsvReader 单遍流式回读（行回调，峰值内存＝当前行） |

## 4. 无头等价口径与边界（如实申报）

1. **分页＝消费侧分页窗口投影的模型层等价**：三类数据的"分页浏览"以页窗口投影承载（每页物化至多 100 行明细——任务点呈现行/逐样本状态行/候选摘要行），Widget 重绘与面板组件在口径外（WP-23-T02 先例同款边界——ui.md"结果分批到达"行：明细面板分页加载，不一次性装全量；生产面板消费同源数据面）。
2. **任务点保存面＝canonical 编码＋原子文件写出**（项目对象库持久化的编码面）；项目命令管线（draft/apply 修订提交）与项目级打开归 B-04（PM-16）测量面，不在本基准。
3. **采样载荷加载面＝冻结布局 IRDCV01 独立最小解析**（conformance 面）：kinematics 公共面只有编码无解码（Coverage.hpp 落位范围口径），本解析仅存在于测试目标、不入产品库；解析结果与产品 computeCoverage 计数分段交叉核对互证（位置轴/位姿轴 U/DI 计数逐轴一致——两面同源互证通过）。归档字节完整性以 manifest 条目 SHA-256（core::ContentDigester）核对。
4. **候选摘要保存面止于六工件数据面组装＋工件 2 候选 CSV 写出**；工件 6 候选包的产品写出适配器随 P-OPT-2 裁决落位（optimization.md Export.hpp 落位范围口径——R1 交付面为测试替身），本基准不测量替身 I/O。

## 5. 测量数据（原始样本序列全量见 perf-reports/ JSON——不得只报统计值）

### 5.1 优化后基线（commit 8045e331，三腿九组，每组 1 预热＋15 有效零无效）

| 腿 | 操作组 | P95（ms） | 有效样本最大（ms） | 备注 |
| --- | --- | --- | --- | --- |
| A 任务点 | taskpoint-save | 29.41 | 36.43 | canonical 编码 5,000 条＋原子写出 |
| A 任务点 | taskpoint-load | 84.95 | 85.51 | 读回＋解码 5,000 条 |
| A 任务点 | taskpoint-page | 5.32 | 5.50 | 50 页×100 行全量翻页一遍 |
| B 采样结果 | sampleresult-archive-save | 124.10 | 128.82 | 载荷编码＋归档三步（新运行/次） |
| B 采样结果 | sampleresult-load | 68.36 | 68.54 | listRuns＋读回＋解析 100,000 条 |
| B 采样结果 | sampleresult-page | 2.59 | 2.59 | 1,000 页×100 行全量翻页一遍 |
| C 候选摘要 | candidate-export-save | 479.64 | 496.75 | 六工件数据面组装＋CSV 10,000 行写出 |
| C 候选摘要 | candidate-load | 41.49 | 43.33 | 单遍流式回读 10,000 行 |
| C 候选摘要 | candidate-page | 16.51 | 17.16 | 100 页×100 行全量翻页一遍 |

结构不变量逐腿核对：A 50 页/5,000 行/单页最大 100；B 1,000 页/100,000 行/单页最大 100；C 100 页/10,000 行/单页最大 100（报告 JSON notes 字段同步留痕）。B 腿附加完整性证据：归档可见运行 17 个（1 指定＋16 保存组，全部经 finalize manifest 发布——D-13 完整性判据）；解析状态表与产品覆盖率计算分段交叉核对一致。

**计时为留痕不作判定**——B-03 无数值时延阈值（零私设数值纪律），上表仅作规模化成本基线与后续优化对照。

### 5.2 优化前实测对照（真实测量——预优化探针，commit c8606037 树）

首个可运行版本（编码对齐回查为逐结果线性扫描——O(N²)）在同一冒烟树实测（15 有效零无效，原始样本序列 `_m5-evidence/perf-reports-preopt/b03-sampleresults.json`）：

| 操作组 | P95（ms） | 有效样本最大（ms） | 优化后 P95 | 降幅 |
| --- | --- | --- | --- | --- |
| sampleresult-archive-save | **9,011.87** | **9,301.49** | 124.10 | ~73× |
| sampleresult-load | 74.78 | 78.19 | 68.36 | ~9% |
| sampleresult-page | 2.91 | 3.08 | 2.59 | （同量级） |

**优化登记（DTB 本行输出列"基准执行与分页优化"的本面兑现）**：`kinematics/src/Sampling.cpp` encodeRegionCoveragePayloadCanonical 的对齐回查由逐结果线性扫描（每条从头扫至命中——约 5×10⁹ 次比较/次编码，100,000 规模单次编码 ~9 s，NFR-PERF-03 量级不可行）改为先建 sampleIndex 哈希索引再 O(1) 对齐（总体 O(N)）——编码输出字节逐位不变（归档字节 SHA-256 与 manifest 逐位一致核对），双射破坏 fail-fast 语义零变化。优化提交 07dd0113；受影响套件全绿（kinematics 178＋1 按设计跳过／contract 57/57）。

## 6. 复现命令（冒烟模式——协议 §4.8"仅凭该命令＋登记的环境前提可复现"）

```bash
# ①冒烟配置（AGENTS §4.1 口径——vcpkg toolchain＋Qt 前缀必带）
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
# ②构建基准目标
cmake --build <冒烟目录> --config Release --target sdurws_ird_ui_largedata_perf_test
# ③设报告目录后经 ctest 运行（或直跑 exe——两形态同判）
#   Windows PowerShell：$env:IRD_PERF_REPORT_DIR="<证据目录>"（可再设 IRD_PERF_GIT_COMMIT=<SHA>）
ctest --test-dir <冒烟目录>/ui -C Release -R "^sdurws_ird_ui_largedata_perf_test$" --output-on-failure
```

**达标判读**：三用例全绿（ird-perf-b03-largedata@1——规模/往返/分页/结构不变量四面断言）＋逐例报告 JSON（`<证据目录>/b03-*.json`：原始样本序列、判定口径注记、结构观测账本）即 B-03 达标。

## 7. 复测挂点与边界

- **集成构建由合入批执行**（AGENTS §6.4 三段式——实施段不操作集成构建树）；集成模式复测建议挂点：合入批在集成树（RWS_BUILD_INDUSTRIALROBOT=ON）重跑 §6 命令（目标与 ctest 注册同名可达）。
- ird_gates 直跑核定随合入批/治理面补做（预期命中集登记册＝`traceability/wp23-t04-gate-registrations.md`）。
- 本基准无两态负载面（§2 统计口径区分行——B-03 为规模/功能类）；NFR-PERF-01 的后台态交互面归 B-01/T02，两基准不混。
- 候选摘要保存面在 P-OPT-2（候选物化通道）裁决与工件 6 产品写出适配器落位后如需扩展测量，按 manifest 升级规则换版本登记（不原地改写）。

## 8. 证据清单（build/wp23-t04-wt/_m5-evidence/）

| 文件 | 内容 |
| --- | --- |
| perf-reports/b03-taskpoints.json ＋ b03-sampleresults.json ＋ b03-candidates.json | 首测逐组报告（gitCommit=8045e331 锚——原始样本序列/结构观测/判定口径注记，allPassed=true×3） |
| perf-reports-preopt/b03-sampleresults.json ＋ b03-taskpoints.json | 优化前探针对照（save 组 P95 9,011.87 ms——§5.2 数据源） |
| gtest-reports/sdurws_ird_ui_largedata_perf_test.xml ＋ sdurws_ird_kinematics_test.xml ＋ sdurws_ird_kinematics_contract_test.xml ＋ sdurws_ird_ui_contract_test.xml | gtest XML（3/3＋178+1skip＋57/57＋27/27） |
| b03-first-direct-run.log ＋ b03-first-ctest.log ＋ b03-preopt-direct-run.log | 首测直跑/ctest 留痕＋预优化探针直跑留痕 |
