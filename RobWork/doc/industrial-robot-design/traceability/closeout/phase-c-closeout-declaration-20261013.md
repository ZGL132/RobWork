# 阶段 C（M5）收口声明

| 字段 | 值 |
| --- | --- |
| 声明时间 | 2026-10-13 |
| 声明基线 | `redesign-main` @ `9366e470a3509d25588247ba9cb6339fb64778cd`（本会话 `git rev-parse HEAD` 实测；分支 `git branch --show-current` ＝ `redesign-main`） |
| 声明性质 | M5（阶段 C 业务链）正式收口声明——按 DTB §1.3 里程碑表 M5 行字面判据逐项亲手复核后作出；授权依据＝所有者 2026-10-13「推进 M5 正式收口」指令（自动化会话；合入口径照 automation-pipeline.md 与库内 Merge 提交惯例） |
| 复核方式 | ①契约 status 逐族 grep 计数（`tasks/foundation/` 内 `grep -l '"status": "done"' <族>*.json \| wc -l`；抽样契约另验 status 字段全文唯一）；②主树验收记录在位＋verdict 行亲读（本日新记录六份全量亲读，五族各抽最新一份）；③本日基准链合入关系实测（`git log --merges --since 2026-10-13` ＋ `git rev-parse` 分支头全 SHA）。全部证据产生于本会话亲手执行的命令输出与亲读文件 |
| 未执行项声明 | 本声明未重新执行集成构建与测试——「全绿」依据＝契约 status 账面＋验收记录 verdict＋合入关系实测（与 M4 收口声明同口径）；各卡构建/测试的真实执行证据以各验收记录在案留痕为准（本日六份记录均含验收者亲跑命令清单与逐项复跑结论） |

---

## 一、判据原文

M5 权威定义（`development-task-breakdown.md:138`，§1 里程碑表）：

> **M5 阶段 C 业务链** | WP-16/17/18/19＋WP-12(C)＋WP-23-T02~T05 全绿（AT-06/07/08/30/32 可验）

单元归属映射（DTB §2 实测）：WP-16＝trajectory（§2.17，:473；AT-06）、WP-17＝dynamics（§2.18，:494；AT-07）、WP-18＝drivetrain（§2.19，:509；DYN-04 唯一映射）、WP-19＝selection（§2.20，:519；AT-08/30）。AT-30＝选型应用与回填（SEL-10，:531）、AT-32＝完整工程报告（RPT-01-C，:533）。WP-12(C)＝reporting C 级（RPT 家族；C 级承载卡实测＝RPT-T15「C 级扩展与变体报告（阶段 C：C 章节/签署元数据/variantDiff）」＋RPT-T16「报告往返复算（roundtripCheck，阶段 C）」；本声明按派发口径以 RPT 全族 16 份为账面，B 级承载卡 RPT-T14 已随 M4 收口声明复核）。

## 二、判据逐项达成表

账面统计命令（本会话亲跑，`tasks/foundation/` 目录内）：
`grep -l '"status": "done"' <族前缀>*.json | wc -l`（逐族）；抽样契约（WP-16-T02／WP-17-T05／WP-18-T03／WP-19-T08／RPT-T09／WP-23-T02~T05）另以 `grep -c '"status"'` 验证全文 status 字段唯一（9/9 均 1 次），排除嵌套字段误计。

| # | 判据项 | 账面统计（本会话实测） | 抽查验收记录（主树 `traceability/acceptance/` 在位确认） | 结论 |
| --- | --- | --- | --- | --- |
| 1 | WP-16（trajectory，15 份 T02~T16） | **done 15/15** | `freeze-p06-20261011.md:7`（承载契约 WP-16-T02——P-06 数值冻结批）verdict **pass**（无阻断级；建议级 2 条）；`wp-16-t16-20261011.md:12` verdict **pass**（无阻断级；建议级观察 2 条） | **达成** |
| 2 | WP-17（dynamics，9 份 T02~T10） | **done 9/9** | `wp-17-t10-20261009.md:10` verdict **pass**（无阻断级问题；3 项建议级） | **达成** |
| 3 | WP-18（drivetrain，4 份 T02~T05） | **done 4/4** | `wp-18-t05-20261011.md:12` verdict **pass**（无阻断级问题；建议级 3 项） | **达成** |
| 4 | WP-19（selection，11 份 T02~T12） | **done 11/11** | `wp-19-t12-20261011.md:9` verdict **pass**（验收清单 1~8 全过＋协议 4.5 变异红复现；建议级 2 条） | **达成** |
| 5 | WP-12(C)（reporting，RPT 16 份 T01~T16；C 级承载 RPT-T15/T16） | **done 16/16**（与 M4 收口声明时点账面一致，零回退） | `rpt-t16-20261012.md:6` verdict **pass**（验收清单 1~8 全过、无阻断级问题；建议级 G-1/G-2） | **达成** |
| 6 | WP-23-T02~T05（4 份契约；性能规模化阶段 C 达标面） | **4/4 done**（逐卡单查 `"status": "done"` 实测） | 本日新记录四份全量亲读：`WP-23-T02-20261013.md:11` verdict **pass**（阻断 0／建议 6）、`WP-23-T03-20261013.md:9` verdict **PASS**（4.1~4.11 全过；阻断 0／建议 3）、`WP-23-T04-20261013.md:9` verdict **PASS**（4.1~4.11 全过；阻断 0／建议 4）、`WP-23-T05-20261013.md:10` verdict **PASS**（4.1~4.11 全过零阻断；建议级 4 条） | **达成** |

