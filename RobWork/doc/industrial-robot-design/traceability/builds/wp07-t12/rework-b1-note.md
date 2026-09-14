# POL-T12 返工说明（attempt 3 fail → 重新送验）：B-1 载体归位

| 字段 | 值 |
| --- | --- |
| 返工输入 | 验收记录 `traceability/acceptance/POL-T12-20260914.md`（分支 `acc/POL-T12/3` @ `67dfb62ef2f71501d4ddcb9d4f5fa7494e3e5116`，attempt 3，verdict=fail）——唯一阻断项 §4.1-B-1＝`governance-log.md` 不在契约 allowedFiles 任何模式内（findings F-094，severity=blocking；其余 4.2~4.11 全部通过且关键主张经验收者亲手复现属实） |
| 处置路径 | 验收记录问题清单 B-1 路径 b（拆分治理提交）的**保守变体**：治理改动另走 `[governance]` 前缀治理提交；任务分支以**追加还原提交**清出该文件（不重写已推送历史、不 force-push、不修改契约） |
| 所有者裁决状态 | 按返工流程向所有者发出 B-1 二选一裁决询问（路径 a 契约增补登记 vs 路径 b 拆分治理提交），**未获答复**；本处置为实施者权限内唯一可自执行路径，已在验收请求中显式标注为待所有者确认事项——合入段所有者可否决本处置或改判路径 a（改判代价见下文"可逆性"） |
| 治理落点 | 分支 `governance/pol-t12-registry` @ `94fe223c09dfd325a1bd52ab612769f1958a8081`（已推 `origin`，自 base 44595aa 单文件建分支，待维护者在合入段并入 `redesign-main`）——仅 `governance-log.md` 一个文件，其补丁与原任务分支 fca3fa97 中该文件改动经 `diff` 比对**逐行等同**（IDENTICAL-PATCH） |
| 任务分支处置 | 追加还原提交：`governance-log.md` 恢复至 base `44595aa` 内容；自此送验 diff（`44595aa..新 head`）`--name-status` 不含 `governance-log.md`，B-1 范围越界消解，以新 SHA 重新送验 |

## 为什么是"追加还原"而非字面"任务分支重写"

- 验收记录路径 b 原文为"任务分支重写后以新 SHA 重新送验"；但 AGENTS §6.3 红线禁止 rebase/amend 已推送提交、禁止 `--force` 强推覆盖远程历史。追加还原提交达成**同一验收事实**（送验 diff 净变更不含该文件、分支尖端新 SHA），且完全免于强推，故采用之。此为对路径 b 字面步骤的保守偏离，偏离理由与结果在本件留档，供验收者与所有者核查。

## 为什么不采用路径 a（契约增补登记）

- 路径 a 需按 DTB §5.4 增补契约 POL-T12 的 allowedFiles——契约修订超出实施者权限（返工纪律："不得私改契约"），必须由所有者授权或亲自执行。所有者询问未获答复，故不执行；若所有者后续改判路径 a，本还原提交不影响该改判（治理分支内容与契约增补可并存，验收记录自评根因即"契约 allowedFiles 编译遗漏"，两路径殊途同归）。

## acceptance 2 语义不丢失

- acceptance 2 的登记册侧落点动作（P-POL-6 行 stale→closed、P-EV-6 行按 governance-log §4 维护规则③以卡为准回改、P-POL-9 行备注更新、§4 汇总行、§5 一致性核对行、§6 v1.1 变更记录）**完整保留**于治理分支提交——机制依据即 governance-log.md §4 维护规则①明文："其任务提交**或紧随治理提交**同步翻转本表对应行"。治理提交通道是登记册翻转的既有合法机制，非本次发明。

## 复用既有验证留痕的依据（如实登记）

- 本次返工**零代码变更**（仅 .md/.json 文档与登记件；产品树、CMake、测试、脚本均未触碰）。attempt 3 验收记录 §4.2/§4.5/§4.6 已对送验对象 85a71bf 全量亲手复现：集成构建 0 error、ctest 2/2、exe 直跑 200/200＋23/23、冒烟 131/131＋8/8、双探针（golden fixture 篡改即红＋零 Qt 红线注入即红）真实有效、ird_gates 7 命中与 POL-T11 基线逐码一致零新增——上述证据对新 head 继续有效，不重复构建。
- 本返工**实际重跑**：契约 verify 命令 `validate-docs.ps1` 与自查 `validate-task.ps1`（文档有变更即须重跑）——日志见同目录 `rework-validate-docs.log`／`rework-validate-task.log`。
- 本返工**未重跑**：双模式构建与测试执行（零代码变更，无新验证义务；纯文档任务 DoD 第 1 条免除口径已在 `ird-test-report.json` taskKind 字段声明）。attempt 3 验收记录 4.10 已核实既有留痕全部在库且与复现一致。
