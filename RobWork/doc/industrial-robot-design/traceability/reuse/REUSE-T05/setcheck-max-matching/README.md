# testkit SetCheck 最大匹配能力复用——任务记录

- 分支 `codex/reuse-testkit-setcheck`（基线 `691cbd42e`＝redesign-main 顶端，F-958 已合入；旧分支 codex/reuse-testkit-listener 与 build-int-lsn 保留）
- 任务编号：**由协调者统一登记**（指令明确不复用 REUSE-T18——该编号已用于 F-958 收口，见 tasks/REUSE-T18.json）
- 状态四态：实施完成 ✅／定向与消费者回归通过 ✅／**治理门禁通过 ✅（PASS 实际边界见 §8.4——governance-only，非算法合规）**／**独立验收未完成 ❌**（所有者另开会话，不自签）

## 1. 复用来源（上游检索与 Boost 候选分开陈述）

**上游 RobWork/RobWorkSim**：本包独立检索 `bipartite|maximum_cardinality|max_cardinality|hopcroft|augmenting path` 于 `RobWork/src`＋`RobWorkSim/src` 零命中；宽松 `matching` 命中均为任务增广/加载器/插件仓库命名，无公共二分图最大匹配 API。与审计 B17 结论一致（"未找到已读公共二分图最大匹配 API，但零命中不证明所有候选无能力"）。

**Boost 候选**：同一 vcpkg 渠道**已装** Boost 1.92（BOOST_LIB_VERSION 1_92；`boost/graph/max_cardinality_matching.hpp`＋`share/boost_headers/` 在位）——零 vcpkg 变更、零新渠道。消费的两个公开 API：
- `boost::checked_edmonds_maximum_cardinality_matching`（算法内部自验配对合法且基数最大）；
- `boost::matching_size`（计数正交源，与转表计数互检）。

既有证据（**仅为候选可运行证明，非生产准入**）：probe-r17/candidates 2 探测 passed（贪心陷阱唯一完美匹配＋删边区分替代配对）、MatchingSuitabilityTest.cpp（Boost::headers 消费形态）。本包补齐的正是"候选→生产接入"缺环。

## 2. 适配判断与边界（§8.1 补正后表述）

**语义等价**：两算法同为最大基数匹配——等价结论、缺失/多余**计数**逐 case 相同；§8.2 规模对照中十组随机案例（n≤4096，稀疏/中密/稠密）两实现匹配基数逐案例一致。多解情形的**具体配对见证**契约允许范围：§5.4.2 钉住等价/基数/计数，**任意一个最大匹配都是合法见证**——同版本内 adjacency_list vecS 构造序＋无随机源 ⇒ 同输入同见证（`MultiSolutionWitnessDeterministic_UT_SET` 钉住）；**跨实现/跨版本见证不可比**（Kuhn 与 Edmonds 在多解输入上可能选不同配对），同一版本重复稳定**不能**自动证明跨版本报告一致。消费者面核查（12 文件）：全部只断言等价/计数/空性，无配对索引钉 → 零破坏面。§5.4.2 契约明示算法自由（"Hopcroft–Karp 为复杂度期望，非算法强制"；v0.6 实现取舍注）。

**业务层保留范围（精确表述）**：阶段 0 重复检测、阶段 1 身份优先匹配、`detail::compatible`（C4 经 profile.resolve＋core::closeWithin）、F-594 交错圈歧义检测、mismatchedPairs 最近对详情、100,000 护栏、checkStableOrder——**本包 diff 中未修改**（checkStableOrder 区间哈希 `c2bf1998…` 与基线逐字复算一致）。但阶段 2 代码是**重写而非原样**：相容边从谓词惰性求值改为按索引物化（求值 O(nL·nR)，与原谓词扫描同阶），并新增 mate→matchR 反推步；文件头实现取舍注同步改写。

**显式图边界与内存（§8.2 实测修正）**：物化 |E| 相容边＋Boost 邻接结构＝**O(E) 新增存储，是本包引入的内存行为变化**（解析口径：16B/边＋≈(V+2E)×8B；n=1024 稠密 ≈12.8MB、n=4096 稀疏 ≈10.3MB；基线为 O(V) 状态）。时间面（§8.2 实测）：稀疏相容图（容差约束的真实输入形态）新实现占优且差距随规模扩大；稠密相容图新实现因全对物化而慢于旧惰性求值（最高 220×@n=1024）——§8.2.4 给出最小适配提案，未实施。收益＝删除自写递归内核＋库侧最大性自验＋稀疏场景实际加速，非性能承诺。