**小结：M5 六项字面判据全部达成。**

## 三、本日基准链合入清单（各卡 SHA，git log/rev-parse 实测）

本日执行序＝T01（协议文档）→ T09（B-08 确定性复现）→ T02（B-01 交互响应）→ T03（B-02 取消时序）→ T04（B-03 大规模数据）→ T05（B-04 项目打开）；每卡两提交（merge＋governance 合入收尾），时间戳为 commit date 实测：

| 卡 | 基准/承载 | 分支头（`git rev-parse` 全 SHA 实测） | merge 提交（入 redesign-main） | 合入收尾提交 | 验收记录（主树） | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| WP-23-T01 | performance-baseline.md v0.1（基准清单 B-01~B-08＋黄金项目定义＋PM-16 测量协议） | `07ab4e6841f360e8696d024057af3c19b490f879` | `18842bc0b238957439e55f9b1146480f3080e177`（05:48） | `25abce7482d65f0e6b864e786583e1e521eeaeb4`（05:52） | `WP-23-T01-20261013.md:11` | **pass**（attempt 1，零阻断；建议级 3 项 S-1~S-3＋观察 4 项） |
| WP-23-T09 | B-08 确定性复现基准（NFR-COR-02） | `41c8b304fe2ff738647b31359317e7512c101a00` | `81bdc4e7f836d4e734b958ddd32eb1afcc81d7cb`（08:31） | `1499b71db3f32b928f8feca4298ae6b9410822cb`（08:38） | `WP-23-T09-20261013.md:10` | **pass**（attempt 2 复验，零阻断；建议级 S-1~S-6＋观察 O-4） |
| WP-23-T02 | B-01 交互响应（NFR-PERF-01） | `5170b4d3a0dc11a71f09597953640217e6589099` | `d7f19f3f16bf23da1e5243e6e6d4caa80cd0c641`（10:37） | `a76fd7b8f9cd1e91a63d10f7923583b2f87fd810`（10:44） | `WP-23-T02-20261013.md:11` | **pass**（attempt 1，阻断 0／建议 6） |
| WP-23-T03 | B-02 取消时序（NFR-PERF-02） | `55c22fe9c2d36fafa869ca7b9ddec452996f17ac` | `025c8607e04716f3fe7b96fe8f1fb5648a0ad90a`（12:48） | `24ee6aad2eeeaedcb2608ba440f014097a30297d`（12:52） | `WP-23-T03-20261013.md:9` | **PASS**（attempt 1，阻断 0／建议 3） |
| WP-23-T04 | B-03 大规模数据加载与分页（NFR-PERF-03） | `1690a7176eae3bc578648e2596af2a922b556f73` | `934ff5f8a1888649f9b9391330821f62c3b3432d`（14:30） | `020cb14a905f9f9505009840ab922b17f978e854`（14:34） | `WP-23-T04-20261013.md:9` | **PASS**（attempt 1，阻断 0／建议 4） |
| WP-23-T05 | B-04 黄金项目打开（PM-16） | `5ce747125917a915ceae06334038f9c62355dc5f` | `6d5ac8496ad549a3f883bdd86cf68e1d1f22ce25`（16:36） | `9366e470a3509d25588247ba9cb6339fb64778cd`（16:41） | `WP-23-T05-20261013.md:10` | **PASS**（attempt 1，零阻断／建议 4） |

各卡合入收尾提交改动面经 `git show --stat` 逐一实测＝契约 JSON（ready→done）＋验收记录落库两文件；T01 收尾另含 DTB 版本行补登（S-2 兑现，三文件）。

## 四、测量口径与协议出处

