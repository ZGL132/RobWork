# GOV-T21 独立验收 r1 FAIL 返工登记（2026-10-24）

对应验收记录：[GOV-T21-20261024-r1.json](../GOV-T21-20261024-r1.json)（verdict **fail**，4 issues）。
任务契约：[GOV-T21](../../../tasks/GOV-T21.json)（status 保持 `ready`，待所有者重新送验；本登记不代替独立验收，独验仍由所有者另会话执行）。
返工基线：redesign-main `6921197dd`（REUSE-T29 之后、本返工提交之前）。

## Issue 1＋4（合并处置）：送验范围收窄为治理提交链，显式提交清单取代 grep 推导

### 根因（两 issue 同源）

- **范围推导缺陷（issue 4）**：原送验范围由 `git log --grep GOV-T21` 首末推导为
  `64ef0fd07..0032c8e78`。但 GOV-T21 核心首实施提交 `64ef0fd07`（2026-10-22
  『建立上游复用文档约束与可执行门禁』，正文与 acceptance 第 1/2/3 条逐条对应，
  原始证据在 `traceability/builds/gov-t21`）正文不含『GOV-T21』字符串，被 grep
  排除后反而成了送验 base——其本体变更不在 diff 检查面内。
- **范围混入（issue 1）**：主线在该区间中段的必经链为
  `c27349a80 → 8e4a08ac8 → eae9d213e → … → 0032c8e78`，其中
  `8e4a08ac8`＝REUSE-T07 能力包实施（单提交同时携带
  modeling/src/CouplingMath.hpp、modeling/test/CouplingReuseTest.cpp、
  modeling/CMakeLists.txt 三个产品文件及其自身契约/证据
  `traceability/reuse/REUSE-T07/**`），任何**单一连续** `base..head` 范围都无法
  同时覆盖 GOV-T21 全链并剔除它，故原送验范围必然越界（verify-task-scope 3 路径
  违规，见 [01-verify-task-scope.log](../GOV-T21-20261024-r1/01-verify-task-scope.log)）。

### 处置：显式任务包提交清单＋两段式范围（不放宽 allowedFiles/forbiddenFiles）

**GOV-T21 显式提交清单**（登记步骤改用本清单，不再用 grep 首末推导）：

| 提交 | 日期 | 标题 | 角色 |
| --- | --- | --- | --- |
| `64ef0fd07` | 2026-10-22 15:28 | [governance] 建立上游复用文档约束与可执行门禁 | **首实施**（acceptance 1/2/3 主体，原 grep 缺陷漏项） |
| `6eab38df8` | 2026-10-22 16:11 | [governance] 修复干净检出自测与源码后缀门禁遗漏 | 正文提及 GOV-T21 |
| `25ecf7f6d` | 2026-10-22 18:03 | [docs] 补齐逐功能复用审计与上游架构权限约束 | 正文提及 GOV-T21 |
| `0032c8e78` | 2026-10-23 10:14 | [governance] 去重验收前置并限定能力包增量验证 | 送验 head（原 grep 末位） |

**两段式送验范围**（verify-task-scope 逐段执行，合计即收窄后的 GOV-T21 治理
提交链，`8e4a08ac8` 单提交剔除）：

- 段 A：`-Base 64ef0fd07 -Head c27349a80`（含首实施 `64ef0fd07` 本体，止于
  REUSE-T07 提交之父）→ 实测 **PASS，6378 路径全部在允许面内**
  （[09-verify-task-scope-segmentA.log](09-verify-task-scope-segmentA.log)）
- 段 B：`-Base 8e4a08ac8 -Head 0032c8e78`（REUSE-T07 之后至送验 head）→ 实测
  **PASS，362 路径全部在允许面内**
  （[10-verify-task-scope-segmentB.log](10-verify-task-scope-segmentB.log)）

佐证（本会话 git 实测）：

- `git diff --name-only 64ef0fd07..c27349a80 -- '…industrialrobot/**'`（剔
  AGENTS.md）＝空；`git diff --name-only 8e4a08ac8..0032c8e78 -- 同范围`＝空——
  产品文件变更**全部**收敛于 `8e4a08ac8` 单提交。