## 3. 实际删除/新增内容

删除：`detail::tryAugmentAt`（Kuhn 递归增广/回滚，34 行）＋其驱动循环与 visited 分配（约 10 行）——**无双实现、无 fallback**。
新增：
- `SetCheck.hpp`：`detail::maximumBipartiteMatching(nL, nR, edges)` 接缝声明（纯索引类型，Boost 不出公共头）；阶段 2 改为相容边物化（业务语义留模板层）→ 接缝调用 → mate 反推 matchR（F-594/缺失多余推导消费该表，推导循环本身未修改）；实现取舍注更新。
- `src/SetCheckMatch.cpp`（新 TU，Boost 唯一编译面）：图构造（左 [0,nL)／右 [nL,nL+nR)）→ checked Edmonds（自验失败 Usage fail-fast）→ matching_size 互检 → matchL 转表。
- `CMakeLists.txt`：find_package(Boost CONFIG REQUIRED)＋源登记＋`Boost::headers` PRIVATE（接口目标，零运行库/DLL）。
- `SetCheckTest.cpp` 新增 4 用例（见 §4）。

## 4. 验证结果（分列；既有证据直接引用，未重跑 F-958 套件/全库）

| 验证 | passed | failed | skipped | 退出码 | 证据 |
| --- | --- | --- | --- | --- | --- |
| sdurws_ird_testkit_test 全量 Release（115 既有＋新增 4） | 119 | 0 | 0 | 0 | `setcheck-run-release/` |
| sdurws_ird_testkit_test 全量 Debug（CRT 退出路径） | 119 | 0 | 0 | 0 | `setcheck-run-debug/` |
| 消费者回归（SetCheck 直接消费单元）：dynamics 116／selection 292＋1 skip※／optimization 214＋1 skip※／optimization_contract 52／kinematics 288＋1 skip※／trajectory 189／policy 211 | 全绿 | 0 | 3（既有 GUI envUnavailable 用例，非本包引入） | 各 0 | `setcheck-run-consumers/` |
| ird_gates（一次真实运行，2026-10-23，登记前实况） | 红线引擎自测全按预期；真实扫描**首个阻断＝`IRD-GATE-LIB`：`sdurws_ird_testkit` 链接未知目标族 `Boost::headers`**（第三方词表未登记——§5 登记项）；validate-reuse 段未执行（同 F-958 先例，凭证整合前不重复运行） | — | — | — | exit 1，`ird-gates-setcheck.log`（控制台混合编码，ASCII 命中码可辨） |

新增 4 用例：①`SetMatchCore.ExhaustiveBruteForceAgreement_UT_SET`——内核穷举 oracle（3×3 全部 512 种边集＋4×5 确定性伪随机 240 例，独立精确最大基数对照，配对合法性/互异性/基数）；②`SetMatchCore.GridFamilyTemplateSemantics_UT_SET`——生产入口全链路网格族 oracle（15625 组 3×3 非传递相容形态：等价/基数/缺失多余计数/歧义 flag 对独立穷举判据，含真歧义形态）；③`SetNumeric.GreedyTrapAugmented_UT_SET`——贪婪陷阱需增广形态；④`SetNumeric.MultiSolutionWitnessDeterministic_UT_SET`——多解见证逐次稳定。oracle 不与被测内核共享代码路径；F-594 双向回归（唯一不误报/真歧义不漏报）随套件通过。

性能口径（§8.2 补正）：~~套件墙钟 19.2s 量级与替换前持平~~——删除该表述（跨套件版本、跨用例集的墙钟对比不成立，本包未做同条件替换前后测量）。规模/内存的受控对照见 §8.2（独立探针，best-of-3，同案例集两实现对照）；护栏语义不变（>100k Usage 由既有用例钉住）。

## 5. 治理补齐片段（交协调唯一修改者；本包未编辑共享文件）

**依赖契约**：testkit.md §3.1 行"不引入 Boost 等第三方运行库"需随包修订为"不引入第三方运行库；header-only 编译期使用（现仅 Boost::headers——SetCheck 匹配内核，单 TU 收敛）经登记允许"＋§9 版本行登记。ird_gates `IRD_THIRDPARTY_ALLOWED_TARGETS` 增列 `"Boost::headers"`（现词表 libzip/expat/pugixml/Eigen3）＋DTB §4.5 登记册同步行。catalog/permissions：能力与权限面无变化（usageRole/permissionRef/capabilityId 均保持原值），**不需变更**。

