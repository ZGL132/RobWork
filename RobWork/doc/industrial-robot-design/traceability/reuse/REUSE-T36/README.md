# REUSE-T36／WP-13：policy 名称规则 F-961 修正候选（blocked）与随机/LHS 候选独立审查原始记录

实施日期：2026-10-25；分支 redesign-main；起始 SHA `0e14bf3e79e46a830ca1b882e4a67ecddf4335e7`。
任务契约：[REUSE-T36](../../../tasks/REUSE-T36.json)。**状态＝blocked（所有者 2026-10-25 escalate 裁决，采 T33 blocked 先例：产品树保持 HEAD 零改动，修复候选留存本目录待 policy 单元迁移包批准后落地）**；随机/LHS 审查部分按批准范围正常交付。

## 1. F-961 修复候选（已验证，未落产品）

### 证据链与准入

- findings.json `F-961`（major/open，owner policy/REUSE-T05）：policy 会话把合法 REGEX 前缀完整名直传上游模式 API，产出排除规则无法匹配原对象名。
- [REUSE-T01/policy-boundary-audit.md §F-961](../REUSE-T01/policy-boundary-audit.md)：runtime 合法字符集 `[A-Za-z0-9_.-]`（`runtime/src/NameMap.cpp` `isLegalNameChar`）允许 `REGEXarm`；上游 `convertToRegEx` 对 size>5 且前 5 字节为 REGEX 的串去前缀直取正则。
- B18 独验（[REUSE-T01-batch18-20261023/acceptance.md](../../acceptance/REUSE-T01-batch18-20261023/acceptance.md)）：独立复现 1 passed/1 failed，F-961 保持 open；修复路线（既有模式接口最小字面适配）已在其 resolution 与审计获批。
- H1 核查（本包实读）：上游 `ProximitySetupRule.hpp:147-178` `makeExclude/makeInclude` 全部经构造函数走 `convertToRegEx`（`ProximitySetupRule.cpp:28-36`），**无字面名 API**——直接 API 不可用，按 resolution 采用薄适配单点转义（非新规则引擎、非上游扩展）。

### 候选内容（candidate/ 目录）

- `candidate/CollisionEvaluator.cpp`＋`candidate/CollisionEvaluator.f961.diff`：匿名命名空间新增 `escapeRegexLiteral`（ECMAScript 特殊字符全集转义 `\ ^ $ . * + ? ( ) [ ] { } |`——覆盖并补齐上游 `patternToRegEx`（`rw/core/StringUtil.cpp:283-298`）未覆盖的 `* + ? { }`；REGEX 分支绕过 patternToRegEx，转义必须自足；注入名不受 NameMap 合法字符集约束）与 `toUpstreamRulePattern` 唯一适配点（分支条件与上游 REGEX 判定逐字镜像 `size()>5 && substr(0,5)=="REGEX"`：命中传 `"REGEX"+字面转义原名`——上游去一个前缀后余串为纯字面正则、`regex_match` 全匹配原名；否则原样透传零扰动；恰 5 字节全名 `REGEX` 走上游字面分支故透传）；`buildPolicyProximitySetup` 调用点换经适配点并订正不实"完整名精确匹配"注释；名称真值保留（对象身份/名称映射不变）、不改上游、无接口/CMake 变化。
- `candidate/CollisionSessionTest.cpp`＋`candidate/CollisionSessionTest.f961.diff`：F961 三用例（REGEX 前缀名生产会话 EXCLUDE 规则 match 原名＋对称＋全匹配＋存储 pattern 字面断言；普通点号名透传零扰动＋近名不匹配；恰 5 字节 REGEX 上游字面分支与 6 字节起前缀解释边界钉）。

### 红转绿 oracle（全部真实执行于本会话）

- `f961-red.log`：未修复生产码（仅测试在位）跑 `--gtest_filter='CollisionSessionNameRules.*:CollisionSessionUpstreamSemantics.*'` → **2 passed / 1 failed（exit 1）**，失败＝`RegexPrefixedNamesExcludeRuleMatchesLiteral_F961`（match 原名 false、存储 pattern 未适配）——与 B18 反例同型。
- `f961-green.log`：候选修复后同 filter → **3 passed（exit 0）**。
- 候选构建全量：`test.log`（run-tests.ps1 ctest 通过）＋`sdurws_ird_policy_test.xml`（`tests="214" failures="0"`，含 3 个 F961 用例）＋`ird-test-report.json`（total 214/passed 214/decisive true）——211 基线＋3 新用例。
- 影响边界（B18 同源，不得夸大）：`evaluate` 按 scope 逐对直查后端、不消费 setup 过滤——候选仅实证公开 setup 规则输出修复，**不声称碰撞误判修复**；F-961 保持 open。

### blocked 原因与解阻路径

