# WP-23-T09 确定性复现基准首测报告（B-08）

> **登记性质**：performance-baseline.md §2 B-08 行的首测执行报告（协议 §4.8 留痕面）。
> 本报告只登记测量事实与判定结论，不新增、不收窄任何达标数值；B-08 为**集合等价
> 与容差判定类基准**（§2 统计口径区分明文）——P95/中位数等分位数统计**不适用**，
> 测量面＝逐腿真实运行的集合/顺序判定与逐元素偏差观测。
>
> **执行段**：WP-23-T09 实施段（分支 wp23-t09；三段式——集成构建由合入批执行）。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 逻辑） |
| 内存 | 63.5 GB 可见 |
| OS | Windows 11 专业版 build 26200 |
| 编译器 | MSVC 19.42.34433（VS2022），x64 Release |
| CMake | 4.3.1（Visual Studio 17 2022 生成器） |
| 软件版本 | 分支 wp23-t09 @ cc59c03d（复现用例提交；本报告随收尾提交入库，纯文档零代码） |
| 构建模式 | 独立冒烟模式（AGENTS §4.1：仓库根 build/wp23-t09-smoke，源＝wp23-t09 工作树；vcpkg toolchain＋Qt 6.11.1 前缀） |
| 电源策略 | 移动工作站（报告执行时未单独锁定电源计划——登记为观察项） |
| 后台进程基线 | 常规桌面会话（本基准为集合等价判定类，墙钟时长仅作原始样本留痕、不作阈值判定——时长波动不影响判定） |

## 2. 复现要素与夹具（performance-baseline §5.8 挂点）

- **夹具**：testkit `ReproBenchmark.hpp`（testkit.md §6.3 消费者提案落位）——reference
  腿→rerun 腿（同输入/版本/配置/线程/种子复跑）→threads 腿（仅线程数不同）；
  判定全委托 SetCheck（TK-T06 两阶段匹配）；偏差观测逐元素（|a−b|、|a−b|/|ref|）。
- **容差档案**：`testdata/tolerance/repro-reduction@1.0.0`——附录 D 第 8 项固定值
  （相对 1×10⁻¹²＋ε_abs=0，source=appendixD-fixed，allowedMax 同值零放宽）。
- **确定性上下文**：各用例 ReproRecord{seed=20260909, threadCount=1}；输入全部为
  固定字面量夹具/生成器（无随机数据、无随机对象 id——复现要素齐备）。

## 3. 抽样面与首测结果（真实测量）

执行方式：ctest 驱动（`IRD_REPRO_REPORT_DIR` 落盘，每例一份 JSON 原始报告）。
下表时长为墙钟实测原始值（毫秒——**非阈值判定面**）。

| caseId | 域 | 抽样构成 | 腿（时长 ms / 元素数） | 集合等价 | 稳定排序 | 最大相对偏差 | 逐元素比较数 | 判定 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| kin-batch-thread-equivalence | kinematics | 12 环形任务点×2 工况（SeededRandom n=8、maxBatchSize=8 多批） | reference 4.6904/24；rerun 4.0893/24；threads-8 4.8726/24 | ✓ | ✓ | 0（容差 1e-12） | 176 | **通过** |
| trj-ptp-chain-rerun | trajectory | 8 段 PTP 链（三键选择全路径） | （集成模式执行——见 §4） | — | — | — | — | **待集成复测** |
| dyn-inverse-rerun | dynamics | 二连杆×40 正弦采样逆动力学（80 行样本序） | reference 0.1129/80；rerun 0.0753/80 | ✓ | ✓ | 0 | 80 | **通过** |
| sel-motor-screening-rerun | selection | 6 电机×3 轴硬筛选（候选×轴 18 记录＋原因稳定序） | reference 0.0606/18；rerun 0.0283/18 | ✓ | ✓ | 0 | 144 | **通过** |
| opt-seeded-lhs-rerun | optimization | seeded-lhs 24 候选批（7 个 R1 词表绑定） | reference 0.1089/24；rerun 0.0972/24 | ✓ | ✓ | 0 | 161 | **通过** |