**changes/<新编号>.json**（编号待登记）：files＝SetCheck.hpp（adapter，sha 7b983374…）、src/SetCheckMatch.cpp（adapter，sha 4cf78053…）、CMakeLists.txt（business，sha 334f1197…）、SetCheckTest.cpp（business，sha 72999190…）；verify＝本记录。

**functions/testkit.json 刷新**（区间惯例＝template 行→闭括号，checkStableOrder 哈希逐字复算验证）：

| 条目 | 处置 |
| --- | --- |
| SetCheck.tryAugmentAt（121-151，3c327665…） | 删除（内核被替代） |
| SetCheck.maximumBipartiteMatching（新增） | sources＝SetCheck.hpp:134-136（b97ff334…）＋src/SetCheckMatch.cpp 全文件（4cf78053…）；differences＝"Boost checked Edmonds＋matching_size 薄适配（probe-r17 候选生产接入）；物化 \|E\| 边、业务语义留模板层" |
| SetCheck.checkSetEquivalent | 162-392（974b636b…）→ **147-380**（1fc53e6b…） |
| SetCheck.compatible | 99-116 → **105-122**（内容逐字未变，哈希不变） |
| SetCheck.checkStableOrder | 394-435 → **382-423**（哈希 c2bf1998… 不变，已复算验证） |
| MatchPair.operator==/!=、SetMatchTraits.identity/numerics | 区间＋6 平移（文件头注 +6 行），内容逐字未变，哈希不变 |
| 各条目 inputs 中 SetCheck.hpp/CMakeLists 旧 pin | 机械失真：SetCheck.hpp→7b983374…、CMakeLists.txt→334f1197…；由协调者按批次裁决 |

**tasks/<新编号>.json 建议要点**：unit=testkit、dependsOn GOV-T21/T22、designRefs=本记录＋compare-set-dataset-audit.md＋probe-r17、verify=validate-reuse.ps1 -TaskFile、allowedFiles＝§3 四文件。

## 6. 未完成事项

1. ~~ird_gates 未转绿~~ **已转绿**（§7 协调落地：复跑 exit 0）。
2. ~~任务编号、testkit.md §3.1/§9 修订、functions/inventory 刷新~~ **已完成**（§7 协调落地：REUSE-T19 登记）。
3. **§8.3 复用登记修正（adapter 表达＋正向证据绑定）待协调落地**（本轮新识别，见 §8）。
4. **§8.2.4 稠密退化最小适配提案待裁决**（本轮新识别，未实施）。
5. 独立验收未开始（所有者另开会话；本记录不构成自签）。
6. 范围声明：本包仅覆盖 SetCheck 阶段 2 匹配内核一项复用，**不得扩大为整个 testkit 迁移完成**（JSON 解析/摘要等审计遗留候选仍各自待核）。

## 7. 协调落地（2026-10-23，共享治理唯一修改者）

**任务登记**：精确能力子任务 **REUSE-T19**（`tasks/REUSE-T19.json` ready＋`reuse/changes/REUSE-T19.json` 5 文件凭证——§5 四产品文件＋白名单）。§5 原拟 4 文件未含白名单：本批 `IRD_THIRDPARTY_ALLOWED_TARGETS` 增列 `Boost::headers`（＋注释 4 行）即白名单产品面改动，须凭证（ird_gates 首跑 LIB 命中的登记本体）。catalog/permissions 确认无真实变化未改。

**依赖契约**：testkit.md §3.1 行按 §5 建议修订（header-only 编译期使用例外）＋§9 v1.7 版本行；DTB §4.5 增 LIB 行（Eigen3 先例；ird-gates-setcheck.log 首跑恰 1 项 IRD-GATE-LIB 命中留证）。