- `3a61e300f`（『合入主会话reuse-t01增量（T07实施+…）』）虽再次携带同三个产品
  文件，但其内容与 `8e4a08ac8` 一致，对 `8e4a08ac8..0032c8e78` 净 diff 为零
  （段 B 路径清单中不含任何产品文件）。

**REUSE-T07 出路**：按其自身契约 [tasks/REUSE-T07.json](../../../tasks/REUSE-T07.json)
（status `ready`、branch `reuse-t01`，allowedFiles 含上述三个产品文件与
`traceability/reuse/REUSE-T07/**`）单独送验独立验收；其提交正文自认『待完整能力
独立验收，不合入』，本返工不代其送验、不关闭其债务。GOV-T21 的
allowedFiles/forbiddenFiles **未改动**（零产品算法变更约束维持）。

## Issue 2：commit-lint 两个跨会话合入提交，按 F-012 口径登记豁免

`717594e5c`『合入覆盖清查会话R1/R2成果（codex/coverage-sweep）』、
`3a61e300f`『合入主会话reuse-t01增量（T07实施+T08旋转提案）用于R2增量核对』
（均 2026-10-23，2/88 无合法中文前缀，且合并动作与 AGENTS §6.2 主工作树串行、
不自行合并其他分支的口径冲突）。二者为**自定义中文标题的 merge 提交**，不匹配
commit-lint.ps1 的 `^Merge ` 前缀豁免，故漏过 F-012 止增门禁。

处置（历史不可改写，AGENTS §6.3 红线）：已在
[traceability/findings.json](../../findings.json) 登记 **F-970**——豁免依据
（`717594e5c` 正文自证『所有者指示合入』且全程只读调查；两提交内容均已经主线
后续验收链消化）、责任归属（合入操作执行会话绕过串行与 commit-lint 自检；
commit-lint 豁免面未覆盖自定义标题 merge 属门禁盲区，仅登记不改脚本）、杜绝
再现（2026-10-22 精简治理起主工作树串行实施，增量一律以主线新提交进入，不再
跨会话/跨分支合入；新提交标题过白名单自检）。本返工新增提交即按 `[governance]`
前缀自检。

## Issue 3：证据索引重建＋validate-docs -DeepEvidence 复跑

r1 时点索引停留于 `6eab38df8`（16525 条）而 traceability 实际 23147 文件
（当前树 24167），`-DeepEvidence` 必然 FAIL，且实施方未按『已知阻断引用原始
记录并明确责任』口径登记。

处置（走『重建取得 PASS 原件』路线，不留阻断）：

1. `git add` 验收 r1 原件（`GOV-T21-20261024-r1.json`＋`GOV-T21-20261024-r1/`
   8 份 log，按该记录 executionNotes『由登记步骤统一 git add』的托付）与本
   返工全部产物；
2. 重跑 `index-evidence.ps1` 重建 `traceability/evidence-index.json`——首轮
   重建原件 [12-index-evidence-rework.log](12-index-evidence-rework.log)
   （16525 → 24179 文件，索引口径＝git ls-files 跟踪面；首轮时 12 号 log 自身
   尚未跟踪故未入索引，属机制内递归，按脚本提示 git add 后再重建）；
3. 复跑 `validate-docs.ps1 -DeepEvidence` → **PASS 原件**
   [11-validate-docs-deep-rework.log](11-validate-docs-deep-rework.log)
   （6 条 r1 自产 untracked 报错随 git add 消除；7644 条 missing/mismatch 随
   重建消除）。11/12 号证据文件自身按『git add → 重跑 index-evidence』口径
   递归纳入，终态条目数以 `traceability/evidence-index.json` 实际内容为准；
   提交前最终复跑 `-DeepEvidence` 以 exit 0 确认稳态（不留新文件）。

## 本返工的验证（提交后自检三门禁，供所有者复核）

```
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 6921197dd..HEAD
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/GOV-T21.json
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/GOV-T21.json -Base 6921197dd -Head HEAD
```

（返工包范围＝`6921197dd..HEAD`：验收原件入仓、本登记目录、findings F-970、
evidence-index.json 重建——全部落在契约 allowedFiles `RobWork/doc/industrial-robot-design/**`
内；GOV-T21 送验范围按上文两段式口径由下一轮独立验收执行。）