- **协议文件**＝`RobWork/doc/industrial-robot-design/performance-baseline.md`（DTB §2.24 WP-23-T01 登记产物，文档代号 performance-baseline）。版本链（变更记录表逐行亲读）：v0.1（2026-10-13，T01 初版——基准清单 B-01~B-08＋黄金项目定义〔PM-16 口径 1,000 对象/100 修订〕＋测量协议〔冷/热分组、每组有效样本 ≥15、P95 线性插值、冷启动软指标单独报告不作门禁〕）→ v0.2（T09 B-08 复现命令位回填）→ v0.3（T02 B-01 回填）→ v0.4（T03 B-02 回填）→ v0.5（T04 B-03 回填）→ v0.6（T05 B-04 回填＋黄金项目生成器明细参数冻结）；v0.2~v0.6 各版变更说明均明文「**零协议数值变更**——达标口径照抄需求原文」。
- **权威边界**（协议文首原文亲读）：需求语义唯一权威＝`REQUIREMENTS.md` §19.3 NFR-PERF-01~06、§17 PM-16、§19.1 NFR-COR-02、§25 附录 D；performance-baseline 只做协议登记与执行挂点组织，**不重定义、不收窄、不扩大任何需求语义**——达标数值一律照抄需求原文，与需求冲突时以需求为准。
- **本日达标面**：B-01（T02——五操作 P95 ≤200 ms／无 >2 s 无响应／>1 s 转后台）、B-02（T03——2 s 内入 Canceling＋10 s 内收敛＋超时强杀腿）、B-03（T04——5,000 任务点/100,000 采样结果/10,000 候选摘要＋不整体装入界面）、B-04（T05——热启动门禁组 P95 158.48 ms ≤3,000 ms〔验收记录亲跑 `_acc-evidence/perf-reports/b04-warm-open.json`，gitCommit=5ce74712 锚〕；冷启动软指标 P95 149.56 ms 只登记不作门禁）、B-08（T09——五域确定性复现：集合等价＋稳定排序＋1e-12 归约容差）。测量证据落位＝`traceability/benchmarks/wp23-t0{2,3,4,5,9}-*-baseline.md` 首测报告＋`testdata/performance/` 数据集 manifest。
- **未达标面（不属 M5 字面判据）**：B-05~B-07（承接卡 WP-23-T06/T07/T08，账面 status＝planned 实测）——属 M6「WP-23 R2」面（DTB:139），不在本声明范围。

## 五、遗留面（如实登记；均不属 M5 六项字面判据）

1. **F-495（severity=blocking）维持 open**：需求三维区域采样四态着色闭环——六项诉求的交付载体均已合入基线（findings 注记载 UI-T64/UI-T65 两批 pass 合入＋FIX-T2 样本级定位交付），但**宿主 GUI 实拍复验动作归所有者**（自动化会话不代行复验），复验通过后翻 fixed。按治理红线，本批不动其 status 字段（本会话 grep 实测 `findings.json:5006` F-495 `"status": "open"` 维持原状）。
2. **F-833（severity=suggestion）维持 open**：测试套件同机并行执行不隔离观察项（platform-batch4 验收转登；隔离串行复跑 7/7 全绿、串行口径〔PIPE 既定〕无影响），登记观察、不阻塞任何在途任务（本会话 grep 实测 `findings.json:8119` F-833 `"status": "open"` 维持原状）。
3. **本日六份验收记录的建议级转登义务未清（如实登记）**：findings.json 最新条目实测仍为 F-833（`grep -o '"id": "F-[0-9]*"' | tail` 实测），`"registeredAt": "2026-10-13"` 计数实测为 **0**——六份记录的建议级项（T01 S-1~S-3＋O-1~O-4、T09 S-1~S-6、T02 建议 6 项、T03 建议 3 项、T04 S-1~S-4、T05 S-1~S-4）尚未转入 findings.json 条目；各合入收尾提交改动面经 `git show --stat` 实测仅契约＋验收记录，均未触 findings.json。转登动作归后续治理批次，不阻塞本收口（建议级按 ACC §5 本就不阻断合入）。
4. **本声明未重新执行构建与测试**（见文首未执行项声明）；B-04 集成模式复测挂点等在途事项以各首测报告登记为准。
5. **WP-23-T06/T07/T08（B-05~B-07）账面 planned**——属 M6（WP-23 R2）后续面，DTB M5 行字面判据不含。

## 六、声明结论

**M5（阶段 C 业务链）六项字面判据全部达成**（逐项证据见第二节）；本日基准链六卡（T01/T09/T02/T03/T04/T05）全部经对抗验收 pass 合入翻 done（各卡 SHA 见第三节）；测量口径与协议出处见第四节；遗留面如实登记于第五节。据此宣布：**M5 字面判据达成，阶段 C 业务链收口**——最终确认以文末所有者签署为准。

## 七、所有者确认

| 栏目 | 内容 |
| --- | --- |
| 确认结论 | （待所有者签署：确认 M5 收口 ／ 驳回并注明理由） |
| 确认人 | （留空待签） |
| 确认日期 | （留空待签） |
| 备注 | （留空） |

---

*证据方法声明：本声明全部证据产生于本会话内亲手执行的命令——契约逐族 grep 计数（`grep -l '"status": "done"' <族>*.json | wc -l`）＋抽样 status 字段唯一性验证（`grep -c`）、验收记录 ls 在位＋verdict 行亲读（`grep -n -i verdict` 定位，本日六份全量、五族各最新一份）、git 只读命令（rev-parse / branch --show-current / log --merges --since / show --stat / rev-parse <分支头>）、findings.json 只读 grep（F-495/F-833 status 与 registeredAt=2026-10-13 计数）。除本声明文件与 governance-log.md 登记行外未修改任何文件；findings.json 零触碰（F-495 status 红线遵守）。*