**functions/testkit.json**：按 §5 处置表逐项落地——tryAugmentAt 删除；maximumBipartiteMatching 新增（SetCheck.hpp 134-136＋SetCheckMatch.cpp 1-88 双源）；compatible 105-122（哈希复算与原值逐字一致）；checkSetEquivalent 147-380／checkStableOrder 382-423／MatchPair＋Traits ＋6 平移全部复算吻合；§5.3 机械 pin 批量刷新（CMakeLists bdf5362e→新值 100 处、SetCheck.hpp 316a6239→新值 31 处、SetCheckTest.cpp f5fde51c→新值 7 处）。COVERAGE 规则（§5 未预见的机器面）：被改四文件须全行矩阵覆盖——SetCheck.hpp 补 6 段、SetCheckMatch 由 maximumBipartiteMatching 全文件源承载、SetCheckTest 按用例块 9 段、CMakeLists 7 行区间/哈希重排；被改文件覆盖行按 F-958／diagnostics 先例登记 `migrated＋business-specific＋NONE＋nonOverlapEvidence`（绑定 setcheck-run-release/gtest.xml，非closed）。foundation-governance 白名单 10 行区间/哈希随本批白名单增列重算。

**哈希口径**：§5 的 4 文件 sha256 为原始字节口径（7b983374/334f1197/72999190 与 raw sha256sum 逐字吻合）；登记值按校验器 Digest 规则（CRLF→LF 归一）重算——SetCheck.hpp c882aeda…、SetCheckMatch.cpp 4cf78053…（两口径同值）、CMakeLists.txt ae543743…、SetCheckTest.cpp 93733df4…。区间哈希（b97ff334/1fc53e6b/c2bf1998 等）经本批复算全部吻合。

**门禁终态**：`cmake --build build-int-lsn --config Release --target ird_gates` 复跑 **exit 0**——红线 11 类零命中（首跑 IRD-GATE-LIB 消解）＋validate-reuse 全量 PASS（inventory=1401、changed=69），日志 `ird-gates-setcheck-run2-final.log`。既有定向/消费者回归证据（§4 表）按增量口径原样引用（本批治理登记零产品代码变化）。F-958 套件与消费者面未重跑。

## 8. 补正轮（REUSE-T19 收口，2026-10-24，受检提交 354b9bebf）

### 8.1 生产正向证据——§6 断开/扰动实验（本轮补齐）

审计契约 §6 要求 direct/adapter 证明"业务入口 → 上游调用 → 实际消费返回值"，且在隔离测试中断开目标上游调用或扰动返回值时相关业务断言必须失败；测试通过的 XML 单独不构成"不重叠"证明。此前缺该实验，本轮补齐（一项）：

**设计**：对生产接缝 `detail::maximumBipartiteMatching` 做一次性断开扰动——函数入口直接返回全未匹配（Boost 调用不执行、返回值被空匹配替换），经同一构建树重建生产测试目标 `sdurws_ird_testkit_test` 全量运行；随后还原源码、重建、复跑。扰动版**未提交**（产品树保持单实现；无生产追踪接口、无双路径——实验仅留痕）。

**结果（`adapter-evidence/red-disconnect/`）**：
- 断开运行（console-trimmed.log，exit 1；原始 console.log 405,977 行/15.7MB sha256 `84da9108…` 与逐组失败流 gtest.xml 35MB 因 15625 组重复失败流体量过大不入库——裁剪版含失败总清单并注明复现路径； trimmed sha256 `82159d10…`）：119 用例中 **8 项失败，全部为阶段 2 依赖用例**——`SetNumeric.NumericOnlyMatching`（等价与缺失计数双双失败）、`PairwiseSlipperyCaseStillMatches`、F-594 双向回归两用例、`GreedyTrapAugmented`、`MultiSolutionWitnessDeterministic`、`SetMatchCore.ExhaustiveBruteForceAgreement`（内核直测）、`SetMatchCore.GridFamilyTemplateSemantics`。日志中的 `LsnChild.Fails` 字样为父用例内子进程的**预期失败转录**（该父用例本身通过——失败子进程退出码断言不受影响）。
- 未受影响面：阶段 0/1 身份/重复/顺序/护栏/lsnchild 全部用例通过——断开命中的恰为匹配核心依赖面（诊断精度）。
- 还原复跑（rerun-green-console.log＋rerun-green.xml，exit 0）：**119/119 通过**。

**证明力**：生产入口（checkSetEquivalent 阶段 2）的业务断言（等价/见证计数/缺失/多余/歧义）是接缝返回值的函数——Boost 调用断开或其返回被丢弃时业务断言必然失败。配合独立穷举 oracle（正确性依据，独立于两实现）与 Boost 库侧自验/计数互检（库侧校验，**不替代**独立 oracle），构成 §6 完整正向证据。实验绑定 `354b9bebf` 树、生产目标与本测试版本（sha256：断开 trimmed console `82159d10…`、原始 console（裁剪前）`84da9108…`、还原 console `9316bc21…`、还原 xml `7190da9f…`）。