原始样本序列：`build/wp23-t09-wt/_m5-evidence/repro/{caseId}.json`（逐腿时长/元素数、
判定计数、偏差观测、确定性上下文——机器可读全量留痕；对应测试二进制＝
`build/wp23-t09-smoke/<unit>/Release/sdurws_ird_<unit>_test.exe`）。

## 4. 范围限定与如实登记

1. **trajectory 域用例集成模式执行**：planPtpSegment 消费 `rw::math::Q`（非模板类，
   链接需框架库符号）——用例随 `if(TARGET sdurw_kinematics)` 条件组（与
   PtpSequenceTest 同 gating），冒烟模式不编译。已做**独立编译验证**（MSVC cl
   语法/类型零错误——vcvars64＋全单元 include 面）；真实执行待集成构建
   （**集成构建由合入批执行**——实施段如实标注）。集成复测判定＝同命令跑
   `sdurws_ird_trajectory_test` 后 `TrjReproBenchmark.*` 全绿＋报告 JSON 落盘。
2. **线程等价腿的适用面**：仅 kinematics 存在 threadCount 执行参数（§8.4 并行分片
   ——本基准的并行归约容差载体）；optimization R1 声明 threadCount 恒 1 且进
   config 身份（改线程数＝改输入非执行参数）、trajectory/dynamics/selection 为纯
   串行计算（无随机种子/无并行分片）——各域夹具按规格如实裁剪腿数，不虚设。
3. **同线程复跑观测偏差＝0**：各域确定性实现（全序槽位写回/纯函数/逐样本独立
   递推）实测位级一致，**优于**附录 D 第 8 项 1e-12 容差——观测值如实记录（0），
   非判定松弛。
4. **首次 ctest 瞬时失败登记**：冒烟树首次 ctest 执行出现一次
   `sdurws_ird_testkit_test` 瞬时失败（测试名未及捕获——输出仅留摘要）；其后 6 轮
   完整 ctest 全绿（含本报告证据轮）未复现。疑为 Windows 首次执行杀扫干扰（本机
   常见一次性开销形态）；如实登记，不带病粉饰。验收段如复现，按验收协议处置。
5. **本报告与集成构建的关系**：本报告为首测（冒烟模式四例）＋集成复测挂点；
   集成模式五例全绿后由验收段对 B-08 达标作最终判定。

## 5. 证据清单（build/wp23-t09-wt/_m5-evidence/）

| 文件 | 内容 |
| --- | --- |
| `repro/kin-batch-thread-equivalence.json` | kinematics 域报告（3 腿——含 threads-8 并行归约腿） |
| `repro/dyn-inverse-rerun.json` | dynamics 域报告（2 腿） |
| `repro/sel-motor-screening-rerun.json` | selection 域报告（2 腿） |
| `repro/opt-seeded-lhs-rerun.json` | optimization 域报告（2 腿） |
| `gtest-xml/sdurws_ird_testkit_test.xml` | testkit 套件 XML（106 用例 0 失败——含 ReproBenchmark 元测试 5 例） |
| `gtest-xml/sdurws_ird_kinematics_test.xml` | kinematics 套件 XML（179 用例 0 失败——含域复现用例 1 例） |
| `gtest-xml/sdurws_ird_trajectory_test.xml` | trajectory 套件 XML（34 用例 0 失败——smoke 子集，复现用例集成面） |
| `gtest-xml/sdurws_ird_dynamics_test.xml` | dynamics 套件 XML（112 用例 0 失败——含域复现用例 1 例） |
| `gtest-xml/sdurws_ird_selection_test.xml` | selection 套件 XML（285 用例 0 失败——含域复现用例 1 例） |
| `gtest-xml/sdurws_ird_optimization_test.xml` | optimization 套件 XML（173 用例 0 失败——含域复现用例 1 例） |
| `ctest-smoke-20261013.log` | 冒烟树 ctest 留痕（六单元 11 测试全绿） |
| `env-20261013.txt` | 环境记录（§1 的采集源） |
