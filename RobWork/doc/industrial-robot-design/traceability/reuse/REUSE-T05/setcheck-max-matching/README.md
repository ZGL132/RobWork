# testkit SetCheck 最大匹配能力复用——任务记录

- 分支 `codex/reuse-testkit-setcheck`（基线 `691cbd42e`＝redesign-main 顶端，F-958 已合入；旧分支 codex/reuse-testkit-listener 与 build-int-lsn 保留）
- 任务编号：**由协调者统一登记**（指令明确不复用 REUSE-T18——该编号已用于 F-958 收口，见 tasks/REUSE-T18.json）
- 状态四态：实施完成 ✅／定向与消费者回归通过 ✅／门禁 ❌ 待登记转绿（见 §6）／独立验收 ❌ 未开始（所有者另开会话，不自签）

## 1. 复用来源（上游检索与 Boost 候选分开陈述）

**上游 RobWork/RobWorkSim**：本包独立检索 `bipartite|maximum_cardinality|max_cardinality|hopcroft|augmenting path` 于 `RobWork/src`＋`RobWorkSim/src` 零命中；宽松 `matching` 命中均为任务增广/加载器/插件仓库命名，无公共二分图最大匹配 API。与审计 B17 结论一致（"未找到已读公共二分图最大匹配 API，但零命中不证明所有候选无能力"）。

**Boost 候选**：同一 vcpkg 渠道**已装** Boost 1.92（BOOST_LIB_VERSION 1_92；`boost/graph/max_cardinality_matching.hpp`＋`share/boost_headers/` 在位）——零 vcpkg 变更、零新渠道。消费的两个公开 API：
- `boost::checked_edmonds_maximum_cardinality_matching`（算法内部自验配对合法且基数最大）；
- `boost::matching_size`（计数正交源，与转表计数互检）。

既有证据（**仅为候选可运行证明，非生产准入**）：probe-r17/candidates 2 探测 passed（贪心陷阱唯一完美匹配＋删边区分替代配对）、MatchingSuitabilityTest.cpp（Boost::headers 消费形态）。本包补齐的正是"候选→生产接入"缺环。

## 2. 适配判断与边界

**语义等价**：两算法同为最大基数匹配——等价结论、缺失/多余**计数**逐 case 相同；多解情形的**具体配对见证**可不同（同输入恒定：adjacency_list vecS 构造序＋无随机源＝确定性，§5.4.2"同输入同报告"）。消费者面核查（12 文件）：全部只断言等价/计数/空性，无配对索引钉 → 零破坏面。§5.4.2 契约明示算法自由（"Hopcroft–Karp 为复杂度期望，非算法强制"；v0.6 实现取舍注）。

**业务层全部保留**（替换面仅匹配内核）：阶段 0 重复检测、阶段 1 身份优先匹配、`detail::compatible`（C4 经 profile.resolve＋core::closeWithin）、F-594 交错圈歧义检测（对任意最大匹配成立）、mismatchedPairs 最近对详情、100,000 护栏、checkStableOrder——逐字未动。

**显式图边界**（审计已命名）：物化 |E| 相容边——求值 O(nL·nR) 与原谓词按需扫描同阶，新增 |E| 边存储；护栏极稠密情形内存退化，**不承诺内存/复杂度收益**（审计口径原样继承）。收益＝删除自写递归内核＋库侧最大性自验，非性能。

## 3. 实际删除/新增内容

删除：`detail::tryAugmentAt`（Kuhn 递归增广/回滚，34 行）＋其驱动循环与 visited 分配（约 10 行）——**无双实现、无 fallback**。
新增：
- `SetCheck.hpp`：`detail::maximumBipartiteMatching(nL, nR, edges)` 接缝声明（纯索引类型，Boost 不出公共头）；阶段 2 改为相容边物化（业务语义留模板层）→ 接缝调用 → mate 反推 matchR（下游 F-594/缺失多余推导原样）；实现取舍注更新。
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

性能口径：套件墙钟 19.2s 量级与替换前持平（115→119 用例 ±噪声；15625 次生产入口调用含于新用例）；护栏语义不变（>100k Usage 由既有用例钉住）；护栏极稠密情形的物化内存取舍如实登记（§2），未做病态规模计时——两实现该形态均不可用，非本包引入。

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
3. 独立验收未开始（所有者另开会话；本记录不构成自签）。
4. 范围声明：本包仅覆盖 SetCheck 阶段 2 匹配内核一项复用，**不得扩大为整个 testkit 迁移完成**（JSON 解析/摘要等审计遗留候选仍各自待核）。

## 7. 协调落地（2026-10-23，共享治理唯一修改者）

**任务登记**：精确能力子任务 **REUSE-T19**（`tasks/REUSE-T19.json` ready＋`reuse/changes/REUSE-T19.json` 5 文件凭证——§5 四产品文件＋白名单）。§5 原拟 4 文件未含白名单：本批 `IRD_THIRDPARTY_ALLOWED_TARGETS` 增列 `Boost::headers`（＋注释 4 行）即白名单产品面改动，须凭证（ird_gates 首跑 LIB 命中的登记本体）。catalog/permissions 确认无真实变化未改。

**依赖契约**：testkit.md §3.1 行按 §5 建议修订（header-only 编译期使用例外）＋§9 v1.7 版本行；DTB §4.5 增 LIB 行（Eigen3 先例；ird-gates-setcheck.log 首跑恰 1 项 IRD-GATE-LIB 命中留证）。

**functions/testkit.json**：按 §5 处置表逐项落地——tryAugmentAt 删除；maximumBipartiteMatching 新增（SetCheck.hpp 134-136＋SetCheckMatch.cpp 1-88 双源）；compatible 105-122（哈希复算与原值逐字一致）；checkSetEquivalent 147-380／checkStableOrder 382-423／MatchPair＋Traits ＋6 平移全部复算吻合；§5.3 机械 pin 批量刷新（CMakeLists bdf5362e→新值 100 处、SetCheck.hpp 316a6239→新值 31 处、SetCheckTest.cpp f5fde51c→新值 7 处）。COVERAGE 规则（§5 未预见的机器面）：被改四文件须全行矩阵覆盖——SetCheck.hpp 补 6 段、SetCheckMatch 由 maximumBipartiteMatching 全文件源承载、SetCheckTest 按用例块 9 段、CMakeLists 7 行区间/哈希重排；被改文件覆盖行按 F-958／diagnostics 先例登记 `migrated＋business-specific＋NONE＋nonOverlapEvidence`（绑定 setcheck-run-release/gtest.xml，非closed）。foundation-governance 白名单 10 行区间/哈希随本批白名单增列重算。

**哈希口径**：§5 的 4 文件 sha256 为原始字节口径（7b983374/334f1197/72999190 与 raw sha256sum 逐字吻合）；登记值按校验器 Digest 规则（CRLF→LF 归一）重算——SetCheck.hpp c882aeda…、SetCheckMatch.cpp 4cf78053…（两口径同值）、CMakeLists.txt ae543743…、SetCheckTest.cpp 93733df4…。区间哈希（b97ff334/1fc53e6b/c2bf1998 等）经本批复算全部吻合。

**门禁终态**：`cmake --build build-int-lsn --config Release --target ird_gates` 复跑 **exit 0**——红线 11 类零命中（首跑 IRD-GATE-LIB 消解）＋validate-reuse 全量 PASS（inventory=1401、changed=69），日志 `ird-gates-setcheck-run2-final.log`。既有定向/消费者回归证据（§4 表）按增量口径原样引用（本批治理登记零产品代码变化）。F-958 套件与消费者面未重跑。