### 8.2 性能与内存对照（本轮补齐，受控规模）

**载体**：`perf-compare/`（独立 CMake 探针；最小产品源集直接编译：SetCheckMatch/ToleranceProfile/JsonLite/Dataset/TestPaths/Compare/Units/Digest——零产品库链接、零上游 RobWork、零 vcpkg 工具链；基线 Kuhn 为 691cbd42e `tryAugmentAt` 逐字快照，measurement-only，无 fallback 语义；不注册产品/测试目标）。两侧共用生产 `detail::compatible` 边语义（profile.resolve＋core::closeWithin 原路径）；计时仅覆盖匹配操作本身（新＝物化＋接缝调用，旧＝谓词驱动驱动循环含逐左点 visited 分配）；best-of-3；LCG 固定种子；密度档位＝容差/值域比（P(|U1−U2|≤t)=2t−t²）。不运行 100,000 节点完全图（指令边界）。

| 案例（n×密度） | 边≈ | 基线 Kuhn | 新实现 | 比值（新/旧） | 匹配基数（两实现一致） |
| --- | --- | --- | --- | --- | --- |
| 64 稀疏(~2%) | 82 | 2.19 ms | 1.72 ms | 0.79× | 37 |
| 256 稀疏 | 1,311 | 47.2 ms | 32.5 ms | 0.69× | 216 |
| 1024 稀疏 | 20,972 | 3,443 ms | 523 ms | **0.15×** | 970 |
| 4096 稀疏 | 335,544 | 137,199 ms | 8,519 ms | **0.06×** | 4068 |
| 64 中密(~10%) | 410 | 6.48 ms | 3.53 ms | 0.54× | 57 |
| 256 中密 | 6,554 | 162.9 ms | 61.9 ms | 0.38× | 256 |
| 1024 中密 | 104,858 | 199.6 ms | 1,021 ms | 5.1× | 1024 |
| 64 稠密(~36%) | 1,638 | 0.64 ms | 5.98 ms | 9.3× | 64 |
| 256 稠密 | 26,214 | 1.97 ms | 97.3 ms | 49× | 256 |
| 1024 稠密 | 419,430 | 6.96 ms | 1,530 ms | **220×** | 1024 |

内存（解析口径，**本包新增面**）：新实现＝edges vector（16B/边）＋Boost vecS 邻接（≈(V+2E)×8B）——n=1024 稠密 ≈12.8MB、n=4096 稀疏 ≈10.3MB；基线＝O(V) 状态（配对表＋visited）。探针进程峰值工作集（粗参照，含进程基线 ≈5MB）：旧 4.9→6.0 MiB 平稳；新 5.0→68.2 MiB 随 E 增长。

**适用结论**：
1. **容差约束的真实输入（相容图稀疏——黄金数据集/容差档案形态）**：新实现占优且差距随规模扩大（n=4096 稀疏 16 倍）；旧 Kuhn 谓词逐次重评估在稀疏大图上不可用（137s）。
2. **稠密相容图（宽容差近退化形态）**：新实现全对物化 O(nL·nR)，旧惰性求值天然跳过已配右点——时间退化最高 220×（n=1024）并新增 O(E) 内存。**该退化是本包引入的实测事实**（替换先前"非本包引入"的不实表述）。
3. **护栏边界**：两实现都无法在 100,000 元素护栏内的病态输入上实用（旧 4096 稀疏已 137s）；契约对大规模数据的既有指引（§5.3 按索引逐对或摘要比较）不变，本包未收窄任何合法输入。
4. **最小适配调整方案（只提案，未实施——按指令不自行改实现）**：阶段 2 增加廉价贪心预检（谓词惰性贪心）——若贪心已达完美匹配则直接返回（完美贪心匹配＝最大匹配，语义等价），否则物化边走 Boost。改动限于 checkSetEquivalent 阶段 2 与接缝内部，不改公开契约、不构成第二匹配内核（贪心仅作"是否已完美"判定）。是否实施由所有者/独立验收裁决；实施须重跑本对照与定向回归。

#### 8.2.1 附：探针过程中的环境事实（如实登记）