修复必须改 `policy/src/CollisionEvaluator.cpp`，而复用门禁对累计改动集产品文件要求全文件解决（`reuse-contract.ps1:258,271-279`、`validate-reuse.ps1:193-199`：`$changed`＝治理基线以来累计集；改该文件即要求其 18 个功能行全部 migrated/closed＋逐行 POSITIVE 生产证据＋permissions.json 精确 grant＋新增 helper 行纳入区段覆盖，且 migrated+direct/adapter 需 catalog COLLISION capability `review.status=verified`——现为 candidate，能力升级本身属独立验收事项）。这等于把 T05-policy 迁移父任务整体提前，远超 migration §13.3 WP-13 批准边界（§13.4"计划登记不自动批准扩展"）。**所有者 2026-10-25 escalate 裁决：采 T33 blocked 先例**——产品树保持 HEAD 零改动，候选留存本目录；解阻路径＝所有者批准 policy 单元迁移包时按 `candidate/*.diff` 落地（成品代码+测试+红转绿证据齐备，落地成本＝应用 diff＋门禁全文件解决批）。

## 2. 随机/LHS 候选独立审查（批准范围交付，零生产改动）

检索命令与完整输出见 [random-lhs-search.md](random-lhs-search.md)。结论（矩阵维持 pending，实施归 WP-19/后续精确子任务）：

- `rw::math::Random`（`rw/math/Random.hpp:35-87`、`Random.cpp:31-80`）：进程级全局 `boost::mt19937` 单例；`seed(unsigned)` 入参即 32 位截断（`Random.cpp:53`），时间种子 `TimerUtil::currentTimeMs`（:58）；静态类无实例流。与产品契约冲突：①§3.4 确定性总约定要求任务私有确定性序列——全局态被任意消费方 reseed 污染；②审查 A06 缺陷形态正是"seed 高 32 位不敏感"——`seed(unsigned)` 天然只有 32 位种子空间，`kinematics.streamSeedFor` 的 64 位 avalanche 派生不可达；③多消费流（随机初值/采样/包络探针）需独立流，全局单流互耦；④无 LHS/分层能力。
- `rw::pathplanning::QSampler`（`QSampler.cpp:104`）：`Math::ranQ` 均匀采样，无种子参数、无分层——均匀 QSampler 与 LHS 分层正交不等价（矩阵既有结论复核成立）；rw 全库无 LHS/拉丁超立方/分层采样实现（S4r 词边界检索零有效命中——`XercesUtils.cpp:164` 的 `chLatin_L` 为 Xerces 字符常量假阳性）。
- RobWorkSim sandbox：`src/CMakeLists.txt` 无 sandbox 注册（目标默认不建）；`src/sandbox/` 实际仅 NewtonEulerDynamics＋grippers，无优化/LHS 目标——与 required-proposals.md 登记（sandbox 优化目标默认关闭待实测）一致。
- 保留理由：splitmix64/streamSeedFor（任务私有 64 位流）与 SeededLhs（Fisher-Yates 层序置换＋层内均匀）承载种子分布/线程隔离/确定性/候选身份，上游无等价入口；矩阵 MATH-RANDOM 行维持 pending，不把"均匀采样存在"混同"LHS 等价"。

## 3. 跨批治理卫生修复（顺带，已登记）

`REUSE-T33/production-rpy.json` 对 `final.log` 的绑定 `sha256=681754d3…` 为 T33 会话内 final.log 重生成（480 项回归最终版，01b770ae6 落库）前的陈旧值（治理 Digest/无尾剔除/原始字节三种口径实测均不等于该值）且 0e14bf3e7 清理提交漏扫该条，致 HEAD 上 validate-docs 全量复用检查 `REUSE-POSITIVE` 必红——属与本包无关的既有阻断，但为本包必需门禁的前置。经所有者裁决确认，按 0e14bf3e7 同类 hygiene 先例将该绑定单值同步为 tip 实际文件摘要 `f38b31ff…`（final.log 内容零改动），并级联同步 `modeling.json` 与 `REUSE-T33/verification.json` 两处对 production-rpy.json 的引用绑定（622e67eb…→7db05648…）。

## 4. 证据清单

| 证据 | 内容 |
| --- | --- |
| candidate/CollisionEvaluator.cpp（+diff） | F-961 修复候选成品 |
| candidate/CollisionSessionTest.cpp（+diff） | F961 三用例候选 |
| f961-red.log / f961-green.log | 未修复红（1 failed）→修复绿（3/3） |
| test.log / sdurws_ird_policy_test.xml / ird-test-report.json | 候选构建全量 214/214 |
| random-lhs-search.md | S1-S8 检索命令与输出 |

独验/后续：blocked 状态由所有者裁决解阻；候选落地随 policy 单元迁移包另立精确任务；随机/LHS 行维持 pending。