探针运行暴露两处与本包产品代码无关的工具链事实：①含 7 层 `..` 的 include 路径串超 MAX_PATH，cmd/cl 拒绝解析（C1083）——CMakeLists 以 `get_filename_component(... ABSOLUTE)` 归一化后构建成功；②ToleranceProfile::load 的目录名/文件名/量纲 token/来源类别校验（profileId 须等于所在目录名、文件名须 `v<版本>.json`、quantityKind 用 kindTokenOf 词表、source 四类别白名单）——探针本地档案按契约改为 `v1.0.0.json`＋`dimensionless`＋`engineering-policy-default`。

### 8.3 复用登记修正（交协调唯一修改者；本包未直接编辑共享文件）

机检依据（reuse-contract.ps1 CheckFunctions）：`capabilityId=NONE` 时 decision 只允许 business-specific/pending/proposal（`NONE cannot claim upstream reuse`）；changes manifest mode=adapter 必须 capabilities 非空且存在于 catalog。**当前登记把真实 Boost 适配记为 business-specific/NONE**（maximumBipartiteMatching 与 checkSetEquivalent 两行、changes 的 SetCheck.hpp/SetCheckMatch.cpp 两文件）——按 audit-contract §4（决策词表/实现定位须含"生产入口、跨层调用和结果消费端"）与 §6（正向证据）应表达为 adapter＋能力绑定。**最小兼容方案＝catalog 增一条能力**（ird-reuse/2 兼容升级，§3 明文"以兼容升级扩充同一 catalog.json，不新建并行能力库"——非另建台账），随后三处绑定：

**(a) catalog.json 增条目（建议片段）**

```json
{
  "id": "MATCH-BIPARTITE-MAX",
  "ownerUnit": "testkit",
  "search": {
    "command": "rg -n \"bipartite|maximum_cardinality|max_cardinality|hopcroft|augmenting path\" RobWork/RobWork/src RobWork/RobWorkSim/src；vcpkg installed boost/graph/max_cardinality_matching.hpp 公开契约阅读；probe-r17/candidates 两项公开 API 探测",
    "baseline": "b12e8503cc1c2a1acc040f506842a02e126b9515",
    "conclusion": "上游 RobWork/RobWorkSim 无公共二分图最大匹配 API（审计 B17 一致）；同渠道已装 Boost 1.92 Boost.Graph checked_edmonds_maximum_cardinality_matching＋matching_size 为选定上游，REUSE-T19 完成生产接入、断开实验与规模对照"
  },
  "upstream": [
    {
      "path": "vcpkg/installed/x64-windows/include/boost/graph/max_cardinality_matching.hpp",
      "sha256": "09c26de2626a0baf4699045569a563440569a7c704f95291adfbae6a3ed9395f",
      "symbols": ["checked_edmonds_maximum_cardinality_matching", "matching_size"],
      "target": "Boost::headers",
      "targetFile": "vcpkg/installed/x64-windows/share/boost_headers/boost_headers-config.cmake",
      "targetSha256": "b1c7e2bab822670e6a760de56b0c0bb3ff2604520141149ff2aeec9860656e28"
    }
  ],
  "build": {
    "targets": ["Boost::headers"],
    "constraints": "header-only 接口目标，零运行库/DLL；Boost 头收敛于 testkit/src/SetCheckMatch.cpp 单 TU（公共模板头与消费者编译面零 Boost）"
  },
  "boundary": "最大基数二分图匹配内核：只承接『给定边表求一个最大基数匹配』；身份优先/C4 相容边构造/歧义检测/缺失多余与诊断归 testkit SetCheck 模板层。多解配对见证由库实现决定——同版本确定性，跨实现/跨版本可不同（契约不钉配对索引）",
  "usages": [
    { "consumerUnit": "testkit", "usageRole": "adapter", "permissionRef": "ARCH:3.6.1", "operations": ["value"] }
  ],
  "review": { "stage": "reviewed", "evidence": "traceability/reuse/REUSE-T05/setcheck-max-matching/README.md", "reviewedVersion": "<协调落地批次 SHA>" }
}
```

**(b) functions/testkit.json 修正（两行）**

- `testkit.SetCheck.maximumBipartiteMatching`：`decision` "business-specific"→**"adapter"**；`capabilityId` "NONE"→**"MATCH-BIPARTITE-MAX"**；`differences` 追加"生产入口链：checkSetEquivalent 阶段 2 物化相容边→本接缝（Boost 公开 API）→返回 matchL 被 matched/missing/extra/歧义探测消费；产品面唯一匹配实现＝本接缝（旧 Kuhn 已于 38290e2bd 删除）——不重叠的结构性依据"；**`nonOverlapEvidence`（现仅 gtest XML，按指令不能单独充当不重叠证明）替换为 `positiveEvidence` 数组**：断开实验 console-trimmed.log（`82159d10…`）＋还原复跑 console.log（`9316bc21…`）＋规模对照 run-old/run-new（`b654ef90…`/`69876b0c…`）；gtest XML 移入 auditEvidence 作测试摘要。
- `testkit.SetCheck.checkSetEquivalent`：**保持 business-specific/NONE**（阶段 0/1、歧义、最近对详情为纯业务规则——按实际职责登记，不要求全改 adapter）；`differences` 更新为："重复/身份优先/交错圈歧义与最近对详情为本功能业务语义；阶段 2 相容边物化后经 detail::maximumBipartiteMatching（adapter 行）承接 Boost 公开 API，matchL 返回值驱动 matched/missing/extra 与歧义探测；不得用普通 set 或排序误替"；`positiveEvidence` 增断开实验两项（业务断言随接缝断开失败＝返回值消费证明）。
- 其余 coverage 行（SetCheckTest 九段、CMakeLists 七段、foundation-governance 白名单行）：**维持现登记不动**（纯测试/构建样板/治理面，按实际职责）。

**(c) changes/REUSE-T19.json 修正（两文件）**

- `SetCheck.hpp`：mode "business"→**"adapter"**＋`capabilities` ["MATCH-BIPARTITE-MAX"]；boundary 修正为："阶段 0/1、compatible、F-594 交错圈、缺失/多余推导循环、护栏、checkStableOrder 未修改（哈希复核）；阶段 2 为重写——物化＋接缝调用＋matchR 反推；多解见证同版本确定性保持（跨实现见证可不同，契约允许）"。
- `src/SetCheckMatch.cpp`：mode "business"→**"adapter"**＋`capabilities` ["MATCH-BIPARTITE-MAX"]（本 TU 即 Boost 适配本体）。
- SetCheckTest.cpp（business）、CMakeLists.txt/白名单（governance）：维持。
- machine-check 前提：catalog 条目（a）先行落地，否则 mode=adapter 触发 CAPABILITY 失败；落地后复跑一次 validate-reuse 确认（本包不重复运行）。

**(d) 无需变更**：permissions.json（权限面无变化）；inventory（产品基线分类固定）；tasks/REUSE-T19.json（allowedFiles 已覆盖全部涉及文件）；testkit.md §3.1/§9（§7 已修订到位）。

### 8.4 门禁结果与 PASS 实际边界

协调批 `354b9bebf` 后 ird_gates 复跑 **exit 0**（`ird-gates-setcheck-run2-final.log`）：红线 11 类零命中＋validate-reuse 全量通过。**PASS 实际边界（精确表述）**：末行自述 "validate-reuse: PASS governance only; inventory=1401 units=20 changed=69 DEBT=16 UNRESOLVED=1399 (not algorithm compliance)"——即①红线/词表/补丁门禁机检通过；②复用校验的 schema/权限/函数绑定/凭证一致性机检通过；③**不构成算法合规或生产验收**（脚本明示 "not algorithm compliance"）；
④**UNRESOLVED=1399 为全库待审行既有总量、日志内 REUSE-AD-PENDING 条目属其他单元既有债务**，均非本包引入或本包可清偿。本轮末次实跑 `ird-gates-setcheck-run3.log`：exit 0，PASS 边界同 run2（governance-only；REUSE-AD-PENDING 为他单元既有债务）；§8.3 修正落地前不重复运行机检。

### 8.5 补正轮最终 SHA 与未完成

- 最终 SHA：本轮唯一提交（父＝354b9bebf，提交时回填 git log；本记录内不自引用 SHA）；本轮改动＝本记录＋`adapter-evidence/`＋`perf-compare/` 证据目录；产品源码/构建脚本零变化。
- 未完成：①§8.3 登记修正待协调落地（落地后一次 validate-reuse 确认）；②§8.2.4 稠密适配提案待裁决；③**独立验收未完成**——由所有者另开独立会话执行（建议范围：断开实验复现、对照复核、oracle/F-594 回归、登记与 audit-contract §4/§6 一致性核对）；实施者不自签。
